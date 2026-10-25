// The .glitchrec replay driver (items 11b-11d): the record + snapshot blocks the live
// path writes, fed through the analysis offline. Shared by the Debug pipe
// `glitch_replay` (in the game) and the unit-test host (core/unit, no game), so the
// parity check runs the same code in both. Not in the Release build.
#pragma once

#include <stdint.h>

#include "glitch_analysis.h"

namespace fdcore::glitch::replay {

constexpr uint32_t kRecSize = 128;  // bytes per block (the cave's record size)

// Where the per-frame box text goes (FDNYCore.glitchbox); null = not written.
using BoxSink = void (*)(const char* text);

// One build() per frame: "== <tick>" then the lines; "## session" per hook install.
void WriteBox(BoxSink sink, const char (*lines)[analysis::kBoxLineLen], int n);

struct Result {
    int sessions, frames;
};
// `blocks` blocks of kRecSize bytes. The collision log goes to log::Raw ("glitch| ").
Result Run(const uint32_t* data, uint32_t blocks, const char* mapsDir, BoxSink sink);

} // namespace fdcore::glitch::replay
