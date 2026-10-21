#include "textdraw.h"

#include <windows.h>

#include "addr.h"
#include "hooks.h"
#include "log.h"

namespace fdcore::textdraw {
namespace {

using addr::Id;

// Genesis bitmap API (ABI read from Genesis.dll, see addresses.json "ghidra").
struct GeRect {
    int32_t left, right, top, bottom;  // GE_Rect, inclusive
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

CreateFn g_create;
LockFn g_lock;
BmpFn g_unlock;
GetBitsFn g_bits;
GetInfoFn g_info;
DestroyFn g_destroy;
DrawFn g_draw;
EngineBmpFn g_add, g_remove;
FlushFn g_flush;
bool g_ok = false;

constexpr int kFormat565 = 5;  // gePixelFormat "565 RGB": the driver's own format, cheapest upload
constexpr int kPad = 4;        // around the text, like the original Text widget
constexpr int kSettle = 2;     // frames between geEngine_AddBitmap and the first blit (D3DDrv crashed earlier)

// The original collision overlay's colours (COLORS / SOURCE_COLORS / PLAYER_COLORS), by tag.
constexpr COLORREF Hex(uint32_t rgb) { return RGB((rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF); }
const COLORREF kFg[T_COUNT] = {
    Hex(0x7c8894), Hex(0xd8e0e8), Hex(0x8a96a2), Hex(0xe8e8e8), Hex(0x5c646c), Hex(0xff3b3b), Hex(0x7fd17f),
    Hex(0xffcc4d), Hex(0xffffff),
    Hex(0xffa040), Hex(0xff5a5a), Hex(0x5aaaff), Hex(0x3f7fbf), Hex(0xffd84a), Hex(0xd0d0d0), Hex(0x8a8a8a),
    Hex(0xb48cff),
    Hex(0x7fd17f), Hex(0x5ad1d1), Hex(0x5aaaff), Hex(0xff5a5a), Hex(0xb48cff), Hex(0xff8c1a), Hex(0x7fd17f),
    Hex(0xffcc4d),
};
const COLORREF kBg = Hex(0x101418), kWarpBg = Hex(0x5a1010), kFlashBg = Hex(0xb02020);

bool Bold(uint8_t a) {  // title / alert / warp / flash / Bugged are bold in the original
    const uint8_t t = a & TAG_MASK;
    return t == T_TITLE || t == T_ALERT || t == T_PL_BUGGED || (a & (BG_WARP | BG_FLASH));
}

struct Panel {
    char name[24];
    Anchor anchor;
    int dx, dy, minFrames;
    uint8_t defTag;
    bool visible;
    char text[kMaxLines][kLineLen];
    uint8_t attr[kMaxLines][kLineLen];
    int n;
    bool dirty;
    unsigned long lastRender;
    // rendered image (GDI) and its engine bitmap
    HBITMAP dib;
    uint32_t* px;
    int w, h;            // content size in pixels
    int dibW, dibH;      // DIB / bitmap capacity
    void* bmp;
    void* engine;
    int settle;
    bool uploaded;
};
constexpr int kMaxPanels = 8;
Panel g_panels[kMaxPanels];
int g_nPanels = 0;
unsigned long g_frame = 0;

HDC g_dc;
HFONT g_font, g_bold;
int g_cw = 6, g_ch = 13;

bool Gdi() {
    if (g_dc) return true;
    g_dc = CreateCompatibleDC(nullptr);
    if (!g_dc) return false;
    g_font = CreateFontA(-11, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                         NONANTIALIASED_QUALITY, FIXED_PITCH | FF_MODERN, "Consolas");
    g_bold = CreateFontA(-11, 0, 0, 0, FW_BOLD, 0, 0, 0, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                         NONANTIALIASED_QUALITY, FIXED_PITCH | FF_MODERN, "Consolas");
    SelectObject(g_dc, g_font);
    TEXTMETRICA tm;
    GetTextMetricsA(g_dc, &tm);
    g_cw = tm.tmAveCharWidth, g_ch = tm.tmHeight;
    return true;
}

int RoundUp(int v, int m) { return (v + m - 1) / m * m; }

void DropBitmap(Panel& p) {
    if (!p.bmp) return;
    if (p.engine) g_remove(p.engine, p.bmp);
    g_destroy(&p.bmp);
    p.bmp = nullptr, p.engine = nullptr;
}

// GDI: runs of equal tag, opaque cells on the exact column grid.
void Render(Panel& p) {
    int cols = 0;
    for (int i = 0; i < p.n; ++i) {
        const int len = lstrlenA(p.text[i]);
        if (len > cols) cols = len;
    }
    p.w = cols * g_cw + 2 * kPad, p.h = p.n * g_ch + 2 * kPad;
    if (p.w > p.dibW || p.h > p.dibH || !p.dib) {  // grow in steps so changing line lengths don't churn
        if (p.dib) DeleteObject(p.dib);
        p.dibW = RoundUp(p.w, 64), p.dibH = RoundUp(p.h, 32);
        BITMAPINFO bi{};
        bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
        bi.bmiHeader.biWidth = p.dibW, bi.bmiHeader.biHeight = -p.dibH;  // top-down
        bi.bmiHeader.biPlanes = 1, bi.bmiHeader.biBitCount = 32, bi.bmiHeader.biCompression = BI_RGB;
        p.dib = CreateDIBSection(g_dc, &bi, DIB_RGB_COLORS, reinterpret_cast<void**>(&p.px), nullptr, 0);
        DropBitmap(p);  // the engine bitmap is the DIB's size
        if (!p.dib) return;
    }
    SelectObject(g_dc, p.dib);
    RECT all = {0, 0, p.w, p.h};
    SetBkColor(g_dc, kBg);
    ExtTextOutA(g_dc, 0, 0, ETO_OPAQUE, &all, "", 0, nullptr);
    static INT dx[kLineLen];
    for (int i = 0; i < kLineLen; ++i) dx[i] = g_cw;
    for (int r = 0; r < p.n; ++r) {
        const int len = lstrlenA(p.text[r]);
        for (int c = 0; c < len;) {
            const uint8_t a = p.attr[r][c];
            int e = c + 1;
            while (e < len && p.attr[r][e] == a) ++e;
            SelectObject(g_dc, Bold(a) ? g_bold : g_font);
            SetTextColor(g_dc, kFg[(a & TAG_MASK) < T_COUNT ? (a & TAG_MASK) : T_TEXT]);
            SetBkColor(g_dc, (a & BG_FLASH) ? kFlashBg : (a & BG_WARP) ? kWarpBg : kBg);
            RECT rc = {kPad + c * g_cw, kPad + r * g_ch, kPad + e * g_cw, kPad + (r + 1) * g_ch};
            ExtTextOutA(g_dc, rc.left, rc.top, ETO_OPAQUE | ETO_CLIPPED, &rc, p.text[r] + c, e - c, dx);
            c = e;
        }
    }
    GdiFlush();
}

bool Upload(Panel& p) {  // DIB -> engine bitmap (565)
    void* target = nullptr;
    if (!g_lock(p.bmp, &target, 0, 0, kFormat565) || !target) return false;
    uint32_t info[9] = {};
    g_info(target, info, nullptr);
    const int stride = static_cast<int>(info[2]);
    auto* dst = static_cast<uint16_t*>(g_bits(target));
    if (dst && stride >= p.w)
        for (int y = 0; y < p.h; ++y) {
            const uint32_t* s = p.px + y * p.dibW;
            uint16_t* d = dst + y * stride;
            for (int x = 0; x < p.w; ++x) {
                const uint32_t c = s[x];  // DIB 0x00RRGGBB
                d[x] = static_cast<uint16_t>(((c >> 8) & 0xF800) | ((c >> 5) & 0x07E0) | ((c >> 3) & 0x001F));
            }
        }
    g_unlock(target);
    return dst != nullptr;
}

void ViewSize(int* w, int* h) {  // the game's client area (= render size, windowed)
    static int cw = 640, chh = 480;
    RECT rc;
    if (HWND wnd = GetActiveWindow(); wnd && GetClientRect(wnd, &rc) && rc.right > 0 && rc.bottom > 0)
        cw = rc.right, chh = rc.bottom;
    *w = cw, *h = chh;
}

void Draw() {
    ++g_frame;
    if (!g_ok) return;
    void* engine = hooks::CurrentEngine();
    if (!engine) return;
    bool any = false;
    for (int i = 0; i < g_nPanels; ++i) {
        Panel& p = g_panels[i];
        if (!p.visible || p.n <= 0) continue;
        if (p.dirty && (!p.uploaded || g_frame - p.lastRender >= static_cast<unsigned long>(p.minFrames))) {
            if (!Gdi()) return;
            Render(p);
            if (!p.dib) continue;
            if (p.bmp && p.engine != engine) DropBitmap(p);
            if (!p.bmp) {
                p.bmp = g_create(p.dibW, p.dibH, 1, kFormat565);
                if (!p.bmp) continue;
                if (!Upload(p) || !g_add(engine, p.bmp)) {
                    log::Warn("textdraw: %s: could not create its bitmap (%dx%d)", p.name, p.dibW, p.dibH);
                    g_destroy(&p.bmp);
                    p.bmp = nullptr;
                    continue;
                }
                p.engine = engine, p.settle = kSettle;
            } else {
                Upload(p);
            }
            p.dirty = false, p.uploaded = true, p.lastRender = g_frame;
        }
        any |= p.bmp && p.uploaded;
    }
    if (!any) return;
    g_flush(engine);  // the cached 3D polys first, or they're drawn over the blits
    int vw, vh;
    ViewSize(&vw, &vh);
    for (int i = 0; i < g_nPanels; ++i) {
        Panel& p = g_panels[i];
        if (!p.visible || p.n <= 0 || !p.bmp || !p.uploaded) continue;
        if (p.settle > 0) {
            --p.settle;
            continue;
        }
        int x = p.dx, y = p.dy;
        if (p.anchor == TOP_RIGHT) x = vw - p.dx - p.w;
        if (p.anchor == BOTTOM_LEFT) y = vh - p.dy - p.h;
        if (x < 0) x = 0;
        if (y < 0) y = 0;
        const GeRect src = {0, p.w - 1, 0, p.h - 1};
        g_draw(engine, p.bmp, &src, static_cast<uint32_t>(x), static_cast<uint32_t>(y));
    }
}

} // namespace

int Create(const char* name, Anchor anchor, int dx, int dy, int minFrames, uint8_t defaultTag) {
    if (g_nPanels >= kMaxPanels) return -1;
    Panel& p = g_panels[g_nPanels];
    lstrcpynA(p.name, name, sizeof(p.name));
    p.anchor = anchor, p.dx = dx, p.dy = dy, p.minFrames = minFrames < 1 ? 1 : minFrames, p.defTag = defaultTag;
    p.visible = true;
    return g_nPanels++;
}

void Set(int panel, const char (*lines)[kLineLen], const uint8_t (*attrs)[kLineLen], int n) {
    if (panel < 0 || panel >= g_nPanels) return;
    Panel& p = g_panels[panel];
    if (n > kMaxLines) n = kMaxLines;
    bool same = n == p.n;
    for (int i = 0; same && i < n; ++i) {
        same = lstrcmpA(lines[i], p.text[i]) == 0;
        if (same && attrs) same = __builtin_memcmp(attrs[i], p.attr[i], lstrlenA(lines[i])) == 0;
    }
    if (same) return;
    for (int i = 0; i < n; ++i) {
        lstrcpynA(p.text[i], lines[i], kLineLen);
        if (attrs) __builtin_memcpy(p.attr[i], attrs[i], kLineLen);
        else __builtin_memset(p.attr[i], p.defTag, kLineLen);
    }
    p.n = n;
    p.dirty = true;
}

void SetLines(int panel, const char* const* lines, int n) {
    static char buf[kMaxLines][kLineLen];
    if (n > kMaxLines) n = kMaxLines;
    for (int i = 0; i < n; ++i) lstrcpynA(buf[i], lines[i], kLineLen);
    Set(panel, buf, nullptr, n);
}

void Show(int panel, bool visible) {
    if (panel >= 0 && panel < g_nPanels) g_panels[panel].visible = visible;
}

bool Init() {
    const Id needs[] = {Id::genesis_bitmap_create, Id::genesis_bitmap_lock_write_format, Id::genesis_bitmap_unlock,
                        Id::genesis_bitmap_get_bits, Id::genesis_bitmap_get_info, Id::genesis_bitmap_destroy,
                        Id::genesis_draw_bitmap, Id::genesis_add_bitmap, Id::genesis_remove_bitmap,
                        Id::genesis_flush_pipeline};
    for (Id id : needs)
        if (!addr::Usable(id)) {
            log::Warn("textdraw: disabled, no on-screen text (needs %s)", addr::Name(id));
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
    hooks::OnFrameEnd(Draw);
    log::Info("textdraw: ready (%d panel(s))", g_nPanels);
    return true;
}

} // namespace fdcore::textdraw
