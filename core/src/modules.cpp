#include "modules.h"

#include <windows.h>

#include "hooks.h"
#include "log.h"

namespace fdcore::modules {
namespace {

// --- levelwatch ------------------------------------------------------------
// On every frame end, logs changes of map name / game state and raises the
// core's level_load event when the game enters a map (state reaches 1).
using addr::Id;

const Id kLevelwatchNeeds[] = {Id::game_state, Id::game_map_name, Id::genesis_end_frame};

char g_lastMap[16] = "\x01";
int32_t g_lastState = -12345;

void LevelwatchFrame() {
    char map[16];
    int32_t state = -1;
    if (!addr::ReadStr(Id::game_map_name, map, sizeof(map))) map[0] = 0;
    addr::ReadI32(Id::game_state, &state);
    bool mapChanged = lstrcmpA(map, g_lastMap) != 0;
    if (state == g_lastState && !mapChanged) return;
    log::Info("levelwatch: map '%s' state %d", map, static_cast<int>(state));
    if (state == 1 && map[0] && (g_lastState != 1 || mapChanged)) hooks::RaiseLevelLoad(map);
    lstrcpynA(g_lastMap, map, sizeof(g_lastMap));
    g_lastState = state;
}

void LevelwatchStart() { hooks::OnFrameEnd(LevelwatchFrame); }

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
