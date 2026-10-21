// Key binds: `bind <key> <command>` runs a console line when a key is pressed
// in-game (console closed, game window focused). Game keys aren't swallowed.
#pragma once

namespace fdcore::binds {

void Init();  // registers bind / unbind / unbindall / bindlist, subscribes to frame_begin

// Appends `bind <key> "<line>"` lines for FDNYCore.cfg; returns chars written.
int AppendConfig(char* out, int cap);

// True when in-game keys should act: the game window has focus and the console
// is closed (the rule binds use; also used by the hud_glitch F9/F10/F12 keys).
bool KeysLive();

} // namespace fdcore::binds
