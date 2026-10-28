# Recordings browser and viewer

The briefing's Recordings button lists six JSONL files per page for the selected
stage, newest first. Recent and Favorites tabs retain a visible selection.
Details offer Show ghost, Compare, Rename and Favorite. Names and favorites are
stored in `<recording>.jsonl.fdny-meta`; writes use a flushed temporary file and
an atomic replacement. Invalid metadata is preserved. JSONL data is never edited.
An incomplete file remains labeled and can be played or compared through its
last usable sample; a partial final line is ignored by the existing reader.

The viewer requires the practice profile, pauses simulation and suppresses new
recordings, checkpoints, trigger overlays and ordinary free-camera controls.
It starts in third person. F releases/re-hooks the camera, Tab switches targets,
right-drag orbits/looks, wheel/PageUp/PageDown zoom, Space plays/pauses,
Left/Right seek five seconds, Home restarts, X toggles through-walls drawing and
Escape returns to the list. Held/repeated keys do not create duplicate actions;
short window-message taps are retained until frame polling consumes them.

## Native integration

`core/menu.cpp` owns the briefing draw hook, native screen/row construction and
window input routing. The page reuses the Records background, profile rows and
matching native font atlas/metrics; detached labels cannot steal menu focus.
The selected tab uses the row font animation's highlight frame. Menu operations
use verified address-table functions, with structure fields and sizes also in
`core/addresses.json`. Their signatures are the first 16 bytes of the retail
functions. The layout was established through native menu inspection and runtime
use of those widgets; the new entries retain that layout without raw addresses.

`core/spectator.cpp` accepts target poses and emits actions. It knows no file
format or feature commands. The camera producer applies the prepared follow
transform before the first world render. Scene transitions wait until after
`geEngine_EndFrame` and clear queued geometry before releasing the old scene.

## Validation

- Architecture rules and generated player reference: pass.
- Debug and Release builds with Zig 0.16.0: pass. Core-only and replay-excluded
  Release builds also pass.
- C++ unit tests: 202 checks passed, covering incomplete tails, metadata validation
  and round trips, paging bounds, orbit geometry, zoom limits and modal key edges.
- Python unit tests: 27 passed.
- `tests/game/spectator_camera.yaml` covers paused playback, camera/target actions,
  recording suppression and exit using real keypresses and read-only state queries.
