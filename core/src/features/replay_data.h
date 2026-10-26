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
    int ground;       // ground_contact_raw (1 when absent)
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

// Which samples a drawn line needs (D50): Douglas-Peucker over samples [a, b], keeping the
// path within eps world units. Sets keep[i] for a, b, every force[i] (may be null) and the
// points needed; returns how many in [a, b]. force may be keep itself (marks kept as given).
// work: 2 * (b - a + 1) ints. The samples
// themselves are untouched (playback and the box use them all).
int Simplify(const Sample* s, int a, int b, float eps, const uint8_t* force, uint8_t* keep, int* work);

// In the air for the drawing: off the ground for at least kAirSamples samples in a row (a
// jump or a fall; single samples off the ground on stairs and slopes don't count).
constexpr int kAirSamples = 3;
void AirFlags(const Sample* s, int n, uint8_t* air);

// --- The recordings folder (D51) -----------------------------------------------------
// The recorder names files <map>_<UTC start>_pid<n>_run<n>.jsonl; the index reads that
// and the end of each file, never the whole file.
struct FileInfo {
    char name[112];     // file name, no folder
    char map[40];       // lower case, from the name ("" if not the recorder's pattern)
    char stamp[25];     // UTC start "YYYY-MM-DDTHH-MM-SS.mmmZ": sorts by time
    unsigned pid, run;
    float duration;     // the last sample's t_ms (ReadTail)
    bool complete;      // ends with its end record: not being written, not cut off
    bool finished;      // complete, and the same game's next run is on another map (MarkFinished)
};
// Fills name, and map / stamp / pid / run if the name is the recorder's pattern (true).
bool ParseName(const char* file, FileInfo* out);
// duration and complete from the file's last bytes.
void ReadTail(const char* tail, int len, FileInfo* f);
// A run is finished when the same game's (pid's) next run is on another map: the level
// was left for the next one (or for another level through the menu, which counts too).
void MarkFinished(FileInfo* f, int n);
int Best(const FileInfo* f, int n, const char* map);    // fastest finished run on map, or -1
int Latest(const FileInfo* f, int n, const char* map);  // newest complete run on map, or -1
int Match(const FileInfo* f, int n, const char* text);  // first by name containing text (any case), or -1

// Colours (owner spec 2026-10-25): one run fades red -> blue -> green over its duration
// (f = t / duration); several runs get one colour each by duration, pure green for the
// fastest through blue to pure red for the slowest.
uint32_t TimeColor(float f);
uint32_t RankColor(int rank, int count);  // rank 0 = fastest

} // namespace fdcore::replay::data
