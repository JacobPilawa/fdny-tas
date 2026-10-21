// In-memory byte patches on the exe (never on disk). Each patch site is an
// address-table code entry and is written only if its signature matches.
#pragma once

namespace fdcore::patches {

// Runs in DllMain, before WinMain: patches that must precede game startup.
void ApplyEarly();

bool ConsoleGatePatched();

} // namespace fdcore::patches
