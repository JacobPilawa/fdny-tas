// ===========================================================================
//  FDNYDeterministic.asi  --  deterministic victims for FDNY Firefighter
// ===========================================================================
//
//  Merges FDNYVictimSpawns.asi (pin each victim's spawn spot per level) with
//  RescueBot deactivation (no random AI rescues on any difficulty).  Loaded
//  by Ultimate ASI Loader (dinput8.dll), like FDNYFirefighterAmericanHeroFOVFix.asi.
//
//  RescueBot  -- two 1-byte code patches, no hooks (see asi/PLAN-norescuebot-merge.md):
//    * RescueBot::TryStartRescue 0x00455230: first byte -> C3 (ret).  It is the
//      sole caller of BeginRescue; both of its callers (Think's difficulty-capped
//      timer, the `runrescuebot` verb) ignore the return.  No bot claims, ever.
//    * RescueBot::Construct 0x004514F0 `mov byte [esi+0x18],1` -> imm8 0
//      (mutes chatter).  enabled=0 is the state training.bsp's `setrescuebot 0`
//      ships with: Think early-outs, so the bot's radio chatter goes too.
//
//  VictimSpawns -- one inline hook inside VictimRescueEnt::SpawnAtSpot at
//    0x0045D717 -- after the game has copied the RNG-chosen spot's coords + set
//    facing, but BEFORE its own ModelEntity::SnapToFloor call.  For each victim
//    we look up the configured spot index and overwrite victim.pos with that
//    spot's authored coords; execution falls through into SnapToFloor, so a
//    forced victim is floor-snapped by the game itself.  The RNG stream is never
//    touched.
//
//  Config: FDNYDeterministic.ini beside the .asi (falls back to an old
//  FDNYVictimSpawns.ini):
//      [RescueBot]
//      Enabled  = false                 ; false = both patches applied
//      [StaticVictimSpawns]
//      Enabled  = true
//      [Spawns]
//      training = 0, 0
//      bar_r    = 0, -1, 0, 0, 2, 0        ; -1 (or x) = leave on the RNG pick
//  0 = editor origin, 1..N = RCS_VictimAltSpot markers.  See victims_reference.md.
//
//  Freestanding: links only kernel32 + user32, no CRT (matches the FOVFix).
//  Build: asi\build.ps1  (bundled zig, -target x86-windows-gnu, 32-bit PE32).
// ===========================================================================

#include <windows.h>
#include <stdint.h>

// ------------------------------------------------------- freestanding shims --
// Built -nostdlib so the .asi imports only kernel32 + user32 (like the FOVFix).
// Provide the handful of symbols the compiler/linker still expects.
extern "C" {
    int _fltused = 0;
    void* memcpy(void* d, const void* s, size_t n){
        uint8_t* dd = (uint8_t*)d; const uint8_t* ss = (const uint8_t*)s;
        while (n--) *dd++ = *ss++;
        return d;
    }
    void* memset(void* d, int c, size_t n){
        uint8_t* dd = (uint8_t*)d; while (n--) *dd++ = (uint8_t)c; return d;
    }
    void* memmove(void* d, const void* s, size_t n){
        uint8_t* dd = (uint8_t*)d; const uint8_t* ss = (const uint8_t*)s;
        if (dd < ss) while (n--) *dd++ = *ss++;
        else { dd += n; ss += n; while (n--) *--dd = *--ss; }
        return d;
    }
    // our own DLL entry -- no CRT startup, no global ctors needed (POD globals only)
    BOOL WINAPI DllMain(HINSTANCE, DWORD, LPVOID);
    BOOL WINAPI _DllMainCRTStartup(HINSTANCE h, DWORD r, LPVOID x){ return DllMain(h, r, x); }
}

// ---------------------------------------------------------------- addresses --
// FDNYFirefighter.exe, image base 0x00400000, no ASLR.  Rebased at runtime.
static const uintptr_t IMAGE_BASE      = 0x00400000;

// RescueBot
static const uintptr_t TRYSTART_RVA    = 0x00055230;  // RescueBot::TryStartRescue  (0x00455230)
static const uintptr_t RBCTOR_EN_RVA   = 0x000514F0;  // RescueBot::Construct: mov byte [esi+0x18],1 (0x004514F0)

// VictimSpawns
static const uintptr_t SPAWN_HOOK_RVA  = 0x0005D717;  // inside SpawnAtSpot, pre-SnapToFloor (0x0045D717)
static const uintptr_t SPAWN_BACK_RVA  = 0x0005D71E;  // resume here            (0x0045D71E)
static const uintptr_t SNAPTOFLOOR_RVA = 0x0009DA00;  // ModelEntity::SnapToFloor(0x0049DA00, ECX=this)
static const uintptr_t GSESSION_PTR_RVA= 0x00102AAC;  // g_session = *(void**)  (0x00502AAC)
static const int       TLI_OFF         = 0x688;       // g_session + 0x688 -> TLevelInfo*
// original 7 bytes at SPAWN_HOOK: MOV ECX,EBP ; CALL SnapToFloor
static const uint8_t   SPAWN_ORIG[7]   = { 0x8B,0xCD,0xE8,0xE2,0x02,0x04,0x00 };

// victim entity offsets (base = `this`)
static const int OFF_SPOTARR   = 0x200;   // -> array of spot-record pointers
static const int OFF_SPOTCOUNT = 0x204;   // int
static const int OFF_POS_X     = 0x4C;    // float x/y/z
static const int OFF_POS_Y     = 0x50;
static const int OFF_POS_Z     = 0x54;
static const int OFF_DEFBLOB   = 0x150;   // -> geEntity def blob
static const int BLOB_NAME_OFF = 0x10;    // -> char* entity name
static const int STRING_CHARS  = 0x0C;    // Genesis3D String: chars at *(strptr)+0x0C

// ---------------------------------------------------------------- tiny libc --
static int   s_len(const char* s){ int n=0; while(s[n]) n++; return n; }
static char  s_low(char c){ return (c>='A'&&c<='Z') ? (char)(c+32) : c; }
static void  s_lowcpy(char* d, const char* s, int cap){ int i=0; for(; s[i] && i<cap-1; i++) d[i]=s_low(s[i]); d[i]=0; }
static bool  s_ieq(const char* a, const char* b){ for(;;++a,++b){ if(s_low(*a)!=s_low(*b)) return false; if(!*a) return true; } }
static int   s_atoi(const char* s){
    while(*s==' '||*s=='\t') s++;
    int sign=1; if(*s=='-'){ sign=-1; s++; } else if(*s=='+') s++;
    int v=0; while(*s>='0'&&*s<='9'){ v=v*10+(*s-'0'); s++; }
    return v*sign;
}

// ---------------------------------------------------------------- log file ---
static HANDLE g_log = INVALID_HANDLE_VALUE;

static void log_raw(const char* s, int n){
    if (g_log != INVALID_HANDLE_VALUE) { DWORD w; WriteFile(g_log, s, (DWORD)n, &w, nullptr); }
}
// append one formatted line.  wvsprintfA (user32) handles %s %d %x %c %u;
// %F is our own: next va_arg is a double, printed with 1 decimal place.
// `tag` names the feature ("rescuebot", "spawns", or "" for general lines).
static void logline(const char* tag, const char* fmt, ...){
    if (g_log == INVALID_HANDLE_VALUE) return;
    char out[640]; int p = 0;

    SYSTEMTIME st; GetLocalTime(&st);
    p += wsprintfA(out + p, "[%04d-%02d-%02d %02d:%02d:%02d.%03d] [FDNYDeterministic]",
                   st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
    p += *tag ? wsprintfA(out + p, "[%s] ", tag) : wsprintfA(out + p, " ");

    va_list ap; va_start(ap, fmt);
    for (const char* f = fmt; *f && p < (int)sizeof(out) - 40; ++f) {
        if (*f != '%') { out[p++] = *f; continue; }
        ++f;
        if (*f == 'F') {                                   // float, 1 dp
            double d = va_arg(ap, double);
            long neg = d < 0; if (neg) d = -d;
            long ip = (long)d;
            long fp = (long)((d - ip) * 10.0 + 0.5);
            if (fp >= 10) { fp = 0; ip++; }
            char tmp[32];
            int q = wsprintfA(tmp, "%s%ld.%ld", neg ? "-" : "", ip, fp);
            for (int i = 0; i < q && p < (int)sizeof(out) - 2; i++) out[p++] = tmp[i];
        } else {
            char spec[4] = { '%', *f, 0, 0 };
            char tmp[128]; int q;
            if (*f == 's')      q = wsprintfA(tmp, spec, va_arg(ap, const char*));
            else                q = wsprintfA(tmp, spec, va_arg(ap, int));
            for (int i = 0; i < q && p < (int)sizeof(out) - 2; i++) out[p++] = tmp[i];
        }
    }
    va_end(ap);
    out[p++] = '\r'; out[p++] = '\n';
    log_raw(out, p);
}

// ---------------------------------------------------------------- config -----
#define MAX_MAPS   32
#define MAX_SPOTS  32
struct MapCfg { char name[24]; int spots[MAX_SPOTS]; int n; };

static uintptr_t g_base       = IMAGE_BASE;
static bool      g_rbEnabled  = false;   // [RescueBot] Enabled
static bool      g_vsEnabled  = true;    // [StaticVictimSpawns] Enabled  (legacy: [Settings] Enabled)
static MapCfg    g_maps[MAX_MAPS];
static int       g_nMaps      = 0;
static uint8_t*  g_tramp      = nullptr;

// per-load state (game thread, synchronous)
static const MapCfg* g_cur = nullptr;
static int           g_ordinal = 0;
static DWORD         g_lastTick = 0;

static void self_dir(char* out, int cap){
    HMODULE self = nullptr;
    GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       (LPCSTR)&self_dir, &self);
    GetModuleFileNameA(self, out, cap);
    for (int i = s_len(out); i >= 0; --i) { if (out[i] == '\\' || out[i] == '/') { out[i] = 0; break; } }
}

static HANDLE open_config(const char* dir, char* path, int cap){
    static const char* const names[] = { "FDNYDeterministic.ini", "FDNYVictimSpawns.ini" };
    for (int i = 0; i < 2; ++i) {
        wsprintfA(path, "%s\\%s", dir, names[i]);
        HANDLE f = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
        if (f != INVALID_HANDLE_VALUE) {
            if (i) logline("", "config: FDNYDeterministic.ini not found -- using legacy %s", path);
            return f;
        }
    }
    (void)cap;
    return INVALID_HANDLE_VALUE;
}

static void load_config(const char* dir){
    char path[MAX_PATH];
    HANDLE f = open_config(dir, path, sizeof path);
    if (f == INVALID_HANDLE_VALUE) { logline("", "config: no FDNYDeterministic.ini -- defaults (bot off, nothing pinned)"); return; }

    DWORD size = GetFileSize(f, nullptr);
    if (size == INVALID_FILE_SIZE || size > (1u << 20)) { CloseHandle(f); return; }
    char* buf = (char*)HeapAlloc(GetProcessHeap(), 0, size + 1);
    DWORD rd = 0; ReadFile(f, buf, size, &rd, nullptr); buf[rd] = 0;
    CloseHandle(f);

    char section[24] = "";
    for (char* p = buf; *p; ) {
        char* eol = p; while (*eol && *eol != '\n') ++eol;
        char* next = (*eol) ? eol + 1 : eol;         // capture BEFORE we null anything
        char* e = eol; if (e > p && e[-1] == '\r') --e;
        *e = 0;                                       // e may == eol; safe now, `next` is saved
        // trim leading ws
        while (*p == ' ' || *p == '\t') ++p;

        if (*p && *p != ';' && *p != '#') {
            if (*p == '[') {
                char* c = p + 1; char* r = c; while (*r && *r != ']') ++r; *r = 0;
                s_lowcpy(section, c, sizeof section);
            } else {
                char* eq = p; while (*eq && *eq != '=') ++eq;
                if (*eq == '=') {
                    *eq = 0;
                    char key[64]; s_lowcpy(key, p, sizeof key);
                    // rtrim key
                    for (int i = s_len(key) - 1; i >= 0 && (key[i]==' '||key[i]=='\t'); --i) key[i] = 0;
                    char* val = eq + 1;
                    while (*val == ' ' || *val == '\t') ++val;
                    char* sc = val; while (*sc && *sc != ';' && *sc != '#') ++sc; *sc = 0;

                    char lv[16]; s_lowcpy(lv, val, sizeof lv);
                    for (int i = s_len(lv) - 1; i >= 0 && (lv[i]==' '||lv[i]=='\t'); --i) lv[i] = 0;
                    bool on = s_ieq(lv, "true") || s_ieq(lv, "1") || s_ieq(lv, "yes");

                    if (s_ieq(section, "rescuebot")) {
                        if (s_ieq(key, "enabled")) g_rbEnabled = on;
                    } else if (s_ieq(section, "staticvictimspawns") || s_ieq(section, "settings")) {
                        if (s_ieq(key, "enabled")) g_vsEnabled = on;
                    } else if (s_ieq(section, "spawns") && g_nMaps < MAX_MAPS && *key) {
                        MapCfg& m = g_maps[g_nMaps++];
                        s_lowcpy(m.name, key, sizeof m.name);
                        m.n = 0;
                        char* t = val;
                        while (*t && m.n < MAX_SPOTS) {
                            while (*t == ' ' || *t == '\t' || *t == ',') ++t;
                            if (!*t) break;
                            char* s = t; while (*t && *t != ',') ++t;
                            char save = *t; *t = 0;
                            // trim token tail
                            char tok[16]; s_lowcpy(tok, s, sizeof tok);
                            for (int i = s_len(tok) - 1; i >= 0 && (tok[i]==' '||tok[i]=='\t'); --i) tok[i] = 0;
                            m.spots[m.n++] = (s_ieq(tok,"x")||s_ieq(tok,"null")||s_ieq(tok,"-")) ? -1 : s_atoi(tok);
                            *t = save;
                        }
                    }
                }
            }
        }
        p = next;
    }
    HeapFree(GetProcessHeap(), 0, buf);

    logline("", "config: %s", path);
    logline("", "config: rescuebot enabled=%s", g_rbEnabled ? "true" : "false");
    logline("", "config: static spawns enabled=%s, %d map(s)", g_vsEnabled ? "true" : "false", g_nMaps);
    for (int i = 0; i < g_nMaps; ++i) {
        char s[128]; int p = 0;
        for (int j = 0; j < g_maps[i].n && p < 100; ++j) p += wsprintfA(s + p, "%d ", g_maps[i].spots[j]);
        s[p] = 0;
        logline("", "config:   %s = [ %s]", g_maps[i].name, s);
    }
}

// ------------------------------------------------------------ code patching --
static void write_protected(void* dst, const void* src, unsigned n){
    DWORD old;
    VirtualProtect(dst, n, PAGE_EXECUTE_READWRITE, &old);
    for (unsigned i = 0; i < n; ++i) ((uint8_t*)dst)[i] = ((const uint8_t*)src)[i];
    VirtualProtect(dst, n, old, &old);
    FlushInstructionCache(GetCurrentProcess(), dst, n);
}

// true if [p, p+n) is committed executable memory
static bool is_code(const uint8_t* p, unsigned n){
    MEMORY_BASIC_INFORMATION mbi;
    return VirtualQuery(p, &mbi, sizeof mbi) && mbi.State == MEM_COMMIT &&
           (mbi.Protect & (PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY)) &&
           (const uint8_t*)mbi.BaseAddress + mbi.RegionSize >= p + n;
}

// A 1-byte patch guarded by a 4-byte signature: sig[] = original bytes at rva,
// sig[at] becomes `want`.  Already-patched bytes count as success (idempotent).
struct BytePatch { const char* name; uintptr_t rva; uint8_t sig[4]; int at; uint8_t want; };

enum PatchState { PATCH_ORIG, PATCH_ALREADY, PATCH_MISMATCH };

static PatchState check_patch(const BytePatch& bp){
    const uint8_t* p = (const uint8_t*)(g_base + bp.rva);
    if (!is_code(p, 4)) return PATCH_MISMATCH;
    for (int i = 0; i < 4; ++i)
        if (i != bp.at && p[i] != bp.sig[i]) return PATCH_MISMATCH;
    if (p[bp.at] == bp.sig[bp.at]) return PATCH_ORIG;
    if (p[bp.at] == bp.want)       return PATCH_ALREADY;
    return PATCH_MISMATCH;
}

// ---------------------------------------------------------------- RescueBot --
static const BytePatch RB_PATCHES[] = {
    // RescueBot::TryStartRescue: push -1 ; push 0x4d0929 (SEH)  ->  ret
    { "TryStartRescue -> ret", TRYSTART_RVA,  { 0x6A, 0xFF, 0x68, 0x29 }, 0, 0xC3 },
    // RescueBot::Construct: mov byte [esi+0x18],1  ->  ,0   (mutes chatter)
    { "ctor enabled 1 -> 0",   RBCTOR_EN_RVA, { 0xC6, 0x46, 0x18, 0x01 }, 3, 0x00 },
};

static void rb_apply(){
    if (g_rbEnabled) { logline("rescuebot", "Enabled=true -- bot left as the game ships it"); return; }
    const int n = sizeof RB_PATCHES / sizeof RB_PATCHES[0];

    // validate every site before writing any (all-or-nothing)
    for (int i = 0; i < n; ++i) {
        const BytePatch& bp = RB_PATCHES[i];
        if (check_patch(bp) == PATCH_MISMATCH) {
            const uint8_t* p = (const uint8_t*)(g_base + bp.rva);
            if (is_code(p, 4))
                logline("rescuebot", "%s @ %x: bytes %x %x %x %x not recognised -- wrong game build? NOTHING PATCHED",
                        bp.name, (int)(uintptr_t)p, p[0], p[1], p[2], p[3]);
            else
                logline("rescuebot", "%s @ %x: not committed executable code -- not FDNYFirefighter.exe? NOTHING PATCHED",
                        bp.name, (int)(uintptr_t)p);
            return;
        }
    }
    for (int i = 0; i < n; ++i) {
        const BytePatch& bp = RB_PATCHES[i];
        uint8_t* p = (uint8_t*)(g_base + bp.rva);
        if (check_patch(bp) == PATCH_ALREADY) {
            logline("rescuebot", "%s @ %x: already patched", bp.name, (int)(uintptr_t)(p + bp.at));
            continue;
        }
        write_protected(p + bp.at, &bp.want, 1);
        logline("rescuebot", "%s @ %x: %x -> %x OK", bp.name, (int)(uintptr_t)(p + bp.at), bp.sig[bp.at], p[bp.at]);
    }
    logline("rescuebot", "disabled on every difficulty (radio chatter muted)");
}

// ------------------------------------------------------------- VictimSpawns --
static void resolve_map(){
    g_cur = nullptr;
    void* session = *(void**)(g_base + GSESSION_PTR_RVA);
    if (!session) return;
    void* tli = *(void**)((char*)session + TLI_OFF);
    if (!tli) return;
    char* ratstr = *(char**)tli;                      // TLevelInfo+0x00 = String -> RatStr
    if (!ratstr) return;
    const char* raw = ratstr + STRING_CHARS;
    if (!*raw) return;
    char map[24]; s_lowcpy(map, raw, sizeof map);
    for (int i = 0; i < g_nMaps; ++i)
        if (s_ieq(g_maps[i].name, map)) { g_cur = &g_maps[i]; break; }
    logline("spawns", "level: '%s'  (%s)", map, g_cur ? "pinned list found" : "no config for this map");
}

// Called from the trampoline with the victim `this`.  Game thread, synchronous,
// inside SpawnAtSpot, right before SnapToFloor.
extern "C" void __cdecl OnVictimSpawn(void* victim){
    if (!g_vsEnabled || !victim) return;

    DWORD now = GetTickCount();
    if (now - g_lastTick > 2000) {          // fresh burst after a quiet gap = new (re)load
        g_ordinal = 0;
        resolve_map();
    }
    g_lastTick = now;
    int ord = g_ordinal++;

    if (!g_cur || ord >= g_cur->n) return;
    int want = g_cur->spots[ord];
    if (want < 0) return;                   // leave on the RNG pick

    int   count = *(int*)((char*)victim + OFF_SPOTCOUNT);
    void** arr  = *(void***)((char*)victim + OFF_SPOTARR);
    if (!arr || want >= count) {
        logline("spawns", "ord=%d: want spot %d out of range (count=%d) -- left on RNG", ord, want, count);
        return;
    }
    float* rec = (float*)arr[want];
    if (!rec) return;

    *(float*)((char*)victim + OFF_POS_X) = rec[0];
    *(float*)((char*)victim + OFF_POS_Y) = rec[1];
    *(float*)((char*)victim + OFF_POS_Z) = rec[2];

    const char* nm = "?";
    char* blob = *(char**)((char*)victim + OFF_DEFBLOB);
    if (blob) { char* n = *(char**)(blob + BLOB_NAME_OFF); if (n) nm = n; }
    logline("spawns", "ord=%d '%s': FORCED spot %d -> (%F, %F, %F)  (game floor-snaps)",
            ord, nm, want, (double)rec[0], (double)rec[1], (double)rec[2]);
}

static bool vs_install_hook(){
    if (!g_vsEnabled) { logline("spawns", "Enabled=false -- no hook installed"); return false; }
    if (GetModuleHandleA("FDNYVictimSpawns.asi"))
        logline("spawns", "WARNING: old FDNYVictimSpawns.asi is also loaded -- remove it from the game folder");

    uint8_t* hook = (uint8_t*)(g_base + SPAWN_HOOK_RVA);
    if (!is_code(hook, 7)) {
        logline("spawns", "hook: %x is not committed executable code -- not FDNYFirefighter.exe? ABORT", (int)(uintptr_t)hook);
        return false;
    }
    for (int i = 0; i < 7; ++i) {
        if (hook[i] != SPAWN_ORIG[i]) {
            logline("spawns", "hook: byte mismatch at +%d (%x != %x) -- wrong game build, or already hooked"
                    " (old FDNYVictimSpawns.asi?) ABORT", i, hook[i], SPAWN_ORIG[i]);
            return false;
        }
    }
    g_tramp = (uint8_t*)VirtualAlloc(nullptr, 0x80, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    if (!g_tramp) { logline("spawns", "hook: VirtualAlloc failed"); return false; }

    uint8_t* t = g_tramp; unsigned k = 0;
    #define PUT1(b)   (t[k++] = (uint8_t)(b))
    #define PUT4(v)   do{ uint32_t _v=(uint32_t)(v); t[k++]=_v&0xFF; t[k++]=(_v>>8)&0xFF; t[k++]=(_v>>16)&0xFF; t[k++]=(_v>>24)&0xFF; }while(0)

    PUT1(0x9C); PUT1(0x60);                                        // pushfd ; pushad
    PUT1(0x55);                                                    // push ebp              (victim this)
    PUT1(0xE8); PUT4((uintptr_t)&OnVictimSpawn - ((uintptr_t)t + k + 4));   // call OnVictimSpawn
    PUT1(0x83); PUT1(0xC4); PUT1(0x04);                            // add esp, 4            (cdecl cleanup)
    PUT1(0x61); PUT1(0x9D);                                        // popad ; popfd
    PUT1(0x8B); PUT1(0xCD);                                        // mov ecx, ebp          (displaced #1)
    PUT1(0xE8); PUT4((g_base + SNAPTOFLOOR_RVA) - ((uintptr_t)t + k + 4)); // call SnapToFloor
    PUT1(0xE9); PUT4((g_base + SPAWN_BACK_RVA)  - ((uintptr_t)t + k + 4)); // jmp back

    uint8_t patch[7] = { 0xE9, 0,0,0,0, 0x90, 0x90 };
    uint32_t rel = (uint32_t)((uintptr_t)g_tramp - ((uintptr_t)hook + 5));
    patch[1] = rel & 0xFF; patch[2] = (rel>>8)&0xFF; patch[3] = (rel>>16)&0xFF; patch[4] = (rel>>24)&0xFF;
    write_protected(hook, patch, 7);

    logline("spawns", "hook: installed at %x -> trampoline %x (%d bytes)",
            (int)(uintptr_t)hook, (int)(uintptr_t)g_tramp, (int)k);
    return true;
}

// ----------------------------------------------------------------- lifecycle -
// Everything runs synchronously in DLL_PROCESS_ATTACH (kernel32/user32 only,
// no loader calls), so the patches are in place before the first level's
// PlayerRescueEnt / victims are constructed.  Each feature fails independently.
BOOL APIENTRY DllMain(HMODULE hMod, DWORD reason, LPVOID){
    if (reason != DLL_PROCESS_ATTACH) return TRUE;
    DisableThreadLibraryCalls(hMod);
    g_base = (uintptr_t)GetModuleHandleA(nullptr);

    char dir[MAX_PATH]; self_dir(dir, sizeof dir);
    char logpath[MAX_PATH]; wsprintfA(logpath, "%s\\FDNYDeterministic.log", dir);
    g_log = CreateFileA(logpath, GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS, 0, nullptr);

    logline("", "----------");
    logline("", "FDNYDeterministic v0.2  module base %x", (int)g_base);
    load_config(dir);
    rb_apply();
    vs_install_hook();
    logline("", "ready.");
    return TRUE;
}
