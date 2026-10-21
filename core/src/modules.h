// Module registry. A module declares the address-table entries it needs; the
// core enables it only if the table version is known and every one of those
// entries' signatures verified. Modules never touch raw addresses.
#pragma once

#include "addr.h"

namespace fdcore::modules {

struct Module {
    const char* name;
    const addr::Id* requires;
    int nrequires;
    void (*start)();  // called once if enabled
    bool enabled;
};

// Decides enabled/disabled for every registered module (logs each verdict),
// then starts the enabled ones.
void StartAll();

} // namespace fdcore::modules
