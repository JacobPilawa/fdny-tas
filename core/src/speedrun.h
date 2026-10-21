// speedrun module (backlog item 8): static victim spawns + RescueBot off
// (port of asi/FDNYDeterministic).
#pragma once

namespace fdcore::speedrun {

// DllMain (before WinMain): install the victim-spawn trampoline.
bool ApplyEarlyPatch(const char* gameDir);

// After the address table and console are up: fd_static_spawns, fd_spawns, fd_rescuebot.
void Init();

} // namespace fdcore::speedrun
