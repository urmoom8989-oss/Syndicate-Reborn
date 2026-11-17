#pragma once
#include "MatchVictory.h"
#include "NativeNavigation.h"
namespace MatchRenewal
{
inline Syndicate::Ref<UWorld> ActiveWorld;
inline Syndicate::Ref<AFortGameModeAthena> ActiveMode;
inline bool HadClients=false,Requested=false,Warning=false;
inline float EmptySince=-1.f,CompletedSince=-1.f,RequestAt=0.f;
inline void Tick(UNetDriver *Driver)
{
    auto W=Driver ? Driver->World : nullptr;
    auto GM=W ? Syndicate::As<AFortGameModeAthena>(W->AuthorityGameMode) : nullptr;
    auto GS=W ? Syndicate::As<AFortGameStateAthena>(W->GameState) : nullptr;
    if(!W || !GM || !GS || Driver!=W->NetDriver || Driver->ServerConnection) return;
    if(ActiveWorld.Get()!=W || ActiveMode.Get()!=GM)
    {
        ActiveWorld.Set(W); ActiveMode.Set(GM);
        HadClients=Requested=Warning=false; EmptySince=CompletedSince=-1.f;
        Log("Match renewal ready: native restart after the result or last client leaves.");
    }
    float Now=Syndicate::Now();
    if(Requested)
    {
        if(!Warning && Now-RequestAt>30.f)
        { Warning=true; Log("Native match restart did not change worlds; restart was rejected or travel is still pending. No forced in-place reset applied."); }
        return;
    }
    bool Connected=false;
    for(auto C:Driver->ClientConnections) if(C && C->PlayerController) { Connected=true; break; }
    HadClients|=Connected;
    if(Connected) EmptySince=-1.f; else if(HadClients && EmptySince<0.f) EmptySince=Now;
    bool Finished=GM->HasMatchEnded() || GS->WinningPlayerState;
    if(Finished && CompletedSince<0.f) CompletedSince=Now;
    bool Empty=HadClients && !Connected && EmptySince>=0.f && Now-EmptySince>=8.f;
    if(!Empty && (!Finished || Now-CompletedSince<25.f)) return;
    if(!GM->GameSession) return; // The native restart dereferences its session permission gate.
    Requested=true; RequestAt=Now;
    if(!GM->HasMatchEnded()) GM->EndMatch();
    NativeNavigation::PrepareBeforeTravel();
    Log("Native match restart requested; the next world will initialize a new match and bot population.");
    GM->RestartGame(); // Verified live 10.40 virtual route calls UWorld server travel with ?Restart.
    // Travel may destroy the driver/world. Do not touch them after this call.
}
} // namespace MatchRenewal
