#pragma once
#include "BotPhysics.h"
namespace BotLanding
{
inline constexpr int MaxNearbyArrivals=2;
inline std::vector<FVector> Claims;
inline int Density(const FVector &Point)
{
    int Count=0;
    for (auto &Claimed : Claims) if (Syndicate::Distance2D(Point,Claimed)<1000.f) ++Count;
    return Count;
}
inline void Claim(const FVector &Point) { if (Syndicate::Finite(Point) && !Point.IsZero()) Claims.push_back(Point); }
inline float PlanFrame=-1.f;
inline int PlansThisFrame=0;
inline bool PermitPlanning(float Now)
{
    if (Now != PlanFrame) { PlanFrame=Now; PlansThisFrame=0; }
    return PlansThisFrame++ < 2; // Spread roof probes across native frames, not all 99 at once.
}
inline float DepartureTime(const FVector &Bus, const FVector &Velocity, const FVector &Target,
                           float Now, float Begin, float End)
{
    float SpeedSquared=Velocity.X*Velocity.X+Velocity.Y*Velocity.Y;
    float Closest=SpeedSquared>1.f ? Now+((Target.X-Bus.X)*Velocity.X+(Target.Y-Bus.Y)*Velocity.Y)/SpeedSquared : Begin;
    return std::clamp(Closest,std::max(Now,Begin),std::max(std::max(Now,Begin),End));
}
inline bool ReachableFromRoute(const FVector &Target,const FVector &Start,const FVector &End,float Range=200000.f)
{
    auto D=End-Start;
    float L=D.X*D.X+D.Y*D.Y;
    float T=L>1.f ? std::clamp(((Target.X-Start.X)*D.X+(Target.Y-Start.Y)*D.Y)/L,0.f,1.f) : 0.f;
    return Syndicate::Finite(Target) && Syndicate::Distance2D(Target,Start+D*T) <= Range;
}
inline bool Inside(const FVector &Point, const FVector &Center, const FVector &Extent)
{
    return Syndicate::Finite(Point) && Syndicate::Finite(Center) && Syndicate::Finite(Extent) &&
        Extent.X > 0.f && Extent.Y > 0.f &&
        std::abs(Point.X-Center.X) <= Extent.X && std::abs(Point.Y-Center.Y) <= Extent.Y;
}
inline bool Surface(const FVector &Loot, FVector &Point, bool &Structure)
{
    Structure = false;
    if (!Syndicate::Finite(Loot)) return false;
    FHitResult Hit{}; TArray<AActor *> Ignored;
    // Approach the actual highest walkable surface from the sky. A floor
    // probe from inside a house hits its ceiling and rejects the indoor loot.
    bool Found = UKismetSystemLibrary::LineTraceSingleByProfile(UWorld::GetWorld(),
        Loot + FVector(0,0,10000.f), Loot - FVector(0,0,600.f), BotPhysics::PawnProfile(),
        false, Ignored, EDrawDebugTrace::None, &Hit, true, {}, {}, 0);
    Ignored.Free();
    auto Actor = Hit.Actor.Get();
    if (!Found || Hit.bStartPenetrating || Hit.ImpactNormal.Z < 0.65f ||
        !Syndicate::Finite(Hit.ImpactPoint) || Cast<APawn>(Actor)) return false;
    Point = Hit.ImpactPoint;
    Structure = Cast<ABuildingSMActor>(Actor) != nullptr;
    return true;
}
struct Region
{
    std::string Key;
    FVector Center{}, Extent{};
    int Assigned=0, Arrived=0;
    float RetryAt=0.f;
};
inline std::vector<Region> Regions;
inline bool RegisterFoundation(ABuildingFoundation *Foundation)
{
    if (!Foundation || Foundation->IsActorBeingDestroyed() || !Foundation->RootComponent ||
        Foundation->FoundationEnabledState==EDynamicFoundationEnabledState::Disabled) return false;
    auto Key=Foundation->MapLocationTag.TagName.ToString();
    if (!Key.starts_with("Athena.Location.POI.")) return false;
    // These reflected streaming bounds survive removal of the POI brush root.
    // Transform all corners: streaming boxes are local, not map coordinates.
    auto Box=Foundation->StreamingBoundingBox;
    if (!Box.IsValid || !Syndicate::Finite(Box.min_0) || !Syndicate::Finite(Box.max_0)) return false;
    auto Transform=Foundation->GetTransform();
    FVector Low{}, High{};
    for (int I=0; I<8; ++I)
    {
        FVector Local((I&1)?Box.max_0.X:Box.min_0.X,(I&2)?Box.max_0.Y:Box.min_0.Y,(I&4)?Box.max_0.Z:Box.min_0.Z);
        auto World=UKismetMathLibrary::TransformLocation(Transform,Local);
        if (!Syndicate::Finite(World)) return false;
        if (!I) Low=High=World;
        else { Low.X=std::min(Low.X,World.X); Low.Y=std::min(Low.Y,World.Y); Low.Z=std::min(Low.Z,World.Z);
               High.X=std::max(High.X,World.X); High.Y=std::max(High.Y,World.Y); High.Z=std::max(High.Z,World.Z); }
    }
    auto Center=(Low+High)*0.5f, Extent=(High-Low)*0.5f;
    if (Extent.X<100.f || Extent.Y<100.f || Extent.X>22000.f || Extent.Y>22000.f || Center.IsZero()) return false;
    for (auto &R : Regions) if (R.Key==Key) return true; // Alternate slabs are one POI, not five slots.
    Regions.push_back({Key,Center,Extent});
    return true;
}
inline Region *FindRegion(const std::string &Key)
{ for (auto &R : Regions) if (R.Key==Key) return &R; return nullptr; }
inline void RecordArrival(const std::string &Key,const FVector &At)
{ if (auto R=FindRegion(Key); R && Inside(At,R->Center,R->Extent)) ++R->Arrived; }
inline void Reset() { Claims.clear(); Regions.clear(); PlanFrame=-1.f; PlansThisFrame=0; }
} // namespace BotLanding
