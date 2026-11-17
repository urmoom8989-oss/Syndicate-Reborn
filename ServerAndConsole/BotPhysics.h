#pragma once
#include "SyndicateRuntime.h"
namespace BotPhysics
{
inline unsigned long long ProbeMissingFloor = 0, ProbeSteepFloor = 0, ProbeLedge = 0,
                          ProbeObstacle = 0, ProbeAccepted = 0;
inline Syndicate::Ref<AActor> LastObstacle;
inline Syndicate::Ref<AFortPlayerPawnAthena> LastBlockedPawn;
inline FVector LastObstacleNormal{}, LastProbeStart{}, LastProbeEnd{};
inline bool LastProbePenetrating = false;
inline void PrepareClass(UClass *Class)
{
    auto Default = Class ? Syndicate::As<AFortPlayerPawnAthena>(Class->DefaultObject) : nullptr;
    if (!Default)
        return;
    // Set the bot archetype before native component/tick registration. Enabling
    // an already unregistered tick after spawning is insufficient on a server.
    Default->PrimaryActorTick.bAllowTickOnDedicatedServer = true;
    if (auto Movement = Default->CharacterMovement)
    {
        Movement->PrimaryComponentTick.bAllowTickOnDedicatedServer = true;
        Movement->DefaultLandMovementMode = EMovementMode::MOVE_Walking;
    }
}
inline bool LandingPoint(AActor *Actor, FVector &Point)
{
    if (!Actor || Actor->IsActorBeingDestroyed())
        return false;
    FVector Origin{}, Extent{};
    Actor->GetActorBounds(false, &Origin, &Extent);
    // Brush/streamed landmarks can have an actor origin at zero while their
    // actual components occupy a POI elsewhere. Whole-map foundations and
    // empty origin-only markers are not landing destinations.
    if (!Syndicate::Finite(Origin) || !Syndicate::Finite(Extent) ||
        Extent.X > 22000.f || Extent.Y > 22000.f)
        return false;
    Point = (Extent.X > 50.f || Extent.Y > 50.f) ? Origin : Actor->K2_GetActorLocation();
    return Syndicate::Finite(Point) && (std::hypot(Point.X, Point.Y) > 300.f ||
                                      Extent.X > 400.f || Extent.Y > 400.f);
}
inline FName PawnProfile()
{
    static FName Name = UKismetStringLibrary::Conv_StringToName(L"Pawn");
    return Name;
}
inline bool Floor(const FVector &At, FVector &Ground, AActor *Ignore = nullptr,
                  const std::vector<Syndicate::Ref<AFortPlayerPawnAthena>> *Crowd = nullptr)
{
    if (!Syndicate::Finite(At))
        return false;
    TArray<AActor *> Ignored;
    if (Ignore)
        Ignored.Add(Ignore);
    // Ground sampling must not treat a neighboring player's head as terrain.
    // This ignore list applies to the query, never to actual pawn collisions.
    if (Crowd)
        for (const auto &Ref : *Crowd)
            if (auto Other = Ref.Get(); Other && Other != Ignore)
                Ignored.Add(Other);
    FHitResult Hit{};
    bool Found = UKismetSystemLibrary::LineTraceSingleByProfile(
        UWorld::GetWorld(), At + FVector(0, 0, 300), At - FVector(0, 0, 600), PawnProfile(),
        false, Ignored, EDrawDebugTrace::None, &Hit, true, {}, {}, 0);
    Ignored.Free();
    if (!Found || Hit.bStartPenetrating || Hit.ImpactNormal.Z < 0.65f ||
        Syndicate::As<APawn>(Hit.Actor.Get()))
        return false;
    Ground = Hit.ImpactPoint;
    return Syndicate::Finite(Ground);
}
inline bool InitialLocation(UClass *Class, const FVector &At, FVector &Center)
{
    auto Default = Class ? Syndicate::As<AFortPlayerPawnAthena>(Class->DefaultObject) : nullptr;
    if (!Default || !Default->CapsuleComponent)
        return false;
    FVector Ground{};
    if (!Floor(At, Ground))
        return false;
    float Half = Default->CapsuleComponent->CapsuleHalfHeight;
    float Radius = Default->CapsuleComponent->CapsuleRadius;
    Center = Ground + FVector(0, 0, Half + 3.f);
    FHitResult Hit{};
    TArray<AActor *> Ignored;
    bool Blocked = UKismetSystemLibrary::CapsuleTraceSingleByProfile(
        UWorld::GetWorld(), Center, Center + FVector(0, 0, 1), Radius, Half, PawnProfile(), false,
        Ignored, EDrawDebugTrace::None, &Hit, true, {}, {}, 0);
    return !Blocked;
}
inline void EnablePawn(AFortPlayerPawnAthena *Pawn)
{
    if (!Pawn || Pawn->IsDead())
        return;
    Pawn->SetActorEnableCollision(true);
    Pawn->PrimaryActorTick.bAllowTickOnDedicatedServer = true;
    Pawn->SetActorTickEnabled(true);
    Pawn->SetReplicates(true);
    Pawn->SetReplicateMovement(true);
    Pawn->NetUpdateFrequency = std::max(Pawn->NetUpdateFrequency, 30.f);
    Pawn->MinNetUpdateFrequency = std::max(Pawn->MinNetUpdateFrequency, 15.f);
    if (auto Capsule = Pawn->CapsuleComponent)
    {
        Capsule->SetCollisionObjectType(ECollisionChannel::ECC_Pawn);
        Capsule->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
        Capsule->SetCollisionResponseToChannel(ECollisionChannel::ECC_WorldStatic, ECollisionResponse::ECR_Block);
        Capsule->SetCollisionResponseToChannel(ECollisionChannel::ECC_WorldDynamic, ECollisionResponse::ECR_Block);
    }
    if (auto Movement = Pawn->CharacterMovement)
    {
        Movement->PrimaryComponentTick.bAllowTickOnDedicatedServer = true;
        Movement->SetUpdatedComponent(Pawn->CapsuleComponent);
        Movement->Activate(false);
        Movement->SetComponentTickEnabled(true);
        Movement->SetComponentTickInterval(0.f);
    }
}
inline void EnableGroundPawn(AFortPlayerPawnAthena *Pawn)
{
    if (!Pawn || Pawn->IsDead() || Pawn->bIsSkydiving || Pawn->bIsParachuteOpen)
        return;
    EnablePawn(Pawn);
    if (auto Movement = Pawn->CharacterMovement)
    {
        Movement->DefaultLandMovementMode = EMovementMode::MOVE_Walking;
        if (Movement->MovementMode == EMovementMode::MOVE_None ||
            Movement->MovementMode == EMovementMode::MOVE_NavWalking)
            Movement->SetMovementMode(EMovementMode::MOVE_Walking, 0);
    }
}
inline void MaintainGroundPawn(AFortPlayerPawnAthena *Pawn)
{
    if (!Pawn || Pawn->IsDead() || Pawn->bIsSkydiving || Pawn->bIsParachuteOpen)
        return;
    auto Movement = Pawn->CharacterMovement;
    if (!Movement)
        return;
    // Repair a disabled pawn without restarting healthy path following or
    // activating/resetting its movement component every maintenance interval.
    if (!Pawn->GetActorEnableCollision() || !Movement->IsActive() ||
        Movement->UpdatedComponent != Pawn->CapsuleComponent ||
        Movement->MovementMode == EMovementMode::MOVE_None ||
        Movement->MovementMode == EMovementMode::MOVE_NavWalking)
        EnableGroundPawn(Pawn);
    else if (Movement->PrimaryComponentTick.TickInterval > 0.f)
        Movement->SetComponentTickInterval(0.f);
}
inline bool SafeBuildAirStep(AFortPlayerPawnAthena *Pawn,const FVector &Direction,
                             ABuildingSMActor *Platform,ABuildingSMActor *Stair,float Probe)
{
    if(!Pawn || !Pawn->CapsuleComponent || !Pawn->CharacterMovement ||
       Pawn->CharacterMovement->MovementMode!=EMovementMode::MOVE_Falling ||
       !Syndicate::Finite(Direction) || !std::isfinite(Probe) || Probe<=0.f) return false;
    auto Here=Pawn->K2_GetActorLocation();auto Ahead=Here+Direction*std::min(Probe,60.f);
    float Half=Pawn->CapsuleComponent->GetScaledCapsuleHalfHeight();float Feet=Here.Z-Half;
    TArray<AActor*> Ignored;Ignored.Add(Pawn);FHitResult Support{};
    bool Found=UKismetSystemLibrary::LineTraceSingleByProfile(UWorld::GetWorld(),
        Ahead+FVector(0,0,30.f),FVector(Ahead.X,Ahead.Y,Feet-500.f),PawnProfile(),false,
        Ignored,EDrawDebugTrace::None,&Support,true,{},{},0);
    auto Under=Support.Actor.Get();
    bool Owned=Under && ((Platform && !Platform->bDestroyed && Under==Platform) ||
                        (Stair && !Stair->bDestroyed && Under==Stair));
    if(!Found || Support.bStartPenetrating || !Owned || !Syndicate::Finite(Support.ImpactPoint) ||
       Support.ImpactNormal.Z<Pawn->CharacterMovement->WalkableFloorZ ||
       Feet-Support.ImpactPoint.Z>480.f || Support.ImpactPoint.Z>Feet+Pawn->CharacterMovement->MaxStepHeight)
    {Ignored.Free();return false;}
    // Prove support under both sides of the capsule, not just its center.
    // Center-only probes admitted air input off the side of a narrow crest.
    FVector Side(-Direction.Y,Direction.X,0);float Margin=Pawn->CapsuleComponent->CapsuleRadius*.75f;
    for(float Offset:{-Margin,Margin}){
        auto Edge=Ahead+Side*Offset;FHitResult H{};
        bool EdgeHit=UKismetSystemLibrary::LineTraceSingleByProfile(UWorld::GetWorld(),Edge+FVector(0,0,30.f),
            FVector(Edge.X,Edge.Y,Feet-500.f),PawnProfile(),false,Ignored,EDrawDebugTrace::None,&H,true,{},{},0);
        auto A=H.Actor.Get();bool OwnedEdge=A&&((Platform&&!Platform->bDestroyed&&A==Platform)||(Stair&&!Stair->bDestroyed&&A==Stair));
        if(!EdgeHit||H.bStartPenetrating||!OwnedEdge||!Syndicate::Finite(H.ImpactPoint)||
           H.ImpactNormal.Z<Pawn->CharacterMovement->WalkableFloorZ||Feet-H.ImpactPoint.Z>480.f)
        {Ignored.Free();return false;}
    }
    FHitResult Hit{};
    bool Blocked=UKismetSystemLibrary::CapsuleTraceSingleByProfile(UWorld::GetWorld(),Here,Ahead,
        Pawn->CapsuleComponent->CapsuleRadius,Half-4.f,PawnProfile(),false,Ignored,
        EDrawDebugTrace::None,&Hit,true,{},{},0);
    Ignored.Free();return !Hit.bStartPenetrating && (!Blocked || Syndicate::As<APawn>(Hit.Actor.Get()));
}
inline bool SafeStep(AFortPlayerPawnAthena *Pawn, const FVector &Direction,
                     const std::vector<Syndicate::Ref<AFortPlayerPawnAthena>> *Crowd = nullptr, bool AllowShortDrop = false,
                     float ProbeDistance = 60.f)
{
    if (!Pawn || !Pawn->CapsuleComponent || !std::isfinite(ProbeDistance) || ProbeDistance<=0.f)
        return false;
    auto Here = Pawn->K2_GetActorLocation();
    auto Movement = Pawn->CharacterMovement;
    float Half = Pawn->CapsuleComponent->CapsuleHalfHeight;
    float Step = Movement ? std::max(0.f, Movement->MaxStepHeight) : 45.f;
    // Follow sampled terrain without raising the full capsule above the pawn.
    // Raising both endpoints by MaxStepHeight falsely collides with overhead
    // stairs/ceilings even when the native character fits underneath them.
    // This is only a query; native character collision remains authoritative.
    // A supported ramp goal can be closer than the ordinary 60-unit probe.
    // Do not inspect past that endpoint and reject the safe crest as a ledge.
    // Floor and capsule collision are still queried at the actual endpoint.
    auto Ahead = Here + Direction * std::min(ProbeDistance,60.f);
    float Feet = Here.Z - Half;
    TArray<AActor *> FloorIgnored;
    FloorIgnored.Add(Pawn);
    if (Crowd)
        for (const auto &Ref : *Crowd)
            if (auto Other = Ref.Get(); Other && Other != Pawn) FloorIgnored.Add(Other);
    FHitResult FloorHit{};
    bool Found = UKismetSystemLibrary::LineTraceSingleByProfile(
        UWorld::GetWorld(), FVector(Ahead.X, Ahead.Y, Feet + Step + 65.f),
        FVector(Ahead.X, Ahead.Y, Feet - (AllowShortDrop ? 430.f : 70.f)), PawnProfile(), false, FloorIgnored,
        EDrawDebugTrace::None, &FloorHit, true, {}, {}, 0);
    FloorIgnored.Free();
    float WalkableZ = Movement ? Movement->WalkableFloorZ : 0.7f;
    if (!Found || FloorHit.bStartPenetrating || Syndicate::As<APawn>(FloorHit.Actor.Get()) ||
        !Syndicate::Finite(FloorHit.ImpactPoint)) { ++ProbeMissingFloor; return false; }
    if (FloorHit.ImpactNormal.Z < WalkableZ) { ++ProbeSteepFloor; return false; }
    if (Feet - FloorHit.ImpactPoint.Z > (AllowShortDrop ? 384.f : 65.f)) { ++ProbeLedge; return false; }
    auto Start = Here;
    auto End = FVector(Ahead.X, Ahead.Y, FloorHit.ImpactPoint.Z + Half + 4.f);
    TArray<AActor *> Ignored;
    Ignored.Add(Pawn);
    FHitResult Hit{};
    bool Blocked = UKismetSystemLibrary::CapsuleTraceSingleByProfile(
        UWorld::GetWorld(), Start, End,
        Pawn->CapsuleComponent->CapsuleRadius, Pawn->CapsuleComponent->CapsuleHalfHeight - 4.f,
        PawnProfile(), false, Ignored, EDrawDebugTrace::None, &Hit, true, {}, {}, 0);
    Ignored.Free();
    // A terrain sweep hit is normal when walking uphill. Native movement
    // handles that surface and resolves pawn bumps/crowding itself; refusing
    // input for either left most landed bots stationary.
    bool Walkable = Pawn->CharacterMovement && !Hit.bStartPenetrating &&
                    Hit.ImpactNormal.Z >= Pawn->CharacterMovement->WalkableFloorZ;
    bool Safe = !Blocked || Walkable || Syndicate::As<APawn>(Hit.Actor.Get());
    if (Safe) ++ProbeAccepted;
    else
    {
        ++ProbeObstacle;
        LastObstacle.Set(Hit.Actor.Get());
        LastBlockedPawn.Set(Pawn);
        LastObstacleNormal = Hit.ImpactNormal;
        LastProbeStart = Start;
        LastProbeEnd = End;
        LastProbePenetrating = Hit.bStartPenetrating;
    }
    return Safe;
}
inline bool NativeGroundStep(AFortPlayerPawnAthena *Pawn,const FVector &Direction,float Probe=45.f)
{
    // Recovery input is still resolved by the normal character capsule.
    // Prove nearby walkable footing, rather than using a second capsule sweep
    // to prohibit native step-up/slide movement before it can happen.
    if(!Pawn || !Pawn->CapsuleComponent || !Pawn->CharacterMovement ||
       (Pawn->CharacterMovement->MovementMode!=EMovementMode::MOVE_Walking &&
        Pawn->CharacterMovement->MovementMode!=EMovementMode::MOVE_Falling) ||
       !Syndicate::Finite(Direction) || !std::isfinite(Probe) || Probe<=0.f) return false;
    auto Here=Pawn->K2_GetActorLocation();float Half=Pawn->CapsuleComponent->GetScaledCapsuleHalfHeight();
    float Feet=Here.Z-Half,Step=std::max(0.f,Pawn->CharacterMovement->MaxStepHeight);
    auto At=Here+Direction*std::min(Probe,45.f);TArray<AActor*> Ignored;Ignored.Add(Pawn);FHitResult H{};
    bool Found=UKismetSystemLibrary::LineTraceSingleByProfile(UWorld::GetWorld(),
        FVector(At.X,At.Y,Feet+Step+20.f),FVector(At.X,At.Y,Feet-65.f),
        PawnProfile(),false,Ignored,EDrawDebugTrace::None,&H,true,{},{},0);
    Ignored.Free();
    if(!Found || H.bStartPenetrating || Syndicate::As<APawn>(H.Actor.Get()) ||
       !Syndicate::Finite(H.ImpactPoint) || H.ImpactPoint.Z>Feet+Step || Feet-H.ImpactPoint.Z>65.f) return false;
    auto Movement=Pawn->CharacterMovement;
    if(H.ImpactNormal.Z>=Movement->WalkableFloorZ)
        return true; // Ordinary near-ground air control may also free a wedged falling capsule.
    // A pawn already wedged on a non-walkable slope may use ordinary input
    // downhill. This grants neither uphill traction nor artificial support;
    // native collision/sliding/gravity remain authoritative.
    auto Normal=H.ImpactNormal; // Use this real query; CurrentFloor can be cleared while falling.
    return Syndicate::Finite(Normal) && Normal.Z>.15f && Normal.Z<Movement->WalkableFloorZ &&
        Direction.X*Normal.X+Direction.Y*Normal.Y>.1f;

}
} // namespace BotPhysics
