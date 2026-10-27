// hud_glitch analysis (backlog item 11b): the per-frame analysis of the collision
// record stream (originally a C++ port of the Python collision overlay's Ev / Model /
// FrameAnalyzer / TextLog / GameSource._classify + poll's record decoding).
#pragma once

#include <stdint.h>

#include "glitch_palette.h"
#include "textdraw.h"

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
    // 11c: MapFinder.update ran this poll (its 1 s throttle is the caller's);
    // planes = the live first bsp::kSigBytes of the plane array, or null.
    bool mapCheck;
    const uint8_t* planes;
};

// New Model + TextLog header + MapFinder (a fresh Python session).
void BeginSession(const char* mapsDir);
void Poll(const Snapshot& s, const uint32_t* const* records, int n);  // GameSource.poll
// What the old tool logged when it closed: hooks removed + prediction stats.
void EndSession();

// For the boxes (11d): the frame verdict text and level.
const char* VerdictText();

// Overlay.build() as text (11d): the WARP / LAST WARP / HISTORY boxes, one
// line per row (tags/colours dropped). Returns the line count.
constexpr int kBoxLines = textdraw::kMaxLines, kBoxLineLen = textdraw::kLineLen;
// Per character: the original Text widget's tag (colour) + background bit (glitch_palette.h).
using namespace fdcore::glitch::palette;  // T_* tags, BG_WARP / BG_FLASH
int BuildBox(char (*lines)[kBoxLineLen], uint8_t (*attrs)[kBoxLineLen] = nullptr);
// F12: Model.clear() + the "history cleared (F12)" note.
void ClearHistory();
uint32_t Tick();  // Model.tick

// Python "%.<d>f" (d <= 3) of a double, exactly.
int PyFixed(char* out, double v, int d);

} // namespace fdcore::glitch::analysis
