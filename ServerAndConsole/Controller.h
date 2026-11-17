#pragma once
#include "framework.h"
#include "FortInventory.h"
#include "AbilitySystemComponent.h"
#include "Looting.h"
#include "WorldSetup.h"
#include "ServerBotManager.h"
#include "GameMode.h"
#include "Bots.h"
#include "PE.h"
#include "KillerSpectating.h"
#include "PlayerVehicles.h"
#include "PlayerTraps.h"

namespace Controller
{
inline AFortPlayerPawnAthena *PlayerPawn(AFortPlayerController *PC)
{
    // Pawn is the authoritative possession. MyFortPawn can lag during native
    // loading/replacement and must not block equipment, drops or building.
    return PC ? Cast<AFortPlayerPawnAthena>(PC->Pawn) : nullptr;
}
void (*ServerAcknowledgePossessionOG)(APlayerController *PC, APawn *P);
void ServerAcknowledgePossession(APlayerController *PC, APawn *P)
{
    if (!PC || PC->Pawn != P)
        return;
    Syndicate::Ref<APlayerController> Guard(PC);
    PC->AcknowledgedPawn = P;
    if (ServerAcknowledgePossessionOG)
        ServerAcknowledgePossessionOG(PC, P);
    PC = Guard.Get();
    if (!PC)
        return;
    auto FortPC = Cast<AFortPlayerController>(PC);
    auto Pawn = PlayerPawn(FortPC);
    if (Pawn && !Pawn->IsDead() && !Pawn->CurrentWeapon)
        FortInventory::EquipPickaxe(FortPC, Pawn);
}

void (*ServerReadyToStartMatchOG)(AFortPlayerControllerAthena *PC);
void ServerReadyToStartMatch(AFortPlayerControllerAthena *PC)
{
    auto World = UWorld::GetWorld();
    auto GameMode = World ? Cast<AFortGameModeAthena>(World->AuthorityGameMode) : nullptr;
    if (!PC || !GameMode)
    {
        if (ServerReadyToStartMatchOG)
            ServerReadyToStartMatchOG(PC);
        return;
    }
    Log("Native ready-to-start request received.");

    Syndicate::Ref<AFortPlayerControllerAthena> Guard(PC);
    if (ServerReadyToStartMatchOG)
        ServerReadyToStartMatchOG(PC);
    auto LivePC = Guard.Get();
    if (!LivePC)
        return;
    Log(std::string("Native ready-to-start request completed: pawn=") + (LivePC->Pawn ? "yes" : "no") +
        ", ready=" + (LivePC->bReadyToStartMatch ? "yes" : "no") + ", serverLoaded=" +
        (LivePC->bHasServerFinishedLoading ? "yes" : "no"));
    // A real native pawn is required before map-wide work or bot population.
    if (LivePC->Pawn && World == UWorld::GetWorld())
        WorldSetup::Request(World);
}

inline void (*ServerSetClientHasFinishedLoadingOG)(AFortPlayerControllerAthena *, bool) = nullptr;
void ServerSetClientHasFinishedLoading(AFortPlayerControllerAthena *PC, bool Loaded)
{
    if (PC)
        Log(std::string("Native client loading acknowledgement: ") + (Loaded ? "loaded" : "loading"));
    if (ServerSetClientHasFinishedLoadingOG)
        ServerSetClientHasFinishedLoadingOG(PC, Loaded);
    if (PC)
        Log(std::string("Native loading handshake completed: ready=") + (PC->bReadyToStartMatch ? "yes" : "no") +
            ", pawn=" + (PC->Pawn ? "yes" : "no"));
}

void (*ServerExecuteInventoryItemOG)(AFortPlayerController *This, const FGuid &ItemGuid);
void ServerExecuteInventoryItem(AFortPlayerController *PC, const FGuid &Guid)
{
    auto Pawn = PlayerPawn(PC);
    if (!Pawn || Pawn->IsDead() || !PlayerVehicles::CanUseInventory(Pawn) || !FortInventory::FindItemEntry(PC, Guid))
        return;
    FGuid Selected = Guid;
    Syndicate::Ref<AFortPlayerController> ControllerGuard(PC);
    if (ServerExecuteInventoryItemOG)
        ServerExecuteInventoryItemOG(PC, Selected);
    PC = ControllerGuard.Get();
    Pawn = PlayerPawn(PC);
    if (Pawn && !Pawn->IsDead())
    {
        bool Equipped = FortInventory::Equip(PC, Pawn, Selected);
        static float NextReport = 0.f;
        float Now = Syndicate::Now();
        if (Now >= NextReport)
        {
            NextReport = Now + 2.f;
            Log(std::string("Player equipment: requested GUID ") + (Equipped ? "held" : "failed") +
                ", authoritativePawn=yes, weapon=" + (Pawn->CurrentWeapon ? Pawn->CurrentWeapon->GetName() : "none"));
        }
    }
}

void ServerCheat(AFortPlayerControllerAthena *PC, FString &Msg)
{
    if (!PC || !PC->Pawn || !UWorld::GetWorld() || Globals::bIsProdServer)
        return;

    auto Gamemode = (AFortGameModeAthena *)UWorld::GetWorld()->AuthorityGameMode;
    auto GameState = (AFortGameStateAthena *)UWorld::GetWorld()->GameState;

    std::string Command = Msg.ToString();
    Log(Command);

    if (Command == "SpawnBot")
    {
        Bots::SpawnPlayerBot(PC->Pawn, PlayerBots::EBotState::Landed);
    }

    if (Command == "StartAircraftEarly")
    {
        UKismetSystemLibrary::ExecuteConsoleCommand(UWorld::GetWorld(), L"startaircraft", PC); // i think???
    }

    if (Command == "StartEvent")
    {
        UObject *TheEnd = nullptr;

        UClass *Scripting = StaticLoadObject<UClass>(
            "/Game/Athena/Prototype/Blueprints/NightNight/BP_NightNight_Scripting.BP_NightNight_Scripting_C");

        TArray<AActor *> Scripts;
        UGameplayStatics::GetAllActorsOfClass(UWorld::GetWorld(), Scripting, &Scripts);

        if (Scripts.Num() > 0)
        {
            Log("can we not crash pls???");
            TheEnd = Scripts[0];
        }

        auto LoadLevel =
            StaticLoadObject<UFunction>("/Game/Athena/Prototype/Blueprints/NightNight/"
                                        "BP_NightNight_Scripting.BP_NightNight_Scripting_C.LoadNightNightLevel");
        if (TheEnd && LoadLevel)
            TheEnd->ProcessEvent(LoadLevel,
                                 []()
                                 {
                                     static bool b = true;
                                     return &b;
                                 }());

        UFunction *StartEventFunc =
            TheEnd && TheEnd->Class ? TheEnd->Class->GetFunction("BP_NightNight_Scripting_C", "startevent") : nullptr;
        if (TheEnd && StartEventFunc)
            TheEnd->ProcessEvent(StartEventFunc,
                                 []()
                                 {
                                     static float f = 0.f;
                                     return &f;
                                 }());
    }
}

void ServerAttemptInventoryDrop(AFortPlayerController *PC, const FGuid &Guid, int32 Count)
{
    auto E = FortInventory::FindItemEntry(PC, Guid);
    auto P = PlayerPawn(PC);
    auto D = E ? Cast<UFortWorldItemDefinition>(E->ItemDefinition) : nullptr;
    if (!P || P->IsDead() || !E || !D || !D->bCanBeDropped || Count <= 0 || Count > E->Count)
        return;
    auto Copy = *E;
    bool WasEquipped = P->CurrentWeapon && P->CurrentWeapon->ItemEntryGuid == Copy.ItemGuid;
    Syndicate::Ref<AFortPlayerController> ControllerGuard(PC);
    Syndicate::Ref<AFortPlayerPawnAthena> PawnGuard(P);
    auto Drop = SpawnPickup(Copy.ItemDefinition, Count, Copy.LoadedAmmo,
                            P->K2_GetActorLocation() + P->GetActorForwardVector() * 100.f,
                            EFortPickupSourceTypeFlag::Tossed, EFortPickupSpawnSource::Unset, P);
    if (!Drop)
        return;
    if (!FortInventory::RemoveGuid(PC, Copy.ItemGuid, Count))
    {
        Drop->K2_DestroyActor();
        return;
    }
    PC = ControllerGuard.Get();
    P = PawnGuard.Get();
    if (PC && P && !P->IsDead() && WasEquipped && !FortInventory::FindItemEntry(PC, Copy.ItemGuid))
        FortInventory::EquipPickaxe(PC, P);
}
inline UFunction *InventoryDropEvent = nullptr;
inline bool InterceptInventoryDrop(UObject *Object, UFunction *Function, void *Parameters)
{
    if (!InventoryDropEvent || !Function || Function->Name != InventoryDropEvent->Name || !Parameters) return false;
    auto W = UWorld::GetWorld();
    auto PC = Cast<AFortPlayerController>(Object);
    if (!PC || !W || !W->AuthorityGameMode || !W->NetDriver || W->NetDriver->ServerConnection) return false;
    auto Request = static_cast<SDK::Params::FortPlayerController_ServerAttemptInventoryDrop *>(Parameters);
    // The native profile validator rejects locally managed item GUIDs before
    // the hooked implementation runs. This exact RPC uses the same ownership,
    // count, drop-permission and genuine-pawn guards as our transaction above.
    // Stale UI requests are ignored; every other RPC keeps native validation.
    ServerAttemptInventoryDrop(PC, Request->ItemGuid, Request->Count);
    return true;
}

void (*ServerCreateBuildingActorOG)(AFortPlayerControllerAthena *PC, FCreateBuildingActorData &CreateBuildingData);
void ServerCreateBuildingActor(AFortPlayerControllerAthena *PC, FCreateBuildingActorData &Data)
{
    auto Pawn = PlayerPawn(PC);
    auto W = UWorld::GetWorld();
    auto PS = PC ? Cast<AFortPlayerStateAthena>(PC->PlayerState) : nullptr;
    if (!W || !PC || !Pawn || Pawn->IsDead() || !PS || !PC->BroadcastRemoteClientInfo ||
        !Syndicate::Finite(Data.BuildLoc))
        return;
    auto Class = PC->BroadcastRemoteClientInfo->RemoteBuildableClass.Get();
    auto CDO = Class ? Cast<ABuildingSMActor>(Class->DefaultObject) : nullptr;
    if (!CDO)
        return;
    auto Resource = UFortKismetLibrary::K2_GetResourceItemDefinition(CDO->ResourceType);
    auto E = FortInventory::FindItemEntry(PC, Resource);
    PC->bBuildFree=false; // This server hosts paid Battle Royale building.
    if (!E || E->Count < 10)
        return;
    FGuid Guid = E ? E->ItemGuid : FGuid{};
    TArray<ABuildingSMActor *> Remove;
    char Restriction = 0;
    if (CantBuild(W, Class, Data.BuildLoc, Data.BuildRot, Data.bMirrored, &Remove, &Restriction))
    {
        Remove.Free();
        return;
    }
    auto Actor = SpawnActor<ABuildingSMActor>(Data.BuildLoc, Data.BuildRot, PC, Class);
    if (!Actor)
    {
        Remove.Free();
        return;
    }
    if (!PC->bBuildFree && !FortInventory::RemoveGuid(PC, Guid, 10))
    {
        Actor->K2_DestroyActor();
        Remove.Free();
        return;
    }
    Actor->bPlayerPlaced = true;
    Actor->InitializeKismetSpawnedBuildingActor(Actor, PC, true);
    Actor->TeamIndex = PS->TeamIndex;
    Actor->Team = static_cast<EFortTeam>(PS->TeamIndex);
    Actor->ForceNetUpdate();
    PlayerBots::ObserveBuild(Actor);
    for (auto B : Remove)
        if (B && !B->IsActorBeingDestroyed())
            B->K2_DestroyActor();
    Remove.Free();
}

void ServerBeginEditingBuildingActor(AFortPlayerControllerAthena *PC, ABuildingSMActor *BuildingActorToEdit)
{
    auto Pawn = PlayerPawn(PC);
    if (!PC || !PC->PlayerState || !PC->WorldInventory || !Syndicate::Ref<ABuildingSMActor>(BuildingActorToEdit).Get() ||
        BuildingActorToEdit->bDestroyed || BuildingActorToEdit->IsActorBeingDestroyed() ||
        BuildingActorToEdit->TeamIndex != ((AFortPlayerStateAthena *)PC->PlayerState)->TeamIndex ||
        !BuildingActorToEdit->bPlayerPlaced || !Pawn || Pawn->IsDead() ||
        (BuildingActorToEdit->EditingPlayer && BuildingActorToEdit->EditingPlayer != PC->PlayerState))
        return;

    AFortPlayerStateAthena *PlayerState = (AFortPlayerStateAthena *)PC->PlayerState;
    for (auto Entry : PC->WorldInventory->Inventory.ReplicatedEntries)
    {
        if (Entry.Count > 0 && Cast<UFortEditToolItemDefinition>(Entry.ItemDefinition))
        {
            FortInventory::Equip(PC, Pawn, Entry.ItemGuid);
            break;
        }
    }

    if (!Cast<AFortWeap_EditingTool>(Pawn->CurrentWeapon))
        return;

    BuildingActorToEdit->SetNetDormancy(ENetDormancy::DORM_Awake);
    BuildingActorToEdit->EditingPlayer = PlayerState;
    AFortWeap_EditingTool *EditTool = (AFortWeap_EditingTool *)Pawn->CurrentWeapon;
    EditTool->EditActor = BuildingActorToEdit;
    EditTool->OnRep_EditActor();
    BuildingActorToEdit->ForceNetUpdate();
}

void ServerEndEditingBuildingActor(AFortPlayerControllerAthena *PC, ABuildingSMActor *Requested)
{
    auto Pawn = PlayerPawn(PC);
    auto State = PC ? Cast<AFortPlayerStateAthena>(PC->PlayerState) : nullptr;
    if (!Pawn || Pawn->IsDead() || !State) return;
    // Replacement destroys the original actor; the client's final end request
    // can still reference it. Only release this player's owned edit state.
    auto ClearOwned = [&](ABuildingSMActor *Actor)
    {
        auto B = Syndicate::Ref<ABuildingSMActor>(Actor).Get();
        if (!B || B->IsActorBeingDestroyed() || B->bDestroyed || !B->bPlayerPlaced ||
            B->TeamIndex != State->TeamIndex || B->EditingPlayer != State) return;
        B->EditingPlayer = nullptr;
        B->SetNetDormancy(ENetDormancy::DORM_DormantAll);
        B->ForceNetUpdate();
    };
    ClearOwned(Requested);
    if (auto Tool = Cast<AFortWeap_EditingTool>(Pawn->CurrentWeapon))
    {
        ClearOwned(Tool->EditActor);
        Tool->EditActor = nullptr;
        Tool->OnRep_EditActor();
    }
}

void ServerEditBuildingActor(AFortPlayerControllerAthena *PC, ABuildingSMActor *Requested,
                             TSubclassOf<ABuildingSMActor> NewBuildingClass, uint8 RotationIterations, bool bMirrored)
{
    auto Pawn = PlayerPawn(PC);
    auto State = PC ? Cast<AFortPlayerStateAthena>(PC->PlayerState) : nullptr;
    auto Building = Syndicate::Ref<ABuildingSMActor>(Requested).Get();
    auto Class = NewBuildingClass.Get();
    auto Default = Class ? Cast<ABuildingSMActor>(Class->DefaultObject) : nullptr;
    if (!Pawn || Pawn->IsDead() || !State || !Building || Building->IsActorBeingDestroyed() ||
        Building->bDestroyed || !Building->bPlayerPlaced || Building->TeamIndex != State->TeamIndex ||
        Building->EditingPlayer != State || !Default || Default->BuildingType != Building->BuildingType ||
        Default->ResourceType != Building->ResourceType || RotationIterations > 3) return;
    Syndicate::Ref<AFortPlayerControllerAthena> ControllerGuard(PC);
    Syndicate::Ref<AFortPlayerStateAthena> StateGuard(State);
    Syndicate::Ref<ABuildingSMActor> BuildingGuard(Building);
    auto TeamIndex = Building->TeamIndex;
    auto Team = Building->Team;
    Building->EditingPlayer = nullptr;
    auto Edited = ReplaceBuildingActor(Building, 1, Class, 0, RotationIterations, bMirrored, PC);
    PC = ControllerGuard.Get(); State = StateGuard.Get();
    if (!PC || !State) return;
    if (!Edited)
    {
        if (auto Original = BuildingGuard.Get(); Original && !Original->bDestroyed && !Original->IsActorBeingDestroyed())
            Original->EditingPlayer = State;
        return; // A rejected replacement retains the original edit session.
    }
    Edited->bPlayerPlaced = true;
    Edited->TeamIndex = TeamIndex; Edited->Team = Team;
    Edited->EditingPlayer = nullptr; // Confirmation ends the session; a replacement must be editable again.
    Edited->ForceNetUpdate();
    if (auto P = PlayerPawn(PC))
        if (auto Tool = Cast<AFortWeap_EditingTool>(P->CurrentWeapon))
        {
            Tool->EditActor = nullptr;
            Tool->OnRep_EditActor();
        }
}
inline UFunction *EndEditingEvent = nullptr;
inline UFunction *BeginEditingEvent = nullptr;
inline bool InterceptLocalEvents(UObject *Object, UFunction *Function, void *Parameters)
{
    if(PlayerTraps::Handle(Object,Function,Parameters)) return true;
    if (InterceptInventoryDrop(Object, Function, Parameters)) return true;
    if (!Function || !Parameters) return false;
    bool Begin=BeginEditingEvent && Function->Name==BeginEditingEvent->Name;
    bool End=EndEditingEvent && Function->Name==EndEditingEvent->Name;
    if(!Begin && !End) return false;
    auto W = UWorld::GetWorld();
    auto PC = Cast<AFortPlayerControllerAthena>(Object);
    if (!PC || !W || !W->AuthorityGameMode || !W->NetDriver || W->NetDriver->ServerConnection) return false;
    if(Begin)
    {
        auto Request=static_cast<SDK::Params::FortPlayerController_ServerBeginEditingBuildingActor *>(Parameters);
        // An unresolved/removed predicted actor is a rejected gameplay action,
        // not a reason to disconnect the local match. The implementation still
        // checks live actor, native edit tool, team, placement and edit ownership.
        ServerBeginEditingBuildingActor(PC,Request->BuildingActorToEdit);
        return true;
    }
    auto Request = static_cast<SDK::Params::FortPlayerController_ServerEndEditingBuildingActor *>(Parameters);
    // The old native validator rejects a completed/replaced custom edit before
    // the implementation is reached. Handle only this cleanup RPC with owned
    // player/team guards; stale completion has no destructive side effects.
    ServerEndEditingBuildingActor(PC, Request->BuildingActorToStopEditing);
    return true;
}

void ServerRepairBuildingActor(AFortPlayerControllerAthena *PC, ABuildingSMActor *BuildingActorToRepair)
{
    auto FortKismet = (UFortKismetLibrary *)UFortKismetLibrary::StaticClass()->DefaultObject;
    if (!PC || !PC->PlayerState || !BuildingActorToRepair || BuildingActorToRepair->bDestroyed ||
        !BuildingActorToRepair->bPlayerPlaced ||
        BuildingActorToRepair->TeamIndex != ((AFortPlayerStateAthena *)PC->PlayerState)->TeamIndex)
        return;

    if (BuildingActorToRepair->EditingPlayer)
    {
        return;
    }

    float BuildingHealthPercent = BuildingActorToRepair->GetHealthPercent();
    float BuildingCost = 10;
    float RepairCostMultiplier = 0.75;

    float BuildingHealthPercentLost = 1.0f - BuildingHealthPercent;
    float RepairCostUnrounded = (BuildingCost * BuildingHealthPercentLost) * RepairCostMultiplier;
    float RepairCost = std::floor(RepairCostUnrounded > 0 ? RepairCostUnrounded < 1 ? 1 : RepairCostUnrounded : 0);
    if (RepairCost <= 0)
        return;

    auto ResourceDef = FortKismet->K2_GetResourceItemDefinition(BuildingActorToRepair->ResourceType);
    if (!ResourceDef)
        return;

    if (!PC->bBuildFree)
    {
        auto E = FortInventory::FindItemEntry(PC, ResourceDef);
        if (!E || E->Count < (int)RepairCost)
            return;
        if (!FortInventory::RemoveGuid(PC, E->ItemGuid, (int)RepairCost))
            return;
    }

    BuildingActorToRepair->RepairBuilding(PC, (int)RepairCost);
}

void (*ClientOnPawnDiedOG)(AFortPlayerControllerZone *PC, const FFortPlayerDeathReport &DeathReport);
void ClientOnPawnDied(AFortPlayerControllerAthena *PC, const FFortPlayerDeathReport &Report)
{
    auto Pawn = PlayerPawn(PC);
    
    Syndicate::Ref<AFortPlayerPawnAthena> Guard(Pawn);
    Syndicate::Ref<AFortPlayerControllerAthena> ControllerGuard(PC);
    auto Killer = Cast<AFortPlayerStateAthena>(Report.KillerPlayerState);
    Syndicate::Ref<AFortPlayerStateAthena> KillerGuard(Killer);
    KillerSpectating::Prepare(PC, Killer);

    if (ClientOnPawnDiedOG)
        ClientOnPawnDiedOG(PC, Report);
    if (auto P = Guard.Get())
        Elimination::Finish(P);
    KillerSpectating::Apply(ControllerGuard.Get(), Guard.Get(), KillerGuard.Get());
    // Native reporting owns rewards/stats and drops. Do not index ItemInstances with ReplicatedEntries' index.
}

inline void (*ServerSetInAircraftOG)(AFortPlayerStateAthena *, bool) = nullptr;
void ServerSetInAircraft(AFortPlayerStateAthena *State, bool InAircraft)
{
    if (!State)
        return;
    bool Entering = InAircraft && !State->bInAircraft;
    Syndicate::Ref<AFortPlayerStateAthena> Guard(State);
    if (ServerSetInAircraftOG)
        ServerSetInAircraftOG(State, InAircraft);
    State = Guard.Get();
    if (!State)
        return;
    State->bInAircraft = InAircraft;
    State->ForceNetUpdate();
    if (!Entering)
        return;
    auto C = Cast<AController>(State->Owner);
    FortInventory::ClearWarmupLoot(C);
}

void (*ServerAttemptInteractOG)(UFortControllerComponent_Interaction *ComponentInteraction, AActor *ReceivingActor,
                                UPrimitiveComponent *InteractComponent, ETInteractionType InteractType,
                                UObject *OptionalObjectData);
void ServerAttemptInteract(UFortControllerComponent_Interaction *C, AActor *Actor, UPrimitiveComponent *Component,
                           ETInteractionType Type, UObject *Data)
{
    if (!C || !Actor || Actor->IsActorBeingDestroyed())
        return;
    // Catch the authoritative request before Blueprint-to-Blueprint dispatch:
    // those internal calls need not re-enter the global ProcessEvent observer.
    if(auto Drop=Cast<AAthenaSupplyDrop_C>(Actor);Drop&&!Drop->VehicleType)
    {
        auto PC=Cast<AFortPlayerControllerAthena>(C->GetOwner());
        auto Pawn=PC?Cast<AFortPlayerPawnAthena>(PC->Pawn):nullptr;
        if(Pawn&&!Pawn->IsDead()&&InteractionLoot::InSupplyReach(Drop,Pawn))
        {InteractionLoot::SearchSupply(Drop,Pawn);return;}
    }
    // Native interaction dispatches to the hooked container loot function once.
    if (ServerAttemptInteractOG)
        ServerAttemptInteractOG(C, Actor, Component, Type, Data);
}

void ServerPlayEmoteItem(AFortPlayerControllerAthena *PC, UFortMontageItemDefinitionBase *EmoteAsset,
                         float EmoteRandomNumber)
{
    auto Pawn = PlayerPawn(PC);
    Log("ServerPlayEmoteItem Called!");

    if (!PC || !Pawn || !PC->PlayerState || !EmoteAsset || !UWorld::GetWorld())
        return;

    AFortGameStateAthena *GameState = (AFortGameStateAthena *)UWorld::GetWorld()->GameState;
    if (!GameState || GameState->GamePhase == EAthenaGamePhase::Aircraft)
    {
        return;
    }

    UClass *DanceAbility = StaticLoadObject<UClass>("/Game/Abilities/Emotes/GAB_Emote_Generic.GAB_Emote_Generic_C");
    UClass *SprayAbility = StaticLoadObject<UClass>("/Game/Abilities/Sprays/GAB_Spray_Generic.GAB_Spray_Generic_C");

    if (!DanceAbility || !SprayAbility)
        return;

    auto EmoteDef = Cast<UAthenaDanceItemDefinition>(EmoteAsset);
    if (!EmoteDef && !EmoteAsset->IsA(UAthenaSprayItemDefinition::StaticClass()))
        return;

    if (EmoteDef)
    {
        Pawn->bMovingEmote = EmoteDef->bMovingEmote;
        Pawn->EmoteWalkSpeed = EmoteDef->WalkForwardSpeed;
        Pawn->bMovingEmoteForwardOnly = EmoteDef->bMoveForwardOnly;
    }

    FGameplayAbilitySpec Spec{};
    UGameplayAbility *Ability = nullptr;

    if (EmoteAsset->IsA(UAthenaSprayItemDefinition::StaticClass()))
    {
        Ability = (UGameplayAbility *)SprayAbility->DefaultObject;
    }

    if (Ability == nullptr)
    {
        Ability = (UGameplayAbility *)DanceAbility->DefaultObject;
    }

    if (!Ability)
        return;
    AbilitySpecConstructor(&Spec, Ability, 1, -1, EmoteAsset);
    auto ASC = ((AFortPlayerStateAthena *)PC->PlayerState)->AbilitySystemComponent;
    if (ASC && Ability)
        GiveAbilityAndActivateOnce(ASC, &Spec.Handle, Spec);
}

void ServerReturnToMainMenu(AFortPlayerControllerAthena *PC)
{
    if (PC)
        PC->ClientReturnToMainMenu(L"");
}

void HookAll()
{
    PlayerVehicles::Initialise();
    PlayerTraps::Initialise();
    HookVTable<AFortPlayerControllerAthena>(0x108, ServerAcknowledgePossession,
                                            (LPVOID *)&ServerAcknowledgePossessionOG);

    InstallHook((LPVOID)(ImageBase + 0x19C7940), ServerReadyToStartMatch, (LPVOID *)&ServerReadyToStartMatchOG);

    // 0x1F19990 is an exec thunk (UObject*, FFrame&, void*), not a one-argument
    // controller method. Leave it native. Observe the verified loading implementation.
    HookVTable<AFortPlayerControllerAthena>(0x260, ServerSetClientHasFinishedLoading,
                                           (LPVOID *)&ServerSetClientHasFinishedLoadingOG);

    HookVTable<AFortPlayerControllerAthena>(0x1FE, ServerExecuteInventoryItem, (LPVOID *)&ServerExecuteInventoryItemOG);

    HookVTable<AFortPlayerControllerAthena>(0x1BC, ServerCheat);
    HookVTable<AFortPlayerControllerAthena>(0x210, ServerAttemptInventoryDrop);
    InventoryDropEvent = AFortPlayerController::StaticClass()->GetFunction("FortPlayerController", "ServerAttemptInventoryDrop");
    EndEditingEvent = AFortPlayerController::StaticClass()->GetFunction("FortPlayerController", "ServerEndEditingBuildingActor");
    BeginEditingEvent = AFortPlayerController::StaticClass()->GetFunction("FortPlayerController", "ServerBeginEditingBuildingActor");
    if (InventoryDropEvent || EndEditingEvent || BeginEditingEvent) PE::HandledEvent = InterceptLocalEvents;
    HookVTable<AFortPlayerControllerAthena>(0x224, ServerCreateBuildingActor);
    HookVTable<AFortPlayerControllerAthena>(0x22A, ServerBeginEditingBuildingActor);
    HookVTable<AFortPlayerControllerAthena>(0x228, ServerEndEditingBuildingActor);
    HookVTable<AFortPlayerControllerAthena>(0x226, ServerEditBuildingActor);
    HookVTable<AFortPlayerControllerAthena>(0x220, ServerRepairBuildingActor);

    InstallHook((LPVOID)(ImageBase + 0x1F34E50), ClientOnPawnDied, (LPVOID *)&ClientOnPawnDiedOG);

    HookVTable<AFortPlayerStateAthena>(0x103, ServerSetInAircraft, reinterpret_cast<void **>(&ServerSetInAircraftOG));
    HookVTable<UFortControllerComponent_Interaction>(0x81, ServerAttemptInteract, (LPVOID *)&ServerAttemptInteractOG);

    HookVTable<AFortPlayerControllerAthena>(0x1BE, ServerPlayEmoteItem);
    HookVTable<AFortPlayerControllerAthena>(0x258, ServerReturnToMainMenu);

    Log("Controller Hooked!");
}
} // namespace Controller
