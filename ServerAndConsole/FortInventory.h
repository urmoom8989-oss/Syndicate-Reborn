#pragma once
#include "framework.h"
// Both controller types use GUID identity; never pair entries and instances by array position.
namespace FortInventory
{
inline bool (*EquipmentAllowed)(AFortPlayerPawnAthena *)=nullptr;
inline void (*EquipmentObserver)(AFortPlayerPawnAthena *,UFortWeaponItemDefinition *)=nullptr;
inline AFortInventory *Get(AController *C)
{
    if (!C)
        return nullptr;
    if (C->IsA(AFortAthenaAIBotController::StaticClass()))
        return static_cast<AFortAthenaAIBotController *>(C)->Inventory;
    if (C->IsA(AFortPlayerController::StaticClass()))
        return static_cast<AFortPlayerController *>(C)->WorldInventory;
    return nullptr;
}
inline FFortItemEntry *FindItemEntry(AController *C, const FGuid &G)
{
    auto I = Get(C);
    if (I)
        for (auto &E : I->Inventory.ReplicatedEntries)
            if (E.ItemGuid == G)
                return &E;
    return nullptr;
}
inline FFortItemEntry *FindItemEntry(AController *C, UFortItemDefinition *D)
{
    auto I = Get(C);
    if (I && D)
        for (auto &E : I->Inventory.ReplicatedEntries)
            if (E.ItemDefinition == D && E.Count > 0)
                return &E;
    return nullptr;
}
inline UFortWorldItem *Instance(AFortInventory *I, const FGuid &G)
{
    if (I)
        for (auto P : I->Inventory.ItemInstances)
            if (P && P->ItemEntry.ItemGuid == G)
                return P;
    return nullptr;
}
inline void Update(AController *C, FFortItemEntry *E = nullptr)
{
    auto I = Get(C);
    if (!I)
        return;
    if (E)
    {
        auto P = Instance(I, E->ItemGuid);
        if (P)
        {
            P->ItemEntry.Count = E->Count;
            P->ItemEntry.LoadedAmmo = E->LoadedAmmo;
        }
        I->Inventory.MarkItemDirty(*E);
        if (P)
        {
            P->ItemEntry.ReplicationID = E->ReplicationID;
            P->ItemEntry.ReplicationKey = E->ReplicationKey;
            P->ItemEntry.MostRecentArrayReplicationKey = E->MostRecentArrayReplicationKey;
        }
    }
    else
        I->Inventory.MarkArrayDirty();
    I->bRequiresLocalUpdate = true;
    I->HandleInventoryLocalUpdate();
    I->ForceNetUpdate();
    if (C->IsA(AFortPlayerController::StaticClass()))
        static_cast<AFortPlayerController *>(C)->HandleWorldInventoryLocalUpdate();
}
inline UFortItemDefinition *FindItemDefinition(AController *C, const FGuid &G)
{
    auto E = FindItemEntry(C, G);
    return E ? E->ItemDefinition : nullptr;
}
inline EFortQuickBars GetQuickBars(UFortItemDefinition *D)
{
    if (!D || D->IsA(UFortWeaponMeleeItemDefinition::StaticClass()) ||
        D->IsA(UFortEditToolItemDefinition::StaticClass()) || D->IsA(UFortBuildingItemDefinition::StaticClass()) ||
        D->IsA(UFortAmmoItemDefinition::StaticClass()) || D->IsA(UFortResourceItemDefinition::StaticClass()) ||
        D->IsA(UFortTrapItemDefinition::StaticClass()))
        return EFortQuickBars::Secondary;
    return EFortQuickBars::Primary;
}
inline int PrimaryCount(AController *C)
{
    auto I = Get(C);
    int N = 0;
    if (I)
        for (auto &E : I->Inventory.ReplicatedEntries)
            if (E.Count > 0 && GetQuickBars(E.ItemDefinition) == EFortQuickBars::Primary)
                ++N;
    return N;
}
inline bool IsFullInventory(AController *C)
{
    return !Get(C) || PrimaryCount(C) >= 5;
}
inline UFortWorldItem *CreateNewItem(AController *C, UFortItemDefinition *D, int N)
{
    auto I = Get(C);
    if (!I || !D || N <= 0)
        return nullptr;
    auto P = static_cast<UFortWorldItem *>(D->CreateTemporaryItemInstanceBP(N, 0));
    if (!P)
        return nullptr;
    P->OwnerInventory = I;
    if (C->IsA(AFortPlayerController::StaticClass()))
        P->SetOwningControllerForTemporaryItem(static_cast<AFortPlayerController *>(C));
    return P;
}
// Resources have one shared 999-unit budget, even across duplicate GUID stacks.
inline int ResourceRoom(AController *C,UFortItemDefinition *D)
{
    if(!Cast<UFortResourceItemDefinition>(D))return 9999;
    int Room=999;auto I=Get(C);if(!I)return 0;
    for(auto &E:I->Inventory.ReplicatedEntries)
        if(E.ItemDefinition==D&&E.Count>0)Room-=std::min(Room,E.Count);
    return Room;
}
inline void ClampResource(AController *C,UFortItemDefinition *D);
// Actual accepted count lets pickup handlers preserve overflow.
inline int Add(AController *C, UFortItemDefinition *D, int Count, int Ammo = 0, bool Stack = true)
{
    auto I = Get(C);
    if (!I || !D || Count <= 0 || Count > 9999)
        return 0;
    bool Resource=Cast<UFortResourceItemDefinition>(D)!=nullptr;
    if(Resource)ClampResource(C,D);
    int Budget=std::min(Count,ResourceRoom(C,D)),Left=Budget;
    int Max = std::clamp(D->MaxStackSize, 1, Resource?999:9999);
    if (Stack && D->IsStackable())
    {
        std::vector<FGuid> Stacks;
        for (auto &E : I->Inventory.ReplicatedEntries)
            if (E.ItemDefinition == D && E.Count > 0 && E.Count < Max)
                Stacks.push_back(E.ItemGuid);
        // Local inventory notifications can mutate/reallocate the entry array.
        // Keep GUIDs across callbacks, never an iterator into that array.
        for (auto &Guid : Stacks)
        {
            auto E = FindItemEntry(C, Guid);
            if (!E || E->ItemDefinition != D || E->Count >= Max)
                continue;
            int N = std::min({Left, Max-E->Count,ResourceRoom(C,D)});
            if(N<=0)break;
            E->Count += N;
            Left -= N;
            Update(C, E);
            if (!Left)
                break;
        }
    }
    while (Left > 0)
    {
        if (GetQuickBars(D) == EFortQuickBars::Primary && IsFullInventory(C))
            break;
        int N = D->IsStackable() ? std::min(Left, Max) : 1;
        N=std::min(N,ResourceRoom(C,D));if(N<=0)break;
        auto P = CreateNewItem(C, D, N);
        if (!P)
            break;
        P->ItemEntry.LoadedAmmo = std::max(0, Ammo);
        I->Inventory.ItemInstances.Add(P);
        I->Inventory.ReplicatedEntries.Add(P->ItemEntry);
        Update(C, &I->Inventory.ReplicatedEntries[I->Inventory.ReplicatedEntries.Num() - 1]);
        Left -= N;
    }
    return Budget - Left;
}
inline void GiveItem(AController *C, UFortItemDefinition *D, int N = 1, int Ammo = 0, bool Stack = false)
{
    Add(C, D, N, Ammo, Stack);
}
inline bool Equip(AController *C, AFortPlayerPawnAthena *Pawn, const FGuid &Guid)
{
    FGuid Selected = Guid;
    auto Entry = FindItemEntry(C, Selected);
    auto Definition = Entry ? Cast<UFortWeaponItemDefinition>(Entry->ItemDefinition) : nullptr;
    if (!Pawn || !Definition || Entry->Count <= 0 || (EquipmentAllowed && !EquipmentAllowed(Pawn)))
        return false;
    if (Pawn->CurrentWeapon && Pawn->CurrentWeapon->ItemEntryGuid == Selected)
    {
        if(EquipmentObserver) EquipmentObserver(Pawn,Definition);
        return true;
    }
    // Shipping-client server RPC bodies can be empty. Use the native pawn
    // equipment function with the actual inventory GUID, as the original GS did.
    Pawn->EquipWeaponDefinition(Definition, Selected);
    if(EquipmentObserver && Pawn->CurrentWeapon && Pawn->CurrentWeapon->ItemEntryGuid==Selected)
        EquipmentObserver(Pawn,Definition);
    return Pawn->CurrentWeapon && Pawn->CurrentWeapon->ItemEntryGuid == Selected;
}
inline UFortWeaponMeleeItemDefinition *SelectedPickaxe(AController *C, AFortPlayerPawnAthena *Pawn)
{
    auto PC = Cast<AFortPlayerController>(C);
    if (!PC) return nullptr;
    auto Selection = PC->CosmeticLoadoutPC.Pickaxe;
    if (Selection && Selection->WeaponDefinition) return Selection->WeaponDefinition;
    Selection = Pawn ? Pawn->CosmeticLoadout.Pickaxe : nullptr;
    return Selection ? Selection->WeaponDefinition : nullptr;
}
inline bool SyncPlayerPickaxe(AFortPlayerController *PC, AFortPlayerPawnAthena *Pawn,
                              UFortWeaponMeleeItemDefinition *Fallback = nullptr);
inline bool EquipPickaxe(AController *C, AFortPlayerPawnAthena *Pawn)
{
    auto Inventory = Get(C);
    if (!Inventory || !Pawn)
        return false;
    if (auto Selection = SelectedPickaxe(C, Pawn))
    {
        // The locker loadout can arrive after the initial inventory. Never
        // select the first (default) melee entry ahead of a valid locker choice.
        SyncPlayerPickaxe(Cast<AFortPlayerController>(C), Pawn);
        auto Entry = FindItemEntry(C, Selection);
        return Entry && Equip(C, Pawn, Entry->ItemGuid);
    }
    for (auto &Entry : Inventory->Inventory.ReplicatedEntries)
        if (Entry.Count > 0 && Cast<UFortWeaponMeleeItemDefinition>(Entry.ItemDefinition))
        {
            FGuid Guid = Entry.ItemGuid;
            return Equip(C, Pawn, Guid);
        }
    return false;
}
inline bool RemoveGuid(AController *C, const FGuid &G, int N);
inline void ClearWarmupLoot(AController *C)
{
    auto Inventory = Get(C);
    if (!Inventory)
        return;
    std::vector<FGuid> Remove;
    for (auto &Entry : Inventory->Inventory.ReplicatedEntries)
    {
        auto Definition = Cast<UFortWorldItemDefinition>(Entry.ItemDefinition);
        if (Definition && Definition->bCanBeDropped &&
            !Cast<UFortWeaponMeleeItemDefinition>(Definition) &&
            !Cast<UFortEditToolItemDefinition>(Definition) && !Cast<UFortBuildingItemDefinition>(Definition))
            Remove.push_back(Entry.ItemGuid);
    }
    for (auto &Guid : Remove)
        if (auto Entry = FindItemEntry(C, Guid))
        {
            int Count = Entry->Count;
            RemoveGuid(C, Guid, Count);
        }
}
inline bool RemoveGuid(AController *C, const FGuid &G, int N = 1)
{
    auto I = Get(C);
    if (!I || N <= 0)
        return false;
    for (int i = 0; i < I->Inventory.ReplicatedEntries.Num(); ++i)
    {
        auto &E = I->Inventory.ReplicatedEntries[i];
        if (E.ItemGuid != G)
            continue;
        if (N > E.Count)
            return false;
        if (N < E.Count)
        {
            E.Count -= N;
            Update(C, &E);
            return true;
        }
        for (int j = I->Inventory.ItemInstances.Num() - 1; j >= 0; --j)
            if (I->Inventory.ItemInstances[j] && I->Inventory.ItemInstances[j]->ItemEntry.ItemGuid == G)
                I->Inventory.ItemInstances.Remove(j);
        I->Inventory.ReplicatedEntries.Remove(i);
        Update(C);
        return true;
    }
    return false;
}
inline void ClampResource(AController *C,UFortItemDefinition *D)
{
    auto I=Get(C);if(!I||!Cast<UFortResourceItemDefinition>(D))return;
    std::vector<FGuid> IDs;for(auto &E:I->Inventory.ReplicatedEntries)if(E.ItemDefinition==D&&E.Count>0)IDs.push_back(E.ItemGuid);
    int Remaining=999;
    for(auto G:IDs)if(auto E=FindItemEntry(C,G);E&&E->ItemDefinition==D&&E->Count>0){
        int Keep=std::min(E->Count,Remaining),Remove=E->Count-Keep;Remaining-=Keep;
        if(Remove>0)RemoveGuid(C,G,Remove);
    }
}
inline void ClampResources(AController *C)
{
    auto I=Get(C);if(!I)return;std::vector<UFortItemDefinition*> Types;
    for(auto &E:I->Inventory.ReplicatedEntries)if(Cast<UFortResourceItemDefinition>(E.ItemDefinition)&&std::find(Types.begin(),Types.end(),E.ItemDefinition)==Types.end())Types.push_back(E.ItemDefinition);
    for(auto D:Types)ClampResource(C,D);
}
inline bool SyncPlayerPickaxe(AFortPlayerController *PC, AFortPlayerPawnAthena *Pawn,
                              UFortWeaponMeleeItemDefinition *Fallback)
{
    if(Pawn && EquipmentAllowed && !EquipmentAllowed(Pawn)) return false;
    auto Inventory = Get(PC);
    auto Wanted = SelectedPickaxe(PC, Pawn);
    if (!Wanted) Wanted = Fallback;
    if (!Inventory || !Wanted) return false;
    bool WasHoldingPickaxe = Pawn && Pawn->CurrentWeapon &&
        Cast<UFortWeaponMeleeItemDefinition>(Pawn->CurrentWeapon->WeaponData);
    if (!FindItemEntry(PC, Wanted) && Add(PC, Wanted, 1, 0, false) != 1)
        return false; // Preserve the old item if native allocation failed.
    auto Entry = FindItemEntry(PC, Wanted);
    if (!Entry) return false;
    FGuid WantedGuid = Entry->ItemGuid;
    // Equipping a gun or consumable must not be interrupted by a late loadout.
    if (Pawn && (!Pawn->CurrentWeapon || WasHoldingPickaxe) && !Equip(PC, Pawn, WantedGuid))
        return false;
    struct OldItem { FGuid Guid; int Count; };
    std::vector<OldItem> Old;
    for (auto &Item : Inventory->Inventory.ReplicatedEntries)
        if (Item.Count > 0 && Item.ItemDefinition != Wanted &&
            Cast<UFortWeaponMeleeItemDefinition>(Item.ItemDefinition))
            Old.push_back({Item.ItemGuid, Item.Count});
    for (auto &Item : Old) RemoveGuid(PC, Item.Guid, Item.Count);
    return true;
}
inline int CountDefinition(AController *C, UFortItemDefinition *D)
{
    auto I = Get(C);
    int Count = 0;
    if (I && D)
        for (auto &E : I->Inventory.ReplicatedEntries)
            if (E.ItemDefinition == D && E.Count > 0)
                Count += E.Count;
    return Count;
}
inline bool RemoveDefinition(AController *C, UFortItemDefinition *D, int Count)
{
    if (Count <= 0 || CountDefinition(C, D) < Count)
        return false;
    auto I = Get(C);
    while (Count > 0)
    {
        FGuid Guid{};
        int Take = 0;
        for (auto &E : I->Inventory.ReplicatedEntries)
            if (E.ItemDefinition == D && E.Count > 0)
            {
                Guid = E.ItemGuid;
                Take = std::min(Count, E.Count);
                break;
            }
        if (Take <= 0 || !RemoveGuid(C, Guid, Take))
            return false;
        Count -= Take;
    }
    return true;
}
inline void RemoveItem(AController *C, UFortItemDefinition *D, int N = 1)
{
    auto E = FindItemEntry(C, D);
    if (E)
    {
        FGuid G = E->ItemGuid;
        RemoveGuid(C, G, N);
    }
}

inline int Transfer(AController *C, AFortPlayerPawnAthena *Pawn, const FFortItemEntry &Incoming, const FGuid &Swap)
{
    if (!Get(C) || !Pawn || !Incoming.ItemDefinition || Incoming.Count <= 0 || Incoming.Count > 9999)
        return 0;
    auto Existing = FindItemEntry(C, Incoming.ItemDefinition);
    bool CanFill = Incoming.ItemDefinition->IsStackable() && Existing &&
                   Existing->Count < static_cast<int>(Incoming.ItemDefinition->MaxStackSize);
    auto Old = FindItemEntry(C, Swap);
    if (Old && !CanFill && GetQuickBars(Old->ItemDefinition) == EFortQuickBars::Primary)
    {
        auto Copy = *Old;
        bool WasEquipped = Pawn->CurrentWeapon && Pawn->CurrentWeapon->ItemEntryGuid == Copy.ItemGuid;
        int Max = std::clamp(Incoming.ItemDefinition->MaxStackSize, 1, 9999);
        int Count = Incoming.ItemDefinition->IsStackable() ? std::min(Incoming.Count, Max) : 1;
        auto Prepared = CreateNewItem(C, Incoming.ItemDefinition, Count);
        if (!Prepared)
            return 0;
        Prepared->ItemEntry.LoadedAmmo = std::max(0, Incoming.LoadedAmmo);
        auto Drop = SpawnPickup(Copy.ItemDefinition, Copy.Count, Copy.LoadedAmmo, Pawn->K2_GetActorLocation(),
                                EFortPickupSourceTypeFlag::Player, EFortPickupSpawnSource::Unset, Pawn);
        if (!Drop)
            return 0;
        if (!RemoveGuid(C, Copy.ItemGuid, Copy.Count))
        {
            Drop->K2_DestroyActor();
            return 0;
        }
        auto I = Get(C);
        I->Inventory.ItemInstances.Add(Prepared);
        I->Inventory.ReplicatedEntries.Add(Prepared->ItemEntry);
        Update(C, &I->Inventory.ReplicatedEntries[I->Inventory.ReplicatedEntries.Num() - 1]);
        if (WasEquipped)
            Equip(C, Pawn, Prepared->ItemEntry.ItemGuid);
        return Count;
    }
    return Add(C, Incoming.ItemDefinition, Incoming.Count, Incoming.LoadedAmmo, true);
}
// The held weapon's magazine on the HUD. Guns get it refreshed when they hit something (damage cues);
// gadgets that never deal damage, like the Batman grapnel gun, never did, so their count stayed at 10.
// Called twice a second for each living human. A grappler that is out of charges is removed, as in the
// real game.
inline void SyncHeldAmmo(AController *C, AFortPlayerPawnAthena *Pawn)
{
    auto Weapon = Pawn ? Pawn->CurrentWeapon : nullptr;
    if (!C || !Weapon || !Weapon->WeaponData) return;
    auto E = FindItemEntry(C, Weapon->ItemEntryGuid);
    if (!E || E->ItemDefinition != Weapon->WeaponData) return;
    int Ammo = std::max(0, Weapon->AmmoCount);
    auto Name = Weapon->WeaponData->GetName();
    bool Grappler = Name.find("Grappl") != std::string::npos || Name.find("Grapnel") != std::string::npos;
    static UFortItemDefinition *LastLogged = nullptr;
    if (Grappler && LastLogged != Weapon->WeaponData)
    {
        LastLogged = Weapon->WeaponData;
        Log("Held gadget " + Name + ": ammo " + std::to_string(Ammo) + ", HUD " + std::to_string(E->LoadedAmmo) +
            ", count " + std::to_string(E->Count));
    }
    if (E->LoadedAmmo == Ammo) return;
    bool Spent = Ammo == 0 && E->LoadedAmmo > 0;
    E->LoadedAmmo = Ammo;
    Update(C, E);
    if (Spent && Grappler)
    {
        FGuid Guid = E->ItemGuid;
        RemoveGuid(C, Guid, E->Count);
    }
}
} // namespace FortInventory
