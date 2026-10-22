#include "gfx.h"

#include <windows.h>

#include "addr.h"
#include "cmd.h"
#include "camera.h"
#include "hooks.h"
#include "log.h"

namespace fdcore::gfx {
namespace {

using addr::Id;

// Genesis API (ABIs in addresses.json "ghidra"; alpha path: core/evidence/alpha_spike).
struct GeRect {
    int32_t left, right, top, bottom;  // GE_Rect, inclusive
};
struct TLVertex {
    float x, y, z, u, v, r, g, b, a;  // GE_TLVertex; colours 0..255
};
using CreateFn = void*(__fastcall*)(int width, int height, int maxMip, int format);
using LockFn = int(__fastcall*)(void* bmp, void** target, int minMip, int maxMip, int format);
using BmpFn = int(__fastcall*)(void* bmp);
using GetBitsFn = void*(__fastcall*)(void* bmp);
using GetInfoFn = int(__fastcall*)(void* bmp, uint32_t* info, uint32_t* info2);
using DestroyFn = void(__fastcall*)(void** bmp);
using DrawFn = int(__fastcall*)(void* engine, void* bmp, const GeRect* src, uint32_t x, uint32_t y);
using EngineBmpFn = int(__cdecl*)(void* engine, void* bmp);
using FlushFn = int(__cdecl*)(void* engine);
using RenderPolyFn = int(__fastcall*)(void* engine, const TLVertex* pts, int n, void* bmp, uint32_t flags);
using WorldBmpFn = int(__cdecl*)(void* world, void* bmp);
using RenderWorldFn = int(__cdecl*)(void* engine, void* world, void* camera, void* a4);

CreateFn g_create;
LockFn g_lock;
BmpFn g_unlock;
GetBitsFn g_bits;
GetInfoFn g_info;
DestroyFn g_destroy;
DrawFn g_draw;
EngineBmpFn g_add, g_remove;
FlushFn g_flush;
RenderPolyFn g_poly;
WorldBmpFn g_worldAdd, g_worldRemove;
RenderWorldFn g_origRenderWorld;
bool g_ok = false, g_alphaOk = false;

constexpr int kFormat565 = 5;   // gePixelFormat 565 RGB: the driver's own format, cheapest upload
constexpr int kFormat4444 = 7;  // 4444 ARGB: the only alpha texture format D3DDrv keeps
constexpr int kSettle = 2;      // frames from an add to a usable handle (D3DDrv crashed earlier)
constexpr uint32_t kImmediate = 1;  // RenderPoly flags bit 0: straight to the driver

// --- frame state -------------------------------------------------------------------
void* g_world = nullptr;      // rendered this frame
void* g_lastWorld = nullptr;  // last one seen (detach on Destroy outside a frame)
bool g_flushed = false;

int __cdecl RenderWorldDetour(void* engine, void* world, void* camera, void* a4) {
    if (world) {
        if (world != g_lastWorld) log::Info("gfx: world %08lx", reinterpret_cast<unsigned long>(world));
        g_world = g_lastWorld = world;
    }
    return g_origRenderWorld(engine, world, camera, a4);
}

void FrameBegin() {
    g_world = nullptr;
    g_flushed = false;
}

// --- font ----------------------------------------------------------------------------
HDC g_dc;
HFONT g_font, g_bold;
int g_cw = 6, g_ch = 13;
unsigned g_fontGen = 1;
int g_fontPx = 0;

void OnTextSize(cmd::Cvar&);
cmd::Cvar g_fdTextSize = {"fd_text_size", "9", "on-screen text height in pixels (7-16; was 11 before 0.15)", false, true,
                          OnTextSize, {0}};

int TextPx() {
    int v = 0;
    for (const char* s = g_fdTextSize.value; *s >= '0' && *s <= '9'; ++s) v = v * 10 + (*s - '0');
    return v < 7 ? 7 : v > 16 ? 16 : v;
}

bool Fonts() {
    if (!g_dc && !(g_dc = CreateCompatibleDC(nullptr))) return false;
    const int px = TextPx();
    if (g_font && px == g_fontPx) return true;
    if (g_font) DeleteObject(g_font), DeleteObject(g_bold);
    g_font = CreateFontA(-px, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                         NONANTIALIASED_QUALITY, FIXED_PITCH | FF_MODERN, "Consolas");
    g_bold = CreateFontA(-px, 0, 0, 0, FW_BOLD, 0, 0, 0, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                         NONANTIALIASED_QUALITY, FIXED_PITCH | FF_MODERN, "Consolas");
    SelectObject(g_dc, g_font);
    TEXTMETRICA tm;
    GetTextMetricsA(g_dc, &tm);
    g_cw = tm.tmAveCharWidth, g_ch = tm.tmHeight;
    g_fontPx = px;
    ++g_fontGen;
    log::Info("gfx: font Consolas %d px, cell %dx%d", px, g_cw, g_ch);
    return true;
}

void OnTextSize(cmd::Cvar&) { Fonts(); }

COLORREF Ref(uint32_t rgb) { return RGB((rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF); }
int RoundUp(int v, int m) { return (v + m - 1) / m * m; }

// --- view ------------------------------------------------------------------------------
// The render size: the camera's rect. The window's client area is only a fallback
// (before the first camera): it is the render size windowed through dgVoodoo, but
// larger when the image is scaled up, which put right / bottom anchored panels
// outside the frame (0.14.1).
int g_vw = 640, g_vh = 480;
bool g_vCamera = false;

bool HandleOf(void* bmp, uint32_t* h) {
    return addr::SafeRead(reinterpret_cast<uintptr_t>(bmp) + addr::Field(Id::bitmap_f_driver_handle), h, 4);
}

} // namespace

// --- frame ------------------------------------------------------------------------------
bool Ready() { return g_ok; }
void* Engine() { return hooks::CurrentEngine(); }
void* World() { return g_world; }

bool BeginDraw() {
    void* engine = Engine();
    if (!g_ok || !engine) return false;
    if (!g_flushed) {
        g_flush(engine);  // the cached 3D polys first, or they're drawn over immediate polys
        g_flushed = true;
    }
    return true;
}

void ViewSize(int* w, int* h) {
    int vw = 0, vh = 0;
    RECT rc;
    const bool fromCamera = camera::ViewSize(&vw, &vh);
    if (!fromCamera) {
        if (HWND wnd = GetActiveWindow(); wnd && GetClientRect(wnd, &rc) && rc.right > 0 && rc.bottom > 0)
            vw = rc.right, vh = rc.bottom;
        else
            vw = g_vw, vh = g_vh;
    }
    if (vw != g_vw || vh != g_vh || fromCamera != g_vCamera) {
        RECT cr = {};
        if (HWND wnd = GetActiveWindow()) GetClientRect(wnd, &cr);
        log::Info("gfx: view %dx%d (%s; window client %ldx%ld)", vw, vh, fromCamera ? "camera" : "window", cr.right,
                  cr.bottom);
        g_vw = vw, g_vh = vh, g_vCamera = fromCamera;
    }
    *w = vw, *h = vh;
}

// --- Canvas ------------------------------------------------------------------------------
void CellSize(int* w, int* h) {
    Fonts();
    *w = g_cw, *h = g_ch;
}
unsigned FontGeneration() {
    Fonts();
    return g_fontGen;
}

bool Canvas::Begin(int cols, int rows, int pad, uint32_t bg) {
    if (!Fonts()) return false;
    pad_ = pad;
    w_ = cols * g_cw + 2 * pad, h_ = rows * g_ch + 2 * pad;
    if (w_ > cap_w_ || h_ > cap_h_ || !dib_) {  // grow in steps so changing line lengths don't churn
        if (dib_) DeleteObject(static_cast<HBITMAP>(dib_));
        cap_w_ = RoundUp(w_, 64), cap_h_ = RoundUp(h_, 32);
        BITMAPINFO bi{};
        bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
        bi.bmiHeader.biWidth = cap_w_, bi.bmiHeader.biHeight = -cap_h_;  // top-down
        bi.bmiHeader.biPlanes = 1, bi.bmiHeader.biBitCount = 32, bi.bmiHeader.biCompression = BI_RGB;
        dib_ = CreateDIBSection(g_dc, &bi, DIB_RGB_COLORS, reinterpret_cast<void**>(&px_), nullptr, 0);
        if (!dib_) {
            px_ = nullptr;
            return false;
        }
    }
    SelectObject(g_dc, static_cast<HBITMAP>(dib_));
    RECT all = {0, 0, w_, h_};
    SetBkColor(g_dc, Ref(bg));
    ExtTextOutA(g_dc, 0, 0, ETO_OPAQUE, &all, "", 0, nullptr);
    return true;
}

void Canvas::Text(int col, int row, const char* s, int len, uint32_t fg, uint32_t bg, bool bold) {
    static INT dx[256];
    if (len > 256) len = 256;
    for (int i = 0; i < len; ++i) dx[i] = g_cw;
    SelectObject(g_dc, bold ? g_bold : g_font);
    SetTextColor(g_dc, Ref(fg));
    SetBkColor(g_dc, Ref(bg));
    RECT rc = {pad_ + col * g_cw, pad_ + row * g_ch, pad_ + (col + len) * g_cw, pad_ + (row + 1) * g_ch};
    ExtTextOutA(g_dc, rc.left, rc.top, ETO_OPAQUE | ETO_CLIPPED, &rc, s, len, dx);
}

void Canvas::Finish() { GdiFlush(); }

// --- Decal -------------------------------------------------------------------------------
void Decal::Destroy() {
    if (!bmp_) return;
    if (engine_) g_remove(engine_, bmp_);
    g_destroy(&bmp_);
    bmp_ = nullptr, engine_ = nullptr;
}

bool Decal::Upload(const uint32_t* px, int stride, int w, int h) {
    void* engine = Engine();
    if (!g_ok || !engine || !px) return false;
    if (bmp_ && (engine_ != engine || w > cap_w_ || h > cap_h_)) Destroy();
    const bool fresh = !bmp_;
    if (fresh) {
        cap_w_ = RoundUp(w, 64), cap_h_ = RoundUp(h, 32);
        if (!(bmp_ = g_create(cap_w_, cap_h_, 1, kFormat565))) return false;
    }
    void* target = nullptr;
    bool ok = g_lock(bmp_, &target, 0, 0, kFormat565) && target;
    if (ok) {
        uint32_t info[9] = {};
        g_info(target, info, nullptr);
        const int ds = static_cast<int>(info[2]);
        auto* dst = static_cast<uint16_t*>(g_bits(target));
        ok = dst && ds >= w;
        for (int y = 0; ok && y < h; ++y) {
            const uint32_t* s = px + y * stride;
            uint16_t* d = dst + y * ds;
            for (int x = 0; x < w; ++x) {
                const uint32_t c = s[x];
                d[x] = static_cast<uint16_t>(((c >> 8) & 0xF800) | ((c >> 5) & 0x07E0) | ((c >> 3) & 0x001F));
            }
        }
        g_unlock(target);
    }
    if (ok && fresh) {
        ok = g_add(engine, bmp_) != 0;
        engine_ = engine, settle_ = kSettle;
    }
    if (!ok) {
        Destroy();
        return false;
    }
    w_ = w, h_ = h;
    return true;
}

bool Decal::Draw(int x, int y) {
    if (!bmp_ || engine_ != Engine() || !BeginDraw()) return false;
    if (settle_ > 0) {
        --settle_;
        return false;
    }
    const GeRect src = {0, w_ - 1, 0, h_ - 1};
    return g_draw(engine_, bmp_, &src, static_cast<uint32_t>(x < 0 ? 0 : x), static_cast<uint32_t>(y < 0 ? 0 : y)) != 0;
}

// --- AlphaSurface ----------------------------------------------------------------------------
namespace {
uint16_t g_tileBuf[AlphaSurface::kTile * AlphaSurface::kTile];  // game thread only
}

bool AlphaSurface::Upload(const uint32_t* px, int stride, int w, int h, const AlphaKey* keys, int nKeys,
                          uint8_t otherAlpha) {
    if (!g_alphaOk || !px) return false;
    constexpr int kSide = 3;  // kMaxTiles = 3 x 3
    if (w > kSide * kTile) w = kSide * kTile;
    if (h > kSide * kTile) h = kSide * kTile;
    w_ = w, h_ = h;
    cols_ = (w + kTile - 1) / kTile, rows_ = (h + kTile - 1) / kTile;
    for (int ty = 0; ty < rows_; ++ty)
        for (int tx = 0; tx < cols_; ++tx) {
            Tile& t = tiles_[ty * kSide + tx];
            for (int y = 0; y < kTile; ++y) {
                const int sy = ty * kTile + y;
                uint16_t* d = g_tileBuf + y * kTile;
                for (int x = 0; x < kTile; ++x) {
                    const int sx = tx * kTile + x;
                    if (sx >= w || sy >= h) {
                        d[x] = 0;  // outside the content: fully transparent
                        continue;
                    }
                    const uint32_t c = px[sy * stride + sx] & 0xFFFFFF;
                    uint32_t a = otherAlpha;
                    for (int k = 0; k < nKeys; ++k)
                        if (keys[k].rgb == c) a = keys[k].alpha;
                    d[x] = static_cast<uint16_t>(((a >> 4) << 12) | (((c >> 20) & 0xF) << 8) | (((c >> 12) & 0xF) << 4) |
                                                 ((c >> 4) & 0xF));
                }
            }
            if (t.bmp && t.shadow && __builtin_memcmp(t.shadow, g_tileBuf, sizeof(g_tileBuf)) == 0) continue;
            if (!t.shadow &&
                !(t.shadow = static_cast<uint16_t*>(VirtualAlloc(nullptr, sizeof(g_tileBuf), MEM_COMMIT | MEM_RESERVE,
                                                                  PAGE_READWRITE))))
                return false;
            if (!t.bmp && !(t.bmp = g_create(kTile, kTile, 1, kFormat4444))) return false;
            void* target = nullptr;
            if (!g_lock(t.bmp, &target, 0, 0, kFormat4444) || !target) return false;
            uint32_t info[9] = {};
            g_info(target, info, nullptr);
            const int ds = static_cast<int>(info[2]);
            auto* dst = static_cast<uint16_t*>(g_bits(target));
            for (int y = 0; dst && ds >= kTile && y < kTile; ++y)
                __builtin_memcpy(dst + y * ds, g_tileBuf + y * kTile, kTile * sizeof(uint16_t));
            g_unlock(target);
            __builtin_memcpy(t.shadow, g_tileBuf, sizeof(g_tileBuf));
            ++uploads_;
        }
    return true;
}

bool AlphaSurface::Attach(Tile& t) {
    void* world = World();
    if (!world || !t.bmp) return false;
    if (t.world != world) {
        if (!g_worldAdd(world, t.bmp)) return false;
        t.world = world, t.settle = kSettle;
    }
    return true;
}

bool AlphaSurface::Draw(int x, int y) {
    if (!g_alphaOk || !World() || cols_ == 0 || !BeginDraw()) return false;
    constexpr int kSide = 3;
    void* engine = Engine();
    for (int ty = 0; ty < rows_; ++ty)
        for (int tx = 0; tx < cols_; ++tx) {
            Tile& t = tiles_[ty * kSide + tx];
            if (!Attach(t)) return false;
            if (t.settle > 0) {
                --t.settle;
                continue;
            }
            uint32_t handle = 0;
            if (!HandleOf(t.bmp, &handle) || !handle) return false;  // never attached: let the caller fall back
            const int w = w_ - tx * kTile < kTile ? w_ - tx * kTile : kTile;
            const int h = h_ - ty * kTile < kTile ? h_ - ty * kTile : kTile;
            // Half a pixel back: texel centres on pixel centres, so the text stays crisp.
            const float x0 = static_cast<float>(x + tx * kTile) - 0.5f, y0 = static_cast<float>(y + ty * kTile) - 0.5f;
            const float x1 = x0 + w, y1 = y0 + h, u1 = static_cast<float>(w) / kTile, v1 = static_cast<float>(h) / kTile;
            const TLVertex v[4] = {{x0, y0, 1.0f, 0, 0, 255, 255, 255, 255},
                                   {x1, y0, 1.0f, u1, 0, 255, 255, 255, 255},
                                   {x1, y1, 1.0f, u1, v1, 255, 255, 255, 255},
                                   {x0, y1, 1.0f, 0, v1, 255, 255, 255, 255}};
            g_poly(engine, v, 4, t.bmp, kImmediate);
        }
    return true;
}

void AlphaSurface::Destroy() {
    for (Tile& t : tiles_) {
        // Only the world still alive can be told; an earlier one (level change) was
        // destroyed with its bitmap list.
        if (t.bmp && t.world && t.world == g_lastWorld) g_worldRemove(t.world, t.bmp);
        if (t.bmp) g_destroy(&t.bmp);
        if (t.shadow) VirtualFree(t.shadow, 0, MEM_RELEASE);
        t = {};
    }
    cols_ = rows_ = 0;
}

// --- SolidRect ---------------------------------------------------------------------------------
bool SolidRect(int x, int y, int w, int h, uint32_t rgb) {
    if (!g_alphaOk || !BeginDraw()) return false;
    const float x0 = static_cast<float>(x), y0 = static_cast<float>(y), x1 = x0 + w, y1 = y0 + h;
    const float r = static_cast<float>((rgb >> 16) & 0xFF), g = static_cast<float>((rgb >> 8) & 0xFF),
                b = static_cast<float>(rgb & 0xFF);
    const TLVertex v[4] = {{x0, y0, 1.0f, 0, 0, r, g, b, 255},
                           {x1, y0, 1.0f, 0, 0, r, g, b, 255},
                           {x1, y1, 1.0f, 0, 0, r, g, b, 255},
                           {x0, y1, 1.0f, 0, 0, r, g, b, 255}};
    return g_poly(Engine(), v, 4, nullptr, kImmediate) != 0;
}

// --- init ----------------------------------------------------------------------------------------
bool Init() {
    const Id needs[] = {Id::genesis_bitmap_create, Id::genesis_bitmap_lock_write_format, Id::genesis_bitmap_unlock,
                        Id::genesis_bitmap_get_bits, Id::genesis_bitmap_get_info, Id::genesis_bitmap_destroy,
                        Id::genesis_draw_bitmap, Id::genesis_add_bitmap, Id::genesis_remove_bitmap,
                        Id::genesis_flush_pipeline};
    for (Id id : needs)
        if (!addr::Usable(id)) {
            log::Warn("gfx: disabled, no on-screen text (needs %s)", addr::Name(id));
            return false;
        }
    g_create = reinterpret_cast<CreateFn>(addr::ExportAddress(Id::genesis_bitmap_create));
    g_lock = reinterpret_cast<LockFn>(addr::ExportAddress(Id::genesis_bitmap_lock_write_format));
    g_unlock = reinterpret_cast<BmpFn>(addr::ExportAddress(Id::genesis_bitmap_unlock));
    g_bits = reinterpret_cast<GetBitsFn>(addr::ExportAddress(Id::genesis_bitmap_get_bits));
    g_info = reinterpret_cast<GetInfoFn>(addr::ExportAddress(Id::genesis_bitmap_get_info));
    g_destroy = reinterpret_cast<DestroyFn>(addr::ExportAddress(Id::genesis_bitmap_destroy));
    g_draw = reinterpret_cast<DrawFn>(addr::ExportAddress(Id::genesis_draw_bitmap));
    g_add = reinterpret_cast<EngineBmpFn>(addr::ExportAddress(Id::genesis_add_bitmap));
    g_remove = reinterpret_cast<EngineBmpFn>(addr::ExportAddress(Id::genesis_remove_bitmap));
    g_flush = reinterpret_cast<FlushFn>(addr::ExportAddress(Id::genesis_flush_pipeline));
    g_ok = true;
    cmd::RegisterCvar(&g_fdTextSize);
    hooks::OnFrameBegin(FrameBegin);

    // Alpha surfaces / solid rects: optional on top of the decal path.
    const Id alphaNeeds[] = {Id::genesis_render_poly, Id::genesis_render_world, Id::genesis_world_add_bitmap,
                             Id::genesis_world_remove_bitmap};
    const char* missing = nullptr;
    for (Id id : alphaNeeds)
        if (!missing && !addr::Usable(id)) missing = addr::Name(id);
    if (missing) {
        log::Warn("gfx: no alpha surfaces (needs %s); panels drawn opaque", missing);
    } else {
        g_poly = reinterpret_cast<RenderPolyFn>(addr::ExportAddress(Id::genesis_render_poly));
        g_worldAdd = reinterpret_cast<WorldBmpFn>(addr::ExportAddress(Id::genesis_world_add_bitmap));
        g_worldRemove = reinterpret_cast<WorldBmpFn>(addr::ExportAddress(Id::genesis_world_remove_bitmap));
        g_alphaOk = hooks::Create("gfx", Id::genesis_render_world, reinterpret_cast<void*>(&RenderWorldDetour),
                                  reinterpret_cast<void**>(&g_origRenderWorld)) &&
                    hooks::SetEnabled("gfx", true);
        if (!g_alphaOk) log::Warn("gfx: geEngine_RenderWorld not hooked; panels drawn opaque");
    }
    log::Info("gfx: ready (decals%s)", g_alphaOk ? ", alpha surfaces, solid rects" : "");
    return true;
}

} // namespace fdcore::gfx
