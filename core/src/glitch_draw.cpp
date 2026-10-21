#include "glitch_draw.h"

#include "log.h"

namespace fdcore::glitch::draw {
namespace {

using analysis::kBoxLineLen;
using analysis::kBoxLines;

constexpr int kCols = 65;            // W_INNER + 4
constexpr int kPad = 4;              // the Text widget's padx/pady
constexpr int kMargin = 12;          // MARGIN_PX
constexpr BYTE kAlpha = 224;         // OVERLAY_ALPHA 0.88
constexpr UINT WM_BOX = WM_APP + 1;  // new content and/or visibility

// The original overlay's colours (COLORS / SOURCE_COLORS / PLAYER_COLORS), by tag.
constexpr COLORREF Hex(uint32_t rgb) { return RGB((rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF); }
const COLORREF kFg[analysis::T_COUNT] = {
    Hex(0x7c8894), Hex(0xd8e0e8), Hex(0x8a96a2), Hex(0xe8e8e8), Hex(0x5c646c), Hex(0xff3b3b), Hex(0x7fd17f),
    Hex(0xffcc4d), Hex(0xffffff),
    Hex(0xffa040), Hex(0xff5a5a), Hex(0x5aaaff), Hex(0x3f7fbf), Hex(0xffd84a), Hex(0xd0d0d0), Hex(0x8a8a8a),
    Hex(0xb48cff),
    Hex(0x7fd17f), Hex(0x5ad1d1), Hex(0x5aaaff), Hex(0xff5a5a), Hex(0xb48cff), Hex(0xff8c1a), Hex(0x7fd17f),
    Hex(0xffcc4d),
};
const COLORREF kBg = Hex(0x101418), kWarpBg = Hex(0x5a1010), kFlashBg = Hex(0xb02020);

bool Bold(uint8_t a) {  // title / alert / warp / flash / Bugged are bold in the original
    const uint8_t t = a & analysis::TAG_MASK;
    return t == analysis::T_TITLE || t == analysis::T_ALERT || t == analysis::T_PL_BUGGED ||
           (a & (analysis::BG_WARP | analysis::BG_FLASH));
}

// --- shared: the game thread writes, the UI thread reads (g_lock) -------------------
CRITICAL_SECTION g_lock;
char g_text[kBoxLines][kBoxLineLen];
uint8_t g_attr[kBoxLines][kBoxLineLen];
int g_n = 0;
bool g_visible = false, g_posted = false;
HWND g_game = nullptr;
HWND volatile g_wnd = nullptr;

// --- game thread: what was last handed over (unchanged frames post nothing) ---------
char g_lastText[kBoxLines][kBoxLineLen];
uint8_t g_lastAttr[kBoxLines][kBoxLineLen];
int g_lastN = -1;
bool g_lastVisible = false;
HANDLE g_thread = nullptr;

// --- UI thread ------------------------------------------------------------------------
HDC g_dc;
HFONT g_font, g_bold;
HBITMAP g_dib;
uint32_t* g_pixels;
int g_cw = 6, g_ch = 13, g_w = 0, g_h = 0, g_rows = 0;
char g_rText[kBoxLines][kBoxLineLen];
uint8_t g_rAttr[kBoxLines][kBoxLineLen];
int g_rN = 0;

void Fonts() {
    g_dc = CreateCompatibleDC(nullptr);
    g_font = CreateFontA(-11, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                         NONANTIALIASED_QUALITY, FIXED_PITCH | FF_MODERN, "Consolas");
    g_bold = CreateFontA(-11, 0, 0, 0, FW_BOLD, 0, 0, 0, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                         NONANTIALIASED_QUALITY, FIXED_PITCH | FF_MODERN, "Consolas");
    SelectObject(g_dc, g_font);
    TEXTMETRICA tm;
    GetTextMetricsA(g_dc, &tm);
    g_cw = tm.tmAveCharWidth, g_ch = tm.tmHeight;
}

// Render the box into the DIB: runs of equal tag, opaque cells (exact columns).
void Render() {
    const int rows = g_rN;
    if (rows != g_rows || !g_dib) {  // as tall as the lines (render(): height=len(lines))
        if (g_dib) DeleteObject(g_dib);
        g_rows = rows;
        g_w = kCols * g_cw + 2 * kPad, g_h = rows * g_ch + 2 * kPad;
        BITMAPINFO bi{};
        bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
        bi.bmiHeader.biWidth = g_w, bi.bmiHeader.biHeight = -g_h;  // top-down
        bi.bmiHeader.biPlanes = 1, bi.bmiHeader.biBitCount = 32, bi.bmiHeader.biCompression = BI_RGB;
        g_dib = CreateDIBSection(g_dc, &bi, DIB_RGB_COLORS, reinterpret_cast<void**>(&g_pixels), nullptr, 0);
        if (!g_dib) return;
        SelectObject(g_dc, g_dib);
    }
    RECT all = {0, 0, g_w, g_h};
    SetBkColor(g_dc, kBg);
    ExtTextOutA(g_dc, 0, 0, ETO_OPAQUE, &all, "", 0, nullptr);
    static INT dx[kBoxLineLen];  // force the exact cell grid (bold glyphs too)
    for (int i = 0; i < kBoxLineLen; ++i) dx[i] = g_cw;
    for (int r = 0; r < rows; ++r) {
        const int len = lstrlenA(g_rText[r]);
        for (int c = 0; c < len;) {
            const uint8_t a = g_rAttr[r][c];
            int e = c + 1;
            while (e < len && g_rAttr[r][e] == a) ++e;
            SelectObject(g_dc, Bold(a) ? g_bold : g_font);
            SetTextColor(g_dc, kFg[(a & analysis::TAG_MASK) < analysis::T_COUNT ? (a & analysis::TAG_MASK) : 0]);
            SetBkColor(g_dc, (a & analysis::BG_FLASH) ? kFlashBg : (a & analysis::BG_WARP) ? kWarpBg : kBg);
            RECT rc = {kPad + c * g_cw, kPad + r * g_ch, kPad + e * g_cw, kPad + (r + 1) * g_ch};
            ExtTextOutA(g_dc, rc.left, rc.top, ETO_OPAQUE | ETO_CLIPPED, &rc, g_rText[r] + c, e - c, dx);
            c = e;
        }
    }
    GdiFlush();
    const int px = g_w * g_h;  // GDI leaves alpha 0; per-pixel opaque, the constant alpha gives the 0.88
    for (int i = 0; i < px; ++i) g_pixels[i] |= 0xFF000000u;
}

// Top-right of the game's client area (the original's default corner "tr").
bool Place(HWND game, POINT* pt) {
    RECT rc;
    if (!game || !IsWindow(game) || IsIconic(game) || !GetClientRect(game, &rc)) return false;
    POINT tr = {rc.right, rc.top};
    ClientToScreen(game, &tr);
    pt->x = tr.x - kMargin - g_w, pt->y = tr.y + kMargin;
    return true;
}

void Present(HWND wnd, HWND game) {
    POINT pt;
    if (!g_dib || !Place(game, &pt)) {
        ShowWindow(wnd, SW_HIDE);
        return;
    }
    SIZE size = {g_w, g_h};
    POINT src = {0, 0};
    BLENDFUNCTION bf = {AC_SRC_OVER, 0, kAlpha, AC_SRC_ALPHA};
    HDC screen = GetDC(nullptr);
    UpdateLayeredWindow(wnd, screen, &pt, &size, g_dc, &src, 0, &bf, ULW_ALPHA);
    ReleaseDC(nullptr, screen);
    if (!IsWindowVisible(wnd)) ShowWindow(wnd, SW_SHOWNOACTIVATE);
}

LRESULT CALLBACK WndProc(HWND wnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_BOX || msg == WM_TIMER) {
        EnterCriticalSection(&g_lock);
        const bool visible = g_visible;
        HWND game = g_game;
        const bool fresh = msg == WM_BOX && g_n > 0;
        if (fresh) {
            for (int i = 0; i < g_n; ++i) lstrcpynA(g_rText[i], g_text[i], kBoxLineLen);
            __builtin_memcpy(g_rAttr, g_attr, sizeof(g_rAttr));
            g_rN = g_n;
        }
        if (msg == WM_BOX) g_posted = false;
        LeaveCriticalSection(&g_lock);
        if (!visible || !g_rN) {
            ShowWindow(wnd, SW_HIDE);
            return 0;
        }
        if (fresh) Render();
        Present(wnd, game);  // the timer keeps it on the game window when that moves
        return 0;
    }
    if (msg == WM_MOUSEACTIVATE) return MA_NOACTIVATE;
    return DefWindowProcA(wnd, msg, wp, lp);
}

DWORD WINAPI UiThread(void*) {
    Fonts();
    WNDCLASSA wc{};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = GetModuleHandleA(nullptr);
    wc.lpszClassName = "FDNYCoreGlitchBox";
    RegisterClassA(&wc);
    EnterCriticalSection(&g_lock);
    HWND owner = g_game;
    LeaveCriticalSection(&g_lock);
    // Never take focus: focusing another window makes FDNY pause its mission
    // clock (GameClock::Pause on WM_ACTIVATEAPP) -- the original's rule.
    // Owned by the game window: above it, minimised with it.
    HWND wnd = CreateWindowExA(WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
                               wc.lpszClassName, "FDNY collision", WS_POPUP, 0, 0, 1, 1, owner, nullptr,
                               wc.hInstance, nullptr);
    if (!wnd) {
        log::Warn("glitch: could not create the box window (%lu)", GetLastError());
        return 0;
    }
    g_wnd = wnd;
    SetTimer(wnd, 1, 100, nullptr);
    PostMessageA(wnd, WM_BOX, 0, 0);
    MSG m;
    while (GetMessageA(&m, nullptr, 0, 0) > 0) DispatchMessageA(&m);
    return 0;
}

void Post() {
    if (!g_thread) g_thread = CreateThread(nullptr, 0, UiThread, nullptr, 0, nullptr);
    else if (g_wnd) PostMessageA(g_wnd, WM_BOX, 0, 0);
}

} // namespace

bool Init() {
    InitializeCriticalSection(&g_lock);
    return true;
}

void Update(const char (*lines)[kBoxLineLen], const uint8_t (*attrs)[kBoxLineLen], int n, bool visible, HWND game) {
    bool changed = visible != g_lastVisible;
    if (n >= 0) {
        if (n > kBoxLines) n = kBoxLines;
        bool same = n == g_lastN;
        for (int i = 0; same && i < n; ++i)
            same = lstrcmpA(lines[i], g_lastText[i]) == 0 &&
                   __builtin_memcmp(attrs[i], g_lastAttr[i], lstrlenA(lines[i])) == 0;
        if (!same) {  // Overlay.render: redraw only when the lines changed
            for (int i = 0; i < n; ++i) {
                lstrcpynA(g_lastText[i], lines[i], kBoxLineLen);
                __builtin_memcpy(g_lastAttr[i], attrs[i], kBoxLineLen);
            }
            g_lastN = n;
            changed = true;
        }
    }
    if (!changed) return;
    g_lastVisible = visible;
    EnterCriticalSection(&g_lock);
    if (g_lastN > 0) {
        for (int i = 0; i < g_lastN; ++i) lstrcpynA(g_text[i], g_lastText[i], kBoxLineLen);
        __builtin_memcpy(g_attr, g_lastAttr, sizeof(g_attr));
        g_n = g_lastN;
    }
    g_visible = visible;
    if (game) g_game = game;
    const bool post = !g_posted;  // one pending message is enough: it reads the latest
    g_posted = true;
    LeaveCriticalSection(&g_lock);
    if (post || !g_thread) Post();
}

void Hide() {
    EnterCriticalSection(&g_lock);
    g_visible = false;
    LeaveCriticalSection(&g_lock);
    g_lastVisible = false;
    if (g_wnd) PostMessageA(g_wnd, WM_BOX, 0, 0);
}

} // namespace fdcore::glitch::draw
