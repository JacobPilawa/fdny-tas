// In-memory byte patches on the exe (never on disk). Each patch site is an
// address-table code entry and is written only if its signature matches.
#pragma once

namespace fdcore::patches {

// Runs in DllMain, before WinMain: patches that must precede game startup.
void ApplyEarly();

bool ConsoleGatePatched();

// DllMain: fd_skip_intro, peeked from FDNYCore.cfg / autoexec.cfg.
void ApplyIntroSkip(const char* gameDir);

// Registers fd_skip_intro with the console (after cmd builtins).
void Init();

} // namespace fdcore::patches
