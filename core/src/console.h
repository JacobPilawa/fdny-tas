// FDNYCore's console = the game's own developer console (main\console.cpp):
// the core makes sure it exists, prints into it, and intercepts submitted
// lines (ScriptEngine::ExecLine) so registered commands/variables run here
// and everything else still goes to the game's script engine.
#pragma once

namespace fdcore::console {

// Hooks ExecLine, registers builtins, subscribes to frame_end.
bool Init(const char* gameDir, const char* coreVersion);

// Prints one line into the game console (game thread only) and mirrors it
// to FDNYCore.log as "console> ...".
void Print(const char* fmt, ...);

const char* GameDir();      // with trailing backslash
// A path from a setting: absolute (X:..., \..., /...) as given, otherwise under GameDir().
// False if it doesn't fit in cap.
bool GamePath(const char* path, char* out, int cap);
const char* CoreVersion();

} // namespace fdcore::console
