// hud_glitch (backlog item 11): port of tools/collision_overlay.
// 11a = the record layer (collision hooks + ring buffer, identical records).
#pragma once

namespace fdcore::glitch {

bool Init();       // registers fd_hud_glitch (hooks install when it is set to 1)
bool Installed();

// Debug pipe / tests: "installed=.. tick=.. trace=.. commit=.. engine_ray=.. dropped=..".
int Stats(char* out, int cap);

} // namespace fdcore::glitch
