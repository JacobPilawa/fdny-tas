// juggleprobe -- research spike for the fire-juggling unknowns (2026-10-22; evidence in
// evidence/fire_juggling/). Standalone, test-only: tests/probes/probe_firejuggle2.yaml
// places zz_juggleprobe.asi in the game folder; nothing in core/src depends on it.
//
// Question: inside one game frame, in what order do the fire touch, the damage and the
// player's Think run? That decides whether the first frame in a fire can still be saved
// as the "safe spot" (+0x3400). Also logs the drop and the burn cool-down / 0x7530
// registrations so their sources can be identified.
//
// Hooks (MinHook, register-preserving naked detours, so no signatures are needed):
//   0 GameSession::RunFrame           0x0042AEA0  frame counter
//   1 PlayerRescueEnt::Think          0x00452570  writes +0x3400 when safe
//   2 PlayerRescueEnt::TakeFireDamage 0x00451640  sets the flinch flags, adds burn
//   3 FireRescueEnt touch callback    0x004483A0  queues msg 0x7531
//   4 PlayerRescueEnt::Verb_Reset     0x0044FF10
//   5 VictimRescueEnt::DropAtPlayer   0x0045C1A0
//   6 burn cool-down (msg 0x7530)     0x0044BC00
//   7 ScriptMsgTable::AddHandler      0x00481A60  queues msg 0x7530 (caller logged)
// Every event records the frame number and the player's pos, +0x3400, burn +0x3538,
// flinch +0x353C, state +0x18C, ground +0x12C, box top +0x88. Records go to a ring; a
// window around any damage / reset / cool-down event is written out.
// Log: zz_juggleprobe.log in the game folder.
#include <windows.h>

#include <stdint.h>

#include "../third_party/minhook/include/MinHook.h"

extern "C" {
void* g_orig[8];
void __cdecl Rec(int id, uintptr_t ecx, uintptr_t ret);
}

namespace {

const uintptr_t kTargets[8] = {0x0042AEA0, 0x00452570, 0x00451640, 0x004483A0,
                               0x0044FF10, 0x0045C1A0, 0x0044BC00, 0x00481A60};
const char* const kNames[8] = {"FRAME", "THINK", "DAMAGE", "FIRETOUCH",
                               "RESET", "DROP", "COOL", "ADDHANDLER"};

char g_dir[MAX_PATH];
HANDLE g_log = INVALID_HANDLE_VALUE;
CRITICAL_SECTION g_logLock;

void Write(const char* buf, int n) {
    EnterCriticalSection(&g_logLock);
    DWORD w;
    if (g_log != INVALID_HANDLE_VALUE) WriteFile(g_log, buf, n, &w, nullptr);
    LeaveCriticalSection(&g_logLock);
}

void Log(const char* fmt, ...) {
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    int o = wvsprintfA(buf, fmt, ap);
    va_end(ap);
    buf[o++] = '\r', buf[o++] = '\n';
    Write(buf, o);
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

// wsprintf has no %f: fixed one decimal / three decimals.
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

struct Rec_ {
    uint32_t frame;
    int id;
    uintptr_t ecx, ret;
    float pos[3], snap[3], burn, top;
    uint32_t flinch;
    int state;
    uint8_t ground;
};
Rec_ g_ring[256];
uint32_t g_ringN;
uint32_t g_frame;
int g_armed;  // frames left to write out

void Emit(const Rec_& r) {
    char a[16], b[16], c[16], d[16], e[16], f[16], g[16], h[16];
    Log("f=%lu %-10s ecx=%08lx ret=%08lx pos=(%s %s %s) snap=(%s %s %s) burn=%s flinch=%08lx state=%d ground=%d top=%s",
        static_cast<unsigned long>(r.frame), kNames[r.id], static_cast<unsigned long>(r.ecx),
        static_cast<unsigned long>(r.ret), F(r.pos[0], a), F(r.pos[1], b), F(r.pos[2], c), F(r.snap[0], d),
        F(r.snap[1], e), F(r.snap[2], f), F(r.burn, g, 3), static_cast<unsigned long>(r.flinch), r.state, r.ground,
        F(r.top, h));
}

}  // namespace

extern "C" void __cdecl Rec(int id, uintptr_t ecx, uintptr_t ret) {
    if (id == 0) ++g_frame;
    const uintptr_t pl = Rd<uintptr_t>(Rd<uintptr_t>(0x00503A78) + 0x30);
    Rec_ r = {};
    r.frame = g_frame;
    r.id = id;
    r.ecx = ecx;
    r.ret = ret;
    if (pl) {
        Read(pl + 0x4C, r.pos, 12);
        Read(pl + 0x3400, r.snap, 12);
        r.burn = Rd<float>(pl + 0x3538);
        r.top = Rd<float>(pl + 0x88);
        r.flinch = Rd<uint32_t>(pl + 0x353C);
        r.state = Rd<int>(pl + 0x18C);
        r.ground = Rd<uint8_t>(pl + 0x12C);
    }
    // FIRETOUCH fires for every entity touching every fire; keep only player contact
    // frames in the ring (the damage that follows shows which ones mattered).
    const bool trigger = id == 2 || id == 4 || id == 5 || id == 6 || id == 7;
    if (trigger && g_armed == 0) {
        Log("---- window opens (%s at frame %lu); preceding records:", kNames[id], static_cast<unsigned long>(g_frame));
        const uint32_t n = g_ringN < 256 ? g_ringN : 256;
        for (uint32_t i = g_ringN - n; i < g_ringN; ++i)
            if (g_ring[i % 256].frame + 4 >= g_frame) Emit(g_ring[i % 256]);
    }
    if (trigger) g_armed = 240;
    if (g_armed > 0) {
        Emit(r);
        if (id == 0 && --g_armed == 0) Log("---- window closes");
    }
    g_ring[g_ringN++ % 256] = r;
}

// Register-preserving detours: record (id, ECX, return address), then jump on.
#define DETOUR(n)                                                                                  \
    extern "C" __attribute__((naked)) void Detour##n() {                                            \
        __asm__ volatile("pushal\n pushfl\n pushl 36(%esp)\n pushl %ecx\n pushl $" #n "\n call _Rec\n" \
                         " addl $12, %esp\n popfl\n popal\n jmp *_g_orig+4*" #n);                    \
    }
DETOUR(0)
DETOUR(1)
DETOUR(2)
DETOUR(3)
DETOUR(4)
DETOUR(5)
DETOUR(6)
DETOUR(7)

namespace {

void* const kDetours[8] = {reinterpret_cast<void*>(&Detour0), reinterpret_cast<void*>(&Detour1),
                           reinterpret_cast<void*>(&Detour2), reinterpret_cast<void*>(&Detour3),
                           reinterpret_cast<void*>(&Detour4), reinterpret_cast<void*>(&Detour5),
                           reinterpret_cast<void*>(&Detour6), reinterpret_cast<void*>(&Detour7)};

DWORD WINAPI Init(LPVOID) {
    Sleep(3000);  // after the core's own startup patches
    MH_STATUS s = MH_Initialize();
    if (s != MH_OK) return Log("MH_Initialize: %s", MH_StatusToString(s)), 0;
    for (int i = 0; i < 8; ++i) {
        uint8_t b[8] = {};
        Read(kTargets[i], b, 8);
        s = MH_CreateHook(reinterpret_cast<void*>(kTargets[i]), kDetours[i], &g_orig[i]);
        if (s == MH_OK) s = MH_EnableHook(reinterpret_cast<void*>(kTargets[i]));
        Log("hook %d %s %08lx [%02x %02x %02x %02x %02x %02x %02x %02x]: %s", i, kNames[i],
            static_cast<unsigned long>(kTargets[i]), b[0], b[1], b[2], b[3], b[4], b[5], b[6], b[7],
            MH_StatusToString(s));
    }
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
    wsprintfA(path, "%szz_juggleprobe.log", g_dir);
    g_log = CreateFileA(path, GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS, 0, nullptr);
    Log("juggleprobe loaded");
    CreateThread(nullptr, 0, Init, nullptr, 0, nullptr);
    return TRUE;
}
