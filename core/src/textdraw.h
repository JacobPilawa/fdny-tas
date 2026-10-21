// In-frame text panels (after item 13, owner request "cut it over"): every
// on-screen text of the core (hud_glitch boxes, hud_player, watermark) is drawn
// through here, on the gfx blocks (gfx.h). 0.15: translucent panels (owner: smaller
// font, watermark stays opaque, reusable helpers per rendering type).
//
// Why: geEngine_Printf blits one glyph per character (~29 us per blit through
// D3DDrv/dgVoodoo, area free) and keeps at most 40 strings x 80 chars, so a
// full HUD cost most of the frame. A panel is rendered with GDI into ONE
// Genesis bitmap only when its text changes (at most every `minFrames`
// frames), and each frame costs one geEngine_FlushPipeline + one
// geEngine_DrawBitmap per visible panel. The flush matters: without it the
// engine's cached 3D polys are drawn after (over) the blit inside EndFrame.
// Measured: a 400x346 box at full frame rate (PROGRESS.md, "In-frame text
// investigation").
#pragma once

#include <stdint.h>

namespace fdcore::textdraw {

constexpr int kMaxLines = 40, kLineLen = 160;

// Colour tags (the original collision overlay's palette); attrs may carry a
// background bit. Plain panels use kDefaultTag for every character.
enum Tag : uint8_t {
    T_FRAME, T_TITLE, T_HEADER, T_TEXT, T_DIM, T_ALERT, T_OK, T_WARN, T_WHITE,
    T_SRC_VICTIM, T_SRC_FIRE, T_SRC_HOSE, T_SRC_HOSEROPE, T_SRC_AXE, T_SRC_PLAYER, T_SRC_ENGINE, T_SRC_OTHER,
    T_PL_STANDING, T_PL_CROUCHED, T_PL_CLIMBING, T_PL_OUTCOLD, T_PL_SCRIPTED, T_PL_BUGGED, T_PL_GROUND, T_PL_INAIR,
    T_COUNT
};
constexpr uint8_t BG_WARP = 0x40, BG_FLASH = 0x80, TAG_MASK = 0x3F;

enum Anchor : uint8_t { TOP_LEFT, TOP_RIGHT, BOTTOM_LEFT };
// STYLE_TRANSLUCENT: gfx::AlphaSurface (background at fd_text_alpha) while the frame has a
// world, the opaque decal otherwise (menus). STYLE_OPAQUE: always the decal.
enum Style : uint8_t { STYLE_TRANSLUCENT, STYLE_OPAQUE };

// A panel: anchor + offset (pixels from that corner of the view), how often its
// bitmap may be re-rendered, the tag for lines set without attrs, and its style.
int Create(const char* name, Anchor anchor, int dx, int dy, int minFrames, uint8_t defaultTag,
           Style style = STYLE_TRANSLUCENT);
// New content. Re-rendered (at most every minFrames frames) only if it changed.
void Set(int panel, const char (*lines)[kLineLen], const uint8_t (*attrs)[kLineLen], int n);
void SetLines(int panel, const char* const* lines, int n);  // plain text, the panel's default tag
void Show(int panel, bool visible);

// Registers the frame-end draw. Call after every module that creates panels,
// so their frame-end updates run before it.
bool Init();

} // namespace fdcore::textdraw
