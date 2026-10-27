#include "glitch_bsp.h"

#include <windows.h>
#include <emmintrin.h>

#include "glitch_analysis.h"
#include "log.h"

namespace fdcore::glitch::bsp {
namespace {

constexpr int GBSP_MODELS = 1, GBSP_NODES = 2, GBSP_LEAFS = 4, GBSP_LEAFSIDES = 8, GBSP_PLANES = 10;
constexpr int32_t PLAYER_MASK = 0x43;
constexpr double STEP_UP = 16.0, STEP_DOWN = 17.0, FLOOR_NY = 0.9;

// Genesis 0x10051330: float32 0x3dcccccd. Compile-time constant: a dynamically
// initialised global is never initialised in this no-CRT DLL (rule R7; this one
// was, until -Wglobal-constructors caught it in 0.16.1).
constexpr double ON_EPSILON = static_cast<double>(__builtin_bit_cast(float, 0x3DCCCCCDu));

double F32(double v) { return static_cast<double>(static_cast<float>(v)); }  // FSTP dword
double Sqrt(double v) { return _mm_cvtsd_f64(_mm_sqrt_sd(_mm_set_sd(v), _mm_set_sd(v))); }
double Min(double a, double b) { return b < a ? b : a; }  // Python min(a, b)
double Max(double a, double b) { return b > a ? b : a; }  // Python max(a, b)

void* Alloc(size_t n) { return VirtualAlloc(nullptr, n ? n : 1, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE); }
void Free(void* p) {
    if (p) VirtualFree(p, 0, MEM_RELEASE);
}

// --- a .bsp file, mapped read-only ---------------------------------------------
struct File {
    HANDLE f = INVALID_HANDLE_VALUE, m = nullptr;
    const uint8_t* p = nullptr;
    uint32_t size = 0;
    bool Open(const char* path) {
        f = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
        if (f == INVALID_HANDLE_VALUE) return false;
        size = GetFileSize(f, nullptr);
        if (size == 0) return true;
        m = CreateFileMappingA(f, nullptr, PAGE_READONLY, 0, 0, nullptr);
        if (m) p = static_cast<const uint8_t*>(MapViewOfFile(m, FILE_MAP_READ, 0, 0, 0));
        return p != nullptr;
    }
    ~File() {
        if (p) UnmapViewOfFile(p);
        if (m) CloseHandle(m);
        if (f != INVALID_HANDLE_VALUE) CloseHandle(f);
    }
};

int32_t I32(const uint8_t* p) {
    int32_t v;
    __builtin_memcpy(&v, p, 4);
    return v;
}
float Flt(const uint8_t* p) {
    float v;
    __builtin_memcpy(&v, p, 4);
    return v;
}

struct Chunk {
    bool present;
    uint32_t off;
    int32_t sz, n;
};

// gbsp_chunks: first occurrence of each chunk type.
void Chunks(const File& f, Chunk (&ch)[16]) {
    for (auto& c : ch) c = Chunk{};
    int64_t p = 0;
    const int64_t end = f.size;
    while (p + 12 <= end) {
        int32_t t = I32(f.p + p), sz = I32(f.p + p + 4), n = I32(f.p + p + 8);
        p += 12;
        if (t >= 0 && t < 16 && !ch[t].present) ch[t] = {true, static_cast<uint32_t>(p), sz, n};
        if (t == 0xFFFF || sz < 0 || n < 0) break;
        p += static_cast<int64_t>(sz) * n;
    }
}

// read_chunk: the bytes Python's f.read() would return (short at end of file).
uint32_t ChunkBytes(const File& f, const Chunk& c, uint32_t limit) {
    uint64_t want = static_cast<uint64_t>(c.sz) * static_cast<uint64_t>(c.n);
    if (want > limit) want = limit;
    uint64_t avail = c.off <= f.size ? f.size - c.off : 0;
    return static_cast<uint32_t>(want < avail ? want : avail);
}

// --- MapFinder sigs ----------------------------------------------------------------
struct Sig {
    char path[MAX_PATH];
    char name[64];
    uint8_t bytes[kSigBytes];
    uint32_t len;
};
constexpr int kMaxMaps = 64;
Sig* g_sigs = nullptr;
int g_nsigs = 0;
int g_current = -1;

// --- Bsp --------------------------------------------------------------------------
struct Leaf {
    int first, count;  // into g_sides / g_exp
    bool cached;
    double box[6];  // mins, maxs the expansion was made for
};
struct Map {
    int sig = -1;
    int nplanes = 0, nnodes = 0, nleafs = 0, nsolid = 0, root = 0;
    float* planes = nullptr;   // 4 per plane
    int32_t* nodes = nullptr;  // front, back, plane
    int* leafSolid = nullptr;  // leaf -> solid index, -1 if not solid
    Leaf* solid = nullptr;
    float* sides = nullptr;  // 4 per side, flipped to face out of the solid
    float* exp = nullptr;    // the same sides expanded by the box (cached per leaf)
    void Release() {
        Free(planes), Free(nodes), Free(leafSolid), Free(solid), Free(sides), Free(exp);
        *this = Map{};
    }
} g_map;

bool Load(int sig) {
    g_map.Release();
    File f;
    if (!f.Open(g_sigs[sig].path) || !f.p) return false;
    Chunk ch[16];
    Chunks(f, ch);
    static const int kNeed[] = {GBSP_PLANES, GBSP_NODES, GBSP_LEAFSIDES, GBSP_MODELS, GBSP_LEAFS};
    for (int t : kNeed)
        if (!ch[t].present || static_cast<uint64_t>(ch[t].off) + static_cast<uint64_t>(ch[t].sz) * ch[t].n > f.size)
            return false;
    Map& m = g_map;
    const Chunk &cp = ch[GBSP_PLANES], &cn = ch[GBSP_NODES], &cs = ch[GBSP_LEAFSIDES], &cm = ch[GBSP_MODELS],
                &cl = ch[GBSP_LEAFS];
    m.nplanes = cp.n;
    m.planes = static_cast<float*>(Alloc(static_cast<size_t>(cp.n) * 16));
    for (int i = 0; i < cp.n; ++i)
        for (int k = 0; k < 4; ++k) m.planes[i * 4 + k] = Flt(f.p + cp.off + i * cp.sz + k * 4);
    m.nnodes = cn.n;
    m.nodes = static_cast<int32_t*>(Alloc(static_cast<size_t>(cn.n) * 12));
    for (int i = 0; i < cn.n; ++i) {
        const uint8_t* r = f.p + cn.off + i * cn.sz;
        m.nodes[i * 3] = I32(r), m.nodes[i * 3 + 1] = I32(r + 4), m.nodes[i * 3 + 2] = I32(r + 16);
    }
    m.root = I32(f.p + cm.off);
    m.nleafs = cl.n;
    m.leafSolid = static_cast<int*>(Alloc(static_cast<size_t>(cl.n) * 4));
    int nsolid = 0, nsides = 0;
    for (int li = 0; li < cl.n; ++li) {
        const uint8_t* r = f.p + cl.off + li * cl.sz;
        int32_t cont = I32(r), first = I32(r + 52), count = I32(r + 56);
        m.leafSolid[li] = -1;
        if (!(cont & PLAYER_MASK) || count == 0) continue;
        // sides[first:first+count] (Python slice, clamped)
        int lo = first < 0 ? 0 : (first > cs.n ? cs.n : first);
        int hi = first + count;
        hi = hi < lo ? lo : (hi > cs.n ? cs.n : hi);
        m.leafSolid[li] = nsolid++;
        nsides += hi - lo;
    }
    m.nsolid = nsolid;
    m.solid = static_cast<Leaf*>(Alloc(static_cast<size_t>(nsolid) * sizeof(Leaf)));
    m.sides = static_cast<float*>(Alloc(static_cast<size_t>(nsides) * 16));
    m.exp = static_cast<float*>(Alloc(static_cast<size_t>(nsides) * 16));
    int si = 0;
    for (int li = 0; li < cl.n; ++li) {
        if (m.leafSolid[li] < 0) continue;
        const uint8_t* r = f.p + cl.off + li * cl.sz;
        int32_t first = I32(r + 52), count = I32(r + 56);
        int lo = first < 0 ? 0 : (first > cs.n ? cs.n : first);
        int hi = first + count;
        hi = hi < lo ? lo : (hi > cs.n ? cs.n : hi);
        Leaf& L = m.solid[m.leafSolid[li]];
        L.first = si, L.count = hi - lo, L.cached = false;
        for (int s = lo; s < hi; ++s, ++si) {
            const uint8_t* sr = f.p + cs.off + s * cs.sz;
            int32_t pn = I32(sr), pside = I32(sr + 4);
            const float* pl = m.planes + pn * 4;
            for (int k = 0; k < 4; ++k) m.sides[si * 4 + k] = pside ? -pl[k] : pl[k];
        }
    }
    m.sig = sig;
    return true;
}

// Bsp.expanded: leaf sides pushed out by the box (Genesis FUN_10035320).
const float* Expanded(int solid, const double* mins, const double* maxs) {
    Leaf& L = g_map.solid[solid];
    float* e = g_map.exp + L.first * 4;
    bool same = L.cached;
    for (int i = 0; same && i < 3; ++i) same = L.box[i] == mins[i] && L.box[3 + i] == maxs[i];
    if (same) return e;
    for (int s = 0; s < L.count; ++s) {
        const float* p = g_map.sides + (L.first + s) * 4;
        double d = p[3];
        for (int i = 0; i < 3; ++i) {
            double c = p[i];
            d = F32(d - c * (c > 0 ? mins[i] : maxs[i]));
        }
        e[s * 4] = p[0], e[s * 4 + 1] = p[1], e[s * 4 + 2] = p[2], e[s * 4 + 3] = static_cast<float>(d);
    }
    for (int i = 0; i < 3; ++i) L.box[i] = mins[i], L.box[3 + i] = maxs[i];
    L.cached = true;
    return e;
}

// Bsp.walk: solid leaves whose BSP cell meets lo..hi, in the engine's order.
// Calls fn(solidIndex) for each.
template <class Fn>
void Walk(const double* lo, const double* hi, Fn fn) {
    static int stack[4096];
    int sp = 0;
    stack[sp++] = g_map.root;
    while (sp) {
        int n = stack[--sp];
        while (n >= 0) {
            const int32_t* nd = g_map.nodes + n * 3;
            const float* pl = g_map.planes + nd[2] * 4;
            double nx = pl[0], ny = pl[1], nz = pl[2], d = pl[3];
            double dmax = nx * (nx > 0 ? hi[0] : lo[0]) + ny * (ny > 0 ? hi[1] : lo[1]) + nz * (nz > 0 ? hi[2] : lo[2]);
            double dmin = nx * (nx > 0 ? lo[0] : hi[0]) + ny * (ny > 0 ? lo[1] : hi[1]) + nz * (nz > 0 ? lo[2] : hi[2]);
            bool f = dmax >= d, b = dmin < d;
            if (f && b) {
                if (sp < 4096) stack[sp++] = nd[1];
                n = nd[0];
            } else if (f) {
                n = nd[0];
            } else if (b) {
                n = nd[1];
            } else {
                break;
            }
        }
        if (n < 0) {
            int leaf = -1 - n;
            if (leaf < g_map.nleafs && g_map.leafSolid[leaf] >= 0) fn(g_map.leafSolid[leaf]);
        }
    }
}

struct SweepState {
    bool e90;
    double best;
    bool hasNormal;
    double normal[3];
    bool f44;
    const double* S;
};

// Bsp._clip: literal port of Genesis' per-leaf box clip FUN_10036ea0.
bool Clip(const float* hull, int count, const double* F, const double* B, int side, bool pside, SweepState& st) {
    if (!pside) return false;
    if (side >= count) return true;
    const float* h = hull + side * 4;
    double nx = h[0], ny = h[1], nz = h[2], d = h[3];
    double fd = nx * F[0] + ny * F[1] + nz * F[2] - d;
    double bd = nx * B[0] + ny * B[1] + nz * B[2] - d;
    if (fd >= 0 && bd >= 0) return false;
    if (fd < 0 && bd < 0) return Clip(hull, count, F, B, side + 1, true, st);
    bool side2 = fd < 0;
    double t = (side2 ? (fd + ON_EPSILON) : (fd - ON_EPSILON)) / (fd - bd);
    t = t < 0 ? 0.0 : (t > 1 ? 1.0 : t);
    double I[3] = {F32(F[0] + t * (B[0] - F[0])), F32(F[1] + t * (B[1] - F[1])), F32(F[2] + t * (B[2] - F[2]))};
    if (Clip(hull, count, F, I, side + 1, side2, st)) {
        st.e90 = true;
        return true;
    }
    if (!Clip(hull, count, I, B, side + 1, !side2, st)) return false;
    const double* S = st.S;
    double dx = I[0] - S[0], dy = I[1] - S[1], dz = I[2] - S[2];
    double dist = Sqrt(dx * dx + dy * dy + dz * dz);
    st.e90 = true;
    if (dist < st.best && !st.f44) {
        st.best = F32(dist);
        st.hasNormal = true;
        st.normal[0] = nx, st.normal[1] = ny, st.normal[2] = nz;
        st.f44 = true;
    }
    return true;
}

// Bsp.sweep -> result, normal of a fresh hit.
SweepResult Sweep(const double* S, const double* E, const double* mins, const double* maxs, double* normal) {
    double lo[3], hi[3];
    for (int i = 0; i < 3; ++i) {
        lo[i] = Min(S[i], E[i]) + mins[i] - 1.0;
        hi[i] = Max(S[i], E[i]) + maxs[i] + 1.0;
    }
    SweepState st{false, 9999.0, false, {0, 0, 0}, false, S};
    Walk(lo, hi, [&](int solid) {
        st.f44 = false;
        Clip(Expanded(solid, mins, maxs), g_map.solid[solid].count, S, E, 0, true, st);
    });
    if (st.hasNormal) {
        if (normal) normal[0] = st.normal[0], normal[1] = st.normal[1], normal[2] = st.normal[2];
        return S_HIT;
    }
    return st.e90 ? S_STALE : S_MISS;
}

const char* const kSweepName[] = {"miss", "hit", "stale"};
const char* const kSweepLabel[] = {"Move", "Up", "Fwd", "Down"};

void Copy(char* dst, const char* src, int cap) { lstrcpynA(dst, src, cap); }

} // namespace

void Index(const char* mapsDir, void (*note)(const char*)) {
    if (!g_sigs) g_sigs = static_cast<Sig*>(Alloc(sizeof(Sig) * kMaxMaps));
    g_nsigs = 0;
    char pattern[MAX_PATH];
    wsprintfA(pattern, "%s\\*", mapsDir);
    WIN32_FIND_DATAA fd;
    HANDLE h = (GetFileAttributesA(mapsDir) & FILE_ATTRIBUTE_DIRECTORY) && GetFileAttributesA(mapsDir) != INVALID_FILE_ATTRIBUTES
                   ? FindFirstFileA(pattern, &fd)
                   : INVALID_HANDLE_VALUE;
    if (h != INVALID_HANDLE_VALUE) {
        do {
            const int len = lstrlenA(fd.cFileName);
            if (len < 4 || lstrcmpiA(fd.cFileName + len - 4, ".bsp") != 0 || g_nsigs >= kMaxMaps) continue;
            Sig& s = g_sigs[g_nsigs];
            wsprintfA(s.path, "%s\\%s", mapsDir, fd.cFileName);
            File f;
            if (!f.Open(s.path) || (f.size && !f.p)) continue;
            Chunk ch[16];
            if (f.size) Chunks(f, ch);
            else for (auto& c : ch) c = Chunk{};
            if (!ch[GBSP_PLANES].present) continue;  // KeyError: skipped
            s.len = ChunkBytes(f, ch[GBSP_PLANES], kSigBytes);
            __builtin_memcpy(s.bytes, f.p + ch[GBSP_PLANES].off, s.len);
            // os.path.splitext(basename)[0]
            Copy(s.name, fd.cFileName, sizeof(s.name));
            for (int i = lstrlenA(s.name) - 1; i > 0; --i)
                if (s.name[i] == '.') {
                    s.name[i] = 0;
                    break;
                }
            ++g_nsigs;
        } while (FindNextFileA(h, &fd));
        FindClose(h);
    }
    char text[MAX_PATH + 64];
    wsprintfA(text, "warp window: %d maps indexed from %s", g_nsigs, mapsDir);
    note(text);
    g_current = -1;
}

bool HaveSigs() { return g_nsigs > 0; }

void ResetCurrent() { g_current = -1; }

bool Update(const uint8_t* live, void (*note)(const char*)) {
    if (!g_nsigs || !live) return g_current >= 0;
    if (g_current >= 0) {
        const Sig& s = g_sigs[g_current];
        if (__builtin_memcmp(live, s.bytes, s.len) == 0) return true;
    }
    int match = -1, nmatch = 0;
    for (int i = 0; i < g_nsigs; ++i)
        if (__builtin_memcmp(live, g_sigs[i].bytes, g_sigs[i].len) == 0) match = i, ++nmatch;
    if (nmatch != 1) {
        if (g_current >= 0) note("warp window: map no longer recognised");
        g_current = -1;
        return false;
    }
    if (g_map.sig != match) {
        const ULONGLONG t0 = GetTickCount64();
        if (!Load(match)) {  // Python would raise; treat as not recognised
            log::Warn("glitch: could not load %s", g_sigs[match].path);
            g_current = -1;
            return false;
        }
        log::Info("glitch: warp window loaded %s in %lu ms (game thread)", g_sigs[match].name,
                  static_cast<unsigned long>(GetTickCount64() - t0));
    }
    g_current = match;
    char text[160];
    wsprintfA(text, "warp window: map %s (%d solid leaves)", g_sigs[match].name, g_map.nsolid);
    note(text);
    return true;
}

const char* CurrentName() { return g_current >= 0 ? g_sigs[g_current].name : ""; }

void Predict(const float posf[3], const float minsf[3], const float maxsf[3], const float dispf[3], double slotNy,
             Window& r) {
    double pos[3], mins[3], maxs[3];
    for (int i = 0; i < 3; ++i) pos[i] = posf[i], mins[i] = minsf[i], maxs[i] = maxsf[i];
    const double dx = dispf[0], dy = dispf[1], dz = dispf[2];
    r = Window{};
    r.hasMap = true;
    Copy(r.map, CurrentName(), sizeof(r.map));
    r.level = L_CLEAR;
    Copy(r.text, "In bounds - no warp possible", sizeof(r.text));
    // inside(): solid leaves whose expanded hull strictly contains pos
    static int inside[4096];
    int nin = 0;
    double lo[3], hi[3];
    for (int i = 0; i < 3; ++i) lo[i] = pos[i] + mins[i] - 1.0, hi[i] = pos[i] + maxs[i] + 1.0;
    Walk(lo, hi, [&](int solid) {
        const float* e = Expanded(solid, mins, maxs);
        for (int s = 0; s < g_map.solid[solid].count; ++s) {
            const float* h = e + s * 4;
            if (!(static_cast<double>(h[0]) * pos[0] + static_cast<double>(h[1]) * pos[1] +
                      static_cast<double>(h[2]) * pos[2] - static_cast<double>(h[3]) <
                  0))
                return;
        }
        if (nin < 4096) inside[nin++] = solid;
    });
    r.inside = nin;
    if (!nin) return;
    for (int k = 0; k < nin; ++k) {
        const float* e = Expanded(inside[k], mins, maxs);
        for (int s = 0; s < g_map.solid[inside[k]].count; ++s) {
            double nx = e[s * 4], ny = e[s * 4 + 1], nz = e[s * 4 + 2], d = e[s * 4 + 3];
            if (ny < 0) {
                double gap = d - (nx * pos[0] + ny * pos[1] + nz * pos[2]);
                if (!r.hasGap || gap < r.gap) r.hasGap = true, r.gap = gap, r.win = -ny + nx * dx + nz * dz;
            }
        }
    }
    const double up[3] = {pos[0], pos[1] + STEP_UP, pos[2]};
    const double fwdEnd[3] = {pos[0] + dx, pos[1] + STEP_UP, pos[2] + dz};
    const double moveEnd[3] = {pos[0] + dx, pos[1] + dy, pos[2] + dz};
    const double downEnd[3] = {fwdEnd[0], fwdEnd[1] - STEP_DOWN, fwdEnd[2]};
    double mn[3] = {0, 0, 0};
    SweepResult move = Sweep(pos, moveEnd, mins, maxs, mn);
    SweepResult sUp = Sweep(pos, up, mins, maxs, nullptr);
    SweepResult sFwd = Sweep(up, fwdEnd, mins, maxs, nullptr);
    SweepResult sDown = Sweep(fwdEnd, downEnd, mins, maxs, nullptr);
    r.hasSweeps = true;
    r.sweeps[0] = move, r.sweeps[1] = sUp, r.sweeps[2] = sFwd, r.sweeps[3] = sDown;
    const double gateNy = move == S_HIT ? mn[1] : slotNy;
    const bool gateOk = gateNy <= FLOOR_NY;
    if (move != S_MISS && sUp == S_MISS && sFwd == S_MISS && sDown == S_STALE) {
        if (move == S_HIT) r.level = L_ARMED, Copy(r.text, "LINED UP - target is your own spot (tiny move)", 96);
        else if (!gateOk) r.level = L_ARMED, Copy(r.text, "LINED UP - waiting for a wall or slope target", 96);
        else r.level = L_WARP, Copy(r.text, "WARP NEXT FRAME if you keep moving", 96);
        return;
    }
    r.level = L_SOLID;
    if (!r.hasGap) {
        Copy(r.text, "In solid - nothing here to line up with", 96);
    } else if (r.gap > r.win) {
        double x = r.gap - r.win;
        if (9999.0 < x) x = 9999.0;  // min(gap - win, 9999.0)
        char t[48];
        analysis::PyFixed(t, x, 1);
        wsprintfA(r.text, "In solid - %s u too high to warp", t);
    } else if (__builtin_fabs(dx) + __builtin_fabs(dy) + __builtin_fabs(dz) < 1e-6) {
        Copy(r.text, "In solid - lined up, start moving to warp", 96);
    } else if (move == S_MISS) {
        Copy(r.text, "In solid - lined up, move toward open space", 96);
    } else if (sUp != S_MISS) {
        Copy(r.text, "In solid - lined up, but blocked above", 96);
    } else if (sFwd != S_MISS) {
        Copy(r.text, "In solid - lined up, but blocked ahead", 96);
    } else if (sDown == S_HIT) {
        Copy(r.text, "In solid - lined up, but a floor stops it", 96);
    } else {
        Copy(r.text, "In solid - almost lined up", 96);
    }
}

void Describe(const Window* w, bool hasTick, uint32_t tick, char* out, int cap) {
    if (!w) {
        Copy(out, "none", cap);
        return;
    }
    char s[400];
    Copy(s, w->text, sizeof(s));
    char t[160];
    if (hasTick) {
        wsprintfA(t, " (made %ld tick(s) before)", static_cast<long>(tick) - static_cast<long>(w->tick));
        lstrcatA(s, t);
    }
    if (w->hasGap) {
        char g[48], wn[48];
        analysis::PyFixed(g, w->gap, 2), analysis::PyFixed(wn, w->win, 2);
        wsprintfA(t, ", head %s u above underside (window %s u)", g, wn);
        lstrcatA(s, t);
    }
    if (w->hasSweeps) {
        for (int i = 0; i < 4; ++i) {
            wsprintfA(t, "%s%s:%s", i ? " " : ", ", kSweepLabel[i], kSweepName[w->sweeps[i]]);
            lstrcatA(s, t);
        }
    }
    Copy(out, s, cap);
}

} // namespace fdcore::glitch::bsp
