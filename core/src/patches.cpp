#include "patches.h"

#include <windows.h>

#include "addr.h"
#include "log.h"

namespace fdcore::patches {
namespace {

bool g_consoleGate = false;

bool WriteCode(uintptr_t at, const uint8_t* bytes, uint32_t n) {
    DWORD old = 0;
    if (!VirtualProtect(reinterpret_cast<void*>(at), n, PAGE_EXECUTE_READWRITE, &old)) return false;
    for (uint32_t i = 0; i < n; ++i) reinterpret_cast<volatile uint8_t*>(at)[i] = bytes[i];
    VirtualProtect(reinterpret_cast<void*>(at), n, old, &old);
    FlushInstructionCache(GetCurrentProcess(), reinterpret_cast<void*>(at), n);
    return true;
}

} // namespace

void ApplyEarly() {
    // The game creates its developer console only when ImADeveloper is set
    // (JZ at roc_CreateEngineSubsystems+0xA0 skips it). Players need the
    // console, but forcing ImADeveloper would switch on ~16 debug paths, so
    // only this one branch is removed: the game then creates the console
    // itself, at the normal moment, exactly as in developer mode.
    uintptr_t gate = addr::EarlyCodeAddress(addr::Id::patch_console_gate);
    if (!gate) {
        log::Warn("patches: console gate signature not found; console only with ImADeveloper");
        return;
    }
    static const uint8_t kNops[2] = {0x90, 0x90};
    g_consoleGate = WriteCode(gate, kNops, sizeof(kNops));
    if (g_consoleGate) log::Info("patches: console creation ungated at %08lx", static_cast<unsigned long>(gate));
    else log::Error("patches: console gate VirtualProtect failed (%lu)", GetLastError());
}

bool ConsoleGatePatched() { return g_consoleGate; }

} // namespace fdcore::patches
