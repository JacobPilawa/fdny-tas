// triggerprobe -- research spike for the trigger-box overlay (owner request
// 2026-10-22; loop status in evidence/trigger_boxes/STATUS.md). Standalone, test-
// only: the probe tests place zz_triggerprobe.asi in the game folder; nothing in
// core/src depends on it.
//
// Questions: is BoundsTriggerEnt+0xe4 a world-space AABB; can the touch action /
// kind / enabled state classify rescue zones vs exits vs other triggers; are
// RenderPoly boxes hidden by walls; what does drawing them cost.
//
// Entity list (as addresses.json entities.head): [[0x00502AAC] + 0x64] = first node,
// node {elem +0, next +4}; entity kind at +0x18. Raw addresses are fine in a probe
// (R4 covers core/src only).
//
// Genesis (capstone, 2026-10-22):
//   @geCamera_Transform@12  fastcall ECX=camera, EDX=&world, [esp+4]=&out; RET 4
//   @geCamera_Project@12    fastcall ECX=camera, EDX=&camSpace, [esp+4]=&out; RET 4
//       Z = -cam.z (clamped); x = [cam+0x28] + [cam+0x24]*cx/Z; y = [cam+0x2C] - S*cy/Z;
//       out.z = Z*[cam+0x11C]
//   @geEngine_RenderPoly@20 as in alphaprobe.cpp.
//
// Commands (game console -> "unknown command: fd_zzt_..." in FDNYCore.log):
//   fd_zzt_0            off
//   fd_zzt_1            dump: every entity (kind, vtable, +0xe4 box, +0x4c pos) once
//   fd_zzt_2_<f>_<z>    draw boxes of entities whose vtable is a BoundsTrigger vtable
//                       (learned from the dump: g_trigVtbl), white 50% tile, vertex colour
//   fd_zzt_3_<f>_<z>    same with one solid 50% tile per colour (no vertex modulation)
//     f = RenderPoly flags (default 1); z: 0 = 1.0, 1 = 1/Z, 2 = out.z of Project
//   fd_zzt_4_<n>        cost: draw every trigger box n times (n <= 50)
// Every second while drawing: player position and the boxes that contain it.
// Log: zz_triggerprobe.log in the game folder.
#include <windows.h>

#include <stdint.h>

#include "../third_party/minhook/include/MinHook.h"

namespace {

struct TLVertex {
    float x, y, z, u, v, r, g, b, a;
};
struct Vec3 {
    float x, y, z;
};

using RenderPolyFn = int(__fastcall*)(void* engine, const TLVertex* pts, int n, void* bmp, uint32_t flags);
using CreateFn = void*(__fastcall*)(int w, int h, int maxMip, int format);
using LockFn = int(__fastcall*)(void* bmp, void** target, int minMip, int maxMip, int format);
using BmpFn = int(__fastcall*)(void* bmp);
using GetBitsFn = void*(__fastcall*)(void* bmp);
using GetInfoFn = int(__fastcall*)(void* bmp, uint32_t* info, uint32_t* info2);
using EngineBmpFn = int(__cdecl*)(void* engine, void* bmp);
using FlushFn = int(__cdecl*)(void* engine);
using EndFrameFn = int(__cdecl*)(void* engine);
using RenderWorldFn = int(__cdecl*)(void* engine, void* world, void* camera, void* a4);
using CamFn = void(__fastcall*)(void* camera, const Vec3* in, Vec3* out);

RenderPolyFn g_poly;
CreateFn g_create;
LockFn g_lock;
BmpFn g_unlock;
GetBitsFn g_bits;
GetInfoFn g_info;
EngineBmpFn g_worldAdd;
FlushFn g_flush;
EndFrameFn g_endOrig;
RenderWorldFn g_rwOrig;
CamFn g_xform, g_project;
void* g_world;
void* g_camera;

char g_dir[MAX_PATH];
HANDLE g_log = INVALID_HANDLE_VALUE;
CRITICAL_SECTION g_logLock;

void Log(const char* fmt, ...) {
    char buf[1024];
    SYSTEMTIME t;
    GetLocalTime(&t);
    int o = wsprintfA(buf, "[%02d:%02d:%02d.%03d] ", t.wHour, t.wMinute, t.wSecond, t.wMilliseconds);
    va_list ap;
    va_start(ap, fmt);
    o += wvsprintfA(buf + o, fmt, ap);
    va_end(ap);
    buf[o++] = '\r', buf[o++] = '\n';
    EnterCriticalSection(&g_logLock);
    DWORD w;
    if (g_log != INVALID_HANDLE_VALUE) WriteFile(g_log, buf, o, &w, nullptr);
    LeaveCriticalSection(&g_logLock);
}

// wsprintf has no %f: fixed one decimal.
const char* F(float v, char* b) {
    const bool neg = v < 0;
    double d = neg ? -v : v;
    if (d > 2e9) d = 2e9;
    const long i = static_cast<long>(d);
    const int f = static_cast<int>((d - i) * 10);
    wsprintfA(b, "%s%ld.%d", neg ? "-" : "", i, f);
    return b;
}

bool Read(const void* p, void* out, SIZE_T n) {
    SIZE_T got = 0;
    return ReadProcessMemory(GetCurrentProcess(), p, out, n, &got) && got == n;
}
template <class T> T Rd(uintptr_t a, T def = T()) {
    T v = def;
    if (!Read(reinterpret_cast<const void*>(a), &v, sizeof(v))) return def;
    return v;
}

// --- command ----------------------------------------------------------------------
struct Cmd {
    int mode, a, b;
};
volatile LONG g_cmdSeq = 0;
Cmd g_pending = {0, 1, 0};
CRITICAL_SECTION g_cmdLock;

int ParseInt(const char*& s) {
    int v = 0;
    while (*s >= '0' && *s <= '9') v = v * 10 + (*s++ - '0');
    if (*s == '_') ++s;
    return v;
}

bool ParseCmd(const char* s, Cmd* c) {
    Cmd n = {0, 1, 0};
    if (*s < '0' || *s > '9') return false;
    n.mode = ParseInt(s);
    if (*s >= '0' && *s <= '9') n.a = ParseInt(s);
    if (*s >= '0' && *s <= '9') n.b = ParseInt(s);
    *c = n;
    return true;
}

void Post(const Cmd& c, const char* from) {
    EnterCriticalSection(&g_cmdLock);
    g_pending = c;
    LeaveCriticalSection(&g_cmdLock);
    InterlockedIncrement(&g_cmdSeq);
    Log("cmd (%s): mode %d a %d b %d", from, c.mode, c.a, c.b);
}

DWORD WINAPI Tail(LPVOID) {  // FDNYCore.log -> commands
    char path[MAX_PATH];
    wsprintfA(path, "%sFDNYCore.log", g_dir);
    LONGLONG pos = -1;
    static char buf[8192];
    static char line[512];
    int ln = 0;
    for (;;) {
        Sleep(100);
        HANDLE f = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                               OPEN_EXISTING, 0, nullptr);
        if (f == INVALID_HANDLE_VALUE) continue;
        LARGE_INTEGER size;
        GetFileSizeEx(f, &size);
        if (pos < 0 || size.QuadPart < pos) pos = size.QuadPart;
        LARGE_INTEGER at;
        at.QuadPart = pos;
        SetFilePointerEx(f, at, nullptr, FILE_BEGIN);
        DWORD got = 0;
        while (ReadFile(f, buf, sizeof(buf), &got, nullptr) && got) {
            pos += got;
            for (DWORD i = 0; i < got; ++i) {
                if (buf[i] != '\n') {
                    if (ln < static_cast<int>(sizeof(line)) - 1) line[ln++] = buf[i];
                    continue;
                }
                line[ln] = 0;
                ln = 0;
                const char* k = "unknown command: fd_zzt_";
                for (const char* s = line; *s; ++s) {
                    const char *a = s, *b = k;
                    while (*b && *a == *b) ++a, ++b;
                    if (*b) continue;
                    Cmd c;
                    if (ParseCmd(a, &c)) Post(c, "console");
                    break;
                }
            }
        }
        CloseHandle(f);
    }
}

// --- entities ----------------------------------------------------------------------
constexpr uintptr_t kSession = 0x00502AAC;  // GameSession* (addresses.json entities.head)
constexpr int kMaxEnt = 1024;

struct Ent {
    uintptr_t p, vtbl;
    int kind;
    float box[6];  // +0xe4
    float pos[3];  // +0x4c
};
Ent g_ents[kMaxEnt];
int g_nEnt;

int Walk() {
    g_nEnt = 0;
    const uintptr_t s = Rd<uintptr_t>(kSession);
    if (!s) return 0;
    uintptr_t node = Rd<uintptr_t>(s + 0x64);
    for (int guard = 0; node && guard < 4096 && g_nEnt < kMaxEnt; ++guard) {
        const uintptr_t e = Rd<uintptr_t>(node);
        node = Rd<uintptr_t>(node + 4);
        if (!e) continue;
        Ent& x = g_ents[g_nEnt++];
        x.p = e;
        x.vtbl = Rd<uintptr_t>(e);
        x.kind = Rd<int>(e + 0x18, -1);
        if (!Read(reinterpret_cast<void*>(e + 0xe4), x.box, sizeof(x.box))) x.box[0] = x.box[3] = 0;
        if (!Read(reinterpret_cast<void*>(e + 0x4c), x.pos, sizeof(x.pos))) x.pos[0] = x.pos[1] = x.pos[2] = 0;
    }
    return g_nEnt;
}

bool SaneBox(const float* b) {
    for (int i = 0; i < 6; ++i)
        if (!(b[i] > -100000.0f && b[i] < 100000.0f)) return false;  // also rejects NaN
    return b[0] < b[3] && b[1] < b[4] && b[2] < b[5];
}

// Vtables seen on the entity list; the BoundsTrigger one is found by its RTTI-less
// constructor signature: the vtable the probe is told about (env/command), else any
// vtable in the exe's .rdata with sane boxes and kinds 0/6/8 is reported in the dump.
uintptr_t g_trigVtbl[8];
int g_nTrigVtbl;
bool IsTrigger(const Ent& e);

void Dump() {
    const int n = Walk();
    Log("dump: %d entities", n);
    struct V {
        uintptr_t vtbl;
        int kind, count, sane;
    };
    static V vs[128];
    int nv = 0;
    for (int i = 0; i < n; ++i) {
        const Ent& e = g_ents[i];
        int j = 0;
        while (j < nv && !(vs[j].vtbl == e.vtbl && vs[j].kind == e.kind)) ++j;
        if (j == nv && nv < 128) vs[nv++] = {e.vtbl, e.kind, 0, 0};
        if (j < nv) ++vs[j].count, vs[j].sane += SaneBox(e.box);
        char b[6][24], q[3][24];
        Log("ent %3d %08lX vtbl %08lX kind %d box [%s %s %s]-[%s %s %s] pos (%s %s %s)%s", i, e.p, e.vtbl, e.kind,
            F(e.box[0], b[0]), F(e.box[1], b[1]), F(e.box[2], b[2]), F(e.box[3], b[3]), F(e.box[4], b[4]),
            F(e.box[5], b[5]), F(e.pos[0], q[0]), F(e.pos[1], q[1]), F(e.pos[2], q[2]),
            SaneBox(e.box) ? " SANE" : "");
    }
    for (int j = 0; j < nv; ++j)
        Log("class vtbl %08lX kind %d: %d entities, %d with a sane +0xe4 box", vs[j].vtbl, vs[j].kind, vs[j].count,
            vs[j].sane);
    // Triggers in detail: user data P = [e+0xfc] {+4 name, +8 model, +0xc, +0x10..+0x34
    // ten script-line char*}; flags +0x160 (dword), +0x164/+0x165 (bytes, SetEnabled).
    for (int i = 0; i < n; ++i) {
        const Ent& e = g_ents[i];
        if (!IsTrigger(e)) continue;
        const uintptr_t P = Rd<uintptr_t>(e.p + 0xfc);
        char name[64] = "";
        Read(reinterpret_cast<void*>(Rd<uintptr_t>(P + 4)), name, sizeof(name) - 1);
        name[sizeof(name) - 1] = 0;
        Log("trig %3d name \"%s\" model %08lX P+0xc %08lX +0x160 %08lX +0x164 %d +0x165 %d", i, name,
            Rd<uint32_t>(P + 8), Rd<uint32_t>(P + 0xc), Rd<uint32_t>(e.p + 0x160), Rd<uint8_t>(e.p + 0x164),
            Rd<uint8_t>(e.p + 0x165));
        for (int k = 0; k < 10; ++k) {
            const uintptr_t s = Rd<uintptr_t>(P + 0x10 + 4 * k);
            if (!s) continue;
            char line[200] = "";
            for (int c = 0; c < 199; ++c) {  // byte-wise: a string may end near a page edge
                if (!Read(reinterpret_cast<void*>(s + c), &line[c], 1) || !line[c]) break;
                line[c + 1] = 0;
            }
            Log("trig %3d   line %d: %s", i, k, line);
        }
    }
}

bool IsTrigger(const Ent& e) {
    for (int i = 0; i < g_nTrigVtbl; ++i)
        if (e.vtbl == g_trigVtbl[i]) return true;
    return false;
}

// --- textures ------------------------------------------------------------------------
// Solid 64x64 4444 tiles, alpha 8/15; 0 = white (vertex-coloured), 1.. = per colour.
constexpr int kTex = 64;
constexpr int kColours = 5;
const uint16_t kTexel[kColours] = {
    0x8FFF,  // white
    0x834F,  // blue   (r3 g4 b15)
    0x8F22,  // red
    0x82E3,  // green
    0x8999,  // grey
};
const float kRgb[kColours][3] = {{255, 255, 255}, {50, 70, 255}, {255, 40, 40}, {40, 230, 60}, {150, 150, 150}};
struct Tex {
    void* bmp;
    void* world;
    int settle;
};
Tex g_tex[kColours];

uint32_t HandleOf(void* bmp) { return Rd<uint32_t>(reinterpret_cast<uintptr_t>(bmp) + 0xAC); }

bool TexReady(int c) {
    Tex& t = g_tex[c];
    if (!g_world) return false;
    if (!t.bmp) {
        t.bmp = g_create(kTex, kTex, 1, 7);
        if (!t.bmp) return Log("tex %d: create failed", c), false;
        void* lk = nullptr;
        if (!g_lock(t.bmp, &lk, 0, 0, 7) || !lk) return Log("tex %d: lock failed", c), false;
        uint32_t info[9] = {};
        g_info(lk, info, nullptr);
        auto* d = static_cast<uint16_t*>(g_bits(lk));
        for (int y = 0; d && y < kTex; ++y)
            for (int x = 0; x < kTex; ++x) d[y * info[2] + x] = kTexel[c];
        g_unlock(lk);
    }
    if (t.world != g_world) {
        const int ok = g_worldAdd(g_world, t.bmp);
        Log("tex %d added to world %p: %d", c, g_world, ok);
        t.world = g_world, t.settle = 2;
    }
    if (t.settle > 0) return --t.settle, false;
    return HandleOf(t.bmp) != 0;
}

// --- box drawing ---------------------------------------------------------------------
constexpr float kNear = 2.0f;
int g_vw = 640, g_vh = 480;

struct CV {  // camera-space vertex
    float x, y, z;  // z: forward depth (= -cam.z)
};

// Sutherland-Hodgman against a plane: keep d(v) >= 0.
template <class D> int ClipPoly(const CV* in, int n, CV* out, D d) {
    int m = 0;
    for (int i = 0; i < n; ++i) {
        const CV &a = in[i], &b = in[(i + 1) % n];
        const float da = d(a), db = d(b);
        if (da >= 0) out[m++] = a;
        if ((da >= 0) != (db >= 0)) {
            const float t = da / (da - db);
            out[m++] = {a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t};
        }
    }
    return m;
}

struct Stats {
    double usSum, usMax;
    unsigned long frames, faces, polys;
} g_st;

// Face of a box: corners in camera space, colour, depth for sorting.
struct Face {
    CV c[4];
    float depth;
    int colour;
};
Face g_faces[6 * 512];
int g_nFaces;

void AddBox(const float* b, int colour) {
    Vec3 w[8];
    for (int i = 0; i < 8; ++i) w[i] = {b[(i & 1) ? 3 : 0], b[(i & 2) ? 4 : 1], b[(i & 4) ? 5 : 2]};
    CV c[8];
    for (int i = 0; i < 8; ++i) {
        Vec3 o;
        g_xform(g_camera, &w[i], &o);
        c[i] = {o.x, o.y, -o.z};
    }
    static const int kF[6][4] = {{0, 1, 3, 2}, {4, 6, 7, 5}, {0, 4, 5, 1}, {2, 3, 7, 6}, {0, 2, 6, 4}, {1, 5, 7, 3}};
    for (int f = 0; f < 6 && g_nFaces < static_cast<int>(sizeof(g_faces) / sizeof(g_faces[0])); ++f) {
        Face& F2 = g_faces[g_nFaces++];
        float dz = 0;
        for (int k = 0; k < 4; ++k) F2.c[k] = c[kF[f][k]], dz += F2.c[k].z;
        F2.depth = dz * 0.25f;
        F2.colour = colour;
    }
}

int DrawFaces(void* engine, const Cmd& cmd, bool perColourTex) {
    // painter's order: far first
    for (int i = 1; i < g_nFaces; ++i) {
        Face t = g_faces[i];
        int j = i - 1;
        while (j >= 0 && g_faces[j].depth < t.depth) g_faces[j + 1] = g_faces[j], --j;
        g_faces[j + 1] = t;
    }
    const float S = Rd<float>(reinterpret_cast<uintptr_t>(g_camera) + 0x24);
    const float XC = Rd<float>(reinterpret_cast<uintptr_t>(g_camera) + 0x28);
    const float YC = Rd<float>(reinterpret_cast<uintptr_t>(g_camera) + 0x2C);
    const float ZS = Rd<float>(reinterpret_cast<uintptr_t>(g_camera) + 0x11C);
    const float W = static_cast<float>(g_vw), H = static_cast<float>(g_vh);
    int polys = 0;
    for (int i = 0; i < g_nFaces; ++i) {
        const Face& f = g_faces[i];
        CV a[16], b[16];
        int n = ClipPoly(f.c, 4, a, [](const CV& v) { return v.z - kNear; });
        if (n < 3) continue;
        // screen-space clip done in camera space against the 4 frustum side planes
        n = ClipPoly(a, n, b, [&](const CV& v) { return (v.x * S / v.z + XC); });
        n = ClipPoly(b, n, a, [&](const CV& v) { return W - (v.x * S / v.z + XC); });
        n = ClipPoly(a, n, b, [&](const CV& v) { return (YC - v.y * S / v.z); });
        n = ClipPoly(b, n, a, [&](const CV& v) { return H - (YC - v.y * S / v.z); });
        if (n < 3) continue;
        const int tex = perColourTex ? f.colour : 0;
        if (!TexReady(tex)) continue;
        TLVertex v[16];
        for (int k = 0; k < n; ++k) {
            const float sx = XC + S * a[k].x / a[k].z, sy = YC - S * a[k].y / a[k].z;
            const float z = cmd.b == 1 ? 1.0f / a[k].z : cmd.b == 2 ? a[k].z * ZS : 1.0f;
            const float* rgb = kRgb[perColourTex ? 0 : f.colour];
            v[k] = {sx, sy, z, 0.5f, 0.5f, rgb[0], rgb[1], rgb[2], 255.0f};
        }
        g_poly(engine, v, n, g_tex[tex].bmp, static_cast<uint32_t>(cmd.a));
        ++polys;
    }
    return polys;
}

// Spike colouring: +0x165 set -> green, else blue (classification is decided from the dump).
int Colour(const Ent& e) { return Rd<uint8_t>(e.p + 0x165) ? 3 : 1; }

Cmd g_cur = {0, 1, 0};
LONG g_seenSeq = 0;
LARGE_INTEGER g_fq;
DWORD g_lastReport;
bool g_firstDraw;

void ViewSize() {
    RECT rc;
    const float XC = g_camera ? Rd<float>(reinterpret_cast<uintptr_t>(g_camera) + 0x28) : 0;
    const float YC = g_camera ? Rd<float>(reinterpret_cast<uintptr_t>(g_camera) + 0x2C) : 0;
    if (XC > 0 && YC > 0) g_vw = static_cast<int>(XC * 2 + 0.5f), g_vh = static_cast<int>(YC * 2 + 0.5f);
    else if (HWND wnd = GetActiveWindow(); wnd && GetClientRect(wnd, &rc) && rc.right > 0) g_vw = rc.right, g_vh = rc.bottom;
}

void Report() {
    int pi = -1;
    for (int i = 0; i < g_nEnt; ++i)
        if (g_ents[i].kind == 1) pi = i;
    if (pi < 0) return Log("report: no player (kind 1) entity");
    const float* p = g_ents[pi].pos;
    char a[3][24];
    char inside[512];
    int o = 0;
    inside[0] = 0;
    for (int i = 0; i < g_nEnt && o < 480; ++i) {
        const Ent& e = g_ents[i];
        if (!IsTrigger(e) || !SaneBox(e.box)) continue;
        if (p[0] >= e.box[0] && p[0] <= e.box[3] && p[1] >= e.box[1] && p[1] <= e.box[4] && p[2] >= e.box[2] &&
            p[2] <= e.box[5])
            o += wsprintfA(inside + o, " %d(k%d)", i, e.kind);
    }
    Log("player (%s %s %s) inside:%s", F(p[0], a[0]), F(p[1], a[1]), F(p[2], a[2]), o ? inside : " none");
    if (g_st.frames)
        Log("cost: %d us/frame avg, %d us max, %lu faces, %lu polys over %lu frames",
            static_cast<int>(g_st.usSum / g_st.frames), static_cast<int>(g_st.usMax), g_st.faces / g_st.frames,
            g_st.polys / g_st.frames, g_st.frames);
    g_st = {};
}

void Frame(void* engine) {
    if (g_cmdSeq != g_seenSeq) {
        EnterCriticalSection(&g_cmdLock);
        g_cur = g_pending;
        g_seenSeq = g_cmdSeq;
        LeaveCriticalSection(&g_cmdLock);
        g_st = {};
        g_firstDraw = false;
        if (g_cur.mode == 1) {
            Dump();
            g_cur.mode = 0;
        }
    }
    const Cmd& c = g_cur;
    if (c.mode < 2 || !engine || !g_camera || !g_world) return;
    LARGE_INTEGER t0, t1;
    QueryPerformanceCounter(&t0);
    Walk();
    ViewSize();
    g_flush(engine);
    g_nFaces = 0;
    const int reps = c.mode == 4 ? (c.a < 1 ? 1 : c.a > 50 ? 50 : c.a) : 1;
    int boxes = 0;
    for (int r = 0; r < reps; ++r)
        for (int i = 0; i < g_nEnt; ++i)
            if (IsTrigger(g_ents[i]) && SaneBox(g_ents[i].box)) AddBox(g_ents[i].box, Colour(g_ents[i])), ++boxes;
    Cmd dc = c;
    if (c.mode == 4) dc.a = 1, dc.b = 0;
    const int faces = g_nFaces;
    const int polys = DrawFaces(engine, dc, c.mode == 3);
    QueryPerformanceCounter(&t1);
    const double us = static_cast<double>(t1.QuadPart - t0.QuadPart) * 1e6 / static_cast<double>(g_fq.QuadPart);
    g_st.usSum += us, ++g_st.frames, g_st.faces += faces, g_st.polys += polys;
    if (us > g_st.usMax) g_st.usMax = us;
    if (!g_firstDraw) {
        g_firstDraw = true;
        Log("draw: mode %d flags %d z %d: %d boxes, %d faces, %d polys, view %dx%d", c.mode, c.a, c.b, boxes, faces,
            polys, g_vw, g_vh);
    }
    const DWORD now = GetTickCount();
    if (now - g_lastReport >= 1000) g_lastReport = now, Report();
}

int __cdecl EndFrameDetour(void* engine) {
    Frame(engine);
    return g_endOrig(engine);
}

int __cdecl RenderWorldDetour(void* engine, void* world, void* camera, void* a4) {
    if (world != g_world) Log("world %p (camera %p)", world, camera);
    g_world = world, g_camera = camera;
    return g_rwOrig(engine, world, camera, a4);
}

FARPROC Need(HMODULE m, const char* name) {
    FARPROC p = GetProcAddress(m, name);
    if (!p) Log("missing export %s", name);
    return p;
}

uintptr_t Hex(const char* s) {
    uintptr_t v = 0;
    for (; *s; ++s) {
        const char ch = *s;
        const int d = ch >= '0' && ch <= '9' ? ch - '0' : ch >= 'a' && ch <= 'f' ? ch - 'a' + 10
                    : ch >= 'A' && ch <= 'F' ? ch - 'A' + 10 : -1;
        if (d < 0) break;
        v = v * 16 + static_cast<uintptr_t>(d);
    }
    return v;
}

DWORD WINAPI Init(LPVOID) {
    HMODULE gen = GetModuleHandleA("Genesis.dll");
    if (!gen) return Log("Genesis.dll not loaded"), 0;
    g_poly = reinterpret_cast<RenderPolyFn>(Need(gen, "@geEngine_RenderPoly@20"));
    g_create = reinterpret_cast<CreateFn>(Need(gen, "@geBitmap_Create@16"));
    g_lock = reinterpret_cast<LockFn>(Need(gen, "@geBitmap_LockForWriteFormat@20"));
    g_unlock = reinterpret_cast<BmpFn>(Need(gen, "@geBitmap_UnLock@4"));
    g_bits = reinterpret_cast<GetBitsFn>(Need(gen, "@geBitmap_GetBits@4"));
    g_info = reinterpret_cast<GetInfoFn>(Need(gen, "@geBitmap_GetInfo@12"));
    g_flush = reinterpret_cast<FlushFn>(Need(gen, "geEngine_FlushPipeline"));
    g_worldAdd = reinterpret_cast<EngineBmpFn>(Need(gen, "geWorld_AddBitmap"));
    g_xform = reinterpret_cast<CamFn>(Need(gen, "@geCamera_Transform@12"));
    g_project = reinterpret_cast<CamFn>(Need(gen, "@geCamera_Project@12"));
    auto* end = reinterpret_cast<uint8_t*>(Need(gen, "geEngine_EndFrame"));
    void* rw = reinterpret_cast<void*>(Need(gen, "geEngine_RenderWorld"));
    if (!g_poly || !g_create || !g_lock || !g_unlock || !g_bits || !g_info || !g_flush || !g_worldAdd || !g_xform ||
        !g_project || !end || !rw)
        return 0;
    // TRIGGERPROBE_VTBL = comma-separated hex vtables treated as triggers.
    char env[128];
    if (GetEnvironmentVariableA("TRIGGERPROBE_VTBL", env, sizeof(env))) {
        for (char* s = env; *s && g_nTrigVtbl < 8;) {
            g_trigVtbl[g_nTrigVtbl++] = Hex(s);
            while (*s && *s != ',') ++s;
            if (*s == ',') ++s;
        }
    }
    if (!g_nTrigVtbl) g_trigVtbl[g_nTrigVtbl++] = 0x004E1090;  // BoundsTriggerEnt::Construct 0x00441CA8
    Log("trigger vtables: %d (first %08lX)", g_nTrigVtbl, g_nTrigVtbl ? g_trigVtbl[0] : 0);
    for (int i = 0; i < 150 && *end != 0xE9; ++i) Sleep(100);
    Log("EndFrame entry byte %02X (%s)", *end, *end == 0xE9 ? "already hooked: core present" : "unhooked");
    MH_STATUS s = MH_Initialize();
    if (s != MH_OK) return Log("MH_Initialize: %s", MH_StatusToString(s)), 0;
    s = MH_CreateHook(end, reinterpret_cast<void*>(&EndFrameDetour), reinterpret_cast<void**>(&g_endOrig));
    if (s == MH_OK) s = MH_EnableHook(end);
    Log("hook geEngine_EndFrame: %s", MH_StatusToString(s));
    s = MH_CreateHook(rw, reinterpret_cast<void*>(&RenderWorldDetour), reinterpret_cast<void**>(&g_rwOrig));
    if (s == MH_OK) s = MH_EnableHook(rw);
    Log("hook geEngine_RenderWorld: %s", MH_StatusToString(s));
    char cmdEnv[64];
    Cmd c;
    if (GetEnvironmentVariableA("TRIGGERPROBE", cmdEnv, sizeof(cmdEnv)) && ParseCmd(cmdEnv, &c)) Post(c, "env");
    CreateThread(nullptr, 0, Tail, nullptr, 0, nullptr);
    return 0;
}

} // namespace

extern "C" BOOL WINAPI _DllMainCRTStartup(HINSTANCE inst, DWORD reason, LPVOID) {
    if (reason != DLL_PROCESS_ATTACH) return TRUE;
    DisableThreadLibraryCalls(inst);
    InitializeCriticalSection(&g_logLock);
    InitializeCriticalSection(&g_cmdLock);
    QueryPerformanceFrequency(&g_fq);
    GetModuleFileNameA(nullptr, g_dir, MAX_PATH);
    for (int i = lstrlenA(g_dir); i > 0; --i)
        if (g_dir[i - 1] == '\\') {
            g_dir[i] = 0;
            break;
        }
    char path[MAX_PATH];
    wsprintfA(path, "%szz_triggerprobe.log", g_dir);
    g_log = CreateFileA(path, GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS, 0, nullptr);
    Log("triggerprobe loaded");
    CreateThread(nullptr, 0, Init, nullptr, 0, nullptr);
    return TRUE;
}
