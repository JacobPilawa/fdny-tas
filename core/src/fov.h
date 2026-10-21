// fov module (backlog item 6): camera field of view via geCamera_SetAttributes.
#pragma once

namespace fdcore::fov {
bool Init();
// The render view size: the rect of the game's last geCamera_SetAttributes
// (inclusive GE_Rect, so right+1 x bottom+1). False before the first call.
bool ViewSize(int* w, int* h);
}
