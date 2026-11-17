#pragma once
#include "Bots.h"
#include "Replication.h"
#include "WorldSetup.h"
#include "MatchFlow.h"
#include "MatchVictory.h"
#include "NativeNavigation.h"
#include "KillerSpectating.h"
#include "MatchRenewal.h"
#include "InteractionLoot.h"
#include "PlayerVehicles.h"
#include "RetailRowFiends.h"
#include "PlayerLocker.h"
#include "MenuSwitches.h"
#include "LateGame.h"
#ifdef SYNDICATE_GAMEPLAY_SELFTEST
#include "DiagnosticGameplayTests.h"
#endif
namespace NetDriver
{
void (*TickFlushOG)(UNetDriver *, float) = nullptr;
inline float NextSpawn = 0.f;
inline float NextLoadoutSync = 0.f;
inline Syndicate::Ref<UWorld> SpawnWorld;
inline Syndicate::Ref<AFortGameModeAthena> SpawnMode;
inline bool ReplicationFallbackLogged = false;
void TickFlush(UNetDriver *Driver, float Delta)
{
    // Observe the original native tick rather than assuming it replicates actors.
    // Client executables used as servers can flush control packets but skip this path.
    auto World = Driver ? Driver->World : nullptr;
    auto Graph = Driver && World && Driver == World->NetDriver && !Driver->ServerConnection &&
                         Driver->ClientConnections.Num() > 0 && Cast<AFortGameModeAthena>(World->AuthorityGameMode)
                     ? Cast<UReplicationGraph>(Driver->ReplicationDriver)
                     : nullptr;
    Syndicate::Ref<UNetDriver> DriverGuard(Driver);
    Syndicate::Ref<UReplicationDriver> GraphGuard(Graph);
    Replication::Frame ReplicationFrame(Graph);
    if (Driver && Driver->World && Driver == Driver->World->NetDriver)
    {
        auto W = Driver->World;
        auto GM = Syndicate::As<AFortGameModeAthena>(W->AuthorityGameMode);
        auto GS = Syndicate::As<AFortGameStateAthena>(W->GameState);
        if (GM && GS)
        {
            BotWork::Scope Measure(BotWork::ServerWork);
            if (SpawnWorld.Get()!=W || SpawnMode.Get()!=GM)
            {
                SpawnWorld.Set(W); SpawnMode.Set(GM);
                MatchVictory::World=nullptr; MatchVictory::Completed=MatchVictory::Contested=false;
                NextSpawn = 0;
                NextLoadoutSync = 0;
                ReplicationFallbackLogged = false;
                PlayerBots::ResetWorld();
                PlayerBots::ActiveWorld = W;
            }
            BotPopulation::Begin(W);
            NativeNavigation::Tick(GM);
            PlayerVehicles::Tick(GM);
            RetailRowFiends::Tick(W, GM, GS);
            MatchProgression::Tick(GM, GS);
            BotPopulation::Reconcile(GM, GS);
            if (GS->GamePhase >= EAthenaGamePhase::Aircraft)
                BotPopulation::Closed = true;
            bool HumanSpawned = MatchFlow::Tick(Driver, GM, GS);
            LateGame::Tick(GM, GS); // Settings > Bots > Late game only; before the bots plan their jump
#ifdef SYNDICATE_GAMEPLAY_SELFTEST
            DiagnosticGameplayTests::Tick(GM);
#endif
            float N = Syndicate::Now();
            if (N >= NextLoadoutSync)
            {
                NextLoadoutSync = N + 0.5f;
                for (auto Human : GM->AlivePlayers)
                    if (auto Pawn = Human ? Cast<AFortPlayerPawnAthena>(Human->Pawn) : nullptr;
                        Pawn && !Pawn->IsDead())
                        {Human->bBuildFree=MenuSwitches::FreeBuild(Human);FortInventory::ClampResources(Human);FortInventory::SyncPlayerPickaxe(Human, Pawn);FortInventory::SyncHeldAmmo(Human, Pawn);PlayerLocker::Keep(Human, Pawn);}
            }
            if (Globals::bBotsEnabled && GS->GamePhase == EAthenaGamePhase::Warmup &&
                Driver->ClientConnections.Num() > 0 && HumanSpawned && GM->GameSession && N >= NextSpawn)
            {
                NextSpawn = N + 0.20f;
                if (BotPopulation::CanSpawn(GM, GS))
                {
                    bool Spawned = Bots::SpawnPlayerBot();
                    if (Spawned)
                    {
                        if (GM->AliveBots.Num() == 1 || GM->AliveBots.Num() % 10 == 0)
                            Log("Native bot population: " + std::to_string(GM->AliveBots.Num()));
                    }
                    else
                        NextSpawn = N + 1.f; // Bound repeated failed native spawns/assets.
                }
            }
            if (Globals::bBotsEnabled)
                PlayerBots::TickBots(Delta);
            BotPopulation::Reconcile(GM, GS);
            MatchVictory::Tick(GM, GS);
            MenuSwitches::Tick(GM);
        }
    }
    if (TickFlushOG)
        TickFlushOG(Driver, Delta);
    // Only supply a missing native graph call; never double an observed replication pass.
    auto LiveDriver = DriverGuard.Get();
    auto LiveGraph = GraphGuard.Get();
    if (LiveDriver && LiveGraph && LiveDriver->World == World && World && World->NetDriver == LiveDriver &&
        !LiveDriver->ServerConnection && LiveDriver->ClientConnections.Num() > 0 &&
        LiveDriver->ReplicationDriver == LiveGraph && Replication::CompleteFrame(ReplicationFrame, LiveGraph, Delta) &&
        !ReplicationFallbackLogged)
    {
        ReplicationFallbackLogged = true;
        Log("Restored native actor replication: the original network tick skipped its graph call.");
    }
    // Do deferred map work only after the native network tick and replication pass.
    if (LiveDriver && World && LiveDriver->World == World && World->NetDriver == LiveDriver &&
        !LiveDriver->ServerConnection && Cast<AFortGameModeAthena>(World->AuthorityGameMode))
    {
        KillerSpectating::Tick();
        WorldSetup::Tick(World);
        InteractionLoot::Tick(World);
        MatchRenewal::Tick(LiveDriver); // Last: native travel can invalidate everything above.
        return;
    }
}
float GetMaxTickRate()
{
    return 30.f;
}
void HookAll()
{
    Replication::HookAll();
    InstallHook((LPVOID)(ImageBase + 0x31EECB0), TickFlush, (LPVOID *)&TickFlushOG);
    InstallHook((LPVOID)(ImageBase + 0x3482550), GetMaxTickRate, nullptr);
    Log("NetDriver hooked.");
}
} // namespace NetDriver
