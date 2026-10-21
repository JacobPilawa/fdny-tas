// fov module: owns the camera FOV via a hook on Genesis.dll's
// geCamera_SetAttributes (fastcall: ECX = geCamera*, EDX = const geRect*,
// stack = Fov float; ret 4). Genesis stores Fov raw at [camera+0] and
// projects with scale = Width / Fov, so horizontal FOV = 2*atan(Fov/2).
//
// Parity with the original FOVFix .asi (measured live, 2026-10-21): it
// replaces every Fov with the constant 1.43 * (W/H)/(4/3) * FOVFactor
// (W/H from its ini; 1.43 ~ the game's stock in-level 1.428). With the
// owner's ini (640x480, 1.4) that is 2.002 = 90.0 deg horizontal.
#include "fov.h"

#include <windows.h>

#include "addr.h"
#include "cmd.h"
#include "console.h"
#include "hooks.h"
#include "log.h"

namespace fdcore::fov {
namespace {

using addr::Id;
using SetAttributesFn = int(__fastcall*)(void* camera, const int* rect, float fov);
SetAttributesFn g_orig = nullptr;

void* g_lastCamera = nullptr;
int g_lastRect[4];
float g_lastGameFov = 0.0f;
float g_lastStored = -1.0f;

// --- no-CRT float helpers ----------------------------------------------------
float Tan(float x) {
    float r;
    __asm__("fptan\n\tfstp %%st(0)" : "=t"(r) : "0"(x));
    return r;
}

bool ParseFloat(const char* s, float* out) {
    float sign = 1.0f, v = 0.0f, scale = 0.1f;
    if (*s == '-') sign = -1.0f, ++s;
    else if (*s == '+') ++s;
    bool any = false;
    while (*s >= '0' && *s <= '9') v = v * 10.0f + static_cast<float>(*s++ - '0'), any = true;
    if (*s == '.') {
        ++s;
        while (*s >= '0' && *s <= '9') v += static_cast<float>(*s++ - '0') * scale, scale *= 0.1f, any = true;
    }
    if (!any || *s) return false;
    *out = sign * v;
    return true;
}

// "123.456789" (wsprintf has no %f)
void FormatFloat(float v, char* out) {
    long m = static_cast<long>(v * 1000000.0f + (v >= 0 ? 0.5f : -0.5f));
    long a = m < 0 ? -m : m;
    wsprintfA(out, "%s%ld.%06ld", m < 0 ? "-" : "", a / 1000000, a % 1000000);
}

// --- variables -----------------------------------------------------------------
void Reapply(cmd::Cvar&);
cmd::Cvar g_fdFov = {"fd_fov", "0",
                     "horizontal FOV in degrees at 4:3, Hor+ for wider screens; 0 = classic FOV fix "
                     "(1.43 * aspect/(4/3) * fd_fov_factor); -1 = game default",
                     false, true, Reapply, {0}};
cmd::Cvar g_fdFovFactor = {"fd_fov_factor", "1.4", "FOV multiplier used when fd_fov is 0 (classic FOV fix)",
                           false, true, Reapply, {0}};

// Fov to store for a camera with this rect, given the game's own Fov.
float Wanted(const int* rect, float gameFov) {
    float fov = 0.0f, factor = 1.4f;
    if (!ParseFloat(g_fdFov.value, &fov)) fov = 0.0f;
    if (!ParseFloat(g_fdFovFactor.value, &factor)) factor = 1.4f;
    if (fov < 0.0f) return gameFov;
    float w = static_cast<float>(rect[1] - rect[0] + 1), h = static_cast<float>(rect[3] - rect[2] + 1);
    float horPlus = h > 0.0f ? (w / h) / (4.0f / 3.0f) : 1.0f;
    if (fov == 0.0f) return 1.43f * horPlus * factor;
    if (fov > 170.0f) fov = 170.0f;
    return 2.0f * Tan(fov * 3.14159265f / 360.0f) * horPlus;
}

int __fastcall SetAttributesDetour(void* camera, const int* rect, float fov) {
    float use = Wanted(rect, fov);
    int r = g_orig(camera, rect, use);
    g_lastCamera = camera;
    for (int i = 0; i < 4; ++i) g_lastRect[i] = rect[i];
    g_lastGameFov = fov;
    float stored = *reinterpret_cast<const float*>(camera);
    if (stored != g_lastStored) {
        char a[32], b[32];
        FormatFloat(fov, a);
        FormatFloat(stored, b);
        log::Info("fov: camera %08lx game_fov=%s stored=%s", reinterpret_cast<unsigned long>(camera), a, b);
        g_lastStored = stored;
#ifdef FDCORE_DEBUG
        // Debug aid: report where the render object holds this camera, so the
        // address table can point at it (looked up once).
        static bool probed = false;
        uintptr_t field;
        uint32_t render = 0;
        if (!probed && addr::Resolve(Id::everGame_render, &field) && addr::SafeRead(field, &render, 4) && render) {
            probed = true;
            for (uint32_t off = 0; off < 0x200; off += 4) {
                uint32_t v = 0;
                if (!addr::SafeRead(render + off, &v, 4)) break;
                if (v == reinterpret_cast<uint32_t>(camera)) log::Info("fov: probe [render+%lx] == camera", off);
                for (uint32_t off2 = 0; v && off2 < 0x100; off2 += 4) {
                    uint32_t w = 0;
                    if (!addr::SafeRead(v + off2, &w, 4)) break;
                    if (w == reinterpret_cast<uint32_t>(camera))
                        log::Info("fov: probe [[render+%lx]+%lx] == camera", off, off2);
                }
            }
        }
#endif
    }
    return r;
}

// Variable changed: apply at once to the live in-level camera (found through
// the address table, not a cached pointer that may be stale), with the rect
// and game FOV of the game's last SetAttributes call.
void Reapply(cmd::Cvar&) {
    uintptr_t fovField;
    if (!g_orig || !g_lastCamera || !addr::Resolve(Id::camera_fov, &fovField)) return;
    void* camera = reinterpret_cast<void*>(fovField);  // Fov is the first field of geCamera
    float probe;
    if (!addr::SafeRead(fovField, &probe, 4)) return;
    SetAttributesDetour(camera, g_lastRect, g_lastGameFov);
}

void CmdFovInfo(int, const char* const*) {
    char a[32], b[32];
    FormatFloat(g_lastGameFov, a);
    FormatFloat(g_lastStored, b);
    console::Print("fov: game %s, camera %s (fd_fov %s, fd_fov_factor %s)", a, b, g_fdFov.value, g_fdFovFactor.value);
}


} // namespace

bool ViewSize(int* w, int* h) {
    if (!g_lastCamera || g_lastRect[1] <= 0 || g_lastRect[3] <= 0) return false;
    *w = g_lastRect[1] + 1, *h = g_lastRect[3] + 1;
    return true;
}

bool Init() {
    if (!addr::Usable(Id::genesis_camera_set_attributes)) {
        log::Warn("fov: disabled (needs genesis.camera_set_attributes)");
        return false;
    }
    cmd::RegisterCvar(&g_fdFov);
    cmd::RegisterCvar(&g_fdFovFactor);
    cmd::RegisterCommand("fd_fovinfo", "show the game's FOV and the camera's current FOV", CmdFovInfo);
    if (!hooks::Create("fov", Id::genesis_camera_set_attributes, reinterpret_cast<void*>(&SetAttributesDetour),
                       reinterpret_cast<void**>(&g_orig)) ||
        !hooks::SetEnabled("fov", true))
        return false;
    log::Info("fov: ready (geCamera_SetAttributes hooked)");
    return true;
}

} // namespace fdcore::fov
