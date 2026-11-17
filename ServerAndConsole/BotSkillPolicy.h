#pragma once
#include <algorithm>
#include <cmath>
namespace BotSkillPolicy
{
inline constexpr int MaterialFloor=300, MaterialGoal=600;
inline int AmmoReserve(int Clip) {return std::clamp(Clip*4,24,240);}
inline bool Rush(float Distance,float Advantage,float Story)
{return Distance>1400.f && Distance<=5000.f && Advantage>=-120.f && Advantage<Story*1.8f;}
inline bool Contest(float Distance,float Advantage,float Story)
{return Distance<=2200.f && Advantage<Story*1.8f;}
inline bool BuildPressure(float Distance,float Advantage,float Story)
{return Rush(Distance,Advantage,Story) || Contest(Distance,Advantage,Story);}
// Approach a crest smoothly, instead of alternating full sprint and braking.
inline float CrestInput(float Remaining,float Speed,float Braking)
{
    float Stop=Speed*Speed/(2.f*std::max(100.f,Braking))+Speed/30.f+12.f;
    return Remaining<=12.f ? 0.f : std::clamp((Remaining-12.f)/std::max(40.f,Stop),.12f,1.f);
}
}
