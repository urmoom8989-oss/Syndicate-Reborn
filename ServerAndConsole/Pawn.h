#pragma once
#include "framework.h"
#include "FortInventory.h"
#include "PickupFlight.h"

namespace Pawn
{
void ServerHandlePickup(AFortPlayerPawn *PlayerPawn, AFortPickup *Pickup, float InFlyTime,
                        const struct FVector &InStartDirection, bool bPlayPickupSound)
{
    PickupFlight::Begin(PlayerPawn, Pickup, InStartDirection, bPlayPickupSound);
}

void (*NetMulticast_Athena_BatchedDamageCuesOG)(AFortPlayerPawn *PlayerPawn,
                                                const FAthenaBatchedDamageGameplayCues_Shared &SharedData,
                                                const FAthenaBatchedDamageGameplayCues_NonShared &NonSharedData);
void NetMulticast_Athena_BatchedDamageCues(AFortPlayerPawn *P, const FAthenaBatchedDamageGameplayCues_Shared &Shared,
                                           const FAthenaBatchedDamageGameplayCues_NonShared &Other)
{
    if (P && P->Controller && P->CurrentWeapon)
    {
        auto E = FortInventory::FindItemEntry(P->Controller, P->CurrentWeapon->ItemEntryGuid);
        if (E)
        {
            E->LoadedAmmo = std::max(0, P->CurrentWeapon->AmmoCount);
            FortInventory::Update(P->Controller, E);
        }
    }
    if (NetMulticast_Athena_BatchedDamageCuesOG)
        NetMulticast_Athena_BatchedDamageCuesOG(P, Shared, Other);
}

void (*OnCapsuleBeginOverlapOG)(AFortPlayerPawn *Pawn, UPrimitiveComponent *OverlappedComp, AActor *OtherActor,
                                UPrimitiveComponent *OtherComp, int32 OtherBodyIndex, bool bFromSweep,
                                const FHitResult &SweepResult);
void OnCapsuleBeginOverlap(AFortPlayerPawn *P, UPrimitiveComponent *Overlap, AActor *Other,
                           UPrimitiveComponent *Component, int32 BodyIndex, bool Sweep, const FHitResult &Hit)
{
    if (OnCapsuleBeginOverlapOG)
        OnCapsuleBeginOverlapOG(P, Overlap, Other, Component, BodyIndex, Sweep, Hit);
    auto Pickup = Cast<AFortPickup>(Other);
    auto PC = P ? Cast<AFortPlayerControllerAthena>(P->Controller) : nullptr;
    if (!P || !PC || !Pickup || Pickup->bPickedUp || Pickup->PawnWhoDroppedPickup == P)
        return;
    auto D = Cast<UFortWorldItemDefinition>(Pickup->PrimaryPickupItemEntry.ItemDefinition);
    if (!D || !D->IsStackable())
        return;
    auto E = FortInventory::FindItemEntry(PC, D);
    bool Auto = D->IsA(UFortAmmoItemDefinition::StaticClass()) || D->IsA(UFortResourceItemDefinition::StaticClass()) ||
                D->IsA(UFortTrapItemDefinition::StaticClass()) || E;
    if (Auto && (!E || E->Count < D->MaxStackSize))
        PickupFlight::Begin(P, Pickup, FVector(), true);
}

void (*ServerSendZiplineStateOG)(AFortPlayerPawn *PlayerPawn, const FZiplinePawnState &InZiplineState);
void ServerSendZiplineState(AFortPlayerPawn *P, const FZiplinePawnState &State)
{
    // Native zipline code owns launch impulses and velocity. Applying a second launch
    // here caused double impulses and discarded horizontal movement.
    if (ServerSendZiplineStateOG)
        ServerSendZiplineStateOG(P, State);
}

void MovingEmoteStopped(AFortPawn *Pawn)
{
    Log("MovingEmoteStopped Called!");
    if (!Pawn)
        return;

    Pawn->bMovingEmote = false;
}

void HookAll()
{
    HookVTable<APlayerPawn_Athena_C>(0x1C7, ServerHandlePickup);

    InstallHook(reinterpret_cast<void *>(ImageBase + 0x1EF0A80), NetMulticast_Athena_BatchedDamageCues,
                reinterpret_cast<void **>(&NetMulticast_Athena_BatchedDamageCuesOG));
    InstallHook((LPVOID)(ImageBase + 0x196DB00), OnCapsuleBeginOverlap, (LPVOID *)&OnCapsuleBeginOverlapOG);
    // InstallHook(reinterpret_cast<void*>(ImageBase + 0x1EFAC60), ServerMove, nullptr);

    HookVTable<APlayerPawn_Athena_C>(0x1D2, ServerSendZiplineState,
                                     reinterpret_cast<void **>(&ServerSendZiplineStateOG));

    // InstallHook((LPVOID)(ImageBase + 0xB3EBA0), MovingEmoteStopped, nullptr);

    Log("Pawn Hooked!");
}
} // namespace Pawn
