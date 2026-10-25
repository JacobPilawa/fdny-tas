// Debug and unit-test builds only (glitch_replay.h).
#if defined(FDCORE_DEBUG) || defined(FDCORE_UNIT)

#include "glitch_replay.h"

#include <windows.h>

namespace fdcore::glitch::replay {

void WriteBox(BoxSink sink, const char (*lines)[analysis::kBoxLineLen], int n) {
    if (!sink) return;
    char h[32];
    wsprintfA(h, "== %lu\n", static_cast<unsigned long>(analysis::Tick()));
    sink(h);
    for (int i = 0; i < n; ++i) sink(lines[i]), sink("\n");
}

Result Run(const uint32_t* data, uint32_t blocks, const char* mapsDir, BoxSink sink) {
    static const uint32_t* recs[16384 * 8];
    static char build[analysis::kBoxLines][analysis::kBoxLineLen];
    analysis::Snapshot snap{};
    bool haveSnap = false;
    int n = 0;
    Result r = {0, 0};
    bool pendingBox = false;  // build() runs after the frame's hotkeys (an F12 marker may follow)
    auto box = [&]() {
        if (pendingBox && sink) WriteBox(sink, build, analysis::BuildBox(build));
        pendingBox = false;
    };
    auto flush = [&]() {
        if (haveSnap) analysis::Poll(snap, recs, n), ++r.frames, pendingBox = true;
        n = 0;
    };
    for (uint32_t i = 0; i < blocks; ++i) {
        const uint32_t* b = data + i * (kRecSize / 4);
        if (b[0] == 0xF1) {
            flush(), box();
            haveSnap = false;
            if (sink) sink("## session\n");
            analysis::BeginSession(mapsDir);
            ++r.sessions;
        } else if (b[0] == 0xF5) {  // F12 after that frame's poll
            flush();
            haveSnap = false;
            analysis::ClearHistory();
        } else if (b[0] == 0xF4) {
            flush(), box();
            haveSnap = false;
            analysis::EndSession();
        } else if (b[0] == 0xF2) {  // belongs to the snapshot just read
            const uint32_t nb = b[2] / kRecSize;
            if (haveSnap) {
                snap.mapCheck = true;
                snap.planes = b[1] && i + nb < blocks ? reinterpret_cast<const uint8_t*>(b + kRecSize / 4) : nullptr;
            }
            i += nb;
        } else if (b[0] == 0xF0) {
            flush(), box();
            snap = analysis::Snapshot{};
            snap.tick = b[1], snap.playerPtr = b[2], snap.state = static_cast<int32_t>(b[3]);
            snap.ground = (b[4] & 0xFF) != 0, snap.spectator = (b[5] & 0xFF) != 0;
            __builtin_memcpy(snap.pos, &b[6], 12), __builtin_memcpy(snap.mins, &b[9], 12);
            __builtin_memcpy(snap.maxs, &b[12], 12), __builtin_memcpy(snap.disp, &b[15], 12);
            __builtin_memcpy(snap.slotImpact, &b[18], 12), __builtin_memcpy(snap.slotPlane, &b[21], 16);
            snap.genesisBase = b[25];
            haveSnap = true;
        } else if (haveSnap && n < static_cast<int>(sizeof(recs) / sizeof(recs[0]))) {
            recs[n++] = b;
        }
    }
    flush(), box();
    return r;
}

} // namespace fdcore::glitch::replay

#endif
