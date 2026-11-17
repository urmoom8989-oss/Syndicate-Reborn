#pragma once
#include "SyndicateRuntime.h"
namespace PickupFlight
{
inline bool Begin(AFortPlayerPawn *Pawn, AFortPickup *Pickup, const FVector &Direction, bool Sound)
{
    if (!Pawn || !Pickup || Pawn->IsDead() || Pickup->bPickedUp ||
        !Pickup->PrimaryPickupItemEntry.ItemDefinition || Pickup->PrimaryPickupItemEntry.Count <= 0 ||
        !Syndicate::Finite(Direction))
        return false;
    // Human and Phoebe pawn classes have different RPC vtables. Both enter the
    // same native flight/replication path and share the completion transaction.
    Syndicate::Ref<AFortPlayerPawn> PawnGuard(Pawn);
    Syndicate::Ref<AFortPickup> PickupGuard(Pickup);
    auto &Data = Pickup->PickupLocationData;
    Data.PickupTarget = Pawn;
    Data.ItemOwner = Pawn;
    Data.FlyTime = 0.40f;
    Data.PickupGuid = Pickup->PrimaryPickupItemEntry.ItemGuid;
    Data.StartDirection = (FVector_NetQuantizeNormal)Direction;
    Data.bPlayPickupSound = Sound;
    // Set the claim before callbacks can re-enter the pickup RPC.
    Pickup->bPickedUp = true;
    Pawn->IncomingPickups.Add(Pickup);
    Pickup->OnRep_PickupLocationData();
    Pickup = PickupGuard.Get();
    if (!Pickup || !PawnGuard.Get())
        return true;
    Pickup->OnRep_bPickedUp();
    if (auto Live = PickupGuard.Get())
    {
        Live->FlushNetDormancy();
        Live->ForceNetUpdate();
    }
    return true;
}
} // namespace PickupFlight
