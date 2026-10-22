#include "camera.h"

#include "addr.h"
#include "hooks.h"
#include "log.h"

namespace fdcore::camera {
namespace {

using addr::Id;
using SetAttributesFn = int(__fastcall*)(void* camera, const int* rect, float fov);
SetAttributesFn g_orig = nullptr;

void* g_lastCamera = nullptr;
int g_lastRect[4];
float g_lastGameFov = 0.0f;
FovFilter g_filter = nullptr;
constexpr int kMaxSubs = 4;
SetFn g_subs[kMaxSubs];
int g_nSubs = 0;

int __fastcall SetAttributesDetour(void* camera, const int* rect, float fov) {
    const float use = g_filter ? g_filter(rect, fov) : fov;
    const int r = g_orig(camera, rect, use);
    g_lastCamera = camera;
    for (int i = 0; i < 4; ++i) g_lastRect[i] = rect[i];
    g_lastGameFov = fov;
    for (int i = 0; i < g_nSubs; ++i) g_subs[i](camera, fov);
    return r;
}

} // namespace

bool Init() {
    if (!addr::Usable(Id::genesis_camera_set_attributes)) {
        log::Warn("camera: disabled (needs genesis.camera_set_attributes); no FOV control, view size from the window");
        return false;
    }
    if (!hooks::Create("camera", Id::genesis_camera_set_attributes, reinterpret_cast<void*>(&SetAttributesDetour),
                       reinterpret_cast<void**>(&g_orig)) ||
        !hooks::SetEnabled("camera", true))
        return false;
    log::Info("camera: ready (geCamera_SetAttributes hooked)");
    return true;
}

bool ViewSize(int* w, int* h) {
    if (!g_lastCamera || g_lastRect[1] <= 0 || g_lastRect[3] <= 0) return false;
    *w = g_lastRect[1] + 1, *h = g_lastRect[3] + 1;
    return true;
}

void SetFovFilter(FovFilter f) { g_filter = f; }

void OnSet(SetFn fn) {
    if (g_nSubs < kMaxSubs) g_subs[g_nSubs++] = fn;
}

bool Reapply() {
    uintptr_t fovField;
    if (!g_orig || !g_lastCamera || !addr::Resolve(Id::camera_fov, &fovField)) return false;
    void* camera = reinterpret_cast<void*>(fovField);  // Fov is the first field of geCamera
    float probe;
    if (!addr::SafeRead(fovField, &probe, 4)) return false;
    SetAttributesDetour(camera, g_lastRect, g_lastGameFov);
    return true;
}

float LastGameFov() { return g_lastGameFov; }

} // namespace fdcore::camera
