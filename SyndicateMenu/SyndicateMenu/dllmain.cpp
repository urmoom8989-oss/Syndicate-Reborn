#include "Core/Memory.h"
#include "Menu/Config.h"
#include "Features/ESP.h"
#include "Features/Exploits.h"
#include "Features/Crosshair.h"
#include "Menu/Hooks.h"
#include "Menu/Overlay.h"
#include "SDK/Init.h"
#include "SDK/Objects.h"

#include <windows.h>
#include <cstdio>
#include <cwchar>

HMODULE g_hModule = nullptr;

static BOOL CALLBACK FindGameWindow(HWND hwnd, LPARAM found) {
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    char cls[32] = {};
    if (pid == GetCurrentProcessId() && GetClassNameA(hwnd, cls, sizeof(cls)) && lstrcmpA(cls, "UnrealWindow") == 0) {
        *reinterpret_cast<bool*>(found) = true;
        return FALSE;
    }
    return TRUE;
}

// Wait until the game has made its own window (its graphics device comes with it) before
// creating the dummy D3D11 device and hooking Present. Loaded early in startup, doing that
// right away closed 1.7.2.
static bool WaitForGameWindow() {
    for (int i = 0; i < 600 && running; ++i) {
        bool found = false;
        EnumWindows(FindGameWindow, reinterpret_cast<LPARAM>(&found));
        if (found) {
            Sleep(2000);
            return true;
        }
        Sleep(500);
    }
    return false;
}

// Everything the menu prints goes to SyndicateMenu.log next to the DLL (Runtime\dlls), written
// at once, so a crash shows the last step that ran.
static void OpenLog(HMODULE self) {
    wchar_t path[MAX_PATH] = {};
    if (!GetModuleFileNameW(self, path, MAX_PATH))
        return;
    wchar_t* dot = wcsrchr(path, L'.');
    if (!dot)
        return;
    wcscpy_s(dot, MAX_PATH - (dot - path), L".log");
    FILE* file = nullptr;
    // _wfreopen (not _s) opens the file shared, so the log can be read while the game runs.
#pragma warning(suppress : 4996)
    file = _wfreopen(path, L"w", stdout);
    if (file)
        setvbuf(stdout, nullptr, _IONBF, 0);
    SYSTEMTIME now{};
    GetLocalTime(&now);
    printf("Syndicate menu log, %04d-%02d-%02d %02d:%02d:%02d, game PID %lu\n", now.wYear, now.wMonth, now.wDay,
           now.wHour, now.wMinute, now.wSecond, GetCurrentProcessId());
}

static DWORD WINAPI MainThread(LPVOID param) {
    HMODULE self = (HMODULE)param;
    OpenLog(self);
    CrosshairOnlyMode(); // decided once, here, before the render thread asks
    printf("[menu] waiting for the game window\n");

    if (!WaitForGameWindow()) {
        printf("[menu] no game window, unloading\n");
        FreeLibraryAndExitThread(self, 0);
    }

    baseAddr = Memory::GetImageBase();
    imageSize = Memory::GetImageSize();
    printf("[menu] game window found; hooking Present (image 0x%llx, size 0x%llx)\n",
           (unsigned long long)baseAddr, (unsigned long long)imageSize);

    if (!Hooks::Initialize()) {
        printf("[menu] hooking failed, unloading\n");
        FreeLibraryAndExitThread(self, 0);
    }
    if (CrosshairOnlyMode()) {
        // The launcher loaded the DLL for the custom crosshair only: no game-object scan, no
        // chams/ProcessEvent hooks. Only the Present hook draws the crosshair.
        printf("[menu] Present hooked; crosshair only, the menu is off\n");
        while (running)
            Sleep(200);
    } else
        printf("[menu] Present hooked; reading the game's objects\n");

    while (!CrosshairOnlyMode() && !InitSDK()) {
        printf("[menu] game objects not ready, trying again in 3 s\n");
        Sleep(3000);
        if (!running) break;
    }

    if (running && !CrosshairOnlyMode()) {
        printf("[menu] game objects ready\n");
        Hooks::SetSdkReady(true);
        // Chams (and aim on the game thread) use an inline MinHook on the engine's
        // shared ProcessEvent. On the Chapter 1 seasons (the Erbium path, where the
        // launcher loads cobalt.dll first) that kind of hook crashed 6.20 and 7.40,
        // so it is only installed on Season X. There the render thread gives the local
        // player's HUD its own vtable copy instead (TickHudChamsHook in ESP.cpp).
        if (!GetModuleHandleW(L"cobalt.dll"))
            InstallChamsHook();
        while (running)
            Sleep(200);
    }

    printf("[menu] unloading\n");
    // Let the next frame give the game back your movement, cursor and speed first (up to 1 s;
    // a minimized game draws no frames, then the switches below still turn the server side off).
    for (int i = 0; i < 50 && !Hooks::UnloadCleaned(); ++i)
        Sleep(20);
    if (!Hooks::UnloadCleaned())
        printf("[unload] no frame drawn in 1 s: skipped the in-game cleanup\n");
    StopMagicBullet();
    Sleep(100);
    Hooks::Shutdown();
    // After the Present hook is off (it is what installs the HUD hook), give the HUD its own
    // vtable back so the game never calls into this DLL once it is freed.
    Sleep(100);
    UninstallHudChamsHook();
    printf("[menu] unloaded cleanly\n");
    FreeLibraryAndExitThread(self, 0);
}

BOOL WINAPI DllMain(HMODULE hMod, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        g_hModule = hMod;
        DisableThreadLibraryCalls(hMod);
        running = true;
        CloseHandle(CreateThread(nullptr, 0, MainThread, hMod, 0, nullptr));
    }
    if (reason == DLL_PROCESS_DETACH) {
        running = false;
        StopMagicBullet();
        Hooks::Shutdown();
    }
    return TRUE;
}
