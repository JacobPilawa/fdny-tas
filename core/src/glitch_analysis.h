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
constexpr int kBoxLines = 40, kBoxLineLen = 160;
// Per character: the original Text widget's tag (colour), plus a background bit.
enum BoxTag : uint8_t {
    T_FRAME, T_TITLE, T_HEADER, T_TEXT, T_DIM, T_ALERT, T_OK, T_WARN, T_WHITE,
    T_SRC_VICTIM, T_SRC_FIRE, T_SRC_HOSE, T_SRC_HOSEROPE, T_SRC_AXE, T_SRC_PLAYER, T_SRC_ENGINE, T_SRC_OTHER,
    T_PL_STANDING, T_PL_CROUCHED, T_PL_CLIMBING, T_PL_OUTCOLD, T_PL_SCRIPTED, T_PL_BUGGED, T_PL_GROUND, T_PL_INAIR,
    T_COUNT
};
constexpr uint8_t BG_WARP = 0x40, BG_FLASH = 0x80, TAG_MASK = 0x3F;
int BuildBox(char (*lines)[kBoxLineLen], uint8_t (*attrs)[kBoxLineLen] = nullptr);
// F12: Model.clear() + the "history cleared (F12)" note.
void ClearHistory();
uint32_t Tick();  // Model.tick

// Python "%.<d>f" (d <= 3) of a double, exactly.
int PyFixed(char* out, double v, int d);

} // namespace fdcore::glitch::analysis
