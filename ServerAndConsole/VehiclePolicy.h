#pragma once
#include <cmath>
#include <algorithm>

namespace VehiclePolicy
{
template<class V> bool Finite(const V &P) { return std::isfinite(P.X)&&std::isfinite(P.Y)&&std::isfinite(P.Z); }
template<class V> float Distance(const V &A,const V &B)
{return std::hypot(std::hypot(A.X-B.X,A.Y-B.Y),A.Z-B.Z);}
template<class V> bool NativeMoveApplied(const V &Before,const V &Current,const V &Target)
{
    if(!Finite(Before)||!Finite(Current)||!Finite(Target)) return false;
    float Error=Distance(Current,Target),Previous=Distance(Before,Target);
    // Gravity or a small unrelated physics tick at the entry location is
    // not evidence that the driver's movement message was applied.
    return Error<=5.f || (Previous>40.f && Error<=Previous*.25f);
}
template<class V> bool StaleExit(const V &Pawn,const V &Socket,const V &Vehicle,const V &Velocity,float Age)
{
    if(!Finite(Pawn)||!Finite(Socket)||!Finite(Vehicle)||!Finite(Velocity)||
       !std::isfinite(Age)||Age<0.f||Age>.5f||Distance(Socket,Vehicle)>2000.f) return false;
    float Speed=std::hypot(std::hypot(Velocity.X,Velocity.Y),Velocity.Z);
    return Distance(Pawn,Socket)>1200.f+std::min(Speed,60000.f)*Age*1.5f;
}
template<class V,class Q> bool ValidMove(const V &At,const V &Previous,const V &Velocity,const V &Angular,const Q &Rotation,float Age)
{
    if (!Finite(At)||!Finite(Previous)||!Finite(Velocity)||!Finite(Angular)||!std::isfinite(Age)||Age<0.f ||
        !std::isfinite(Rotation.X)||!std::isfinite(Rotation.Y)||!std::isfinite(Rotation.Z)||!std::isfinite(Rotation.W)) return false;
    float Norm=Rotation.X*Rotation.X+Rotation.Y*Rotation.Y+Rotation.Z*Rotation.Z+Rotation.W*Rotation.W;
    if (Norm<.9f || Norm>1.1f || std::abs(At.X)>1000000.f || std::abs(At.Y)>1000000.f || std::abs(At.Z)>500000.f) return false;
    auto Size=[](const V &P){return std::sqrt(P.X*P.X+P.Y*P.Y+P.Z*P.Z);};
    if(Size(Velocity)>60000.f || Size(Angular)>36000.f) return false;
    float Distance=std::sqrt((At.X-Previous.X)*(At.X-Previous.X)+(At.Y-Previous.Y)*(At.Y-Previous.Y)+(At.Z-Previous.Z)*(At.Z-Previous.Z));
    return Distance<=2000.f+60000.f*std::clamp(Age,0.f,2.f);
}
inline bool InventoryAllowed(bool InVehicle,bool Mounted,bool NativeCanShoot,bool Driver,bool DriverCanUseInventory)
{ return !InVehicle || (!Mounted && NativeCanShoot && (!Driver || DriverCanUseInventory)); }
}
