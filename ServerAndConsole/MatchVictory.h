#pragma once
#include "SyndicateRuntime.h"
#include "MatchProgression.h"
namespace MatchVictory
{
inline UWorld *World = nullptr;
inline bool Contested = false, Completed = false;
inline void Tick(AFortGameModeAthena *GM, AFortGameStateAthena *GS)
{
    auto W = UWorld::GetWorld();
    if (World != W)
    {
        World = W;
        Contested = Completed = false;
    }
    if (!W || !GM || !GS || Completed)
        return;
    // Count distinct, genuinely living participants, including bots. A stale
    // native array slot cannot prevent a win or manufacture a second player.
    std::vector<AFortPlayerStateAthena *> Survivors;
    AController *LastController = nullptr;
    auto Add = [&](AController *Controller)
    {
        if (!Syndicate::Ref<AController>(Controller).Get())
            return;
        auto State = Cast<AFortPlayerStateAthena>(Controller->PlayerState);
        if (!Syndicate::Ref<AFortPlayerStateAthena>(State).Get() || State->IsPlayerDead() || State->bOnlySpectator)
            return;
        auto Pawn = Cast<AFortPlayerPawnAthena>(Controller->Pawn);
        if (Pawn && Pawn->IsDead())
            return;
        if (std::find(Survivors.begin(), Survivors.end(), State) == Survivors.end())
        {
            Survivors.push_back(State);
            LastController = Controller;
        }
    };
    for (auto Controller : GM->AlivePlayers)
        Add(Controller);
    for (auto Controller : GM->AliveBots)
        Add(Controller);
    Contested |= Survivors.size() > 1;
    if (GM->HasMatchEnded() || GS->WinningPlayerState)
    {
        Completed = true; // Preserve a result already published by native rules.
        return;
    }
    if (!Contested || GS->GamePhase <= EAthenaGamePhase::Aircraft || Survivors.size() != 1)
        return;
    auto Pawn = Cast<AFortPlayerPawnAthena>(LastController->Pawn);
    if (!Syndicate::Ref<AFortPlayerPawnAthena>(Pawn).Get() || Pawn->IsDead() || Pawn->bIsDBNO)
        return; // Loading, aircraft and downed pawns are not a completed win.
    Completed = true; // Native replication/RPCs may reenter this tick.
    auto State = Survivors.front();
    State->Place = 1;
    State->OnRep_Place();
    State->ForceNetUpdate();
    GS->WinningPlayerState = State;
    GS->WinningTeam = State->TeamIndex;
    GS->OnRep_WinningPlayerState();
    GS->OnRep_WinningTeam();
    GS->ForceNetUpdate();
    MatchProgression::Won(State); // 500 XP and 450 V-Bucks for a human winner (backend pays none with the menu on)
    if (auto Human = Cast<AFortPlayerControllerAthena>(LastController))
        Human->ClientNotifyWon(Pawn, Pawn->CurrentWeapon ? Pawn->CurrentWeapon->WeaponData : nullptr,
                               EDeathCause::Unspecified);
    // Use the engine's authoritative end-match transition and its normal
    // Athena end handlers, rather than drawing a local victory widget.
    GM->EndMatch();
    Log("Last living participant awarded native Victory Royale; match ended.");
}
} // namespace MatchVictory
