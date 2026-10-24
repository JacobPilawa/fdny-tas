// ===========================================================================
//  FDNYHLMove.asi  --  Half-Life (GoldSrc) player movement in F.D.N.Y. Firefighter
// ===========================================================================
//
//  Self-contained port of GoldSrc pm_shared.c's movement core -- no FreemanAPI,
//  no external DLL, no config file.  Everything runs in FDNY's native frame
//  (Y-up, entity origin at entity+0x4c, velocity at entity+0xfc), so there is
//  no coordinate / unit / angle marshalling anywhere.
//
//  Hook: 5-byte patch at 0x0042af0a (the CALL to GameSession::TickSubsystems)
//  -> trampoline re-issues that call, then invokes pm_frame(), then returns to
//  the safe point 0x0042af0f.  FDNY's Think has already run for this frame;
//  when HL movement owns the player (state 3 stand / 4 crouch, not spectator,
//  not input-frozen, toggle on) pm_frame overwrites the entity's position,
//  velocity, frame-delta and onground byte.  The camera + animation code that
//  runs afterwards re-derives from those, so nothing else needs touching.
//
//  Scale: FDNY units = HL units x 1.25 (HL sv_gravity 800 / sv_maxspeed 320,
//  scaled by 1.25, are exactly FDNY's own measured 1000 / 400).  Every HL
//  constant below is the canonical GoldSrc value x 1.25 where it is a
//  distance/speed, unchanged where it is a rate or ratio.
//
//  INSERT = toggle on/off (byte-clean A/B vs vanilla).   '/' = dump state.
// ===========================================================================

#include <windows.h>
#include <stdint.h>

// -------------------------------------------------------- freestanding shims --
extern "C" {
    int _fltused = 0;
    void* memcpy(void* d, const void* s, size_t n){ auto a=(uint8_t*)d; auto b=(const uint8_t*)s; while(n--)*a++=*b++; return d; }
    void* memset(void* d, int c, size_t n){ auto a=(uint8_t*)d; while(n--)*a++=(uint8_t)c; return d; }
    size_t strlen(const char* s){ size_t n=0; while(s[n]) n++; return n; }
    BOOL WINAPI DllMain(HINSTANCE, DWORD, LPVOID);
    BOOL WINAPI _DllMainCRTStartup(HINSTANCE h, DWORD r, LPVOID x){ return DllMain(h,r,x); }
}
static float  s_sqrt(float x){ float r; __asm__("fsqrt":"=t"(r):"0"((double)x)); return r; }
static double s_sin (double x){ double r; __asm__("fsin" :"=t"(r):"0"(x)); return r; }
static double s_cos (double x){ double r; __asm__("fcos" :"=t"(r):"0"(x)); return r; }

// ------------------------------------------------------------------ addresses --
static const uintptr_t GFRAMEDT_RVA   = 0x00102b04;   // float g_frameDeltaTime  (0x00502b04)
static const uintptr_t GGAMECTX_RVA   = 0x00103a78;   // g_everGame = *(void**)   (0x00503a78)
static const uintptr_t IAT_GEWORLDCOL = 0x000dc0bc;   // IAT -> GENESIS.DLL geWorld_Collision
static const uintptr_t TICK_CALL_RVA  = 0x0002af0a;   // CALL GameSession::TickSubsystems (E8 rel32)
static const uintptr_t TICKSUBSYS_RVA = 0x00029520;
static const int  EG_ROC    = 0x04;   // g_everGame -> ROCRender
static const int  EG_PLAYER = 0x30;   // g_everGame -> player entity
static const int  ROC_FLAG   = 0x160, ROC_WRES_L = 0xb4, ROC_WRES_S = 0x08, WRES_WORLD = 0x78;
static const int  OFF_KIND    = 0x18;    // == 1 for the player
static const int  OFF_POS     = 0x4c;    // float xyz
static const int  OFF_DELTA   = 0x90;    // float xyz this-frame move delta (animation reads it)
static const int  OFF_ONGRND  = 0x12c;   // byte
static const int  OFF_ONGRND_P= 0x12d;   // byte prev
static const int  OFF_VEL     = 0xfc;    // float xyz
static const int  OFF_STATE   = 0x18c;   // 3 stand / 4 crouch / 5 climb-spec / 6 passed-out / 7 scripted
static const int  OFF_SPECT   = 0x380;   // byte
static const int  OFF_FROZEN  = 0x345c;  // int, nonzero = movement input frozen
static const int  OFF_YAW     = 0x3434;  // float degrees
static const int  OFF_HULLMAXY= 0x88;    // float: hull maxs.Y  (85 stand / 25 crouch -- FDNY manages it)

// -------------------------------------------------- HL movement, FDNY-scaled --
// Derivation: GoldSrc is 1 unit = 1 inch (HL player hull = 72u = 6ft).  FDNY's
// player hull is 115u tall (origin -30..+85) for the same ~6ft firefighter, so
// FDNY's world is S = 115/72 ~= 1.6x bigger.  Distances and speeds scale by S;
// rates (per-second) and ratios do not.
//
// All of these are LIVE-TUNABLE via FDNYHLMove.ini (next to the .asi), section
// [movement].  Any key omitted from the .ini falls back to the derived default
// below.  Press the reload key (\ by default) in-game to re-read the .ini.
static const float PM_SCALE_DEF = 1.60f;                // .ini: scale  (drives every derived default)
static float PM_GRAVITY   = 800.0f  * PM_SCALE_DEF;     // .ini: gravity
static float PM_MAXSPEED  = 320.0f  * PM_SCALE_DEF;     // .ini: maxspeed
static float PM_MOVESPEED = 400.0f  * PM_SCALE_DEF;     // .ini: movespeed
static float PM_STOPSPEED = 100.0f  * PM_SCALE_DEF;     // .ini: stopspeed
static float PM_AIRCAP    = 30.0f   * PM_SCALE_DEF;     // .ini: aircap      (air-strafe wishspeed cap)
static float PM_STEPSIZE  = 18.0f   * PM_SCALE_DEF;     // .ini: stepsize
static float PM_MAXVEL    = 2000.0f * PM_SCALE_DEF;     // .ini: maxvelocity
static float PM_JUMP_APEX = 76.0f;                      // .ini: jump_apex   (FDNY units, NOT scaled)
static float PM_FRICTION  = 4.0f;                       // .ini: friction        (rate -- default unscaled)
static float PM_ACCEL     = 10.0f;                      // .ini: accelerate      (rate)
static float PM_AIRACCEL  = 10.0f;                      // .ini: airaccelerate   (rate)
static float PM_TICKRATE  = 100.0f;                     // .ini: tickrate    (target substep Hz; adapts to fps)
static int   PM_AUTOHOP   = 1;                          // .ini: autohop     (1 = hold jump to bhop; 0 = tap)
static const float STOP_EPSILON = 0.1f;
static const float MOVE_CONTENTS = 0x43;                // SOLID|WINDOW|CLIP  (verified)
static const int   UP = 1;                              // Y is up

// FDNY hull: mins const (-30,-30,-30); maxs.X/.Z = 30, maxs.Y read live (85/25)
static const float HULL_MIN[3] = { -30.f, -30.f, -30.f };

// ------------------------------------------------------------------ globals --
static uintptr_t g_base    = 0x00400000;
static HANDLE    g_log     = INVALID_HANDLE_VALUE;
static bool      g_enabled = true;
static uint8_t*  g_tramp   = nullptr;
static void*     g_world   = nullptr;    // cached geWorld* (re-resolved when null)
static bool      g_jumpHeld = false;
static bool      g_jumpPrev = false;    // jump input last frame (for non-autohop)
static float     g_jumpVel  = 0.f;      // = sqrt(2 * PM_GRAVITY * PM_JUMP_APEX), (re)set by load_config
static char      g_iniPath[MAX_PATH] = {0};

// -------------------------------------------------------------------- .ini I/O --
static float p_atof(const char* s){
    while (*s==' '||*s=='\t') s++;
    float sign = 1.f;
    if (*s=='-'){ sign=-1.f; s++; } else if (*s=='+') s++;
    float v = 0.f;
    while (*s>='0' && *s<='9'){ v = v*10.f + (float)(*s - '0'); s++; }
    if (*s=='.'){ s++; float f=0.1f;
        while (*s>='0' && *s<='9'){ v += (float)(*s-'0')*f; f*=0.1f; s++; }
    }
    return v*sign;
}
static float ini_f(const char* key, float def){
    char buf[64] = {0};
    GetPrivateProfileStringA("movement", key, "", buf, sizeof buf, g_iniPath);
    return buf[0] ? p_atof(buf) : def;
}
static void load_config(){
    float sc     = ini_f("scale",        PM_SCALE_DEF);
    PM_GRAVITY   = ini_f("gravity",      800.0f  * sc);
    PM_MAXSPEED  = ini_f("maxspeed",     320.0f  * sc);
    PM_MOVESPEED = ini_f("movespeed",    400.0f  * sc);
    PM_STOPSPEED = ini_f("stopspeed",    100.0f  * sc);
    PM_AIRCAP    = ini_f("aircap",        30.0f  * sc);
    PM_STEPSIZE  = ini_f("stepsize",      18.0f  * sc);
    PM_MAXVEL    = ini_f("maxvelocity", 2000.0f  * sc);
    PM_JUMP_APEX = ini_f("jump_apex",     76.0f);
    PM_FRICTION  = ini_f("friction",       4.0f);
    PM_ACCEL     = ini_f("accelerate",    10.0f);
    PM_AIRACCEL  = ini_f("airaccelerate", 10.0f);
    PM_TICKRATE  = ini_f("tickrate",     100.0f);
    PM_AUTOHOP   = (int)ini_f("autohop",   1.0f);
    g_jumpVel = s_sqrt(2.0f * PM_GRAVITY * PM_JUMP_APEX);
}

// ------------------------------------------------------------------- logging --
static void L(const char* s){
    if (g_log == INVALID_HANDLE_VALUE) return;
    SYSTEMTIME st; GetLocalTime(&st);
    char h[40]; int n = wsprintfA(h, "[%02d:%02d:%02d] ", st.wHour, st.wMinute, st.wSecond);
    DWORD w; WriteFile(g_log, h, n, &w, nullptr);
    int m=0; while (s[m]) m++;
    WriteFile(g_log, s, m, &w, nullptr);
    WriteFile(g_log, "\r\n", 2, &w, nullptr);
}
static void Lx(const char* s, int v){ char b[96]; wsprintfA(b, "%s %d", s, v); L(b); }
#define ONCE(s) do{ static bool _o=false; if(!_o){_o=true; L(s);} }while(0)
static bool rok(const void* p, unsigned n){ return p && !IsBadReadPtr(p, n); }

// -------------------------------------------------------------- vec3 helpers --
static void  v_cpy(float* o, const float* a){ o[0]=a[0]; o[1]=a[1]; o[2]=a[2]; }
static void  v_clr(float* o){ o[0]=o[1]=o[2]=0; }
static float v_dot(const float* a, const float* b){ return a[0]*b[0]+a[1]*b[1]+a[2]*b[2]; }
static float v_len(const float* a){ return s_sqrt(a[0]*a[0]+a[1]*a[1]+a[2]*a[2]); }
static void  v_ma(const float* s, float sc, const float* d, float* o){ o[0]=s[0]+sc*d[0]; o[1]=s[1]+sc*d[1]; o[2]=s[2]+sc*d[2]; }
static void  v_scale(const float* a, float s, float* o){ o[0]=a[0]*s; o[1]=a[1]*s; o[2]=a[2]*s; }
static float v_norm(float* a){ float l=v_len(a); if(l>1e-6f){ float i=1.f/l; a[0]*=i; a[1]*=i; a[2]*=i; } return l; }
static void  v_cross(const float* a, const float* b, float* o){
    o[0]=a[1]*b[2]-a[2]*b[1]; o[1]=a[2]*b[0]-a[0]*b[2]; o[2]=a[0]*b[1]-a[1]*b[0];
}

// ------------------------------------------------------------ collision bridge --
typedef int (__cdecl *fn_geColl)(void* World, const float* Mins, const float* Maxs,
                                 const float* Front, const float* Back, uint32_t Contents,
                                 uint32_t Flags, void* StopModel, void* CB, void* Ctx, void* Out);

static void resolve_world(){
    char* eg  = rok((void*)(g_base+GGAMECTX_RVA),4) ? *(char**)(g_base+GGAMECTX_RVA) : nullptr;
    char* roc = (eg && rok(eg+EG_ROC,4)) ? *(char**)(eg+EG_ROC) : nullptr;
    if (!roc) { g_world = nullptr; return; }
    int off = (rok(roc+ROC_FLAG,1) && *(uint8_t*)(roc+ROC_FLAG)) ? ROC_WRES_L : ROC_WRES_S;
    g_world = rok(roc+off+WRES_WORLD,4) ? *(void**)(roc+off+WRES_WORLD) : nullptr;
}

struct trace_t { float frac; float endpos[3]; float normal[3]; bool startsolid; bool allsolid; };

static int g_trLog = 8;
static void pm_trace(const float* start, const float* end, const float* maxs, trace_t* tr){
    tr->frac = 1.f; v_cpy(tr->endpos, end); v_clr(tr->normal); tr->startsolid = tr->allsolid = false;

    fn_geColl gec = rok((void*)(g_base+IAT_GEWORLDCOL),4) ? *(fn_geColl*)(g_base+IAT_GEWORLDCOL) : nullptr;
    if (!gec || !g_world) { ONCE("trace: no world/gec"); return; }

    uint8_t out[0x30]; memset(out, 0, sizeof out);
    int hit = gec(g_world, HULL_MIN, maxs, start, end, (uint32_t)MOVE_CONTENTS, 2, nullptr,nullptr,nullptr, out);
    if (g_trLog > 0){ g_trLog--;
        float* im=(float*)(out+0x0c); float* nm=(float*)(out+0x1c);
        char b[192]; wsprintfA(b,"tr s(%d,%d,%d) e(%d,%d,%d) HIT=%d imp(%d,%d,%d) n(%d,%d,%d)*100",
            (int)start[0],(int)start[1],(int)start[2],(int)end[0],(int)end[1],(int)end[2],hit,
            hit?(int)im[0]:0,hit?(int)im[1]:0,hit?(int)im[2]:0,
            hit?(int)(nm[0]*100):0,hit?(int)(nm[1]*100):0,hit?(int)(nm[2]*100):0);
        L(b);
    }
    if (!hit) return;
    float* impact = (float*)(out+0x0c);
    float* normal = (float*)(out+0x1c);
    tr->startsolid = tr->allsolid = (*(int*)(out+0x18) != 0);
    float d[3]; d[0]=impact[0]-start[0]; d[1]=impact[1]-start[1]; d[2]=impact[2]-start[2];
    float de[3]; de[0]=end[0]-start[0]; de[1]=end[1]-start[1]; de[2]=end[2]-start[2];
    float num = v_len(d), den = v_len(de);
    tr->frac = (den > 1e-6f) ? (num/den) : 0.f;
    if (tr->frac > 1.f) tr->frac = 1.f;
    v_cpy(tr->endpos, impact);
    v_cpy(tr->normal, normal);
}

// ------------------------------------------------------------ GoldSrc pm core --
// per-move working state
static float pm_org[3], pm_vel[3];
static bool  pm_onground;
static float pm_maxs[3];
static float pm_frametime;

static int pm_clip(const float* in, const float* normal, float* out, float overbounce){
    int blocked = 0;
    if (normal[UP] > 0)  blocked |= 1;
    if (normal[UP] == 0) blocked |= 2;
    float backoff = v_dot(in, normal) * overbounce;
    for (int i=0;i<3;i++){
        float ch = normal[i]*backoff;
        out[i] = in[i] - ch;
        if (out[i] > -STOP_EPSILON && out[i] < STOP_EPSILON) out[i] = 0;
    }
    return blocked;
}

// collide-and-slide; moves pm_org / pm_vel over pm_frametime.  Returns blocked bits.
static int pm_flymove(){
    const int NUMBUMPS = 4, MAXPLANES = 5;
    float planes[MAXPLANES][3];
    float primal[3], original[3], newv[3], end[3], dir[3];
    int numplanes = 0, blocked = 0;
    float time_left = pm_frametime, allfrac = 0;
    v_cpy(primal, pm_vel); v_cpy(original, pm_vel);

    for (int bump=0; bump<NUMBUMPS; bump++){
        if (v_len(pm_vel) == 0) break;
        v_ma(pm_org, time_left, pm_vel, end);
        trace_t tr; pm_trace(pm_org, end, pm_maxs, &tr);
        allfrac += tr.frac;
        if (tr.allsolid){ v_clr(pm_vel); return 4; }
        if (tr.frac > 0){ v_cpy(pm_org, tr.endpos); v_cpy(original, pm_vel); numplanes = 0; }
        if (tr.frac == 1) break;
        if (tr.normal[UP] > 0.7f) blocked |= 1;
        if (tr.normal[UP] == 0)   blocked |= 2;
        time_left -= time_left * tr.frac;
        if (numplanes >= MAXPLANES){ v_clr(pm_vel); break; }
        v_cpy(planes[numplanes], tr.normal); numplanes++;

        if (!pm_onground){
            // airborne: clip against every plane, floors non-bouncy
            for (int i=0;i<numplanes;i++){
                if (planes[i][UP] > 0.7f){ pm_clip(original, planes[i], newv, 1.0f); v_cpy(original, newv); }
                else                     { pm_clip(original, planes[i], newv, 1.0f); v_cpy(original, newv); }
            }
            v_cpy(pm_vel, newv); v_cpy(original, newv);
        } else {
            int i;
            for (i=0;i<numplanes;i++){
                pm_clip(original, planes[i], pm_vel, 1.0f);
                int j; for (j=0;j<numplanes;j++){ if (j!=i && v_dot(pm_vel, planes[j]) < 0) break; }
                if (j == numplanes) break;
            }
            if (i == numplanes){
                if (numplanes != 2){ v_clr(pm_vel); break; }
                v_cross(planes[0], planes[1], dir);
                float d = v_dot(dir, pm_vel);
                v_scale(dir, d, pm_vel);
            }
            if (v_dot(pm_vel, primal) <= 0){ v_clr(pm_vel); break; }
        }
    }
    if (allfrac == 0) v_clr(pm_vel);
    return blocked;
}

static void pm_categorize(){
    if (pm_vel[UP] > 180.0f){ pm_onground = false; return; }
    float pt[3]; v_cpy(pt, pm_org); pt[UP] = pm_org[UP] - 2.0f;
    trace_t tr; pm_trace(pm_org, pt, pm_maxs, &tr);
    if (tr.frac == 1.f || tr.normal[UP] < 0.7f){ pm_onground = false; return; }
    pm_onground = true;
    if (!tr.startsolid && !tr.allsolid) v_cpy(pm_org, tr.endpos);
}

static void pm_friction(){
    float speed = v_len(pm_vel);
    if (speed < 0.1f) return;
    float drop = 0;
    if (pm_onground){
        float control = (speed < PM_STOPSPEED) ? PM_STOPSPEED : speed;
        drop += control * PM_FRICTION * pm_frametime;
    }
    float newspeed = speed - drop;
    if (newspeed < 0) newspeed = 0;
    v_scale(pm_vel, newspeed / speed, pm_vel);
}

static void pm_accelerate(const float* wishdir, float wishspeed, float accel){
    float cur = v_dot(pm_vel, wishdir);
    float add = wishspeed - cur;
    if (add <= 0) return;
    float acc = accel * pm_frametime * wishspeed;
    if (acc > add) acc = add;
    v_ma(pm_vel, acc, wishdir, pm_vel);
}
static void pm_airaccelerate(const float* wishdir, float wishspeed, float accel){
    float wishspd = wishspeed;
    if (wishspd > PM_AIRCAP) wishspd = PM_AIRCAP;
    float cur = v_dot(pm_vel, wishdir);
    float add = wishspd - cur;
    if (add <= 0) return;
    float acc = accel * wishspeed * pm_frametime;
    if (acc > add) acc = add;
    v_ma(pm_vel, acc, wishdir, pm_vel);
}

// build flattened forward/right from FDNY yaw, and wishdir/wishspeed from WASD.
// derived (cluster 3): FDNY forward = (sinY,0,cosY), right = (cosY,0,-sinY).
// strafe sign is empirical (A = +), matching what shipped in the FreemanAPI build.
static void pm_wishdir(char* e, float* wishdir, float* wishspeed){
    double y = (double)(*(float*)(e + OFF_YAW)) * 0.017453292519943295;
    float sy = (float)s_sin(y), cy = (float)s_cos(y);
    float fwd[3]   = { sy, 0.f, cy };
    float right[3] = { cy, 0.f, -sy };
    auto kd = [](int vk){ return (GetAsyncKeyState(vk) & 0x8000) != 0; };
    float fmove = ((kd('W')?1.f:0.f) - (kd('S')?1.f:0.f)) * PM_MOVESPEED;
    float smove = ((kd('A')?1.f:0.f) - (kd('D')?1.f:0.f)) * PM_MOVESPEED;
    float wv[3];
    wv[0] = fwd[0]*fmove + right[0]*smove;
    wv[1] = 0.f;
    wv[2] = fwd[2]*fmove + right[2]*smove;
    v_cpy(wishdir, wv);
    float ws = v_norm(wishdir);
    if (ws > PM_MAXSPEED){ v_scale(wv, PM_MAXSPEED/ws, wv); ws = PM_MAXSPEED; }
    *wishspeed = ws;
}

static void pm_walkmove(char* e){
    float wishdir[3], wishspeed;
    pm_wishdir(e, wishdir, &wishspeed);

    pm_vel[UP] = 0;
    pm_accelerate(wishdir, wishspeed, PM_ACCEL);
    pm_vel[UP] = 0;

    if (v_len(pm_vel) < 1.f){ v_clr(pm_vel); return; }

    // 1. try a straight flat move
    float dest[3]; v_ma(pm_org, pm_frametime, pm_vel, dest);
    trace_t tr; pm_trace(pm_org, dest, pm_maxs, &tr);
    if (tr.frac == 1.f){ v_cpy(pm_org, tr.endpos); return; }

    // 2. blocked -- keep a slide-only result, then try a stepped result, take the better
    float orgIn[3], velIn[3]; v_cpy(orgIn, pm_org); v_cpy(velIn, pm_vel);
    pm_flymove();
    float downOrg[3], downVel[3]; v_cpy(downOrg, pm_org); v_cpy(downVel, pm_vel);

    v_cpy(pm_org, orgIn); v_cpy(pm_vel, velIn);
    float up[3]; v_cpy(up, pm_org); up[UP] += PM_STEPSIZE;
    pm_trace(pm_org, up, pm_maxs, &tr);
    if (!tr.startsolid && !tr.allsolid) v_cpy(pm_org, tr.endpos);
    pm_flymove();
    float dn[3]; v_cpy(dn, pm_org); dn[UP] -= PM_STEPSIZE;
    pm_trace(pm_org, dn, pm_maxs, &tr);
    if (tr.normal[UP] < 0.7f){ v_cpy(pm_org, downOrg); v_cpy(pm_vel, downVel); return; }
    if (!tr.startsolid && !tr.allsolid) v_cpy(pm_org, tr.endpos);

    float dd = (downOrg[0]-orgIn[0])*(downOrg[0]-orgIn[0]) + (downOrg[2]-orgIn[2])*(downOrg[2]-orgIn[2]);
    float ud = (pm_org[0] -orgIn[0])*(pm_org[0] -orgIn[0]) + (pm_org[2] -orgIn[2])*(pm_org[2] -orgIn[2]);
    if (dd > ud){ v_cpy(pm_org, downOrg); v_cpy(pm_vel, downVel); }
    else        { pm_vel[UP] = downVel[UP]; }
}

static void pm_airmove(char* e){
    float wishdir[3], wishspeed;
    pm_wishdir(e, wishdir, &wishspeed);
    pm_airaccelerate(wishdir, wishspeed, PM_AIRACCEL);
    pm_flymove();
}

// one PM_PlayerMove pass (MOVETYPE_WALK), in FreemanAPI's exact order.  `ft` is
// the substep frametime (whole-frame dt / PM_STEPS).
static bool pm_didJump;
static void pm_step(char* e, float ft, bool jumpHeld){
    pm_frametime = ft;
    pm_categorize();
    pm_vel[UP] -= 0.5f * PM_GRAVITY * ft;                    // PM_AddCorrectGravity
    if (jumpHeld && pm_onground){                            // PM_Jump (overwrites vel[UP])
        pm_vel[UP] = g_jumpVel;
        pm_onground = false;
        pm_didJump = true;
    }
    if (pm_onground){ pm_vel[UP] = 0; pm_friction(); }       // friction before the move
    if (pm_onground) pm_walkmove(e); else pm_airmove(e);
    pm_categorize();
    pm_vel[UP] -= 0.5f * PM_GRAVITY * ft;                    // PM_FixupGravityVelocity
    if (pm_onground) pm_vel[UP] = 0;
    for (int i=0;i<3;i++){                                   // PM_CheckVelocity
        if (pm_vel[i] >  PM_MAXVEL) pm_vel[i] =  PM_MAXVEL;
        if (pm_vel[i] < -PM_MAXVEL) pm_vel[i] = -PM_MAXVEL;
    }
}

// ------------------------------------------------------------------ per frame --
static char* g_lastPlayer = nullptr;
static float g_lastWritten[3] = {0,0,0};

extern "C" void __cdecl pm_frame(){
    ONCE("pm_frame: first entry");
    char* eg = rok((void*)(g_base+GGAMECTX_RVA),4) ? *(char**)(g_base+GGAMECTX_RVA) : nullptr;
    char* e  = (eg && rok(eg+EG_PLAYER,4)) ? *(char**)(eg+EG_PLAYER) : nullptr;
    if (!e || !rok(e, 0x3600) || *(int*)(e + OFF_KIND) != 1) return;

    // toggle / dump keys (edge + cooldown -- immune to remote-session key repeat)
    {
        static bool tp=false, dp=false; static DWORD tl=0, dl=0;
        bool td = (GetAsyncKeyState(VK_INSERT) & 0x8000) != 0;
        if (td && !tp && GetTickCount()-tl > 400){ tl=GetTickCount(); g_enabled=!g_enabled; g_lastPlayer=nullptr; Lx("HL move =", g_enabled); }
        tp = td;
        bool dd = (GetAsyncKeyState(VK_OEM_2) & 0x8000) != 0;
        if (dd && !dp && GetTickCount()-dl > 250){ dl=GetTickCount();
            char b[192]; wsprintfA(b,"dump on=%d st=%d pos(%d,%d,%d) vel(%d,%d,%d) og=%d",
                g_enabled?1:0, *(int*)(e+OFF_STATE),
                (int)*(float*)(e+OFF_POS+0),(int)*(float*)(e+OFF_POS+4),(int)*(float*)(e+OFF_POS+8),
                (int)*(float*)(e+OFF_VEL+0),(int)*(float*)(e+OFF_VEL+4),(int)*(float*)(e+OFF_VEL+8),
                *(uint8_t*)(e+OFF_ONGRND)); L(b);
        }
        dp = dd;
        static bool rp=false; static DWORD rl=0;
        bool rd = (GetAsyncKeyState(VK_OEM_5) & 0x8000) != 0;   // '\' -- reload FDNYHLMove.ini
        if (rd && !rp && GetTickCount()-rl > 400){ rl=GetTickCount();
            load_config();
            char b[160]; wsprintfA(b,"ini reloaded: gravity=%d maxspeed=%d aircap=%d jumpvel=%d apex=%d autohop=%d",
                (int)PM_GRAVITY,(int)PM_MAXSPEED,(int)PM_AIRCAP,(int)g_jumpVel,(int)PM_JUMP_APEX,PM_AUTOHOP); L(b);
        }
        rp = rd;
    }
    if (!g_enabled) return;

    int  state  = *(int*)(e + OFF_STATE);
    bool spect  = rok(e+OFF_SPECT,1) && *(uint8_t*)(e+OFF_SPECT) != 0;
    bool frozen = *(int*)(e + OFF_FROZEN) != 0;
    if ((state != 3 && state != 4) || spect || frozen) return;

    float dt = *(float*)(g_base + GFRAMEDT_RVA);
    if (!(dt > 0.f && dt < 0.5f)) dt = 1.f/60.f;
    pm_frametime = dt;
    resolve_world();                 // cheap; re-resolve every frame so level changes are picked up
    { static int fc=0; if ((fc++ % 120) == 0){
        char b[96]; wsprintfA(b,"dt*1e5=%d (~%dfps)", (int)(dt*1e5f), dt>0?(int)(1.f/dt):-1); L(b); } }

    float* ep = (float*)(e + OFF_POS);
    float* ev = (float*)(e + OFF_VEL);
    float* ed = (float*)(e + OFF_DELTA);
    if (!rok(ep,12) || !rok(ev,12) || !rok(ed,12)) return;

    // If something other than us moved the player (level load / teleport /
    // respawn) since our last write, take the entity's state as-is.  Otherwise
    // FDNY's Think also moved it this frame -- ignore that, we are authoritative.
    v_cpy(pm_org, ep);
    if (g_lastPlayer == e){
        float dx=ep[0]-g_lastWritten[0], dy=ep[1]-g_lastWritten[1], dz=ep[2]-g_lastWritten[2];
        if (dx*dx+dy*dy+dz*dz < 128.f*128.f) v_cpy(pm_org, g_lastWritten);
    }
    g_lastPlayer = e;
    v_cpy(pm_vel, ev);

    pm_maxs[0] = 30.f;
    pm_maxs[UP] = rok(e+OFF_HULLMAXY,4) ? *(float*)(e+OFF_HULLMAXY) : 85.f;
    pm_maxs[2] = 30.f;

    float orgBefore[3]; v_cpy(orgBefore, pm_org);

    // autohop: fires every substep it is grounded while a jump input is held.
    // Read Space AND the mouse buttons (FDNY binds jump to a mouse button).
    bool jd = (GetAsyncKeyState(VK_SPACE)  & 0x8000)
           || (GetAsyncKeyState(VK_LBUTTON)& 0x8000)
           || (GetAsyncKeyState(VK_RBUTTON)& 0x8000);
    g_jumpHeld = jd;
    // autohop on: jump every grounded substep while held.  off: only on a fresh press.
    bool jTrig = jd && (PM_AUTOHOP || !g_jumpPrev);
    g_jumpPrev = jd;
    pm_didJump = false;

    // --- PM_PlayerMove, adaptive substeps (target ~PM_TICKRATE Hz) ---
    int steps = (int)(dt * PM_TICKRATE + 0.999f);
    if (steps < 1) steps = 1; if (steps > 16) steps = 16;
    float ft = dt / (float)steps;
    for (int i = 0; i < steps; i++) pm_step(e, ft, jTrig);

    // per-frame diag (capped): state / ground / jump / wishspeed / velocity / horiz speed
    {
        static int dlog = 200; static int lastHs = -999;
        float wd[3], wsp; pm_wishdir(e, wd, &wsp);
        int hs = (int)s_sqrt(pm_vel[0]*pm_vel[0] + pm_vel[2]*pm_vel[2]);
        if (dlog > 0 && (pm_didJump || hs != lastHs)) { dlog--; lastHs = hs;
            char b[192];
            wsprintfA(b,"f st=%d og=%d jmp=%d wsp=%d vel(%d,%d,%d) hspd=%d",
                state, pm_onground?1:0, pm_didJump?1:0, (int)wsp,
                (int)pm_vel[0],(int)pm_vel[1],(int)pm_vel[2], hs);
            L(b);
        }
    }

    // --- write back where the camera / animation / next frame read ---
    ed[0] = pm_org[0]-orgBefore[0]; ed[1] = pm_org[1]-orgBefore[1]; ed[2] = pm_org[2]-orgBefore[2];
    ep[0] = pm_org[0]; ep[1] = pm_org[1]; ep[2] = pm_org[2];
    ev[0] = pm_vel[0]; ev[1] = pm_vel[1]; ev[2] = pm_vel[2];
    if (rok(e+OFF_ONGRND,2)){
        *(uint8_t*)(e+OFF_ONGRND_P) = *(uint8_t*)(e+OFF_ONGRND);
        *(uint8_t*)(e+OFF_ONGRND)   = pm_onground ? 1 : 0;
    }
    v_cpy(g_lastWritten, pm_org);
    ONCE("pm_frame: first writeback ok");
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
    if (!VirtualQuery(site,&mbi,sizeof mbi) || mbi.State!=MEM_COMMIT ||
        !(mbi.Protect & (PAGE_EXECUTE|PAGE_EXECUTE_READ|PAGE_EXECUTE_READWRITE|PAGE_EXECUTE_WRITECOPY)) ||
        (uint8_t*)mbi.BaseAddress + mbi.RegionSize < site + 5){
        L("hook: site not committed executable -- ABORT"); return false;
    }
    if (site[0] != 0xE8){ Lx("hook: site[0] not E8, got", site[0]); return false; }
    int32_t rel = *(int32_t*)(site+1);
    if ((uintptr_t)site + 5 + rel != g_base + TICKSUBSYS_RVA){ L("hook: CALL target != TickSubsystems -- ABORT"); return false; }

    g_tramp = (uint8_t*)VirtualAlloc(nullptr, 0x60, MEM_COMMIT|MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    if (!g_tramp) return false;
    uint8_t* t = g_tramp; unsigned k = 0;
    #define B(x) (t[k++]=(uint8_t)(x))
    #define D(x) do{ uint32_t _v=(uint32_t)(x); B(_v); B(_v>>8); B(_v>>16); B(_v>>24);}while(0)
    B(0xE8); D((g_base + TICKSUBSYS_RVA) - ((uintptr_t)t + k + 4));   // call real TickSubsystems (ECX = ESI, intact)
    B(0x9C); B(0x60);                                                // pushfd ; pushad
    B(0xE8); D((uintptr_t)&pm_frame - ((uintptr_t)t + k + 4));       // call pm_frame()
    B(0x61); B(0x9D);                                                // popad ; popfd
    B(0xC3);                                                         // ret -> 0x0042af0f

    uint8_t patch[5] = { 0xE8, 0,0,0,0 };
    *(int32_t*)(patch+1) = (int32_t)((uintptr_t)g_tramp - ((uintptr_t)site + 5));
    wprot(site, patch, 5);
    L("hook installed at 0x0042af0a");
    return true;
}

// ------------------------------------------------------------------ lifecycle --
static DWORD WINAPI init_thread(LPVOID){
    g_base = (uintptr_t)GetModuleHandleA(nullptr);
    char dir[MAX_PATH]; GetModuleFileNameA(nullptr, dir, MAX_PATH);
    for (int i=lstrlenA(dir); i>=0; --i) if (dir[i]=='\\'||dir[i]=='/'){ dir[i]=0; break; }
    char lp[MAX_PATH]; wsprintfA(lp, "%s\\FDNYHLMove.log", dir);
    g_log = CreateFileA(lp, GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS, 0, nullptr);
    L("FDNYHLMove v0.5 -- self-contained GoldSrc pm_shared port + .ini");
    wsprintfA(g_iniPath, "%s\\FDNYHLMove.ini", dir);
    load_config();
    { char b[192]; wsprintfA(b,"cfg: gravity=%d maxspeed=%d movespeed=%d stopspeed=%d aircap=%d stepsize=%d jumpvel=%d(apex=%d) fric=%d acc=%d airacc=%d tick=%d autohop=%d",
        (int)PM_GRAVITY,(int)PM_MAXSPEED,(int)PM_MOVESPEED,(int)PM_STOPSPEED,(int)PM_AIRCAP,(int)PM_STEPSIZE,
        (int)g_jumpVel,(int)PM_JUMP_APEX,(int)PM_FRICTION,(int)PM_ACCEL,(int)PM_AIRACCEL,(int)PM_TICKRATE,PM_AUTOHOP); L(b); }

    if (!install_hook()){ L("hook failed -- not FDNYFirefighter.exe?"); return 0; }
    L("ready -- INSERT toggle, '/' dump, '\\' reload FDNYHLMove.ini");
    return 0;
}

BOOL APIENTRY DllMain(HMODULE h, DWORD reason, LPVOID){
    if (reason == DLL_PROCESS_ATTACH){
        DisableThreadLibraryCalls(h);
        CreateThread(nullptr, 0, init_thread, nullptr, 0, nullptr);
    }
    return TRUE;
}
