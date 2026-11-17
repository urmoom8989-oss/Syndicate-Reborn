#pragma once
#include "Looting.h"
#include "FortInventory.h"
#include "BotWork.h"
#include "SDK/SDK/FortniteGame_parameters.hpp"
#include "SDK/SDK/AthenaSupplyDrop_parameters.hpp"
namespace InteractionLoot
{
struct Record { Syndicate::Ref<AActor> Actor; bool Busy=false,Done=false; float RetryAt=0.f; };
inline Syndicate::Ref<UWorld> ActiveWorld;
inline std::vector<Record> Records;
inline Record &Track(AActor *Actor)
{
    auto World=UWorld::GetWorld();
    if(ActiveWorld.Get()!=World) { ActiveWorld.Set(World); Records.clear(); }
    std::erase_if(Records,[](const Record &R){ return !R.Actor.Get(); });
    for(auto &R:Records) if(R.Actor.Get()==Actor) return R;
    Records.push_back({Syndicate::Ref<AActor>(Actor)}); return Records.back();
}
inline bool SpawnBatch(const std::vector<FFortItemEntry> &Drops,const FVector &Location,
                       EFortPickupSpawnSource Source,std::vector<Syndicate::Ref<AFortPickup>> &Spawned)
{
    if(Drops.empty() || Drops.size()>32 || !Syndicate::Finite(Location)) return false;
    for(auto E:Drops)
    {
        if(!E.ItemDefinition || E.Count<=0 || E.Count>9999) return false;
        auto P=SpawnPickup(E.ItemDefinition,E.Count,E.LoadedAmmo,Location,EFortPickupSourceTypeFlag::Container,Source);
        if(!P) return false;
        Spawned.emplace_back(P);
    }
    return true;
}
inline void Rollback(const std::vector<Syndicate::Ref<AFortPickup>> &Spawned)
{
    for(auto Ref:Spawned) if(auto P=Ref.Get()) P->K2_DestroyActor();
}
inline void Tick(UWorld *) {}
inline FVector SupplyPoint(AFortAthenaSupplyDrop *Drop)
{
    auto Point=Drop?Drop->K2_GetActorLocation():FVector{};
    if(auto BP=Syndicate::As<AAthenaSupplyDrop_C>(Drop))
    {
        if(BP->CrateCollision){auto At=BP->CrateCollision->K2_GetComponentLocation();if(Syndicate::Finite(At)&&At.Magnitude()>1.f)Point=At;}
        else if(BP->CrateStaticMesh){auto At=BP->CrateStaticMesh->K2_GetComponentLocation();if(Syndicate::Finite(At)&&At.Magnitude()>1.f)Point=At;}
    }
    return Point;
}
inline bool InSupplyReach(AFortAthenaSupplyDrop *Drop,AFortPawn *Pawn)
{return Drop&&Pawn&&(SupplyPoint(Drop)-Pawn->K2_GetActorLocation()).Magnitude()<=350.f;}
inline bool SupplyAvailable(AFortAthenaSupplyDrop *Drop)
{
    if(!Drop || Drop->IsActorBeingDestroyed()) return false;
    auto &State=Track(Drop);
    if(State.Busy || State.Done || Syndicate::Now()<State.RetryAt) return false;
    if(auto BP=Syndicate::As<AAthenaSupplyDrop_C>(Drop)) return !BP->Opened && !BP->VehicleType;
    return false;
}
inline bool Supply(AFortAthenaSupplyDrop *Drop,std::vector<AFortPickup*> *Output=nullptr)
{
    if(!Drop || Drop->IsActorBeingDestroyed()) return false;
    auto &Record=Track(Drop);
    if(Record.Busy || Record.Done || Syndicate::Now()<Record.RetryAt) return true;
    Record.Busy=true;
    Syndicate::Ref<AFortAthenaSupplyDrop> Guard(Drop);
    auto Group=Drop->LootTableNameOverride;
    if(auto BP=Syndicate::As<AAthenaSupplyDrop_C>(Drop))
    {
        if(Group.ToString()=="None" || Group.ToString().empty()) Group=BP->LootTableNameToUse;
        // 10.40 can leave the runtime selection unset while the Blueprint's
        // default table is valid. Do not silently discard that configuration.
        if(Group.ToString()=="None" || Group.ToString().empty()) Group=BP->LootTableName;
        if(BP->VehicleType) {Track(Drop).Busy=false;return false;}
    }
    if(Group.ToString()=="None" || Group.ToString().empty())
        Group=UKismetStringLibrary::Conv_StringToName(L"Loot_AthenaSupplyDrop");
    auto Drops=Looting::PickLootDrops(Group);
    if(Drops.empty() && (Drop->LootTableNameOverride.ToString()=="None" || Drop->LootTableNameOverride.ToString().empty()))
        if(auto BP=Syndicate::As<AAthenaSupplyDrop_C>(Guard.Get()))
        {
            if(BP->LootTableName!=Group && BP->LootTableName.ToString()!="None" && !BP->LootTableName.ToString().empty())
            {Group=BP->LootTableName;Drops=Looting::PickLootDrops(Group);}
            auto Default=UKismetStringLibrary::Conv_StringToName(L"Loot_AthenaSupplyDrop");
            if(Drops.empty() && Group!=Default) {Group=Default;Drops=Looting::PickLootDrops(Group);}
        }
    // Some playlist tables use a named native supply tier that differs from
    // the historical default. Discover only actual standard supply rows.
    if(Drops.empty())
    {
        UDataTable *Tier=nullptr,*Package=nullptr;
        if(Looting::Tables(Tier,Package))for(auto &Pair:Tier->RowMap)
        {
            auto Row=reinterpret_cast<FFortLootTierData*>(Pair.Value());if(!Row||Row->Weight<=0.f)continue;
            auto Name=Row->TierGroup.ToString();std::transform(Name.begin(),Name.end(),Name.begin(),[](unsigned char C){return std::tolower(C);});
            if(Name.find("supplydrop")==std::string::npos||Name.contains("vehicle")||Name.contains("llama"))continue;
            auto Candidate=Looting::PickLootDrops(Row->TierGroup);
            if(!Candidate.empty()){Group=Row->TierGroup;Drops=std::move(Candidate);break;}
        }
    }
    Drop=Guard.Get();
    if(!Drop) return true;
    if(Drops.empty())
    {
        auto &Live=Track(Drop); Live.Busy=false; Live.RetryAt=Syndicate::Now()+1.f;
        Log("Supply drop loot unavailable for native tier: "+Group.ToString());
        return false; // Retain native handling for unsupported special drop tables/vehicle drops.
    }
    FVector Location=SupplyPoint(Drop)+FVector(0,0,80.f);
    // A Blueprint location query is optional; the reflected actor transform is a safe fallback.
    if(auto BP=Syndicate::As<AAthenaSupplyDrop_C>(Drop))
        if(auto Function=BP->Class->GetFunction("AthenaSupplyDrop_C","GetLootSpawnLocation"))
        {
            SDK::Params::AthenaSupplyDrop_C_GetLootSpawnLocation Args{};
            BP->ProcessEvent(Function,&Args);
            if(Syndicate::Finite(Args.LootSpawnLocation)&&Args.LootSpawnLocation.Magnitude()>0.f&&
               Syndicate::Distance2D(Args.LootSpawnLocation,Location)<1500.f)Location=Args.LootSpawnLocation;
        }
    std::vector<Syndicate::Ref<AFortPickup>> Spawned;
    bool Success=SpawnBatch(Drops,Location,EFortPickupSpawnSource::SupplyDrop,Spawned);
    Drop=Guard.Get();
    if(!Drop) { Rollback(Spawned); return true; }
    // ProcessEvent and actor creation can reenter the tracker and move vector storage.
    auto &Live=Track(Drop); Live.Busy=false; Live.Done=Success; Live.RetryAt=Syndicate::Now()+1.f;
    if(!Success) { Rollback(Spawned); return true; }
    if(Output) for(auto Ref:Spawned) if(auto P=Ref.Get()) Output->push_back(P);
    Drop->OnLootSpawned();
    Log("Supply drop native loot spawned: "+std::to_string(Spawned.size()));
    return true;
}
inline bool SearchSupply(AFortAthenaSupplyDrop *Drop,AFortPawn *Pawn,std::vector<AFortPickup*> *Output=nullptr)
{
    if(!Pawn || Pawn->IsDead() || !SupplyAvailable(Drop) || !InSupplyReach(Drop,Pawn)) return false;
    Syndicate::Ref<AFortAthenaSupplyDrop> Guard(Drop);
    Supply(Drop,Output);
    Drop=Guard.Get();
    if(!Drop) return Output && !Output->empty();
    if(!Track(Drop).Done) return false; // A failed output cannot consume the crate.
    if(auto BP=Syndicate::As<AAthenaSupplyDrop_C>(Drop))
    {
        BP->MyInteractingPawn=Pawn;
        if(auto Function=BP->Class->GetFunction("AthenaSupplyDrop_C","OpenSupplyDrop"))
        {
            SDK::Params::AthenaSupplyDrop_C_OpenSupplyDrop Args{};
            BP->ProcessEvent(Function,&Args); // Native opening/replication/FX; nested SpawnLoot is deduplicated.
        }
    }
    return true;
}
inline bool Handle(UObject *Object,UFunction *Function,void *Parameters)
{
    if(!Object || !Function) return false;
    if(Parameters&&Function->Name.ToString()=="ServerAttemptInteract")
        if(auto C=Syndicate::As<UFortControllerComponent_Interaction>(Object))
        {
            auto Args=static_cast<SDK::Params::FortControllerComponent_Interaction_ServerAttemptInteract*>(Parameters);
            if(auto Drop=Syndicate::As<AAthenaSupplyDrop_C>(Args->ReceivingActor);Drop&&!Drop->VehicleType)
            {
                auto PC=Syndicate::As<AFortPlayerControllerAthena>(C->GetOwner());
                auto Pawn=PC?Syndicate::As<AFortPlayerPawnAthena>(PC->Pawn):nullptr;
                if(Pawn&&!Pawn->IsDead()&&InSupplyReach(Drop,Pawn))
                {SearchSupply(Drop,Pawn);return true;}
            }
        }

    static FName SpawnLoot=UKismetStringLibrary::Conv_StringToName(L"SpawnLoot");
    static FName Destruction=UKismetStringLibrary::Conv_StringToName(L"SpawningLootOnDestruction");
    static FName Interact=UKismetStringLibrary::Conv_StringToName(L"BlueprintOnInteract");
    static FName Output=UKismetStringLibrary::Conv_StringToName(L"ServerSpawnItem");
    if(Function->Name==SpawnLoot || Function->Name==Destruction)
        if(auto Drop=Syndicate::As<AFortAthenaSupplyDrop>(Object)) return Supply(Drop);
    if(Function->Name==Interact && Parameters)
    {
        if(auto Drop=Syndicate::As<AAthenaSupplyDrop_C>(Object))
        {
            if(Drop->VehicleType) return false;
            auto Pawn=const_cast<AFortPawn*>(static_cast<SDK::Params::BuildingActor_BlueprintOnInteract *>(Parameters)->InteractingPawn);
            SearchSupply(Drop,Pawn);
            return true; // Retry failed output; never let native opening consume an empty crate.
        }
    }
    return false;
}
} // namespace InteractionLoot
