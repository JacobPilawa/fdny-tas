// replay_data against the fdny-maps website's rules, on the sample recording in reference/
// (expected numbers computed from the file with the site's canJoin, 2026-10-25).
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../src/features/replay_data.h"
#include "unit.h"

namespace d = fdcore::replay::data;

namespace {
char* ReadFile(const char* path, int* len) {
    FILE* f = fopen(path, "rb");
    if (!f) return nullptr;
    fseek(f, 0, SEEK_END);
    *len = static_cast<int>(ftell(f));
    fseek(f, 0, SEEK_SET);
    char* b = static_cast<char*>(malloc(*len + 1));
    *len = static_cast<int>(fread(b, 1, *len, f));
    fclose(f);
    return b;
}
bool Near(float a, float b, float eps = 1e-3f) { return fabsf(a - b) <= eps; }
bool Parse(const char* text, d::Recording* r, char* err) { return d::Parse(text, static_cast<int>(strlen(text)), r, err, 128); }
} // namespace

void TestReplayParsesSample() {
    int len = 0;
    char* text = ReadFile("reference/original_tools/mbase_2026-10-23T21-11-10.274Z_pid204_run0001.jsonl", &len);
    CHECK(text != nullptr);
    if (!text) return;
    d::Recording r;
    char err[128];
    CHECK(d::Parse(text, len, &r, err, sizeof(err)));
    CHECK(strcmp(r.stage, "mbase") == 0);
    CHECK(r.n == 1391);
    CHECK(Near(r.duration, 33847));
    CHECK(Near(r.s[0].t, 16));
    // Values straight from the file, unchanged (sample 700).
    CHECK(Near(r.s[700].t, 16552));
    CHECK(Near(r.s[700].pos[0], 1551.00696f) && Near(r.s[700].pos[1], -181.826859f) && Near(r.s[700].pos[2], -3397.72583f));
    CHECK(Near(r.s[700].pitch, -12.7999992f));
    CHECK(Near(r.s[700].mins[0], -30) && Near(r.s[700].maxs[1], 85));
    // The site's gap rule breaks this run in exactly two places: before samples 3 and 1390.
    int breaks = 0, first = -1, second = -1;
    for (int i = 1; i < r.n; ++i)
        if (!d::CanJoin(r.s[i - 1], r.s[i])) (breaks++ ? second : first) = i;
    CHECK(breaks == 2 && first == 3 && second == 1390);
    // frameAt halfway between 700 and 701: position and pitch linear.
    const d::Sample m = d::FrameAt(r, (16552 + 16569) / 2.0f);
    CHECK(Near(m.pos[0], (1551.00696f + 1556.71606f) / 2, 0.01f));
    CHECK(d::IndexAt(r, 15) == -1 && d::IndexAt(r, 16) == 0 && d::IndexAt(r, 1e9f) == r.n - 1);
    d::Free(&r);
    free(text);
}

void TestReplayRejectsBadFiles() {
    d::Recording r;
    char err[128];
    CHECK(!Parse("{\"type\":\"sample\",\"n\":0,\"t_ms\":1,\"position\":[0,0,0]}\n", &r, err));  // no start first
    CHECK(!Parse("{\"type\":\"start\",\"schema\":\"other\",\"stage\":\"bank\"}\n", &r, err));
    const char* gap = "{\"type\":\"start\",\"schema\":\"fdny-trajectory-v1\",\"stage\":\"bank\"}\n"
                      "{\"type\":\"sample\",\"n\":0,\"t_ms\":1,\"position\":[0,0,0]}\n"
                      "{\"type\":\"sample\",\"n\":2,\"t_ms\":2,\"position\":[0,0,0]}\n";
    CHECK(!Parse(gap, &r, err) && strstr(err, "sequence"));
    const char* ok = "{\"type\":\"start\",\"schema\":\"fdny-trajectory-v1\",\"stage\":\"Levels\\\\Bank.BSP\"}\n"
                     "{\"type\":\"sample\",\"n\":0,\"t_ms\":1,\"position\":[1,2,3],\"player_state_raw\":4}\n"
                     "{\"type\":\"sample\",\"n\":1,\"t_ms\":2,\"position\":[1,2,3]}";  // unfinished last line: dropped
    CHECK(Parse(ok, &r, err));
    CHECK(strcmp(r.stage, "bank") == 0 && r.n == 1);
    CHECK(Near(r.s[0].maxs[1], 25) && Near(r.s[0].fwd[2], 1));  // crouched default box, default facing
    d::Free(&r);
}

namespace {
float SegDist(const float* p, const float* a, const float* b) {
    float ab[3], ap[3], l2 = 0, t = 0;
    for (int i = 0; i < 3; ++i) ab[i] = b[i] - a[i], ap[i] = p[i] - a[i], l2 += ab[i] * ab[i], t += ap[i] * ab[i];
    t = l2 > 0 ? t / l2 : 0;
    t = t < 0 ? 0 : t > 1 ? 1 : t;
    float d2 = 0;
    for (int i = 0; i < 3; ++i) d2 += (ap[i] - ab[i] * t) * (ap[i] - ab[i] * t);
    return sqrtf(d2);
}
} // namespace

// The drawn line through the reference recording: under a third of the samples, every
// dropped sample within eps of the line kept (Python's Douglas-Peucker: 476 of 1391 at 0.5).
void TestReplaySimplify() {
    int len = 0;
    char* text = ReadFile("reference/original_tools/mbase_2026-10-23T21-11-10.274Z_pid204_run0001.jsonl", &len);
    CHECK(text != nullptr);
    if (!text) return;
    d::Recording r;
    char err[128];
    CHECK(d::Parse(text, len, &r, err, sizeof(err)));
    unsigned char* keep = static_cast<unsigned char*>(calloc(r.n, 1));
    int* work = static_cast<int*>(malloc(2 * r.n * sizeof(int)));
    const int kept = d::Simplify(r.s, 0, r.n - 1, 0.5f, nullptr, keep, work);
    CHECK(kept > 400 && kept < 500);
    CHECK(keep[0] && keep[r.n - 1]);
    bool within = true;
    for (int i = 0, prev = 0; i < r.n; ++i) {
        if (!keep[i]) continue;
        for (int k = prev + 1; k < i; ++k) within = within && SegDist(r.s[k].pos, r.s[prev].pos, r.s[i].pos) <= 0.5f + 1e-3f;
        prev = i;
    }
    CHECK(within);
    // Forced points stay: a straight line keeps only its ends and the forced one.
    d::Sample line[5] = {};
    for (int i = 0; i < 5; ++i) line[i].pos[0] = static_cast<float>(i * 10);
    unsigned char force[5] = {0, 0, 1, 0, 0}, k5[5];
    int w5[10];
    CHECK(d::Simplify(line, 0, 4, 0.5f, force, k5, w5) == 3 && k5[0] && k5[2] && k5[4] && !k5[1] && !k5[3]);
    free(keep), free(work);
    d::Free(&r);
    free(text);
}

// In the air: off the ground for 3 samples or more; one or two off (stairs) don't count.
void TestReplayAirFlags() {
    const int g[10] = {1, 0, 1, 0, 0, 1, 0, 0, 0, 1};
    d::Sample s[10] = {};
    for (int i = 0; i < 10; ++i) s[i].ground = g[i];
    unsigned char air[10];
    d::AirFlags(s, 10, air);
    const unsigned char want[10] = {0, 0, 0, 0, 0, 0, 1, 1, 1, 0};
    CHECK(memcmp(air, want, sizeof(want)) == 0);
}

void TestReplayColors() {
    CHECK(d::TimeColor(0) == 0xFF0000);
    CHECK(d::TimeColor(0.5f) == 0x0000FF);
    CHECK(d::TimeColor(1) == 0x00FF00);
    CHECK(d::RankColor(0, 3) == 0x00FF00 && d::RankColor(1, 3) == 0x0000FF && d::RankColor(2, 3) == 0xFF0000);
    d::Sample s = {};
    s.fwd[2] = 1, s.pitch = 90;  // straight down
    float dir[3];
    d::ArrowDir(s, dir);
    CHECK(Near(dir[1], -1, 1e-3f) && Near(dir[2], 0, 1e-3f));
}
