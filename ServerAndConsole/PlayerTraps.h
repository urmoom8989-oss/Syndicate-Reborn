#pragma once
#include "FortInventory.h"
#include "SDK/SDK/FortniteGame_parameters.hpp"

namespace PlayerTraps
{
inline std::vector<FName> PlaceEvents,CreateEvents;
inline UFortTrapItemDefinition *Definition(UFortTrapItemDefinition *D,EBuildingAttachmentType Attachment)
{
    if(auto C=Cast<UFortContextTrapItemDefinition>(D))
    {
        if(Attachment==EBuildingAttachmentType::ATTACH_Wall) return C->WallTrap;
        if(Attachment==EBuildingAttachmentType::ATTACH_Ceiling) return C->CeilingTrap;
        if(Attachment==EBuildingAttachmentType::ATTACH_Floor) return C->FloorTrap;
        return nullptr;
    }
    return D;
}
inline AFortPlayerPawnAthena *OwningPawn(AActor *Actor)
{
    for(int Depth=0;Actor && Depth<8;++Depth)
    {
        if(auto P=Cast<AFortPlayerPawnAthena>(Actor)) return P;
        if(auto P=Cast<AFortPlayerPawnAthena>(Actor->Instigator)) return P;
        if(auto PC=Cast<AFortPlayerControllerAthena>(Actor)) return Cast<AFortPlayerPawnAthena>(PC->Pawn);
        auto Next=Actor->GetOwner();if(Next==Actor) break;Actor=Next;
    }
    return nullptr;
}
inline void RepairOwnership(ABuildingTrap *T)
{
    if(!T || T->IsActorBeingDestroyed()) return;
    auto P=OwningPawn(T);
    auto S=P ? Cast<AFortPlayerStateAthena>(P->PlayerState) : nullptr;
    if(!P || !S) return;
    T->Instigator=P;
    if(T->TeamIndex!=S->TeamIndex || T->Team!=static_cast<EFortTeam>(S->TeamIndex))
    {
        T->TeamIndex=S->TeamIndex;T->Team=static_cast<EFortTeam>(S->TeamIndex);
        T->OnRep_Team();T->ForceNetUpdate();
    }
    auto Name=T->GetName();
    std::transform(Name.begin(),Name.end(),Name.begin(),[](unsigned char C){return static_cast<char>(std::tolower(C));});
    bool Damaging=Name.contains("spike")||Name.contains("zippytrout")||Name.contains("dynamo")||Name.contains("zapper");
    if(Damaging)
    {
        T->TriggerFilter.bUseTrapsOwningPawnAsInstigator=true;
        T->TriggerFilter.bExcludeInstigator=true;
        T->TriggerFilter.bExcludePawnFriends=true;
        T->TriggerFilter.bConsiderFriendlyFireWhenExcludingFriends=false;
        T->TriggerFilter.bExcludePawnEnemies=false;
        T->TriggerFilter.bExcludeAllAI=false;
    }
    else if(Name.contains("launch")||Name.contains("jump")||Name.contains("bounce"))
    {
        // Phoebe pawns are players too. Native mobility traps must admit their
        // real overlaps, including the placer, without a damage-trap filter.
        T->TriggerFilter.bExcludeInstigator=false;T->TriggerFilter.bExcludePawnFriends=false;
        T->TriggerFilter.bExcludePawnEnemies=false;T->TriggerFilter.bExcludeAllAI=false;
    }
}
inline bool ValidRotation(const FRotator &R)
{return std::isfinite(R.Pitch)&&std::isfinite(R.Yaw)&&std::isfinite(R.Roll);}
inline bool SupportAllows(UFortTrapItemDefinition *D,ABuildingSMActor *B,AFortPlayerStateAthena *S,EBuildingAttachmentType Type)
{
    if(!D||!B||!S||B->bDestroyed||B->IsActorBeingDestroyed()) return false;
    if(B->bPlayerPlaced && B->TeamIndex!=S->TeamIndex && !D->bCanBePlacedOnEnemyBuildings) return false;
    if(Type==EBuildingAttachmentType::ATTACH_Wall) return B->BuildingType==EFortBuildingType::Wall;
    if(Type==EBuildingAttachmentType::ATTACH_Floor||Type==EBuildingAttachmentType::ATTACH_Ceiling)
        return B->BuildingType==EFortBuildingType::Floor ||
            (B->BuildingType==EFortBuildingType::Stairs && D->ShouldAllowStairsWhenAttachingToFloors());
    return false;
}
inline ABuildingTrap *Place(AFortDecoTool *Tool,const FVector &At,const FRotator &Rotation,ABuildingSMActor *Support,
                           EBuildingAttachmentType Type,const FVector *BuildAt=nullptr,const FRotator *BuildRotation=nullptr)
{
    auto P=OwningPawn(Tool);
    auto Controller=P ? P->Controller : nullptr;
    auto PC=Cast<AFortPlayerControllerAthena>(Controller);
    auto AI=Cast<AFortAthenaAIBotController>(Controller);
    auto PS=Controller ? Cast<AFortPlayerStateAthena>(Controller->PlayerState) : nullptr;
    auto Original=Tool ? Cast<UFortTrapItemDefinition>(Tool->ItemDefinition) : nullptr;
    if(auto Context=Cast<AFortDecoTool_ContextTrap>(Tool); !Original && Context) Original=Context->ContextTrapItemDefinition;
    auto D=Definition(Original,Type);
    auto Entry=Controller ? FortInventory::FindItemEntry(Controller,Tool->ItemEntryGuid) : nullptr;
    if(!P||(!PC&&!AI)||Controller->Pawn!=P||P->IsDead()||P->CurrentWeapon!=Tool||!PS||!D||!Entry||Entry->Count<1||
        Entry->ItemDefinition!=Original||!Syndicate::Finite(At)||!ValidRotation(Rotation)||P->IsInVehicle()) return nullptr;
    int NativeReach=D->GetMaxPlacementDistance();
    int Reach=NativeReach>0 ? std::clamp(NativeReach,256,4096) : 1536;
    if((At-P->K2_GetActorLocation()).Magnitude()>Reach+150.f) return nullptr;
    auto Class=D->GetBlueprintClass().Get();
    auto Default=Class ? Cast<ABuildingTrap>(Class->DefaultObject) : nullptr;
    if(!Default) return nullptr;
    FGuid TrapGuid=Entry->ItemGuid;
    ABuildingSMActor *Created=nullptr;
    UFortResourceItemDefinition *Cost=nullptr;
    if(BuildAt)
    {
        if(!BuildRotation||!Syndicate::Finite(*BuildAt)||!ValidRotation(*BuildRotation)||
            (*BuildAt-At).Magnitude()>900.f||(*BuildAt-P->K2_GetActorLocation()).Magnitude()>Reach+600.f) return nullptr;
        auto Material=PC ? PC->CurrentResourceType : EFortResourceType::Wood;
        bool WallType=Type==EBuildingAttachmentType::ATTACH_Wall;
        if(!WallType && Type!=EBuildingAttachmentType::ATTACH_Floor && Type!=EBuildingAttachmentType::ATTACH_Ceiling) return nullptr;
        UClass *Base=Material==EFortResourceType::Stone ? (WallType ? APBWA_S1_Solid_C::StaticClass() : APBWA_S1_Floor_C::StaticClass()) :
            Material==EFortResourceType::Metal ? (WallType ? APBWA_M1_Solid_C::StaticClass() : APBWA_M1_Floor_C::StaticClass()) :
            (WallType ? APBWA_W1_Solid_C::StaticClass() : APBWA_W1_Floor_C::StaticClass());
        Cost=UFortKismetLibrary::K2_GetResourceItemDefinition(Material);
        if(!Base||!Cost||(!(PC&&PC->bBuildFree) && FortInventory::CountDefinition(Controller,Cost)<10)) return nullptr;
        TArray<ABuildingSMActor *> Replace;char Reason=0;
        bool Blocked=CantBuild(UWorld::GetWorld(),Base,*BuildAt,*BuildRotation,false,&Replace,&Reason);
        bool Destructive=Replace.Num()>0;Replace.Free();
        if(Blocked||Destructive) return nullptr; // Never delete another build for a failed trap transaction.
        Created=SpawnActor<ABuildingSMActor>(*BuildAt,*BuildRotation,Controller,Base);
        if(!Created) return nullptr;
        Created->bPlayerPlaced=true;Created->TeamIndex=PS->TeamIndex;Created->Team=static_cast<EFortTeam>(PS->TeamIndex);
        Created->InitializeKismetSpawnedBuildingActor(Created,PC,true);Support=Created;
    }
    auto UndoSupport=[&](){if(Created && !Created->IsActorBeingDestroyed()) Created->K2_DestroyActor();};
    if(!SupportAllows(D,Support,PS,Type)) {UndoSupport();return nullptr;}
    FText Failure{};
    if(!Default->IsAcceptablePositionForPlacement(At,Rotation,Tool,true,&Failure)) {UndoSupport();return nullptr;}
    // Prevent a repeated predicted RPC from consuming another trap on one face.
    for(auto &Ref:Syndicate::ActorsOf<ABuildingTrap>())
        if(auto Existing=Ref.Get(); Existing && Existing->GetBuildingAttachedTo()==Support &&
            (Existing->K2_GetActorLocation()-At).Magnitude()<60.f)
        {UndoSupport();return nullptr;}
    FTransform Transform{};Transform.Scale3D=FVector(1,1,1);Transform.Translation=At;Transform.Rotation=FRotToQuat(Rotation);
    auto Spawn=UGameplayStatics::BeginDeferredActorSpawnFromClass(UWorld::GetWorld(),Class,Transform,
        ESpawnActorCollisionHandlingMethod::AlwaysSpawn,P);
    auto Trap=Cast<ABuildingTrap>(Spawn);
    if(!Trap) {if(Spawn) Spawn->K2_DestroyActor();UndoSupport();return nullptr;}
    Trap->Instigator=P;Trap->TrapData=D;Trap->bPlayerPlaced=true;
    Trap->TeamIndex=PS->TeamIndex;Trap->Team=static_cast<EFortTeam>(PS->TeamIndex);
    Trap=Cast<ABuildingTrap>(UGameplayStatics::FinishSpawningActor(Trap,Transform));
    if(!Trap) {UndoSupport();return nullptr;}
    Trap->SetAttachedTo(Support);
    Trap->SpawnedFromItemDefinition(D,1);
    Trap->InitializeKismetSpawnedBuildingActor(Trap,PC,true);
    RepairOwnership(Trap);
    bool Free=PC&&PC->bBuildFree;
    bool PaidSupport=!Created||Free||FortInventory::RemoveDefinition(Controller,Cost,10);
    bool Consume=Original->ShouldConsumeWhenPlaced();
    if(!PaidSupport || (Consume&&!FortInventory::RemoveGuid(Controller,TrapGuid,1)))
    {
        if(Created&&!Free&&PaidSupport) FortInventory::Add(Controller,Cost,10,0,true);
        Trap->K2_DestroyActor();UndoSupport();return nullptr;
    }
    Trap->ForceNetUpdate();
    Log("Native trap placed: "+Trap->GetName()+", team="+std::to_string(PS->TeamIndex));
    return Trap;
}
inline bool Handle(UObject *Object,UFunction *Function,void *Parameters)
{
    if(!Function||!Parameters) return false;
    bool PlaceEvent=std::find(PlaceEvents.begin(),PlaceEvents.end(),Function->Name)!=PlaceEvents.end();
    bool CreateEvent=std::find(CreateEvents.begin(),CreateEvents.end(),Function->Name)!=CreateEvents.end();
    if(!PlaceEvent&&!CreateEvent) return false;
    auto Tool=Cast<AFortDecoTool>(Object);
    if(!Tool || (!Cast<UFortTrapItemDefinition>(Tool->ItemDefinition) && !Cast<AFortDecoTool_ContextTrap>(Tool))) return false;
    auto W=UWorld::GetWorld();
    if(!W||!W->AuthorityGameMode||!W->NetDriver||W->NetDriver->ServerConnection) return false;
    // Projectile-created zapper tools are not inventory tools; keep their native
    // placement, then repair attribution when the resulting trap initializes.
    auto P=OwningPawn(Tool);
    if(!P||P->CurrentWeapon!=Tool) return false;
    if(PlaceEvent)
    {
        auto R=static_cast<SDK::Params::FortDecoTool_ServerSpawnDeco *>(Parameters);
        Place(Tool,R->Location,R->Rotation,R->AttachedActor,R->InBuildingAttachmentType);
    }
    else
    {
        auto R=static_cast<SDK::Params::FortDecoTool_ServerCreateBuildingAndSpawnDeco *>(Parameters);
        Place(Tool,R->Location,R->Rotation,nullptr,R->InBuildingAttachmentType,&R->BuildingLocation,&R->BuildingRotation);
    }
    return true;
}
inline void Observe(UObject *Object,UFunction *Function,void *,bool Before)
{
    if(!Before||!Function) return;
    auto T=Cast<ABuildingTrap>(Object);
    if(!T) return;
    static thread_local bool Busy=false;if(Busy) return;
    Busy=true;RepairOwnership(T);Busy=false;
}
inline void Initialise()
{
    FortInventory::EquipmentObserver=[](AFortPlayerPawnAthena *P,UFortWeaponItemDefinition *D)
    {
        auto Definition=Cast<UFortTrapItemDefinition>(D);
        auto Tool=P ? Cast<AFortDecoTool>(P->CurrentWeapon) : nullptr;
        if(!Definition||!Tool) return;
        if(Tool->ItemDefinition!=Definition) {Tool->ItemDefinition=Definition;Tool->OnRep_ItemDefinition();}
        if(auto Context=Cast<AFortDecoTool_ContextTrap>(Tool))
            if(auto C=Cast<UFortContextTrapItemDefinition>(Definition);C && Context->ContextTrapItemDefinition!=C)
                Context->SetContextTrapItemDefinition(C);
    };
    auto Add=[](std::vector<FName> &List,UClass *C,const char *ClassName,const char *FunctionName)
    {if(C) if(auto F=C->GetFunction(ClassName,FunctionName)) List.push_back(F->Name);};
    Add(PlaceEvents,AFortDecoTool::StaticClass(),"FortDecoTool","ServerSpawnDeco");
    Add(CreateEvents,AFortDecoTool::StaticClass(),"FortDecoTool","ServerCreateBuildingAndSpawnDeco");
    Add(PlaceEvents,AFortDecoTool_ContextTrap::StaticClass(),"FortDecoTool_ContextTrap","ServerSpawnDeco_Implementation");
    Add(CreateEvents,AFortDecoTool_ContextTrap::StaticClass(),"FortDecoTool_ContextTrap","ServerCreateBuildingAndSpawnDeco_Implementation");
}
}
