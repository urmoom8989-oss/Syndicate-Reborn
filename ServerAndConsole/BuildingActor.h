#pragma once
#include "FortInventory.h"
namespace BuildingActor
{
inline void (*DamageObserver)(ABuildingSMActor *,AController *,float,const FVector &) = nullptr;
inline void (*OnDamageServerOG)(ABuildingActor *, float, const FGameplayTagContainer &, const FVector &,
                                const FHitResult &, AController *, AActor *,
                                const FGameplayEffectContextHandle &) = nullptr;
inline void OnDamageServer(ABuildingActor *A, float Damage, const FGameplayTagContainer &Tags, const FVector &Momentum,
                           const FHitResult &Hit, AController *Instigator, AActor *Causer,
                           const FGameplayEffectContextHandle &Context)
{
    auto B = Cast<ABuildingSMActor>(A);
    if (DamageObserver && B && std::isfinite(Damage) && Damage>0.f)
        DamageObserver(B,Instigator,Damage,Momentum);
    auto W = Cast<AFortWeapon>(Causer);
    auto Pawn = Instigator ? Cast<AFortPlayerPawnAthena>(Instigator->Pawn) : nullptr;
    bool Harvest = B && B->bAllowResourceDrop && !B->bPlayerPlaced && Pawn && W && W->WeaponData &&
                   W->WeaponData->IsA(UFortWeaponMeleeItemDefinition::StaticClass()) &&
                   FortInventory::Get(Instigator) && std::isfinite(Damage) && Damage > 0;
    static thread_local std::vector<ABuildingSMActor*> InFlight;
    bool Nested=std::find(InFlight.begin(),InFlight.end(),B)!=InFlight.end();
    if(Nested)Harvest=false;
    struct FlightGuard {std::vector<ABuildingSMActor*> &List;bool Active;FlightGuard(std::vector<ABuildingSMActor*> &L,ABuildingSMActor *B,bool A):List(L),Active(A){if(A)List.push_back(B);}~FlightGuard(){if(Active)List.pop_back();}} InFlightGuard(InFlight,B,Harvest);
    auto Resource = Harvest ? UFortKismetLibrary::K2_GetResourceItemDefinition(B->ResourceType) : nullptr;
    int Before = Resource ? FortInventory::CountDefinition(Instigator, Resource) : 0;
    // OnDamageServer can arrive after native health/destruction has changed.
    // Use the damage event, not post-hit remaining health; one-HP/final hits
    // still earn a resource, while the native award is counted below.
    bool Terminal=Harvest&&(B->bDestroyed||B->GetHealth()<=0.f);
    struct Finished {Syndicate::Ref<ABuildingSMActor> Actor;};
    static std::vector<Finished> FinishedHarvest;
    std::erase_if(FinishedHarvest,[](const auto &R){return !R.Actor.Get();});
    if(Terminal){
        for(auto &R:FinishedHarvest)if(R.Actor.Get()==B)Harvest=false;
        if(Harvest){if(FinishedHarvest.size()>=1024)FinishedHarvest.erase(FinishedHarvest.begin());FinishedHarvest.push_back({Syndicate::Ref<ABuildingSMActor>(B)});}
    }
    int Award = Harvest ? std::clamp(static_cast<int>(std::ceil(std::min(Damage,400.f)/8.f)),1,50) : 0;
    Syndicate::Ref<AController> ControllerGuard(Instigator);
    Syndicate::Ref<ABuildingSMActor> BuildingGuard(B);
    if (OnDamageServerOG)
        OnDamageServerOG(A, Damage, Tags, Momentum, Hit, Instigator, Causer, Context);
    if(Harvest)if(auto Surviving=BuildingGuard.Get();Surviving&&(Surviving->bDestroyed||Surviving->GetHealth()<=0.f)){
        bool Seen=false;for(auto &R:FinishedHarvest)Seen|=R.Actor.Get()==Surviving;
        if(!Seen){if(FinishedHarvest.size()>=1024)FinishedHarvest.erase(FinishedHarvest.begin());FinishedHarvest.push_back({Syndicate::Ref<ABuildingSMActor>(Surviving)});}
    }
    Instigator = ControllerGuard.Get();
    if (!Instigator || !Harvest || !Resource || Award <= 0)
        return;
    FortInventory::ClampResource(Instigator,Resource);
    int NativeAdded = std::max(0, FortInventory::CountDefinition(Instigator, Resource) - std::min(Before,999));
    int Added = FortInventory::Add(Instigator, Resource, NativeAdded>0?0:Award, 0, true);
    B = BuildingGuard.Get();
    if (auto PC = Cast<AFortPlayerControllerAthena>(Instigator); PC && B)
        PC->ClientReportDamagedResourceBuilding(B, B->ResourceType, NativeAdded+Added, B->bDestroyed, Damage == 100.f);
}
inline void HookAll()
{
    InstallHook(reinterpret_cast<void *>(ImageBase + 0x1CC36A0), OnDamageServer,
                reinterpret_cast<void **>(&OnDamageServerOG));
}
} // namespace BuildingActor
