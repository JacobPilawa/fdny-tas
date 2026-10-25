#include "entities.h"

#include <windows.h>

#include "addr.h"
#include "hooks.h"
#include "log.h"

namespace fdcore::entities {
namespace {
using Method = uintptr_t(__fastcall*)(void*, void*);
using Destroy = uintptr_t(__fastcall*)(void*, void*, unsigned);
struct Listener { EntityFn init, update, destroy; };
constexpr int kMaxListeners = 8;
Listener g_players[kMaxListeners], g_victims[kMaxListeners];
int g_nPlayers, g_nVictims;
bool g_playerReady, g_victimReady, g_playerTried, g_victimTried;
Method g_playerInit, g_playerUpdate, g_victimSpawn, g_victimUpdate;
Destroy g_victimDestroy;

void Notify(Listener* listeners, int count, int event, void* entity) {
    // Observers may serialize floats or call Win32. Preserve the state left by
    // the game callback, including x87 registers/control and SSE/MXCSR.
    unsigned char fp[512] __attribute__((aligned(16)));
    __asm__ volatile("fxsave %0" : "=m"(fp));
    DWORD error = GetLastError();
    for (int i = 0; i < count; ++i) {
        EntityFn fn = event == 0 ? listeners[i].init : event == 1 ? listeners[i].update : listeners[i].destroy;
        if (fn) fn(entity);
    }
    SetLastError(error);
    __asm__ volatile("fxrstor %0" : : "m"(fp));
}

uintptr_t __fastcall PlayerInit(void* p, void* edx) {
    uintptr_t result = g_playerInit(p, edx);
    Notify(g_players, g_nPlayers, 0, p);
    return result;
}
uintptr_t __fastcall PlayerUpdate(void* p, void* edx) {
    uintptr_t result = g_playerUpdate(p, edx);
    Notify(g_players, g_nPlayers, 1, p);
    return result;
}
uintptr_t __fastcall VictimSpawn(void* p, void* edx) {
    uintptr_t result = g_victimSpawn(p, edx);
    Notify(g_victims, g_nVictims, 0, p);
    return result;
}
uintptr_t __fastcall VictimUpdate(void* p, void* edx) {
    uintptr_t result = g_victimUpdate(p, edx);
    Notify(g_victims, g_nVictims, 1, p);
    return result;
}
uintptr_t __fastcall VictimDestroy(void* p, void* edx, unsigned flags) {
    Notify(g_victims, g_nVictims, 2, p);
    return g_victimDestroy(p, edx, flags);
}

// Spot filter: SpawnAtSpot has copied the spot to the victim and, at
// patch_victim_spawn, does `mov ecx,ebp; call SnapToFloor` (EBP = victim). Those
// 7 bytes become a jmp to a trampoline that calls the filter, then runs them.
EntityFn g_spotFilter = nullptr;
uint8_t* g_spotTramp = nullptr;

extern "C" void __cdecl FdcoreVictimSpot(void* victim) { g_spotFilter(victim); }
} // namespace

bool FilterVictimSpot(EntityFn filter) {
    if (g_spotFilter || !filter) return false;
    uintptr_t site = addr::EarlyCodeAddress(addr::Id::patch_victim_spawn);
    uintptr_t snap = addr::EarlyCodeAddress(addr::Id::game_snap_to_floor);
    if (!site || !snap) {
        log::Warn("entities: victim-spawn site doesn't match (already hooked by another mod, e.g. "
                  "FDNYDeterministic.asi?); no spot filter");
        return false;
    }
    g_spotTramp = static_cast<uint8_t*>(VirtualAlloc(nullptr, 0x40, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
    if (!g_spotTramp) return false;
    g_spotFilter = filter;  // before the patch makes the trampoline reachable
    uint8_t* t = g_spotTramp;
    int k = 0;
    auto put4 = [&](uint32_t v) {
        for (int i = 0; i < 4; ++i) t[k++] = static_cast<uint8_t>(v >> (8 * i));
    };
    t[k++] = 0x9C;  // pushfd
    t[k++] = 0x60;  // pushad
    t[k++] = 0x55;  // push ebp (victim this)
    t[k++] = 0xE8;  // call FdcoreVictimSpot
    put4(reinterpret_cast<uintptr_t>(&FdcoreVictimSpot) - (reinterpret_cast<uintptr_t>(t) + k + 4));
    t[k++] = 0x83, t[k++] = 0xC4, t[k++] = 0x04;  // add esp,4 (cdecl)
    t[k++] = 0x61;                                // popad
    t[k++] = 0x9D;                                // popfd
    t[k++] = 0x8B, t[k++] = 0xCD;                 // mov ecx,ebp   (displaced)
    t[k++] = 0xE8;                                // call SnapToFloor (displaced)
    put4(snap - (reinterpret_cast<uintptr_t>(t) + k + 4));
    t[k++] = 0xE9;  // jmp back after the 7 displaced bytes
    put4(site + 7 - (reinterpret_cast<uintptr_t>(t) + k + 4));

    uint8_t patch[7] = {0xE9, 0, 0, 0, 0, 0x90, 0x90};
    uint32_t rel = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(g_spotTramp) - (site + 5));
    for (int i = 0; i < 4; ++i) patch[1 + i] = static_cast<uint8_t>(rel >> (8 * i));
    DWORD old = 0;
    if (!VirtualProtect(reinterpret_cast<void*>(site), 7, PAGE_EXECUTE_READWRITE, &old)) {
        g_spotFilter = nullptr;
        return false;
    }
    for (int i = 0; i < 7; ++i) reinterpret_cast<volatile uint8_t*>(site)[i] = patch[i];
    VirtualProtect(reinterpret_cast<void*>(site), 7, old, &old);
    FlushInstructionCache(GetCurrentProcess(), reinterpret_cast<void*>(site), 7);
    log::Info("entities: victim-spot filter at %08lx -> trampoline %08lx", static_cast<unsigned long>(site),
              reinterpret_cast<unsigned long>(g_spotTramp));
    return true;
}

bool ObservePlayer(EntityFn initialized, EntityFn updated) {
    if (g_nPlayers == kMaxListeners) return false;
    if (!g_playerTried) {
        g_playerTried = true;
        g_playerReady = addr::Usable(addr::Id::player_initialize) && addr::Usable(addr::Id::player_root_update) &&
            hooks::Create("player_observers", addr::Id::player_initialize,
            reinterpret_cast<void*>(PlayerInit), reinterpret_cast<void**>(&g_playerInit)) &&
            hooks::Create("player_observers", addr::Id::player_root_update,
            reinterpret_cast<void*>(PlayerUpdate), reinterpret_cast<void**>(&g_playerUpdate));
        // Publish the subscription before enabling the hooks.
    }
    if (!g_playerReady) return false;
    g_players[g_nPlayers++] = {initialized, updated, nullptr};
    if (!hooks::SetEnabled("player_observers", true)) { --g_nPlayers; return false; }
    return true;
}

bool ObserveVictims(EntityFn spawned, EntityFn updated, EntityFn destroying) {
    if (g_nVictims == kMaxListeners) return false;
    if (!g_victimTried) {
        g_victimTried = true;
        g_victimReady = addr::Usable(addr::Id::victim_spawn) && addr::Usable(addr::Id::victim_root_update) &&
            addr::Usable(addr::Id::victim_destroy) &&
            hooks::Create("victim_observers", addr::Id::victim_spawn,
            reinterpret_cast<void*>(VictimSpawn), reinterpret_cast<void**>(&g_victimSpawn)) &&
            hooks::Create("victim_observers", addr::Id::victim_root_update,
            reinterpret_cast<void*>(VictimUpdate), reinterpret_cast<void**>(&g_victimUpdate)) &&
            hooks::Create("victim_observers", addr::Id::victim_destroy,
            reinterpret_cast<void*>(VictimDestroy), reinterpret_cast<void**>(&g_victimDestroy));
    }
    if (!g_victimReady) return false;
    g_victims[g_nVictims++] = {spawned, updated, destroying};
    if (!hooks::SetEnabled("victim_observers", true)) { --g_nVictims; return false; }
    return true;
}
} // namespace fdcore::entities
