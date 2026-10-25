// Tests: tests/replay.yaml unit/test_replay.cpp
// User: Replay -- plays recorder .jsonl files back in the level: the trace, the player's box and a facing arrow.
// replay feature (owner request 2026-10-25): the fdny-maps website's recording playback
// (map_explorer/recording.js), in the game. The data is read the site's way (replay_data.h),
// so both draw the same trace. Colours (owner spec, not the site's): one recording fades red ->
// blue -> green over its run; several on one map get one colour each by duration, pure green
// for the fastest through blue to pure red for the slowest. Playback runs in real time
// (fd_replay_speed), and the line is drawn up to the playback time, like the site. Drawing: gfx
// world lines (each segment a camera-facing strip) and a world box, hidden by walls unless
// fd_replay_xray.
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
constexpr float kTraceWidth = 3, kGlowWidth = 11, kEdgeWidth = 2, kArrowWidth = 4;
constexpr float kArrowLen = 100, kHeadLen = 20, kHeadWidth = 10;  // the site's ArrowHelper
constexpr uint32_t kArrowRgb = 0xFFFF40;  // yellow: reads on sky, walls and the box
// Alpha levels: gfx keeps one tile per level and 4 levels a frame (triggers use 128 and 64).
constexpr uint8_t kLineAlpha = 255, kGlowAlpha = 64, kBoxAlpha = 64, kBoxAlphaInside = 32;

struct Rec {
    d::Recording r;
    char file[MAX_PATH];
    float (*pts)[3];       // sample positions, as read
    uint32_t* rgb;         // per sample (time fade) or all one colour (several on a map)
    int* runStart;         // first sample of each joined run (the site's canJoin breaks)
    int nRuns;
};
Rec g_rec[kMaxRec];
int g_nRec = 0;

float g_time = 0;          // playback time, ms
bool g_playing = false;
DWORD g_lastTick = 0;

// Per frame: what's submitted (static, the world layer draws it in the next frame).
constexpr int kMaxLines = 2048;
gfx::WorldLine g_lines[kMaxLines];
int g_nLines = 0;
gfx::WorldBox g_boxes[kMaxRec];
float g_shapePts[kMaxRec][26][3];  // box edges (18 points) and arrow (5)
uint32_t g_shapeRgb[kMaxRec][26];
int g_visible = 0;
char g_map[16] = "";

// sqrt without the CRT (SSE, like glitch_bsp's Sqrt).
float Sqrtf(float v) { return _mm_cvtss_f32(_mm_sqrt_ss(_mm_set_ss(v))); }

bool On(const cmd::Cvar& v) { return v.value[0] != '0'; }
cmd::Cvar g_fdReplaySpeed = {"fd_replay_speed", "1", "playback speed: 0.25 / 0.5 / 1 / 2 / 4 (any value > 0)", false, true, nullptr, {0}};
cmd::Cvar g_fdReplayXray = {"fd_replay_xray", "0", "1 = the replay shows through walls; 0 = walls hide it", false, true, nullptr, {0}};

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
        for (int k = 0; k < x.r.n; ++k)
            x.rgb[k] = count > 1 ? d::RankColor(rank, count) : d::TimeColor(x.r.duration > 0 ? x.r.s[k].t / x.r.duration : 0);
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
    // One block: positions, colours, run starts.
    const int n = x.r.n;
    x.pts = static_cast<float(*)[3]>(Alloc(n * (12 + 4 + 4)));
    if (!x.pts) {
        d::Free(&x.r);
        return console::Print("fd_replay_load: out of memory");
    }
    x.rgb = reinterpret_cast<uint32_t*>(x.pts + n);
    x.runStart = reinterpret_cast<int*>(x.rgb + n);
    x.nRuns = 0;
    for (int i = 0; i < n; ++i) {
        __builtin_memcpy(x.pts[i], x.r.s[i].pos, sizeof(x.pts[i]));
        if (i == 0 || !d::CanJoin(x.r.s[i - 1], x.r.s[i])) x.runStart[x.nRuns++] = i;
    }
    lstrcpynA(x.file, argv[1], sizeof(x.file));
    ++g_nRec;
    Recolor();
    g_time = 0, g_playing = false;
    console::Print("fd_replay_load: %s: map %s, %d samples, %ld.%03ld s, %d line(s)", argv[1], x.r.stage, n,
                   static_cast<long>(x.r.duration) / 1000, static_cast<long>(x.r.duration) % 1000, x.nRuns);
    log::Info("replay: loaded %s (map %s, %d samples, %ld ms, %d runs)", path, x.r.stage, n,
              static_cast<long>(x.r.duration), x.nRuns);
}

void CmdClear(int, const char* const*) {
    for (int i = 0; i < g_nRec; ++i) FreeRec(g_rec[i]);
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

void AddLine(const float (*pts)[3], const uint32_t* rgb, int n, float width, uint8_t alpha = kLineAlpha) {
    if (n >= 2 && g_nLines < kMaxLines) g_lines[g_nLines++] = {pts, rgb, n, width, alpha};
}

// The box at frame f as gfx box + 12 edges, and the facing arrow; points into g_shapePts[slot].
void AddShape(int slot, const d::Sample& f, uint32_t rgb) {
    float lo[3], hi[3];
    for (int i = 0; i < 3; ++i) lo[i] = f.pos[i] + f.mins[i], hi[i] = f.pos[i] + f.maxs[i];
    g_boxes[slot] = {{lo[0], lo[1], lo[2]}, {hi[0], hi[1], hi[2]}, rgb, kBoxAlpha, kBoxAlphaInside};
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
    AddLine(p, c, 5, kEdgeWidth);
    AddLine(p + 5, c + 5, 5, kEdgeWidth);
    for (int k = 0; k < 4; ++k) AddLine(p + 10 + 2 * k, c + 10 + 2 * k, 2, kEdgeWidth);
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
    AddLine(a, c + 18, 2, kArrowWidth);
    AddLine(a + 2, c + 20, 3, kArrowWidth);
}

void FrameEnd() {
    const state::State& s = state::Current();
    g_nLines = 0, g_visible = 0;
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
    int nBoxes = 0;
    for (int i = 0; i < g_nRec; ++i) {
        Rec& x = g_rec[i];
        if (!SameMap(x.r.stage, s.map)) continue;
        ++g_visible;
        const int last = d::IndexAt(x.r, g_time);
        if (last < 0) {  // before its first sample: the box waits at the start
            AddShape(nBoxes++, x.r.s[0], x.rgb[0]);
            continue;
        }
        for (int k = 0; k < x.nRuns; ++k) {
            const int a = x.runStart[k], b = k + 1 < x.nRuns ? x.runStart[k + 1] - 1 : x.r.n - 1;
            if (a > last) break;
            // Neon look: a wide faint strip under the bright line (two strips per segment).
            const int n = (last < b ? last : b) - a + 1;
            AddLine(x.pts + a, x.rgb + a, n, kGlowWidth, kGlowAlpha);
            AddLine(x.pts + a, x.rgb + a, n, kTraceWidth);
        }
        AddShape(nBoxes++, d::FrameAt(x.r, g_time), x.rgb[last]);
    }
    const gfx::Layer layer = On(g_fdReplayXray) ? gfx::LAYER_OVERLAY : gfx::LAYER_WORLD;
    gfx::SubmitWorldBoxes(g_boxes, nBoxes, layer);
    gfx::SubmitWorldLines(g_lines, g_nLines, layer);
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
        wsprintfA(resp, "loaded=%d visible=%d playing=%d time_ms=%ld lines=%d segs=%d", g_nRec, g_visible, g_playing ? 1 : 0,
                  static_cast<long>(g_time), g_nLines, gfx::LastWorldLineSegments());
    });
#endif
    log::Info("replay: ready (fd_replay_load, key V%s)", gfx::WorldBoxesReady() ? "" : "; no world drawing: off");
    return true;
}

} // namespace
} // namespace fdcore::replay

FDCORE_MODULE(replay, "replay", nullptr, 0, nullptr, &fdcore::replay::Init, 85)
