#pragma once
#include "FortInventory.h"
#include "SyndicateRuntime.h"
namespace BotReload
{
enum class Result { Ready, Waiting, NeedsAmmo };
inline Result Check(AController *Controller, AFortWeapon *Weapon, float &NextAttempt)
{
    if (!Controller || !Weapon) return Result::NeedsAmmo;
    if (Weapon->bIsReloadingWeapon) return Result::Waiting;
    if (Weapon->AmmoCount > 0) return Result::Ready;
    auto Ranged = Cast<UFortWeaponRangedItemDefinition>(Weapon->WeaponData);
    if (!Ranged) return Result::NeedsAmmo;
    Syndicate::Ref<AFortWeapon> Guard(Weapon);
    Syndicate::Ref<AController> ControllerGuard(Controller);
    auto Ammo = Ranged->GetAmmoWorldItemDefinition_BP();
    Controller = ControllerGuard.Get(); Weapon = Guard.Get();
    if (!Controller || !Weapon || !Ammo || FortInventory::CountDefinition(Controller,Ammo) <= 0)
        return Result::NeedsAmmo;
    float Now = Syndicate::Now();
    if (Now < NextAttempt) return Result::Waiting;
    float Duration = Weapon->GetReloadTime();
    Weapon = Guard.Get();
    if (!Weapon || !ControllerGuard.Get()) return Result::NeedsAmmo;
    // Reserve the full native animation time BEFORE its reentrant callback.
    NextAttempt = Now + (std::isfinite(Duration) ? std::clamp(Duration,0.5f,10.f) : 3.f) + 0.5f;
    Weapon->Reload();
    return Result::Waiting;
}
} // namespace BotReload
