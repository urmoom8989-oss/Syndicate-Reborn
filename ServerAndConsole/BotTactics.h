#pragma once
#include "SyndicateRuntime.h"
namespace BotTactics
{
enum class Posture { Standing, Harvest, Pickup, Healing };
struct CrouchRhythm
{
    Posture Intent=Posture::Standing, Applied=Posture::Standing;
    float Until=0.f, PhaseAt=0.f, Tempo=.31f;
    bool Down=false;
    void Request(Posture Mode,float Now,float Duration=.25f) { Intent=Mode;Until=Now+Duration; }
    bool Sample(float Now,bool Permitted)
    {
        auto Mode=Permitted && Now<Until ? Intent : Posture::Standing;
        if(Mode!=Applied) { Applied=Mode;Down=Mode!=Posture::Standing;PhaseAt=Now+Tempo; }
        if(Mode==Posture::Healing && Now>=PhaseAt)
        { Down=!Down;PhaseAt=Now+(Down ? Tempo : Tempo*.8f); }
        return Mode!=Posture::Standing && Down;
    }
};
// Accept a genuine world-space native weak spot only on this reachable surface.
// Unproven/local-space records fail these checks and fall back to ordinary hits.
inline bool ReachableWeakSpot(const FVector &Eyes,const FVector &Point,const FVector &Normal,
                              const FVector &Origin,const FVector &Extent)
{
    if(!Syndicate::Finite(Eyes) || !Syndicate::Finite(Point) || !Syndicate::Finite(Normal) ||
       !Syndicate::Finite(Origin) || !Syndicate::Finite(Extent)) return false;
    auto D=Eyes-Point;float Range=std::sqrt(D.X*D.X+D.Y*D.Y+D.Z*D.Z);
    float Norm=std::sqrt(Normal.X*Normal.X+Normal.Y*Normal.Y+Normal.Z*Normal.Z);
    return Range>=15.f && Range<=330.f && Norm>.7f && Norm<1.3f &&
        D.X*Normal.X+D.Y*Normal.Y+D.Z*Normal.Z>5.f &&
        std::abs(Point.X-Origin.X)<=Extent.X+35.f && std::abs(Point.Y-Origin.Y)<=Extent.Y+35.f &&
        std::abs(Point.Z-Origin.Z)<=Extent.Z+35.f;
}
inline bool SupportedRampCrest(const FVector &Here,const FVector &Top,float Feet,float UpperZ,const FVector &Support)
{
    return Syndicate::Finite(Here) && Syndicate::Finite(Top) && Syndicate::Finite(Support) &&
        std::isfinite(Feet) && std::isfinite(UpperZ) && Syndicate::Distance2D(Here,Top)<45.f &&
        Feet>=UpperZ-20.f && Feet<=UpperZ+75.f && Feet-Support.Z>=-12.f && Feet-Support.Z<30.f;
}
struct ShotEvidence
{
    enum class Kind { None, NativeFire, ConsumedRound };
    bool Seeded=false;float FireAt=0.f;int Ammo=0;
    Kind Sample(float NativeFire,int Loaded,bool Reloading)
    {
        if(!Seeded) { Seeded=true;FireAt=NativeFire;Ammo=Loaded;return Kind::None; }
        bool Native=std::isfinite(NativeFire) && std::isfinite(FireAt) && NativeFire>FireAt+.001f;
        bool Consumed=!Reloading && Ammo>0 && Loaded>=0 && Loaded<Ammo;
        if(std::isfinite(NativeFire)) FireAt=NativeFire;
        Ammo=Loaded;
        return Native ? Kind::NativeFire : (Consumed ? Kind::ConsumedRound : Kind::None);
    }
};
inline bool CornerCellContains(const FVector &Center,const FVector &Half,float Height,const FVector &Here,float Capsule)
{
    float Feet=Here.Z-Capsule;
    return Syndicate::Finite(Center) && Syndicate::Finite(Half) && Syndicate::Finite(Here) &&
        std::isfinite(Height) && Height>0.f && std::isfinite(Capsule) && Capsule>0.f &&
        std::abs(Here.X-Center.X)<=Half.X+120.f && std::abs(Here.Y-Center.Y)<=Half.Y+120.f &&
        Feet>=Center.Z-25.f && Feet<=Center.Z+Height+60.f;
}
struct Footfall
{
    FVector Last{}; float At=0.f,Travel=0.f; bool Seeded=false;
    bool Sample(const FVector &Here,float Now,bool Grounded,float Speed,bool Crouched)
    {
        if(!Syndicate::Finite(Here) || !std::isfinite(Now) || !std::isfinite(Speed)) { Seeded=false; Travel=0.f; return false; }
        if(!Seeded) { Last=Here; At=Now; Seeded=true; return false; }
        float DT=Now-At,Distance=Syndicate::Distance2D(Last,Here); Last=Here;At=Now;
        // Ignore airborne travel, spawn/network corrections, stationary shuffling and stale samples.
        if(!Grounded || Speed<70.f || DT<=0.f || DT>1.f || Distance>std::max(200.f,Speed*DT*1.6f) || Distance<2.f)
        { Travel=0.f; return false; }
        Travel+=Distance;
        if(Travel<(Crouched ? 130.f : 110.f)) return false;
        Travel=0.f; return true;
    }
};
struct Interest
{
    FVector Point{}; float Until=0.f;
    bool Active(float Now) const { return Now<Until; }
    void Clear() { Until=0.f; }
    bool Offer(const FVector &At,float Now)
    {
        if(Active(Now) || !Syndicate::Finite(At)) return false;
        Point=At; Until=Now+8.f; return true; // One bounded event objective; new noises cannot reverse it each frame.
    }
};
inline bool Reached(const FVector &Here, const FVector &Goal, float Radius)
{
    return Syndicate::Distance2D(Here, Goal) <= Radius && std::abs(Here.Z - Goal.Z) <= 140.f;
}
inline bool AmmoEscapeTarget(const FVector &Here,const FVector &Target,const FVector &Threat)
{
    if(!Syndicate::Finite(Target) || !Syndicate::Finite(Threat)) return false;
    float Now=Syndicate::Distance2D(Here,Threat), Next=Syndicate::Distance2D(Target,Threat);
    // Nearby ammo on the current safe side remains usable. Reject a refill behind/near the attacker.
    return Next>=800.f && Next+150.f>=Now;
}
inline bool IncomingShot(const FVector &Here,const FVector &Start,const FVector &Direction)
{
    if(!Syndicate::Finite(Here) || !Syndicate::Finite(Start) || !Syndicate::Finite(Direction)) return false;
    float Norm=std::sqrt(Direction.X*Direction.X+Direction.Y*Direction.Y+Direction.Z*Direction.Z);
    if(Norm<.9f || Norm>1.1f) return false;
    auto D=Here-Start;float Along=(D.X*Direction.X+D.Y*Direction.Y+D.Z*Direction.Z)/Norm;
    float Square=D.X*D.X+D.Y*D.Y+D.Z*D.Z-Along*Along;
    return Along>100.f && Along<6000.f && Square>=-1.f && Square<180.f*180.f;
}
inline bool AimAligned(const FRotator &Desired, const FRotator &Control, const FRotator &Body)
{
    return std::abs(std::remainder(Desired.Yaw - Control.Yaw, 360.f)) <= 7.f &&
           std::abs(std::remainder(Desired.Pitch - Control.Pitch, 360.f)) <= 7.f &&
           std::abs(std::remainder(Desired.Yaw - Body.Yaw, 360.f)) <= 15.f;
}
inline FVector CombatGoal(const FVector &Here, const FVector &Enemy, float Range, float Side)
{
    auto D = Enemy - Here;
    float Length = std::max(1.f, std::hypot(D.X, D.Y));
    D = FVector(D.X / Length, D.Y / Length, 0);
    float Advance = std::clamp(Length - Range, -350.f, 800.f);
    float Strafe = Length > Range * 1.4f ? 120.f : 300.f;
    return Here + D * Advance + FVector(-D.Y, D.X, 0) * (Side * Strafe);
}
inline bool ShouldRotate(const FVector &Here, const FVector &Center, float Radius,
                         float Now, float ShrinkStarts, bool InStorm)
{
    if (!std::isfinite(Radius) || Radius < 0.f || !Syndicate::Finite(Center)) return false;
    float Travel = Syndicate::Distance2D(Here, Center) - std::max(0.f, Radius - std::min(600.f,Radius*0.25f));
    if (Travel <= 0.f) return false;
    // Start early enough to walk the route, with time for obstacles. Bots near
    // the next ring can loot/fight during the wait instead of all disengaging.
    return InStorm || ShrinkStarts <= Now + Travel / 180.f + 120.f;
}
// A countdown alone is not a reason to leave a fight already inside the next ring.
// Outside it, reserve conservative walking time and a short decision margin.
inline bool CanCommitFight(const FVector &Here,const FVector &Center,float Radius,float Now,float ShrinkAt,bool InStorm)
{
    if(InStorm || !Syndicate::Finite(Here) || !Syndicate::Finite(Center) ||
       !std::isfinite(Radius) || Radius<=0.f || !std::isfinite(Now) || !std::isfinite(ShrinkAt)) return false;
    float SafeRadius=std::max(0.f,Radius-std::min(300.f,Radius*.1f));
    float Travel=std::max(0.f,Syndicate::Distance2D(Here,Center)-SafeRadius);
    return Travel<=0.f || ShrinkAt-Now>Travel/180.f+25.f;
}
// Rotation may start early; only its actual arrival deadline interrupts a
// fight. Reserve time to finish a short exchange plus conservative traversal.
inline bool CombatRotationBudget(const FVector &Here,const FVector &Center,float Radius,
                                 float Now,float ShrinkFinishes,bool InStorm)
{
    if(InStorm || !Syndicate::Finite(Here) || !Syndicate::Finite(Center) ||
       !std::isfinite(Radius) || Radius<=0.f || !std::isfinite(Now) || !std::isfinite(ShrinkFinishes)) return false;
    float SafeRadius=std::max(0.f,Radius-std::min(300.f,Radius*.1f));
    float Travel=std::max(0.f,Syndicate::Distance2D(Here,Center)-SafeRadius);
    return Travel<=0.f || ShrinkFinishes-Now>Travel/300.f+20.f;
}
inline bool SafeDestination(const FVector &Here, const FVector &Goal, const FVector &Center, float Radius, bool Urgent)
{
    if (!Syndicate::Finite(Goal)) return false;
    if (!Syndicate::Finite(Center) || !std::isfinite(Radius) || Radius < 0.f) return true;
    float Current = Syndicate::Distance2D(Here,Center), Target = Syndicate::Distance2D(Goal,Center);
    if (Current <= std::max(0.f,Radius-std::min(600.f,Radius*0.25f))) return Target <= std::max(0.f,Radius-std::min(450.f,Radius*0.2f));
    return Target + 100.f < Current || (!Urgent && Syndicate::Distance2D(Here,Goal) < 1200.f);
}
inline bool AvoidStormReentry(const FVector &Here,const FVector &Goal,const FVector &Center,float Radius)
{
    if(!Syndicate::Finite(Center) || !std::isfinite(Radius) || Radius<=0.f) return true;
    float Current=Syndicate::Distance2D(Here,Center),Target=Syndicate::Distance2D(Goal,Center);
    // The current blue boundary is distinct from the next white circle.
    // An already exposed pawn may contour a cliff; a safe pawn may not enter the storm for loot/strafe.
    return Current>Radius || Target<=Radius-std::min(40.f,Radius*.05f) || Target<=Current;
}
inline bool RotationContour(const FVector &Here,const FVector &Goal,const FVector &Center,float Allowance=600.f)
{
    return Syndicate::Finite(Here) && Syndicate::Finite(Goal) && Syndicate::Finite(Center) &&
        Syndicate::Distance2D(Here,Goal)<=1800.f &&
        Syndicate::Distance2D(Goal,Center)<=Syndicate::Distance2D(Here,Center)+Allowance;
}
inline FVector FloorRoot(const FVector &Center,const FVector &Half,float Yaw)
{
    float R=Yaw*3.14159265f/180.f;
    // Verified 10.40 PBW floor hull: local X +/-256, local Y 0..512.
    // The actor is rooted at an edge; Floor placements supplied to Build are desired cell centers.
    return Center-FVector(-std::sin(R),std::cos(R),0)*Half.Y;
}
inline FVector ZoneWaypoint(const FVector &Here, const FVector &Center, float Radius)
{
    auto D = Center - Here;
    float Distance = std::max(1.f, std::hypot(D.X, D.Y));
    float Travel = std::min(1200.f, std::max(0.f, Distance - std::max(0.f, Radius - std::min(900.f,Radius*0.3f))));
    return Here + FVector(D.X / Distance, D.Y / Distance, 0) * Travel;
}
inline FVector WallFocus(const FVector &Root,float ActorYaw,const FVector &Here,float HalfWidth)
{
    float Radians=ActorYaw*3.14159265f/180.f;
    FVector Tangent(std::cos(Radians),std::sin(Radians),0);
    float Along=(Here.X-Root.X)*Tangent.X+(Here.Y-Root.Y)*Tangent.Y;
    return Root+Tangent*std::clamp(Along,-HalfWidth,HalfWidth);
}
inline bool WindowShot(const FVector &Eyes,const FVector &Target,const FVector &Root,float ActorYaw)
{
    if(!Syndicate::Finite(Eyes) || !Syndicate::Finite(Target) || !Syndicate::Finite(Root) ||
       !std::isfinite(ActorYaw) || std::abs(Target.Z-Eyes.Z)>120.f) return false;
    float R=ActorYaw*3.14159265f/180.f;
    FVector Normal(-std::sin(R),std::cos(R),0),Tangent(std::cos(R),std::sin(R),0);
    auto D=Target-Eyes;
    float Denominator=D.X*Normal.X+D.Y*Normal.Y;
    if(std::abs(Denominator)<1.f) return false;
    float T=((Root.X-Eyes.X)*Normal.X+(Root.Y-Eyes.Y)*Normal.Y)/Denominator;
    if(T<=.02f || T>=.98f) return false;
    auto At=Eyes+D*T-Root;
    // Conservative center-window aperture. Native collision checks the actual edited opening before firing.
    return std::abs(At.X*Tangent.X+At.Y*Tangent.Y)<=70.f && At.Z>=125.f && At.Z<=255.f;
}
inline FVector CornerToward(const FVector &Here,const FVector &Enemy,float Side)
{
    auto D=Enemy-Here; float L=std::max(1.f,std::hypot(D.X,D.Y));
    return Here+FVector(-D.Y/L*Side,D.X/L*Side,0)*512.f+FVector(0,0,384.f);
}
inline bool RearThreat(const FVector &Here,const FVector &Observed,const FVector &Climb)
{
    if(!Syndicate::Finite(Here) || !Syndicate::Finite(Observed) || !Syndicate::Finite(Climb)) return false;
    auto D=Observed-Here;float A=std::hypot(D.X,D.Y),B=std::hypot(Climb.X,Climb.Y);
    return A>120.f && B>100.f && (D.X*Climb.X+D.Y*Climb.Y)/(A*B)<-.35f;
}
inline FVector BuildContinuation(const FVector &Here,const FVector &Observed,const FVector &Climb,float Side,bool Rush)
{
    auto D=Observed-Here;float L=std::hypot(D.X,D.Y);
    if((Rush || RearThreat(Here,Observed,Climb)) && L>120.f)
        return Here+FVector(D.X/L,D.Y/L,0)*512.f+FVector(0,0,384.f);
    return CornerToward(Here,Here+Climb,Side);
}
struct RampPlan
{
    FVector Root, Entry, Top;
    float ActorYaw = 0.f;
};
// Same-cell stair recipe for combat 90s. The grid is authoritative; each
// quarter turn happens after a paid upper platform and real pawn travel.
inline RampPlan CombatRamp(const FVector &Center,const FVector &Half,float Height,
                           float Capsule,const FVector &Desired)
{
    float Facing=std::round(std::atan2(Desired.Y,Desired.X)*180.f/3.14159265f/90.f)*90.f;
    FVector Direction(std::cos(Facing*3.14159265f/180.f),std::sin(Facing*3.14159265f/180.f),0);
    float Along=std::abs(Direction.X)>.5f ? Half.X : Half.Y;
    float LowInset=std::min(70.f,Along*.4f),TopInset=std::min(80.f,Along*.35f);
    RampPlan P;P.ActorYaw=std::remainder(Facing+90.f,360.f);P.Root=Center+Direction*Along;
    P.Entry=Center-Direction*(Along-LowInset)+FVector(0,0,Capsule+Height*LowInset/(2.f*Along)+19.f);
    P.Top=P.Root-Direction*TopInset+FVector(0,0,Capsule+Height-Height*TopInset/(2.f*Along)+19.f);
    return P;
}
inline RampPlan ForwardRamp(const FVector &CellCenter, const FVector &Half, float Height,
                            float CapsuleHalf, const FVector &Here, const FVector &Toward, bool NearCell=false)
{
    auto Delta = Toward - Here;
    float Facing = std::round(std::atan2(Delta.Y,Delta.X)*180.f/3.14159265f/90.f)*90.f;
    FVector Direction(std::cos(Facing*3.14159265f/180.f),std::sin(Facing*3.14159265f/180.f),0);
    float HalfLength = std::abs(Direction.X) > 0.5f ? Half.X : Half.Y;
    RampPlan Plan;
    // 10.40 StairW collision hull: width on local X, low end at local Y=512,
    // high end at local Y=0. Its actor origin is the HIGH edge, not the center.
    // Put the low edge at the current cell's forward boundary, in the next cell.
    Plan.ActorYaw = std::remainder(Facing+90.f,360.f);
    Plan.Root = CellCenter + Direction*(HalfLength*(NearCell ? 1.f : 3.f));
    // A stale/outside cell must never put the low edge behind its builder.
    auto Low = Plan.Root-Direction*(HalfLength*2.f);
    // A supported crest is just inside the previous high edge. Its connected
    // successor starts 16 units ahead; applying the ground-start 32-unit
    // margin here moved that successor an entire tile away, leaving a gap.
    float Minimum=NearCell ? 0.f : 32.f;
    if ((Low.X-Here.X)*Direction.X+(Low.Y-Here.Y)*Direction.Y < Minimum)
        Plan.Root = Plan.Root+Direction*(HalfLength*2.f);
    float Inset = std::min(70.f,HalfLength*0.4f);
    float Rise = Height*Inset/std::max(1.f,HalfLength*2.f);
    Plan.Entry = Plan.Root-Direction*(HalfLength*2.f-Inset)+FVector(0,0,CapsuleHalf+Rise+19.f);
    float TopInset=std::min(16.f,HalfLength*0.06f);
    float TopRise=Height*TopInset/std::max(1.f,HalfLength*2.f);
    Plan.Top = Plan.Root-Direction*TopInset+FVector(0,0,CapsuleHalf+Height-TopRise+19.f); // Feet cross into the upper grid level before another stair.
    return Plan;
}
inline bool SupportedRampProgress(const FVector &Here,const FVector &Entry,const FVector &Top,
                                  float Feet,const FVector &Support)
{
    if(!Syndicate::Finite(Here) || !Syndicate::Finite(Entry) || !Syndicate::Finite(Top) ||
       !Syndicate::Finite(Support) || !std::isfinite(Feet)) return false;
    auto D=Top-Entry, At=Here-Entry;float L=std::hypot(D.X,D.Y);
    if(L<100.f) return false;
    float Along=(At.X*D.X+At.Y*D.Y)/L,Across=std::abs(At.X*D.Y-At.Y*D.X)/L;
    return Along>60.f && Along<L+45.f && Across<190.f && Here.Z>Entry.Z+35.f &&
           Feet-Support.Z>=-12.f && Feet-Support.Z<65.f;
}
// A climber that has passed its inset crest must brake/recenter, not
// reverse down the same ramp and then choose to climb it again.
inline FVector RampDriveDelta(const FVector &Here,const FVector &Entry,const FVector &Top)
{
    auto Axis=Top-Entry;float Length=std::hypot(Axis.X,Axis.Y);
    if(!Syndicate::Finite(Here)||!Syndicate::Finite(Entry)||!Syndicate::Finite(Top)||Length<100.f)return {};
    FVector Forward(Axis.X/Length,Axis.Y/Length,0),Side(-Axis.Y/Length,Axis.X/Length,0);
    auto To=Top-Here;float Ahead=To.X*Forward.X+To.Y*Forward.Y,Across=To.X*Side.X+To.Y*Side.Y;
    if(Ahead<=0.f&&std::abs(Across)<25.f)return {};
    return Forward*std::max(0.f,Ahead)+Side*Across;
}
inline FVector HarvestPoint(const FVector &Here, const FVector &Origin, const FVector &Extent)
{
    return FVector(std::clamp(Here.X, Origin.X - Extent.X, Origin.X + Extent.X),
                   std::clamp(Here.Y, Origin.Y - Extent.Y, Origin.Y + Extent.Y),
                   std::clamp(Here.Z + 60.f, Origin.Z - Extent.Z, Origin.Z + Extent.Z));
}
} // namespace BotTactics
