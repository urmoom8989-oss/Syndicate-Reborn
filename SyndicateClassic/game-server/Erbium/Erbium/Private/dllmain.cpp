// dllmain.cpp : Defines the entry point for the DLL application.
#include "pch.h"
#include "../Public/Utils.h"
#include <thread>
#include <iostream>
#include "../Public/Finders.h"
#include "../../FortniteGame/Public/FortInventory.h"
#include <chrono>
#include "../Public/Configuration.h"
#include "../Public/Misc.h"
#include "../Public/GUI.h"
#include "../../Erbium/Plugins/CrashReporter/Public/CrashReporter.h"
#include "../../FortniteGame/Public/FortPlayerControllerAthena.h"
#include "../../Engine/Public/NetDriver.h"
#include <fstream>
#include <iterator>

#pragma comment(lib, "libcurl/libcurl.lib")
#pragma comment(lib, "libcurl/zlib.lib")
#pragma comment(lib, "Ws2_32.lib")
#pragma comment(lib, "Wldap32.lib")
#pragma comment(lib, "Crypt32.lib")
#pragma comment(lib, "Normaliz.lib")

// Syndicate: some cosmetics (e.g. Glow in 10.31) live in encrypted packs. The game client unlocks them
// with the backend's keychain at login; the game server never logs in, so it could not load them and
// gave players the default outfit. Feed the same keychain to the game's ApplyContentKeyChain command.
// The exact line format the command expects is not known, so every likely format is written; lines in
// a format it does not accept are only logged as parse warnings.
static std::string SyndicateBase64ToHex(const std::string& In)
{
    static const std::string Chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string Bytes; int Value = 0, Bits = -8;
    for (char C : In) { auto At = Chars.find(C); if (At == std::string::npos) break; Value = (Value << 6) + (int)At; Bits += 6; if (Bits >= 0) { Bytes += char((Value >> Bits) & 0xFF); Bits -= 8; } }
    static const char* Digits = "0123456789ABCDEF"; std::string Out;
    for (unsigned char B : Bytes) { Out += Digits[B >> 4]; Out += Digits[B & 15]; }
    return Out;
}
// Direct route (used as well, in case the console command format is not accepted): find the engine's
// FPakPlatformFile::RegisterEncryptionKey through its log message, find the pak platform object through
// the delegate that binds it, and register every key the way the client does after downloading them.
static uintptr_t SyndicateFindRegisterKeyFn()
{
    auto Base = (uint8_t*)GetModuleHandleW(nullptr); auto Nt = (IMAGE_NT_HEADERS64*)(Base + ((IMAGE_DOS_HEADER*)Base)->e_lfanew);
    auto Sections = IMAGE_FIRST_SECTION(Nt);
    static const wchar_t Text[] = L"Registered encryption key '%s': %d pak files mounted, %d remain pending";
    const size_t Len = sizeof(Text) - sizeof(wchar_t);
    uint8_t* String = nullptr;
    for (int i = 0; i < Nt->FileHeader.NumberOfSections && !String; i++)
    {
        if (Sections[i].Characteristics & IMAGE_SCN_MEM_EXECUTE) continue;
        auto Start = Base + Sections[i].VirtualAddress; size_t Size = Sections[i].Misc.VirtualSize;
        for (size_t o = 0; o + Len <= Size; o += 2) if (memcmp(Start + o, Text, Len) == 0) { String = Start + o; break; }
    }
    if (!String) return 0;
    for (int i = 0; i < Nt->FileHeader.NumberOfSections; i++)
    {
        if (!(Sections[i].Characteristics & IMAGE_SCN_MEM_EXECUTE)) continue;
        auto Start = Base + Sections[i].VirtualAddress; size_t Size = Sections[i].Misc.VirtualSize;
        for (size_t o = 0; o + 7 <= Size; o++)
        {
            if ((Start[o] != 0x48 && Start[o] != 0x4C) || Start[o + 1] != 0x8D || (Start[o + 2] & 0xC7) != 0x05) continue;
            int32_t Disp; memcpy(&Disp, Start + o + 3, 4);
            if (Start + o + 7 + Disp != String) continue;
            DWORD64 ImageBase = 0; auto Function = RtlLookupFunctionEntry((DWORD64)(Start + o), &ImageBase, nullptr); if (!Function) return 0;
            for (int Depth = 0; Depth < 8; Depth++)
            {
                auto Info = (uint8_t*)(ImageBase + Function->UnwindData);
                if (!((Info[0] >> 3) & 4)) break; // not a chained fragment
                Function = (PRUNTIME_FUNCTION)(Info + 4 + ((Info[2] + 1) & ~1) * 2);
            }
            return (uintptr_t)(ImageBase + Function->BeginAddress);
        }
    }
    return 0;
}
static uintptr_t SyndicateFindRegisterKeyFnTry() { __try { return SyndicateFindRegisterKeyFn(); } __except (1) { return 0; } }
static uintptr_t SyndicateFindRegisterKeyFnSafe() { FErbiumProbeScope Probe; return SyndicateFindRegisterKeyFnTry(); }
static bool SyndicateReadQword(uintptr_t At, uint64_t& Out) { __try { Out = *(const uint64_t*)At; return true; } __except (1) { return false; } }
// The engine binds RegisterEncryptionKey to a raw delegate: [vtable][object][method]. Find that method
// pointer in memory; the object stored before it is the pak platform file.
static uintptr_t SyndicateFindPakPlatform(uintptr_t Fn)
{
    // Other threads free memory while this scans; a page can vanish between VirtualQuery and the read.
    FErbiumProbeScope Probe;
    auto Base = (uintptr_t)GetModuleHandleW(nullptr); auto Nt = (IMAGE_NT_HEADERS64*)(Base + ((IMAGE_DOS_HEADER*)Base)->e_lfanew);
    uintptr_t End = Base + Nt->OptionalHeader.SizeOfImage;
    MEMORY_BASIC_INFORMATION Info{};
    for (uintptr_t At = 0x10000; VirtualQuery((void*)At, &Info, sizeof(Info)) == sizeof(Info);)
    {
        uintptr_t Next = (uintptr_t)Info.BaseAddress + Info.RegionSize; if (Next <= At) break;
        bool Readable = Info.State == MEM_COMMIT && Info.Type == MEM_PRIVATE && !(Info.Protect & (PAGE_GUARD | PAGE_NOACCESS)) && (Info.Protect & (PAGE_READWRITE | PAGE_EXECUTE_READWRITE));
        if (Readable)
            for (uintptr_t Slot = (uintptr_t)Info.BaseAddress + 16; Slot + 8 <= Next; Slot += 8)
            {
                uint64_t Value = 0; if (!SyndicateReadQword(Slot, Value)) break;
                if (Value != Fn) continue;
                uint64_t Object = 0, Table = 0, ObjectTable = 0;
                if (!SyndicateReadQword(Slot - 8, Object) || !SyndicateReadQword(Slot - 16, Table) || !Object) continue;
                if (Table < Base || Table >= End || !SyndicateReadQword(Object, ObjectTable) || ObjectTable < Base || ObjectTable >= End) continue;
                return (uintptr_t)Object;
            }
        At = Next;
    }
    return 0;
}
static bool SyndicateRegisterKeyTry(uintptr_t Fn, uintptr_t PakPlatform, const uint32_t* Guid, const uint8_t* Key)
{
    __try { ((void (*)(void*, const uint32_t*, const uint8_t*))Fn)((void*)PakPlatform, Guid, Key); return true; }
    __except (1) { return false; }
}
static bool SyndicateRegisterKey(uintptr_t Fn, uintptr_t PakPlatform, const uint32_t* Guid, const uint8_t* Key)
{
    FErbiumProbeScope Probe; return SyndicateRegisterKeyTry(Fn, PakPlatform, Guid, Key);
}
static bool SyndicateGlowLoadable()
{
    if (VersionInfo.FortniteVersion < 10.30) return false;
    auto Class = FindClass("AthenaCharacterItemDefinition");
    return Class && StaticLoadObject(L"/Game/Athena/Items/Cosmetics/Characters/CID_479_Athena_Commando_F_Davinci.CID_479_Athena_Commando_F_Davinci", Class) != nullptr;
}

static void SyndicateApplyKeychain()
{
    // The keychain holds Season X (10.31) pak keys only; other seasons have nothing to unlock.
    if (VersionInfo.FortniteVersion < 10.30 || VersionInfo.FortniteVersion >= 11) return;
    wchar_t Ready[32768]{}; if (!GetEnvironmentVariableW(L"TILTED_SERVER_READY_FILE", Ready, 32768)) return;
    std::wstring Server = Ready; auto Slash = Server.find_last_of(L"\\/"); if (Slash == std::wstring::npos) return; Server.resize(Slash + 1);
    // The backend folder is Syndicate-backend; installs the launcher has not renamed yet still use the old name.
    std::ifstream In(Server + L"..\\..\\Syndicate-backend\\src\\local\\KeyChain\\keychain.json", std::ios::binary);
    if (!In) In.open(Server + L"..\\..\\Helix-backend-main\\src\\local\\KeyChain\\keychain.json", std::ios::binary);
    if (!In) { printf("[Syndicate] keychain.json not found; encrypted cosmetics stay locked on the server\n"); return; }
    std::string Text((std::istreambuf_iterator<char>(In)), {});
    std::vector<std::pair<std::string, std::string>> Keys;
    for (size_t P = 0; (P = Text.find('"', P)) != std::string::npos;)
    {
        auto End = Text.find('"', P + 1); if (End == std::string::npos) break;
        auto Entry = Text.substr(P + 1, End - P - 1); P = End + 1;
        auto Colon = Entry.find(':'); if (Colon != 32) continue;
        auto Next = Entry.find(':', Colon + 1);
        Keys.emplace_back(Entry.substr(0, 32), Entry.substr(Colon + 1, Next == std::string::npos ? std::string::npos : Next - Colon - 1));
    }
    if (Keys.empty()) return;
    CreateDirectoryW((Server + L"keychain").c_str(), nullptr);
    const wchar_t* Names[4] = { L"guid-base64-released.txt", L"guid-base64.txt", L"guid-hex-released.txt", L"guid-hex.txt" };
    for (int Variant = 0; Variant < 4; Variant++)
    {
        auto File = Server + L"keychain\\" + Names[Variant];
        {
            std::ofstream Out(File, std::ios::binary | std::ios::trunc);
            for (auto& [Guid, Key] : Keys) Out << Guid << ":" << (Variant < 2 ? Key : SyndicateBase64ToHex(Key)) << (Variant % 2 == 0 ? ":RELEASED" : "") << "\n";
        }
        wchar_t Short[MAX_PATH]{};
        std::wstring Path = GetShortPathNameW(File.c_str(), Short, MAX_PATH) ? std::wstring(Short) : L"\"" + File + L"\"";
        UKismetSystemLibrary::ExecuteConsoleCommand(UWorld::GetWorld(), FString((L"ApplyContentKeyChain " + Path).c_str()), nullptr);
    }
    auto Fn = SyndicateFindRegisterKeyFnSafe();
    auto PakPlatform = Fn ? SyndicateFindPakPlatform(Fn) : 0;
    int Registered = 0;
    if (Fn && PakPlatform)
        for (auto& [Guid, Key] : Keys)
        {
            uint32_t G[4]{}; bool Valid = true;
            for (int Part = 0; Part < 4; Part++) { auto Hex = Guid.substr(Part * 8, 8); char* EndPtr = nullptr; G[Part] = (uint32_t)strtoul(Hex.c_str(), &EndPtr, 16); if (!EndPtr || *EndPtr) Valid = false; }
            auto Bytes = SyndicateBase64ToHex(Key); if (!Valid || Bytes.size() != 64) continue;
            uint8_t K[32]{}; for (int b = 0; b < 32; b++) K[b] = (uint8_t)strtoul(Bytes.substr(b * 2, 2).c_str(), nullptr, 16);
            if (SyndicateRegisterKey(Fn, PakPlatform, G, K)) ++Registered;
        }
    auto Base = (uintptr_t)GetModuleHandleW(nullptr);
    printf("[Syndicate] content keychain: %d keys offered by command; direct register fn=%s+0x%llx pak=%s registered=%d; Glow loadable=%d\n",
        (int)Keys.size(), Fn ? "game" : "none", Fn ? (unsigned long long)(Fn - Base) : 0ull, PakPlatform ? "found" : "none", Registered, (int)SyndicateGlowLoadable());
}

#include <timeapi.h>
#pragma comment(lib, "winmm.lib")
// Server timing. The match server runs hidden in the background, so Windows 11 throttles it (EcoQoS) and keeps the
// default 15.6 ms timer: the engine's frame-rate sleep then overslept, the server ran at about 25 ticks a second
// instead of 30 with 47 ms frames even with nobody in it, and players on another PC (Radmin) rubber-banded. Opt out of
// the throttling (including the timer resolution clamp for background processes) and ask for a 1 ms timer.
//
// Priority: the server and the player's client usually share one PC. Running the CPU-heavy server ABOVE normal made
// the client's audio thread and renderer stall (audio cutting in/out, FPS drops). Default the server to BELOW normal
// so the client wins when both need the CPU; TILTED_SERVER_PRIORITY=normal/above/high overrides it for dedicated hosts.
static void SyndicateServerTiming()
{
#ifndef PROCESS_POWER_THROTTLING_IGNORE_TIMER_RESOLUTION
#define PROCESS_POWER_THROTTLING_IGNORE_TIMER_RESOLUTION 0x4
#endif
    PROCESS_POWER_THROTTLING_STATE State{};
    State.Version = PROCESS_POWER_THROTTLING_CURRENT_VERSION;
    State.ControlMask = PROCESS_POWER_THROTTLING_EXECUTION_SPEED | PROCESS_POWER_THROTTLING_IGNORE_TIMER_RESOLUTION;
    State.StateMask = 0; // not throttled, and the timer resolution asked for below applies
    const bool Unthrottled = SetProcessInformation(GetCurrentProcess(), ProcessPowerThrottling, &State, sizeof(State)) != 0;
    const bool Timer = timeBeginPeriod(1) == TIMERR_NOERROR;
    DWORD PriorityClass = ABOVE_NORMAL_PRIORITY_CLASS;
    wchar_t PriorityEnv[16]{};
    if (GetEnvironmentVariableW(L"TILTED_SERVER_PRIORITY", PriorityEnv, 16))
    {
        if (_wcsicmp(PriorityEnv, L"high") == 0) PriorityClass = HIGH_PRIORITY_CLASS;
        else if (_wcsicmp(PriorityEnv, L"above") == 0) PriorityClass = ABOVE_NORMAL_PRIORITY_CLASS;
        else if (_wcsicmp(PriorityEnv, L"normal") == 0) PriorityClass = NORMAL_PRIORITY_CLASS;
        else if (_wcsicmp(PriorityEnv, L"below") == 0) PriorityClass = BELOW_NORMAL_PRIORITY_CLASS;
    }
    const bool Priority = SetPriorityClass(GetCurrentProcess(), PriorityClass) != 0;
    printf("[Syndicate] server timing: unthrottled=%d timer1ms=%d priority=%lu tickrate=%d\n",
        int(Unthrottled), int(Timer), (unsigned long)PriorityClass, FConfiguration::MaxTickRate);
}

extern void (*volatile SyndicateRunOnGameThread)(); // Misc.cpp
// The part of Main that changes the running game (client/server flags, the local player, the map); see Main.
static volatile bool SyndicateServerWorldStarted = false;
static void SyndicateStartServerWorld()
{
    *(bool*)FindGIsClient() = false;
    if (VersionInfo.EngineVersion > 4.20) // 3.6 and below have a crash on ALandscapeProxy
        *(bool*)FindGIsServer() = true;

    srand((uint32_t)time(0));

    UWorld::GetWorld()->OwningGameInstance->LocalPlayers.Remove(0);
    const wchar_t* terrainOpen = L"open Athena_Terrain";

    if (wcsstr(FConfiguration::Playlist, L"/MoleGame/Playlists/Playlist_MoleGame"))
    {
        UKismetSystemLibrary::ExecuteConsoleCommand(UWorld::GetWorld(), FString(L"Mole.WorstCasePlayerCount 1"), nullptr);
        terrainOpen = L"open Mole_UnderBase_Parent";
    }
    else if (VersionInfo.FortniteVersion >= 12.00 && wcsstr(FConfiguration::Playlist, L"/Game/Athena/Playlists/Creative/Playlist_PlaygroundV2.Playlist_PlaygroundV2"))
        terrainOpen = L"open Creative_NoApollo_Terrain";
    else
    {
        if (VersionInfo.FortniteVersion >= 27.00)
        {
            if (VersionInfo.FortniteVersion >= 28.00)
                terrainOpen = L"open Helios_Terrain";
        }
        else if (VersionInfo.FortniteVersion >= 23.00)
            terrainOpen = L"open Asteria_Terrain";
        else if (VersionInfo.FortniteVersion >= 19.00)
            terrainOpen = L"open Artemis_Terrain";
        else if (VersionInfo.FortniteVersion >= 11.00)
            terrainOpen = L"open Apollo_Terrain";
    }

    // Save the World dev host (dev launcher, Pre-season/Season 1, Documents\Syndicate\stw-host.txt): start straight on
    // the tutorial zone with its zone game mode instead of the Battle Royale island. The client's empty-server
    // reservation then lands on this map, and Battle Royale setup stays out of the way (SyndicateStwZoneTravel).
    {
        wchar_t Flag[4]{};
        if (VersionInfo.FortniteVersion < 2.0 && GetEnvironmentVariableW(L"SYNDICATE_STW_HOST", Flag, 4) == 1 && Flag[0] == L'1')
        {
            extern bool SyndicateStwZoneTravel;
            SyndicateStwZoneTravel = true;
            terrainOpen = L"open /Game/Maps/Zones/TheFarmstead/Zone_Onboarding_FarmsteadFort?game=/Game/Blueprints/GameModeZone.GameModeZone_C";
            printf("[Syndicate STW] host starts on the tutorial zone\n");
        }
    }
    SyndicateApplyKeychain();
    UKismetSystemLibrary::ExecuteConsoleCommand(UWorld::GetWorld(), FString(terrainOpen), nullptr);
    SyndicateServerWorldStarted = true;
}

void Main()
{
    SyndicateServerTiming();
    wchar_t selectedPlaylist[512]{};
    GetEnvironmentVariableW(L"TILTED_PLAYLIST", selectedPlaylist, 512);
    if (_wcsicmp(selectedPlaylist,L"squads")==0 || _wcsicmp(selectedPlaylist,L"squad")==0 ||
        _wcsicmp(selectedPlaylist,L"Playlist_DefaultSquad")==0 ||
        _wcsicmp(selectedPlaylist,L"/Game/Athena/Playlists/Playlist_DefaultSquad.Playlist_DefaultSquad")==0)
        FConfiguration::Playlist=L"/Game/Athena/Playlists/Playlist_DefaultSquad.Playlist_DefaultSquad";
    else if (_wcsicmp(selectedPlaylist,L"duos")==0 || _wcsicmp(selectedPlaylist,L"duo")==0 ||
        _wcsicmp(selectedPlaylist,L"Playlist_DefaultDuo")==0 ||
        _wcsicmp(selectedPlaylist,L"/Game/Athena/Playlists/Playlist_DefaultDuo.Playlist_DefaultDuo")==0)
        FConfiguration::Playlist=L"/Game/Athena/Playlists/Playlist_DefaultDuo.Playlist_DefaultDuo";
    // Arena (Seasons 8+). If this path is wrong the server finds the playlist by name.
    else if (_wcsicmp(selectedPlaylist,L"arenasolo")==0 || _wcsicmp(selectedPlaylist,L"Playlist_ShowdownAlt_Solo")==0)
        FConfiguration::Playlist=L"/Game/Athena/Playlists/Showdown/Playlist_ShowdownAlt_Solo.Playlist_ShowdownAlt_Solo";
    // 50v50 (Season 4). The launcher already passes "50v50"; without this branch the
    // server silently fell back to Solo.
    else if (_wcsicmp(selectedPlaylist,L"50v50")==0 || _wcsicmp(selectedPlaylist,L"Playlist_50v50")==0)
        FConfiguration::Playlist=L"/Game/Athena/Playlists/Playlist_50v50.Playlist_50v50";
    // Playground (Season 4, 4.5): the game's own LTM. Its playlist sets the respawns, materials and no storm damage.
    else if (_wcsicmp(selectedPlaylist,L"playground")==0 || _wcsicmp(selectedPlaylist,L"Playlist_Playground")==0)
        FConfiguration::Playlist=L"/Game/Athena/Playlists/Playground/Playlist_Playground.Playlist_Playground";
    // Creative (Seasons 7-X): the hub with player islands; players join at any time.
    else if (_wcsicmp(selectedPlaylist,L"creative")==0 || _wcsicmp(selectedPlaylist,L"Playlist_PlaygroundV2")==0)
    {
        FConfiguration::Playlist=L"/Game/Athena/Playlists/Creative/Playlist_PlaygroundV2.Playlist_PlaygroundV2";
        FConfiguration::bJoinInProgress=true;
    }
    // Chapter 2 entrance (11.00, an account's first match): Fortnite's own intro playlist (found by name if the folder differs).
    else if (_wcsicmp(selectedPlaylist,L"intro")==0 || _wcsicmp(selectedPlaylist,L"Playlist_Intro_Apollo_NewPlayer")==0)
        FConfiguration::Playlist=L"/Game/Athena/Playlists/Playlist_Intro_Apollo_NewPlayer.Playlist_Intro_Apollo_NewPlayer";
    else if (_wcsicmp(selectedPlaylist,L"arenatrios")==0 || _wcsicmp(selectedPlaylist,L"Playlist_ShowdownAlt_Trios")==0) // Arena Trios removed (2026-10-30): falls back to Arena Solo
        FConfiguration::Playlist=L"/Game/Athena/Playlists/Showdown/Playlist_ShowdownAlt_Solo.Playlist_ShowdownAlt_Solo";
    wchar_t managedLog[32768]{};
    const bool managed = GetEnvironmentVariableW(L"TILTED_SERVER_LOG", managedLog, 32768) != 0;
    if (managed)
    {
        // CrashReportClient can inherit a dead host's handles. Each host gets
        // distinct files, and a failed freopen must never reach setvbuf.
        const auto runLog=std::wstring(managedLog)+L"."+std::to_wstring(GetCurrentProcessId())+L".log";
        // _wfreopen (not _wfreopen_s): the _s version opens the file with no sharing at all, so the launcher's
        // error report could not read the log while the server (or a crash reporter holding its handle) ran.
#pragma warning(push)
#pragma warning(disable: 4996)
        FILE* output=_wfreopen(runLog.c_str(),L"w",stdout);
        if(output)
            setvbuf(output,nullptr,_IONBF,0);
        const auto errorLog=runLog+L".stderr.log";
        FILE* errors=_wfreopen(errorLog.c_str(),L"w",stderr);
        if(errors)
            setvbuf(errors,nullptr,_IONBF,0);
#pragma warning(pop)
    }
    if constexpr (!FConfiguration::bGUI)
        if (!managed) AllocConsole();

    if constexpr (!FConfiguration::bGUI || !FConfiguration::bUseStdoutLog)
    {
        if (!managed && (!FConfiguration::bGUI || GetConsoleWindow()))
        {
            FILE* s;
            freopen_s(&s, "CONOUT$", "w", stdout);
            freopen_s(&s, "CONOUT$", "w+", stderr);
            freopen_s(&s, "CONIN$", "r", stdin);
        }
    }

    if constexpr (FConfiguration::bCustomCrashReporter)
        FCrashReporter::Register();

    printf("Initializing SDK...\n");
    printf("[Syndicate] Selected server playlist: %ls\n", FConfiguration::Playlist);
    SDK::Init();

    if constexpr (FConfiguration::bGUI)
    {
      if (!managed)
      {
        if constexpr (FConfiguration::bUseStdoutLog)
        {
            FILE* s;
            freopen_s(&s, "stdout.log", "w", stdout);
            freopen_s(&s, "stdout.log", "w+", stderr);
        }

        CreateThread(0, 0, (LPTHREAD_START_ROUTINE)GUI::Init, 0, 0, 0);
      }
    }

    if (VersionInfo.EngineVersion >= 5.0)
    {
        UKismetSystemLibrary::ExecuteConsoleCommand(UWorld::GetWorld(), FString(L"log LogFortUIDirector None"), nullptr);
        UKismetSystemLibrary::ExecuteConsoleCommand(UWorld::GetWorld(), FString(L"log LogFortUIManager None"), nullptr);
    }
    if (VersionInfo.EngineVersion >= 5.1)
    {
        UKismetSystemLibrary::ExecuteConsoleCommand(UWorld::GetWorld(), FString(L"net.AllowEncryption 0"), nullptr);

        auto DefaultCurieGlobals = FindClass("CurieGlobals")->GetDefaultObj();

        if (DefaultCurieGlobals)
        {
            uint32 Offset = DefaultCurieGlobals->GetOffset("bEnableCurie");

            //if (Offset != -1)
            //    *(bool*)(uintptr_t(DefaultCurieGlobals) + Offset) = false;
        }
    }
    if (VersionInfo.EngineVersion >= 5.3 && FConfiguration::bEnableIris)
    {
        UKismetSystemLibrary::ExecuteConsoleCommand(UWorld::GetWorld(), FString(L"log LogIris None"), nullptr);
        UKismetSystemLibrary::ExecuteConsoleCommand(UWorld::GetWorld(), FString(L"log LogIrisRpc None"), nullptr);
        UKismetSystemLibrary::ExecuteConsoleCommand(UWorld::GetWorld(), FString(L"log LogIrisBridge None"), nullptr);
        /*auto IrisBool = Memcury::Scanner::FindPattern("83 3D ? ? ? ? ? 0F 8E ? ? ? ? 49 8B B9").RelativeOffset(2, 1).Get();
        if (IrisBool)
            *(uint32_t*)IrisBool = true;
        else
        {
            IrisBool = Memcury::Scanner::FindPattern("44 39 25 ? ? ? ? 0F 9F C0 45 84 FF").RelativeOffset(3).Get();

            if (IrisBool)
                *(uint32_t*)IrisBool = true;
        }*/
        auto IrisBool = FindCVar<uint32_t>(L"net.Iris.UseIrisReplication");

        if (IrisBool)
            *IrisBool = true;

        if (VersionInfo.FortniteVersion >= 29)
        {
            auto ReplicationBridgeConfig = UObjectReplicationBridgeConfig::GetDefaultObj();

            auto FortInventoryName = FName(L"/Script/FortniteGame.FortInventory");
            for (int i = 0; i < ReplicationBridgeConfig->FilterConfigs.Num(); i++)
            {
                auto& FilterConfig = ReplicationBridgeConfig->FilterConfigs.Get(i, FObjectReplicationBridgeFilterConfig::Size());

                if (FilterConfig.ClassName == FortInventoryName)
                {
                    FilterConfig.DynamicFilterName = FName(0);
                    break;
                }
            }
        }
        //UKismetSystemLibrary::ExecuteConsoleCommand(UWorld::GetWorld(), FString(L"net.Iris.UseIrisReplication 1"), nullptr);
    }
    if (VersionInfo.EngineVersion >= 5.4)
    {
        // sprint fix
        auto SprintCVar = FindCVar<uint32_t>(L"Fort.MME.TacticalSprint");
        auto HurdleCVar = FindCVar<uint32_t>(L"Fort.MME.Hurdle");
        auto SlideCVar = FindCVar<uint32_t>(L"Fort.MME.Sliding");
        auto MantleCVar = FindCVar<uint32_t>(L"Fort.MME.Clambering");

        //if (SprintCVar)
        //    *SprintCVar = false;

        if (HurdleCVar)
            *HurdleCVar = false;

        if (SlideCVar)
            *SlideCVar = false;

        if (MantleCVar)
            *MantleCVar = false;
        UKismetSystemLibrary::ExecuteConsoleCommand(UWorld::GetWorld(), FString(L"Fort.MME.TacticalSprint 0"), nullptr);
        UKismetSystemLibrary::ExecuteConsoleCommand(UWorld::GetWorld(), FString(L"Fort.MME.Hurdle 0"), nullptr);
        UKismetSystemLibrary::ExecuteConsoleCommand(UWorld::GetWorld(), FString(L"Fort.MME.Sliding 0"), nullptr);
        UKismetSystemLibrary::ExecuteConsoleCommand(UWorld::GetWorld(), FString(L"Fort.MME.Clambering 0"), nullptr);
    }

#ifdef CLIENT
    Misc::InitClient();

    return;
#endif

    if constexpr (FConfiguration::WebhookURL && *FConfiguration::WebhookURL)
        curl_global_init(CURL_GLOBAL_ALL);

    sprintf_s(GUI::windowTitle, VersionInfo.EngineVersion >= 5.0 ? "Erbium (FN %.2f, UE %.1f): Setting up" : (VersionInfo.FortniteVersion >= 5.00 || VersionInfo.FortniteVersion < 1.2 ? "Erbium (FN %.2f, UE %.2f): Setting up" : "Erbium (FN %.1f, UE %.2f): Setting up"), VersionInfo.FortniteVersion, VersionInfo.EngineVersion);
    SetConsoleTitleA(GUI::windowTitle);


    //if constexpr (!FConfiguration::bGUI)
    //    Sleep(2000);

    printf("Hooking & finding offsets... (this may take a while)\n");

    FindNullsAndRetTrues();

    for (auto& NullFunc : NullFuncs)
        if (NullFunc != 0)
        {
            Utils::Patch<uint8_t>(NullFunc, 0xc3);
        }

    for (auto& RetTrueFunc : RetTrueFuncs)
    {
        if (RetTrueFunc == 0)
            continue;

        Utils::Patch<uint32_t>(RetTrueFunc, 0xc0ffc031);
        Utils::Patch<uint8_t>(RetTrueFunc + 4, 0xc3);
    }

    auto GameSessionPatch = FindGameSessionPatch();
    if (GameSessionPatch)
        Utils::Patch<uint8_t>(GameSessionPatch, 0x85);

    MH_Initialize();

    for (auto& HookFunc : _HookFuncs)
        HookFunc();

    MH_EnableHook(MH_ALL_HOOKS);

    // Leave the menu world and open the island on the game thread, between two frames (from the start of the game's
    // own frame, Misc::GetMaxTickRate). Done from this loader thread, removing the local player while the game thread
    // was ticking the menu's widgets crashed the server now and then while it loaded (null read deep in the UI tick;
    // seen on 3.5.1 and 4.5, gone on the next try).
    // On a slow or low-memory PC the game thread can still be inside its very first frame (the frontend's login and
    // splash UI) for half a minute after the lobby looks loaded. Doing the step from this thread then crashed the
    // server every time ([HOST-ISLAND-01] 0xC0000005 on 3.5.1 and 8.50 with 3 GB or less free). So keep waiting for
    // a frame, up to 4 minutes; this thread only does it itself when the frame hook was not found in this build.
    SyndicateRunOnGameThread = SyndicateStartServerWorld;
    {
        const int Limit = FindGetMaxTickRate() ? 24000 : 1000; // x 10 ms
        for (int Waited = 0; Waited < Limit && !SyndicateServerWorldStarted; Waited++)
        {
            if (Waited == 1000)
                printf("[Syndicate] waiting for the game thread to finish its first frame before opening the island\n");
            Sleep(10);
        }
    }
    if (!SyndicateServerWorldStarted)
    {
        // Take the step back unless a frame grabbed it at the last moment (then wait for that frame to finish it).
        if (InterlockedExchangePointer((PVOID volatile*)&SyndicateRunOnGameThread, nullptr))
        {
            printf("[Syndicate] game thread did not pick up the island start; starting it from the loader thread\n");
            SyndicateStartServerWorld();
        }
        else
            while (!SyndicateServerWorldStarted)
                Sleep(10);
    }

    auto EncryptionPatch = FindEncryptionPatch();
    if (EncryptionPatch)
        Utils::Patch<uint8_t>(EncryptionPatch, 0x74);
    else
        printf("Matchmaking is NOT supported on this version, please make a github issue.\n");

    for (auto& HookFunc : _PostLoadHookFuncs)
        HookFunc();

    MH_EnableHook(MH_ALL_HOOKS);

    Misc::bHookedAll = true;
}

BOOL APIENTRY DllMain(HMODULE hModule,
    DWORD  ul_reason_for_call,
    LPVOID lpReserved
)
{
    switch (ul_reason_for_call)
    {
    case DLL_PROCESS_ATTACH:
        std::thread(Main).detach();
    case DLL_THREAD_ATTACH:
    case DLL_THREAD_DETACH:
    case DLL_PROCESS_DETACH:
        break;
    }
    return TRUE;
}
