#pragma once
#include "FortInventory.h"
#include "SyndicateRuntime.h"
namespace BotInteraction
{
inline bool OpenDoor(AController *Controller,AFortPlayerPawnAthena *Pawn,ABuildingSMActor *Building,float &NextAttempt)
{
    auto Wall=Syndicate::As<ABuildingWall>(Building);
    if(!Controller || !Pawn || Pawn->IsDead() || !Wall || Wall->bDestroyed || Wall->IsActorBeingDestroyed()) return false;
    if(!Wall->DoorComponent && !Wall->SlidingDoorComponent && !Wall->DoubleDoorComponent) return false;
    if(Wall->IsDoorOpen()) return false; // Never toggle an already open door shut.
    auto At=Wall->DoorBoxComponent ? Wall->DoorBoxComponent->K2_GetComponentLocation() :
        (Wall->DoorComponent ? Wall->DoorComponent->K2_GetComponentLocation() :
         (Wall->SlidingDoorComponent ? Wall->SlidingDoorComponent->K2_GetComponentLocation() :
          (Wall->DoubleDoorComponent ? Wall->DoubleDoorComponent->K2_GetComponentLocation() : Wall->K2_GetActorLocation())));
    auto Here=Pawn->K2_GetActorLocation();
    if(!Syndicate::Finite(At) || Syndicate::Distance2D(At,Here)>350.f || std::abs(At.Z-Here.Z)>250.f) return false;
    float Now=Syndicate::Now();
    if(Now<NextAttempt) return false;
    NextAttempt=Now+0.8f;
    bool Attempted=false;
    // Automatic doors use their near-door handler independently of the
    // player interaction prompt; bots may not qualify for that prompt.
    if(Wall->DoorBoxComponent && Pawn->CapsuleComponent)
    {
        FHitResult Hit{};
        // Native wall doors also expose their normal near-door input handler.
        // Invoke that door handler at interaction range; never rotate the mesh,
        // change the pawn collision or directly flip replicated open flags.
        Wall->OnNearDoorTriggerTouched(Wall->DoorBoxComponent,Pawn,Pawn->CapsuleComponent,0,false,Hit);
        Attempted=true;
    }
    if(!Wall->IsDoorOpen() && Wall->bAllowInteract && Wall->BlueprintCanInteract(Pawn))
    {Wall->BlueprintOnInteract(Pawn);Attempted=true;}
    return Attempted; // Traversal still verifies the actual open state and expires if the engine rejects it.
}
inline bool UseSpec(const FGameplayAbilitySpec &Spec,AFortWeapon *Weapon)
{
    if(!Spec.ActiveCount || !Spec.Ability || !Weapon) return false;
    if(Spec.SourceObject==Weapon || Spec.SourceObject==Weapon->WeaponData) return true;
    if(Spec.SourceObject) return false;
    auto Name=Spec.Ability->GetName();
    return Name.contains("Consumable") || Name.contains("Shield") || Name.contains("Medkit") ||
           Name.contains("Bandage") || Name.contains("Slurp") || Name.contains("Chug");
}
inline bool ConsumableActive(AFortPlayerStateAthena *State,AFortPlayerPawnAthena *Pawn,const FGuid &Guid)
{
    auto Weapon=Pawn ? Pawn->CurrentWeapon : nullptr;
    if(!State || !State->AbilitySystemComponent || !Weapon || Weapon->ItemEntryGuid!=Guid) return false;
    for(auto &Spec:State->AbilitySystemComponent->ActivatableAbilities.Items)
        if(UseSpec(Spec,Weapon)) return true;
    return false;
}
inline void CancelConsumable(AFortPlayerStateAthena *State,AFortPlayerPawnAthena *Pawn,const FGuid &Guid)
{
    auto Weapon=Pawn ? Pawn->CurrentWeapon : nullptr;
    if(!State || !State->AbilitySystemComponent || !Weapon || Weapon->ItemEntryGuid!=Guid) return;
    std::vector<Syndicate::Ref<UGameplayAbility>> Instances;
    for(auto &Spec:State->AbilitySystemComponent->ActivatableAbilities.Items)
        if(UseSpec(Spec,Weapon))
        {
            for(auto Ability:Spec.NonReplicatedInstances) Instances.emplace_back(Ability);
            for(auto Ability:Spec.ReplicatedInstances) Instances.emplace_back(Ability);
        }
    for(auto Ref:Instances)
        if(auto Ability=Ref.Get(); Ability && !Ability->IsDefaultObject()) Ability->K2_CancelAbility();
}
inline UClass *EditClass(EFortResourceType Material,bool Passage,bool Reset=false)
{
    if(Material==EFortResourceType::Wood)
        return Reset ? APBWA_W1_Solid_C::StaticClass() : (Passage ? APBWA_W1_Archway_C::StaticClass() : APBWA_W1_WindowC_C::StaticClass());
    if(Material==EFortResourceType::Stone)
        return Reset ? APBWA_S1_Solid_C::StaticClass() : (Passage ? APBWA_S1_Archway_C::StaticClass() : APBWA_S1_WindowsC_C::StaticClass());
    if(Material==EFortResourceType::Metal)
        return Reset ? APBWA_M1_Solid_C::StaticClass() : (Passage ? APBWA_M1_Archway_C::StaticClass() : APBWA_M1_WindowC_C::StaticClass());
    return nullptr;
}
struct EditSession
{
    Syndicate::Ref<ABuildingSMActor> Target,Result;
    float CommitAt=0.f,Deadline=0.f;
    bool Passage=false,Reset=false;
    bool Active() const { return Target.Pointer!=nullptr; }
    bool Allowed(AController *Controller,AFortPlayerPawnAthena *Pawn,AFortPlayerStateAthena *State,ABuildingSMActor *B)
    {
        return Controller && Pawn && !Pawn->IsDead() && State && B && !B->bDestroyed && !B->IsActorBeingDestroyed() &&
            B->bPlayerPlaced && B->GetOwner()==Controller && B->TeamIndex==State->TeamIndex &&
            B->IsA(ABuildingWall::StaticClass()) && B->GetDistanceTo(Pawn)<550.f &&
            (!B->EditingPlayer || B->EditingPlayer==State);
    }
    void Clear(AFortPlayerPawnAthena *Pawn,AFortPlayerStateAthena *State)
    {
        if(auto B=Target.Get(); B && B->EditingPlayer==State)
        { B->EditingPlayer=nullptr; B->OnRep_EditingPlayer(); B->ForceNetUpdate(); }
        if(Pawn)
            if(auto Tool=Syndicate::As<AFortWeap_EditingTool>(Pawn->CurrentWeapon); Tool && Tool->EditActor==Target.Pointer)
            { Tool->EditActor=nullptr; Tool->OnRep_EditActor(); }
        Target.Clear();
    }
    bool Begin(AController *Controller,AFortPlayerPawnAthena *Pawn,AFortPlayerStateAthena *State,ABuildingSMActor *B,bool MakePassage,bool MakeReset=false)
    {
        if(Active() || !Allowed(Controller,Pawn,State,B) || !EditClass(B->ResourceType,MakePassage,MakeReset)) return false;
        auto Entry=FortInventory::FindItemEntry(Controller,EditTool);
        if(!Entry || !FortInventory::Equip(Controller,Pawn,Entry->ItemGuid)) return false;
        auto Tool=Syndicate::As<AFortWeap_EditingTool>(Pawn->CurrentWeapon);
        if(!Tool) return false;
        Target.Set(B); Result.Clear(); Passage=MakePassage; Reset=MakeReset;
        B->SetNetDormancy(ENetDormancy::DORM_Awake);
        B->EditingPlayer=State; B->OnRep_EditingPlayer(); B->ForceNetUpdate();
        Tool->EditActor=B; Tool->OnRep_EditActor();
        CommitAt=Syndicate::Now()+Syndicate::Between(MakeReset ? .08f : .12f,MakeReset ? .14f : .22f); Deadline=CommitAt+1.2f;
        return true;
    }
    bool Tick(AController *Controller,AFortPlayerPawnAthena *Pawn,AFortPlayerStateAthena *State)
    {
        auto B=Target.Get(); auto Tool=Pawn ? Syndicate::As<AFortWeap_EditingTool>(Pawn->CurrentWeapon) : nullptr;
        float Now=Syndicate::Now();
        if(!Allowed(Controller,Pawn,State,B) || !Tool || Tool->EditActor!=B || Now>Deadline)
        { Clear(Pawn,State); return false; }
        if(Now<CommitAt) return true;
        auto Class=EditClass(B->ResourceType,Passage,Reset);
        auto Default=Class ? Syndicate::As<ABuildingSMActor>(Class->DefaultObject) : nullptr;
        if(!Default || Default->ResourceType!=B->ResourceType || Default->BuildingType!=B->BuildingType)
        { Clear(Pawn,State); return false; }
        auto Team=B->Team; auto TeamIndex=B->TeamIndex;
        Syndicate::Ref<AController> Guard(Controller); Syndicate::Ref<AFortPlayerStateAthena> StateGuard(State);
        Syndicate::Ref<AFortPlayerPawnAthena> PawnGuard(Pawn);
        // Read-only inspection of the 10.40 native routine confirmed the optional
        // player-controller argument is null-checked for its team fallback. The
        // existing structure already carries the bot team. Never cast an AI
        // controller into a player controller: their layouts differ.
        B->EditingPlayer=nullptr;
        auto Edited=ReplaceBuildingActor(B,1,Class,0,0,false,nullptr);
        Clear(PawnGuard.Get(),StateGuard.Get());
        Controller=Guard.Get(); State=StateGuard.Get();
        if(Edited && Controller && State)
        {
            Edited->SetOwner(Controller); Edited->bPlayerPlaced=true;
            Edited->Team=Team; Edited->TeamIndex=TeamIndex;
            Edited->EditingPlayer=nullptr; Edited->OnRep_EditingPlayer(); Edited->ForceNetUpdate();
            Result.Set(Edited);
        }
        return false;
    }
};
} // namespace BotInteraction
