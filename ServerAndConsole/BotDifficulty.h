#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <string_view>
#include <unordered_map>
namespace BotDifficulty
{
enum Key { AimAccuracy,AimTracking,ReactionSpeed,FiringFrequency,Aggression,Pursuit,BuildFrequency,BuildSpeed,HeightSeeking,DefensiveBuilding,EditFrequency,EditSpeed,Strafing,Jumping,Awareness,Prediction,Looting,Harvesting,Healing,Rotation,Count };
using Values=std::array<int,Count>;
inline Values Uniform(int V){Values R{};R.fill(std::clamp(V,0,100));return R;}
inline Values Config=Uniform(100);
inline bool Mixed=false;
inline std::unordered_map<const void*,Values> Bots;
inline unsigned Serial=0;
inline bool Parse(std::wstring_view Token,Values &Out,bool &Mix)
{
    if(Token==L"mixed"){Out=Uniform(50);Mix=true;return true;}
    Values Result{};size_t At=0;
    for(int I=0;I<Count;++I){
        auto End=Token.find(L',',At);auto Part=Token.substr(At,End==Token.npos?End:End-At);
        if(Part.empty()||Part.size()>3)return false;
        int V=0;for(auto C:Part){if(C<L'0'||C>L'9')return false;V=V*10+int(C-L'0');}
        if(V>100 || (I<Count-1)==(End==Token.npos))return false;
        Result[I]=V;At=End+1;
    }
    Out=Result;Mix=false;return true;
}
inline bool Configure(std::wstring_view Cmd)
{
    constexpr std::wstring_view Key=L"-SyndicateDifficulty=";size_t At=Cmd.find(Key);
    if(At==Cmd.npos){Config=Uniform(100);Mixed=false;return true;}
    if(At&&Cmd[At-1]!=L' '&&Cmd[At-1]!=L'\t')return false;
    At+=Key.size();size_t End=Cmd.find_first_of(L" \t\"",At);
    Values V;bool M=false;if(!Parse(Cmd.substr(At,End==Cmd.npos?End:End-At),V,M))return false;
    Config=V;Mixed=M;return true;
}
inline void Reset(){Bots.clear();Serial=0;}
inline void Assign(const void *PC)
{
    if(!PC)return;
    // Stride-three assignment spreads all five levels through every group of
    // five spawns. A bot retains its profile through possession/phase changes.
    static constexpr int Levels[]={5,25,50,75,100};
    Bots.insert_or_assign(PC,Mixed?Uniform(Levels[((Serial++)*3)%5]):Config);
}
inline const Values &Profile(const void *PC){auto I=Bots.find(PC);static const auto Default=Uniform(100);return I==Bots.end()?Default:I->second;}
inline float Pct(const void *PC,Key K){return Profile(PC)[K]*.01f;}
inline float Scale(const void *PC,Key K,float Low,float High=1.f){return Low+(High-Low)*Pct(PC,K);}
inline float Delay(const void *PC,Key K,float Seconds){return Seconds*Scale(PC,K,5.f);}
inline float BuildPause(const void *PC,float Seconds){return Seconds+(1.f-Pct(PC,BuildSpeed))*.24f;}
inline bool Allow(const void *PC,Key K,float Now)
{
    float P=Pct(PC,K);if(P>=1.f)return true;if(P<=0.f||!std::isfinite(Now))return false;
    auto Slot=static_cast<std::uint64_t>(std::max(0.f,Now)/2.f);
    auto Seed=static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(PC)>>4);
    auto H=(Slot*2654435761ull+Seed*2246822519ull+K*3266489917ull)%100;
    return H<static_cast<unsigned>(P*100.f);
}
inline bool Fire(const void *PC,float Now)
{
    if(!std::isfinite(Now))return false;
    float Duty=Scale(PC,FiringFrequency,.2f);return Duty>=1.f||std::fmod(std::max(0.f,Now),1.5f)<1.5f*Duty;
}
inline int MaterialTarget(const void *PC,int Base){return static_cast<int>(std::lround(Base*Scale(PC,Harvesting,.25f)));}
inline float HealThreshold(const void *PC){return Scale(PC,Healing,30.f,70.f);}
inline float RotationLead(const void *PC){return Bots.contains(PC)?Pct(PC,Rotation)*45.f:0.f;}
}
