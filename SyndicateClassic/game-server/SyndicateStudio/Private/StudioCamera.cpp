#include "pch.h"
#include "../Public/StudioCamera.h"

// SyndicateStudio camera: reads the live view, and a real free camera.
//
// Free cam: spawn a CameraActor in the client's world and make it the PlayerController's view target. It works like
// the Unreal editor viewport: hold the RIGHT mouse button over the game to look around (our own mouse look: the
// cursor delta turns the camera and the cursor is put back where you pressed), WASD to move, Space/E up, C/Ctrl/Q
// down, Shift faster, mouse wheel = slower/faster. The game itself gets none of your keys, clicks or wheel while the
// free cam is on (no pickaxe swing, no building, no weapon switch), its HUD is hidden, and the character's look and
// movement input are ignored, so it stays where it was. Left clicks and drags become Studio picks (click = select
// what is under the cursor, drag = selection box) that the launcher reads with CMD_POLL_INPUT; key presses go there
// too (Delete, Ctrl+Z, Ctrl+Y, R, Esc). Everything runs on the game thread (Tick and FilterInput are called from the
// WH_GETMESSAGE hook).
//
// UE4 builds only (all Studio seasons): FVector/FRotator are three floats inside function parameters.

extern HWND StudioGetGameWindow();
extern void StudioLog(const char* Text);

namespace StudioCamera {

    // ---- Camera manager access ----

    static int32 PcCameraOff = -2;

    class CamManager : public UObject {
    public:
        DEFINE_FUNC(GetCameraLocation, FVector);
        DEFINE_FUNC(GetCameraRotation, FRotator);
        DEFINE_FUNC(GetFOVAngle, float);
    };

    static UObject* GetPC() {
        auto World = UWorld::GetWorld();
        if (!World) return nullptr;
        auto Instance = World->OwningGameInstance;
        if (!Instance || !Instance->LocalPlayers.Num()) return nullptr;
        auto Player = Instance->LocalPlayers[0];
        return Player ? (UObject*)Player->PlayerController : nullptr;
    }

    static CamManager* GetCam(UObject* PC) {
        if (PcCameraOff == -2) PcCameraOff = int32(PC->GetOffset("PlayerCameraManager"));
        if (PcCameraOff < 0) return nullptr;
        return *(CamManager**)(__int64(PC) + PcCameraOff);
    }

    // ---- Cached UFunction calls with named parameters ----

    struct Fn {
        UFunction* F = nullptr;
        uint32 Size = 0;
        int32 Off[6] = { -1, -1, -1, -1, -1, -1 };
        bool Tried = false;
    };

    // Resolve Name on Obj's class once; Params are the parameter names to find offsets for (in order).
    static Fn& Resolve(Fn& Cache, UObject* Obj, const char* Name, std::initializer_list<const char*> Params) {
        if (Cache.Tried) return Cache;
        Cache.Tried = true;
        Cache.F = Obj->GetFunction(Name);
        if (!Cache.F) return Cache;
        auto P = Cache.F->GetParamsNamed();
        Cache.Size = P.Size;
        // A wanted name may list alternatives "A|B": parameter names changed between engine versions
        // (4.16's WidgetBlueprintLibrary input modes call the controller "Target", later ones "PlayerController").
        int i = 0;
        for (auto Want : Params) {
            std::string Alts = Want;
            size_t Start = 0;
            while (Cache.Off[i] < 0 && Start <= Alts.size()) {
                size_t Bar = Alts.find('|', Start);
                std::string One = Alts.substr(Start, Bar == std::string::npos ? std::string::npos : Bar - Start);
                for (auto& M : P.NameOffsetMap) if (M.Name == One.c_str()) { Cache.Off[i] = int32(M.Offset); break; }
                if (Bar == std::string::npos) break;
                Start = Bar + 1;
            }
            i++;
        }
        return Cache;
    }

    // _Impl functions may use C++ objects (Resolve builds a parameter list); the _Safe wrappers hold the __try.
    static Fn FnSetViewTarget, FnGetControlRotation, FnSetControlRotation, FnIgnoreMove, FnResetIgnoreMove,
              FnSetLocRot, FnDestroy, FnGetViewTarget, FnModeGameUI, FnModeGame, FnIgnoreLook, FnResetIgnoreLook,
              FnInViewport, FnGetVisibility, FnSetVisibility;

    class PCBits : public UObject {
    public:
        DEFINE_BITFIELD_PROP(bShowMouseCursor);
    };

    static void PutVec(uint8_t* Buf, int32 Off, float a, float b, float c) {
        if (Off < 0) return;
        float v[3] = { a, b, c }; memcpy(Buf + Off, v, 12);
    }

    // ---- Free cam state ----

    static bool FreeCamOn = false;
    static AActor* CamActor = nullptr;
    static UWorld* CamWorld = nullptr;
    static UObject* CamPC = nullptr;
    static float CamPos[3] = {};
    static float CamRot[3] = {};      // pitch, yaw, roll
    static float Speed = 3000.f;      // units per second (from the Studio's fly-speed file, see ReadSpeed)
    static float SpeedMul = 1.f;      // mouse wheel in the game: x1.25 per notch
    static bool Looking = false;      // right mouse button held: mouse look
    static POINT LookAnchor{};
    static LARGE_INTEGER LastQpc{}, QpcFreq{};
    static ULONGLONG NextSpeedRead = 0, NextViewCheck = 0;

    static void ReadSpeed() {
        if (GetTickCount64() < NextSpeedRead) return;
        NextSpeedRead = GetTickCount64() + 1000;
        wchar_t Base[MAX_PATH]{};
        if (!GetEnvironmentVariableW(L"LOCALAPPDATA", Base, MAX_PATH)) return;
        std::wstring File = std::wstring(Base) + L"\\ProjectTilted\\dev-fly-speed.txt";
        FILE* In = nullptr;
        if (_wfopen_s(&In, File.c_str(), L"r") == 0 && In) {
            // The Studio's fly-speed slider (1000-120000) is tuned for flying a character; a camera wants far less.
            // Slider max (120000) -> 3000 units/s (30 m/s; Shift x4). Lower the Studio's Speed slider to go slower.
            float V = 0; if (fscanf_s(In, "%f", &V) == 1 && V >= 500.f) Speed = V / 40.f;
            fclose(In);
        }
        if (Speed < 300.f) Speed = 300.f;
        if (Speed > 6000.f) Speed = 6000.f;
    }

    // ---- Engine calls ----

    static bool SetViewTarget_Impl(UObject* PC, AActor* Target) {
        auto& C = Resolve(FnSetViewTarget, PC, "SetViewTargetWithBlend", { "NewViewTarget" });
        if (!C.F || C.Off[0] < 0 || C.Size > 64) return false;
        alignas(16) uint8_t Buf[64]{};
        *(AActor**)(Buf + C.Off[0]) = Target;
        PC->ProcessEvent(C.F, Buf);
        return true;
    }
    static bool SetViewTarget_Safe(UObject* PC, AActor* Target) {
        __try { return SetViewTarget_Impl(PC, Target); } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    }

    static AActor* GetViewTarget_Impl(UObject* PC) {
        auto& C = Resolve(FnGetViewTarget, PC, "GetViewTarget", { "ReturnValue" });
        if (!C.F || C.Off[0] < 0 || C.Size > 64) return nullptr;
        alignas(16) uint8_t Buf[64]{};
        PC->ProcessEvent(C.F, Buf);
        return *(AActor**)(Buf + C.Off[0]);
    }
    static AActor* GetViewTarget_Safe(UObject* PC) {
        __try { return GetViewTarget_Impl(PC); } __except (EXCEPTION_EXECUTE_HANDLER) { return nullptr; }
    }

    static bool GetControlRotation_Impl(UObject* PC, float* Out3) {
        auto& C = Resolve(FnGetControlRotation, PC, "GetControlRotation", { "ReturnValue" });
        if (!C.F || C.Off[0] < 0 || C.Size > 64) return false;
        alignas(16) uint8_t Buf[64]{};
        PC->ProcessEvent(C.F, Buf);
        memcpy(Out3, Buf + C.Off[0], 12);
        return true;
    }
    static bool GetControlRotation_Safe(UObject* PC, float* Out3) {
        __try { return GetControlRotation_Impl(PC, Out3); } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    }

    static bool SetControlRotation_Impl(UObject* PC, float Pitch, float Yaw) {
        auto& C = Resolve(FnSetControlRotation, PC, "SetControlRotation", { "NewRotation" });
        if (!C.F || C.Off[0] < 0 || C.Size > 64) return false;
        alignas(16) uint8_t Buf[64]{};
        PutVec(Buf, C.Off[0], Pitch, Yaw, 0.f);
        PC->ProcessEvent(C.F, Buf);
        return true;
    }
    static bool SetControlRotation_Safe(UObject* PC, float Pitch, float Yaw) {
        __try { return SetControlRotation_Impl(PC, Pitch, Yaw); } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    }

    static bool IgnoreMove_Impl(UObject* PC, bool Ignore) {
        alignas(16) uint8_t Buf[64]{};
        if (Ignore) {
            auto& C = Resolve(FnIgnoreMove, PC, "SetIgnoreMoveInput", { "bNewMoveInput" });
            if (!C.F || C.Off[0] < 0 || C.Size > 64) return false;
            Buf[C.Off[0]] = 1;
            PC->ProcessEvent(C.F, Buf);
        } else {
            auto& C = Resolve(FnResetIgnoreMove, PC, "ResetIgnoreMoveInput", {});
            if (!C.F || C.Size > 64) return false;
            PC->ProcessEvent(C.F, Buf);
        }
        return true;
    }
    static bool IgnoreMove_Safe(UObject* PC, bool Ignore) {
        __try { return IgnoreMove_Impl(PC, Ignore); } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    }

    // The character keeps facing where it was while you look around with the free cam
    static bool IgnoreLook_Impl(UObject* PC, bool Ignore) {
        alignas(16) uint8_t Buf[64]{};
        if (Ignore) {
            auto& C = Resolve(FnIgnoreLook, PC, "SetIgnoreLookInput", { "bNewLookInput" });
            if (!C.F || C.Off[0] < 0 || C.Size > 64) return false;
            Buf[C.Off[0]] = 1;
            PC->ProcessEvent(C.F, Buf);
        } else {
            auto& C = Resolve(FnResetIgnoreLook, PC, "ResetIgnoreLookInput", {});
            if (!C.F || C.Size > 64) return false;
            PC->ProcessEvent(C.F, Buf);
        }
        return true;
    }
    static bool IgnoreLook_Safe(UObject* PC, bool Ignore) {
        __try { return IgnoreLook_Impl(PC, Ignore); } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    }

    // ---- HUD: hidden while the free cam is on ----
    // Fortnite's HUD (reticle, hotbar, materials, build bar, health, minimap) is UMG widgets in the viewport. Every
    // UserWidget that is in the viewport and visible is set to Hidden, and put back to what it was when the free cam
    // goes off. New widgets (the game makes some on the fly) are caught by a rescan every second. AHUD::bShowHUD is
    // cleared too, for anything drawn on the canvas.
    struct HiddenWidget { UObject* Widget; int32 Index; uint8_t Was; };
    static std::vector<HiddenWidget> HiddenWidgets;
    static ULONGLONG NextHudScan = 0;
    static int HudWasShown = -1;

    class HUDBits : public UObject {
    public:
        DEFINE_BITFIELD_PROP(bShowHUD);
    };

    static UObject* GetHud(UObject* PC) {
        static int32 HudOff = -2;
        if (HudOff == -2) HudOff = int32(PC->GetOffset("MyHUD"));
        return HudOff >= 0 ? *(UObject**)(__int64(PC) + HudOff) : nullptr;
    }

    static void ShowHudFlag_Impl(UObject* PC, bool Show) {
        auto Hud = (HUDBits*)GetHud(PC);
        if (!Hud || !Hud->HasbShowHUD()) return;
        if (!Show) { if (HudWasShown < 0) HudWasShown = Hud->GetbShowHUD() ? 1 : 0; Hud->SetbShowHUD(false); }
        else if (HudWasShown >= 0) { Hud->SetbShowHUD(HudWasShown == 1); HudWasShown = -1; }
    }
    static void ShowHudFlag_Safe(UObject* PC, bool Show) {
        __try { ShowHudFlag_Impl(PC, Show); } __except (EXCEPTION_EXECUTE_HANDLER) {}
    }

    static bool IsUserWidget_Safe(const UObject* Obj, const UClass* WidgetClass) {
        __try { return Obj && !Obj->IsDefaultObject() && Obj->IsA(WidgetClass); } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    }

    // Returns the widget's visibility before (0-4), or -1 if it was left alone (not in the viewport, already hidden)
    static int HideWidget_Impl(UObject* W) {
        auto& In = Resolve(FnInViewport, W, "IsInViewport", { "ReturnValue" });
        auto& Get = Resolve(FnGetVisibility, W, "GetVisibility", { "ReturnValue" });
        auto& Set = Resolve(FnSetVisibility, W, "SetVisibility", { "InVisibility" });
        if (!In.F || !Get.F || !Set.F || In.Off[0] < 0 || Get.Off[0] < 0 || Set.Off[0] < 0 || In.Size > 32 || Get.Size > 32 || Set.Size > 32) return -1;
        alignas(16) uint8_t Buf[32]{};
        W->ProcessEvent(In.F, Buf);
        if (!Buf[In.Off[0]]) return -1;
        memset(Buf, 0, sizeof(Buf));
        W->ProcessEvent(Get.F, Buf);
        uint8_t Was = Buf[Get.Off[0]];
        if (Was == 1 || Was == 2) return -1;             // Collapsed / Hidden already
        memset(Buf, 0, sizeof(Buf));
        Buf[Set.Off[0]] = 2;                              // ESlateVisibility::Hidden
        W->ProcessEvent(Set.F, Buf);
        return Was;
    }
    static int HideWidget_Safe(UObject* W) {
        __try { return HideWidget_Impl(W); } __except (EXCEPTION_EXECUTE_HANDLER) { return -1; }
    }

    static void RestoreWidget_Impl(UObject* W, uint8_t Was) {
        auto& Set = Resolve(FnSetVisibility, W, "SetVisibility", { "InVisibility" });
        if (!Set.F || Set.Off[0] < 0 || Set.Size > 32) return;
        alignas(16) uint8_t Buf[32]{};
        Buf[Set.Off[0]] = Was;
        W->ProcessEvent(Set.F, Buf);
    }
    static void RestoreWidget_Safe(UObject* W, uint8_t Was) {
        __try { RestoreWidget_Impl(W, Was); } __except (EXCEPTION_EXECUTE_HANDLER) {}
    }

    // Is this object still the live object it was (not freed and its slot reused)?
    static bool StillAlive_Safe(UObject* Obj, int32 Index) {
        __try { return Index >= 0 && Index < TUObjectArray::Num() && TUObjectArray::GetObjectByIndex(Index) == Obj; }
        __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    }

    static void HideHud(UObject* PC) {
        ShowHudFlag_Safe(PC, false);
        static const UClass* WidgetClass = nullptr;
        if (!WidgetClass) WidgetClass = FindClass("UserWidget");
        if (!WidgetClass) return;
        int Hidden = 0;
        const int32 Count = TUObjectArray::Num();
        for (int32 i = 0; i < Count; i++) {
            auto Obj = (UObject*)TUObjectArray::GetObjectByIndex(i);
            if (!IsUserWidget_Safe(Obj, WidgetClass)) continue;
            bool Known = false;
            for (auto& H : HiddenWidgets) if (H.Widget == Obj) { Known = true; break; }
            int Was = HideWidget_Safe(Obj);
            if (Was < 0) continue;
            Hidden++;
            if (!Known) HiddenWidgets.push_back({ Obj, i, (uint8_t)Was });
        }
        static bool Logged = false;
        if (!Logged && Hidden) { Logged = true; char Line[96]; snprintf(Line, sizeof(Line), "free cam: HUD hidden (%d widgets)", Hidden); StudioLog(Line); }
    }

    static void RestoreHud(UObject* PC, bool SameWorld) {
        if (PC && SameWorld) ShowHudFlag_Safe(PC, true);
        else HudWasShown = -1;
        if (SameWorld)
            for (auto& H : HiddenWidgets)
                if (StillAlive_Safe(H.Widget, H.Index)) RestoreWidget_Safe(H.Widget, H.Was);
        HiddenWidgets.clear();
    }

    static bool PlaceCam_Impl(AActor* Cam, const float* Pos, const float* Rot) {
        auto& C = Resolve(FnSetLocRot, (UObject*)Cam, "K2_SetActorLocationAndRotation", { "NewLocation", "NewRotation", "bTeleport" });
        if (!C.F || C.Off[0] < 0 || C.Off[1] < 0 || C.Size > 512) return false;
        alignas(16) uint8_t Buf[512]{};
        PutVec(Buf, C.Off[0], Pos[0], Pos[1], Pos[2]);
        PutVec(Buf, C.Off[1], Rot[0], Rot[1], Rot[2]);
        if (C.Off[2] >= 0) Buf[C.Off[2]] = 1;
        ((UObject*)Cam)->ProcessEvent(C.F, Buf);
        return true;
    }
    static bool PlaceCam_Safe(AActor* Cam, const float* Pos, const float* Rot) {
        __try { return PlaceCam_Impl(Cam, Pos, Rot); } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    }

    static void DestroyCam_Impl(AActor* Cam) {
        auto& C = Resolve(FnDestroy, (UObject*)Cam, "K2_DestroyActor", {});
        if (!C.F || C.Size > 32) return;
        alignas(16) uint8_t Buf[32]{};
        ((UObject*)Cam)->ProcessEvent(C.F, Buf);
    }
    static void DestroyCam_Safe(AActor* Cam) {
        __try { DestroyCam_Impl(Cam); } __except (EXCEPTION_EXECUTE_HANDLER) {}
    }

    static AActor* SpawnCam_Safe(const UClass* Class) {
        __try {
            FVector Loc = { 0, 0, 0 };
            FRotator Rot; memset(&Rot, 0, sizeof(Rot));
            return UWorld::SpawnActor<AActor>(Class, Loc, Rot);
        }
        __except (EXCEPTION_EXECUTE_HANDLER) { return nullptr; }
    }

    static bool ReadCamera_Safe(CamManager* Cam, CameraState* Out) {
        __try {
            auto Loc = Cam->GetCameraLocation();
            auto Rot = Cam->GetCameraRotation();
            Out->X = (float)Loc.X; Out->Y = (float)Loc.Y; Out->Z = (float)Loc.Z;
            Out->Pitch = (float)Rot.Pitch; Out->Yaw = (float)Rot.Yaw; Out->Roll = (float)Rot.Roll;
            Out->FOV = Cam->GetFOVAngle();
            Out->Valid = true;
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    }

    // ---- Edit cursor vs mouse look ----
    // Free cam is for editing, so the cursor stays visible and free (you can click the Studio gizmo over the game)
    // and holding the RIGHT mouse button turns the camera, like the Unreal editor. Fortnite normally hides and locks
    // the cursor in a match; WidgetBlueprintLibrary's input modes switch that per frame.
    static UObject* WidgetLib() {
        static UObject* Lib = nullptr; static bool Tried = false;
        if (!Tried) { Tried = true; if (auto C = FindClass("WidgetBlueprintLibrary")) Lib = C->GetDefaultObj(); }
        return Lib;
    }

    static bool SetCursorMode_Impl(UObject* PC, bool Edit) {
        auto Lib = WidgetLib();
        if (!Lib) return false;
        alignas(16) uint8_t Buf[64]{};
        if (Edit) {
            // 4.16 has SetInputMode_GameAndUI; later builds add ...Ex. Zeroed params = don't lock, don't hide.
            auto& C = FnModeGameUI.Tried ? FnModeGameUI : Resolve(FnModeGameUI, Lib, "SetInputMode_GameAndUIEx", { "PlayerController|Target" });
            if (!C.F) { FnModeGameUI = Fn{}; Resolve(FnModeGameUI, Lib, "SetInputMode_GameAndUI", { "PlayerController|Target" }); }
            if (!FnModeGameUI.F || FnModeGameUI.Off[0] < 0 || FnModeGameUI.Size > 64) return false;
            *(UObject**)(Buf + FnModeGameUI.Off[0]) = PC;
            Lib->ProcessEvent(FnModeGameUI.F, Buf);
        } else {
            auto& C = Resolve(FnModeGame, Lib, "SetInputMode_GameOnly", { "PlayerController|Target" });
            if (!C.F || C.Off[0] < 0 || C.Size > 64) return false;
            *(UObject**)(Buf + C.Off[0]) = PC;
            Lib->ProcessEvent(C.F, Buf);
        }
        auto Bits = (PCBits*)PC;
        if (Bits->HasbShowMouseCursor()) Bits->SetbShowMouseCursor(Edit);
        return true;
    }
    static bool SetCursorMode_Safe(UObject* PC, bool Edit) {
        __try { return SetCursorMode_Impl(PC, Edit); } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    }

    static int CursorApplied = -1;   // -1 unknown, 0 look, 1 edit
    static bool LeftWasDown = false, RightWasDown = false;
    static ULONGLONG NextCursorFix = 0;
    static bool CursorLogged = false;

    // Does the game currently show the cursor? (-1 = can't tell)
    static int CursorShown_Impl(UObject* PC) {
        auto Bits = (PCBits*)PC;
        if (!Bits->HasbShowMouseCursor()) return -1;
        return Bits->GetbShowMouseCursor() ? 1 : 0;
    }
    static int CursorShown_Safe(UObject* PC) {
        __try { return CursorShown_Impl(PC); } __except (EXCEPTION_EXECUTE_HANDLER) { return -1; }
    }

    // ---- Mouse look (right button) ----

    static bool Key(int Vk) { return (GetAsyncKeyState(Vk) & 0x8000) != 0; }

    // Is this screen point over the game's client area?
    static bool OverGame(HWND Game, POINT Screen) {
        if (!Game) return false;
        RECT R{};
        if (!GetClientRect(Game, &R)) return false;
        POINT TL{ R.left, R.top }, BR{ R.right, R.bottom };
        ClientToScreen(Game, &TL); ClientToScreen(Game, &BR);
        return Screen.x >= TL.x && Screen.x < BR.x && Screen.y >= TL.y && Screen.y < BR.y;
    }

    // The Studio docks the game inside its own window, so the foreground window is the Studio's, not the game's
    static bool StudioOrGameInFront(HWND Game) {
        if (!Game) return false;
        HWND Front = GetForegroundWindow();
        return Front && (Front == Game || Front == GetAncestor(Game, GA_ROOT));
    }

    static void EndLook() {
        if (!Looking) return;
        Looking = false;
        SetCursorPos(LookAnchor.x, LookAnchor.y);
        HWND Game = StudioGetGameWindow();
        if (Game && GetCapture() == Game && !Key(VK_LBUTTON)) ReleaseCapture();
    }

    // ---- Picks: left click = select, left drag = selection box (game client pixels = viewport pixels) ----

    static std::vector<InputEvent> Events;
    static bool PickDown = false;
    static POINT PickStart{}, PickNow{};
    static uint8_t PickMods = 0;

    static uint8_t Mods() {
        return uint8_t((GetKeyState(VK_SHIFT) < 0 ? 1 : 0) | (GetKeyState(VK_CONTROL) < 0 ? 2 : 0) | (GetKeyState(VK_MENU) < 0 ? 4 : 0));
    }

    static void Push(const InputEvent& E) {
        if (Events.size() >= 64) Events.erase(Events.begin());
        Events.push_back(E);
    }

    static POINT CursorInGame(HWND Game) {
        POINT P{}; GetCursorPos(&P);
        if (Game) ScreenToClient(Game, &P);
        return P;
    }

    static void EndPick(bool Report) {
        if (!PickDown) return;
        PickDown = false;
        HWND Game = StudioGetGameWindow();
        if (Game && GetCapture() == Game && !Looking) ReleaseCapture();
        if (!Report) return;
        InputEvent E{};
        E.Mods = PickMods;
        LONG Dx = PickNow.x - PickStart.x, Dy = PickNow.y - PickStart.y;
        if (Dx * Dx + Dy * Dy < 6 * 6) {
            E.Type = 1;
            E.X0 = E.X1 = float(PickStart.x); E.Y0 = E.Y1 = float(PickStart.y);
        } else {
            E.Type = 2;
            E.X0 = float((std::min)(PickStart.x, PickNow.x)); E.Y0 = float((std::min)(PickStart.y, PickNow.y));
            E.X1 = float((std::max)(PickStart.x, PickNow.x)); E.Y1 = float((std::max)(PickStart.y, PickNow.y));
        }
        Push(E);
    }

    bool FilterInput(MSG* M, bool Removing) {
        if (!FreeCamOn || !M) return false;
        const UINT Msg = M->message;
        const bool KeyMsg = Msg >= WM_KEYFIRST && Msg <= WM_KEYLAST;
        const bool MouseMsg = Msg >= WM_LBUTTONDOWN && Msg <= 0x020E; // buttons, wheel, X buttons, horizontal wheel
        if (!KeyMsg && !MouseMsg) return false;
        // Alt+F4 still closes the game
        if ((Msg == WM_SYSKEYDOWN || Msg == WM_SYSKEYUP) && M->wParam == VK_F4) return false;
        if (!Removing) { M->message = WM_NULL; return true; } // a peek: hide it, act on it once when it is really taken off the queue

        HWND Game = StudioGetGameWindow();
        switch (Msg) {
        case WM_LBUTTONDOWN:
        case WM_LBUTTONDBLCLK:
            if (!Looking && Game) {
                POINT P{ (int)(short)LOWORD(M->lParam), (int)(short)HIWORD(M->lParam) };
                if (M->hwnd && M->hwnd != Game) MapWindowPoints(M->hwnd, Game, &P, 1);
                PickDown = true; PickStart = P; PickNow = P; PickMods = Mods();
                SetCapture(Game); // the button-up and the drag come back here even over the Studio's overlay
            }
            break;
        case WM_LBUTTONUP:
            if (PickDown && Game) {
                POINT P{ (int)(short)LOWORD(M->lParam), (int)(short)HIWORD(M->lParam) };
                if (M->hwnd && M->hwnd != Game) MapWindowPoints(M->hwnd, Game, &P, 1);
                PickNow = P;
                EndPick(true);
            }
            break;
        case WM_MOUSEWHEEL: {
            int Notches = (short)HIWORD(M->wParam) / WHEEL_DELTA;
            for (; Notches > 0; Notches--) SpeedMul *= 1.25f;
            for (; Notches < 0; Notches++) SpeedMul /= 1.25f;
            if (SpeedMul < 0.1f) SpeedMul = 0.1f;
            if (SpeedMul > 10.f) SpeedMul = 10.f;
            break;
        }
        case WM_KEYDOWN:
        case WM_SYSKEYDOWN:
            if (!(M->lParam & (1 << 30)) && !Looking) { // first press only, and not while flying around
                InputEvent E{}; E.Type = 3; E.Mods = Mods(); E.Vk = uint16_t(M->wParam);
                Push(E);
            }
            break;
        }
        M->message = WM_NULL;
        return true;
    }

    bool GetPickBox(float* Out4) {
        if (!PickDown) return false;
        Out4[0] = float((std::min)(PickStart.x, PickNow.x)); Out4[1] = float((std::min)(PickStart.y, PickNow.y));
        Out4[2] = float((std::max)(PickStart.x, PickNow.x)); Out4[3] = float((std::max)(PickStart.y, PickNow.y));
        return true;
    }

    std::vector<InputEvent> TakeInputEvents() {
        std::vector<InputEvent> Out;
        Out.swap(Events);
        return Out;
    }

    // ---- Public API ----

    CameraState GetCamera() {
        CameraState state{ 0, 0, 0, 0, 0, 0, 90.f, false };
        auto PC = GetPC();
        if (!PC) return state;
        auto Cam = GetCam(PC);
        if (!Cam) return state;
        if (!ReadCamera_Safe(Cam, &state)) { CameraState none{ 0, 0, 0, 0, 0, 0, 90.f, false }; return none; }
        return state;
    }

    // Move the free cam (e.g. Studio "Go to"). The ControlRotation follows too, so Play here faces the same way.
    void SetCamera(float x, float y, float z, float pitch, float yaw, float roll) {
        if (!FreeCamOn) return;
        CamPos[0] = x; CamPos[1] = y; CamPos[2] = z;
        if (CamPC) SetControlRotation_Safe(CamPC, pitch, yaw);
        CamRot[0] = pitch; CamRot[1] = yaw; CamRot[2] = 0.f;
        if (CamActor) PlaceCam_Safe(CamActor, CamPos, CamRot);
    }

    static void TurnOff(const char* Why) {
        const bool SameWorld = CamWorld && CamWorld == UWorld::GetWorld();
        if (CamPC && SameWorld) {
            // Back to the character: the controller's pawn is the usual view target
            static int32 PawnOff = -2;
            if (PawnOff == -2) PawnOff = int32(CamPC->GetOffset("Pawn"));
            AActor* Pawn = PawnOff >= 0 ? *(AActor**)(__int64(CamPC) + PawnOff) : nullptr;
            if (Pawn) SetViewTarget_Safe(CamPC, Pawn);
            // Play here: the character looks where the camera looked
            SetControlRotation_Safe(CamPC, CamRot[0], CamRot[1]);
            IgnoreMove_Safe(CamPC, false);
            IgnoreLook_Safe(CamPC, false);
            SetCursorMode_Safe(CamPC, false); // back to the normal locked, hidden cursor
        }
        RestoreHud(CamPC, SameWorld);
        EndLook();
        EndPick(false);
        if (CamActor && SameWorld) DestroyCam_Safe(CamActor);
        CamActor = nullptr; CamPC = nullptr; CamWorld = nullptr;
        FreeCamOn = false; CursorApplied = -1; SpeedMul = 1.f;
        char Line[128]; snprintf(Line, sizeof(Line), "free cam: off (%s)", Why); StudioLog(Line);
    }

    bool SetFreeCam(bool enable) {
        if (!enable) { if (FreeCamOn) TurnOff("Studio"); return true; }
        if (FreeCamOn) return true;

        auto PC = GetPC();
        if (!PC) { StudioLog("free cam: no player controller yet"); return false; }
        auto View = GetCamera();
        static const UClass* CamClass = nullptr;
        if (!CamClass) CamClass = FindClass("CameraActor");
        if (!CamClass) { StudioLog("free cam: CameraActor class not found"); return false; }
        auto Cam = SpawnCam_Safe(CamClass);
        if (!Cam) { StudioLog("free cam: could not spawn the camera"); return false; }

        CamPos[0] = View.X; CamPos[1] = View.Y; CamPos[2] = View.Z;
        float Ctrl[3]{};
        if (GetControlRotation_Safe(PC, Ctrl)) { CamRot[0] = Ctrl[0]; CamRot[1] = Ctrl[1]; }
        else { CamRot[0] = View.Pitch; CamRot[1] = View.Yaw; }
        CamRot[2] = 0.f;
        PlaceCam_Safe(Cam, CamPos, CamRot);
        if (!SetViewTarget_Safe(PC, Cam)) { DestroyCam_Safe(Cam); StudioLog("free cam: SetViewTargetWithBlend missing"); return false; }
        IgnoreMove_Safe(PC, true);
        IgnoreLook_Safe(PC, true);

        CamActor = Cam; CamPC = PC; CamWorld = UWorld::GetWorld();
        FreeCamOn = true;
        QueryPerformanceFrequency(&QpcFreq); QueryPerformanceCounter(&LastQpc);
        NextViewCheck = GetTickCount64() + 500;
        NextHudScan = 0; // hide the HUD on the first tick
        char Line[160]; snprintf(Line, sizeof(Line), "free cam: on at (%.0f,%.0f,%.0f)", CamPos[0], CamPos[1], CamPos[2]); StudioLog(Line);
        return true;
    }

    bool IsFreeCam() { return FreeCamOn; }

    void Tick() {
        if (!FreeCamOn) return;
        if (UWorld::GetWorld() != CamWorld || GetPC() != CamPC) { TurnOff("world changed"); return; }

        LARGE_INTEGER Now; QueryPerformanceCounter(&Now);
        float Dt = QpcFreq.QuadPart ? float(double(Now.QuadPart - LastQpc.QuadPart) / double(QpcFreq.QuadPart)) : 0.016f;
        LastQpc = Now;
        if (Dt > 0.1f) Dt = 0.1f;
        ReadSpeed();

        // HUD: hidden while the free cam is on (rescanned: the game makes some widgets later)
        if (GetTickCount64() >= NextHudScan) { NextHudScan = GetTickCount64() + 1000; HideHud(CamPC); }

        HWND Game = StudioGetGameWindow();
        const bool Front = StudioOrGameInFront(Game);
        POINT Cursor{}; GetCursorPos(&Cursor);
        const bool Over = Front && OverGame(Game, Cursor);

        // Edit cursor: always free and visible in free cam (the game never sees the clicks, so it can't grab it)
        const bool LeftDown = Key(VK_LBUTTON);
        bool Apply = CursorApplied != 1 || (LeftWasDown && !LeftDown && Front);
        if (!Apply && GetTickCount64() >= NextCursorFix && CursorShown_Safe(CamPC) == 0) Apply = true;
        if (Apply) {
            NextCursorFix = GetTickCount64() + 150;
            bool Ok = SetCursorMode_Safe(CamPC, true);
            if (Ok) CursorApplied = 1;
            static int Fails = 0;
            if (!Ok) Fails++;
            if (!CursorLogged || (!Ok && Fails <= 3)) {
                CursorLogged = true;
                char Line[160];
                snprintf(Line, sizeof(Line), "free cam: edit cursor -> %s (input-mode fn %s, controller param %d, size %u, cursor flag %d)", Ok ? "ok" : "FAILED",
                    FnModeGameUI.F ? "found" : "missing", FnModeGameUI.Off[0], FnModeGameUI.Size, CursorShown_Safe(CamPC));
                StudioLog(Line);
            }
        }
        LeftWasDown = LeftDown;

        // Look: hold the right button over the game. The cursor delta turns the camera and the cursor goes back to
        // where you pressed, like the Unreal editor viewport.
        const bool RightDown = Key(VK_RBUTTON);
        if (!Looking && RightDown && !RightWasDown && Over && !PickDown) {
            Looking = true; LookAnchor = Cursor;
            if (Game) SetCapture(Game);
        }
        if (Looking && !RightDown) EndLook();
        RightWasDown = RightDown;
        if (Looking) {
            const float Sens = 0.15f; // degrees per pixel
            LONG Dx = Cursor.x - LookAnchor.x, Dy = Cursor.y - LookAnchor.y;
            if (Dx || Dy) {
                CamRot[1] += Dx * Sens;
                CamRot[0] -= Dy * Sens;
                if (CamRot[0] > 89.f) CamRot[0] = 89.f;
                if (CamRot[0] < -89.f) CamRot[0] = -89.f;
                if (CamRot[1] > 360.f) CamRot[1] -= 360.f;
                if (CamRot[1] < -360.f) CamRot[1] += 360.f;
                SetCursorPos(LookAnchor.x, LookAnchor.y);
            }
        }

        // Selection box follows the cursor; a lost button-up still ends it
        if (PickDown) {
            PickNow = CursorInGame(Game);
            if (!LeftDown) EndPick(true);
        }

        // Even with the cursor shown, Fortnite keeps the Windows mouse capture on its window, so a click anywhere (the
        // Studio, the gizmo) still goes to the game. Let go of it while you aren't holding a button in the game.
        // ClipCursor(nullptr) lifts any "keep the cursor inside" clip.
        if (CursorApplied == 1 && !LeftDown && !RightDown && !Key(VK_MBUTTON) && !Looking && !PickDown) {
            if (Game && GetCapture() == Game) ReleaseCapture();
            RECT Clip{};
            if (GetClipCursor(&Clip)) {
                RECT Screen{ GetSystemMetrics(SM_XVIRTUALSCREEN), GetSystemMetrics(SM_YVIRTUALSCREEN),
                             GetSystemMetrics(SM_XVIRTUALSCREEN) + GetSystemMetrics(SM_CXVIRTUALSCREEN),
                             GetSystemMetrics(SM_YVIRTUALSCREEN) + GetSystemMetrics(SM_CYVIRTUALSCREEN) };
                if (Clip.left > Screen.left || Clip.top > Screen.top || Clip.right < Screen.right || Clip.bottom < Screen.bottom) ClipCursor(nullptr);
            }
        }

        // Move: while looking, or with the cursor over the game (never while you type in the Studio's panels)
        if (Looking || Over) {
            float F = 0, R = 0, U = 0;
            if (Key('W')) F += 1;
            if (Key('S')) F -= 1;
            if (Key('D')) R += 1;
            if (Key('A')) R -= 1;
            if (Key(VK_SPACE) || Key('E')) U += 1;
            if (Key('C') || Key('Q') || (Key(VK_CONTROL) && Looking)) U -= 1; // Ctrl alone is for Ctrl+Z / Ctrl+Y
            if (F != 0 || R != 0 || U != 0) {
                float Mult = (Key(VK_SHIFT) ? 4.f : 1.f) * SpeedMul;
                const float D2R = 3.14159265f / 180.f;
                float P = CamRot[0] * D2R, Y = CamRot[1] * D2R;
                float Fwd[3] = { cosf(P) * cosf(Y), cosf(P) * sinf(Y), sinf(P) };
                float Rgt[3] = { -sinf(Y), cosf(Y), 0.f };
                float Step = Speed * Mult * Dt;
                for (int i = 0; i < 3; i++) CamPos[i] += (Fwd[i] * F + Rgt[i] * R) * Step;
                CamPos[2] += U * Step;
            }
        }
        PlaceCam_Safe(CamActor, CamPos, CamRot);

        // The game can hand the view back to the character (respawn, emotes, menus): take it again
        if (GetTickCount64() >= NextViewCheck) {
            NextViewCheck = GetTickCount64() + 500;
            if (GetViewTarget_Safe(CamPC) != CamActor) SetViewTarget_Safe(CamPC, CamActor);
        }
    }
}
