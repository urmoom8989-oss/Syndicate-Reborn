#pragma once
#include <algorithm>
#include <cmath>
#include <array>
#include <cstdint>
namespace CombatSkill
{
inline constexpr bool Enabled=true;
inline constexpr float CrestShootPause=.75f,AimError=14.f;
inline bool CanClimb(float Now,float ShootUntil,float,float,float){return Now>=ShootUntil;}
// A continuous encounter has a fixed lower reference; opponents taking turns
// climbing cannot keep raising each other's height target without limit.
struct HeightBudget
{
    bool Seeded=false;float Base=0.f,LastContact=-100.f;
    void Observe(float Now,float OwnZ)
    {
        if(!std::isfinite(Now)||!std::isfinite(OwnZ))return;
        if(!Seeded||Now-LastContact>12.f){Base=OwnZ;Seeded=true;}
        LastContact=Now;
    }
    float Goal(float EnemyZ,float Story)const
    {return Seeded?std::min(EnemyZ+2.f*Story,Base+4.f*Story):EnemyZ+2.f*Story;}
    bool Allows(float OwnZ,float Story)const
    {return !Seeded||OwnZ<Base+4.f*Story-50.f;}
};
inline bool CloseIntrusion(float Distance,float HeightDifference,bool Visible)
{return Visible&&Distance>=0.f&&Distance<650.f&&std::abs(HeightDifference)<220.f;}
struct Prediction
{
    std::uintptr_t Target=0;float At=-100.f,VX=0.f,VY=0.f,VZ=0.f;
    void Observe(std::uintptr_t ID,float Now,float X,float Y,float Z)
    {
        if(!ID||!std::isfinite(Now)||!std::isfinite(X)||!std::isfinite(Y)||!std::isfinite(Z))return;
        float Speed=std::hypot(X,Y),Scale=Speed>900.f?900.f/Speed:1.f;X*=Scale;Y*=Scale;Z=std::clamp(Z,-500.f,500.f);
        float Blend=ID!=Target||Now-At>.35f?1.f:.4f;
        VX+=(X-VX)*Blend;VY+=(Y-VY)*Blend;VZ+=(Z-VZ)*Blend;At=Now;Target=ID;
    }
    std::array<float,3> Offset(std::uintptr_t ID,float Now,float Distance,bool Shotgun)const
    {
        if(ID!=Target||Now<At||Now-At>.35f||!std::isfinite(Distance))return {};
        float T=Shotgun?.025f:std::clamp(.04f+Distance/150000.f,.04f,.1f);
        return {std::clamp(VX*T,-90.f,90.f),std::clamp(VY*T,-90.f,90.f),std::clamp(VZ*T,-45.f,45.f)};
    }
};
// Only observed hits and last-seen positions justify a box push.
inline bool PressWounded(int Hits,float Damage,float HealthAndShield)
{return Hits>=2 || (std::isfinite(Damage)&&Damage>0.f&&std::isfinite(HealthAndShield)&&HealthAndShield-Damage<=65.f);}
inline float UpgradeChestPriority(float Age,bool NeedUpgrade,bool AmmoBox,bool NeedAmmo)
{return AmmoBox ? (NeedAmmo?2500.f:250.f) : (NeedUpgrade&&Age>=120.f?4200.f:0.f);}
struct TravelHops
{
    float Next=0.f;
    bool Due(float Now,bool SafeMoving,int Roll)
    {
        if(!std::isfinite(Now)||Now<Next)return false;
        Next=Now+6.f+float(std::clamp(Roll,0,99))*.08f;
        return SafeMoving&&Roll>=0&&Roll<20;
    }
};
struct Materials
{
    float Next=0.f;
    void Land(float Now){Next=Now+20.f;}
    int Due(float Now)
    {
        if(Next<=0.f||!std::isfinite(Now)||Now<Next)return 0;
        int Ticks=std::clamp(static_cast<int>((Now-Next)/20.f)+1,1,12);
        Next+=Ticks*20.f;return Ticks*10;
    }
};
enum class TriggerAction { Wait,Press,Release };
struct SingleShot
{
    float Next=0.f;
    TriggerAction Step(float Now,bool Held,float RoundsPerSecond)
    {
        if(!std::isfinite(Now))return TriggerAction::Wait;
        if(Held)return TriggerAction::Release;
        if(Now<Next)return TriggerAction::Wait;
        if(!std::isfinite(RoundsPerSecond)||RoundsPerSecond<=0)RoundsPerSecond=2.f;
        Next=Now+1.f/std::clamp(RoundsPerSecond,.5f,20.f);return TriggerAction::Press;
    }
};
inline bool CanDepartPOI(float Age,bool Gun,bool Magazine,bool Threat,bool Safe,int LocalEnemies,int LocalChests)
{return Age>=60.f&&Gun&&Magazine&&!Threat&&Safe&&LocalEnemies==0&&(LocalChests==0||Age>=120.f);}
struct AmmoIncome
{
    float Next=0.f;
    void Land(float Now){Next=Now+25.f;}
    int Due(float Now)
    {
        if(Next<=0.f||!std::isfinite(Now)||Now<Next)return 0;
        int Ticks=std::clamp(static_cast<int>((Now-Next)/25.f)+1,1,12);Next+=Ticks*25.f;return Ticks;
    }
};
struct Celebration
{
    bool Pending=false,Active=false;
    float Requested=0.f,Ends=0.f,Started=0.f;
    bool Offer(float Now,int Roll,bool HumanVictim)
    {if(!HumanVictim||Roll<0||Roll>=5)return false;Pending=true;Requested=Now;return true;}
    bool Begin(float Now,float Duration,bool Safe)
    {
        if(!Pending)return false;
        if(!Safe||Now-Requested>3.f){Pending=false;return false;}
        Pending=false;Active=true;Started=Now;Ends=Now+std::clamp(Duration,3.f,8.f);return true;
    }
    bool Stop(float Now,bool Threatened)const{return Active&&(Threatened||Now>=Ends);}
    void Clear(){Pending=Active=false;Ends=0.f;}
};
// Five player-sized primary slots: shotgun, rifle, flex, shield and health.
inline int Slot(int Kind,bool Health)
{return Kind==0?0:Kind==1?1:Kind==6?3:Health?4:Kind>=0?2:-1;}
inline float Quality(int Kind,int Rarity,bool PreferSMG,bool Medkit=false)
{
    float Preference=Kind==4?(PreferSMG?55.f:20.f):Kind==2?(PreferSMG?10.f:40.f):Kind==3?-20.f:0.f;
    return std::clamp(Rarity,0,7)*10.f+Preference+(Medkit?45.f:0.f);
}
inline float RangePreference(int Kind,float Distance,bool ShotgunFollowup)
{
    if(Kind==4)return Distance<1600.f?(ShotgunFollowup?95.f:50.f):Distance<3000.f?10.f:-100.f;
    if(Kind==1)return Distance>1800.f?35.f:0.f;
    return 0.f;
}
inline bool BoxHeal(float Health,bool HasHeal,int Mats,bool BoxActive,bool Suppressed)
{return Health<70.f&&HasHeal&&!Suppressed&&(BoxActive||Mats>=60);}
}
