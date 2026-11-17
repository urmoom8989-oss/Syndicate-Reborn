#define IMGUI_DEFINE_MATH_OPERATORS
#include "Hooks.h"
#include "Config.h"
#include "Menu.h"
#include "Overlay.h"
#include "../Features/Aim.h"
#include "../Features/ESP.h"
#include "../Features/Exploits.h"
#include "../Features/CustomOutfits.h"
#include "../Features/Crosshair.h"
#include "../Features/Game.h"
#include "../SDK/Init.h"
#include "../SDK/Objects.h"
#include "../SDK/Offsets.h"
#include "../Core/Memory.h"

#include <MinHook.h>
#include <imgui.h>
#include <imgui_impl_dx11.h>
#include <imgui_impl_win32.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <dcomp.h>
#include <cstdio>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "dcomp.lib")

#ifndef WDA_EXCLUDEFROMCAPTURE
#define WDA_EXCLUDEFROMCAPTURE 0x00000011
#endif

typedef HRESULT(WINAPI* Present_t)(IDXGISwapChain* pSwapChain, UINT SyncInterval, UINT Flags);
typedef HRESULT(WINAPI* ResizeBuffers_t)(IDXGISwapChain* pSwapChain, UINT BufferCount, UINT Width, UINT Height, DXGI_FORMAT NewFormat, UINT SwapChainFlags);

static Present_t oPresent = nullptr;
static ResizeBuffers_t oResizeBuffers = nullptr;
static WNDPROC oWndProc = nullptr;
static ID3D11RenderTargetView* g_pRTV = nullptr;
static ID3D11Device* g_pd3dDevice = nullptr;
static ID3D11DeviceContext* g_pd3dContext = nullptr;
static HWND g_hWnd = nullptr;
static bool g_bImGui = false;
static bool g_bHooks = false;
static bool g_bSdk = false;
static int g_frame = 0;

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
void LoadSyndicateLogo(ID3D11Device* Device);

static HCURSOR MenuCursorHandle();
LRESULT CALLBACK HookedWndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    if (uMsg == WM_KEYDOWN && wParam == VK_END) {
        RequestUnload();
        return 0;
    }
    // Insert, or the "Menu key" picked in Misc (for keyboards without an Insert key).
    if (uMsg == WM_KEYDOWN && (wParam == VK_INSERT || (cheat.menuKey && (int)wParam == cheat.menuKey)) && (lParam & (1 << 30)) == 0) {
        ToggleMenu();
        return 0;
    }

    if (MenuCapturesInput()) {
        if (ImGui_ImplWin32_WndProcHandler(hWnd, uMsg, wParam, lParam))
            return true;
        // Input lock: while the menu is open the game gets no presses, clicks or mouse
        // look (raw input), so the character stands still behind the menu. Key and button
        // releases still reach the game, so a key held while opening the menu lets go.
        switch (uMsg) {
            case WM_LBUTTONDOWN: case WM_LBUTTONDBLCLK:
            case WM_RBUTTONDOWN: case WM_RBUTTONDBLCLK:
            case WM_MBUTTONDOWN: case WM_MBUTTONDBLCLK:
            case WM_XBUTTONDOWN: case WM_XBUTTONDBLCLK:
            case WM_MOUSEMOVE:
            case WM_MOUSEWHEEL: case WM_MOUSEHWHEEL:
            case WM_KEYDOWN: case WM_SYSKEYDOWN:
            case WM_CHAR: case WM_SYSCHAR:
                return true;
            case WM_INPUT:
                return DefWindowProcW(hWnd, uMsg, wParam, lParam);
            case WM_SETCURSOR:
                // The cursor is decided once per frame (MenuCursor); letting the game answer
                // this too made it flicker between the game's hidden cursor and an arrow.
                if (LOWORD(lParam) == HTCLIENT) {
                    SetCursor(MenuCursorHandle());
                    return TRUE;
                }
                break;
        }
    }
    return CallWindowProcW(oWndProc, hWnd, uMsg, wParam, lParam);
}

// Syndicate rule: weapon mods change how hard we hit anyone, so they only run
// while every other player in the match is a bot. With a real player in the
// match they pause (the menu keeps the player's choices and they resume when
// the match is bots only again). Aim, silent aim and projectile redirect stay
// on because they only ever pick bot targets (see CollectTargets).
struct CombatModsPause {
    bool active = false;
    bool noSpread, noBloom, noRecoil, noReload, noCharge, rapidFire, damageMultiplier,
        fastPickaxe, automaticWeapons, doublePump, distanceMultiplier;
    explicit CombatModsPause(bool pause) : active(pause) {
        noSpread = cheat.noSpread; noBloom = cheat.noBloom; noRecoil = cheat.noRecoil;
        noReload = cheat.noReload; noCharge = cheat.noCharge; rapidFire = cheat.rapidFire;
        damageMultiplier = cheat.damageMultiplier; fastPickaxe = cheat.fastPickaxe;
        automaticWeapons = cheat.automaticWeapons; doublePump = cheat.doublePump;
        distanceMultiplier = cheat.distanceMultiplier;
        if (!active) return;
        cheat.noSpread = cheat.noBloom = cheat.noRecoil = cheat.noReload = cheat.noCharge = false;
        cheat.rapidFire = cheat.damageMultiplier = cheat.fastPickaxe = false;
        cheat.automaticWeapons = cheat.doublePump = cheat.distanceMultiplier = false;
    }
    ~CombatModsPause() {
        if (!active) return;
        cheat.noSpread = noSpread; cheat.noBloom = noBloom; cheat.noRecoil = noRecoil;
        cheat.noReload = noReload; cheat.noCharge = noCharge; cheat.rapidFire = rapidFire;
        cheat.damageMultiplier = damageMultiplier; cheat.fastPickaxe = fastPickaxe;
        cheat.automaticWeapons = automaticWeapons; cheat.doublePump = doublePump;
        cheat.distanceMultiplier = distanceMultiplier;
    }
};

// Mouse cursor while the menu is open. The game normally keeps its own cursor hidden and pins
// the mouse to one spot every frame (high-precision mouse mode), so with the menu open the cursor
// jumped back and blinked. Now: the player controller's bShowMouseCursor is set (the game lets go
// of the mouse and stops re-centring it), ImGui never changes the OS cursor itself, and if the
// game still hides the cursor for half a second, ImGui draws its own instead.
static bool g_softCursor = false;
static HCURSOR MenuCursorHandle() {
    static HCURSOR arrow = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512)); // IDC_ARROW (the project is not built as Unicode)
    return g_softCursor ? nullptr : arrow;
}

static void WriteBitProp(uintptr_t base, const PropInfo& prop, bool value) {
    if (!base || !prop.found)
        return;
    uint8_t byte = Read<uint8_t>(base + prop.offset);
    if (prop.mask)
        byte = value ? (uint8_t)(byte | prop.mask) : (uint8_t)(byte & ~prop.mask);
    else
        byte = value ? 1 : 0;
    Write<uint8_t>(base + prop.offset, byte);
}

static void TickMenuCursor(ImGuiIO& io) {
    static PropInfo showCursor = FindClassProp("PlayerController", "bShowMouseCursor");
    static bool told = false;
    static bool shown = false;
    static bool wasShowing = false;
    static uintptr_t shownPC = 0;
    static DWORD hiddenSince = 0;
    const bool open = MenuCapturesInput() && running;
    if (!told) {
        told = true;
        printf("[cursor] bShowMouseCursor %s (offset 0x%X mask 0x%X)\n", showCursor.found ? "found" : "MISSING",
            (unsigned)showCursor.offset, (unsigned)showCursor.mask);
    }
    io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;
    if (open && localPC && showCursor.found) {
        if (!shown || shownPC != localPC) {
            wasShowing = (Read<uint8_t>(localPC + showCursor.offset) & (showCursor.mask ? showCursor.mask : 1)) != 0;
            shownPC = localPC;
            shown = true;
        }
        WriteBitProp(localPC, showCursor, true);
    } else if (shown) {
        if (shownPC && shownPC == localPC)
            WriteBitProp(shownPC, showCursor, wasShowing);
        shown = false;
        shownPC = 0;
    }
    if (open) {
        ClipCursor(nullptr);
        CURSORINFO info{ sizeof(info) };
        const bool visible = GetCursorInfo(&info) && (info.flags & CURSOR_SHOWING) && info.hCursor;
        const DWORD now = GetTickCount();
        if (visible && !g_softCursor) {
            hiddenSince = 0;
        } else if (!visible && !g_softCursor) {
            if (!hiddenSince)
                hiddenSince = now;
            else if (now - hiddenSince > 500) {
                g_softCursor = true;
                printf("[cursor] the game keeps the cursor hidden: drawing the menu's own cursor\n");
            }
        }
        if (GetForegroundWindow() == g_hWnd)
            SetCursor(MenuCursorHandle());
    } else {
        hiddenSince = 0;
    }
    io.MouseDrawCursor = open && g_softCursor;
}

// Second half of the input lock: the player controller ignores move and look input while
// the menu is open (SetIgnoreMoveInput/SetIgnoreLookInput count up and down, so every lock
// is paired with one unlock). Also unlocks when the menu unloads (End).
static void TickInputLock() {
    static bool locked = false;
    static uintptr_t lockedPC = 0;
    const bool want = MenuCapturesInput() && running && localPC;
    if (want == locked && (!locked || lockedPC == localPC))
        return;
    static uintptr_t fnMove = 0, fnLook = 0, fnFlush = 0;
    static bool tried = false;
    if (!tried) {
        tried = true;
        fnMove = GetFunction("Controller", "SetIgnoreMoveInput");
        fnLook = GetFunction("Controller", "SetIgnoreLookInput");
        fnFlush = GetFunction("PlayerController", "FlushPressedKeys");
        printf("[input] lock functions: move %s, look %s, flush %s\n", fnMove ? "yes" : "no",
            fnLook ? "yes" : "no", fnFlush ? "yes" : "no");
    }
    struct { bool value; } params{};
    if (locked && lockedPC) {
        // Release the lock on the controller we locked (it may be a new one after respawn).
        params.value = false;
        if (lockedPC == localPC) {
            if (fnMove) ProcessEvent(lockedPC, fnMove, &params);
            if (fnLook) ProcessEvent(lockedPC, fnLook, &params);
        }
        locked = false;
        lockedPC = 0;
        printf("[input] menu closed: game input unlocked\n");
    }
    if (want) {
        params.value = true;
        if (fnFlush) { char none[8]{}; ProcessEvent(localPC, fnFlush, none); }
        if (fnMove) ProcessEvent(localPC, fnMove, &params);
        if (fnLook) ProcessEvent(localPC, fnLook, &params);
        locked = true;
        lockedPC = localPC;
        printf("[input] menu open: game input locked (pc %p)\n", (void*)localPC);
    }
}

static void DrawFrame() {
    ImGuiIO& io = ImGui::GetIO();
    W = (int)io.DisplaySize.x;
    H = (int)io.DisplaySize.y;
    TickCrosshairConfig();
    if (CrosshairOnlyMode()) {
        // Loaded for the crosshair alone (menu setting off): no game objects, menu or keys.
        DrawCrosshair(io.DisplaySize.x, io.DisplaySize.y, false);
        g_frame++;
        return;
    }
    NoteRenderThread();
    TickMenuMotion();
    TickMenuHotkeys();

    if (g_bSdk) {
        static bool firstTick = true;
        if (firstTick) {
            firstTick = false;
            printf("[hooks] first frame with game objects\n");
        }
        TickGame();
        TickInputLock();
        TickCustomOutfits();
        TickMagicBullet();
        if (world) {
            TickHudChamsHook();
            CollectTargets(W, H, cheat.aimBone);
            if (gCam.valid && (localPawn || localPC)) {
                const bool humans = OtherHumansInMatch();
                if (humans && (cheat.noSpread || cheat.noBloom || cheat.noRecoil || cheat.noReload || cheat.noCharge ||
                               cheat.rapidFire || cheat.damageMultiplier || cheat.fastPickaxe || cheat.automaticWeapons ||
                               cheat.doublePump))
                    FeatureStatus("Weapon mods", "paused: a real player is in the match (bots only)");
                CombatModsPause pause(humans);
                if (localPawn)
                    DoWeaponMods();
                TickExploits();
                if (localPawn)
                    DoAim();
            }
            DrawESP();
            DrawDebugOverlay();
        } else {
            targets.clear();
        }
    }

    DrawCrosshair(io.DisplaySize.x, io.DisplaySize.y, MenuCapturesInput());

    if (MenuVisible())
        DrawMenu();

    g_frame++;
}

// End / Unload DLL. The menu used to stop drawing at once, so the input lock it holds while the
// menu is open (SetIgnoreMoveInput / SetIgnoreLookInput) was never released when you unloaded
// with the menu open, and you could not move again. On the first frame after the unload request
// it now releases the lock (and clears the controller's ignore-input counters outright), gives
// the cursor back, and puts your pawn's speed and fly tuning back; the unload waits for this.
static volatile bool g_unloadCleaned = false;

static void CleanupBeforeUnload() {
    printf("[unload] cleaning up on the render thread\n");
    TickInputLock(); // running is false: releases the lock it holds
    if (localPC) {
        static uintptr_t resetMove = GetFunction("Controller", "ResetIgnoreMoveInput");
        static uintptr_t resetLook = GetFunction("Controller", "ResetIgnoreLookInput");
        char none[8]{};
        if (resetMove) ProcessEvent(localPC, resetMove, none);
        if (resetLook) ProcessEvent(localPC, resetLook, none);
        printf("[unload] ignore-input counters cleared (move %s, look %s)\n", resetMove ? "yes" : "missing", resetLook ? "yes" : "missing");
    }
    if (g_bImGui)
        TickMenuCursor(ImGui::GetIO()); // gives the game its own cursor setting back
    ClipCursor(nullptr);
    ResetLocalCheats();
}

// __try in its own function (MSVC C2712: not in a function that needs object unwinding).
static void SafeCleanupBeforeUnload() {
    __try { CleanupBeforeUnload(); } __except (EXCEPTION_EXECUTE_HANDLER) { printf("[unload] cleanup hit an error\n"); }
}

namespace Hooks {
    bool UnloadCleaned() { return g_unloadCleaned; }
}

// ---------------------------------------------------------------------------------------------
// Stream proof (Stream tab). Recording and streaming apps (OBS, Discord, Game Bar, ShadowPlay)
// capture the game's own frames, so anything drawn into the game's swapchain is on stream. With
// stream proof on, the menu's ImGui frame (menu, ESP, debug overlay, soft cursor) is drawn on a
// click-through transparent window laid exactly over the game instead, and Windows is told to
// leave that window out of every capture (SetWindowDisplayAffinity WDA_EXCLUDEFROMCAPTURE).
// The overlay's swapchain is made on the game's own D3D11 device, so ImGui keeps one renderer.
// Input still goes to the game window (the overlay is WS_EX_TRANSPARENT), same as before.
// ---------------------------------------------------------------------------------------------
static HWND g_spWnd = nullptr;
static HANDLE g_spThread = nullptr;
static HANDLE g_spReady = nullptr;
static IDXGISwapChain* g_spSwap = nullptr;
static IDCompositionDevice* g_spDComp = nullptr;
static IDCompositionTarget* g_spTarget = nullptr;
static IDCompositionVisual* g_spVisual = nullptr;
static ID3D11RenderTargetView* g_spRTV = nullptr;
static UINT g_spW = 0, g_spH = 0;
static bool g_spHiddenFromCapture = false;
static bool g_spFailed = false;
static bool g_spShown = false;
static const char* g_spStatus = "Off: the menu and ESP show on stream and in recordings.";
static bool g_spSwitchedMode = false;   // stream proof moved the game from exclusive fullscreen to windowed fullscreen
static ULONGLONG g_spModeNextTry = 0;

// The game's window mode through its own settings (GameUserSettings.SetFullscreenMode + ApplyResolutionSettings):
// 0 fullscreen, 1 windowed fullscreen, 2 windowed. Windows can't draw anything over (or hide anything in) exclusive
// fullscreen, so stream proof moves the game to windowed fullscreen while it is on and puts fullscreen back after.
static __declspec(noinline) bool SetGameWindowModeInner(uint8_t mode) {
    static PropInfo settingsProp = FindClassProp("Engine", "GameUserSettings");
    static uintptr_t setMode = GetFunction("GameUserSettings", "SetFullscreenMode");
    static uintptr_t apply = GetFunction("GameUserSettings", "ApplyResolutionSettings");
    const uintptr_t engine = FindEngine();
    if (!engine || !settingsProp.found || !setMode || !apply)
        return false;
    const uintptr_t settings = Read<uintptr_t>(engine + settingsProp.offset);
    if (!settings)
        return false;
    alignas(8) uint8_t params[0x10]{};
    params[0] = mode;
    if (!ProcessEvent(settings, setMode, params))
        return false;
    alignas(8) uint8_t applyParams[0x10]{};
    return ProcessEvent(settings, apply, applyParams);
}

static bool SetGameWindowMode(uint8_t mode) {
    __try {
        return SetGameWindowModeInner(mode);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

static void StreamProofRestoreWindowMode() {
    if (!g_spSwitchedMode)
        return;
    g_spSwitchedMode = false;
    const bool ok = SetGameWindowMode(0);
    printf("[stream] %s the game back to fullscreen\n", ok ? "switched" : "could not switch");
}

static LRESULT CALLBACK StreamProofWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_NCHITTEST: return HTTRANSPARENT;
        case WM_MOUSEACTIVATE: return MA_NOACTIVATE;
        case WM_CLOSE: DestroyWindow(hWnd); return 0;
        case WM_DESTROY: PostQuitMessage(0); return 0;
    }
    return DefWindowProcW(hWnd, msg, wParam, lParam);
}

// The overlay window lives on its own thread with its own message loop (the render thread
// does not pump messages, and a window nobody pumps would make Windows hang the game).
static DWORD WINAPI StreamProofThread(LPVOID) {
    WNDCLASSEXW wc{ sizeof(wc) };
    wc.lpfnWndProc = StreamProofWndProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"SyndicateOverlay";
    RegisterClassExW(&wc);
    // No redirection surface: what the window shows comes only from its DirectComposition visual (the overlay
    // swapchain, premultiplied alpha), so everything ImGui does not draw is see-through. (The first version drew
    // into a normal window and relied on DWM glass for the alpha, which left the whole game covered on Edwin's PC.)
    // Layered + transparent makes it click-through for the game window behind it.
    HWND hwnd = CreateWindowExW(WS_EX_NOREDIRECTIONBITMAP | WS_EX_TOPMOST | WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
        L"SyndicateOverlay", L"", WS_POPUP, 0, 0, 1, 1, nullptr, nullptr, wc.hInstance, nullptr);
    if (hwnd) {
        SetLayeredWindowAttributes(hwnd, 0, 255, LWA_ALPHA);
        g_spHiddenFromCapture = SetWindowDisplayAffinity(hwnd, WDA_EXCLUDEFROMCAPTURE) != FALSE;
        if (!g_spHiddenFromCapture)
            printf("[stream] WDA_EXCLUDEFROMCAPTURE refused (error %lu): Windows 10 2004 or newer is needed\n", GetLastError());
        else
            printf("[stream] overlay window made and excluded from capture\n");
    } else {
        printf("[stream] overlay window could not be made (error %lu)\n", GetLastError());
    }
    g_spWnd = hwnd;
    SetEvent(g_spReady);
    if (hwnd) {
        MSG msg;
        while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    // The class points at this DLL's window procedure: drop it so a menu loaded again after
    // End registers its own instead of reusing one that points into the unloaded DLL.
    UnregisterClassW(L"SyndicateOverlay", wc.hInstance);
    return 0;
}

static void StreamProofReleaseTargets() {
    if (g_spRTV) { g_spRTV->Release(); g_spRTV = nullptr; }
}

// Render thread. Takes the overlay down (toggle off, failure or unload).
static void StreamProofStop() {
    StreamProofReleaseTargets();
    if (g_spVisual) { g_spVisual->SetContent(nullptr); g_spVisual->Release(); g_spVisual = nullptr; }
    if (g_spTarget) { g_spTarget->SetRoot(nullptr); g_spTarget->Release(); g_spTarget = nullptr; }
    if (g_spDComp) { g_spDComp->Commit(); g_spDComp->Release(); g_spDComp = nullptr; }
    if (g_spSwap) { g_spSwap->Release(); g_spSwap = nullptr; }
    if (g_spWnd) { PostMessageW(g_spWnd, WM_CLOSE, 0, 0); g_spWnd = nullptr; }
    if (g_spThread) {
        WaitForSingleObject(g_spThread, 2000);
        CloseHandle(g_spThread);
        g_spThread = nullptr;
    }
    if (g_spReady) { CloseHandle(g_spReady); g_spReady = nullptr; }
    g_spW = g_spH = 0;
    g_spShown = false;
    g_spHiddenFromCapture = false;
}

static bool StreamProofStart() {
    if (g_spSwap)
        return true;
    if (!g_pd3dDevice || !g_hWnd)
        return false;
    if (!g_spThread) {
        g_spReady = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        g_spThread = CreateThread(nullptr, 0, StreamProofThread, nullptr, 0, nullptr);
        if (!g_spThread || WaitForSingleObject(g_spReady, 2000) != WAIT_OBJECT_0 || !g_spWnd) {
            StreamProofStop();
            return false;
        }
    }
    IDXGIDevice* dxgiDevice = nullptr;
    IDXGIAdapter* adapter = nullptr;
    IDXGIFactory2* factory = nullptr;
    bool ok = false;
    if (SUCCEEDED(g_pd3dDevice->QueryInterface(__uuidof(IDXGIDevice), (void**)&dxgiDevice)) &&
        SUCCEEDED(dxgiDevice->GetAdapter(&adapter)) &&
        SUCCEEDED(adapter->GetParent(__uuidof(IDXGIFactory2), (void**)&factory))) {
        RECT rc{};
        GetClientRect(g_hWnd, &rc);
        // A composition swapchain (flip model, premultiplied alpha) on the game's own device.
        DXGI_SWAP_CHAIN_DESC1 scd{};
        scd.Width = (UINT)(rc.right - rc.left > 1 ? rc.right - rc.left : 1);
        scd.Height = (UINT)(rc.bottom - rc.top > 1 ? rc.bottom - rc.top : 1);
        scd.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        scd.SampleDesc.Count = 1;
        scd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        scd.BufferCount = 2;
        scd.Scaling = DXGI_SCALING_STRETCH;
        scd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
        scd.AlphaMode = DXGI_ALPHA_MODE_PREMULTIPLIED;
        IDXGISwapChain1* swap = nullptr;
        HRESULT hr = factory->CreateSwapChainForComposition(g_pd3dDevice, &scd, nullptr, &swap);
        if (SUCCEEDED(hr)) {
            g_spSwap = swap; // IDXGISwapChain1 starts with IDXGISwapChain: the same pointer the Present hook sees
            hr = DCompositionCreateDevice(dxgiDevice, __uuidof(IDCompositionDevice), (void**)&g_spDComp);
            if (SUCCEEDED(hr)) hr = g_spDComp->CreateTargetForHwnd(g_spWnd, TRUE, &g_spTarget);
            if (SUCCEEDED(hr)) hr = g_spDComp->CreateVisual(&g_spVisual);
            if (SUCCEEDED(hr)) hr = g_spVisual->SetContent(swap);
            if (SUCCEEDED(hr)) hr = g_spTarget->SetRoot(g_spVisual);
            if (SUCCEEDED(hr)) hr = g_spDComp->Commit();
            if (SUCCEEDED(hr)) {
                g_spW = scd.Width;
                g_spH = scd.Height;
                ok = true;
                printf("[stream] overlay %ux%u: composition swapchain on the game's device\n", g_spW, g_spH);
            } else {
                printf("[stream] DirectComposition setup failed 0x%08X\n", (unsigned)hr);
            }
        } else {
            printf("[stream] overlay swapchain failed 0x%08X\n", (unsigned)hr);
        }
    } else {
        printf("[stream] no DXGI 1.2 factory for the game's device\n");
    }
    if (factory) factory->Release();
    if (adapter) adapter->Release();
    if (dxgiDevice) dxgiDevice->Release();
    if (!ok)
        StreamProofStop();
    return ok;
}

// Called every frame before ImGui renders. Returns true when this frame goes to the overlay.
static bool StreamProofTick(IDXGISwapChain* gameSwap) {
    const bool want = cheat.streamProof && running;
    if (!want) {
        if (g_spSwap || g_spThread)
            printf("[stream] stream proof off: drawing in the game again\n");
        StreamProofStop();
        if (running)
            StreamProofRestoreWindowMode();
        g_spFailed = false;
        g_spStatus = "Off: the menu and ESP show on stream and in recordings.";
        return false;
    }
    BOOL fullscreen = FALSE;
    if (gameSwap && SUCCEEDED(gameSwap->GetFullscreenState(&fullscreen, nullptr)) && fullscreen) {
        // Nothing can be drawn over exclusive fullscreen: move the game to windowed fullscreen (looks the same) while
        // stream proof is on. Until the switch lands, the menu stays in the game.
        StreamProofStop();
        if (GetTickCount64() >= g_spModeNextTry) {
            g_spModeNextTry = GetTickCount64() + 3000;
            const bool ok = SetGameWindowMode(1);
            if (ok)
                g_spSwitchedMode = true;
            printf("[stream] game is in exclusive fullscreen: %s\n", ok ? "switching it to windowed fullscreen" : "could not switch it to windowed fullscreen");
            g_spStatus = ok ? "Starting: switching the game from Fullscreen to Windowed Fullscreen..."
                            : "Not working: the game is in exclusive fullscreen and could not be switched. Pick Windowed Fullscreen in the game settings.";
        }
        return false;
    }
    if (g_spFailed) {
        g_spStatus = "Not working: the overlay could not start (see SyndicateMenu.log). Turn it off and on to retry.";
        return false;
    }
    if (!g_spSwap && !StreamProofStart()) {
        g_spFailed = true;
        return false;
    }
    g_spStatus = g_spHiddenFromCapture
        ? "On: the menu and ESP are hidden from OBS, Discord, Game Bar, ShadowPlay and screenshots."
        : "Partly working: your Windows is too old to hide windows from capture (needs Windows 10 2004+).";

    // Keep the overlay exactly over the game's client area, and only while the game is in front.
    RECT rc{};
    GetClientRect(g_hWnd, &rc);
    POINT origin{ 0, 0 };
    ClientToScreen(g_hWnd, &origin);
    const UINT w = (UINT)(rc.right - rc.left > 1 ? rc.right - rc.left : 1);
    const UINT h = (UINT)(rc.bottom - rc.top > 1 ? rc.bottom - rc.top : 1);
    const HWND fg = GetForegroundWindow();
    const bool show = (fg == g_hWnd || fg == g_spWnd) && !IsIconic(g_hWnd);
    static POINT lastOrigin{ -99999, -99999 };
    if (show != g_spShown || origin.x != lastOrigin.x || origin.y != lastOrigin.y || w != g_spW || h != g_spH) {
        SetWindowPos(g_spWnd, HWND_TOPMOST, origin.x, origin.y, (int)w, (int)h,
            SWP_NOACTIVATE | SWP_ASYNCWINDOWPOS | (show ? SWP_SHOWWINDOW : SWP_HIDEWINDOW));
        g_spShown = show;
        lastOrigin = origin;
    }
    if (w != g_spW || h != g_spH) {
        StreamProofReleaseTargets();
        if (FAILED(g_spSwap->ResizeBuffers(0, w, h, DXGI_FORMAT_UNKNOWN, 0))) {
            printf("[stream] overlay resize to %ux%u failed\n", w, h);
            StreamProofStop();
            g_spFailed = true;
            return false;
        }
        g_spW = w;
        g_spH = h;
    }
    if (!g_spRTV) {
        ID3D11Texture2D* buffer = nullptr;
        if (SUCCEEDED(g_spSwap->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&buffer))) {
            g_pd3dDevice->CreateRenderTargetView(buffer, nullptr, &g_spRTV);
            buffer->Release();
        }
        if (!g_spRTV) {
            StreamProofStop();
            g_spFailed = true;
            return false;
        }
    }
    return true;
}

// Draws this frame's ImGui output on the overlay and shows it.
static void StreamProofPresent(ImDrawData* drawData) {
    const float clear[4] = { 0.f, 0.f, 0.f, 0.f };
    g_pd3dContext->OMSetRenderTargets(1, &g_spRTV, nullptr);
    g_pd3dContext->ClearRenderTargetView(g_spRTV, clear);
    ImGui_ImplDX11_RenderDrawData(drawData);
    g_spSwap->Present(0, 0); // goes through HookedPresent, which passes the overlay straight on
    if (g_pRTV)
        g_pd3dContext->OMSetRenderTargets(1, &g_pRTV, nullptr);
}

const char* StreamProofStatus() { return g_spStatus; }

HRESULT WINAPI HookedPresent(IDXGISwapChain* pSwapChain, UINT SyncInterval, UINT Flags) {
    if (g_spSwap && pSwapChain == g_spSwap)
        return oPresent(pSwapChain, SyncInterval, Flags); // the stream proof overlay

    if (!running) {
        if (!g_unloadCleaned) {
            if (g_bSdk)
                SafeCleanupBeforeUnload();
            StreamProofStop();
            StreamProofRestoreWindowMode();
            g_unloadCleaned = true;
        }
        return oPresent(pSwapChain, SyncInterval, Flags);
    }

    if (!g_bImGui) {
        printf("[hooks] first Present: setting up ImGui on the game's swapchain\n");
        if (SUCCEEDED(pSwapChain->GetDevice(__uuidof(ID3D11Device), (void**)&g_pd3dDevice))) {
            g_pd3dDevice->GetImmediateContext(&g_pd3dContext);
            DXGI_SWAP_CHAIN_DESC desc{};
            pSwapChain->GetDesc(&desc);
            g_hWnd = desc.OutputWindow;
            if (g_hWnd) {
                ImGui::CreateContext();
                ImGuiIO& io = ImGui::GetIO();
                io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
                ImGui::StyleColorsDark();
                // Crosshair-only mode keeps the game's window procedure: every key (Insert and End
                // too) and click goes straight to the game, and no menu, fonts or logo are loaded.
                if (!CrosshairOnlyMode())
                    oWndProc = (WNDPROC)SetWindowLongPtrW(g_hWnd, GWLP_WNDPROC, (LONG_PTR)HookedWndProc);
                else
                    io.IniFilename = nullptr;
                ImGui_ImplWin32_Init(g_hWnd);
                ImGui_ImplDX11_Init(g_pd3dDevice, g_pd3dContext);
                if (!CrosshairOnlyMode()) {
                    InitMenu();
                    LoadSyndicateLogo(g_pd3dDevice);
                    UpdateMenuFonts();
                }
                ID3D11Texture2D* backBuffer = nullptr;
                if (SUCCEEDED(pSwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&backBuffer))) {
                    g_pd3dDevice->CreateRenderTargetView(backBuffer, nullptr, &g_pRTV);
                    backBuffer->Release();
                }
                g_bImGui = true;
                printf("[hooks] Present hooked, ImGui on game swapchain\n");
            }
        }
    }

    if (g_bImGui) {
        if (!g_pRTV) {
            ID3D11Texture2D* backBuffer = nullptr;
            if (SUCCEEDED(pSwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&backBuffer))) {
                g_pd3dDevice->CreateRenderTargetView(backBuffer, nullptr, &g_pRTV);
                backBuffer->Release();
            }
        }
        if (g_pRTV) {
            g_pd3dContext->OMSetRenderTargets(1, &g_pRTV, nullptr);
            const bool crosshairOnly = CrosshairOnlyMode();
            if (!crosshairOnly) {
                LoadSyndicateLogo(g_pd3dDevice);
                UpdateMenuFonts();
            }
            ImGui_ImplDX11_NewFrame();
            if (!crosshairOnly) {
                TickMenuCursor(ImGui::GetIO());
            } else {
                // Never touch the game's cursor: ImGui must not set or draw one.
                ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;
                ImGui::GetIO().MouseDrawCursor = false;
            }
            ImGui_ImplWin32_NewFrame();
            ImGui::NewFrame();
            DrawFrame();
            ImGui::Render();
            if (StreamProofTick(pSwapChain))
                StreamProofPresent(ImGui::GetDrawData());
            else
                ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
        }
    }

    return oPresent(pSwapChain, SyncInterval, Flags);
}

HRESULT WINAPI HookedResizeBuffers(IDXGISwapChain* pSwapChain, UINT BufferCount, UINT Width, UINT Height, DXGI_FORMAT NewFormat, UINT SwapChainFlags) {
    if (g_spSwap && pSwapChain == g_spSwap)
        return oResizeBuffers(pSwapChain, BufferCount, Width, Height, NewFormat, SwapChainFlags);
    if (g_pRTV) {
        g_pRTV->Release();
        g_pRTV = nullptr;
    }
    return oResizeBuffers(pSwapChain, BufferCount, Width, Height, NewFormat, SwapChainFlags);
}

namespace Hooks {
    bool Initialize() {
        HWND hwnd = CreateWindowA("BUTTON", "dummy", WS_SYSMENU | WS_MINIMIZEBOX, 0, 0, 100, 100, NULL, NULL, NULL, NULL);
        if (!hwnd) {
            printf("[hooks] dummy window failed\n");
            return false;
        }

        DXGI_SWAP_CHAIN_DESC scd{};
        scd.BufferCount = 1;
        scd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        scd.BufferDesc.Width = 1;
        scd.BufferDesc.Height = 1;
        scd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        scd.OutputWindow = hwnd;
        scd.SampleDesc.Count = 1;
        scd.Windowed = TRUE;
        scd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

        IDXGISwapChain* swapChain = nullptr;
        ID3D11Device* device = nullptr;
        ID3D11DeviceContext* context = nullptr;
        D3D_FEATURE_LEVEL featureLevels[] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1 };
        D3D_FEATURE_LEVEL featureLevel;

        HRESULT hr = D3D11CreateDeviceAndSwapChain(
            nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, featureLevels, 2,
            D3D11_SDK_VERSION, &scd, &swapChain, &device, &featureLevel, &context);

        if (FAILED(hr)) {
            printf("[hooks] D3D11CreateDeviceAndSwapChain failed 0x%08X\n", (unsigned)hr);
            DestroyWindow(hwnd);
            return false;
        }

        void** vtable = *(void***)swapChain;
        void* presentAddress = vtable[8];
        void* resizeBuffersAddress = vtable[13];

        swapChain->Release();
        device->Release();
        context->Release();
        DestroyWindow(hwnd);

        if (MH_Initialize() != MH_OK) {
            printf("[hooks] MH_Initialize failed\n");
            return false;
        }
        if (MH_CreateHook(presentAddress, &HookedPresent, reinterpret_cast<void**>(&oPresent)) != MH_OK) {
            printf("[hooks] Present hook create failed\n");
            return false;
        }
        if (MH_CreateHook(resizeBuffersAddress, &HookedResizeBuffers, reinterpret_cast<void**>(&oResizeBuffers)) != MH_OK) {
            printf("[hooks] ResizeBuffers hook create failed\n");
            return false;
        }
        if (MH_EnableHook(MH_ALL_HOOKS) != MH_OK) {
            printf("[hooks] MH_EnableHook failed\n");
            return false;
        }

        g_bHooks = true;
        printf("[hooks] DXGI Present hooked like OGFN\n");
        return true;
    }

    void Shutdown() {
        if (!g_bHooks) return;
        g_bHooks = false;
        MH_DisableHook(MH_ALL_HOOKS);
        MH_Uninitialize();
        if (g_hWnd && oWndProc) {
            SetWindowLongPtrW(g_hWnd, GWLP_WNDPROC, (LONG_PTR)oWndProc);
            oWndProc = nullptr;
        }
        if (g_bImGui) {
            ImGui_ImplDX11_Shutdown();
            ImGui_ImplWin32_Shutdown();
            ImGui::DestroyContext();
            g_bImGui = false;
        }
        if (g_pRTV) { g_pRTV->Release(); g_pRTV = nullptr; }
        if (g_pd3dContext) { g_pd3dContext->Release(); g_pd3dContext = nullptr; }
        if (g_pd3dDevice) { g_pd3dDevice->Release(); g_pd3dDevice = nullptr; }
    }

    bool Ready() { return g_bImGui; }

    void SetSdkReady(bool v) { g_bSdk = v; }
}