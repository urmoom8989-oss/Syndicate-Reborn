#pragma once
#include "SyndicateRuntime.h"
namespace BotPopulation
{
inline UWorld *World = nullptr;
inline int Target = 0, Admitted = 0;
inline bool SpawnInProgress = false, Faulted = false, Closed = false;
inline bool CountLayoutKnown = false, CountIncludesBots = false;
inline int ReservedTeam = 0;
struct Admission
{
    explicit Admission(int Team)
    {
        SpawnInProgress = true;
        ReservedTeam = Team;
    }
    ~Admission()
    {
        SpawnInProgress = false;
        ReservedTeam = 0;
    }
};
inline void Begin(UWorld *W)
{
    if (World == W)
        return;
    World = W;
    int Low=std::clamp(Globals::MinBotsToSpawn,0,99),High=std::clamp(Globals::MaxBotsToSpawn,Low,99);
    Target=Low+Syndicate::Pick(High-Low+1);
    Admitted = 0;
    SpawnInProgress = Faulted = Closed = false;
    CountLayoutKnown = CountIncludesBots = false;
    ReservedTeam = 0;
    Log("Bot roster target selected: " + std::to_string(Target) + " (configured range "+std::to_string(Low)+"-"+std::to_string(High)+", at most 100 total players).");
}
template <class T> inline void Unique(TArray<T *> &Actors)
{
    std::unordered_set<T *> Seen;
    for (int i = Actors.Num() - 1; i >= 0; --i)
        if (!Syndicate::Ref<T>(Actors[i]).Get() || !Seen.insert(Actors[i]).second)
            Actors.Remove(i);
}
inline void Reconcile(AFortGameModeAthena *GM, AFortGameStateAthena *GS)
{
    if (!GM || !GS)
        return;
    Unique(GM->AlivePlayers);
    Unique(GM->AliveBots);
    // Ask this executable's native API how its replicated fields compose the
    // total. Client-server builds differ; never blindly add bots a second time.
    int Humans = GM->AlivePlayers.Num(), Bots = GM->AliveBots.Num();
    bool Changed = GS->PlayerBotsLeft != Bots;
    GS->PlayerBotsLeft = Bots;
    if (!CountLayoutKnown && Bots > 0)
    {
        auto Native = GS->GetPlayersLeft();
        if (Native.Bots == Bots && Native.Total == GS->PlayersLeft + Bots && Native.Humans == GS->PlayersLeft)
        {
            CountLayoutKnown = true;
            CountIncludesBots = false;
        }
        else if (Native.Bots == Bots && Native.Total == GS->PlayersLeft && Native.Humans == GS->PlayersLeft - Bots)
        {
            CountLayoutKnown = CountIncludesBots = true;
        }
        if (CountLayoutKnown)
            Log(CountIncludesBots ? "Native player count layout: PlayersLeft includes bots."
                                  : "Native player count layout: separate human and bot counts.");
    }
    int Field = Humans + (CountIncludesBots ? Bots : 0);
    if ((CountLayoutKnown || Bots == 0) && GS->PlayersLeft != Field)
    {
        GS->PlayersLeft = Field;
        Changed = true;
    }
    if (auto Manager = GM->ServerBotManager)
    {
        std::unordered_set<AFortAthenaAIBotController *> Seen;
        for (int i = Manager->PlayerBots.Num() - 1; i >= 0; --i)
        {
            auto Controller = Manager->PlayerBots[i].BotController;
            if (!Syndicate::Ref<AFortAthenaAIBotController>(Controller).Get() || !Seen.insert(Controller).second)
                Manager->PlayerBots.Remove(i);
        }
    }
    if (Changed)
    {
        GS->OnRep_PlayersLeft();
        GS->ForceNetUpdate();
    }
}
inline int Capacity(AFortGameModeAthena *GM)
{
    int Limit = GM && GM->GameSession ? std::clamp(GM->GameSession->MaxPlayers, 1, 100) : 100;
    return GM ? std::min(Target, std::max(0, Limit - GM->AlivePlayers.Num())) : 0;
}
inline bool CanSpawn(AFortGameModeAthena *GM, AFortGameStateAthena *GS)
{
    Begin(UWorld::GetWorld());
    if (!GM || !GS || GS->GamePhase != EAthenaGamePhase::Warmup)
        return false;
    int Count = std::max(Admitted, GM->AliveBots.Num());
    return !Closed && !Faulted && !SpawnInProgress && Count < Capacity(GM);
}
inline int Team(AFortGameModeAthena *GM)
{
    // Allocate from actual player states instead of consuming a human team in
    // native startup and a second team in the bot hook.
    bool Used[103]{};
    for (auto PS : GM->GameState->PlayerArray)
        if (auto P = Syndicate::As<AFortPlayerStateAthena>(PS); P && P->TeamIndex < 103)
            Used[P->TeamIndex] = true;
    for (int T = 102; T >= 3; --T)
        if (!Used[T])
            return T;
    return 0;
}
} // namespace BotPopulation
