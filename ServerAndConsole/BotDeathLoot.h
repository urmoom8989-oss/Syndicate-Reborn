#pragma once
#include "FortInventory.h"
namespace BotDeathLoot
{
inline int Drop(AFortAthenaAIBotController *Controller, AFortPlayerPawnAthena *Pawn)
{
    auto Inventory = FortInventory::Get(Controller);
    if (!Inventory || !Pawn || !Pawn->IsDead()) return 0;
    Syndicate::Ref<AFortAthenaAIBotController> ControllerGuard(Controller);
    Syndicate::Ref<AFortPlayerPawnAthena> PawnGuard(Pawn);
    struct Entry { FGuid Guid; Syndicate::Ref<UFortWorldItemDefinition> Definition; };
    std::vector<Entry> Entries;
    for (auto &E : Inventory->Inventory.ReplicatedEntries)
        if (auto D = Cast<UFortWorldItemDefinition>(E.ItemDefinition); D && E.Count > 0 && D->bCanBeDropped &&
            !Cast<UFortWeaponMeleeItemDefinition>(D) && !Cast<UFortBuildingItemDefinition>(D))
            Entries.push_back({E.ItemGuid, Syndicate::Ref<UFortWorldItemDefinition>(D)});
    int Dropped = 0;
    for (auto &Saved : Entries)
    {
        Controller = ControllerGuard.Get(); Pawn = PawnGuard.Get();
        auto D = Saved.Definition.Get();
        if (!Controller || !Pawn || !D) break;
        auto E = FortInventory::FindItemEntry(Controller, Saved.Guid);
        if (!E || E->ItemDefinition != D || E->Count <= 0) continue; // Native removal already handled this item.
        int Count = E->Count, Ammo = std::max(0, E->LoadedAmmo);
        if (auto W = Pawn->CurrentWeapon; W && W->ItemEntryGuid == Saved.Guid)
            Ammo = std::max(0, W->AmmoCount);
        auto Pickup = SpawnPickup(D, Count, Ammo, Pawn->K2_GetActorLocation() + FVector(0, 0, 30.f),
                                  EFortPickupSourceTypeFlag::Tossed, EFortPickupSpawnSource::Unset, Pawn);
        if (!Pickup) continue;
        Syndicate::Ref<AFortPickup> PickupGuard(Pickup);
        Controller = ControllerGuard.Get();
        if (!Controller || !FortInventory::RemoveGuid(Controller, Saved.Guid, Count))
        {
            if (auto Failed = PickupGuard.Get()) Failed->K2_DestroyActor();
            continue;
        }
        ++Dropped;
    }
    return Dropped;
}
} // namespace BotDeathLoot
