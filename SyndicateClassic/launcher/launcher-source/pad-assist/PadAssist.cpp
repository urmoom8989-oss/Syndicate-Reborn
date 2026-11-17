// TiltedPadAssist.dll: sprint by default and turbo build for Xbox controllers, every season.
//
// Pre-season to Season 4 have no "sprint by default" setting; on a controller you click L3 to sprint.
// The launcher loads this DLL into Fortnite for every season. It wraps the game's XInputGetState:
// while the setting is on (%LOCALAPPDATA%\ProjectTilted\sprint-default-on.txt exists) and the left
// stick is pushed forward (straight or diagonal), L3 reads as held, so the game starts sprinting.
// Turbo build on a controller (every season): while TiltedClient.dll reports a building piece in
// hand (turbo build on, see SyndicateClient.cpp) and the right trigger is held, the trigger reads as released
// for a moment every TurboCycleMs after the first TurboFirstMs, so the game places again, like holding the
// left mouse button with turbo build on keyboard and mouse.
// Edit on release on a controller (edit-release-on.txt, same switch as keyboard): hold your edit button
// (pad-edit-button.txt, default B), pick tiles with the right trigger, let go of the edit button and the edit is
// confirmed (the edit button reads as pressed once more for you). Letting go of the right trigger in edit mode
// confirms too, unless edit-release-select-off.txt exists. TiltedClient.dll reports when the edit tool is in hand.
// Custom controller binds (controller-binds.txt, Settings > Modded > Controller button layout), every season:
// Fortnite only got its own controller binding screen in Season 6, so for older seasons each button (and trigger)
// can be moved to another one here, before anything else reads the pad. Lines are "<your button>=<game button>"
// (A, B, X, Y, LB, RB, LT, RT, LS, RS, UP, DOWN, LEFT, RIGHT, BACK, START, or NONE to turn a button off), plus
// "SWAPSTICKS=1" for a left-handed layout. Buttons not listed keep their place.
// Nothing else about the controller changes. Diagnostics: pad-assist.log next to this DLL.
#include <windows.h>
#include <psapi.h>
#include <delayimp.h>
#include <cstring>
#include <cwchar>
#include <cstdio>

namespace {
struct Gamepad { WORD Buttons; BYTE LeftTrigger, RightTrigger; SHORT LX, LY, RX, RY; };
struct PadState { DWORD PacketNumber; Gamepad Pad; };
using GetStateFn = DWORD(WINAPI*)(DWORD, PadState*);

constexpr WORD LeftThumb = 0x0040;
constexpr SHORT Forward = 13000;           // about 40% of the stick's travel
volatile LONG Enabled = 0;
GetStateFn Original = nullptr;

// Build mode published by TiltedClient.dll (Local\TiltedClientState_<pid>, one LONG).
volatile LONG* BuildMode = nullptr;
constexpr ULONGLONG TurboFirstMs = 150, TurboCycleMs = 90;
constexpr BYTE TriggerHeld = 60;
bool InBuildMode()
{
    if (!BuildMode)
    {
        static ULONGLONG nextTry = 0;
        if (GetTickCount64() < nextTry) return false;
        nextTry = GetTickCount64() + 1000;
        wchar_t name[64]; swprintf_s(name, L"Local\\TiltedClientState_%lu", GetCurrentProcessId());
        if (HANDLE map = OpenFileMappingW(FILE_MAP_READ, FALSE, name))
            BuildMode = (volatile LONG*)MapViewOfFile(map, FILE_MAP_READ, 0, 0, 64);
        if (!BuildMode) return false;
    }
    return BuildMode[0] != 0;
}
bool InEditTool() { InBuildMode(); return BuildMode && BuildMode[1] != 0; } // second LONG: the edit tool is in hand
ULONGLONG TriggerSince[4]{};
HMODULE Self = nullptr;
wchar_t Flag[MAX_PATH]{};
wchar_t NativeSeason[MAX_PATH]{}; // written by the launcher while Season 5+ runs: Fortnite has its own sprint by default there

void Log(const char* text)
{
    wchar_t path[MAX_PATH]{};
    if (!GetModuleFileNameW(Self, path, MAX_PATH)) return;
    if (auto slash = wcsrchr(path, L'\\')) wcscpy_s(slash + 1, MAX_PATH - (slash + 1 - path), L"pad-assist.log"); else return;
    FILE* out = nullptr;
    if (_wfopen_s(&out, path, L"a") == 0 && out) { fprintf(out, "pid=%lu %s\n", GetCurrentProcessId(), text); fclose(out); }
}

void LogOnce(int& counter, int max, const char* text) { if (counter < max) { ++counter; Log(text); } }

// Edit on release (controller).
// Reset on release (reset-release-on.txt, same switch as keyboard): let go of the edit button without picking a tile
// and the piece is reset (pad-reset-button.txt, default RS: Fortnite's "Reset Building Edit" on a controller), then
// confirmed, so it closes as a full piece.
volatile LONG EditOn = 0, ResetOn = 0, EditSelectRelease = 1, EditAnyTile = 0, EditButton = 0x2000, ResetButton = 0x0080; // B, RS
struct EditPad { bool Held, OpenedByPress, PickedTile, SelectHeld, SelectInEdit, Started; WORD PressButton; ULONGLONG PressFrom, PressUntil, ConfirmFrom; };
EditPad Edits[4]{};
// Holds a button down from PressFrom to PressUntil (after a short gap, so the game first sees your release).
void QueueEditPress(EditPad& e, ULONGLONG now) { e.PressButton = WORD(EditButton); e.Started = false; e.ConfirmFrom = 0; e.PressFrom = now + 35; e.PressUntil = now + 35 + 60; }
// Reset first, then the confirm a moment later (queued when the reset press ends).
void QueueResetPress(EditPad& e, ULONGLONG now) { e.PressButton = WORD(ResetButton); e.Started = false; e.PressFrom = now + 35; e.PressUntil = now + 35 + 60; e.ConfirmFrom = now + 35 + 60 + 40; }
void EditOnRelease(DWORD user, PadState* state)
{
    EditPad& e = Edits[user];
    const ULONGLONG now = GetTickCount64();
    const WORD button = WORD(EditButton);
    const bool down = (state->Pad.Buttons & button) != 0;           // your hand, before anything is added
    const bool select = state->Pad.RightTrigger >= TriggerHeld;
    if (!EditOn && !ResetOn) { e = {}; return; }
    const bool inEdit = InEditTool();
    // Never press into a closed edit: when the edit tool is gone by the time the press would start (Fortnite's own
    // "confirm edit on release" already closed it, or the piece broke), the press is dropped instead of opening edit
    // mode again, which is what left people stuck in edit.
    if (e.PressUntil && !e.Started && now >= e.PressFrom && !inEdit) { e.PressUntil = 0; e.ConfirmFrom = 0; }
    const bool pressing = e.PressUntil && now >= e.PressFrom && now < e.PressUntil;
    if (pressing) e.Started = true;
    if (e.PressUntil && now >= e.PressUntil)
    {
        e.PressUntil = 0;
        // After the reset press: the confirm.
        if (e.ConfirmFrom) { const ULONGLONG at = e.ConfirmFrom; e.ConfirmFrom = 0; e.PressButton = WORD(EditButton); e.Started = false; e.PressFrom = at; e.PressUntil = at + 60; }
    }
    static bool wasEdit = false; static int editLogs = 0;
    if (inEdit != wasEdit) { wasEdit = inEdit; LogOnce(editLogs, 8, inEdit ? "edit on release: edit tool in hand" : "edit on release: edit tool put away"); }
    static int logs = 0;
    // The edit button.
    if (down && !e.Held) { e.Held = true; e.OpenedByPress = !inEdit; e.PickedTile = select; LogOnce(logs, 6, e.OpenedByPress ? "edit on release: edit button down (opening edit mode)" : "edit on release: edit button down while editing (manual confirm)"); }
    else if (down) { if (select) e.PickedTile = true; }
    else if (e.Held)
    {
        e.Held = false;
        if (e.OpenedByPress && inEdit && !e.PickedTile && ResetOn && !e.PressUntil) { QueueResetPress(e, now); LogOnce(logs, 6, "reset on release: piece reset, then confirmed (edit button let go without a tile)"); }
        else if (EditOn && e.OpenedByPress && inEdit && (e.PickedTile || EditAnyTile) && !e.PressUntil) { QueueEditPress(e, now); LogOnce(logs, 6, "edit on release: confirmed (edit button let go)"); }
        else if (e.OpenedByPress && !inEdit) LogOnce(logs, 6, "edit on release: edit button let go but the edit tool was not in hand (not looking at a build, or a different edit button in Fortnite)");
    }
    // Select (right trigger) let go in edit mode.
    if (EditOn && EditSelectRelease)
    {
        if (select && !e.SelectHeld) { e.SelectHeld = true; e.SelectInEdit = inEdit; }
        else if (select) { if (inEdit) e.SelectInEdit = true; }
        else if (e.SelectHeld)
        {
            e.SelectHeld = false;
            if (e.SelectInEdit && inEdit && !down && !e.PressUntil) { QueueEditPress(e, now); LogOnce(logs, 6, "edit on release: confirmed (right trigger let go)"); }
            e.SelectInEdit = false;
        }
    }
    else { e.SelectHeld = select; e.SelectInEdit = false; }
    if (pressing) { state->Pad.Buttons |= e.PressButton ? e.PressButton : button; ++state->PacketNumber; }
}

// Custom binds. Index: A B X Y LB RB LT RT LS RS UP DOWN LEFT RIGHT BACK START (6 and 7 are the triggers).
constexpr int Inputs = 16, Unbound = -1;
const char* const InputNames[Inputs] = { "A","B","X","Y","LB","RB","LT","RT","LS","RS","UP","DOWN","LEFT","RIGHT","BACK","START" };
constexpr WORD InputBits[Inputs] = { 0x1000,0x2000,0x4000,0x8000,0x0100,0x0200,0,0,0x0040,0x0080,0x0001,0x0002,0x0004,0x0008,0x0020,0x0010 };
constexpr WORD AllInputBits = 0xF3FF;
constexpr BYTE TriggerPressed = 30; // XINPUT_GAMEPAD_TRIGGER_THRESHOLD
volatile LONG BindTo[Inputs] = { 0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15 };
volatile LONG SwapSticks = 0, Remapping = 0;
void Remap(PadState* state)
{
    if (!Remapping) return;
    Gamepad& pad = state->Pad;
    const Gamepad in = pad;
    pad.Buttons = WORD(in.Buttons & ~AllInputBits);
    pad.LeftTrigger = pad.RightTrigger = 0;
    for (int i = 0; i < Inputs; ++i)
    {
        const BYTE value = i == 6 ? in.LeftTrigger : i == 7 ? in.RightTrigger : (in.Buttons & InputBits[i]) ? 255 : 0;
        const LONG to = BindTo[i];
        if (!value || to < 0 || to >= Inputs) continue;
        if (to == 6) { if (value > pad.LeftTrigger) pad.LeftTrigger = value; }
        else if (to == 7) { if (value > pad.RightTrigger) pad.RightTrigger = value; }
        else if (value >= TriggerPressed) pad.Buttons |= InputBits[to];
    }
    if (SwapSticks) { pad.LX = in.RX; pad.LY = in.RY; pad.RX = in.LX; pad.RY = in.LY; }
}

DWORD WINAPI Hooked(DWORD user, PadState* state)
{
    DWORD result = Original ? Original(user, state) : ERROR_DEVICE_NOT_CONNECTED;
    if (result == ERROR_SUCCESS && state) Remap(state); // first: everything below sees the game's buttons
    static int calls = 0;
    if (result == ERROR_SUCCESS && calls < 1) { ++calls; char line[96]; sprintf_s(line, "game reads controller %lu through the hook", user); Log(line); }
    if (result == ERROR_SUCCESS && state && Enabled && state->Pad.LY > Forward)
        state->Pad.Buttons |= LeftThumb;
    if (result == ERROR_SUCCESS && state && user < 4)
    {
        EditOnRelease(user, state); // before turbo: it reads your own trigger and buttons
        static int turboLogs = 0, notBuilding = 0;
        // How often the game reads the pad (once, after a few seconds): a release has to last at least two reads
        // to be seen, however slowly the game polls.
        static ULONGLONG rateFrom = 0; static int rateCalls = 0, rateLogged = 0;
        if (!rateLogged) { const ULONGLONG t = GetTickCount64(); if (!rateFrom) rateFrom = t; ++rateCalls;
            if (t - rateFrom >= 3000) { rateLogged = 1; char line[96]; sprintf_s(line, "game reads the pad about %d times a second", int(rateCalls * 1000 / int(t - rateFrom))); Log(line); } }
        static int releaseReads[4]{}; static ULONGLONG nextPulse[4]{}; static int pulses = 0;
        if (state->Pad.RightTrigger >= TriggerHeld && InBuildMode())
        {
            const ULONGLONG now = GetTickCount64();
            if (!TriggerSince[user]) { TriggerSince[user] = now; nextPulse[user] = now + TurboFirstMs; releaseReads[user] = 0; LogOnce(turboLogs, 3, "turbo: right trigger held with a building piece in hand, re-pressing"); }
            if (releaseReads[user] > 0)
            {
                // Released for this read; the press comes back once the release has been read twice and enough time passed.
                --releaseReads[user];
                state->Pad.RightTrigger = 0; ++state->PacketNumber;
            }
            else if (now >= nextPulse[user])
            {
                releaseReads[user] = 2; // this read and the next one see the trigger let go
                nextPulse[user] = now + TurboCycleMs;
                state->Pad.RightTrigger = 0; ++state->PacketNumber;
                if (pulses < 3) { ++pulses; Log("turbo: trigger let go for a moment (new press for the game)"); }
            }
        }
        else
        {
            if (state->Pad.RightTrigger >= TriggerHeld && !TriggerSince[user])
                LogOnce(notBuilding, 3, BuildMode ? "turbo: right trigger held, no building piece in hand (or turbo build off)" : "turbo: right trigger held, TiltedClient.dll not reporting build mode yet");
            TriggerSince[user] = 0; releaseReads[user] = 0;
        }
    }
    return result;
}

// Later lookups (GetProcAddress(xinput, "XInputGetState")) get the hook too.
using GetProcFn = FARPROC(WINAPI*)(HMODULE, LPCSTR);
GetProcFn RealGetProc = nullptr;
FARPROC WINAPI HookedGetProc(HMODULE module, LPCSTR name)
{
    FARPROC real = RealGetProc ? RealGetProc(module, name) : nullptr;
    if (!real) return real;
    const bool byName = (ULONG_PTR(name) >> 16) != 0;
    if ((byName && strcmp(name, "XInputGetState") == 0) || (!byName && ULONG_PTR(name) == 2))
    {
        char path[MAX_PATH]{}; GetModuleFileNameA(module, path, MAX_PATH);
        const char* file = strrchr(path, '\\'); file = file ? file + 1 : path;
        if (_strnicmp(file, "xinput", 6) == 0)
        {
            if (!Original) Original = reinterpret_cast<GetStateFn>(real);
            static int logs = 0; LogOnce(logs, 2, "XInputGetState looked up by the game: hook handed out");
            return reinterpret_cast<FARPROC>(&Hooked);
        }
    }
    return real;
}

bool IsXInput(const char* name) { return name && _strnicmp(name, "xinput", 6) == 0; }

// The real XInputGetState from the same DLL the game imports (loaded once, shared with the game).
void ResolveOriginal(const char* dll)
{
    if (Original) return;
    if (HMODULE module = LoadLibraryA(dll))
        Original = reinterpret_cast<GetStateFn>(GetProcAddress(module, "XInputGetState"));
}

bool Patch(void** slot)
{
    if (*slot == reinterpret_cast<void*>(&Hooked)) return false;
    DWORD old = 0;
    if (!VirtualProtect(slot, sizeof(void*), PAGE_READWRITE, &old)) return false;
    *slot = reinterpret_cast<void*>(&Hooked);
    VirtualProtect(slot, sizeof(void*), old, &old);
    return true;
}

// XInputGetState is "XInputGetState" by name, or ordinal 2.
bool IsGetState(BYTE* base, ULONGLONG thunkData)
{
    if (IMAGE_SNAP_BY_ORDINAL64(thunkData)) return IMAGE_ORDINAL64(thunkData) == 2;
    auto byName = reinterpret_cast<IMAGE_IMPORT_BY_NAME*>(base + thunkData);
    return strcmp(reinterpret_cast<const char*>(byName->Name), "XInputGetState") == 0;
}

int PatchModule(HMODULE module)
{
    auto base = reinterpret_cast<BYTE*>(module);
    auto dos = reinterpret_cast<IMAGE_DOS_HEADER*>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return 0;
    auto nt = reinterpret_cast<IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE || nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC) return 0;
    int patched = 0;

    // Normal imports.
    auto& imports = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (imports.VirtualAddress)
        for (auto d = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(base + imports.VirtualAddress); d->Name; ++d)
        {
            auto dll = reinterpret_cast<const char*>(base + d->Name);
            if (module == GetModuleHandleW(nullptr) && d->OriginalFirstThunk && d->FirstThunk && _strnicmp(dll, "kernel32", 8) == 0)
            {
                auto names = reinterpret_cast<IMAGE_THUNK_DATA64*>(base + d->OriginalFirstThunk);
                auto slots = reinterpret_cast<IMAGE_THUNK_DATA64*>(base + d->FirstThunk);
                for (; names->u1.AddressOfData; ++names, ++slots)
                    if (!IMAGE_SNAP_BY_ORDINAL64(names->u1.AddressOfData) && strcmp(reinterpret_cast<IMAGE_IMPORT_BY_NAME*>(base + names->u1.AddressOfData)->Name, "GetProcAddress") == 0)
                    {
                        void** slot = reinterpret_cast<void**>(&slots->u1.Function);
                        if (*slot == reinterpret_cast<void*>(&HookedGetProc)) continue;
                        RealGetProc = reinterpret_cast<GetProcFn>(*slot);
                        DWORD old = 0;
                        if (VirtualProtect(slot, sizeof(void*), PAGE_READWRITE, &old)) { *slot = reinterpret_cast<void*>(&HookedGetProc); VirtualProtect(slot, sizeof(void*), old, &old); }
                    }
            }
            if (!IsXInput(dll) || !d->FirstThunk) continue;
            auto names = reinterpret_cast<IMAGE_THUNK_DATA64*>(base + (d->OriginalFirstThunk ? d->OriginalFirstThunk : d->FirstThunk));
            auto slots = reinterpret_cast<IMAGE_THUNK_DATA64*>(base + d->FirstThunk);
            for (; names->u1.AddressOfData; ++names, ++slots)
                if (d->OriginalFirstThunk && IsGetState(base, names->u1.AddressOfData))
                { ResolveOriginal(dll); if (Original && Patch(reinterpret_cast<void**>(&slots->u1.Function))) ++patched; }
        }

    // Delay-loaded imports (Unreal loads XInput this way).
    auto& delayed = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_DELAY_IMPORT];
    if (delayed.VirtualAddress)
        for (auto d = reinterpret_cast<ImgDelayDescr*>(base + delayed.VirtualAddress); d->rvaDLLName; ++d)
        {
            auto dll = reinterpret_cast<const char*>(base + d->rvaDLLName);
            if (!IsXInput(dll) || !d->rvaINT || !d->rvaIAT) continue;
            auto names = reinterpret_cast<IMAGE_THUNK_DATA64*>(base + d->rvaINT);
            auto slots = reinterpret_cast<IMAGE_THUNK_DATA64*>(base + d->rvaIAT);
            for (; names->u1.AddressOfData; ++names, ++slots)
                if (IsGetState(base, names->u1.AddressOfData))
                { ResolveOriginal(dll); if (Original && Patch(reinterpret_cast<void**>(&slots->u1.Function))) ++patched; }
        }
    return patched;
}

wchar_t Folder[MAX_PATH]{};
bool FileThere(const wchar_t* name)
{
    if (!Folder[0]) return false;
    wchar_t path[MAX_PATH]; swprintf_s(path, L"%s\\%s", Folder, name);
    return GetFileAttributesW(path) != INVALID_FILE_ATTRIBUTES;
}
// pad-edit-button.txt: A, B, X, Y, LB, RB, LS, RS, UP, DOWN, LEFT, RIGHT (default B, Fortnite's edit button).
LONG ReadPadButton(const wchar_t* file, LONG fallback)
{
    if (!Folder[0]) return fallback;
    wchar_t path[MAX_PATH]; swprintf_s(path, L"%s\\%s", Folder, file);
    char text[16]{}; FILE* in = nullptr;
    if (_wfopen_s(&in, path, L"r") == 0 && in) { fgets(text, sizeof(text), in); fclose(in); }
    for (char* c = text; *c; ++c) if (*c == '\r' || *c == '\n' || *c == ' ') { *c = 0; break; }
    struct { const char* Name; LONG Mask; } map[] = { {"A",0x1000},{"B",0x2000},{"X",0x4000},{"Y",0x8000},{"LB",0x0100},{"RB",0x0200},{"LS",0x0040},{"RS",0x0080},{"UP",0x0001},{"DOWN",0x0002},{"LEFT",0x0004},{"RIGHT",0x0008} };
    for (auto& m : map) if (_stricmp(text, m.Name) == 0) return m.Mask;
    return fallback;
}
LONG ReadEditButton() { return ReadPadButton(L"pad-edit-button.txt", 0x2000); }

// controller-binds.txt (see the top of this file). Missing or empty: every button where it was.
void ReadBinds()
{
    LONG to[Inputs]; for (int i = 0; i < Inputs; ++i) to[i] = i;
    LONG swap = 0;
    if (Folder[0])
    {
        wchar_t path[MAX_PATH]; swprintf_s(path, L"%s\\controller-binds.txt", Folder);
        FILE* in = nullptr;
        if (_wfopen_s(&in, path, L"r") == 0 && in)
        {
            char line[64];
            while (fgets(line, sizeof(line), in))
            {
                for (char* c = line; *c; ++c) if (*c == '\r' || *c == '\n' || *c == ' ') { *c = 0; break; }
                char* eq = strchr(line, '='); if (!eq) continue; *eq = 0;
                const char* value = eq + 1;
                if (_stricmp(line, "SWAPSTICKS") == 0) { swap = strcmp(value, "1") == 0; continue; }
                int from = -1, target = -2;
                for (int i = 0; i < Inputs; ++i) { if (_stricmp(line, InputNames[i]) == 0) from = i; if (_stricmp(value, InputNames[i]) == 0) target = i; }
                if (_stricmp(value, "NONE") == 0) target = Unbound;
                if (from >= 0 && target != -2) to[from] = target;
            }
            fclose(in);
        }
    }
    bool changed = InterlockedExchange(&SwapSticks, swap) != swap, custom = swap != 0;
    for (int i = 0; i < Inputs; ++i) { if (InterlockedExchange(&BindTo[i], to[i]) != to[i]) changed = true; if (to[i] != i) custom = true; }
    // Season 6 and later have Fortnite's own controller binds: the launcher writes controller-binds-native-season.txt
    // while one of those runs, and the layout here stays off.
    InterlockedExchange(&Remapping, custom && !FileThere(L"controller-binds-native-season.txt") ? 1 : 0);
    static bool first = true;
    if (changed || first) { first = false; Log(custom ? "custom controller binds on" : "controller binds: default layout"); }
}

DWORD WINAPI Start(void*)
{
    wchar_t local[MAX_PATH]{};
    if (GetEnvironmentVariableW(L"LOCALAPPDATA", local, MAX_PATH))
    {
        swprintf_s(Flag, L"%s\\ProjectTilted\\sprint-default-on.txt", local);
        swprintf_s(NativeSeason, L"%s\\ProjectTilted\\sprint-native-season.txt", local);
        swprintf_s(Folder, L"%s\\SyndicateClassic", local);
    }

    int patched = 0;
    HMODULE modules[1024]; DWORD needed = 0;
    if (K32EnumProcessModules(GetCurrentProcess(), modules, sizeof(modules), &needed))
        for (DWORD i = 0; i < needed / sizeof(HMODULE) && i < 1024; ++i)
            if (modules[i] != Self)
            {
                const int n = PatchModule(modules[i]); patched += n;
                if (n) { char name[MAX_PATH]{}; K32GetModuleBaseNameA(GetCurrentProcess(), modules[i], name, MAX_PATH); char line[MAX_PATH + 48]; sprintf_s(line, "XInputGetState hooked in %s", name); Log(line); }
            }
    char line[128];
    sprintf_s(line, "XInputGetState hooks installed: %d", patched);
    Log(line);

    // Follow the launcher's switch (Settings > Modded) without restarting the game.
    for (;;)
    {
        const bool On = Flag[0] && GetFileAttributesW(Flag) != INVALID_FILE_ATTRIBUTES;
        const bool Native = NativeSeason[0] && GetFileAttributesW(NativeSeason) != INVALID_FILE_ATTRIBUTES;
        InterlockedExchange(&Enabled, On && !Native ? 1 : 0);
        InterlockedExchange(&EditOn, FileThere(L"edit-release-on.txt") ? 1 : 0);
        InterlockedExchange(&ResetOn, FileThere(L"reset-release-on.txt") ? 1 : 0);
        InterlockedExchange(&ResetButton, ReadPadButton(L"pad-reset-button.txt", 0x0080));
        InterlockedExchange(&EditSelectRelease, FileThere(L"edit-release-select-off.txt") ? 0 : 1);
        InterlockedExchange(&EditAnyTile, FileThere(L"edit-release-any.txt") ? 1 : 0);
        const LONG button = ReadEditButton();
        if (InterlockedExchange(&EditButton, button) != button) { char line[80]; sprintf_s(line, "controller edit button 0x%04lx", button); Log(line); }
        ReadBinds();
        Sleep(500);
    }
}
}

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH)
    {
        Self = module;
        DisableThreadLibraryCalls(module);
        if (HANDLE thread = CreateThread(nullptr, 0, Start, nullptr, 0, nullptr)) CloseHandle(thread);
    }
    return TRUE;
}
