// demoprobe -- research spike for demos / ghosts (2026-10-23; evidence in evidence/demos/).
// Standalone, test-only: tests/probes/probe_demo.yaml places zz_demoprobe.asi in the game
// folder; nothing in core/src depends on it.
//
// Question: does a level replay identically (Hourglass / libTAS style, in-process) when
// the three inputs the game takes from outside are fed back exactly?
//   - time:  GameClock::Tick 0x0042C080 publishes dt (+0x4C) and elapsed ms (+0x44) at the
//            start of GameSession::RunFrame, before input and the simulation;
//   - input: both DirectInputProvider drains (mouse 0x00437AB0, keyboard 0x00437C80) read
//            buffered IDirectInputDevice8::GetDeviceData (vtable +0x28);
//   - RNG:   the one MT19937 stream (g_mtState 0x00501A28, 624 words; words-remaining
//            0x004FF370; next pointer 0x005023EC).
// Start point: the first frame with game.state == 1 after a level load (the RNG is
// restored then; victims are pinned with fd_static_spawns 1 so the load's own draws
// don't matter). Compared every frame: dt, player position / velocity / yaw / pitch /
// move state, mission timer, and the RNG cursor (draws consumed). Bit-exact.
//
// Keys (GetAsyncKeyState edges, once per frame; F-keys so the harness's console typing
// doesn't trigger them):
//   F5  arm RECORD: from the next level start until F7 or the level ends
//   F6  arm PLAYBACK (time + input + RNG) at the next level start
//   F8  arm PLAYBACK without time (input + RNG only; real dt) -- what dt control buys
//   F9  arm PLAYBACK without RNG restore (time + input only)
//   F7  stop recording
// Log: zz_demoprobe.log in the game folder (+ zz_demoprobe_rec.txt / _play<N>.txt traces).
#include <windows.h>

#include <stdint.h>

#include "../third_party/minhook/include/MinHook.h"

namespace {

constexpr uintptr_t kTick = 0x0042C080;
constexpr uintptr_t kClock = 0x00502AB8;    // +0x44 elapsed ms, +0x4C dt (float)
constexpr uintptr_t kSession = 0x00502AAC;  // *: +0x6F0 game.state
constexpr uintptr_t kEverGame = 0x00503A78; // *: +0x30 player
constexpr uintptr_t kMtState = 0x00501A28, kMtLeft = 0x004FF370, kMtNext = 0x005023EC;
constexpr int kMtBytes = 624 * 4;
constexpr HRESULT DI_OK = 0;

char g_dir[MAX_PATH];
HANDLE g_log = INVALID_HANDLE_VALUE;

void Write(HANDLE h, const char* s, int n) {
    DWORD w;
    if (h != INVALID_HANDLE_VALUE) WriteFile(h, s, n, &w, nullptr);
}
void Log(const char* fmt, ...) {
    char buf[700];
    va_list ap;
    va_start(ap, fmt);
    int o = wvsprintfA(buf, fmt, ap);
    va_end(ap);
    buf[o++] = '\r', buf[o++] = '\n';
    Write(g_log, buf, o);
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
uint32_t Bits(float f) {
    uint32_t u;
    __builtin_memcpy(&u, &f, 4);
    return u;
}

// --- per-frame state ------------------------------------------------------------------
struct Frame {
    uint32_t dt, elapsed;       // what the clock published this frame (after injection)
    uint32_t pos[3], vel[3], yaw, pitch, timer, timeLeft;
    int32_t mstate, gstate, mtLeft;
    uint32_t mtNext;            // offset of the next-word pointer into g_mtState
    uint32_t entN, entHash, vicHash, fireHash;  // the level entity list (v2)
    uint32_t rngN;              // MT19937 draws during the previous frame (v2)
};
// v2: who drew from the RNG each frame: (return address into the Rand_* wrapper or the
// caller, first game call site found further up the stack), the first kRngLog draws.
constexpr int kRngLog = 12;
uint32_t g_recDraws[20000][kRngLog][2], g_playDraws[20000][kRngLog][2];
uint32_t g_rngN;  // draws so far this frame
uint32_t (*g_rngLog)[kRngLog][2];
int g_rngFrame = -1;

uint32_t Fnv(uint32_t h, uint32_t v) {
    for (int i = 0; i < 4; ++i) h = (h ^ ((v >> (8 * i)) & 0xFF)) * 16777619u;
    return h;
}
constexpr int kMaxFrames = 20000;  // > 5 min at 60 fps
Frame g_rec[kMaxFrames];
int g_recFrames;
uint32_t g_rec0Elapsed;  // elapsed at the first recorded frame
uint8_t g_recMt[kMtBytes];
int32_t g_recMtLeft;
uint32_t g_recMtNext;

void Sample(Frame& f) {
    f.dt = Rd<uint32_t>(kClock + 0x4C), f.elapsed = Rd<uint32_t>(kClock + 0x44);
    const uintptr_t s = Rd<uintptr_t>(kSession);
    f.gstate = s ? Rd<int32_t>(s + 0x6F0) : -1;
    const uintptr_t eg = Rd<uintptr_t>(kEverGame);
    const uintptr_t p = eg ? Rd<uintptr_t>(eg + 0x30) : 0;
    for (int i = 0; i < 3; ++i) {
        f.pos[i] = p ? Rd<uint32_t>(p + 0x4C + 4 * i) : 0;
        f.vel[i] = p ? Rd<uint32_t>(p + 0xFC + 4 * i) : 0;
    }
    f.yaw = p ? Rd<uint32_t>(p + 0x3434) : 0, f.pitch = p ? Rd<uint32_t>(p + 0x3430) : 0;
    f.timer = p ? Rd<uint32_t>(p + 0x32FC) : 0;
    f.timeLeft = p ? Rd<uint32_t>(p + 0x343C) : 0;
    // Level entities (GameSession+0x64 list; node: entity @+0, next @+4): kind (+0x18) and
    // position (+0x4C) of each, victim state (+0x120), fire mode (+0x70). In order.
    f.entN = 0, f.entHash = f.vicHash = f.fireHash = 2166136261u;
    uint32_t node = (s && f.gstate == 1) ? Rd<uint32_t>(s + 0x64) : 0;
    for (int guard = 0; node && guard < 4096; ++guard) {
        const uint32_t e = Rd<uint32_t>(node), next = Rd<uint32_t>(node + 4);
        if (e) {
            const int32_t kind = Rd<int32_t>(e + 0x18);
            ++f.entN;
            f.entHash = Fnv(f.entHash, static_cast<uint32_t>(kind));
            for (int i = 0; i < 3; ++i) f.entHash = Fnv(f.entHash, Rd<uint32_t>(e + 0x4C + 4 * i));
            if (kind == 2) {
                for (int i = 0; i < 3; ++i) f.vicHash = Fnv(f.vicHash, Rd<uint32_t>(e + 0x4C + 4 * i));
                f.vicHash = Fnv(f.vicHash, Rd<uint32_t>(e + 0x120));
            } else {
                f.fireHash = Fnv(f.fireHash, Rd<uint32_t>(e + 0x70));
            }
        }
        node = next;
    }
    f.rngN = g_rngN;
    f.mstate = p ? Rd<int32_t>(p + 0x18C) : -1;
    f.mtLeft = Rd<int32_t>(kMtLeft);
    f.mtNext = Rd<uint32_t>(kMtNext) - kMtState;
}

// --- recorded input: GetDeviceData calls ---------------------------------------------------
// Stream of calls: [frame u32][dev u8][hr i32][n u32][n * 20 bytes]
constexpr int kInBytes = 8 * 1024 * 1024;
uint8_t g_in[kInBytes];
int g_inLen, g_inPos;
void* g_dev[2];  // device pointers in order of first sight (the game polls mouse first)

int DevId(void* d) {
    for (int i = 0; i < 2; ++i) {
        if (g_dev[i] == d) return i;
        if (!g_dev[i]) return g_dev[i] = d, i;
    }
    return 1;
}

enum Mode { IDLE, ARM_REC, REC, ARM_PLAY, PLAY };
Mode g_mode;
bool g_playTime, g_playRng, g_playFirst;
int g_frame;  // frame index within the recording / playback
bool g_seenLoad;
int g_playNo;
// playback comparison
int g_firstDiff, g_diffFrames, g_inputMismatch;

// --- v2: RNG draws (MT19937_NextU32 0x00404A70: no args, plain RET, eax/ecx/edx only) ---------
constexpr uintptr_t kNextU32 = 0x00404A70;
using NextFn = uint32_t(__cdecl*)();
NextFn g_origNext;

void Count(uint32_t site);
uint32_t GameCaller(const uint32_t* sp, uint32_t skip);
bool Cosmetic(uint32_t site);
uint32_t PrivateU32();
extern void* g_histOn;
extern bool g_split;

__attribute__((noinline)) uint32_t __cdecl HookNext() {
    if (g_histOn) {  // Rand_Below draws, attributed to Rand_Below's caller
        const uint32_t ret0 = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(__builtin_return_address(0)));
        const uint32_t site = GameCaller(static_cast<const uint32_t*>(__builtin_frame_address(0)), ret0);
        if (g_split && Cosmetic(site)) {
            Count(site | 0x80000000u);
            return PrivateU32();
        }
        Count(site);
    }
    const uint32_t v = g_origNext();
    if ((g_mode == REC || g_mode == PLAY) && g_rngFrame >= 0 && g_rngFrame < 20000 && g_rngLog) {
        if (g_rngN < kRngLog) {
            const uint32_t* sp = static_cast<const uint32_t*>(__builtin_frame_address(0));
            uint32_t caller = 0;
            for (int i = 2; i < 64 && !caller; ++i) {
                const uint32_t a = sp[i];
                if (a > 0x00401005 && a < 0x004CC000 && Rd<uint8_t>(a - 5) == 0xE8) caller = a;
            }
            g_rngLog[g_rngFrame][g_rngN][0] = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(__builtin_return_address(0)));
            g_rngLog[g_rngFrame][g_rngN][1] = caller;
        }
        ++g_rngN;
    }
    return v;
}

// v3: Rand_Float01 0x00404AC0 inlines its own MT draw (run 2: NextU32 saw no draws while the
// cursor moved); every Rand_FloatRange goes through it. cdecl, no args, float in st0.
constexpr uintptr_t kFloat01 = 0x00404AC0;
using Float01Fn = float(__cdecl*)();
Float01Fn g_origFloat01;

// v5: per-slot histogram of draws by game call site, from LoadLevel on (slot 0 = inside
// LoadLevel, then one slot per frame), for the recording and the current playback; the
// first slot whose histogram differs names the consumer that breaks the RNG stream.
constexpr int kSlots = 400, kSites = 96;
struct Hist {
    int n;
    uint32_t site[kSites], count[kSites];
};
Hist g_recHist[kSlots], g_playHist[kSlots];
Hist* g_hist;  // the array being filled, or null
void* g_histOn;  // == g_hist, for HookNext (declared before Hist)
int g_slot = -1;

void Count(uint32_t site) {
    if (!g_hist || g_slot < 0 || g_slot >= kSlots) return;
    Hist& h = g_hist[g_slot];
    for (int i = 0; i < h.n; ++i)
        if (h.site[i] == site) return void(++h.count[i]);
    if (h.n < kSites) h.site[h.n] = site, h.count[h.n++] = 1;
}

uint32_t GameCaller(const uint32_t* sp, uint32_t skip) {
    for (int i = 2; i < 64; ++i) {
        const uint32_t a = sp[i];
        if (a != skip && a > 0x00401005 && a < 0x004CC000 && Rd<uint8_t>(a - 5) == 0xE8 &&
            !(a >= 0x00498B30 && a < 0x00498B80))  // skip Rand_FloatRange's own return
            return a;
    }
    return 0;
}

// v6: the RNG split. Presentation code (particles, effects, ash, embers, camera shake, sound,
// chatter, footsteps, menu, roster names) draws from a private generator from LoadLevel on,
// in both runs, so its frame-rate / render-dependent draw counts (run 5: ParticleSystem::Emit
// at load frame 2) can't shift the gameplay stream. Ranges from research.db (function start ..
// next symbol). Counted with the top bit set.
const struct {
    uint32_t lo, hi;
} kCosmetic[] = {
    {0x00480090, 0x00480260}, {0x004696A0, 0x00469940}, {0x0046C5A0, 0x0046D1B0}, {0x00446510, 0x004465D0},
    {0x00446470, 0x00446510}, {0x004467E0, 0x004469F0}, {0x00463EF0, 0x004640F0}, {0x0044B270, 0x0044B510},
    {0x00445F00, 0x00446030}, {0x0049CD80, 0x0049CF10}, {0x004A0BE0, 0x004A0D60}, {0x00488D40, 0x00488DD0},
    {0x0046A390, 0x0046B9D0}, {0x0044EA90, 0x0044ECD0}, {0x00435A10, 0x00435B30}, {0x00454490, 0x004545F0},
};
bool g_split;          // armed for this run
uint32_t g_private;    // xorshift32 state, reseeded at LoadLevel
bool Cosmetic(uint32_t site) {
    for (const auto& r : kCosmetic)
        if (site >= r.lo && site < r.hi) return true;
    return false;
}
uint32_t PrivateU32() {
    uint32_t x = g_private;
    x ^= x << 13, x ^= x >> 17, x ^= x << 5;
    return g_private = x;
}

__attribute__((noinline)) float __cdecl HookFloat01() {
    if (g_hist) {
        const uint32_t ret0 = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(__builtin_return_address(0)));
        const bool viaRange = ret0 >= 0x00498B30 && ret0 < 0x00498B80;
        const uint32_t site = viaRange ? GameCaller(static_cast<const uint32_t*>(__builtin_frame_address(0)), ret0) : ret0;
        if (g_split && Cosmetic(site)) {
            Count(site | 0x80000000u);
            return static_cast<float>(PrivateU32() >> 9) * (1.0f / 8388608.0f);  // [0, 1)
        }
        Count(site);
    }
    const float v = g_origFloat01();
    if ((g_mode == REC || g_mode == PLAY) && g_rngFrame >= 0 && g_rngFrame < 20000 && g_rngLog) {
        if (g_rngN < kRngLog) {
            const uint32_t* sp = static_cast<const uint32_t*>(__builtin_frame_address(0));
            const uint32_t ret0 = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(__builtin_return_address(0)));
            uint32_t caller = 0;
            for (int i = 2; i < 64 && !caller; ++i) {
                const uint32_t a = sp[i];
                if (a != ret0 && a > 0x00401005 && a < 0x004CC000 && Rd<uint8_t>(a - 5) == 0xE8) caller = a;
            }
            g_rngLog[g_rngFrame][g_rngN][0] = ret0;
            g_rngLog[g_rngFrame][g_rngN][1] = caller;
        }
        ++g_rngN;
    }
    return v;
}

// v3: GameSession::LoadLevel 0x0042AB00 (thiscall, 1 stack arg, RET 4): the synchronous level
// start, called from RunFrame before the clock tick; the victims' spawn / idle-pose draws
// happen inside it (run 2: victims differed from frame 0 with the RNG restored later).
constexpr uintptr_t kLoadLevel = 0x0042AB00;
using LoadLevelFn = void(__fastcall*)(void* self, void* edx, void* arg);
LoadLevelFn g_origLoadLevel;
void OnLoadLevel();  // below

void __fastcall HookLoadLevel(void* self, void* edx, void* arg) {
    OnLoadLevel();
    g_origLoadLevel(self, edx, arg);
}

using GetDataFn = HRESULT(__stdcall*)(void* self, DWORD cb, void* data, DWORD* inout, DWORD flags);
GetDataFn g_origGetData;

HRESULT __stdcall HookGetData(void* self, DWORD cb, void* data, DWORD* inout, DWORD flags) {
    if (g_mode != REC && g_mode != PLAY) return g_origGetData(self, cb, data, inout, flags);
    const int dev = DevId(self);
    if (g_mode == REC) {
        const DWORD cap = inout ? *inout : 0;
        HRESULT hr = g_origGetData(self, cb, data, inout, flags);
        const DWORD n = (data && inout && SUCCEEDED(hr)) ? *inout : 0;
        if (cb == 20 && g_inLen + 13 + static_cast<int>(n) * 20 < kInBytes) {
            uint8_t* o = g_in + g_inLen;
            __builtin_memcpy(o, &g_frame, 4), o[4] = static_cast<uint8_t>(dev), __builtin_memcpy(o + 5, &hr, 4);
            __builtin_memcpy(o + 9, &n, 4);
            if (n) __builtin_memcpy(o + 13, data, n * 20);
            g_inLen += 13 + n * 20;
        }
        (void)cap;
        return hr;
    }
    // PLAY: drain the real buffer (discarded), then hand back the recorded call.
    static uint8_t scratch[64 * 20];
    DWORD want = inout ? *inout : 0;
    DWORD sn = sizeof(scratch) / 20;
    g_origGetData(self, cb, data ? scratch : nullptr, data ? &sn : inout, flags);
    if (!data || !inout || cb != 20) return DI_OK;
    if (g_inPos + 13 <= g_inLen) {
        const uint8_t* r = g_in + g_inPos;
        uint32_t f, n;
        int32_t hr;
        __builtin_memcpy(&f, r, 4), __builtin_memcpy(&hr, r + 5, 4), __builtin_memcpy(&n, r + 9, 4);
        if (static_cast<int>(f) == g_frame && r[4] == dev) {
            if (n > want) n = want, ++g_inputMismatch;
            __builtin_memcpy(data, r + 13, n * 20);
            *inout = n;
            g_inPos += 13 + (*reinterpret_cast<const uint32_t*>(r + 9)) * 20;
            return hr;
        }
    }
    ++g_inputMismatch;  // the game asked for input the recording doesn't have here
    *inout = 0;
    return DI_OK;
}

bool HookDirectInput() {
    using CreateFn = HRESULT(WINAPI*)(HINSTANCE, DWORD, const GUID&, void**, void*);
    HMODULE m = GetModuleHandleA("dinput8.dll");
    auto create = m ? reinterpret_cast<CreateFn>(GetProcAddress(m, "DirectInput8Create")) : nullptr;
    if (!create) return Log("dinput8: no DirectInput8Create"), false;
    static const GUID kIID_DI8A = {0xBF798030, 0x483A, 0x4DA2, {0xAA, 0x99, 0x5D, 0x64, 0xED, 0x36, 0x97, 0x00}};
    static const GUID kSysMouse = {0x6F1D2B60, 0xD5A0, 0x11CF, {0xBF, 0xC7, 0x44, 0x45, 0x53, 0x54, 0x00, 0x00}};
    void* di = nullptr;
    HRESULT hr = create(GetModuleHandleA(nullptr), 0x0800, kIID_DI8A, &di, nullptr);
    if (FAILED(hr) || !di) return Log("dinput8: create failed %08lx", hr), false;
    void** vt = *reinterpret_cast<void***>(di);
    using CreateDevFn = HRESULT(__stdcall*)(void*, const GUID&, void**, void*);
    using ReleaseFn = ULONG(__stdcall*)(void*);
    void* dev = nullptr;
    hr = reinterpret_cast<CreateDevFn>(vt[3])(di, kSysMouse, &dev, nullptr);
    if (FAILED(hr) || !dev) {
        reinterpret_cast<ReleaseFn>(vt[2])(di);
        return Log("dinput8: CreateDevice failed %08lx", hr), false;
    }
    void* getData = (*reinterpret_cast<void***>(dev))[10];
    reinterpret_cast<ReleaseFn>((*reinterpret_cast<void***>(dev))[2])(dev);
    reinterpret_cast<ReleaseFn>(vt[2])(di);
    if (MH_CreateHook(getData, reinterpret_cast<void*>(&HookGetData), reinterpret_cast<void**>(&g_origGetData)) ||
        MH_EnableHook(getData))
        return Log("dinput8: hook on GetDeviceData %08lx failed", reinterpret_cast<unsigned long>(getData)), false;
    Log("dinput8: GetDeviceData hooked at %08lx", reinterpret_cast<unsigned long>(getData));
    return true;
}

// --- traces --------------------------------------------------------------------------------
void Dump(const char* name, const Frame* fr, int n) {
    char path[MAX_PATH];
    wsprintfA(path, "%s%s", g_dir, name);
    HANDLE h = CreateFileA(path, GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS, 0, nullptr);
    static char line[400];
    int o = wsprintfA(line, "frame dt elapsed_rel gstate mstate pos_x pos_y pos_z vel_x vel_y vel_z yaw pitch "
                            "timer time_left mt_left mt_next rng_draws ent_n ent_hash vic_hash other_hash "
                            "(floats and hashes as hex)\r\n");
    Write(h, line, o);
    for (int i = 0; i < n; ++i) {
        const Frame& f = fr[i];
        o = wsprintfA(line, "%d %08lx %lu %ld %ld %08lx %08lx %08lx %08lx %08lx %08lx %08lx %08lx %08lx %08lx %ld %lu "
                            "%lu %lu %08lx %08lx %08lx\r\n",
                      i, f.dt, f.elapsed, f.gstate, f.mstate, f.pos[0], f.pos[1], f.pos[2], f.vel[0], f.vel[1],
                      f.vel[2], f.yaw, f.pitch, f.timer, f.timeLeft, f.mtLeft, f.mtNext, f.rngN, f.entN, f.entHash,
                      f.vicHash, f.fireHash);
        Write(h, line, o);
    }
    if (h != INVALID_HANDLE_VALUE) CloseHandle(h);
}
Frame g_play[kMaxFrames];

// "x" of a float as a decimal with 3 places (wsprintf has no %f)
const char* F(uint32_t bits, char* b) {
    float v;
    __builtin_memcpy(&v, &bits, 4);
    const bool neg = v < 0;
    double d = neg ? -v : v;
    if (d > 2e9) d = 2e9;
    const long i = static_cast<long>(d), f = static_cast<long>((d - i) * 1000);
    wsprintfA(b, "%s%ld.%03ld", neg ? "-" : "", i, f);
    return b;
}

// v2: the first differing frame per category, each logged once.
const char* const kCats[] = {"dt",       "rng draws", "rng callers", "player position", "player velocity",
                             "view",     "move state", "timer +0x32fc", "time left +0x343c", "entity count",
                             "victims",  "other entities (+0x70)", "entity positions"};
constexpr int kNCats = sizeof(kCats) / sizeof(kCats[0]);
int g_catFirst[kNCats];

void Compare(int i, const Frame& a, const Frame& b) {
    bool d[kNCats] = {};
    d[0] = a.dt != b.dt;
    d[1] = a.rngN != b.rngN;
    if (i > 0)
        for (int k = 0; k < kRngLog && k < static_cast<int>(a.rngN); ++k)
            d[2] = d[2] || g_recDraws[i - 1][k][1] != g_playDraws[i - 1][k][1] || g_recDraws[i - 1][k][0] != g_playDraws[i - 1][k][0];
    d[3] = a.pos[0] != b.pos[0] || a.pos[1] != b.pos[1] || a.pos[2] != b.pos[2];
    d[4] = a.vel[0] != b.vel[0] || a.vel[1] != b.vel[1] || a.vel[2] != b.vel[2];
    d[5] = a.yaw != b.yaw || a.pitch != b.pitch;
    d[6] = a.mstate != b.mstate;
    d[7] = a.timer != b.timer;
    d[8] = a.timeLeft != b.timeLeft;
    d[9] = a.entN != b.entN;
    d[10] = a.vicHash != b.vicHash;
    d[11] = a.fireHash != b.fireHash;
    d[12] = a.entHash != b.entHash;
    bool any = false;
    for (int k = 0; k < kNCats; ++k) {
        if (!d[k]) continue;
        any = true;
        if (g_catFirst[k] >= 0) continue;
        g_catFirst[k] = i;
        char x0[24], x1[24], z0[24], z1[24];
        Log("play %d: %s first differs at frame %d (rec pos %s,%s play %s,%s; draws %lu/%lu)", g_playNo, kCats[k], i,
            F(a.pos[0], x0), F(a.pos[2], z0), F(b.pos[0], x1), F(b.pos[2], z1), a.rngN, b.rngN);
        if (k == 1 || k == 2) {  // the draws of the previous frame, both runs
            const int f = i > 0 ? i - 1 : 0;
            char line[600];
            int o = 0;
            for (int j = 0; j < kRngLog && j < static_cast<int>(a.rngN); ++j)
                o += wsprintfA(line + o, " %lx<%lx", g_recDraws[f][j][0], g_recDraws[f][j][1]);
            Log("  rec  frame %d draws:%s", f, o ? line : " none");
            o = 0;
            for (int j = 0; j < kRngLog && j < static_cast<int>(b.rngN); ++j)
                o += wsprintfA(line + o, " %lx<%lx", g_playDraws[f][j][0], g_playDraws[f][j][1]);
            Log("  play frame %d draws:%s", f, o ? line : " none");
        }
    }
    if (!any) return;
    ++g_diffFrames;
    if (g_firstDiff < 0) g_firstDiff = i;
}

void CompareHists();
void EndPlayback(const char* why) {
    char d0[24], d1[24], d2[24], d3[24], d4[24];
    const int last = g_frame > 0 ? g_frame - 1 : 0;  // the last frame both have
    const Frame& a = g_rec[last];
    const Frame& b = g_play[last];
    Log("play %d END (%s): %d/%d frames, time %s, rng %s, frames differing %d, first at %d, input-call mismatches %d",
        g_playNo, why, g_frame, g_recFrames, g_playTime ? "fed" : "real", g_playRng ? "restored" : "not restored",
        g_diffFrames, g_firstDiff, g_inputMismatch);
    float ax, az, bx, bz;
    __builtin_memcpy(&ax, &a.pos[0], 4), __builtin_memcpy(&az, &a.pos[2], 4);
    __builtin_memcpy(&bx, &b.pos[0], 4), __builtin_memcpy(&bz, &b.pos[2], 4);
    const float dx = ax - bx, dz = az - bz;
    Log("play %d frame %d: rec pos (%s, %s) play (%s, %s) dist^2 %s", g_playNo, last, F(a.pos[0], d0),
        F(a.pos[2], d1), F(b.pos[0], d2), F(b.pos[2], d3), F(Bits(dx * dx + dz * dz), d4));
    char name[40];
    wsprintfA(name, "zz_demoprobe_play%d.txt", g_playNo);
    Dump(name, g_play, g_frame);
    CompareHists();
    g_hist = nullptr, g_histOn = nullptr, g_split = false;
    g_mode = IDLE;
}

// --- the frame hook -------------------------------------------------------------------------
using TickFn = void(__fastcall*)(void* self, void* edx);
TickFn g_origTick;
bool g_prev[10];

bool Edge(int vk, int slot) {
    const bool down = (GetAsyncKeyState(vk) & 0x8000) != 0;
    const bool e = down && !g_prev[slot];
    g_prev[slot] = down;
    return e;
}

bool g_rngAtLoad;  // v3: the RNG was saved / restored at LoadLevel for this run
// v4: game time is virtual from LoadLevel on: elapsed = E0 + frames * 1000/60 ms through the
// load (run 3: fire spread and roaming victims differed from frame 0 -- the load's real
// duration was in elapsed). The recording's elapsed values are relative to its E0.
uint32_t g_loadE0;
int g_loadFrames;
bool g_loadSeen;
void OnLoadLevel() {
    if (g_mode == ARM_REC || g_mode == ARM_PLAY) {
        g_loadE0 = Rd<uint32_t>(kClock + 0x44), g_loadFrames = 0, g_loadSeen = true;
        g_hist = g_mode == ARM_REC ? g_recHist : g_playHist;
        g_histOn = g_hist;
        g_split = true, g_private = 0x9E3779B9u;  // same seed in both runs
        for (int i = 0; i < kSlots; ++i) g_hist[i].n = 0;
        g_slot = 0;
    }
    if (g_mode == ARM_REC) {
        Read(kMtState, g_recMt, kMtBytes);
        g_recMtLeft = Rd<int32_t>(kMtLeft), g_recMtNext = Rd<uint32_t>(kMtNext);
        g_rngAtLoad = true;
        Log("LoadLevel: rng saved (left %ld)", g_recMtLeft);
    } else if (g_mode == ARM_PLAY && g_playRng && g_rngAtLoad) {
        __builtin_memcpy(reinterpret_cast<void*>(kMtState), g_recMt, kMtBytes);
        *reinterpret_cast<int32_t*>(kMtLeft) = g_recMtLeft;
        *reinterpret_cast<uint32_t*>(kMtNext) = g_recMtNext;
        Log("LoadLevel: rng restored (left %ld)", g_recMtLeft);
    }
}

// v3: every entity (list order) at a few frames, for the entity-level diff.
void DumpEnts(const char* tag, int frame) {
    if (frame != 0 && frame != 60 && frame != 600 && frame != 1200 && frame != 1800) return;
    char path[MAX_PATH];
    wsprintfA(path, "%szz_demoprobe_ents_%s.txt", g_dir, tag);
    HANDLE h = CreateFileA(path, FILE_APPEND_DATA, FILE_SHARE_READ, nullptr, frame == 0 ? CREATE_ALWAYS : OPEN_ALWAYS,
                           0, nullptr);
    const uintptr_t s = Rd<uintptr_t>(kSession);
    uint32_t node = s ? Rd<uint32_t>(s + 0x64) : 0;
    static char line[200];
    for (int guard = 0, i = 0; node && guard < 4096; ++guard) {
        const uint32_t e = Rd<uint32_t>(node), next = Rd<uint32_t>(node + 4);
        if (e) {
            const int o = wsprintfA(line, "%d %d kind %ld pos %08lx %08lx %08lx s120 %08lx s70 %08lx\r\n", frame, i++,
                                    Rd<int32_t>(e + 0x18), Rd<uint32_t>(e + 0x4C), Rd<uint32_t>(e + 0x50),
                                    Rd<uint32_t>(e + 0x54), Rd<uint32_t>(e + 0x120), Rd<uint32_t>(e + 0x70));
            Write(h, line, o);
        }
        node = next;
    }
    if (h != INVALID_HANDLE_VALUE) CloseHandle(h);
}

void Arm(Mode m, bool time, bool rng) {
    g_mode = m, g_playTime = time, g_playRng = rng, g_seenLoad = false, g_loadSeen = false;
    if (m == ARM_REC) g_rngAtLoad = false;
    Log("armed %s%s%s: waiting for the next level start", m == ARM_REC ? "RECORD" : "PLAYBACK",
        m == ARM_PLAY && !time ? " (real time)" : "", m == ARM_PLAY && !rng ? " (rng not restored)" : "");
}

int g_recSlots;
// v6: gameplay draws (main stream) and cosmetic draws (private stream, top bit) separately:
// the first slot where each differs, with the sites [site:rec/play].
void CompareHists() {
    const int n = g_slot < g_recSlots ? g_slot : g_recSlots;
    for (int cosmetic = 0; cosmetic < 2; ++cosmetic) {
        bool found = false;
        for (int s = 0; s < n && s < kSlots && !found; ++s) {
            const Hist& a = g_recHist[s];
            const Hist& b = g_playHist[s];
            char line[700];
            int o = 0, diffs = 0;
            for (int pass = 0; pass < 2; ++pass) {
                const Hist& x = pass ? b : a;
                const Hist& y = pass ? a : b;
                for (int i = 0; i < x.n; ++i) {
                    if (((x.site[i] >> 31) != 0) != (cosmetic != 0)) continue;
                    uint32_t cy = 0;
                    bool there = false;
                    for (int j = 0; j < y.n; ++j)
                        if (y.site[j] == x.site[i]) cy = y.count[j], there = true;
                    if (pass && there) continue;  // already compared in pass 0
                    if (cy == x.count[i]) continue;
                    ++diffs;
                    if (o < 600)
                        o += wsprintfA(line + o, " %lx:%lu/%lu", x.site[i] & 0x7FFFFFFF, pass ? cy : x.count[i],
                                       pass ? x.count[i] : cy);
                }
            }
            if (!diffs) continue;
            found = true;
            Log("play %d: %s draws first differ at slot %d (0 = inside LoadLevel, k = frame k after it), %d site(s) "
                "[site:rec/play]:%s", g_playNo, cosmetic ? "COSMETIC (private stream)" : "GAMEPLAY (main stream)", s,
                diffs, line);
        }
        if (!found)
            Log("play %d: %s draws identical for all %d slots", g_playNo,
                cosmetic ? "COSMETIC (private stream)" : "GAMEPLAY (main stream)", n);
    }
}

void __fastcall HookTick(void* self, void* edx) {
    g_origTick(self, edx);
    if (g_hist && g_slot >= 0) ++g_slot;
    static bool diHooked = false;
    if (!diHooked) diHooked = HookDirectInput() || true;

    const bool focused = [] {
        DWORD pid = 0;
        GetWindowThreadProcessId(GetForegroundWindow(), &pid);
        return pid == GetCurrentProcessId();
    }();
    if (focused) {
        if (Edge(VK_F5, 0)) Arm(ARM_REC, true, true);
        if (Edge(VK_F6, 1) && g_recFrames) Arm(ARM_PLAY, true, true);
        if (Edge(VK_F8, 2) && g_recFrames) Arm(ARM_PLAY, false, true);
        if (Edge(VK_F9, 4) && g_recFrames) Arm(ARM_PLAY, true, false);
        if (Edge(VK_F7, 3) && g_mode == REC) {
            Log("record STOP (F7): %d frames, %d input bytes", g_recFrames, g_inLen);
            Dump("zz_demoprobe_rec.txt", g_rec, g_recFrames);
            g_recSlots = g_slot, g_hist = nullptr, g_histOn = nullptr, g_split = false;
            g_mode = IDLE;
        }
    }

    const uintptr_t s = Rd<uintptr_t>(kSession);
    const int gstate = s ? Rd<int32_t>(s + 0x6F0) : -1;
    if (g_mode == ARM_REC || g_mode == ARM_PLAY) {
        // v2: the frames before the start (the load: game.state 2 / 3) run at a fixed
        // 1/60 s in both runs, so the level starts from the same state (run 1: the
        // player's height and the timer differed from frame 0 when they ran on real time).
        if (gstate != 1) {
            g_seenLoad = true;
            *reinterpret_cast<uint32_t*>(kClock + 0x4C) = Bits(1.0f / 60.0f);
            if (g_loadSeen) {
                ++g_loadFrames;
                *reinterpret_cast<uint32_t*>(kClock + 0x44) =
                    g_loadE0 + static_cast<uint32_t>(MulDiv(g_loadFrames, 1000, 60));
            }
        }
        // v7: each load frame: entity / fire counts, fires' extents.x (FireFx +0x12C, extents
        // +0x44) and spread timers (+0x124) relative to now, gameplay draws so far this frame.
        if (g_loadSeen) {
            const uintptr_t ss = Rd<uintptr_t>(kSession);
            const uint32_t now = Rd<uint32_t>(kClock + 0x44);
            uint32_t node = ss ? Rd<uint32_t>(ss + 0x64) : 0;
            int ents = 0, fires = 0, armed = 0;
            float ext = 0;
            long timers = 0;
            for (int guard = 0; node && guard < 4096; ++guard) {
                const uint32_t e = Rd<uint32_t>(node), next = Rd<uint32_t>(node + 4);
                if (e) {
                    ++ents;
                    if (Rd<int32_t>(e + 0x18) == 6) {
                        ++fires;
                        float x;
                        const uint32_t xb = Rd<uint32_t>(e + 0x12C + 0x44);
                        __builtin_memcpy(&x, &xb, 4);
                        ext += x;
                        const long t = static_cast<long>(Rd<uint32_t>(e + 0x124)) - static_cast<long>(now);
                        timers += t;
                        armed += t <= 0;
                    }
                }
                node = next;
            }
            char eb[24];
            Log("  load slot %d: state %d ents %d fires %d (timer due %d) sum ext.x %s sum timer-now %ld ms, "
                "draws in prev slot %lu", g_slot, gstate, ents, fires, armed, F(Bits(ext), eb), timers,
                g_slot > 0 && g_hist ? static_cast<unsigned long>(g_hist[g_slot - 1].n) : 0UL);
        }
        if (!(g_seenLoad && gstate == 1)) return;
        Log("level start after %d load frames (LoadLevel %s)", g_loadFrames, g_loadSeen ? "seen" : "NOT seen");
        for (int k = 0; k < kNCats; ++k) g_catFirst[k] = -1;
        g_rngN = 0, g_rngFrame = 0;
        g_rngLog = g_mode == ARM_REC ? g_recDraws : g_playDraws;
        g_frame = 0;
        if (g_mode == ARM_REC) {
            if (!g_rngAtLoad) {  // no LoadLevel seen: save it here (v1 / v2 behaviour)
                Read(kMtState, g_recMt, kMtBytes);
                g_recMtLeft = Rd<int32_t>(kMtLeft), g_recMtNext = Rd<uint32_t>(kMtNext);
            }
            g_rec0Elapsed = g_loadSeen ? g_loadE0 : Rd<uint32_t>(kClock + 0x44);
            g_recFrames = 0, g_inLen = 0;
            g_mode = REC;
            Log("record START at elapsed %lu ms (rng left %ld)", g_rec0Elapsed, g_recMtLeft);
        } else {
            if (g_playRng && !g_rngAtLoad) {
                __builtin_memcpy(reinterpret_cast<void*>(kMtState), g_recMt, kMtBytes);
                *reinterpret_cast<int32_t*>(kMtLeft) = g_recMtLeft;
                *reinterpret_cast<uint32_t*>(kMtNext) = g_recMtNext;
            }
            g_inPos = 0, g_firstDiff = -1, g_diffFrames = 0, g_inputMismatch = 0;
            ++g_playNo;
            g_mode = PLAY, g_playFirst = true;
            Log("play %d START at elapsed %lu ms", g_playNo, Rd<uint32_t>(kClock + 0x44));
        }
    }
    static uint32_t play0Elapsed;
    if (g_mode == REC) {
        if (gstate != 1 || g_recFrames >= kMaxFrames) {
            Log("record STOP (%s): %d frames, %d input bytes", gstate != 1 ? "level left" : "full", g_recFrames, g_inLen);
            Dump("zz_demoprobe_rec.txt", g_rec, g_recFrames);
            g_recSlots = g_slot, g_hist = nullptr, g_histOn = nullptr, g_split = false;
            g_mode = IDLE;
            return;
        }
        Sample(g_rec[g_recFrames]);
        g_rec[g_recFrames].elapsed -= g_rec0Elapsed;
        g_frame = g_recFrames++;
        DumpEnts("rec", g_frame);
        g_rngN = 0, g_rngFrame = g_frame;
    } else if (g_mode == PLAY) {
        // g_frame = this frame's index, as during REC; the input hook matches calls by it.
        if (g_playFirst) g_playFirst = false, play0Elapsed = g_loadSeen ? g_loadE0 : Rd<uint32_t>(kClock + 0x44);
        else ++g_frame;
        if (g_frame >= g_recFrames) return EndPlayback("end of recording");
        if (gstate != 1) return EndPlayback("level left");
        if (g_playTime) {
            *reinterpret_cast<uint32_t*>(kClock + 0x4C) = g_rec[g_frame].dt;
            *reinterpret_cast<uint32_t*>(kClock + 0x44) = play0Elapsed + g_rec[g_frame].elapsed;
        }
        Sample(g_play[g_frame]);
        g_play[g_frame].elapsed -= play0Elapsed;
        Compare(g_frame, g_rec[g_frame], g_play[g_frame]);
        char tag[16];
        wsprintfA(tag, "play%d", g_playNo);
        DumpEnts(tag, g_frame);
        g_rngN = 0, g_rngFrame = g_frame;
    }
}

} // namespace

BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, LPVOID) {
    if (reason != DLL_PROCESS_ATTACH) return TRUE;
    DisableThreadLibraryCalls(inst);
    GetModuleFileNameA(inst, g_dir, MAX_PATH);
    for (char* p = g_dir + lstrlenA(g_dir); p > g_dir; --p)
        if (p[-1] == '\\') {
            *p = 0;
            break;
        }
    char path[MAX_PATH];
    wsprintfA(path, "%szz_demoprobe.log", g_dir);
    g_log = CreateFileA(path, GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS, 0, nullptr);
    Log("demoprobe loaded");
    if (MH_Initialize() != MH_OK && MH_Initialize() != MH_ERROR_ALREADY_INITIALIZED) return TRUE;
    if (MH_CreateHook(reinterpret_cast<void*>(kTick), reinterpret_cast<void*>(&HookTick),
                      reinterpret_cast<void**>(&g_origTick)) ||
        MH_EnableHook(reinterpret_cast<void*>(kTick)))
        Log("hook GameClock::Tick failed");
    else
        Log("hooked GameClock::Tick");
    if (MH_CreateHook(reinterpret_cast<void*>(kNextU32), reinterpret_cast<void*>(&HookNext),
                      reinterpret_cast<void**>(&g_origNext)) ||
        MH_EnableHook(reinterpret_cast<void*>(kNextU32)))
        Log("hook MT19937_NextU32 failed");
    else
        Log("hooked MT19937_NextU32");
    const struct {
        uintptr_t at;
        void* hook;
        void** orig;
        const char* name;
    } more[] = {{kFloat01, reinterpret_cast<void*>(&HookFloat01), reinterpret_cast<void**>(&g_origFloat01), "Rand_Float01"},
                {kLoadLevel, reinterpret_cast<void*>(&HookLoadLevel), reinterpret_cast<void**>(&g_origLoadLevel),
                 "GameSession::LoadLevel"}};
    for (const auto& h : more) {
        if (MH_CreateHook(reinterpret_cast<void*>(h.at), h.hook, h.orig) || MH_EnableHook(reinterpret_cast<void*>(h.at)))
            Log("hook %s failed", h.name);
        else
            Log("hooked %s", h.name);
    }
    return TRUE;
}

extern "C" BOOL WINAPI _DllMainCRTStartup(HINSTANCE inst, DWORD reason, LPVOID reserved) {
    return DllMain(inst, reason, reserved);
}
