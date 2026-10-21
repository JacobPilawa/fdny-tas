// window module (backlog item 7): replaces the game's per-frame cursor warp
// (alttabfix.asi parity) so alt-tabbing out no longer traps the mouse.
#pragma once

namespace fdcore::window {

// DllMain (before WinMain): patch the cursor-warp call site. False if the
// site's bytes don't match (another mod such as alttabfix.asi patched it).
bool ApplyEarlyPatch();

// After the address table and console are up: registers fd_window_cursor.
void Init();

} // namespace fdcore::window
