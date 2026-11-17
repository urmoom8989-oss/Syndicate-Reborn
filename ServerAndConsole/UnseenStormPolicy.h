#pragma once
#include <algorithm>
#include <cmath>

// Keep a wide margin beyond the actual camera view; spectator identities are
// vetoed separately even when a camera transition is still in progress.
namespace UnseenStormPolicy
{
inline constexpr float HumanDistance = 20000.f; // 200 metres, at BOTH ends.
inline constexpr float BotDistance = 6000.f;    // 60 metres between live bots.
inline constexpr float Cooldown = 20.f;
inline constexpr float HiddenHold = .2f;
inline constexpr int Attempts = 8;
struct State
{
    float DamageAt = -100.f, NextAttempt = 0.f, HiddenSince = -1.f, NextObservation = 0.f;
    bool Pending = false;
    unsigned int Moves = 0;
    void Damage(float Now, float Amount, bool StormCause)
    {
        if (!StormCause || !std::isfinite(Now) || !std::isfinite(Amount) || Amount <= 0.f) return;
        Pending = true; DamageAt = Now;
    }
    bool Eligible(float Now, bool Alive, bool Grounded, bool InStorm, bool MatchActive)
    {
        if (!Alive || !Grounded || !InStorm || !MatchActive || !std::isfinite(Now))
        { Pending = false; HiddenSince = -1.f; return false; }
        if (!Pending || Now < NextAttempt || Now - DamageAt > 3.f) return false;
        return true;
    }
    bool ObserveHidden(float Now, bool Hidden)
    {
        if (!Hidden) { HiddenSince = -1.f; return false; }
        if (HiddenSince < 0.f || Now < HiddenSince) HiddenSince = Now;
        return Now - HiddenSince >= HiddenHold;
    }
    // The adapter rechecks source, destination, observers, collision, circle and
    // spacing at commit. A failed native teleport never consumes the request.
    template<class Point, class Sample, class Validate, class Commit>
    bool Try(float Now, Sample SamplePoint, Validate StillSafe, Commit Move)
    {
        NextAttempt = Now + .5f;
        for (int I = 0; I < Attempts; ++I)
        {
            Point Destination{};
            if (!SamplePoint(Destination)) continue;
            if (!StillSafe(Destination)) continue;
            if (!Move(Destination)) continue;
            Pending = false; HiddenSince = -1.f; NextAttempt = Now + Cooldown; ++Moves;
            return true;
        }
        return false;
    }
};
template<class Point> bool Inside(const Point &P, const Point &Center, float Radius, float Inset)
{
    return std::isfinite(P.X) && std::isfinite(P.Y) && std::isfinite(P.Z) &&
        std::isfinite(Center.X) && std::isfinite(Center.Y) && std::isfinite(Radius) &&
        std::isfinite(Inset) && Radius > Inset && Inset >= 0.f &&
        std::hypot(P.X-Center.X,P.Y-Center.Y) < Radius-Inset;
}
template<class Point> bool Separated(const Point &A, const Point &B, float Distance)
{
    // XY is intentional: a person directly above/below a bot must block it too.
    return std::isfinite(A.X) && std::isfinite(A.Y) && std::isfinite(B.X) && std::isfinite(B.Y) &&
        std::isfinite(Distance) && Distance > 0.f && std::hypot(A.X-B.X,A.Y-B.Y) > Distance;
}
template<class Point> bool OutsideView(const Point &At,const Point &Eye,const Point &Forward)
{
    if (!std::isfinite(Forward.X) || !std::isfinite(Forward.Y) || !std::isfinite(Forward.Z)) return false;
    float X=At.X-Eye.X,Y=At.Y-Eye.Y,Z=At.Z-Eye.Z;
    float Distance=std::sqrt(X*X+Y*Y+Z*Z);
    float Length=std::sqrt(Forward.X*Forward.X+Forward.Y*Forward.Y+Forward.Z*Forward.Z);
    // A deliberately wide 220-degree cone. Only points safely behind the
    // camera qualify without physical occlusion; spectators still veto above.
    return std::isfinite(Distance) && std::isfinite(Length) && Distance>0.f && Length>.5f &&
        (X*Forward.X+Y*Forward.Y+Z*Forward.Z)<-.34202015f*Distance*Length;
}
}
