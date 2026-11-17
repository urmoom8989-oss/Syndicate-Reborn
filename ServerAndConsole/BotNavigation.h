#pragma once
#include <algorithm>
#include <cmath>
namespace BotNavigation
{
// Request acceptance and physical progress are separate. Reissuing a stalled
// native path cannot reset the deadline, even when its projected endpoint moves.
struct Progress
{
    bool Seeded=false;
    float X=0,Y=0,Z=0,At=0,RequestedAt=-100.f,RecoveryAt=0,MissionAt=0;
    unsigned int Failures=0;
    template<class Point> void Observe(const Point &Here,float Now)
    {
        if(!std::isfinite(Now) || !std::isfinite(Here.X) || !std::isfinite(Here.Y) || !std::isfinite(Here.Z)) return;
        if(!Seeded || Now<At || std::hypot(Here.X-X,Here.Y-Y)>30.f || std::abs(Here.Z-Z)>35.f)
        {X=Here.X;Y=Here.Y;Z=Here.Z;At=Now;Seeded=true;}
    }
    void BeginMission(float Now,bool IdleWarmup=false)
    {
        // A different request is not a different physical attempt. Only
        // observed travel permits another startup allowance.
        if(IdleWarmup || !Seeded || MissionAt<=At || Now<MissionAt) MissionAt=Now;
    }
    bool Stalled(float Now,float Seconds=2.5f) const {return Seeded && Now-std::max(At,MissionAt)>Seconds;}
    void Requested(float Now) {RequestedAt=Now;}
    void Failed(float Now) {++Failures;RecoveryAt=Now;}
};
} // namespace BotNavigation
