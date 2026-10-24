# SpawnAtSpot: one owner, the entities service (D44)

Before: the `entities` service hooked `VictimRescueEnt::SpawnAtSpot`'s entry (0x0045D5B0, the
recorder's victim observers), and the speedrun feature patched a site inside the same function
(0x0045D717, `mov ecx,ebp; call SnapToFloor`) with its own trampoline. That broke the rule of one
owner per hooked function.

After: `entities` owns both. `entities::FilterVictimSpot(fn)` installs the same byte-for-byte
trampoline (pushfd/pushad, call the filter with the victim, the displaced `mov ecx,ebp; call
SnapToFloor`, jmp back) and allows one filter. speedrun calls it from its early step (DllMain,
before any level), keeps its own FDNYDeterministic / FDNYVictimSpawns defer check, and its spot
logic is unchanged.

## Tests, 2026-10-24, Debug

| Test | Result |
|---|---|
| `speedrun_static_spawns` (now also asserts the entities line and the recorder enabled alongside) | PASS |
| `speedrun_reference_deterministic` (defers to FDNYDeterministic.asi) | PASS |
| `speedrun_rescuebot` | PASS |
| `recorder` | PASS |
| `core_single_instance` | PASS |

From `FDNYCore.log` (static spawns run):

```
[INFO] entities: victim-spot filter at 0045d717 -> trampoline 00890000
[INFO] speedrun: static spawns use the entities victim-spot filter
[INFO] module recorder: enabled
[INFO] event: victim_spawn training #0 'victim1' pinned to spot 0
[INFO] event: victim_spawn training #1 'victim2' pinned to spot 0
```

6 "pinned to spot 0" lines over the 3 Training loads; the victims' positions are equal across loads and at spot 0.

Files: `FDNYCore.log`, `result.json`.
