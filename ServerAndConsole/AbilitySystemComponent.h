#pragma once
#include "SyndicateRuntime.h"
namespace AbilitySystemComponent
{
inline void (*InputObserver)(UAbilitySystemComponent *,FGameplayAbilitySpecHandle,bool)=nullptr;
inline FGameplayAbilitySpec *FindAbilitySpecFromHandle(UAbilitySystemComponent *C, FGameplayAbilitySpecHandle H)
{
    if (C)
        for (auto &S : C->ActivatableAbilities.Items)
            if (S.Handle == H)
                return &S;
    return nullptr;
}
inline void InternalServerTryActiveAbility(UAbilitySystemComponent *C, FGameplayAbilitySpecHandle H, bool Pressed,
                                           const FPredictionKey &Key, const FGameplayEventData *Event)
{
    if (!C)
        return;
    auto S = FindAbilitySpecFromHandle(C, H);
    if (!S || !S->Ability)
    {
        C->ClientActivateAbilityFailed(H, Key.Current);
        return;
    }
    S->InputPressed = Pressed;
    C->ActivatableAbilities.MarkItemDirty(*S);
    UGameplayAbility *Instance = nullptr;
    if (!InternalTryActivateAbility(C, H, Key, &Instance, nullptr, Event))
    {
        C->ClientActivateAbilityFailed(H, Key.Current);
        // Activation can change the array: resolve the handle again before writing.
        if (auto Current = FindAbilitySpecFromHandle(C, H))
        {
            Current->InputPressed = false;
            C->ActivatableAbilities.MarkItemDirty(*Current);
        }
    }
    else if(InputObserver) InputObserver(C,H,Pressed);
}
inline void GiveAbility(UFortGameplayAbility *A, AFortPlayerState *P)
{
    if (!A || !P || !P->AbilitySystemComponent)
        return;
    auto C = P->AbilitySystemComponent;
    for (auto &S : C->ActivatableAbilities.Items)
        if (S.Ability && S.Ability->Class == A->Class)
            return;
    FGameplayAbilitySpec S{};
    AbilitySpecConstructor(&S, A, 1, -1, nullptr);
    GiveAbilityOG(C, &S.Handle, S);
}
struct GrantedSet
{
    Syndicate::Ref<AFortPlayerState> State;
    Syndicate::Ref<UFortAbilitySet> Set;
};
inline std::vector<GrantedSet> Sets;
inline void GiveAbilitySet(UFortAbilitySet *Set, AFortPlayerState *P)
{
    if (!Set || !P || !P->AbilitySystemComponent)
        return;
    std::erase_if(Sets, [](auto &G) { return !G.State.Get() || !G.Set.Get(); });
    for (auto &G : Sets)
        if (G.State.Get() == P && G.Set.Get() == Set)
            return;
    for (auto Class : Set->GameplayAbilities)
        if (auto C = Class.Get())
            GiveAbility(Cast<UFortGameplayAbility>(C->DefaultObject), P);
    for (auto &E : Set->GrantedGameplayEffects)
        if (E.GameplayEffect.Get())
            P->AbilitySystemComponent->BP_ApplyGameplayEffectToSelf(E.GameplayEffect.Get(), E.Level,
                                                                    P->AbilitySystemComponent->MakeEffectContext());
    Sets.push_back({Syndicate::Ref<AFortPlayerState>(P), Syndicate::Ref<UFortAbilitySet>(Set)});
}
inline void HookAll()
{
    HookVTable<UAbilitySystemComponent>(0xF6, InternalServerTryActiveAbility);
    HookVTable<UFortAbilitySystemComponent>(0xF6, InternalServerTryActiveAbility);
    HookVTable<UFortAbilitySystemComponentAthena>(0xF6, InternalServerTryActiveAbility);
}
} // namespace AbilitySystemComponent
