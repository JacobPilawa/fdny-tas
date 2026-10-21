// hud_glitch record layer (backlog item 11a): a port of the code caves in
// tools/collision_overlay/fdny_collision_overlay.py (GameSource._install).
// The cave code below is emitted byte for byte like the Python Asm emitter
// (same layout, same LOG routine, same entry/exit/writer/commit/tick tails),
// so the record stream is identical to what the Python overlay reads.
//
// Cave layout: 0x00 TICK, 0x04 HEAD, 0x08 INCALL, 0x0C FRESH, 0x40 scratch
// record (128 bytes), 0x100 code, 0x1000 ring of 16384 x 128-byte records.
#include "glitch.h"

#include <windows.h>
#include <tlhelp32.h>

#include "addr.h"
#include "cmd.h"
#include "console.h"
#include "hooks.h"
#include "log.h"

namespace fdcore::glitch {
namespace {

using addr::Id;

constexpr uint32_t kTick = 0x00, kHead = 0x04, kIncall = 0x08, kFresh = 0x0C, kRec = 0x40;
constexpr uint32_t kCode = 0x100, kRing = 0x1000, kRecSize = 128, kRingN = 16384;
constexpr uint32_t kAlloc = kRing + kRecSize * kRingN;
// record dword indices (REC_FMT "<11I3f3ff3fI3f7I")
constexpr int R_KIND = 0, R_TICK = 1, R_RET0 = 2, R_RESULT = 9, R_FRESH = 10, R_IMP = 11, R_NRM = 14,
              R_DIST = 17, R_PRE = 18, R_THIS = 21, R_GPL = 22;
constexpr int kStackSlots[6][2] = {{3, 0x30}, {4, 0x34}, {5, 0x44}, {6, 0x54}, {7, 0x90}, {8, 0xC4}};
constexpr uint32_t KIND_TRACE = 1, KIND_COMMIT = 2, KIND_ENGINE_RAY = 3;

struct Site {
    Id id;
    int len;            // displaced bytes
    int resumeOff;      // jump back to site+resumeOff (0 = cave re-emits a ret tail)
    uintptr_t addr = 0;
    uint8_t orig[16] = {};
    uintptr_t entry = 0;
};
Site g_sites[] = {
    {Id::glitch_entry, 7, 7},  {Id::glitch_exit_box, 5, 0}, {Id::glitch_exit_ray, 5, 0},
    {Id::glitch_exit_miss, 5, 0}, {Id::glitch_boxw, 5, 0},   {Id::glitch_rayw, 10, 10},
    {Id::glitch_commit, 7, 7}, {Id::glitch_tick, 5, 5},
};
enum { S_ENTRY, S_XBOX, S_XRAY, S_XMISS, S_BOXW, S_RAYW, S_COMMIT, S_TICK, S_COUNT };

uint8_t* g_cave = nullptr;
bool g_installed = false;
uint32_t g_lastHead = 0;
volatile long g_counts[4];  // per record kind, for tests
uint32_t g_dropped = 0;

// --- tiny emitter (mirrors the Python Asm class) --------------------------------
struct Asm {
    uint8_t* p;
    uintptr_t pc() const { return reinterpret_cast<uintptr_t>(p); }
    void raw(const uint8_t* b, int n) {
        for (int i = 0; i < n; ++i) *p++ = b[i];
    }
    void b1(uint8_t v) { *p++ = v; }
    void d32(uint32_t v) {
        for (int i = 0; i < 4; ++i) *p++ = static_cast<uint8_t>(v >> (8 * i));
    }
    void push_eax() { b1(0x50); }
    void pop_eax() { b1(0x58); }
    void ret() { b1(0xC3); }
    void mov_abs_eax(uint32_t a) { b1(0xA3), d32(a); }
    void mov_eax_abs(uint32_t a) { b1(0xA1), d32(a); }
    void mov_abs_esi(uint32_t a) { b1(0x89), b1(0x35), d32(a); }
    void mov_abs_imm(uint32_t a, uint32_t v) { b1(0xC7), b1(0x05), d32(a), d32(v); }
    void inc_abs(uint32_t a) { b1(0xFF), b1(0x05), d32(a); }
    void call(uintptr_t t) { b1(0xE8), d32(static_cast<uint32_t>(t - (pc() + 4))); }
    void jmp(uintptr_t t) { b1(0xE9), d32(static_cast<uint32_t>(t - (pc() + 4))); }
    void mov_eax_esp(uint32_t off) {
        if (off < 0x80) b1(0x8B), b1(0x44), b1(0x24), b1(static_cast<uint8_t>(off));
        else b1(0x8B), b1(0x84), b1(0x24), d32(off);
    }
    void mov_eax_esi(uint32_t off) { b1(0x8B), b1(0x46), b1(static_cast<uint8_t>(off)); }
    void copy_esp(uint32_t off, uint32_t dst) { mov_eax_esp(off), mov_abs_eax(dst); }
    void copy_esi(uint32_t off, uint32_t dst) { mov_eax_esi(off), mov_abs_eax(dst); }
    void copy_abs(uint32_t src, uint32_t dst) { mov_eax_abs(src), mov_abs_eax(dst); }
};

uint32_t A(uint32_t off) { return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(g_cave) + off); }
uint32_t Rec(int i) { return A(kRec + 4 * i); }

void Emit() {
    const uint32_t TICK = A(kTick), HEAD = A(kHead), INCALL = A(kIncall), FRESH = A(kFresh);
    const uint32_t RING = A(kRing), REC = A(kRec);
    uintptr_t impact = 0, plane = 0;
    addr::Resolve(Id::genesis_impact, &impact);
    addr::Resolve(Id::genesis_plane, &plane);
    Asm a{g_cave + kCode};

    // LOG: copy REC into ring[head & mask], head++
    const uintptr_t LOG = a.pc();
    static const uint8_t kPush[] = {0x56, 0x57, 0x51};
    a.raw(kPush, 3);
    a.b1(0x8B), a.b1(0x3D), a.d32(HEAD);          // mov edi,[HEAD]
    a.b1(0x81), a.b1(0xE7), a.d32(kRingN - 1);    // and edi,mask
    a.b1(0xC1), a.b1(0xE7), a.b1(0x07);           // shl edi,7
    a.b1(0x81), a.b1(0xC7), a.d32(RING);          // add edi,RING
    a.b1(0xBE), a.d32(REC);                       // mov esi,REC
    a.b1(0xB9), a.d32(kRecSize / 4);              // mov ecx,32
    a.b1(0xFC), a.b1(0xF3), a.b1(0xA5);           // cld; rep movsd
    a.inc_abs(HEAD);
    static const uint8_t kPop[] = {0x59, 0x5F, 0x5E};
    a.raw(kPop, 3);
    a.ret();

    // ENTRY (offsets +4 for the pushed eax)
    Site& en = g_sites[S_ENTRY];
    en.entry = a.pc();
    a.push_eax();
    a.copy_esp(4, Rec(R_RET0));
    for (const auto& s : kStackSlots) a.copy_esp(4 + s[1], Rec(s[0]));
    a.copy_abs(TICK, Rec(R_TICK));
    for (int i = 0; i < 3; ++i) a.copy_abs(static_cast<uint32_t>(impact) + 4 * i, Rec(R_PRE + i));
    a.mov_abs_imm(FRESH, 0);
    a.mov_abs_imm(INCALL, 1);
    a.pop_eax();
    a.raw(en.orig, en.len);
    a.jmp(en.addr + en.resumeOff);

    // EXIT tails: ESI = Col*, EAX = result
    static const int kExits[] = {S_XBOX, S_XRAY, S_XMISS};
    for (int k : kExits) {
        Site& s = g_sites[k];
        s.entry = a.pc();
        a.mov_abs_eax(Rec(R_RESULT));
        a.push_eax();
        a.mov_abs_imm(Rec(R_KIND), KIND_TRACE);
        for (int i = 0; i < 3; ++i) {
            a.copy_esi(0x0C + 4 * i, Rec(R_IMP + i));  // Col->Impact
            a.copy_esi(0x1C + 4 * i, Rec(R_NRM + i));  // Col->Plane.Normal
        }
        a.copy_esi(0x28, Rec(R_DIST));
        for (int i = 0; i < 3; ++i) a.copy_abs(static_cast<uint32_t>(plane) + 4 * i, Rec(R_GPL + i));
        a.copy_abs(FRESH, Rec(R_FRESH));
        a.mov_abs_imm(INCALL, 0);
        a.call(LOG);
        a.pop_eax();
        a.raw(s.orig, s.len);  // pop esi; add esp,24h; ret
    }

    // fresh-write counters
    Site& bw = g_sites[S_BOXW];
    bw.entry = a.pc();
    a.inc_abs(FRESH);
    a.raw(bw.orig, bw.len);  // pop ebx; add esp,2ch; ret
    Site& rw = g_sites[S_RAYW];
    rw.entry = a.pc();
    a.inc_abs(FRESH);
    a.b1(0x83), a.b1(0x3D), a.d32(INCALL), a.b1(0x00);  // cmp dword [INCALL],0
    a.b1(0x75), a.b1(0x00);                             // jnz skip (patched below)
    uint8_t* jnzOperand = a.p - 1;
    a.push_eax();
    a.mov_abs_imm(Rec(R_KIND), KIND_ENGINE_RAY);
    a.mov_abs_imm(Rec(R_RET0), 0);
    a.copy_abs(TICK, Rec(R_TICK));
    for (int i = 0; i < 3; ++i) {
        a.copy_abs(static_cast<uint32_t>(impact) + 4 * i, Rec(R_IMP + i));
        a.copy_abs(static_cast<uint32_t>(plane) + 4 * i, Rec(R_GPL + i));
    }
    a.call(LOG);
    a.pop_eax();
    *jnzOperand = static_cast<uint8_t>(a.p - (jnzOperand + 1));
    a.raw(rw.orig, rw.len);  // mov [latch],1 (live, relocated bytes)
    a.jmp(rw.addr + rw.resumeOff);

    // COMMIT: ESI = entity, [esp+10h..18h] = impact, [esp+34h..3ch] = normal
    Site& cm = g_sites[S_COMMIT];
    cm.entry = a.pc();
    a.push_eax();
    a.mov_abs_imm(Rec(R_KIND), KIND_COMMIT);
    a.copy_abs(TICK, Rec(R_TICK));
    a.mov_abs_esi(Rec(R_THIS));
    for (int i = 0; i < 3; ++i) {
        a.copy_esp(4 + 0x10 + 4 * i, Rec(R_IMP + i));
        a.copy_esp(4 + 0x34 + 4 * i, Rec(R_NRM + i));
        a.copy_esi(addr::Field(Id::player_f_pos) + 4 * i, Rec(R_PRE + i));  // old position
    }
    a.call(LOG);
    a.pop_eax();
    a.raw(cm.orig, cm.len);
    a.jmp(cm.addr + cm.resumeOff);

    // TICK
    Site& tk = g_sites[S_TICK];
    tk.entry = a.pc();
    a.inc_abs(TICK);
    a.raw(tk.orig, tk.len);
    a.jmp(tk.addr + tk.resumeOff);
}

// Suspend every other thread of the process; fail if one is executing inside
// a range we're about to rewrite (retry later).
bool Freeze(HANDLE* held, int* n, int cap) {
    *n = 0;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snap == INVALID_HANDLE_VALUE) return false;
    THREADENTRY32 te;
    te.dwSize = sizeof(te);
    const DWORD pid = GetCurrentProcessId(), me = GetCurrentThreadId();
    bool ok = true;
    for (BOOL more = Thread32First(snap, &te); more && *n < cap; more = Thread32Next(snap, &te)) {
        if (te.th32OwnerProcessID != pid || te.th32ThreadID == me) continue;
        HANDLE t = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT, FALSE, te.th32ThreadID);
        if (!t) continue;
        if (SuspendThread(t) == static_cast<DWORD>(-1)) {
            CloseHandle(t);
            continue;
        }
        held[(*n)++] = t;
        CONTEXT c;
        c.ContextFlags = CONTEXT_CONTROL;
        if (GetThreadContext(t, &c))
            for (const Site& s : g_sites)
                if (c.Eip >= s.addr && c.Eip < s.addr + s.len) ok = false;
    }
    CloseHandle(snap);
    return ok;
}

void Unfreeze(HANDLE* held, int n) {
    for (int i = 0; i < n; ++i) {
        ResumeThread(held[i]);
        CloseHandle(held[i]);
    }
}

void WriteBytes(uintptr_t at, const uint8_t* b, int n) {
    DWORD old;
    VirtualProtect(reinterpret_cast<void*>(at), n, PAGE_EXECUTE_READWRITE, &old);
    for (int i = 0; i < n; ++i) reinterpret_cast<volatile uint8_t*>(at)[i] = b[i];
    VirtualProtect(reinterpret_cast<void*>(at), n, old, &old);
    FlushInstructionCache(GetCurrentProcess(), reinterpret_cast<void*>(at), n);
}

bool PatchAll(bool install) {
    static HANDLE held[512];
    for (int attempt = 0; attempt < 50; ++attempt) {
        int n = 0;
        if (Freeze(held, &n, 512)) {
            for (const Site& s : g_sites) {
                uint8_t patch[16];
                if (install) {
                    patch[0] = 0xE9;
                    uint32_t rel = static_cast<uint32_t>(s.entry - (s.addr + 5));
                    for (int i = 0; i < 4; ++i) patch[1 + i] = static_cast<uint8_t>(rel >> (8 * i));
                    for (int i = 5; i < s.len; ++i) patch[i] = 0x90;
                    WriteBytes(s.addr, patch, s.len);
                } else {
                    WriteBytes(s.addr, s.orig, s.len);
                }
            }
            Unfreeze(held, n);
            return true;
        }
        Unfreeze(held, n);
        Sleep(2);
    }
    return false;
}

bool Install() {
    if (g_installed) return true;
    for (Site& s : g_sites) {
        if (!addr::Usable(s.id)) {
            console::Print("fd_hud_glitch: unavailable (%s not verified)", addr::Name(s.id));
            return false;
        }
        s.addr = addr::ExportAddress(s.id);
        if (!addr::SafeRead(s.addr, s.orig, s.len)) return false;  // live bytes (relocated operands included)
    }
    if (!g_cave) {
        g_cave = static_cast<uint8_t*>(VirtualAlloc(nullptr, kAlloc, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
        if (!g_cave) return false;
    }
    for (uint32_t i = 0; i < kRing; ++i) g_cave[i] = 0;
    Emit();
    g_lastHead = 0;
    if (!PatchAll(true)) {
        log::Error("glitch: could not patch (a thread stayed inside a hook site)");
        return false;
    }
    g_installed = true;
    log::Info("glitch: hooks installed: cave 0x%08lX, Genesis base 0x%08lX",
              reinterpret_cast<unsigned long>(g_cave), static_cast<unsigned long>(addr::ModuleBase(1)));
    return true;
}

void Uninstall() {
    if (!g_installed) return;
    if (PatchAll(false)) {
        g_installed = false;  // the cave stays allocated: nothing may jump into freed memory
        log::Info("glitch: hooks removed, original bytes restored");
    }
}

#ifdef FDCORE_DEBUG
// Debug: the raw stream + one snapshot block per frame (KIND 0xF0), for the
// Python replay driver (core/harness/glitch_replay.py).
HANDLE g_dump = INVALID_HANDLE_VALUE;
void DumpBlock(const uint32_t* rec) {
    if (g_dump == INVALID_HANDLE_VALUE) {
        char path[MAX_PATH];
        wsprintfA(path, "%sFDNYCore.glitchrec", console::GameDir());
        g_dump = CreateFileA(path, GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS, 0, nullptr);
    }
    DWORD w;
    if (g_dump != INVALID_HANDLE_VALUE) WriteFile(g_dump, rec, kRecSize, &w, nullptr);
}

void DumpSnapshot() {
    uint32_t b[32] = {};
    b[0] = 0xF0;
    b[1] = *reinterpret_cast<volatile uint32_t*>(g_cave + kTick);
    uintptr_t slot;
    uint32_t player = 0;
    if (addr::Resolve(Id::player_object, &slot)) addr::SafeRead(slot, &player, 4);
    b[2] = player;
    if (player) {
        int32_t state = 0;
        uint8_t ground = 0, spec = 0;
        addr::SafeRead(player + addr::Field(Id::player_f_state), &state, 4);
        addr::SafeRead(player + addr::Field(Id::player_f_ground), &ground, 1);
        addr::SafeRead(player + addr::Field(Id::player_f_spectator), &spec, 1);
        b[3] = static_cast<uint32_t>(state), b[4] = ground, b[5] = spec;
        addr::SafeRead(player + addr::Field(Id::player_f_pos), &b[6], 12);
        addr::SafeRead(player + addr::Field(Id::player_f_mins), &b[9], 12);
        addr::SafeRead(player + addr::Field(Id::player_f_maxs), &b[12], 12);
        addr::SafeRead(player + addr::Field(Id::player_f_disp), &b[15], 12);
    }
    uintptr_t a;
    if (addr::Resolve(Id::genesis_impact, &a)) addr::SafeRead(a, &b[18], 12);  // slot impact
    if (addr::Resolve(Id::genesis_plane, &a)) addr::SafeRead(a, &b[21], 16);   // slot plane n + dist
    b[25] = static_cast<uint32_t>(addr::ModuleBase(1));  // Genesis base (replay rebases return addresses)
    DumpBlock(b);
}
#endif

void FrameEnd() {
    if (!g_installed) return;
#ifdef FDCORE_DEBUG
    DumpSnapshot();  // like Python: live read first, then the new records
#endif
    const uint32_t head = *reinterpret_cast<volatile uint32_t*>(g_cave + kHead);
    uint32_t n = head - g_lastHead;
    if (n > kRingN) {
        g_dropped += n - kRingN;
        log::Warn("glitch: ring overflow: %lu records lost", static_cast<unsigned long>(n - kRingN));
        g_lastHead = head - kRingN;
        n = kRingN;
    }
    for (uint32_t i = 0; i < n; ++i) {
        const uint32_t* rec =
            reinterpret_cast<const uint32_t*>(g_cave + kRing + ((g_lastHead + i) & (kRingN - 1)) * kRecSize);
        uint32_t kind = rec[R_KIND];
        if (kind < 4) InterlockedIncrement(&g_counts[kind]);
#ifdef FDCORE_DEBUG
        DumpBlock(rec);
#endif
    }
    g_lastHead = head;
}

void OnToggle(cmd::Cvar& v) {
    if (v.value[0] != '0') {
        if (!Install()) console::Print("fd_hud_glitch: hooks could not be installed (see FDNYCore.log)");
    } else {
        Uninstall();
    }
}
cmd::Cvar g_fdHudGlitch = {"fd_hud_glitch", "0",
                           "1 = collision-warp HUD (installs collision hooks; practice only)", false, true,
                           OnToggle, {0}};

} // namespace

bool Init() {
    cmd::RegisterCvar(&g_fdHudGlitch);
    hooks::OnFrameEnd(FrameEnd);
    log::Info("glitch: ready (fd_hud_glitch)");
    return true;
}

bool Installed() { return g_installed; }

int Stats(char* out, int cap) {
    if (cap < 128) return 0;
    uint32_t tick = g_cave ? *reinterpret_cast<volatile uint32_t*>(g_cave + kTick) : 0;
    return wsprintfA(out, "installed=%d tick=%lu trace=%ld commit=%ld engine_ray=%ld dropped=%lu",
                     g_installed ? 1 : 0, static_cast<unsigned long>(tick), g_counts[KIND_TRACE],
                     g_counts[KIND_COMMIT], g_counts[KIND_ENGINE_RAY], static_cast<unsigned long>(g_dropped));
}

} // namespace fdcore::glitch
