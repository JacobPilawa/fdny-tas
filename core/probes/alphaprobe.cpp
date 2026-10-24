// alphaprobe -- research spike (owner request: "try C as a research spike with
// one off tests, don't modify working code"). Can Genesis draw a truly
// translucent 2D panel, and what does it cost?
//
// A standalone .asi (zz_alphaprobe.asi: loads after FDNYCore.asi, so the
// core's signature check sees the unhooked geEngine_EndFrame). It hooks
// geEngine_EndFrame itself (outermost: runs before the core's frame end) and
// draws per the current command. Test-only: the probe tests place it in the
// game folder via pre_files; nothing in core/src depends on it.
//
// ABIs (capstone disassembly of Genesis.dll, 2026-10-21; see
// evidence/alpha_spike/README.md):
//   @geEngine_RenderPoly@20  fastcall ECX=engine (unused), EDX=GE_TLVertex*,
//       stack (NumPoints, geBitmap* or NULL, Flags); RET 0xC. Flags bit 0
//       (GE_RENDER_DO_NOT_OCCLUDE_OTHERS) = straight to the driver (vtbl +0x84
//       textured / +0x7C gouraud); otherwise queued in the sorted poly lists.
//   GE_TLVertex = {x, y, z, u, v, r, g, b, a} floats (36 bytes); Genesis's own
//       geEngine_DrawAlphaBitmap uses z = 1.0, colours 0..255.
//
// Commands: the game console prints "unknown command: fd_zzp_..." into
// FDNYCore.log; a thread tails it. Without the core, env ALPHAPROBE gives the
// start command. Format fd_zzp_<mode>_<alpha>_<z>_<count>_<format>:
//   mode 0 off
//        1 untextured quad(s), immediate (flags 1), after geEngine_FlushPipeline
//        2 untextured quad(s), queued (flags 0), no flush
//        3 mode 1 + a colour-keyed text decal (geEngine_DrawBitmap) on top
//        4 textured quad, per-pixel alpha bitmap (512x512 in <format>), immediate
//        5 mode 1 without the flush (ordering check)
//        6 mode 4 attached with geWorld_AddBitmap (3D texture handle; world from geEngine_RenderWorld)
//        7 mode 4 attached with geEngine_AddBitmap + driver flags 0x40003 (internal setter)
//        8 the world-attached texture through geEngine_DrawAlphaBitmap (Genesis's own 2D alpha call)
//        9 the real-overlay shape: 2x2 world-attached 256 tiles, live text re-uploaded every <count>
//          frames, texel-centred, re-added on a world change (see Panel9)
//   (mode 4 itself crashed in D3DDrv: an engine-attached bitmap has only a 2D decal handle)
//   alpha 0..255 (vertex alpha; mode 4: the background texel alpha)
//   z     0: 1.0  1: 0.0  2: 0.5  3: 100.0
//   count quads per frame (cost), stacked 4 px apart
//   size  mode 4/6/7/8 texture edge: 64, 128, 256 (default) or 512
//   format gePixelFormat for mode 4: 7 = 4444 ARGB, 8 = 1555 ARGB, 17 = 32-bit ARGB
// Log: zz_alphaprobe.log in the game folder (throwaway; the test copies it to
// evidence and deletes it).
#include <windows.h>

#include <stdint.h>

#include "../third_party/minhook/include/MinHook.h"

namespace {

struct TLVertex {
    float x, y, z, u, v, r, g, b, a;
};
struct GeRect {
    int32_t left, right, top, bottom;
};

using RenderPolyFn = int(__fastcall*)(void* engine, const TLVertex* pts, int n, void* bmp, uint32_t flags);
using CreateFn = void*(__fastcall*)(int w, int h, int maxMip, int format);
using LockFn = int(__fastcall*)(void* bmp, void** target, int minMip, int maxMip, int format);
using BmpFn = int(__fastcall*)(void* bmp);
using GetBitsFn = void*(__fastcall*)(void* bmp);
using GetInfoFn = int(__fastcall*)(void* bmp, uint32_t* info, uint32_t* info2);
using SetCKFn = int(__fastcall*)(void* bmp, int have, uint32_t key, int smart);
using DrawFn = int(__fastcall*)(void* engine, void* bmp, const GeRect* src, uint32_t x, uint32_t y);
using EngineBmpFn = int(__cdecl*)(void* engine, void* bmp);
using FlushFn = int(__cdecl*)(void* engine);
using EndFrameFn = int(__cdecl*)(void* engine);

RenderPolyFn g_poly;
CreateFn g_create;
LockFn g_lock;
BmpFn g_unlock;
GetBitsFn g_bits;
GetInfoFn g_info;
SetCKFn g_setck;
DrawFn g_draw;
EngineBmpFn g_add;
FlushFn g_flush;
EndFrameFn g_endOrig;
using RenderWorldFn = int(__cdecl*)(void* engine, void* world, void* camera, void* a4);
using DrawAlphaFn = int(__fastcall*)(void* engine, void* bmp, void* uv, void* camera, const GeRect* pixel,
                                     const GeRect* percent, float alpha, void* rgba);
using SetFlagsFn = int(__fastcall*)(void* bmp, uint32_t flags);
RenderWorldFn g_rwOrig;
EngineBmpFn g_worldAdd;
DrawAlphaFn g_drawAlpha;
SetFlagsFn g_setFlags;
void* g_world;
void* g_camera;

char g_dir[MAX_PATH];
HANDLE g_log = INVALID_HANDLE_VALUE;
CRITICAL_SECTION g_logLock;

void Log(const char* fmt, ...) {
    char buf[512];
    SYSTEMTIME t;
    GetLocalTime(&t);
    int o = wsprintfA(buf, "[%02d:%02d:%02d.%03d] ", t.wHour, t.wMinute, t.wSecond, t.wMilliseconds);
    va_list ap;
    va_start(ap, fmt);
    o += wvsprintfA(buf + o, fmt, ap);
    va_end(ap);
    buf[o++] = '\r', buf[o++] = '\n';
    EnterCriticalSection(&g_logLock);
    DWORD w;
    if (g_log != INVALID_HANDLE_VALUE) WriteFile(g_log, buf, o, &w, nullptr);
    LeaveCriticalSection(&g_logLock);
}

// --- command ----------------------------------------------------------------------
struct Cmd {
    int mode, alpha, z, count, format, size;
};
volatile LONG g_cmdSeq = 0;
Cmd g_pending = {0, 128, 0, 1, 7, 256};
CRITICAL_SECTION g_cmdLock;

int ParseInt(const char*& s) {
    int v = 0;
    while (*s >= '0' && *s <= '9') v = v * 10 + (*s++ - '0');
    if (*s == '_') ++s;
    return v;
}

bool ParseCmd(const char* s, Cmd* c) {  // s after "fd_zzp_"
    Cmd n = {0, 128, 0, 1, 7, 256};
    if (*s < '0' || *s > '9') return false;
    n.mode = ParseInt(s);
    if (*s >= '0' && *s <= '9') n.alpha = ParseInt(s);
    if (*s >= '0' && *s <= '9') n.z = ParseInt(s);
    if (*s >= '0' && *s <= '9') n.count = ParseInt(s);
    if (*s >= '0' && *s <= '9') n.format = ParseInt(s);
    if (*s >= '0' && *s <= '9') n.size = ParseInt(s);
    if (n.size != 64 && n.size != 128 && n.size != 256 && n.size != 512) n.size = 256;
    if (n.count < 1) n.count = 1;
    if (n.count > 400) n.count = 400;
    *c = n;
    return true;
}

void Post(const Cmd& c, const char* from) {
    EnterCriticalSection(&g_cmdLock);
    g_pending = c;
    LeaveCriticalSection(&g_cmdLock);
    InterlockedIncrement(&g_cmdSeq);
    Log("cmd (%s): mode %d alpha %d z %d count %d format %d", from, c.mode, c.alpha, c.z, c.count, c.format);
}

DWORD WINAPI Tail(LPVOID) {  // FDNYCore.log -> commands
    char path[MAX_PATH];
    wsprintfA(path, "%sFDNYCore.log", g_dir);
    LONGLONG pos = -1;
    static char buf[8192];
    static char line[512];
    int ln = 0;
    for (;;) {
        Sleep(100);
        HANDLE f = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                               OPEN_EXISTING, 0, nullptr);
        if (f == INVALID_HANDLE_VALUE) continue;
        LARGE_INTEGER size;
        GetFileSizeEx(f, &size);
        if (pos < 0 || size.QuadPart < pos) pos = size.QuadPart;  // start at the end: only new lines
        LARGE_INTEGER at;
        at.QuadPart = pos;
        SetFilePointerEx(f, at, nullptr, FILE_BEGIN);
        DWORD got = 0;
        while (ReadFile(f, buf, sizeof(buf), &got, nullptr) && got) {
            pos += got;
            for (DWORD i = 0; i < got; ++i) {
                if (buf[i] != '\n') {
                    if (ln < static_cast<int>(sizeof(line)) - 1) line[ln++] = buf[i];
                    continue;
                }
                line[ln] = 0;
                ln = 0;
                const char* k = "unknown command: fd_zzp_";
                for (const char* s = line; *s; ++s) {
                    const char *a = s, *b = k;
                    while (*b && *a == *b) ++a, ++b;
                    if (*b) continue;
                    Cmd c;
                    if (ParseCmd(a, &c)) Post(c, "console");
                    break;
                }
            }
        }
        CloseHandle(f);
    }
}

// --- drawing ----------------------------------------------------------------------
Cmd g_cur = {0, 128, 0, 1, 7, 256};
LONG g_seenSeq = 0;
bool g_firstLogged = false;

const float kZ[] = {1.0f, 0.0f, 0.5f, 100.0f};

void Quad(TLVertex* v, float x0, float y0, float x1, float y1, float z, float r, float g, float b, float a) {
    const float xs[4] = {x0, x1, x1, x0}, ys[4] = {y0, y0, y1, y1}, us[4] = {0, 1, 1, 0}, vs[4] = {0, 0, 1, 1};
    for (int i = 0; i < 4; ++i) v[i] = {xs[i], ys[i], z, us[i], vs[i], r, g, b, a};
}

void ViewSize(int* w, int* h) {
    RECT rc;
    *w = 640, *h = 480;
    if (HWND wnd = GetActiveWindow(); wnd && GetClientRect(wnd, &rc) && rc.right > 0) *w = rc.right, *h = rc.bottom;
}

// GDI text -> 32-bit DIB (white on black), shared by modes 3 and 4.
constexpr int kTexW = 512, kTexH = 512, kPanelW = 400, kPanelH = 346;
uint32_t* g_dibPx;
HDC g_dc;
bool RenderText() {
    if (g_dibPx) return true;
    HDC dc = g_dc = CreateCompatibleDC(nullptr);
    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
    bi.bmiHeader.biWidth = kTexW, bi.bmiHeader.biHeight = -kTexH;
    bi.bmiHeader.biPlanes = 1, bi.bmiHeader.biBitCount = 32, bi.bmiHeader.biCompression = BI_RGB;
    HBITMAP dib = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, reinterpret_cast<void**>(&g_dibPx), nullptr, 0);
    if (!dib) return false;
    SelectObject(dc, dib);
    HFONT f = CreateFontA(-11, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                          NONANTIALIASED_QUALITY, FIXED_PITCH | FF_MODERN, "Consolas");
    SelectObject(dc, f);
    SetBkColor(dc, RGB(0, 0, 0));
    SetTextColor(dc, RGB(255, 255, 255));
    RECT all = {0, 0, kTexW, kTexH};
    ExtTextOutA(dc, 0, 0, ETO_OPAQUE, &all, "", 0, nullptr);
    for (int i = 0; i < 24; ++i) {
        char t[96];
        int n = wsprintfA(t, "ALPHA PROBE line %02d  The quick brown fox 0123456789", i);
        TextOutA(dc, 6, 6 + i * 13, t, n);
    }
    GdiFlush();
    return true;
}

// mode 3: colour-keyed 565 text decal (black = key)
void* g_textBmp;
int g_textSettle;
bool MakeTextDecal(void* engine) {
    if (g_textBmp) return true;
    if (!RenderText()) return false;
    g_textBmp = g_create(kPanelW, kPanelH, 1, 5);
    if (!g_textBmp) return Log("mode3: geBitmap_Create failed"), false;
    g_setck(g_textBmp, 1, 0, 0);
    void* t = nullptr;
    if (!g_lock(g_textBmp, &t, 0, 0, 5) || !t) return Log("mode3: lock failed"), false;
    uint32_t info[9] = {};
    g_info(t, info, nullptr);
    auto* d = static_cast<uint16_t*>(g_bits(t));
    for (int y = 0; d && y < kPanelH; ++y)
        for (int x = 0; x < kPanelW; ++x) {
            uint32_t c = g_dibPx[y * kTexW + x];
            d[y * info[2] + x] = static_cast<uint16_t>(((c >> 8) & 0xF800) | ((c >> 5) & 0x07E0) | ((c >> 3) & 0x001F));
        }
    g_unlock(t);
    const int ok = g_add(engine, g_textBmp);
    Log("mode3: text decal %dx%d stride %lu, add %d", kPanelW, kPanelH, info[2], ok);
    g_textSettle = 2;
    return ok != 0;
}

// mode 4: per-pixel alpha texture in the requested format (g_tex x g_tex)
int g_tex = 256;

// The bitmap's driver handle ([bmp+0xAC], what RenderPoly passes on): logged once with
// its first fields; a null handle skips drawing instead of crashing in D3DDrv.
bool g_handleLogged;
bool HandleOk(void* bmp) {
    uint32_t h = 0, f[8] = {};
    SIZE_T got = 0;
    ReadProcessMemory(GetCurrentProcess(), static_cast<uint8_t*>(bmp) + 0xAC, &h, 4, &got);
    if (h) ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<void*>(h), f, sizeof(f), &got);
    if (!g_handleLogged) {
        g_handleLogged = true;
        Log("handle: bmp %p [+0xAC] = %08lX; fields %08lX %08lX %08lX %08lX %08lX %08lX %08lX %08lX", bmp, h, f[0],
            f[1], f[2], f[3], f[4], f[5], f[6], f[7]);
    }
    return h != 0;
}
void* g_alphaBmp;
int g_alphaFormat = -1, g_alphaA = -1, g_alphaSettle;
uint32_t Pack(int format, int a, int r, int g, int b) {
    switch (format) {
    case 7: return static_cast<uint32_t>(((a >> 4) << 12) | ((r >> 4) << 8) | ((g >> 4) << 4) | (b >> 4));
    case 8: return static_cast<uint32_t>(((a >= 128) << 15) | ((r >> 3) << 10) | ((g >> 3) << 5) | (b >> 3));
    default: return static_cast<uint32_t>((a << 24) | (r << 16) | (g << 8) | b);  // 17: 32-bit ARGB
    }
}
bool MakeAlphaTexture(void* engine, int format, int alpha, int attach) {
    if (g_alphaBmp && g_alphaFormat == format && g_alphaA == alpha) return true;
    if (g_alphaBmp) return false;  // one texture per run (no removal path needed for the spike)
    if (!RenderText()) return false;
    g_alphaFormat = format, g_alphaA = alpha;
    g_alphaBmp = g_create(g_tex, g_tex, 1, format);
    if (!g_alphaBmp) return Log("mode4: geBitmap_Create(format %d) failed", format), false;
    void* t = nullptr;
    if (!g_lock(g_alphaBmp, &t, 0, 0, format) || !t) return Log("mode4: lock(format %d) failed", format), false;
    uint32_t info[9] = {};
    g_info(t, info, nullptr);
    void* bits = g_bits(t);
    const bool wide = format == 17;
    for (int y = 0; bits && y < g_tex; ++y)
        for (int x = 0; x < g_tex; ++x) {
            const bool text = (g_dibPx[y * kTexW + x] & 0xFF) > 0x80;
            const bool inside = x < kPanelW && y < kPanelH;
            const uint32_t p = text && inside ? Pack(format, 255, 255, 255, 255)
                               : inside       ? Pack(format, alpha, 16, 20, 24)
                                              : Pack(format, 0, 0, 0, 0);
            if (wide) static_cast<uint32_t*>(bits)[y * info[2] + x] = p;
            else static_cast<uint16_t*>(bits)[y * info[2] + x] = static_cast<uint16_t>(p);
        }
    g_unlock(t);
    // How the bitmap is attached decides its driver handle: the engine list sets
    // driver flags 1 (2D decal), the world list 0x40002 (3D texture).
    int ok = 0;
    if (attach == 1) {
        ok = g_world && g_worldAdd(g_world, g_alphaBmp);
    } else {
        ok = g_add(engine, g_alphaBmp);
        if (attach == 2 && ok) ok = g_setFlags(g_alphaBmp, 0x40003);  // spike only: internal setter
    }
    Log("mode4/6/7: texture %dx%d format %d (locked format %lu, stride %lu), attach %d (world %p), ok %d", g_tex,
        g_tex, format, info[3], info[2], attach, g_world, ok);
    g_alphaSettle = 2;
    return ok != 0;
}

// cost accounting
LARGE_INTEGER g_fq;
double g_usSum = 0, g_usMax = 0;
unsigned long g_frames = 0, g_frameTotal = 0;

// --- mode 9: the real-overlay shape -------------------------------------------------
// A 400x346 panel as 2x2 world-attached 256x256 4444 tiles; the text is re-rendered
// (frame counter on line 0) and re-uploaded every <count> frames; quads are offset by
// half a pixel so texel centres land on pixel centres; tiles are re-added when the
// world changes (level load).
constexpr int kTile = 256;
struct Tile {
    void* bmp;
    void* world;
    int settle;
};
Tile g_tiles[4];
double g_upSum = 0, g_upMax = 0;
unsigned long g_upN = 0;

void RedrawText(unsigned long n) {
    RECT all = {0, 0, kPanelW, kPanelH};
    SetBkColor(g_dc, RGB(0, 0, 0));
    ExtTextOutA(g_dc, 0, 0, ETO_OPAQUE, &all, "", 0, nullptr);
    for (int i = 0; i < 24; ++i) {
        char t[96];
        int len = i == 0 ? wsprintfA(t, "ALPHA PROBE frame %lu  (live upload test)", n)
                         : wsprintfA(t, "ALPHA PROBE line %02d  The quick brown fox 0123456789", i);
        TextOutA(g_dc, 6, 6 + i * 13, t, len);
    }
    GdiFlush();
}

bool FillTile(void* bmp, int tx, int ty, int alpha) {
    void* t = nullptr;
    if (!g_lock(bmp, &t, 0, 0, 7) || !t) return false;
    uint32_t info[9] = {};
    g_info(t, info, nullptr);
    auto* d = static_cast<uint16_t*>(g_bits(t));
    for (int y = 0; d && y < kTile; ++y)
        for (int x = 0; x < kTile; ++x) {
            const int px = tx * kTile + x, py = ty * kTile + y;
            const bool inside = px < kPanelW && py < kPanelH;
            const bool text = inside && (g_dibPx[py * kTexW + px] & 0xFF) > 0x80;
            d[y * info[2] + x] = static_cast<uint16_t>(text ? Pack(7, 255, 255, 255, 255)
                                                       : inside ? Pack(7, alpha, 16, 20, 24) : 0);
        }
    g_unlock(t);
    return d != nullptr;
}

uint32_t HandleOf(void* bmp) {
    uint32_t h = 0;
    SIZE_T got = 0;
    ReadProcessMemory(GetCurrentProcess(), static_cast<uint8_t*>(bmp) + 0xAC, &h, 4, &got);
    return h;
}

int Panel9(void* engine, const Cmd& c, float x0, float y0) {
    if (!g_world || !RenderText()) return -1;
    const bool update = g_frameTotal % static_cast<unsigned long>(c.count) == 0;
    LARGE_INTEGER t0, t1;
    QueryPerformanceCounter(&t0);
    if (update) RedrawText(g_frameTotal);
    for (int i = 0; i < 4; ++i) {
        Tile& t = g_tiles[i];
        if (!t.bmp) {
            t.bmp = g_create(kTile, kTile, 1, 7);
            if (!t.bmp) return Log("mode9: create failed"), -1;
            FillTile(t.bmp, i & 1, i >> 1, c.alpha);
        } else if (update) {
            FillTile(t.bmp, i & 1, i >> 1, c.alpha);
        }
    }
    QueryPerformanceCounter(&t1);
    if (update) {
        const double us = static_cast<double>(t1.QuadPart - t0.QuadPart) * 1e6 / static_cast<double>(g_fq.QuadPart);
        g_upSum += us, ++g_upN;
        if (us > g_upMax) g_upMax = us;
        if (g_upN % 100 == 0)
            Log("mode9 upload: %d us avg, %d us max over %lu uploads (every %d frames)",
                static_cast<int>(g_upSum / g_upN), static_cast<int>(g_upMax), g_upN, c.count);
    }
    for (int i = 0; i < 4; ++i) {
        Tile& t = g_tiles[i];
        if (t.world != g_world) {
            const int ok = g_worldAdd(g_world, t.bmp);
            Log("mode9: tile %d added to world %p (was %p): %d, handle now %08lX", i, g_world, t.world, ok,
                HandleOf(t.bmp));
            t.world = g_world, t.settle = 2;
        }
    }
    g_flush(engine);
    int r = 0;
    for (int i = 0; i < 4; ++i) {
        Tile& t = g_tiles[i];
        if (t.settle > 0) {
            if (--t.settle == 0) Log("mode9: tile %d handle %08lX", i, HandleOf(t.bmp));
            continue;
        }
        if (!HandleOf(t.bmp)) continue;
        const int tx = i & 1, ty = i >> 1;
        const int w = kPanelW - tx * kTile < kTile ? kPanelW - tx * kTile : kTile;
        const int h = kPanelH - ty * kTile < kTile ? kPanelH - ty * kTile : kTile;
        const float qx = x0 + tx * kTile - 0.5f, qy = y0 + ty * kTile - 0.5f;
        TLVertex v[4];
        Quad(v, qx, qy, qx + w, qy + h, 1.0f, 255, 255, 255, 255);
        const float u1 = static_cast<float>(w) / kTile, v1 = static_cast<float>(h) / kTile;
        v[1].u = v[2].u = u1, v[2].v = v[3].v = v1;
        r = g_poly(engine, v, 4, t.bmp, 1u);
    }
    return r;
}

void Draw(void* engine) {
    ++g_frameTotal;
    if (g_cmdSeq != g_seenSeq) {
        EnterCriticalSection(&g_cmdLock);
        g_cur = g_pending;
        g_seenSeq = g_cmdSeq;
        LeaveCriticalSection(&g_cmdLock);
        g_firstLogged = false;
        g_usSum = g_usMax = 0, g_frames = 0;
    }
    const Cmd& c = g_cur;
    if (c.mode == 0 || !engine) return;
    if (!g_alphaBmp) g_tex = c.size;
    int vw, vh;
    ViewSize(&vw, &vh);
    const float x0 = static_cast<float>(vw - 12 - kPanelW), y0 = 12.0f;
    const float z = kZ[c.z & 3], a = static_cast<float>(c.alpha);
    LARGE_INTEGER t0, t1;
    QueryPerformanceCounter(&t0);
    int r = -1;
    if (c.mode == 1 || c.mode == 2 || c.mode == 3 || c.mode == 5) {
        if (c.mode == 1 || c.mode == 3) g_flush(engine);
        TLVertex v[4];
        for (int i = 0; i < c.count; ++i) {
            const float o = static_cast<float>((i % 16) * 4);
            Quad(v, x0 + o, y0 + o, x0 + o + kPanelW, y0 + o + kPanelH, z, 16, 20, 24, a);
            r = g_poly(engine, v, 4, nullptr, c.mode == 2 ? 0u : 1u);
        }
        if (c.mode == 3 && MakeTextDecal(engine)) {
            if (g_textSettle > 0) --g_textSettle;
            else {
                const GeRect src = {0, kPanelW - 1, 0, kPanelH - 1};
                g_draw(engine, g_textBmp, &src, static_cast<uint32_t>(x0), static_cast<uint32_t>(y0));
            }
        }
    } else if (c.mode == 9) {
        r = Panel9(engine, c, x0, y0);
    } else if (c.mode == 4 || c.mode == 6 || c.mode == 7 || c.mode == 8) {
        const int attach = c.mode == 4 ? 0 : c.mode == 7 ? 2 : 1;
        if (MakeAlphaTexture(engine, c.format, c.alpha, attach)) {
            if (g_alphaSettle > 0) --g_alphaSettle;
            else if (!HandleOk(g_alphaBmp)) {
            } else if (c.mode == 8) {  // Genesis's own: geEngine_DrawAlphaBitmap (queued, drawn at the EndFrame flush)
                GeRect px = {static_cast<int32_t>(x0), static_cast<int32_t>(x0) + g_tex - 1, 12, 12 + g_tex - 1};
                for (int i = 0; i < c.count; ++i)
                    r = g_drawAlpha(engine, g_alphaBmp, nullptr, g_camera, &px, nullptr, 255.0f, nullptr);
            } else {
                g_flush(engine);
                TLVertex v[4];
                Quad(v, x0, y0, x0 + g_tex, y0 + g_tex, z, 255, 255, 255, 255);
                for (int i = 0; i < c.count; ++i) r = g_poly(engine, v, 4, g_alphaBmp, 1u);
            }
        }
    }
    QueryPerformanceCounter(&t1);
    const double us = static_cast<double>(t1.QuadPart - t0.QuadPart) * 1e6 / static_cast<double>(g_fq.QuadPart);
    g_usSum += us, ++g_frames;
    if (us > g_usMax) g_usMax = us;
    if (!g_firstLogged) {
        g_firstLogged = true;
        Log("draw: mode %d first frame: RenderPoly returned %d, view %dx%d, %d us", c.mode, r, vw, vh,
            static_cast<int>(us));
    }
    if (g_frames % 300 == 0)
        Log("cost: mode %d count %d: %d.%02d us/frame avg, %d us max over %lu frames", c.mode, c.count,
            static_cast<int>(g_usSum / g_frames), static_cast<int>(g_usSum * 100 / g_frames) % 100,
            static_cast<int>(g_usMax), g_frames);
}

int __cdecl EndFrameDetour(void* engine) {
    Draw(engine);
    return g_endOrig(engine);
}

// geEngine_RenderWorld(engine, world, camera, ...) cdecl: remember the world and camera.
int __cdecl RenderWorldDetour(void* engine, void* world, void* camera, void* a4) {
    if (world != g_world) Log("world %p (camera %p)", world, camera);
    g_world = world, g_camera = camera;
    return g_rwOrig(engine, world, camera, a4);
}

FARPROC Need(HMODULE m, const char* name) {
    FARPROC p = GetProcAddress(m, name);
    if (!p) Log("missing export %s", name);
    return p;
}

DWORD WINAPI Init(LPVOID) {
    HMODULE gen = GetModuleHandleA("Genesis.dll");
    if (!gen) return Log("Genesis.dll not loaded"), 0;
    g_poly = reinterpret_cast<RenderPolyFn>(Need(gen, "@geEngine_RenderPoly@20"));
    g_create = reinterpret_cast<CreateFn>(Need(gen, "@geBitmap_Create@16"));
    g_lock = reinterpret_cast<LockFn>(Need(gen, "@geBitmap_LockForWriteFormat@20"));
    g_unlock = reinterpret_cast<BmpFn>(Need(gen, "@geBitmap_UnLock@4"));
    g_bits = reinterpret_cast<GetBitsFn>(Need(gen, "@geBitmap_GetBits@4"));
    g_info = reinterpret_cast<GetInfoFn>(Need(gen, "@geBitmap_GetInfo@12"));
    g_setck = reinterpret_cast<SetCKFn>(Need(gen, "@geBitmap_SetColorKey@16"));
    g_draw = reinterpret_cast<DrawFn>(Need(gen, "@geEngine_DrawBitmap@20"));
    g_add = reinterpret_cast<EngineBmpFn>(Need(gen, "geEngine_AddBitmap"));
    g_flush = reinterpret_cast<FlushFn>(Need(gen, "geEngine_FlushPipeline"));
    g_worldAdd = reinterpret_cast<EngineBmpFn>(Need(gen, "geWorld_AddBitmap"));
    g_drawAlpha = reinterpret_cast<DrawAlphaFn>(Need(gen, "@geEngine_DrawAlphaBitmap@32"));
    // Not exported: geBitmap's driver-flag setter (spike only, RVA from the AddBitmap disassembly).
    g_setFlags = reinterpret_cast<SetFlagsFn>(reinterpret_cast<uint8_t*>(gen) + 0x171E0);
    auto* end = reinterpret_cast<uint8_t*>(Need(gen, "geEngine_EndFrame"));
    void* rw = reinterpret_cast<void*>(Need(gen, "geEngine_RenderWorld"));
    if (!g_poly || !g_create || !g_lock || !g_unlock || !g_bits || !g_info || !g_setck || !g_draw || !g_add ||
        !g_flush || !end || !rw || !g_worldAdd || !g_drawAlpha)
        return 0;
    // Let FDNYCore hook (and signature-check) geEngine_EndFrame first: wait for its jmp.
    for (int i = 0; i < 150 && *end != 0xE9; ++i) Sleep(100);
    Log("EndFrame entry byte %02X (%s)", *end, *end == 0xE9 ? "already hooked: core present" : "unhooked");
    MH_STATUS s = MH_Initialize();  // this DLL's own MinHook instance
    if (s != MH_OK) return Log("MH_Initialize: %s", MH_StatusToString(s)), 0;
    s = MH_CreateHook(end, reinterpret_cast<void*>(&EndFrameDetour), reinterpret_cast<void**>(&g_endOrig));
    if (s == MH_OK) s = MH_EnableHook(end);
    Log("hook geEngine_EndFrame: %s", MH_StatusToString(s));
    s = MH_CreateHook(rw, reinterpret_cast<void*>(&RenderWorldDetour), reinterpret_cast<void**>(&g_rwOrig));
    if (s == MH_OK) s = MH_EnableHook(rw);
    Log("hook geEngine_RenderWorld: %s", MH_StatusToString(s));
    char env[64];
    Cmd c;
    if (GetEnvironmentVariableA("ALPHAPROBE", env, sizeof(env)) && ParseCmd(env, &c)) Post(c, "env");
    CreateThread(nullptr, 0, Tail, nullptr, 0, nullptr);
    return 0;
}

} // namespace

extern "C" BOOL WINAPI _DllMainCRTStartup(HINSTANCE inst, DWORD reason, LPVOID) {
    if (reason != DLL_PROCESS_ATTACH) return TRUE;
    DisableThreadLibraryCalls(inst);
    InitializeCriticalSection(&g_logLock);
    InitializeCriticalSection(&g_cmdLock);
    QueryPerformanceFrequency(&g_fq);
    GetModuleFileNameA(nullptr, g_dir, MAX_PATH);
    for (int i = lstrlenA(g_dir); i > 0; --i)
        if (g_dir[i - 1] == '\\') {
            g_dir[i] = 0;
            break;
        }
    char path[MAX_PATH];
    wsprintfA(path, "%szz_alphaprobe.log", g_dir);
    g_log = CreateFileA(path, GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS, 0, nullptr);
    Log("alphaprobe loaded");
    CreateThread(nullptr, 0, Init, nullptr, 0, nullptr);
    return TRUE;
}
