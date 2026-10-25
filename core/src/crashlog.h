// First-chance crash reporter (vectored exception handler); log only, never
// changes how the game handles the exception.
#pragma once

namespace fdcore::crashlog {
void Install();
#ifdef FDCORE_DEBUG
// Test hook (tests/crashlog.yaml): an access violation Windows catches itself (IsBadReadPtr
// on an unmapped address), so the handler reports it and the game carries on.
void DebugFault();
#endif
}
