// Module (feature) registry: the "plugin" boundary of the core.
//
// The core (src/) provides services: address table, hooks + frame/level events,
// console / variables / binds, logging, camera, gfx / textdraw, player state,
// profile gates. Features (src/features/) only use those services; the core
// never names a feature. A feature file registers itself with FDCORE_MODULE and
// is picked up from the link: delete the file from the build and the core still
// compiles and runs without it.
//
// A module declares the address-table entries it needs; it is enabled only if
// the table version is known and every one of those entries' signatures
// verified. Modules never touch raw addresses.
//
// Self-registration without static constructors (no CRT runs them): each
// FDCORE_MODULE puts a pointer into the COFF section ".fdmod$m"; the linker
// merges ".fdmod$a" < "$m" < "$z" in name order, and the core walks the
// pointers between its two markers.
#pragma once

#include "addr.h"

namespace fdcore::modules {

struct Module {
    const char* name;
    const addr::Id* requires;  // may be null
    int nrequires;
    // DllMain, before WinMain (startup patches); the table isn't verified yet,
    // so it must guard itself (addr::EarlyCodeAddress). May be null.
    void (*early)(const char* gameDir);
    // Init thread, after the core services. False = it disabled itself (it logs why).
    bool (*init)();
    int order;     // lower first; ties in link order
    bool enabled;  // set by StartAll
};

// DllMain: every module's early step, in order.
void RunEarly(const char* gameDir);
// Decides enabled/disabled for every module (logs each verdict as
// "module <name>: ..."), then initialises the enabled ones. servicesOk = the core
// services came up (known version, hooks installed).
void StartAll(bool servicesOk);

int Count();
const Module* At(int i);  // in start order

} // namespace fdcore::modules

#define FDCORE_MODULE_CAT2(a, b) a##b
#define FDCORE_MODULE_CAT(a, b) FDCORE_MODULE_CAT2(a, b)
// At namespace scope (outside any namespace), once per feature file:
//   FDCORE_MODULE(fov, "fov", nullptr, 0, nullptr, &fdcore::fov::Init, 10)
#define FDCORE_MODULE(id, name, requires, nrequires, early, init, order)                                       \
    static fdcore::modules::Module FDCORE_MODULE_CAT(fdmod_, id) = {name, requires, nrequires, early, init, order, \
                                                                     false};                                       \
    extern "C" __attribute__((used, section(".fdmod$m")))                                                          \
    fdcore::modules::Module* const FDCORE_MODULE_CAT(fdmod_reg_, id) = &FDCORE_MODULE_CAT(fdmod_, id);
