#include "textdraw.h"

#include <windows.h>

#include "cmd.h"
#include "console.h"
#include "gfx.h"
#include "hooks.h"
#include "log.h"

namespace fdcore::textdraw {
namespace {

constexpr int kPad = 4;  // around the text, like the original Text widget

// The original collision overlay's colours (COLORS / SOURCE_COLORS / PLAYER_COLORS), by tag.
const uint32_t kFg[T_COUNT] = {
    0x7c8894, 0xd8e0e8, 0x8a96a2, 0xe8e8e8, 0x5c646c, 0xff3b3b, 0x7fd17f, 0xffcc4d, 0xffffff,
    0xffa040, 0xff5a5a, 0x5aaaff, 0x3f7fbf, 0xffd84a, 0xd0d0d0, 0x8a8a8a, 0xb48cff,
    0x7fd17f, 0x5ad1d1, 0x5aaaff, 0xff5a5a, 0xb48cff, 0xff8c1a, 0x7fd17f, 0xffcc4d,
};
constexpr uint32_t kBg = 0x101418, kWarpBg = 0x5a1010, kFlashBg = 0xb02020;
constexpr uint8_t kAlertAlpha = 230;  // warp / flash backgrounds stay nearly opaque

bool Bold(uint8_t a) {  // title / alert / warp / flash / Bugged are bold in the original
    const uint8_t t = a & TAG_MASK;
    return t == T_TITLE || t == T_ALERT || t == T_PL_BUGGED || (a & (BG_WARP | BG_FLASH));
}

void OnAlpha(cmd::Cvar&);
cmd::Cvar g_fdTextAlpha = {"fd_text_alpha", "160",
                           "panel background opacity 0-255 (translucent panels; 255 = solid)", false, true, OnAlpha,
                           {0}};

struct Panel {
    char name[24];
    Anchor anchor;
    Style style;
    int dx, dy, minFrames;
    uint8_t defTag;
    bool visible;
    char text[kMaxLines][kLineLen];
    uint8_t attr[kMaxLines][kLineLen];
    int n;
    bool dirty;
    unsigned long lastRender;
    gfx::Canvas canvas;
    gfx::AlphaSurface surf;
    gfx::Decal decal;
    bool rendered;      // the canvas holds the current text
    bool alphaReady;    // surf holds it
    bool decalStale;    // decal doesn't (translucent panels upload it only for the fallback)
    bool translucent;   // how it was drawn last frame (fd_textdraw, logged on change)
    int x, y;           // last draw position
    bool reported, failed;
};
constexpr int kMaxPanels = 8;
Panel g_panels[kMaxPanels];
int g_nPanels = 0;
unsigned long g_frame = 0;
unsigned g_fontGen = 0;

void DirtyAll() {
    for (int i = 0; i < g_nPanels; ++i) g_panels[i].dirty = true;
}
void OnAlpha(cmd::Cvar&) { DirtyAll(); }

int Alpha() {
    int v = 0;
    for (const char* s = g_fdTextAlpha.value; *s >= '0' && *s <= '9'; ++s) v = v * 10 + (*s - '0');
    return v > 255 ? 255 : v;
}

// Runs of equal tag, opaque cells on the exact column grid.
bool Render(Panel& p) {
    int cols = 0;
    for (int i = 0; i < p.n; ++i) {
        const int len = lstrlenA(p.text[i]);
        if (len > cols) cols = len;
    }
    if (!p.canvas.Begin(cols, p.n, kPad, kBg)) return false;
    for (int r = 0; r < p.n; ++r) {
        const int len = lstrlenA(p.text[r]);
        for (int c = 0; c < len;) {
            const uint8_t a = p.attr[r][c];
            int e = c + 1;
            while (e < len && p.attr[r][e] == a) ++e;
            const uint8_t tag = (a & TAG_MASK) < T_COUNT ? (a & TAG_MASK) : T_TEXT;
            p.canvas.Text(c, r, p.text[r] + c, e - c, kFg[tag], (a & BG_FLASH) ? kFlashBg : (a & BG_WARP) ? kWarpBg : kBg,
                          Bold(a));
            c = e;
        }
    }
    p.canvas.Finish();
    return true;
}

void Fail(Panel& p, const char* what) {  // once per panel, not every frame
    if (p.failed) return;
    p.failed = true;
    log::Warn("textdraw: %s: %s (%dx%d)", p.name, what, p.canvas.Width(), p.canvas.Height());
}

void Draw() {
    ++g_frame;
    if (!gfx::Ready() || !gfx::Engine()) return;
    if (const unsigned gen = gfx::FontGeneration(); gen != g_fontGen) g_fontGen = gen, DirtyAll();
    int vw, vh;
    gfx::ViewSize(&vw, &vh);
    const uint8_t alpha = static_cast<uint8_t>(Alpha());
    const gfx::AlphaKey keys[] = {{kBg, alpha},
                                  {kWarpBg, alpha > kAlertAlpha ? alpha : kAlertAlpha},
                                  {kFlashBg, alpha > kAlertAlpha ? alpha : kAlertAlpha}};
    for (int i = 0; i < g_nPanels; ++i) {
        Panel& p = g_panels[i];
        if (!p.visible || p.n <= 0) continue;
        if (p.dirty && (!p.rendered || g_frame - p.lastRender >= static_cast<unsigned long>(p.minFrames))) {
            if (!Render(p)) {
                Fail(p, "GDI could not render it");
                continue;
            }
            p.rendered = true, p.dirty = false, p.lastRender = g_frame;
            const uint32_t* px = p.canvas.Pixels();
            const int stride = p.canvas.Stride(), w = p.canvas.Width(), h = p.canvas.Height();
            p.alphaReady = p.style == STYLE_TRANSLUCENT && p.surf.Upload(px, stride, w, h, keys, 3);
            p.decalStale = true;
            if (!p.alphaReady && !p.decal.Upload(px, stride, w, h)) Fail(p, "could not create / upload its bitmap");
            else if (!p.alphaReady) p.decalStale = false;
        }
        if (!p.rendered) continue;
        const int w = p.canvas.Width(), h = p.canvas.Height();
        int x = p.dx, y = p.dy;
        if (p.anchor == TOP_RIGHT) x = vw - p.dx - w;
        if (p.anchor == BOTTOM_LEFT) y = vh - p.dy - h;
        if (x < 0) x = 0;
        if (y < 0) y = 0;
        p.x = x, p.y = y;
        // Translucent when the frame has a world; otherwise the opaque decal (menus).
        bool translucent = p.alphaReady && p.surf.Draw(x, y);
        if (!translucent) {
            if (p.decalStale && p.decal.Upload(p.canvas.Pixels(), p.canvas.Stride(), w, h)) p.decalStale = false;
            p.decal.Draw(x, y);
        }
        if (!p.reported || translucent != p.translucent) {
            p.reported = true;
            log::Info("textdraw: %s: %dx%d at %d,%d (view %dx%d), %s", p.name, w, h, x, y, vw, vh,
                      translucent ? "translucent" : "opaque");
        }
        p.translucent = translucent;
    }
}

void CmdInfo(int, const char* const*) {
    int vw, vh, cw, ch;
    gfx::ViewSize(&vw, &vh);
    gfx::CellSize(&cw, &ch);
    console::Print("textdraw: view %dx%d, cell %dx%d, fd_text_alpha %d", vw, vh, cw, ch, Alpha());
    for (int i = 0; i < g_nPanels; ++i) {
        const Panel& p = g_panels[i];
        console::Print("  %s: %s, %d line(s), %dx%d at %d,%d, %s", p.name, p.visible ? "shown" : "hidden", p.n,
                       p.canvas.Width(), p.canvas.Height(), p.x, p.y,
                       !p.rendered ? "not drawn yet" : p.translucent ? "translucent" : "opaque");
    }
}

} // namespace

int Create(const char* name, Anchor anchor, int dx, int dy, int minFrames, uint8_t defaultTag, Style style) {
    if (g_nPanels >= kMaxPanels) return -1;
    Panel& p = g_panels[g_nPanels];
    lstrcpynA(p.name, name, sizeof(p.name));
    p.anchor = anchor, p.style = style, p.dx = dx, p.dy = dy, p.minFrames = minFrames < 1 ? 1 : minFrames;
    p.defTag = defaultTag;
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
    if (!gfx::Init()) return false;
    cmd::RegisterCvar(&g_fdTextAlpha);
    cmd::RegisterCommand("fd_textdraw", "list the on-screen text panels: shown, size, position, translucent/opaque",
                         CmdInfo);
    hooks::OnFrameEnd(Draw);
    log::Info("textdraw: ready (%d panel(s))", g_nPanels);
    return true;
}

} // namespace fdcore::textdraw
