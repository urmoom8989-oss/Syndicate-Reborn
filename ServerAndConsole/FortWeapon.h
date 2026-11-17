#pragma once
#include "FortInventory.h"
#include "SyndicateRuntime.h"
namespace FortWeapon
{
inline void (*OnReloadOG)(AFortWeapon *, int) = nullptr;
inline void OnReload(AFortWeapon *W, int RemoveCount)
{
    if (!W)
        return;
    auto Pawn = Cast<AFortPlayerPawn>(W->GetOwner());
    auto Controller = Pawn ? Pawn->Controller : nullptr;
    Syndicate::Ref<AFortWeapon> Guard(W);
    Syndicate::Ref<AController> ControllerGuard(Controller);
    auto Definition = W->WeaponData;
    auto Ranged = Cast<UFortWeaponRangedItemDefinition>(Definition);
    auto CostDefinition = Ranged ? Ranged->GetAmmoWorldItemDefinition_BP() : nullptr;
    // Missing ammo data must never turn the gun itself into a reload cost.
    // A stackable consumable can legitimately spend its own item definition.
    if (!CostDefinition && Definition && Definition->IsStackable() && Definition->MaxStackSize > 1)
        CostDefinition = Definition;
    FGuid WeaponGuid = W->ItemEntryGuid;
    auto InventoryEntry=FortInventory::FindItemEntry(Controller,WeaponGuid);
    auto AthenaPawn=Cast<AFortPlayerPawnAthena>(Pawn);
    bool InventoryWeapon=InventoryEntry && InventoryEntry->ItemDefinition==Definition &&
        (!FortInventory::EquipmentAllowed || (AthenaPawn && FortInventory::EquipmentAllowed(AthenaPawn)));
    int Before = Controller && CostDefinition ? FortInventory::CountDefinition(Controller, CostDefinition) : 0;
    if (OnReloadOG)
        OnReloadOG(W, RemoveCount);
    Controller = ControllerGuard.Get();
    if (!Controller || !InventoryWeapon)
        return;
    // Some 10.40 server paths update the magazine/consumable ability but leave reserve
    // inventory client-owned. Charge only the part the native callback did NOT charge.
    int After = CostDefinition ? FortInventory::CountDefinition(Controller, CostDefinition) : 0;
    int NativeSpent = std::max(0, Before - After);
    int Due = std::max(0, std::clamp(RemoveCount, 0, 999) - NativeSpent);
    if (Due > 0 && CostDefinition)
        FortInventory::RemoveDefinition(Controller, CostDefinition, std::min(Due, After));
    if (auto Weapon = Guard.Get())
        if (auto E = FortInventory::FindItemEntry(Controller, WeaponGuid))
        {
            E->LoadedAmmo = std::max(0, Weapon->AmmoCount);
            FortInventory::Update(Controller, E);
        }
}
inline void HookAll()
{
    InstallHook(reinterpret_cast<void *>(ImageBase + 0x1C66A30), OnReload, reinterpret_cast<void **>(&OnReloadOG));
}
} // namespace FortWeapon
