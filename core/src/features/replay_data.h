// Replay data: the recorder's fdny-trajectory-v1 JSONL, read the way the fdny-maps website
// reads it (map_explorer/recording-data.js), so the game and the site draw the same trace:
// player samples in file order, positions untouched (Genesis units, Y up), the same gap
// rule, the same box defaults and the same interpolation. No game calls: unit-tested in
// core/unit/test_replay.cpp.
#pragma once

#include <stdint.h>

namespace fdcore::replay::data {

struct Sample {
    float t;          // t_ms
    float pos[3];
    float fwd[3];     // body_forward ([0,0,1] when absent)
    float pitch;      // pitch_deg (positive looks down)
    float mins[3], maxs[3];  // collision box relative to pos (measured or the site's default)
    int state;        // player_state_raw (4 crouched, 5 climbing)
};

struct Recording {
    char stage[32];   // lower case, no path, no .bsp (the site's stage key)
    Sample* s;
    int n;
    float duration;   // the last sample's t_ms
};

// Parses a whole file into `out` (samples allocated with VirtualAlloc; Free releases them).
// False with a reason in err for anything the site would reject: no start record first, a
// wrong schema, samples out of sequence or time order, a bad position.
bool Parse(const char* text, int len, Recording* out, char* err, int errCap);
void Free(Recording* r);

// The site's canJoin: consecutive samples are one line unless more than 500 ms or 2000 u apart.
bool CanJoin(const Sample& a, const Sample& b);

// The site's frameAt: the sample at time t, position and pitch linear between two joinable
// samples, facing turned the short way; bounds and state held from the earlier sample.
Sample FrameAt(const Recording& r, float t);
// Index of the last sample with s.t <= t (-1 before the first).
int IndexAt(const Recording& r, float t);

// Facing arrow direction: body_forward's horizontal part tilted by pitch (the site's formula).
void ArrowDir(const Sample& s, float out[3]);

// Colours (owner spec 2026-10-25): one run fades red -> blue -> green over its duration
// (f = t / duration); several runs get one colour each by duration, pure green for the
// fastest through blue to pure red for the slowest.
uint32_t TimeColor(float f);
uint32_t RankColor(int rank, int count);  // rank 0 = fastest

} // namespace fdcore::replay::data
