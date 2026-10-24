// Teleporter (backlog item 13, owner request 2026-10-21): fd_tp_save stores the
// player's position + view angles, fd_tp_load puts the player back there --
// only on the map it was saved on. Practice-only (profile gate).
//
// The game has no teleport verb, and its own reset (Verb_Reset ->
// ResetLocomotionState 0x0044CD10) is not reusable: it also drops held
// objects / the carried victim, resets health, sound and zones, moves to the
// escape-mutated checkpoint, and SnapToFloor 0x0049DA00 commits (0,1,0) when
// no floor is found. Instead the load writes, at frame end (after
// TickSubsystems, before the next Think), position (= the inline transform's
// translation at +0x4C), pitch and yaw, and zeroes velocity, this frame's
// displacement and the impulse accumulator, so the next Think integrates and
// depenetrates from the new spot.
#pragma once

namespace fdcore::tp {

bool Init();

} // namespace fdcore::tp
