#include "hooks.h"

#include <windows.h>

#include "../third_party/minhook/include/MinHook.h"
#include "log.h"

namespace fdcore::hooks {
namespace {

using addr::Id;

// geBoolean geEngine_BeginFrame(geEngine*, geCamera*, geBoolean) / EndFrame(geEngine*):
// cdecl (undecorated exports; the exe cleans the stack after both calls).
using BeginFrameFn = int(__cdecl*)(void* engine, void* camera, int clear);
using EndFrameFn = int(__cdecl*)(void* engine);

BeginFrameFn g_origBegin = nullptr;
EndFrameFn g_origEnd = nullptr;
void* g_targetBegin = nullptr;
void* g_targetEnd = nullptr;
bool g_frameEnabled = false;
bool g_mhInit = false;

Counters g_stats;

constexpr int kMaxSubs = 32;
FrameFn g_frameSubs[kMaxSubs];
int g_nFrameSubs = 0;
LevelFn g_levelSubs[kMaxSubs];
int g_nLevelSubs = 0;

int __cdecl BeginFrameDetour(void* engine, void* camera, int clear) {
    InterlockedIncrement(&g_stats.beginFrame);
    return g_origBegin(engine, camera, clear);
}

int __cdecl EndFrameDetour(void* engine) {
    InterlockedIncrement(&g_stats.endFrame);
    for (int i = 0; i < g_nFrameSubs; ++i) g_frameSubs[i]();
    InterlockedIncrement(&g_stats.frameEndEvents);
    return g_origEnd(engine);
}

bool StrEq(const char* a, const char* b) {
    while (*a && *a == *b) ++a, ++b;
    return *a == *b;
}

bool CreateHook(Id id, void* detour, void** orig, void** target) {
    uintptr_t t = addr::ExportAddress(id);
    if (!t) {
        log::Warn("hooks: %s unavailable (address table); not hooked", addr::Name(id));
        return false;
    }
    MH_STATUS s = MH_CreateHook(reinterpret_cast<void*>(t), detour, orig);
    if (s != MH_OK) {
        log::Error("hooks: MH_CreateHook(%s) failed: %s", addr::Name(id), MH_StatusToString(s));
        return false;
    }
    *target = reinterpret_cast<void*>(t);
    log::Info("hooks: created %s at %08lx", addr::Name(id), static_cast<unsigned long>(t));
    return true;
}

} // namespace

bool Init() {
    MH_STATUS s = MH_Initialize();
    if (s != MH_OK) {
        log::Error("hooks: MH_Initialize failed: %s", MH_StatusToString(s));
        return false;
    }
    g_mhInit = true;
    bool ok = CreateHook(Id::genesis_begin_frame, reinterpret_cast<void*>(&BeginFrameDetour),
                         reinterpret_cast<void**>(&g_origBegin), &g_targetBegin) &&
              CreateHook(Id::genesis_end_frame, reinterpret_cast<void*>(&EndFrameDetour),
                         reinterpret_cast<void**>(&g_origEnd), &g_targetEnd);
    if (ok) ok = SetEnabled("frame", true);
    return ok;
}

void Shutdown() {
    if (!g_mhInit) return;
    MH_DisableHook(MH_ALL_HOOKS);
    MH_Uninitialize();
    g_mhInit = false;
}

bool SetEnabled(const char* group, bool on) {
    if (!StrEq(group, "frame") || !g_targetBegin || !g_targetEnd) return false;
    if (on == g_frameEnabled) return true;
    // Queue both, apply atomically: MinHook suspends the game's threads while
    // it patches, so a frame never sees Begin hooked and End not.
    MH_STATUS a = on ? MH_QueueEnableHook(g_targetBegin) : MH_QueueDisableHook(g_targetBegin);
    MH_STATUS b = on ? MH_QueueEnableHook(g_targetEnd) : MH_QueueDisableHook(g_targetEnd);
    MH_STATUS c = MH_ApplyQueued();
    if (a != MH_OK || b != MH_OK || c != MH_OK) {
        log::Error("hooks: %s frame hooks failed: %s / %s / %s", on ? "enable" : "disable",
                   MH_StatusToString(a), MH_StatusToString(b), MH_StatusToString(c));
        return false;
    }
    g_frameEnabled = on;
    InterlockedIncrement(&g_stats.toggles);
    return true;
}

bool IsEnabled(const char* group) { return StrEq(group, "frame") && g_frameEnabled; }

void OnFrameEnd(FrameFn fn) {
    if (g_nFrameSubs < kMaxSubs) g_frameSubs[g_nFrameSubs++] = fn;
}

void OnLevelLoad(LevelFn fn) {
    if (g_nLevelSubs < kMaxSubs) g_levelSubs[g_nLevelSubs++] = fn;
}

void RaiseLevelLoad(const char* map) {
    InterlockedIncrement(&g_stats.levelLoads);
    log::Info("event: level_load '%s'", map);
    for (int i = 0; i < g_nLevelSubs; ++i) g_levelSubs[i](map);
}

const Counters& Stats() { return g_stats; }

} // namespace fdcore::hooks
