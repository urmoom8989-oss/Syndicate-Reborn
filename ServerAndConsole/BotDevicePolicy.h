#pragma once
#include <algorithm>
#include <cctype>
#include <cmath>
#include <string>
namespace BotDevices
{
enum class Kind { None, Damage, Launch, Bounce };
inline Kind Classify(std::string Name)
{
    std::transform(Name.begin(),Name.end(),Name.begin(),[](unsigned char C){return std::tolower(C);});
    std::erase(Name,'_');
    if(Name.contains("jumppad")||Name.contains("launchpad"))return Kind::Launch;
    if(Name.contains("bouncer")||Name.contains("bouncepad"))return Kind::Bounce;
    if(Name.contains("spike"))return Kind::Damage;
    return Kind::None;
}
inline bool Useful(Kind K,float Distance,float Height,bool Busy,bool Safe)
{
    if(Busy||!Safe||!std::isfinite(Distance)||!std::isfinite(Height))return false;
    return (K==Kind::Launch&&Distance>=9000.f) ||
        (K==Kind::Bounce&&Distance>=800.f&&Distance<=9000.f&&Height>=200.f);
}
inline bool KeepVictim(float Distance,float Height,float Age)
{return std::isfinite(Distance)&&std::isfinite(Height)&&Distance<=300.f&&std::abs(Height)<=160.f&&Age<4.f;}
}
