#pragma once
#include "PlayerBots.h"
namespace FortPickup
{
__int64 (*CompletePickupAnimationOG)(AFortPickup *) = nullptr;
inline std::vector<Syndicate::Ref<AFortPickup>> Completed;
inline UWorld *PickupWorld = nullptr;
__int64 CompletePickupAnimation(AFortPickup *P)
{
    if (!P)
        return 0;
    if (PickupWorld != UWorld::GetWorld())
    {
        Completed.clear();
        PickupWorld = UWorld::GetWorld();
    }
    std::erase_if(Completed, [](auto &R) { return !R.Get(); });
    for (auto &R : Completed)
        if (R.Get() == P)
            return 0;
    auto Pawn = Cast<AFortPlayerPawnAthena>(P->PickupLocationData.PickupTarget);
    auto C = Pawn ? Pawn->Controller : nullptr;
    if (!Pawn || !FortInventory::Get(C) || !P->PrimaryPickupItemEntry.ItemDefinition)
        return CompletePickupAnimationOG ? CompletePickupAnimationOG(P) : 0;
    // Mark before inventory callbacks: native notifications can re-enter this
    // completion hook while a transfer is still in progress.
    Completed.emplace_back(P);
    Syndicate::Ref<AFortPickup> PickupGuard(P);
    Syndicate::Ref<AFortPlayerPawnAthena> PawnGuard(Pawn);
    int Accepted = 0;
    auto E = P->PrimaryPickupItemEntry;
    if (!Pawn->IsDead())
    {
        if (auto B = PlayerBots::Find(Pawn))
            Accepted = B->FinishPickup(P);
        else
        {
            FGuid Swap{};
            auto Existing = FortInventory::FindItemEntry(C, E.ItemDefinition);
            // Picking up a full stack leaves it on the ground; it never displaces another item.
            if (FortInventory::GetQuickBars(E.ItemDefinition) == EFortQuickBars::Primary &&
                FortInventory::IsFullInventory(C) && (!Existing || !E.ItemDefinition->IsStackable()))
                if (auto W = Pawn->CurrentWeapon)
                    Swap = W->ItemEntryGuid;
            Accepted = FortInventory::Transfer(C, Pawn, E, Swap);
        }
    }
    P = PickupGuard.Get();
    Pawn = PawnGuard.Get();
    if (!P)
        return 0;
    Accepted = std::clamp(Accepted, 0, E.Count);
    if (Pawn)
    for (int i = Pawn->IncomingPickups.Num() - 1; i >= 0; --i)
        if (Pawn->IncomingPickups[i] == P)
            Pawn->IncomingPickups.Remove(i);
    if (Accepted >= E.Count)
    {
        P->K2_DestroyActor();
        return 0;
    }
    // Native pickup flight has removed its projectile movement component. A
    // second TossPickup dereferences that null component (crash at 0x170D64F).
    // Spawn overflow through a fresh native pickup; retain the original if it
    // cannot be allocated, without ever retossing the completed flight actor.
    int Left = E.Count - Accepted;
    if (auto Overflow = SpawnPickup(E.ItemDefinition, Left, E.LoadedAmmo, Pawn ? Pawn->K2_GetActorLocation() : P->K2_GetActorLocation(),
                                    EFortPickupSourceTypeFlag::Player, EFortPickupSpawnSource::Unset, Pawn))
    {
        P->K2_DestroyActor();
        return 0;
    }
    P->PrimaryPickupItemEntry.Count = E.Count - Accepted;
    ++P->PrimaryPickupItemEntry.ReplicationKey;
    P->OnRep_PrimaryPickupItemEntry();
    P->PickupLocationData.PickupTarget = nullptr;
    P->PickupLocationData.ItemOwner = nullptr;
    P->OnRep_PickupLocationData();
    P->bPickedUp = false;
    P->OnRep_bPickedUp();
    P->SetActorHiddenInGame(false);
    P->SetActorEnableCollision(true);
    P->ForceNetUpdate();
    std::erase_if(Completed, [P](auto &R) { return R.Get() == P; });
    return 0;
}
void HookAll()
{
    InstallHook((LPVOID)(ImageBase + 0x16F7D10), CompletePickupAnimation, (LPVOID *)&CompletePickupAnimationOG);
    Log("FortPickup hooked.");
}
} // namespace FortPickup
