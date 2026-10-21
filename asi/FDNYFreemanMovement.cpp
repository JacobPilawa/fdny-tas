// ===========================================================================
//  FDNYFreemanMovement.asi  --  Half-Life movement in F.D.N.Y. Firefighter
// ===========================================================================
//
//  Glue between gaycoderprincess/FreemanAPI (FreemanAPI_gcp.dll -- a port of
//  Half-Life 1/2 PM_PlayerMove) and FDNY's PlayerRescueEnt.  Loaded by
//  Ultimate ASI Loader alongside FreemanAPI_gcp.dll.
//
//  === v0.3 -- additive replace on a known-safe hook, with the v0.2 research ===
//  The mid-Think byte-splice (v0.2) was abandoned: FDNY hand-inlines its move
//  step, and reproducing the exact register/stack contract at the splice
//  boundary proved too fragile.  v0.3 goes back to the hook that never
//  crashed -- after GameSession::TickSubsystems, at the safe point
//  0x0042af0f -- and keeps every derived fix from the architecture pass:
//
//   * FDNY's own Think still runs the player move; each frame, AFTER the tick,
//     the glue overwrites the player's position/velocity with FreemanAPI's.
//     Per clusters 3-4 the camera + animation re-derive from the entity
//     position we just wrote (UpdateLocomotionAndTools / RenderFrame run
//     after our hook), so "two physics fighting" is mostly cosmetic here.
//   * Position: glue-authoritative g_pos/g_vel, seeded from the entity and
//     re-seeded (+ FreemanAPI_ResetPhysics) on first sight / any >500u jump.
//   * View angle -> FreemanAPI's default rotate order (idx0=YAW, idx1=PITCH):
//     array[0] = -yaw, array[1] = -pitch, array[2] = 0.  Derived closed-form.
//   * Origin: FreemanAPI game-Y = FDNY entity-Y + 6  (feet = entity-30;
//     FreemanAPI HL2-hull centre-up = 36; 36-30 = 6).  ResetPhysics puts
//     FreemanAPI's internal origin at the true feet.
//   * Input from FDNY's own GameControls (IsHeld "MOVE_FORWARD" ...), not
//     GetAsyncKeyState.  (No FDNY sprint -> HL sprint stays on Shift.)
//   * Collision -> Genesis3D geWorld_Collision (IAT 0x004dc0bc), world ptr via
//     roc[0x160] ? roc+0xb4 : roc+0x08, then +0x78.  Traces are already in
//     FreemanAPI's feet-frame -- no Y conversion.
//   * onground for the animation code: taken from FreemanAPI's own downward
//     hull traces as they pass through cb_trace_hull.
//
//  INSERT = toggle HL movement on/off (live A/B).   '/' = dump state.
//
//  Only touches the player while state (entity+0x18c) is 3 (stand) / 4
//  (crouch), not spectator (entity+0x380), not move-frozen (entity+0x345c).
//  Everything else is left to vanilla FDNY.
// ===========================================================================

#include <windows.h>
#include <stdint.h>

// ------------------------------------------------------- freestanding shims --
extern "C" {
    int _fltused = 0;
    void* memcpy(void* d, const void* s, size_t n){ auto a=(uint8_t*)d; auto b=(const uint8_t*)s; while(n--)*a++=*b++; return d; }
    void* memset(void* d, int c, size_t n){ auto a=(uint8_t*)d; while(n--)*a++=(uint8_t)c; return d; }
    size_t strlen(const char* s){ size_t n=0; while(s[n]) n++; return n; }
    BOOL WINAPI DllMain(HINSTANCE, DWORD, LPVOID);
    BOOL WINAPI _DllMainCRTStartup(HINSTANCE h, DWORD r, LPVOID x){ return DllMain(h,r,x); }
}
static double f_sqrt(double x){ double r; __asm__("fsqrt":"=t"(r):"0"(x)); return r; }

// ---------------------------------------------------------------- addresses --
static const uintptr_t IMAGE_BASE     = 0x00400000;
static const uintptr_t TICK_CALL_RVA  = 0x0002af0a;   // CALL GameSession::TickSubsystems (E8 rel32)
static const uintptr_t TICK_BACK_RVA  = 0x0002af0f;   // resume here after our trampoline
static const uintptr_t TICKSUBSYS_RVA = 0x00029520;   // GameSession::TickSubsystems
static const uintptr_t GFRAMEDT_RVA   = 0x00102b04;   // float g_frameDeltaTime  (0x00502b04)
static const uintptr_t GGAMECTX_RVA   = 0x00103a78;   // g_everGame = *(void**)   (0x00503a78)
static const uintptr_t IAT_GEWORLDCOL = 0x000dc0bc;   // IAT slot -> GENESIS.DLL geWorld_Collision
static const uintptr_t RVA_ISHELD     = 0x00038480;   // GameControls::IsHeld (__thiscall(ActionMap*, char***))

// g_everGame layout
static const int  EG_CTRL   = 0x00;   // *(void**)g_everGame -> GameControls instance
static const int  EG_ROC    = 0x04;   // -> ROCRender
static const int  EG_HT     = 0x10;   // -> EntityHandleTable
static const int  EG_PLAYER = 0x30;   // -> the player entity (Construct stores it here)
static const int  GC_ACTIONMAP = 0x334;
static const int  HT_SLOTS_OFF = 0x04, HT_NODE_ENT = 0x08, HT_COUNT = 512;
// ROCRender world-ptr chain
static const int  ROC_FLAG   = 0x160, ROC_WRES_L = 0xb4, ROC_WRES_S = 0x08, WRES_WORLD = 0x78;
// PlayerRescueEnt offsets
static const int OFF_KIND       = 0x18;    // == 1 for the player
static const int OFF_POS        = 0x4c;    // float xyz position (Y-up)
static const int OFF_DELTA      = 0x90;    // float xyz this-frame move delta (animation reads it)
static const int OFF_ONGROUND   = 0x12c;   // byte: nonzero = on ground
static const int OFF_ONGROUND_P = 0x12d;   // byte: previous-frame onground
static const int OFF_VEL        = 0xfc;    // float xyz velocity
static const int OFF_STATE      = 0x18c;   // 3 stand / 4 crouch / 5 climb-spectator / 6 passed-out / 7 scripted
static const int OFF_SPECT      = 0x380;   // byte: spectator/noclip
static const int OFF_MOVEFREEZE = 0x345c;  // int: nonzero = movement input frozen
static const int OFF_PITCH      = 0x3430;  // float degrees, +ve = looking up
static const int OFF_YAW        = 0x3434;  // float degrees

static const double ORIGIN_Y_FIX = 6.0;   // FreemanAPI game-Y = FDNY entity-Y + this

// original 5 bytes at TICK_CALL: E8 <rel32 to 0x00429520>
static const uint8_t TICK_ORIG0 = 0xE8;

// --------------------------------------------------------------- FreemanAPI --
typedef void (__cdecl *fn_reg_getv)(void(*)(double*));
typedef void (__cdecl *fn_reg_setpv)(void(*)(const double*, const double*));
typedef void (__cdecl *fn_reg_getf)(float(*)());
typedef void (__cdecl *fn_reg_getb)(bool(*)());
typedef void (__cdecl *fn_reg_snd)(void(*)(const char*, float));
typedef void (__cdecl *fn_reg_falld)(void(*)(float));
struct pmtrace_t {                     // must match FreemanAPI hl_types.h
    size_t size; bool allsolid, startsolid, inopen, inwater;
    float fraction; double endpos[3];
    double pn[3]; float pdist; int ent; int surfaceId;
};
typedef pmtrace_t* (__cdecl *fn_reg_trace)(pmtrace_t*(*)(const double*, const double*));
typedef int  (__cdecl *fn_reg_pc)(int(*)(const double*));
typedef void (__cdecl *fn_process)(double);
typedef void (__cdecl *fn_b)(bool);
typedef void (__cdecl *fn_i)(int);
typedef void (__cdecl *fn_iii)(int,int,int);
typedef void (__cdecl *fn_void)();
typedef double* (__cdecl *fn_getd)();
typedef int  (__thiscall *fn_isheld)(void*, void*);

static HMODULE    g_fapi = nullptr;
static fn_process p_Process      = nullptr;
static fn_getd    p_BBoxMin      = nullptr, p_BBoxMax = nullptr;
static fn_i       p_SetMoveType  = nullptr;
static fn_void    p_ResetPhysics = nullptr;

// ------------------------------------------------------------------ globals --
static uintptr_t g_base    = IMAGE_BASE;
static HANDLE    g_log     = INVALID_HANDLE_VALUE;
static bool      g_enabled = true;
static uint8_t*  g_tramp   = nullptr;
static char*     g_player  = nullptr;
static bool      g_seeded  = false;
static double    g_pos[3]  = {0,0,0};     // glue-authoritative position (FreemanAPI game frame)
static double    g_vel[3]  = {0,0,0};     // glue-authoritative velocity
static bool      g_groundish = false;     // set by cb_trace_hull on a downward walkable hit
static float     g_hmn[3]  = {-16,0,-16};
static float     g_hmx[3]  = { 16,72,16};

#define TOGGLE_KEY  VK_INSERT
#define DUMP_KEY    VK_OEM_2   // '/'

// ------------------------------------------------------------------- logging --
static void L(const char* s){
    if (g_log == INVALID_HANDLE_VALUE) return;
    SYSTEMTIME st; GetLocalTime(&st);
    char h[48]; int n = wsprintfA(h, "[%02d:%02d:%02d] ", st.wHour, st.wMinute, st.wSecond);
    DWORD w; WriteFile(g_log, h, n, &w, nullptr);
    int m = 0; while (s[m]) m++;
    WriteFile(g_log, s, m, &w, nullptr);
    WriteFile(g_log, "\r\n", 2, &w, nullptr);
}
static void Lx(const char* s, int v){ char b[128]; wsprintfA(b, "%s %d", s, v); L(b); }
#define ONCE(s) do{ static bool _o=false; if(!_o){_o=true; L(s);} }while(0)

static bool  rok(const void* p, unsigned n){ return p && !IsBadReadPtr(p, n); }
static void* rd_ptr(const void* p){ return rok(p,4) ? *(void**)p : nullptr; }
static int   rd_i32(const void* p){ return rok(p,4) ? *(int*)p : 0; }
static float rd_f32(const void* p){ return rok(p,4) ? *(float*)p : 0.0f; }

// ----------------------------------------------------------- player discovery --
static bool is_player(char* e){ return e && rok(e, 0x3600) && *(int*)(e + OFF_KIND) == 1; }
static char* find_player(){
    if (is_player(g_player)) return g_player;
    g_player = nullptr; g_seeded = false;
    char* eg = (char*)rd_ptr((void*)(g_base + GGAMECTX_RVA));
    if (!eg || !rok(eg, 0x40)) return nullptr;
    char* direct = (char*)rd_ptr(eg + EG_PLAYER);
    if (is_player(direct)) { g_player = direct; ONCE("player: direct +0x30"); return direct; }
    char* ht = (char*)rd_ptr(eg + EG_HT);
    if (!ht || !rok(ht, 0x804)) return nullptr;
    for (int i = 1; i < HT_COUNT; ++i) {
        char* node = (char*)rd_ptr(ht + HT_SLOTS_OFF + i * 4);
        if (!node || !rok(node, 12)) continue;
        char* ent0 = (char*)rd_ptr(node + HT_NODE_ENT);
        if (is_player(ent0)) { g_player = ent0; ONCE("player: handle walk"); return ent0; }
    }
    return nullptr;
}

// ------------------------------------------------------------ collision bridge --
typedef int (__cdecl *fn_geColl)(void* World, const float* Mins, const float* Maxs,
                                 const float* Front, const float* Back, uint32_t Contents,
                                 uint32_t Flags, void* StopModel, void* CB, void* Ctx, void* Out);

static void* get_world(){
    char* eg  = (char*)rd_ptr((void*)(g_base + GGAMECTX_RVA));
    char* roc = eg ? (char*)rd_ptr(eg + EG_ROC) : nullptr;
    if (!roc) return nullptr;
    int   flag  = rok(roc + ROC_FLAG, 1) ? *(uint8_t*)(roc + ROC_FLAG) : 0;
    int   twOff = flag ? ROC_WRES_L : ROC_WRES_S;
    return rok(roc + twOff + WRES_WORLD, 4) ? *(void**)(roc + twOff + WRES_WORLD) : nullptr;
}

static int g_traceLog = 10;
static void do_trace(const double* o, const double* e, const float* mn, const float* mx, pmtrace_t* tr){
    tr->size = sizeof(*tr); tr->allsolid = tr->startsolid = false; tr->inopen = true; tr->inwater = false;
    tr->fraction = 1.0f; tr->endpos[0]=e[0]; tr->endpos[1]=e[1]; tr->endpos[2]=e[2];
    tr->pn[0]=tr->pn[1]=tr->pn[2]=0; tr->pdist = 9999; tr->ent = -1; tr->surfaceId = 0;

    void* world = get_world();
    fn_geColl gec = rok((void*)(g_base + IAT_GEWORLDCOL), 4) ? *(fn_geColl*)(g_base + IAT_GEWORLDCOL) : nullptr;
    if (!gec || !world) { ONCE("trace: world/gec null"); return; }

    float front[3] = { (float)o[0], (float)o[1], (float)o[2] };
    float back[3]  = { (float)e[0], (float)e[1], (float)e[2] };
    uint8_t out[0x30]; memset(out, 0, sizeof out);

    int hit = gec(world, mn, mx, front, back, 0x43, 2, nullptr, nullptr, nullptr, out);
    if (g_traceLog > 0) { g_traceLog--;
        float* imp = (float*)(out + 0x0c); float* nrm = (float*)(out + 0x1c);
        char b[224];
        wsprintfA(b, "tr %s f(%d,%d,%d) b(%d,%d,%d) HIT=%d imp(%d,%d,%d) n(%d,%d,%d)*100",
            (mn?"HULL":"ray"),
            (int)front[0],(int)front[1],(int)front[2], (int)back[0],(int)back[1],(int)back[2], hit,
            hit?(int)imp[0]:0, hit?(int)imp[1]:0, hit?(int)imp[2]:0,
            hit?(int)(nrm[0]*100):0, hit?(int)(nrm[1]*100):0, hit?(int)(nrm[2]*100):0);
        L(b);
    }
    if (!hit) return;

    float* impact = (float*)(out + 0x0c);
    float* normal = (float*)(out + 0x1c);
    float  pdist  = *(float*)(out + 0x28);
    void*  model  = *(void**)(out + 0x00);
    int    ssolid = *(int*)(out + 0x18);
    tr->startsolid = tr->allsolid = (ssolid != 0);
    double ix = impact[0], iy = impact[1], iz = impact[2];
    double dx = ix-o[0], dy = iy-o[1], dz = iz-o[2];
    double dex= e[0]-o[0], dey= e[1]-o[1], dez= e[2]-o[2];
    double num = f_sqrt(dx*dx+dy*dy+dz*dz);
    double den = f_sqrt(dex*dex+dey*dey+dez*dez);
    tr->fraction = (den > 1e-6) ? (float)(num/den) : 0.0f;
    if (tr->fraction > 1.0f) tr->fraction = 1.0f;
    tr->endpos[0]=ix; tr->endpos[1]=iy; tr->endpos[2]=iz;
    tr->pn[0]=normal[0]; tr->pn[1]=normal[1]; tr->pn[2]=normal[2];
    tr->pdist = pdist;
    tr->ent = model ? 1 : 0;
    tr->inopen = (tr->fraction >= 1.0f);
}

// ------------------------------------------------------------- EXT callbacks --
static void cb_getpos(double* o){ o[0]=g_pos[0]; o[1]=g_pos[1]; o[2]=g_pos[2]; }
static void cb_getvel(double* o){ o[0]=g_vel[0]; o[1]=g_vel[1]; o[2]=g_vel[2]; }
static void cb_setpos(const double* p, const double* v){
    g_pos[0]=p[0]; g_pos[1]=p[1]; g_pos[2]=p[2];
    g_vel[0]=v[0]; g_vel[1]=v[1]; g_vel[2]=v[2];
}
static void cb_getview(double* o){
    // FreemanAPI default rotate order: idx 0 = YAW, idx 1 = PITCH, idx 2 = ROLL.
    double pitch = rd_f32(g_player + OFF_PITCH);
    double yaw   = rd_f32(g_player + OFF_YAW);
    o[0] = -yaw;
    o[1] = -pitch;
    o[2] = 0;
}
static bool cb_dead(){
    char* p = g_player;
    if (!p) return true;
    return rd_i32(p + OFF_STATE) == 6 || rd_i32(p + OFF_MOVEFREEZE) != 0;
}
static void cb_sound(const char*, float){}
static void cb_falldmg(float){}

static bool kd(int vk){ return (GetAsyncKeyState(vk) & 0x8000) != 0; }
static float cb_fwd(){ return (kd('W')?1.f:0.f) - (kd('S')?1.f:0.f); }
static float cb_side(){ return (kd('A')?1.f:0.f) - (kd('D')?1.f:0.f); }  // FreemanAPI +smove = the derived-right dir; A is +
static float cb_up(){ return 0.0f; }
static bool  cb_jump(){ return kd(VK_SPACE); }
static bool  cb_duck(){ return kd(VK_CONTROL); }
static bool  cb_use(){  return kd('E'); }
static bool  cb_run(){  return kd(VK_SHIFT); }

static void live_hull(float* mn, float* mx){
    if (p_BBoxMin && p_BBoxMax) {
        double* a = p_BBoxMin(); double* b = p_BBoxMax();
        if (rok(a,24) && rok(b,24)) { for (int i=0;i<3;i++){ mn[i]=(float)a[i]; mx[i]=(float)b[i]; } return; }
    }
    mn[0]=mn[2]=-16; mn[1]=0; mx[0]=mx[2]=16; mx[1]=72;
}
static pmtrace_t* cb_trace_hull(const double* o, const double* e){
    static pmtrace_t tr;
    live_hull(g_hmn, g_hmx);
    do_trace(o, e, g_hmn, g_hmx, &tr);
    // FreemanAPI's ground check is a short downward hull trace -- reuse it
    if (e[1] < o[1] - 0.01 && tr.fraction < 1.0f && tr.pn[1] >= 0.7) g_groundish = true;
    return &tr;
}
static pmtrace_t* cb_trace_ray(const double* o, const double* e){
    static pmtrace_t tr;
    do_trace(o, e, nullptr, nullptr, &tr);
    return &tr;
}
static int cb_pointcontents(const double*){ return 3; /* CONTENTS_EMPTY */ }

// --------------------------------------------------------------- toggle key --
struct Key { bool prev; DWORD tLast; };
static bool key_hit(Key& k, int vk, DWORD cooldownMs){
    bool d = (GetAsyncKeyState(vk) & 0x8000) != 0;
    bool edge = d && !k.prev; k.prev = d;
    if (!edge) return false;
    DWORD now = GetTickCount();
    if (now - k.tLast < cooldownMs) return false;
    k.tLast = now; return true;
}
static void dump_state(char* e){
    int spd = (int)f_sqrt(g_vel[0]*g_vel[0] + g_vel[2]*g_vel[2]);
    char b[224];
    wsprintfA(b, "state on=%d st=%d epos(%d,%d,%d) g_pos(%d,%d,%d) vel(%d,%d,%d) spd2d=%d yaw=%d pitch=%d og=%d",
        g_enabled?1:0, rd_i32(e+OFF_STATE),
        (int)rd_f32(e+OFF_POS+0),(int)rd_f32(e+OFF_POS+4),(int)rd_f32(e+OFF_POS+8),
        (int)g_pos[0],(int)g_pos[1],(int)g_pos[2],
        (int)g_vel[0],(int)g_vel[1],(int)g_vel[2], spd,
        (int)rd_f32(e+OFF_YAW), (int)rd_f32(e+OFF_PITCH),
        rok(e+OFF_ONGROUND,1)?*(uint8_t*)(e+OFF_ONGROUND):-1);
    L(b);
}
static void poll_keys(char* e){
    static Key kTog{}, kDmp{};
    if (key_hit(kTog, TOGGLE_KEY, 400)) { g_enabled = !g_enabled; g_seeded = false; Lx("HL movement =", g_enabled); }
    if (key_hit(kDmp, DUMP_KEY,   250)) dump_state(e);
}

// ------------------------------------------------------------------ per frame --
// Runs right after GameSession::TickSubsystems (so FDNY's Think has already
// moved the player this frame); returns to the safe point 0x0042af0f.
extern "C" void __cdecl frame_tick(double dt){
    ONCE("frame_tick: first entry");
    if (!p_Process) return;
    char* p = find_player();
    if (!p) return;
    poll_keys(p);
    if (!g_enabled) { g_seeded = false; return; }

    int  state     = rd_i32(p + OFF_STATE);
    bool spectator = rok(p+OFF_SPECT,1) && *(uint8_t*)(p + OFF_SPECT) != 0;
    bool frozen    = rd_i32(p + OFF_MOVEFREEZE) != 0;
    if ((state != 3 && state != 4) || spectator || frozen) { g_seeded = false; return; }
    if (!(dt > 0.0 && dt < 0.5)) dt = 1.0/60.0;
    { static bool _o=false; if(!_o){_o=true;
        char b[96]; wsprintfA(b, "movement active -- dt*1e5=%d (%dfps)",
            (int)(dt*100000.0), dt>0?(int)(1.0/dt):-1); L(b); } }

    float* epos = (float*)(p + OFF_POS);
    float* evel = (float*)(p + OFF_VEL);
    float* edsp = (float*)(p + OFF_DELTA);
    if (!rok(epos,12) || !rok(evel,12) || !rok(edsp,12)) return;

    double gx = epos[0], gyF = (double)epos[1] + ORIGIN_Y_FIX, gz = epos[2];
    double dx = gx-g_pos[0], dy = gyF-g_pos[1], dz = gz-g_pos[2];
    if (!g_seeded || (dx*dx+dy*dy+dz*dz) > 500.0*500.0) {
        g_pos[0]=gx; g_pos[1]=gyF; g_pos[2]=gz;
        g_vel[0]=evel[0]; g_vel[1]=evel[1]; g_vel[2]=evel[2];
        g_seeded = true;
        if (p_ResetPhysics) p_ResetPhysics();   // FreemanAPI reads our pos only here -> snap internal origin to feet
        char b[128]; wsprintfA(b, "seed g_pos(%d,%d,%d)", (int)gx,(int)gyF,(int)gz); L(b);
    }

    if (p_SetMoveType) p_SetMoveType(3);        // MOVETYPE_WALK
    g_groundish = false;

    // input trace (capped): confirm the movement callbacks see the keyboard
    {
        static int inLog = 60;
        static float lf=-9, ls=-9; static int lj=-1;
        float f = cb_fwd(), s = cb_side(); int j = cb_jump()?1:0;
        if (inLog > 0 && (f!=lf || s!=ls || j!=lj)) { inLog--; lf=f; ls=s; lj=j;
            char b[192];
            wsprintfA(b, "input fwd=%d side=%d jump=%d duck=%d | g_vel(%d,%d,%d)",
                (int)f,(int)s,j, cb_duck()?1:0, (int)g_vel[0],(int)g_vel[1],(int)g_vel[2]);
            L(b);
        }
    }

    double px = g_pos[0], py = g_pos[1], pz = g_pos[2];
    ONCE("frame_tick: first Process");
    p_Process(dt);
    ONCE("frame_tick: first Process returned");
    if (g_traceLog < 0) { } // (kept: trace log self-caps)
    { static int vLog = 40;
      if (vLog > 0 && (g_pos[0]!=px || g_pos[2]!=pz)) { vLog--;
          char b[160]; wsprintfA(b, "moved g_pos(%d,%d,%d) g_vel(%d,%d,%d)",
              (int)g_pos[0],(int)g_pos[1],(int)g_pos[2],(int)g_vel[0],(int)g_vel[1],(int)g_vel[2]); L(b); }
    }

    // overwrite the entity with FreemanAPI's result (G frame -> entity frame: Y-6)
    float nx = (float)g_pos[0], ny = (float)(g_pos[1] - ORIGIN_Y_FIX), nz = (float)g_pos[2];
    edsp[0] = nx - epos[0]; edsp[1] = ny - epos[1]; edsp[2] = nz - epos[2];
    epos[0] = nx; epos[1] = ny; epos[2] = nz;
    evel[0] = (float)g_vel[0]; evel[1] = (float)g_vel[1]; evel[2] = (float)g_vel[2];
    if (rok(p + OFF_ONGROUND, 2)) {
        *(uint8_t*)(p + OFF_ONGROUND_P) = *(uint8_t*)(p + OFF_ONGROUND);
        *(uint8_t*)(p + OFF_ONGROUND)   = g_groundish ? 1 : 0;
    }
    (void)px; (void)py; (void)pz;
    ONCE("frame_tick: first writeback ok");
}

// ------------------------------------------------------------------- hooking --
static void wprot(void* d, const void* s, unsigned n){
    DWORD o; VirtualProtect(d, n, PAGE_EXECUTE_READWRITE, &o);
    for (unsigned i=0;i<n;i++) ((uint8_t*)d)[i] = ((const uint8_t*)s)[i];
    VirtualProtect(d, n, o, &o);
    FlushInstructionCache(GetCurrentProcess(), d, n);
}
static bool install_hook(){
    uint8_t* site = (uint8_t*)(g_base + TICK_CALL_RVA);
    MEMORY_BASIC_INFORMATION mbi;
    if (!VirtualQuery(site, &mbi, sizeof mbi) || mbi.State != MEM_COMMIT ||
        !(mbi.Protect & (PAGE_EXECUTE|PAGE_EXECUTE_READ|PAGE_EXECUTE_READWRITE|PAGE_EXECUTE_WRITECOPY)) ||
        (uint8_t*)mbi.BaseAddress + mbi.RegionSize < site + 5) {
        L("hook: site not committed executable -- not FDNYFirefighter.exe? ABORT"); return false;
    }
    if (site[0] != TICK_ORIG0) { Lx("hook: site[0] not E8, got", site[0]); return false; }
    int32_t rel = *(int32_t*)(site + 1);
    if ((uintptr_t)site + 5 + rel != g_base + TICKSUBSYS_RVA) { L("hook: CALL target != TickSubsystems -- ABORT"); return false; }

    g_tramp = (uint8_t*)VirtualAlloc(nullptr, 0x80, MEM_COMMIT|MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    if (!g_tramp) return false;
    uint8_t* t = g_tramp; unsigned k = 0;
    #define B(x) (t[k++]=(uint8_t)(x))
    #define D(x) do{ uint32_t _v=(uint32_t)(x); B(_v); B(_v>>8); B(_v>>16); B(_v>>24);}while(0)
    B(0xE8); D((g_base + TICKSUBSYS_RVA) - ((uintptr_t)t + k + 4));  // call real TickSubsystems (ECX intact from MOV ECX,ESI)
    B(0x9C); B(0x60);                                  // pushfd ; pushad
    B(0x83); B(0xEC); B(0x08);                         // sub esp, 8
    B(0xDD); B(0x05); D(g_base + GFRAMEDT_RVA);        // fld dword [g_frameDeltaTime]
    B(0xDD); B(0x1C); B(0x24);                         // fstp qword [esp]
    B(0xE8); D((uintptr_t)&frame_tick - ((uintptr_t)t + k + 4));    // call frame_tick(double)
    B(0x83); B(0xC4); B(0x08);                         // add esp, 8
    B(0x61); B(0x9D);                                  // popad ; popfd
    B(0xC3);                                           // ret -> 0x0042af0f

    uint8_t patch[5] = { 0xE8, 0,0,0,0 };
    *(int32_t*)(patch + 1) = (int32_t)((uintptr_t)g_tramp - ((uintptr_t)site + 5));
    wprot(site, patch, 5);
    L("hook installed at 0x0042af0a");
    return true;
}

// ------------------------------------------------------------------ lifecycle --
static DWORD WINAPI init_thread(LPVOID){
    g_base = (uintptr_t)GetModuleHandleA(nullptr);
    char dir[MAX_PATH]; GetModuleFileNameA(nullptr, dir, MAX_PATH);
    for (int i=lstrlenA(dir); i>=0; --i) if (dir[i]=='\\'||dir[i]=='/'){ dir[i]=0; break; }
    char lp[MAX_PATH]; wsprintfA(lp, "%s\\FDNYFreemanMovement.log", dir);
    g_log = CreateFileA(lp, GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS, 0, nullptr);
    L("FDNYFreemanMovement v0.3 (post-TickSubsystems hook)");

    char fp[MAX_PATH]; wsprintfA(fp, "%s\\FreemanAPI_gcp.dll", dir);
    g_fapi = LoadLibraryA(fp);
    if (!g_fapi) g_fapi = LoadLibraryA("FreemanAPI_gcp.dll");
    if (!g_fapi) { L("FreemanAPI_gcp.dll NOT FOUND -- put it next to this .asi"); return 0; }

    #define R(nm) GetProcAddress(g_fapi, nm)
    p_Process      = (fn_process)R("FreemanAPI_Process");
    p_BBoxMin      = (fn_getd)R("FreemanAPI_GetPlayerBBoxMin");
    p_BBoxMax      = (fn_getd)R("FreemanAPI_GetPlayerBBoxMax");
    p_SetMoveType  = (fn_i)  R("FreemanAPI_SetMoveType");
    p_ResetPhysics = (fn_void)R("FreemanAPI_ResetPhysics");
    if (!p_Process) { L("FreemanAPI_Process missing -- wrong dll?"); return 0; }

    ((fn_b)  R("FreemanAPI_SetIsZUp"))(false);
    ((fn_b)  R("FreemanAPI_SetConvertUnits"))(false);
    ((fn_i)  R("FreemanAPI_SetDefaultMoveType"))(3);
    ((fn_b)  R("FreemanAPI_SetIsEnabled"))(true);
    if (auto shl2 = (fn_b)R("FreemanAPI_SetIsHL2Mode")) shl2(false);  // HL1: classic quake-style accel/airaccel, no speed-crop
    if (auto sro  = (fn_iii)R("FreemanAPI_SetRotateOrder")) sro(1, 0, 2);

    ((fn_reg_getv) R("FreemanAPI_Register_GetGamePlayerPosition"))(cb_getpos);
    ((fn_reg_getv) R("FreemanAPI_Register_GetGamePlayerVelocity"))(cb_getvel);
    ((fn_reg_getv) R("FreemanAPI_Register_GetGamePlayerViewAngle"))(cb_getview);
    ((fn_reg_setpv)R("FreemanAPI_Register_SetGamePlayerPosition"))(cb_setpos);
    ((fn_reg_getb) R("FreemanAPI_Register_GetGamePlayerDead"))(cb_dead);
    ((fn_reg_snd)  R("FreemanAPI_Register_PlayGameSound"))(cb_sound);
    ((fn_reg_falld)R("FreemanAPI_Register_OnTakeFallDamage"))(cb_falldmg);
    ((fn_reg_getf) R("FreemanAPI_Register_GetGameMoveFwdBack"))(cb_fwd);
    ((fn_reg_getf) R("FreemanAPI_Register_GetGameMoveLeftRight"))(cb_side);
    ((fn_reg_getf) R("FreemanAPI_Register_GetGameMoveUpDown"))(cb_up);
    ((fn_reg_getb) R("FreemanAPI_Register_GetGameMoveJump"))(cb_jump);
    ((fn_reg_getb) R("FreemanAPI_Register_GetGameMoveDuck"))(cb_duck);
    ((fn_reg_getb) R("FreemanAPI_Register_GetGameMoveRun"))(cb_run);
    ((fn_reg_getb) R("FreemanAPI_Register_GetGameMoveUse"))(cb_use);
    ((fn_reg_trace)R("FreemanAPI_Register_PM_PlayerTrace"))((pmtrace_t*(*)(const double*,const double*))cb_trace_hull);
    ((fn_reg_trace)R("FreemanAPI_Register_PM_PlayerTraceDown"))((pmtrace_t*(*)(const double*,const double*))cb_trace_hull);
    ((fn_reg_trace)R("FreemanAPI_Register_PointRaytrace"))((pmtrace_t*(*)(const double*,const double*))cb_trace_ray);
    ((fn_reg_pc)   R("FreemanAPI_Register_GetPointContents"))(cb_pointcontents);

    // Load FreemanAPI_gcp.toml (our .asi is the only consumer -- nothing else
    // calls this), then FORCE the HL1-scale cvars from code so the scale holds
    // even if the toml isn't in the game's working directory.  Values = HL1
    // defaults x 1.25 (FDNY world scale); gravity 1000 / maxspeed 400 match
    // FDNY's own measured constants.
    if (auto lc = (fn_void)R("FreemanAPI_LoadConfig")) lc();
    {
        typedef float* (__cdecl *fn_cf)(const char*);
        fn_cf cf = (fn_cf)R("FreemanAPI_GetConfigFloatHL1");
        if (cf) {
            struct { const char* k; float v; } kv[] = {
                {"sv_gravity",1000.f}, {"sv_maxspeed",400.f}, {"sv_stopspeed",125.f},
                {"cl_forwardspeed",500.f}, {"cl_sidespeed",500.f}, {"cl_upspeed",400.f},
                {"sv_stepsize",22.f}, {"sv_maxvelocity",2500.f}, {"sv_noclipspeed",400.f},
            };
            for (auto& e : kv) { float* p = cf(e.k); if (p) *p = e.v; }
            float* g = cf("sv_gravity"); float* m = cf("sv_maxspeed");
            char b[128]; wsprintfA(b, "cvars forced: gravity=%d maxspeed=%d",
                g?(int)*g:-1, m?(int)*m:-1); L(b);
        } else L("cvars: GetConfigFloatHL1 missing -- relying on toml only");
    }

    // throwaway Process: consumes FreemanAPI's one-time internal Reset() and runs
    // the first SetPlayerBBoxes(), so the hull (hence centre-up) is valid before
    // frame_tick's real seed calls ResetPhysics().  g_pos is {0,0,0}, no level
    // loaded -> the physics no-ops.
    g_seeded = false;
    p_Process(1.0/60.0);
    g_seeded = false;

    if (!install_hook()) { L("hook failed -- not FDNYFirefighter.exe?"); return 0; }
    L("ready -- INSERT toggles HL movement, '/' dumps state");
    return 0;
}

BOOL APIENTRY DllMain(HMODULE h, DWORD reason, LPVOID){
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(h);
        CreateThread(nullptr, 0, init_thread, nullptr, 0, nullptr);
    }
    return TRUE;
}
