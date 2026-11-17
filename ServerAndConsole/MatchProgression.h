#pragma once
// XP and V-Bucks for Season X matches.
//
// Syndicate's backend (Syndicate's) gives XP and V-Bucks from match events that the match server
// drops into Runtime\Server\progression-queue as one JSON file each (see syndicateProgression.js): a kill
// gives 100 XP and 25 V-Bucks, a win 500 XP and 450 V-Bucks (no V-Bucks with the Syndicate menu on), every survived minute 50 XP, and "match"
// records the placement. Erbium (the other seasons) writes these; Syndicate never did, so a Season X
// match left the level at 0 and paid nothing. This writes the same events for human players. The
// folder comes from SYNDICATE_SERVER_RUNTIME, set by the launcher.
#include "framework.h"
#include "SyndicateRuntime.h"
#include <objbase.h>
#pragma comment(lib, "ole32.lib")
#include <fstream>
#include <map>

namespace MatchProgression
{
inline UWorld *World = nullptr;
inline std::string MatchId;
inline int StartingPlayers = 0;
inline float AliveSince = 0.f;
inline std::map<AFortPlayerStateAthena *, float> NextSurvival;
inline std::vector<AFortPlayerStateAthena *> MatchSent;

inline std::wstring Folder()
{
    wchar_t Buffer[1024]{};
    DWORD Length = GetEnvironmentVariableW(L"SYNDICATE_SERVER_RUNTIME", Buffer, 1024);
    if (Length == 0 || Length >= 1024) return L"";
    return std::wstring(Buffer) + L"\\progression-queue";
}

inline std::string NewGuid()
{
    GUID G{};
    if (FAILED(CoCreateGuid(&G))) return "";
    wchar_t Text[40]{};
    StringFromGUID2(G, Text, 40);
    std::string Out;
    for (wchar_t C : std::wstring(Text)) Out += (char)C;
    for (auto &C : Out) C = (char)tolower((unsigned char)C);
    return Out; // "{xxxxxxxx-...}"
}

inline void Reset()
{
    auto W = UWorld::GetWorld();
    if (World == W) return;
    World = W;
    MatchId = NewGuid();
    StartingPlayers = 0;
    AliveSince = 0.f;
    NextSurvival.clear();
    MatchSent.clear();
}

// A real player: the state belongs to a controller with a network connection.
inline AFortPlayerControllerAthena *Human(AFortPlayerStateAthena *State)
{
    auto W = UWorld::GetWorld();
    auto Net = W ? W->NetDriver : nullptr;
    if (!State || !Net) return nullptr;
    for (auto Connection : Net->ClientConnections)
        if (Connection && Connection->PlayerController && Connection->PlayerController->PlayerState == State)
            return Cast<AFortPlayerControllerAthena>(Connection->PlayerController);
    return nullptr;
}

inline std::string Mode(AFortGameStateAthena *GS)
{
    auto Playlist = GS ? GS->CurrentPlaylistInfo.BasePlaylist : nullptr;
    std::string Name = Playlist ? Playlist->GetName() : "";
    if (Name.find("Duo") != std::string::npos) return "duo";
    if (Name.find("Squad") != std::string::npos || Name.find("Trio") != std::string::npos) return "squad";
    return "solo";
}

inline void Queue(AFortPlayerStateAthena *State, const char *Kind)
{
    Reset();
    if (!Human(State)) return;
    auto Dir = Folder();
    if (Dir.empty())
    {
        static bool Told = false;
        if (!Told) { Told = true; Log("Match progression: SYNDICATE_SERVER_RUNTIME not set; no XP or V-Bucks are sent."); }
        return;
    }
    std::string Name = State->GetPlayerName().ToString(), Escaped;
    for (unsigned char C : Name)
    {
        if (C == '"' || C == '\\') Escaped += '\\';
        if (C >= 32) Escaped += (char)C;
    }
    if (Escaped.empty()) return;
    auto Id = NewGuid();
    if (Id.empty()) return;
    auto GS = Cast<AFortGameStateAthena>(UWorld::GetWorld()->GameState);
    int Seconds = AliveSince > 0.f ? (int)(Syndicate::Now() - AliveSince) : 0;
    std::string Json = "{\"id\":\"" + Id + "\",\"username\":\"" + Escaped + "\",\"kind\":\"" + Kind + "\",\"mode\":\"" + Mode(GS) +
                       "\",\"place\":" + std::to_string(State->Place > 0 ? State->Place : 0) + ",\"arena\":false,\"season\":10,\"seconds\":" +
                       std::to_string(Seconds) + ",\"quest\":\"\",\"objective\":\"\",\"count\":0,\"players\":" + std::to_string(StartingPlayers) +
                       ",\"weapon\":\"\",\"match\":\"" + MatchId + "\"}";
    CreateDirectoryW(Dir.c_str(), nullptr);
    std::wstring WideId(Id.begin(), Id.end());
    auto Temp = Dir + L"\\" + WideId + L".tmp", Final = Dir + L"\\" + WideId + L".json";
    {
        std::ofstream Out(Temp, std::ios::binary);
        Out << Json;
        if (!Out) return;
    }
    if (!MoveFileExW(Temp.c_str(), Final.c_str(), MOVEFILE_REPLACE_EXISTING)) { DeleteFileW(Temp.c_str()); return; }
    static int Told = 0;
    if (Told++ < 20) Log(std::string("Match progression: queued ") + Kind + " for " + Name);
}

inline void Kill(AFortPlayerStateAthena *Killer) { Queue(Killer, "kill"); }

// Placement, once per player per match (death or win).
inline void Finished(AFortPlayerStateAthena *State)
{
    Reset();
    if (!State || std::find(MatchSent.begin(), MatchSent.end(), State) != MatchSent.end() || !Human(State)) return;
    MatchSent.push_back(State);
    Queue(State, "match");
}

inline void Won(AFortPlayerStateAthena *State)
{
    Queue(State, "win");
    Finished(State);
}

// Every minute a human player stays alive after the bus: "survival" (50 XP).
inline void Tick(AFortGameModeAthena *GM, AFortGameStateAthena *GS)
{
    Reset();
    if (!GM || !GS || GS->GamePhase < EAthenaGamePhase::Aircraft) return;
    float Now = Syndicate::Now();
    if (StartingPlayers == 0)
    {
        StartingPlayers = GM->AlivePlayers.Num() + GM->AliveBots.Num();
        AliveSince = Now;
    }
    for (auto PC : GM->AlivePlayers)
    {
        auto State = PC ? Cast<AFortPlayerStateAthena>(PC->PlayerState) : nullptr;
        if (!State || State->IsPlayerDead()) continue;
        auto &Next = NextSurvival[State];
        if (Next == 0.f) { Next = Now + 60.f; continue; }
        if (Now >= Next) { Next += 60.f; Queue(State, "survival"); }
    }
}
} // namespace MatchProgression
