// hud_glitch boxes on screen (backlog item 11d), shown the way the original
// tools/collision_overlay showed them: a click-through, never-activating
// overlay window over the game (0.88 alpha, the original's colours,
// monospace font), owned by the game window and placed at its top-right.
// Rendered with GDI on the core's own UI thread, so the game thread only
// builds the text.
//
// Why not inside the frame: geEngine_Printf draws one quad per character
// (~2000 characters cost ~75% of the frame rate), and Genesis bitmap decals
// (geEngine_DrawBitmap) attached and returned TRUE but never showed through
// this D3DDrv/dgVoodoo setup (see PROGRESS.md item 11d).
// Like the original it needs the game windowed or dgVoodoo fake-fullscreen.
#pragma once

#include <windows.h>
#include <stdint.h>

#include "glitch_analysis.h"

namespace fdcore::glitch::draw {

bool Init();  // starts nothing yet; the UI thread starts on the first Update
// Per frame from the game thread: the box content (only re-rendered when it
// changed), whether it is shown, and the game window (from GetActiveWindow).
void Update(const char (*lines)[analysis::kBoxLineLen], const uint8_t (*attrs)[analysis::kBoxLineLen], int n,
            bool visible, HWND game);
void Hide();  // hooks removed

} // namespace fdcore::glitch::draw
