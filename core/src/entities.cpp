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
} // namespace

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
