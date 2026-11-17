#pragma once
#include <algorithm>
#include <cmath>
// Policy shared by native adapters and regression scenarios. Never grants
// inventory, movement, damage or structural support.
namespace BotMission
{
inline bool WeaponInRange(int Role,float Distance)
{ return std::isfinite(Distance) && Distance>=0.f &&
    (Role==0 ? Distance<=1600.f : Role==2 ? Distance<=14000.f : Role==1 && Distance<=12000.f); }
inline float WeaponScore(int Role,float Distance,int Loaded,int Reserve,int Rarity,bool Followup)
{
    if(Role<0 || Role>2 || !std::isfinite(Distance)) return -10000.f;
    bool Usable=Loaded>0 || Reserve>0;
    float Range=Role==0 ? (Distance<=1200.f ? 90.f : Distance<=1600.f ? 30.f : -220.f) :
        Role==2 ? (Distance>=6500.f ? 60.f : Distance>=2500.f ? 5.f : -40.f) : 40.f;
    return (Usable ? 1000.f : 0.f)+Range+std::clamp(Rarity,0,10)*2.f+
        (Followup && Role==1 ? 70.f : 0.f)+(Loaded>0 ? 8.f : 0.f);
}
enum class TerrainStep : unsigned char { None, Contour, Gather, Climb };
struct TerrainMission
{
    TerrainStep Step=TerrainStep::None;
    float X=0,Y=0,Z=0,ProgressAt=0,ProbeAt=0,Started=0;
    int LastMaterials=0,Failures=0;
    bool Active() const {return Step!=TerrainStep::None;}
    template<class Point> void Begin(const Point &Here,float Now,int Materials)
    {Step=TerrainStep::Contour;X=Here.X;Y=Here.Y;Z=Here.Z;Started=ProgressAt=Now;ProbeAt=Now;LastMaterials=Materials;Failures=0;}
    template<class Point> bool Observe(const Point &Here,float Now,int Materials)
    {
        bool Progress=std::hypot(Here.X-X,Here.Y-Y)>100.f || Here.Z>Z+50.f ||
            (Step==TerrainStep::Gather && Materials>LastMaterials);
        if(Progress){X=Here.X;Y=Here.Y;Z=Here.Z;ProgressAt=Now;Failures=0;}
        LastMaterials=Materials;return Progress;
    }
    bool Stalled(float Now,float Limit)const{return Active() && Now-ProgressAt>Limit;}
    void Clear(){Step=TerrainStep::None;}
};
}
