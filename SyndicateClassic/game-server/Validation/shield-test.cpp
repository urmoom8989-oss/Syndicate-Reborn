#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstring>
#include <iostream>
#include <limits>
#include <string>
using uint8=unsigned char;using uint32=unsigned int;
namespace Offsets{constexpr uint32 ElementSize=0;}
struct Property{uint32 Size=32;};
struct AttributeType{uint32 GetOffset(const char* n){std::string s=n;return s=="BaseValue"?8:s=="CurrentValue"?12:s=="UnclampedBaseValue"?16:s=="UnclampedCurrentValue"?20:uint32(-1);}} type;
AttributeType* FindStruct(const char*){return &type;}
struct HealthSet{uint8 Data[192]{};Property Prop;uint32 GetOffset(const char* n){std::string s=n;return s=="Health"?48:s=="CurrentShield"?112:s=="Shield"?144:uint32(-1);}Property* GetProperty(const char* n){return GetOffset(n)==uint32(-1)?nullptr:&Prop;}};
template<class T>T& GetFromOffset(void* p,uint32 offset){return *reinterpret_cast<T*>(static_cast<uint8*>(p)+offset);}
struct AFortPlayerPawnAthena{HealthSet* HealthSet;int Replications=0;void ForceNetUpdate(){Replications++;}float GetShield(){return GetFromOffset<float>(HealthSet,156);}};
struct {float FortniteVersion=4.0;}VersionInfo;
void AppendBotLog(const char*){}
    inline void CommitBotDamageAttribute(AFortPlayerPawnAthena* Pawn,const char* Name,float Value)
    {
        auto Set=Pawn->HealthSet;if(!Set || !std::isfinite(Value))return;
        auto Prop=Set->GetProperty(Name);auto Off=Set->GetOffset(Name);
        auto Type=FindStruct("FortGameplayAttributeData");
        if(!Prop || Off==uint32(-1) || !Type){AppendBotLog("damage attribute unavailable\n");return;}
        auto Size=GetFromOffset<uint32>(Prop,Offsets::ElementSize);
        auto Data=(uint8*)Set+Off;
        for(auto Field:{"BaseValue","CurrentValue","UnclampedBaseValue","UnclampedCurrentValue"}){
            auto F=Type->GetOffset(Field);if(F!=uint32(-1) && F+4<=Size)memcpy(Data+F,&Value,4);
        }
        // OnRep is a receiving-client callback. Calling it on the authority
        // reconciles against the server ASC's previous value and can undo damage.
        // Replication invokes the callback on clients with their previous value.
        Pawn->ForceNetUpdate();
    }

    inline float CurrentBotShield(AFortPlayerPawnAthena* Pawn)
    {
        auto Set=Pawn?Pawn->HealthSet:nullptr;auto Type=FindStruct("FortGameplayAttributeData");
        if(Set && Type && VersionInfo.FortniteVersion>=4 && VersionInfo.FortniteVersion<5){
            auto O=Set->GetOffset("CurrentShield"),V=Type->GetOffset("CurrentValue");
            if(O!=uint32(-1) && V!=uint32(-1))return (std::max)(0.f,GetFromOffset<float>((uint8*)Set+O,V));
        }
        return Pawn?Pawn->GetShield():0.f;
    }


int main(){HealthSet set;AFortPlayerPawnAthena pawn{&set};GetFromOffset<float>(&set,156)=100;GetFromOffset<float>(&set,124)=100;
CommitBotDamageAttribute(&pawn,"CurrentShield",77);assert(CurrentBotShield(&pawn)==77);assert(pawn.GetShield()==100);
CommitBotDamageAttribute(&pawn,"CurrentShield",23);assert(CurrentBotShield(&pawn)==23);assert(pawn.GetShield()==100);
CommitBotDamageAttribute(&pawn,"CurrentShield",0);assert(CurrentBotShield(&pawn)==0);assert(pawn.GetShield()==100);
CommitBotDamageAttribute(&pawn,"CurrentShield",std::numeric_limits<float>::quiet_NaN());assert(CurrentBotShield(&pawn)==0);
CommitBotDamageAttribute(&pawn,"missing",99);assert(CurrentBotShield(&pawn)==0);
set.Prop.Size=12;CommitBotDamageAttribute(&pawn,"CurrentShield",9);assert(CurrentBotShield(&pawn)==0);assert(pawn.GetShield()==100);
std::cout<<"6 reflected shield regression cases passed; shield capacity unchanged\n";}
