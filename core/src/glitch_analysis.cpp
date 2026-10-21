#include "glitch_analysis.h"

#include <windows.h>

#include "addr.h"
#include "log.h"

namespace fdcore::glitch::analysis {
namespace {

namespace cls = addr::classify;

constexpr double FLOOR_NY = 0.9, WALL_NY = 0.1, CROUCH_BOX_MAX_Y = 55.0;
constexpr uint32_t KIND_TRACE = 1, KIND_COMMIT = 2, KIND_ENGINE_RAY = 3;
constexpr int R_KIND = 0, R_TICK = 1, R_RET0 = 2, R_S30 = 3, R_S34 = 4, R_S44 = 5, R_S90 = 7, R_SC4 = 8,
              R_RESULT = 9, R_FRESH = 10, R_IMP = 11, R_NRM = 14, R_DIST = 17, R_PRE = 18, R_THIS = 21, R_GPL = 22;
constexpr uint32_t GENESIS_PREF = 0x10000000;

// ---------------------------------------------------------------------------
// Number formatting identical to Python's %.Nf: round the exact binary value
// half-to-even. Every value here comes from a float32, so v * 10^d (d <= 3)
// is exact in double and an integer half-even rounding reproduces Python.
bool SignBit(double v) {
    uint64_t b;
    __builtin_memcpy(&b, &v, 8);
    return (b >> 63) != 0;
}

bool HalfEven(double t, long* out) {
    if (!(t > -2.0e9 && t < 2.0e9)) return false;
    long ip = static_cast<long>(t);       // truncates toward zero
    if (t < 0 && static_cast<double>(ip) != t) --ip;  // floor
    double frac = t - static_cast<double>(ip);
    if (frac > 0.5 || (frac == 0.5 && (ip & 1))) ++ip;
    *out = ip;
    return true;
}

const long kPow10[] = {1, 10, 100, 1000};

// "%.<d>f" of v into out (no padding).
int Fixed(char* out, double v, int d) {
    if (v != v) return wsprintfA(out, "nan");
    long r;
    if (!HalfEven(v * kPow10[d], &r)) return wsprintfA(out, v < 0 ? "-inf" : "inf");
    bool neg = r < 0 || (r == 0 && SignBit(v));
    unsigned long a = r < 0 ? static_cast<unsigned long>(-r) : static_cast<unsigned long>(r);
    if (d == 0) return wsprintfA(out, "%s%lu", neg ? "-" : "", a);
    char fmt[16];
    wsprintfA(fmt, "%%s%%lu.%%0%dlu", d);
    return wsprintfA(out, fmt, neg ? "-" : "", a / kPow10[d], a % kPow10[d]);
}

// Python round(x, 1): the key used for writer lookup / coordinates.
struct Key {
    long k[3];
    uint8_t neg0;  // display sign of a -0.0 component (Python keeps it; -0.0 == 0.0 for lookup)
    bool valid;
};

Key MakeKey(const float* v) {
    Key key{};
    key.valid = true;
    for (int i = 0; i < 3; ++i) {
        long r = 0;
        HalfEven(static_cast<double>(v[i]) * 10.0, &r);
        key.k[i] = r;
        if (r == 0 && SignBit(v[i])) key.neg0 |= 1 << i;
    }
    return key;
}

bool KeyEq(const Key& a, const Key& b) {
    return a.valid && b.valid && a.k[0] == b.k[0] && a.k[1] == b.k[1] && a.k[2] == b.k[2];
}

int KeyComp(char* out, const Key& key, int i) {  // "%.1f" of round(x, 1)
    long r = key.k[i];
    bool neg = r < 0 || (r == 0 && (key.neg0 >> i) & 1);
    unsigned long a = r < 0 ? static_cast<unsigned long>(-r) : static_cast<unsigned long>(r);
    return wsprintfA(out, "%s%lu.%lu", neg ? "-" : "", a / 10, a % 10);
}

// Small string builder.
struct Buf {  // used as a static (game thread only): 4 KB frames would need stack probes
    char s[4096];
    int n = 0;
    Buf& reset() {
        n = 0;
        s[0] = 0;
        return *this;
    }
    void put(const char* t) {
        while (*t && n < 4095) s[n++] = *t++;
        s[n] = 0;
    }
    void pad(const char* t, int width, bool left) {  // %-Ns / %Ns (no truncation)
        int len = lstrlenA(t);
        if (!left)
            for (int i = len; i < width; ++i) put(" ");
        put(t);
        if (left)
            for (int i = len; i < width; ++i) put(" ");
    }
    void num(long v, int width) {
        char t[24];
        wsprintfA(t, "%ld", v);
        pad(t, width, false);
    }
};

// ---------------------------------------------------------------------------
enum Kind : uint8_t { K_HIT, K_REUSED, K_MISS, K_COMMIT, K_WARP };
const char* const kKindName[] = {"hit", "reused", "miss", "commit", "warp"};
enum Role : uint8_t { R_NONE, R_FIRST, R_RESWEEP, R_STEPUP, R_FORWARD, R_STEPDOWN, R_COMMITROLE };
const char* const kRoleName[] = {"None", "first", "resweep", "stepup", "forward", "stepdown", "commit"};

// The fields of a Python Ev that any consumer reads.
struct Ev {
    uint32_t seq, tick;
    Kind kind;
    const char* source;
    char func[72];
    uint32_t chain[2];
    uint8_t nchain;
    Key pos;
    float normal[3];
    bool hasNormal;
    float dist;
    bool hasDist;
    Role role;
    Key pre, frm;
};

double Ny(const Ev& e) { return e.hasNormal ? static_cast<double>(e.normal[1]) : 0.0; }

const char* SurfaceName(const Ev& e) {
    if (!e.hasNormal) return "?";
    double ny = Ny(e);
    if (ny > FLOOR_NY) return "Floor";
    if (ny < -WALL_NY) return "Ceiling";
    if (ny < WALL_NY) return "Wall";
    return "Slope";
}

bool WarpAllowed(const Ev& e) { return e.hasNormal && Ny(e) <= FLOOR_NY; }

// --- Model.writers: OrderedDict, FIFO eviction by last-write order ---------------
constexpr int kWritersMax = 50000;
constexpr int kPool = kWritersMax + 1;
constexpr int kHash = 1 << 17;
Ev g_wr[kPool];
int g_prev[kPool], g_next[kPool];
int g_head = -1, g_tail = -1, g_count = 0, g_free = 0;
int g_freeList[kPool];
int g_table[kHash];  // -1 empty, else pool index

uint32_t HashKey(const Key& k) {
    uint32_t h = 2166136261u;
    for (int i = 0; i < 3; ++i) h = (h ^ static_cast<uint32_t>(k.k[i])) * 16777619u;
    return h;
}

int FindSlot(const Key& k) {  // table slot holding k, or the empty slot where it would go
    uint32_t i = HashKey(k) & (kHash - 1);
    while (g_table[i] >= 0 && !KeyEq(g_wr[g_table[i]].pos, k)) i = (i + 1) & (kHash - 1);
    return static_cast<int>(i);
}

void TableErase(int slot) {  // backward-shift deletion (linear probing)
    int i = slot;
    for (;;) {
        g_table[i] = -1;
        int j = i;
        for (;;) {
            j = (j + 1) & (kHash - 1);
            if (g_table[j] < 0) return;
            int home = static_cast<int>(HashKey(g_wr[g_table[j]].pos) & (kHash - 1));
            bool between = i <= j ? (home > i && home <= j) : (home > i || home <= j);
            if (!between) break;
        }
        g_table[i] = g_table[j];
        i = j;
    }
}

void Unlink(int x) {
    if (g_prev[x] >= 0) g_next[g_prev[x]] = g_next[x];
    else g_head = g_next[x];
    if (g_next[x] >= 0) g_prev[g_next[x]] = g_prev[x];
    else g_tail = g_prev[x];
}

void PushBack(int x) {
    g_prev[x] = g_tail;
    g_next[x] = -1;
    if (g_tail >= 0) g_next[g_tail] = x;
    else g_head = x;
    g_tail = x;
}

void WritersReset() {
    for (int i = 0; i < kHash; ++i) g_table[i] = -1;
    for (int i = 0; i < kPool; ++i) g_freeList[i] = kPool - 1 - i;
    g_free = kPool;
    g_head = g_tail = -1;
    g_count = 0;
}

void NoteWrite(const Ev& ev) {  // writers[pos] = ev; move_to_end; evict oldest
    int slot = FindSlot(ev.pos);
    if (g_table[slot] >= 0) {
        int x = g_table[slot];
        g_wr[x] = ev;  // the key keeps its original -0.0 display? Python replaces the value only
        Unlink(x);
        PushBack(x);
        return;
    }
    int x = g_freeList[--g_free];
    g_wr[x] = ev;
    g_table[slot] = x;
    PushBack(x);
    if (++g_count > kWritersMax) {
        int old = g_head;
        Unlink(old);
        TableErase(FindSlot(g_wr[old].pos));
        g_freeList[g_free++] = old;
        --g_count;
    }
}

const Ev* FindWriter(const Key& k) {
    if (!k.valid) return nullptr;
    int slot = FindSlot(k);
    return g_table[slot] >= 0 ? &g_wr[g_table[slot]] : nullptr;
}

// --- Model ----------------------------------------------------------------------
struct Row {           // history entry: a capable hit (kind hit) or a warp
    Ev ev;
    uint32_t count, lastTick;
    bool hasSrc;
    Ev src;
    char detail[2048];
};
constexpr int kHistory = 500;
Row g_hist[kHistory];
int g_histStart = 0, g_histLen = 0;
uint32_t g_seq = 0;
bool g_crouchbug = false;
Key g_slot;
bool g_hasSlotWriter = false;
Ev g_slotWriter;
uint32_t g_warps = 0;
char g_verdict[96] = "";

Row& HistAt(int i) { return g_hist[(g_histStart + i) % kHistory]; }  // 0 = oldest

Row& HistAppend() {
    if (g_histLen == kHistory) g_histStart = (g_histStart + 1) % kHistory;
    else ++g_histLen;
    return HistAt(g_histLen - 1);
}

uint32_t NextSeq() { return ++g_seq; }

// --- TextLog --------------------------------------------------------------------
void Out(const char* line) { log::Raw("glitch| ", line); }

void Line(const Ev& ev, const char* kind, uint32_t at, const char* notes) {
    static Buf sb;
    Buf& b = sb.reset();
    char t[64];
    b.num(static_cast<long>(ev.seq), 8);
    b.put(" ");
    b.num(static_cast<long>(ev.tick), 7);
    b.put(" ");
    b.num(static_cast<long>(at), 7);
    b.put("  ");
    b.pad(kind, 7, true);
    b.put("  ");
    b.pad(ev.source, 6, true);
    b.put("  ");
    char func[35];
    lstrcpynA(func, ev.func, sizeof(func));
    b.pad(func, 34, true);
    for (int i = 0; i < 3; ++i) {
        b.put(" ");
        if (ev.pos.valid) KeyComp(t, ev.pos, i);
        else lstrcpyA(t, "nan");
        b.pad(t, 10, false);
    }
    b.put(" ");
    for (int i = 0; i < 3; ++i) {
        b.put(" ");
        if (ev.hasNormal) Fixed(t, ev.normal[i], 3);
        else lstrcpyA(t, "nan");
        b.pad(t, 7, false);
    }
    b.put("  ");
    for (int i = 0; i < ev.nchain; ++i) {
        wsprintfA(t, i ? " %08lx" : "%08lx", static_cast<unsigned long>(ev.chain[i]));
        b.put(t);
    }
    b.put(notes);
    Out(b.s);
}

void Note(const char* text) {
    SYSTEMTIME st;
    GetLocalTime(&st);
    char line[1200];
    wsprintfA(line, "# %02d:%02d:%02d  %s", st.wHour, st.wMinute, st.wSecond, text);
    Out(line);
}

void Header() {
    SYSTEMTIME st;
    GetLocalTime(&st);
    char line[256];
    wsprintfA(line, "# FDNY collision log  %04d-%02d-%02d %02d:%02d:%02d", st.wYear, st.wMonth, st.wDay, st.wHour,
              st.wMinute, st.wSecond);
    Out(line);
    Out("# Only what can warp the player:");
    Out("#   capable = a write with a non-floor plane (normal.y <= 0.90) that was still");
    Out("#             in the slot when the player's sweep began (first time seen)");
    Out("#   warp    = the player was moved onto a reused impact; 'frame' lists every");
    Out("#             sweep+Think iteration that frame (role:result); 'predicted' is");
    Out("#             what the WARP status said just before that frame ran");
    Out("# chain = return addresses, innermost first (exe callers of the trace)");
    Out("#");
    static Buf sb;
    Buf& b = sb.reset();
    b.put("#");
    b.pad("seq", 7, false), b.put(" "), b.pad("tick", 7, false), b.put(" "), b.pad("at", 7, false), b.put("  ");
    b.pad("kind", 7, true), b.put("  "), b.pad("source", 6, true), b.put("  "), b.pad("function", 34, true);
    b.put(" "), b.pad("x", 10, false), b.put(" "), b.pad("y", 10, false), b.put(" "), b.pad("z", 10, false);
    b.put("  "), b.pad("nX", 7, false), b.put(" "), b.pad("nY", 7, false), b.put(" "), b.pad("nZ", 7, false);
    b.put("  chain / notes");
    Out(b.s);
}

void LogCapable(const Row& row, uint32_t consumed) {
    char notes[96];
    wsprintfA(notes, "  %s, survived %ld tick(s)", SurfaceName(row.ev),
              static_cast<long>(consumed) - static_cast<long>(row.ev.tick));
    Line(row.ev, "capable", consumed, notes);
}

void LogWarp(const Row& row) {
    static char notes[2400];
    char src[200];
    if (row.hasSrc)
        wsprintfA(src, "<- %s %s written @tick %lu (%s)", row.src.source, row.src.func,
                  static_cast<unsigned long>(row.src.tick), SurfaceName(row.src));
    else lstrcpyA(src, "<- writer unknown (engine lighting ray or before attach)");
    char frm[96] = "";
    if (row.ev.frm.valid) {
        char a[24], b2[24], c[24];
        KeyComp(a, row.ev.frm, 0), KeyComp(b2, row.ev.frm, 1), KeyComp(c, row.ev.frm, 2);
        wsprintfA(frm, "  from %s %s %s", a, b2, c);
    }
    wsprintfA(notes, "  %s%s  frame: ", src, frm);
    int n = lstrlenA(notes);
    lstrcpynA(notes + n, row.detail, static_cast<int>(sizeof(notes)) - n);
    Line(row.ev, "WARP", row.ev.tick, notes);
}

// --- crouchbug (track_crouchbug) ---------------------------------------------------
void TrackCrouchbug(const Snapshot& s, uint32_t tick) {
    bool bug = s.playerPtr && s.state != 4 && static_cast<double>(s.maxs[1]) < CROUCH_BOX_MAX_Y;
    if (bug == g_crouchbug) return;
    g_crouchbug = bug;
    char text[400];
    if (bug) {
        char m[24], x[24], y[24], z[24];
        Fixed(m, s.maxs[1], 0), Fixed(x, s.pos[0], 1), Fixed(y, s.pos[1], 1), Fixed(z, s.pos[2], 1);
        wsprintfA(text,
                  "crouchbug ON at tick %lu: stance %ld with the crouched %s-unit box top, pos %s %s %s -- "
                  "camera/speed are standing, collision and the warp use the short box",
                  static_cast<unsigned long>(tick), static_cast<long>(s.state), m, x, y, z);
    } else {
        wsprintfA(text, "crouchbug OFF at tick %lu", static_cast<unsigned long>(tick));
    }
    Note(text);
}

// --- FrameAnalyzer --------------------------------------------------------------
struct Step {
    Role role;
    Kind kind;
    bool hasSrc;
    Ev src;
};
struct Iter {
    Kind sweepKind;
    Role sweepRole;
    Step steps[8];
    int nsteps;
    bool hasCommit;
};
constexpr int kMaxIters = 32;
Iter g_iters[kMaxIters];
int g_nIters = 0;
bool g_anHasTick = false;
uint32_t g_anTick = 0;

const char* const kOutcome[] = {"clear", "floor contact", "already stepped", "step-up blocked", "forward blocked",
                                "no floor below", "stepped up", "WARPED"};
enum Outcome { O_CLEAR, O_FLOOR, O_LATCHED, O_STEPUP, O_FORWARD, O_NOFLOOR, O_STEPPED, O_WARP };

const Step* FindStep(const Iter& it, Role r) {  // dict(role -> e): the LAST step of that role
    const Step* f = nullptr;
    for (int i = 0; i < it.nsteps; ++i)
        if (it.steps[i].role == r) f = &it.steps[i];
    return f;
}

Outcome IterationOutcome(const Iter& it, bool committedBefore) {
    if (it.sweepKind == K_MISS) return O_CLEAR;
    const Step* up = FindStep(it, R_STEPUP);
    if (!up) return committedBefore ? O_LATCHED : O_FLOOR;
    if (up->kind != K_MISS) return O_STEPUP;
    const Step* fw = FindStep(it, R_FORWARD);
    if (!fw || fw->kind != K_MISS) return O_FORWARD;
    const Step* sd = FindStep(it, R_STEPDOWN);
    if (!sd || sd->kind == K_MISS) return O_NOFLOOR;
    return sd->kind == K_REUSED ? O_WARP : O_STEPPED;
}

void Describe(char* out, int cap) {
    static Buf sb;
    Buf& b = sb.reset();
    bool committed = false;
    for (int i = 0; i < g_nIters; ++i) {
        const Iter& it = g_iters[i];
        Outcome o = IterationOutcome(it, committed);
        committed = committed || it.hasCommit;
        char t[64];
        wsprintfA(t, "%s#%d %s [%s:%s", i ? "; " : "", i + 1, kOutcome[o], kRoleName[it.sweepRole],
                  kKindName[it.sweepKind]);
        b.put(t);
        for (int s = 0; s < it.nsteps; ++s) {
            wsprintfA(t, " %s:%s", kRoleName[it.steps[s].role], kKindName[it.steps[s].kind]);
            b.put(t);
        }
        b.put("]");
    }
    lstrcpynA(out, b.s, cap);
}

void Finish() {
    int n = g_nIters;
    g_nIters = 0;
    g_anHasTick = false;
    if (!n) return;
    Outcome outs[kMaxIters];
    bool committed = false;
    int reusedI = -1;
    bool anyWarp = false;
    for (int i = 0; i < n; ++i) {
        outs[i] = IterationOutcome(g_iters[i], committed);
        committed = committed || g_iters[i].hasCommit;
        if (reusedI < 0 && g_iters[i].sweepKind == K_REUSED) reusedI = i;
        if (outs[i] == O_WARP) anyWarp = true;
    }
    if (anyWarp) lstrcpyA(g_verdict, "WARPED");
    else if (reusedI >= 0) {
        Outcome o = outs[reusedI];
        if (o == O_FLOOR || o == O_LATCHED) lstrcpyA(g_verdict, "inside solid - slot is a floor (no warp)");
        else wsprintfA(g_verdict, "ARMED - %s", kOutcome[o]);
    } else {
        static const char* const kClear[] = {"no contact", "on floor", "stepped up", "against a wall",
                                             "against a wall", "at a ledge", "stepped up", "WARPED"};
        lstrcpyA(g_verdict, kClear[outs[0]]);
    }
}

void AddCapable(const Ev& w, uint32_t tick) {
    int from = g_histLen > 4 ? g_histLen - 4 : 0;
    for (int i = from; i < g_histLen; ++i) {
        Row& row = HistAt(i);
        if (row.ev.kind == K_HIT && lstrcmpA(row.ev.source, w.source) == 0 && KeyEq(row.ev.pos, w.pos)) {
            if (row.lastTick != tick) ++row.count, row.lastTick = tick;
            return;
        }
    }
    Row& row = HistAppend();
    row.ev = w;
    row.ev.seq = NextSeq();
    row.ev.kind = K_HIT;
    row.ev.role = R_NONE;
    row.ev.pre.valid = row.ev.frm.valid = false;
    row.count = 1;
    row.lastTick = tick;
    row.hasSrc = false;
    row.detail[0] = 0;
    LogCapable(row, tick);
}

void Feed(Ev& ev, bool& hasSrc, Ev& src) {
    if (g_anHasTick && ev.tick != g_anTick) Finish();
    g_anTick = ev.tick;
    g_anHasTick = true;
    if (ev.kind == K_HIT) NoteWrite(ev);
    if (ev.kind == K_REUSED && !hasSrc && ev.pre.valid) {
        if (const Ev* w = FindWriter(ev.pre)) hasSrc = true, src = *w;
    }
    if (ev.role == R_FIRST) {
        const Ev* w = ev.pre.valid ? FindWriter(ev.pre) : nullptr;
        g_slot = ev.pre;
        g_hasSlotWriter = w != nullptr;
        if (w) g_slotWriter = *w;
        if (w && lstrcmpA(w->source, "Player") != 0 && WarpAllowed(*w)) AddCapable(*w, ev.tick);
    }
    if (ev.role == R_FIRST || ev.role == R_RESWEEP) {
        if (g_nIters < kMaxIters) {
            Iter& it = g_iters[g_nIters++];
            it.sweepKind = ev.kind, it.sweepRole = ev.role, it.nsteps = 0, it.hasCommit = false;
        }
    } else if ((ev.role == R_STEPUP || ev.role == R_FORWARD || ev.role == R_STEPDOWN) && g_nIters) {
        Iter& it = g_iters[g_nIters - 1];
        if (it.nsteps < 8) it.steps[it.nsteps++] = {ev.role, ev.kind, hasSrc, src};
    } else if (ev.role == R_COMMITROLE && g_nIters) {
        Iter& it = g_iters[g_nIters - 1];
        it.hasCommit = true;
        const Step* sd = nullptr;  // next(...): the FIRST step with role stepdown
        for (int i = 0; i < it.nsteps && !sd; ++i)
            if (it.steps[i].role == R_STEPDOWN) sd = &it.steps[i];
        if (sd && sd->kind == K_REUSED) {
            Row& row = HistAppend();
            row.ev = ev;
            row.ev.seq = NextSeq();
            row.ev.kind = K_WARP;
            row.ev.source = sd->hasSrc ? sd->src.source : "Unknown";
            lstrcpyA(row.ev.func, "Think step-down commit (reused impact)");
            if (sd->hasSrc) {
                for (int i = 0; i < 3; ++i) row.ev.normal[i] = sd->src.normal[i];
                row.ev.hasNormal = sd->src.hasNormal;
            }
            row.ev.hasDist = false;
            row.hasSrc = sd->hasSrc;
            if (sd->hasSrc) row.src = sd->src;
            row.count = 1, row.lastTick = ev.tick;
            Describe(row.detail, sizeof(row.detail));
            ++g_warps;
            // 11b: no prediction yet (both sides), so describe_window(None) == "none".
            int n = lstrlenA(row.detail);
            lstrcpynA(row.detail + n, "; predicted: none", static_cast<int>(sizeof(row.detail)) - n);
            LogWarp(row);
        }
    }
}

// --- GameSource._classify -------------------------------------------------------
const cls::Outer* FindOuter(uint32_t ret) {
    for (const auto& o : cls::kOuterRets)
        if (o.ret == ret) return &o;
    return nullptr;
}

void NearestFunc(uint32_t addr, const char** src, char* name) {
    const cls::FuncStart* best = nullptr;
    for (const auto& f : cls::kFuncStarts) {
        if (f.start <= addr) best = &f;
        else break;
    }
    if (best && addr - best->start < 0x2000) {
        *src = best->source;
        wsprintfA(name, "%s+0x%lx", best->name, static_cast<unsigned long>(addr - best->start));
    } else {
        *src = "Other";
        wsprintfA(name, "0x%08lx", static_cast<unsigned long>(addr));
    }
}

void Classify(const uint32_t* r, uint32_t gdelta, Ev& ev) {
    const uint32_t ret0 = r[R_RET0];
    uint32_t outer;
    ev.chain[0] = ret0;
    ev.nchain = 1;
    if (ret0 == cls::kRet_tracetofloor) outer = r[R_S90];
    else if (ret0 == cls::kRet_groundheight) outer = r[R_S34];
    else if (ret0 == cls::kRet_segmentlos) outer = r[R_S30] == cls::kRet_los_activeworld ? r[R_S44] : r[R_S30];
    else if (ret0 == cls::kRet_sweepbox) outer = r[R_S30];
    else if (ret0 == cls::kRet_floorme) {
        outer = r[R_SC4];
        if (!FindOuter(outer)) {
            ev.source = "Fire";
            lstrcpyA(ev.func, "FireRescueEnt::FloorMe");
            ev.chain[1] = outer, ev.nchain = 2;
            return;
        }
    } else if (ret0 == cls::kRet_hosegfx) {
        ev.source = "Hose";
        lstrcpyA(ev.func, "HoseGfx::RebuildGeometry");
        return;
    } else {
        uint32_t rel = ret0 - gdelta;
        if (rel >= GENESIS_PREF && rel < GENESIS_PREF + 0x400000) {
            ev.source = "Engine";
            const char* nm = nullptr;
            for (const auto& g : cls::kGenesisRets)
                if (g.rel == rel) nm = g.name;
            if (nm) lstrcpyA(ev.func, nm);
            else wsprintfA(ev.func, "engine 0x%08lx", static_cast<unsigned long>(rel));
            return;
        }
        NearestFunc(ret0, &ev.source, ev.func);
        return;
    }
    if (const cls::Outer* o = FindOuter(outer)) {
        ev.source = o->source;
        lstrcpyA(ev.func, o->name);
    } else {
        NearestFunc(outer, &ev.source, ev.func);
    }
    ev.chain[1] = outer, ev.nchain = 2;
}

Role RoleOf(uint32_t ret) {
    for (const auto& r : cls::kRoles) {
        const char* n = r.role;
        if (r.ret != ret) continue;
        if (lstrcmpA(n, "first") == 0) return R_FIRST;
        if (lstrcmpA(n, "resweep") == 0) return R_RESWEEP;
        if (lstrcmpA(n, "stepup") == 0) return R_STEPUP;
        if (lstrcmpA(n, "forward") == 0) return R_FORWARD;
        if (lstrcmpA(n, "stepdown") == 0) return R_STEPDOWN;
    }
    return R_NONE;
}

const float* F(const uint32_t* r, int i) { return reinterpret_cast<const float*>(r + i); }

} // namespace

void BeginSession() {
    WritersReset();
    g_histStart = g_histLen = 0;
    g_seq = 0;
    g_crouchbug = false;
    g_slot.valid = false;
    g_hasSlotWriter = false;
    g_nIters = 0;
    g_anHasTick = false;
    g_warps = 0;
    g_verdict[0] = 0;
    Header();
}

void Poll(const Snapshot& s, const uint32_t* const* records, int n) {
    const uint32_t gdelta = s.genesisBase - GENESIS_PREF;
    TrackCrouchbug(s, s.tick);
    for (int i = 0; i < n; ++i) {
        const uint32_t* r = records[i];
        Ev ev{};
        ev.tick = r[R_TICK];
        bool hasSrc = false;
        static Ev src;
        if (r[R_KIND] == KIND_TRACE) {
            Classify(r, gdelta, ev);
            ev.kind = r[R_RESULT] == 0 ? K_MISS : (r[R_FRESH] == 0 ? K_REUSED : K_HIT);
            ev.role = lstrcmpA(ev.source, "Player") == 0 ? RoleOf(ev.chain[ev.nchain - 1]) : R_NONE;
            ev.seq = NextSeq();
            ev.pos = MakeKey(F(r, R_IMP));
            for (int k = 0; k < 3; ++k) ev.normal[k] = F(r, R_GPL)[k];
            ev.hasNormal = true;
            ev.dist = *F(r, R_DIST), ev.hasDist = true;
            ev.pre = MakeKey(F(r, R_PRE));
            Feed(ev, hasSrc, src);
        } else if (r[R_KIND] == KIND_ENGINE_RAY) {
            ev.seq = NextSeq();
            ev.kind = K_HIT;
            ev.source = "Engine";
            lstrcpyA(ev.func, "engine lighting ray (floor light / lightmap)");
            ev.nchain = 0;
            ev.pos = MakeKey(F(r, R_IMP));
            for (int k = 0; k < 3; ++k) ev.normal[k] = F(r, R_GPL)[k];
            ev.hasNormal = true;
            Feed(ev, hasSrc, src);
        } else if (r[R_KIND] == KIND_COMMIT && r[R_THIS] == s.playerPtr) {
            ev.seq = NextSeq();
            ev.kind = K_COMMIT;
            ev.source = "Player";
            lstrcpyA(ev.func, "Think step-down commit");
            ev.chain[0] = cls::kRet_commit_site, ev.nchain = 1;
            ev.pos = MakeKey(F(r, R_IMP));
            for (int k = 0; k < 3; ++k) ev.normal[k] = F(r, R_NRM)[k];
            ev.hasNormal = true;
            ev.role = R_COMMITROLE;
            ev.frm = MakeKey(F(r, R_PRE));
            Feed(ev, hasSrc, src);
        }
    }
    if (g_anHasTick && s.tick > g_anTick) Finish();  // end_of_poll
}

const char* VerdictText() { return g_verdict; }

} // namespace fdcore::glitch::analysis
