#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include "BotSkillPolicy.h"

// Ground intent is independent of engine actors. Only observed facts enter this
// state; the adapter owns native movement, interaction, weapon and build calls.
namespace BotBrain
{
enum class Goal : unsigned char { Arm, Resupply, Fight, Heal, Gather, Loot, Investigate, Rotate };
struct Facts
{
    float Now=0,Health=100,Shield=0,EnemyDistance=0;
    bool Armed=false,CanFire=false,NeedsAmmo=false,Enemy=false,Incoming=false;
    bool HealOwned=false,HealAvailable=false,HealActive=false,Rotate=false,Urgent=false;
    bool LootCommitted=false,Interest=false,ChestLoot=false,CombatCommitted=false,AmmoForGather=false;
    int Materials=0,MaterialTarget=BotSkillPolicy::MaterialFloor;float HealThreshold=70.f;
};
// A mission has a retained objective; optional preparation cannot steal a
// useful chest/pickup route every frame. Combat and survival are interrupts.
inline Goal Choose(const Facts &F)
{
    if(F.Urgent) return Goal::Rotate;
    if(F.HealActive && !F.Incoming) return Goal::Heal;
    if(F.Health<25.f && F.HealOwned && !F.Incoming) return Goal::Heal;
    if(!F.Armed) return Goal::Arm;
    if(!F.CanFire) return Goal::Resupply;
    if(F.Enemy && (F.Incoming || F.CombatCommitted || F.Materials>=40 || F.EnemyDistance<=1800.f)) return Goal::Fight;
    if(F.Interest) return Goal::Investigate;
    if(F.HealOwned && F.Health<F.HealThreshold) return Goal::Heal;
    if(F.Health<50.f && F.HealAvailable) return Goal::Loot;
    if(F.LootCommitted && !F.Rotate) return Goal::Loot; // Finish a real reserved chest/pickup before optional farming.
    if(F.Materials<std::min(120,F.MaterialTarget) && (!F.NeedsAmmo||F.AmmoForGather) && !F.ChestLoot && !F.Rotate) return Goal::Gather;
    if(F.Rotate) return Goal::Rotate;
    // Local sound/build observations interrupt routine preparation. An armed
    // bot first secures enough stock for cover plus two protected stories.
    if(F.ChestLoot) return Goal::Loot; // Finish the real pickup flight beside an opened chest.
    if(F.Materials<std::min(120,F.MaterialTarget) && !F.NeedsAmmo) return Goal::Gather;
    if(F.HealOwned && (F.Health<100.f || F.Shield<100.f)) return Goal::Heal;
    if(F.LootCommitted) return Goal::Loot;
    if(F.Materials<F.MaterialTarget && !F.NeedsAmmo) return Goal::Gather;
    return Goal::Loot;
}
struct State
{
    Goal Intent=Goal::Arm;
    bool Seeded=false;
    float Since=0,NextGather=0,GatherEnds=0,LastProgress=0;
    float X=0,Y=0,Z=0;
    unsigned int Transitions=0,Recoveries=0,NativeSerial=0;
    template<class Point> bool Update(const Facts &F,const Point &Here,unsigned int Serial)
    {
        if(!std::isfinite(F.Now)) return false;
        bool Progress=!Seeded || Serial!=NativeSerial || std::hypot(Here.X-X,Here.Y-Y)>60.f || std::abs(Here.Z-Z)>50.f;
        if(Progress) {LastProgress=F.Now;X=Here.X;Y=Here.Y;Z=Here.Z;}
        auto Next=Choose(F);
        if(Next==Goal::Gather && F.Now<NextGather) Next=Goal::Loot;
        if(Seeded && Intent==Goal::Rotate && F.Rotate && Next==Goal::Gather) Next=Goal::Rotate;
        bool Routine=Next==Goal::Loot || Next==Goal::Gather || Next==Goal::Investigate;
        // First combat stock gets a longer physical approach/harvest window.
        // Later preparation remains a short alternating gather/loot burst.
        if(Seeded && Intent==Goal::Gather && Routine && F.Now<GatherEnds &&
            F.Materials<BotSkillPolicy::MaterialGoal && !F.ChestLoot && (!F.NeedsAmmo||F.AmmoForGather) && !F.Interest && !F.LootCommitted) Next=Goal::Gather;
        if(Seeded && Intent==Goal::Gather && Next==Goal::Gather && F.Now>=GatherEnds)
        {NextGather=F.Now+(F.Materials<std::min(120,F.MaterialTarget) ? 1.5f : 10.f);Next=Goal::Loot;}
        // Finish a retained routine route while it makes observable progress.
        // A mere accepted path/build request does not renew this commitment.
        if(Seeded && Intent==Goal::Loot && Next==Goal::Gather && F.Now-Since<4.f &&
            F.Now-LastProgress<2.f && F.Materials>=std::min(120,F.MaterialTarget)) Next=Goal::Loot;
        bool Changed=!Seeded || Next!=Intent;
        if(Changed)
        {
            Intent=Next;Since=F.Now;
            if(!Seeded) LastProgress=F.Now;
            if(Next==Goal::Gather) GatherEnds=F.Now+(F.Materials<std::min(120,F.MaterialTarget) ? 12.f : 6.f);
            ++Transitions;
        }
        NativeSerial=Serial;Seeded=true;return Changed;
    }
    bool Stalled(float Now,float Allowance,bool BoundedNativeWait) const
    {return Seeded && !BoundedNativeWait && std::isfinite(Now) && Now-LastProgress>Allowance;}
};
enum class BuildStep : unsigned char { Idle, Platform, Center, Floor, SideWall, FrontWall, Stair, Enter, Climb };
// A single owner advances only after a paid native commit or observed travel.
struct BuildSequence
{
    BuildStep Step=BuildStep::Idle;
    bool Waiting=false,Corner=false,JumpIssued=false;
    std::uintptr_t Stamp=0;
    float Started=0,LastProgress=0,RetryAt=0,CornerAt=0;
    unsigned int Completed=0,Failures=0,Stories=0;
    void Begin(float Now)
    {Step=BuildStep::Platform;Waiting=Corner=JumpIssued=false;Started=LastProgress=Now;Stories=0;}
    void Await(std::uintptr_t Previous,float Now)
    {Waiting=true;Stamp=Previous;LastProgress=Now;}
    bool Resolve(bool Pending,std::uintptr_t Actual,float Now)
    {
        if(!Waiting) return true;
        // A newer committed actor wins even when another callback changed the
        // queue; pending acceptance never proves structural support.
        if(Actual && Actual!=Stamp)
        {Waiting=false;LastProgress=Now;++Completed;return true;}
        if(Pending && Now-LastProgress<.65f) return false;
        Waiting=false;Fail(Now);return false;
    }
    void NextStory(float Now)
    {Step=BuildStep::Floor;Corner=true;JumpIssued=false;CornerAt=LastProgress=Now;++Stories;}
    void Fail(float Now)
    {Step=BuildStep::Idle;Waiting=Corner=JumpIssued=false;RetryAt=Now+.2f;++Failures;}
    void Clear() {Step=BuildStep::Idle;Waiting=Corner=JumpIssued=false;}
    bool Active() const {return Step!=BuildStep::Idle;}
};
} // namespace BotBrain
