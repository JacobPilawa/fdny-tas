// hud_glitch analysis (backlog item 11b): C++ port of the per-frame analysis
// of tools/collision_overlay/fdny_collision_overlay.py -- Ev / Model /
// FrameAnalyzer / TextLog / GameSource._classify + poll's record decoding.
// Parity is checked by replaying the same record stream through the
// original Python classes (core/harness/glitch_replay.py).
#pragma once

#include <stdint.h>

namespace fdcore::glitch::analysis {

// One frame of input, as GameSource._read_live + _records see it.
struct Snapshot {
    uint32_t tick;
    uint32_t playerPtr;
    int32_t state;
    bool ground, spectator;
    float pos[3], mins[3], maxs[3], disp[3];
    float slotImpact[3], slotPlane[4];
    uint32_t genesisBase;
};

void BeginSession();  // new Model + TextLog header (a fresh Python session)
void Poll(const Snapshot& s, const uint32_t* const* records, int n);  // GameSource.poll

// For the boxes (11d): the frame verdict text and level.
const char* VerdictText();

} // namespace fdcore::glitch::analysis
