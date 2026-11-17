#pragma once
#include "SyndicateRuntime.h"
namespace KillerSpectating
{
struct Handoff
{
    Syndicate::Ref<AFortPlayerControllerAthena> Controller;
    Syndicate::Ref<AFortPlayerStateAthena> Killer;
    float At;
};
inline std::vector<Handoff> Pending;
inline Syndicate::Ref<UWorld> PendingWorld;
inline void Queue(AFortPlayerControllerAthena *PC, AFortPlayerStateAthena *Killer)
{
    auto W=UWorld::GetWorld();
    if (PendingWorld.Get()!=W) { Pending.clear(); PendingWorld.Set(W); }
    for (auto I=Pending.begin(); I!=Pending.end();)
        if (!I->Controller.Get() || I->Controller.Get()==PC) I=Pending.erase(I); else ++I;
    if (Pending.size()<64) Pending.push_back({PC,Killer,Syndicate::Now()+0.75f});
}
inline AFortPlayerPawnAthena *Target(AFortPlayerControllerAthena *PC, AFortPlayerStateAthena *Killer)
{
    auto W = UWorld::GetWorld();
    auto GS = W ? Cast<AFortGameStateAthena>(W->GameState) : nullptr;
    if (!PC || !Killer || !W || !W->AuthorityGameMode || !GS || GS->GamePhase <= EAthenaGamePhase::Warmup ||
        Killer == PC->PlayerState || Killer->IsPlayerDead() || Killer->bInAircraft)
        return nullptr;
    auto Owner = Syndicate::Ref<AController>(Cast<AController>(Killer->Owner)).Get();
    auto Pawn = Owner ? Syndicate::Ref<AFortPlayerPawnAthena>(Cast<AFortPlayerPawnAthena>(Owner->Pawn)).Get() : nullptr;
    return Pawn && Pawn->PlayerState == Killer && !Pawn->IsDead() && !Pawn->bIsDBNO ? Pawn : nullptr;
}
inline void Prepare(AFortPlayerControllerAthena *PC, AFortPlayerStateAthena *Killer)
{
    if (auto Pawn = Target(PC, Killer)) PC->PlayerToSpectateOnDeath = Pawn;
}
inline void Apply(AFortPlayerControllerAthena *PC, AFortPlayerPawnAthena *Victim, AFortPlayerStateAthena *Killer)
{
    auto State = PC ? Cast<AFortPlayerStateAthena>(PC->PlayerState) : nullptr;
    if (!State || ((!Victim || !Victim->IsDead()) && !State->IsPlayerDead())) return;
    if (auto Pawn = Target(PC, Killer))
    {
        // Native death reporting can replace the preferred target. Restore it
        // once after reporting, then let native camera/streaming and manual
        // spectator cycling own the view. Never force a view each frame.
        PC->PlayerToSpectateOnDeath = Pawn;
        PC->ServerSpectatePlayer(Pawn, true);
        Queue(PC, Killer);
    }
}
inline void Tick()
{
    if (Pending.empty()) return;
    if (PendingWorld.Get()!=UWorld::GetWorld()) { Pending.clear(); PendingWorld.Clear(); return; }
    float Now=Syndicate::Now();
    for (auto I=Pending.begin(); I!=Pending.end();)
    {
        auto PC=I->Controller.Get(); auto Killer=I->Killer.Get();
        auto State=PC ? Cast<AFortPlayerStateAthena>(PC->PlayerState) : nullptr;
        auto Pawn=Target(PC,Killer);
        // A changed native target means the player has cycled, the match has
        // advanced, or the killer died. Never pull that camera back to the killer.
        if (!PC || !State || !State->IsPlayerDead() || !Pawn || PC->GetViewTarget()!=Pawn)
        { I=Pending.erase(I); continue; }
        if (Now<I->At) { ++I; continue; }
        if (PC->StateName.ToString()=="Spectating")
        {
            Pawn->ForceNetUpdate();
            FViewTargetTransitionParams Blend{};
            Blend.BlendTime=0.12f;
            PC->ClientSetViewTarget(Pawn,Blend);
        }
        // One delayed reliable client handoff, never a per-frame camera lock.
        I=Pending.erase(I);
    }
}
} // namespace KillerSpectating
