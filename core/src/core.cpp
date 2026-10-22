// FDNYCore entry point. Loaded by Ultimate ASI Loader (dinput8.dll) like the
// other .asi plugins. DllMain only spawns the init thread (loader lock);
// everything else happens there.
#include <windows.h>
#include <bcrypt.h>

// The core never names a feature: features (src/features/) register themselves
// with FDCORE_MODULE and are started by modules::StartAll after the services.
#include "addr.h"
#include "camera.h"
#include "console.h"
#include "crashlog.h"
#include "gen/version_gen.h"
#include "hooks.h"
#include "log.h"
#include "modules.h"
#include "patches.h"
#include "pause.h"
#include "profile.h"
#include "textdraw.h"
#include "state.h"
#include "testpipe.h"

#ifndef FDCORE_VERSION
#error FDCORE_VERSION must come from core/VERSION (build.ps1)
#endif
#ifdef FDCORE_DEBUG
#define FDCORE_CONFIG "Debug"
#else
#define FDCORE_CONFIG "Release"
#endif

namespace {

HMODULE g_self = nullptr;
char g_dir[MAX_PATH];

// SHA-256 of a whole file as 64 lowercase hex chars. False on any failure.
bool HashFile(const char* path, char (&hex)[65]) {
    hex[0] = 0;
    HANDLE f = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                           OPEN_EXISTING, 0, nullptr);
    if (f == INVALID_HANDLE_VALUE) return false;

    BCRYPT_ALG_HANDLE alg = nullptr;
    BCRYPT_HASH_HANDLE h = nullptr;
    bool ok = BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, nullptr, 0) == 0 &&
              BCryptCreateHash(alg, &h, nullptr, 0, nullptr, 0, 0) == 0;
    static unsigned char buf[64 * 1024];
    DWORD got = 0;
    while (ok && ReadFile(f, buf, sizeof(buf), &got, nullptr) && got)
        ok = BCryptHashData(h, buf, got, 0) == 0;
    unsigned char digest[32];
    ok = ok && BCryptFinishHash(h, digest, sizeof(digest), 0) == 0;
    if (h) BCryptDestroyHash(h);
    if (alg) BCryptCloseAlgorithmProvider(alg, 0);
    CloseHandle(f);
    if (!ok) return false;

    static const char kHex[] = "0123456789abcdef";
    for (int i = 0; i < 32; ++i) {
        hex[i * 2] = kHex[digest[i] >> 4];
        hex[i * 2 + 1] = kHex[digest[i] & 15];
    }
    hex[64] = 0;
    return true;
}

void ModulePath(HMODULE m, char (&out)[MAX_PATH]) {
    out[0] = 0;
    GetModuleFileNameA(m, out, MAX_PATH);
}

DWORD WINAPI InitThread(LPVOID) {
    char exePath[MAX_PATH];
    ModulePath(nullptr, exePath);

    // Genesis.dll is an import of the exe, so it's normally mapped already;
    // wait briefly in case the loader runs us earlier than that.
    HMODULE genesis = nullptr;
    for (int i = 0; i < 200 && !(genesis = GetModuleHandleA("Genesis.dll")); ++i) Sleep(50);
    if (!genesis) {
        fdcore::log::Error("Genesis.dll not loaded after 10 s; core disabled");
        return 0;
    }
    char genesisPath[MAX_PATH];
    ModulePath(genesis, genesisPath);

    char exeHash[65], genesisHash[65];
    if (!HashFile(exePath, exeHash)) fdcore::log::Warn("could not hash %s", exePath);
    if (!HashFile(genesisPath, genesisHash)) fdcore::log::Warn("could not hash %s", genesisPath);

    fdcore::log::Info("FDNYCore %s (%s) exe=%s sha256=%s base=%08lx", FDCORE_VERSION,
                      FDCORE_CONFIG, exePath, exeHash,
                      reinterpret_cast<unsigned long>(GetModuleHandleA(nullptr)));
    fdcore::log::Info("Genesis.dll sha256=%s base=%08lx", genesisHash,
                      reinterpret_cast<unsigned long>(genesis));
    fdcore::addr::Init(exeHash, genesisHash, reinterpret_cast<uintptr_t>(GetModuleHandleA(nullptr)),
                       reinterpret_cast<uintptr_t>(genesis));
    // Core services, then every registered feature module.
    const bool services = fdcore::addr::VersionKnown() && fdcore::hooks::Init();
    if (services) {
        fdcore::console::Init(g_dir, FDCORE_VERSION);
        fdcore::camera::Init();
        fdcore::state::Init();
        fdcore::patches::Init();
        fdcore::pause::Init();     // after state (it reads the level state each paused frame)
        fdcore::textdraw::Init();  // draws late in the frame, after every module's update
        fdcore::profile::Init();   // gates read cmd::IsPracticeOnly at use time
    }
    fdcore::modules::StartAll(services);
#ifdef FDCORE_DEBUG
    fdcore::testpipe::Start();
#endif
    fdcore::log::Info("FDNYCore ready");
    return 0;
}

} // namespace

BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        g_self = inst;
        DisableThreadLibraryCalls(inst);

        char path[MAX_PATH];
        ModulePath(inst, path);
        lstrcpyA(g_dir, path);
        for (char* p = g_dir + lstrlenA(g_dir); p > g_dir; --p)
            if (p[-1] == '\\' || p[-1] == '/') {
                p[0] = 0;
                break;
            }
        char logPath[MAX_PATH];
        wsprintfA(logPath, "%sFDNYCore.log", g_dir);
        fdcore::log::Open(logPath);
        fdcore::log::Info("FDNYCore loaded from %s", path);
        fdcore::crashlog::Install();
        // DllMain runs before WinMain (verified 2026-10-21: g_mainWindow and
        // g_console are still null here), so startup patches go in now.
        fdcore::patches::ApplyEarly();
        fdcore::patches::ApplyIntroSkip(g_dir);
        fdcore::modules::RunEarly(g_dir);  // the features' own startup patches

        HANDLE t = CreateThread(nullptr, 0, InitThread, nullptr, 0, nullptr);
        if (t) CloseHandle(t);
        else fdcore::log::Error("CreateThread failed (%lu)", GetLastError());
    }
    return TRUE;
}

extern "C" BOOL WINAPI _DllMainCRTStartup(HINSTANCE inst, DWORD reason, LPVOID reserved) {
    return DllMain(inst, reason, reserved);
}
