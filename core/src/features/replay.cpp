// Tests: tests/replay.yaml unit/test_replay.cpp
// User: Replay -- plays recorder .jsonl files back in the level: the trace, the player's box and a facing arrow.
// replay feature (owner request 2026-10-25): the fdny-maps website's recording playback
// (map_explorer/recording.js), in the game. The data is read the site's way (replay_data.h),
// so both draw the same trace. Colours (owner spec, not the site's): one recording fades red ->
// blue -> green over its run; several on one map get one colour each by duration, pure green
// for the fastest through blue to pure red for the slowest. Playback runs in real time
// (fd_replay_speed), and the line is drawn up to the playback time, like the site. Drawing: gfx
// world lines and boxes, hidden by walls; fd_replay_xray adds a dimmed copy over walls (D50).
// The line is narrower with distance, half as wide in the air, has a white tick every second,
// and is drawn through the samples it needs (replay_data's Simplify; all are used for playback).
#include <windows.h>
#include <xmmintrin.h>

#include "binds.h"
#include "cmd.h"
#include "console.h"
#include "gfx.h"
#include "hooks.h"
#include "log.h"
#include "modules.h"
#include "replay_data.h"
#include "state.h"
#include "testpipe.h"

namespace fdcore::replay {
namespace {

namespace d = data;

constexpr int kMaxRec = 8;
// Trace: 8 units wide (the box is 60), at least 3 px far away and at most 8 px close up.
constexpr float kTraceWorld = 8, kTraceMinPx = 3, kTraceMaxPx = 8, kGlowScale = 3.5f;
constexpr float kEdgeWidth = 2, kArrowWidth = 4;
constexpr float kSimplifyEps = 0.5f;  // world units: under a pixel from 160 units away
constexpr float kArrowLen = 100, kHeadLen = 20, kHeadWidth = 10;  // the site's ArrowHelper
constexpr uint32_t kArrowRgb = 0xFFFF40;  // yellow: reads on sky, walls and the box
constexpr uint32_t kTickRgb = 0xFFFFFF;
constexpr float kTickHalf = 5;        // tick cube half size, units
constexpr int kMaxTicks = 60;         // per recording: every second, or sparser for long runs
// Alpha levels: gfx keeps one tile per level and 4 levels a frame (triggers use 128 and 64).
constexpr uint8_t kLineAlpha = 255, kGlowAlpha = 64, kDimAlpha = 64, kBoxAlpha = 64, kBoxAlphaInside = 32;

struct Rec {
    d::Recording r;
    char file[MAX_PATH];
    float (*pts)[3];       // sample positions, as read
    uint32_t* rgb;         // per sample (time fade) or all one colour (several on a map)
    int* runStart;         // first sample of each joined run (the site's canJoin breaks)
    int nRuns;
    int* kept;             // the samples the drawn line goes through, in order
    int nKept;
    uint8_t* air;          // per sample: drawn thin (d::AirFlags)
    float (*dpts)[3];      // this frame's line points: kept samples up to now, then the playback point
    uint32_t* drgb;
    uint8_t* dair;
    float tick[kMaxTicks][3];
    float tickStep;        // ms between ticks
    int nTicks;
    bool ranked;           // one colour (several recordings on its map)
};
Rec g_rec[kMaxRec];
int g_nRec = 0;

float g_time = 0;          // playback time, ms
bool g_playing = false;
DWORD g_lastTick = 0;

// Per frame and layer (0 world, 1 x-ray overlay): what's submitted (static, the world layer
// draws it in the next frame).
constexpr int kMaxLines = 1024, kMaxBoxes = kMaxRec * (kMaxTicks + 1);
struct DrawList {
    gfx::WorldLine lines[kMaxLines];
    int nLines;
    gfx::WorldBox boxes[kMaxBoxes];
    int nBoxes;
};
DrawList g_draw[2];
float g_shapePts[kMaxRec][26][3];  // box edges (18 points) and arrow (5)
uint32_t g_shapeRgb[kMaxRec][26];
int g_visible = 0, g_ticks = 0;
char g_map[16] = "";

// sqrt without the CRT (SSE, like glitch_bsp's Sqrt).
float Sqrtf(float v) { return _mm_cvtss_f32(_mm_sqrt_ss(_mm_set_ss(v))); }

bool On(const cmd::Cvar& v) { return v.value[0] != '0'; }
cmd::Cvar g_fdReplaySpeed = {"fd_replay_speed", "1", "playback speed: 0.25 / 0.5 / 1 / 2 / 4 (any value > 0)", false, true, nullptr, {0}};
cmd::Cvar g_fdReplayXray = {"fd_replay_xray", "0", "1 = the replay shows through walls (the trace dimmed there); 0 = walls hide it", false, true, nullptr, {0}};

float Speed() {
    float v = 0, scale = 1;
    bool frac = false;
    for (const char* s = g_fdReplaySpeed.value; *s; ++s) {
        if (*s == '.') frac = true;
        else if (*s >= '0' && *s <= '9') v = v * 10 + (*s - '0'), scale *= frac ? 10 : 1;
    }
    v /= scale;
    return v > 0 && v <= 64 ? v : 1;
}

bool SameMap(const char* a, const char* b) { return lstrcmpiA(a, b) == 0; }

uint32_t TimeRgb(const Rec& x, float t) { return d::TimeColor(x.r.duration > 0 ? t / x.r.duration : 0); }

// Colours: a recording alone on its map fades over time; several on one map are ranked.
void Recolor() {
    for (int i = 0; i < g_nRec; ++i) {
        Rec& x = g_rec[i];
        int rank = 0, count = 0;
        for (int j = 0; j < g_nRec; ++j)
            if (SameMap(g_rec[j].r.stage, x.r.stage)) {
                ++count;
                rank += g_rec[j].r.duration < x.r.duration || (g_rec[j].r.duration == x.r.duration && j < i);
            }
        x.ranked = count > 1;
        for (int k = 0; k < x.r.n; ++k) x.rgb[k] = x.ranked ? d::RankColor(rank, count) : TimeRgb(x, x.r.s[k].t);
    }
}

void FreeRec(Rec& x) {
    d::Free(&x.r);
    if (x.pts) VirtualFree(x.pts, 0, MEM_RELEASE);
    x = Rec{};
}

void* Alloc(int bytes) { return VirtualAlloc(nullptr, bytes > 0 ? bytes : 1, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE); }

void Path(const char* name, char* out) {
    const bool abs = name[0] && (name[1] == ':' || name[0] == '\\');
    if (abs) {
        lstrcpynA(out, name, MAX_PATH);
        return;
    }
    const cmd::Cvar* dir = cmd::FindCvar("fd_record_directory");  // where the recorder writes
    const char* sub = dir && dir->value[0] ? dir->value : "Recordings";
    const bool subAbs = sub[1] == ':' || sub[0] == '\\';
    wsprintfA(out, "%s%s\\%s", subAbs ? "" : console::GameDir(), sub, name);
}

void CmdLoad(int argc, const char* const* argv) {
    if (argc < 2) return console::Print("usage: fd_replay_load <file.jsonl> (in the recordings folder, or a full path)");
    if (g_nRec >= kMaxRec) return console::Print("fd_replay_load: %d recordings loaded already (fd_replay_clear)", kMaxRec);
    char path[MAX_PATH];
    Path(argv[1], path);
    HANDLE f = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
    if (f == INVALID_HANDLE_VALUE) return console::Print("fd_replay_load: cannot open %s", path);
    const DWORD size = GetFileSize(f, nullptr);
    char* text = static_cast<char*>(Alloc(static_cast<int>(size) + 1));
    DWORD got = 0;
    const bool read = text && size < 128u * 1024 * 1024 && ReadFile(f, text, size, &got, nullptr);
    CloseHandle(f);
    Rec& x = g_rec[g_nRec];
    char err[128];
    const bool ok = read && d::Parse(text, static_cast<int>(got), &x.r, err, sizeof(err));
    if (text) VirtualFree(text, 0, MEM_RELEASE);
    if (!ok) {
        d::Free(&x.r);
        return console::Print("fd_replay_load: %s: %s", path, read ? err : "cannot read");
    }
    // One block: positions, colours, run starts, kept samples, the frame's line (up to one
    // extra point per run), Simplify's work space, then the byte arrays.
    const int n = x.r.n;
    x.pts = static_cast<float(*)[3]>(Alloc(n * (12 + 4 + 4 + 4 + 2 * (12 + 4 + 1) + 8 + 1 + 1)));
    if (!x.pts) {
        d::Free(&x.r);
        return console::Print("fd_replay_load: out of memory");
    }
    x.dpts = x.pts + n;
    x.rgb = reinterpret_cast<uint32_t*>(x.dpts + 2 * n);
    x.drgb = x.rgb + n;
    x.runStart = reinterpret_cast<int*>(x.drgb + 2 * n);
    x.kept = x.runStart + n;
    int* work = x.kept + n;
    x.air = reinterpret_cast<uint8_t*>(work + 2 * n);
    x.dair = x.air + n;
    uint8_t* keep = x.dair + 2 * n;  // also the forced points first
    x.nRuns = 0;
    for (int i = 0; i < n; ++i) {
        __builtin_memcpy(x.pts[i], x.r.s[i].pos, sizeof(x.pts[i]));
        if (i == 0 || !d::CanJoin(x.r.s[i - 1], x.r.s[i])) x.runStart[x.nRuns++] = i;
    }
    // Drawn through: Simplify's points plus where the line turns thin, and the sample at half
    // time (the time fade's blue, so the colour blends the same way).
    d::AirFlags(x.r.s, n, x.air);
    const int half = d::IndexAt(x.r, x.r.duration / 2);
    for (int i = 0; i < n; ++i) keep[i] = (i && x.air[i] != x.air[i - 1]) || i == half;
    x.nKept = 0;
    for (int k = 0; k < x.nRuns; ++k) {
        const int a = x.runStart[k], b = k + 1 < x.nRuns ? x.runStart[k + 1] - 1 : n - 1;
        d::Simplify(x.r.s, a, b, kSimplifyEps, keep, keep, work);
        for (int i = a; i <= b; ++i)
            if (keep[i]) x.kept[x.nKept++] = i;
    }
    // A tick every second (sparser for runs over kMaxTicks s), on the interpolated path.
    x.tickStep = 1000;
    while (x.r.duration / x.tickStep > kMaxTicks) x.tickStep += 1000;
    x.nTicks = 0;
    for (float t = x.tickStep; t <= x.r.duration && x.nTicks < kMaxTicks; t += x.tickStep)
        __builtin_memcpy(x.tick[x.nTicks++], d::FrameAt(x.r, t).pos, sizeof(x.tick[0]));
    lstrcpynA(x.file, argv[1], sizeof(x.file));
    ++g_nRec;
    Recolor();
    g_time = 0, g_playing = false;
    console::Print("fd_replay_load: %s: map %s, %d samples, %ld.%03ld s, %d line(s)", argv[1], x.r.stage, n,
                   static_cast<long>(x.r.duration) / 1000, static_cast<long>(x.r.duration) % 1000, x.nRuns);
    log::Info("replay: loaded %s (map %s, %d samples, %ld ms, %d runs)", path, x.r.stage, n,
              static_cast<long>(x.r.duration), x.nRuns);
    log::Info("replay: line through %d of %d samples, %d ticks", x.nKept, n, x.nTicks);
}

// Cleared recordings are freed at the next frame end: the world layer draws the line list
// submitted at the previous frame end, which points into them, until then.
Rec g_cleared[kMaxRec];
int g_nCleared = 0;

void CmdClear(int, const char* const*) {
    for (int i = 0; i < g_nRec && g_nCleared < kMaxRec; ++i) g_cleared[g_nCleared++] = g_rec[i], g_rec[i] = Rec{};
    g_nRec = 0, g_time = 0, g_playing = false;
    console::Print("fd_replay_clear: no recordings loaded");
}

float VisibleDuration() {
    float dur = 0;
    for (int i = 0; i < g_nRec; ++i)
        if (SameMap(g_rec[i].r.stage, g_map) && g_rec[i].r.duration > dur) dur = g_rec[i].r.duration;
    return dur;
}

// Key V: play / pause; from the end it starts again.
void CmdToggle(int, const char* const*) {
    if (!g_nRec) return console::Print("replay: nothing loaded (fd_replay_load <file>)");
    const float dur = VisibleDuration();
    if (dur <= 0) return console::Print("replay: no loaded recording is for this map ('%s')", g_map);
    if (!g_playing && g_time >= dur) g_time = 0;
    g_playing = !g_playing;
    g_lastTick = GetTickCount();
    console::Print("replay: %s at %ld.%01ld s", g_playing ? "playing" : "paused", static_cast<long>(g_time) / 1000,
                   static_cast<long>(g_time) % 1000 / 100);
}

void CmdList(int, const char* const*) {
    char pattern[MAX_PATH];
    Path("*.jsonl", pattern);
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(pattern, &fd);
    int n = 0;
    for (BOOL more = h != INVALID_HANDLE_VALUE; more; more = FindNextFileA(h, &fd)) console::Print("  %s", fd.cFileName), ++n;
    if (h != INVALID_HANDLE_VALUE) FindClose(h);
    console::Print("%d recording(s) in %s; loaded: %d", n, pattern, g_nRec);
}

void AddLine(DrawList& L, const gfx::WorldLine& line) {
    if (line.n >= 2 && L.nLines < kMaxLines) L.lines[L.nLines++] = line;
}
void AddLine(DrawList& L, const float (*pts)[3], const uint32_t* rgb, int n, float width) {
    AddLine(L, {pts, rgb, nullptr, n, width, 0, 0, kLineAlpha, 0, 0});
}
void AddBox(DrawList& L, const gfx::WorldBox& box) {
    if (L.nBoxes < kMaxBoxes) L.boxes[L.nBoxes++] = box;
}

// The trace: glowing in the world layer, and dimmed over walls with x-ray.
void AddTrace(const float (*pts)[3], const uint32_t* rgb, const uint8_t* air, int n, bool xray) {
    AddLine(g_draw[0], {pts, rgb, air, n, kTraceMinPx, kTraceWorld, kTraceMaxPx, kLineAlpha, kGlowAlpha, kGlowScale});
    if (xray) AddLine(g_draw[1], {pts, rgb, air, n, kTraceMinPx, kTraceWorld, kTraceMaxPx, kDimAlpha, 0, 0});
}

void AddTicks(const Rec& x, bool xray) {
    for (int k = 0; k < x.nTicks && (k + 1) * x.tickStep <= g_time; ++k) {
        const float* p = x.tick[k];
        gfx::WorldBox b = {{p[0] - kTickHalf, p[1] - kTickHalf, p[2] - kTickHalf},
                           {p[0] + kTickHalf, p[1] + kTickHalf, p[2] + kTickHalf}, kTickRgb, kLineAlpha, kLineAlpha};
        AddBox(g_draw[0], b);
        b.alpha = b.alphaInside = kDimAlpha;
        if (xray) AddBox(g_draw[1], b);
        ++g_ticks;
    }
}

// The box at frame f as gfx box + 12 edges, and the facing arrow; points into g_shapePts[slot].
void AddShape(DrawList& L, int slot, const d::Sample& f, uint32_t rgb) {
    float lo[3], hi[3];
    for (int i = 0; i < 3; ++i) lo[i] = f.pos[i] + f.mins[i], hi[i] = f.pos[i] + f.maxs[i];
    AddBox(L, {{lo[0], lo[1], lo[2]}, {hi[0], hi[1], hi[2]}, rgb, kBoxAlpha, kBoxAlphaInside});
    float (*p)[3] = g_shapePts[slot];
    uint32_t* c = g_shapeRgb[slot];
    auto corner = [&](int k, int bits) {
        p[k][0] = bits & 1 ? hi[0] : lo[0], p[k][1] = bits & 2 ? hi[1] : lo[1], p[k][2] = bits & 4 ? hi[2] : lo[2];
        c[k] = rgb;
    };
    static const int kLoop[5] = {0, 1, 5, 4, 0};  // around x/z, bottom (y lo) then top (y hi)
    for (int k = 0; k < 5; ++k) corner(k, kLoop[k]), corner(5 + k, kLoop[k] | 2);
    static const int kPillar[4] = {0, 1, 5, 4};
    for (int k = 0; k < 4; ++k) corner(10 + 2 * k, kPillar[k]), corner(11 + 2 * k, kPillar[k] | 2);
    AddLine(L, p, c, 5, kEdgeWidth);
    AddLine(L, p + 5, c + 5, 5, kEdgeWidth);
    for (int k = 0; k < 4; ++k) AddLine(L, p + 10 + 2 * k, c + 10 + 2 * k, 2, kEdgeWidth);
    // Arrow from the box centre: shaft, then the head as a V around the tip.
    float dir[3];
    d::ArrowDir(f, dir);
    float o[3], tip[3];
    for (int i = 0; i < 3; ++i) o[i] = (lo[i] + hi[i]) * 0.5f, tip[i] = o[i] + dir[i] * kArrowLen;
    float side[3] = {-dir[2], 0, dir[0]};  // horizontal, across the arrow
    const float sl = Sqrtf(side[0] * side[0] + side[2] * side[2]);
    if (sl < 1e-4f) side[0] = 1, side[2] = 0;
    else side[0] /= sl, side[2] /= sl;
    float (*a)[3] = p + 18;
    for (int i = 0; i < 3; ++i) {
        a[0][i] = o[i], a[1][i] = tip[i];
        const float back = tip[i] - dir[i] * kHeadLen;
        a[2][i] = back + side[i] * kHeadWidth * 0.5f, a[3][i] = tip[i], a[4][i] = back - side[i] * kHeadWidth * 0.5f;
    }
    for (int k = 18; k < 23; ++k) c[k] = kArrowRgb;
    AddLine(L, a, c + 18, 2, kArrowWidth);
    AddLine(L, a + 2, c + 20, 3, kArrowWidth);
}

void FrameEnd() {
    const state::State& s = state::Current();
    for (; g_nCleared; --g_nCleared) FreeRec(g_cleared[g_nCleared - 1]);
    g_draw[0].nLines = g_draw[0].nBoxes = g_draw[1].nLines = g_draw[1].nBoxes = 0, g_visible = 0, g_ticks = 0;
    if (s.gameState != 1 || !g_nRec) return;
    if (!SameMap(g_map, s.map)) lstrcpynA(g_map, s.map, sizeof(g_map)), g_time = 0, g_playing = false;
    const float dur = VisibleDuration();
    if (g_playing) {
        const DWORD now = GetTickCount();
        DWORD dt = now - g_lastTick;
        if (dt > 250) dt = 250;  // the site's cap per frame
        g_lastTick = now;
        g_time += static_cast<float>(dt) * Speed();
        if (g_time >= dur) g_time = dur, g_playing = false;
    }
    const bool xray = On(g_fdReplayXray);
    DrawList& shapes = g_draw[xray ? 1 : 0];  // the box and arrow: full strength over walls with x-ray
    int slot = 0;
    for (int i = 0; i < g_nRec; ++i) {
        Rec& x = g_rec[i];
        if (!SameMap(x.r.stage, s.map)) continue;
        ++g_visible;
        const int last = d::IndexAt(x.r, g_time);
        if (last < 0) {  // before its first sample: the box waits at the start
            AddShape(shapes, slot++, x.r.s[0], x.rgb[0]);
            continue;
        }
        const d::Sample f = d::FrameAt(x.r, g_time);
        // Each run: its kept samples up to now, then the playback point (the box's position).
        int k = 0, o = 0;
        for (int r = 0; r < x.nRuns; ++r) {
            const int a = x.runStart[r], b = r + 1 < x.nRuns ? x.runStart[r + 1] - 1 : x.r.n - 1;
            if (a > last) break;
            const int from = o;
            for (; k < x.nKept && x.kept[k] <= b; ++k) {
                const int j = x.kept[k];
                if (j > last) continue;
                __builtin_memcpy(x.dpts[o], x.pts[j], sizeof(x.dpts[o]));
                x.drgb[o] = x.rgb[j], x.dair[o++] = x.air[j];
            }
            if (last < b) {
                __builtin_memcpy(x.dpts[o], f.pos, sizeof(x.dpts[o]));
                x.drgb[o] = x.ranked ? x.rgb[last] : TimeRgb(x, g_time), x.dair[o++] = x.air[last];
            }
            AddTrace(x.dpts + from, x.drgb + from, x.dair + from, o - from, xray);
        }
        AddTicks(x, xray);
        AddShape(shapes, slot++, f, x.rgb[last]);
    }
    for (int l = 0; l < 2; ++l) {
        const gfx::Layer layer = l ? gfx::LAYER_OVERLAY : gfx::LAYER_WORLD;
        gfx::SubmitWorldBoxes(g_draw[l].boxes, g_draw[l].nBoxes, layer);
        gfx::SubmitWorldLines(g_draw[l].lines, g_draw[l].nLines, layer);
    }
}

bool Init() {
    cmd::RegisterCvar(&g_fdReplaySpeed);
    cmd::RegisterCvar(&g_fdReplayXray);
    cmd::RegisterCommand("fd_replay_load", "fd_replay_load <file.jsonl>: load a recording (the recordings folder, or a full path); "
                         "several on one map are drawn together", CmdLoad);
    cmd::RegisterCommand("fd_replay_clear", "unload every recording", CmdClear);
    cmd::RegisterCommand("fd_replay_toggle", "play / pause the loaded recordings for this map; from the end, start again (key V)", CmdToggle);
    cmd::RegisterCommand("fd_replay_list", "list the .jsonl recordings in the recordings folder", CmdList);
    cmd::SetPracticeOnly("fd_replay_speed");
    cmd::SetPracticeOnly("fd_replay_xray");
    cmd::SetPracticeOnly("fd_replay_load");
    cmd::SetPracticeOnly("fd_replay_toggle");
    binds::BindDefault("v", "fd_replay_toggle");  // D37
    hooks::OnFrameEnd(FrameEnd);
#ifdef FDCORE_DEBUG
    testpipe::Register("replay", [](const char*, char* resp, int) {
        int us, meanUs;
        gfx::WorldLineCost(&us, &meanUs);
        wsprintfA(resp, "loaded=%d visible=%d playing=%d time_ms=%ld lines=%d segs=%d ticks=%d us=%d mean_us=%d", g_nRec,
                  g_visible, g_playing ? 1 : 0, static_cast<long>(g_time), g_draw[0].nLines + g_draw[1].nLines,
                  gfx::LastWorldLineSegments(), g_ticks, us, meanUs);
    });
#endif
    log::Info("replay: ready (fd_replay_load, key V%s)", gfx::WorldBoxesReady() ? "" : "; no world drawing: off");
    return true;
}

} // namespace
} // namespace fdcore::replay

FDCORE_MODULE(replay, "replay", nullptr, 0, nullptr, &fdcore::replay::Init, 85)
