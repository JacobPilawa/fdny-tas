#include "glitch_analysis.h"

#include <windows.h>

#include "addr.h"
#include "glitch_bsp.h"
#include "log.h"

namespace fdcore::glitch::analysis {
namespace {

namespace cls = addr::classify;

constexpr double FLOOR_NY = 0.9, WALL_NY = 0.1, CROUCH_BOX_MAX_Y = 55.0;
constexpr uint32_t KIND_TRACE = 1, KIND_COMMIT = 2, KIND_ENGINE_RAY = 3;
constexpr int R_KIND = 0, R_TICK = 1, R_RET0 = 2, R_S30 = 3, R_S34 = 4, R_S44 = 5, R_S90 = 7, R_SC4 = 8,
              R_RESULT = 9, R_FRESH = 10, R_IMP = 11, R_NRM = 14, R_DIST = 17, R_PRE = 18, R_THIS = 21, R_GPL = 22;
constexpr uint32_t GENESIS_PREF = 0x10000000;  // arch-allow: R4 Genesis.dll preferred base (PE header), as the Python tool normalises

// ---------------------------------------------------------------------------
// Number formatting identical to Python's %.Nf: round the exact binary value
// half-to-even.
bool SignBit(double v) {
    uint64_t b;
    __builtin_memcpy(&b, &v, 8);
    return (b >> 63) != 0;
}

// For float32-sourced values v * 10 is exact, so an integer half-even
// rounding reproduces Python's round(x, 1) (writer keys).
bool HalfEven(double t, long* out) {
    if (!(t > -2.0e9 && t < 2.0e9)) return false;
    long ip = static_cast<long>(t);       // truncates toward zero
    if (t < 0 && static_cast<double>(ip) != t) --ip;  // floor
    double frac = t - static_cast<double>(ip);
    if (frac > 0.5 || (frac == 0.5 && (ip & 1))) ++ip;
    *out = ip;
    return true;
}

const uint64_t kPow10[] = {1, 10, 100, 1000};

// "%.<d>f" (d <= 3) of any double into out (no padding): the exact value
// m * 2^e scaled by 10^d in integers, rounded half-to-even. |v| >= 2^53 is
// out of range here (never a coordinate) and printed via its integer part.
int Fixed(char* out, double v, int d) {
    uint64_t bits;
    __builtin_memcpy(&bits, &v, 8);
    const bool neg = (bits >> 63) != 0;
    const int ex = static_cast<int>((bits >> 52) & 0x7FF);
    uint64_t m = bits & ((1ULL << 52) - 1);
    if (ex == 0x7FF) return wsprintfA(out, m ? "nan" : (neg ? "-inf" : "inf"));
    int e;
    if (ex == 0) e = -1074;
    else m |= 1ULL << 52, e = ex - 1075;
    uint64_t q;
    if (e >= 0) {
        if (e > 10) return wsprintfA(out, "%s%lu", neg ? "-" : "", 0xFFFFFFFFUL);  // unreachable for our values
        q = (m * kPow10[d]) << e;
    } else {
        const uint64_t M = m * kPow10[d];  // < 2^63
        const int s = -e;
        if (s >= 64) {
            q = 0;  // M < 2^63 <= half: rounds down
        } else {
            q = M >> s;
            const uint64_t rem = M & ((1ULL << s) - 1), half = 1ULL << (s - 1);
            if (rem > half || (rem == half && (q & 1))) ++q;
        }
    }
    char digits[32];
    int n = 0;
    do digits[n++] = static_cast<char>('0' + q % 10), q /= 10;
    while (q);
    while (n <= d) digits[n++] = '0';
    int o = 0;
    if (neg) out[o++] = '-';
    for (int i = n - 1; i >= 0; --i) {
        out[o++] = digits[i];
        if (i == d && d) out[o++] = '.';
    }
    out[o] = 0;
    return o;
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
Key g_slot;  // Model.slot = (pos, normal or None); valid=false: slot is None
float g_slotNormal[3];
bool g_slotHasNormal = false;
bool g_hasSlotWriter = false;
Ev g_slotWriter;
long long g_lastPlayerTraceTick = -1000000000LL;  // GameSource.last_player_trace_tick
uint32_t g_tick = 0;                               // Model.tick
bool g_hasLastWarp = false;                        // Model.last_warp (survives history eviction)
Row g_lastWarp;
ULONGLONG g_lastWarpWall = 0;                      // its Ev.wall (LAST WARP flash)
bool g_hasPlayer = false;                          // Model.player
Snapshot g_player;
uint32_t g_warps = 0;
char g_verdict[96] = "";

// Model.window / predictions (deque maxlen 256) / pred_stats
bool g_hasWindow = false;
bsp::Window g_window;
constexpr int kPred = 256;
bsp::Window g_pred[kPred];
int g_predStart = 0, g_predLen = 0;
uint32_t g_polls = 0, g_warpPolls = 0, g_warpsPredicted = 0;

void SetWindow(const bsp::Window* w, uint32_t tick) {  // Model.set_window
    g_hasWindow = w != nullptr;
    if (!w) return;
    g_window = *w;
    g_window.tick = tick;
    if (g_window.hasSweeps || g_window.level == bsp::L_CLEAR) {
        ++g_polls;
        g_warpPolls += g_window.level == bsp::L_WARP;
        if (g_predLen == kPred) g_predStart = (g_predStart + 1) % kPred;
        else ++g_predLen;
        g_pred[(g_predStart + g_predLen - 1) % kPred] = g_window;
    }
}

const bsp::Window* PredictionBefore(uint32_t tick) {
    for (int i = g_predLen - 1; i >= 0; --i) {
        const bsp::Window& w = g_pred[(g_predStart + i) % kPred];
        if (w.tick <= tick) return &w;
    }
    return nullptr;
}

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
        g_slotHasNormal = false;
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
            const bsp::Window* pw = PredictionBefore(ev.tick);
            if (pw && pw->level == bsp::L_WARP) ++g_warpsPredicted;
            char pred[512];
            bsp::Describe(pw, true, ev.tick, pred, sizeof(pred));
            int n = lstrlenA(row.detail);
            lstrcpynA(row.detail + n, "; predicted: ", static_cast<int>(sizeof(row.detail)) - n);
            n = lstrlenA(row.detail);
            lstrcpynA(row.detail + n, pred, static_cast<int>(sizeof(row.detail)) - n);
            g_lastWarp = row, g_hasLastWarp = true;  // Model.add_warp
            g_lastWarpWall = GetTickCount64();
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

void BeginSession(const char* mapsDir) {
    WritersReset();
    g_hasWindow = false;
    g_predStart = g_predLen = 0;
    g_polls = g_warpPolls = g_warpsPredicted = 0;
    g_histStart = g_histLen = 0;
    g_seq = 0;
    g_crouchbug = false;
    g_slot.valid = false;
    g_slotHasNormal = false;
    g_hasSlotWriter = false;
    g_lastPlayerTraceTick = -1000000000LL;
    g_tick = 0;
    g_hasLastWarp = false;
    g_hasPlayer = false;
    g_nIters = 0;
    g_anHasTick = false;
    g_warps = 0;
    g_verdict[0] = 0;
    Header();
    bsp::Index(mapsDir, Note);  // GameSource.__init__ -> MapFinder(...)
}

void EndSession() {
    Note("hooks removed, original bytes restored");
    char text[200];
    wsprintfA(text, "warp window: %lu predictions, %lu said WARP NEXT FRAME; %lu warps, %lu of them predicted",
              static_cast<unsigned long>(g_polls), static_cast<unsigned long>(g_warpPolls),
              static_cast<unsigned long>(g_warps), static_cast<unsigned long>(g_warpsPredicted));
    Note(text);
}

int PyFixed(char* out, double v, int d) { return Fixed(out, v, d); }

void Poll(const Snapshot& s, const uint32_t* const* records, int n) {
    const uint32_t gdelta = s.genesisBase - GENESIS_PREF;
    g_tick = s.tick;
    g_hasPlayer = s.playerPtr != 0;
    g_player = s;
    TrackCrouchbug(s, s.tick);
    // GameSource._predict (after track_crouchbug, before the records)
    bool haveMap = bsp::HaveSigs() && bsp::CurrentName()[0];
    if (s.mapCheck) haveMap = bsp::Update(s.planes, Note);
    if (!s.playerPtr) {
        SetWindow(nullptr, s.tick);
    } else {
        static bsp::Window w;
        w = bsp::Window{};
        w.level = bsp::L_NONE;
        if (!haveMap) {
            lstrcpyA(w.text, bsp::HaveSigs() ? "map not recognised" : "Data\\Maps not found (use --maps)");
        } else if (s.spectator) {
            w.hasMap = true;
            lstrcpynA(w.map, bsp::CurrentName(), sizeof(w.map));
            lstrcpyA(w.text, "spectator - no collision");
        } else {
            bsp::Predict(s.pos, s.mins, s.maxs, s.disp, s.slotPlane[1], w);
        }
        SetWindow(&w, s.tick);
    }
    // WARP box Target: the slot as the player's own first trace saw it; a live
    // read when the player has not traced recently.
    if (static_cast<long long>(s.tick) - g_lastPlayerTraceTick > 10) {
        g_slot = MakeKey(s.slotImpact);
        for (int k = 0; k < 3; ++k) g_slotNormal[k] = s.slotPlane[k];
        g_slotHasNormal = true;
        g_hasSlotWriter = false;
    }
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
            if (lstrcmpA(ev.source, "Player") == 0) g_lastPlayerTraceTick = ev.tick;
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

uint32_t Tick() { return g_tick; }

void ClearHistory() {
    g_histStart = g_histLen = 0;
    g_hasLastWarp = false;
    Note("history cleared (F12)");
}

// --- Overlay.build() --------------------------------------------------------------
// Text plus the original's Text-widget tags, per character (attrs). The tags
// map to colours in the renderer.
namespace {

constexpr int W_INNER = 61, COORD_COL = 19, SRC_W = COORD_COL - 8, HISTORY_ROWS = 10, AGE_DIM_TICKS = 180;
constexpr ULONGLONG WARP_FLASH_MS = 1000;
const char* const kStateNames[] = {"Standing", "Crouched", "Climbing", "Out cold", "Scripted"};  // 3..7

uint8_t SrcTag(const char* s) {
    static const char* const kSrc[] = {"Victim", "Fire", "Hose", "HoseRope", "Axe", "Player", "Engine"};
    for (int i = 0; i < 7; ++i)
        if (lstrcmpA(s, kSrc[i]) == 0) return static_cast<uint8_t>(T_SRC_VICTIM + i);
    return T_SRC_OTHER;
}

uint8_t PlayerTag(const char* s) {  // pl_tag(): PLAYER_COLORS or dim
    static const char* const kPl[] = {"Standing", "Crouched", "Climbing", "Out cold", "Scripted", "Bugged", "Ground", "In air"};
    for (int i = 0; i < 8; ++i)
        if (lstrcmpA(s, kPl[i]) == 0) return static_cast<uint8_t>(T_PL_STANDING + i);
    return T_DIM;
}

struct Box {
    char (*L)[kBoxLineLen];
    uint8_t (*A)[kBoxLineLen];
    int n = 0;
    char row[kBoxLineLen * 2];
    uint8_t rowA[kBoxLineLen * 2];
    int rn = 0;
    uint8_t bg = 0;  // body(..., bg): the background bit for every segment and the pad
    void put(const char* s, uint8_t tag) {
        // with a background, plain tags render white (the bg tag outranks them);
        // source / player colours keep their foreground (they outrank the bg tag)
        if (bg && tag < T_SRC_VICTIM) tag = T_WHITE;
        for (; *s && rn < kBoxLineLen * 2 - 1; ++s) row[rn] = *s, rowA[rn++] = static_cast<uint8_t>(tag | bg);
        row[rn] = 0;
    }
    void pad(const char* s, int width, bool left, uint8_t tag) {  // "%-Ns" / "%Ns" in one segment
        int len = lstrlenA(s);
        if (!left)
            for (int i = len; i < width; ++i) put(" ", tag);
        put(s, tag);
        if (left)
            for (int i = len; i < width; ++i) put(" ", tag);
    }
    void emit(const char* s, const uint8_t* a, int len) {
        if (n >= kBoxLines) return;
        if (len > kBoxLineLen - 1) len = kBoxLineLen - 1;
        for (int i = 0; i < len; ++i) {
            L[n][i] = s[i];
            if (A) A[n][i] = a[i];
        }
        L[n][len] = 0;
        ++n;
    }
    void top(const char* title, const char* extra, bool flash) {
        rn = 0, bg = 0;
        const uint8_t frame = flash ? static_cast<uint8_t>(T_WHITE | BG_FLASH) : T_FRAME;
        put("+-- ", frame), put(title, T_TITLE);
        if (extra[0]) put(extra, flash ? T_ALERT : T_HEADER);
        put(" ", frame);
        for (int i = W_INNER + 4 - 6 - lstrlenA(title) - lstrlenA(extra); i > 0; --i) put("-", frame);
        put("+", frame);
        emit(row, rowA, rn);
    }
    void begin(uint8_t background = 0) {  // body(): "| " in frame colour, then the segments
        rn = 0, bg = 0;
        put("| ", T_FRAME);
        bg = background;
    }
    void seg(const char* s, uint8_t tag) { put(s, tag); }
    void end() {
        for (int i = rn - 2; i < W_INNER; ++i) put(" ", T_TEXT);  // pad: (bg or "text")
        bg = 0;
        put(" |", T_FRAME);
        emit(row, rowA, rn);
    }
    void body(const char* s, uint8_t tag) { begin(), seg(s, tag), end(); }
    void bottom() {
        rn = 0, bg = 0;
        put("+", T_FRAME);
        for (int i = 0; i < W_INNER + 2; ++i) put("-", T_FRAME);
        put("+", T_FRAME);
        emit(row, rowA, rn);
    }
};

void Coords(Box& x, const Key& k, uint8_t tag) {  // fmt_coords of a round(.., 1) tuple
    char t[32];
    for (int i = 0; i < 3; ++i) {
        if (k.valid) KeyComp(t, k, i);
        else lstrcpyA(t, "nan");
        x.pad(t, 10, false, tag);
    }
}

void CoordsF(Box& x, const float* v, uint8_t tag) {  // fmt_coords of raw floats
    char t[32];
    for (int i = 0; i < 3; ++i) Fixed(t, v[i], 1), x.pad(t, 10, false, tag);
}

// ev_row(ev, dim); tickCol replaces the tick column (the Target row)
void EvRow(Box& x, const Ev& ev, bool dim, const char* tickCol) {
    char t[32];
    if (tickCol) {
        x.seg(tickCol, T_HEADER);
    } else {
        wsprintfA(t, "%6ld  ", static_cast<long>(ev.tick));
        x.seg(t, dim ? T_DIM : T_TEXT);
    }
    x.pad(ev.source, SRC_W, true, dim ? T_DIM : SrcTag(ev.source));
    Coords(x, ev.pos, dim ? T_DIM : T_TEXT);
    const char* surf = SurfaceName(ev);
    x.seg("  ", dim ? T_DIM : (lstrcmpA(surf, "Floor") == 0 ? T_OK : T_ALERT));
    x.seg(surf, dim ? T_DIM : (lstrcmpA(surf, "Floor") == 0 ? T_OK : T_ALERT));
}

void TableHeader(Box& x) {
    x.begin();
    x.pad("Tick", 6, false, T_HEADER), x.seg("  ", T_HEADER), x.pad("Source", SRC_W, true, T_HEADER);
    x.pad("X", 10, false, T_HEADER), x.pad("Y", 10, false, T_HEADER), x.pad("Z", 10, false, T_HEADER);
    x.seg("  Surface", T_HEADER);
    x.end();
}

} // namespace

int BuildBox(char (*lines)[kBoxLineLen], uint8_t (*attrs)[kBoxLineLen]) {
    static Box x;  // static: its row buffers would need stack probes (no CRT)
    x.L = lines, x.A = attrs, x.n = 0;
    char t[200];
    // WARP: who you are, what the next frame will do, where it would send you
    // ("  FROZEN" is never visible in the original: build() only runs unfrozen.)
    const bsp::Window* w = g_hasWindow ? &g_window : nullptr;
    char extra[160] = "";
    if (w && w->hasMap && w->map[0]) wsprintfA(extra, "  (map %s)", w->map);
    wsprintfA(t, "  tick %lu", static_cast<unsigned long>(g_tick));
    lstrcatA(extra, t);
    x.top("WARP", extra, false);
    x.begin();
    for (int i = 0; i < COORD_COL; ++i) x.seg(" ", T_HEADER);
    x.pad("X", 10, false, T_HEADER), x.pad("Y", 10, false, T_HEADER), x.pad("Z", 10, false, T_HEADER);
    x.end();
    if (g_hasPlayer) {
        const Snapshot& p = g_player;
        char stance[16];
        if (p.state >= 3 && p.state <= 7) lstrcpyA(stance, kStateNames[p.state - 3]);
        else wsprintfA(stance, "State %ld", static_cast<long>(p.state));
        stance[8] = 0;  // [:8]
        uint8_t stanceTag = PlayerTag(stance);
        if (p.state != 4 && static_cast<double>(p.maxs[1]) < CROUCH_BOX_MAX_Y) {  // crouchbug
            if (p.state == 3) lstrcpyA(stance, "Bugged");
            stanceTag = T_PL_BUGGED;
        }
        const char* ground = p.ground ? "Ground" : "In air";
        x.begin();
        x.pad(stance, 8, true, stanceTag);
        x.seg("  -  ", T_FRAME);
        x.pad(ground, 6, true, PlayerTag(ground));
        CoordsF(x, p.pos, T_TEXT);
        x.end();
    } else {
        x.body("(no player -- menu or loading)", T_DIM);
    }
    if (!w || w->level == bsp::L_NONE) {
        x.body(w ? w->text : "", T_DIM);
    } else {
        static const uint8_t kLevelTag[] = {T_OK, T_ALERT, static_cast<uint8_t>(T_WHITE | BG_FLASH), T_WARN};
        x.body(w->text, kLevelTag[w->level]);  // clear / armed / warp / solid
    }
    if (w && w->inside && g_slot.valid) {
        const Ev* wr = (g_hasSlotWriter && KeyEq(g_slotWriter.pos, g_slot)) ? &g_slotWriter : FindWriter(g_slot);
        static Ev unk;
        if (!wr) {  // no logged writer has these coords
            unk = Ev{};
            unk.source = "Unknown";
            unk.pos = g_slot;
            unk.hasNormal = g_slotHasNormal;
            for (int k = 0; k < 3; ++k) unk.normal[k] = g_slotNormal[k];
            wr = &unk;
        }
        x.begin();
        EvRow(x, *wr, false, "Target  ");
        x.end();
    } else {
        x.body("Target  -", T_DIM);
    }
    x.bottom();

    // LAST WARP
    if (!g_hasLastWarp) {
        x.top("LAST WARP", "", false);
        TableHeader(x);
        x.body("(none yet)", T_DIM);
    } else {
        const Row& lw = g_lastWarp;
        const bool flash = GetTickCount64() - g_lastWarpWall < WARP_FLASH_MS;
        wsprintfA(t, "  -  %ld ticks ago", static_cast<long>(g_tick) - static_cast<long>(lw.ev.tick));
        x.top("LAST WARP", t, flash);
        TableHeader(x);
        x.begin(flash ? BG_FLASH : BG_WARP);
        EvRow(x, lw.ev, false, nullptr);
        x.end();
        if (lw.ev.frm.valid) {
            x.begin();
            x.pad("from", 6, false, T_TEXT);
            for (int i = 6; i < COORD_COL; ++i) x.seg(" ", T_TEXT);
            Coords(x, lw.ev.frm, T_TEXT);
            x.end();
        }
    }
    x.bottom();

    // HISTORY
    x.top("HISTORY", "  (warp-capable hits, newest first)", false);
    TableHeader(x);
    int shown = 0;
    for (int i = g_histLen - 1; i >= 0 && i >= g_histLen - HISTORY_ROWS; --i) {
        const Row& r = HistAt(i);
        ++shown;
        const bool dim = static_cast<long>(g_tick) - static_cast<long>(r.lastTick) > AGE_DIM_TICKS;
        if (r.ev.kind == K_WARP) {
            x.begin(BG_WARP);
            wsprintfA(t, "%6ld  ", static_cast<long>(r.ev.tick));
            x.seg(t, T_TEXT);
            wsprintfA(t, ">>>>> WARP: player moved to %s hit <<<<<", r.hasSrc ? r.src.source : "unknown");
            x.seg(t, T_WHITE);
        } else {
            x.begin();
            EvRow(x, r.ev, dim, nullptr);
            if (r.count > 1) wsprintfA(t, " x%lu", static_cast<unsigned long>(r.count)), x.seg(t, dim ? T_DIM : T_HEADER);
        }
        x.end();
    }
    for (; shown < HISTORY_ROWS; ++shown) x.body("", T_TEXT);
    x.bottom();
    return x.n;
}

} // namespace fdcore::glitch::analysis
