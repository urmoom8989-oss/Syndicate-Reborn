#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include "BotSkillPolicy.h"

// Decisions derived from the supplied match, not prerecorded movement or an
// imitation model. The adapter must still prove visibility, ammo and support.
namespace BotPlayerStyle
{
enum class Action : unsigned char { Search, Approach, Climb, Peek, Reload, EscapeAmmo };
struct CombatFacts
{
    float Now=0,Distance=0,HeightAdvantage=0,Story=384,SeenAge=100;
    bool Visible=false,CanFight=false,Loaded=false,Reloading=false,Climbing=false;
    int Materials=0;
};
struct CombatPlan
{
    Action Intent=Action::Search;
    std::uintptr_t Target=0;
    float Since=0,PeekUntil=0,RetryAt=0,LastCrestHeight=0,FollowupUntil=0;
    unsigned int Decisions=0,Crests=0,Failures=0;
    void Reset() {*this={};}
    void ShotgunShot(float Now) {FollowupUntil=Now+.55f;}
    bool Followup(float Now,float Distance,int Role,int Loaded) const
    {return Now<FollowupUntil && Distance<1500.f && Role==1 && Loaded>0;}
    void Crest(float Now,float Height)
    {++Crests;LastCrestHeight=Height;PeekUntil=Now+1.1f;}
    void Failed(float Now)
    {++Failures;RetryAt=Now+.45f;PeekUntil=Now+.45f;}
    Action Choose(const CombatFacts &F) const
    {
        if(!F.CanFight) return Action::EscapeAmmo;
        if(F.Climbing && F.SeenAge<10.f) return Action::Climb;
        if(!F.Visible && !(F.SeenAge<6.f && F.Distance<=5000.f &&
            F.HeightAdvantage<F.Story*1.8f && F.Materials>=20 && F.Now>=RetryAt)) return Action::Search;
        if(F.Distance>5000.f) return F.Loaded ? Action::Approach : Action::Reload;
        // A supported crest offers a short shot before the next retake. A
        // rising opponent immediately invalidates that advantage.
        bool Retake=BotSkillPolicy::BuildPressure(F.Distance,F.HeightAdvantage,F.Story);
        bool UsefulPeek=F.Now<PeekUntil && F.HeightAdvantage>=F.Story*1.8f;
        if(Retake && !UsefulPeek && F.Materials>=20 && F.Now>=RetryAt) return Action::Climb;
        if(!F.Loaded || F.Reloading) return Action::Reload;
        if(F.Distance>2200.f && F.HeightAdvantage<F.Story*.7f) return Action::Approach;
        return Action::Peek;
    }
    Action Update(std::uintptr_t Identity,const CombatFacts &F)
    {
        if(Target!=Identity) {Reset();Target=Identity;Since=F.Now;}
        auto Next=Choose(F);
        if(Next!=Intent) {Intent=Next;Since=F.Now;++Decisions;}
        return Intent;
    }
};
struct Interaction
{
    std::uintptr_t Target=0;
    float Since=0;
    void Clear(){Target=0;Since=0;}
    bool Ready(std::uintptr_t Identity,float Now,bool Reachable,float Hold)
    {
        if(!Reachable || !Identity) {Clear();return false;}
        if(Target!=Identity) {Target=Identity;Since=Now;}
        return Now-Since>=Hold;
    }
};
// A pickup is a native flight followed by inventory completion. Waiting for
// that callback cannot become a new exploration or harvest task.
inline bool PickupWait(float Now,float Started,bool ActorStillClaimed)
{return ActorStillClaimed && Now-Started>=0.f && Now-Started<1.5f;}
inline bool KeepOpponent(bool Alive,bool Visible,float SeenAge,float Distance)
{return Alive && Distance<14000.f && (Visible || SeenAge<10.f);}
}
