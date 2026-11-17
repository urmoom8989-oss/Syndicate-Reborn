#pragma once
// Included in PlayerBots namespace. Observe a successfully activated primary
// gun ability even when client-owned weapons omit server magazine/fire fields.
inline unsigned NativeAbilityInputs=0,NativeAbilityGunInputs=0;
inline void ObserveAbilityFire(UAbilitySystemComponent *Component,FGameplayAbilitySpecHandle Handle,bool Pressed)
{
    if(!Pressed || !Syndicate::Ref<UAbilitySystemComponent>(Component).Get()) return;
    ++NativeAbilityInputs;
    auto Spec=AbilitySystemComponent::FindAbilitySpecFromHandle(Component,Handle);
    if(!Spec || !Spec->Ability) return;
    auto AbilityClass=Spec->Ability->Class;
    for(auto Ref:PawnCache)
        if(auto Pawn=Ref.Get();Pawn && !Pawn->IsDead() && !Pawn->IsActorBeingDestroyed())
        {
            auto State=Syndicate::As<AFortPlayerStateAthena>(Pawn->PlayerState);
            auto Weapon=Pawn->CurrentWeapon;
            if(!State || State->AbilitySystemComponent!=Component || State->bInAircraft || !Weapon ||
               !Syndicate::Ref<AFortWeapon>(Weapon).Get() || !IsGun(Weapon->WeaponData)) continue;
            auto Definition=Syndicate::As<UFortWeaponRangedItemDefinition>(Weapon->WeaponData);
            if(!Definition || Definition->GetPrimaryFireAbility().Get()!=AbilityClass) return;
            ++NativeAbilityGunInputs;
            FVector Start{};FRotator Eyes{};Pawn->GetActorEyesViewPoint(&Start,&Eyes);
            auto Direction=UKismetMathLibrary::GetForwardVector(Pawn->GetBaseAimRotation());
            if(!Syndicate::Finite(Start) || !Syndicate::Finite(Direction)) return;
            float N=Syndicate::Now();
            std::erase_if(RecentShots,[N](const auto &E){return !E.Source.Get() || N-E.Time>1.f;});
            for(auto &E:RecentShots) if(E.Source.Get()==Pawn && N-E.Time<.05f) return;
            if(RecentShots.size()>=64) RecentShots.erase(RecentShots.begin());
            RecentShots.push_back({{Pawn},Start,Direction,N});ObserveSound(Pawn,Start,3500.f);return;
        }
}
