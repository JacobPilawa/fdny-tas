// First-chance crash reporter (vectored exception handler); log only, never
// changes how the game handles the exception.
#pragma once

namespace fdcore::crashlog {
void Install();
}
