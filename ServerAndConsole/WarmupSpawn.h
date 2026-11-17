#pragma once
#include "SyndicateRuntime.h"
namespace WarmupSpawn
{
// Use Unreal's normal collision-aware creation. This policy never permits a
// blocking spawn, and no existing pawn is moved or manually possessed here.
inline AFortPlayerPawnAthena *CollisionSafePawn(AGameModeBase *Mode, AController *Controller, AActor *Start)
{
    if (!Mode || !Controller || !Start || Start->IsActorBeingDestroyed())
        return nullptr;
    auto Class = Mode->GetDefaultPawnClassForController(Controller);
    if (!Class || !Cast<AFortPlayerPawnAthena>(Class->DefaultObject))
        return nullptr;
    auto Transform = Start->GetTransform();
    if (!Syndicate::Finite(Transform.Translation))
        return nullptr;
    auto Actor = UGameplayStatics::BeginDeferredActorSpawnFromClass(
        UWorld::GetWorld(), Class, Transform,
        ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButDontSpawnIfColliding, Controller);
    if (!Actor)
        return nullptr;
    // Preserve the collision-resolved initial transform through construction.
    auto Resolved = Actor->GetTransform();
    return Cast<AFortPlayerPawnAthena>(UGameplayStatics::FinishSpawningActor(Actor, Resolved));
}
inline APawn *Create(AGameModeBase *Mode, AController *Controller, AActor *Start)
{
    if (!Mode || !Controller || !Start)
        return nullptr;
    auto Pawn = Mode->SpawnDefaultPawnAtTransform(Controller, Start->GetTransform());
    if (Pawn)
        return Pawn;
    auto W = UWorld::GetWorld();
    auto GS = W ? Cast<AFortGameStateAthena>(W->GameState) : nullptr;
    // Retry only warmup creation; native aircraft/respawn rules remain in control.
    if (!Cast<AFortPlayerControllerAthena>(Controller) || !GS ||
        (GS->GamePhase != EAthenaGamePhase::Setup && GS->GamePhase != EAthenaGamePhase::Warmup))
        return nullptr;
    if (auto Safe = CollisionSafePawn(Mode, Controller, Start))
    {
        Log("Warmup pawn collision resolved by native spawning.");
        return Safe;
    }
    auto Starts = Syndicate::ActorsOf<AFortPlayerStartWarmup>();
    int First = Syndicate::Pick(static_cast<int>(Starts.size()));
    for (int i = 0; i < std::min<int>(16, static_cast<int>(Starts.size())); ++i)
    {
        auto Other = Starts[(First + i) % Starts.size()].Get();
        if (!Other || Other == Start)
            continue;
        if (auto Safe = CollisionSafePawn(Mode, Controller, Other))
        {
            Log("Warmup pawn created at another native player start after a blocked start.");
            return Safe;
        }
    }
    Log("Warmup pawn spawn blocked: no collision-free native player start found in this batch.");
    return nullptr;
}
} // namespace WarmupSpawn
