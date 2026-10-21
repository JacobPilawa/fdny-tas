// In-frame HUDs drawn with Genesis' own geEngine_Printf (backlog item 10:
// hud_player = the old Python player overlay, in-game).
#pragma once

namespace fdcore::hud {

bool Init();  // after state::Init (draws that frame's snapshot)

// Debug pipe / tests: the lines drawn on the last frame, "frame=N|line|line...".
int LastDrawn(char* out, int cap);

} // namespace fdcore::hud
