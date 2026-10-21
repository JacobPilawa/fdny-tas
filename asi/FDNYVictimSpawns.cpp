// ===========================================================================
//  FDNYVictimSpawns.asi  --  pin FDNY Firefighter victim spawn points per level
// ===========================================================================
//
//  Native port of tools/fdny_spawns.py.  Loaded by Ultimate ASI Loader
//  (dinput8.dll), the same way FDNYFirefighterAmericanHeroFOVFix.asi is.
//
//  One inline hook inside VictimRescueEnt::SpawnAtSpot at 0x0045D717 -- after
//  the game has copied the RNG-chosen spot's coords + set facing, but BEFORE
//  its own ModelEntity::SnapToFloor call.  For each victim we look up the spot
//  index configured for it and overwrite victim.pos with that spot's authored
//  coords; execution then falls through into SnapToFloor, so a forced victim
//  is floor-snapped by the game itself (exact Y, first try).  The RNG stream
//  is never touched.
//
//  Config: FDNYVictimSpawns.ini beside the .asi.  [Spawns] section, one key
//  per map = a comma list of spot indices in .bsp spawn order:
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
static const int OFF_FSMSTATE  = 0x120;   // 4 idle / 5 carried / 6 claimed / 7 roaming
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
static void logline(const char* fmt, ...){
    if (g_log == INVALID_HANDLE_VALUE) return;
    char out[640]; int p = 0;

    SYSTEMTIME st; GetLocalTime(&st);
    p += wsprintfA(out + p, "[%04d-%02d-%02d %02d:%02d:%02d.%03d] [FDNYVictimSpawns] ",
                   st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);

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

static uintptr_t g_base     = IMAGE_BASE;
static bool      g_enabled  = true;
static bool      g_stopRoam = true;
static MapCfg    g_maps[MAX_MAPS];
static int       g_nMaps    = 0;
static uint8_t*  g_tramp    = nullptr;

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

static void load_config(const char* dir){
    char path[MAX_PATH]; wsprintfA(path, "%s\\FDNYVictimSpawns.ini", dir);
    HANDLE f = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    if (f == INVALID_HANDLE_VALUE) { logline("config: %s not found -- nothing pinned", path); return; }

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

                    if (s_ieq(section, "settings")) {
                        char lv[16]; s_lowcpy(lv, val, sizeof lv);
                        bool on = s_ieq(lv, "true") || s_ieq(lv, "1") || s_ieq(lv, "yes");
                        if (s_ieq(key, "enabled"))  g_enabled  = on;
                        if (s_ieq(key, "stoproam")) g_stopRoam = on;
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

    logline("config: %d map(s), enabled=%s stopRoam=%s",
            g_nMaps, g_enabled ? "true" : "false", g_stopRoam ? "true" : "false");
    for (int i = 0; i < g_nMaps; ++i) {
        char s[128]; int p = 0;
        for (int j = 0; j < g_maps[i].n && p < 100; ++j) p += wsprintfA(s + p, "%d ", g_maps[i].spots[j]);
        s[p] = 0;
        logline("config:   %s = [ %s]", g_maps[i].name, s);
    }
}

// ---------------------------------------------------------- current map ------
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
    logline("level: '%s'  (%s)", map, g_cur ? "pinned list found" : "no config for this map");
}

// ------------------------------------------------------------- spawn handler -
// Called from the trampoline with the victim `this`.  Game thread, synchronous,
// inside SpawnAtSpot, right before SnapToFloor.
extern "C" void __cdecl OnVictimSpawn(void* victim){
    if (!g_enabled || !victim) return;

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
        logline("spawn ord=%d: want spot %d out of range (count=%d) -- left on RNG", ord, want, count);
        return;
    }
    float* rec = (float*)arr[want];
    if (!rec) return;

    *(float*)((char*)victim + OFF_POS_X) = rec[0];
    *(float*)((char*)victim + OFF_POS_Y) = rec[1];
    *(float*)((char*)victim + OFF_POS_Z) = rec[2];
    if (g_stopRoam) *(int*)((char*)victim + OFF_FSMSTATE) = 4;

    const char* nm = "?";
    char* blob = *(char**)((char*)victim + OFF_DEFBLOB);
    if (blob) { char* n = *(char**)(blob + BLOB_NAME_OFF); if (n) nm = n; }
    logline("spawn ord=%d '%s': FORCED spot %d -> (%F, %F, %F)  (game floor-snaps)",
            ord, nm, want, (double)rec[0], (double)rec[1], (double)rec[2]);
}

// ------------------------------------------------------------------ hooking --
static void write_protected(void* dst, const void* src, unsigned n){
    DWORD old;
    VirtualProtect(dst, n, PAGE_EXECUTE_READWRITE, &old);
    for (unsigned i = 0; i < n; ++i) ((uint8_t*)dst)[i] = ((const uint8_t*)src)[i];
    VirtualProtect(dst, n, old, &old);
    FlushInstructionCache(GetCurrentProcess(), dst, n);
}

static bool install_hook(){
    uint8_t* hook = (uint8_t*)(g_base + SPAWN_HOOK_RVA);

    MEMORY_BASIC_INFORMATION mbi;
    if (!VirtualQuery(hook, &mbi, sizeof mbi) || mbi.State != MEM_COMMIT ||
        !(mbi.Protect & (PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY)) ||
        (uint8_t*)mbi.BaseAddress + mbi.RegionSize < hook + 7) {
        logline("hook: %x is not committed executable code -- not FDNYFirefighter.exe? ABORT", (int)(uintptr_t)hook);
        return false;
    }
    for (int i = 0; i < 7; ++i) {
        if (hook[i] != SPAWN_ORIG[i]) {
            logline("hook: byte mismatch at +%d (%x != %x) -- wrong game build? ABORT", i, hook[i], SPAWN_ORIG[i]);
            return false;
        }
    }
    g_tramp = (uint8_t*)VirtualAlloc(nullptr, 0x80, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    if (!g_tramp) { logline("hook: VirtualAlloc failed"); return false; }

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

    logline("hook: installed at %x -> trampoline %x (%d bytes)",
            (int)(uintptr_t)hook, (int)(uintptr_t)g_tramp, (int)k);
    return true;
}

// ----------------------------------------------------------------- lifecycle -
static DWORD WINAPI init_thread(LPVOID){
    g_base = (uintptr_t)GetModuleHandleA(nullptr);

    char dir[MAX_PATH]; self_dir(dir, sizeof dir);
    char logpath[MAX_PATH]; wsprintfA(logpath, "%s\\FDNYVictimSpawns.log", dir);
    g_log = CreateFileA(logpath, GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS, 0, nullptr);

    logline("----------");
    logline("FDNYVictimSpawns v0.1  module base %x", (int)g_base);
    load_config(dir);
    if (!g_enabled) { logline("disabled via config -- no hook installed"); return 0; }
    install_hook();
    logline("ready.");
    return 0;
}

BOOL APIENTRY DllMain(HMODULE hMod, DWORD reason, LPVOID){
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hMod);
        CreateThread(nullptr, 0, init_thread, nullptr, 0, nullptr);
    }
    return TRUE;
}
