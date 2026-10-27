# Translucent overlay panel: research spike (2026-10-21)

Owner request: "Try C (true alpha) as a research spike with one-off tests. Don't modify
working code; write new probes and determine feasibility."

The work is purely additive. `core/src` is untouched, and `tests/probes/` is not in the suite.

- Probe: `core/probes/alphaprobe.cpp` builds `zz_alphaprobe.asi` (`core/probes/build_probes.ps1`).
  - It is a standalone `.asi`. Test `pre_files` place it in the game folder only for the probe runs.
  - It hooks `geEngine_EndFrame` over the core's hook, so it runs before the core's frame end. It also hooks `geEngine_RenderWorld` to learn the world and camera.
  - Commands come in through the game console as `fd_zzp_<mode>_<alpha>_<z>_<count>_<format>_<size>`. The probe tails `FDNYCore.log` for the core's "unknown command" line.
- Tests: `core/tests/probes/*.yaml`. Logs from each run: `*.probe.log`.

**Verdict: FEASIBLE.**

- A full translucent panel with live text works:
  - built from 2×2 textured 256×256 4444 tiles attached with `geWorld_AddBitmap`;
  - drawn with `geEngine_RenderPoly` straight to the driver, after `geEngine_FlushPipeline`.
- It survives a level change by re-adding the tiles to the new world.
- Text is crisp with a half-pixel vertex offset.

## ABIs (capstone disassembly of Genesis.dll)

| Export | ABI | Notes |
|---|---|---|
| `@geEngine_RenderPoly@20` | fastcall: ECX=engine (unused), EDX=`GE_TLVertex*`; stack: NumPoints, `geBitmap*` or NULL, Flags; `RET 0xC` | When `Flags & 1`, the poly goes straight to the driver: vtbl +0x84 if textured, +0x7C (Gouraud) if not. Otherwise it is queued into the sorted lists. The textured internal (0x10023400) ORs in 0xC (no z-mask, no z-write). |
| `GE_TLVertex` | {x, y, z, u, v, r, g, b, a} floats, 36 bytes | Genesis's own 2D alpha path uses z = 1.0, colours 0..255. |
| `@geEngine_DrawAlphaBitmap@32` | fastcall: engine, bitmap; stack: UV[4], clip camera, pixel rect, percent rect, float alpha, RGBA[4]; `RET 0x18` | Builds TL verts, ORs flags 0x70 and queues them. Needs a texture handle, like RenderPoly. |
| `geEngine_AddBitmap` / `geWorld_AddBitmap` (cdecl) | | Set the bitmap's driver flags through internal 0x100171E0 (fastcall: bmp, flags → `[bmp+0xA4]`). The engine list sets **1 (2D decal)**; the world list sets **0x40002 (3D texture)**. |
| `geEngine_RenderWorld` (cdecl) | engine, world, camera, … | Used to capture the world. |
| bitmap driver handle | `[bmp+0xAC]` | The handle exists 2 frames after the add. Fields: +4 w, +8 h, +0x18 driver format (7 = 4444), +0x1C flags. |

## Results (VM, dgVoodoo windowed 640×480)

The VM ran at about 37 fps all evening. The no-probe control measured the same (`probe_control_noprobe`: 36–37), so only compare frame rates within a single run.

| Variant | Result |
|---|---|
| Untextured quad, vertex alpha 255 / 128 / 64 | **Opaque.** One flat colour in all three; D3DDrv's Gouraud path ignores alpha (`01_…png`). |
| … z = 0 / 100, queued (flags 0), no flush | z = 100 and queued are partly covered by 3D. **The flush is required** before an immediate draw. |
| Untextured quad + colour-keyed 565 text decal | Works: the decal draws on top at the EndFrame flush (`02_…png`). No alpha, though. |
| Textured, engine-attached (`geEngine_AddBitmap`) | **Crash** in D3DDrv+5834: a 2D decal handle can't texture a poly. |
| Textured, engine-attached + forced flags 0x40003 | **Crash** in D3DDrv+7C52 at the flush. |
| Textured 512×512, world-attached (4444 / ARGB) and `DrawAlphaBitmap` | **Crash** in D3DDrv+7C52: no 3D texture at 512. **The driver's limit is 256.** |
| **Textured 256 / 128, world-attached** | **Works: true per-pixel alpha.** Background correlation with the scene is 0.56 (untextured: 0.00) (`03_…png`). |
| 32-bit ARGB upload | Accepted, but the driver stores 4444 (handle +0x18 = 7). **Alpha textures are 4444 only:** 4 bits per colour channel, 16 alpha levels. |
| **Mode 9: 2×2 tiles, 400×346 panel, live text** | Works, crisp (half-pixel offset), no seams (`04_…png`). |
| … after `loadmap bank` | The world changed; the tiles were re-added, had new handles 2 frames later, no crash (`05_…png`). |

### Cost

| Measurement | Result |
|---|---|
| Untextured: 1 / 40 / 400 quads | 38.3 / 38.3 / 19.3 fps. About 65 µs per poly, but a panel needs only 4. |
| One textured quad | 37.3 fps against a 37.5 base. Not measurable. |
| Probe draw time (about 1 ms per frame) | Almost entirely `geEngine_FlushPipeline`, which renders cached 3D that EndFrame would draw anyway. Without the flush, an untextured quad costs 9 µs. |
| Live upload (GDI redraw + lock/write/unlock of 4 tiles) | **740 µs per update** on the game thread. |
| Frame rate with live uploads | Every 3 frames: 34.5 fps; every frame: 32.3; no panel: 37.3 (same run). That's about 2–4 ms per frame including the driver's texture re-upload. |
| First add to a world | One hitch of about 18 ms. |

## What a production version would need

- **Draw path:** draw at frame end:
  - flush, then RenderPoly with flags 1 and texel-centred quads (−0.5 px);
  - tiles of at most 256, format 4444 (requesting 7 directly avoids a conversion).
- **Attachment:** use `geWorld_AddBitmap` on the current world:
  - re-add when the world changes;
  - skip drawing while `[bmp+0xAC]` is 0 (the 2-frame settle);
  - `geWorld_RemoveBitmap` (exported) when the panel is turned off.
- **No world means no panel** (menus). The watermark could stay on today's decal path.
- **Upload cost:** re-upload only tiles whose content changed. Most of the glitch box is static per frame, and a 4-tile upload costs about 0.7 ms plus driver time. Keep textdraw's `minFrames` throttle.
- **Colours:** 4444 quantises the colour palette to 16 levels per channel. Text in white or saturated colours looks fine; subtle greys band.
- **Untested:**
  - the user's real-GPU dgVoodoo fullscreen setup;
  - mid-load behaviour beyond one level change;
  - whether `geWorld_AddBitmap` on very large worlds hitches longer than the 18 ms seen in Training.

## Not changed

- `core/src`: untouched. The shipping textdraw (565 decals) is unchanged.
- The probe uses one non-exported Genesis RVA (0x171E0, mode 7 only). It is spike code, kept out of `addresses.json` on purpose.

_The logs, screenshots and run output named above were removed on 2026-10-27 (repository tidy-up); git history keeps them._
