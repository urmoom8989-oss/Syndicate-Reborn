#include "pch.h"
#include <cmath>
#include <string>
#include <vector>
#include <unordered_map>
#include <cstdlib>
#include <cctype>
#include <cstdarg>
#include <cwctype>
#include <cstring>
#include "MouseCoalesce.h"

// SyndicateClient.dll: client-side mods the launcher loads into Fortnite when one of them is turned on.
//   * Custom field of view, every season (below).
//   * Turbo build, every season (Pre-season to Season 2 have no turbo building): see TurboScan().
//   * Edit on release, every season: see EditTick().
// What it does is written to %LOCALAPPDATA%\ProjectTilted\syndicate-client.log (the folder keeps its old name:
// the prebuilt SyndicatePadAssist.dll reads it).
//
// Custom field of view (Pre-season 1.7.2 to Season X 10.31).
//
// The launcher writes the wanted FOV (degrees) to %LOCALAPPDATA%\ProjectTilted\fov.txt and loads this
// module into Fortnite when a value is set. The module reads that file once a second, so a new value
// from the launcher applies straight away.
//
// How it works: the engine draws the local player's view with APlayerCameraManager::GetFOVAngle(),
// which returns LockedFOV when it is above zero and the camera mode's FOV otherwise (that is how the
// engine's own "fov" console command works). LockedFOV is not a reflected property; it sits right
// after DefaultFOV, so its offset is DefaultFOV + 4. That is only trusted when DefaultOrthoWidth sits
// exactly 8 bytes after DefaultFOV (the layout DefaultFOV, LockedFOV, DefaultOrthoWidth); otherwise
// the module does nothing and says so in syndicate-client.log.
//
// Aiming down sights and scopes: the camera mode lowers its own FOV while you aim. When the camera's
// FOV drops below 92% of its normal (hip) value, the lock is released so the game's zoom works, and it
// is set again when you stop aiming.
//
// Everything that touches game objects runs on the game thread: a WH_GETMESSAGE hook on the game window
// runs Apply() when this module posts its message (about 60 times a second).
namespace {
    constexpr UINT ApplyMessage = WM_APP + 0x4F0;
    constexpr UINT ConsoleMessage = WM_APP + 0x4F1; // the in-game console tick (once a second), see ConsoleTick()
    HMODULE SelfModule = nullptr;
    HWND GameWindow = nullptr;
    HHOOK MessageHook = nullptr;

    // Dev input (fly, turbo, edit-on-release) is accepted when the game is the foreground window, OR when the game is
    // docked in Syndicate Studio and the Studio launcher that owns it is foreground. This makes the controls work while
    // the game is embedded in the Studio (previously they were dead because the launcher, not the game, had focus).
    // Any other app in front still blocks input, so our keys never fire while you are in a different program.
    bool GameHasInput() {
        if (!GameWindow) return false;
        const HWND Fg = GetForegroundWindow();
        if (Fg == GameWindow) return true;
        const HWND Owner = GetWindow(GameWindow, GW_OWNER); // the Studio launcher when docked; null otherwise
        return Owner != nullptr && Fg == Owner;
    }

    std::wstring DataFile(const wchar_t* Name) {
        wchar_t Base[MAX_PATH]{};
        if (!GetEnvironmentVariableW(L"LOCALAPPDATA", Base, MAX_PATH)) return L"";
        return std::wstring(Base) + L"\\ProjectTilted\\" + Name;
    }
    void Log(const char* Text) {
        static int Lines = 0; if (++Lines > 400) return; // a few lines per launch is all that is needed
        FILE* Out = nullptr; auto File = DataFile(L"syndicate-client.log");
        if (!File.empty() && _wfopen_s(&Out, File.c_str(), L"a") == 0 && Out) {
            SYSTEMTIME T; GetLocalTime(&T);
            fprintf(Out, "%02d:%02d:%02d pid=%lu %s\n", T.wHour, T.wMinute, T.wSecond, GetCurrentProcessId(), Text); fclose(Out);
        }
    }

    // All actors of a class: the parameters are laid out by name and the game's own result array is copied out and
    // freed here, instead of going through the SDK's generic Call() with a pointer to our array.
    struct ActorList {
        std::vector<AActor*> Items;
        int Num() const { return int(Items.size()); }
        AActor* operator[](int i) const { return Items[size_t(i)]; }
    };
    void SyndicateAllActors(UWorld* World, const UClass* Class, ActorList& Out) {
        Out.Items.clear();
        static UObject* Lib = nullptr; static UFunction* Fn = nullptr; static int32 WorldOff = -1, ClassOff = -1, OutOff = -1; static uint32 Size = 0; static bool Tried = false;
        if (!Tried) {
            Tried = true;
            Lib = (UObject*)UGameplayStatics::GetDefaultObj(); Fn = Lib ? Lib->GetFunction("GetAllActorsOfClass") : nullptr;
            if (Fn) { auto P = Fn->GetParamsNamed(); Size = uint32(P.Size);
                for (auto& M : P.NameOffsetMap) { if (M.Name == "WorldContextObject") WorldOff = int32(M.Offset); else if (M.Name == "ActorClass") ClassOff = int32(M.Offset); else if (M.Name == "OutActors") OutOff = int32(M.Offset); } }
            if (!Fn || WorldOff < 0 || ClassOff < 0 || OutOff < 0 || Size > 256) Log("actors: GetAllActorsOfClass parameters not found (markers, aim assist and squad bars skip this season)");
        }
        if (!Fn || !World || !Class || WorldOff < 0 || ClassOff < 0 || OutOff < 0 || Size > 256) return;
        alignas(16) uint8_t Buf[256]{};
        *(UWorld**)(Buf + WorldOff) = World; *(const UClass**)(Buf + ClassOff) = Class;
        Lib->ProcessEvent(Fn, Buf);
        auto& Arr = *(TArray<AActor*>*)(Buf + OutOff);
        Out.Items.reserve(size_t(Arr.Num() > 0 ? Arr.Num() : 0));
        for (int i = 0; i < Arr.Num(); ++i) Out.Items.push_back(Arr[i]);
        Arr.Free(); // the game's own allocation
    }
    // Wanted FOV, read by the background thread, used on the game thread. 0 = off (game default).
    volatile LONG WantedTimes100 = 0;
    void ReadSetting() {
        float Value = 0.f; FILE* In = nullptr; auto File = DataFile(L"fov.txt");
        if (!File.empty() && _wfopen_s(&In, File.c_str(), L"r") == 0 && In) { if (fscanf_s(In, "%f", &Value) != 1) Value = 0.f; fclose(In); }
        if (!(Value >= 60.f && Value <= 130.f)) Value = 0.f;
        const LONG Before = InterlockedExchange(&WantedTimes100, LONG(Value * 100.f));
        if (Before != LONG(Value * 100.f)) { char Line[80]; snprintf(Line, sizeof(Line), "setting: FOV %.0f%s", double(Value), Value > 0 ? "" : " (game default)"); Log(Line); }
    }

    // ---- Turbo build (every season) ---------------------------------------------------------------
    // Later seasons place a piece again and again while you hold the place button in build mode; Pre-season
    // to Season 2 place one per click (in later seasons this simply adds to the game's own turbo build). With turbo build on (%LOCALAPPDATA%\ProjectTilted\turbo-build-on.txt),
    // while a building piece (wall, floor, stairs, roof) is in your hands and the left mouse button is held,
    // the button is released and pressed again every TurboEveryMs. Only building pieces: never a gun (no
    // rapid fire) and never the edit tool.
    constexpr ULONGLONG TurboFirstMs = 150, TurboEveryMs = 60;
    volatile LONG TurboOn = 0, InBuildMode = 0;
    // The real left button, from a low-level hook that flags injected input. GetAsyncKeyState cannot tell our own
    // injected turbo clicks from a real press, so a race could latch the button "down" forever (stuck / spamming).
    volatile LONG PhysicalLDown = 0, MouseHookOk = 0;
    LRESULT CALLBACK MouseLLProc(int Code, WPARAM W, LPARAM L) {
        if (Code == HC_ACTION) {
            auto Info = (MSLLHOOKSTRUCT*)L;
            if (Info && !(Info->flags & LLMHF_INJECTED)) {
                if (W == WM_LBUTTONDOWN) InterlockedExchange(&PhysicalLDown, 1);
                else if (W == WM_LBUTTONUP) InterlockedExchange(&PhysicalLDown, 0);
            }
        }
        return CallNextHookEx(nullptr, Code, W, L);
    }
    // The low-level hook sits in front of every mouse event on the whole PC (8000 a second with a high polling
    // rate mouse), so it is only installed while turbo build is on, and taken off again when it is turned off.
    DWORD WINAPI MouseHookLoop(void*) {
        HHOOK Hook = nullptr; int Failed = 0;
        for (;;) {
            if (TurboOn && !Hook && Failed < 3) {
                Hook = SetWindowsHookExW(WH_MOUSE_LL, MouseLLProc, SelfModule, 0);
                if (!Hook) { if (++Failed == 3) Log("turbo: physical mouse hook not installed; falling back to key state"); }
                else { InterlockedExchange(&PhysicalLDown, (GetAsyncKeyState(VK_LBUTTON) & 0x8000) ? 1 : 0); InterlockedExchange(&MouseHookOk, 1); Log("turbo: physical mouse tracking on (no more stuck clicks)"); }
            } else if (!TurboOn && Hook) {
                UnhookWindowsHookEx(Hook); Hook = nullptr; InterlockedExchange(&MouseHookOk, 0); InterlockedExchange(&PhysicalLDown, 0);
            }
            // Hook callbacks are delivered while this thread waits for messages.
            MsgWaitForMultipleObjects(0, nullptr, FALSE, 500, QS_ALLINPUT);
            MSG Msg; while (PeekMessageW(&Msg, nullptr, 0, 0, PM_REMOVE)) { TranslateMessage(&Msg); DispatchMessageW(&Msg); }
        }
    }
    void ReadTurbo() {
        auto File = DataFile(L"turbo-build-on.txt");
        const bool On = !File.empty() && GetFileAttributesW(File.c_str()) != INVALID_FILE_ATTRIBUTES;
        if (InterlockedExchange(&TurboOn, On ? 1 : 0) != (On ? 1 : 0)) Log(On ? "turbo build on" : "turbo build off");
    }
    int32 PcPawn = -2, PawnWeapon = -2, WeaponDataOff = -2;
    // Build mode is shared with SyndicatePadAssist.dll (controller turbo build) through a small named mapping (its
    // name stays as the prebuilt SyndicatePadAssist.dll opens it):
    // Local\TiltedClientState_<pid>: LONG 0 = 1 while a building piece is in hand and turbo build is on,
    // LONG 1 = 1 while the edit tool is in hand and edit on release is on (controller edit on release).
    volatile LONG* SharedBuildMode = nullptr;
    void PublishBuildMode(LONG Value, LONG Editing = 0) {
        if (!SharedBuildMode) {
            wchar_t Name[64]; swprintf_s(Name, L"Local\\TiltedClientState_%lu", GetCurrentProcessId());
            if (HANDLE Map = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, 64, Name))
                SharedBuildMode = (volatile LONG*)MapViewOfFile(Map, FILE_MAP_ALL_ACCESS, 0, 0, 64); // kept open for the process lifetime
        }
        if (SharedBuildMode) { InterlockedExchange(SharedBuildMode, Value); InterlockedExchange(SharedBuildMode + 1, Editing); }
    }
    // The local player's pawn (game thread), or null.
    UObject* LocalPawn() {
        auto World = UWorld::GetWorld();
        auto Instance = World ? World->OwningGameInstance : nullptr;
        if (Instance) for (auto Player : Instance->LocalPlayers) {
            auto PC = Player ? (UObject*)Player->PlayerController : nullptr;
            if (!PC) continue;
            if (PcPawn == -2) { PcPawn = int32(PC->GetOffset("AcknowledgedPawn")); if (PcPawn < 0) PcPawn = int32(PC->GetOffset("Pawn")); }
            return PcPawn >= 0 ? *(UObject**)(__int64(PC) + PcPawn) : nullptr;
        }
        return nullptr;
    }
    // Aiming down sights / scoped: the pawn's replicated bIsTargeting bit.
    class TargetingPawn : public UObject { public: DEFINE_BITFIELD_PROP(bIsTargeting); };
    // Game thread: is a building piece in the local player's hands?
    volatile LONG EditOn = 0, InEditTool = 0; // edit on release (see EditTick); InEditTool = the edit tool is in hand
    volatile LONG ResetOn = 0; // reset on release: let go of the edit key without picking tiles and the piece resets to a full wall
    // Counts finished TurboScan passes, so edit on release can tell a fresh InEditTool reading from a stale one
    // (the scan runs on the game thread; a hitch can leave InEditTool a few frames behind the real held item).
    volatile LONG ScanSeq = 0;
    void TurboScan() {
        if (!TurboOn && !EditOn && !ResetOn) { InBuildMode = 0; InEditTool = 0; PublishBuildMode(0); InterlockedIncrement(&ScanSeq); return; }
        bool Build = false, Edit = false;
        auto World = UWorld::GetWorld();
        auto Instance = World ? World->OwningGameInstance : nullptr;
        if (Instance) for (auto Player : Instance->LocalPlayers) {
            auto PC = Player ? (UObject*)Player->PlayerController : nullptr;
            if (!PC) continue;
            if (PcPawn == -2) { PcPawn = int32(PC->GetOffset("AcknowledgedPawn")); if (PcPawn < 0) PcPawn = int32(PC->GetOffset("Pawn")); }
            auto Pawn = PcPawn >= 0 ? *(UObject**)(__int64(PC) + PcPawn) : nullptr;
            if (Pawn) {
                if (PawnWeapon == -2) PawnWeapon = int32(Pawn->GetOffset("CurrentWeapon"));
                auto Weapon = PawnWeapon >= 0 ? *(UObject**)(__int64(Pawn) + PawnWeapon) : nullptr;
                if (Weapon) {
                    if (WeaponDataOff == -2) WeaponDataOff = int32(Weapon->GetOffset("WeaponData"));
                    auto Data = WeaponDataOff >= 0 ? *(UObject**)(__int64(Weapon) + WeaponDataOff) : nullptr;
                    // Building pieces are BuildingItemData_Wall / _Floor / _Stair_W / _RoofS; the edit tool is EditTool.
                    if (Data) {
                        const std::string Item = Data->Name.ToString().c_str();
                        // "contains", not "starts with": newer seasons (Season 4+) prefix the build-piece data name
                        // (e.g. Athena_BuildingItemData_Wall), so a starts-with check missed them and turbo build
                        // never engaged there. The edit tool is excluded below.
                        Build = Item.find("BuildingItemData") != std::string::npos;
                        Edit = Item.find("EditTool") != std::string::npos;
                        static std::string LastItem;
                        static int HoldLogs = 0; // only the first few: they used to fill the whole log (it stops at 200 lines)
                        if (Item != LastItem && HoldLogs < 15) { ++HoldLogs; LastItem = Item; char Line[160]; snprintf(Line, sizeof(Line), "holding %s (%s)", Item.c_str(), Build ? "building piece" : Edit ? "edit tool" : "not a building piece"); Log(Line); }
                    }
                }
            }
            break; // the first local player is the one on screen
        }
        Build = Build && TurboOn;
        InterlockedExchange(&InBuildMode, Build ? 1 : 0);
        InterlockedExchange(&InEditTool, Edit ? 1 : 0);
        PublishBuildMode(Build ? 1 : 0, Edit && (EditOn || ResetOn) ? 1 : 0);
        InterlockedIncrement(&ScanSeq);
    }
    // Background thread, every ~16 ms.
    ULONGLONG NextTurbo = 0;
    void TurboTick() {
        const ULONGLONG Now = GetTickCount64();
        // The physical button from the hook; only fall back to key state if the hook could not be installed. Using
        // GetAsyncKeyState here is what let a turbo-injected DOWN keep "holding" after you let go.
        const bool Down = MouseHookOk ? (PhysicalLDown != 0) : ((GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0);
        const bool Holding = TurboOn && InBuildMode && GameHasInput() && Down;
        if (!Holding) { NextTurbo = Now + TurboFirstMs; return; }
        if (Now < NextTurbo) return;
        NextTurbo = Now + TurboEveryMs;
        // Up and down in one batch: the button stays held as far as your hand is concerned, the game sees a new press.
        INPUT Click[2]{}; Click[0].type = Click[1].type = INPUT_MOUSE;
        Click[0].mi.dwFlags = MOUSEEVENTF_LEFTUP; Click[1].mi.dwFlags = MOUSEEVENTF_LEFTDOWN;
        const UINT Sent = SendInput(2, Click, sizeof(INPUT));
        static int Logged = 0;
        if (Logged < 3) { ++Logged; char Line[80]; snprintf(Line, sizeof(Line), "turbo: re-pressed place (%u of 2 inputs sent)", Sent); Log(Line); }
    }


    bool FlagFile(const wchar_t* Name) { auto File = DataFile(Name); return !File.empty() && GetFileAttributesW(File.c_str()) != INVALID_FILE_ATTRIBUTES; }
    std::string ReadSmallFile(const wchar_t* Name) {
        char Text[64]{}; FILE* In = nullptr; auto File = DataFile(Name);
        if (!File.empty() && _wfopen_s(&In, File.c_str(), L"r") == 0 && In) { fgets(Text, sizeof(Text), In); fclose(In); }
        std::string Out = Text; while (!Out.empty() && (Out.back() == '\n' || Out.back() == '\r' || Out.back() == ' ')) Out.pop_back();
        return Out;
    }

    // ---- Edit on release (every season) ----------------------------------------------------------
    // With edit-release-on.txt: hold your edit key, pick the tiles with the left mouse button, let go of the
    // edit key and the edit is confirmed (this module presses the edit key once more for you, the same way
    // you would confirm by hand). Only the press that OPENED edit mode confirms when it ends: a press made
    // while edit mode was already open is a confirm of its own and is left alone. Unless edit-release-any.txt
    // exists, nothing is confirmed when no tile was picked while the key was held (you stay in edit mode).
    // The key is the one in edit-key.txt ("key:<virtual key>" or "mouse:middle|x1|x2", default G) and must
    // match your edit key in Fortnite. Keyboard and mouse only.
    volatile LONG EditAnyTile = 0, EditKeyCode = 'G', EditSelectRelease = 1, NativeSelectBridge = 0;
    void ReadEdit() {
        const bool On = FlagFile(L"edit-release-on.txt");
        // Also confirm when you let go of select (the left mouse button that picks the tiles). On unless
        // edit-release-select-off.txt exists.
        const LONG Select = FlagFile(L"edit-release-select-off.txt") ? 0 : 1;
        if (InterlockedExchange(&EditSelectRelease, Select) != Select && On) Log(Select ? "edit on release: also confirms when select is let go" : "edit on release: select release does not confirm");
        InterlockedExchange(&EditAnyTile, FlagFile(L"edit-release-any.txt") ? 1 : 0);
        // Season X: SyndicateConsoleRecovery.dll confirms natively when select is let go (ClientEditRelease.h) and
        // signals that with this event. A second, key-press confirm from here on the same release reopened the edit
        // when it landed after the native one, so select release is left to the native bridge there.
        {
            wchar_t Name[64]; swprintf_s(Name, L"Local\\SyndicateEditBridge_%lu", GetCurrentProcessId());
            HANDLE Bridge = OpenEventW(SYNCHRONIZE, FALSE, Name);
            const LONG Native = Bridge ? 1 : 0;
            if (Bridge) CloseHandle(Bridge);
            if (InterlockedExchange(&NativeSelectBridge, Native) != Native && Native) Log("edit on release: Season X native select-release confirm is active; this module only handles the edit key");
        }
        LONG Vk = 'G'; // Fortnite's default edit key
        const std::string Key = ReadSmallFile(L"edit-key.txt");
        if (Key == "mouse:middle") Vk = VK_MBUTTON;
        else if (Key == "mouse:x1") Vk = VK_XBUTTON1;
        else if (Key == "mouse:x2") Vk = VK_XBUTTON2;
        else if (Key.rfind("key:", 0) == 0) { const int Code = atoi(Key.c_str() + 4); if (Code > 7 && Code < 255) Vk = Code; }
        const LONG BeforeKey = InterlockedExchange(&EditKeyCode, Vk);
        const LONG Before = InterlockedExchange(&EditOn, On ? 1 : 0);
        if (Before != (On ? 1 : 0) || (On && BeforeKey != Vk)) {
            char Line[96]; snprintf(Line, sizeof(Line), On ? "edit on release on (edit key vk %ld%s)" : "edit on release off", Vk, EditAnyTile ? ", confirms without a tile too" : ""); Log(Line);
        }
        // Reset on release: shares the same edit key as the trigger. When you let go of it without picking any tile,
        // Syndicate fires Fortnite's Reset Building Edit (right mouse) and then confirms, so the piece snaps back to a full
        // wall (an "edit peek" that always resets). On unless reset-release-on.txt is absent.
        const bool Reset = FlagFile(L"reset-release-on.txt");
        const LONG BeforeReset = InterlockedExchange(&ResetOn, Reset ? 1 : 0);
        if (BeforeReset != (Reset ? 1 : 0)) Log(Reset ? "reset on release on (piece resets to a full wall when you let go without editing)" : "reset on release off");
    }
    void SendEditPress(LONG Vk) {
        INPUT Press[2]{};
        if (Vk == VK_MBUTTON || Vk == VK_XBUTTON1 || Vk == VK_XBUTTON2) {
            Press[0].type = Press[1].type = INPUT_MOUSE;
            if (Vk == VK_MBUTTON) { Press[0].mi.dwFlags = MOUSEEVENTF_MIDDLEDOWN; Press[1].mi.dwFlags = MOUSEEVENTF_MIDDLEUP; }
            else {
                Press[0].mi.dwFlags = MOUSEEVENTF_XDOWN; Press[1].mi.dwFlags = MOUSEEVENTF_XUP;
                Press[0].mi.mouseData = Press[1].mi.mouseData = Vk == VK_XBUTTON1 ? XBUTTON1 : XBUTTON2;
            }
        } else {
            const UINT Scan = MapVirtualKeyW(UINT(Vk), MAPVK_VK_TO_VSC_EX);
            for (int i = 0; i < 2; ++i) {
                Press[i].type = INPUT_KEYBOARD; Press[i].ki.wVk = WORD(Vk); Press[i].ki.wScan = WORD(Scan & 0xFF);
                Press[i].ki.dwFlags = (Scan ? KEYEVENTF_SCANCODE : 0) | ((Scan & 0xFF00) ? KEYEVENTF_EXTENDEDKEY : 0) | (i == 1 ? KEYEVENTF_KEYUP : 0);
            }
        }
        // Held for a moment so the game sees it on a frame even at a low frame rate.
        SendInput(1, &Press[0], sizeof(INPUT)); Sleep(30); SendInput(1, &Press[1], sizeof(INPUT));
    }
    // Fortnite's default "Reset Building Edit" is the right mouse button: a quick click clears the tile selection
    // back to the original full piece while the edit tool stays in hand.
    void SendRightClick() {
        INPUT Click[2]{};
        Click[0].type = Click[1].type = INPUT_MOUSE;
        Click[0].mi.dwFlags = MOUSEEVENTF_RIGHTDOWN;
        Click[1].mi.dwFlags = MOUSEEVENTF_RIGHTUP;
        SendInput(1, &Click[0], sizeof(INPUT)); Sleep(20); SendInput(1, &Click[1], sizeof(INPUT));
    }
    // InEditTool comes from the game thread (TurboScan), a frame or more behind the real held item. Waits until the
    // scan has run Passes more times (or MaxMs passed), so a decision is made on a reading taken after the event.
    bool WaitForScans(LONG Passes, DWORD MaxMs) {
        const LONG Start = ScanSeq; const ULONGLONG Until = GetTickCount64() + MaxMs;
        while (ScanSeq - Start < Passes) { if (GetTickCount64() >= Until) return false; Sleep(2); }
        return true;
    }
    bool EditKeyHeld = false, EditOpenedByPress = false, EditPickedTile = false, SelectHeld = false, SelectInEdit = false;
    ULONGLONG EditIgnoreUntil = 0;
    static int EditLogs = 0; // edit lines in the log (shared cap, so a long session still shows what happened)
    void EditLog(const char* Text) { if (EditLogs < 40) { ++EditLogs; Log(Text); } }
    // Confirm the open edit with one press of the edit key, but only while the edit is really still open: if the game
    // (Fortnite's own confirm, the native Season X bridge, a broken piece) already closed it, the press would OPEN
    // edit mode again and leave you stuck in it. After the press, check that the edit closed; a press the game
    // dropped (a hitch, a low frame rate) is sent once more, so you are never left holding the edit tool.
    bool ConfirmEdit(LONG Vk, const char* Why) {
        WaitForScans(2, 120);
        if (!InEditTool) { EditLog("edit on release: the edit was already closed, no confirm sent"); return false; }
        SendEditPress(Vk);
        EditIgnoreUntil = GetTickCount64() + 150;
        EditLog(Why);
        for (int Try = 0; Try < 2; ++Try) {
            const ULONGLONG Until = GetTickCount64() + 500;
            while (InEditTool && GetTickCount64() < Until) Sleep(4);
            if (!InEditTool) return true;
            // Still in edit after half a second. Only trust that on fresh scans, and never press for you while you
            // are pressing the edit key or select yourself.
            if (!WaitForScans(4, 300) || !InEditTool) return true;
            if ((GetAsyncKeyState(int(Vk)) & 0x8000) || (GetAsyncKeyState(VK_LBUTTON) & 0x8000)) return true;
            if (Try == 1) { EditLog("edit on release: the edit stayed open after two confirms; left as it is"); return false; }
            SendEditPress(Vk);
            EditIgnoreUntil = GetTickCount64() + 150;
            EditLog("edit on release: the game missed the confirm, pressed the edit key again");
        }
        return true;
    }
    // Select release: in edit mode, press the left mouse button to pick tiles, let go and the edit is confirmed
    // (the edit key is pressed for you). While the edit key itself is still held, its own release confirms.
    void SelectTick(ULONGLONG Now, LONG Vk, bool EditKeyDown) {
        const bool Down = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
        if (!EditSelectRelease || NativeSelectBridge) { SelectHeld = Down; SelectInEdit = false; return; }
        if (Down && !SelectHeld) { SelectHeld = true; SelectInEdit = InEditTool != 0; }
        else if (Down) { if (InEditTool) SelectInEdit = true; } // edit mode opened while select was held
        else if (SelectHeld) {
            SelectHeld = false;
            const bool Confirm = SelectInEdit && InEditTool && !EditKeyDown;
            SelectInEdit = false;
            if (!Confirm) return;
            Sleep(40); // let the game take the tile stroke first
            ConfirmEdit(Vk, "edit on release: confirmed (select let go)");
        }
    }
    // Its own thread, every 4 ms (a key press is never shorter than that).
    void EditTick() {
        const ULONGLONG Now = GetTickCount64();
        if ((!EditOn && !ResetOn) || !GameHasInput()) { EditKeyHeld = false; SelectHeld = false; SelectInEdit = false; return; }
        if (Now < EditIgnoreUntil) return; // our own confirm press is going through
        const LONG Vk = EditKeyCode;
        const bool Down = (GetAsyncKeyState(int(Vk)) & 0x8000) != 0;
        if (EditOn) SelectTick(Now, Vk, Down); // select-release confirm is an edit-on-release behaviour only
        if (GetTickCount64() < EditIgnoreUntil) return; // select release just confirmed
        if (Down && !EditKeyHeld) {
            EditKeyHeld = true;
            EditOpenedByPress = InEditTool == 0; // edit mode not open yet: this press opens it
            EditPickedTile = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
            EditLog(EditOpenedByPress ? "edit on release: edit key down (opening edit mode)" : "edit on release: edit key down while editing (manual confirm)");
        } else if (Down) {
            if (GetAsyncKeyState(VK_LBUTTON) & 0x8000) EditPickedTile = true;
        } else if (EditKeyHeld) {
            EditKeyHeld = false;
            if (!EditOpenedByPress) return;
            if (!InEditTool) {
                // A quick tap can be let go before the game has put the edit tool in your hands. Waiting here (instead
                // of giving up) is what stops a fast edit from leaving you stuck in edit mode with nothing confirmed.
                const ULONGLONG Until = GetTickCount64() + 350;
                while (!InEditTool && GetTickCount64() < Until) {
                    if (GetAsyncKeyState(int(Vk)) & 0x8000) return; // pressed again: that press is handled on its own
                    if (GetAsyncKeyState(VK_LBUTTON) & 0x8000) EditPickedTile = true;
                    Sleep(4);
                }
                if (!InEditTool) { EditLog("edit on release: key let go but the edit tool never came up (not looking at your build, or a different edit key in Fortnite)"); return; }
            }
            if (ResetOn && !EditPickedTile) {
                // Peek let go without picking a tile: reset the edit back to the full piece, then confirm so it
                // closes as a full wall. Reset (right mouse) first, a short beat, then the edit key.
                WaitForScans(2, 120);
                if (!InEditTool) { EditLog("edit on release: the edit was already closed, no reset sent"); return; }
                EditIgnoreUntil = GetTickCount64() + 400;
                SendRightClick();
                Sleep(25);
                ConfirmEdit(Vk, "reset on release: piece reset to a full wall (let go without editing)");
            } else if (EditOn && (EditPickedTile || EditAnyTile)) {
                Sleep(40); // let the game handle your own release first
                ConfirmEdit(Vk, EditPickedTile ? "edit on release: confirmed" : "edit on release: confirmed (no tile picked)");
            }
        }
    }
    // ---- Dev: STORE tab reverse-engineering dump (read-only) -------------------------------------
    // The STORE (AthenaStore) tab shows "No Offers Available" because its offer list is built client-side and
    // the backend can't fill it. Step one of adding a tile there is to see the store's own objects. With the
    // dev console on (dev-console-on.txt), press Ctrl+Shift+F10 on the STORE tab to write every store/catalog/
    // offer object to %LOCALAPPDATA%\ProjectTilted\store-dump.log. Read-only: it only reads object names.
    void StoreDumpLine(const char* Text) {
        FILE* Out = nullptr; auto File = DataFile(L"store-dump.log");
        if (!File.empty() && _wfopen_s(&Out, File.c_str(), L"a") == 0 && Out) { fprintf(Out, "%s\n", Text); fclose(Out); }
    }
    // POD-only (no C++ objects, so __try is allowed): returns the class pointer only when it is safe to read.
    const UClass* StoreSafeClass(const UObject* Obj) {
        __try { return (Obj && Obj->Class) ? Obj->Class : nullptr; }
        __except (1) { return nullptr; }
    }
    bool StoreNameWanted(const char* Cls) {
        static const char* Keys[] = { "Store", "Catalog", "Offer", "Mtx", "Commerce", "Storefront",
            "Purchase", "CardPack", "VBuck", "Vbuck", "StarterKit", "Receipt" };
        for (auto K : Keys) if (strstr(Cls, K)) return true;
        return false;
    }
    // POD-only (__try-safe) pointer walks over the class hierarchy and its fields.
    const UStruct* StoreSafeSuper(const UStruct* S) { __try { return S ? S->GetSuper() : nullptr; } __except (1) { return nullptr; } }
    const UField* StoreSafeChildren(const UStruct* S) { __try { return S ? S->GetChildren() : nullptr; } __except (1) { return nullptr; } }
    const UField* StoreSafeNext(const UField* F) { __try { return F ? F->GetNext() : nullptr; } __except (1) { return nullptr; } }
    // Lay out a class: every base, and each base's properties/functions with offset and type, so we can see the
    // offer-array field and the populate function to inject into.
    bool StoreFuncInteresting(const char* N) {
        static const char* T[] = { "AddOffer", "OnMtxOfferGenerated", "OnOffersGenerated", "HandleMtxPackagesRead",
            "GetStoreOfferType", "AreOffersLoaded", "GetMtxBreakdown", "OnStartReadingOffers" };
        for (auto K : T) if (strcmp(N, K) == 0) return true;
        return false;
    }
    void StoreDumpClass(const UObject* Obj) {
        const UClass* Cls = StoreSafeClass(Obj);
        if (!Cls) return;
        { char H[256]; snprintf(H, sizeof(H), "---- CLASS: %s  (instance %s) ----", Cls->Name.ToString().c_str(), Obj->Name.ToString().c_str()); StoreDumpLine(H); }
        int Depth = 0;
        for (const UStruct* S = (const UStruct*)Cls; S && Depth < 24; S = StoreSafeSuper(S), ++Depth) {
            { std::string Sn = S->Name.ToString().c_str(); char H[160]; snprintf(H, sizeof(H), "  [%s]", Sn.c_str()); StoreDumpLine(H); }
            int Count = 0;
            for (const UField* F = StoreSafeChildren(S); F && Count < 2000; F = StoreSafeNext(F), ++Count) {
                const UClass* Fc = StoreSafeClass((const UObject*)F);
                std::string Fname = F->GetName().ToString().c_str();
                std::string Ftype = Fc ? Fc->Name.ToString().c_str() : "?";
                const unsigned Off = *(const unsigned*)((const unsigned char*)F + Offsets::Offset_Internal);
                char L[384]; snprintf(L, sizeof(L), "    %-46s %-22s off=0x%X", Fname.c_str(), Ftype.c_str(), Off);
                StoreDumpLine(L);
                // For the offer-loading functions, also list parameters (name, type, flags) so we know what AddOffer takes.
                if (Ftype == "Function" && StoreFuncInteresting(Fname.c_str())) {
                    const UStruct* Fn = (const UStruct*)F;
                    int Pc = 0;
                    for (const UField* P = StoreSafeChildren(Fn); P && Pc < 64; P = StoreSafeNext(P), ++Pc) {
                        const UClass* Pcl = StoreSafeClass((const UObject*)P);
                        std::string Pname = P->GetName().ToString().c_str();
                        std::string Ptype = Pcl ? Pcl->Name.ToString().c_str() : "?";
                        const unsigned long long Pflags = *(const unsigned long long*)((const unsigned char*)P + Offsets::PropertyFlags);
                        char PL[320]; snprintf(PL, sizeof(PL), "        param %-30s %-20s flags=0x%llX", Pname.c_str(), Ptype.c_str(), Pflags);
                        StoreDumpLine(PL);
                    }
                }
            }
        }
    }
    bool StoreClassTargeted(const char* Cls) {
        static const char* T[] = { "FortStoreContext", "StoreMain_Root_C", "FortStoreRootBase", "FortMtxStoreRootBase",
            "FortStorefront", "FortStoreSummary", "StoreMain_MTXOffer_C", "FortMtxOfferData", "FortMtxStoreOfferBase" };
        for (auto K : T) if (strcmp(Cls, K) == 0) return true;
        return false;
    }
    void StoreDumpOnce() {
        StoreDumpLine("=== store dump begin ===");
        const int N = SDK::TUObjectArray::Num();
        char Head[96]; snprintf(Head, sizeof(Head), "object count: %d", N); StoreDumpLine(Head);
        int Logged = 0;
        std::unordered_map<std::string, int> Dumped;   // target class -> times its layout was dumped (cap 1)
        for (int i = 0; i < N && Logged < 6000; ++i) {
            const UObject* Obj = SDK::TUObjectArray::GetObjectByIndex(i);
            if (!Obj) continue;
            const UClass* Cls = StoreSafeClass(Obj);
            if (!Cls) continue;
            const std::string ClsName = Cls->Name.ToString().c_str();
            if (!StoreNameWanted(ClsName.c_str())) continue;
            const std::string ObjName = Obj->Name.ToString().c_str();
            char Line[512]; snprintf(Line, sizeof(Line), "[%d] class=%s name=%s", i, ClsName.c_str(), ObjName.c_str());
            StoreDumpLine(Line);
            ++Logged;
            // The first real (non-archetype, non-CDO) instance of a target class gets its full layout dumped.
            if (StoreClassTargeted(ClsName.c_str()) && Dumped[ClsName] == 0
                && ObjName.find("Default__") == std::string::npos && ObjName.find("Archetype") == std::string::npos) {
                StoreDumpClass(Obj);
                Dumped[ClsName] = 1;
            }
        }
        char Tail[80]; snprintf(Tail, sizeof(Tail), "=== store dump end (%d matched) ===", Logged); StoreDumpLine(Tail);
        Log("dev store dump: written to store-dump.log");
    }
    void StoreDumpTick() {
        static bool Was = false;
        const bool On = FlagFile(L"dev-console-on.txt");
        const bool Key = On && GameHasInput()
            && (GetAsyncKeyState(VK_CONTROL) & 0x8000)
            && (GetAsyncKeyState(VK_SHIFT) & 0x8000)
            && (GetAsyncKeyState(VK_F10) & 0x8000);
        if (Key && !Was) StoreDumpOnce();
        Was = Key;
    }

    // ---- Dev: experimental STORE-tab placeholder tile ----------------------------------------------
    // Ctrl+Shift+F9 on the STORE tab asks for a placeholder: StoreMain_Root_C.AddOffer(FortMtxOfferData CDO),
    // which should create a Store_Main_MTXOffer tile and add it to the scroll box. AddOffer builds UMG widgets,
    // so it must run on the game thread; the key only sets a flag and OnMessage runs it. Experimental.
    volatile LONG StoreInjectActive = 0;   // persistent toggle: keep showing the injected tile while on
    const UObject* StoreFindLive(const char* ClassName) {
        const int N = SDK::TUObjectArray::Num();
        for (int i = 0; i < N; ++i) {
            const UObject* Obj = SDK::TUObjectArray::GetObjectByIndex(i);
            if (!Obj) continue;
            const UClass* Cls = StoreSafeClass(Obj);
            if (!Cls) continue;
            const std::string Cn = Cls->Name.ToString().c_str();
            if (Cn != ClassName) continue;
            const std::string On = Obj->Name.ToString().c_str();
            if (On.find("Default__") != std::string::npos || On.find("Archetype") != std::string::npos) continue;
            return Obj;
        }
        return nullptr;
    }
    void StoreSetVis(UObject* W, unsigned char Vis) {
        if (!W) return;
        UFunction* Fn = W->GetFunction("SetVisibility");
        if (!Fn) return;
        struct { unsigned char Vis; } A{}; A.Vis = Vis;   // ESlateVisibility
        W->ProcessEvent(Fn, &A);
    }
    // The store the tile was added to, and the tile, so we create it once per open store and re-assert it each frame.
    UObject* StoreInjectedRoot = nullptr;
    UObject* StoreInjectedTile = nullptr;
    // Create a tile widget and add it to the offers scroll box. Returns the tile (or null). Game thread only.
    UObject* StoreCreateTile(UObject* Root) {
        UObject* WidgetClass = *(UObject**)((unsigned char*)Root + 0x3C8);   // MtxOfferWidgetClass
        if (!WidgetClass) WidgetClass = (UObject*)SDK::FindClass("StoreMain_MTXOffer_C");
        if (!WidgetClass) { Log("store inject: no tile widget class (0x3C8 null and class not found)"); return nullptr; }
        const UClass* WblCls = SDK::FindClass("WidgetBlueprintLibrary");
        UObject* Wbl = WblCls ? WblCls->GetDefaultObj() : nullptr;
        UFunction* CreateFn = Wbl ? Wbl->GetFunction("Create") : nullptr;
        if (!CreateFn) { Log("store inject: WidgetBlueprintLibrary.Create not found"); return nullptr; }
        struct { UObject* WorldContext; UObject* WidgetType; UObject* OwningPlayer; UObject* ReturnValue; } C{};
        C.WorldContext = Root; C.WidgetType = WidgetClass;
        Wbl->ProcessEvent(CreateFn, &C);
        if (!C.ReturnValue) { Log("store inject: Create returned null"); return nullptr; }
        UObject* ScrollBox = *(UObject**)((unsigned char*)Root + 0x430);     // OffersHorizontalScrollBox
        if (ScrollBox) {
            UFunction* AddChildFn = ScrollBox->GetFunction("AddChild");
            if (AddChildFn) { struct { UObject* Content; UObject* Ret; } A{}; A.Content = C.ReturnValue; ScrollBox->ProcessEvent(AddChildFn, &A); }
            else Log("store inject: AddChild not found on the scroll box");
        } else Log("store inject: offers scroll box (0x430) is null");
        const UClass* Tc = StoreSafeClass(C.ReturnValue);
        std::string Tn = Tc ? Tc->Name.ToString().c_str() : "?";
        char H[160]; snprintf(H, sizeof(H), "store inject: created tile of class %s, added to scroll box", Tn.c_str());
        Log(H);
        return C.ReturnValue;
    }
    // Runs every frame while active (from OnMessage). Creates the tile once per open store, then keeps the store's
    // own "no offers" layout from hiding it again by re-asserting visibility. No-ops when the STORE tab is closed.
    void StoreInjectMaintain() {
        const UObject* RootC = StoreFindLive("StoreMain_Root_C");
        if (!RootC) { StoreInjectedRoot = nullptr; StoreInjectedTile = nullptr; return; } // store not open
        UObject* Root = (UObject*)RootC;
        if (Root != StoreInjectedRoot) { StoreInjectedRoot = Root; StoreInjectedTile = StoreCreateTile(Root); }
        StoreSetVis(*(UObject**)((unsigned char*)Root + 0x428), 1); // NoOffersText -> Collapsed
        StoreSetVis(*(UObject**)((unsigned char*)Root + 0x430), 0); // OffersHorizontalScrollBox -> Visible
        StoreSetVis(*(UObject**)((unsigned char*)Root + 0x450), 0); // VboxOffers -> Visible
        if (StoreInjectedTile) StoreSetVis(StoreInjectedTile, 0);   // the tile -> Visible
    }
    void StoreInjectTick() {
        static bool Was = false;
        const bool On = FlagFile(L"dev-console-on.txt");
        const bool Key = On && GameHasInput()
            && (GetAsyncKeyState(VK_CONTROL) & 0x8000)
            && (GetAsyncKeyState(VK_SHIFT) & 0x8000)
            && (GetAsyncKeyState(VK_F9) & 0x8000);
        if (Key && !Was) { InterlockedExchange(&StoreInjectActive, StoreInjectActive ? 0 : 1); } // toggle on/off
        Was = Key;
    }
    DWORD WINAPI EditLoop(void*) { for (;;) { EditTick(); StoreDumpTick(); StoreInjectTick(); Sleep(4); } }

    // ---- Dev Kit camera share (Dev Console on-screen markers) ------------------------------------
    // Only while dev-camera-on.txt exists, which only the Dev Kit launcher's Dev Console creates: the camera's
    // position, angles and field of view go to a small shared memory block (Local\TiltedDevCamera_<pid>) so the
    // launcher can place player markers over the game. Nothing is sent anywhere else.
    volatile LONG DevCameraOn = 0;
    struct DevCamera { LONG Seq; float X, Y, Z, Pitch, Yaw, Roll, Fov; };
    DevCamera* DevCameraView = nullptr;
    // Player positions as this game shows them (same frame as the camera), so the Dev Console markers and radar
    // sit exactly on the players instead of trailing behind the match server's numbers. Only while a Dev Kit
    // match server runs on this same PC (it creates Local\SyndicateDevLive; see PublishDevLive in SpectraBots).
    volatile LONG DevPawnsOn = 0, DevAimLevel = 0, DevDrawMarkers = 0, DevDrawRadar = 0;
    // Dev Console cheats (Dev Kit only, only with a Dev Kit match server on this PC): fly, no recoil, no
    // bullet drop. The match server does its own part (see DevApplyCheats in SpectraBots).
    volatile LONG DevLocalServer = 0, DevFlyOn = 0, DevNoRecoil = 0, DevNoDrop = 0, DevFlyKey = VK_F8, DevFreeCamOn = 0, DevFlySpeed = 8000;
    void DevReadCheats() {
        const bool Console = FlagFile(L"dev-console-on.txt");
        InterlockedExchange(&DevFlyOn, Console && FlagFile(L"dev-fly-on.txt") ? 1 : 0);
        InterlockedExchange(&DevNoRecoil, Console && FlagFile(L"dev-no-recoil.txt") ? 1 : 0);
        InterlockedExchange(&DevNoDrop, Console && FlagFile(L"dev-no-drop.txt") ? 1 : 0);
        { const int Speed = atoi(ReadSmallFile(L"dev-fly-speed.txt").c_str()); InterlockedExchange(&DevFlySpeed, Speed >= 1000 && Speed <= 120000 ? Speed : 8000); }
        const std::string Key = ReadSmallFile(L"dev-fly-key.txt");
        LONG Vk = VK_F8;
        if (Key.rfind("key:", 0) == 0) { const int Code = atoi(Key.c_str() + 4); if (Code > 7 && Code < 255) Vk = Code; }
        else if (Key == "mouse:middle") Vk = VK_MBUTTON; else if (Key == "mouse:x1") Vk = VK_XBUTTON1; else if (Key == "mouse:x2") Vk = VK_XBUTTON2;
        InterlockedExchange(&DevFlyKey, Vk);
    }
    // Head and Foot are where the game itself draws that point on screen (its own ProjectWorldLocationToScreen,
    // in viewport pixels, ViewW x ViewH): no camera maths outside the game, so the markers land exactly on players.
    struct DevPawn { char Name[48]; float X, Y, Z; LONG Team; float HeadX, HeadY, FootX, FootY; LONG OnScreen, Local; };
    struct DevPawns { LONG Seq, Count, ViewW, ViewH; DevPawn Items[160]; };
    DevPawns* DevPawnsView = nullptr;
    void ReadDevCamera() {
        // Everything from the Dev Console (markers, aim assist, fly, no recoil, no drop) only while the Dev Console
        // itself is switched on in Settings > Modded. Its own switches used to stay in effect after the Dev Console
        // was turned off (aim assist kept running and Season 8 crashed whenever the player aimed).
        const bool Console = FlagFile(L"dev-console-on.txt");
        // Bot markers on is enough: the separate camera switch was sometimes missing (no markers, nothing shared).
        const bool On = Console && (FlagFile(L"dev-camera-on.txt") || FlagFile(L"dev-markers-on.txt"));
        if (InterlockedExchange(&DevCameraOn, On ? 1 : 0) != (On ? 1 : 0)) Log(On ? "dev camera share on (Dev Console markers)" : "dev camera share off");
        // Dev aim assist (Dev Kit only): dev-aim-assist.txt holds the strength, 1 (light) to 3 (strong).
        const std::string Aim = ReadSmallFile(L"dev-aim-assist.txt");
        const LONG AimLevel = Console && FlagFile(L"dev-aim-assist.txt") ? (Aim == "3" ? 3 : Aim == "1" ? 1 : 2) : 0;
        bool LocalDevServer = false;
        if (Console) if (HANDLE Live = OpenFileMappingW(FILE_MAP_READ, FALSE, L"Local\\TiltedDevLive")) { LocalDevServer = true; CloseHandle(Live); }
        // Season 8 and later: no player share (bot markers). Turning Bot markers on crashed Season 8 every time, even
        // with the in-game drawing off; the markers need the game's screen projection for every player every frame.
        const bool ShareOk = VersionInfo.FortniteVersion < 8.f;
        if (On && LocalDevServer && !ShareOk) { static bool Told = false; if (!Told) { Told = true; Log("dev markers: off in Season 8 and later (turning them on crashed the game)"); } }
        if (InterlockedExchange(&DevPawnsOn, (LocalDevServer && On && ShareOk) ? 1 : 0) != ((LocalDevServer && On && ShareOk) ? 1 : 0)) Log(LocalDevServer && On && ShareOk ? "dev player share on (local Dev Kit match server)" : "dev player share off");
        InterlockedExchange(&DevLocalServer, LocalDevServer ? 1 : 0);
        // Free cam (Level Editor): a detached debug camera. Gated only on a local Dev Kit server being present (the
        // SyndicateDevLive mapping), NOT on the Dev Console toggle, so the Level Editor can switch it on by itself.
        bool FreeCamServer = false;
        if (HANDLE Live = OpenFileMappingW(FILE_MAP_READ, FALSE, L"Local\\TiltedDevLive")) { FreeCamServer = true; CloseHandle(Live); }
        const LONG FreeCam = (FreeCamServer && FlagFile(L"free-cam-on.txt")) ? 1 : 0;
        if (InterlockedExchange(&DevFreeCamOn, FreeCam) != FreeCam) Log(FreeCam ? "free cam: requested (debug camera)" : "free cam: off");
        DevReadCheats();
        const LONG Markers = LocalDevServer && FlagFile(L"dev-markers-on.txt") ? 1 : 0, Radar = 0; // the radar was removed
        InterlockedExchange(&DevDrawMarkers, Markers); InterlockedExchange(&DevDrawRadar, Radar);
        const LONG AimNow = LocalDevServer ? AimLevel : 0;
        if (InterlockedExchange(&DevAimLevel, AimNow) != AimNow) { char Line[96]; snprintf(Line, sizeof(Line), AimNow ? "dev aim assist on (strength %ld, bots only)" : "dev aim assist off", AimNow); Log(Line); }
    }
    // ProjectWorldLocationToScreen(WorldLocation, ScreenLocation&, bPlayerViewportRelative) -> bool, called with
    // a raw parameter block (offsets found once) so ~300 calls a frame stay cheap.
    UFunction* DevProjectFn = nullptr; UFunction* DevViewportFn = nullptr;
    int32 DevPjWorld = -1, DevPjScreen = -1, DevPjRelative = -1, DevPjReturn = -1, DevPjSize = 0, DevVpX = -1, DevVpY = -1, DevVpSize = 0;
    bool DevProjectReady(UObject* PC) {
        if (DevProjectFn && DevViewportFn) return DevPjWorld >= 0 && DevPjScreen >= 0 && DevPjReturn >= 0 && DevVpX >= 0 && DevVpY >= 0;
        DevProjectFn = PC->GetFunction("ProjectWorldLocationToScreen");
        DevViewportFn = PC->GetFunction("GetViewportSize");
        if (!DevProjectFn || !DevViewportFn) { Log("dev markers: ProjectWorldLocationToScreen or GetViewportSize not found"); DevProjectFn = DevViewportFn = (UFunction*)1; return false; }
        auto P = DevProjectFn->GetParamsNamed(); DevPjSize = int32(P.Size);
        for (auto& Pm : P.NameOffsetMap) {
            if (Pm.Name == "WorldLocation") DevPjWorld = int32(Pm.Offset);
            else if (Pm.Name == "ScreenLocation") DevPjScreen = int32(Pm.Offset);
            else if (Pm.Name == "bPlayerViewportRelative") DevPjRelative = int32(Pm.Offset);
            else if (Pm.Name == "ReturnValue") DevPjReturn = int32(Pm.Offset);
        }
        auto V = DevViewportFn->GetParamsNamed(); DevVpSize = int32(V.Size);
        for (auto& Pm : V.NameOffsetMap) { if (Pm.Name == "SizeX") DevVpX = int32(Pm.Offset); else if (Pm.Name == "SizeY") DevVpY = int32(Pm.Offset); }
        char Line[200]; snprintf(Line, sizeof(Line), "dev markers: projection params world %d screen %d relative %d return %d (size %d), viewport %d/%d", DevPjWorld, DevPjScreen, DevPjRelative, DevPjReturn, DevPjSize, DevVpX, DevVpY); Log(Line);
        return DevPjWorld >= 0 && DevPjScreen >= 0 && DevPjReturn >= 0 && DevVpX >= 0 && DevVpY >= 0 && DevPjSize <= 64 && DevVpSize <= 32;
    }
    bool DevProject(UObject* PC, float X, float Y, float Z, float& SX, float& SY) {
        alignas(16) uint8_t Buf[64]{};
        float W[3] = { X, Y, Z }; memcpy(Buf + DevPjWorld, W, 12);
        PC->ProcessEvent(DevProjectFn, Buf);
        const float* S = (const float*)(Buf + DevPjScreen); SX = S[0]; SY = S[1];
        return Buf[DevPjReturn] != 0;
    }
    int32 DevPawnPs = -2, DevPsTeam = -2;
    const UClass* DevPawnClass = nullptr;
    struct DevNameCache { UObject* State = nullptr; std::string Name; };
    std::unordered_map<UObject*, DevNameCache> DevNames;
    // A player's name: GetPlayerName where the season has it, else the PlayerName property (Season 1 has no
    // callable GetPlayerName, which left the markers without names).
    std::string DevPlayerName(UObject* State) {
        std::string Out;
        if (auto Fn = State->GetFunction("GetPlayerName")) { auto N = State->Call<FString>(Fn); Out = N.ToString().c_str(); N.Free(); }
        if (Out.empty()) {
            static int32 Off = -2; if (Off == -2) Off = int32(State->GetOffset("PlayerName"));
            if (Off >= 0) { auto& S = *(FString*)(__int64(State) + Off); if (S.Num() > 1) Out = S.ToString().c_str(); }
        }
        return Out;
    }
    // The team, or -1. The property is looked up again (once a second) until it is found.
    LONG DevTeamOf(UObject* State) {
        if (!State) return -1;
        if (DevPsTeam < 0) {
            static ULONGLONG Retry = 0; const ULONGLONG Now = GetTickCount64();
            if (Now < Retry) return -1;
            Retry = Now + 1000;
            DevPsTeam = int32(State->GetOffset("TeamIndex"));
            static int Logged = 0;
            if (Logged < 2) { ++Logged; char Line[96]; snprintf(Line, sizeof(Line), "dev draw: TeamIndex at %d", DevPsTeam); Log(Line); }
            if (DevPsTeam < 0) return -1;
        }
        return LONG(*(uint8_t*)(__int64(State) + DevPsTeam));
    }
    // Dev markers and aim assist only look at bots within 100 m.
    constexpr float DevMaxMetres = 100.f;
    void DevPawnsExport(UWorld* World, UObject* PC, UObject* LocalPawn) {
        if (!DevPawnsOn) return;
        if (!DevPawnsView) {
            wchar_t Name[64]; swprintf_s(Name, L"Local\\TiltedDevPawns_%lu", GetCurrentProcessId());
            if (HANDLE Map = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, sizeof(DevPawns), Name))
                DevPawnsView = (DevPawns*)MapViewOfFile(Map, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(DevPawns)); // kept for the process lifetime
            if (!DevPawnsView) return;
        }
        if (!DevPawnClass) { DevPawnClass = FindClass("FortPlayerPawnAthena"); if (!DevPawnClass) return; }
        const bool CanProject = DevProjectReady(PC);
        LONG ViewW = 0, ViewH = 0;
        if (CanProject) { alignas(16) uint8_t Vp[32]{}; PC->ProcessEvent(DevViewportFn, Vp); ViewW = *(int32*)(Vp + DevVpX); ViewH = *(int32*)(Vp + DevVpY); }
        static ActorList Found;
        SyndicateAllActors(World, DevPawnClass, Found);
        InterlockedIncrement(&DevPawnsView->Seq); // odd: being written
        DevPawnsView->ViewW = ViewW; DevPawnsView->ViewH = ViewH;
        LONG Count = 0;
        for (int i = 0; i < Found.Num() && Count < 160; ++i) {
            auto Pawn = Found[i];
            if (!Pawn) continue;
            if (LocalPawn && (UObject*)Pawn != LocalPawn) {
                const auto A = Pawn->K2_GetActorLocation(), B = ((AActor*)LocalPawn)->K2_GetActorLocation();
                const double Dx = A.X - B.X, Dy = A.Y - B.Y, Dz = A.Z - B.Z;
                if (Dx * Dx + Dy * Dy + Dz * Dz > double(DevMaxMetres) * DevMaxMetres * 10000.0) continue;
            }
            if (DevPawnPs == -2) DevPawnPs = int32(((UObject*)Pawn)->GetOffset("PlayerState"));
            auto State = DevPawnPs >= 0 ? *(UObject**)(__int64(Pawn) + DevPawnPs) : nullptr;
            auto& Item = DevPawnsView->Items[Count];
            auto& Cached = DevNames[(UObject*)Pawn];
            if (State && Cached.State != State) {
                Cached.State = State; Cached.Name.clear();
                Cached.Name = DevPlayerName(State);
            }
            Item.Team = DevTeamOf(State);
            strncpy_s(Item.Name, Cached.Name.c_str(), _TRUNCATE);
            auto L = Pawn->K2_GetActorLocation(); Item.X = L.X; Item.Y = L.Y; Item.Z = L.Z;
            Item.Local = (UObject*)Pawn == LocalPawn ? 1 : 0;
            // The actor location is the middle of the body: feet about 90 below, top of the head about 90 above.
            Item.OnScreen = CanProject && !Item.Local
                && DevProject(PC, L.X, L.Y, L.Z + 90.f, Item.HeadX, Item.HeadY)
                && DevProject(PC, L.X, L.Y, L.Z - 90.f, Item.FootX, Item.FootY) ? 1 : 0;
            ++Count;
        }
        DevPawnsView->Count = Count;
        InterlockedIncrement(&DevPawnsView->Seq); // even: complete
        if (DevNames.size() > 512) DevNames.clear(); // pawns come and go; names are looked up again
        static int Logged = 0;
        if (Logged < 3 && Count > 0) { ++Logged; char Line[160]; snprintf(Line, sizeof(Line), "dev markers: %ld players loaded, viewport %ldx%ld, projection %s", Count, ViewW, ViewH, CanProject ? "on" : "off"); Log(Line); }
    }
    // ---- Dev aim assist (Dev Kit only, keyboard and mouse) -----------------------------------------
    // For testing. Only while a Dev Kit match server runs on this PC and dev-aim-assist.txt exists, and it only
    // ever pulls toward BOTS (never toward a real player). While you aim down sights or shoot, when a bot
    // (also behind walls and builds, for testing) is within a few degrees of your crosshair, your aim is eased toward its chest, like a
    // controller's aim assist. Your own mouse movement always still counts; nothing is locked.
    class DevAimPC : public UObject { public: DEFINE_FUNC(GetControlRotation, FRotator); DEFINE_FUNC(SetControlRotation, void); DEFINE_FUNC(LineOfSightTo, bool); };
    class DevAimCamera : public UObject { public: DEFINE_FUNC(GetCameraLocation, FVector); DEFINE_FUNC(GetCameraRotation, FRotator); DEFINE_FUNC(GetFOVAngle, float); };
    class DevAimState : public UObject { public: DEFINE_BITFIELD_PROP(bIsABot); };
    int32 DevAimPs = -2, DevAimCam = -2;
    const UClass* DevAimClass = nullptr;
    ULONGLONG DevAimLast = 0;
    static float DevAngle(float A) { while (A > 180.f) A -= 360.f; while (A < -180.f) A += 360.f; return A; }
    void DevAimTick() {
        const LONG Level = DevAimLevel;
        const ULONGLONG Now = GetTickCount64();
        const float Dt = DevAimLast ? float(Now - DevAimLast) / 1000.f : 0.f; DevAimLast = Now;
        if (!Level || Dt <= 0.f || Dt > .1f || !GameHasInput()) return;
        // Season 8 and later: off. Season 8 crashed every time you aimed with it on.
        if (VersionInfo.FortniteVersion >= 8.f) { static bool Told = false; if (!Told) { Told = true; Log("dev aim assist: off in Season 8 and later (aiming crashed the game)"); } return; }
        auto Pawn = (TargetingPawn*)LocalPawn();
        if (!Pawn) return;
        // Aiming down sights (right mouse) or shooting (left mouse).
        const bool Aiming = (Pawn->HasbIsTargeting() && Pawn->GetbIsTargeting()) || (GetAsyncKeyState(VK_RBUTTON) & 0x8000) || (GetAsyncKeyState(VK_LBUTTON) & 0x8000);
        if (!Aiming) return;
        auto World = UWorld::GetWorld();
        auto Instance = World ? World->OwningGameInstance : nullptr;
        if (!Instance) return;
        for (auto Player : Instance->LocalPlayers) {
            auto PC = Player ? (DevAimPC*)(UObject*)Player->PlayerController : nullptr;
            if (!PC) continue;
            if (DevAimCam == -2) DevAimCam = int32(PC->GetOffset("PlayerCameraManager"));
            auto Camera = DevAimCam >= 0 ? *(DevAimCamera**)(__int64(PC) + DevAimCam) : nullptr;
            if (!Camera) return;
            const FVector Eye = Camera->GetCameraLocation();
            FRotator Aim = PC->GetControlRotation();
            if (!DevAimClass) { DevAimClass = FindClass("FortPlayerPawnAthena"); if (!DevAimClass) return; }
            static ActorList Found;
        SyndicateAllActors(World, DevAimClass, Found);
            // Strength: how far (degrees) from the crosshair a bot is picked up, and how fast the aim eases in.
            const float Cone = Level == 1 ? 3.5f : Level == 3 ? 8.f : 5.5f, Pull = Level == 1 ? 4.f : Level == 3 ? 13.f : 8.f;
            float BestScore = 1e9f, BestYaw = 0.f, BestPitch = 0.f;
            for (int i = 0; i < Found.Num(); ++i) {
                auto Other = Found[i];
                if (!Other || (UObject*)Other == (UObject*)Pawn) continue;
                if (DevAimPs == -2) DevAimPs = int32(((UObject*)Other)->GetOffset("PlayerState"));
                auto State = DevAimPs >= 0 ? *(DevAimState**)(__int64(Other) + DevAimPs) : nullptr;
                if (!State || !State->HasbIsABot() || !State->GetbIsABot()) continue; // bots only
                auto L = Other->K2_GetActorLocation();
                const float Dx = L.X - Eye.X, Dy = L.Y - Eye.Y, Dz = (L.Z + 35.f) - Eye.Z; // chest
                const float Flat = sqrtf(Dx * Dx + Dy * Dy);
                if (Flat < 50.f || Flat > DevMaxMetres * 100.f) continue; // 100 m
                const float Yaw = atan2f(Dy, Dx) * 57.29578f, Pitch = atan2f(Dz, Flat) * 57.29578f;
                const float DYaw = DevAngle(Yaw - float(Aim.Yaw)), DPitch = DevAngle(Pitch - float(Aim.Pitch));
                const float Off = sqrtf(DYaw * DYaw + DPitch * DPitch);
                if (Off > Cone || Off >= BestScore) continue;
                BestScore = Off; BestYaw = DYaw; BestPitch = DPitch;
            }
            if (BestScore > 1e8f) return;
            const float K = 1.f - expf(-Pull * Dt); // the same feel at any frame rate
            Aim.Yaw = float(Aim.Yaw) + BestYaw * K;
            Aim.Pitch = float(Aim.Pitch) + BestPitch * K;
            PC->SetControlRotation(Aim);
            static int Logged = 0;
            if (Logged < 3) { ++Logged; Log("dev aim assist: easing toward a bot"); }
            return; // the first local player is the one on screen
        }
    }
    // ---- Dev cheats on the game side ---------------------------------------------------------------------
    // The fly key (Dev Console > Cheats, default F8) switches dev-fly-on.txt; the match server picks it up within
    // a quarter of a second and switches flying on for you (Unreal's own ghost cheat: no collision).
    void DevFlyKeyTick() {
        static bool Held = false;
        const bool Down = DevLocalServer && GameHasInput() && (GetAsyncKeyState(int(DevFlyKey)) & 0x8000);
        if (Down && !Held) {
            const auto File = DataFile(L"dev-fly-on.txt");
            if (!File.empty()) {
                if (DevFlyOn) DeleteFileW(File.c_str());
                else if (HANDLE H = CreateFileW(File.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr); H != INVALID_HANDLE_VALUE) CloseHandle(H);
                InterlockedExchange(&DevFlyOn, DevFlyOn ? 0 : 1);
                Log(DevFlyOn ? "dev fly: on (fly key)" : "dev fly: off (fly key)");
            }
        }
        Held = Down;
    }
    class DevMovePawn : public UObject { public: DEFINE_FUNC(AddMovementInput, void); };
    // Game thread: while flying, Space goes up and Ctrl goes down (flying ignores jump and crouch); fly speed
    // matches the server's so the game does not pull you back.
    int32 DevCmcOff = -2, DevFlySpeedOff = -2;
    void DevFlyTick() {
        if (!DevLocalServer || !DevFlyOn || !GameHasInput()) return;
        auto Pawn = (DevMovePawn*)LocalPawn();
        if (!Pawn) return;
        if (DevCmcOff == -2) DevCmcOff = int32(Pawn->GetOffset("CharacterMovement"));
        if (auto Move = DevCmcOff >= 0 ? *(UObject**)(__int64(Pawn) + DevCmcOff) : nullptr) {
            if (DevFlySpeedOff == -2) DevFlySpeedOff = int32(Move->GetOffset("MaxFlySpeed"));
            if (DevFlySpeedOff >= 0) *(float*)(__int64(Move) + DevFlySpeedOff) = float(DevFlySpeed);
        }
        const float Up = (GetAsyncKeyState(VK_SPACE) & 0x8000) ? 1.f : 0.f, Down = (GetAsyncKeyState(VK_LCONTROL) & 0x8000) || (GetAsyncKeyState('C') & 0x8000) ? 1.f : 0.f;
        if (Up - Down != 0.f) Pawn->AddMovementInput(FVector(0, 0, 1), Up - Down, false);
    }
    // Free cam (ToggleDebugCamera) was removed: in these shipping builds it calls a pure virtual function and
    // Fortnite closes with "Pure virtual function being called". Fly (dev-fly-on.txt) is the editing camera.
    void DevFreeCamTick() {}
    // No recoil: the recoil numbers of the gun in your hands are set to zero in your game (your game applies
    // recoil to your own aim). The original numbers come back when the switch is turned off.
    struct DevRecoilSave { uint8_t* Row; std::vector<std::pair<int32, float>> Values; };
    std::vector<DevRecoilSave> DevRecoilSaved;
    std::vector<int32> DevRecoilFields;
    int32 DevWeaponOff = -2, DevWeaponDataOff = -2, DevStatHandleOff = -2;
    void DevRecoilTick() {
        const bool Want = DevLocalServer && DevNoRecoil;
        if (!Want) {
            if (!DevRecoilSaved.empty()) { for (auto& S : DevRecoilSaved) for (auto& [Off, V] : S.Values) *(float*)(S.Row + Off) = V; DevRecoilSaved.clear(); Log("dev no recoil: off, recoil restored"); }
            return;
        }
        auto Pawn = LocalPawn();
        if (!Pawn) return;
        if (DevWeaponOff == -2) DevWeaponOff = int32(Pawn->GetOffset("CurrentWeapon"));
        auto Weapon = DevWeaponOff >= 0 ? *(UObject**)(__int64(Pawn) + DevWeaponOff) : nullptr;
        if (!Weapon) return;
        if (DevWeaponDataOff == -2) DevWeaponDataOff = int32(Weapon->GetOffset("WeaponData"));
        auto Def = DevWeaponDataOff >= 0 ? *(UObject**)(__int64(Weapon) + DevWeaponDataOff) : nullptr;
        if (!Def) return;
        if (DevStatHandleOff == -2) DevStatHandleOff = int32(Def->GetOffset("WeaponStatHandle"));
        if (DevStatHandleOff < 0) return;
        struct Handle { UObject* Table; FName Row; };
        auto& H = *(Handle*)(__int64(Def) + DevStatHandleOff);
        if (!H.Table) return;
        if (DevRecoilFields.empty()) {
            static bool Tried = false; if (Tried) return; Tried = true;
            auto Stats = FindStruct("FortRangedWeaponStats");
            if (!Stats) { Log("dev no recoil: FortRangedWeaponStats not found"); return; }
            for (const char* Name : { "RecoilVert", "RecoilHoriz", "RecoilVertScale", "RecoilHorizScale", "RecoilVertScaleGamepad", "RecoilHorizScaleGamepad", "RecoilDownsightsMultiplier" }) {
                const int32 Off = int32(Stats->GetOffset(Name)); if (Off >= 0) DevRecoilFields.push_back(Off);
            }
            char Line[96]; snprintf(Line, sizeof(Line), "dev no recoil: %d recoil fields found", int(DevRecoilFields.size())); Log(Line);
            if (DevRecoilFields.empty()) return;
        }
        // Only a table of ranged weapon stats: the pickaxe's table holds melee stats, a smaller row, and writing
        // the ranged recoil fields into it wrote past the row and corrupted memory (crash in garbage collection).
        static const UStruct* RangedStats = FindStruct("FortRangedWeaponStats");
        bool Ranged = false;
        for (auto T = *(const UStruct**)(__int64(H.Table) + 0x28); T; T = T->GetSuper()) if (T == RangedStats) { Ranged = true; break; } // UDataTable::RowStruct
        if (!Ranged) return;
        auto& Rows = *(TMap<FName, uint8_t*>*)(__int64(H.Table) + 0x30); // UDataTable::RowMap
        auto Found = Rows.Search([&](FName& Key, uint8_t* Value) { return Key == H.Row && Value; });
        uint8_t* Row = Found ? *Found : nullptr;
        if (!Row) return;
        for (auto& S : DevRecoilSaved) if (S.Row == Row) return; // already zeroed
        DevRecoilSave Save{ Row, {} };
        for (auto Off : DevRecoilFields) { Save.Values.push_back({ Off, *(float*)(Row + Off) }); *(float*)(Row + Off) = 0.f; }
        DevRecoilSaved.push_back(std::move(Save));
        static int Logged = 0; if (Logged < 3) { ++Logged; Log("dev no recoil: recoil removed from the gun in your hands"); }
    }
    // No bullet drop on your side too (the server does the same), so what you see matches the hit.
    void DevNoDropTick(UWorld* World) {
        if (!DevLocalServer || !DevNoDrop || !World) return;
        auto Me = LocalPawn(); if (!Me) return;
        static const UClass* ProjectileClass = FindClass("FortProjectileBase");
        if (!ProjectileClass) return;
        static ActorList Found;
        SyndicateAllActors(World, ProjectileClass, Found);
        static int32 InstigatorOff = -2, MoveOff = -2, GravityOff = -2;
        for (int i = 0; i < Found.Num(); i++) {
            auto P = (UObject*)Found[i]; if (!P) continue;
            if (InstigatorOff == -2) { InstigatorOff = int32(P->GetOffset("Instigator")); MoveOff = int32(P->GetOffset("ProjectileMovement")); }
            if (InstigatorOff < 0 || MoveOff < 0 || *(UObject**)(__int64(P) + InstigatorOff) != Me) continue;
            auto Move = *(UObject**)(__int64(P) + MoveOff); if (!Move) continue;
            if (GravityOff == -2) GravityOff = int32(Move->GetOffset("ProjectileGravityScale"));
            if (GravityOff >= 0) *(float*)(__int64(Move) + GravityOff) = 0.f;
        }
    }
    // Bot health from the match server (Local\SyndicateDevLive): what the server says is the truth, the game's
    // own copy of a bot's health can lag or never update. Name -> health, shield, knocked.
    struct DevServerHealth { float Hp, Sh; bool Knocked; LONG Team; };
    std::unordered_map<std::string, DevServerHealth> DevHealth;
    void DevReadServerHealth() {
        static ULONGLONG Next = 0; const ULONGLONG Now = GetTickCount64(); if (Now < Next) return; Next = Now + 100;
        static volatile LONG* View = nullptr;
        if (!View) { if (HANDLE Map = OpenFileMappingW(FILE_MAP_READ, FALSE, L"Local\\TiltedDevLive")) View = (volatile LONG*)MapViewOfFile(Map, FILE_MAP_READ, 0, 0, 0); if (!View) return; }
        std::string J;
        for (int Try = 0; Try < 3; ++Try) {
            const LONG Seq = View[0]; if (Seq & 1) continue;
            const LONG Len = View[1]; if (Len <= 0 || Len > 500 * 1024) return;
            J.assign((const char*)(View + 2), size_t(Len));
            if (View[0] == Seq) break; J.clear();
        }
        if (J.empty()) return;
        DevHealth.clear();
        auto Num = [&](size_t From, size_t End, const char* Key) -> float { const size_t K = J.find(Key, From); return K != std::string::npos && K < End ? float(atof(J.c_str() + K + strlen(Key))) : 0.f; };
        size_t At = 0;
        while ((At = J.find("{\"n\":\"", At)) != std::string::npos) {
            const size_t NameStart = At + 6, NameEnd = J.find('"', NameStart), End = J.find('}', NameStart);
            if (NameEnd == std::string::npos || End == std::string::npos) break;
            DevHealth[J.substr(NameStart, NameEnd - NameStart)] = { Num(NameEnd, End, "\"hp\":"), Num(NameEnd, End, "\"sh\":"), Num(NameEnd, End, "\"k\":") > 0.f, LONG(Num(NameEnd, End, "\"t\":")) };
            At = End;
        }
    }
    int32 DevPcCamera = -2, DevPcPawn = -2;
    // ---- Dev in-game drawing (Dev Kit only) -------------------------------------------------------
    // The markers and the radar are drawn by the game itself, inside its own HUD pass, with the game's canvas
    // and its own projection: they are part of the frame (no separate window, no lag behind the camera).
    // How: the HUD's ProcessEvent is hooked; right after the game runs AHUD's ReceiveDrawHUD for the frame,
    // DevDrawHud() draws with UCanvas K2_DrawLine / K2_DrawText / K2_Project. BOTS ONLY, never a real player,
    // and only while a Dev Kit match server runs on this PC (DevPawnsOn).
    volatile LONG* DevDrawFrames = nullptr; // Local\TiltedDevDrawing_<pid>: counts frames drawn in game
    struct DevFn { UFunction* Fn = nullptr; int32 Size = 0; bool Ok = false; std::unordered_map<std::string, int32> Off; };
    bool DevPrep(DevFn& C, UObject* Obj, const char* Name) {
        if (C.Fn) return C.Ok;
        C.Fn = Obj->GetFunction(Name);
        if (!C.Fn) { C.Fn = (UFunction*)1; char Line[96]; snprintf(Line, sizeof(Line), "dev draw: %s not found", Name); Log(Line); return false; }
        auto P = C.Fn->GetParamsNamed(); C.Size = int32(P.Size);
        for (auto& Pm : P.NameOffsetMap) C.Off[std::string(Pm.Name.c_str())] = int32(Pm.Offset);
        C.Ok = C.Size <= 512;
        return C.Ok;
    }
    int32 DevOff(const DevFn& C, const char* Name) { auto It = C.Off.find(Name); return It == C.Off.end() ? -1 : It->second; }
    template <typename T> void DevPut(uint8_t* B, const DevFn& C, const char* Name, const T& V) { const int32 O = DevOff(C, Name); if (O >= 0) memcpy(B + O, &V, sizeof(T)); }
    struct DevV2 { float X, Y; };
    struct DevColor { float R, G, B, A; };
    DevFn DevLineFn, DevTextFn, DevProjectCanvasFn;
    UObject* DevFont = nullptr;
    int32 DevHudCanvas = -2, DevCanvasClipX = -2, DevCanvasClipY = -2;

    void DevLine(UObject* Canvas, float X1, float Y1, float X2, float Y2, float Thick, DevColor C) {
        if (!DevPrep(DevLineFn, Canvas, "K2_DrawLine")) return;
        alignas(16) uint8_t B[512]{};
        DevPut(B, DevLineFn, "ScreenPositionA", DevV2{ X1, Y1 }); DevPut(B, DevLineFn, "ScreenPositionB", DevV2{ X2, Y2 });
        DevPut(B, DevLineFn, "Thickness", Thick); DevPut(B, DevLineFn, "RenderColor", C);
        Canvas->ProcessEvent(DevLineFn.Fn, B);
    }
    void DevText(UObject* Canvas, const wchar_t* Text, float X, float Y, DevColor C, float Scale) {
        if (!DevFont || !DevPrep(DevTextFn, Canvas, "K2_DrawText")) return;
        alignas(16) uint8_t B[512]{};
        struct { const wchar_t* Data; int32 Num, Max; } Str{ Text, int32(wcslen(Text) + 1), int32(wcslen(Text) + 1) }; // FString over our own buffer: the game only reads it
        DevPut(B, DevTextFn, "RenderFont", DevFont); DevPut(B, DevTextFn, "RenderText", Str);
        DevPut(B, DevTextFn, "ScreenPosition", DevV2{ X, Y }); DevPut(B, DevTextFn, "Scale", DevV2{ Scale, Scale });
        DevPut(B, DevTextFn, "RenderColor", C); DevPut(B, DevTextFn, "ShadowColor", DevColor{ 0, 0, 0, 0 });
        DevPut(B, DevTextFn, "bCentreX", true); DevPut(B, DevTextFn, "bOutlined", true); DevPut(B, DevTextFn, "OutlineColor", DevColor{ 0, 0, 0, .85f });
        Canvas->ProcessEvent(DevTextFn.Fn, B);
    }
    // Screen position of a world point (the game's own projection for this frame); false when behind you.
    bool DevToScreen(UObject* Canvas, float X, float Y, float Z, float& SX, float& SY) {
        if (!DevPrep(DevProjectCanvasFn, Canvas, "K2_Project")) return false;
        alignas(16) uint8_t B[64]{};
        float W[3] = { X, Y, Z }; const int32 In = DevOff(DevProjectCanvasFn, "WorldLocation"), Out = DevOff(DevProjectCanvasFn, "ReturnValue");
        if (In < 0 || Out < 0) return false;
        memcpy(B + In, W, 12);
        Canvas->ProcessEvent(DevProjectCanvasFn.Fn, B);
        const float* R = (const float*)(B + Out); SX = R[0]; SY = R[1];
        return R[2] > 0.f; // the game sets depth to 0 for points behind the camera
    }
    void DevFindFont() {
        if (DevFont) return;
        static bool Tried = false; if (Tried) return; Tried = true;
        auto FontClass = FindClass("Font");
        if (!FontClass) { Log("dev draw: no Font class"); return; }
        for (const char* Name : { "Roboto", "RobotoDistanceField", "SmallFont" })
            if (auto F = TUObjectArray::FindObject(Name, 0, FontClass)) { DevFont = (UObject*)F; break; }
        if (!DevFont) for (int i = 0; i < TUObjectArray::Num(); i++) { auto O = TUObjectArray::GetObjectByIndex(i); if (O && O->IsA(FontClass) && !O->IsDefaultObject()) { DevFont = (UObject*)O; break; } }
        char Line[128]; snprintf(Line, sizeof(Line), "dev draw: font %s", DevFont ? DevFont->Name.ToString().c_str() : "not found (names are not drawn)"); Log(Line);
    }
    class DevFortPawn : public UObject { public: DEFINE_FUNC(GetHealth, float); DEFINE_FUNC(GetShield, float); DEFINE_FUNC(IsDBNO, bool); };
    DevColor DevTeamColor(LONG Team, LONG MyTeam) {
        static const DevColor Palette[] = { {1.f,.35f,.42f,1}, {.3f,.76f,1.f,1}, {1.f,.69f,.23f,1}, {.73f,.65f,1.f,1}, {1.f,.48f,.85f,1}, {.37f,.9f,.84f,1}, {.95f,.9f,.36f,1}, {1.f,.55f,.35f,1} };
        if (Team == MyTeam) return { .23f, .89f, .55f, 1 };
        return Palette[(Team < 0 ? -Team : Team) % 8];
    }
    void DevDrawHud(UObject* Hud) {
        if (!DevPawnsOn || (!DevDrawMarkers && !DevDrawRadar)) return;
        if (DevHudCanvas == -2) DevHudCanvas = int32(Hud->GetOffset("Canvas"));
        auto Canvas = DevHudCanvas >= 0 ? *(UObject**)(__int64(Hud) + DevHudCanvas) : nullptr;
        if (!Canvas) return;
        if (DevCanvasClipX == -2) { DevCanvasClipX = int32(Canvas->GetOffset("ClipX")); DevCanvasClipY = int32(Canvas->GetOffset("ClipY")); }
        if (DevCanvasClipX < 0 || DevCanvasClipY < 0) return;
        const float ClipX = *(float*)(__int64(Canvas) + DevCanvasClipX), ClipY = *(float*)(__int64(Canvas) + DevCanvasClipY);
        if (ClipX < 200.f || ClipY < 200.f) return;
        DevFindFont();
        DevReadServerHealth();
        auto World = UWorld::GetWorld();
        auto Instance = World ? World->OwningGameInstance : nullptr;
        if (!Instance || !Instance->LocalPlayers.Num()) return;
        auto Player = Instance->LocalPlayers[0];
        auto PC = Player ? (UObject*)Player->PlayerController : nullptr;
        if (!PC) return;
        if (DevPcCamera == -2) DevPcCamera = int32(PC->GetOffset("PlayerCameraManager"));
        auto Camera = DevPcCamera >= 0 ? *(DevAimCamera**)(__int64(PC) + DevPcCamera) : nullptr;
        if (DevPcPawn == -2) { DevPcPawn = int32(PC->GetOffset("AcknowledgedPawn")); if (DevPcPawn < 0) DevPcPawn = int32(PC->GetOffset("Pawn")); }
        UObject* Me = DevPcPawn >= 0 ? *(UObject**)(__int64(PC) + DevPcPawn) : nullptr;
        if (!Camera) return;
        const FVector Eye = Camera->GetCameraLocation();
        const float EyeYaw = float(Camera->GetCameraRotation().Yaw) * .0174533f;
        if (!DevAimClass) { DevAimClass = FindClass("FortPlayerPawnAthena"); if (!DevAimClass) return; }
        static ActorList Found;
        SyndicateAllActors(World, DevAimClass, Found);
        if (DevAimPs == -2 && Found.Num() > 0 && Found[0]) DevAimPs = int32(((UObject*)Found[0])->GetOffset("PlayerState"));
        LONG MyTeam = -1;
        if (Me && DevAimPs >= 0) if (auto MyState = *(UObject**)(__int64(Me) + DevAimPs)) {
            MyTeam = DevTeamOf(MyState);
            // The server's team when the game's own copy cannot be read (Season 4 showed every bot as T-1).
            static std::string MyName; if (MyName.empty()) MyName = DevPlayerName(MyState);
            if (auto It = DevHealth.find(MyName); It != DevHealth.end()) MyTeam = It->second.Team;
        }

        // Radar: top left, turned the way your camera faces, 250 m to the edge.
        const float RSize = ClipY * .23f, RX = ClipY * .025f + RSize / 2, RY = ClipY * .30f + RSize / 2, RR = RSize / 2, Range = 25000.f;
        if (DevDrawRadar) {
            const DevColor Ring{ .55f, .42f, 1.f, .95f }, Faint{ 1, 1, 1, .16f }, Back{ .03f, .04f, .09f, .62f };
            // Filled disc: one thin line per row.
            for (float Yo = -RR + 1.f; Yo < RR; Yo += 2.f) { const float Half = sqrtf(RR * RR - Yo * Yo); DevLine(Canvas, RX - Half, RY + Yo, RX + Half, RY + Yo, 2.4f, Back); }
            for (float Ring2 : { .333f, .666f }) for (int k = 0; k < 40; ++k) { const float A = k * 6.2831853f / 40, B2 = (k + 1) * 6.2831853f / 40; DevLine(Canvas, RX + cosf(A) * RR * Ring2, RY + sinf(A) * RR * Ring2, RX + cosf(B2) * RR * Ring2, RY + sinf(B2) * RR * Ring2, 1.f, Faint); }
            for (int k = 0; k < 64; ++k) { const float A = k * 6.2831853f / 64, B2 = (k + 1) * 6.2831853f / 64; DevLine(Canvas, RX + cosf(A) * RR, RY + sinf(A) * RR, RX + cosf(B2) * RR, RY + sinf(B2) * RR, 2.5f, Ring); }
            DevLine(Canvas, RX - RR, RY, RX + RR, RY, 1.f, Faint); DevLine(Canvas, RX, RY - RR, RX, RY + RR, 1.f, Faint);
            // Your view: a soft wedge ahead.
            DevLine(Canvas, RX, RY, RX - RR * .62f, RY - RR * .78f, 1.f, { 1, 1, 1, .28f }); DevLine(Canvas, RX, RY, RX + RR * .62f, RY - RR * .78f, 1.f, { 1, 1, 1, .28f });
            DevText(Canvas, L"250 m", RX, RY + RR + 4.f, { .75f, .78f, .9f, .9f }, .8f);
        }
        int Drawn = 0;
        for (int i = 0; i < Found.Num(); ++i) {
            auto Other = (DevFortPawn*)(UObject*)Found[i];
            if (!Other || (UObject*)Other == Me) continue;
            auto State = DevAimPs >= 0 ? *(DevAimState**)(__int64(Other) + DevAimPs) : nullptr;
            if (!State || !State->HasbIsABot() || !State->GetbIsABot()) continue; // bots only
            float Hp = Other->GetHealth(), Sh = Other->GetShield(); LONG ServerTeam = -1;
            bool Knocked = Other->IsDBNO();
            // The server's numbers when it has them (the game's own copy of a bot's health may not update).
            auto& Cached = DevNames[(UObject*)Other];
            if (Cached.State != (UObject*)State) { Cached.State = (UObject*)State; Cached.Name = DevPlayerName((UObject*)State); }
            if (auto It = DevHealth.find(Cached.Name); It != DevHealth.end()) {
                static int Compared = 0;
                if (Compared < 6 && (fabsf(It->second.Hp - Hp) > 1.f || fabsf(It->second.Sh - Sh) > 1.f)) { ++Compared; char Line[160]; snprintf(Line, sizeof(Line), "dev health: %s game %.0f/%.0f, server %.0f/%.0f (server used)", Cached.Name.c_str(), double(Hp), double(Sh), double(It->second.Hp), double(It->second.Sh)); Log(Line); }
                // Squad HUD check: what this game actually received for a bot teammate (its health set, and the
                // health number copied onto its player state). The squad list top left draws from these.
                static int Probed = 0;
                if (Probed < 4 && MyTeam >= 0 && It->second.Team == MyTeam) {
                    ++Probed;
                    static int32 HsOff = -2; if (HsOff == -2) HsOff = int32(((UObject*)Other)->GetOffset("HealthSet"));
                    static int32 PsHp = -2; if (PsHp == -2) PsHp = int32(((UObject*)State)->GetOffset("CurrentHealth"));
                    void* Set = HsOff >= 0 ? *(void**)(__int64(Other) + HsOff) : nullptr;
                    const float StateHp = PsHp >= 0 ? *(float*)(__int64(State) + PsHp) : -1.f;
                    char Line[200]; snprintf(Line, sizeof(Line), "squad check: teammate %s: health set %s, game health %.0f, player state health %.0f, server %.0f",
                        Cached.Name.c_str(), Set ? "received" : "missing", double(Hp), double(StateHp), double(It->second.Hp)); Log(Line);
                }
                Hp = It->second.Hp; Sh = It->second.Sh; Knocked = It->second.Knocked; ServerTeam = It->second.Team;
            }
            if (Hp <= 0.f && !Knocked) continue;
            const LONG Team = ServerTeam >= 0 ? ServerTeam : DevTeamOf((UObject*)State);
            const DevColor Col = DevTeamColor(Team, MyTeam);
            auto L = ((AActor*)(UObject*)Other)->K2_GetActorLocation();
            const float X = float(L.X), Y = float(L.Y), Z = float(L.Z);
            const float Dx = X - float(Eye.X), Dy = Y - float(Eye.Y), Dz = Z - float(Eye.Z);
            const float Metres = sqrtf(Dx * Dx + Dy * Dy + Dz * Dz) / 100.f;
            if (DevDrawRadar) {
                const float Fwd = Dx * cosf(EyeYaw) + Dy * sinf(EyeYaw), Right = -Dx * sinf(EyeYaw) + Dy * cosf(EyeYaw);
                float PX = Right / Range * RR, PY = -Fwd / Range * RR; const float Len = sqrtf(PX * PX + PY * PY);
                const bool Edge = Len > RR - 6.f;
                if (Edge) { PX *= (RR - 6.f) / Len; PY *= (RR - 6.f) / Len; }
                const float DX = RX + PX, DY = RY + PY, Rd = Edge ? 2.f : 3.5f;
                const DevColor Dot = Knocked || Edge ? DevColor{ Col.R, Col.G, Col.B, .55f } : Col;
                DevLine(Canvas, DX - Rd - 1.f, DY, DX + Rd + 1.f, DY, Rd * 2.f + 2.f, { 0, 0, 0, .55f }); // outline
                DevLine(Canvas, DX - Rd, DY, DX + Rd, DY, Rd * 2.f, Dot);
                // Higher (tick above) or lower (tick below) than you by more than 4 m.
                if (!Edge && fabsf(Dz) > 400.f) { const float T = Dz > 0 ? DY - Rd - 3.f : DY + Rd + 3.f; DevLine(Canvas, DX - 3.f, T, DX + 3.f, T, 1.5f, Dot); }
            }
            if (!DevDrawMarkers || Metres > DevMaxMetres) continue; // bots within 100 m only
            float HX, HY, FX, FY;
            if (!DevToScreen(Canvas, X, Y, Z + 88.f, HX, HY) || !DevToScreen(Canvas, X, Y, Z - 90.f, FX, FY)) continue;
            const float Tall = (FY - HY) > 8.f ? FY - HY : 8.f, Wide = Tall * .5f, CX = (HX + FX) / 2;
            if (CX < -100 || CX > ClipX + 100 || HY > ClipY + 100 || FY < -100) continue;
            const float L1 = CX - Wide / 2, R1 = CX + Wide / 2, Corner = Wide * .3f;
            const DevColor Dark{ 0, 0, 0, .65f };
            // Corner box: four corners, dark outline under a team-colour line.
            for (int Pass = 0; Pass < 2; ++Pass) {
                const float T = Pass == 0 ? 3.5f : 1.6f; const DevColor C = Pass == 0 ? Dark : Col;
                DevLine(Canvas, L1, HY, L1 + Corner, HY, T, C); DevLine(Canvas, L1, HY, L1, HY + Corner, T, C);
                DevLine(Canvas, R1, HY, R1 - Corner, HY, T, C); DevLine(Canvas, R1, HY, R1, HY + Corner, T, C);
                DevLine(Canvas, L1, FY, L1 + Corner, FY, T, C); DevLine(Canvas, L1, FY, L1, FY - Corner, T, C);
                DevLine(Canvas, R1, FY, R1 - Corner, FY, T, C); DevLine(Canvas, R1, FY, R1, FY - Corner, T, C);
            }
            // Health (green) and shield (blue) bars at the left.
            const float HpK = Hp / 100.f > 1.f ? 1.f : Hp / 100.f, ShK = Sh / 100.f > 1.f ? 1.f : Sh / 100.f;
            DevLine(Canvas, L1 - 6, FY + 1, L1 - 6, HY - 1, 5.f, Dark);
            DevLine(Canvas, L1 - 6, FY, L1 - 6, FY - Tall * HpK, 3.f, Knocked ? DevColor{ 1.f, .35f, .42f, 1 } : DevColor{ .23f, .89f, .55f, 1 });
            if (ShK > 0.f) { DevLine(Canvas, L1 - 11, FY + 1, L1 - 11, HY - 1, 5.f, Dark); DevLine(Canvas, L1 - 11, FY, L1 - 11, FY - Tall * ShK, 3.f, { .3f, .76f, 1.f, 1 }); }
            const float Scale = Metres < 50.f ? 1.15f : Metres < 120.f ? 1.f : .85f;
            if (Metres < 200.f) {
                wchar_t Label[128];
                swprintf_s(Label, L"%S%s", Cached.Name.empty() ? "bot" : Cached.Name.c_str(), Knocked ? L"  ·  KNOCKED" : L"");
                DevText(Canvas, Label, CX, HY - 20.f * Scale - 6.f, Col, Scale);
            }
            wchar_t Info[64];
            swprintf_s(Info, L"%.0f HP  %.0f SH  ·  %.0f m", double(Hp), double(Sh), double(Metres));
            DevText(Canvas, Info, CX, FY + 3.f, { .92f, .94f, 1.f, .9f }, Scale * .85f);
            ++Drawn;
        }
        if (DevDrawFrames) InterlockedIncrement(DevDrawFrames);
        static int Logged = 0;
        if (Logged < 2) { ++Logged; char Line[128]; snprintf(Line, sizeof(Line), "dev draw: drawing in game (%d bots on screen, canvas %.0fx%.0f)", Drawn, double(ClipX), double(ClipY)); Log(Line); }
    }
    using DevProcessEventFn = void (*)(UObject*, UFunction*, void*);
    DevProcessEventFn DevPEOriginal = nullptr;
    UFunction* DevDrawHudFn = nullptr;
    void DevPEDetour(UObject* Object, UFunction* Function, void* Params) {
        DevPEOriginal(Object, Function, Params);
        if (!DevPawnsOn) return;
        if (Function && Function == DevDrawHudFn) { __try { DevDrawHud(Object); } __except (EXCEPTION_EXECUTE_HANDLER) {} }
    }
    // Game thread: hook ProcessEvent for the HUD object only, by giving that one HUD a copy of its vtable
    // with the ProcessEvent slot pointing at DevPEDetour. Nothing in the game's code is patched.
    // (It used to be a MinHook inline hook on the engine's shared ProcessEvent: turning the Dev Console
    // boxes on crashed 6.20 and 7.40 at once, then on every launch at the main menu.)
    // A new HUD (main menu -> match) gets its own copy; old copies are kept, since a HUD being destroyed
    // still calls through its vtable.
    void DevDrawInstall(UObject* PC) {
        static UObject* HookedHud = nullptr; static void** HookedVft = nullptr; static bool Failed = false;
        if (Failed) return;
        const int32 HudOff = int32(PC->GetOffset("MyHUD"));
        auto Hud = HudOff >= 0 ? *(UObject**)(__int64(PC) + HudOff) : nullptr;
        if (!Hud) return; // not spawned yet: try again next tick
        if (Hud == HookedHud && Hud->Vft == HookedVft) return;
        if (!DevDrawHudFn) DevDrawHudFn = Hud->GetFunction("ReceiveDrawHUD");
        if (!DevDrawHudFn) { Failed = true; Log("dev draw: ReceiveDrawHUD not found (markers are not drawn)"); return; }
        const int32 Slot = int32(Offsets::ProcessEventVft);
        void** Source = Hud->Vft;
        if (Slot <= 0 || !Source) { Failed = true; Log("dev draw: ProcessEvent slot unknown (markers are not drawn)"); return; }
        // Copy as many entries as are readable (vtables sit in read-only data; stop at the end of the region).
        int32 Count = 0;
        MEMORY_BASIC_INFORMATION Info{};
        if (VirtualQuery(Source, &Info, sizeof(Info))) {
            const auto End = __int64(Info.BaseAddress) + __int64(Info.RegionSize);
            while (Count < 1024 && __int64(Source + Count + 1) <= End) ++Count;
        }
        if (Count <= Slot) { Failed = true; Log("dev draw: HUD vtable not readable (markers are not drawn)"); return; }
        // One entry more in front: MSVC keeps the type information pointer just before the vtable (Vft[-1]);
        // without it a type check or cast on the HUD read garbage and crashed the game.
        auto Block = (void**)VirtualAlloc(nullptr, sizeof(void*) * (Count + 1), MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
        if (!Block) { Failed = true; Log("dev draw: no memory for the HUD hook"); return; }
        Block[0] = Source[-1];
        auto Copy = Block + 1;
        memcpy(Copy, Source, sizeof(void*) * Count);
        if (Copy[Slot] != (void*)DevPEDetour) DevPEOriginal = (DevProcessEventFn)Copy[Slot];
        Copy[Slot] = (void*)DevPEDetour;
        DWORD Old = 0; VirtualProtect(Block, sizeof(void*) * (Count + 1), PAGE_READONLY, &Old);
        Hud->Vft = Copy; HookedHud = Hud; HookedVft = Copy;
        if (!DevDrawFrames) {
            wchar_t Name[64]; swprintf_s(Name, L"Local\\TiltedDevDrawing_%lu", GetCurrentProcessId());
            if (HANDLE Map = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, 64, Name))
                DevDrawFrames = (volatile LONG*)MapViewOfFile(Map, FILE_MAP_ALL_ACCESS, 0, 0, 64); // kept for the process lifetime
        }
        char Line[160]; snprintf(Line, sizeof(Line), "dev draw: HUD hook on (%s, vtable copy of %d entries)", Hud->Class ? Hud->Class->Name.ToString().c_str() : "?", int(Count)); Log(Line);
    }
    void DevReadCamera(DevAimCamera* Camera) {
        const FVector L = Camera->GetCameraLocation();
        const FRotator R = Camera->GetCameraRotation();
        DevCameraView->X = float(L.X); DevCameraView->Y = float(L.Y); DevCameraView->Z = float(L.Z);
        DevCameraView->Pitch = float(R.Pitch); DevCameraView->Yaw = float(R.Yaw); DevCameraView->Roll = float(R.Roll);
        DevCameraView->Fov = Camera->GetFOVAngle();
        InterlockedIncrement(&DevCameraView->Seq);
        static int Logged = 0;
        if (Logged < 2) { ++Logged; char Line[160]; snprintf(Line, sizeof(Line), "dev camera: at %.0f, %.0f, %.0f yaw %.0f pitch %.0f fov %.0f", double(DevCameraView->X), double(DevCameraView->Y), double(DevCameraView->Z), double(DevCameraView->Yaw), double(DevCameraView->Pitch), double(DevCameraView->Fov)); Log(Line); }
    }
    // CameraCache.POV (Location, Rotation, FOV), straight from the camera manager's memory: no game function calls.
    void DevReadCameraMemory(UObject* Camera) {
        static int32 Loc = -2, Rot = -1, Fov = -1;
        if (Loc == -2) {
            Loc = -1;
            int32 Cache = int32(Camera->GetOffset("CameraCachePrivate")); if (Cache < 0) Cache = int32(Camera->GetOffset("CameraCache"));
            auto Entry = FindStruct("CameraCacheEntry"), View = FindStruct("MinimalViewInfo");
            const int32 Pov = Entry ? int32(Entry->GetOffset("POV")) : -1;
            const int32 L = View ? int32(View->GetOffset("Location")) : -1, R = View ? int32(View->GetOffset("Rotation")) : -1, F = View ? int32(View->GetOffset("FOV")) : -1;
            if (Cache >= 0 && Pov >= 0 && L >= 0 && R >= 0 && F >= 0) { Loc = Cache + Pov + L; Rot = Cache + Pov + R; Fov = Cache + Pov + F; }
            char Line[160]; snprintf(Line, sizeof(Line), "dev camera: read from memory (Season 8+), location@%d rotation@%d fov@%d", Loc, Rot, Fov); Log(Line);
        }
        if (Loc < 0) return;
        // Season 8 is single precision: three floats each (pitch, yaw, roll for the rotation).
        const float* L = (const float*)(__int64(Camera) + Loc);
        const float* R = (const float*)(__int64(Camera) + Rot);
        DevCameraView->X = L[0]; DevCameraView->Y = L[1]; DevCameraView->Z = L[2];
        DevCameraView->Pitch = R[0]; DevCameraView->Yaw = R[1]; DevCameraView->Roll = R[2];
        DevCameraView->Fov = *(float*)(__int64(Camera) + Fov);
        InterlockedIncrement(&DevCameraView->Seq);
        static int Logged = 0;
        if (Logged < 2) { ++Logged; char Line[160]; snprintf(Line, sizeof(Line), "dev camera: at %.0f, %.0f, %.0f yaw %.0f pitch %.0f fov %.0f", double(DevCameraView->X), double(DevCameraView->Y), double(DevCameraView->Z), double(DevCameraView->Yaw), double(DevCameraView->Pitch), double(DevCameraView->Fov)); Log(Line); }
    }
    void DevCameraExport() {
        if (!DevCameraOn) return;
        if (!DevCameraView) {
            wchar_t Name[64]; swprintf_s(Name, L"Local\\TiltedDevCamera_%lu", GetCurrentProcessId());
            if (HANDLE Map = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, sizeof(DevCamera), Name))
                DevCameraView = (DevCamera*)MapViewOfFile(Map, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(DevCamera)); // kept for the process lifetime
            if (!DevCameraView) return;
        }
        auto World = UWorld::GetWorld();
        auto Instance = World ? World->OwningGameInstance : nullptr;
        if (!Instance) return;
        for (auto Player : Instance->LocalPlayers) {
            auto PC = Player ? (UObject*)Player->PlayerController : nullptr;
            if (!PC) continue;
            if (DevPcCamera == -2) DevPcCamera = int32(PC->GetOffset("PlayerCameraManager"));
            auto Camera = DevPcCamera >= 0 ? *(UObject**)(__int64(PC) + DevPcCamera) : nullptr;
            if (!Camera) return;
            // The camera manager's own functions (reading its CameraCache gave zeros in some seasons). Season 8 and
            // later: read from memory only. Calling these functions there crashed the game within a second of Bot
            // markers going on (and every time you aimed with Dev aim assist, which calls the same ones).
            if (VersionInfo.FortniteVersion >= 8.f) DevReadCameraMemory(Camera);
            else DevReadCamera((DevAimCamera*)Camera);
            if (DevPcPawn == -2) { DevPcPawn = int32(PC->GetOffset("AcknowledgedPawn")); if (DevPcPawn < 0) DevPcPawn = int32(PC->GetOffset("Pawn")); }
            UObject* LocalPawn = DevPcPawn >= 0 ? *(UObject**)(__int64(PC) + DevPcPawn) : nullptr;
            // Season 8 and later: no HUD hook (the Dev Console's own overlay draws the markers instead). Every Season 8
            // squads crash came 20-35 s into the match, when the hook goes on, with the Dev markers switched on.
            if (DevPawnsOn && VersionInfo.FortniteVersion < 8.f) DevDrawInstall(PC);
            else if (DevPawnsOn) { static bool Told = false; if (!Told) { Told = true; Log("dev draw: in-game drawing off in Season 8 and later (Dev Console overlay draws the markers)"); } }
            __try { DevPawnsExport(World, PC, LocalPawn); } __except (EXCEPTION_EXECUTE_HANDLER) { if (DevPawnsView && (DevPawnsView->Seq & 1)) { DevPawnsView->Count = 0; InterlockedIncrement(&DevPawnsView->Seq); } }
            return; // the first local player is the one on screen
        }
    }

    // Offsets, found once by reflection on the game thread.
    int32 PcCameraManager = -2, CamDefaultFov = -1, CamLockedFov = -1, CamCacheFov = -1;
    bool Broken = false;
    bool FindOffsets(UObject* PC, UObject* Camera) {
        if (PcCameraManager == -2) PcCameraManager = int32(PC->GetOffset("PlayerCameraManager"));
        if (CamLockedFov >= 0 || Broken) return !Broken;
        const int32 DefaultFov = int32(Camera->GetOffset("DefaultFOV")), Ortho = int32(Camera->GetOffset("DefaultOrthoWidth"));
        const int32 Cache = int32(Camera->GetOffset("CameraCache"));
        char Line[220];
        if (DefaultFov < 0 || Ortho != DefaultFov + 8) {
            snprintf(Line, sizeof(Line), "camera layout not recognised (DefaultFOV=%d DefaultOrthoWidth=%d): custom FOV is off for this season", DefaultFov, Ortho); Log(Line);
            Broken = true; return false;
        }
        CamDefaultFov = DefaultFov; CamLockedFov = DefaultFov + 4;
        // CameraCache.POV.FOV: the camera mode's own FOV (hip fire / aiming), used to spot aiming down sights.
        auto Entry = FindStruct("CameraCacheEntry"), View = FindStruct("MinimalViewInfo");
        const int32 Pov = Entry ? int32(Entry->GetOffset("POV")) : -1, Fov = View ? int32(View->GetOffset("FOV")) : -1;
        if (Cache >= 0 && Pov >= 0 && Fov >= 0) CamCacheFov = Cache + Pov + Fov;
        snprintf(Line, sizeof(Line), "offsets: PlayerCameraManager=%d DefaultFOV=%d LockedFOV=%d CameraCache.POV.FOV=%d (Fortnite %.2f)",
            PcCameraManager, CamDefaultFov, CamLockedFov, CamCacheFov, double(VersionInfo.FortniteVersion)); Log(Line);
        return true;
    }

    float HipFov = 0.f;      // the camera mode's normal FOV, learned while not aiming
    float PrevModeFov = 0.f; // last tick's camera mode FOV
    int AimTicks = 0;        // ticks in a row counted as aiming
    float LastSet = -1.f;    // what this module last wrote into LockedFOV
    float StretchFov(float Base); // stretched picture FOV (True stretched section below)
    void Apply() {
        const float Custom = float(WantedTimes100) / 100.f;
        const float Stretched = StretchFov(Custom > 0.f ? Custom : 80.f);
        const float Wanted = Stretched > 0.f ? Stretched : Custom;
        auto World = UWorld::GetWorld();
        auto Instance = World ? World->OwningGameInstance : nullptr;
        if (!Instance) return;
        for (auto Player : Instance->LocalPlayers) {
            auto PC = Player ? (UObject*)Player->PlayerController : nullptr;
            if (!PC) continue;
            if (PcCameraManager == -2) PcCameraManager = int32(PC->GetOffset("PlayerCameraManager"));
            if (PcCameraManager < 0) return;
            auto Camera = *(UObject**)(__int64(PC) + PcCameraManager);
            if (!Camera || !FindOffsets(PC, Camera)) return;
            float& Locked = *(float*)(__int64(Camera) + CamLockedFov);
            if (Wanted <= 0.f) {
                // Turned off: give the camera back, once.
                if (LastSet > 0.f && Locked == LastSet) Locked = 0.f;
                LastSet = -1.f; return;
            }
            bool Aiming = false;
            auto Pawn = (TargetingPawn*)LocalPawn();
            if (Pawn && Pawn->HasbIsTargeting()) Aiming = Pawn->GetbIsTargeting();
            else if (CamCacheFov >= 0) {
                const float ModeFov = *(float*)(__int64(Camera) + CamCacheFov);
                if (ModeFov > 20.f && ModeFov < 170.f) {
                    // Aiming zooms in fast (a big drop within a few frames); other camera changes are slower.
                    // The normal FOV follows slow changes and any rise, and stays put during a fast drop.
                    const bool Slow = PrevModeFov > 0.f && fabsf(ModeFov - PrevModeFov) < .5f;
                    if (HipFov <= 0.f || ModeFov > HipFov || (Slow && AimTicks == 0)) HipFov = ModeFov;
                    Aiming = ModeFov < HipFov * .92f;
                    AimTicks = Aiming ? AimTicks + 1 : 0;
                    // A lower FOV that lasts 10 s is a new normal (landing after the skydive), not aiming.
                    if (AimTicks > 600) { HipFov = ModeFov; Aiming = false; AimTicks = 0; }
                    PrevModeFov = ModeFov;
                }
            }
            const float Target = Aiming ? 0.f : Wanted;
            if (Locked != Target) Locked = Target;
            if (LastSet != Target) {
                char Line[120]; snprintf(Line, sizeof(Line), Target > 0 ? "FOV set to %.0f" : "aiming: game zoom (FOV %.0f after)", double(Wanted)); Log(Line);
            }
            LastSet = Target;
            return; // the first local player is the one on screen
        }
    }

    // ---- True stretched (Season 8-X) ------------------------------------------------------------------
    // With the launcher's stretched resolution on (stretched-on.txt), Season 7 and earlier show the stretched
    // view players know: the field of view a 16:9 screen gets, drawn into the narrower picture and stretched.
    // From Season 8 the game locks the other screen axis instead, so a stretched picture showed the plain
    // widescreen view. While stretched is on, the local player's aspect-ratio axis rule is switched to the
    // other axis (logged, so the value each season uses is on record); it is put back when stretched is off.
    volatile LONG StretchOn = 0;
    // Stretched picture: the game's render size (GameUserSettings ResolutionSizeX/Y) and the screen's size, x1000.
    volatile LONG StretchRenderAspect1000 = 0, StretchScreenAspect1000 = 0;
    void ReadStretch() {
        // Stretched = the launcher's switch, or simply a game resolution narrower than the screen (set in game,
        // by hand in GameUserSettings.ini or with another tool): the FOV then follows automatically either way.
        const bool Flag = FlagFile(L"stretched-on.txt");
        {
            wchar_t Base[MAX_PATH]{}; LONG Render = 0;
            if (GetEnvironmentVariableW(L"LOCALAPPDATA", Base, MAX_PATH)) {
                std::wstring File = std::wstring(Base) + L"\\FortniteGame\\Saved\\Config\\WindowsClient\\GameUserSettings.ini";
                FILE* F = nullptr; int W = 0, H = 0;
                if (_wfopen_s(&F, File.c_str(), L"rb") == 0 && F) {
                    char Line[256];
                    while (fgets(Line, sizeof(Line), F)) { if (!strncmp(Line, "ResolutionSizeX=", 16)) W = atoi(Line + 16); else if (!strncmp(Line, "ResolutionSizeY=", 16)) H = atoi(Line + 16); }
                    fclose(F);
                }
                if (W >= 640 && H >= 480) Render = LONG(1000.0 * W / H);
            }
            // What the game really draws: its window's client area. In windowed fullscreen the game ignores
            // ResolutionSizeX/Y and draws at the desktop size, so nothing is stretched and the FOV must not change
            // (it used to be raised anyway, giving a fish-eye 16:9 picture). In exclusive fullscreen at 1440x1080,
            // or with a 4:3 desktop resolution, the window is 1440x1080 and the screen stretches it.
            if (HWND Game = GameWindow) { RECT R{}; if (GetClientRect(Game, &R) && R.right - R.left >= 640 && R.bottom - R.top >= 480) Render = LONG(1000.0 * (R.right - R.left) / (R.bottom - R.top)); }
            // The monitor's real shape: its largest (native) mode, not the current desktop mode. With a custom
            // stretched desktop resolution (NVIDIA/AMD custom resolution, e.g. 1440x1080 on a 16:9 panel) the
            // desktop itself is 4:3 and the graphics card stretches everything; the panel is still 16:9.
            int SW = 0, SH = 0;
            { DEVMODEW Mode{}; Mode.dmSize = sizeof(Mode);
              for (DWORD I = 0; EnumDisplaySettingsW(nullptr, I, &Mode); ++I)
                  if (double(Mode.dmPelsWidth) * Mode.dmPelsHeight > double(SW) * SH) { SW = int(Mode.dmPelsWidth); SH = int(Mode.dmPelsHeight); } }
            if (SW <= 0 || SH <= 0) { SW = GetSystemMetrics(SM_CXSCREEN); SH = GetSystemMetrics(SM_CYSCREEN); }
            const LONG Screen = SW > 0 && SH > 0 ? LONG(1000.0 * SW / SH) : 1778;
            const bool RenderChanged = InterlockedExchange(&StretchRenderAspect1000, Render) != Render;
            const bool ScreenChanged = InterlockedExchange(&StretchScreenAspect1000, Screen) != Screen;
            if (RenderChanged || ScreenChanged) {
                char Line[160]; snprintf(Line, sizeof(Line), "stretched: picture %.3f wide, screen %.3f wide", Render / 1000.0, Screen / 1000.0); Log(Line);
            }
            // Stretched only when the picture really is narrower than the screen (the launcher switch alone is not enough).
            const bool On = Render > 0 && Render < Screen - 10; (void)Flag;
            if (InterlockedExchange(&StretchOn, On ? 1 : 0) != (On ? 1 : 0)) Log(On ? (Flag ? "stretched: on (launcher setting)" : "stretched: on (game resolution narrower than the screen)") : "stretched: off");
        }
    }
    // From Season 7/8 the game keeps the vertical field of view (MaintainYFOV), so a narrower stretched picture
    // only showed a cropped, zoomed-in middle of the widescreen view. Instead of changing the game's axis rule
    // (which left the camera's own aspect ratio fighting it), the FOV is raised so the narrow picture holds the
    // same horizontal view a widescreen one does; the screen then stretches it, which is the OG stretched look:
    //   FOV = 2 * atan( tan(base / 2) * camera aspect / picture aspect )
    // e.g. base 80 at 1440x1080 (1.333) with a 16:9 camera: 96.4. Seasons that keep the horizontal view
    // (MaintainXFOV, Season 7 and earlier on most builds) already stretch correctly and get no change.
    int32 StretchAxisOff = -2; int StretchSaved = -1, StretchWant = -1;
    volatile LONG StretchKeepsY = 0; // 1 = the game keeps the vertical FOV: compensate in Apply()
    void StretchTick() {
        auto World = UWorld::GetWorld();
        auto Instance = World ? World->OwningGameInstance : nullptr;
        if (!Instance || !Instance->LocalPlayers.Num()) return;
        auto LP = (UObject*)Instance->LocalPlayers[0];
        if (!LP) return;
        if (StretchAxisOff == -2) StretchAxisOff = int32(LP->GetOffset("AspectRatioAxisConstraint"));
        if (StretchAxisOff < 0) return;
        auto& Axis = *(uint8_t*)(__int64(LP) + StretchAxisOff);
        static int X = -2, Y = -2;
        if (X == -2) { auto E = SDK::FindEnum("EAspectRatioAxisConstraint"); X = E ? int(E->GetValue("AspectRatio_MaintainXFOV")) : -1; Y = E ? int(E->GetValue("AspectRatio_MaintainYFOV")) : -1; }
        // An axis rule changed by an older build of this module is put back.
        if (StretchSaved >= 0) { Axis = uint8_t(StretchSaved); StretchSaved = -1; }
        const LONG KeepsY = (Y >= 0 && Axis == uint8_t(Y)) ? 1 : 0;
        if (InterlockedExchange(&StretchKeepsY, KeepsY) != KeepsY || StretchWant != int(Axis)) {
            StretchWant = int(Axis);
            char Line[160]; snprintf(Line, sizeof(Line), "stretched: game aspect rule %d (MaintainXFOV=%d, MaintainYFOV=%d, Fortnite %.2f) -> %s", int(Axis), X, Y, double(VersionInfo.FortniteVersion), KeepsY ? "FOV compensated" : "no FOV change needed"); Log(Line);
        }
    }
    // The FOV to use while stretched (0 = no change), from the player's base FOV (their custom FOV, else 80).
    float StretchFov(float Base) {
        if (!StretchOn || !StretchKeepsY) return 0.f;
        const double Picture = StretchRenderAspect1000 / 1000.0, Camera = StretchScreenAspect1000 > 0 ? StretchScreenAspect1000 / 1000.0 : 16.0 / 9.0;
        if (Picture < 0.5 || Picture >= Camera - .01) return 0.f; // not narrower than the screen: nothing to stretch
        const double Rad = 3.14159265358979 / 180.0, B = (Base > 20.f && Base < 170.f ? Base : 80.f) * Rad;
        const double Fov = 2.0 * atan(tan(B / 2.0) * Camera / Picture) / Rad;
        return float(Fov > 150.0 ? 150.0 : Fov);
    }

    // Dev Console features call into the game (camera, players, HUD). Turned on at the main menu or while a
    // map was loading they crashed 6.20 / 7.40 ("Unexpected concurency while adding new object"): the game
    // pumps window messages in the middle of loading, and this hook ran engine calls right then. They now run
    // only in a match world (never the main menu), and only once that world has been up for 8 seconds.
    bool MatchSettled();
    // A match world (not the main menu) loaded at least 15 s ago. No body needed (spawn island, bus).
    bool SquadWorldReady() {
        static UWorld* Last = nullptr; static ULONGLONG ReadyAt = 0; static bool Menu = true;
        auto World = UWorld::GetWorld();
        if (!World) { Last = nullptr; return false; }
        if (World != Last) {
            Last = World; ReadyAt = GetTickCount64() + 15000;
            const std::string Name = World->Name.ToString().c_str();
            Menu = Name.find("Frontend") != std::string::npos || Name.find("FrontEnd") != std::string::npos;
        }
        return !Menu && GetTickCount64() >= ReadyAt;
    }
    bool DevWorldSettled() {
        if (!DevCameraOn && !DevAimLevel && !DevFlyOn && !DevNoRecoil && !DevNoDrop && DevRecoilSaved.empty()) return false;
        return MatchSettled();
    }
    // A match world (not the main menu) that has been up 20 s, with your body for 3 s: engine calls made earlier
    // (while the game is still loading and creating players) crashed some seasons.
    bool MatchSettled() {
        static UWorld* Last = nullptr; static ULONGLONG ReadyAt = 0; static bool Menu = true;
        auto World = UWorld::GetWorld();
        if (!World) { Last = nullptr; return false; }
        if (World != Last) {
            Last = World; ReadyAt = GetTickCount64() + 20000;
            const std::string Name = World->Name.ToString().c_str();
            Menu = Name.find("Frontend") != std::string::npos || Name.find("FrontEnd") != std::string::npos;
            char Line[128]; snprintf(Line, sizeof(Line), "world %s (%s)", Name.c_str(), Menu ? "main menu: match features wait for a match" : "match: match features start 20 s after you have a body"); Log(Line);
        }
        if (Menu || GetTickCount64() < ReadyAt) return false;
        // Also wait until you have a body: before that (bus, loading) the game is still creating the players'
        // characters and outfits, and engine calls made then crashed it ("Unexpected concurency while adding
        // new object", 4.5, exactly when the features started 8 s after the island loaded).
        static ULONGLONG BodySince = 0;
        if (!LocalPawn()) { BodySince = 0; return false; }
        if (!BodySince) BodySince = GetTickCount64();
        return GetTickCount64() - BodySince >= 3000;
    }
    // ---- Squad list health of bot teammates (every player, not only the Dev Kit) -------------------------------
    // Fortnite never sends a bot's health to players: every game saw 1 for every bot (0 on its player info), so the
    // bot teammates' bars in the squad list top left stayed empty. The match server on this PC shares its bot
    // teammates' health (Local\SyndicateSquadHealth, teammates only); it is put on those bots in this game: their
    // health set (what the game shows for a character) and the copy on their player info.
    struct SquadHealthItem { char Name[64]; float Hp, Sh; LONG Alive; };
    struct SquadHealthShare { volatile LONG Seq; LONG Count; SquadHealthItem Items[16]; };
    volatile LONG SquadShared = 0; // set by the background thread when the server shares something
    SquadHealthShare* SquadView() {
        static SquadHealthShare* View = nullptr; static ULONGLONG Retry = 0;
        if (!View && GetTickCount64() >= Retry) {
            Retry = GetTickCount64() + 2000;
            if (HANDLE Map = OpenFileMappingW(FILE_MAP_READ, FALSE, L"Local\\TiltedSquadHealth")) {
                // The handle stays open: it keeps the name alive, so the next match server (a new process each match)
                // opens this same shared memory instead of creating a new one this game never sees. Closing it here
                // made every match after the first one share nothing ("server shares 0").
                View = (SquadHealthShare*)MapViewOfFile(Map, FILE_MAP_READ, 0, 0, sizeof(SquadHealthShare));
                if (!View) CloseHandle(Map);
            }
        }
        return View;
    }
    void SquadSetAttribute(UObject* Set, const char* Name, float Value) {
        const int32 Off = int32(Set->GetOffset(Name)); if (Off < 0) return;
        float* Data = (float*)(__int64(Set) + Off);
        Data[2] = Value; Data[3] = Value; // FortGameplayAttributeData: BaseValue at 8, CurrentValue at 12
    }
    // One-time look at the squad list widgets: their class, and every field with its value (numbers, and which
    // object a reference points to), to find what the bars are drawn from. Written to syndicate-client.log.
    void SquadHudDumpObject(const UObject* Obj) {
        char Line[300];
        snprintf(Line, sizeof(Line), "squad hud: object %s (%s)", Obj->Name.ToString().c_str(), Obj->Class->Name.ToString().c_str()); Log(Line);
        int Lines = 0;
        for (auto T = (const UStruct*)Obj->Class; T && Lines < 120; T = T->GetSuper()) {
            const std::string TName = T->Name.ToString().c_str();
            if (TName == "UserWidget" || TName == "Widget" || TName == "Object" || TName == "Actor" || TName == "Info") break;
            for (auto F = T->GetChildren(); F && Lines < 120; F = F->GetNext()) {
                const std::string Kind = F->Class->Name.ToString().c_str(), Name = F->Name.ToString().c_str();
                if (Kind == "Function") {
                    if (Name.find("ealth") != std::string::npos || Name.find("hield") != std::string::npos || Name.find("Update") != std::string::npos || Name.find("OnRep") != std::string::npos) {
                        snprintf(Line, sizeof(Line), "squad hud:   [%s] fn %s", TName.c_str(), Name.c_str()); Log(Line); ++Lines;
                    }
                    continue;
                }
                const int32 Off = int32(GetFromOffset<uint32>(F, Offsets::Offset_Internal));
                std::string Value;
                if (Kind == "FloatProperty") { char B[32]; snprintf(B, sizeof(B), "%.2f", double(*(float*)(__int64(Obj) + Off))); Value = B; }
                else if (Kind == "IntProperty") Value = std::to_string(*(int32*)(__int64(Obj) + Off));
                else if (Kind == "ByteProperty" || Kind == "EnumProperty") Value = std::to_string(int(*(uint8_t*)(__int64(Obj) + Off)));
                else if (Kind == "ObjectProperty" || Kind == "ClassProperty") {
                    auto O = *(UObject**)(__int64(Obj) + Off);
                    Value = O ? std::string(O->Name.ToString().c_str()) + " (" + O->Class->Name.ToString().c_str() + ")" : "null";
                }
                snprintf(Line, sizeof(Line), "squad hud:   [%s] %s %s @%d = %s", TName.c_str(), Kind.c_str(), Name.c_str(), Off, Value.c_str()); Log(Line); ++Lines;
            }
        }
    }
    bool SquadHudDumpSafe(const UObject* Obj) { __try { SquadHudDumpObject(Obj); return true; } __except (EXCEPTION_EXECUTE_HANDLER) { return false; } }
    void SquadHudDump() {
        Log("squad hud: looking for the squad list widgets");
        auto Match = [](const std::string& N) {
            for (const char* K : { "TeamMember", "Teammate", "SquadMember", "MemberHealth", "PlayerHealth", "HealthBar", "TeamInfo", "PartyMember", "TeamStatus", "SquadStatus", "MemberStatus" })
                if (N.find(K) != std::string::npos) return true;
            return false;
        };
        // Classes the game knows with those names (native and blueprint), once each.
        std::vector<std::string> Classes;
        for (int i = 0; i < TUObjectArray::Num() && Classes.size() < 80; ++i) {
            auto Obj = TUObjectArray::GetObjectByIndex(i); if (!Obj || !Obj->Class) continue;
            const std::string Meta = Obj->Class->Name.ToString().c_str();
            if (Meta != "Class" && Meta != "WidgetBlueprintGeneratedClass" && Meta != "BlueprintGeneratedClass") continue;
            const std::string Name = Obj->Name.ToString().c_str();
            if (Match(Name)) Classes.push_back(Name);
        }
        for (auto& C : Classes) { std::string L = "squad hud: class " + C; Log(L.c_str()); }
        // Live instances (not templates) of those classes, with their fields.
        int Found = 0;
        for (int i = 0; i < TUObjectArray::Num() && Found < 8; ++i) {
            auto Obj = TUObjectArray::GetObjectByIndex(i); if (!Obj || !Obj->Class) continue;
            const std::string Name = Obj->Name.ToString().c_str();
            if (Name.rfind("Default__", 0) == 0 || Name == "WidgetArchetype") continue;
            const std::string Class = Obj->Class->Name.ToString().c_str();
            if (!Match(Class)) continue;
            if (!SquadHudDumpSafe(Obj)) Log("squad hud: (object could not be read)");
            ++Found;
        }
        char Line[96]; snprintf(Line, sizeof(Line), "squad hud: %d live objects listed", Found); Log(Line);
    }
    void SquadHudDumpTick() {
        static ULONGLONG DumpAt = 0; static bool Dumped = false;
        if (Dumped) return;
        if (!DumpAt) { DumpAt = GetTickCount64() + 30000; return; }
        if (GetTickCount64() >= DumpAt) { Dumped = true; SquadHudDump(); }
    }
    // The squad list top left: one HUD-TeamMemberInfo widget per teammate. Its bars come from its MemberInfo
    // (filled by the game from attribute-change events, which never fire for bots). Put the bot teammates'
    // health/shield into that MemberInfo and ask the widget to redraw (UpdateHealthData).
    // The squad list top left: one HUD-TeamMemberInfo widget per teammate. Its bar is drawn by the widget's own
    // SetHealth(Value) / UpdateHealthData(Healths, Shields), which the game calls from attribute-change events that
    // never fire for bots. Its MemberInfo (FortTeamMemberInfo) holds the teammate's name (PlayerName, text) but no
    // health. So: read the name, find that bot's health from the match server, and call those two functions.
    std::vector<UObject*> SquadWidgets; std::vector<int32> SquadWidgetIndex;
    struct SquadRow { UObject* Widget; int32 Index; std::string Name; };
    std::vector<SquadRow> SquadMatched;           // visible rows and the bot each one shows
    SquadHealthItem SquadLastItems[16]; LONG SquadLastCount = 0; // the latest health from the match server
    struct SquadCall { int32 HealthsOff = -1, ShieldsOff = -1, ValueOff = -1; uint32 UpdateSize = 0, SetSize = 0; bool Logged = false; };
    SquadCall SquadCalls;
    int32 SquadMemberOff = -2;
    void SquadDescribeFunction(UObject* Widget, const char* Name) {
        auto Fn = Widget->GetFunction(Name); if (!Fn) { std::string L = std::string("squad hud: fn ") + Name + " not found"; Log(L.c_str()); return; }
        std::string L = std::string("squad hud: fn ") + Name + " params:";
        for (auto F = ((const UStruct*)Fn)->GetChildren(); F; F = F->GetNext()) {
            if (!(GetFromOffset<uint64>(F, Offsets::PropertyFlags) & 0x80)) continue; // parameters only
            L += " "; L += F->Name.ToString().c_str(); L += "("; L += F->Class->Name.ToString().c_str();
            if (std::string(F->Class->Name.ToString().c_str()) == "ArrayProperty") if (auto In = GetFromOffset<const UObject*>(F, 0x70)) { L += " of "; L += In->Class->Name.ToString().c_str(); }
            L += "@" + std::to_string(GetFromOffset<uint32>(F, Offsets::Offset_Internal)) + ")";
        }
        Log(L.c_str());
    }
    std::string SquadTextToString(FText& Text) {
        auto Str = UKismetTextLibrary::Conv_TextToString(Text); // (FText::ToString is private in the SDK)
        std::string Out = Str.ToString().c_str(); Str.Free();
        return Out;
    }
    // The name shown on the widget (its TextPlayerName text block): the game fills the widget's MemberInfo only for
    // real players, so for bots that one is empty, but the name on screen is right.
    std::string SquadWidgetName(UObject* W) {
        static int32 TextOff = -2; if (TextOff == -2) TextOff = int32(W->GetOffset("TextPlayerName"));
        static int32 InfoOff = -2; if (InfoOff == -2) InfoOff = int32(W->GetOffset("PlayerInfo"));
        std::string Out, Kind;
        // The player the row belongs to (a weak pointer: object index + serial). Its name is the real one; the text
        // block only holds the designer's "Player" because the shown name comes from a binding.
        if (InfoOff >= 0) {
            const int32 Index = *(int32*)((uint8_t*)W + InfoOff);
            if (Index > 0 && Index < TUObjectArray::Num()) if (auto Obj = (UObject*)TUObjectArray::GetObjectByIndex(Index)) if (Obj->Class) {
                Kind = Obj->Class->Name.ToString().c_str();
                UObject* State = nullptr;
                if (int32(Obj->GetOffset("PlayerName")) >= 0 || Obj->GetFunction("GetPlayerName")) State = Obj;
                else if (const int32 Off = int32(Obj->GetOffset("PlayerState")); Off >= 0) State = *(UObject**)((uint8_t*)Obj + Off);
                if (State) Out = DevPlayerName(State);
            }
        }
        if (!Out.empty()) { static int Logged = 0; if (Logged < 8) { ++Logged; std::string L = "squad hud: row belongs to \"" + Out + "\" (" + Kind + ")"; Log(L.c_str()); } return Out; }
        if (!Kind.empty()) { static int Logged = 0; if (Logged < 4) { ++Logged; std::string L = "squad hud: row points at a " + Kind + " with no name"; Log(L.c_str()); } }
        if (TextOff >= 0) if (auto Block = *(UObject**)((uint8_t*)W + TextOff)) if (auto Fn = Block->GetFunction("GetText")) {
            alignas(16) uint8_t Buf[64]{};
            Block->ProcessEvent(Fn, Buf);
            Out = SquadTextToString(*(FText*)Buf);
        }
        if (Out.empty()) {
            if (SquadMemberOff == -2) SquadMemberOff = int32(W->GetOffset("MemberInfo"));
            if (SquadMemberOff >= 0) Out = SquadTextToString(*(FText*)((uint8_t*)W + SquadMemberOff + 136)); // FortTeamMemberInfo.PlayerName
        }
        if (Out == "Player") Out.clear(); // the designer's placeholder, not a name
        return Out;
    }
    // Names match ignoring case and surrounding spaces (the widget may also show the name in upper case).
    static bool SquadSameName(const std::string& A, const char* B) {
        auto Trim = [](std::string X) { while (!X.empty() && (unsigned char)X.back() <= ' ') X.pop_back(); size_t i = 0; while (i < X.size() && (unsigned char)X[i] <= ' ') ++i; X.erase(0, i); for (auto& c : X) c = char(tolower((unsigned char)c)); return X; };
        auto L = Trim(A), R = Trim(B); return !L.empty() && L == R;
    }
    static std::string SquadFnParams(UFunction* Fn) {
        std::string L;
        for (auto F = ((const UStruct*)Fn)->GetChildren(); F; F = F->GetNext()) {
            if (!(GetFromOffset<uint64>(F, Offsets::PropertyFlags) & 0x80)) continue;
            L += " "; L += F->Name.ToString().c_str(); L += "("; L += F->Class->Name.ToString().c_str();
            L += "@" + std::to_string(GetFromOffset<uint32>(F, Offsets::Offset_Internal)) + ")";
        }
        return L;
    }
    // Every function and widget field of the class, once per class, so the next fix never has to guess.
    void SquadDescribeClass(UObject* Obj) {
        static std::vector<std::string> Done;
        const std::string Cls = Obj->Class->Name.ToString().c_str();
        for (auto& D : Done) if (D == Cls) return;
        Done.push_back(Cls); if (Done.size() > 24) return;
        std::string Chain; for (auto T = (const UStruct*)Obj->Class; T; T = T->GetSuper()) { if (!Chain.empty()) Chain += " < "; Chain += T->Name.ToString().c_str(); }
        Log(("squad row class: " + Chain).c_str());
        int Lines = 0;
        for (auto T = (const UStruct*)Obj->Class; T && Lines < 80; T = T->GetSuper()) {
            const std::string TName = T->Name.ToString().c_str();
            if (TName == "UserWidget" || TName == "Widget" || TName == "Object") break;
            for (auto F = T->GetChildren(); F && Lines < 80; F = F->GetNext()) {
                const std::string Kind = F->Class->Name.ToString().c_str(), Name = F->Name.ToString().c_str();
                std::string L;
                if (Kind == "Function") L = "  [" + TName + "] fn " + Name + SquadFnParams((UFunction*)F);
                else if (Kind == "ObjectProperty") {
                    auto O = *(UObject**)((uint8_t*)Obj + GetFromOffset<uint32>(F, Offsets::Offset_Internal));
                    L = "  [" + TName + "] " + Name + " = " + (O ? std::string(O->Class->Name.ToString().c_str()) : std::string("null"));
                } else L = "  [" + TName + "] " + Kind + " " + Name + " @" + std::to_string(GetFromOffset<uint32>(F, Offsets::Offset_Internal));
                Log(("squad row:" + L).c_str()); ++Lines;
            }
        }
    }
    std::string SquadBlockText(UObject* Block) {
        std::string Out;
        if (auto Fn = Block->GetFunction("GetText")) { alignas(16) uint8_t Buf[64]{}; Block->ProcessEvent(Fn, Buf); Out = SquadTextToString(*(FText*)Buf); }
        return Out;
    }
    // Calls Fn(Value) when it takes exactly one float.
    static bool SquadCallFloat(UObject* Obj, UFunction* Fn, float Value) {
        int Params = 0; int32 Off = -1; bool Float = false;
        for (auto F = ((const UStruct*)Fn)->GetChildren(); F; F = F->GetNext()) {
            if (!(GetFromOffset<uint64>(F, Offsets::PropertyFlags) & 0x80)) continue;
            ++Params; Off = int32(GetFromOffset<uint32>(F, Offsets::Offset_Internal)); Float = std::string(F->Class->Name.ToString().c_str()) == "FloatProperty";
        }
        if (Params != 1 || !Float || Off < 0 || Off > 56) return false;
        alignas(16) uint8_t Buf[64]{}; *(float*)(Buf + Off) = Value; Obj->ProcessEvent(Fn, Buf);
        return true;
    }
    // Which bot a row is for: the name in one of its text blocks (the on-screen name), or the player it points at.
    const SquadHealthItem* SquadRowBot(UObject* W, const SquadHealthItem* Items, LONG Count, std::string& Seen) {
        auto Pick = [&](const std::string& N) -> const SquadHealthItem* { for (LONG k = 0; k < Count; ++k) if (SquadSameName(N, Items[k].Name)) return &Items[k]; return nullptr; };
        for (auto T = (const UStruct*)W->Class; T; T = T->GetSuper()) {
            const std::string TName = T->Name.ToString().c_str();
            if (TName == "UserWidget" || TName == "Object") break;
            for (auto F = T->GetChildren(); F; F = F->GetNext()) {
                if (std::string(F->Class->Name.ToString().c_str()) != "ObjectProperty") continue;
                auto O = *(UObject**)((uint8_t*)W + GetFromOffset<uint32>(F, Offsets::Offset_Internal)); if (!O || !O->Class) continue;
                const std::string C = O->Class->Name.ToString().c_str();
                std::string N;
                if (C.find("TextBlock") != std::string::npos) N = SquadBlockText(O);
                else if (O->GetFunction("GetPlayerName")) N = DevPlayerName(O);
                if (N.empty()) continue;
                if (Seen.size() < 120) { if (!Seen.empty()) Seen += ", "; Seen += N; }
                if (auto Hit = Pick(N)) return Hit;
            }
        }
        return nullptr;
    }
    // Fills one row's bars: the row's own health functions, and any health/shield progress bar or bar widget in it.
    void SquadFillRow(UObject* W, const SquadHealthItem* Mine, std::string& Done) {
        const float H = Mine->Hp / 100.f, Sh = Mine->Sh / 100.f;
        // The row's knocked icon (the circle next to the bar): the game set it when the bot was knocked and, with a bot
        // revived by the server, never cleared it. Only when it changes.
        if (Mine->Alive == 1 || Mine->Alive == 2) if (auto Fn = W->GetFunction("SetDBNO")) {
            static std::unordered_map<UObject*, LONG> Shown;
            auto& Last = Shown[W];
            if (Last != Mine->Alive) {
                int Params = 0; int32 Off = -1;
                for (auto F = ((const UStruct*)Fn)->GetChildren(); F; F = F->GetNext()) if (GetFromOffset<uint64>(F, Offsets::PropertyFlags) & 0x80) { ++Params; Off = int32(GetFromOffset<uint32>(F, Offsets::Offset_Internal)); }
                if (Params == 1 && Off >= 0 && Off < 56) { alignas(16) uint8_t Buf[64]{}; Buf[Off] = Mine->Alive == 2 ? 1 : 0; W->ProcessEvent(Fn, Buf); Done += Mine->Alive == 2 ? " SetDBNO(on)" : " SetDBNO(off)"; }
                Last = Mine->Alive;
            }
        }
        if (const int32 ShowOff = int32(W->GetOffset("ShowingHealth")); ShowOff >= 0) *((uint8_t*)W + ShowOff) = 1;
        if (auto Update = W->GetFunction("UpdateHealthData")) {
            int32 HOff = -1, SOff = -1; const auto Size = Update->GetParamsNamed().Size;
            for (auto& P : Update->GetParamsNamed().NameOffsetMap) { if (P.Name == "Healths") HOff = int32(P.Offset); if (P.Name == "Shields") SOff = int32(P.Offset); }
            if (HOff >= 0 && SOff >= 0 && Size <= 256) {
                alignas(16) uint8_t Buf[256]{}; TArray<float> Hs, Ss; Hs.Add(H); Ss.Add(Sh);
                memcpy(Buf + HOff, &Hs, sizeof(Hs)); memcpy(Buf + SOff, &Ss, sizeof(Ss));
                W->ProcessEvent(Update, Buf); Hs.Free(); Ss.Free(); Done += " UpdateHealthData";
            }
        }
        for (const char* Name : { "SetHealth", "SetHealthPercent", "UpdateHealth", "SetHealthPct" }) if (auto Fn = W->GetFunction(Name)) if (SquadCallFloat(W, Fn, H)) { Done += " "; Done += Name; }
        for (const char* Name : { "SetShield", "SetShieldPercent", "UpdateShield", "SetShieldPct" }) if (auto Fn = W->GetFunction(Name)) if (SquadCallFloat(W, Fn, Sh)) { Done += " "; Done += Name; }
        // Child bars (progress bars or bar widgets) whose field name says health or shield.
        for (auto T = (const UStruct*)W->Class; T; T = T->GetSuper()) {
            const std::string TName = T->Name.ToString().c_str();
            if (TName == "UserWidget" || TName == "Object") break;
            for (auto F = T->GetChildren(); F; F = F->GetNext()) {
                if (std::string(F->Class->Name.ToString().c_str()) != "ObjectProperty") continue;
                std::string Field = F->Name.ToString().c_str(); for (auto& c : Field) c = char(tolower((unsigned char)c));
                const bool IsHp = Field.find("health") != std::string::npos, IsSh = Field.find("shield") != std::string::npos;
                if (!IsHp && !IsSh) continue;
                auto O = *(UObject**)((uint8_t*)W + GetFromOffset<uint32>(F, Offsets::Offset_Internal)); if (!O || !O->Class) continue;
                const float V = IsSh ? Sh : H;
                for (const char* Name : { "SetPercent", "SetHealth", "SetShield", "SetValue", "SetFillPercent" })
                    if (auto Fn = O->GetFunction(Name)) if (SquadCallFloat(O, Fn, V)) { Done += " "; Done += F->Name.ToString().c_str(); Done += "."; Done += Name; break; }
            }
        }
    }
    void SquadWidgetsTick(const SquadHealthItem* Items, LONG Count) {
        static ULONGLONG NextFind = 0; static bool AnyMatched = false;
        if (GetTickCount64() >= NextFind) {
            // Looking through every object is the costly part: every 2 s until the rows are found, then every 10 s.
            NextFind = GetTickCount64() + (AnyMatched ? 10000 : 2000); SquadWidgets.clear(); SquadWidgetIndex.clear();
            // The match's squad list rows are AthenaTeamMemberInfo widgets; HUD-TeamMemberInfo is the older one
            // (Save the World / hidden rows that only show the placeholder "Player").
            for (int i = 0; i < TUObjectArray::Num(); ++i) {
                auto Obj = TUObjectArray::GetObjectByIndex(i); if (!Obj || !Obj->Class) continue;
                const std::string Cls = Obj->Class->Name.ToString().c_str();
                if (Cls.find("TeamMemberInfo") == std::string::npos && Cls.find("SquadMember") == std::string::npos && Cls.find("TeamMemberHealth") == std::string::npos) continue;
                const std::string Name = Obj->Name.ToString().c_str();
                if (Name.rfind("Default__", 0) == 0 || Name == "WidgetArchetype") continue;
                if (!((UObject*)Obj)->GetFunction("IsVisible")) continue; // widgets only
                SquadWidgets.push_back((UObject*)Obj);
                SquadWidgetIndex.push_back(Obj->Index);
            }
        }
        static int Told = 0;
        int Visible = 0, Matched = 0; std::string Report;
        SquadMatched.clear();
        for (size_t i = 0; i < SquadWidgets.size(); ++i) {
            auto W = SquadWidgets[i];
            // A row removed since the last look (the game reuses its slot): skip it, never touch freed memory.
            if (TUObjectArray::GetObjectByIndex(SquadWidgetIndex[i]) != W) { NextFind = 0; continue; }
            // Visible rows are counted for the log, but every row is tried: Season 8 reported its squad list rows as
            // not visible, and a row only gets health when the name it shows is one of the bot teammates.
            if (auto Fn = W->GetFunction("IsVisible")) { struct { bool Ret; } P{}; W->ProcessEvent(Fn, &P); if (P.Ret) ++Visible; }
            std::string Seen;
            auto Mine = SquadRowBot(W, Items, Count, Seen);
            if (Told < 3 && Report.size() < 400) Report += std::string(" | ") + W->Class->Name.ToString().c_str() + " [" + Seen + "]";
            if (!Mine) continue;
            ++Matched;
            SquadMatched.push_back({ W, SquadWidgetIndex[i], std::string(Mine->Name) });
        }
        AnyMatched = Matched > 0;
        if (Told < 3) { ++Told; char Line[600]; snprintf(Line, sizeof(Line), "squad hud: %d rows, %d visible, %d matched a bot:%s", int(SquadWidgets.size()), Visible, Matched, Report.c_str()); Log(Line); }
    }
    // Every time the game thread comes by (about every 6-16 ms): the matched rows get their bot's health again.
    // The game redraws a bot's bar from its own (empty) data now and then; filling it only once a second made
    // the bar flicker between full and empty. Rows are matched to bots in SquadWidgetsTick (every 150 ms).
    void SquadFillRowMinimal(UObject* W, const SquadHealthItem* Mine); // defined below
    void SquadRowsFastTick() {
        static int Filled = 0;
        // Season 7 and later: only the row's SetHealth / SetShield setters, never UpdateHealthData or the child
        // progress-bar calls. Those crashed Season 7 and Season 8 squads (the game's own script recursed). Season 6
        // and earlier keep the full fill they are confirmed working with.
        const bool Full = VersionInfo.FortniteVersion < 7.f;
        for (auto& R : SquadMatched) {
            if (TUObjectArray::GetObjectByIndex(R.Index) != R.Widget) continue; // gone since it was matched
            const SquadHealthItem* Mine = nullptr;
            for (LONG k = 0; k < SquadLastCount; ++k) if (R.Name == SquadLastItems[k].Name) { Mine = &SquadLastItems[k]; break; }
            if (!Mine) continue;
            std::string Done;
            if (Full) SquadFillRow(R.Widget, Mine, Done); else { SquadFillRowMinimal(R.Widget, Mine); Done = " SetHealth/SetShield only"; }
            if (Filled < 6) { ++Filled; char Line[300]; snprintf(Line, sizeof(Line), "squad hud: %s (%s) health %.0f shield %.0f via%s", Mine->Name, R.Widget->Class->Name.ToString().c_str(), double(Mine->Hp), double(Mine->Sh), Done.empty() ? " nothing (no health function found)" : Done.c_str()); Log(Line); }
        }
    }
    void SquadHealthTick() {
        static ULONGLONG Next = 0; if (GetTickCount64() < Next) return; Next = GetTickCount64() + 150;
        auto View = SquadView(); if (!View) return;
        SquadHealthItem Items[16]; LONG Count = 0;
        for (int Try = 0; Try < 3; ++Try) {
            const LONG Seq = View->Seq; if (Seq & 1) { Count = 0; continue; }
            Count = View->Count; if (Count < 0 || Count > 16) return;
            memcpy(Items, View->Items, sizeof(SquadHealthItem) * size_t(Count));
            if (View->Seq == Seq) break; Count = 0;
        }
        if (Count <= 0) return;
        // Season 8 and later: only the squad list rows are filled; the bots' own health in this game and their
        // player info are left alone (those run the game's update code; Season 8 was crashing).
        if (VersionInfo.FortniteVersion >= 8.f) {
            memcpy(SquadLastItems, Items, sizeof(SquadHealthItem) * size_t(Count)); SquadLastCount = Count;
            SquadWidgetsTick(Items, Count);
            return;
        }
        auto World = UWorld::GetWorld(); if (!World) return;
        static const UClass* PawnClass = FindClass("FortPlayerPawnAthena"); if (!PawnClass) return;
        static ActorList Found;
        SyndicateAllActors(World, PawnClass, Found);
        static int32 PsOff = -2, SetOff = -2;
        static int Logged = 0;
        for (int i = 0; i < Found.Num(); ++i) {
            auto Pawn = (UObject*)Found[i]; if (!Pawn) continue;
            if (PsOff == -2) { PsOff = int32(Pawn->GetOffset("PlayerState")); SetOff = int32(Pawn->GetOffset("HealthSet")); }
            auto State = PsOff >= 0 ? *(DevAimState**)(__int64(Pawn) + PsOff) : nullptr;
            if (!State || !State->HasbIsABot() || !State->GetbIsABot()) continue; // bots only
            auto& Cached = DevNames[Pawn];
            if (Cached.State != (UObject*)State) { Cached.State = (UObject*)State; Cached.Name = DevPlayerName((UObject*)State); }
            const SquadHealthItem* Mine = nullptr;
            for (int Pass = 0; Pass < 2 && !Mine; ++Pass) {
                if (Pass == 1) Cached.Name = DevPlayerName((UObject*)State); // bots are renamed when they join your squad
                for (LONG k = 0; k < Count; ++k) if (Cached.Name == Items[k].Name) { Mine = &Items[k]; break; }
            }
            if (!Mine) continue;
            if (auto Set = SetOff >= 0 ? *(UObject**)(__int64(Pawn) + SetOff) : nullptr) {
                SquadSetAttribute(Set, "Health", Mine->Hp); SquadSetAttribute(Set, "MaxHealth", 100.f);
                SquadSetAttribute(Set, "CurrentShield", Mine->Sh); SquadSetAttribute(Set, "MaxShield", 100.f); // ("Shield" is the maximum in early seasons)
            }
            static int32 Hp = -2, MaxHp = -2, Sh = -2, MaxSh = -2;
            if (Hp == -2) { Hp = int32(State->GetOffset("CurrentHealth")); MaxHp = int32(State->GetOffset("MaxHealth")); Sh = int32(State->GetOffset("CurrentShield")); MaxSh = int32(State->GetOffset("MaxShield")); }
            bool Changed = false;
            auto Put = [&](int32 Off, float V) { if (Off < 0) return; float& F = *(float*)(__int64(State) + Off); if (F != V) { F = V; Changed = true; } };
            Put(Hp, Mine->Hp); Put(MaxHp, 100.f); Put(Sh, Mine->Sh); Put(MaxSh, 100.f);
            if (Changed) for (const char* Rep : { "OnRep_CurrentHealth", "OnRep_MaxHealth", "OnRep_CurrentShield", "OnRep_MaxShield" })
                if (auto Fn = State->GetFunction(Rep)) if (Fn->GetParamsNamed().Size == 0) State->ProcessEvent(Fn, nullptr);
            if (Logged < 4) { ++Logged; char Line[160]; snprintf(Line, sizeof(Line), "squad bars: %s health %.0f shield %.0f put on the bot in this game", Cached.Name.c_str(), double(Mine->Hp), double(Mine->Sh)); Log(Line); }
        }
        memcpy(SquadLastItems, Items, sizeof(SquadHealthItem) * size_t(Count)); SquadLastCount = Count;
        SquadWidgetsTick(Items, Count);
    }
    // Season 8 and later: the squad list (top left) draws each teammate's bars natively from the player state's
    // CurrentHealth / MaxHealth / CurrentShield / MaxShield in YOUR game. Fortnite fills those for real players
    // from the team's private data, which is keyed by account id; bots have none, so their bars stayed empty.
    // Here the bots' numbers from the match server are written straight into those fields (memory only: no game
    // functions, no widgets; calling the row widget's own functions is what crashed Season 8 duos/squads).
    void SquadPlayerStateTick(bool CallOnRep = false) {
        static ULONGLONG Next = 0; if (GetTickCount64() < Next) return; Next = GetTickCount64() + 100;
        auto View = SquadView(); if (!View) return;
        SquadHealthItem Items[16]; LONG Count = 0;
        for (int Try = 0; Try < 3; ++Try) {
            const LONG Seq = View->Seq; if (Seq & 1) { Count = 0; continue; }
            Count = View->Count; if (Count < 0 || Count > 16) return;
            memcpy(Items, View->Items, sizeof(SquadHealthItem) * size_t(Count));
            if (View->Seq == Seq) break; Count = 0;
        }
        if (Count <= 0) return;
        auto World = UWorld::GetWorld(); if (!World) return;
        auto GS = (UObject*)World->GameState; if (!GS) return;
        static int32 ArrOff = -2; if (ArrOff == -2) ArrOff = int32(GS->GetOffset("PlayerArray"));
        if (ArrOff < 0) return;
        auto& Players = *(TArray<UObject*>*)(__int64(GS) + ArrOff);
        if (Players.Num() <= 0 || Players.Num() > 200) return;
        static int32 NameOff = -2, Hp = -2, MaxHp = -2, Sh = -2, MaxSh = -2;
        static int Logged = 0;
        for (int i = 0; i < Players.Num(); ++i) {
            auto State = Players[i]; if (!State || !State->Class) continue;
            if (NameOff == -2) {
                NameOff = int32(State->GetOffset("PlayerNamePrivate")); if (NameOff < 0) NameOff = int32(State->GetOffset("PlayerName"));
                Hp = int32(State->GetOffset("CurrentHealth")); MaxHp = int32(State->GetOffset("MaxHealth"));
                Sh = int32(State->GetOffset("CurrentShield")); MaxSh = int32(State->GetOffset("MaxShield"));
                char Line[200]; snprintf(Line, sizeof(Line), "squad bars: player state name@%d CurrentHealth@%d MaxHealth@%d CurrentShield@%d MaxShield@%d", NameOff, Hp, MaxHp, Sh, MaxSh); Log(Line);
            }
            if (NameOff < 0 || Hp < 0) return;
            // The name, one byte per character (the same way the match server shares it). Matched by name only: the
            // shared list holds only your bot teammates (the bot flag is not always sent to your game).
            auto& Text = *(TArray<wchar_t>*)(__int64(State) + NameOff);
            std::string Name;
            if (Text.Data && Text.Num() > 1 && Text.Num() <= 64) for (int c = 0; c < Text.Num() && Text.Data[c]; ++c) Name += char(Text.Data[c]);
            const SquadHealthItem* Mine = nullptr;
            if (!Name.empty()) for (LONG k = 0; k < Count; ++k) if (SquadSameName(Name, Items[k].Name)) { Mine = &Items[k]; break; }
            static int Listed = 0;
            if (!Mine) {
                // What the squad list reads for a real player: shows whether these fields are the ones it draws from.
                if (Listed < 12) { ++Listed; char Line[200]; snprintf(Line, sizeof(Line), "squad bars: player state %d '%s' (name length %d) health %.0f/%.0f shield %.0f/%.0f, not a shared bot teammate", i, Name.c_str(), Text.Num(), double(*(float*)(__int64(State) + Hp)), double(MaxHp >= 0 ? *(float*)(__int64(State) + MaxHp) : -1.f), double(Sh >= 0 ? *(float*)(__int64(State) + Sh) : -1.f), double(MaxSh >= 0 ? *(float*)(__int64(State) + MaxSh) : -1.f)); Log(Line); }
                continue;
            }
            auto Put = [&](int32 Off, float V) { if (Off >= 0) *(float*)(__int64(State) + Off) = V; };
            const float Before = *(float*)(__int64(State) + Hp), ShBefore = Sh >= 0 ? *(float*)(__int64(State) + Sh) : 0.f;
            Put(Hp, Mine->Hp); Put(MaxHp, 100.f); Put(Sh, Mine->Sh); Put(MaxSh, 100.f);
            // Season 4: the squad list updates when the values replicate (OnRep), so tell it - only on a change.
            if (CallOnRep && (Before != Mine->Hp || ShBefore != Mine->Sh))
                for (const char* Rep : { "OnRep_CurrentHealth", "OnRep_MaxHealth", "OnRep_CurrentShield", "OnRep_MaxShield" })
                    if (auto Fn = State->GetFunction(Rep)) if (Fn->GetParamsNamed().Size == 0) State->ProcessEvent(Fn, nullptr);
            if (Logged < 4) { ++Logged; char Line[200]; snprintf(Line, sizeof(Line), "squad bars: %s health %.0f shield %.0f put on its player state (was %.0f)", Name.c_str(), double(Mine->Hp), double(Mine->Sh), double(Before)); Log(Line); }
        }
    }
    // Season 8 squad bars, the safe version. The full SquadFillRow crashed duos/squads (the BP recursion came from
    // UpdateHealthData and the child progress-bar functions). This fills the bars with ONLY the row's own SetHealth /
    // SetShield float setters, which were seen to fill the bars without crashing, plus the knocked icon. Nothing else.
    void SquadFillRowMinimal(UObject* W, const SquadHealthItem* Mine) {
        const float H = Mine->Hp / 100.f, Sh = Mine->Sh / 100.f;
        if (const int32 ShowOff = int32(W->GetOffset("ShowingHealth")); ShowOff >= 0) *((uint8_t*)W + ShowOff) = 1;
        for (const char* Name : { "SetHealth", "SetHealthPercent", "SetHealthPct" }) if (auto Fn = W->GetFunction(Name)) if (SquadCallFloat(W, Fn, H)) break;
        for (const char* Name : { "SetShield", "SetShieldPercent", "SetShieldPct" }) if (auto Fn = W->GetFunction(Name)) if (SquadCallFloat(W, Fn, Sh)) break;
        // Knocked circle next to the name: on when knocked (2), off when up (1); only when it changes.
        if (Mine->Alive == 1 || Mine->Alive == 2) if (auto Fn = W->GetFunction("SetDBNO")) {
            static std::unordered_map<UObject*, LONG> Shown; auto& Last = Shown[W];
            if (Last != Mine->Alive) {
                int Params = 0; int32 Off = -1;
                for (auto F = ((const UStruct*)Fn)->GetChildren(); F; F = F->GetNext()) if (GetFromOffset<uint64>(F, Offsets::PropertyFlags) & 0x80) { ++Params; Off = int32(GetFromOffset<uint32>(F, Offsets::Offset_Internal)); }
                if (Params == 1 && Off >= 0 && Off < 56) { alignas(16) uint8_t Buf[64]{}; Buf[Off] = Mine->Alive == 2 ? 1 : 0; W->ProcessEvent(Fn, Buf); }
                Last = Mine->Alive;
            }
        }
    }
    void SquadRowsMinimalTick() {
        static ULONGLONG Next = 0; if (GetTickCount64() < Next) return; Next = GetTickCount64() + 150;
        auto View = SquadView(); if (!View) return;
        SquadHealthItem Items[16]; LONG Count = 0;
        for (int Try = 0; Try < 3; ++Try) {
            const LONG Seq = View->Seq; if (Seq & 1) { Count = 0; continue; }
            Count = View->Count; if (Count < 0 || Count > 16) return;
            memcpy(Items, View->Items, sizeof(SquadHealthItem) * size_t(Count));
            if (View->Seq == Seq) break; Count = 0;
        }
        if (Count <= 0) return;
        memcpy(SquadLastItems, Items, sizeof(SquadHealthItem) * size_t(Count)); SquadLastCount = Count;
        SquadWidgetsTick(Items, Count); // match the rows to the shared bot teammates (throttled inside)
        static int Logged = 0;
        for (auto& R : SquadMatched) {
            if (TUObjectArray::GetObjectByIndex(R.Index) != R.Widget) continue;
            const SquadHealthItem* Mine = nullptr;
            for (LONG k = 0; k < SquadLastCount; ++k) if (R.Name == SquadLastItems[k].Name) { Mine = &SquadLastItems[k]; break; }
            if (!Mine) continue;
            SquadFillRowMinimal(R.Widget, Mine);
            if (Logged < 6) { ++Logged; char Line[200]; snprintf(Line, sizeof(Line), "squad bars (Season 8+): filled %s row health %.0f shield %.0f (SetHealth/SetShield only)", Mine->Name, double(Mine->Hp), double(Mine->Sh)); Log(Line); }
        }
    }
    // Season 4: the squad bars are filled the safe way (SquadPlayerStateTick: the game state's player list, the bots'
    // player state numbers, no actor search) and only once the battle bus has left (every player has joined): the
    // old code crashed 4.5 while squads were still joining. 4.5 could not find GetAllActorsOfClass either.
    bool Season4Squads() { return VersionInfo.FortniteVersion >= 4.f && VersionInfo.FortniteVersion < 5.f; }
    bool Season4SquadsReady() {
        auto World = UWorld::GetWorld(); auto GS = World ? (UObject*)World->GameState : nullptr; if (!GS) return false;
        static int32 PhaseOff = -2; if (PhaseOff == -2) PhaseOff = int32(GS->GetOffset("GamePhase"));
        if (PhaseOff < 0) return false;
        const uint8_t Phase = *(uint8_t*)(__int64(GS) + PhaseOff); // 3 = battle bus, 4 = safe zones
        static bool Told = false; if (Phase >= 3 && !Told) { Told = true; char L[96]; snprintf(L, sizeof(L), "squad bars (Season 4): game phase %d, filling the squad list", int(Phase)); Log(L); }
        return Phase >= 3 && Phase < 6;
    }
    bool SquadSeasonOk() {
        // Season 8 and later: off. Filling the Season 8 squad list rows (calling the row widget's own health
        // functions every frame) is the only Syndicate code that runs only when you have teammates, and Season 8
        // duos/squads crashed in the game's script (endless recursion) as soon as teammates were listed.
        // Season 7 and later: off. Filling the squad list rows crashed Season 7 and Season 8 duos/squads (the game's
        // own script recursed / corrupted during match load). Every variant tried crashed, so those seasons keep the
        // game's own (empty for bots) squad bars rather than risk the match. Season 6 and earlier are confirmed safe.
        // Season 4 (4.5) too: the client crashed ("Unexpected concurency while adding new object") the moment this code
        // first ran, 15 s after the server started sharing a bot teammate, while the other squads were still joining.
        const bool Season4 = VersionInfo.FortniteVersion >= 4.f && VersionInfo.FortniteVersion < 5.f;
        const bool Ok = VersionInfo.FortniteVersion < 7.f && !Season4;
        static bool Told = false; if (!Ok && !Told) { Told = true; Log(Season4 ? "squad bars: off in Season 4 (crashed the client as squads joined)" : "squad bars: off in Season 7 and later (filling the rows crashed duos/squads)"); }
        return Ok;
    }
    // Pre-season (1.7.2): Purple Stuff is shown as "Lean" (its effect is the match server's, SyndicateLeanTick). The item
    // definition's DisplayName is replaced once it is loaded; the new text is kept alive for the whole session.
    int LeanStage = 0; // 0 trying, 1 done, -1 off
    void LeanRename() {
        if (VersionInfo.FortniteVersion != 1.72) { LeanStage = -1; return; }
        static ULONGLONG Next = 0; static int Tries = 0; if (GetTickCount64() < Next) return; Next = GetTickCount64() + 2000;
        if (++Tries > 300) { LeanStage = -1; Log("lean: Purple Stuff never loaded; name unchanged"); return; }
        auto Class = FindClass("FortItemDefinition"); if (!Class) return;
        auto Item = (UObject*)FindObject(L"/Game/Athena/Items/Consumables/PurpleStuff/Athena_PurpleStuff.Athena_PurpleStuff", Class); if (!Item) return;
        auto Off = Item->GetOffset("DisplayName"); if (Off == uint32(-1)) { LeanStage = -1; Log("lean: no DisplayName on the item"); return; }
        alignas(16) static uint8_t Keep[64]{}; static bool Made = false; static_assert(sizeof(FText) <= sizeof(Keep), "FText size");
        if (!Made) { FText Text = UKismetTextLibrary::Conv_StringToText(FString(L"Lean")); memcpy(Keep, &Text, sizeof(FText)); memset((void*)&Text, 0, sizeof(FText)); Made = true; }
        memcpy((uint8_t*)Item + Off, Keep, sizeof(FText)); // the old text is left as it was (never freed twice)
        LeanStage = 1; Log("lean: Purple Stuff is now called Lean");
    }
#include "SyndicateStwLocker.inl"
    // ---- Two players on one PC: split the input between the two Fortnite clients ---------------------------------
    // The launcher's "Launch 2nd player" writes input-role-<pid>.txt for each client: "kbm" or "controller". Without
    // it both clients read every device (raw mouse input and XInput both reach a background window), so:
    //   kbm        - the controller is invisible here: XInput reports no pad, raw HID pad input is dropped
    //   controller - keyboard and mouse messages are dropped here
    // No file (normal play): nothing is filtered and the XInput hooks are never installed.
    volatile LONG InputRole = 0; // 0 all devices, 1 keyboard/mouse only, 2 controller only
    void ReadInputRole() {
        wchar_t Name[64]; swprintf_s(Name, L"input-role-%lu.txt", GetCurrentProcessId());
        std::string Role = ReadSmallFile(Name);
        while (!Role.empty() && (Role.back() == '\r' || Role.back() == '\n' || Role.back() == ' ')) Role.pop_back();
        const LONG Now = Role == "kbm" ? 1 : Role == "controller" ? 2 : 0;
        if (InterlockedExchange(&InputRole, Now) != Now) Log(Now == 1 ? "input split: keyboard/mouse only (controller ignored)" : Now == 2 ? "input split: controller only (keyboard/mouse ignored)" : "input split: off (all devices)");
    }
    typedef DWORD (WINAPI* XInputGetStateFn)(DWORD, void*);
    typedef DWORD (WINAPI* XInputGetCapsFn)(DWORD, DWORD, void*);
    constexpr int XInputSlots = 5;
    XInputGetStateFn XGetStateOrig[XInputSlots]{}, XGetStateExOrig[XInputSlots]{};
    XInputGetCapsFn XGetCapsOrig[XInputSlots]{};
    bool XInputHooked[XInputSlots]{};
    template <int N> DWORD WINAPI XGetStateDetour(DWORD User, void* State) { return InputRole == 1 ? ERROR_DEVICE_NOT_CONNECTED : XGetStateOrig[N](User, State); }
    template <int N> DWORD WINAPI XGetStateExDetour(DWORD User, void* State) { return InputRole == 1 ? ERROR_DEVICE_NOT_CONNECTED : XGetStateExOrig[N](User, State); }
    template <int N> DWORD WINAPI XGetCapsDetour(DWORD User, DWORD Flags, void* Caps) { return InputRole == 1 ? ERROR_DEVICE_NOT_CONNECTED : XGetCapsOrig[N](User, Flags, Caps); }
    void XInputHook(void* Target, void* Detour, void** Original) {
        if (Target && MH_CreateHook(Target, Detour, Original) == MH_OK) MH_EnableHook(Target);
    }
    template <int N> void HookXInputModule(const wchar_t* Dll, bool HasGetStateEx) {
        if (XInputHooked[N]) return;
        const HMODULE M = GetModuleHandleW(Dll); if (!M) return; // not loaded (yet)
        XInputHooked[N] = true;
        MH_Initialize(); // already initialized by the redirect guard is fine
        XInputHook((void*)GetProcAddress(M, "XInputGetState"), (void*)&XGetStateDetour<N>, (void**)&XGetStateOrig[N]);
        if (HasGetStateEx) XInputHook((void*)GetProcAddress(M, MAKEINTRESOURCEA(100)), (void*)&XGetStateExDetour<N>, (void**)&XGetStateExOrig[N]); // XInputGetStateEx (Guide button)
        XInputHook((void*)GetProcAddress(M, "XInputGetCapabilities"), (void*)&XGetCapsDetour<N>, (void**)&XGetCapsOrig[N]);
        char Line[96]; snprintf(Line, sizeof(Line), "input split: controller hooks on %ls", Dll); Log(Line);
    }
    void HookXInput() {
        HookXInputModule<0>(L"xinput1_4.dll", true);
        HookXInputModule<1>(L"xinput1_3.dll", true);
        HookXInputModule<2>(L"xinput9_1_0.dll", false);
        HookXInputModule<3>(L"xinput1_2.dll", false);
        HookXInputModule<4>(L"xinput1_1.dll", false);
    }
    // Runs on every message the game takes from its queue: a dropped message becomes WM_NULL.
    void FilterInput(MSG* M) {
        const LONG Role = InputRole; if (!Role || !M) return;
        const UINT Msg = M->message;
        if (Msg == WM_INPUT) {
            RAWINPUTHEADER Header{}; UINT Size = sizeof(Header);
            if (GetRawInputData((HRAWINPUT)M->lParam, RID_HEADER, &Header, &Size, sizeof(RAWINPUTHEADER)) == (UINT)-1) return;
            const bool Pad = Header.dwType == RIM_TYPEHID;
            if ((Role == 1 && Pad) || (Role == 2 && !Pad)) M->message = WM_NULL;
            return;
        }
        if (Role == 2 && ((Msg >= WM_KEYFIRST && Msg <= WM_KEYLAST) || (Msg >= WM_MOUSEFIRST && Msg <= WM_MOUSELAST))) M->message = WM_NULL;
    }

    // ---- Sound stays on with "Visualize Sound Effects" (every season) ------------------------------------
    // The game's hearing-accessibility code mutes all game audio while Visualize Sound Effects is on, unless
    // FortRuntimeOptions.bDisableSpatializationInsteadOfMutingWhenHearingAccessibilityEnabled is set (Epic turned
    // that on later by hotfix; then the option only drops 3D positioning and sound keeps playing). The flag is a
    // Config property on the runtime options default object, which the game reads each time it applies the
    // setting, so it is set here and set again every few seconds (a hotfix reload could clear it). The backend's
    // DefaultRuntimeOptions hotfix sets it too. Seasons without the property are logged with the related
    // property names found on FortRuntimeOptions, so the next step for them can be chosen from the log.
    volatile LONG AudioKeepDue = 1;
    void AudioKeepTick() {
        static int Stage = 0; // 0 = not looked yet, 1 = flag found, -1 = this season has no such flag
        static UObject* Options = nullptr; static int32 Off = -1; static uint8 Mask = 0;
        if (Stage < 0) return;
        if (Stage == 0) {
            auto Class = FindClass("FortRuntimeOptions");
            Options = Class ? Class->GetDefaultObj() : nullptr;
            if (!Options) return; // not loaded yet: try again on the next round
            auto Prop = Options->GetProperty("bDisableSpatializationInsteadOfMutingWhenHearingAccessibilityEnabled", 0x20000);
            if (!Prop) {
                Stage = -1;
                std::string Line = "sound: FortRuntimeOptions has no mute-instead flag in this season; related options:";
                if (VersionInfo.FortniteVersion < 12.10f)
                    for (const UStruct* S = (const UStruct*)Class; S; S = S->GetSuper()) {
                        int Count = 0;
                        for (const UField* F = S->GetChildren(); F && Count < 2000; F = F->GetNext(), ++Count) {
                            auto N = F->GetName().ToString(); std::string Low(N.c_str());
                            for (auto& C : Low) C = char(tolower((unsigned char)C));
                            if (Low.find("hearing") != std::string::npos || Low.find("sound") != std::string::npos || Low.find("audio") != std::string::npos
                                || Low.find("mute") != std::string::npos || Low.find("spatial") != std::string::npos) { Line += " "; Line += N.c_str(); }
                        }
                    }
                Log(Line.c_str());
                return;
            }
            Off = int32(*(const uint32*)((const uint8*)Prop + Offsets::Offset_Internal)); Mask = Prop->GetFieldMask();
            if (Off <= 0 || !Mask) { Stage = -1; Log("sound: mute-instead flag has no usable offset; left alone"); return; }
            Stage = 1;
        }
        auto& Byte = *((uint8*)Options + Off);
        if ((Byte & Mask) != Mask) {
            static int Told = 0;
            Byte |= Mask;
            if (Told++ < 3) { char Line[160]; snprintf(Line, sizeof(Line), "sound: Visualize Sound Effects keeps game audio on (runtime option set, Fortnite %.2f)", double(VersionInfo.FortniteVersion)); Log(Line); }
        }
    }

    // ---- In-game console, every season ------------------------------------------------------------------------
    // Season X gets its console from console.dll + SyndicateConsoleRecovery.dll (10.40 only). Every other season gets
    // the same thing here: the engine's own UConsole is created on the game viewport (shipping Fortnite never makes
    // one) and pointed at the local player, and Tilde is added to the console keys if the game's input settings
    // lack it. Press ~ (the key left of 1) once for the command line, twice for the full console. Nothing here hooks
    // the game; it only sets objects the engine already uses. console-off.txt in the ProjectTilted folder turns it off.
    volatile LONG ConsoleDue = 0; // 1 = run on the next ConsoleMessage (posted once a second), 0 = done, -1 = stopped
    bool SeasonXConsoleLoaded() {
        return std::fabs(VersionInfo.FortniteVersion - 10.40) < 0.001 // the launcher loads the 10.40 modules ~30 s after this one
            || GetModuleHandleW(L"SyndicateConsoleRecovery.dll") || GetModuleHandleW(L"console.dll");
    }
    struct ConsoleKey { FName KeyName; void* Details[2]; }; // FKey: FName + TSharedPtr<FKeyDetails>, 0x18 bytes on every UE4 build
    void ConsoleTick() {
        static int State = 0; // 0 = working, -1 = stopped (logged why)
        if (State < 0) return;
        if (SeasonXConsoleLoaded()) { State = -1; Log("console: Season X has its own console (console.dll + SyndicateConsoleRecovery.dll); not touching it"); return; }
        auto Engine = UEngine::GetEngine();
        auto View = Engine ? Engine->GameViewport : nullptr;
        if (!View) return; // engine still starting
        auto World = UWorld::GetWorld();
        auto Instance = World ? World->OwningGameInstance : nullptr;
        ULocalPlayer* Player = Instance && Instance->LocalPlayers.Num() > 0 ? Instance->LocalPlayers[0] : nullptr;
        if (!Player) return;
        static bool Told = false;
        auto Console = View->ViewportConsole;
        if (!Console) {
            const UClass* Class = Engine->ConsoleClass;
            if (!Class) Class = FindClass("Console");
            if (!Class || !UGameplayStatics::GetDefaultObj()->GetFunction("SpawnObject")) { State = -1; Log("console: this build has no console class or SpawnObject; console off"); return; }
            Console = UGameplayStatics::SpawnObject((UClass*)Class, View);
            if (!Console) { State = -1; Log("console: could not create the console; console off"); return; }
            View->ViewportConsole = Console;
        }
        static int32 TargetOff = -2;
        if (TargetOff == -2) TargetOff = int32(Console->GetOffset("ConsoleTargetPlayer"));
        if (TargetOff >= 0 && *(ULocalPlayer**)((uint8*)Console + TargetOff) != Player) *(ULocalPlayer**)((uint8*)Console + TargetOff) = Player;
        static bool KeysDone = false;
        if (!KeysDone) {
            KeysDone = true;
            const UClass* InputClass = FindClass("InputSettings");
            UObject* Input = InputClass ? InputClass->GetDefaultObj() : nullptr;
            const int32 KeysOff = Input ? int32(Input->GetOffset("ConsoleKeys")) : -1;
            if (KeysOff < 0) Log("console: no ConsoleKeys in this build's input settings; using the game's own keys");
            else {
                auto& Keys = *(TArray<ConsoleKey>*)((uint8*)Input + KeysOff);
                bool HasTilde = false;
                for (int32 i = 0; i < Keys.Num(); ++i) if (Keys[i].KeyName.ToString() == "Tilde") { HasTilde = true; break; }
                if (HasTilde) Log("console: Tilde is already a console key");
                else if (!Offsets::FNameConstructor || !Offsets::Realloc) Log("console: cannot add Tilde on this build (name or memory function not found)");
                else { ConsoleKey Key{}; Key.KeyName = FName(L"Tilde"); Keys.Add(Key); Log("console: added Tilde to the console keys"); }
            }
        }
        if (!Told) { Told = true; char Line[128]; snprintf(Line, sizeof(Line), "console: in-game console ready (Fortnite %.2f); press ~ to open it", double(VersionInfo.FortniteVersion)); Log(Line); }
    }

    // Separate from OnMessage: that function uses __try, which cannot share a function with a std::wstring.
    void CleanExit() {
        if (GetFileAttributesW(DataFile(L"clean-exit-off.txt").c_str()) != INVALID_FILE_ATTRIBUTES) return;
        Log("exit: game is closing; ending the process before the engine shutdown (avoids the exit crash)");
        TerminateProcess(GetCurrentProcess(), 0);
    }
    LRESULT CALLBACK OnMessage(int Code, WPARAM W, LPARAM L) {
        // Clean exit. Closing the game (Alt+F4, the window's X or the in-game Quit button) makes the engine post
        // WM_QUIT and then run its full shutdown, which crashed some old seasons (3.5: a call through a null
        // function pointer during teardown) and left the "Application Crash Detected" window behind. Settings
        // are saved when they are applied, not at exit, so the process ends here before that shutdown runs.
        // clean-exit-off.txt keeps the game's own shutdown.
        if (Code >= 0 && W == PM_REMOVE && reinterpret_cast<MSG*>(L)->message == WM_QUIT) CleanExit();
        if (Code >= 0) FilterInput(reinterpret_cast<MSG*>(L));
        if (Code >= 0 && W == PM_REMOVE) MouseCoalesce::Filter(reinterpret_cast<MSG*>(L));
        // The console has its own message so the other mods' ticks keep running only while they are switched on.
        if (Code >= 0 && W == PM_REMOVE && reinterpret_cast<MSG*>(L)->message == ConsoleMessage && InterlockedCompareExchange(&ConsoleDue, 0, 1) == 1)
            { __try { ConsoleTick(); } __except (EXCEPTION_EXECUTE_HANDLER) { InterlockedExchange(&ConsoleDue, -1); Log("console: stopped after an error"); } }
        if (Code >= 0 && W == PM_REMOVE && reinterpret_cast<MSG*>(L)->message == ApplyMessage) {
            if (StwWanted()) { __try { StwLockerTick(); } __except (EXCEPTION_EXECUTE_HANDLER) { StwLockerFailed(); } }
            if (LeanStage == 0) { __try { LeanRename(); } __except (EXCEPTION_EXECUTE_HANDLER) { LeanStage = -1; Log("lean: stopped after an error"); } }
            __try { Apply(); } __except (EXCEPTION_EXECUTE_HANDLER) {}
            if (InterlockedExchange(&AudioKeepDue, 0)) { __try { AudioKeepTick(); } __except (EXCEPTION_EXECUTE_HANDLER) {} }
            __try { TurboScan(); } __except (EXCEPTION_EXECUTE_HANDLER) { InBuildMode = 0; InEditTool = 0; }
            bool DevOk = false;
            __try { DevOk = DevWorldSettled(); } __except (EXCEPTION_EXECUTE_HANDLER) { DevOk = false; }
            if (SquadShared || DevOk) {
                // The squad list shows from the spawn island on (also in the bus, with no body): it only needs the
                // match world to have finished loading, not your character.
                bool Settled = DevOk; if (!Settled) { __try { Settled = SquadWorldReady(); } __except (EXCEPTION_EXECUTE_HANDLER) {} }
                if (Settled) {
                    // Season 8 and later: off. Season 8 crashed right after the squad bar code started touching the
                    // squad list and the bots' health (twice, as all squads were joining the match). Until it is shown
                    // to be safe there, those seasons keep the game's own squad bars.
                    if (SquadShared && SquadSeasonOk()) { __try { SquadHealthTick(); } __except (EXCEPTION_EXECUTE_HANDLER) {} }
                    if (SquadShared && SquadSeasonOk()) { __try { SquadRowsFastTick(); } __except (EXCEPTION_EXECUTE_HANDLER) {} }
                    if (SquadShared && Season4Squads()) { bool Ready = false; __try { Ready = Season4SquadsReady(); } __except (EXCEPTION_EXECUTE_HANDLER) {}
                        if (Ready) { __try { SquadPlayerStateTick(true); } __except (EXCEPTION_EXECUTE_HANDLER) {} } }
                    // Season 7 and later: no Syndicate squad-bar code touches the rows or player states (it crashed duos).
                }
            }
            __try { StretchTick(); } __except (EXCEPTION_EXECUTE_HANDLER) {}
            if (DevOk) {
                __try { DevCameraExport(); } __except (EXCEPTION_EXECUTE_HANDLER) {}
                __try { DevAimTick(); } __except (EXCEPTION_EXECUTE_HANDLER) {}
                __try { DevFlyTick(); } __except (EXCEPTION_EXECUTE_HANDLER) {}
                __try { DevFreeCamTick(); } __except (EXCEPTION_EXECUTE_HANDLER) {}
                __try { DevRecoilTick(); } __except (EXCEPTION_EXECUTE_HANDLER) {}
                __try { DevNoDropTick(UWorld::GetWorld()); } __except (EXCEPTION_EXECUTE_HANDLER) {}
            }
            if (StoreInjectActive) {
                __try { StoreInjectMaintain(); } __except (EXCEPTION_EXECUTE_HANDLER) { Log("store inject: crashed (caught)"); }
            }
        }
        return CallNextHookEx(MessageHook, Code, W, L);
    }
    BOOL CALLBACK FindGameWindow(HWND Window, LPARAM) {
        DWORD Pid = 0; GetWindowThreadProcessId(Window, &Pid);
        wchar_t Class[128]{}; GetClassNameW(Window, Class, 128);
        if (Pid == GetCurrentProcessId() && IsWindowVisible(Window) && wcsstr(Class, L"UnrealWindow")) { GameWindow = Window; return FALSE; }
        return TRUE;
    }
    DWORD WINAPI Start(void*) {
        Log("attached, waiting for the game window");
        for (int Attempt = 0; Attempt < 240 && !GameWindow; ++Attempt) { EnumWindows(FindGameWindow, 0); if (!GameWindow) Sleep(500); }
        if (!GameWindow) { Log("game window not found"); return 0; }
        SDK::Init();
        Log("loaded (built " __DATE__ " " __TIME__ ")");
        if (auto Thread = CreateThread(nullptr, 0, EditLoop, nullptr, 0, nullptr)) CloseHandle(Thread);
        if (auto Thread = CreateThread(nullptr, 0, MouseHookLoop, nullptr, 0, nullptr)) CloseHandle(Thread); // physical left-button tracking for turbo build
        ULONGLONG NextRead = 0;
        for (;;) {
            if (!IsWindow(GameWindow)) {
                StwReleaseForExitSafe();
                if (MessageHook) { UnhookWindowsHookEx(MessageHook); MessageHook = nullptr; }
                GameWindow = nullptr; EnumWindows(FindGameWindow, 0);
                // No new game window: the game is closing. End it now, before the engine's shutdown (crashed 3.5 on
                // Alt+F4 with "Application Crash Detected"); 3.5 does not post WM_QUIT through the hook first. A game
                // that only recreated its window (display change) has a new one by the second look.
                if (!GameWindow) { Sleep(400); EnumWindows(FindGameWindow, 0); if (!GameWindow) CleanExit(); }
                if (!GameWindow) { Sleep(1000); continue; }
            }
            if (GetTickCount64() >= NextRead) { NextRead = GetTickCount64() + 1000; ReadSetting(); ReadStretch(); ReadTurbo(); ReadEdit(); ReadDevCamera(); ReadInputRole(); if (InputRole) HookXInput();
                static int AudioRound = 0; if (++AudioRound % 3 == 1) InterlockedExchange(&AudioKeepDue, 1);
                if (MessageHook && ConsoleDue >= 0 && !FlagFile(L"console-off.txt")) { InterlockedExchange(&ConsoleDue, 1); PostMessageW(GameWindow, ConsoleMessage, 0, 0); } }
            if (!MessageHook) MessageHook = SetWindowsHookExW(WH_GETMESSAGE, OnMessage, nullptr, GetWindowThreadProcessId(GameWindow, nullptr));
            if (MessageHook && !MouseCoalesce::Active) { static bool Tried = false; if (!Tried) { Tried = true; Log(MouseCoalesce::Install(GetWindowThreadProcessId(GameWindow, nullptr)) ? "mouse: polling-rate merge ready (only 2000-8000 Hz mice are merged)" : "mouse: polling-rate merge off"); } }
            { static ULONGLONG NextSquad = 0, NextReport = 0; static int Reports = 0;
              if (GetTickCount64() >= NextSquad) {
                  NextSquad = GetTickCount64() + 1000; auto V = SquadView();
                  const LONG Now = V && V->Count > 0 ? 1 : 0;
                  if (Now != SquadShared) { Reports = 0; NextReport = 0; } // report again when sharing starts or stops
                  InterlockedExchange(&SquadShared, Now);
                  if (Reports < 6 && GetTickCount64() >= NextReport) {
                      NextReport = GetTickCount64() + 20000; ++Reports;
                      std::string L = V ? "squad bars: server shares " + std::to_string(V->Count) + " bot teammates:" : std::string("squad bars: no shared squad health from a server on this PC");
                      if (V) for (LONG k = 0; k < V->Count && k < 16; ++k) { L += " "; L += std::string(V->Items[k].Name, strnlen(V->Items[k].Name, 63)); }
                      Log(L.c_str());
                  }
              } }
            if (MessageHook && (AudioKeepDue || StwWanted() || SquadShared || WantedTimes100 > 0 || LastSet > 0.f || StretchOn || StretchSaved >= 0 || TurboOn || EditOn || ResetOn || DevCameraOn || DevAimLevel || DevFlyOn || DevFreeCamOn || DevNoRecoil || DevNoDrop || StoreInjectActive || !DevRecoilSaved.empty())) PostMessageW(GameWindow, ApplyMessage, 0, 0);
            TurboTick();
            DevFlyKeyTick();
            Sleep(DevCameraOn || DevAimLevel || DevFlyOn ? 6 : 16); // Dev Console markers: sample the game more often so they keep up
        }
    }
}

// Backend redirect guard, Chapter 2 (11.00 and later) only.
// cobalt.dll sends the game's web requests to Syndicate's backend (127.0.0.1:3551) by hooking curl_easy_setopt
// with a page-guard (VEH) hook. That kind of hook is one-shot per page: while one thread trips it, another
// thread calling curl_easy_setopt runs unhooked. 11.00 sends several requests at once during login, so one
// (tryPlayOnPlatform) went to Epic's real server, failed its certificate check, and the game logged out to
// the email/password screen. For Chapter 2 the launcher therefore loads this module instead of cobalt.dll, right
// after starting the game, and this ordinary inline hook (MinHook) does the redirect: every call goes through it.
namespace {
    using CurlEasySetOptFn = int (*)(void*, int, ...);
    CurlEasySetOptFn CurlEasySetOptOriginal = nullptr;
    constexpr int CurlOptSslVerifyPeer = 64, CurlOptSslVerifyHost = 81, CurlOptUrl = 10002;

    bool RedirectHost(const std::string& Host) {
        auto Ends = [&](const char* Tail) { size_t N = strlen(Tail); return Host.size() >= N && Host.compare(Host.size() - N, N, Tail) == 0; };
        return Ends("ol.epicgames.com") || Ends("epicgames.dev") || Ends("ol.epicgames.net") || Ends(".akamaized.net")
            || Ends("on.epicgames.com") || Ends("game-social.epicgames.com") || Host.find("superawesome.com") != std::string::npos
            || Host.find("ak.epicgames.com") != std::string::npos;
    }

    // https://host[:port]/path?query -> http://127.0.0.1:3551/path?query, for the hosts cobalt redirects.
    bool RedirectUrl(const char* Url, std::string& Out) {
        if (!Url) return false;
        std::string Text = Url;
        size_t Scheme = Text.find("://");
        if (Scheme == std::string::npos) return false;
        size_t HostStart = Scheme + 3, PathStart = Text.find_first_of("/?", HostStart);
        std::string Host = Text.substr(HostStart, PathStart == std::string::npos ? std::string::npos : PathStart - HostStart);
        if (size_t Port = Host.find(':'); Port != std::string::npos) Host.resize(Port);
        for (auto& C : Host) C = (char)tolower((unsigned char)C);
        if (Host == "127.0.0.1" || !RedirectHost(Host)) return false;
        Out = "http://127.0.0.1:3551" + (PathStart == std::string::npos ? std::string("/") : (Text[PathStart] == '?' ? "/" + Text.substr(PathStart) : Text.substr(PathStart)));
        return true;
    }

    // Every curl option takes exactly one argument, passed in one 8-byte slot on x64.
    int CurlEasySetOptDetour(void* Handle, int Option, ...) {
        va_list Args; va_start(Args, Option); void* Value = va_arg(Args, void*); va_end(Args);
        if (Handle && (Option == CurlOptSslVerifyPeer || Option == CurlOptSslVerifyHost)) return CurlEasySetOptOriginal(Handle, Option, (long)0);
        if (Handle && Option == CurlOptUrl) {
            std::string Redirected;
            if (RedirectUrl((const char*)Value, Redirected)) return CurlEasySetOptOriginal(Handle, Option, Redirected.c_str()); // curl copies the string
        }
        return CurlEasySetOptOriginal(Handle, Option, Value);
    }

    // Fortnite's major version from the build string in the game exe (L"++Fortnite+Release-11.00"), or -1.
    int GameMajorVersion() {
        auto Base = (uint8_t*)GetModuleHandleW(nullptr);
        if (!Base) return -1;
        auto Dos = (IMAGE_DOS_HEADER*)Base; auto Nt = (IMAGE_NT_HEADERS*)(Base + Dos->e_lfanew);
        static const wchar_t Marker[] = L"++Fortnite+Release-";
        const auto MarkerBytes = (const uint8_t*)Marker; const size_t MarkerSize = (sizeof(Marker) - sizeof(wchar_t));
        auto Section = IMAGE_FIRST_SECTION(Nt);
        for (WORD I = 0; I < Nt->FileHeader.NumberOfSections; ++I, ++Section) {
            if ((Section->Characteristics & IMAGE_SCN_MEM_EXECUTE) || !(Section->Characteristics & IMAGE_SCN_MEM_READ)) continue;
            uint8_t* Start = Base + Section->VirtualAddress; uint8_t* End = Start + Section->Misc.VirtualSize;
            for (uint8_t* At = std::search(Start, End, MarkerBytes, MarkerBytes + MarkerSize); At != End; At = std::search(At + 2, End, MarkerBytes, MarkerBytes + MarkerSize)) {
                auto Digits = (const wchar_t*)(At + MarkerSize);
                if ((uint8_t*)(Digits + 3) > End || !iswdigit(Digits[0])) continue;
                int Major = 0;
                for (int D = 0; D < 3 && iswdigit(Digits[D]); ++D) Major = Major * 10 + (Digits[D] - L'0');
                return Major;
            }
        }
        return -1;
    }

    DWORD WINAPI RedirectGuard(void*) {
        const int Major = GameMajorVersion();
        if (Major < 11) return 0; // Chapter 1 seasons: cobalt alone has been reliable, leave them as they are
        // The launcher no longer loads cobalt.dll for Chapter 2: this hook is the only redirect there. With cobalt.dll
        // in the process as well (its page-guard hook on the same function) the game closed shortly after launch, so
        // then it stays off unless redirect-guard-on.txt asks for it.
        if (GetModuleHandleW(L"cobalt.dll") && GetFileAttributesW(DataFile(L"redirect-guard-on.txt").c_str()) == INVALID_FILE_ATTRIBUTES) { Log("redirect guard: off (cobalt.dll is loaded)"); return 0; }
        uint8_t* Target = nullptr;
        for (int Attempt = 0; Attempt < 100 && !Target; ++Attempt) { // same signature cobalt uses for curl_easy_setopt
            Target = (uint8_t*)Memcury::Scanner::FindPattern("89 54 24 10 4C 89 44 24 18 4C 89 4C 24 20 48 83 EC 28 48 85 C9 75 08 8D 41 2B 48 83 C4 28 C3 4C", false).Get();
            if (!Target) Sleep(200);
        }
        if (!Target) { Log("redirect guard: curl_easy_setopt not found, cobalt only"); return 0; }
        const MH_STATUS Init = MH_Initialize();
        const MH_STATUS Made = MH_CreateHook(Target, (void*)CurlEasySetOptDetour, (void**)&CurlEasySetOptOriginal);
        const MH_STATUS On = Made == MH_OK ? MH_EnableHook(Target) : Made;
        char Line[160]; snprintf(Line, sizeof(Line), "redirect guard (Fortnite %d): %s (init %d, create %d, enable %d)", Major, On == MH_OK ? "on" : "FAILED", int(Init), int(Made), int(On)); Log(Line);
        return 0;
    }
}

// Crash breadcrumbs. Some client crashes (Pre-Season, leaving the bus) end the process without the engine's crash
// reporter running, so nothing says where it died. The first 20 access violations of a session are written to
// syndicate-client.log with the module and offset they happened in. Most are harmless (guarded memory reads in
// the menu); the last lines before a silent crash show where it was.
namespace {
    volatile LONG FaultLines = 0;
    LONG CALLBACK FaultBreadcrumb(EXCEPTION_POINTERS* Info) {
        if (!Info || !Info->ExceptionRecord) return EXCEPTION_CONTINUE_SEARCH;
        const DWORD Code = Info->ExceptionRecord->ExceptionCode;
        if (Code != EXCEPTION_ACCESS_VIOLATION && Code != EXCEPTION_STACK_OVERFLOW && Code != EXCEPTION_ILLEGAL_INSTRUCTION) return EXCEPTION_CONTINUE_SEARCH;
        if (InterlockedIncrement(&FaultLines) > 20) return EXCEPTION_CONTINUE_SEARCH;
        void* At = Info->ExceptionRecord->ExceptionAddress; HMODULE Mod = nullptr; wchar_t Path[MAX_PATH] = L"?";
        if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCWSTR)At, &Mod) && Mod) GetModuleFileNameW(Mod, Path, MAX_PATH);
        const wchar_t* Name = wcsrchr(Path, L'\\'); Name = Name ? Name + 1 : Path;
        char Line[256]; snprintf(Line, sizeof(Line), "fault: code %08lX in %ls+0x%llX (reading %p, thread %lu)", Code, Name,
            (unsigned long long)((uintptr_t)At - (uintptr_t)Mod), Info->ExceptionRecord->NumberParameters > 1 ? (void*)Info->ExceptionRecord->ExceptionInformation[1] : nullptr, GetCurrentThreadId());
        Log(Line);
        return EXCEPTION_CONTINUE_SEARCH;
    }
}

BOOL APIENTRY DllMain(HMODULE Module, DWORD Reason, LPVOID) {
    if (Reason == DLL_PROCESS_ATTACH) {
        SelfModule = Module;
        DisableThreadLibraryCalls(Module);
        AddVectoredExceptionHandler(1, FaultBreadcrumb);
        if (auto Thread = CreateThread(nullptr, 0, RedirectGuard, nullptr, 0, nullptr)) CloseHandle(Thread); // first: login starts soon after launch
        if (auto Thread = CreateThread(nullptr, 0, Start, nullptr, 0, nullptr)) CloseHandle(Thread);
    }
    return TRUE;
}
