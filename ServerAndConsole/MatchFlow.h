#pragma once
#include "WorldSetup.h"
#include "BotPopulation.h"
namespace MatchFlow
{
inline Syndicate::Ref<UWorld> ActiveWorld;
inline bool WarmupActivated = false, WaitingLogged = false;
inline float NextPawnRetry = 0.f;
inline float BusDeadline = 0.f, NextBusKick = 0.f, ClockEnd = 0.f, ClockStart = 0.f;
inline constexpr float FinalCountdown = 10.f;
inline constexpr int StartPlayers = 80;
inline int BusKicks = 0;
inline bool Tick(UNetDriver *Driver, AFortGameModeAthena *GM, AFortGameStateAthena *GS)
{
    auto W = Driver ? Driver->World : nullptr;
    if (!W || !GM || !GS)
        return false;
    if (ActiveWorld.Get() != W)
    {
        ActiveWorld.Set(W);
        WarmupActivated = WaitingLogged = false;
        NextPawnRetry = 0.f;
        BusDeadline = NextBusKick = ClockEnd = ClockStart = 0.f;
        BusKicks = 0;
    }
    float Now = Syndicate::Now();
    bool Warmup = GS->GamePhase == EAthenaGamePhase::Warmup;
    bool Retry = Warmup && !WarmupActivated && Now >= NextPawnRetry;
    if (Retry)
        NextPawnRetry = Now + 1.f;
    bool PossessedHuman = false;
    for (auto Connection : Driver->ClientConnections)
    {
        auto PC = Connection ? Cast<AFortPlayerControllerAthena>(Connection->PlayerController) : nullptr;
        if (!PC || !PC->PlayerState || PC->PlayerState->bOnlySpectator || !PC->bReadyToStartMatch ||
            !PC->bHasClientFinishedLoading || !PC->bHasServerFinishedLoading)
            continue;
        if (Retry && !PC->Pawn && PC->PlayerState && !PC->PlayerState->bOnlySpectator)
            GM->RestartPlayer(PC); // Native restart calls SpawnDefaultPawnFor and owns possession.
        auto Pawn = Cast<AFortPlayerPawnAthena>(PC->Pawn);
        if (Pawn && !Pawn->IsDead())
        {
            // Native pawn creation can occur after the ready RPC has returned.
            WorldSetup::Request(W);
            if (PC->AcknowledgedPawn == PC->Pawn)
                PossessedHuman = true;
        }
    }
    if (Warmup && !WarmupActivated)
    {
        float Duration = std::max(30.f, GM->WarmupCountdownDuration);
        // This is the gameplay warmup clock, not a network timeout or a fabricated
        // loading flag. Give the real client its warmup after possession is acked.
        GS->WarmupCountdownStartTime = Now;
        GS->WarmupCountdownEndTime = Now + Duration;
        if (PossessedHuman)
        {
            WarmupActivated = true;
            Log("Client possession acknowledged; native warmup clock and bot population started.");
        }
        else if (!WaitingLogged)
        {
            WaitingLogged = true;
            Log("Warmup waiting for native pawn creation and real client possession acknowledgement.");
        }
    }
    // Like the other seasons, the bus countdown starts while the lobby is still filling: once 80 of the
    // players are in (or everyone, in a smaller match), a 10 second countdown starts, and the game is
    // kept from pushing it back. Before, it only started once all 100 were in and then ran a full minute.
    if (Warmup && WarmupActivated)
    {
        int Present = GM->AlivePlayers.Num() + GM->AliveBots.Num();
        int Expected = std::max(1, BotPopulation::Target + GM->AlivePlayers.Num());
        if (ClockEnd == 0.f && Present >= std::min(StartPlayers, Expected))
        {
            ClockStart = Now;
            ClockEnd = Now + FinalCountdown;
            BusDeadline = ClockEnd + 15.f;
            GS->WarmupCountdownStartTime = Now;
            GS->WarmupCountdownEndTime = ClockEnd;
            GS->ForceNetUpdate();
            Log("Bus countdown started with " + std::to_string(Present) + " of " + std::to_string(Expected) + " players; bus in " +
                std::to_string((int)FinalCountdown) + "s.");
        }
        else if (ClockEnd > 0.f && (std::fabs(GS->WarmupCountdownEndTime - ClockEnd) > 0.5f ||
                                    std::fabs(GS->WarmupCountdownStartTime - ClockStart) > 0.5f))
        {
            // The game rewrites the warmup clock while the lobby fills (a test showed the countdown on screen
            // only a minute after 100 players, though the bus timing here was right). Hold both ends of it.
            static int Told = 0;
            if (Told++ < 5)
                Log("Bus countdown: the game changed the warmup clock (start " + std::to_string(GS->WarmupCountdownStartTime) + ", end " +
                    std::to_string(GS->WarmupCountdownEndTime) + ", now " + std::to_string(Now) + "); restored it.");
            GS->WarmupCountdownStartTime = ClockStart;
            GS->WarmupCountdownEndTime = ClockEnd;
            GS->ForceNetUpdate();
        }
    }
    // Backstop: the battle bus did not always leave (seen with 80-90 players). The warmup clock runs out
    // 15 seconds before this deadline (set when the countdown above starts); if the match is still in warmup then, start the bus the way the
    // StartAircraftEarly cheat does, and retry every 10 seconds (at most 6 times).
    if (Warmup && WarmupActivated && BusDeadline > 0.f && Now >= BusDeadline && Now >= NextBusKick && BusKicks < 6)
    {
        NextBusKick = Now + 10.f;
        ++BusKicks;
        Log("Warmup overran by " + std::to_string((int)(Now - BusDeadline + 15.f)) + "s (players " + std::to_string(GM->AlivePlayers.Num()) +
            ", bots " + std::to_string(GM->AliveBots.Num()) + ", countdown end " + std::to_string(GS->WarmupCountdownEndTime) +
            ", now " + std::to_string(Now) + "); starting the battle bus (attempt " + std::to_string(BusKicks) + ").");
        APlayerController *Kicker = nullptr;
        for (auto Connection : Driver->ClientConnections)
            if (Connection && Connection->PlayerController) { Kicker = Connection->PlayerController; break; }
        UKismetSystemLibrary::ExecuteConsoleCommand(W, L"startaircraft", Kicker);
    }
    return PossessedHuman;
}
} // namespace MatchFlow
