#pragma once
#include "SyndicateRuntime.h"
namespace NativeBotController
{
inline AFortAthenaAIBotController *Create(AFortPlayerPawnAthena *Pawn, UFortServerBotManagerAthena *Manager,
                                         AFortGameModeAthena *Mode)
{
    if (!Pawn || Pawn->Controller || !Manager || !Mode)
        return Pawn ? Syndicate::As<AFortAthenaAIBotController>(Pawn->Controller) : nullptr;
    auto Transform = Pawn->GetTransform();
    auto Actor = UGameplayStatics::BeginDeferredActorSpawnFromClass(
        UWorld::GetWorld(), AFortAthenaAIBotController::StaticClass(), Transform,
        ESpawnActorCollisionHandlingMethod::AlwaysSpawn, Mode);
    auto Controller = Syndicate::As<AFortAthenaAIBotController>(Actor);
    if (!Controller)
        return nullptr;
    // Native BeginPlay/OnPossess must see these dependencies on the FIRST
    // possession. Setting them after SpawnDefaultController was too late.
    Controller->CachedBotManager = Manager;
    Controller->CachedGameMode = Mode;
    Controller->CachedBotMutator = Manager->CachedBotMutator;
    Controller->PlayerBotPawn = Pawn;
    Controller->bWantsPlayerState = true;
    Controller->PrimaryActorTick.bAllowTickOnDedicatedServer = true;
    if (Controller->PathFollowingComponent)
        Controller->PathFollowingComponent->PrimaryComponentTick.bAllowTickOnDedicatedServer = true;
    if (Controller->BrainComponent)
        Controller->BrainComponent->PrimaryComponentTick.bAllowTickOnDedicatedServer = true;
    Controller = Syndicate::As<AFortAthenaAIBotController>(
        UGameplayStatics::FinishSpawningActor(Controller, Controller->GetTransform()));
    if (!Controller)
        return nullptr;
    Controller->Possess(Pawn);
    return Pawn->Controller == Controller ? Controller : nullptr;
}
} // namespace NativeBotController
