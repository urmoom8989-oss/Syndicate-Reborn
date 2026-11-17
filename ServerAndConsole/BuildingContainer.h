#pragma once
#include "Looting.h"
namespace BuildingContainer
{
inline char SpawnLootTracked(ABuildingContainer *C, std::vector<AFortPickup *> *Output)
{
    if (!C || C->bAlreadySearched || C->bDestroyed || C->IsActorBeingDestroyed())
        return false;
    auto Group = C->SearchLootTierGroup;
    auto Name = Group.ToString();
    auto Source = EFortPickupSpawnSource::Unset;
    auto Flag = EFortPickupSourceTypeFlag::Container;
    if (Name == "Loot_Treasure")
    {
        Group = UKismetStringLibrary::Conv_StringToName(L"Loot_AthenaTreasure");
        Source = EFortPickupSpawnSource::Chest;
    }
    else if (Name == "Loot_Ammo")
    {
        Group = UKismetStringLibrary::Conv_StringToName(L"Loot_AthenaAmmoLarge");
        Source = EFortPickupSpawnSource::AmmoBox;
    }
    else if (Name.contains("FloorLoot"))
        Flag = EFortPickupSourceTypeFlag::FloorLoot;
    auto Drops = Looting::PickLootDrops(Group);
    if (Drops.empty())
        return false;
    auto L = C->K2_GetActorLocation() + C->GetActorForwardVector() * C->LootSpawnLocation_Athena.X +
             C->GetActorRightVector() * C->LootSpawnLocation_Athena.Y +
             C->GetActorUpVector() * C->LootSpawnLocation_Athena.Z;
    std::vector<AFortPickup *> Spawned;
    for (auto &E : Drops)
    {
        auto P = SpawnPickup(E.ItemDefinition, E.Count, E.LoadedAmmo, L, Flag, Source);
        if (!P)
        {
            for (auto Existing : Spawned)
                Existing->K2_DestroyActor();
            return false;
        }
        Spawned.push_back(P);
    }
    C->bAlreadySearched = true;
    ++C->SearchBounceData.SearchAnimationCount;
    C->OnRep_bAlreadySearched();
    C->ForceNetUpdate();
    if (Output) *Output = Spawned; // Publish only a completely successful native drop transaction.
    return true;
}
inline char SpawnLoot(ABuildingContainer *C)
{
    return SpawnLootTracked(C, nullptr); // Keep the hooked engine signature unchanged.
}
inline void HookAll()
{
    InstallHook(reinterpret_cast<void *>(ImageBase + 0x13A91C0), SpawnLoot, nullptr);
    Log("Container loot hooked.");
}
} // namespace BuildingContainer
