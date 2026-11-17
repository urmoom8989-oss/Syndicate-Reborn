#include "pch.h"
#include "../Public/StudioPipe.h"
#include "../Public/StudioCamera.h"
#include <algorithm>
#include <map>
#include <intrin.h> // _ReturnAddress
#include <objbase.h> // CoCreateGuid (WIN32_LEAN_AND_MEAN leaves it out)
#include "../External/minhook/include/MinHook.h"
#pragma comment(lib, "ole32.lib")

// SyndicateStudio.dll: loaded into Fortnite by the launcher when Syndicate Studio docks the game.
// Provides direct UE4 access for: camera control, actor enumeration, world-to-screen
// projection, raycasting, and actor manipulation. Communicates with the launcher via
// a named pipe (\\.\pipe\SyndicateStudio_<pid>).
//
// Architecture mirrors SyndicateConsole.cpp: find the game window, install a WH_GETMESSAGE
// hook on the game thread, execute UE4 operations when our custom message fires.
// The pipe server runs on its own thread; requests that touch UE4 objects are queued
// and executed on the game thread via the message hook.

namespace {
    constexpr UINT StudioMessage = WM_APP + 0x5A0; // unique to SyndicateStudio, avoids SyndicateConsole's 0x4A2 and SyndicateClient's 0x4F0
    HMODULE SelfModule = nullptr;
    HWND GameWindow = nullptr;
    HHOOK MessageHook = nullptr;
    std::atomic<bool> MessagePending{ false }; // one Studio message in the queue at a time
    std::atomic<ULONGLONG> PostedAt{ 0 };

    void PostStudioMessage() {
        // If a message was lost (window recreated, hook reinstalled), don't wait on it forever
        if (MessagePending.load() && GetTickCount64() - PostedAt.load() > 250) MessagePending.store(false);
        if (GameWindow && MessageHook && !MessagePending.exchange(true)) {
            PostedAt.store(GetTickCount64());
            PostMessageW(GameWindow, StudioMessage, 0, 0);
        }
    }

    void Log(const char* Text) {
        wchar_t Base[MAX_PATH]{};
        if (!GetEnvironmentVariableW(L"LOCALAPPDATA", Base, MAX_PATH)) return;
        std::wstring File = std::wstring(Base) + L"\\ProjectTilted\\syndicate-studio.log";
        FILE* Out = nullptr;
        if (_wfopen_s(&Out, File.c_str(), L"a") == 0 && Out) {
            SYSTEMTIME T; GetLocalTime(&T);
            fprintf(Out, "%02d:%02d:%02d pid=%lu %s\n", T.wHour, T.wMinute, T.wSecond, GetCurrentProcessId(), Text);
            fclose(Out);
        }
    }

    // Save the World local tutorial (dev launcher): %LOCALAPPDATA%\ProjectTilted\stw-local.flag turns it on; the
    // backend writes stw-local-go.txt when the game asks for the tutorial, and the game thread then opens the
    // tutorial zone in this game (standalone: this game is its own server).
    std::wstring StwShared() {
        wchar_t Base[MAX_PATH]{};
        if (!GetEnvironmentVariableW(L"LOCALAPPDATA", Base, MAX_PATH)) return L"";
        return std::wstring(Base) + L"\\ProjectTilted\\";
    }
    bool StwLocal = false;
    std::atomic<bool> StwOpenPending{ false };
    std::atomic<bool> StwOpened{ false };      // the tutorial is opened once per game session
    ULONGLONG StwCancelUntil = 0, StwNextCancel = 0;
    void StwPoll() {
        if (!StwLocal) return;
        auto Go = StwShared() + L"stw-local-go.txt";
        if (GetFileAttributesW(Go.c_str()) == INVALID_FILE_ATTRIBUTES) return;
        DeleteFileW(Go.c_str());
        // The game keeps retrying its tutorial matchmaking every few seconds; open the map only the first time.
        if (StwOpened.load() || StwOpenPending.load()) return;
        StwOpenPending.store(true);
        Log("SyndicateStudio: Save the World tutorial requested");
    }
    // The game's matchmaking (UFortMatchmakingContext) would keep searching and keep the loading screen up
    // ("Matchmaking is pending"); the tutorial needs no server, so stop it.
    bool StwCancelMatchmaking_Impl() {
        static const UClass* Cls = nullptr;
        if (!Cls) Cls = FindClass("FortMatchmakingContext");
        if (!Cls) return false;
        bool Any = false;
        const int32 Count = TUObjectArray::Num();
        for (int32 i = 0; i < Count; i++) {
            auto Obj = (UObject*)TUObjectArray::GetObjectByIndex(i);
            if (!Obj || Obj->IsDefaultObject() || !Obj->IsA(Cls)) continue;
            if (auto F = Obj->GetFunction("CancelMatchmaking")) { alignas(16) uint8_t Buf[64]{}; Obj->ProcessEvent(F, Buf); Any = true; }
        }
        return Any;
    }
    bool StwCancelMatchmaking() { __try { return StwCancelMatchmaking_Impl(); } __except (EXCEPTION_EXECUTE_HANDLER) { return false; } }
    // What is the tutorial waiting for? Log the simple properties of the zone's game mode, game state and player
    // controller (bools, bytes, ints, floats, names, object pointers), up to the engine base classes.
    void StwDumpObject_Impl(const char* Label, UObject* Obj) {
        char Line[512];
        if (!Obj) { snprintf(Line, sizeof(Line), "SyndicateStudio: stw dump %s: none", Label); Log(Line); return; }
        snprintf(Line, sizeof(Line), "SyndicateStudio: stw dump %s: %s", Label, Obj->Class ? std::string(Obj->Class->Name.ToString().c_str()).c_str() : "?"); Log(Line);
        for (auto C = (const UStruct*)Obj->Class; C; C = C->GetSuper()) {
            std::string CName(((const UField*)C)->GetName().ToString().c_str());
            if (CName == "Actor" || CName == "Object" || CName == "Info") break;
            for (const UField* P = C->GetChildren(); P; P = P->GetNext()) {
                if (!P->Class) continue;
                std::string Type(P->Class->Name.ToString().c_str());
                uint32 Off = GetFromOffset<uint32>(P, Offsets::Offset_Internal);
                auto At = (uint8*)Obj + Off;
                char V[160] = "";
                if (Type == "BoolProperty" || Type == "ByteProperty" || Type == "EnumProperty") snprintf(V, sizeof(V), "0x%02x", *At);
                else if (Type == "IntProperty") snprintf(V, sizeof(V), "%d", *(int32*)At);
                else if (Type == "FloatProperty") snprintf(V, sizeof(V), "%g", *(float*)At);
                else if (Type == "ArrayProperty") snprintf(V, sizeof(V), "[%d]", *(int32*)(At + 8));
                else if (Type == "StrProperty") { auto& Str = *(FString*)At; snprintf(V, sizeof(V), "\"%s\"", Str.Num() > 0 ? std::string(Str.ToString().c_str()).substr(0, 140).c_str() : ""); }
                else if (Type == "NameProperty") snprintf(V, sizeof(V), "%s", std::string(((FName*)At)->ToString().c_str()).c_str());
                else if (Type == "ObjectProperty" || Type == "ClassProperty") { auto O = *(UObject**)At; snprintf(V, sizeof(V), "%s", O ? std::string(O->Name.ToString().c_str()).c_str() : "null"); }
                else continue;
                snprintf(Line, sizeof(Line), "SyndicateStudio:   %s::%s +0x%x = %s", CName.c_str(), std::string(P->GetName().ToString().c_str()).c_str(), Off, V);
                Log(Line);
            }
        }
    }
    void StwListFunctions_Impl(UObject* Obj) {
        if (!Obj) return;
        char Line[300];
        for (auto C = (const UStruct*)Obj->Class; C; C = C->GetSuper()) {
            std::string CName(((const UField*)C)->GetName().ToString().c_str());
            if (CName == "Actor" || CName == "Object" || CName == "Info") break;
            for (const UField* P = C->GetChildren(); P; P = P->GetNext())
                if (P->Class && std::string(P->Class->Name.ToString().c_str()) == "Function") {
                    snprintf(Line, sizeof(Line), "SyndicateStudio:   function %s::%s", CName.c_str(), std::string(P->GetName().ToString().c_str()).c_str());
                    Log(Line);
                }
        }
    }
    void StwListFunctions(UObject* Obj) { __try { StwListFunctions_Impl(Obj); } __except (EXCEPTION_EXECUTE_HANDLER) {} }
    void StwDumpObject(const char* Label, UObject* Obj) { __try { StwDumpObject_Impl(Label, Obj); } __except (EXCEPTION_EXECUTE_HANDLER) { Log("SyndicateStudio: stw dump crashed (caught)"); } }
    // Which URL options / names does the game know about zone instances? List the exe's wide strings that mention them.
    void StwListZoneStrings_Impl() {
        auto Base = (uint8_t*)GetModuleHandleW(nullptr); auto Nt = (IMAGE_NT_HEADERS64*)(Base + ((IMAGE_DOS_HEADER*)Base)->e_lfanew);
        auto Sec = IMAGE_FIRST_SECTION(Nt);
        int Found = 0;
        for (int i = 0; i < Nt->FileHeader.NumberOfSections && Found < 120; i++) {
            if (Sec[i].Characteristics & IMAGE_SCN_MEM_EXECUTE) continue;
            auto Start = (wchar_t*)(Base + Sec[i].VirtualAddress); size_t Count = Sec[i].Misc.VirtualSize / 2;
            for (size_t o = 0; o + 16 < Count && Found < 120; o++) {
                bool Hit = (Start[o] == L'Z' && wcsncmp(Start + o, L"ZoneInstance", 12) == 0)
                    || (Start[o] == L'/' && wcsncmp(Start + o, L"/world/", 7) == 0)
                    || (Start[o] == L'Z' && wcsncmp(Start + o, L"ZONEINSTANCE", 12) == 0)
                    || (Start[o] == L't' && wcsncmp(Start + o, L"theaterId", 9) == 0)
                    || (Start[o] == L'z' && wcsncmp(Start + o, L"zoneInstance", 12) == 0);
                if (!Hit) continue;
                size_t b = o; while (b > 0 && Start[b - 1] >= 32 && Start[b - 1] < 127 && o - b < 80) b--;
                size_t e = o; while (e < Count && Start[e] >= 32 && Start[e] < 127 && e - o < 160) e++;
                char Line[400]; int n = snprintf(Line, sizeof(Line), "SyndicateStudio: exe text: ");
                for (size_t k = b; k < e && n < (int)sizeof(Line) - 1; k++) Line[n++] = (char)Start[k];
                Line[n] = 0; Log(Line); Found++; o = e;
            }
        }
    }
    void StwListZoneStrings() { __try { StwListZoneStrings_Impl(); } __except (EXCEPTION_EXECUTE_HANDLER) { Log("SyndicateStudio: text scan crashed (caught)"); } }
    UObject* StwWorldManager = nullptr; ULONGLONG StwWatchUntil = 0; int StwLastState = -1;
    void StwWatch_Impl() {
        auto F = StwWorldManager->GetOffset("WorldManagerState");
        int State = F != uint32(-1) ? GetFromOffset<uint8_t>(StwWorldManager, F) : -1;
        if (State != StwLastState) { StwLastState = State; char L[100]; snprintf(L, sizeof(L), "SyndicateStudio: world manager state now %d", State); Log(L); }
    }
    void StwWatch() { __try { StwWatch_Impl(); } __except (EXCEPTION_EXECUTE_HANDLER) { StwWatchUntil = 0; } }
    ULONGLONG StwKickAt = 0;
    ULONGLONG StwRestartAt = 0;
    ULONGLONG StwDressAt = 0; // see StwDress
    ULONGLONG StwAIWatchUntil = 0, StwAINext = 0; // see StwAIWatch
    ULONGLONG StwSanitizeFrom = 0, StwSanitizeNext = 0; // see StwSanitizeEncounters
    std::vector<UObject*> StwStartedEncounters;
    // The zone game mode never took the ZoneInstanceId from the URL (standalone has no game session). Set it on
    // the game mode and game state, then ask the match to start.
    bool StwSetFlag(UObject* Obj, const char* Name) {
        if (!Obj) return false;
        auto Prop = Obj->GetProperty(Name);
        if (!Prop) return false;
        auto Off = GetFromOffset<uint32>(Prop, Offsets::Offset_Internal);
        uint8_t Mask = Prop->GetFieldMask(); if (!Mask) Mask = 0xFF;
        GetFromOffset<uint8_t>(Obj, Off) |= Mask;
        char Line[160]; snprintf(Line, sizeof(Line), "SyndicateStudio: set %s (+0x%x mask 0x%02x)", Name, Off, Mask); Log(Line);
        return true;
    }
    void StwDumpTextUsers(const wchar_t* Text);
    void StwFindReadySetters();
    // ---- The tutorial's mission data ----
    // The zone's mission (Ramirez, the husks, the objectives) comes from UFortWorldRecord::ZoneInstanceInfo, which a
    // real server fills from its game session. Here it stays empty ("ZoneInfo Invalid!"), so nothing spawns. Fill
    // it from the backend's tutorial mission before the match starts.
    // A search walks every object and makes a string of each name, which costs a visible frame stall; several
    // callers run often (inventory counts, status logging). Remember what was found and reuse it while that object
    // is still alive at its index with the same name.
    UObject* StwFindByName(const char* Name, const char* ClassName) {
        static std::map<std::string, UObject*> Found;
        const std::string Key = std::string(Name) + "|" + (ClassName ? ClassName : "");
        if (auto It = Found.find(Key); It != Found.end()) {
            auto O = It->second;
            if (TUObjectArray::GetObjectByIndex(O->Index) == O && std::string(O->Name.ToString().c_str()) == Name) return O;
            Found.erase(It);
        }
        const int32 Count = TUObjectArray::Num();
        for (int32 i = 0; i < Count; i++) {
            auto Obj = (UObject*)TUObjectArray::GetObjectByIndex(i);
            if (!Obj || !Obj->Class || std::string(Obj->Name.ToString().c_str()) != Name) continue;
            if (ClassName && std::string(Obj->Class->Name.ToString().c_str()) != ClassName) continue;
            Found[Key] = Obj;
            return Obj;
        }
        return nullptr;
    }
    // A field of a struct (or class) by name, walking its parents: offset and element size, or false.
    bool StwField(const UStruct* S, const char* Name, uint32& Off, uint32& Size, std::string* Type = nullptr) {
        for (auto C = S; C; C = C->GetSuper())
            for (const UField* P = C->GetChildren(); P; P = P->GetNext())
                if (std::string(P->GetName().ToString().c_str()) == Name) {
                    Off = GetFromOffset<uint32>(P, Offsets::Offset_Internal);
                    Size = GetFromOffset<uint32>(P, Offsets::ElementSize);
                    if (Type && P->Class) *Type = std::string(P->Class->Name.ToString().c_str());
                    return true;
                }
        return false;
    }
    void StwLogStruct(const UStruct* S, const char* Label) {
        char Line[300];
        for (auto C = S; C; C = C->GetSuper())
            for (const UField* P = C->GetChildren(); P; P = P->GetNext()) {
                snprintf(Line, sizeof(Line), "SyndicateStudio:   %s %s.%s (%s) +0x%x size 0x%x", Label, std::string(((const UField*)C)->GetName().ToString().c_str()).c_str(),
                    std::string(P->GetName().ToString().c_str()).c_str(), P->Class ? std::string(P->Class->Name.ToString().c_str()).c_str() : "?",
                    GetFromOffset<uint32>(P, Offsets::Offset_Internal), GetFromOffset<uint32>(P, Offsets::ElementSize));
                Log(Line);
            }
    }
    void StwFillZoneInfo_Impl() {
        auto Details = (const UStruct*)StwFindByName("FortZoneInstanceDetails", "ScriptStruct");
        if (!Details) { Log("SyndicateStudio: FortZoneInstanceDetails not found"); return; }
        StwLogStruct(Details, "zone details");
        auto DifficultyTable = StwFindByName("GameDifficultyGrowthBounds", "DataTable");
        const int32 Count = TUObjectArray::Num();
        int Filled = 0;
        for (int32 i = 0; i < Count; i++) {
            auto Rec = (UObject*)TUObjectArray::GetObjectByIndex(i);
            if (!Rec || !Rec->Class || Rec->IsDefaultObject() || std::string(Rec->Class->Name.ToString().c_str()) != "FortWorldRecord") continue;
            uint32 InfoOff, InfoSize;
            if (!StwField((const UStruct*)Rec->Class, "ZoneInstanceInfo", InfoOff, InfoSize)) { Log("SyndicateStudio: world record has no ZoneInstanceInfo"); continue; }
            auto Info = (uint8_t*)Rec + InfoOff;
            uint32 O, Sz; std::string T;
            auto SetStr = [&](const char* Name, const wchar_t* Value) { if (StwField(Details, Name, O, Sz, &T) && T == "StrProperty") *(FString*)(Info + O) = FString(Value); };
            // TAssetSubclassOf (4.16): weak pointer, tag, then the asset path as an FString at +0x10.
            auto SetSoft = [&](const char* Name, const wchar_t* Path) { if (StwField(Details, Name, O, Sz, &T) && Sz >= 0x20) *(FString*)(Info + O + 0x10) = FString(Path); };
            // The world manager only creates the zone's entry in the world record (and from it the zone's levels
            // and missions) when the zone has a world id and the game mode a zone index (1.7.2
            // HandleWorldQueryComplete, exe+0x60cd90). Both come from the URL too (worldId, ?zoneIndex=0).
            SetStr("WorldId", L"SyndicateStwTutorialWorld");
            SetStr("TheaterId", L"8633333E41A86F67F78EAEAF25BF4735");
            SetStr("TheaterMissionId", L"b9c086b0-d157-446b-b52c-0648752201d3");
            SetSoft("ZoneThemeClass", L"/Game/World/ZoneThemes/Onboarding/ZT_OnboardingFort/BP_ZT_Onboarding_Fort.BP_ZT_Onboarding_Fort_C");
            SetSoft("MissionGenerator", L"/Game/World/MissionGens/MissionGen_Onboarding_Fort.MissionGen_Onboarding_Fort_C");
            if (StwField(Details, "ZoneDifficultyInfo", O, Sz)) { *(UObject**)(Info + O) = DifficultyTable; *(FName*)(Info + O + 8) = FName(L"Theater_Tutorial_Zone1"); }
            if (StwField(Details, "TileIndex", O, Sz)) *(int32*)(Info + O) = 0;
            if (StwField((const UStruct*)Rec->Class, "CurrentZoneIndex", O, Sz)) *(int32*)((uint8_t*)Rec + O) = 0;
            char Line[200]; snprintf(Line, sizeof(Line), "SyndicateStudio: filled the tutorial zone info on %s (difficulty table %p)", std::string(Rec->Name.ToString().c_str()).c_str(), DifficultyTable); Log(Line);
            Filled++;
        }
        if (!Filled) Log("SyndicateStudio: no world record to fill");
    }
    void StwFillZoneInfo() { __try { StwFillZoneInfo_Impl(); } __except (EXCEPTION_EXECUTE_HANDLER) { Log("SyndicateStudio: zone info fill crashed (caught)"); } }

    // ---- Fill the mission data at the moment the map load checks it ----
    // While the zone map loads, the game creates the world record and immediately checks its mission data
    // (the function that logs "ZoneInfo Invalid!", 1.7.2 exe+0x608e10); the zone, the mission, the spawn pad and the
    // player's hero are set up from what it finds. A one-shot hook on that function fills the record first, puts
    // the original code back and runs it. (A 14-byte jump; this DLL doesn't link MinHook.)
    uint8_t* StwCheckFn = nullptr; uint8_t StwCheckSaved[14]{}; bool StwCheckHooked = false;
    void StwFillZoneInfo();
    uintptr_t StwFindTextUser(const wchar_t* Text) {
        auto Base = (uint8_t*)GetModuleHandleW(nullptr); auto Nt = (IMAGE_NT_HEADERS64*)(Base + ((IMAGE_DOS_HEADER*)Base)->e_lfanew);
        auto Sec = IMAGE_FIRST_SECTION(Nt); const size_t Len = wcslen(Text) * 2; uint8_t* Str = nullptr;
        for (int i = 0; i < Nt->FileHeader.NumberOfSections && !Str; i++) {
            if (Sec[i].Characteristics & IMAGE_SCN_MEM_EXECUTE) continue;
            auto St = Base + Sec[i].VirtualAddress; size_t Size = Sec[i].Misc.VirtualSize;
            for (size_t o = 0; o + Len <= Size; o += 2) if (memcmp(St + o, Text, Len) == 0) { Str = St + o; break; }
        }
        if (!Str) return 0;
        for (int i = 0; i < Nt->FileHeader.NumberOfSections; i++) {
            if (!(Sec[i].Characteristics & IMAGE_SCN_MEM_EXECUTE)) continue;
            auto St = Base + Sec[i].VirtualAddress; size_t Size = Sec[i].Misc.VirtualSize;
            for (size_t o = 0; o + 7 <= Size; o++) {
                if ((St[o] != 0x48 && St[o] != 0x4C) || St[o + 1] != 0x8D || (St[o + 2] & 0xC7) != 0x05) continue;
                int32_t D; memcpy(&D, St + o + 3, 4);
                if (St + o + 7 + D != Str) continue;
                DWORD64 IB = 0; auto F = RtlLookupFunctionEntry((DWORD64)(St + o), &IB, nullptr);
                for (int d = 0; F && d < 8; d++) { auto Info = (uint8_t*)(IB + F->UnwindData); if (!((Info[0] >> 3) & 4)) break; F = (PRUNTIME_FUNCTION)(Info + 4 + ((Info[2] + 1) & ~1) * 2); }
                return F ? (uintptr_t)(IB + F->BeginAddress) : 0;
            }
        }
        return 0;
    }
    void StwPatch(uint8_t* At, const uint8_t* Bytes, size_t N) {
        DWORD Old; VirtualProtect(At, N, PAGE_EXECUTE_READWRITE, &Old); memcpy(At, Bytes, N); VirtualProtect(At, N, Old, &Old);
        FlushInstructionCache(GetCurrentProcess(), At, N);
    }
    uint64_t StwCheckHook(void* A, void* B, void* C, void* D) {
        Log("SyndicateStudio: the zone is checking its mission data: filling it now");
        StwFillZoneInfo();
        StwPatch(StwCheckFn, StwCheckSaved, sizeof(StwCheckSaved)); // put the original back (one shot)
        StwCheckHooked = false;
        return ((uint64_t(*)(void*, void*, void*, void*))StwCheckFn)(A, B, C, D);
    }
    void StwHookZoneCheck() {
        if (StwCheckHooked) return;
        if (!StwCheckFn) StwCheckFn = (uint8_t*)StwFindTextUser(L"ZoneInfo Invalid!");
        if (!StwCheckFn) { Log("SyndicateStudio: the zone's mission data check was not found"); return; }
        memcpy(StwCheckSaved, StwCheckFn, sizeof(StwCheckSaved));
        uint8_t Jmp[14] = { 0xFF, 0x25, 0, 0, 0, 0 }; auto Target = (uint64_t)&StwCheckHook; memcpy(Jmp + 6, &Target, 8);
        StwPatch(StwCheckFn, Jmp, sizeof(Jmp));
        StwCheckHooked = true;
        char L[120]; snprintf(L, sizeof(L), "SyndicateStudio: hooked the zone's mission data check at exe+0x%llx", (unsigned long long)((uintptr_t)StwCheckFn - (uintptr_t)GetModuleHandleW(nullptr))); Log(L);
    }

    // HandleWorldQueryComplete (1.7.2 exe+0x60cd90) makes the zone's entry in the world record, and from it the
    // zone's levels and missions, only when the world manager has a zone index (CurrentZoneIndex != -1) and the
    // zone a world id. The world manager copies the zone index from the game mode only on the path a real server
    // takes (a game session with a zone instance), so here it stays -1. A one-shot hook on HandleWorldQueryComplete
    // sets it to 0 (and fills the zone info again) right before it runs.
    uint8_t* StwQueryFn = nullptr; uint8_t StwQuerySaved[14]{}; bool StwQueryHooked = false;
    void StwQueryFix_Impl(UObject* WM) {
        if (!WM || !WM->Class) return;
        std::string Cls(WM->Class->Name.ToString().c_str());
        char Line[200];
        if (Cls.find("WorldManager") == std::string::npos) { snprintf(Line, sizeof(Line), "SyndicateStudio: world query: unexpected object %s", Cls.c_str()); Log(Line); return; }
        StwFillZoneInfo();
        if (auto O = WM->GetOffset("CurrentZoneIndex"); O != uint32(-1)) {
            auto& Index = GetFromOffset<int32>(WM, O);
            snprintf(Line, sizeof(Line), "SyndicateStudio: world query: zone index was %d, now 0", Index); Log(Line);
            if (Index < 0) Index = 0;
        }
    }
    uint64_t StwQueryHook(void* A, void* B, void* C, void* D) {
        __try { StwQueryFix_Impl((UObject*)A); } __except (EXCEPTION_EXECUTE_HANDLER) { Log("SyndicateStudio: world query fix crashed (caught)"); }
        StwPatch(StwQueryFn, StwQuerySaved, sizeof(StwQuerySaved));
        StwQueryHooked = false;
        return ((uint64_t(*)(void*, void*, void*, void*))StwQueryFn)(A, B, C, D);
    }
    void StwHookWorldQuery() {
        if (StwQueryHooked) return;
        if (!StwQueryFn) StwQueryFn = (uint8_t*)StwFindTextUser(L"HandleWorldQueryComplete: Creating brand new zone data for %s. Current Zones = %d");
        if (!StwQueryFn) { Log("SyndicateStudio: HandleWorldQueryComplete not found"); return; }
        memcpy(StwQuerySaved, StwQueryFn, sizeof(StwQuerySaved));
        uint8_t Jmp[14] = { 0xFF, 0x25, 0, 0, 0, 0 }; auto Target = (uint64_t)&StwQueryHook; memcpy(Jmp + 6, &Target, 8);
        StwPatch(StwQueryFn, Jmp, sizeof(Jmp));
        StwQueryHooked = true;
        char L[120]; snprintf(L, sizeof(L), "SyndicateStudio: hooked HandleWorldQueryComplete at exe+0x%llx", (unsigned long long)((uintptr_t)StwQueryFn - (uintptr_t)GetModuleHandleW(nullptr))); Log(L);
    }

    // The client exe won't create objects of server-only classes ("Unable to spawn class 'AIDirector_Fortnite_C'
    // due to client/server context"): the AI director (husks), the mission generation manager and the like. A zone
    // server made them; here we are the zone server, so let UObject::CanCreateInCurrentContext always say yes, as
    // Erbium does for its Battle Royale server (Finders.cpp, RetTrueFuncs).
    void StwAllowServerClasses() {
        static bool Done = false;
        if (Done) return;
        Done = true;
        auto At = Memcury::Scanner::FindPattern("8B ? E8 ? ? ? ? 84 C0 75 ? 80 3D ? ? ? ? 03 0F 82 ? ? ? ? ? 8B ? 18 ? 8D 54");
        if (!At.IsValid()) At = Memcury::Scanner::FindPattern("8B ? E8 ? ? ? ? 84 C0 0F 85 ? ? ? ? 80 3D ? ? ? ? 03 0F 82 ? ? ? ? ? 8B ? 18 ? 8D 54");
        if (!At.IsValid()) At = Memcury::Scanner::FindPattern("8B ? E8 ? ? ? ? 84 C0 75 ? 80 3D ? ? ? ? 03 72 ? ? 8B ? 18 ? 8D 54");
        if (!At.IsValid()) { Log("SyndicateStudio: CanCreateInCurrentContext not found (server-only classes stay blocked)"); return; }
        auto Fn = (uint8_t*)At.RelativeOffset(3).Get();
        const uint8_t RetTrue[] = { 0x31, 0xC0, 0xFF, 0xC0, 0xC3 }; // xor eax,eax; inc eax; ret
        StwPatch(Fn, RetTrue, sizeof(RetTrue));
        char L[120]; snprintf(L, sizeof(L), "SyndicateStudio: server-only classes allowed (CanCreateInCurrentContext at exe+0x%llx)", (unsigned long long)((uintptr_t)Fn - (uintptr_t)GetModuleHandleW(nullptr))); Log(L);
    }

    void StwKickZone_Impl() {
        auto World = UWorld::GetWorld();
        if (!World) return;
        auto GM = (UObject*)World->AuthorityGameMode, GS = (UObject*)World->GameState;
        const wchar_t* Zone = L"{\"theaterId\":\"8633333E41A86F67F78EAEAF25BF4735\",\"theaterMissionId\":\"b9c086b0-d157-446b-b52c-0648752201d3\",\"theaterMissionAlertId\":\"\",\"zoneThemeClass\":\"/Game/World/ZoneThemes/Onboarding/ZT_OnboardingFort/BP_ZT_Onboarding_Fort.BP_ZT_Onboarding_Fort_C\"}";
        if (GM) {
            if (auto O = GM->GetOffset("CurrentZoneInstanceId"); O != uint32(-1)) GetFromOffset<FString>(GM, O) = FString(Zone);
            Log("SyndicateStudio: zone instance set on the game mode; game mode functions:");
            StwListFunctions(GM);
        }
        if (GS) {
            if (auto O = GS->GetOffset("TheaterUniqueId"); O != uint32(-1)) GetFromOffset<FString>(GS, O) = FString(L"8633333E41A86F67F78EAEAF25BF4735");
            Log("SyndicateStudio: game state functions:");
            StwListFunctions(GS);
        }
        StwFillZoneInfo();
        // The game mode waits for the world record and theater data from a game session this game doesn't have.
        static bool Dumped = false;
        if (!Dumped) {
            Dumped = true;
            for (auto Name : { "FortWorldRecord" }) {
                const int32 Count = TUObjectArray::Num();
                for (int32 i = 0, Count = TUObjectArray::Num(); i < Count; i++) {
                    auto Obj = (UObject*)TUObjectArray::GetObjectByIndex(i);
                    if (!Obj || !Obj->Class || Obj->IsDefaultObject() || std::string(Obj->Class->Name.ToString().c_str()) != Name) continue;
                    char Line[200]; snprintf(Line, sizeof(Line), "SyndicateStudio: object %s (%s):", std::string(Obj->Name.ToString().c_str()).c_str(), Name); Log(Line);
                    StwListFunctions(Obj); StwDumpObject(Name, Obj);
                    break;
                }
            }
        }
        StwSetFlag(GM, "bWorldIsReady");
        StwSetFlag(GM, "bTheaterDataIsReady");
        if (GM) if (auto F = GM->GetFunction("StartMatch")) { alignas(16) uint8_t Buf[64]{}; GM->ProcessEvent(F, Buf); Log("SyndicateStudio: StartMatch called"); }
        StwRestartAt = GetTickCount64() + 3000;
    }
    // A few seconds after the match starts: mark the player as loaded and spawn them (GameModeBase::RestartPlayer).
    void StwRestart_Impl() {
        auto World = UWorld::GetWorld();
        if (!World) return;
        auto GM = (UObject*)World->AuthorityGameMode;
        UObject* PC = nullptr;
        if (auto Instance = World->OwningGameInstance; Instance && Instance->LocalPlayers.Num() && Instance->LocalPlayers[0]) PC = (UObject*)Instance->LocalPlayers[0]->PlayerController;
        if (!GM || !PC) { Log("SyndicateStudio: no game mode or controller to spawn"); return; }
        StwSetFlag(PC, "bHasServerFinishedLoading");
        StwSetFlag(PC, "bHasClientFinishedLoading");
        StwSetFlag(PC, "bReadyToStartMatch");
        StwSetFlag(PC, "bClientPawnIsLoaded");
        if (auto O = PC->GetOffset("Pawn"); O != uint32(-1) && GetFromOffset<void*>(PC, O)) { Log("SyndicateStudio: the player already has a pawn"); StwDressAt = GetTickCount64() + 2000; return; }
        if (auto F = GM->GetFunction("RestartPlayer")) { alignas(16) uint8_t Buf[64]{}; *(UObject**)Buf = PC; GM->ProcessEvent(F, Buf); Log("SyndicateStudio: RestartPlayer called"); }
        char Line[300]; auto O = PC->GetOffset("Pawn");
        auto Pawn = O != uint32(-1) ? GetFromOffset<UObject*>(PC, O) : nullptr;
        snprintf(Line, sizeof(Line), "SyndicateStudio: player pawn now %p", Pawn); Log(Line);
        if (Pawn) { StwDressAt = GetTickCount64() + 2000; return; }
        // RestartPlayer spawned nothing: spawn the player's pawn ourselves and possess it.
        const UClass* PawnClass = nullptr;
        if (auto F = GM->GetFunction("GetDefaultPawnClassForController")) { alignas(16) uint8_t Buf[64]{}; *(UObject**)Buf = PC; GM->ProcessEvent(F, Buf); PawnClass = *(const UClass**)(Buf + 8); }
        snprintf(Line, sizeof(Line), "SyndicateStudio: default pawn class %s", PawnClass ? std::string(PawnClass->Name.ToString().c_str()).c_str() : "none"); Log(Line);
        if (!PawnClass || std::string(PawnClass->Name.ToString().c_str()) == "DefaultPawn") {
            PawnClass = nullptr;
            static const UClass* ClassClass = FindClass("Class");
            const int32 Count = TUObjectArray::Num();
            for (int32 i = 0, Count = TUObjectArray::Num(); i < Count; i++) {
                auto Obj = (UObject*)TUObjectArray::GetObjectByIndex(i);
                if (!Obj || !ClassClass || Obj->Class != ClassClass) continue;
                std::string N(Obj->Name.ToString().c_str());
                if (N.rfind("PlayerPawn", 0) != 0 || N.find("Athena") != std::string::npos) continue;
                snprintf(Line, sizeof(Line), "SyndicateStudio: pawn class candidate %s", N.c_str()); Log(Line);
                if (!PawnClass) PawnClass = (const UClass*)Obj;
            }
        }
        if (!PawnClass) { Log("SyndicateStudio: no pawn class to spawn"); return; }
        // Where the tutorial starts the player: the map's player starts / spawn pads (logged; a spawn pad or a Fort
        // player start wins over the engine's plain PlayerStart). UE4: K2_GetActorLocation returns three floats.
        float At[3]{}, Rot[3]{}; int Best = -1;
        {
            static const UClass* ActorClass = FindClass("Actor");
            const int32 Count = TUObjectArray::Num();
            for (int32 i = 0, Count = TUObjectArray::Num(); i < Count; i++) {
                auto Obj = (UObject*)TUObjectArray::GetObjectByIndex(i);
                if (!Obj || !Obj->Class || Obj->IsDefaultObject() || !ActorClass || !Obj->IsA(ActorClass)) continue;
                std::string C(Obj->Class->Name.ToString().c_str());
                int Rank = C.find("SpawnPad") != std::string::npos ? 3 : C.find("FortPlayerStart") != std::string::npos ? 2 : C.find("PlayerStart") != std::string::npos ? 1 : 0;
                if (!Rank) continue;
                float L[3]{};
                if (auto F = Obj->GetFunction("K2_GetActorLocation")) { alignas(16) uint8_t Buf[64]{}; Obj->ProcessEvent(F, Buf); memcpy(L, Buf, sizeof(L)); }
                snprintf(Line, sizeof(Line), "SyndicateStudio: start %s (%s) at %.0f %.0f %.0f", std::string(Obj->Name.ToString().c_str()).c_str(), C.c_str(), L[0], L[1], L[2]); Log(Line);
                if (Rank > Best) {
                    Best = Rank; memcpy(At, L, sizeof(At));
                    if (auto F = Obj->GetFunction("K2_GetActorRotation")) { alignas(16) uint8_t Buf[64]{}; Obj->ProcessEvent(F, Buf); memcpy(Rot, Buf, sizeof(Rot)); }
                }
            }
        }
        FVector Loc(At[0], At[1], At[2] + 100.f);
        auto NewPawn = (UObject*)UWorld::SpawnActor(PawnClass, Loc, FRotator{}, (AActor*)PC);
        snprintf(Line, sizeof(Line), "SyndicateStudio: spawned %s at %.0f %.0f %.0f: %p", std::string(PawnClass->Name.ToString().c_str()).c_str(), At[0], At[1], At[2], NewPawn); Log(Line);
        if (!NewPawn) return;
        if (auto F = PC->GetFunction("Possess")) { alignas(16) uint8_t Buf[64]{}; *(UObject**)Buf = NewPawn; PC->ProcessEvent(F, Buf); Log("SyndicateStudio: possessed the new pawn"); }
        StwDressAt = GetTickCount64() + 2000;
        // Face the way the player start faces (UE4 FRotator: pitch, yaw, roll floats).
        if (auto F = PC->GetFunction("SetControlRotation")) { alignas(16) uint8_t Buf[64]{}; memcpy(Buf, Rot, sizeof(Rot)); PC->ProcessEvent(F, Buf); }
        else Log("SyndicateStudio: no Possess function");
    }
    // The player's look, pickaxe and weapons. The zone dresses the player as the profile's commander hero and fills
    // the quickbar from the backpack (theater0); a fresh account had neither, so the pawn was invisible and empty
    // (the backend now gives a starting kit). Log what the pawn, its player state and the controller offer for
    // heroes, character parts and inventory, and call the ones that take no parameters and only re-apply state.
    bool StwNameMatches(const std::string& N) {
        static const char* Keys[] = { "Hero", "Character", "Part", "Cosmetic", "Inventory", "Item", "Weapon", "Pickaxe", "Harvest", "Loadout", "QuickBar", "Quickbar", "Mesh", "Profile" };
        for (auto K : Keys) if (N.find(K) != std::string::npos) return true;
        return false;
    }
    void StwListMatching_Impl(const char* Label, UObject* Obj) {
        if (!Obj) { char L[160]; snprintf(L, sizeof(L), "SyndicateStudio: dress: no %s", Label); Log(L); return; }
        char Line[320];
        snprintf(Line, sizeof(Line), "SyndicateStudio: dress: %s is %s (%s)", Label, std::string(Obj->Name.ToString().c_str()).c_str(), std::string(Obj->Class->Name.ToString().c_str()).c_str()); Log(Line);
        for (auto C = (const UStruct*)Obj->Class; C; C = C->GetSuper()) {
            std::string CName(((const UField*)C)->GetName().ToString().c_str());
            if (CName == "Actor" || CName == "Object") break;
            for (const UField* P = C->GetChildren(); P; P = P->GetNext()) {
                if (!P->Class) continue;
                std::string N(P->GetName().ToString().c_str());
                if (!StwNameMatches(N)) continue;
                std::string Kind(P->Class->Name.ToString().c_str());
                bool NoParms = Kind == "Function" && !((const UStruct*)P)->GetChildren();
                snprintf(Line, sizeof(Line), "SyndicateStudio:   %s %s::%s%s", Kind.c_str(), CName.c_str(), N.c_str(), NoParms ? " ()" : ""); Log(Line);
            }
        }
    }
    void StwListMatching(const char* Label, UObject* Obj) { __try { StwListMatching_Impl(Label, Obj); } __except (EXCEPTION_EXECUTE_HANDLER) { Log("SyndicateStudio: dress list crashed (caught)"); } }
    // Calls a function by name when the object has it and it takes no parameters.
    void StwCallNoParms_Impl(UObject* Obj, const char* Name) {
        if (!Obj) return;
        auto F = Obj->GetFunction(Name);
        if (!F) return;
        char Line[200];
        if (((const UStruct*)F)->GetChildren()) { snprintf(Line, sizeof(Line), "SyndicateStudio: dress: %s takes parameters, skipped", Name); Log(Line); return; }
        alignas(16) uint8_t Buf[64]{};
        Obj->ProcessEvent(F, Buf);
        snprintf(Line, sizeof(Line), "SyndicateStudio: dress: called %s on %s", Name, std::string(Obj->Name.ToString().c_str()).c_str()); Log(Line);
    }
    void StwCallNoParms(UObject* Obj, const char* Name) { __try { StwCallNoParms_Impl(Obj, Name); } __except (EXCEPTION_EXECUTE_HANDLER) { Log("SyndicateStudio: dress call crashed (caught)"); } }
    void StwMakeMission();
    void StwGiveKit();
    void StwDress_Impl() {
        auto World = UWorld::GetWorld();
        if (!World) return;
        UObject* PC = nullptr;
        if (auto Instance = World->OwningGameInstance; Instance && Instance->LocalPlayers.Num() && Instance->LocalPlayers[0]) PC = (UObject*)Instance->LocalPlayers[0]->PlayerController;
        if (!PC) { Log("SyndicateStudio: dress: no player controller"); return; }
        UObject* Pawn = nullptr; UObject* PS = nullptr;
        if (auto O = PC->GetOffset("Pawn"); O != uint32(-1)) Pawn = GetFromOffset<UObject*>(PC, O);
        if (auto O = PC->GetOffset("PlayerState"); O != uint32(-1)) PS = GetFromOffset<UObject*>(PC, O);
        StwListMatching("player controller", PC);
        StwListMatching("pawn", Pawn);
        StwListMatching("player state", PS);
        StwDumpObject("pawn", Pawn);
        StwDumpObject("player state", PS);
        // Re-apply what the hero and the backpack give, where the game has a parameterless way to.
        for (auto N : { "OnRep_CharacterParts", "OnRep_HeroType", "OnRep_CharacterData", "OnRep_HeroData" }) { StwCallNoParms(PS, N); StwCallNoParms(Pawn, N); }
        for (auto N : { "OnCharacterPartsReinitialized", "OnRep_CustomizationLoadout", "OnRep_CharacterPartsReinitialized" }) StwCallNoParms(Pawn, N);
        for (auto N : { "ClientForceUpdateQuickbar", "OnRep_QuickBar", "OnRep_QuickBars", "ClientRefreshInventory" }) StwCallNoParms(PC, N);
        StwGiveKit();
        StwMakeMission();
    }
    // ---- The player's kit: quickbars, default abilities (jump, crouch, sprint) and items ----
    // A real zone server sets these up as the player joins; our pawn is spawned by hand, so the controller had no
    // QuickBars, the world inventory was never filled (the HUD showed 0 materials and an empty quickbar) and the
    // default ability set (GAS_DefaultPlayer: jump, crouch, sprint...) was never given. Erbium does the same for
    // Pre-season Battle Royale (FortGameMode HandleStartingNewPlayer, AbilitySystemComponent GiveAbility,
    // FortInventory GiveItem); this is that, through reflection.
    struct StwCall {
        UObject* Obj = nullptr; UFunction* F = nullptr; std::vector<uint8_t> Buf;
        StwCall(UObject* O, const char* Name) : Obj(O) {
            if (!O) return;
            F = O->GetFunction(Name);
            if (F) Buf.assign((size_t)((const UStruct*)F)->GetPropertiesSize() + 16, 0);
        }
        explicit operator bool() const { return F != nullptr; }
        // A parameter by name; when the name is not found, the given offset (the usual layout).
        uint8_t* At(const char* Param, int32 Fallback) {
            uint32 Off = 0, Size = 0;
            if (!F) return nullptr;
            if (StwField((const UStruct*)F, Param, Off, Size)) return Buf.data() + Off;
            if (Fallback < 0 || (size_t)Fallback + 8 > Buf.size()) return nullptr;
            char Line[200]; snprintf(Line, sizeof(Line), "SyndicateStudio: kit: no parameter %s, using +0x%x", Param, Fallback); Log(Line);
            return Buf.data() + Fallback;
        }
        template <typename T> void Set(const char* Param, const T& V, int32 Fallback = -1) { if (auto P = At(Param, Fallback)) memcpy(P, &V, sizeof(T)); }
        template <typename T> T Get(const char* Param, int32 Fallback = -1) { T V{}; if (auto P = At(Param, Fallback)) memcpy(&V, P, sizeof(T)); return V; }
        void Run() { if (F) Obj->ProcessEvent(F, Buf.data()); }
    };
    void StwKitLog(const char* Fmt, const char* A = "", const char* B = "") {
        char Line[400]; snprintf(Line, sizeof(Line), Fmt, A, B); Log(Line);
    }
    std::string StwName(const UObject* O) { return O ? std::string(O->Name.ToString().c_str()) : std::string("null"); }
    UObject* StwPtr(UObject* Obj, const char* Name) {
        if (!Obj) return nullptr;
        auto O = Obj->GetOffset(Name);
        return O == uint32(-1) ? nullptr : GetFromOffset<UObject*>(Obj, O);
    }
    // An item definition: loaded already (the profile's items load theirs) or one of the given paths.
    UObject* StwItemDef(const char* Name, std::initializer_list<const wchar_t*> Paths) {
        const int32 Count = TUObjectArray::Num();
        for (int32 i = 0; i < Count; i++) {
            auto Obj = (UObject*)TUObjectArray::GetObjectByIndex(i);
            if (!Obj || !Obj->Class || Obj->IsDefaultObject()) continue;
            if (_stricmp(Obj->Name.ToString().c_str(), Name)) continue;
            if (std::string(Obj->Class->Name.ToString().c_str()).find("ItemDefinition") == std::string::npos && std::string(Obj->Class->Name.ToString().c_str()).find("ItemData") == std::string::npos) continue;
            return Obj;
        }
        for (auto P : Paths) if (auto Obj = (UObject*)SDK::FindObject(P, FindClass("Object"))) return Obj;
        return nullptr;
    }
    // Names of loaded item definitions starting with a prefix, for the log.
    void StwLogItemDefs(const char* Prefix) {
        const int32 Count = TUObjectArray::Num(); int Shown = 0;
        const size_t Len = strlen(Prefix);
        for (int32 i = 0; i < Count && Shown < 30; i++) {
            auto Obj = (UObject*)TUObjectArray::GetObjectByIndex(i);
            if (!Obj || !Obj->Class || Obj->IsDefaultObject()) continue;
            std::string N(Obj->Name.ToString().c_str());
            if (_strnicmp(N.c_str(), Prefix, Len)) continue;
            StwKitLog("SyndicateStudio: kit:   loaded %s (%s)", N.c_str(), std::string(Obj->Class->Name.ToString().c_str()).c_str());
            Shown++;
        }
    }
    void StwGiveAbilities(UObject* PS) {
        auto ASC = StwPtr(PS, "AbilitySystemComponent");
        if (!ASC) { Log("SyndicateStudio: kit: no ability system component"); return; }
        auto SpecStruct = (const UStruct*)StwFindByName("GameplayAbilitySpec", "ScriptStruct");
        auto ContStruct = (const UStruct*)StwFindByName("GameplayAbilitySpecContainer", "ScriptStruct");
        uint32 ItemsOff = 0, AbilityOff = 0, Sz = 0;
        auto ActOff = ASC->GetOffset("ActivatableAbilities");
        bool HasJump = false;
        if (SpecStruct && ContStruct && ActOff != uint32(-1) && StwField(ContStruct, "Items", ItemsOff, Sz) && StwField(SpecStruct, "Ability", AbilityOff, Sz)) {
            auto& Items = GetFromOffset<TArray<uint8>>(ASC, ActOff + ItemsOff);
            const int32 SpecSize = SpecStruct->GetPropertiesSize();
            char Line[200]; snprintf(Line, sizeof(Line), "SyndicateStudio: kit: the player has %d abilities", Items.Num()); Log(Line);
            for (int32 i = 0; i < Items.Num(); i++) {
                auto A = *(UObject**)(Items.GetData() + (size_t)i * SpecSize + AbilityOff);
                std::string N = A ? std::string(A->Class->Name.ToString().c_str()) : "null";
                if (N.find("Jump") != std::string::npos) HasJump = true;
                StwKitLog("SyndicateStudio: kit:   ability %s", N.c_str());
            }
        }
        if (HasJump) { Log("SyndicateStudio: kit: the default abilities are there already"); return; }
        auto Set = (UObject*)SDK::FindObject(L"/Game/Abilities/Player/Generic/Traits/DefaultPlayer/GAS_DefaultPlayer.GAS_DefaultPlayer", FindClass("Object"));
        if (!Set) { Log("SyndicateStudio: kit: GAS_DefaultPlayer not found"); return; }
        static uint64_t GiveAbility = Memcury::Scanner::FindPattern("48 89 5C 24 ? 56 57 41 56 48 83 EC 20 83 B9").Get();
        if (!GiveAbility || !SpecStruct) { Log("SyndicateStudio: kit: GiveAbility not found"); return; }
        auto ListOff = Set->GetOffset("GameplayAbilities");
        if (ListOff == uint32(-1)) { Log("SyndicateStudio: kit: GAS_DefaultPlayer has no GameplayAbilities"); return; }
        auto& List = GetFromOffset<TArray<UClass*>>(Set, ListOff);
        uint32 HandleOff = 0, LevelOff = 0, InputOff = 0, SourceOff = 0, RepId = 0, RepKey = 0, RecentKey = 0;
        StwField(SpecStruct, "Handle", HandleOff, Sz); StwField(SpecStruct, "Level", LevelOff, Sz); StwField(SpecStruct, "InputID", InputOff, Sz);
        StwField(SpecStruct, "SourceObject", SourceOff, Sz); bool HasRep = StwField(SpecStruct, "ReplicationID", RepId, Sz);
        StwField(SpecStruct, "ReplicationKey", RepKey, Sz); StwField(SpecStruct, "MostRecentArrayReplicationKey", RecentKey, Sz);
        int Given = 0;
        for (int32 i = 0; i < List.Num(); i++) {
            auto Cls = List[i];
            if (!Cls) continue;
            auto CDO = Cls->GetDefaultObj();
            if (!CDO) continue;
            std::vector<uint8_t> Spec((size_t)SpecStruct->GetPropertiesSize() + 64, 0);
            if (HasRep) { *(int32*)(Spec.data() + RepId) = -1; *(int32*)(Spec.data() + RepKey) = -1; *(int32*)(Spec.data() + RecentKey) = -1; }
            *(int32*)(Spec.data() + HandleOff) = 0x5000 + rand() % 0x7000000;
            *(UObject**)(Spec.data() + AbilityOff) = CDO;
            *(int32*)(Spec.data() + LevelOff) = 1;
            *(int32*)(Spec.data() + InputOff) = -1;
            *(UObject**)(Spec.data() + SourceOff) = nullptr;
            int32 Out[4]{};
            ((void* (*)(UObject*, void*, void*))GiveAbility)(ASC, Out, Spec.data());
            Given++;
            StwKitLog("SyndicateStudio: kit: gave ability %s", std::string(Cls->Name.ToString().c_str()).c_str());
        }
        char Line[120]; snprintf(Line, sizeof(Line), "SyndicateStudio: kit: gave %d default abilities", Given); Log(Line);
    }
    // Puts one item into the controller's world inventory and, when Bar >= 0, onto that quickbar (0 primary,
    // 1 secondary). Returns the item's guid (all zero when it failed).
    struct StwGuid { uint32 A, B, C, D; bool Zero() const { return !A && !B && !C && !D; } };
    StwGuid StwGiveItem(UObject* PC, UObject* Inv, UObject* Bars, UObject* Def, int Count, int Bar) {
        StwGuid Guid{};
        if (!Def || !Inv) return Guid;
        StwCall Create(Def, "CreateTemporaryItemInstanceBP");
        if (!Create) { StwKitLog("SyndicateStudio: kit: %s cannot make items", StwName(Def).c_str()); return Guid; }
        Create.Set<int32>("Count", Count, 0); Create.Set<int32>("Level", 1, 4);
        Create.Run();
        auto Item = Create.Get<UObject*>("ReturnValue", 8);
        if (!Item) { StwKitLog("SyndicateStudio: kit: no item made for %s", StwName(Def).c_str()); return Guid; }
        StwCall Own(Item, "SetOwningControllerForTemporaryItem");
        if (Own) { Own.Set<UObject*>("Controller", PC, 0); Own.Run(); }
        auto EntryStruct = (const UStruct*)StwFindByName("FortItemEntry", "ScriptStruct");
        auto ListStruct = (const UStruct*)StwFindByName("FortItemList", "ScriptStruct");
        auto EntryOff = Item->GetOffset("ItemEntry"), InvOff = Inv->GetOffset("Inventory");
        uint32 GuidOff = 0, CopyOff = 0, RepOff = 0, InstOff = 0, Sz = 0;
        if (!EntryStruct || !ListStruct || EntryOff == uint32(-1) || InvOff == uint32(-1) || !StwField(EntryStruct, "ItemGuid", GuidOff, Sz)
            || !StwField(ListStruct, "ReplicatedEntries", RepOff, Sz) || !StwField(ListStruct, "ItemInstances", InstOff, Sz)) {
            Log("SyndicateStudio: kit: item layout not found"); return Guid;
        }
        auto Entry = (uint8_t*)Item + EntryOff;
        auto& G = *(StwGuid*)(Entry + GuidOff);
        // Always a fresh guid: temporary items of different definitions came back with the same one, so selecting
        // any build piece equipped the roof (the first entry with that guid).
        CoCreateGuid((GUID*)&G);
        Guid = G;
        const int32 EntrySize = EntryStruct->GetPropertiesSize();
        auto& Entries = GetFromOffset<TArray<uint8>>(Inv, InvOff + RepOff);
        auto& Copy = Entries.Add(*Entry, EntrySize);
        if (auto P = EntryStruct->GetProperty("bIsReplicatedCopy")) {
            uint32 BO = GetFromOffset<uint32>(P, Offsets::Offset_Internal); uint8_t M = P->GetFieldMask(); if (!M) M = 0xFF;
            (&Copy)[BO] |= M;
        }
        GetFromOffset<TArray<UObject*>>(Inv, InvOff + InstOff).Add(Item);
        StwSetFlag(Inv, "bRequiresLocalUpdate");
        if (Bars && Bar >= 0) {
            StwCall Add(Bars, "ServerAddItemInternal");
            if (Add) { Add.Set<StwGuid>("Item", Guid, 0); Add.Set<uint8_t>("Quickbar", (uint8_t)Bar, 0x10); Add.Set<int32>("Slot", -1, 0x14); Add.Run(); }
            else Log("SyndicateStudio: kit: no ServerAddItemInternal");
        }
        char Line[200]; snprintf(Line, sizeof(Line), "SyndicateStudio: kit: gave %s x%d", StwName(Def).c_str(), Count); Log(Line);
        return Guid;
    }
    // Selecting a quickbar slot sends ServerExecuteInventoryItem(ItemGuid). The game's own version only marks the
    // item; a server has to put the weapon in the pawn's hands (Erbium does it in its hook of the same function).
    // Without this the slot changed but the pickaxe and the pistol never appeared.
    // Logs every property of a class or struct (name, type, offset), and the fields of struct members one level down.
    void StwDumpLayout_Impl(const char* Label, const UStruct* S, int Depth) {
        char Line[400];
        for (auto C = S; C; C = C->GetSuper()) {
            std::string CName(((const UField*)C)->GetName().ToString().c_str());
            if (CName == "Actor" || CName == "Object" || CName == "Info" || CName == "GameStateBase" || CName == "GameState") break;
            for (const UField* P = C->GetChildren(); P; P = P->GetNext()) {
                if (!P->Class) continue;
                std::string Type(P->Class->Name.ToString().c_str());
                if (Type == "Function") continue;
                uint32 Off = GetFromOffset<uint32>(P, Offsets::Offset_Internal);
                bool HasInner = Type == "StructProperty" || Type == "ArrayProperty" || Type == "ObjectProperty" || Type == "ClassProperty" || Type == "AssetObjectProperty" || Type == "AssetClassProperty" || Type == "WeakObjectProperty";
                auto Inner = HasInner ? GetFromOffset<const UObject*>(P, 0x70) : nullptr;
                if (Type == "StructProperty" || Type == "ArrayProperty") HasInner = true;
                snprintf(Line, sizeof(Line), "SyndicateStudio: layout %s:%*s %s::%s +0x%x %s size 0x%x dim %d (%s)", Label, Depth * 2, "", CName.c_str(), std::string(P->GetName().ToString().c_str()).c_str(),
                    Off, Type.c_str(), GetFromOffset<uint32>(P, Offsets::ElementSize), GetFromOffset<int32>(P, Offsets::ElementSize - 4), Inner ? StwName(Inner).c_str() : "-"); Log(Line);
                if (Depth < 2 && Type == "StructProperty" && Inner) StwDumpLayout_Impl(Label, (const UStruct*)Inner, Depth + 1);
                if (Depth < 2 && Type == "ArrayProperty" && Inner && Inner->Class && std::string(Inner->Class->Name.ToString().c_str()) == "StructProperty")
                    if (auto IS = GetFromOffset<const UObject*>(Inner, 0x70)) StwDumpLayout_Impl(Label, (const UStruct*)IS, Depth + 1);
            }
            if (Depth > 0) break;
        }
    }
    void StwDumpLayout(const char* Label, const UStruct* S, int Depth) { __try { StwDumpLayout_Impl(Label, S, Depth); } __except (EXCEPTION_EXECUTE_HANDLER) { Log("SyndicateStudio: layout dump crashed (caught)"); } }
    // Picking a piece in build mode is client side here: the controller looks the piece's class up in the game
    // state's PlayerBuildableClasses (one container per EFortBuildingType, each holding the classes of that type),
    // which a zone server fills and which is empty, so the pick changed nothing. Fill it with the wood pieces:
    // Wall (0), Floor (1), Stairs (5) and Roof (6). Works with a fixed array of containers or a TArray of them;
    // the container's first array field holds the classes.
    const UField* StwProp(const UStruct* S, const char* Name) {
        for (auto C = S; C; C = C->GetSuper())
            for (const UField* P = C->GetChildren(); P; P = P->GetNext())
                if (std::string(P->GetName().ToString().c_str()) == Name) return P;
        return nullptr;
    }
    void StwFillBuildableClasses_Impl(UObject* GS) {
        char Line[300];
        // Four containers, one per resource (wood, stone, metal, ...), each listing that material's pieces.
        static const char* Mats[3][2] = { { "Wood", "W1" }, { "Stone", "S1" }, { "Metal", "M1" } };
        static const char* Pieces[4] = { "Solid", "Floor", "StairW", "RoofC" };
        UClass* Classes[3][4]{};
        for (int m = 0; m < 3; m++) for (int i = 0; i < 4; i++) {
            char Path[200]; snprintf(Path, sizeof(Path), "/Game/Building/ActorBlueprints/Player/%s/L1/PBWA_%s_%s.PBWA_%s_%s_C", Mats[m][0], Mats[m][1], Pieces[i], Mats[m][1], Pieces[i]);
            std::string P(Path); std::wstring W(P.begin(), P.end());
            Classes[m][i] = (UClass*)SDK::FindObject(W.c_str(), FindClass("Class"));
        }
        auto Prop = StwProp((const UStruct*)GS->Class, "PlayerBuildableClasses");
        if (!Prop || !Prop->Class) { Log("SyndicateStudio: build: the game state has no PlayerBuildableClasses"); return; }
        std::string Type(Prop->Class->Name.ToString().c_str());
        uint32 Off = GetFromOffset<uint32>(Prop, Offsets::Offset_Internal);
        uint32 ElemSize = GetFromOffset<uint32>(Prop, Offsets::ElementSize);
        int32 Dim = GetFromOffset<int32>(Prop, Offsets::ElementSize - 4);
        const UStruct* Container = nullptr;
        uint8_t* Elems = nullptr;
        int32 Count = 0;
        if (Type == "StructProperty") { Container = GetFromOffset<const UStruct*>(Prop, 0x70); Elems = (uint8_t*)GS + Off; Count = Dim; }
        else if (Type == "ArrayProperty") {
            auto Inner = GetFromOffset<const UField*>(Prop, 0x70);
            if (Inner && Inner->Class && std::string(Inner->Class->Name.ToString().c_str()) == "StructProperty") {
                Container = GetFromOffset<const UStruct*>(Inner, 0x70);
                ElemSize = GetFromOffset<uint32>(Inner, Offsets::ElementSize);
                auto& Arr = *(TArray<uint8>*)((uint8_t*)GS + Off);
                if (Arr.Num() < 13) {
                    auto Data = FMemory::Realloc<uint8_t>(Arr.GetData(), (uint64)ElemSize * 13, 8);
                    memset(Data + (size_t)Arr.Num() * ElemSize, 0, (size_t)(13 - Arr.Num()) * ElemSize);
                    *(uint8_t**)((uint8_t*)GS + Off) = Data; *(int32*)((uint8_t*)GS + Off + 8) = 13; *(int32*)((uint8_t*)GS + Off + 12) = 13;
                }
                Elems = *(uint8_t**)((uint8_t*)GS + Off); Count = 13;
            }
        }
        snprintf(Line, sizeof(Line), "SyndicateStudio: build: PlayerBuildableClasses +0x%x %s, container %s size 0x%x, %d slots", Off, Type.c_str(), StwName((const UObject*)Container).c_str(), ElemSize, Count); Log(Line);
        if (!Container || !Elems || Count < 4) return;
        uint32 ListOff = uint32(-1);
        for (const UField* P = Container->GetChildren(); P; P = P->GetNext())
            if (P->Class && std::string(P->Class->Name.ToString().c_str()) == "ArrayProperty") { ListOff = GetFromOffset<uint32>(P, Offsets::Offset_Internal); break; }
        if (ListOff == uint32(-1)) { Log("SyndicateStudio: build: the container has no class list"); return; }
        for (int m = 0; m < 3 && m < Count; m++) {
            auto& List = *(TArray<UClass*>*)(Elems + (size_t)m * ElemSize + ListOff);
            for (int i = 0; i < 4; i++) {
                if (!Classes[m][i]) continue;
                bool Have = false;
                for (int32 k = 0; k < List.Num(); k++) if (List[k] == Classes[m][i]) Have = true;
                if (!Have) List.Add(Classes[m][i]);
            }
            snprintf(Line, sizeof(Line), "SyndicateStudio: build: %s container now has %d classes", Mats[m][0], List.Num()); Log(Line);
        }
    }
    void StwFillBuildableClasses(UObject* GS) { __try { StwFillBuildableClasses_Impl(GS); } __except (EXCEPTION_EXECUTE_HANDLER) { Log("SyndicateStudio: build: filling PlayerBuildableClasses crashed (caught)"); } }
    bool StwLooksLikeObject(UObject* O);
    // Writing CurrentBuildableClass by hand left the build preview on the old piece (roof picked, stairs shown) until
    // a right-click refreshed it. The controller's own setter (exe+0x815410: PC, class, reset rotation, reset
    // mirror) stores the class, sets PreviousBuildableClass and rebuilds the preview marker; use it on 1.7.2 and
    // fall back to the plain write on any other exe.
    void StwApplyBuildableClass(UObject* PC, uint32 PCOff, const UClass* Cls) {
        auto Base = (uint8_t*)GetModuleHandleW(nullptr);
        static const uint8_t Expect[] = { 0x48, 0x8D, 0xB3, 0x00, 0x1B, 0x00, 0x00 }; // lea rsi,[rbx+0x1b00] at +0x50
        if (PCOff == 0x1b00 && !memcmp(Base + 0x815410 + 0x50, Expect, sizeof(Expect))) {
            auto Set = (void(*)(UObject*, const UClass*, bool, bool))(Base + 0x815410);
            Set(PC, Cls, false, false);
            return;
        }
        if (auto O = PC->GetOffset("PreviousBuildableClass"); O != uint32(-1)) GetFromOffset<const UClass*>(PC, O) = GetFromOffset<const UClass*>(PC, PCOff);
        GetFromOffset<const UClass*>(PC, PCOff) = Cls;
    }
    void StwSetBuildableClass(UObject* PC, UObject* Def) {
        char Line[400];
        auto Off = Def->GetOffset("BlueprintClass");
        auto PCOff = PC->GetOffset("CurrentBuildableClass");
        if (PCOff == uint32(-1)) { Log("SyndicateStudio: build: the controller has no CurrentBuildableClass"); return; }
        if (Off == uint32(-1)) {
            // This build's piece items carry no BlueprintClass: the controller takes the class from the game
            // state's PlayerBuildableClasses, which a zone server filled and which is empty here. Log both layouts
            // once, then use the wood piece of the item's type.
            static bool Dumped = false;
            if (!Dumped) {
                Dumped = true;
                StwDumpLayout("building item", (const UStruct*)Def->Class, 0);
                if (auto GS = (UObject*)UWorld::GetWorld()->GameState) { StwDumpLayout("game state", (const UStruct*)GS->Class, 0); StwFillBuildableClasses(GS); }
            }
            // Keep the material the player picked (right-click cycles it): take it from the piece in use, if any.
            static int Material = 0;
            if (auto Cur = GetFromOffset<const UClass*>(PC, PCOff)) if (auto CDO = (UObject*)Cur->GetDefaultObj())
                if (auto RO = CDO->GetOffset("ResourceType"); RO != uint32(-1) && GetFromOffset<uint8_t>(CDO, RO) < 3) Material = GetFromOffset<uint8_t>(CDO, RO);
            static const char* Mats[3][2] = { { "Wood", "W1" }, { "Stone", "S1" }, { "Metal", "M1" } };
            static const char* Pieces[][2] = { { "Wall", "Solid" }, { "Floor", "Floor" }, { "Stair", "StairW" }, { "Roof", "RoofC" } };
            std::string DefName = StwName(Def);
            const UClass* Cls = nullptr;
            for (auto& P : Pieces) if (DefName.find(P[0]) != std::string::npos) {
                char Path[200]; snprintf(Path, sizeof(Path), "/Game/Building/ActorBlueprints/Player/%s/L1/PBWA_%s_%s.PBWA_%s_%s_C", Mats[Material][0], Mats[Material][1], P[1], Mats[Material][1], P[1]);
                std::string PathS(Path); std::wstring W(PathS.begin(), PathS.end());
                Cls = (const UClass*)SDK::FindObject(W.c_str(), FindClass("Class"));
                snprintf(Line, sizeof(Line), "SyndicateStudio: build: %s -> %s (%s)", DefName.c_str(), Path, StwName(Cls).c_str()); Log(Line);
                break;
            }
            if (!Cls) return;
            StwApplyBuildableClass(PC, PCOff, Cls);
            return;
        }
        auto Raw = (uint8_t*)Def + Off;
        const UClass* Cls = nullptr;
        auto First = *(UObject**)Raw;
        if (StwLooksLikeObject(First)) Cls = (const UClass*)First;   // hard reference, or the soft reference's loaded object
        if (!Cls) {
            // TAssetSubclassOf: weak pointer (8), tag (4), then the path FString at +0x10.
            auto& Path = *(FString*)(Raw + 0x10);
            if (Path.Num() > 1 && Path.Num() < 512) {
                std::string P(Path.ToString().c_str()); std::wstring W(P.begin(), P.end());
                Cls = (const UClass*)SDK::FindObject(W.c_str(), FindClass("Class"));
                if (!Cls) Cls = (const UClass*)SDK::FindObject(W.c_str(), FindClass("Object"));
                snprintf(Line, sizeof(Line), "SyndicateStudio: build: %s blueprint %s -> %s", StwName(Def).c_str(), P.c_str(), StwName(Cls).c_str()); Log(Line);
            }
        }
        if (!Cls) { snprintf(Line, sizeof(Line), "SyndicateStudio: build: no blueprint class for %s", StwName(Def).c_str()); Log(Line); return; }
        StwApplyBuildableClass(PC, PCOff, Cls);
        snprintf(Line, sizeof(Line), "SyndicateStudio: build: CurrentBuildableClass = %s", StwName(Cls).c_str()); Log(Line);
    }
    // Reloading: the weapon takes the rounds out through its owner's inventory interface (RemoveInventoryItem,
    // vtable slot 0xa0, reached through exe+0x904210 from the weapon), which this build leaves to the server: it
    // returned false ("Failed to remove item ... during reload"), so the ammo never went down. Point that slot
    // at our own removal: lower the entry's count by Count.
    UObject* StwLocalPC();
    void StwAddResource(UObject* PC, UObject* Def, int Count);
    bool StwRemoveItem_Impl(const StwGuid& Guid, int Count) {
        auto PC = StwLocalPC();
        auto Inv = StwPtr(PC, "WorldInventory");
        auto EntryStruct = (const UStruct*)StwFindByName("FortItemEntry", "ScriptStruct");
        auto ListStruct = (const UStruct*)StwFindByName("FortItemList", "ScriptStruct");
        uint32 GuidOff = 0, DefOff = 0, RepOff = 0, Sz = 0;
        auto InvOff = Inv ? Inv->GetOffset("Inventory") : uint32(-1);
        if (!EntryStruct || !ListStruct || InvOff == uint32(-1) || !StwField(EntryStruct, "ItemGuid", GuidOff, Sz) || !StwField(EntryStruct, "ItemDefinition", DefOff, Sz)
            || !StwField(ListStruct, "ReplicatedEntries", RepOff, Sz)) return false;
        auto& Entries = GetFromOffset<TArray<uint8>>(Inv, InvOff + RepOff);
        const int32 EntrySize = EntryStruct->GetPropertiesSize();
        for (int32 i = 0; i < Entries.Num(); i++) {
            auto E = Entries.GetData() + (size_t)i * EntrySize;
            if (memcmp(E + GuidOff, &Guid, sizeof(StwGuid))) continue;
            StwAddResource(PC, *(UObject**)(E + DefOff), -Count);
            return true;
        }
        return false;
    }
    bool StwRemoveItemDetour(void* This, const StwGuid* Guid, int Count, bool bForce) {
        __try { return Guid && Count > 0 ? StwRemoveItem_Impl(*Guid, Count) : true; } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    }
    void StwHookRemoveItem(UObject* Weapon) {
        static bool Done = false;
        if (Done || !Weapon) return;
        auto Base = (uint8_t*)GetModuleHandleW(nullptr);
        auto Call = Base + 0x9239d3;
        int32_t Rel; memcpy(&Rel, Call + 1, 4);
        if (Call[0] != 0xE8 || Call + 5 + Rel != Base + 0x904210) { Log("SyndicateStudio: kit: not the 1.7.2 exe, reload removal left alone"); Done = true; return; }
        auto GetOwner = (void*(*)(UObject*))(Base + 0x904210);
        auto Itf = GetOwner(Weapon);
        if (!Itf) return; // not owned yet; try again on the next equip
        Done = true;
        auto Slot = (void**)(*(uint8_t**)Itf + 0xa0);
        DWORD Old;
        if (!VirtualProtect(Slot, 8, PAGE_EXECUTE_READWRITE, &Old)) return;
        *Slot = (void*)&StwRemoveItemDetour;
        VirtualProtect(Slot, 8, Old, &Old);
        Log("SyndicateStudio: kit: reloading now takes the ammo out of the inventory");
    }
    // Traps: the trap tool shows nothing to place (no preview on the tutorial's highlighted tiles). The trap item
    // names its trap actor by a soft class reference that nothing loaded in this client: load it before the tool
    // is equipped, and log the item, the tool and the controller's deco helper once to see what else is missing.
    UObject* StwPrepareTrap(UObject* PC, UObject* Def) {
        char Line[400];
        auto Off = Def->GetOffset("BlueprintClass");
        if (Off != uint32(-1)) {
            auto Raw = (uint8_t*)Def + Off;
            auto First = *(UObject**)Raw;
            auto& Path = *(FString*)(Raw + 0x10);
            std::string P = Path.Num() > 1 && Path.Num() < 512 ? std::string(Path.ToString().c_str()) : std::string();
            UObject* Cls = StwLooksLikeObject(First) ? First : nullptr;
            if (!Cls && !P.empty()) {
                std::wstring W(P.begin(), P.end());
                Cls = (UObject*)SDK::FindObject(W.c_str(), FindClass("Class"));
                if (!Cls) Cls = (UObject*)SDK::FindObject(W.c_str(), FindClass("Object"));
            }
            snprintf(Line, sizeof(Line), "SyndicateStudio: trap: %s blueprint %s -> %s", StwName(Def).c_str(), P.empty() ? "(hard reference)" : P.c_str(), StwName(Cls).c_str()); Log(Line);
            static bool Dumped = false;
            if (!Dumped) { Dumped = true; StwDumpObject("trap item", Def); }
            return Cls;
        }
        snprintf(Line, sizeof(Line), "SyndicateStudio: trap: %s has no BlueprintClass", StwName(Def).c_str()); Log(Line);
        return nullptr;
    }
    void StwDumpMissionTraps();
    void StwTrapEquipped(UObject* PC, UObject* Weapon) {
        static bool Dumped = false;
        if (Dumped) return;
        Dumped = true;
        StwDumpObject("trap tool", Weapon);
        Log("SyndicateStudio: trap: tool functions"); StwListFunctions(Weapon);
        StwDumpObject("trap deco helper", StwPtr(PC, "TrapPickerDecoHelper"));
        StwDumpMissionTraps();
    }
    void StwEquip_Impl(UObject* PC, const StwGuid& Guid) {
        auto Pawn = StwPtr(PC, "Pawn"), Inv = StwPtr(PC, "WorldInventory");
        if (!Pawn || !Inv) return;
        auto EntryStruct = (const UStruct*)StwFindByName("FortItemEntry", "ScriptStruct");
        auto ListStruct = (const UStruct*)StwFindByName("FortItemList", "ScriptStruct");
        auto InvOff = Inv->GetOffset("Inventory");
        uint32 GuidOff = 0, DefOff = 0, RepOff = 0, Sz = 0;
        if (!EntryStruct || !ListStruct || InvOff == uint32(-1) || !StwField(EntryStruct, "ItemGuid", GuidOff, Sz)
            || !StwField(EntryStruct, "ItemDefinition", DefOff, Sz) || !StwField(ListStruct, "ReplicatedEntries", RepOff, Sz)) return;
        auto& Entries = GetFromOffset<TArray<uint8>>(Inv, InvOff + RepOff);
        const int32 EntrySize = EntryStruct->GetPropertiesSize();
        UObject* Def = nullptr;
        for (int32 i = 0; i < Entries.Num(); i++) {
            auto E = Entries.GetData() + (size_t)i * EntrySize;
            if (!memcmp(E + GuidOff, &Guid, sizeof(StwGuid))) { Def = *(UObject**)(E + DefOff); break; }
        }
        if (!Def) return;
        // Only weapons (pickaxe, guns, build pieces, the edit tool) go in the hands.
        std::string Cls(Def->Class->Name.ToString().c_str());
        // Traps (FortTrapItemDefinition / deco items) go in the hands too: they become the trap placing tool, whose
        // ServerSpawnDeco places them (the game's own in this build).
        if (Cls.find("Weapon") == std::string::npos && Cls.find("Building") == std::string::npos && Cls.find("EditTool") == std::string::npos
            && Cls.find("Trap") == std::string::npos && Cls.find("Deco") == std::string::npos) {
            StwKitLog("SyndicateStudio: kit: %s (%s) does not go in the hands", StwName(Def).c_str(), Cls.c_str());
            return;
        }
        // A build piece: the preview follows the controller's CurrentBuildableClass, and the building tool reads it
        // when it is equipped, so set it first (right-click, which re-reads it, was the only thing that switched).
        if (Cls.find("Building") != std::string::npos) StwSetBuildableClass(PC, Def);
        bool IsTrap = Cls.find("Trap") != std::string::npos || Cls.find("Deco") != std::string::npos;
        if (IsTrap) StwPrepareTrap(PC, Def);
        StwCall Equip(Pawn, "EquipWeaponDefinition");
        if (!Equip) { Log("SyndicateStudio: kit: no EquipWeaponDefinition"); return; }
        Equip.Set<UObject*>("WeaponData", Def, 0);
        Equip.Set<StwGuid>("ItemEntryGuid", Guid, 8);
        Equip.Run();
        auto Weapon = Equip.Get<UObject*>("ReturnValue", 0x18);
        if (Weapon) StwHookRemoveItem(Weapon);
        if (Weapon && IsTrap) StwTrapEquipped(PC, Weapon);
        // The trap tool (FortDecoTool) previews and places the item in its ItemDefinition, which a zone server
        // sets when the tool is equipped; here it stayed null, so no trap preview showed and no tile lit up (the
        // build preview's red ghost was a wrong earlier attempt). Set it and run its OnRep.
        if (IsTrap && Weapon) {
            if (auto IO = Weapon->GetOffset("ItemDefinition"); IO != uint32(-1)) {
                GetFromOffset<UObject*>(Weapon, IO) = Def;
                StwCallNoParms(Weapon, "OnRep_ItemDefinition");
                StwKitLog("SyndicateStudio: trap: %s now places %s", StwName(Weapon).c_str(), StwName(Def).c_str());
            }
            // Clear the build piece so its preview ghost goes away. Through the controller's setter with both
            // reset flags set: that path is safe with no class (without them it reads the class unchecked).
            auto Base = (uint8_t*)GetModuleHandleW(nullptr);
            static const uint8_t Expect[] = { 0x48, 0x8D, 0xB3, 0x00, 0x1B, 0x00, 0x00 };
            if (PC->GetOffset("CurrentBuildableClass") == 0x1b00 && !memcmp(Base + 0x815410 + 0x50, Expect, sizeof(Expect)) && StwPtr(PC, "CurrentBuildableClass"))
                ((void(*)(UObject*, const UClass*, bool, bool))(Base + 0x815410))(PC, nullptr, true, true);
        }
        // Equipping the tool cleared CurrentBuildableClass again about half the time (the preview then vanished
        // until the next press): set it once more when that happened.
        if (Cls.find("Building") != std::string::npos && !StwPtr(PC, "CurrentBuildableClass")) StwSetBuildableClass(PC, Def);
        StwKitLog("SyndicateStudio: kit: equipped %s -> %s", StwName(Def).c_str(), StwName(Weapon).c_str());
        StwKitLog("SyndicateStudio: kit:   buildable class now %s", StwName(StwPtr(PC, "CurrentBuildableClass")).c_str());
    }
    void StwEquip(UObject* PC, const StwGuid& Guid) { __try { StwEquip_Impl(PC, Guid); } __except (EXCEPTION_EXECUTE_HANDLER) { Log("SyndicateStudio: equip crashed (caught)"); } }
    void* StwExecOriginal = nullptr;
    void StwExecDetour(UObject* Context, FFrame& Stack, void* Result) {
        StwGuid Guid{};
        Stack.StepCompiledIn(&Guid);
        Stack.IncrementCode();
        StwEquip(Context, Guid);
    }
    // Building: ServerCreateBuildingActor(BuildingClassData, BuildLoc, BuildRot, bMirrored). As with equipping, the
    // game leaves placing the piece to the server (Erbium's hook of the same function); this places it and takes
    // 10 of its material from the player's inventory.
    void StwBuild_Impl(UObject* PC, const UClass* Cls, const uint8_t* Loc, const uint8_t* Rot, bool Mirrored) {
        if (!PC || !Cls) return;
        auto Inv = StwPtr(PC, "WorldInventory");
        auto CDO = (UObject*)Cls->GetDefaultObj();
        int Resource = 0;
        if (CDO) if (auto O = CDO->GetOffset("ResourceType"); O != uint32(-1)) Resource = GetFromOffset<uint8_t>(CDO, O);
        static const char* Mats[] = { "WoodItemData", "StoneItemData", "MetalItemData" };
        const char* Mat = Mats[Resource < 3 ? Resource : 0];
        // Find the material and pay 10 of it.
        auto EntryStruct = (const UStruct*)StwFindByName("FortItemEntry", "ScriptStruct");
        auto ListStruct = (const UStruct*)StwFindByName("FortItemList", "ScriptStruct");
        uint32 GuidOff = 0, DefOff = 0, CountOff = 0, RepOff = 0, InstOff = 0, Sz = 0;
        auto InvOff = Inv ? Inv->GetOffset("Inventory") : uint32(-1);
        if (!EntryStruct || !ListStruct || InvOff == uint32(-1) || !StwField(EntryStruct, "ItemGuid", GuidOff, Sz) || !StwField(EntryStruct, "ItemDefinition", DefOff, Sz)
            || !StwField(EntryStruct, "Count", CountOff, Sz) || !StwField(ListStruct, "ReplicatedEntries", RepOff, Sz) || !StwField(ListStruct, "ItemInstances", InstOff, Sz)) return;
        auto& Entries = GetFromOffset<TArray<uint8>>(Inv, InvOff + RepOff);
        auto& Instances = GetFromOffset<TArray<UObject*>>(Inv, InvOff + InstOff);
        const int32 EntrySize = EntryStruct->GetPropertiesSize();
        uint8_t* Entry = nullptr;
        for (int32 i = 0; i < Entries.Num() && !Entry; i++) {
            auto E = Entries.GetData() + (size_t)i * EntrySize;
            auto D = *(UObject**)(E + DefOff);
            if (D && !_stricmp(D->Name.ToString().c_str(), Mat)) Entry = E;
        }
        if (!Entry || *(int32*)(Entry + CountOff) < 10) { StwKitLog("SyndicateStudio: build: not enough %s", Mat); return; }
        FVector At{}; FRotator Facing{};
        memcpy(&At, Loc, 12); memcpy(&Facing, Rot, 12);
        auto Building = (UObject*)UWorld::SpawnActor(Cls, At, Facing, (AActor*)PC);
        if (!Building) { Log("SyndicateStudio: build: the piece did not spawn"); return; }
        StwCall Init(Building, "InitializeKismetSpawnedBuildingActor");
        if (Init) { Init.Set<UObject*>("BuildingOwner", Building, 0); Init.Set<UObject*>("SpawningController", PC, 8); Init.Set<bool>("bUsePlayerBuildAnimations", true, 0x10); Init.Run(); }
        StwSetFlag(Building, "bPlayerPlaced");
        StwCall Mirror(Building, "SetMirrored"); if (Mirror) { Mirror.Set<bool>("bIsMirrored", Mirrored, 0); Mirror.Run(); }
        // Pay: the replicated entry and the item instance both hold the count.
        *(int32*)(Entry + CountOff) -= 10;
        StwGuid G = *(StwGuid*)(Entry + GuidOff);
        for (int32 i = 0; i < Instances.Num(); i++) {
            auto Item = Instances[i];
            if (!Item) continue;
            auto EO = Item->GetOffset("ItemEntry");
            if (EO == uint32(-1)) continue;
            auto IE = (uint8_t*)Item + EO;
            if (!memcmp(IE + GuidOff, &G, sizeof(G))) { *(int32*)(IE + CountOff) = *(int32*)(Entry + CountOff); break; }
        }
        StwSetFlag(Inv, "bRequiresLocalUpdate");
        StwCallNoParms(PC, "HandleWorldInventoryLocalUpdate");
        StwCallNoParms(Inv, "HandleInventoryLocalUpdate");
        StwKitLog("SyndicateStudio: build: placed %s", std::string(Cls->Name.ToString().c_str()).c_str());
    }
    void StwBuild(UObject* PC, const UClass* Cls, const uint8_t* Loc, const uint8_t* Rot, bool Mirrored) {
        __try { StwBuild_Impl(PC, Cls, Loc, Rot, Mirrored); } __except (EXCEPTION_EXECUTE_HANDLER) { Log("SyndicateStudio: build crashed (caught)"); }
    }
    void StwBuildDetour(UObject* Context, FFrame& Stack, void* Result) {
        alignas(16) uint8_t ClassData[0x40]{}, Loc[0x20]{}, Rot[0x20]{};
        bool Mirrored = false;
        Stack.StepCompiledIn(ClassData);
        Stack.StepCompiledIn(Loc);
        Stack.StepCompiledIn(Rot);
        Stack.StepCompiledIn(&Mirrored);
        Stack.IncrementCode();
        StwBuild(Context, *(const UClass**)ClassData, Loc, Rot, Mirrored);
    }
    // ---- Harvesting ----
    // A pickaxe hit on a world object calls the building's OnDamageServer; a zone server then adds the material
    // to the player's inventory (Erbium does it in its hook of the same function, BuildingSMActor.cpp). Locally
    // nothing did, so harvesting gave nothing. Hooked with MinHook (the game's own code runs after ours).
    using StwDamageFn = void(*)(UObject*, float, void*, void*, int64_t, UObject*, UObject*, int64_t);
    StwDamageFn StwDamageOriginal = nullptr;
    UObject* StwLocalPC() {
        auto World = UWorld::GetWorld();
        if (World) if (auto Instance = World->OwningGameInstance; Instance && Instance->LocalPlayers.Num() && Instance->LocalPlayers[0]) return (UObject*)Instance->LocalPlayers[0]->PlayerController;
        return nullptr;
    }
    // Adds Count of a resource to the player's world inventory (or gives the item when it isn't there yet).
    void StwAddResource(UObject* PC, UObject* Def, int Count) {
        auto Inv = StwPtr(PC, "WorldInventory");
        auto EntryStruct = (const UStruct*)StwFindByName("FortItemEntry", "ScriptStruct");
        auto ListStruct = (const UStruct*)StwFindByName("FortItemList", "ScriptStruct");
        uint32 GuidOff = 0, DefOff = 0, CountOff = 0, RepOff = 0, InstOff = 0, Sz = 0;
        auto InvOff = Inv ? Inv->GetOffset("Inventory") : uint32(-1);
        if (!EntryStruct || !ListStruct || InvOff == uint32(-1) || !StwField(EntryStruct, "ItemGuid", GuidOff, Sz) || !StwField(EntryStruct, "ItemDefinition", DefOff, Sz)
            || !StwField(EntryStruct, "Count", CountOff, Sz) || !StwField(ListStruct, "ReplicatedEntries", RepOff, Sz) || !StwField(ListStruct, "ItemInstances", InstOff, Sz)) return;
        auto& Entries = GetFromOffset<TArray<uint8>>(Inv, InvOff + RepOff);
        auto& Instances = GetFromOffset<TArray<UObject*>>(Inv, InvOff + InstOff);
        const int32 EntrySize = EntryStruct->GetPropertiesSize();
        uint8_t* Entry = nullptr;
        for (int32 i = 0; i < Entries.Num() && !Entry; i++) {
            auto E = Entries.GetData() + (size_t)i * EntrySize;
            if (*(UObject**)(E + DefOff) == Def) Entry = E;
        }
        if (!Entry) { StwGiveItem(PC, Inv, StwPtr(PC, "QuickBars"), Def, Count, -1); }
        else {
            int32 New = *(int32*)(Entry + CountOff) + Count;
            if (New > 999) New = 999;
            if (New < 0) New = 0;
            // Only the replicated entry changes: the inventory's local update copies it to the item instance and
            // tells the HUD (changing the instance too left nothing for it to notice, so the counter stayed put).
            *(int32*)(Entry + CountOff) = New;
            if (auto P = EntryStruct->GetProperty("bIsDirty")) {
                uint32 BO = GetFromOffset<uint32>(P, Offsets::Offset_Internal); uint8_t M = P->GetFieldMask(); if (!M) M = 0xFF;
                Entry[BO] |= M;
            }
        }
        StwSetFlag(Inv, "bRequiresLocalUpdate");
        StwCallNoParms(PC, "HandleWorldInventoryLocalUpdate");
        StwCallNoParms(Inv, "HandleInventoryLocalUpdate");
        // If the local update did not copy the count to the item instance, copy it ourselves.
        if (Entry) {
            StwGuid G = *(StwGuid*)(Entry + GuidOff);
            for (int32 i = 0; i < Instances.Num(); i++) {
                auto Item = Instances[i];
                if (!Item) continue;
                auto EO = Item->GetOffset("ItemEntry");
                if (EO == uint32(-1)) continue;
                auto IE = (uint8_t*)Item + EO;
                if (memcmp(IE + GuidOff, &G, sizeof(G))) continue;
                if (*(int32*)(IE + CountOff) != *(int32*)(Entry + CountOff)) {
                    static int Logged = 0;
                    if (Logged++ < 3) Log("SyndicateStudio: harvest:   the local update did not refresh the item, setting its count directly");
                    *(int32*)(IE + CountOff) = *(int32*)(Entry + CountOff);
                }
                break;
            }
        }
    }
    int StwHarvestLogs = 0;
    // Loot (chests, destroyed cars and the like). The search and destruction loot is rolled by the server, which this
    // build leaves out, so roll it here from the game's own loot tables: FortLootTierData rows (TierGroup, Weight,
    // LootPackage, NumLootPackageDrops) pick a package; FortLootPackageData rows with that LootPackageID (Weight,
    // LootPackageCall to another package, or an ItemDefinition and Count) give the items. Tables are the loaded
    // DataTables with those row structs; fields are found by name.
    struct StwLootField { uint32 Off = uint32(-1); };
    struct StwLootTables {
        bool Ready = false;
        std::vector<UObject*> Tier, Pkg;
        uint32 TGroup = ~0u, TWeight = ~0u, TPackage = ~0u, TDrops = ~0u, TMinWL = ~0u, TMaxWL = ~0u;
        uint32 PId = ~0u, PWeight = ~0u, PCall = ~0u, PItem = ~0u, PCount = ~0u, PMinWL = ~0u, PMaxWL = ~0u;
    } StwLoot;
    uint32 StwRowField(const UStruct* S, const char* Name) { uint32 Off = 0, Size = 0; return StwField(S, Name, Off, Size) ? Off : ~0u; }
    // The loot tables are not loaded in the client: load the ones the game data, game state and game mode point
    // at (properties with "Loot" in the name: soft references, or arrays of them), then the usual paths.
    void StwLoadLootTables_Impl() {
        char Line[400];
        auto LoadPath = [&](const std::string& P) {
            if (P.empty()) return;
            std::wstring W(P.begin(), P.end());
            auto O = (UObject*)SDK::FindObject(W.c_str(), FindClass("Object"));
            snprintf(Line, sizeof(Line), "SyndicateStudio: loot: load %s -> %s", P.c_str(), StwName(O).c_str()); Log(Line);
        };
        std::vector<UObject*> Owners;
        if (auto GD = StwFindByName("DefaultGameData", nullptr)) Owners.push_back(GD);
        if (auto W = UWorld::GetWorld()) { Owners.push_back((UObject*)W->GameState); Owners.push_back((UObject*)W->AuthorityGameMode); }
        // The tables are listed by LootTierDataTables / LootPackageDataTables: find the classes declaring them, then
        // every object of those classes (their defaults included).
        std::vector<const UStruct*> Declaring;
        const int32 Num = TUObjectArray::Num();
        for (int32 i = 0; i < Num; i++) {
            auto O = (UObject*)TUObjectArray::GetObjectByIndex(i);
            if (!O || !O->Class) continue;
            std::string C(O->Class->Name.ToString().c_str());
            if (C != "Class" && C != "BlueprintGeneratedClass") continue;
            for (const UField* P = ((const UStruct*)O)->GetChildren(); P; P = P->GetNext()) {
                std::string N(P->GetName().ToString().c_str());
                if (N == "LootTierDataTables" || N == "LootPackageDataTables") {
                    Declaring.push_back((const UStruct*)O);
                    snprintf(Line, sizeof(Line), "SyndicateStudio: loot: %s declares %s", StwName(O).c_str(), N.c_str()); Log(Line);
                    break;
                }
            }
        }
        for (int32 i = 0; i < Num && !Declaring.empty(); i++) {
            auto O = (UObject*)TUObjectArray::GetObjectByIndex(i);
            if (!O || !O->Class) continue;
            for (auto C = (const UStruct*)O->Class; C; C = C->GetSuper())
                if (std::find(Declaring.begin(), Declaring.end(), C) != Declaring.end()) { Owners.push_back(O); break; }
        }
        for (auto Owner : Owners) {
            if (!Owner) continue;
            for (auto C = (const UStruct*)Owner->Class; C; C = C->GetSuper())
                for (const UField* P = C->GetChildren(); P; P = P->GetNext()) {
                    if (!P->Class) continue;
                    std::string Name(P->GetName().ToString().c_str()), Type(P->Class->Name.ToString().c_str());
                    if (Name.find("Loot") == std::string::npos) continue;
                    uint32 Off = GetFromOffset<uint32>(P, Offsets::Offset_Internal);
                    auto At = (uint8_t*)Owner + Off;
                    snprintf(Line, sizeof(Line), "SyndicateStudio: loot: %s.%s (%s)", StwName(Owner).c_str(), Name.c_str(), Type.c_str()); Log(Line);
                    if (Type == "AssetObjectProperty") { auto& F = *(FString*)(At + 0x10); if (F.Num() > 1) LoadPath(std::string(F.ToString().c_str())); }
                    else if (Type == "ArrayProperty") {
                        auto Inner = GetFromOffset<const UField*>(P, 0x70);
                        if (!Inner || !Inner->Class || std::string(Inner->Class->Name.ToString().c_str()) != "AssetObjectProperty") continue;
                        uint32 ES = GetFromOffset<uint32>(Inner, Offsets::ElementSize);
                        auto& Arr = *(TArray<uint8>*)At;
                        for (int32 i = 0; i < Arr.Num() && i < 32; i++) { auto& F = *(FString*)(Arr.GetData() + (size_t)i * ES + 0x10); if (F.Num() > 1) LoadPath(std::string(F.ToString().c_str())); }
                    }
                }
        }
        for (auto P : { "/Game/Items/Datatables/LootTierData.LootTierData", "/Game/Items/Datatables/LootPackages.LootPackages",
                        "/Game/Items/DataTables/LootTierData.LootTierData", "/Game/Items/DataTables/LootPackages.LootPackages" })
            LoadPath(P);
    }
    void StwLoadLootTables() { __try { StwLoadLootTables_Impl(); } __except (EXCEPTION_EXECUTE_HANDLER) { Log("SyndicateStudio: loot: loading the tables crashed (caught)"); } }
    // A table still alive (garbage collection freed the first ones a minute or two in, and every roll after that
    // found no rows, then crashed): its object slot still holds it and it is still a DataTable.
    bool StwTableAlive_Impl(UObject* T) {
        return T && TUObjectArray::GetObjectByIndex(T->Index) == T && T->Class && std::string(T->Class->Name.ToString().c_str()) == "DataTable";
    }
    bool StwTableAlive(UObject* T) { __try { return StwTableAlive_Impl(T); } __except (EXCEPTION_EXECUTE_HANDLER) { return false; } }
    void StwLootInit() {
        if (StwLoot.Ready) {
            bool Alive = true;
            for (auto T : StwLoot.Tier) Alive = Alive && StwTableAlive(T);
            for (auto T : StwLoot.Pkg) Alive = Alive && StwTableAlive(T);
            if (Alive && !StwLoot.Tier.empty()) return;
            Log("SyndicateStudio: loot: the tables were freed, loading them again");
            StwLoot.Tier.clear(); StwLoot.Pkg.clear();
        }
        StwLoot.Ready = true;
        StwLoadLootTables();
        char Line[300];
        const int32 Count = TUObjectArray::Num();
        for (int32 i = 0; i < Count; i++) {
            auto O = (UObject*)TUObjectArray::GetObjectByIndex(i);
            if (!O || !O->Class || O->IsDefaultObject() || std::string(O->Class->Name.ToString().c_str()) != "DataTable") continue;
            auto RowStruct = *(const UStruct**)((uint8_t*)O + 0x28);
            if (!RowStruct) continue;
            std::string RS(((const UObject*)RowStruct)->Name.ToString().c_str());
            if (RS == "FortLootTierData") {
                StwLoot.Tier.push_back(O);
                StwLoot.TGroup = StwRowField(RowStruct, "TierGroup"); StwLoot.TWeight = StwRowField(RowStruct, "Weight");
                StwLoot.TPackage = StwRowField(RowStruct, "LootPackage"); StwLoot.TDrops = StwRowField(RowStruct, "NumLootPackageDrops");
                StwLoot.TMinWL = StwRowField(RowStruct, "MinWorldLevel"); StwLoot.TMaxWL = StwRowField(RowStruct, "MaxWorldLevel");
            } else if (RS == "FortLootPackageData") {
                StwLoot.Pkg.push_back(O);
                StwLoot.PId = StwRowField(RowStruct, "LootPackageID"); StwLoot.PWeight = StwRowField(RowStruct, "Weight");
                StwLoot.PCall = StwRowField(RowStruct, "LootPackageCall"); StwLoot.PItem = StwRowField(RowStruct, "ItemDefinition");
                StwLoot.PCount = StwRowField(RowStruct, "Count");
                StwLoot.PMinWL = StwRowField(RowStruct, "MinWorldLevel"); StwLoot.PMaxWL = StwRowField(RowStruct, "MaxWorldLevel");
            } else continue;
            O->AddToRoot(); // keep it from garbage collection
            snprintf(Line, sizeof(Line), "SyndicateStudio: loot: table %s (%s)", StwName(O).c_str(), RS.c_str()); Log(Line);
        }
        snprintf(Line, sizeof(Line), "SyndicateStudio: loot: %d tier tables, %d package tables", (int)StwLoot.Tier.size(), (int)StwLoot.Pkg.size()); Log(Line);
    }
    bool StwRowLevelOk(uint8_t* Row, uint32 MinOff, uint32 MaxOff) {
        const int WL = 1;
        if (MinOff != ~0u && *(int32*)(Row + MinOff) > WL && *(int32*)(Row + MinOff) >= 0) return false;
        if (MaxOff != ~0u && *(int32*)(Row + MaxOff) >= 0 && *(int32*)(Row + MaxOff) < WL) return false;
        return true;
    }
    template <typename F> void StwForRows(const std::vector<UObject*>& Tables, F&& Fn) {
        for (auto T : Tables) {
            auto& Map = *(TMap<FName, uint8_t*>*)((uint8_t*)T + 0x30);
            for (auto& Pair : Map) if (Pair.Value()) Fn(Pair.Value());
        }
    }
    uint8_t* StwPickRow(std::vector<std::pair<uint8_t*, float>>& Rows) {
        float Total = 0.f;
        for (auto& R : Rows) Total += R.second;
        if (Rows.empty() || Total <= 0.f) return nullptr;
        float X = (float)rand() / RAND_MAX * Total;
        for (auto& R : Rows) { X -= R.second; if (X <= 0.f) return R.first; }
        return Rows.back().first;
    }
    void StwRollPackage(const std::string& Id, int Depth, std::vector<std::pair<UObject*, int>>& Out) {
        if (Depth > 6 || StwLoot.PId == ~0u) return;
        std::vector<std::pair<uint8_t*, float>> Rows;
        StwForRows(StwLoot.Pkg, [&](uint8_t* R) {
            if (std::string(((FName*)(R + StwLoot.PId))->ToString().c_str()) != Id) return;
            float W = StwLoot.PWeight != ~0u ? *(float*)(R + StwLoot.PWeight) : 1.f;
            if (W > 0.f && StwRowLevelOk(R, StwLoot.PMinWL, StwLoot.PMaxWL)) Rows.push_back({ R, W });
        });
        auto Row = StwPickRow(Rows);
        if (!Row) return;
        if (StwLoot.PCall != ~0u) {
            auto& Call = *(FString*)(Row + StwLoot.PCall);
            if (Call.Num() > 1) { StwRollPackage(std::string(Call.ToString().c_str()), Depth + 1, Out); return; }
        }
        if (StwLoot.PItem == ~0u) return;
        // ItemDefinition is a soft reference: weak pointer (8), tag (4), then the path FString at +0x10.
        auto& Path = *(FString*)(Row + StwLoot.PItem + 0x10);
        if (Path.Num() <= 1) return;
        std::string P(Path.ToString().c_str()); std::wstring W(P.begin(), P.end());
        auto Def = (UObject*)SDK::FindObject(W.c_str(), FindClass("Object"));
        int Count = StwLoot.PCount != ~0u ? *(int32*)(Row + StwLoot.PCount) : 1;
        char Line[300]; snprintf(Line, sizeof(Line), "SyndicateStudio: loot:   %s x%d -> %s", P.c_str(), Count, StwName(Def).c_str()); Log(Line);
        if (Def && Count > 0) Out.push_back({ Def, Count });
    }
    void StwRollLoot_Impl(const std::string& Group, std::vector<std::pair<UObject*, int>>& Out) {
        StwLootInit();
        char Line[300];
        if (StwLoot.TGroup == ~0u || StwLoot.TPackage == ~0u) { Log("SyndicateStudio: loot: no tier table"); return; }
        std::vector<std::pair<uint8_t*, float>> Rows;
        StwForRows(StwLoot.Tier, [&](uint8_t* R) {
            if (std::string(((FName*)(R + StwLoot.TGroup))->ToString().c_str()) != Group) return;
            float W = StwLoot.TWeight != ~0u ? *(float*)(R + StwLoot.TWeight) : 1.f;
            if (W > 0.f && StwRowLevelOk(R, StwLoot.TMinWL, StwLoot.TMaxWL)) Rows.push_back({ R, W });
        });
        auto Tier = StwPickRow(Rows);
        snprintf(Line, sizeof(Line), "SyndicateStudio: loot: %s: %d tier rows", Group.c_str(), (int)Rows.size()); Log(Line);
        if (!Tier) return;
        std::string Package(((FName*)(Tier + StwLoot.TPackage))->ToString().c_str());
        float Drops = StwLoot.TDrops != ~0u ? *(float*)(Tier + StwLoot.TDrops) : 1.f;
        int N = (int)Drops; if ((float)rand() / RAND_MAX < Drops - (float)N) N++;
        if (N < 1) N = 1; if (N > 12) N = 12;
        snprintf(Line, sizeof(Line), "SyndicateStudio: loot: %s -> package %s, %d drops", Group.c_str(), Package.c_str(), N); Log(Line);
        for (int i = 0; i < N; i++) StwRollPackage(Package, 0, Out);
    }
    void StwRollLoot(const std::string& Group, std::vector<std::pair<UObject*, int>>& Out) {
        __try { StwRollLoot_Impl(Group, Out); } __except (EXCEPTION_EXECUTE_HANDLER) { Log("SyndicateStudio: loot: rolling crashed (caught)"); }
    }
    // Rolls a loot tier group and puts the items in the player's inventory (stacking onto what is there).
    void StwGiveLoot(UObject* PC, const std::string& Group) {
        if (!PC || Group.empty() || Group == "None") return;
        std::vector<std::pair<UObject*, int>> Items;
        StwRollLoot(Group, Items);
        for (auto& [Def, Count] : Items) StwAddResource(PC, Def, Count);
    }
    std::string StwNameProp(UObject* Actor, const char* Prop) {
        auto Off = Actor ? Actor->GetOffset(Prop) : uint32(-1);
        return Off == uint32(-1) ? std::string() : std::string(GetFromOffset<FName>(Actor, Off).ToString().c_str());
    }
    void StwHarvest_Impl(UObject* Actor, float Damage, UObject* InstigatedBy, UObject* DamageCauser) {
        auto PC = StwLocalPC();
        if (StwHarvestLogs < 8) {
            StwHarvestLogs++;
            char L[300]; snprintf(L, sizeof(L), "SyndicateStudio: harvest: %s took %.0f from %s (%s)", StwName(Actor).c_str(), Damage, StwName(InstigatedBy).c_str(), DamageCauser ? StwName(DamageCauser->Class).c_str() : "none"); Log(L);
        }
        if (!PC || InstigatedBy != PC || !Actor || !DamageCauser || Damage <= 0.f) return;
        std::string Cause(DamageCauser->Class->Name.ToString().c_str());
        if (Cause.find("Melee") == std::string::npos && Cause.find("Pickaxe") == std::string::npos) return;
        // Destroying it (this hit takes its last health) drops its destruction loot: cars give nuts and bolts, etc.
        {
            static std::vector<UObject*> Looted;
            StwCall Health(Actor, "GetHealth");
            if (Health) {
                Health.Run();
                float H = Health.Get<float>("ReturnValue", 0);
                auto Group = StwNameProp(Actor, "DestructionLootTierGroup");
                if (H > 0.f && H <= Damage && !Group.empty() && Group != "None" && std::find(Looted.begin(), Looted.end(), Actor) == Looted.end()) {
                    Looted.push_back(Actor);
                    char L[200]; snprintf(L, sizeof(L), "SyndicateStudio: loot: destroyed %s, rolling %s", StwName(Actor).c_str(), Group.c_str()); Log(L);
                    StwGiveLoot(PC, Group);
                }
            }
        }
        if (auto P = Actor->GetProperty("bPlayerPlaced")) {
            uint8_t M = P->GetFieldMask(); if (!M) M = 0xFF;
            if (GetFromOffset<uint8_t>(Actor, GetFromOffset<uint32>(P, Offsets::Offset_Internal)) & M) { if (StwHarvestLogs < 12) { StwHarvestLogs++; Log("SyndicateStudio: harvest:   player built, no resources"); } return; }
        }
        if (auto P = Actor->GetProperty("bAllowResourceDrop")) {
            uint8_t M = P->GetFieldMask(); if (!M) M = 0xFF;
            if (!(GetFromOffset<uint8_t>(Actor, GetFromOffset<uint32>(P, Offsets::Offset_Internal)) & M)) { if (StwHarvestLogs < 12) { StwHarvestLogs++; Log("SyndicateStudio: harvest:   resource drop off for this object"); } return; }
        }
        auto TypeOff = Actor->GetOffset("ResourceType");
        if (TypeOff == uint32(-1)) { if (StwHarvestLogs < 16) { StwHarvestLogs++; Log("SyndicateStudio: harvest:   no ResourceType"); } return; }
        uint8_t Type = GetFromOffset<uint8_t>(Actor, TypeOff);
        // The resource item for the object's type (EFortResourceType: wood, stone, metal).
        static const char* Names[] = { "WoodItemData", "StoneItemData", "MetalItemData" };
        static const wchar_t* Paths[] = { L"/Game/Items/ResourcePickups/WoodItemData.WoodItemData", L"/Game/Items/ResourcePickups/StoneItemData.StoneItemData", L"/Game/Items/ResourcePickups/MetalItemData.MetalItemData" };
        if (Type > 2) { if (StwHarvestLogs < 16) { StwHarvestLogs++; char L[120]; snprintf(L, sizeof(L), "SyndicateStudio: harvest:   resource type %d gives nothing", Type); Log(L); } return; }
        static UObject* Defs[3] = {};
        if (!Defs[Type]) Defs[Type] = StwItemDef(Names[Type], { Paths[Type] });
        auto Def = Defs[Type];
        if (!Def) { if (StwHarvestLogs < 16) { StwHarvestLogs++; char L[120]; snprintf(L, sizeof(L), "SyndicateStudio: harvest:   no resource item for type %d", Type); Log(L); } return; }
        float MaxHealth = 0.f;
        StwCall Max(Actor, "GetMaxHealth");
        if (Max) { Max.Run(); MaxHealth = Max.Get<float>("ReturnValue", 0); }
        if (MaxHealth <= 0.f) { if (StwHarvestLogs < 16) { StwHarvestLogs++; Log("SyndicateStudio: harvest:   no max health"); } return; }
        // How much the whole object gives: its resource curve row (BuildingResourceAmountOverride) in the
        // resource rates table.
        float Total = 0.f;
        if (auto RowOff = Actor->GetOffset("BuildingResourceAmountOverride"); RowOff != uint32(-1)) {
            auto Table = GetFromOffset<UObject*>(Actor, RowOff);
            FName Row = GetFromOffset<FName>(Actor, RowOff + 8);
            if (!Table) Table = (UObject*)SDK::FindObject(L"/Game/Balance/DataTables/ResourceRates.ResourceRates", FindClass("Object"));
            static auto Lib = FindClass("DataTableFunctionLibrary") ? (UObject*)FindClass("DataTableFunctionLibrary")->GetDefaultObj() : nullptr;
            StwCall Eval(Lib, "EvaluateCurveTableRow");
            if (Eval && Table && Row.ComparisonIndex > 0) {
                Eval.Set<UObject*>("CurveTable", Table, 0);
                Eval.Set<FName>("RowName", Row, 8);
                Eval.Set<float>("InXY", 0.f, 0x10);
                Eval.Run();
                Total = Eval.Get<float>("OutXY", 0x18);
            }
        }
        int Count = Total > 0.f ? (int)roundf(Total / (MaxHealth / Damage)) : (int)roundf(Damage / MaxHealth * 50.f);
        if (Count < 1) Count = 1;
        if (StwHarvestLogs < 16) { StwHarvestLogs++; char L[240]; snprintf(L, sizeof(L), "SyndicateStudio: harvest:   %s type %d max health %.0f total %.1f -> %d", StwName(Def).c_str(), Type, MaxHealth, Total, Count); Log(L); }
        StwAddResource(PC, Def, Count);
        StwCall Report(PC, "ClientReportDamagedResourceBuilding");
        if (Report) {
            Report.Set<UObject*>("BuildingSMActor", Actor, -1);
            Report.Set<uint8_t>("PotentialResourceType", Type, -1);
            Report.Set<int32>("PotentialResourceCount", Count, -1);
            Report.Run();
        }
    }
    void StwDamageDetour(UObject* Actor, float Damage, void* Tags, void* Momentum, int64_t Hit, UObject* InstigatedBy, UObject* DamageCauser, int64_t Context) {
        __try { StwHarvest_Impl(Actor, Damage, InstigatedBy, DamageCauser); } __except (EXCEPTION_EXECUTE_HANDLER) { Log("SyndicateStudio: harvest crashed (caught)"); }
        StwDamageOriginal(Actor, Damage, Tags, Momentum, Hit, InstigatedBy, DamageCauser, Context);
    }
    void StwHookHarvest() {
        static bool Done = false;
        if (Done) return;
        Done = true;
        auto Addr = FindFunctionCall(L"OnDamageServer", std::vector<uint8_t>{ 0x4C, 0x89, 0x4C });
        char Line[160];
        if (!Addr) { Log("SyndicateStudio: kit: OnDamageServer not found (no harvesting)"); return; }
        MH_Initialize();
        if (MH_CreateHook((void*)Addr, (void*)&StwDamageDetour, (void**)&StwDamageOriginal) != MH_OK || MH_EnableHook((void*)Addr) != MH_OK) { Log("SyndicateStudio: kit: could not hook OnDamageServer"); return; }
        snprintf(Line, sizeof(Line), "SyndicateStudio: kit: hooked OnDamageServer at exe+0x%llx (harvesting)", (unsigned long long)(Addr - (uintptr_t)GetModuleHandleW(nullptr))); Log(Line);
    }
    // Searching a chest: the client calls ServerAttemptInteract(ReceivingActor, ...) on its controller, which runs
    // here as the server. Nothing happened, so log what is interacted with (and, once per class, its properties and
    // functions) and whether the game marked it searched.
    using StwExecFn = void(*)(UObject*, FFrame&, void*);
    StwExecFn StwInteractOriginal = nullptr;
    void StwInteractLog_Impl(UObject* PC, UObject* Actor, bool After) {
        char Line[300];
        if (!Actor) { Log("SyndicateStudio: interact: no actor"); return; }
        auto Off = Actor->GetOffset("bAlreadySearched");
        snprintf(Line, sizeof(Line), "SyndicateStudio: interact %s: %s (%s), searched %d", After ? "after" : "with", StwName(Actor).c_str(),
            Actor->Class ? StwName(Actor->Class).c_str() : "?", Off == uint32(-1) ? -1 : (int)GetFromOffset<uint8_t>(Actor, Off)); Log(Line);
        static std::vector<const void*> Seen;
        if (After || std::find(Seen.begin(), Seen.end(), (const void*)Actor->Class) != Seen.end()) return;
        Seen.push_back(Actor->Class);
        StwDumpObject("interact actor", Actor);
        StwListFunctions(Actor);
    }
    void StwInteractLog(UObject* PC, UObject* Actor, bool After) { __try { StwInteractLog_Impl(PC, Actor, After); } __except (EXCEPTION_EXECUTE_HANDLER) { Log("SyndicateStudio: interact log crashed (caught)"); } }
    // The game's search does nothing here (its loot code is server only): roll the container's
    // SearchLootTierGroup into the player's inventory, mark it searched and play its open (OnRep_bAlreadySearched).
    void StwSearchContainer_Impl(UObject* PC, UObject* Actor) {
        if (!PC || !Actor) return;
        auto Group = StwNameProp(Actor, "SearchLootTierGroup");
        if (Group.empty() || Group == "None") return;
        auto P = Actor->GetProperty("bAlreadySearched");
        uint32 Off = P ? GetFromOffset<uint32>(P, Offsets::Offset_Internal) : 0;
        uint8_t M = P ? P->GetFieldMask() : 0; if (P && !M) M = 0xFF;
        static std::vector<UObject*> Searched;
        if (std::find(Searched.begin(), Searched.end(), Actor) != Searched.end()) return;
        Searched.push_back(Actor);
        char Line[200]; snprintf(Line, sizeof(Line), "SyndicateStudio: loot: searching %s (%s)", StwName(Actor).c_str(), Group.c_str()); Log(Line);
        StwGiveLoot(PC, Group);
        if (P) GetFromOffset<uint8_t>(Actor, Off) |= M;
        StwCall Rep(Actor, "OnRep_bAlreadySearched");
        if (Rep) Rep.Run();
        // The open (lid, effects, sound) is the container's OnLoot event, which the server's search fires.
        StwCall Open(Actor, "OnLoot");
        if (Open) { Open.Run(); Log("SyndicateStudio: loot:   played OnLoot"); }
        else Log("SyndicateStudio: loot:   no OnLoot on this container");
        // OnLoot alone left the chest shut: the chest blueprints open on their own searched state (IsSearched with
        // OnRep_IsSearched, the Searched and OnSetSearched events).
        if (auto IP = Actor->GetProperty("IsSearched")) {
            uint8_t IM = IP->GetFieldMask(); if (!IM) IM = 0xFF;
            GetFromOffset<uint8_t>(Actor, GetFromOffset<uint32>(IP, Offsets::Offset_Internal)) |= IM;
        }
        for (auto Ev : { "OnSetSearched", "Searched", "OnRep_IsSearched" }) {
            StwCall C(Actor, Ev);
            if (!C) continue;
            if (std::string(Ev) == "OnSetSearched") C.Set<bool>("bNewSearched", true, -1);
            C.Run();
            snprintf(Line, sizeof(Line), "SyndicateStudio: loot:   called %s", Ev); Log(Line);
        }
    }
    void StwSearchContainer(UObject* PC, UObject* Actor) { __try { StwSearchContainer_Impl(PC, Actor); } __except (EXCEPTION_EXECUTE_HANDLER) { Log("SyndicateStudio: loot: search crashed (caught)"); } }
    void StwInteractDetour(UObject* Context, FFrame& Stack, void* Result) {
        UObject* Actor = Stack.Locals ? *(UObject**)Stack.Locals : nullptr;
        StwInteractLog(Context, Actor, false);
        StwInteractOriginal(Context, Stack, Result);
        StwSearchContainer(Context, Actor);
    }
    // Crafting: the craft button stayed grey with enough materials. Log what the craft gate reads (the game state's,
    // player controller's and player state's fields with Craft, TeamLevel or Gate in the name) and whether
    // ServerCraftSchematic is called at all, with its parameters.
    void StwDumpCraftFields_Impl(const char* Label, UObject* Obj) {
        if (!Obj) return;
        char Line[400];
        for (auto C = (const UStruct*)Obj->Class; C; C = C->GetSuper()) {
            std::string CName(((const UField*)C)->GetName().ToString().c_str());
            if (CName == "Actor" || CName == "Object") break;
            for (const UField* P = C->GetChildren(); P; P = P->GetNext()) {
                if (!P->Class) continue;
                std::string Name(P->GetName().ToString().c_str()), Type(P->Class->Name.ToString().c_str());
                if (Name.find("Craft") == std::string::npos && Name.find("TeamLevel") == std::string::npos && Name.find("Gate") == std::string::npos) continue;
                uint32 Off = GetFromOffset<uint32>(P, Offsets::Offset_Internal);
                auto At = (uint8_t*)Obj + Off;
                char V[64] = "";
                if (Type == "BoolProperty" || Type == "ByteProperty") snprintf(V, sizeof(V), "0x%02x", *At);
                else if (Type == "IntProperty") snprintf(V, sizeof(V), "%d", *(int32*)At);
                else if (Type == "FloatProperty") snprintf(V, sizeof(V), "%g", *(float*)At);
                else if (Type == "ArrayProperty") snprintf(V, sizeof(V), "[%d]", *(int32*)(At + 8));
                else if (Type == "ObjectProperty") snprintf(V, sizeof(V), "%s", StwName(*(UObject**)At).c_str());
                snprintf(Line, sizeof(Line), "SyndicateStudio: craft: %s %s::%s (%s) +0x%x = %s", Label, CName.c_str(), Name.c_str(), Type.c_str(), Off, V); Log(Line);
            }
        }
    }
    void StwDumpCraftFields(const char* Label, UObject* Obj) { __try { StwDumpCraftFields_Impl(Label, Obj); } __except (EXCEPTION_EXECUTE_HANDLER) {} }
    using StwExecFn2 = void(*)(UObject*, FFrame&, void*);
    StwExecFn2 StwCraftOriginal = nullptr;
    void StwCraftLog_Impl(UFunction* F, uint8_t* Locals) {
        char Line[400];
        Log("SyndicateStudio: craft: ServerCraftSchematic called");
        if (!F || !Locals) return;
        for (const UField* P = ((const UStruct*)F)->GetChildren(); P; P = P->GetNext()) {
            if (!P->Class) continue;
            uint32 Off = GetFromOffset<uint32>(P, Offsets::Offset_Internal), Size = GetFromOffset<uint32>(P, Offsets::ElementSize);
            std::string Hex;
            for (uint32 i = 0; i < Size && i < 24; i++) { char B[4]; snprintf(B, sizeof(B), "%02x", Locals[Off + i]); Hex += B; }
            snprintf(Line, sizeof(Line), "SyndicateStudio: craft:   %s (%s) +0x%x = %s", std::string(P->GetName().ToString().c_str()).c_str(), std::string(P->Class->Name.ToString().c_str()).c_str(), Off, Hex.c_str()); Log(Line);
        }
    }
    // The game's own ServerCraftSchematic takes the ingredients and runs the craft bar, but the result is server
    // only: no gun ever arrived. Find the schematic (the account item whose string ItemId matches), its weapon
    // (GetSchematicCraftingResultBP, else the schematic's name with SID_ -> WID_), and give it when the bar ends.
    void StwSendItemCrafted(UObject* Schem, UObject* PC, int Count); // below
    struct StwPendingCraft { ULONGLONG At; UObject* Def; int Count; UObject* Schem; };
    std::vector<StwPendingCraft> StwPendingCrafts;
    std::string StwFStr(const FString& F) { return F.Num() > 0 ? std::string(F.ToString().c_str()) : std::string(); }
    UObject* StwCraftResult(UObject* SchemDef) {
        char Line[300];
        UObject* Result = nullptr;
        StwCall R(SchemDef, "GetSchematicCraftingResultBP");
        if (R) {
            R.Run();
            for (const UField* P = ((const UStruct*)R.F)->GetChildren(); P; P = P->GetNext()) {
                if (!P->Class || std::string(P->Class->Name.ToString().c_str()) != "ObjectProperty") continue;
                auto V = *(UObject**)(R.Buf.data() + GetFromOffset<uint32>(P, Offsets::Offset_Internal));
                if (V && StwLooksLikeObject(V)) { Result = V; break; }
            }
        }
        if (!Result) {
            std::string N = StwName(SchemDef);
            if (N.rfind("SID_", 0) == 0) Result = StwItemDef(("WID_" + N.substr(4)).c_str(), {});
        }
        snprintf(Line, sizeof(Line), "SyndicateStudio: craft:   %s makes %s", StwName(SchemDef).c_str(), StwName(Result).c_str()); Log(Line);
        return Result;
    }
    // ---- Crafting costs ----
    // The game's ServerCraftSchematic no longer took the ingredients out ("7/6" stayed after crafting, so the gun
    // could be crafted forever). Read the schematic's recipe (CraftingRecipe: a row in the recipe table, whose
    // RecipeCosts list ingredient asset ids and quantities), refuse when the player lacks any of them, and after
    // the game's own call take out whatever it left in.
    struct StwCost { UObject* Def; int32 Need; int32 Before; };
    std::vector<StwCost> StwCraftCosts;
    int32 StwInvCount(UObject* PC, UObject* Def) {
        auto Inv = StwPtr(PC, "WorldInventory");
        auto EntryStruct = (const UStruct*)StwFindByName("FortItemEntry", "ScriptStruct");
        auto ListStruct = (const UStruct*)StwFindByName("FortItemList", "ScriptStruct");
        uint32 DefOff = 0, CountOff = 0, RepOff = 0, Sz = 0;
        auto InvOff = Inv ? Inv->GetOffset("Inventory") : uint32(-1);
        if (!EntryStruct || !ListStruct || InvOff == uint32(-1) || !StwField(EntryStruct, "ItemDefinition", DefOff, Sz) || !StwField(EntryStruct, "Count", CountOff, Sz)
            || !StwField(ListStruct, "ReplicatedEntries", RepOff, Sz)) return 0;
        auto& Entries = GetFromOffset<TArray<uint8>>(Inv, InvOff + RepOff);
        const int32 EntrySize = EntryStruct->GetPropertiesSize();
        int32 N = 0;
        for (int32 i = 0; i < Entries.Num(); i++) { auto E = Entries.GetData() + (size_t)i * EntrySize; if (*(UObject**)(E + DefOff) == Def) N += *(int32*)(E + CountOff); }
        return N;
    }
    // Fills StwCraftCosts for a schematic; false when the recipe can't be read.
    bool StwReadCosts(UObject* PC, UObject* SchemDef) {
        char Line[300];
        StwCraftCosts.clear();
        auto ROff = SchemDef->GetOffset("CraftingRecipe");
        if (ROff == uint32(-1)) { Log("SyndicateStudio: craft:   the schematic has no CraftingRecipe"); return false; }
        auto Table = GetFromOffset<UObject*>(SchemDef, ROff);
        FName Row = GetFromOffset<FName>(SchemDef, ROff + 8);
        if (std::string(Row.ToString().c_str()) == "None") { std::string N = SchemDef->Name.ToString().c_str(); std::wstring W(N.begin(), N.end()); Row = FName(W.c_str()); }
        // Why crafting cost nothing: on this client the schematic's CraftingRecipe handle has no table in it (the
        // tutorial AR's SID_Assault_Auto_C_Ore_T00 came back with a null DataTable), so the costs were never read and
        // nothing was taken. The row name is still there, and the recipe tables are loaded (the crafting screen shows
        // the ingredients). Without a table, look through every loaded DataTable whose rows have RecipeCosts for
        // that row.
        uint8_t* RowData = nullptr;
        const UStruct* RowStruct = nullptr;
        auto FindRow = [&](UObject* T) -> bool {
            auto RS = GetFromOffset<const UStruct*>(T, 0x28);
            uint32 O = 0, S = 0;
            if (!RS || !StwField(RS, "RecipeCosts", O, S)) return false;
            auto& Map = *(TMap<FName, uint8_t*>*)((uint8_t*)T + 0x30);
            for (auto& Pair : Map) if (Pair.Key() == Row) { RowData = Pair.Value(); RowStruct = RS; return true; }
            return false;
        };
        if (StwLooksLikeObject(Table)) FindRow(Table);
        else {
            // The last run found no loaded table with that row: the recipe table isn't loaded on this client
            // either (the crafting screen gets its ingredient list elsewhere). Load the game's crafting recipe
            // tables, then look for the row by name and, failing that, for the row whose RecipeResults makes this
            // schematic's item (SID_x makes WID_x).
            static bool Loaded = false;
            if (!Loaded) {
                Loaded = true;
                for (auto P : { L"/Game/Items/Datatables/CraftingRecipes_New.CraftingRecipes_New", L"/Game/Items/Datatables/CraftingRecipes.CraftingRecipes",
                                L"/Game/Items/DataTables/CraftingRecipes_New.CraftingRecipes_New", L"/Game/Items/DataTables/CraftingRecipes.CraftingRecipes" }) {
                    auto O = (UObject*)SDK::FindObject(P, FindClass("DataTable"));
                    std::wstring W(P); std::string N(W.begin(), W.end());
                    snprintf(Line, sizeof(Line), "SyndicateStudio: craft:   load %s -> %s", N.c_str(), StwName(O).c_str()); Log(Line);
                }
            }
            std::string Item = SchemDef->Name.ToString().c_str();
            if (Item.rfind("SID_", 0) == 0) Item = "WID_" + Item.substr(4);
            auto PairStruct0 = (const UStruct*)StwFindByName("FortItemQuantityPair", "ScriptStruct");
            uint32 IdOff0 = 0, Sz0 = 0;
            const bool PairOk = PairStruct0 && StwField(PairStruct0, "ItemPrimaryAssetId", IdOff0, Sz0);
            const int32 PairStride = PairOk ? PairStruct0->GetPropertiesSize() : 0;
            int Tables = 0;
            for (int32 k = 0, N = TUObjectArray::Num(); k < N && !RowData; k++) {
                auto O = (UObject*)TUObjectArray::GetObjectByIndex(k);
                if (!O || !O->Class || O->IsDefaultObject() || std::string(O->Class->Name.ToString().c_str()) != "DataTable") continue;
                auto RS = GetFromOffset<const UStruct*>(O, 0x28);
                uint32 CO = 0, RO = 0, S = 0;
                if (!RS || !StwField(RS, "RecipeCosts", CO, S)) continue;
                Tables++;
                if (FindRow(O)) { Table = O; break; }
                {
                    // The last runs logged no rows at all here: show the row struct and a few row names of each
                    // recipe table once, so the naming can be matched.
                    static std::vector<UObject*> Described;
                    if (std::find(Described.begin(), Described.end(), O) == Described.end()) {
                        Described.push_back(O);
                        auto& M = *(TMap<FName, uint8_t*>*)((uint8_t*)O + 0x30);
                        std::string Names; int Cnt = 0;
                        for (auto& Pair : M) { if (Cnt++ < 6) Names += std::string(Pair.Key().ToString().c_str()) + " "; }
                        snprintf(Line, sizeof(Line), "SyndicateStudio: craft:   table %s (%s), %d rows: %s, pair struct %s", StwName(O).c_str(), StwName((UObject*)RS).c_str(), Cnt, Names.c_str(), PairOk ? "ok" : "missing"); Log(Line);
                        StwDumpLayout("recipe row", RS, 0);
                    }
                }
                if (!PairOk || !StwField(RS, "RecipeResults", RO, S)) continue;
                auto& Map = *(TMap<FName, uint8_t*>*)((uint8_t*)O + 0x30);
                auto Lower = [](std::string V) { for (auto& Ch : V) Ch = (char)tolower((unsigned char)Ch); return V; };
                const std::string ItemL = Lower(Item);
                int Shown = 0;
                for (auto& Pair : Map) {
                    auto& Results = *(TArray<uint8_t>*)(Pair.Value() + RO);
                    std::string Names;
                    for (int32 r = 0; r < Results.Num(); r++) {
                        auto E = Results.GetData() + (size_t)r * PairStride + IdOff0;
                        std::string Type = (*(FName*)E).ToString().c_str(), Name = (*(FName*)(E + 8)).ToString().c_str();
                        Names += Type + ":" + Name + " ";
                        if (Lower(Name) == ItemL || Lower(Name).find(ItemL) != std::string::npos) { RowData = Pair.Value(); RowStruct = RS; Table = O; Row = Pair.Key(); }
                    }
                    // A few rows of each table, to see how the recipes are named if this one isn't found.
                    if (Shown++ < 3) { snprintf(Line, sizeof(Line), "SyndicateStudio: craft:   %s row %s makes %s", StwName(O).c_str(), Pair.Key().ToString().c_str(), Names.c_str()); Log(Line); }
                    if (RowData) break;
                }
            }
            snprintf(Line, sizeof(Line), "SyndicateStudio: craft:   the schematic's recipe handle has no table; %d recipe tables loaded; recipe for %s %s %s (row %s)", Tables, Item.c_str(),
                RowData ? "found in" : "not found", RowData ? StwName(Table).c_str() : "", Row.ToString().c_str()); Log(Line);
            if (!RowData) return false;
        }
        uint32 CostsOff = 0, Sz = 0;
        if (!RowData || !RowStruct || !StwField(RowStruct, "RecipeCosts", CostsOff, Sz)) {
            snprintf(Line, sizeof(Line), "SyndicateStudio: craft:   recipe row %s not found in %s", Row.ToString().c_str(), StwName(Table).c_str()); Log(Line); return false;
        }
        auto PairStruct = (const UStruct*)StwFindByName("FortItemQuantityPair", "ScriptStruct");
        uint32 IdOff = 0, QtyOff = 0;
        if (!PairStruct || !StwField(PairStruct, "ItemPrimaryAssetId", IdOff, Sz) || !StwField(PairStruct, "Quantity", QtyOff, Sz)) {
            static bool Dumped = false; if (!Dumped) { Dumped = true; StwDumpLayout("recipe row", RowStruct, 0); }
            Log("SyndicateStudio: craft:   recipe cost layout not understood"); return false;
        }
        auto& Costs = *(TArray<uint8_t>*)(RowData + CostsOff);
        const int32 Stride = PairStruct->GetPropertiesSize();
        for (int32 i = 0; i < Costs.Num(); i++) {
            auto E = Costs.GetData() + (size_t)i * Stride;
            // FPrimaryAssetId: PrimaryAssetType (an FName in a struct), then PrimaryAssetName.
            std::string Name = (*(FName*)(E + IdOff + 8)).ToString().c_str();
            int32 Qty = *(int32*)(E + QtyOff);
            UObject* Def = nullptr;
            for (int32 k = 0, N = TUObjectArray::Num(); k < N && !Def; k++) {
                auto O = (UObject*)TUObjectArray::GetObjectByIndex(k);
                if (O && O->Class && !O->IsDefaultObject() && std::string(O->Name.ToString().c_str()) == Name
                    && std::string(O->Class->Name.ToString().c_str()).find("Definition") != std::string::npos) Def = O;
            }
            int32 Have = Def ? StwInvCount(PC, Def) : 0;
            snprintf(Line, sizeof(Line), "SyndicateStudio: craft:   needs %s x%d (has %d)", Name.c_str(), Qty, Have); Log(Line);
            if (!Def) return false;
            StwCraftCosts.push_back({ Def, Qty, Have });
        }
        return true;
    }
    // After the game's own ServerCraftSchematic: take out what it did not.
    void StwPayCosts() {
        auto PC = StwLocalPC();
        for (auto& C : StwCraftCosts) {
            int32 Now = StwInvCount(PC, C.Def);
            int32 Taken = C.Before - Now;
            if (Taken < C.Need) {
                StwAddResource(PC, C.Def, -(C.Need - Taken));
                char Line[200]; snprintf(Line, sizeof(Line), "SyndicateStudio: craft:   took %d %s (the game took %d)", C.Need - Taken, StwName(C.Def).c_str(), Taken); Log(Line);
            }
        }
        StwCraftCosts.clear();
        StwCallNoParms(PC, "HandleWorldInventoryLocalUpdate");
        StwCallNoParms(StwPtr(PC, "WorldInventory"), "HandleInventoryLocalUpdate");
    }
    void StwCraft_Impl(UObject* PC, uint8_t* Locals) {
        char Line[400];
        if (!PC || !Locals) return;
        std::string Id = StwFStr(*(FString*)Locals);
        snprintf(Line, sizeof(Line), "SyndicateStudio: craft: crafting item %s", Id.c_str()); Log(Line);
        UObject* SchemDef = nullptr;
        int Logged = 0;
        const int32 Count = TUObjectArray::Num();
        for (int32 i = 0; i < Count && !SchemDef; i++) {
            auto O = (UObject*)TUObjectArray::GetObjectByIndex(i);
            if (!O || !O->Class || O->IsDefaultObject() || std::string(O->Class->Name.ToString().c_str()).find("SchematicItem") == std::string::npos) continue;
            if (std::string(O->Class->Name.ToString().c_str()).find("Definition") != std::string::npos) continue;
            bool Match = false;
            std::string Strs;
            for (auto C = (const UStruct*)O->Class; C && !Match; C = C->GetSuper())
                for (const UField* P = C->GetChildren(); P; P = P->GetNext()) {
                    if (!P->Class || std::string(P->Class->Name.ToString().c_str()) != "StrProperty") continue;
                    std::string V = StwFStr(*(FString*)((uint8_t*)O + GetFromOffset<uint32>(P, Offsets::Offset_Internal)));
                    Strs += std::string(P->GetName().ToString().c_str()) + "=" + V + " ";
                    if (!V.empty() && _stricmp(V.c_str(), Id.c_str()) == 0) { Match = true; break; }
                }
            StwCall GetDef(O, "GetItemDefinitionBP");
            UObject* Def = nullptr;
            if (GetDef) { GetDef.Run(); Def = GetDef.Get<UObject*>("ReturnValue", 0); }
            if (Logged++ < 6) { snprintf(Line, sizeof(Line), "SyndicateStudio: craft:   schematic item %s (%s) %s", StwName(O).c_str(), StwName(Def).c_str(), Strs.substr(0, 200).c_str()); Log(Line); }
            if (Match) SchemDef = Def;
        }
        if (!SchemDef) { Log("SyndicateStudio: craft:   no schematic item has that id"); return; }
        auto Result = StwCraftResult(SchemDef);
        if (!Result) return;
        if (StwReadCosts(PC, SchemDef)) {
            for (auto& C : StwCraftCosts) if (C.Before < C.Need) {
                snprintf(Line, sizeof(Line), "SyndicateStudio: craft:   not enough %s (%d of %d): not crafted", StwName(C.Def).c_str(), C.Before, C.Need); Log(Line);
                StwCraftCosts.clear();
                return;
            }
        }
        int Made = 1;
        StwCall Q(SchemDef, "GetSchematicQuantityProduced");
        if (Q) { Q.Run(); Made = Q.Get<int32>("ReturnValue", 0); if (Made < 1 || Made > 999) Made = 1; }
        StwPendingCrafts.push_back({ GetTickCount64() + 3000, Result, Made, SchemDef });
    }
    void StwCraftTick() {
        auto Now = GetTickCount64();
        for (auto It = StwPendingCrafts.begin(); It != StwPendingCrafts.end();) {
            if (Now < It->At) { ++It; continue; }
            auto PC = StwLocalPC();
            auto Inv = StwPtr(PC, "WorldInventory");
            char Line[200]; snprintf(Line, sizeof(Line), "SyndicateStudio: craft: crafted %s x%d", StwName(It->Def).c_str(), It->Count); Log(Line);
            if (std::string(It->Def->Class->Name.ToString().c_str()).find("Weapon") != std::string::npos)
                StwGiveItem(PC, Inv, StwPtr(PC, "QuickBars"), It->Def, It->Count, -1);
            else StwAddResource(PC, It->Def, It->Count);
            StwCallNoParms(PC, "HandleWorldInventoryLocalUpdate");
            StwCallNoParms(Inv, "HandleInventoryLocalUpdate");
            // ItemCrafted: the game already sends it (09:23:21 in the last run, before this), so it isn't sent again here.
            It = StwPendingCrafts.erase(It);
        }
    }
    // ---- Telling the mission an item was crafted ----
    // After the AR was crafted the tutorial kept giving the crafting materials back: its ItemCrafted handler never
    // ran. A zone server sends "ItemCrafted" when a player crafts (exe+0x140825480: a UFortItemCraftedParams made
    // with exe+0x1407f02c0 in the transient package, filled by exe+0x1406b29d0 with the schematic definition at
    // +0x28, the controller at +0x30, the count at +0x38 and bIsQuickCrafted at +0x3c, then the event "ItemCrafted"),
    // but only through the zone game mode's mission manager, which this game doesn't have. Send the same event
    // through FortMissionLibrary.ExecuteMissionEvent, the way the trap step's PlayerBuilt is sent.
    void StwSendItemCrafted_Impl(UObject* Schem, UObject* PC, int Count) {
        char Line[300];
        auto Base = (uint8_t*)GetModuleHandleW(nullptr);
        static const uint8_t NewParams[] = { 0x48, 0x89, 0x5C, 0x24, 0x08, 0x57, 0x48, 0x83, 0xEC, 0x60 }, Fill[] = { 0x0F, 0xB6, 0x44, 0x24, 0x28, 0x88, 0x41, 0x3C, 0x48, 0x89, 0x51, 0x28 },
            Transient[] = { 0x48, 0x8B, 0x05, 0x71, 0x3A, 0x2D, 0x05, 0xC3 };
        if (memcmp(Base + 0x7f02c0, NewParams, sizeof(NewParams)) || memcmp(Base + 0x6b29d0, Fill, sizeof(Fill)) || memcmp(Base + 0x138c1e0, Transient, sizeof(Transient))) { Log("SyndicateStudio: craft: not the 1.7.2 exe, ItemCrafted not sent"); return; }
        static UObject* Lib = StwFindByName("Default__FortMissionLibrary", nullptr);
        if (!Lib || !StwLooksLikeObject(Schem)) return;
        auto Params = ((UObject*(*)(UObject*))(Base + 0x7f02c0))(((UObject*(*)())(Base + 0x138c1e0))());
        if (!StwLooksLikeObject(Params) || std::string(Params->Class->Name.ToString().c_str()) != "FortItemCraftedParams") {
            snprintf(Line, sizeof(Line), "SyndicateStudio: craft: params came out as %s, ItemCrafted not sent", StwLooksLikeObject(Params) ? StwName(Params->Class).c_str() : "null"); Log(Line); return;
        }
        ((void(*)(UObject*, UObject*, UObject*, int32, uint8_t))(Base + 0x6b29d0))(Params, Schem, PC, Count, 0);
        static const FName KItemCrafted(L"ItemCrafted");
        StwCall C(Lib, "ExecuteMissionEvent");
        if (!C) return;
        C.Set("WorldContextObject", (UObject*)UWorld::GetWorld(), 0);
        C.Set("EventType", KItemCrafted, 8);
        C.Set("Params", Params, 0x40);
        C.Run();
        snprintf(Line, sizeof(Line), "SyndicateStudio: craft: sent ItemCrafted for %s x%d", StwName(Schem).c_str(), Count); Log(Line);
    }
    void StwSendItemCrafted(UObject* Schem, UObject* PC, int Count) { __try { StwSendItemCrafted_Impl(Schem, PC, Count); } __except (EXCEPTION_EXECUTE_HANDLER) { Log("SyndicateStudio: craft: sending ItemCrafted crashed (caught)"); } }
    void StwCraftTickSafe() { __try { StwCraftTick(); } __except (EXCEPTION_EXECUTE_HANDLER) { Log("SyndicateStudio: craft: giving the crafted item crashed (caught)"); } }
    void StwCraftDetour(UObject* Context, FFrame& Stack, void* Result) {
        __try { StwCraftLog_Impl(Stack.Node, Stack.Locals); } __except (EXCEPTION_EXECUTE_HANDLER) {}
        __try { StwCraft_Impl(Context, Stack.Locals); } __except (EXCEPTION_EXECUTE_HANDLER) { Log("SyndicateStudio: craft: crashed (caught)"); }
        StwCraftOriginal(Context, Stack, Result);
        if (!StwCraftCosts.empty()) { __try { StwPayCosts(); } __except (EXCEPTION_EXECUTE_HANDLER) { Log("SyndicateStudio: craft: paying the cost crashed (caught)"); } }
    }
    // The backpack showed 0 slots ("BACKPACK 1/0"), which also greys out crafting. Its size is the game's base
    // plus WorldInventorySizeBonus (from the profile's inventory_limit_bonus); give the player 100 extra slots
    // wherever the bonus lives (controller, player state, pawn) and lift a size limit, logging what was there.
    void StwBackpackSize_Impl(UObject* PC) {
        char Line[300];
        for (auto O : { PC, StwPtr(PC, "PlayerState"), StwPtr(PC, "Pawn"), StwPtr(PC, "WorldInventory") }) {
            if (!O) continue;
            for (auto Name : { "WorldInventorySizeBonus", "InventorySizeBonus", "OverriddenBackpackSize", "MaxBackpackSize" }) {
                auto Off = O->GetOffset(Name);
                if (Off == uint32(-1)) continue;
                auto& V = GetFromOffset<int32>(O, Off);
                snprintf(Line, sizeof(Line), "SyndicateStudio: craft: %s.%s = %d", StwName(O).c_str(), Name, V); Log(Line);
                // The controller's OverriddenBackpackSize (-1 = none) sets the size outright; the profile's bonus
                // never reached the zone.
                if (std::string(Name) == "OverriddenBackpackSize" && V < 100) {
                    V = 100;
                    snprintf(Line, sizeof(Line), "SyndicateStudio: craft:   set %s.OverriddenBackpackSize to 100", StwName(O).c_str()); Log(Line);
                }
                if (std::string(Name) == "WorldInventorySizeBonus" && V < 100) {
                    V = 100;
                    StwCallNoParms(O, "OnRep_WorldInventorySizeBonus");
                    snprintf(Line, sizeof(Line), "SyndicateStudio: craft:   set %s.WorldInventorySizeBonus to 100", StwName(O).c_str()); Log(Line);
                }
            }
        }
    }
    void StwBackpackSize(UObject* PC) { __try { StwBackpackSize_Impl(PC); } __except (EXCEPTION_EXECUTE_HANDLER) { Log("SyndicateStudio: craft: backpack size crashed (caught)"); } }
    // A bool property read with its bit mask (several bools share a byte).
    int StwBool(UObject* O, const char* Name) {
        if (!O) return -1;
        auto P = O->GetProperty(Name);
        if (!P) return -1;
        uint8_t M = P->GetFieldMask(); if (!M) M = 0xFF;
        return (GetFromOffset<uint8_t>(O, GetFromOffset<uint32>(P, Offsets::Offset_Internal)) & M) ? 1 : 0;
    }
    // ---- How a husk gets on an assignment ----
    // The settings are right now (PrimaryWorldAssignment), yet no controller ever joins the encounter's assignment.
    // In 1.7.2 the goal manager registers each AI (exe+0x3088f0: adds it to its AI info map at +0x3b8), runs an EQS
    // goal query for it (exe+0x30dea0), and joins it to the assignment of the goal the query picks when the result
    // comes back (exe+0x3046c0, called from the query's callback). Watch all three to see which step stops.
    using StwGmFn2 = void(*)(UObject*, UObject*);
    using StwGmFn3 = void(*)(UObject*, UObject*, void*);
    using StwGmRes = void(*)(UObject*, void*, UObject*);
    StwGmFn2 StwRegisterAIOriginal = nullptr; StwGmFn3 StwGoalQueryOriginal = nullptr; StwGmRes StwGoalResultOriginal = nullptr;
    void StwLogGoalStep(const char* What, UObject* Ctl, UObject* GM) {
        char L[200]; snprintf(L, sizeof(L), "SyndicateStudio: goals: %s %s (%s)", What, StwName(Ctl).c_str(), StwName(GM).c_str()); Log(L);
    }
    void StwRegisterAIDetour(UObject* GM, UObject* Ctl) {
        StwRegisterAIOriginal(GM, Ctl);
        static int N = 0; if (N++ < 20) { __try { StwLogGoalStep("registered", Ctl, GM); } __except (EXCEPTION_EXECUTE_HANDLER) {} }
    }
    void StwGoalQueryDetour(UObject* GM, UObject* Ctl, void* Info) {
        static int N = 0; if (N++ < 20) { __try { StwLogGoalStep("goal query started for", Ctl, GM); } __except (EXCEPTION_EXECUTE_HANDLER) {} }
        StwGoalQueryOriginal(GM, Ctl, Info);
    }
    void StwLogGoalResult(void* Res, UObject* Ctl) {
        char L[400];
        auto R = Res ? *(uint8_t**)Res : nullptr;
        if (!R) { snprintf(L, sizeof(L), "SyndicateStudio: goals: query result for %s: none", StwName(Ctl).c_str()); Log(L); return; }
        auto Q = (uint64_t*)R;
        snprintf(L, sizeof(L), "SyndicateStudio: goals: query result for %s: %d items; raw %016llx %016llx %016llx %016llx %016llx %016llx %016llx %016llx", StwName(Ctl).c_str(), *(int32*)(R + 8),
            (unsigned long long)Q[0], (unsigned long long)Q[1], (unsigned long long)Q[2], (unsigned long long)Q[3], (unsigned long long)Q[4], (unsigned long long)Q[5], (unsigned long long)Q[6], (unsigned long long)Q[7]); Log(L);
    }
    // Why the husks walk an inch, stop for a second and walk again: each goal query comes back with no items, and
    // the goal manager's result handler (exe+0x140301580) then clears the controller's goal when no goal can be
    // taken from the result (exe+0x1402d66d0 with null, then exe+0x1402bac10). The husk drive sets the player back
    // as the goal a second later, the tree starts the move again, the next empty result stops it. An empty result
    // says "nothing new to go for", not "stop chasing the player": when the husk is going for a live player and the
    // query found nothing, keep that goal and do the rest of the handler ourselves: clear the query's in-progress
    // flag (AI info +0x54, exe+0x1403046e9) and schedule the next query (exe+0x14030a960 with the controller's
    // +0x5c8 and the AI info), exactly as the handler does after applying a result. Any husk not chasing a
    // player goes through the game's handler unchanged.
    bool StwKeepPlayerGoal(UObject* GM, void* Res, UObject* Ctl) {
        // Also a result that has items: the fort defence husks sent at the player went back to the encounter's
        // objective (BP_FinalEncounterTarget) within seconds, stood idle again, were sent at the player again: the
        // same walk-stop-walk as before. A husk already chasing a live player stays on that player.
        auto R = Res ? *(uint8_t**)Res : nullptr;
        const int32 Items = R ? *(int32*)(R + 8) : 0;
        if (!StwLooksLikeObject(Ctl)) return false;
        auto Goal = StwPtr(Ctl, "GoalActor");
        auto Player = StwPtr(StwLocalPC(), "Pawn");
        if (!Goal || Goal != Player || !StwLooksLikeObject(Player)) return false;
        auto Base = (uint8_t*)GetModuleHandleW(nullptr);
        static const uint8_t Find[] = { 0x48, 0x89, 0x5C, 0x24, 0x10, 0x57, 0x48, 0x83, 0xEC, 0x20 }, Next[] = { 0x48, 0x89, 0x5C, 0x24, 0x08, 0x57, 0x48, 0x83, 0xEC, 0x20 };
        if (memcmp(Base + 0x2fb2a0, Find, sizeof(Find)) || memcmp(Base + 0x30a960, Next, sizeof(Next))) return false;
        auto Info = ((uint8_t*(*)(UObject*, UObject*))(Base + 0x2fb2a0))(GM, Ctl);
        if (!Info || Info[0x55]) return false; // the other branch (exe+0x14030a8e0) is left to the game
        Info[0x54] = 0;
        if (auto Assign = *(void**)((uint8_t*)Ctl + 0x5c8)) ((void(*)(UObject*, void*, void*))(Base + 0x30a960))(GM, Assign, Info);
        static int Logged = 0;
        static int LoggedItems = 0;
        if (Items ? LoggedItems++ < 20 : Logged++ < 10) { char L[200]; snprintf(L, sizeof(L), "SyndicateStudio: goals: result with %d items for %s, kept its goal %s", Items, StwName(Ctl).c_str(), StwName(Goal).c_str()); Log(L); }
        return true;
    }
    void StwGoalResultDetour(UObject* GM, void* Res, UObject* Ctl) {
        static int N = 0; if (N++ < 30) { __try { StwLogGoalResult(Res, Ctl); } __except (EXCEPTION_EXECUTE_HANDLER) {} }
        bool Kept = false;
        __try { Kept = StwKeepPlayerGoal(GM, Res, Ctl); } __except (EXCEPTION_EXECUTE_HANDLER) { Kept = false; }
        if (!Kept) StwGoalResultOriginal(GM, Res, Ctl);
    }
    // ---- Which mission events run ----
    // The trap step's counter reached its target but the laser barrier never opened, and the tutorial keeps giving
    // crafting materials back after the AR is crafted: both wait on a later mission event that never comes. Every
    // mission event reaches the blueprints as a HandleMissionEvent_<event> call through ProcessEvent; log each one
    // (event, receiver), plus the trap objective's own counter, so the event the next step waits for shows up.
    using StwPEFn = void(*)(UObject*, UObject*, void*);
    StwPEFn StwPEOriginal = nullptr;
    SRWLOCK StwEventLock = SRWLOCK_INIT;
    std::vector<UObject*> StwEventFuncs; // sorted
    void StwScanEventFuncs() {
        std::vector<UObject*> V;
        static const UClass* FuncCls = FindClass("Function");
        for (int32 i = 0, N = TUObjectArray::Num(); i < N; i++) {
            auto O = (UObject*)TUObjectArray::GetObjectByIndex(i);
            if (!O || O->Class != FuncCls) continue;
            if (std::string(O->Name.ToString().c_str()).rfind("HandleMissionEvent_", 0) == 0) V.push_back(O);
        }
        std::sort(V.begin(), V.end());
        AcquireSRWLockExclusive(&StwEventLock); StwEventFuncs.swap(V); ReleaseSRWLockExclusive(&StwEventLock);
    }
    void StwScanEventFuncsSafe() { __try { StwScanEventFuncs(); } __except (EXCEPTION_EXECUTE_HANDLER) {} }
    void StwLogMissionEvent(UObject* O, UObject* F) {
        static int Logged = 0;
        static std::map<std::pair<UObject*, UObject*>, int> Per; // a few lines per handler and receiver: the harvest counters fire every second
        if (Logged >= 3000 || Per[{ O, F }]++ >= 3) return;
        Logged++;
        char Line[300];
        std::string Extra;
        static UObject* TrapObj = StwFindByName("OBJ_TrapPlacement_C_0", nullptr); // looked up once: a full object search per event stalls the frame
        if (auto T = TrapObj) if (auto Off = T->GetOffset("TrapsPlaced"); Off != uint32(-1)) Extra = " (trap objective TrapsPlaced " + std::to_string(GetFromOffset<int32>(T, Off)) + ")";
        snprintf(Line, sizeof(Line), "SyndicateStudio: event: %s on %s%s", F->Name.ToString().c_str(), StwName(O).c_str(), Extra.c_str()); Log(Line);
    }
    bool StwIsEventFunc(UObject* F) {
        AcquireSRWLockShared(&StwEventLock);
        bool Hit = std::binary_search(StwEventFuncs.begin(), StwEventFuncs.end(), F);
        ReleaseSRWLockShared(&StwEventLock);
        return Hit;
    }
    void StwPEDetour(UObject* O, UObject* F, void* P) {
        if (F && StwIsEventFunc(F)) { __try { StwLogMissionEvent(O, F); } __except (EXCEPTION_EXECUTE_HANDLER) {} }
        StwPEOriginal(O, F, P);
    }
    void StwHookMissionEvents() {
        static bool Done = false;
        if (Done) return;
        Done = true;
        StwScanEventFuncs();
        auto Any = (UObject*)UWorld::GetWorld();
        if (!Any || !Offsets::ProcessEventVft) { Log("SyndicateStudio: event: no ProcessEvent to hook"); return; }
        auto Target = (*(void***)Any)[Offsets::ProcessEventVft];
        MH_Initialize();
        if (MH_CreateHook(Target, (void*)&StwPEDetour, (void**)&StwPEOriginal) == MH_OK && MH_EnableHook(Target) == MH_OK) {
            char Line[200]; snprintf(Line, sizeof(Line), "SyndicateStudio: event: watching %d mission event handlers", (int)StwEventFuncs.size()); Log(Line);
        } else Log("SyndicateStudio: event: could not hook ProcessEvent");
    }
    void StwHookGoalFlow() {
        static bool Done = false;
        if (Done) return;
        Done = true;
        auto Base = (uint8_t*)GetModuleHandleW(nullptr);
        // Each start checked against the 1.7.2 bytes before it is hooked.
        static const uint8_t Reg[] = { 0x48, 0x89, 0x5C, 0x24, 0x20, 0x56 }, Qry[] = { 0x40, 0x55, 0x56, 0x57, 0x41, 0x56 }, Res[] = { 0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x74 };
        MH_Initialize();
        int Hooked = 0;
        if (!memcmp(Base + 0x3088f0, Reg, sizeof(Reg)) && MH_CreateHook(Base + 0x3088f0, (void*)&StwRegisterAIDetour, (void**)&StwRegisterAIOriginal) == MH_OK && MH_EnableHook(Base + 0x3088f0) == MH_OK) Hooked++;
        if (!memcmp(Base + 0x30dea0, Qry, sizeof(Qry)) && MH_CreateHook(Base + 0x30dea0, (void*)&StwGoalQueryDetour, (void**)&StwGoalQueryOriginal) == MH_OK && MH_EnableHook(Base + 0x30dea0) == MH_OK) Hooked++;
        if (!memcmp(Base + 0x3046c0, Res, sizeof(Res)) && MH_CreateHook(Base + 0x3046c0, (void*)&StwGoalResultDetour, (void**)&StwGoalResultOriginal) == MH_OK && MH_EnableHook(Base + 0x3046c0) == MH_OK) Hooked++;
        char L[120]; snprintf(L, sizeof(L), "SyndicateStudio: goals: watching %d of 3 goal steps", Hooked); Log(L);
    }
    void StwHookExecute(UObject* PC) {
        static bool Done = false;
        if (Done || !PC) return;
        auto F = PC->GetFunction("ServerExecuteInventoryItem");
        if (!F) { Log("SyndicateStudio: kit: no ServerExecuteInventoryItem"); return; }
        StwExecOriginal = F->GetNativeFunc();
        F->SetNativeFunc((void*)&StwExecDetour);
        Done = true;
        Log("SyndicateStudio: kit: hooked ServerExecuteInventoryItem (selecting a slot puts the weapon in hand)");
        if (auto B = PC->GetFunction("ServerCreateBuildingActor")) { B->SetNativeFunc((void*)&StwBuildDetour); Log("SyndicateStudio: kit: hooked ServerCreateBuildingActor (building)"); }
        if (auto Cr = PC->GetFunction("ServerCraftSchematic")) {
            auto Native = Cr->GetNativeFunc();
            MH_Initialize();
            if (Native && MH_CreateHook(Native, (void*)&StwCraftDetour, (void**)&StwCraftOriginal) == MH_OK && MH_EnableHook(Native) == MH_OK)
                Log("SyndicateStudio: kit: hooked ServerCraftSchematic (crafting)");
        }
        StwBackpackSize(PC);
        StwHookGoalFlow();
        StwHookMissionEvents();
        StwDumpCraftFields("controller", PC);
        if (auto W = UWorld::GetWorld()) StwDumpCraftFields("game state", (UObject*)W->GameState);
        StwDumpCraftFields("player state", StwPtr(PC, "PlayerState"));
        if (auto I = PC->GetFunction("ServerAttemptInteract")) {
            auto Native = I->GetNativeFunc();
            MH_Initialize();
            if (Native && MH_CreateHook(Native, (void*)&StwInteractDetour, (void**)&StwInteractOriginal) == MH_OK && MH_EnableHook(Native) == MH_OK)
                Log("SyndicateStudio: kit: hooked ServerAttemptInteract (searching)");
        }
    }
    bool StwKitGiven = false;
    void StwGiveKit_Impl() {
        auto World = UWorld::GetWorld();
        if (!World) return;
        UObject* PC = nullptr;
        if (auto Instance = World->OwningGameInstance; Instance && Instance->LocalPlayers.Num() && Instance->LocalPlayers[0]) PC = (UObject*)Instance->LocalPlayers[0]->PlayerController;
        if (!PC) return;
        auto PS = StwPtr(PC, "PlayerState");
        StwGiveAbilities(PS);
        if (StwKitGiven) return;
        StwKitGiven = true;
        StwHookExecute(PC);
        StwHookHarvest();
        // Quickbars (Pre-season and Season 1 keep them in a separate actor).
        auto Bars = StwPtr(PC, "QuickBars");
        if (!Bars) {
            if (auto Cls = (const UClass*)StwFindByName("FortQuickBars", "Class")) {
                Bars = (UObject*)UWorld::SpawnActor(Cls, FVector{}, FRotator{}, (AActor*)PC);
                if (Bars) {
                    if (auto O = PC->GetOffset("QuickBars"); O != uint32(-1)) GetFromOffset<UObject*>(PC, O) = Bars;
                    StwCall Owner(Bars, "SetOwner"); if (Owner) { Owner.Set<UObject*>("NewOwner", PC, 0); Owner.Run(); }
                    StwCallNoParms(PC, "OnRep_QuickBar");
                    Log("SyndicateStudio: kit: made the quickbars");
                }
            } else Log("SyndicateStudio: kit: no FortQuickBars class");
        }
        auto Inv = StwPtr(PC, "WorldInventory");
        if (!Inv) { Log("SyndicateStudio: kit: no world inventory"); return; }
        StwLogItemDefs("WID_Harvest");
        StwLogItemDefs("WID_Pistol");
        StwLogItemDefs("WID_Assault");
        auto Pickaxe = StwItemDef("WID_Harvest_Pickaxe_C_T01", { L"/Game/Items/Weapons/Melee/Harvest/WID_Harvest_Pickaxe_C_T01.WID_Harvest_Pickaxe_C_T01",
            L"/Game/Items/Weapons/Harvest/WID_Harvest_Pickaxe_C_T01.WID_Harvest_Pickaxe_C_T01",
            L"/Game/Athena/Items/Weapons/WID_Harvest_Pickaxe_Athena_C_T01.WID_Harvest_Pickaxe_Athena_C_T01" });
        auto PickGuid = StwGiveItem(PC, Inv, Bars, Pickaxe, 1, 0);
        if (!Pickaxe) Log("SyndicateStudio: kit: no pickaxe definition found");
        auto Pistol = StwItemDef("WID_Pistol_SemiAuto_C_Ore_T00", { L"/Game/Items/Weapons/Ranged/Pistol/WID_Pistol_SemiAuto_C_Ore_T00.WID_Pistol_SemiAuto_C_Ore_T00" });
        if (!Pistol) Log("SyndicateStudio: kit: no starter pistol definition found");
        StwGiveItem(PC, Inv, Bars, Pistol, 1, 0);
        for (auto [Name, Path, Count, Bar] : std::initializer_list<std::tuple<const char*, const wchar_t*, int, int>>{
                { "BuildingItemData_Wall", L"/Game/Items/Weapons/BuildingTools/BuildingItemData_Wall.BuildingItemData_Wall", 1, 1 },
                { "BuildingItemData_Floor", L"/Game/Items/Weapons/BuildingTools/BuildingItemData_Floor.BuildingItemData_Floor", 1, 1 },
                { "BuildingItemData_Stair_W", L"/Game/Items/Weapons/BuildingTools/BuildingItemData_Stair_W.BuildingItemData_Stair_W", 1, 1 },
                { "BuildingItemData_RoofS", L"/Game/Items/Weapons/BuildingTools/BuildingItemData_RoofS.BuildingItemData_RoofS", 1, 1 },
                { "EditTool", L"/Game/Items/Weapons/BuildingTools/EditTool.EditTool", 1, -1 },
                { "AmmoDataBulletsLight", L"/Game/Items/Ammo/AmmoDataBulletsLight.AmmoDataBulletsLight", 120, -1 } }) {
            auto Def = StwItemDef(Name, { Path });
            if (!Def) { StwKitLog("SyndicateStudio: kit: %s not found", Name); continue; }
            StwGiveItem(PC, Inv, Bars, Def, Count, Bar);
        }
        StwCallNoParms(PC, "HandleWorldInventoryLocalUpdate");
        StwCallNoParms(Inv, "HandleInventoryLocalUpdate");
        if (Bars) { StwCallNoParms(Bars, "OnRep_PrimaryQuickBar"); StwCallNoParms(Bars, "OnRep_SecondaryQuickBar"); }
        // Hold the pickaxe.
        if (!PickGuid.Zero()) {
            StwCall Exec(PC, "ServerExecuteInventoryItem");
            if (Exec) { Exec.Set<StwGuid>("ItemGuid", PickGuid, 0); Exec.Run(); Log("SyndicateStudio: kit: equipped the pickaxe"); }
        }
    }
    void StwGiveKit() { __try { StwGiveKit_Impl(); } __except (EXCEPTION_EXECUTE_HANDLER) { Log("SyndicateStudio: kit crashed (caught)"); } }
    // ---- The tutorial mission ----
    // A zone's missions come from its mission records: FFortMissionRecord (0x128 bytes; MissionEntry.MissionInfo
    // at +0x28, MissionGenerator at +0x88, MissionStatus at +0xf0, MissionGuid at +0x118), kept by the world manager
    // (its record container at +0x8a0, the records array at +0x8a8). A real server generated them from the zone's
    // mission generator; locally nothing does, so the tutorial had no mission (no objectives, no Ramirez, no husks).
    // Build the tutorial's record from MissionGen_Onboarding_Fort (its PrimaryMissionInfo) and hand it to the mission
    // manager's own loader (the function that logs "Loading mission records, %i in total.", 1.7.2 exe+0x6aa4d0).
    // The client exe can't find the AI director even when one exists: the engine's "get the AI director of this
    // world" helper is compiled out of the client (1.7.2: every call goes to a shared `xor eax,eax; ret`,
    // exe+0x15862c0), so missions log "No AI Director!" and no husks spawn. Point the AI director's call sites
    // (the mission encounter starters, the encounter/director code; found in the exe by what the code does with
    // the result) at a stub that returns ours. Each site is checked before it is patched.
    UObject* StwAIDirectorPtr = nullptr;
    UObject* StwGetAIDirector(UObject*) { return StwAIDirectorPtr; }
    uint8_t* StwAllocNear(uint8_t* Base, size_t Size) {
        auto Nt = (IMAGE_NT_HEADERS64*)(Base + ((IMAGE_DOS_HEADER*)Base)->e_lfanew);
        uintptr_t Start = ((uintptr_t)Base + Nt->OptionalHeader.SizeOfImage + 0xFFFF) & ~(uintptr_t)0xFFFF;
        for (uintptr_t A = Start; A < Start + 0x70000000; A += 0x10000)
            if (auto P = VirtualAlloc((void*)A, Size, MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE)) return (uint8_t*)P;
        return nullptr;
    }
    void StwRedirectAIDirectorCalls() {
        static bool Done = false;
        if (Done) return;
        Done = true;
        auto Base = (uint8_t*)GetModuleHandleW(nullptr);
        // (0x2bc550 / 0x2bc5a0 were here: they are the encounter's assignment-settings lookup, which reads the GOAL
        // MANAGER's DefaultEnemyAssignmentSettings (+0x480) / DefaultEncounterAssignmentSettings (+0x478). Given the
        // director, they read director memory as the settings: the garbage 0x6A289 every encounter assignment got,
        // which kept the husks off their assignment. They are goal-manager sites now.)
        static const uint32_t Sites[] = { 0x30d6c2, 0x32e8fe, 0x3300d8, 0x7c7b69, 0x7e2bb0, 0x83204a, 0x8dfea6, 0x315c7f, 0x431689, 0x49ec1c, 0x576ec7, 0x662a4e, 0x6821b7, 0x682518,
            0x682c65, 0x682d50, 0x7c3910, 0x7c39f2, 0x7c3af2, 0x7c5b22, 0x7c9021, 0x7c945c, 0x7c97aa, 0x7c9831, 0x7c9d91, 0x7d390a, 0x7d4132,
            0x7e2749, 0x7e45b0, 0x7e4aaa, 0x7e4eb8, 0x7e4fb8, 0x7e50d9, 0x7eab10, 0x7eb636, 0x7eb7c0, 0x825653, 0x82566e, 0x825720 };
        const uint32_t Stub = 0x15862c0;
        // Only this exe: the stub must be `xor eax,eax; ret` (31 C0 C3 / 33 C0 C3).
        if (!((Base[Stub] == 0x31 || Base[Stub] == 0x33) && Base[Stub + 1] == 0xC0 && Base[Stub + 2] == 0xC3)) { Log("SyndicateStudio: ai: not the 1.7.2 exe, AI director calls left alone"); return; }
        auto Near = StwAllocNear(Base, 0x1000);
        if (!Near) { Log("SyndicateStudio: ai: no memory near the exe for the AI director stub"); return; }
        uint8_t Jump[12] = { 0x48, 0xB8 }; auto Target = (uint64_t)&StwGetAIDirector; memcpy(Jump + 2, &Target, 8); Jump[10] = 0xFF; Jump[11] = 0xE0; // mov rax, imm64; jmp rax
        memcpy(Near, Jump, sizeof(Jump));
        int Patched = 0;
        for (auto Site : Sites) {
            auto At = Base + Site;
            if (At[0] != 0xE8) continue;
            int32_t Rel; memcpy(&Rel, At + 1, 4);
            if (At + 5 + Rel != Base + Stub) continue;
            int32_t New = (int32_t)(Near - (At + 5));
            uint8_t Call[5] = { 0xE8 }; memcpy(Call + 1, &New, 4);
            StwPatch(At, Call, 5);
            Patched++;
        }
        char L[160]; snprintf(L, sizeof(L), "SyndicateStudio: ai: %d AI director calls now find the zone's AI director", Patched); Log(L);
    }
    // The AI director spawns the husks (missions start their encounters through it: "StartMission
    // AIEncounterFromGeneratedProfile: No AI Director!"). A zone server spawned one from the game data
    // (UFortGameData::AIDirectors[AIDirectorIndex]); locally the game mode's AIDirector stayed empty.
    bool StwLooksLikeObject(UObject* O) {
        __try { return O && O->Class && O->Class->Class && O->Class->Class->Class; } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    }
    void StwSpawnAIDirector(UObject* GM) {
        char Line[300];
        if (!GM) return;
        auto DirOff = GM->GetOffset("AIDirector");
        if (DirOff == uint32(-1)) { Log("SyndicateStudio: ai: the game mode has no AIDirector"); return; }
        if (GetFromOffset<UObject*>(GM, DirOff)) { Log("SyndicateStudio: ai: the zone already has an AI director"); return; }
        UObject* Data = nullptr;
        const int32 Count = TUObjectArray::Num();
        for (int32 i = 0; i < Count && !Data; i++) {
            auto O = (UObject*)TUObjectArray::GetObjectByIndex(i);
            if (O && O->Class && !O->IsDefaultObject() && std::string(O->Class->Name.ToString().c_str()) == "FortGameData") Data = O;
        }
        if (!Data) { Log("SyndicateStudio: ai: no FortGameData"); return; }
        int32 Index = 0;
        if (auto O = Data->GetOffset("AIDirectorIndex"); O != uint32(-1)) Index = GetFromOffset<int32>(Data, O);
        if (auto O = GM->GetOffset("OverrideAIDirectorIndex"); O != uint32(-1) && GetFromOffset<int32>(GM, O) > 0) Index = GetFromOffset<int32>(GM, O);
        auto ListOff = Data->GetOffset("AIDirectors");
        if (ListOff == uint32(-1)) { Log("SyndicateStudio: ai: FortGameData has no AIDirectors"); return; }
        auto& List = GetFromOffset<TArray<uint8>>(Data, ListOff);
        uint32 ElemSize = 8, ListPropOff = 0;
        if (auto P = Data->Class ? ((const UStruct*)Data->Class) : nullptr) { uint32 Sz; StwField(P, "AIDirectors", ListPropOff, Sz); }
        snprintf(Line, sizeof(Line), "SyndicateStudio: ai: game data %s, %d AI directors, index %d", StwName(Data).c_str(), List.Num(), Index); Log(Line);
        if (List.Num() <= 0) return;
        if (Index < 0 || Index >= List.Num()) Index = 0;
        // Each entry is either a class pointer or (TAssetSubclassOf, 0x20 bytes) a soft path at +0x10.
        const UClass* Cls = nullptr;
        auto First = *(UObject**)List.GetData();
        if (StwLooksLikeObject(First)) Cls = (const UClass*)*(UObject**)(List.GetData() + (size_t)Index * 8);
        else {
            ElemSize = 0x20;
            auto& Path = *(FString*)(List.GetData() + (size_t)Index * ElemSize + 0x10);
            if (Path.Num() > 0) {
                std::string P(Path.ToString().c_str());
                snprintf(Line, sizeof(Line), "SyndicateStudio: ai: AI director class path %s", P.c_str()); Log(Line);
                std::wstring W(P.begin(), P.end());
                Cls = (const UClass*)SDK::FindObject(W.c_str(), FindClass("Object"));
            }
        }
        // Try the game data's director, the other entry, then the native base class; log each spawn step (the
        // first build got null back from the spawn).
        std::vector<const UClass*> Tries;
        if (Cls) Tries.push_back(Cls);
        if (ElemSize == 0x20) for (int32 i = 0; i < List.Num(); i++) {
            if (i == Index) continue;
            auto& Path = *(FString*)(List.GetData() + (size_t)i * 0x20 + 0x10);
            if (Path.Num() <= 0) continue;
            std::string P(Path.ToString().c_str()); std::wstring W(P.begin(), P.end());
            if (auto C = (const UClass*)SDK::FindObject(W.c_str(), FindClass("Object"))) Tries.push_back(C);
        }
        if (auto C = FindClass("FortAIDirector")) Tries.push_back(C);
        UObject* Dir = nullptr;
        for (auto C : Tries) {
            FTransform T(FVector{}, FRotator{});
            auto Begun = (UObject*)UGameplayStatics::BeginDeferredActorSpawnFromClass(UWorld::GetWorld(), C, T, 1, nullptr);
            snprintf(Line, sizeof(Line), "SyndicateStudio: ai: %s: deferred spawn %s", StwName(C).c_str(), StwName(Begun).c_str()); Log(Line);
            if (!Begun) continue;
            Dir = (UObject*)UGameplayStatics::FinishSpawningActor((AActor*)Begun, T);
            snprintf(Line, sizeof(Line), "SyndicateStudio: ai: %s: finished %s", StwName(C).c_str(), StwName(Dir).c_str()); Log(Line);
            if (!Dir && StwLooksLikeObject(Begun)) Dir = Begun;
            if (Dir) break;
        }
        snprintf(Line, sizeof(Line), "SyndicateStudio: ai: AI director %s", StwName(Dir).c_str()); Log(Line);
        if (!Dir) return;
        GetFromOffset<UObject*>(GM, DirOff) = Dir;
        StwAIDirectorPtr = Dir;
        StwRedirectAIDirectorCalls();
        if (auto GS = (UObject*)UWorld::GetWorld()->GameState) if (auto O = GS->GetOffset("AIDirector"); O != uint32(-1)) GetFromOffset<UObject*>(GS, O) = Dir;
    }
    // The husks spawned but never went for the player, and a later wave crashed the game in the goal manager
    // lookup (exe+0x2c5c40 -> null +0xc8): the AI goal manager (which picks the husks' targets) is the other
    // server-only object the shared stub hides. Spawn the game mode's AIGoalManagerClass, set it as the game
    // mode's AIGoalManager and point the goal manager's call sites (the ones whose result goes to the goal
    // manager's own code, exe+0x2ec000..0x310000) at a stub returning it.
    UObject* StwGoalManagerPtr = nullptr;
    UObject* StwGetGoalManager(UObject*) { return StwGoalManagerPtr; }
    void StwSpawnGoalManager(UObject* GM) {
        char Line[300];
        if (!GM) return;
        auto MgrOff = GM->GetOffset("AIGoalManager"), ClsOff = GM->GetOffset("AIGoalManagerClass");
        snprintf(Line, sizeof(Line), "SyndicateStudio: ai: game mode AIGoalManager +0x%x, AIGoalManagerClass +0x%x", MgrOff, ClsOff); Log(Line);
        UObject* Mgr = MgrOff != uint32(-1) ? GetFromOffset<UObject*>(GM, MgrOff) : nullptr;
        if (!Mgr) {
            const UClass* Cls = ClsOff != uint32(-1) ? GetFromOffset<const UClass*>(GM, ClsOff) : nullptr;
            if (!Cls || !StwLooksLikeObject((UObject*)Cls)) Cls = FindClass("FortAIGoalManager");
            if (!Cls) { Log("SyndicateStudio: ai: no goal manager class"); return; }
            FTransform T(FVector{}, FRotator{});
            auto Begun = (UObject*)UGameplayStatics::BeginDeferredActorSpawnFromClass(UWorld::GetWorld(), Cls, T, 1, nullptr);
            if (Begun) { Mgr = (UObject*)UGameplayStatics::FinishSpawningActor((AActor*)Begun, T); if (!Mgr) Mgr = Begun; }
            snprintf(Line, sizeof(Line), "SyndicateStudio: ai: goal manager %s spawned as %s", StwName(Cls).c_str(), StwName(Mgr).c_str()); Log(Line);
            if (!Mgr) return;
            if (MgrOff != uint32(-1)) GetFromOffset<UObject*>(GM, MgrOff) = Mgr;
        }
        StwGoalManagerPtr = Mgr;
        if (auto GS = (UObject*)UWorld::GetWorld()->GameState) if (auto O = GS->GetOffset("AIGoalManager"); O != uint32(-1)) GetFromOffset<UObject*>(GS, O) = Mgr;
        auto Base = (uint8_t*)GetModuleHandleW(nullptr);
        const uint32_t Stub = 0x15862c0;
        if (!((Base[Stub] == 0x31 || Base[Stub] == 0x33) && Base[Stub + 1] == 0xC0 && Base[Stub + 2] == 0xC3)) return;
        // 0x2bc550 / 0x2bc5a0: the encounter's assignment-settings lookup (reads +0x480 / +0x478). 0x2d433e: looks
        // an AI up in the goal manager's AI assignment map (+0x3b8). 0x31e290: reads the default settings (+0x478 /
        // +0x480) through exe+0x2f4a20.
        // 0x324a15 / 0x324a78 / 0x329c2d / 0x32ca3e / 0x32ce47 / 0x32d1e8 / 0x32da5d / 0x32e2d4: the goal query's own
        // EQS generators and tests (Fort goal / assignment generators, exe+0x324000-0x32f000). Each one gets the goal
        // manager first and skips all its work when it is null, so every husk's goal query came back with 0 items
        // and no husk ever got a goal or joined its encounter's assignment.
        static const uint32_t Sites[] = { 0x2bc550, 0x2bc5a0, 0x2d433e, 0x31e290, 0x324a15, 0x324a78, 0x329c2d, 0x32ca3e, 0x32ce47, 0x32d1e8, 0x32da5d,
            0x32e2d4, 0x2bb922, 0x2bd1fb, 0x2c5c49, 0x2ca2ad, 0x2ccb16, 0x2cd673, 0x2cee4c, 0x2d18b5, 0x2dbaf5, 0x2ff5fd,
            0x32bfce, 0x65fde1, 0x65fec1, 0x662995, 0x67da91, 0x67db11, 0x7e0c98 };
        auto Near = StwAllocNear(Base, 0x1000);
        if (!Near) return;
        uint8_t Jump[12] = { 0x48, 0xB8 }; auto Target = (uint64_t)&StwGetGoalManager; memcpy(Jump + 2, &Target, 8); Jump[10] = 0xFF; Jump[11] = 0xE0;
        memcpy(Near, Jump, sizeof(Jump));
        int Patched = 0;
        for (auto Site : Sites) {
            auto At = Base + Site;
            if (At[0] != 0xE8) continue;
            int32_t Rel; memcpy(&Rel, At + 1, 4);
            if (At + 5 + Rel != Base + Stub) continue;
            int32_t New = (int32_t)(Near - (At + 5));
            uint8_t Call[5] = { 0xE8 }; memcpy(Call + 1, &New, 4);
            StwPatch(At, Call, 5);
            Patched++;
        }
        snprintf(Line, sizeof(Line), "SyndicateStudio: ai: %d goal manager calls now find %s", Patched, StwName(Mgr).c_str()); Log(Line);
    }
    // With the goal manager in place the husks started thinking, and the game crashed when one picked its goal:
    // exe+0x2c3270 (goal settings, pawn) got a broken settings pointer from the encounter's assignment (+0x58,
    // 0x6a28a). Its callers already handle a null result (they fall back to the goal manager's defaults), so
    // return null when the settings are not a live object.
    using StwGoalSettingsFn = void*(*)(UObject*, UObject*);
    StwGoalSettingsFn StwGoalSettingsOriginal = nullptr;
    void* StwGoalSettingsDetour(UObject* Settings, UObject* Pawn) {
        if (!StwLooksLikeObject(Settings)) {
            static int Logged = 0;
            if (Logged++ < 3) Log("SyndicateStudio: ai: skipped a broken goal settings object (using the goal manager's defaults)");
            return nullptr;
        }
        return StwGoalSettingsOriginal(Settings, Pawn);
    }
    void StwHookGoalSettings() {
        static bool Done = false;
        if (Done) return;
        Done = true;
        auto Base = (uint8_t*)GetModuleHandleW(nullptr);
        const uint8_t Expect[] = { 0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x74, 0x24, 0x10, 0x57 };
        if (memcmp(Base + 0x2c3270, Expect, sizeof(Expect))) return;
        MH_Initialize();
        if (MH_CreateHook(Base + 0x2c3270, (void*)&StwGoalSettingsDetour, (void**)&StwGoalSettingsOriginal) == MH_OK && MH_EnableHook(Base + 0x2c3270) == MH_OK)
            Log("SyndicateStudio: ai: guarded the goal settings lookup");
    }
    void StwSpawnAIDirectorSafe(UObject* GM) {
        __try { StwSpawnAIDirector(GM); } __except (EXCEPTION_EXECUTE_HANDLER) { Log("SyndicateStudio: ai: crashed (caught)"); }
        __try { StwSpawnGoalManager(GM); } __except (EXCEPTION_EXECUTE_HANDLER) { Log("SyndicateStudio: ai: goal manager crashed (caught)"); }
        StwHookGoalSettings();
    }
    // A mission starts its husk waves from its generated encounter profiles (FortMission::GeneratedMissionOptions,
    // +0x678; the profiles array at +0x30 of it, 0x50 bytes each). A zone server generated them; the mission's
    // own generator (1.7.2 exe+0x684000: mission, index, bForce, request) exists in the client but the first run
    // still found none ("no generated encounter profile found for index 0"). Call it for the first profiles and
    // log what it needs (the game state's difficulty, the asset manager's data at +0x1368, the mission info).
    // The husks still did not come: the director started BaseGateEncounter_C but refused it ("improperly
    // initialized! ... does not have a valid intensity curve sequence"). An encounter gets its intensity curve,
    // EQS query, spawn groups and so on from the difficulty options picked into its generated profile (profile
    // +0x8: array of {category, option, value}, 0x18 each). When the generator picked none, pick them here:
    // the categories the mission info's profile needs (exe+0x6a2c60 copies the profile, exe+0x6a2b20 lists its
    // categories), each category's options (category vfunc 0x230), and add the first one with the game's own
    // adder (exe+0x697eb0: options, index, category, option, value).
    UObject* StwTutorialMission = nullptr;
    // Every byte property whose name has "Team" in it, for the trap team log.
    std::string StwTeamFields(UObject* O) {
        std::string Out;
        if (!O) return "-";
        for (auto C = (const UStruct*)O->Class; C; C = C->GetSuper())
            for (const UField* F = C->GetChildren(); F; F = F->GetNext()) {
                std::string N = F->GetName().ToString().c_str();
                if (N.find("Team") == std::string::npos || !F->Class) continue;
                std::string T = F->Class->Name.ToString().c_str();
                if (T != "ByteProperty" && T != "EnumProperty" && T != "IntProperty") continue;
                char B[96]; snprintf(B, sizeof(B), "%s=%d ", N.c_str(), (int)GetFromOffset<uint8_t>(O, GetFromOffset<uint32>(F, Offsets::Offset_Internal))); Out += B;
            }
        return Out.empty() ? "(none)" : Out;
    }
    // ---- Traps ----
    // Placed traps hurt the player: the trap the tool spawns carries no team (the zone server sets it from the
    // placing player), so it treats everyone as an enemy. Give each new trap the player's team: the Team byte of the
    // player's own placed building pieces (bPlayerPlaced), or the player state's TeamIndex.
    // ---- Telling the mission a trap was built ----
    // Placing all the traps didn't move the tutorial on: the mission's TrapPlacedCount stayed 0. The trap objective
    // (OBJ_TrapPlacement_C) waits for the "TrapsPlaced" mission event, which the mission raises from its
    // PlayerBuilt handler. A zone server sends "PlayerBuilt" when a player's building is placed (exe+0x1404333b0:
    // a UFortPlayerBuiltParams made with exe+0x14040ae00 in the transient package, with the building, its
    // BuildingType and the builder, then the event "PlayerBuilt" with a zero mission guid). That send is server
    // work and never happens here. Send the same event for each new trap, through FortMissionLibrary's
    // ExecuteMissionEvent, with the params object built the way the game builds it.
    void StwSendPlayerBuilt_Impl(UObject* Trap, UObject* PC) {
        char Line[300];
        auto Base = (uint8_t*)GetModuleHandleW(nullptr);
        static const uint8_t NewParams[] = { 0x48, 0x89, 0x5C, 0x24, 0x08, 0x57, 0x48, 0x83, 0xEC, 0x60 }, Transient[] = { 0x48, 0x8B, 0x05, 0x71, 0x3A, 0x2D, 0x05, 0xC3 };
        if (memcmp(Base + 0x40ae00, NewParams, sizeof(NewParams)) || memcmp(Base + 0x138c1e0, Transient, sizeof(Transient))) { Log("SyndicateStudio: trap: not the 1.7.2 exe, PlayerBuilt not sent"); return; }
        static UObject* Lib = StwFindByName("Default__FortMissionLibrary", nullptr);
        if (!Lib) { Log("SyndicateStudio: trap: no FortMissionLibrary"); return; }
        auto Pkg = ((UObject*(*)())(Base + 0x138c1e0))();
        auto Params = ((UObject*(*)(UObject*))(Base + 0x40ae00))(Pkg);
        if (!StwLooksLikeObject(Params) || std::string(Params->Class->Name.ToString().c_str()) != "FortPlayerBuiltParams") {
            snprintf(Line, sizeof(Line), "SyndicateStudio: trap: params came out as %s, PlayerBuilt not sent", StwLooksLikeObject(Params) ? StwName(Params->Class).c_str() : "null"); Log(Line); return;
        }
        auto TypeOff = Trap->GetOffset("BuildingType");
        if (auto O = Params->GetOffset("Building"); O != uint32(-1)) GetFromOffset<UObject*>(Params, O) = Trap;
        if (auto O = Params->GetOffset("BuildingType"); O != uint32(-1) && TypeOff != uint32(-1)) GetFromOffset<uint8_t>(Params, O) = GetFromOffset<uint8_t>(Trap, TypeOff);
        if (auto O = Params->GetOffset("Builder"); O != uint32(-1)) GetFromOffset<UObject*>(Params, O) = PC;
        static const FName KPlayerBuilt(L"PlayerBuilt");
        auto M = StwTutorialMission;
        auto COff = StwLooksLikeObject(M) ? M->GetOffset("TrapPlacedCount") : uint32(-1);
        const int Before = COff == uint32(-1) ? -1 : GetFromOffset<int32>(M, COff);
        StwCall C(Lib, "ExecuteMissionEvent");
        if (!C) { Log("SyndicateStudio: trap: no ExecuteMissionEvent"); return; }
        C.Set("WorldContextObject", (UObject*)UWorld::GetWorld(), 0);
        C.Set("EventType", KPlayerBuilt, 8);
        C.Set("Params", Params, 0x40);
        C.Run();
        const int After = COff == uint32(-1) ? -1 : GetFromOffset<int32>(M, COff);
        snprintf(Line, sizeof(Line), "SyndicateStudio: trap: sent PlayerBuilt for %s (type %d): mission TrapPlacedCount %d -> %d", StwName(Trap).c_str(),
            TypeOff == uint32(-1) ? -1 : (int)GetFromOffset<uint8_t>(Trap, TypeOff), Before, After); Log(Line);
    }
    void StwSendPlayerBuilt(UObject* Trap, UObject* PC) { __try { StwSendPlayerBuilt_Impl(Trap, PC); } __except (EXCEPTION_EXECUTE_HANDLER) { Log("SyndicateStudio: trap: sending PlayerBuilt crashed (caught)"); } }
    void StwFixTraps_Impl() {
        static UObject* Statics = StwFindByName("Default__GameplayStatics", nullptr);
        static const UClass* TrapCls = FindClass("BuildingTrap");
        static const UClass* BuildCls = FindClass("BuildingSMActor");
        static std::vector<UObject*> Seen;
        if (!Statics || !TrapCls) return;
        auto World = (UObject*)UWorld::GetWorld();
        StwCall C(Statics, "GetAllActorsOfClass");
        if (!C) return;
        C.Set("WorldContextObject", World, 0); C.Set("ActorClass", (UObject*)TrapCls, 8); C.Run();
        auto Arr = (TArray<UObject*>*)C.At("OutActors", 0x10);
        if (!Arr || !Arr->Num()) return;
        auto PC = StwLocalPC();
        auto PS = StwPtr(StwPtr(PC, "Pawn"), "PlayerState");
        for (int32 i = 0; i < Arr->Num(); i++) {
            auto T = (*Arr)[i];
            if (!StwLooksLikeObject(T) || std::find(Seen.begin(), Seen.end(), T) != Seen.end()) continue;
            Seen.push_back(T);
            char Line[600];
            snprintf(Line, sizeof(Line), "SyndicateStudio: trap: placed %s: %s; player state %s: %s", StwName(T).c_str(), StwTeamFields(T).c_str(), StwName(PS).c_str(), StwTeamFields(PS).c_str()); Log(Line);
            // Placing every trap didn't move the tutorial on. Show what the trap step counts: the mission's own
            // count, the trap objective and the layout requirement the mission points at for the trap tiles.
            if (auto M = StwTutorialMission; StwLooksLikeObject(M)) {
                auto COff = M->GetOffset("TrapPlacedCount");
                snprintf(Line, sizeof(Line), "SyndicateStudio: trap: mission TrapPlacedCount %d, TrapsPlaced %d, TrapCheckActive %d, PlaceTrapsTutorialActive %d",
                    COff == uint32(-1) ? -1 : GetFromOffset<int32>(M, COff), StwBool(M, "TrapsPlaced"), StwBool(M, "TrapCheckActive"), StwBool(M, "PlaceTrapsTutorialActive")); Log(Line);
                static bool Dumped = false;
                if (!Dumped) {
                    Dumped = true;
                    StwDumpObject("placed trap", T); Log("SyndicateStudio: trap: placed trap functions"); StwListFunctions(T);
                    auto Tile = StwPtr(M, "TrapMarkerTileRef");
                    StwDumpObject("trap tile requirement", Tile); Log("SyndicateStudio: trap: tile requirement functions"); StwListFunctions(Tile);
                    if (auto Obj = StwFindByName("OBJ_TrapPlacement_C_0", nullptr)) { StwDumpObject("trap objective", Obj); Log("SyndicateStudio: trap: objective functions"); StwListFunctions(Obj); }
                    // How a placed trap reaches the trap step: the objective waits for the "TrapsPlaced" mission
                    // event, and the mission has a PlayerBuilt handler (the game sends "PlayerBuilt" with
                    // UFortPlayerBuiltParams when a player's building is placed, exe+0x1404335f1). Show those
                    // handlers' parameters and the game's mission event functions, to send it the way the game does.
                    for (auto N : { "HandleMissionEvent_PlayerBuilt", "placetrap", "HandleMissionEvent_TrapsPlaced", "CreateMissionEvent", "ExecuteMissionEvent", "ExecuteMissionEventEx", "SendMissionEvent", "BlueprintCustomHandleMissionEvent" }) {
                        for (int32 k = 0, Cnt = TUObjectArray::Num(); k < Cnt; k++) {
                            auto O = (UObject*)TUObjectArray::GetObjectByIndex(k);
                            if (!O || !O->Class || std::string(O->Name.ToString().c_str()) != N || std::string(O->Class->Name.ToString().c_str()) != "Function") continue;
                            snprintf(Line, sizeof(Line), "SyndicateStudio: trap: function %s.%s", StwName(O->Outer).c_str(), N); Log(Line);
                            StwDumpLayout(N, (const UStruct*)O, 0);
                        }
                    }
                    if (auto PB = StwFindByName("FortPlayerBuiltParams", "Class")) StwDumpLayout("FortPlayerBuiltParams", (const UStruct*)PB, 0);
                    if (auto ME = StwFindByName("FortMissionEvent", "ScriptStruct")) StwDumpLayout("FortMissionEvent", (const UStruct*)ME, 0);
                }
            }
            StwSendPlayerBuilt(T, PC);
            auto TeamOff = T->GetOffset("Team");
            if (TeamOff == uint32(-1)) continue;
            int Want = -1;
            // A building piece the player placed already carries the right team.
            if (BuildCls) {
                StwCall B(Statics, "GetAllActorsOfClass");
                B.Set("WorldContextObject", World, 0); B.Set("ActorClass", (UObject*)BuildCls, 8); B.Run();
                if (auto Bs = (TArray<UObject*>*)B.At("OutActors", 0x10))
                    for (int32 k = 0; k < Bs->Num() && Want < 0; k++) {
                        auto Piece = (*Bs)[k];
                        if (!StwLooksLikeObject(Piece) || Piece == T || StwBool(Piece, "bPlayerPlaced") != 1) continue;
                        if (auto O = Piece->GetOffset("Team"); O != uint32(-1)) Want = GetFromOffset<uint8_t>(Piece, O);
                    }
            }
            if (Want < 0 && PS) if (auto O = PS->GetOffset("TeamIndex"); O != uint32(-1)) Want = GetFromOffset<uint8_t>(PS, O);
            if (Want < 0) continue;
            GetFromOffset<uint8_t>(T, TeamOff) = (uint8_t)Want;
            StwCallNoParms(T, "OnRep_Team");
            snprintf(Line, sizeof(Line), "SyndicateStudio: trap: %s team set to %d", StwName(T).c_str(), Want); Log(Line);
        }
    }
    void StwFixTraps() { __try { StwFixTraps_Impl(); } __except (EXCEPTION_EXECUTE_HANDLER) { Log("SyndicateStudio: trap: team fix crashed (caught)"); } }
    // ---- The highlighted trap tiles ----
    // At the trap step the mission sets HighlightTraps (a RepNotify bool) and the fort's BP_TrapHighlight actors
    // are meant to light up the tiles to place traps on. The last run had HighlightTraps = 1 and seven
    // BP_TrapHighlight actors in the zone, yet nothing showed: OnRep_HighlightTraps is what shows them, and a
    // RepNotify only runs where the value arrives by replication (on the clients of a zone server); this zone has
    // no server sending it, so the notify never ran. Run OnRep_HighlightTraps whenever HighlightTraps changes, the
    // way a client would on receiving it, then check the highlight actors: if they are still hidden while the
    // mission wants them shown (or shown once it is done with them), set their hidden flag to match.
    void StwTrapHighlights_Impl() {
        auto M = StwTutorialMission;
        if (!StwLooksLikeObject(M)) return;
        const int Want = StwBool(M, "HighlightTraps");
        if (Want < 0) return;
        static int Last = 0;
        static ULONGLONG CheckAt = 0;
        static bool WeShowed = false;
        char Line[300];
        auto Now = GetTickCount64();
        if (Want != Last) {
            Last = Want;
            // OnRep_HighlightTraps itself is run by the RepNotify watch (StwRepNotifies).
            snprintf(Line, sizeof(Line), "SyndicateStudio: trap: HighlightTraps is now %d", Want); Log(Line);
            CheckAt = Now + 600;
        }
        if (!CheckAt || Now < CheckAt) return;
        CheckAt = 0;
        static UObject* Statics = StwFindByName("Default__GameplayStatics", nullptr);
        static const UClass* HiCls = FindClass("BP_TrapHighlight_C");
        if (!HiCls) HiCls = FindClass("BP_TrapHighlight_C");
        if (!Statics || !HiCls) { Log("SyndicateStudio: trap: no BP_TrapHighlight class loaded"); return; }
        StwCall C(Statics, "GetAllActorsOfClass");
        if (!C) return;
        C.Set("WorldContextObject", (UObject*)UWorld::GetWorld(), 0); C.Set("ActorClass", (UObject*)HiCls, 8); C.Run();
        auto Arr = (TArray<UObject*>*)C.At("OutActors", 0x10);
        if (!Arr) return;
        static bool Dumped = false;
        int Hidden = 0, Fixed = 0;
        for (int32 i = 0; i < Arr->Num(); i++) {
            auto H = (*Arr)[i];
            if (!StwLooksLikeObject(H)) continue;
            if (!Dumped) { Dumped = true; StwDumpObject("trap highlight", H); Log("SyndicateStudio: trap: highlight functions"); StwListFunctions(H); }
            const int IsHidden = StwBool(H, "bHidden");
            if (IsHidden == 1) Hidden++;
            const bool Fix = (Want == 1 && IsHidden == 1) || (Want == 0 && IsHidden == 0 && WeShowed);
            if (Fix) { StwCall S(H, "SetActorHiddenInGame"); if (S) { S.Set<uint8_t>("bNewHidden", Want ? 0 : 1, 0); S.Run(); Fixed++; } }
        }
        if (Want == 1 && Fixed) WeShowed = true;
        if (Want == 0) WeShowed = false;
        snprintf(Line, sizeof(Line), "SyndicateStudio: trap: %d trap highlights, %d were hidden, %d %s", Arr->Num(), Hidden, Fixed, Want ? "shown" : "hidden again"); Log(Line);
    }
    void StwTrapHighlights() { __try { StwTrapHighlights_Impl(); } __except (EXCEPTION_EXECUTE_HANDLER) { Log("SyndicateStudio: trap: highlight check crashed (caught)"); } }
    // ---- RepNotifies in the tutorial's blueprints ----
    // The trap tiles stayed dark and, once the traps were placed, the laser barrier stayed up: the tutorial's
    // blueprints (the mission, the trap highlights, the laser walls, markers) change what you see in OnRep_ functions,
    // and an OnRep only runs on a client receiving the value from a server. Nothing sends them here, so every visual
    // step driven by a replicated value never happened. Do what a client does: for every live object of a blueprint
    // class, watch each blueprint-declared OnRep_X that takes no parameters and its property X; when X changes, run
    // OnRep_X. When an object is first seen and X already differs from the class default, run it once, as a client
    // does on the object's first replication.
    struct StwRepWatch { const char* Func; std::string FuncName; uint32 Off; uint32 Size; uint8_t Mask; };
    std::map<const UObject*, std::vector<StwRepWatch>> StwRepClasses;
    std::map<UObject*, std::vector<std::string>> StwRepLast;
    std::vector<UObject*> StwRepObjects;
    const std::vector<StwRepWatch>& StwRepWatches(const UObject* Cls) {
        auto It = StwRepClasses.find(Cls);
        if (It != StwRepClasses.end()) return It->second;
        auto& W = StwRepClasses[Cls];
        for (auto C = (const UStruct*)Cls; C; C = C->GetSuper()) {
            auto CC = ((const UObject*)C)->Class;
            if (!CC || std::string(CC->Name.ToString().c_str()) != "BlueprintGeneratedClass") break;
            for (const UField* F = C->GetChildren(); F; F = F->GetNext()) {
                if (!F->Class || std::string(F->Class->Name.ToString().c_str()) != "Function") continue;
                std::string N = F->GetName().ToString().c_str();
                if (N.rfind("OnRep_", 0) != 0) continue;
                bool HasParms = false; // locals are fine, parameters (CPF_Parm) are not
                for (const UField* Q = ((const UStruct*)F)->GetChildren(); Q; Q = Q->GetNext()) if (GetFromOffset<uint64>(Q, Offsets::PropertyFlags) & 0x80) HasParms = true;
                if (HasParms) continue;
                std::string Prop = N.substr(6);
                for (auto P = (const UStruct*)Cls; P; P = P->GetSuper()) {
                    const UField* Found = nullptr;
                    for (const UField* Q = P->GetChildren(); Q; Q = Q->GetNext())
                        if (Q->Class && std::string(Q->Class->Name.ToString().c_str()) != "Function" && std::string(Q->GetName().ToString().c_str()) == Prop) { Found = Q; break; }
                    if (!Found) continue;
                    uint32 Off = GetFromOffset<uint32>(Found, Offsets::Offset_Internal), Size = GetFromOffset<uint32>(Found, Offsets::ElementSize);
                    uint8_t Mask = std::string(Found->Class->Name.ToString().c_str()) == "BoolProperty" ? Found->GetFieldMask() : 0;
                    if (Size && Size <= 64) W.push_back({ nullptr, N, Off, Size, Mask });
                    break;
                }
            }
        }
        return W;
    }
    std::string StwRepValue(UObject* O, const StwRepWatch& W) {
        auto P = (const char*)O + W.Off;
        if (W.Mask) return std::string(1, (char)((*(const uint8_t*)P & W.Mask) ? 1 : 0));
        return std::string(P, W.Size);
    }
    void StwRepNotifies_Impl() {
        static ULONGLONG ScanNext = 0;
        auto Now = GetTickCount64();
        char Line[300];
        // The object scan used to walk every UObject at once and turn each class's class name into a string: a
        // frame drop to ~1 fps every 5 s. It now compares class pointers only and walks 20000 objects per call, so a
        // full pass is spread over several seconds of calls and no single frame pays for it.
        (void)ScanNext;
        static const UClass* BPGC = FindClass("BlueprintGeneratedClass");
        static int32 Cursor = 0;
        static std::vector<UObject*> Building;
        if (BPGC) {
            const int32 N = TUObjectArray::Num();
            const int32 End = (std::min)(N, Cursor + 20000);
            for (int32 i = Cursor; i < End; i++) {
                auto O = (UObject*)TUObjectArray::GetObjectByIndex(i);
                if (!O || !O->Class || ((const UObject*)O->Class)->Class != BPGC || O->IsDefaultObject()) continue;
                if (StwRepWatches(O->Class).empty()) continue;
                Building.push_back(O);
            }
            Cursor = End;
            if (Cursor >= N) { Cursor = 0; StwRepObjects.swap(Building); Building.clear(); }
        }
        static int Logged = 0;
        for (auto O : StwRepObjects) {
            if (TUObjectArray::GetObjectByIndex(O->Index) != O) continue;
            auto& Ws = StwRepWatches(O->Class);
            auto It = StwRepLast.find(O);
            const bool First = It == StwRepLast.end();
            auto& Last = StwRepLast[O];
            if (First) Last.resize(Ws.size());
            auto Def = (UObject*)O->Class->GetDefaultObj();
            for (size_t k = 0; k < Ws.size(); k++) {
                auto V = StwRepValue(O, Ws[k]);
                bool Run = false;
                if (First) Run = Def && V != StwRepValue(Def, Ws[k]);
                else Run = V != Last[k];
                Last[k] = V;
                if (!Run) continue;
                StwCall C(O, Ws[k].FuncName.c_str());
                if (C) C.Run();
                static std::map<std::pair<UObject*, size_t>, int> Per;
                if (Logged++ < 2000 && Per[{ O, k }]++ < 3) { snprintf(Line, sizeof(Line), "SyndicateStudio: rep: ran %s on %s", Ws[k].FuncName.c_str(), StwName(O).c_str()); Log(Line); }
            }
        }
    }
    void StwRepNotifies() { __try { StwRepNotifies_Impl(); } __except (EXCEPTION_EXECUTE_HANDLER) { Log("SyndicateStudio: rep: notify check crashed (caught)"); } }
    // ---- Where the tutorial stands ----
    // After the traps the laser barrier stayed up. Log each mission objective's status whenever it changes (and
    // which are visible), and, once, every actor whose class looks like a laser wall or blocker with its state, so
    // the step that should open the barrier can be found.
    void StwWatchObjectives_Impl() {
        static UObject* Statics = StwFindByName("Default__GameplayStatics", nullptr);
        static const UClass* ObjCls = FindClass("FortObjectiveBase");
        static std::map<UObject*, int> Last;
        static bool BarriersShown = false;
        char Line[400];
        if (!Statics || !ObjCls) return;
        StwCall C(Statics, "GetAllActorsOfClass");
        if (!C) return;
        C.Set("WorldContextObject", (UObject*)UWorld::GetWorld(), 0); C.Set("ActorClass", (UObject*)ObjCls, 8); C.Run();
        auto Arr = (TArray<UObject*>*)C.At("OutActors", 0x10);
        if (!Arr) return;
        for (int32 i = 0; i < Arr->Num(); i++) {
            auto O = (*Arr)[i];
            if (!StwLooksLikeObject(O)) continue;
            auto SOff = O->GetOffset("ObjectiveStatus");
            const int St = SOff == uint32(-1) ? -1 : GetFromOffset<uint8_t>(O, SOff);
            auto It = Last.find(O);
            if (It != Last.end() && It->second == St) continue;
            Last[O] = St;
            snprintf(Line, sizeof(Line), "SyndicateStudio: objective: %s status %d, visible %d", StwName(O).c_str(), St, StwBool(O, "bIsObjectiveVisible")); Log(Line);
        }
        if (!BarriersShown && StwLooksLikeObject(StwTutorialMission) && StwBool(StwTutorialMission, "TrapsPlaced") == 1) {
            BarriersShown = true;
            int Shown = 0;
            for (int32 i = 0, N = TUObjectArray::Num(); i < N && Shown < 20; i++) {
                auto O = (UObject*)TUObjectArray::GetObjectByIndex(i);
                if (!O || !O->Class || O->IsDefaultObject()) continue;
                std::string CN = O->Class->Name.ToString().c_str();
                if (CN.find("Laser") == std::string::npos && CN.find("Blocker") == std::string::npos && CN.find("Barrier") == std::string::npos && CN.find("ForceField") == std::string::npos) continue;
                if (CN.find("Class") != std::string::npos || CN.find("Function") != std::string::npos || CN.find("Component") != std::string::npos) continue;
                Shown++;
                snprintf(Line, sizeof(Line), "SyndicateStudio: objective: barrier actor %s (%s) hidden %d", StwName(O).c_str(), CN.c_str(), StwBool(O, "bHidden")); Log(Line);
                if (CN.find("Laser") != std::string::npos) { StwDumpObject("laser barrier", O); Log("SyndicateStudio: objective: laser barrier functions"); StwListFunctions(O); }
            }
            if (!Shown) Log("SyndicateStudio: objective: no laser or blocker actors found");
            // The mission's own laser wall functions and what they take.
            for (int32 k = 0, Cnt = TUObjectArray::Num(); k < Cnt; k++) {
                auto O = (UObject*)TUObjectArray::GetObjectByIndex(k);
                if (!O || !O->Class || O->Outer != (UObject*)StwTutorialMission->Class || std::string(O->Class->Name.ToString().c_str()) != "Function") continue;
                std::string N = O->Name.ToString().c_str();
                if (N.find("Laser") == std::string::npos && N.find("ControlRoom") == std::string::npos && N.find("OBJ_8") == std::string::npos) continue;
                snprintf(Line, sizeof(Line), "SyndicateStudio: objective: mission function %s", N.c_str()); Log(Line);
                StwDumpLayout(N.c_str(), (const UStruct*)O, 0);
            }
        }
    }
    // ---- The laser wall after the trap step ----
    // Once the trap step completes (OBJ_7_Complete) the mission activates OBJ_8 "go to mission control", but the
    // laser beams across the corridor stay on. The mission blueprint has its own DisableLaserWall / DisableLaserWalls
    // functions; nothing in this zone ran them. Two seconds after the trap step completes, if a laser wall is still
    // there, run the mission's DisableLaserWalls (or DisableLaserWall) when it takes no parameters, and log every
    // laser wall's state before and after so the change can be checked against what the tutorial does.
    void StwOpenLaserWalls_Impl() {
        static int Stage = 0;
        static ULONGLONG At = 0;
        auto M = StwTutorialMission;
        if (Stage >= 2 || !StwLooksLikeObject(M)) return;
        static UObject* TrapObj = StwFindByName("OBJ_TrapPlacement_C_0", nullptr);
        if (!StwLooksLikeObject(TrapObj)) return;
        auto TOff = TrapObj->GetOffset("TrapsPlaced"), ROff = TrapObj->GetOffset("RequiredTrapsToPlace");
        if (TOff == uint32(-1) || ROff == uint32(-1) || GetFromOffset<int32>(TrapObj, TOff) < GetFromOffset<int32>(TrapObj, ROff)) return;
        auto Now = GetTickCount64();
        if (!At) { At = Now + 2000; return; }
        if (Now < At) return;
        static UObject* Statics = StwFindByName("Default__GameplayStatics", nullptr);
        std::vector<UObject*> Walls;
        for (auto Name : { "BP_LaserWallPrelaunch_C", "BP_LaserWall_C", "BP_LaserWindow_C" }) {
            auto Cls = FindClass(Name);
            if (!Cls || !Statics) continue;
            StwCall C(Statics, "GetAllActorsOfClass");
            if (!C) continue;
            C.Set("WorldContextObject", (UObject*)UWorld::GetWorld(), 0); C.Set("ActorClass", (UObject*)Cls, 8); C.Run();
            if (auto A = (TArray<UObject*>*)C.At("OutActors", 0x10)) for (int32 i = 0; i < A->Num(); i++) Walls.push_back((*A)[i]);
        }
        char Line[300];
        if (Stage == 0) {
            Stage = 1;
            for (auto W : Walls) { StwDumpObject("laser wall before", W); Log("SyndicateStudio: laser: functions"); StwListFunctions(W); }
            const char* Ran = nullptr;
            for (auto Fn : { "DisableLaserWalls", "DisableLaserWall" }) {
                auto FO = M->GetFunction(Fn);
                if (!FO) continue;
                bool Parms = false;
                for (const UField* Q = ((const UStruct*)FO)->GetChildren(); Q; Q = Q->GetNext()) if (GetFromOffset<uint64>(Q, Offsets::PropertyFlags) & 0x80) Parms = true;
                if (Parms) { snprintf(Line, sizeof(Line), "SyndicateStudio: laser: %s takes parameters, not run", Fn); Log(Line); continue; }
                alignas(16) uint8_t Buf[256]{};
                M->ProcessEvent(FO, Buf); Ran = Fn; break;
            }
            snprintf(Line, sizeof(Line), "SyndicateStudio: laser: trap step done, %d laser walls; ran %s", (int)Walls.size(), Ran ? Ran : "nothing"); Log(Line);
            At = Now + 1500;
            return;
        }
        Stage = 2;
        for (auto W : Walls) StwDumpObject("laser wall after", W);
    }
    void StwOpenLaserWalls() { __try { StwOpenLaserWalls_Impl(); } __except (EXCEPTION_EXECUTE_HANDLER) { Log("SyndicateStudio: laser: crashed (caught)"); } }
    void StwWatchObjectives() { __try { StwWatchObjectives_Impl(); } __except (EXCEPTION_EXECUTE_HANDLER) { Log("SyndicateStudio: objective: watch crashed (caught)"); } }
    // What the tutorial's trap step reads: the mission's trap fields and functions, logged when the trap is first
    // equipped (the highlighted tiles never show).
    void StwDumpMissionTraps() {
        auto M = StwTutorialMission;
        if (!StwLooksLikeObject(M)) { Log("SyndicateStudio: trap: no tutorial mission to inspect"); return; }
        char Line[300];
        for (auto C = (const UStruct*)M->Class; C; C = C->GetSuper()) {
            for (const UField* F = C->GetChildren(); F; F = F->GetNext()) {
                std::string N = F->GetName().ToString().c_str();
                if (N.find("Trap") == std::string::npos && N.find("Tile") == std::string::npos && N.find("Marker") == std::string::npos && N.find("Hint") == std::string::npos) continue;
                std::string T = F->Class ? F->Class->Name.ToString().c_str() : "?";
                uint32 Off = GetFromOffset<uint32>(F, Offsets::Offset_Internal);
                if (T == "Function") { snprintf(Line, sizeof(Line), "SyndicateStudio: trap: mission function %s", N.c_str()); Log(Line); continue; }
                if (T == "ObjectProperty") snprintf(Line, sizeof(Line), "SyndicateStudio: trap: mission %s = %s", N.c_str(), StwName(GetFromOffset<UObject*>(M, Off)).c_str());
                else if (T == "ArrayProperty") snprintf(Line, sizeof(Line), "SyndicateStudio: trap: mission %s = [%d]", N.c_str(), GetFromOffset<TArray<uint8_t>>(M, Off).Num());
                else snprintf(Line, sizeof(Line), "SyndicateStudio: trap: mission %s (%s) = %d", N.c_str(), T.c_str(), (int)GetFromOffset<uint8_t>(M, Off));
                Log(Line);
            }
            if (std::string(((const UField*)C)->GetName().ToString().c_str()) == "FortMission") break;
        }
        // Actors placed in the zone for the trap step (markers, hints, highlight tiles).
        const int32 Count = TUObjectArray::Num();
        int Shown = 0;
        for (int32 i = 0; i < Count && Shown < 30; i++) {
            auto O = (UObject*)TUObjectArray::GetObjectByIndex(i);
            if (!O || !O->Class || O->IsDefaultObject()) continue;
            std::string CN = O->Class->Name.ToString().c_str();
            if (CN.find("Trap") == std::string::npos && CN.find("Marker") == std::string::npos && CN.find("Hint") == std::string::npos) continue;
            if (CN.find("Item") != std::string::npos || CN.find("Function") != std::string::npos || CN == "Class" || CN.find("BlueprintGeneratedClass") != std::string::npos) continue;
            snprintf(Line, sizeof(Line), "SyndicateStudio: trap: zone actor %s (%s)", StwName(O).c_str(), CN.c_str()); Log(Line);
            Shown++;
        }
    }
    int32 StwNextProfile = 0;
    struct StwRawArray { UObject** Data; int32 Num; int32 Max; };
    void StwForceEncounterOptions(UObject* Mission, int32 Index) {
        char Line[400];
        auto Base = (uint8_t*)GetModuleHandleW(nullptr);
        const uint8_t ExpCopy[] = { 0x48, 0x89, 0x5C, 0x24, 0x08, 0x57, 0x48, 0x83, 0xEC, 0x20 };
        const uint8_t ExpList[] = { 0x48, 0x89, 0x5C, 0x24, 0x10, 0x48, 0x89, 0x6C, 0x24, 0x18 };
        const uint8_t ExpAdd[] = { 0x85, 0xD2, 0x0F, 0x88 };
        if (memcmp(Base + 0x6a2c60, ExpCopy, sizeof(ExpCopy)) || memcmp(Base + 0x6a2b20, ExpList, sizeof(ExpList)) || memcmp(Base + 0x697eb0, ExpAdd, sizeof(ExpAdd))) {
            Log("SyndicateStudio: options: not the 1.7.2 exe"); return;
        }
        auto CopyProfile = (bool(*)(UObject*, int32, void*))(Base + 0x6a2c60);
        auto ListCategories = (void*(*)(void*, StwRawArray*, void*))(Base + 0x6a2b20);
        auto AddOption = (void(*)(void*, int32, UObject*, UObject*, float))(Base + 0x697eb0);
        auto Info = *(UObject**)((uint8_t*)Mission + 0x3c8);
        if (!Info) return;
        auto Opts = (uint8_t*)Mission + 0x678;
        auto& Profiles = *(TArray<uint8>*)(Opts + 0x30);
        if (Index < 0 || Index >= Profiles.Num()) return;
        alignas(16) static uint8_t Local[0x200]; memset(Local, 0, sizeof(Local));
        alignas(16) static uint8_t Request[0x400]{};
        if (!CopyProfile(Info, Index, Local)) { snprintf(Line, sizeof(Line), "SyndicateStudio: options: profile %d: the mission info has none", Index); Log(Line); return; }
        StwRawArray Cats{};
        ListCategories(Local, &Cats, Request);
        auto P = Profiles.GetData() + (size_t)Index * 0x50;
        auto Picked = (uint8_t*)*(void**)(P + 0x8);
        int32 PickedNum = *(int32*)(P + 0x10);
        snprintf(Line, sizeof(Line), "SyndicateStudio: options: profile %d: %d categories, %d options already picked", Index, Cats.Num, PickedNum); Log(Line);
        for (int32 c = 0; c < Cats.Num && c < 64; c++) {
            auto Cat = Cats.Data[c];
            if (!StwLooksLikeObject(Cat)) continue;
            bool Have = false;
            for (int32 k = 0; k < PickedNum; k++) if (*(UObject**)(Picked + (size_t)k * 0x18) == Cat) Have = true;
            StwRawArray Options{};
            using GetOptionsFn = void(*)(UObject*, StwRawArray*);
            (*(GetOptionsFn*)(*(uint8_t**)Cat + 0x230))(Cat, &Options);
            std::string Names;
            for (int32 o = 0; o < Options.Num && o < 6; o++) { Names += StwName(Options.Data[o]); Names += " "; }
            snprintf(Line, sizeof(Line), "SyndicateStudio: options:   %s (%s): %d options%s: %s", StwName(Cat).c_str(), Cat->Class ? Cat->Class->Name.ToString().c_str() : "?",
                Options.Num, Have ? ", already picked" : "", Names.c_str()); Log(Line);
            if (Have || Options.Num <= 0 || !StwLooksLikeObject(Options.Data[0])) continue;
            AddOption(Opts, Index, Cat, Options.Data[0], 1.0f);
            Picked = (uint8_t*)*(void**)(P + 0x8); PickedNum = *(int32*)(P + 0x10);
        }
        snprintf(Line, sizeof(Line), "SyndicateStudio: options: profile %d now has %d options", Index, PickedNum); Log(Line);
    }
    void StwGenerateEncounters_Impl(UObject* Mission) {
        char Line[300];
        auto Base = (uint8_t*)GetModuleHandleW(nullptr);
        auto Gen = (void(*)(UObject*, int32, bool, void*))(Base + 0x684000);
        // Only this exe: the function starts with mov [rsp+10h],rbx; mov [rsp+18h],rbp.
        const uint8_t Expect[] = { 0x48, 0x89, 0x5C, 0x24, 0x10, 0x48, 0x89, 0x6C, 0x24, 0x18 };
        if (memcmp(Base + 0x684000, Expect, sizeof(Expect))) { Log("SyndicateStudio: encounters: not the 1.7.2 exe"); return; }
        auto Opts = (uint8_t*)Mission + 0x678;
        StwTutorialMission = Mission;
        auto World = UWorld::GetWorld();
        auto GS = (UObject*)World->GameState;
        snprintf(Line, sizeof(Line), "SyndicateStudio: encounters: game state %s difficulty %.2f, mission info %s, profiles before %d",
            StwName(GS).c_str(), GS ? *(float*)((uint8_t*)GS + 0xfcc) : -1.f, StwName(*(UObject**)((uint8_t*)Mission + 0x3c8)).c_str(), *(int32*)(Opts + 0x38)); Log(Line);
        if (auto Info = *(UObject**)((uint8_t*)Mission + 0x3c8)) {
            snprintf(Line, sizeof(Line), "SyndicateStudio: encounters: mission info has %d encounter profiles, %d encounter sequences",
                *(int32*)((uint8_t*)Info + 0x708 + 8), *(int32*)((uint8_t*)Info + 0x718 + 8)); Log(Line);
        }
        // The generator fills existing profile slots (one per encounter profile of the mission info, 0x50 bytes,
        // the slot's index at +0x4c); a zone server sized the array when it generated the mission. Size it here.
        int32 Wanted = 0;
        if (auto Info = *(UObject**)((uint8_t*)Mission + 0x3c8)) Wanted = *(int32*)((uint8_t*)Info + 0x708 + 8);
        auto& Profiles = *(TArray<uint8>*)(Opts + 0x30);
        if (Profiles.Num() == 0 && Wanted > 0 && Wanted < 64) {
            alignas(16) uint8_t Slot[0x50]{};
            for (int32 i = 0; i < Wanted; i++) { memset(Slot, 0, sizeof(Slot)); *(int32*)(Slot + 0x4c) = i; Profiles.Add(*Slot, 0x50); }
            snprintf(Line, sizeof(Line), "SyndicateStudio: encounters: made %d profile slots", Profiles.Num()); Log(Line);
        }
        alignas(16) static uint8_t Request[0x400]{};
        for (int32 Index = 0; Index < Wanted && Index < 64; Index++) {
            snprintf(Line, sizeof(Line), "SyndicateStudio: encounters: generating %d", Index); Log(Line);
            Gen(Mission, Index, true, Request);
            auto P = Profiles.GetData() + (size_t)Index * 0x50;
            snprintf(Line, sizeof(Line), "SyndicateStudio: encounters: after generating %d: %d profiles, difficulty %.2f, points %d-%d, %d options", Index, Profiles.Num(),
                *(float*)P, *(int32*)(P + 0x40), *(int32*)(P + 0x44), *(int32*)(P + 0x10)); Log(Line);
            StwForceEncounterOptions(Mission, Index);
        }
    }
    // Logs where a caught crash happened (exe offset, the address it touched), so the next build can fix it.
    int StwCrashFilter(EXCEPTION_POINTERS* E, const char* What) {
        auto Base = (uintptr_t)GetModuleHandleW(nullptr);
        auto At = (uintptr_t)E->ExceptionRecord->ExceptionAddress;
        char L[260];
        snprintf(L, sizeof(L), "SyndicateStudio: %s crashed: code 0x%08lx at %s+0x%llx, touching %p", What, E->ExceptionRecord->ExceptionCode,
            At >= Base && At < Base + 0x8000000 ? "exe" : "abs", (unsigned long long)(At >= Base && At < Base + 0x8000000 ? At - Base : At),
            E->ExceptionRecord->NumberParameters > 1 ? (void*)E->ExceptionRecord->ExceptionInformation[1] : nullptr);
        Log(L);
        HMODULE Mod = nullptr; char Name[MAX_PATH] = "?";
        if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCSTR)At, &Mod) && Mod) {
            GetModuleFileNameA(Mod, Name, MAX_PATH);
            snprintf(L, sizeof(L), "SyndicateStudio:   in %s +0x%llx", Name, (unsigned long long)(At - (uintptr_t)Mod)); Log(L);
        }
        // Return addresses into the exe on the stack (the call chain).
        auto Sp = (uintptr_t*)E->ContextRecord->Rsp;
        int Shown = 0;
        for (int i = 0; i < 256 && Shown < 12; i++) {
            uintptr_t V = 0;
            __try { V = Sp[i]; } __except (EXCEPTION_EXECUTE_HANDLER) { break; }
            if (V > Base + 0x1000 && V < Base + 0x3F00000) { snprintf(L, sizeof(L), "SyndicateStudio:   stack exe+0x%llx", (unsigned long long)(V - Base)); Log(L); Shown++; }
        }
        return EXCEPTION_EXECUTE_HANDLER;
    }
    void StwGenerateEncounters(UObject* Mission) { __try { StwGenerateEncounters_Impl(Mission); } __except (StwCrashFilter(GetExceptionInformation(), "encounters")) {} }
    bool StwMissionMade = false;
    // The encounter still came up without its settings. Watch the director's StartEncounter (exe+0x2d8b90:
    // director, encounter class, settings): the settings carry the generated profile at +0xd0 (options array at
    // +0xd8, count +0xe0). Log which path started it and how many options arrived; when none did (the mission's
    // StartMissionAIEncounter path, which passes no profile), copy in the mission's next generated profile with
    // the game's own copy (exe+0x2b3bb0: dest, source).
    using StwStartEncounterFn = UObject*(*)(UObject*, UObject*, uint8_t*);
    StwStartEncounterFn StwStartEncounterOriginal = nullptr;
    void StwStartEncounterFix(UObject* Dir, UObject* Cls, uint8_t* Settings, void* Ret) {
        char Line[300];
        auto Base = (uint8_t*)GetModuleHandleW(nullptr);
        int32 Count = *(int32*)(Settings + 0xe0);
        snprintf(Line, sizeof(Line), "SyndicateStudio: encounter: start %s from exe+0x%llx, profile difficulty %.2f, %d options",
            StwName(Cls).c_str(), (unsigned long long)((uint8_t*)Ret - Base), *(float*)(Settings + 0xd0), Count); Log(Line);
        if (Count > 0 || !StwTutorialMission) return;
        auto& Profiles = *(TArray<uint8>*)((uint8_t*)StwTutorialMission + 0x678 + 0x30);
        if (StwNextProfile >= Profiles.Num()) return;
        auto Copy = (void*(*)(uint8_t*, uint8_t*))(Base + 0x2b3bb0);
        Copy(Settings + 0xd0, Profiles.GetData() + (size_t)StwNextProfile * 0x50);
        snprintf(Line, sizeof(Line), "SyndicateStudio: encounter:   gave it generated profile %d (%d options)", StwNextProfile, *(int32*)(Settings + 0xe0)); Log(Line);
        StwNextProfile++;
    }
    void StwSanitizeEncounters();
    void StwLogStartedEncounter_Impl(UObject* Enc) {
        char Line[200]; snprintf(Line, sizeof(Line), "SyndicateStudio: encounter:   -> %s", StwName(Enc).c_str()); Log(Line);
        if (Enc) StwDumpObject("encounter", Enc);
    }
    void StwLogStartedEncounter(UObject* Enc) { __try { StwLogStartedEncounter_Impl(Enc); } __except (EXCEPTION_EXECUTE_HANDLER) {} }
    UObject* StwStartEncounterDetour(UObject* Dir, UObject* Cls, uint8_t* Settings) {
        __try { StwStartEncounterFix(Dir, Cls, Settings, _ReturnAddress()); } __except (StwCrashFilter(GetExceptionInformation(), "start encounter")) {}
        auto Enc = StwStartEncounterOriginal(Dir, Cls, Settings);
        StwLogStartedEncounter(Enc);
        StwAIWatchUntil = GetTickCount64() + 60000; StwAINext = GetTickCount64() + 5000;
        if (Enc) StwStartedEncounters.push_back(Enc);
        StwSanitizeFrom = GetTickCount64(); StwSanitizeNext = 0;
        StwSanitizeEncounters();
        return Enc;
    }
    // The encounter got its options but was still refused: its current intensity curve sequence (+0x58, checked by
    // exe+0x2e11c0 when the pacing byte +0x9f8 is 1; +0x48 when it is 0) stays empty. It should come from its
    // IntensitySequenceProgression (+0x1b0). Before the check, log what is there and the loaded sequence objects,
    // and fill an empty slot with a loaded sequence.
    using StwCheckEncounterFn = bool(*)(UObject*);
    StwCheckEncounterFn StwCheckEncounterOriginal = nullptr;
    void StwCheckEncounterFix(UObject* Enc) {
        char Line[400];
        auto E = (uint8_t*)Enc;
        auto Seq48 = *(UObject**)(E + 0x48), Seq58 = *(UObject**)(E + 0x58);
        auto Prog = *(UObject**)(E + 0x1b0);
        snprintf(Line, sizeof(Line), "SyndicateStudio: encounter check %s: pacing %d, +0x48 %s, +0x58 %s, progression %s",
            StwName(Enc).c_str(), E[0x9f8], StwName(Seq48).c_str(), StwName(Seq58).c_str(), StwName(Prog).c_str()); Log(Line);
        static bool Dumped = false;
        if (!Dumped && Prog) { Dumped = true; StwDumpObject("intensity progression", Prog); StwDumpLayout("intensity progression", (const UStruct*)Prog->Class, 0); }
        if ((E[0x9f8] == 1 && Seq58) || (E[0x9f8] == 0 && Seq48) || E[0x9f8] > 1) return;
        UObject* Found = nullptr;
        const int32 Count = TUObjectArray::Num();
        int Logged = 0;
        for (int32 i = 0; i < Count; i++) {
            auto O = (UObject*)TUObjectArray::GetObjectByIndex(i);
            if (!O || !O->Class) continue;
            std::string C(O->Class->Name.ToString().c_str());
            if (C != "FortIntensityCurveSequence") continue;
            std::string N(O->Name.ToString().c_str());
            if (N.rfind("Default__", 0) == 0) continue;
            if (Logged++ < 12) { snprintf(Line, sizeof(Line), "SyndicateStudio: encounter check:   loaded %s (%s)", N.c_str(), C.c_str()); Log(Line); }
            if (!Found) Found = O;
        }
        // Prefer the progression's own first sequence (IntensityCurveSequences at +0x28, 0x18 each, sequence first).
        if (Prog) {
            auto& Seqs = *(TArray<uint8>*)((uint8_t*)Prog + 0x28);
            for (int32 k = 0; k < Seqs.Num() && k < 16; k++) if (auto Q = *(UObject**)(Seqs.GetData() + (size_t)k * 0x18)) { Found = Q; break; }
        }
        if (!Found) { Log("SyndicateStudio: encounter check:   no intensity curve sequence is loaded"); return; }
        // Pacing 0 wants a spawn-points-percentage sequence at +0x48 (an intensity sequence there is the wrong
        // row type for AIDirIntensityPacing). Use a loaded one if there is one; otherwise switch the encounter to
        // intensity-curve pacing (1), which takes the intensity sequence at +0x58.
        if (E[0x9f8] == 0 && !Seq48) {
            UObject* Spawn = nullptr;
            for (int32 i = 0; i < Count && !Spawn; i++) {
                auto O = (UObject*)TUObjectArray::GetObjectByIndex(i);
                if (!O || !O->Class || O->IsDefaultObject()) continue;
                std::string C(O->Class->Name.ToString().c_str());
                if (C.find("SpawnPointsPercentage") != std::string::npos && C.find("Sequence") != std::string::npos && C.find("Category") == std::string::npos) Spawn = O;
            }
            if (Spawn) { *(UObject**)(E + 0x48) = Spawn; Found = Spawn; }
            else { E[0x9f8] = 1; if (!Seq58) *(UObject**)(E + 0x58) = Found; Log("SyndicateStudio: encounter check:   no spawn points sequence loaded, using intensity curve pacing"); }
        }
        else if (E[0x9f8] == 1 && !Seq58) *(UObject**)(E + 0x58) = Found;
        else return;
        snprintf(Line, sizeof(Line), "SyndicateStudio: encounter check:   filled the empty sequence with %s", StwName(Found).c_str()); Log(Line);
    }
    bool StwCheckEncounterDetour(UObject* Enc) {
        __try { StwCheckEncounterFix(Enc); } __except (StwCrashFilter(GetExceptionInformation(), "encounter check")) {}
        return StwCheckEncounterOriginal(Enc);
    }
    void StwHookStartEncounter() {
        static bool Done = false;
        if (Done) return;
        Done = true;
        auto Base = (uint8_t*)GetModuleHandleW(nullptr);
        const uint8_t Expect[] = { 0x48, 0x89, 0x54, 0x24, 0x10, 0x55, 0x41, 0x55, 0x41, 0x56 };
        const uint8_t ExpCopy[] = { 0x48, 0x89, 0x5C, 0x24, 0x08, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x8B, 0x02 };
        if (memcmp(Base + 0x2d8b90, Expect, sizeof(Expect)) || memcmp(Base + 0x2b3bb0, ExpCopy, sizeof(ExpCopy))) { Log("SyndicateStudio: encounter: not the 1.7.2 exe"); return; }
        MH_Initialize();
        if (MH_CreateHook(Base + 0x2d8b90, (void*)&StwStartEncounterDetour, (void**)&StwStartEncounterOriginal) != MH_OK || MH_EnableHook(Base + 0x2d8b90) != MH_OK) { Log("SyndicateStudio: encounter: could not hook StartEncounter"); return; }
        Log("SyndicateStudio: encounter: hooked the director's StartEncounter");
        const uint8_t ExpCheck[] = { 0x48, 0x89, 0x5C, 0x24, 0x18, 0x55, 0x56, 0x57, 0x48, 0x8B, 0xEC };
        if (memcmp(Base + 0x2e11c0, ExpCheck, sizeof(ExpCheck))) return;
        if (MH_CreateHook(Base + 0x2e11c0, (void*)&StwCheckEncounterDetour, (void**)&StwCheckEncounterOriginal) == MH_OK && MH_EnableHook(Base + 0x2e11c0) == MH_OK)
            Log("SyndicateStudio: encounter: hooked the encounter check");
    }
    void StwMakeMission_Impl() {
        auto World = UWorld::GetWorld();
        auto GS = World ? (UObject*)World->GameState : nullptr;
        auto WM = StwPtr(GS, "WorldManager"), MM = StwPtr(GS, "MissionManager");
        char Line[400];
        if (!WM || !MM) { Log("SyndicateStudio: mission: no world or mission manager"); return; }
        StwHookStartEncounter();
        StwSpawnAIDirectorSafe((UObject*)World->AuthorityGameMode);
        if (auto O = MM->GetOffset("Missions"); O != uint32(-1) && GetFromOffset<TArray<UObject*>>(MM, O).Num() > 0) { Log("SyndicateStudio: mission: the zone already has missions"); return; }
        auto GenClass = (const UClass*)SDK::FindObject(L"/Game/World/MissionGens/MissionGen_Onboarding_Fort.MissionGen_Onboarding_Fort_C", FindClass("Object"));
        auto Gen = GenClass ? (UObject*)GenClass->GetDefaultObj() : nullptr;
        if (!Gen) { Log("SyndicateStudio: mission: the tutorial's mission generator was not found"); return; }
        // PrimaryMissionInfo: TAssetPtr (4.16): weak pointer, tag, then the path as an FString at +0x10.
        UObject* Info = nullptr;
        if (auto O = Gen->GetOffset("PrimaryMissionInfo"); O != uint32(-1)) {
            auto& Path = *(FString*)((uint8_t*)Gen + O + 0x10);
            std::string P = Path.Num() > 0 ? std::string(Path.ToString().c_str()) : "";
            snprintf(Line, sizeof(Line), "SyndicateStudio: mission: primary mission info %s", P.c_str()); Log(Line);
            if (!P.empty()) { std::wstring W(P.begin(), P.end()); Info = (UObject*)SDK::FindObject(W.c_str(), FindClass("Object")); }
        }
        if (!Info) { Log("SyndicateStudio: mission: the tutorial's mission info did not load"); return; }
        snprintf(Line, sizeof(Line), "SyndicateStudio: mission: mission info %s (%s)", StwName(Info).c_str(), StwName(Info->Class).c_str()); Log(Line);
        if (auto O = Info->GetOffset("MissionRef"); O != uint32(-1)) {
            auto& Ref = *(FString*)((uint8_t*)Info + O + 0x10);
            snprintf(Line, sizeof(Line), "SyndicateStudio: mission:   mission actor %s", Ref.Num() > 0 ? std::string(Ref.ToString().c_str()).c_str() : "?"); Log(Line);
        }
        // The record, added to the world manager's own records so it lives as long as the zone.
        alignas(16) uint8_t Record[0x128]{};
        *(float*)(Record + 0x0) = 1.f;            // MissionEntry.Weight
        *(UObject**)(Record + 0x20) = Gen;        // MissionEntry.MissionGenerator
        *(UObject**)(Record + 0x28) = Info;       // MissionEntry.MissionInfo
        Record[0x30] = 0;                         // MissionEntry.GenerationCategory: primary
        *(UObject**)(Record + 0x88) = Gen;        // MissionGenerator
        Record[0xf0] = 0;                         // MissionStatus
        CoCreateGuid((GUID*)(Record + 0x118));    // MissionGuid
        auto& Records = GetFromOffset<TArray<uint8>>(WM, 0x8a8);
        Records.Add(*Record, 0x128);
        snprintf(Line, sizeof(Line), "SyndicateStudio: mission: added the tutorial's mission record (%d now)", Records.Num()); Log(Line);
        static auto Load = (void(*)(UObject*, void*))StwFindTextUser(L"Loading mission records, %i in total.");
        if (!Load) { Log("SyndicateStudio: mission: the mission loader was not found"); return; }
        snprintf(Line, sizeof(Line), "SyndicateStudio: mission: loading missions (exe+0x%llx)", (unsigned long long)((uintptr_t)Load - (uintptr_t)GetModuleHandleW(nullptr))); Log(Line);
        Load(MM, (uint8_t*)WM + 0x8a0);
        int32 Count = -1;
        if (auto O = MM->GetOffset("Missions"); O != uint32(-1)) Count = GetFromOffset<TArray<UObject*>>(MM, O).Num();
        snprintf(Line, sizeof(Line), "SyndicateStudio: mission: the mission manager now has %d missions", Count); Log(Line);
        if (Count > 0) {
            auto Mission = GetFromOffset<TArray<UObject*>>(MM, MM->GetOffset("Missions"))[0];
            StwDumpObject("mission", Mission);
            StwGenerateEncounters(Mission);
        }
    }
    void StwMakeMission() {
        if (StwMissionMade) return;
        StwMissionMade = true;
        __try { StwMakeMission_Impl(); } __except (EXCEPTION_EXECUTE_HANDLER) { Log("SyndicateStudio: mission: crashed (caught)"); }
    }
    void StwDress() { __try { StwDress_Impl(); } __except (EXCEPTION_EXECUTE_HANDLER) { Log("SyndicateStudio: dress crashed (caught)"); } }
    void StwRestart() { __try { StwRestart_Impl(); } __except (EXCEPTION_EXECUTE_HANDLER) { Log("SyndicateStudio: spawn crashed (caught)"); } }
    void StwKickZone() { __try { StwKickZone_Impl(); } __except (EXCEPTION_EXECUTE_HANDLER) { Log("SyndicateStudio: zone kick crashed (caught)"); } }
    // Which code checks the zone's mission data while the map loads? Log the functions that use a text, with their
    // code (hex), so they can be called again once the data is filled.
    void StwDumpTextUsers_Impl(const wchar_t* Text) {
        auto Base = (uint8_t*)GetModuleHandleW(nullptr); auto Nt = (IMAGE_NT_HEADERS64*)(Base + ((IMAGE_DOS_HEADER*)Base)->e_lfanew);
        auto Sec = IMAGE_FIRST_SECTION(Nt);
        const size_t Len = wcslen(Text) * 2;
        uint8_t* Str = nullptr;
        for (int i = 0; i < Nt->FileHeader.NumberOfSections && !Str; i++) {
            if (Sec[i].Characteristics & IMAGE_SCN_MEM_EXECUTE) continue;
            auto St = Base + Sec[i].VirtualAddress; size_t Size = Sec[i].Misc.VirtualSize;
            for (size_t o = 0; o + Len <= Size; o += 2) if (memcmp(St + o, Text, Len) == 0) { Str = St + o; break; }
        }
        char Line[400];
        if (!Str) { snprintf(Line, sizeof(Line), "SyndicateStudio: text not found: %ls", Text); Log(Line); return; }
        int Found = 0;
        for (int i = 0; i < Nt->FileHeader.NumberOfSections && Found < 3; i++) {
            if (!(Sec[i].Characteristics & IMAGE_SCN_MEM_EXECUTE)) continue;
            auto St = Base + Sec[i].VirtualAddress; size_t Size = Sec[i].Misc.VirtualSize;
            for (size_t o = 0; o + 7 <= Size && Found < 3; o++) {
                if ((St[o] != 0x48 && St[o] != 0x4C) || St[o + 1] != 0x8D || (St[o + 2] & 0xC7) != 0x05) continue;
                int32_t D; memcpy(&D, St + o + 3, 4);
                if (St + o + 7 + D != Str) continue;
                Found++;
                DWORD64 IB = 0; auto F = RtlLookupFunctionEntry((DWORD64)(St + o), &IB, nullptr);
                for (int d = 0; F && d < 8; d++) { auto Info = (uint8_t*)(IB + F->UnwindData); if (!((Info[0] >> 3) & 4)) break; F = (PRUNTIME_FUNCTION)(Info + 4 + ((Info[2] + 1) & ~1) * 2); }
                uintptr_t Fn = F ? (uintptr_t)(IB + F->BeginAddress) : (uintptr_t)(St + o), Use = (uintptr_t)(St + o);
                snprintf(Line, sizeof(Line), "SyndicateStudio: text \"%ls\" used at exe+0x%llx in function exe+0x%llx", Text, (unsigned long long)(Use - (uintptr_t)Base), (unsigned long long)(Fn - (uintptr_t)Base)); Log(Line);
                size_t End = (Use - Fn) + 0x60; if (End > 0x800) End = 0x800;
                for (size_t k = 0; k < End; k += 32) {
                    int n = snprintf(Line, sizeof(Line), "SyndicateStudio: code %llx ", (unsigned long long)(Fn - (uintptr_t)Base + k));
                    for (size_t b = k; b < k + 32 && b < End; b++) n += snprintf(Line + n, sizeof(Line) - n, "%02X", ((uint8_t*)Fn)[b]);
                    Log(Line);
                }
            }
        }
    }
    void StwDumpCode(uintptr_t Fn, size_t End) {
        auto Base = (uintptr_t)GetModuleHandleW(nullptr); char Line[400];
        if (End > 0x800) End = 0x800;
        for (size_t k = 0; k < End; k += 32) {
            int n = snprintf(Line, sizeof(Line), "SyndicateStudio: code %llx ", (unsigned long long)(Fn - Base + k));
            for (size_t b = k; b < k + 32 && b < End; b++) n += snprintf(Line + n, sizeof(Line) - n, "%02X", ((uint8_t*)Fn)[b]);
            Log(Line);
        }
    }
    // Which functions set the game mode's bWorldIsReady / bTheaterDataIsReady (1.7.2: or byte [reg+0x568], 1 / 2)?
    // Those set up the mission and the spawn pad; they are what should run once the zone's mission data is filled.
    void StwFindReadySetters_Impl() {
        auto Base = (uint8_t*)GetModuleHandleW(nullptr); auto Nt = (IMAGE_NT_HEADERS64*)(Base + ((IMAGE_DOS_HEADER*)Base)->e_lfanew);
        auto Sec = IMAGE_FIRST_SECTION(Nt); char Line[200]; int Found = 0;
        for (int i = 0; i < Nt->FileHeader.NumberOfSections && Found < 80; i++) {
            if (!(Sec[i].Characteristics & IMAGE_SCN_MEM_EXECUTE)) continue;
            auto St = Base + Sec[i].VirtualAddress; size_t Size = Sec[i].Misc.VirtualSize;
            for (size_t o = 0; o + 8 <= Size && Found < 80; o++) {
                // byte writes to [reg+0x568]: or r/m8,imm8 (80 /1), or r/m8,r8 (08), mov r/m8,r8 (88), mov r/m8,imm8 (C6 /0)
                uint8_t Op = St[o], M = St[o + 1];
                if ((M & 0xC0) != 0x80 || (M & 7) == 4) continue;
                bool Ok = (Op == 0x80 && (M & 0x38) == 0x08) || Op == 0x08 || Op == 0x88 || (Op == 0xC6 && (M & 0x38) == 0);
                if (!Ok || St[o + 2] != 0x68 || St[o + 3] != 0x05 || St[o + 4] || St[o + 5]) continue;
                DWORD64 IB = 0; auto F = RtlLookupFunctionEntry((DWORD64)(St + o), &IB, nullptr);
                for (int d = 0; F && d < 8; d++) { auto Info = (uint8_t*)(IB + F->UnwindData); if (!((Info[0] >> 3) & 4)) break; F = (PRUNTIME_FUNCTION)(Info + 4 + ((Info[2] + 1) & ~1) * 2); }
                uintptr_t Fn = F ? (uintptr_t)(IB + F->BeginAddress) : (uintptr_t)(St + o);
                snprintf(Line, sizeof(Line), "SyndicateStudio: ready setter (op %02X) at exe+0x%llx in function exe+0x%llx", Op, (unsigned long long)((St + o) - Base), (unsigned long long)(Fn - (uintptr_t)Base)); Log(Line);
                Found++;
            }
        }
        if (!Found) Log("SyndicateStudio: no ready setters found");
    }
    void StwFindReadySetters() { __try { StwFindReadySetters_Impl(); } __except (EXCEPTION_EXECUTE_HANDLER) { Log("SyndicateStudio: ready setter scan crashed (caught)"); } }
    void StwDumpTextUsers(const wchar_t* Text) { __try { StwDumpTextUsers_Impl(Text); } __except (EXCEPTION_EXECUTE_HANDLER) { Log("SyndicateStudio: code dump crashed (caught)"); } }
    ULONGLONG StwDumpAt = 0;
    // Map of the mission side of the zone, for finding why it has no missions: the zone record (its mission
    // records), the tutorial's mission generator and the classes the missions are made of.
    void StwDumpMissionSide_Impl() {
        auto World = UWorld::GetWorld();
        auto GS = World ? (UObject*)World->GameState : nullptr;
        auto WM = StwPtr(GS, "WorldManager");
        for (auto Name : { "CurrentZoneRecord", "CurrentWorldRecord" })
            if (auto R = StwPtr(WM, Name)) { StwDumpObject(Name, R); StwLogStruct((const UStruct*)R->Class, Name); }
        auto Gen = (UObject*)SDK::FindObject(L"/Game/World/MissionGens/MissionGen_Onboarding_Fort.MissionGen_Onboarding_Fort_C", FindClass("Object"));
        char Line[300];
        snprintf(Line, sizeof(Line), "SyndicateStudio: mission side: generator class %p", Gen); Log(Line);
        if (Gen) {
            auto CDO = ((const UClass*)Gen)->GetDefaultObj();
            StwLogStruct((const UStruct*)Gen, "generator");
            StwDumpObject("generator CDO", CDO);
            // One level down: the objects and classes the generator points at (mission infos and the like).
            for (auto C = (const UStruct*)Gen; C; C = C->GetSuper())
                for (const UField* P = C->GetChildren(); P; P = P->GetNext()) {
                    if (!P->Class) continue;
                    std::string T(P->Class->Name.ToString().c_str());
                    auto Off = GetFromOffset<uint32>(P, Offsets::Offset_Internal);
                    if ((T == "ObjectProperty" || T == "ClassProperty") && CDO) {
                        auto O = *(UObject**)((uint8_t*)CDO + Off);
                        if (!O) continue;
                        auto Target = T == "ClassProperty" ? ((const UClass*)O)->GetDefaultObj() : O;
                        StwDumpObject(std::string(P->GetName().ToString().c_str()).c_str(), Target);
                    }
                }
        }
        auto Theme = SDK::FindObject(L"/Game/World/ZoneThemes/Onboarding/ZT_OnboardingFort/BP_ZT_Onboarding_Fort.BP_ZT_Onboarding_Fort_C", FindClass("Object"));
        snprintf(Line, sizeof(Line), "SyndicateStudio: mission side: zone theme class %p", Theme); Log(Line);
        for (auto Name : { "FortMission", "FortMissionInfo", "FortMissionGenerator", "FortMissionManager", "FortMissionGenerationManager", "FortWorldManager", "FortAIDirector", "FortZoneRecord", "FortMissionRecord" }) {
            auto Cls = FindClass(Name);
            snprintf(Line, sizeof(Line), "SyndicateStudio: mission side: class %s %p", Name, Cls); Log(Line);
            if (Cls) StwLogStruct((const UStruct*)Cls, Name);
        }
        // The mission records the mission manager loads missions from (1.7.2 exe+0x6aa4d0 walks 0x128-byte records).
        for (auto Name : { "FortMissionRecord", "FortMissionRecordContainer", "FortMissionRecords", "FortMissionGenerationElement", "FortMissionEntry" })
            if (auto S = (const UStruct*)StwFindByName(Name, "ScriptStruct")) {
                snprintf(Line, sizeof(Line), "SyndicateStudio: mission side: struct %s size 0x%x", Name, S->GetPropertiesSize()); Log(Line);
                StwLogStruct(S, Name);
            }
    }
    void StwDumpMissionSide() { __try { StwDumpMissionSide_Impl(); } __except (EXCEPTION_EXECUTE_HANDLER) { Log("SyndicateStudio: mission side dump crashed (caught)"); } }
    void StwDumpState() {
        auto World = UWorld::GetWorld();
        if (!World) return;
        if (auto GS = (UObject*)World->GameState) {
            for (auto Name : { "WorldManager", "MissionManager" })
                if (auto O = GS->GetOffset(Name); O != uint32(-1)) if (auto Obj = GetFromOffset<UObject*>(GS, O)) { StwDumpObject(Name, Obj); StwListFunctions(Obj); }
        }
        StwDumpObject("game mode", (UObject*)World->AuthorityGameMode);
        StwDumpObject("game state", (UObject*)World->GameState);
        UObject* PC = nullptr;
        if (auto Instance = World->OwningGameInstance; Instance && Instance->LocalPlayers.Num() && Instance->LocalPlayers[0]) PC = (UObject*)Instance->LocalPlayers[0]->PlayerController;
        StwDumpObject("player controller", PC);
        StwDumpMissionSide();
    }

    // The zone needs to know which mission it runs (its zone instance: theater, mission, zone theme), or it stays
    // in WaitingToStart behind the loading screen. The tutorial is Stonewood's onboarding mission in the
    // backend's world/info (theater 8633333E..., mission b9c086b0-...).
    const wchar_t* StwTutorialUrl =
        L"open /Game/Maps/Zones/TheFarmstead/Zone_Onboarding_FarmsteadFort?game=/Game/Blueprints/GameModeZone.GameModeZone_C"
        L"?ZoneInstanceId={\"worldId\":\"SyndicateStwTutorialWorld\",\"theaterId\":\"8633333E41A86F67F78EAEAF25BF4735\",\"theaterMissionId\":\"b9c086b0-d157-446b-b52c-0648752201d3\","
        L"\"theaterMissionAlertId\":\"\",\"zoneThemeClass\":\"/Game/World/ZoneThemes/Onboarding/ZT_OnboardingFort/BP_ZT_Onboarding_Fort.BP_ZT_Onboarding_Fort_C\"}"
        L"?theaterId=8633333E41A86F67F78EAEAF25BF4735?zoneIndex=0";
    // The husks spawn but stand still: every 5 s for a minute after the encounter starts, log the husks, their
    // controllers and brains, and the world's navigation system.
    // What a husk is doing: where it is and how fast it moves, whether its behaviour tree runs, its move status,
    // and (once) every blackboard key with its value, read through the blackboard's GetValueAs* functions.
    // SetActorTickEnabled(true) / SetComponentTickEnabled(true) left the husks not ticking, which is what those do
    // when the tick function's bCanEverTick is false. Log the tick function of an object (PrimaryActorTick or
    // PrimaryComponentTick): every reflected field with its value, plus the raw bit byte and state byte after them.
    void StwDumpTick(const char* Label, UObject* O, const char* Field) {
        char Line[400];
        if (!O) return;
        auto P = O->GetProperty(Field);
        if (!P) { snprintf(Line, sizeof(Line), "SyndicateStudio: tick: %s has no %s", StwName(O).c_str(), Field); Log(Line); return; }
        uint32 Base = GetFromOffset<uint32>(P, Offsets::Offset_Internal);
        auto S = GetFromOffset<const UStruct*>(P, 0x70);
        std::string Out;
        for (auto C = S; C; C = C->GetSuper())
            for (const UField* F = C->GetChildren(); F; F = F->GetNext()) {
                if (!F->Class) continue;
                std::string T(F->Class->Name.ToString().c_str());
                uint32 Off = GetFromOffset<uint32>(F, Offsets::Offset_Internal);
                char B[120];
                if (T == "BoolProperty") { uint8_t M = F->GetFieldMask(); if (!M) M = 0xFF; snprintf(B, sizeof(B), "%s=%d(+0x%x/0x%02x) ", F->GetName().ToString().c_str(), (GetFromOffset<uint8_t>(O, Base + Off) & M) ? 1 : 0, Off, M); }
                else if (T == "FloatProperty") snprintf(B, sizeof(B), "%s=%g ", F->GetName().ToString().c_str(), GetFromOffset<float>(O, Base + Off));
                else snprintf(B, sizeof(B), "%s=0x%02x ", F->GetName().ToString().c_str(), GetFromOffset<uint8_t>(O, Base + Off));
                Out += B;
            }
        char Raw[64]; snprintf(Raw, sizeof(Raw), " raw +8..+0xf: %016llx", (unsigned long long)GetFromOffset<uint64_t>(O, Base + 8));
        snprintf(Line, sizeof(Line), "SyndicateStudio: tick: %s %s.%s: %s%s", Label, StwName(O).c_str(), Field, Out.substr(0, 300).c_str(), Raw); Log(Line);
    }
    void StwHuskStatus(UObject* Pawn, UObject* Ctl, UObject* Brain, UObject* BB, bool DumpKeys) {
        char Line[512];
        float Loc[3]{}, Vel[3]{};
        { StwCall C(Pawn, "K2_GetActorLocation"); if (C) { C.Run(); if (auto P = C.At("ReturnValue", 0)) memcpy(Loc, P, 12); } }
        { StwCall C(Pawn, "GetVelocity"); if (C) { C.Run(); if (auto P = C.At("ReturnValue", 0)) memcpy(Vel, P, 12); } }
        int Running = -1, Paused = -1, Move = -1;
        { StwCall C(Brain, "IsRunning"); if (C) { C.Run(); Running = C.Get<uint8_t>("ReturnValue", 0); } }
        { StwCall C(Brain, "IsPaused"); if (C) { C.Run(); Paused = C.Get<uint8_t>("ReturnValue", 0); } }
        { StwCall C(Ctl, "GetMoveStatus"); if (C) { C.Run(); Move = C.Get<uint8_t>("ReturnValue", 0); } }
        UObject* Focus = nullptr;
        { StwCall C(Ctl, "GetFocusActor"); if (C) { C.Run(); Focus = C.Get<UObject*>("ReturnValue", 0); } }
        snprintf(Line, sizeof(Line), "SyndicateStudio: ai watch:   %s at (%.0f, %.0f, %.0f) speed %.0f, tree running %d paused %d, move status %d, focus %s, goal actor %s",
            StwName(Pawn).c_str(), Loc[0], Loc[1], Loc[2], sqrtf(Vel[0] * Vel[0] + Vel[1] * Vel[1] + Vel[2] * Vel[2]), Running, Paused, Move,
            StwName(StwLooksLikeObject(Focus) ? Focus : nullptr).c_str(), StwName(StwPtr(Ctl, "GoalActor")).c_str()); Log(Line);
        // Even a straight push (AddMovementInput every frame) did not move them, so look at the movement itself:
        // its max speed, whether it ticks, the last input it got, the pawn's network role and time dilation.
        {
            auto Move = StwPtr(Pawn, "CharacterMovement");
            float MaxSpeed = -1.f, In[3]{}, Acc[3]{};
            int Ticks = -1, ActorTicks = -1;
            { StwCall C(Move, "GetMaxSpeed"); if (C) { C.Run(); MaxSpeed = C.Get<float>("ReturnValue", 0); } }
            { StwCall C(Move, "IsComponentTickEnabled"); if (C) { C.Run(); Ticks = C.Get<uint8_t>("ReturnValue", 0); } }
            { StwCall C(Move, "GetCurrentAcceleration"); if (C) { C.Run(); if (auto P = C.At("ReturnValue", 0)) memcpy(Acc, P, 12); } }
            { StwCall C(Pawn, "GetLastMovementInputVector"); if (C) { C.Run(); if (auto P = C.At("ReturnValue", 0)) memcpy(In, P, 12); } }
            { StwCall C(Pawn, "IsActorTickEnabled"); if (C) { C.Run(); ActorTicks = C.Get<uint8_t>("ReturnValue", 0); } }
            int Role = Pawn->GetOffset("Role") != uint32(-1) ? GetFromOffset<uint8_t>(Pawn, Pawn->GetOffset("Role")) : -1;
            float Dil = Pawn->GetOffset("CustomTimeDilation") != uint32(-1) ? GetFromOffset<float>(Pawn, Pawn->GetOffset("CustomTimeDilation")) : -1.f;
            snprintf(Line, sizeof(Line), "SyndicateStudio: ai watch:     movement: max speed %.0f, ticks %d (actor %d), accel (%.0f, %.0f, %.0f), last input (%.2f, %.2f), role %d, dilation %.2f; sleeping %d (can %d, should start %d, blueprint %d), AI LOD %d",
                MaxSpeed, Ticks, ActorTicks, Acc[0], Acc[1], Acc[2], In[0], In[1], Role, Dil, StwBool(Pawn, "bIsSleeping"), StwBool(Pawn, "bCanSleep"), StwBool(Pawn, "bShouldStartSleeping"),
                StwBool(Pawn, "Sleeping"), Pawn->GetOffset("CurrentFortAILODLevel") != uint32(-1) ? GetFromOffset<uint8_t>(Pawn, Pawn->GetOffset("CurrentFortAILODLevel")) : -1); Log(Line);
            static bool SetDumped = false;
            if (!SetDumped) {
                SetDumped = true;
                StwDumpTick("husk", Pawn, "PrimaryActorTick");
                StwDumpTick("husk", Move, "PrimaryComponentTick");
                StwDumpTick("husk default", (UObject*)Pawn->Class->GetDefaultObj(), "PrimaryActorTick");
                auto Me = StwPtr(StwLocalPC(), "Pawn");
                StwDumpTick("player", Me, "PrimaryActorTick");
                StwDumpTick("player", StwPtr(Me, "CharacterMovement"), "PrimaryComponentTick");
                StwDumpTick("husk controller", Ctl, "PrimaryActorTick");
                // Attribute sets the husk's movement may read (its MovementSet and any set on its ability system).
                std::vector<UObject*> Sets;
                if (auto MS = StwPtr(Pawn, "MovementSet")) Sets.push_back(MS);
                const int32 N = TUObjectArray::Num();
                for (int32 i = 0; i < N; i++) {
                    auto O = (UObject*)TUObjectArray::GetObjectByIndex(i);
                    if (!O || !O->Class || O->IsDefaultObject() || O->Outer != Pawn) continue;
                    if (std::string(O->Class->Name.ToString().c_str()).find("Set") != std::string::npos && std::find(Sets.begin(), Sets.end(), O) == Sets.end()) Sets.push_back(O);
                }
                for (auto Set : Sets)
                    for (auto C = (const UStruct*)Set->Class; C; C = C->GetSuper()) {
                        for (const UField* P = C->GetChildren(); P; P = P->GetNext()) {
                            if (!P->Class || std::string(P->Class->Name.ToString().c_str()) != "StructProperty") continue;
                            auto St = GetFromOffset<const UObject*>(P, 0x70);
                            if (!St || StwName(St) != "GameplayAttributeData") continue;
                            uint32 Off = GetFromOffset<uint32>(P, Offsets::Offset_Internal);
                            snprintf(Line, sizeof(Line), "SyndicateStudio: ai watch:     %s.%s base %g current %g", StwName(Set).c_str(), std::string(P->GetName().ToString().c_str()).c_str(),
                                GetFromOffset<float>(Set, Off + 8), GetFromOffset<float>(Set, Off + 12)); Log(Line);
                        }
                        if (std::string(((const UField*)C)->GetName().ToString().c_str()) == "AttributeSet") break;
                    }
            }
        }
        if (!DumpKeys || !BB) return;
        auto Asset = StwPtr(BB, "BlackboardAsset");
        snprintf(Line, sizeof(Line), "SyndicateStudio: ai watch:   blackboard %s", StwName(Asset).c_str()); Log(Line);
        for (auto Data = Asset; StwLooksLikeObject(Data); Data = StwPtr(Data, "Parent")) {
            auto KOff = Data->GetOffset("Keys");
            auto EntryStruct = (const UStruct*)StwFindByName("BlackboardEntry", "ScriptStruct");
            if (KOff == uint32(-1) || !EntryStruct) break;
            uint32 NameOff = 0, TypeOff = 0, Sz = 0;
            if (!StwField(EntryStruct, "EntryName", NameOff, Sz) || !StwField(EntryStruct, "KeyType", TypeOff, Sz)) break;
            auto& Keys = *(TArray<uint8_t>*)((uint8_t*)Data + KOff);
            const int32 Stride = EntryStruct->GetPropertiesSize();
            for (int32 k = 0; k < Keys.Num() && k < 64; k++) {
                auto E = Keys.GetData() + (size_t)k * Stride;
                FName KeyName = *(FName*)(E + NameOff);
                auto Type = *(UObject**)(E + TypeOff);
                std::string T = StwLooksLikeObject(Type) ? StwName(Type->Class) : std::string("?");
                std::string V = "?";
                char Buf[160];
                auto Get = [&](const char* Fn) -> StwCall { StwCall C(BB, Fn); if (C) { C.Set("KeyName", KeyName, 0); C.Run(); } return C; };
                if (T.find("Object") != std::string::npos || T.find("Class") != std::string::npos) { auto C = Get("GetValueAsObject"); if (C) { auto O = C.Get<UObject*>("ReturnValue", 8); V = StwLooksLikeObject(O) ? StwName(O) : std::string(O ? "(not an object)" : "null"); } }
                else if (T.find("Vector") != std::string::npos) { auto C = Get("GetValueAsVector"); if (C) { float F[3]{}; if (auto P = C.At("ReturnValue", 8)) memcpy(F, P, 12); snprintf(Buf, sizeof(Buf), "(%.0f, %.0f, %.0f)", F[0], F[1], F[2]); V = Buf; } }
                else if (T.find("Bool") != std::string::npos) { auto C = Get("GetValueAsBool"); if (C) V = C.Get<uint8_t>("ReturnValue", 8) ? "true" : "false"; }
                else if (T.find("Enum") != std::string::npos) { auto C = Get("GetValueAsEnum"); if (C) { snprintf(Buf, sizeof(Buf), "%d", C.Get<uint8_t>("ReturnValue", 8)); V = Buf; } }
                else if (T.find("Float") != std::string::npos) { auto C = Get("GetValueAsFloat"); if (C) { snprintf(Buf, sizeof(Buf), "%g", C.Get<float>("ReturnValue", 8)); V = Buf; } }
                else if (T.find("Int") != std::string::npos) { auto C = Get("GetValueAsInt"); if (C) { snprintf(Buf, sizeof(Buf), "%d", C.Get<int32>("ReturnValue", 8)); V = Buf; } }
                else if (T.find("Name") != std::string::npos) { auto C = Get("GetValueAsName"); if (C) V = C.Get<FName>("ReturnValue", 8).ToString().c_str(); }
                snprintf(Line, sizeof(Line), "SyndicateStudio: ai watch:     key %s (%s) = %s", KeyName.ToString().c_str(), T.c_str(), V.c_str()); Log(Line);
            }
        }
    }
    // The husks never got on their encounter assignment (ControllersOnAssignment stayed empty), so the goal
    // system gave them no goal: their blackboard's GoalActor stayed null and the behaviour tree idled where they
    // spawned. Every second, give each husk of a started encounter that has no goal the player as its goal (the
    // controller's GoalActor and the blackboard keys the tree reads). If one still does not move a few seconds
    // later, ask its controller to walk to the player.
    std::vector<UObject*> StwHusks;
    ULONGLONG StwHuskScanNext = 0, StwDriveNext = 0;
    // ---- Ticking the husks ----
    // Why the husks never moved: every Fortnite pawn is built with PrimaryActorTick.bCanEverTick off (the FortPawn
    // constructor, exe+0x7762e5, clears bCanEverTick and bStartWithTickEnabled); player pawns turn it back on, but AI
    // pawns are ticked by the server's net driver at their net update rate (FortniteChar.ServerTickMovementAtNetUpdateRate).
    // A pawn's tick is also what runs its CharacterMovement (the movement component's own tick is off for players
    // too). In this local zone there is no net driver, so the husks' tick never ran: the AI could see and swing
    // (the controller ticks on its own), but nothing moved them. Give each husk a normal actor tick, the way
    // AActor::RegisterActorTickFunctions does on a standalone game: bCanEverTick and bStartWithTickEnabled on,
    // Target = the pawn, enabled (FTickFunction::SetTickFunctionEnable, exe+0x243f7e0) and registered with its
    // level (FTickFunction::RegisterTickFunction, exe+0x243d380). FTickFunction layout (1.7.2): bits at +0xc
    // (0x02 bCanEverTick, 0x04 bStartWithTickEnabled, 0x40 bRegistered), TickState at +0xd, FActorTickFunction::
    // Target at +0x50; PrimaryActorTick is at +0x28 in AActor. The player's own tick is checked against that
    // layout first.
    bool StwTickLayoutOk() {
        static int Ok = -1;
        if (Ok >= 0) return Ok == 1;
        auto Base = (uint8_t*)GetModuleHandleW(nullptr);
        auto Me = StwPtr(StwLocalPC(), "Pawn");
        if (!StwLooksLikeObject(Me)) return false;
        static const uint8_t Reg[] = { 0xF6, 0x41, 0x0C, 0x40 };          // test byte ptr [rcx+0xc],0x40 (RegisterTickFunction)
        static const uint8_t Ena[] = { 0xF6, 0x41, 0x0C, 0x40 };          // same test in SetTickFunctionEnable, at +0xa
        auto Tick = (uint8_t*)Me + 0x28;
        bool Good = Me->GetOffset("PrimaryActorTick") == 0x28 && !memcmp(Base + 0x243d380 + 0xa, Reg, 4) && !memcmp(Base + 0x243f7e0 + 0xa, Ena, 4)
            && *(UObject**)(Tick + 0x50) == Me && (Tick[0xc] & 0x42) == 0x42;
        Ok = Good ? 1 : 0;
        char Line[200]; snprintf(Line, sizeof(Line), "SyndicateStudio: tick: 1.7.2 tick layout %s (player tick bits 0x%02x)", Good ? "confirmed" : "NOT confirmed, husks left alone", Tick[0xc]); Log(Line);
        return Good;
    }
    void StwRegisterAITick(UObject* Pawn) {
        if (!StwTickLayoutOk()) return;
        auto Tick = (uint8_t*)Pawn + 0x28;
        if (Tick[0xc] & 0x40) return; // already registered
        auto Level = Pawn->Outer;
        if (!StwLooksLikeObject(Level)) return;
        auto Base = (uint8_t*)GetModuleHandleW(nullptr);
        Tick[0xc] |= 0x06;
        *(UObject**)(Tick + 0x50) = Pawn;
        ((void(*)(void*, bool))(Base + 0x243f7e0))(Tick, true);
        ((void(*)(void*, UObject*))(Base + 0x243d380))(Tick, Level);
        static int Logged = 0;
        if (Logged++ < 20) { char Line[200]; snprintf(Line, sizeof(Line), "SyndicateStudio: tick: %s now ticks (registered with %s, bits 0x%02x, state %d)", StwName(Pawn).c_str(), StwName(Level).c_str(), Tick[0xc], Tick[0xd]); Log(Line); }
    }
    void StwUnlockNavigation();
    void StwDriveHusks_Impl() {
        char Line[400];
        auto Now = GetTickCount64();
        // Build the nav mesh as soon as an encounter starts, not once husks are seen: an encounter whose spawns
        // are chosen by environment queries waits for the nav mesh ("waiting for NavMesh"), and no husk came.
        static bool NavDone = false;
        if (!NavDone && StwLooksLikeObject(StwPtr(StwLocalPC(), "Pawn"))) { NavDone = true; StwUnlockNavigation(); }
        if (Now >= StwHuskScanNext) {
            StwHuskScanNext = Now + 3000;
            StwHusks.clear();
            // This used to walk every object and look up two properties by name on each: a frame drop every 3 s.
            // The game keeps its pawns in its actor list, so ask it for the AI pawns directly.
            static UObject* Statics = StwFindByName("Default__GameplayStatics", nullptr);
            static const UClass* AICls = FindClass("FortAIPawn");
            std::vector<UObject*> Pawns;
            if (Statics && AICls) {
                StwCall C(Statics, "GetAllActorsOfClass");
                if (C) { C.Set("WorldContextObject", (UObject*)UWorld::GetWorld(), 0); C.Set("ActorClass", (UObject*)AICls, 8); C.Run(); if (auto A = (TArray<UObject*>*)C.At("OutActors", 0x10)) for (int32 i = 0; i < A->Num(); i++) Pawns.push_back((*A)[i]); }
            }
            for (auto O : Pawns) {
                if (!StwLooksLikeObject(O)) continue;
                if (O->GetOffset("EncounterInfo") == uint32(-1) || O->GetOffset("Controller") == uint32(-1)) continue;
                auto Enc = StwPtr(O, "EncounterInfo");
                if (!Enc || std::find(StwStartedEncounters.begin(), StwStartedEncounters.end(), Enc) == StwStartedEncounters.end()) continue;
                StwHusks.push_back(O);
            }
        }
        auto PC = StwLocalPC();
        auto Player = StwPtr(PC, "Pawn");
        if (!StwLooksLikeObject(Player)) return;
        float PLoc[3]{};
        { StwCall C(Player, "K2_GetActorLocation"); if (C) { C.Run(); if (auto P = C.At("ReturnValue", 0)) memcpy(PLoc, P, 12); } }
        static const FName KGoalActor(L"GoalActor"), KGoalLocation(L"GoalLocation"), KVisible(L"GoalActorIsVisible"), KPerceived(L"GoalActorAlwaysPerceived"),
            KLastSeen(L"GoalActorLastPerceivedLocation"), KCombat(L"IsInCombat"), KMoveTo(L"MovementDestinationLocation");
        static int Logged = 0;
        for (auto H : StwHusks) {
            if (TUObjectArray::GetObjectByIndex(H->Index) != H) continue;
            auto Ctl = StwPtr(H, "Controller");
            if (!StwLooksLikeObject(Ctl)) continue;
            StwRegisterAITick(H);
            auto BB = StwPtr(Ctl, "Blackboard");
            UObject* Cur = nullptr;
            if (BB) { StwCall C(BB, "GetValueAsObject"); if (C) { C.Set("KeyName", KGoalActor, 0); C.Run(); Cur = C.Get<UObject*>("ReturnValue", 8); } }
            {
                // The fortress husks stood still while the cave husks walked. Log every live husk's state (where it
                // is, its AI LOD, its goal on the blackboard and on the controller, its tree and move status) a few
                // times each, so a husk that doesn't move can be told apart from the ones that do.
                static std::map<UObject*, int> Seen;
                static std::map<UObject*, ULONGLONG> Next;
                auto& Times = Seen[H];
                if (Times < 6 && Now >= Next[H]) {
                    Times++;
                    Next[H] = Now + 4000;
                    auto LOff = H->GetOffset("CurrentFortAILODLevel");
                    auto CGoal = Ctl->GetOffset("GoalActor") != uint32(-1) ? GetFromOffset<UObject*>(Ctl, Ctl->GetOffset("GoalActor")) : nullptr;
                    snprintf(Line, sizeof(Line), "SyndicateStudio: husk: %s AI LOD %d, blackboard goal %s, controller goal %s", StwName(H).c_str(),
                        LOff == uint32(-1) ? -1 : (int)GetFromOffset<uint8_t>(H, LOff), StwName(StwLooksLikeObject(Cur) ? Cur : nullptr).c_str(), StwName(StwLooksLikeObject(CGoal) ? CGoal : nullptr).c_str()); Log(Line);
                    StwHuskStatus(H, Ctl, StwPtr(Ctl, "BrainComponent"), BB, false);
                }
            }
            // The fort defence husks get the encounter's objective (BP_FinalEncounterTarget) as their goal, not a
            // player. Their goal query comes back empty like every other, so nothing gives the tree a place to walk
            // to for that objective: the log showed every one of them idle (move status 0, speed 0) on the same spot
            // for the whole wave. A husk whose goal is not the player and that has not moved for 4 s gets the player
            // as its goal, the same as the cave husks, so it walks at the player and through the fort to reach them.
            bool Stuck = false;
            if (StwLooksLikeObject(Cur) && Cur != Player) {
                struct Track { float P[3]; ULONGLONG Since; };
                static std::map<UObject*, Track> Still;
                float HL[3]{};
                { StwCall C(H, "K2_GetActorLocation"); if (C) { C.Run(); if (auto P = C.At("ReturnValue", 0)) memcpy(HL, P, 12); } }
                auto It = Still.find(H);
                if (It == Still.end()) Still[H] = { { HL[0], HL[1], HL[2] }, Now };
                else {
                    float D = (HL[0] - It->second.P[0]) * (HL[0] - It->second.P[0]) + (HL[1] - It->second.P[1]) * (HL[1] - It->second.P[1]);
                    if (D > 30.f * 30.f) It->second = { { HL[0], HL[1], HL[2] }, Now };
                    else if (Now - It->second.Since >= 4000) { Stuck = true; Still.erase(It); }
                }
            }
            if (!StwLooksLikeObject(Cur) || Stuck) {
                if (auto GO = Ctl->GetOffset("GoalActor"); GO != uint32(-1)) GetFromOffset<UObject*>(Ctl, GO) = Player;
                if (BB) {
                    { StwCall C(BB, "SetValueAsObject"); if (C) { C.Set("KeyName", KGoalActor, 0); C.Set("ObjectValue", Player, 8); C.Run(); } }
                    for (auto K : { KGoalLocation, KLastSeen, KMoveTo }) { StwCall C(BB, "SetValueAsVector"); if (C) { C.Set("KeyName", K, 0); if (auto P = C.At("VectorValue", 8)) memcpy(P, PLoc, 12); C.Run(); } }
                    for (auto K : { KVisible, KPerceived, KCombat }) { StwCall C(BB, "SetValueAsBool"); if (C) { C.Set("KeyName", K, 0); C.Set<uint8_t>("BoolValue", 1, 8); C.Run(); } }
                }
                if (Logged++ < 40) { snprintf(Line, sizeof(Line), "SyndicateStudio: ai: %s (goal was %s) now goes for %s", StwName(H).c_str(), StwName(StwLooksLikeObject(Cur) ? Cur : nullptr).c_str(), StwName(Player).c_str()); Log(Line); }
            } else if (BB && Cur == Player) {
                // Keep the player's position fresh for the tree.
                for (auto K : { KGoalLocation, KLastSeen }) { StwCall C(BB, "SetValueAsVector"); if (C) { C.Set("KeyName", K, 0); if (auto P = C.At("VectorValue", 8)) memcpy(P, PLoc, 12); C.Run(); } }
            }
        }
    }
    void StwDriveHusks() { __try { StwDriveHusks_Impl(); } __except (EXCEPTION_EXECUTE_HANDLER) { Log("SyndicateStudio: ai: driving the husks crashed (caught)"); } }
    // The husks now go for the player and swing when close, but never walk: asking a controller to walk to the
    // player failed (MoveToActor result 0). Test the navigation directly: can the husk's and the player's
    // positions be projected onto the nav mesh, and is there a path between them?
    void StwNavTest(const char* When, UObject* Husk, UObject* Player) {
        char Line[400];
        auto World = UWorld::GetWorld();
        auto Nav = World ? StwPtr((UObject*)World, "NavigationSystem") : nullptr;
        if (!Nav || !Husk || !Player) return;
        float HLoc[3]{}, PLoc[3]{};
        { StwCall C(Husk, "K2_GetActorLocation"); if (C) { C.Run(); if (auto P = C.At("ReturnValue", 0)) memcpy(HLoc, P, 12); } }
        { StwCall C(Player, "K2_GetActorLocation"); if (C) { C.Run(); if (auto P = C.At("ReturnValue", 0)) memcpy(PLoc, P, 12); } }
        int Building = -1;
        { StwCall C(Nav, "IsNavigationBeingBuilt"); if (C) { C.Set("WorldContextObject", (UObject*)World, 0); C.Run(); Building = C.Get<uint8_t>("ReturnValue", 8); } }
        float Proj[2][3]{};
        for (int k = 0; k < 2; k++) {
            StwCall C(Nav, "ProjectPointToNavigation");
            if (!C) break;
            C.Set("WorldContextObject", (UObject*)World, 0);
            if (auto P = C.At("Point", 8)) memcpy(P, k ? PLoc : HLoc, 12);
            { float Ext[3] = { 200.f, 200.f, 400.f }; if (auto P = C.At("QueryExtent", 0x28)) memcpy(P, Ext, 12); }
            C.Run();
            if (auto P = C.At("ReturnValue", 0)) memcpy(Proj[k], P, 12);
        }
        int Points = -1, Partial = -1;
        {
            StwCall C(Nav, "FindPathToActorSynchronously");
            if (C) {
                C.Set("WorldContextObject", (UObject*)World, 0);
                if (auto P = C.At("PathStart", 8)) memcpy(P, HLoc, 12);
                C.Set("GoalActor", Player, 0x18);
                C.Set<float>("TetherDistance", 50.f, 0x20);
                C.Run();
                auto Path = C.Get<UObject*>("ReturnValue", 0x38);
                if (StwLooksLikeObject(Path)) {
                    if (auto O = Path->GetOffset("PathPoints"); O != uint32(-1)) Points = ((TArray<uint8_t>*)((uint8_t*)Path + O))->Num();
                    StwCall Q(Path, "IsPartial"); if (Q) { Q.Run(); Partial = Q.Get<uint8_t>("ReturnValue", 0); }
                }
            }
        }
        {
            // Does each nav mesh have a generator (ANavigationData +0x418, NavDataGenerator; exe+0x141ef8790 reads it)?
            // Without one nothing is ever built. And the dirty-area rebuild rate FortNavSystem sets for active AI.
            static int GenLogged = 0;
            if (GenLogged++ < 3) {
                auto SetOff = Nav->GetOffset("NavDataSet");
                std::string Out;
                if (SetOff != uint32(-1)) {
                    auto& Set = GetFromOffset<TArray<UObject*>>(Nav, SetOff);
                    for (int32 i = 0; i < Set.Num(); i++) {
                        auto D = Set[i];
                        if (!StwLooksLikeObject(D)) continue;
                        char B[200]; snprintf(B, sizeof(B), "%s (%s) generator %p; ", StwName(D).c_str(), StwName(D->Class).c_str(), GetFromOffset<void*>(D, 0x418)); Out += B;
                    }
                }
                float Freq = Nav->GetOffset("DirtyAreasUpdateFreq") != uint32(-1) ? GetFromOffset<float>(Nav, Nav->GetOffset("DirtyAreasUpdateFreq")) : -1.f;
                snprintf(Line, sizeof(Line), "SyndicateStudio: nav: lock 0x%02x, dirty area rebuild %.2f Hz, nav data: %s", GetFromOffset<uint8_t>(Nav, 0x3e8), Freq, Out.substr(0, 250).c_str()); Log(Line);
            }
        }
        snprintf(Line, sizeof(Line), "SyndicateStudio: nav %s: building %d; husk (%.0f, %.0f, %.0f) -> (%.0f, %.0f, %.0f); player (%.0f, %.0f, %.0f) -> (%.0f, %.0f, %.0f); path %d points, partial %d",
            When, Building, HLoc[0], HLoc[1], HLoc[2], Proj[0][0], Proj[0][1], Proj[0][2], PLoc[0], PLoc[1], PLoc[2], Proj[1][0], Proj[1][1], Proj[1][2], Points, Partial); Log(Line);
    }
    // A zone server lets the navigation system build once the world is ready; here its initial building lock may
    // never be released, leaving an empty nav mesh. Clear the lock flag and ask it to rebuild the nav bounds.
    void StwActorBounds(UObject* A, float Origin[3], float Extent[3]) {
        StwCall C(A, "GetActorBounds");
        if (!C) return;
        C.Run();
        if (auto P = C.At("Origin", 4)) memcpy(Origin, P, 12);
        if (auto P = C.At("BoxExtent", 16)) memcpy(Extent, P, 12);
    }
    // The nav mesh is built inside UnitNavMeshBounds, a 1-unit volume at the origin (its extent read 0 only
    // because the log rounded 0.5 away). A zone server stretches it over the world it builds; that never happens
    // here, so the only nav mesh is the tile at the origin (a point there projects onto it, the husks' and the
    // player's positions don't, and no path is found). Make it movable, centre it on the player and scale it to
    // cover 40,000 units each way (6,000 up and down), then the caller asks for a rebuild.
    void StwResizeNavBounds(UObject* Volume) {
        char Line[400];
        auto Player = StwPtr(StwLocalPC(), "Pawn");
        float PLoc[3]{}, O[3]{}, E[3]{}, S[3]{ 1, 1, 1 };
        { StwCall C(Player, "K2_GetActorLocation"); if (C) { C.Run(); if (auto P = C.At("ReturnValue", 0)) memcpy(PLoc, P, 12); } }
        StwActorBounds(Volume, O, E);
        { StwCall C(Volume, "GetActorScale3D"); if (C) { C.Run(); if (auto P = C.At("ReturnValue", 0)) memcpy(S, P, 12); } }
        snprintf(Line, sizeof(Line), "SyndicateStudio: nav: %s bounds centre (%.1f, %.1f, %.1f) extent (%.4f, %.4f, %.4f) scale (%.3f, %.3f, %.3f)", StwName(Volume).c_str(), O[0], O[1], O[2], E[0], E[1], E[2], S[0], S[1], S[2]); Log(Line);
        for (int k = 0; k < 3; k++) if (E[k] < 0.0001f) E[k] = 0.5f * S[k]; // a unit cube if the bounds read nothing
        if (auto Root = StwPtr(Volume, "RootComponent")) { StwCall C(Root, "SetMobility"); if (C) { C.Set<uint8_t>("NewMobility", 2, 0); C.Run(); } }
        { StwCall C(Volume, "K2_SetActorLocation"); if (C) { if (auto P = C.At("NewLocation", 0)) memcpy(P, PLoc, 12); C.Set<uint8_t>("bTeleport", 1, 0x0d); C.Run(); } }
        float NS[3] = { S[0] * 40000.f / E[0], S[1] * 40000.f / E[1], S[2] * 6000.f / E[2] };
        { StwCall C(Volume, "SetActorScale3D"); if (C) { if (auto P = C.At("NewScale3D", 0)) memcpy(P, NS, 12); C.Run(); } }
        StwActorBounds(Volume, O, E);
        snprintf(Line, sizeof(Line), "SyndicateStudio: nav: %s now centre (%.0f, %.0f, %.0f) extent (%.0f, %.0f, %.0f), scale (%.0f, %.0f, %.0f)", StwName(Volume).c_str(), O[0], O[1], O[2], E[0], E[1], E[2], NS[0], NS[1], NS[2]); Log(Line);
    }
    // The zone streams in /Game/Maps/NavMeshBounds (its UnitNavMeshBounds volume is what the nav mesh is built
    // in), but nothing here makes that level visible, so the volume's components never register: its bounds read
    // zero, no nav mesh tiles are built, and the husks can't path. List the streaming levels and make that one
    // loaded and visible.
    void StwShowNavBoundsLevel() {
        char Line[400];
        const int32 Count = TUObjectArray::Num();
        for (int32 i = 0; i < Count; i++) {
            auto O = (UObject*)TUObjectArray::GetObjectByIndex(i);
            if (!O || !O->Class || O->IsDefaultObject()) continue;
            if (O->GetOffset("bShouldBeVisible") == uint32(-1) || O->GetOffset("PackageNameToLoad") == uint32(-1)) continue;
            std::string Pkg;
            if (auto WO = O->GetOffset("WorldAsset"); WO != uint32(-1)) { auto& F = *(FString*)((uint8_t*)O + WO + 0x10); if (F.Num() > 1 && F.Num() < 512) Pkg = F.ToString().c_str(); }
            std::string ToLoad = GetFromOffset<FName>(O, O->GetOffset("PackageNameToLoad")).ToString().c_str();
            int Loaded = -1, Visible = -1;
            { StwCall C(O, "IsLevelLoaded"); if (C) { C.Run(); Loaded = C.Get<uint8_t>("ReturnValue", 0); } }
            { StwCall C(O, "IsLevelVisible"); if (C) { C.Run(); Visible = C.Get<uint8_t>("ReturnValue", 0); } }
            snprintf(Line, sizeof(Line), "SyndicateStudio: nav: streaming level %s (%s) world %s, load %s, loaded %d visible %d", StwName(O).c_str(), StwName(O->Class).c_str(),
                Pkg.c_str(), ToLoad.c_str(), Loaded, Visible); Log(Line);
            if (Pkg.find("NavMeshBounds") == std::string::npos && ToLoad.find("NavMeshBounds") == std::string::npos) continue;
            StwSetFlag(O, "bShouldBeLoaded");
            StwSetFlag(O, "bShouldBeVisible");
            Log("SyndicateStudio: nav:   asked for the nav bounds level to be shown");
        }
    }
    // The lock was not the problem (only the editor bit was set) and a bounds update alone queued nothing: build
    // every nav mesh outright, the way the RebuildNavigation cheat does (UNavigationSystem::Build), through the
    // controller's cheat manager, which this build has (FortCheatManager_0).
    void StwBuildNavigation() {
        char Line[200];
        // Turn the navigation logs up (they go to FortniteGame.log) so the game says what it does with the build.
        for (auto Cmd : { L"log LogNavigation Verbose", L"log LogFortNavigation Verbose", L"log LogNavigationDataBuild Verbose" }) {
            StwCall K(StwLocalPC(), "ConsoleCommand");
            if (!K) break;
            FString S(Cmd);
            K.Set("Command", S, 0);
            K.Run();
        }
        auto CM = StwPtr(StwLocalPC(), "CheatManager");
        StwCall C(CM, "RebuildNavigation");
        if (!C) { snprintf(Line, sizeof(Line), "SyndicateStudio: nav: no RebuildNavigation on %s", StwName(CM).c_str()); Log(Line); return; }
        C.Run();
        auto World = UWorld::GetWorld();
        auto Nav = World ? StwPtr((UObject*)World, "NavigationSystem") : nullptr;
        int Building = -1;
        { StwCall B(Nav, "IsNavigationBeingBuilt"); if (B) { B.Set("WorldContextObject", (UObject*)World, 0); B.Run(); Building = B.Get<uint8_t>("ReturnValue", 8); } }
        snprintf(Line, sizeof(Line), "SyndicateStudio: nav: RebuildNavigation through %s; building now %d", StwName(CM).c_str(), Building); Log(Line);
    }
    // Why the nav mesh stays empty with the lock released: a Recast generator only builds tiles inside its
    // InclusionBounds, which UpdateNavigationBounds (exe+0x141f2f1e0) fills from the nav system's
    // RegisteredNavBounds set (+0x180) for the nav mesh's agent. RebuildAll (exe+0x141f2cbf0) turns those boxes
    // into dirty tiles and does nothing when there are none; so Build() returned at once with nothing to build.
    // Read the real state (registered bounds, agent index, inclusion bounds, pending/running tiles), register the
    // play area directly through PerformNavigationBoundsUpdate (exe+0x141efc760) when it is missing, and if a nav
    // mesh still has no inclusion bounds give it the box the same way UpdateNavigationBounds does, then rebuild it.
    // 1.7.2 layouts, all checked against the exe: FRecastNavMeshGenerator vtable exe+0x46896a8 (+0x08 RebuildAll,
    // +0x28 OnNavigationBoundsChanged), +0xac TotalNavBounds, +0xc8 InclusionBounds (FBox, 0x1c each), +0xd8
    // DestNavMesh, +0xe8 PendingDirtyTiles.Num, +0xf8 RunningDirtyTiles.Num. FNavigationBoundsUpdateRequest is 0x30
    // bytes (UniqueID, AreaBox at +4, SupportedAgents +0x20, Level +0x24, type +0x2c: 0 added, 1 removed, 2 updated).
#pragma pack(push, 4)
    struct StwFBox { float Min[3]; float Max[3]; uint8_t Valid; uint8_t Pad[3]; };
    struct StwBoundsReq { uint32_t Id; StwFBox Box; uint32_t Agents; int32_t LevelIndex; int32_t LevelSerial; int32_t Type; };
#pragma pack(pop)
    static_assert(sizeof(StwFBox) == 0x1c && sizeof(StwBoundsReq) == 0x30, "1.7.2 nav bounds layout");
    struct StwBoxArray { StwFBox* Data; int32_t Num; int32_t Max; };
    static bool StwBytesAt(uintptr_t Addr, const char* Hex) {
        auto P = (const uint8_t*)Addr;
        for (size_t i = 0; Hex[i * 3] && Hex[i * 3 + 1]; i++) {
            unsigned V = 0; sscanf_s(Hex + i * 3, "%2x", &V);
            if (P[i] != (uint8_t)V) return false;
        }
        return true;
    }
    void StwFixNavBounds_Impl(const char* When, bool Repair) {
        char Line[600];
        auto World = UWorld::GetWorld();
        auto Nav = World ? StwPtr((UObject*)World, "NavigationSystem") : nullptr;
        if (!Nav || Nav->GetOffset("OperationMode") != 0x224) return;
        auto Base = (uintptr_t)GetModuleHandleW(nullptr);
        auto NavB = (uint8_t*)Nav;
        // bAsyncBuildPaused (+0x3ea bit 0x10) stops UNavigationSystem::Tick from ticking the generators at all.
        if (NavB[0x3ea] & 0x10) { NavB[0x3ea] &= ~0x10; Log("SyndicateStudio: navfix: async nav building was paused, unpaused"); }
        int32_t RegNum = *(int32_t*)(NavB + 0x188) - *(int32_t*)(NavB + 0x1b4);
        auto RegData = *(uint8_t**)(NavB + 0x180);
        std::string Reg;
        bool Covered = false;
        float PLoc[3]{};
        auto Player = StwPtr(StwLocalPC(), "Pawn");
        if (StwLooksLikeObject(Player)) { StwCall C(Player, "K2_GetActorLocation"); if (C) { C.Run(); if (auto P = C.At("ReturnValue", 0)) memcpy(PLoc, P, 12); } }
        if (RegData && *(int32_t*)(NavB + 0x1b4) == 0) {
            for (int32_t i = 0; i < RegNum && i < 4; i++) {
                auto E = RegData + i * 0x34;
                auto B = (StwFBox*)(E + 4);
                char T[200]; snprintf(T, sizeof(T), "[id %u (%.0f,%.0f,%.0f)-(%.0f,%.0f,%.0f) agents 0x%x] ", *(uint32_t*)E, B->Min[0], B->Min[1], B->Min[2], B->Max[0], B->Max[1], B->Max[2], *(uint32_t*)(E + 0x20)); Reg += T;
                if (PLoc[0] > B->Min[0] && PLoc[0] < B->Max[0] && PLoc[1] > B->Min[1] && PLoc[1] < B->Max[1] && PLoc[2] > B->Min[2] && PLoc[2] < B->Max[2]) Covered = true;
            }
        }
        snprintf(Line, sizeof(Line), "SyndicateStudio: navfix %s: lock 0x%02x, %d registered nav bounds %s; %d agents; player (%.0f, %.0f, %.0f) %s", When, NavB[0x3e8], RegNum, Reg.c_str(),
            *(int32_t*)(NavB + 0x50), PLoc[0], PLoc[1], PLoc[2], Covered ? "inside" : "OUTSIDE them"); Log(Line);
        const bool PerformOk = StwBytesAt(Base + 0x1efc760, "48 8b c4 48 89 48 08 55 41 55");
        const bool AgentOk = StwBytesAt(Base + 0x1ef70b0, "40 55 48 83 ec 50 8b 41 50");
        const bool GrowOk = StwBytesAt(Base + 0x1d6da70, "48 89 5c 24 08 57 48 83 ec 20");
        StwFBox Want{};
        Want.Min[0] = PLoc[0] - 16000.f; Want.Min[1] = PLoc[1] - 16000.f; Want.Min[2] = PLoc[2] - 5000.f;
        Want.Max[0] = PLoc[0] + 16000.f; Want.Max[1] = PLoc[1] + 16000.f; Want.Max[2] = PLoc[2] + 5000.f;
        Want.Valid = 1;
        if (Repair && !Covered && PerformOk && StwLooksLikeObject(Player) && NavB[0x3e8] == 0) {
            // Register the play area the way OnNavigationBoundsUpdated would, under the bounds volume's id.
            UObject* Volume = nullptr;
            const int32 Count = TUObjectArray::Num();
            for (int32 i = 0; i < Count && !Volume; i++) {
                auto O = (UObject*)TUObjectArray::GetObjectByIndex(i);
                if (O && O->Class && !O->IsDefaultObject() && std::string(O->Class->Name.ToString().c_str()) == "NavMeshBoundsVolume") Volume = O;
            }
            StwBoundsReq Req{};
            Req.Id = Volume ? (uint32_t)Volume->Index : 0x5717;
            Req.Box = Want;
            Req.Agents = 0xFFFFFFFFu;
            Req.LevelIndex = -1;
            Req.Type = 2; // updated (adds it when the id is new)
            StwBoxArray Arr{ (StwFBox*)&Req, 1, 1 };
            ((void(*)(UObject*, void*))(Base + 0x1efc760))(Nav, &Arr);
            snprintf(Line, sizeof(Line), "SyndicateStudio: navfix: registered nav bounds (%.0f, %.0f, %.0f)-(%.0f, %.0f, %.0f) id %u; now %d registered", Want.Min[0], Want.Min[1], Want.Min[2], Want.Max[0], Want.Max[1], Want.Max[2],
                Req.Id, *(int32_t*)(NavB + 0x188) - *(int32_t*)(NavB + 0x1b4)); Log(Line);
        }
        auto SetOff = Nav->GetOffset("NavDataSet");
        if (SetOff == uint32(-1)) return;
        auto& Set = GetFromOffset<TArray<UObject*>>(Nav, SetOff);
        // The tiles were queued (6400 per nav mesh) and finished within seconds, all empty: there is no geometry
        // to build them from. FortNavSystem's constructor (exe+0x1403120a7, again at exe+0x1403123d5) sets
        // bNavOctreeLock (+0x3ea bit 0x01) for every game world, so no level geometry ever enters the nav octree
        // (ConditionalPopulateNavOctree, exe+0x141ee8150, skips the levels while it is set, exe+0x141ee8543, and
        // component registration skips the octree too). A zone server unlocks and fills it once its world is
        // built; nothing does here. Release the lock, fill the octree from every loaded level while no tiles are
        // being generated, then rebuild every nav mesh from it.
        bool Refill = false;
        {
            bool Idle = true;
            for (int32 i = 0; i < Set.Num(); i++) {
                auto D = Set[i];
                if (!StwLooksLikeObject(D)) continue;
                auto Gen = GetFromOffset<uint8_t*>(D, 0x418);
                if (Gen && (uintptr_t)*(uintptr_t**)Gen == Base + 0x46896a8 && (*(int32_t*)(Gen + 0xe8) || *(int32_t*)(Gen + 0xf8))) Idle = false;
            }
            snprintf(Line, sizeof(Line), "SyndicateStudio: navfix %s: nav octree %p, octree lock %d, generators %s", When, *(void**)(NavB + 0x228), NavB[0x3ea] & 1, Idle ? "idle" : "busy"); Log(Line);
            static bool Filled = false;
            if (Repair && !Filled && Idle && NavB[0x3e8] == 0 && StwBytesAt(Base + 0x1ee8150, "40 55 53 57 41 55 48 8d 6c 24 f8")) {
                Filled = true;
                NavB[0x3ea] &= ~0x01;
                ((void(*)(UObject*))(Base + 0x1ee8150))(Nav);
                Refill = true;
                snprintf(Line, sizeof(Line), "SyndicateStudio: navfix: nav octree unlocked and filled from the loaded levels (octree %p, octree lock %d)", *(void**)(NavB + 0x228), NavB[0x3ea] & 1); Log(Line);
            }
        }
        for (int32 i = 0; i < Set.Num(); i++) {
            auto D = Set[i];
            if (!StwLooksLikeObject(D)) continue;
            auto Gen = GetFromOffset<uint8_t*>(D, 0x418);
            if (!Gen) continue;
            auto VT = *(uintptr_t**)Gen;
            if ((uintptr_t)VT != Base + 0x46896a8) { snprintf(Line, sizeof(Line), "SyndicateStudio: navfix: %s generator vtable exe+0x%llx is not the Recast one", StwName(D).c_str(), (unsigned long long)((uintptr_t)VT - Base)); Log(Line); continue; }
            if (*(UObject**)(Gen + 0xd8) != D) { snprintf(Line, sizeof(Line), "SyndicateStudio: navfix: %s generator points at another nav mesh, left alone", StwName(D).c_str()); Log(Line); continue; }
            int Agent = AgentOk ? ((int(*)(UObject*, UObject*))(Base + 0x1ef70b0))(Nav, D) : -2;
            auto& Inc = *(StwBoxArray*)(Gen + 0xc8);
            auto Pending = *(int32_t*)(Gen + 0xe8), Running = *(int32_t*)(Gen + 0xf8);
            std::string Boxes;
            for (int32_t k = 0; k < Inc.Num && k < 3 && Inc.Data; k++) { char T[120]; snprintf(T, sizeof(T), "(%.0f,%.0f,%.0f)-(%.0f,%.0f,%.0f) ", Inc.Data[k].Min[0], Inc.Data[k].Min[1], Inc.Data[k].Min[2], Inc.Data[k].Max[0], Inc.Data[k].Max[1], Inc.Data[k].Max[2]); Boxes += T; }
            snprintf(Line, sizeof(Line), "SyndicateStudio: navfix %s: %s agent %d, %d inclusion bounds %s, %d tiles pending, %d running", When, StwName(D).c_str(), Agent, Inc.Num, Boxes.c_str(), Pending, Running); Log(Line);
            if (!Repair || NavB[0x3e8] != 0 || Pending || Running) continue;
            bool Inside = false;
            if (Refill) { ((void(*)(void*))VT[5])(Gen); }
            for (int32_t k = 0; k < Inc.Num && Inc.Data; k++)
                if (PLoc[0] > Inc.Data[k].Min[0] && PLoc[0] < Inc.Data[k].Max[0] && PLoc[1] > Inc.Data[k].Min[1] && PLoc[1] < Inc.Data[k].Max[1]) Inside = true;
            const bool WasInside = Inside;
            if (!Inside) {
                ((void(*)(void*))VT[5])(Gen); // OnNavigationBoundsChanged: re-read the registered bounds
                for (int32_t k = 0; k < Inc.Num && Inc.Data; k++)
                    if (PLoc[0] > Inc.Data[k].Min[0] && PLoc[0] < Inc.Data[k].Max[0] && PLoc[1] > Inc.Data[k].Min[1] && PLoc[1] < Inc.Data[k].Max[1]) Inside = true;
            }
            if (!Inside && GrowOk && StwLooksLikeObject(Player)) {
                // The agent filter still left the player's area out: add the box the way UpdateNavigationBounds
                // does for a whole-world nav mesh (Num++, grow through the game's allocator, write the box).
                int32_t Old = Inc.Num;
                Inc.Num = Old + 1;
                if (Inc.Num > Inc.Max) ((void(*)(void*, int32_t))(Base + 0x1d6da70))(&Inc, Old);
                if (Inc.Data) { Inc.Data[Old] = Want; Inside = true; }
                else Inc.Num = Old;
                snprintf(Line, sizeof(Line), "SyndicateStudio: navfix: gave %s the play area as inclusion bounds (agent %d left it out)", StwName(D).c_str(), Agent); Log(Line);
            }
            if (WasInside && !Refill) continue; // its area is covered and nothing changed: the tiles it built stand
            if (Inside) {
                ((bool(*)(void*))VT[1])(Gen); // RebuildAll: every inclusion box becomes dirty tiles
                snprintf(Line, sizeof(Line), "SyndicateStudio: navfix: rebuilding %s: %d tiles pending", StwName(D).c_str(), *(int32_t*)(Gen + 0xe8)); Log(Line);
            }
        }
    }
    void StwFixNavBounds(const char* When, bool Repair) { __try { StwFixNavBounds_Impl(When, Repair); } __except (EXCEPTION_EXECUTE_HANDLER) { Log("SyndicateStudio: navfix: crashed (caught)"); } }
    void StwUnlockNavigation() {
        char Line[300];
        auto World = UWorld::GetWorld();
        auto Nav = World ? StwPtr((UObject*)World, "NavigationSystem") : nullptr;
        if (!Nav) return;
        if (auto Prop = Nav->GetProperty("bInitialBuildingLocked")) {
            auto Off = GetFromOffset<uint32>(Prop, Offsets::Offset_Internal);
            uint8_t Mask = Prop->GetFieldMask(); if (!Mask) Mask = 0xFF;
            snprintf(Line, sizeof(Line), "SyndicateStudio: nav: bInitialBuildingLocked was %d, cleared", (GetFromOffset<uint8_t>(Nav, Off) & Mask) ? 1 : 0); Log(Line);
            GetFromOffset<uint8_t>(Nav, Off) &= ~Mask;
        }
        StwShowNavBoundsLevel();
        // The real lock: NavBuildingLockFlags (1.7.2: +0x3e8) with InitialNavBuildingLockFlags at +0x3e9. 0x04 is
        // ENavigationBuildLock::InitialLock (UNavigationSystem::OnWorldInitDone, exe+0x141efd0cf, sets it when
        // bInitialBuildingLocked is on and only releases it in the editor). On a zone server the world manager
        // releases it once the world is built (ReleaseInitialBuildingLock); nothing does here, and while it is set
        // FortNavSystem's tick (exe+0x33c7b0) skips everything (it never turns the nav mesh rebuild on for active
        // AI) and Build() refuses. An earlier version kept 0x04 thinking it was the editor bit; release it fully.
        if (Nav->GetOffset("OperationMode") == 0x224) {
            auto& Flags = GetFromOffset<uint8_t>(Nav, 0x3e8);
            auto& Initial = GetFromOffset<uint8_t>(Nav, 0x3e9);
            snprintf(Line, sizeof(Line), "SyndicateStudio: nav: build lock flags 0x%02x (initial 0x%02x) -> 0x%02x", Flags, Initial, Flags & ~Initial & ~0x04); Log(Line);
            Flags &= (uint8_t)(~Initial & ~0x04);
            Initial = 0;
        } else Log("SyndicateStudio: nav: not the 1.7.2 nav system layout, build lock left alone");
        const int32 Count = TUObjectArray::Num();
        int Volumes = 0;
        for (int32 i = 0; i < Count; i++) {
            auto O = (UObject*)TUObjectArray::GetObjectByIndex(i);
            if (!O || !O->Class || O->IsDefaultObject() || std::string(O->Class->Name.ToString().c_str()) != "NavMeshBoundsVolume") continue;
            StwResizeNavBounds(O);
            StwCall C(Nav, "OnNavigationBoundsUpdated");
            if (!C) { Log("SyndicateStudio: nav: no OnNavigationBoundsUpdated"); break; }
            C.Set("NavVolume", O, 0);
            C.Run();
            Volumes++;
        }
        snprintf(Line, sizeof(Line), "SyndicateStudio: nav: asked for a rebuild of %d nav bounds", Volumes); Log(Line);
        StwBuildNavigation();
        StwFixNavBounds("start", true);
    }
    // Once the nav bounds level is visible (a few seconds later), ask for the bounds to be rebuilt again and log them.
    void StwRebuildNavBounds() {
        char Line[300];
        auto World = UWorld::GetWorld();
        auto Nav = World ? StwPtr((UObject*)World, "NavigationSystem") : nullptr;
        if (!Nav) return;
        StwShowNavBoundsLevel();
        if (Nav->GetOffset("OperationMode") == 0x224) { snprintf(Line, sizeof(Line), "SyndicateStudio: nav: build lock flags now 0x%02x", GetFromOffset<uint8_t>(Nav, 0x3e8)); Log(Line); GetFromOffset<uint8_t>(Nav, 0x3e8) &= (uint8_t)~0x04; }
        const int32 Count = TUObjectArray::Num();
        for (int32 i = 0; i < Count; i++) {
            auto O = (UObject*)TUObjectArray::GetObjectByIndex(i);
            if (!O || !O->Class || O->IsDefaultObject() || std::string(O->Class->Name.ToString().c_str()) != "NavMeshBoundsVolume") continue;
            float Or[3]{}, E[3]{};
            StwActorBounds(O, Or, E);
            snprintf(Line, sizeof(Line), "SyndicateStudio: nav: %s bounds now centre (%.0f, %.0f, %.0f) extent (%.1f, %.1f, %.1f)", StwName(O).c_str(), Or[0], Or[1], Or[2], E[0], E[1], E[2]); Log(Line);
            StwCall C(Nav, "OnNavigationBoundsUpdated");
            if (C) { C.Set("NavVolume", O, 0); C.Run(); }
        }
        StwFixNavBounds("bounds", true);
    }
    void StwAIWatch_Impl() {
        char Line[400];
        auto World = UWorld::GetWorld();
        snprintf(Line, sizeof(Line), "SyndicateStudio: ai watch: navigation system %s", StwName(World ? StwPtr((UObject*)World, "NavigationSystem") : nullptr).c_str()); Log(Line);
        // The husks the drive already found (StwHusks), not a walk over every object: that walk stalled a frame.
        int N = 0;
        for (auto O : StwHusks) {
            if (N >= 6) break;
            if (TUObjectArray::GetObjectByIndex(O->Index) != O || O->GetOffset("Controller") == uint32(-1)) continue;
            N++;
            auto Ctl = StwPtr(O, "Controller");
            auto Brain = StwPtr(Ctl, "BrainComponent");
            auto BB = StwPtr(Ctl, "Blackboard");
            snprintf(Line, sizeof(Line), "SyndicateStudio: ai watch: %s controller %s (%s) brain %s (%s) blackboard %s", StwName(O).c_str(), StwName(Ctl).c_str(),
                Ctl ? StwName(Ctl->Class).c_str() : "-", StwName(Brain).c_str(), Brain ? StwName(Brain->Class).c_str() : "-", StwName(BB).c_str()); Log(Line);
            { static int Keys = 0; StwHuskStatus(O, Ctl, Brain, BB, N == 1 && (Keys++ % 3) == 0); }
            if (N == 1 && Ctl) { static bool D = false; if (!D) { D = true; StwDumpObject("husk controller", Ctl); StwDumpObject("husk", O); if (Brain) StwDumpObject("husk brain", Brain);
                StwDumpObject("husk path following", StwPtr(Ctl, "PathFollowingComponent")); StwDumpObject("husk movement", StwPtr(O, "CharacterMovement")); } }
        }
        if (!N) Log("SyndicateStudio: ai watch: no husks");
        {
            static int Tests = 0;
            UObject* First = nullptr;
            for (auto H : StwHusks) if (TUObjectArray::GetObjectByIndex(H->Index) == H) { First = H; break; }
            if (First && Tests < 12) {
                StwNavTest("husk", First, StwPtr(StwLocalPC(), "Pawn"));
                StwFixNavBounds("husk", Tests >= 3);
                if (Tests == 2) StwRebuildNavBounds();
                Tests++;
            }
        }
        for (auto Enc : StwStartedEncounters) {
            if (!StwLooksLikeObject(Enc)) continue;
            auto Off = Enc->GetOffset("EncounterAssignments");
            if (Off == uint32(-1)) continue;
            auto& As = *(TArray<UObject*>*)((uint8_t*)Enc + Off);
            for (int32 a = 0; a < As.Num(); a++) {
                if (!StwLooksLikeObject(As[a])) continue;
                auto COff = As[a]->GetOffset("ControllersOnAssignment"), GOff = As[a]->GetOffset("Goals");
                snprintf(Line, sizeof(Line), "SyndicateStudio: ai watch: %s %s: %d controllers on it, %d goals, settings %s", StwName(Enc).c_str(), StwName(As[a]).c_str(),
                    COff == uint32(-1) ? -1 : ((TArray<uint8_t>*)((uint8_t*)As[a] + COff))->Num(), GOff == uint32(-1) ? -1 : ((TArray<uint8_t>*)((uint8_t*)As[a] + GOff))->Num(),
                    StwName(StwPtr(As[a], "AssignmentSettings")).c_str()); Log(Line);
            }
        }
        // The husks have a goal but stand still: is there navigation data (a nav mesh) for them to walk on?
        static bool NavDumped = false;
        if (!NavDumped && N) {
            NavDumped = true;
            Log("SyndicateStudio: ai watch: navigation system functions"); StwListFunctions(World ? StwPtr((UObject*)World, "NavigationSystem") : nullptr);
            StwDumpObject("husk nav mesh", World ? StwPtr(StwPtr((UObject*)World, "NavigationSystem"), "MainNavData") : nullptr);
            StwDumpObject("navigation system", World ? StwPtr((UObject*)World, "NavigationSystem") : nullptr);
            int NavData = 0;
            for (int32 i = 0, Count = TUObjectArray::Num(); i < Count; i++) {
                auto O = (UObject*)TUObjectArray::GetObjectByIndex(i);
                if (!O || !O->Class || O->IsDefaultObject()) continue;
                std::string C(O->Class->Name.ToString().c_str());
                if (C.find("NavMesh") == std::string::npos && C.find("NavigationData") == std::string::npos) continue;
                if (NavData++ < 8) { snprintf(Line, sizeof(Line), "SyndicateStudio: ai watch: nav data %s (%s) outer %s", StwName(O).c_str(), C.c_str(), StwName(O->Outer).c_str()); Log(Line); }
            }
            snprintf(Line, sizeof(Line), "SyndicateStudio: ai watch: %d nav data objects", NavData); Log(Line);
        }
    }
    void StwAIWatch() { __try { StwAIWatch_Impl(); } __except (EXCEPTION_EXECUTE_HANDLER) { Log("SyndicateStudio: ai watch crashed (caught)"); } }
    // Once the husks went for the player the game crashed twice on the same broken pointer (0x6a28a, 0x6a288):
    // first in the goal settings lookup (an encounter assignment's +0x58), then in garbage collection, which
    // follows every object property. Every 2 s, check the encounters and their assignments: an object property
    // (or array element) that is set but not a live object is cleared and logged with the class and field.
    // A FortAIAssignmentSettings to give an encounter assignment: a loaded instance (from the map or a data
    // asset) if there is one, else the default object of the property's class.
    UObject* StwAssignmentSettings(const UClass* Cls) {
        static UObject* Found = nullptr;
        if (Found && TUObjectArray::GetObjectByIndex(Found->Index) == Found) return Found;
        Found = nullptr;
        // CircleGoalsAssignment (the first one loaded) is not an enemy assignment and has no goal queries, and the
        // husks were never put on it. List them all once and prefer an enemy assignment with goal queries.
        char Line[300];
        int Best = -1;
        const int32 Count = TUObjectArray::Num();
        for (int32 i = 0; i < Count; i++) {
            auto O = (UObject*)TUObjectArray::GetObjectByIndex(i);
            if (!O || !O->Class || O->IsDefaultObject()) continue;
            if (Cls ? !O->IsA(Cls) : std::string(O->Class->Name.ToString().c_str()).find("AssignmentSettings") == std::string::npos) continue;
            auto EOff = O->GetOffset("bIsEnemyAssignment"), QOff = O->GetOffset("GoalSelectionQueryInfos");
            bool Enemy = EOff != uint32(-1) && GetFromOffset<uint8_t>(O, EOff) != 0;
            int32 Queries = QOff == uint32(-1) ? 0 : ((TArray<uint8_t>*)((uint8_t*)O + QOff))->Num();
            std::string N = StwName(O);
            int Score = (Enemy ? 4 : 0) + (Queries > 0 ? 2 : 0) + (N.find("Encounter") != std::string::npos || N.find("Enemy") != std::string::npos ? 1 : 0);
            snprintf(Line, sizeof(Line), "SyndicateStudio: ai: assignment settings %s (%s) enemy %d, %d goal queries", N.c_str(), StwName(O->Class).c_str(), Enemy, Queries); Log(Line);
            if (Score > Best) { Best = Score; Found = O; }
        }
        if (!Found && Cls) Found = (UObject*)Cls->GetDefaultObj();
        return Found;
    }
    bool StwSanitizeObject(UObject* O, int Depth, int& Fixed) {
        if (!StwLooksLikeObject(O)) return false;
        for (auto C = (const UStruct*)O->Class; C; C = C->GetSuper()) {
            std::string CName(((const UField*)C)->GetName().ToString().c_str());
            if (CName == "Object" || CName == "Actor") break;
            for (const UField* P = C->GetChildren(); P; P = P->GetNext()) {
                if (!P->Class) continue;
                std::string Type(P->Class->Name.ToString().c_str());
                uint32 Off = GetFromOffset<uint32>(P, Offsets::Offset_Internal);
                if (Type == "ObjectProperty" || Type == "ClassProperty") {
                    auto& V = *(UObject**)((uint8_t*)O + Off);
                    std::string PName(P->GetName().ToString().c_str());
                    // The assignment's settings come from the encounter's DefaultEncounterAssignmentSettings, which
                    // the tutorial encounter leaves empty: the assignment then holds garbage (0x6a289, neither an
                    // object nor a name) and the husks get no goal, so they stand still. Give both a real
                    // FortAIAssignmentSettings (a loaded one, or the class default object).
                    bool IsSettings = PName == "AssignmentSettings" || PName == "DefaultEncounterAssignmentSettings";
                    if (IsSettings && (!V || !StwLooksLikeObject(V))) {
                        auto Want = StwAssignmentSettings(GetFromOffset<const UClass*>(P, 0x70));
                        char Line[400]; snprintf(Line, sizeof(Line), "SyndicateStudio: ai: %s::%s (+0x%x = %p) on %s set to %s (%s)", CName.c_str(), PName.c_str(), Off, (void*)V, StwName(O).c_str(),
                            StwName(Want).c_str(), Want ? StwName(Want->Class).c_str() : "-"); Log(Line);
                        V = Want; Fixed++;
                    } else if (V && !StwLooksLikeObject(V)) {
                        char Line[400]; snprintf(Line, sizeof(Line), "SyndicateStudio: ai: cleared broken %s::%s (+0x%x = %p) on %s", CName.c_str(), PName.c_str(), Off, (void*)V, StwName(O).c_str()); Log(Line);
                        V = nullptr; Fixed++;
                    }
                } else if (Type == "StructProperty") {
                    auto S = GetFromOffset<const UStruct*>(P, 0x70);
                    for (const UField* F = S ? S->GetChildren() : nullptr; F; F = F->GetNext()) {
                        if (!F->Class || std::string(F->Class->Name.ToString().c_str()) != "ObjectProperty") continue;
                        uint32 FO = GetFromOffset<uint32>(F, Offsets::Offset_Internal);
                        auto& V = *(UObject**)((uint8_t*)O + Off + FO);
                        if (V && !StwLooksLikeObject(V)) {
                            char Line[300]; snprintf(Line, sizeof(Line), "SyndicateStudio: ai: cleared broken %s::%s.%s (+0x%x = %p) on %s", CName.c_str(), std::string(P->GetName().ToString().c_str()).c_str(),
                                std::string(F->GetName().ToString().c_str()).c_str(), Off + FO, (void*)V, StwName(O).c_str()); Log(Line);
                            V = nullptr; Fixed++;
                        }
                    }
                } else if (Type == "ArrayProperty") {
                    auto Inner = GetFromOffset<const UField*>(P, 0x70);
                    if (!Inner || !Inner->Class || std::string(Inner->Class->Name.ToString().c_str()) != "ObjectProperty") continue;
                    auto& Arr = *(TArray<UObject*>*)((uint8_t*)O + Off);
                    for (int32 i = 0; i < Arr.Num() && i < 256; i++) {
                        auto& V = Arr[i];
                        if (!V) continue;
                        if (!StwLooksLikeObject(V)) {
                            char Line[300]; snprintf(Line, sizeof(Line), "SyndicateStudio: ai: cleared broken %s::%s[%d] (%p) on %s", CName.c_str(), std::string(P->GetName().ToString().c_str()).c_str(), i, (void*)V, StwName(O).c_str()); Log(Line);
                            V = nullptr; Fixed++;
                        } else if (Depth < 1) StwSanitizeObject(V, Depth + 1, Fixed);
                    }
                }
            }
        }
        return true;
    }
    // With real settings the husks still stood behind the fence: their assignment had no goals (the mission blueprint
    // adds them while the settings were still garbage), so nothing told them to go for the player or the objective.
    // When an encounter's assignment has no goals, add the player's pawn and the encounter's target objective
    // through the mission's AddGoalToEncounterAssignment (parameters filled by type).
    void StwGiveEncounterGoals_Impl(UObject* Enc) {
        char Line[400];
        auto Off = Enc->GetOffset("EncounterAssignments");
        if (Off == uint32(-1)) return;
        auto& Assignments = *(TArray<UObject*>*)((uint8_t*)Enc + Off);
        if (!Assignments.Num() || !StwLooksLikeObject(Assignments[0])) return;
        auto As = Assignments[0];
        auto GOff = As->GetOffset("Goals");
        int32 Goals = GOff == uint32(-1) ? -1 : ((TArray<uint8_t>*)((uint8_t*)As + GOff))->Num();
        static int Logged = 0;
        if (Logged++ < 3) { snprintf(Line, sizeof(Line), "SyndicateStudio: ai: %s assignment %s has %d goals", StwName(Enc).c_str(), StwName(As).c_str(), Goals); Log(Line); }
        static bool AsDumped = false;
        if (!AsDumped && Goals > 0) { AsDumped = true; StwDumpObject("encounter assignment", As); StwDumpObject("assignment settings", StwPtr(As, "AssignmentSettings"));
            // The husks were not on the assignment (ControllersOnAssignment empty): list what the assignment, the
            // goal manager and the encounter can do to put them there.
            Log("SyndicateStudio: ai: assignment functions"); StwListFunctions(As);
            auto World = UWorld::GetWorld();
            auto GM = World ? StwPtr((UObject*)World->AuthorityGameMode, "AIGoalManager") : nullptr;
            Log("SyndicateStudio: ai: goal manager functions"); StwListFunctions(GM); StwDumpObject("goal manager", GM);
            Log("SyndicateStudio: ai: encounter functions"); StwListFunctions(Enc);
            auto& G = *(TArray<uint8_t>*)((uint8_t*)As + GOff);
            auto Raw = (UObject**)G.GetData();
            for (int k = 0; k < 8; k++) { snprintf(Line, sizeof(Line), "SyndicateStudio: ai:   goal[0] +0x%x = %p %s", k * 8, (void*)Raw[k], StwLooksLikeObject(Raw[k]) ? StwName(Raw[k]).c_str() : ""); Log(Line); } }
        if (Goals != 0) return;
        static std::vector<UObject*> Tried; // once per encounter: the call may fill something other than Goals
        if (std::find(Tried.begin(), Tried.end(), Enc) != Tried.end()) return;
        auto Mission = StwTutorialMission;
        if (!StwLooksLikeObject(Mission)) { if (Logged < 6) Log("SyndicateStudio: ai: no mission to add encounter goals through"); return; }
        UObject* Pawn = nullptr;
        if (auto PC = StwLocalPC()) Pawn = StwPtr(PC, "Pawn");
        UObject* Objective = StwPtr(Enc, "TargetObjective");
        if (!StwLooksLikeObject(Pawn)) return; // the player's pawn is not in yet: try again in 2 s
        Tried.push_back(Enc);
        static bool Dumped = false;
        for (UObject* Goal : { Pawn, Objective }) {
            if (!StwLooksLikeObject(Goal)) continue;
            StwCall C(Mission, "AddGoalToEncounterAssignment");
            if (!C) { Log("SyndicateStudio: ai: mission has no AddGoalToEncounterAssignment"); return; }
            if (!Dumped) { Dumped = true; StwDumpLayout("add goal", (const UStruct*)C.F, 0); }
            for (const UField* P = ((const UStruct*)C.F)->GetChildren(); P; P = P->GetNext()) {
                if (!P->Class) continue;
                std::string Type(P->Class->Name.ToString().c_str());
                uint32 POff = GetFromOffset<uint32>(P, Offsets::Offset_Internal);
                auto Inner = GetFromOffset<const UObject*>(P, 0x70);
                std::string IName = Inner ? StwName(Inner) : std::string();
                if (Type == "ObjectProperty") {
                    UObject* V = IName.find("Encounter") != std::string::npos ? Enc : IName.find("Assignment") != std::string::npos ? As : Goal;
                    memcpy(C.Buf.data() + POff, &V, sizeof(V));
                } else if (Type == "StructProperty" && IName == "FortAIAssignmentIdentifier") {
                    auto IOff = As->GetOffset("AssignmentIdentifier");
                    if (IOff != uint32(-1)) memcpy(C.Buf.data() + POff, (uint8_t*)As + IOff, 0x30);
                }
            }
            C.Run();
            snprintf(Line, sizeof(Line), "SyndicateStudio: ai: added goal %s to %s's assignment, now %d goals", StwName(Goal).c_str(), StwName(Enc).c_str(),
                GOff == uint32(-1) ? -1 : ((TArray<uint8_t>*)((uint8_t*)As + GOff))->Num()); Log(Line);
        }
    }
    void StwGiveEncounterGoals(UObject* Enc) { __try { StwGiveEncounterGoals_Impl(Enc); } __except (EXCEPTION_EXECUTE_HANDLER) { Log("SyndicateStudio: ai: adding encounter goals crashed (caught)"); } }
    // Only the encounters the director started (kept by the StartEncounter hook): scanning every object every 2 s
    // stalled the game for 150 ms each time.
    void StwSanitizeEncounters_Impl() {
        int Fixed = 0;
        for (auto It = StwStartedEncounters.begin(); It != StwStartedEncounters.end();) {
            auto O = *It;
            if (!O || TUObjectArray::GetObjectByIndex(O->Index) != O) { It = StwStartedEncounters.erase(It); continue; }
            StwSanitizeObject(O, 0, Fixed);
            StwGiveEncounterGoals(O);
            ++It;
        }
    }
    void StwSanitizeEncounters() { __try { StwSanitizeEncounters_Impl(); } __except (EXCEPTION_EXECUTE_HANDLER) { Log("SyndicateStudio: ai: encounter check crashed (caught)"); } }
    void StwOpenTutorial() {
        auto Now = GetTickCount64();
        if (StwAIWatchUntil && Now < StwAIWatchUntil && Now >= StwAINext) { StwAINext = Now + 5000; StwAIWatch(); }
        if (StwSanitizeFrom && Now >= StwSanitizeNext) { StwSanitizeNext = Now + 2000; StwSanitizeEncounters(); }
        if (StwSanitizeFrom && !StwStartedEncounters.empty() && Now >= StwDriveNext) { StwDriveNext = Now + 1000; StwDriveHusks(); }
        { static ULONGLONG TrapNext = 0; if (Now >= TrapNext) { TrapNext = Now + 1000; StwFixTraps(); StwTrapHighlights(); StwRepNotifies(); StwWatchObjectives(); StwOpenLaserWalls();
            // Rescan for mission event handlers only twice more (blueprints loaded after the hook), not every 15 s.
            static ULONGLONG EvStart = Now; static int EvScans = 0;
            if (EvScans < 2 && Now >= EvStart + (EvScans ? 60000 : 20000)) { EvScans++; StwScanEventFuncsSafe(); } } }

        if (!StwPendingCrafts.empty()) StwCraftTickSafe();
        if (StwCancelUntil && Now < StwCancelUntil && Now >= StwNextCancel) { StwNextCancel = Now + 1000; StwCancelMatchmaking(); }
        if (StwKickAt && Now >= StwKickAt) { StwKickAt = 0; StwKickZone(); }
        if (StwWatchUntil && Now < StwWatchUntil && StwWorldManager) StwWatch();
        if (StwRestartAt && Now >= StwRestartAt) { StwRestartAt = 0; StwRestart(); }
        if (StwDumpAt && Now >= StwDumpAt) { StwDumpAt = 0; StwDumpState(); }
        if (StwDressAt && Now >= StwDressAt) { StwDressAt = 0; StwDress(); }
        if (!StwOpenPending.exchange(false)) return;
        StwOpened.store(true);
        auto World = UWorld::GetWorld();
        if (!World) { Log("SyndicateStudio: no world to open the tutorial from"); return; }
        Log(StwCancelMatchmaking() ? "SyndicateStudio: matchmaking cancelled" : "SyndicateStudio: no matchmaking to cancel");
        // The zone checks its mission data while the map loads ("ZoneInfo Invalid!") and only then sets up the
        // mission, the spawn pad and the player's hero. If the world record already exists here (it belongs to
        // the game, not the map), fill it before opening the map.
        StwHookZoneCheck(); StwHookWorldQuery(); StwAllowServerClasses();
        StwCancelUntil = Now + 20000; StwNextCancel = Now + 1000; StwKickAt = Now + 8000; StwDumpAt = Now + 20000;
        UKismetSystemLibrary::ExecuteConsoleCommand(World, FString(StwTutorialUrl), nullptr);
        Log("SyndicateStudio: opening the Save the World tutorial zone");
    }

    LRESULT CALLBACK OnMessage(int Code, WPARAM W, LPARAM L) {
        // Free cam: the game gets none of your keys, clicks or wheel (no pickaxe swing, no building); the Studio does
        if (Code >= 0 && L) StudioCamera::FilterInput(reinterpret_cast<MSG*>(L), (W & PM_REMOVE) != 0);
        if (Code >= 0 && W == PM_REMOVE && reinterpret_cast<MSG*>(L)->message == StudioMessage) {
            MessagePending.store(false);
            StwOpenTutorial();
            // Execute pending pipe requests on the game thread
            StudioPipe::ProcessPendingRequests();
            // Tick the free camera
            StudioCamera::Tick();
        }
        return CallNextHookEx(MessageHook, Code, W, L);
    }

    BOOL CALLBACK FindGameWindow(HWND Window, LPARAM) {
        DWORD Pid = 0; GetWindowThreadProcessId(Window, &Pid);
        wchar_t Class[128]{}; GetClassNameW(Window, Class, 128);
        if (Pid == GetCurrentProcessId() && IsWindowVisible(Window) && wcsstr(Class, L"UnrealWindow")) {
            GameWindow = Window; return FALSE;
        }
        return TRUE;
    }

    DWORD WINAPI StudioMain(void*) {
        Log("SyndicateStudio: starting");

        // Wait for the game window (up to 120 seconds)
        for (int Attempt = 0; Attempt < 240 && !GameWindow; ++Attempt) {
            EnumWindows(FindGameWindow, 0);
            if (!GameWindow) Sleep(500);
        }
        if (!GameWindow) { Log("SyndicateStudio: game window not found, exiting"); return 0; }

        char Line[128]; snprintf(Line, sizeof(Line), "SyndicateStudio: game window found (HWND %p)", GameWindow);
        Log(Line);

        // Initialize the SDK (UObject system, version detection)
        SDK::Init();

        snprintf(Line, sizeof(Line), "SyndicateStudio: SDK init done, engine %.2f fortnite %.2f",
            VersionInfo.EngineVersion, VersionInfo.FortniteVersion);
        Log(Line);

        StwLocal = GetFileAttributesW((StwShared() + L"stw-local.flag").c_str()) != INVALID_FILE_ATTRIBUTES;
        if (StwLocal) Log("SyndicateStudio: Save the World local tutorial is on");

        // Start the named pipe server
        StudioPipe::Init();
        Log("SyndicateStudio: pipe server started");

        // Install the message hook and keep the game thread ticking
        int MissingWindow = 0;
        while (MissingWindow < 240) {
            if (!IsWindow(GameWindow)) {
                if (MessageHook) { UnhookWindowsHookEx(MessageHook); MessageHook = nullptr; }
                GameWindow = nullptr; EnumWindows(FindGameWindow, 0);
            }
            MissingWindow = GameWindow ? 0 : MissingWindow + 1;
            if (GameWindow) {
                if (!MessageHook) {
                    DWORD Tid = GetWindowThreadProcessId(GameWindow, nullptr);
                    MessageHook = SetWindowsHookExW(WH_GETMESSAGE, OnMessage, nullptr, Tid);
                }
                StwPoll();
                PostStudioMessage();
            }
            // The free cam moves every game frame (the message is only re-posted once the last one ran, so this
            // never floods the queue); otherwise ~60 Hz is plenty, and pipe requests wake the game thread directly.
            Sleep(StudioCamera::IsFreeCam() ? 1 : 16);
        }

        Log("SyndicateStudio: game window lost, shutting down");
        if (MessageHook) { UnhookWindowsHookEx(MessageHook); MessageHook = nullptr; }
        StudioPipe::Shutdown();
        return 0;
    }
}

// The game window HWND, for the pipe server to report
HWND StudioGetGameWindow() { return GameWindow; }

// Shared log line for the other Studio files (syndicate-studio.log)
void StudioLog(const char* Text) { Log(Text); }

// Called by the pipe thread when a request is queued: run it on the next message pump instead of the next tick
void StudioWake() {
    PostStudioMessage();
}

BOOL APIENTRY DllMain(HMODULE Module, DWORD Reason, LPVOID) {
    if (Reason == DLL_PROCESS_ATTACH) {
        SelfModule = Module;
        DisableThreadLibraryCalls(Module);
        if (auto Thread = CreateThread(nullptr, 0, StudioMain, nullptr, 0, nullptr))
            CloseHandle(Thread);
    }
    else if (Reason == DLL_PROCESS_DETACH) {
        StudioPipe::Shutdown();
        if (MessageHook) { UnhookWindowsHookEx(MessageHook); MessageHook = nullptr; }
    }
    return TRUE;
}
