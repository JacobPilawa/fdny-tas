// hud_glitch warp window (backlog item 11c): C++ port of Bsp / MapFinder /
// describe_window from tools/collision_overlay/fdny_collision_overlay.py.
// Predicts the player's NEXT frame from the loaded map's .bsp (world model,
// Genesis' box clip ported literally). Python's double arithmetic and its
// float32 rounding points are reproduced exactly (SSE2 doubles, no FMA), so
// the replay parity test can compare text byte for byte.
#pragma once

#include <stdint.h>

namespace fdcore::glitch::bsp {

enum Level : uint8_t { L_CLEAR, L_ARMED, L_WARP, L_SOLID, L_NONE };
enum SweepResult : uint8_t { S_MISS, S_HIT, S_STALE };

// A Model.window dict.
struct Window {
    Level level;
    char text[96];
    bool hasMap;
    char map[64];
    int inside;
    bool hasGap;
    double gap, win;
    bool hasSweeps;
    SweepResult sweeps[4];
    uint32_t tick;  // set by Model.set_window
};

constexpr int kSigBytes = 0x4000;  // MAP_SIG_BYTES: plane-array prefix that identifies the map

// MapFinder.__init__: index <mapsDir>\*.bsp; writes the "maps indexed" note.
void Index(const char* mapsDir, void (*note)(const char*));
bool HaveSigs();

// MapFinder.update with the throttle decided by the caller: planes = the live
// first kSigBytes of Genesis' plane array, or nullptr if unreadable.
// Returns true when a map is current afterwards.
bool Update(const uint8_t* planes, void (*note)(const char*));
void ResetCurrent();  // a new MapFinder (per session)

// Bsp.predict for the current map.
void Predict(const float pos[3], const float mins[3], const float maxs[3], const float disp[3], double slotNy,
             Window& out);
const char* CurrentName();

// describe_window(w, tick) (w may be null -> "none").
void Describe(const Window* w, bool hasTick, uint32_t tick, char* out, int cap);

} // namespace fdcore::glitch::bsp
