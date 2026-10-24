// pauseprobe -- research spike for fd_pause / free camera (2026-10-22; evidence in
// evidence/freecam/). Standalone, test-only: tests/probes/probe_pause.yaml places
// zz_pauseprobe.asi in the game folder; nothing in core/src depends on it.
//
// Questions:
//   1. Skipping GameSession::TickSubsystems: does rendering go on, without a crash, and
//      does game state stay frozen? (state is sampled by the harness)
//   2. What sets the render camera (ROCRender+0x170) each frame: who calls
//      geCamera_SetWorldSpaceXForm, on which camera, and do those calls stop while paused?
//   3. Does a transform we set on that camera while paused hold on screen?
//   4. Do the game's own mouse sums (GameControls+0x18/+0x1c) see mouse motion while paused?
//   5. Game clock: elapsed (+0x44) and dt (+0x4c) while paused and on resume, with and
//      without GameClock::Pause / Resume.
//
// Keys (GetAsyncKeyState edges, read once per rendered frame; F-keys, because the harness
// types console commands and letters in them would toggle the probe, run p1):
//   F5  toggle pause (skip TickSubsystems; input-block byte GameControls+0x330 held at 1)
//   F6  arm: the next pause also pauses the game clock (GameClock::Pause / Resume)
//   F7  while paused: camera override on/off (the transform the game last set, +150 y)
// Hooks (MinHook):
//   0 GameSession::TickSubsystems 0x00429520  thiscall, no stack args
//   1 ROCRender::RenderFrame      0x00494670  thiscall, one stack arg
//   2 Genesis @geCamera_SetWorldSpaceXForm@8   fastcall (camera, xform)
//   3 Genesis geEngine_RenderWorld             cdecl (engine, world, camera, a4); chained on
//     top of FDNYCore gfx's hook. Run p4: a transform set at RenderFrame entry reads back
//     but is gone by the world render, so the override is applied here, on the camera
//     the world is rendered with.
// Log: zz_pauseprobe.log in the game folder.
#include <windows.h>

#include <stdint.h>

#include "../third_party/minhook/include/MinHook.h"

namespace {

constexpr uintptr_t kTickSubsystems = 0x00429520, kRenderFrame = 0x00494670;
constexpr uintptr_t kEverGame = 0x00503A78;  // g_everGame: [0] = GameControls, [4] = ROCRender, [0x30] = player
constexpr uintptr_t kClock = 0x00502AB8;     // g_gameClock: +0x44 elapsed ms, +0x4c dt
constexpr uintptr_t kClockPause = 0x0042BF20, kClockResume = 0x0042BF40;

struct XForm {  // geXForm3d
    float m[9];
    float t[3];
};
using SetXFormFn = int(__fastcall*)(void* cam, const XForm* x);
using GetXFormFn = const XForm*(__fastcall*)(void* cam);
using TickFn = void(__fastcall*)(void* self, void* edx);
using RenderFn = void(__fastcall*)(void* self, void* edx, void* cb);
using ClockFn = void(__fastcall*)(void* self, void* edx);
using RenderWorldFn = int(__cdecl*)(void* engine, void* world, void* camera, void* a4);
RenderWorldFn g_origRenderWorld;

TickFn g_origTick;
RenderFn g_origRender;
SetXFormFn g_origSet, g_set;
GetXFormFn g_get;

char g_dir[MAX_PATH];
HANDLE g_log = INVALID_HANDLE_VALUE;
CRITICAL_SECTION g_logLock;

void Log(const char* fmt, ...) {
    char buf[600];
    va_list ap;
    va_start(ap, fmt);
    int o = wvsprintfA(buf, fmt, ap);
    va_end(ap);
    buf[o++] = '\r', buf[o++] = '\n';
    EnterCriticalSection(&g_logLock);
    DWORD w;
    if (g_log != INVALID_HANDLE_VALUE) WriteFile(g_log, buf, o, &w, nullptr);
    LeaveCriticalSection(&g_logLock);
}

bool Read(uintptr_t p, void* out, SIZE_T n) {
    SIZE_T got = 0;
    return ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<const void*>(p), out, n, &got) && got == n;
}
template <class T> T Rd(uintptr_t a) {
    T v = T();
    Read(a, &v, sizeof(v));
    return v;
}

const char* F(float v, char* b, int places = 1) {
    const bool neg = v < 0;
    double d = neg ? -v : v;
    if (d > 2e9) d = 2e9;
    const long i = static_cast<long>(d);
    const long m = places == 3 ? 1000 : 10;
    const long f = static_cast<long>((d - i) * m);
    wsprintfA(b, places == 3 ? "%s%ld.%03ld" : "%s%ld.%ld", neg ? "-" : "", i, f);
    return b;
}

uint32_t g_frame;
bool g_paused, g_clockToo, g_clockPaused, g_override;
int g_afterResume;     // frames still to log after a resume
uint32_t g_pausedFrames, g_ticksSkipped;
XForm g_lastGameXf;    // the last transform the game's per-frame view set wrote
bool g_haveGameXf;
void* g_viewCam;       // the camera of that set (return address kViewSetRet)
constexpr uintptr_t kViewSetRet = 0x0049217E;  // run p2: once per frame, sim side

// geCamera_SetWorldSpaceXForm callers: (return address, camera) -> calls, split by paused.
struct Caller {
    uintptr_t ret, cam;
    uint32_t running, paused;
};
Caller g_callers[16];
int g_nCallers;
bool g_inOverride;

void* RenderCam() {
    const uintptr_t ren = Rd<uintptr_t>(kEverGame + 0) ? Rd<uintptr_t>(Rd<uintptr_t>(kEverGame) + 4) : 0;
    return ren ? reinterpret_cast<void*>(Rd<uintptr_t>(ren + 0x170)) : nullptr;
}
uintptr_t Controls() { return Rd<uintptr_t>(Rd<uintptr_t>(kEverGame)); }
uintptr_t Player() { return Rd<uintptr_t>(Rd<uintptr_t>(kEverGame) + 0x30); }

int __fastcall DetourSet(void* cam, const XForm* x) {
    if (!g_inOverride) {
        const uintptr_t ret = reinterpret_cast<uintptr_t>(__builtin_return_address(0));
        int i = 0;
        for (; i < g_nCallers; ++i)
            if (g_callers[i].ret == ret && g_callers[i].cam == reinterpret_cast<uintptr_t>(cam)) break;
        if (i == g_nCallers && g_nCallers < 16) g_callers[g_nCallers++] = {ret, reinterpret_cast<uintptr_t>(cam), 0, 0};
        if (i < 16) ++(g_paused ? g_callers[i].paused : g_callers[i].running);
        if (ret == kViewSetRet && x) g_lastGameXf = *x, g_haveGameXf = true, g_viewCam = cam;
    }
    return g_origSet(cam, x);
}

void __fastcall DetourTick(void* self, void* edx) {
    if (g_paused) {
        ++g_ticksSkipped;
        return;
    }
    g_origTick(self, edx);
}

bool Edge(int vk) {
    static bool down[256];
    const bool now = (GetAsyncKeyState(vk) & 0x8000) != 0;
    const bool edge = now && !down[vk];
    down[vk] = now;
    return edge;
}

void LogState(const char* tag) {
    char a[16], b[16], c[16], d[16], e[16], f[16], g[16];
    const uintptr_t pl = Player(), gc = Controls();
    float pos[3] = {}, cam[3] = {};
    if (pl) Read(pl + 0x4C, pos, 12);
    void* rc = g_viewCam;
    if (rc && g_get) {
        const XForm* x = g_get(rc);
        if (x) cam[0] = x->t[0], cam[1] = x->t[1], cam[2] = x->t[2];
    }
    Log("f=%lu %s elapsed=%lu dt=%s mouse=(%ld %ld) block=%d pos=(%s %s %s) viewcam=%08lx rendercam=%08lx cam=(%s %s %s) skipped=%lu",
        static_cast<unsigned long>(g_frame), tag, static_cast<unsigned long>(Rd<uint32_t>(kClock + 0x44)),
        F(Rd<float>(kClock + 0x4C), a, 3), static_cast<long>(gc ? Rd<int32_t>(gc + 0x18) : 0),
        static_cast<long>(gc ? Rd<int32_t>(gc + 0x1C) : 0), gc ? Rd<uint8_t>(gc + 0x330) : -1, F(pos[0], b),
        F(pos[1], c), F(pos[2], d), static_cast<unsigned long>(reinterpret_cast<uintptr_t>(g_viewCam)),
        static_cast<unsigned long>(reinterpret_cast<uintptr_t>(RenderCam())), F(cam[0], e), F(cam[1], f), F(cam[2], g),
        static_cast<unsigned long>(g_ticksSkipped));
}

void SetBlock(uint8_t v) {
    const uintptr_t gc = Controls();
    if (gc) WriteProcessMemory(GetCurrentProcess(), reinterpret_cast<void*>(gc + 0x330), &v, 1, nullptr);
}

// Pause / Resume act on the active subtimer, chosen like MainWindow::RunMessageLoop does
// (0x004994E6): the selector byte at +0x40 picks +0x20 (real) over +0x00 (game).
void ClockCall(uintptr_t fn) {
    const uintptr_t sub = Rd<uint8_t>(kClock + 0x40) ? kClock + 0x20 : kClock;
    reinterpret_cast<ClockFn>(fn)(reinterpret_cast<void*>(sub), nullptr);
}

void __fastcall DetourRender(void* self, void* edx, void* cb) {
    ++g_frame;
    if (Edge(VK_F5)) {
        g_paused = !g_paused;
        if (g_paused) {
                        if (g_clockToo && !g_clockPaused) ClockCall(kClockPause), g_clockPaused = true;
            SetBlock(1);
            g_pausedFrames = 0;
            LogState("PAUSE");
        } else {
            g_override = false;
            if (g_clockPaused) ClockCall(kClockResume), g_clockPaused = false;
            SetBlock(0);
            LogState("RESUME");
            g_afterResume = 8;
        }
    }
    if (Edge(VK_F6)) g_clockToo = !g_clockToo, Log("f=%lu clock-pause mode %d", static_cast<unsigned long>(g_frame), g_clockToo);
    if (g_paused && Edge(VK_F7)) g_override = !g_override, Log("f=%lu override %d", static_cast<unsigned long>(g_frame), g_override);
    if (g_paused) {
        ++g_pausedFrames;
        const uintptr_t gc = Controls();
        const bool mouse = gc && (Rd<int32_t>(gc + 0x18) || Rd<int32_t>(gc + 0x1C));
        if (g_pausedFrames % 30 == 1 || mouse) LogState("paused");
    } else if (g_afterResume > 0) {
        --g_afterResume;
        LogState("after");
    }
    if (g_frame % 600 == 0 || (g_paused && g_pausedFrames == 120)) {
        for (int i = 0; i < g_nCallers; ++i)
            Log("f=%lu setxform caller ret=%08lx cam=%08lx%s running=%lu paused=%lu", static_cast<unsigned long>(g_frame),
                static_cast<unsigned long>(g_callers[i].ret), static_cast<unsigned long>(g_callers[i].cam),
                reinterpret_cast<void*>(g_callers[i].cam) == RenderCam() ? " (render cam)" : "",
                static_cast<unsigned long>(g_callers[i].running), static_cast<unsigned long>(g_callers[i].paused));
    }
    g_origRender(self, edx, cb);
}

uint32_t g_rwFrame, g_rwCalls;
int __cdecl DetourRenderWorld(void* engine, void* world, void* camera, void* a4) {
    if (g_rwFrame != g_frame) g_rwFrame = g_frame, g_rwCalls = 0;
    ++g_rwCalls;
    if (g_paused && g_override && camera && g_get && g_set) {
        static XForm base;
        static void* baseCam;
        if (baseCam != camera) base = *g_get(camera), baseCam = camera;
        XForm x = base;
        x.t[1] += 150.0f;
        g_inOverride = true;
        g_set(camera, &x);
        g_inOverride = false;
        static int dbg;
        if (dbg++ < 3) {
            char a[16];
            Log("f=%lu renderworld #%lu cam=%08lx (viewcam %08lx, rendercam %08lx) world=%08lx override y=%s",
                static_cast<unsigned long>(g_frame), static_cast<unsigned long>(g_rwCalls),
                static_cast<unsigned long>(reinterpret_cast<uintptr_t>(camera)),
                static_cast<unsigned long>(reinterpret_cast<uintptr_t>(g_viewCam)),
                static_cast<unsigned long>(reinterpret_cast<uintptr_t>(RenderCam())),
                static_cast<unsigned long>(reinterpret_cast<uintptr_t>(world)), F(x.t[1], a));
        }
    }
    return g_origRenderWorld(engine, world, camera, a4);
}

void Hook(const char* name, void* target, void* detour, void** orig) {
    uint8_t b[8] = {};
    Read(reinterpret_cast<uintptr_t>(target), b, 8);
    MH_STATUS s = MH_CreateHook(target, detour, orig);
    if (s == MH_OK) s = MH_EnableHook(target);
    Log("hook %s %08lx [%02x %02x %02x %02x %02x %02x %02x %02x]: %s", name,
        static_cast<unsigned long>(reinterpret_cast<uintptr_t>(target)), b[0], b[1], b[2], b[3], b[4], b[5], b[6], b[7],
        MH_StatusToString(s));
}

DWORD WINAPI Init(LPVOID) {
    Sleep(3000);  // after the core's own startup patches
    MH_STATUS s = MH_Initialize();
    if (s != MH_OK) return Log("MH_Initialize: %s", MH_StatusToString(s)), 0;
    HMODULE gen = GetModuleHandleA("Genesis.dll");
    g_set = gen ? reinterpret_cast<SetXFormFn>(GetProcAddress(gen, "@geCamera_SetWorldSpaceXForm@8")) : nullptr;
    g_get = gen ? reinterpret_cast<GetXFormFn>(GetProcAddress(gen, "@geCamera_GetWorldSpaceXForm@4")) : nullptr;
    Log("genesis %08lx set %08lx get %08lx", static_cast<unsigned long>(reinterpret_cast<uintptr_t>(gen)),
        static_cast<unsigned long>(reinterpret_cast<uintptr_t>(g_set)),
        static_cast<unsigned long>(reinterpret_cast<uintptr_t>(g_get)));
    if (g_set) Hook("SetWorldSpaceXForm", reinterpret_cast<void*>(g_set), reinterpret_cast<void*>(&DetourSet),
                    reinterpret_cast<void**>(&g_origSet));
    // The override must call the unhooked original.
    if (g_origSet) g_set = g_origSet;
    void* rw = gen ? reinterpret_cast<void*>(GetProcAddress(gen, "geEngine_RenderWorld")) : nullptr;
    if (rw) Hook("RenderWorld (chained)", rw, reinterpret_cast<void*>(&DetourRenderWorld),
                 reinterpret_cast<void**>(&g_origRenderWorld));
    Hook("TickSubsystems", reinterpret_cast<void*>(kTickSubsystems), reinterpret_cast<void*>(&DetourTick),
         reinterpret_cast<void**>(&g_origTick));
    Hook("RenderFrame", reinterpret_cast<void*>(kRenderFrame), reinterpret_cast<void*>(&DetourRender),
         reinterpret_cast<void**>(&g_origRender));
    return 0;
}

}  // namespace

extern "C" BOOL WINAPI _DllMainCRTStartup(HINSTANCE inst, DWORD reason, LPVOID) {
    if (reason != DLL_PROCESS_ATTACH) return TRUE;
    DisableThreadLibraryCalls(inst);
    InitializeCriticalSection(&g_logLock);
    GetModuleFileNameA(nullptr, g_dir, MAX_PATH);
    for (int i = lstrlenA(g_dir); i > 0; --i)
        if (g_dir[i - 1] == '\\') {
            g_dir[i] = 0;
            break;
        }
    char path[MAX_PATH];
    wsprintfA(path, "%szz_pauseprobe.log", g_dir);
    g_log = CreateFileA(path, GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS, 0, nullptr);
    Log("pauseprobe loaded");
    CreateThread(nullptr, 0, Init, nullptr, 0, nullptr);
    return TRUE;
}
