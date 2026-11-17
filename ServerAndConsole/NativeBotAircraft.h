#pragma once
#include "SyndicateRuntime.h"
#include "FortInventory.h"
#include "BotPhysics.h"
namespace NativeBotAircraft
{
inline bool (*Depart)(AFortAthenaAIBotController *) = nullptr;
inline bool Initialise()
{
    Depart = nullptr;
    // Verified in CL-9380822: FortAthenaBTService_JumpOffBus calls this
    // controller routine. It checks aircraft membership, restarts/possesses
    // the pawn through GameMode. Flight still needs the public BeginSkydiving call.
    auto Address = reinterpret_cast<unsigned char *>(ImageBase + 0xFA1290);
    const unsigned char Prologue[]{0x48, 0x89, 0x5C, 0x24, 0x10, 0x48, 0x89, 0x74, 0x24, 0x18};
    if (std::memcmp(Address, Prologue, sizeof(Prologue)) != 0)
    {
        Log("Native bot departure signature differs from CL-9380822; departure disabled.");
        return false;
    }
    Depart = reinterpret_cast<decltype(Depart)>(Address);
    return true;
}
inline bool Board(AFortAthenaAIBotController *Controller, AFortAthenaAircraft *Bus)
{
    auto World = UWorld::GetWorld();
    auto State = World ? Syndicate::As<AFortGameStateAthena>(World->GameState) : nullptr;
    auto Player = Controller ? Syndicate::As<AFortPlayerStateAthena>(Controller->PlayerState) : nullptr;
    if (!Depart || !Bus || !State || State->GamePhase != EAthenaGamePhase::Aircraft || !Player ||
        Player->IsPlayerDead() || !Controller->Blackboard || !Controller->CachedGameMode)
        return false;
    Syndicate::Ref<AFortAthenaAIBotController> ControllerGuard(Controller);
    Syndicate::Ref<AFortPlayerStateAthena> PlayerGuard(Player);
    // This executable's client-owned ServerSetInAircraft RPC is not a boarding
    // implementation for AI controllers. Authority records the real passenger
    // state before the native jump routine consumes and clears it.
    bool Entering = !Player->bInAircraft;
    Player->bInAircraft = true;
    Player->ForceNetUpdate();
    if (Entering)
        FortInventory::ClearWarmupLoot(Controller);
    if (!PlayerGuard.Get() || !ControllerGuard.Get() || !Player->bInAircraft)
        return false;
    if (auto Warmup = Syndicate::As<AFortPlayerPawnAthena>(Controller->Pawn))
    {
        // Boarding retires the warmup pawn just as the human aircraft flow does.
        // Departure creates/possesses the next pawn through native GameMode.
        auto PawnClass = Warmup->Class;
        Syndicate::Ref<AFortPlayerPawnAthena> Guard(Warmup);
        Controller->UnPossess();
        if (auto Old = Guard.Get())
            Old->K2_DestroyActor();
        if (!ControllerGuard.Get() || !PlayerGuard.Get())
            return false;
        Controller->CacheBotPawnClass = PawnClass;
    }
    return Player->bInAircraft && !Controller->Pawn;
}
inline bool TryDeparture(AFortAthenaAIBotController *Controller, AFortAthenaAircraft *Bus)
{
    auto Player = Controller ? Syndicate::As<AFortPlayerStateAthena>(Controller->PlayerState) : nullptr;
    if (!Depart || !Controller || !Bus || !Player || Player->IsPlayerDead() ||
        !Controller->Blackboard || !Controller->CachedGameMode || !Controller->CacheBotPawnClass.Get())
        return false;
    Syndicate::Ref<AFortAthenaAIBotController> ControllerGuard(Controller);
    Syndicate::Ref<AFortPlayerStateAthena> PlayerGuard(Player);
    Syndicate::Ref<AFortAthenaAircraft> BusGuard(Bus);
    // Retry flight activation on the existing native pawn; never duplicate it.
    bool NewDeparture = Player->bInAircraft;
    if (NewDeparture)
    {
        if (!Depart(Controller))
            return false;
    }
    else if (!Controller->Pawn)
        return false;
    if (!ControllerGuard.Get() || !PlayerGuard.Get() || !BusGuard.Get())
        return false;
    auto Pawn = Syndicate::As<AFortPlayerPawnAthena>(Controller->Pawn);
    if (!Pawn)
    {
        // Native departure clears aircraft membership before attempting spawn.
        // A collision/allocation failure leaves the same passenger aboard so
        // the next bounded attempt can retry native creation in the drop window.
        Player->bInAircraft = true;
        Player->ForceNetUpdate();
        return false;
    }
    // A native return alone does not prove possession succeeded. Confirm the
    // actual aircraft spawn location; a falling warmup pawn cannot pass this.
    if (!Pawn || Pawn->IsDead() || Player->bInAircraft ||
        Syndicate::Distance2D(Pawn->K2_GetActorLocation(), Bus->K2_GetActorLocation()) > 2500.f ||
        std::abs(Pawn->K2_GetActorLocation().Z - Bus->K2_GetActorLocation().Z) > 2500.f)
        return false;
    Syndicate::Ref<AFortPlayerPawnAthena> PawnGuard(Pawn);
    if (NewDeparture)
    {
        // Initialise the fresh match pawn once. Its ability-system attributes
        // can retain warmup damage even though the warmup pawn was retired.
        // Flight activation retries must never heal an existing match pawn.
        Pawn->SetMaxHealth(100.f);
        Pawn = PawnGuard.Get(); if (!Pawn || !ControllerGuard.Get()) return false;
        Pawn->SetMaxShield(100.f);
        Pawn = PawnGuard.Get(); if (!Pawn || !ControllerGuard.Get()) return false;
        Pawn->SetHealth(100.f);
        Pawn = PawnGuard.Get(); if (!Pawn || !ControllerGuard.Get()) return false;
        Pawn->SetShield(0.f);
        Pawn = PawnGuard.Get(); if (!Pawn || !ControllerGuard.Get()) return false;
    }
    if (!Pawn->bIsSkydiving && !Pawn->bIsParachuteOpen)
    {
        BotPhysics::EnablePawn(Pawn); // Flight keeps its native movement mode.
        Pawn->BeginSkydiving(true);
    }
    Pawn = PawnGuard.Get();
    // Possession is not flight. Verify real movement/animation/glider state.
    return Pawn && ControllerGuard.Get() && PlayerGuard.Get() && !Pawn->IsDead() &&
           (Pawn->bIsSkydiving || Pawn->bIsParachuteOpen);
}
} // namespace NativeBotAircraft
