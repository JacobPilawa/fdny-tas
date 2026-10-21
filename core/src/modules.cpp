#include "modules.h"

#include <windows.h>

#include "log.h"

namespace fdcore::modules {
namespace {

// --- levelwatch ------------------------------------------------------------
// Logs every change of map name / game state. Polls 4x a second from its own
// thread until the hook manager (item 3) provides frame and level-load events.
using addr::Id;

const Id kLevelwatchNeeds[] = {Id::game_state, Id::game_map_name};

DWORD WINAPI LevelwatchThread(LPVOID) {
    char last[16] = "\x01";
    int32_t lastState = -12345;
    for (;;) {
        char map[16];
        int32_t state = -1;
        if (!addr::ReadStr(Id::game_map_name, map, sizeof(map))) map[0] = 0;
        addr::ReadI32(Id::game_state, &state);
        bool changed = state != lastState;
        for (int i = 0; !changed && i < 16; ++i) {
            if (map[i] != last[i]) changed = true;
            if (!map[i]) break;
        }
        if (changed) {
            log::Info("levelwatch: map '%s' state %d", map, static_cast<int>(state));
            lstrcpynA(last, map, sizeof(last));
            lastState = state;
        }
        Sleep(250);
    }
}

void LevelwatchStart() {
    HANDLE t = CreateThread(nullptr, 0, LevelwatchThread, nullptr, 0, nullptr);
    if (t) CloseHandle(t);
    else log::Error("levelwatch: CreateThread failed (%lu)", GetLastError());
}

Module g_modules[] = {
    {"levelwatch", kLevelwatchNeeds, sizeof(kLevelwatchNeeds) / sizeof(kLevelwatchNeeds[0]),
     LevelwatchStart, false},
};

} // namespace

void StartAll() {
    for (Module& m : g_modules) {
        m.enabled = false;
        if (!addr::VersionKnown()) {
            log::Info("module %s: disabled (unknown version)", m.name);
            continue;
        }
        const char* missing = nullptr;
        for (int i = 0; i < m.nrequires && !missing; ++i)
            if (!addr::Usable(m.requires[i])) missing = addr::Name(m.requires[i]);
        if (missing) {
            log::Info("module %s: disabled (needs %s)", m.name, missing);
            continue;
        }
        m.enabled = true;
        log::Info("module %s: enabled", m.name);
        m.start();
    }
}

} // namespace fdcore::modules
