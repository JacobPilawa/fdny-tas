// camera (core service): owns the geCamera_SetAttributes hook (Genesis.dll,
// fastcall ECX = geCamera*, EDX = const geRect*, stack = Fov float; ret 4).
// Gives the render view size to everyone (gfx) and lets one feature (fov)
// replace the FOV the game asks for.
#pragma once

namespace fdcore::camera {

bool Init();

// The render view size: the rect of the game's last SetAttributes (inclusive
// GE_Rect, so right+1 x bottom+1). False before the first call.
bool ViewSize(int* w, int* h);

// Maps the game's Fov (and the camera rect) to the one stored. One filter; null = unchanged.
using FovFilter = float (*)(const int* rect, float gameFov);
void SetFovFilter(FovFilter f);
// After every SetAttributes: the camera and the game's own Fov (the stored one is at [camera+0]).
using SetFn = void (*)(void* camera, float gameFov);
void OnSet(SetFn fn);

// Re-runs the game's last SetAttributes on the live in-level camera (found
// through the address table), e.g. after the filter's settings changed.
bool Reapply();
float LastGameFov();

} // namespace fdcore::camera
