// Key binds: `bind <key> <command>` runs a console line when a key is pressed
// in-game (console closed, game window focused). Game keys aren't swallowed.
#pragma once

namespace fdcore::binds {

void Init();  // registers bind / unbind / unbindall / bindlist, subscribes to frame_begin

// Appends `bind <key> "<line>"` lines for FDNYCore.cfg; returns chars written.
int AppendConfig(char* out, int cap);

} // namespace fdcore::binds
