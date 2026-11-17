#pragma once
#include "BotPhysics.h"
#include "UnseenStormPolicy.h"

namespace UnseenStormRecovery
{
struct Observer
{
    Syndicate::Ref<APlayerController> Controller;
    Syndicate::Ref<AActor> Target, PendingTarget, Body, Spectator, DeathTarget, NetworkTarget;
    Syndicate::Ref<UNetConnection> Connection;
    std::vector<FVector> Eyes;
    FVector Facing{};
};
struct Reservation { FVector Point; float Until; };
inline std::vector<Reservation> Recent;
inline std::vector<Syndicate::Ref<AFortPlayerPawnAthena>> Waiting;
inline std::vector<Syndicate::Ref<APhysicsVolume>> WaterVolumes;
inline std::vector<Syndicate::Ref<AActor>> WaterSurfaces;
inline float NextWaterScan = 0.f;
inline float NextWork = 0.f;
inline unsigned int Relocations = 0, VisibleBlocks = 0, NoDestination = 0, StormEvents = 0;
inline void Reset()
{ Recent.clear(); Waiting.clear(); WaterVolumes.clear(); WaterSurfaces.clear(); NextWaterScan=0.f; NextWork=0.f; Relocations=VisibleBlocks=NoDestination=StormEvents=0; }

inline bool Eyes(AActor *Actor, std::vector<FVector> &Out)
{
    if (!Actor || Actor->IsActorBeingDestroyed()) return false;
    FVector Point{}; FRotator Rotation{}; Actor->GetActorEyesViewPoint(&Point,&Rotation);
    if (!Syndicate::Finite(Point) || !Syndicate::Finite(Actor->K2_GetActorLocation())) return false;
    Out.push_back(Point); Out.push_back(Actor->K2_GetActorLocation()+FVector(0,0,70.f));
    return true;
}
inline bool Observers(std::vector<Observer> &Out)
{
    // Include dead humans and spectators; AI controllers inherit AAIController,
    // not APlayerController. Never use only the local player's camera.
    for (auto &Ref : Syndicate::ActorsOf<APlayerController>())
    {
        auto PC=Ref.Get(); auto Camera=PC ? Syndicate::Ref<APlayerCameraManager>(PC->PlayerCameraManager).Get() : nullptr;
        if (!PC || !Camera) return false; // Unknown remote camera: fail closed.
        Observer O; O.Controller.Set(PC); O.Target.Set(PC->GetViewTarget());
        O.Connection.Set(PC->NetConnection);
        if (auto Connection=O.Connection.Get()) O.NetworkTarget.Set(Connection->ViewTarget);
        O.PendingTarget.Set(Camera->PendingViewTarget.Target); O.Body.Set(PC->Pawn);
        O.Spectator.Set(PC->GetSpectatorPawn());
        if (auto FortPC=Cast<AFortPlayerControllerAthena>(PC)) O.DeathTarget.Set(FortPC->PlayerToSpectateOnDeath);
        if (!O.Body.Get() && !O.Spectator.Get() && O.Target.Get()==PC) return false;
        auto Position=Camera->GetCameraLocation();
        auto Rotation=Camera->GetCameraRotation();
        if (!std::isfinite(Rotation.Pitch) || !std::isfinite(Rotation.Yaw) || !std::isfinite(Rotation.Roll)) return false;
        constexpr float Radians=.01745329252f;
        float Pitch=Rotation.Pitch*Radians,Yaw=Rotation.Yaw*Radians;
        O.Facing=FVector(std::cos(Pitch)*std::cos(Yaw),std::cos(Pitch)*std::sin(Yaw),std::sin(Pitch));
        if (!Syndicate::Finite(Position) || !O.Target.Get() || !Eyes(O.Target.Get(),O.Eyes)) return false;
        O.Eyes.push_back(Position);
        if (auto P=O.PendingTarget.Get(); P && !Eyes(P,O.Eyes)) return false;
        if (auto P=O.Body.Get(); P && !Eyes(P,O.Eyes)) return false;
        if (auto P=O.Spectator.Get(); P && !Eyes(P,O.Eyes)) return false;
        Out.push_back(std::move(O));
    }
    // The owned host without a joined player is not a useful visibility test.
    return !Out.empty();
}
inline bool SolidOccluder(const FHitResult &Hit)
{
    auto Actor=Syndicate::Ref<AActor>(Hit.Actor.Get()).Get();
    if (!Actor || Actor->bCanBeDamaged || Hit.bStartPenetrating || !Syndicate::Finite(Hit.ImpactPoint) ||
        Cast<APawn>(Actor) || Cast<ABuildingProp>(Actor)) return false;
    // A player can destroy/reset a build before the relocation is replicated.
    if (auto Building=Cast<ABuildingSMActor>(Actor); Building &&
        (Building->bPlayerPlaced || Building->bDestroyed)) return false;
    return true;
}
inline bool NotReplicated(AFortPlayerPawnAthena *Pawn,const Observer &O)
{
    auto Connection=O.Connection.Get();
    if (!Pawn || Pawn->bTearOff || (Pawn->NetDormancy!=ENetDormancy::DORM_Awake && Pawn->NetDormancy!=ENetDormancy::DORM_Never) ||
        !Connection || Cast<UChildConnection>(Connection) || Connection->OpenChannels.Num()<=0 || Connection->OpenChannels.Num()>20000) return false;
    // Live engine-owned channels on the game thread. A dormant/torn-off actor
    // could remain on a client without a channel, so those are excluded above.
    for (auto Channel : Connection->OpenChannels)
    {
        if (!Channel) return false;
        if (auto ActorChannel=Cast<UActorChannel>(Channel); ActorChannel && ActorChannel->Actor==Pawn) return false;
    }
    return true;
}
inline bool Hidden(AFortPlayerPawnAthena *Pawn, const FVector &At, const std::vector<Observer> &Viewers,bool Departure=false)
{
    if (!Pawn || !Pawn->CapsuleComponent || !Syndicate::Finite(At) || Viewers.empty()) return false;
    float Half=Pawn->CapsuleComponent->GetScaledCapsuleHalfHeight();
    float Radius=Pawn->CapsuleComponent->GetScaledCapsuleRadius();
    if (!std::isfinite(Half) || !std::isfinite(Radius) || Half<=0.f || Radius<=0.f) return false;
    // Within the widened view cone, every padded body sample must be occluded.
    // Far behind the camera may qualify without terrain. Foliage and damageable
    // cover cannot certify occlusion inside that cone.
    FVector Samples[] = {At,At+FVector(0,0,Half+80.f),At-FVector(0,0,Half),
        At+FVector(Radius+100.f,0,0),At-FVector(Radius+100.f,0,0),
        At+FVector(0,Radius+100.f,0),At-FVector(0,Radius+100.f,0)};
    for (const auto &O : Viewers)
    {
        auto PC=O.Controller.Get();
        if (!PC || O.Target.Get()==Pawn || O.PendingTarget.Get()==Pawn || O.Body.Get()==Pawn || O.Spectator.Get()==Pawn || O.DeathTarget.Get()==Pawn || O.NetworkTarget.Get()==Pawn)
            return false;
        bool Absent=Departure && NotReplicated(Pawn,O);
        for (auto Eye : O.Eyes)
        {
            if (!UnseenStormPolicy::Separated(At,Eye,UnseenStormPolicy::HumanDistance)) return false;
            if (Absent) continue;
            if (UnseenStormPolicy::OutsideView(At,Eye,O.Facing)) continue;
            for (auto Point : Samples)
            {
                TArray<AActor*> Ignored; Ignored.Add(Pawn);
                if (auto Body=O.Body.Get()) Ignored.Add(Body);
                if (auto Body=O.Spectator.Get()) Ignored.Add(Body);
                FHitResult Hit{};
                bool Blocked=UKismetSystemLibrary::LineTraceSingle(UWorld::GetWorld(),Eye,Point,
                    ETraceTypeQuery::TraceTypeQuery1,true,Ignored,EDrawDebugTrace::None,&Hit,true,{},{},0.f);
                Ignored.Free();
                if (!Blocked || !SolidOccluder(Hit) || (Hit.ImpactPoint-Point).Magnitude()<200.f) return false;
            }
        }
    }
    return true;
}
inline bool InCircle(AFortSafeZoneIndicator *Zone, const FVector &At)
{
    if (!Zone) return false;
    float Radius=Zone->GetSafeZoneRadius();
    float Inset=std::clamp(Radius*.12f,150.f,1500.f);
    if (!UnseenStormPolicy::Inside<FVector>(At,Zone->GetSafeZoneCenter(),Radius,Inset)) return false;
    // Use the next circle as well when it is a real, smaller active circle.
    if (std::isfinite(Zone->NextRadius) && Zone->NextRadius>0.f && Zone->NextRadius<Radius)
        return UnseenStormPolicy::Inside<FVector>(At,Zone->NextCenter,Zone->NextRadius,std::clamp(Zone->NextRadius*.12f,150.f,1500.f));
    return true;
}
inline bool Dry(const FVector &At)
{
    float Now=Syndicate::Now();
    if (Now>=NextWaterScan)
    {
        NextWaterScan=Now+8.f; WaterVolumes=Syndicate::ActorsOf<APhysicsVolume>(); WaterSurfaces.clear();
        for (auto &Ref : Syndicate::ActorsOf<AFortAthenaWater>())
            if (auto Water=Ref.Get()) WaterSurfaces.emplace_back(Water);
        for (auto &Ref : Syndicate::ActorsOf<AFortStaticMeshActor>())
            if (auto Surface=Ref.Get())
            {
                auto Name=Surface->GetName();
                std::transform(Name.begin(),Name.end(),Name.begin(),[](unsigned char C){return static_cast<char>(std::tolower(C));});
                // These actual 10.40 static water meshes are not physics volumes
                // or AFortAthenaWater (notably SM_Athena_Water_Ocean3).
                if (Name.contains("water_ocean") || Name.contains("waterplane") || Name.contains("water_plane"))
                    WaterSurfaces.emplace_back(Surface);
            }
    }
    for (auto &Ref : WaterVolumes)
        if (auto Volume=Ref.Get(); Volume && (Volume->bWaterVolume || Cast<AWaterVolume_Shallow>(Volume)))
        {
            FVector Center{},Extent{}; Volume->GetActorBounds(false,&Center,&Extent);
            if (!Syndicate::Finite(Center) || !Syndicate::Finite(Extent)) return false;
            if (std::abs(At.X-Center.X)<=Extent.X+100.f && std::abs(At.Y-Center.Y)<=Extent.Y+100.f &&
                std::abs(At.Z-Center.Z)<=Extent.Z+150.f) return false;
        }
    for (auto &Ref : WaterSurfaces)
        if (auto Water=Ref.Get())
        {
            FVector Center{},Extent{}; Water->GetActorBounds(false,&Center,&Extent);
            if (!Syndicate::Finite(Center) || !Syndicate::Finite(Extent) || Extent.X<=0.f || Extent.Y<=0.f) return false;
            // The actor origin is not its water height: the ocean actor is at
            // origin zero, while its actual mesh surface is lower in the map.
            if (std::abs(At.X-Center.X)<=Extent.X+100.f && std::abs(At.Y-Center.Y)<=Extent.Y+100.f &&
                At.Z<=Center.Z+Extent.Z+150.f) return false;
        }
    return true;
}
inline bool Ground(AFortPlayerPawnAthena *Pawn, const FVector &XY, FVector &Out)
{
    if (!Pawn || !Pawn->CapsuleComponent || !Syndicate::Finite(XY)) return false;
    TArray<AActor*> Ignored; Ignored.Add(Pawn); FHitResult Floor{};
    bool Found=UKismetSystemLibrary::LineTraceSingleByProfile(UWorld::GetWorld(),
        FVector(XY.X,XY.Y,30000.f),FVector(XY.X,XY.Y,-25000.f),BotPhysics::PawnProfile(),false,
        Ignored,EDrawDebugTrace::None,&Floor,true,{},{},0.f);
    auto Actor=Floor.Actor.Get();
    bool Good=Found && !Floor.bStartPenetrating && Actor && !Cast<APawn>(Actor) &&
        !Cast<ABuildingProp>(Actor) && !Cast<AFortAthenaWater>(Actor) &&
        Syndicate::Finite(Floor.ImpactPoint) && Floor.ImpactNormal.Z>=.8f;
    if (auto B=Cast<ABuildingSMActor>(Actor); B && (B->bPlayerPlaced || B->bDestroyed)) Good=false;
    if (Good)
    {
        float Half=Pawn->CapsuleComponent->GetScaledCapsuleHalfHeight();
        float Radius=Pawn->CapsuleComponent->GetScaledCapsuleRadius();
        Good=std::isfinite(Half) && std::isfinite(Radius) && Half>0.f && Radius>0.f;
        Out=Floor.ImpactPoint+FVector(0,0,Half+4.f);
        FHitResult Space{};
        if (Good) Good=!UKismetSystemLibrary::CapsuleTraceSingleByProfile(UWorld::GetWorld(),Out,
            Out+FVector(0,0,1.f),Radius+10.f,Half,BotPhysics::PawnProfile(),false,
            Ignored,EDrawDebugTrace::None,&Space,true,{},{},0.f) && !Space.bStartPenetrating;
    }
    Ignored.Free();
    return Good && Dry(Out);
}
inline bool Spaced(AFortPlayerPawnAthena *Pawn, const FVector &At,
                   const std::vector<Syndicate::Ref<AFortPlayerPawnAthena>> &Crowd)
{
    for (const auto &Ref : Crowd)
        if (auto Other=Ref.Get(); Other && Other!=Pawn && !Other->IsDead() &&
            !UnseenStormPolicy::Separated(At,Other->K2_GetActorLocation(),UnseenStormPolicy::BotDistance)) return false;
    for (const auto &R : Recent)
        if (!UnseenStormPolicy::Separated(At,R.Point,UnseenStormPolicy::BotDistance)) return false;
    return true;
}
inline bool Try(AFortPlayerPawnAthena *Pawn, AFortGameStateAthena *GS, UnseenStormPolicy::State &State,
                const std::vector<Syndicate::Ref<AFortPlayerPawnAthena>> &Crowd)
{
    float Now=Syndicate::Now();
    auto Move=Pawn ? Pawn->CharacterMovement : nullptr;
    bool Grounded=Move && Move->MovementMode==EMovementMode::MOVE_Walking && !Pawn->bIsSkydiving && !Pawn->bIsParachuteOpen;
    std::erase_if(Waiting,[](const auto &Ref){auto P=Ref.Get();return !P || P->IsDead() || !P->bIsInAnyStorm;});
    if (!State.Eligible(Now,Pawn && !Pawn->IsDead() && !Pawn->bIsDBNO,Grounded,Pawn && Pawn->bIsInAnyStorm,
        GS && GS->GamePhase>=EAthenaGamePhase::SafeZones && GS->GamePhase<EAthenaGamePhase::EndGame))
    { std::erase_if(Waiting,[Pawn](const auto &Ref){return Ref.Get()==Pawn;}); return false; }
    if (Now<State.NextObservation) return false;
    State.NextObservation=Now+.1f;
    std::vector<Observer> Viewers;
    bool Safe=Observers(Viewers) && Hidden(Pawn,Pawn->K2_GetActorLocation(),Viewers,true);
    if (!State.ObserveHidden(Now,Safe))
    {
        if (!Safe) {++VisibleBlocks; std::erase_if(Waiting,[Pawn](const auto &Ref){return Ref.Get()==Pawn;});}
        return false;
    }
    if (std::none_of(Waiting.begin(),Waiting.end(),[Pawn](const auto &Ref){return Ref.Get()==Pawn;})) Waiting.emplace_back(Pawn);
    if (Waiting.front().Get()!=Pawn) return false;
    if (Now<NextWork) return false;
    Waiting.erase(Waiting.begin()); // Fair service: failed candidates cannot monopolize the shared budget.
    NextWork=Now+.03f; // At most one recovery per normal network frame.
    std::erase_if(Recent,[Now](const auto &R){return Now>=R.Until;});
    auto Zone=Syndicate::Ref<AFortSafeZoneIndicator>(GS->SafeZoneIndicator).Get();
    if (!Zone) return false;
    auto Source=Pawn->K2_GetActorLocation();
    bool Moved=State.Try<FVector>(Now,[&](FVector &At)
    {
        auto Center=Zone->GetSafeZoneCenter(); float Radius=Zone->GetSafeZoneRadius();
        if (std::isfinite(Zone->NextRadius) && Zone->NextRadius>0.f && Zone->NextRadius<Radius)
        { Center=Zone->NextCenter; Radius=Zone->NextRadius; }
        if (!Syndicate::Finite(Center) || !std::isfinite(Radius) || Radius<=250.f) return false;
        float Angle=Syndicate::Between(0.f,6.2831853f), Distance=std::sqrt(Syndicate::Between(0.f,1.f))*Radius*.8f;
        FVector XY=Center+FVector(std::cos(Angle)*Distance,std::sin(Angle)*Distance,0.f);
        return InCircle(Zone,XY) && Spaced(Pawn,XY,Crowd) && Ground(Pawn,XY,At);
    },[&](const FVector &At)
    {
        // Fresh observers and actual world poses immediately before the move.
        std::vector<Observer> Current;
        FVector Checked{};
        return Observers(Current) && InCircle(Zone,At) && Spaced(Pawn,At,Crowd) &&
            Ground(Pawn,At,Checked) && (Checked-At).Magnitude()<10.f &&
            Hidden(Pawn,Pawn->K2_GetActorLocation(),Current,true) && Hidden(Pawn,At,Current);
    },[&](const FVector &At)
    {
        if (!Pawn->K2_TeleportTo(At,Pawn->K2_GetActorRotation())) return false;
        Move->StopMovementImmediately(); Pawn->ForceNetUpdate();
        Recent.push_back({At,Now+120.f}); ++Relocations;
        Log(std::format("Unseen storm relocation: pawn={} from={},{},{} to={},{},{} observers={} health={} moves={}",
            Pawn->GetName(),Source.X,Source.Y,Source.Z,At.X,At.Y,At.Z,Viewers.size(),Pawn->GetHealth(),State.Moves+1));
        return true;
    });
    if (!Moved) ++NoDestination;
    return Moved;
}
}
