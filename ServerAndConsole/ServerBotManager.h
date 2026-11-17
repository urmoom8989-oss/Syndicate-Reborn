#pragma once
#include "framework.h"

#include "PlayerBots.h"
#include "BotPopulation.h"
#include "BotPhysics.h"
#include "NativeBotController.h"

namespace ServerBotManager
{
AFortPlayerPawnAthena *(*SpawnBotOG)(UFortServerBotManagerAthena *This, FVector SpawnLoc, FRotator SpawnRot,
                                     UFortAthenaAIBotCustomizationData *BotData);
AFortPlayerPawnAthena *SpawnBot(UFortServerBotManagerAthena *This, FVector SpawnLoc, FRotator SpawnRot,
                                UFortAthenaAIBotCustomizationData *BotData)
{
    auto World = UWorld::GetWorld();
    auto GameMode = World ? Cast<AFortGameModeAthena>(World->AuthorityGameMode) : nullptr;
    auto GameState = World ? Cast<AFortGameStateAthena>(World->GameState) : nullptr;
    if (!This || !BotData || !GameMode || !GameState || !Syndicate::Finite(SpawnLoc))
        return nullptr;
    if (PlayerBots::ActiveWorld != World)
    {
        PlayerBots::ResetWorld();
        PlayerBots::ActiveWorld = World;
    }
    BotPopulation::Begin(World);
    BotPopulation::Reconcile(GameMode, GameState);
    if (!BotPopulation::CanSpawn(GameMode, GameState))
        return nullptr;
    int Team = BotPopulation::Team(GameMode);
    if (!Team)
        return nullptr;
    BotPhysics::PrepareClass(BotData->PawnClass.Get());
    FVector Grounded{};
    if (!BotPhysics::InitialLocation(BotData->PawnClass.Get(), SpawnLoc, Grounded))
        return nullptr;
    BotPopulation::Admission Admission(Team);
    auto Pawn = SpawnBotOG ? SpawnBotOG(This, Grounded, SpawnRot, BotData) : nullptr;
    if (Pawn && !Pawn->Controller)
        NativeBotController::Create(Pawn, This, GameMode);
    auto PC = Pawn ? Cast<AFortAthenaAIBotController>(Pawn->Controller) : nullptr;
    auto PlayerState = PC ? Cast<AFortPlayerStateAthena>(PC->PlayerState) : nullptr;
    if (!Pawn || !PC || !PlayerState)
    {
        Log("Native bot spawn did not produce a complete pawn/controller/state");
        if (Pawn)
        {
            BotPopulation::Faulted = true;
            Log("Population paused after an incomplete native bot spawn; no extra pawn retries.");
        }
        return Pawn;
    }
    if (PlayerBots::Find(Pawn))
        return Pawn;
    int PreviousTeam = PlayerState->TeamIndex;
    PlayerState->TeamIndex = static_cast<uint8>(Team);
    PlayerState->SquadId = static_cast<uint8>(Team - 3);
    PlayerState->OnRep_TeamIndex(PreviousTeam);
    PlayerState->OnRep_SquadId();
    PlayerState->OnRep_PlayerTeam();
    PlayerState->OnRep_PlayerTeamPrivate();
    bool Registered = false;
    for (auto Existing : GameMode->AliveBots)
        if (Existing == PC)
        {
            Registered = true;
            break;
        }
    if (!Registered)
    {
        GameMode->AliveBots.Add(PC);
    }
    bool InManager = false;
    for (auto Entry : This->PlayerBots)
        InManager |= Entry.BotController == PC;
    if (!InManager)
    {
        FFortServerBotInfo Info{};
        Info.BotController = PC;
        This->PlayerBots.Add(Info);
    }
    ++BotPopulation::Admitted;
    PC->PlayerBotPawn = Pawn;
    PC->CachedBotManager = This;
    PC->CachedGameMode = GameMode;
    PC->CachedBotMutator = This->CachedBotMutator;
    PlayerState->bIsABot = true;
    PC->PrimaryActorTick.bAllowTickOnDedicatedServer = true;
    PC->SetActorTickEnabled(true);
    PC->ResetIgnoreMoveInput();
    PC->ResetIgnoreLookInput();
    if (PC->PathFollowingComponent)
    {
        PC->PathFollowingComponent->PrimaryComponentTick.bAllowTickOnDedicatedServer = true;
        PC->PathFollowingComponent->SetComponentTickEnabled(true);
    }
    BotPhysics::EnableGroundPawn(Pawn);
    BotPopulation::Reconcile(GameMode, GameState);

    PruneCosmetics();
    if (!Characters.empty())
    {
        auto CID = Characters[UKismetMathLibrary::GetDefaultObj()->RandomIntegerInRange(
            0, static_cast<int>(Characters.size()) - 1)];
        if (CID && CID->HeroDefinition)
        {
            PlayerState->HeroType = CID->HeroDefinition;
            if (CID->HeroDefinition->Specializations.IsValid())
            {
                for (int i = 0; i < CID->HeroDefinition->Specializations.Num(); i++)
                {
                    UFortHeroSpecialization *Spec = StaticLoadObject<UFortHeroSpecialization>(
                        UKismetStringLibrary::GetDefaultObj()
                            ->Conv_NameToString(CID->HeroDefinition->Specializations[i].ObjectID.AssetPathName)
                            .ToString());
                    if (Spec)
                    {
                        for (int j = 0; j < Spec->CharacterParts.Num(); j++)
                        {
                            UCustomCharacterPart *Part = StaticLoadObject<UCustomCharacterPart>(
                                UKismetStringLibrary::GetDefaultObj()
                                    ->Conv_NameToString(Spec->CharacterParts[j].ObjectID.AssetPathName)
                                    .ToString());
                            if (Part && static_cast<size_t>(Part->CharacterPartType) <
                                            std::size(PlayerState->CharacterData.Parts))
                            {
                                PlayerState->CharacterData.Parts[(uintptr_t)Part->CharacterPartType] = Part;
                            }
                        }
                    }
                }
            }
        }
        if (CID)
        {
            Pawn->CosmeticLoadout.Character = CID;
        }
    }
    if (!Backpacks.empty() && UKismetMathLibrary::GetDefaultObj()->RandomBoolWithWeight(0.5f))
    { // less likely to equip than skin cause lots of ppl prefer not to use backpack
        auto Backpack = Backpacks[UKismetMathLibrary::GetDefaultObj()->RandomIntegerInRange(
            0, static_cast<int>(Backpacks.size()) - 1)];
        for (int j = 0; j < Backpack->CharacterParts.Num(); j++)
        {
            UCustomCharacterPart *Part = Backpack->CharacterParts[j];
            if (Part && static_cast<size_t>(Part->CharacterPartType) < std::size(PlayerState->CharacterData.Parts))
            {
                PlayerState->CharacterData.Parts[(uintptr_t)Part->CharacterPartType] = Part;
            }
        }
    }
    if (!Gliders.empty())
    {
        auto Glider =
            Gliders[UKismetMathLibrary::GetDefaultObj()->RandomIntegerInRange(0, static_cast<int>(Gliders.size()) - 1)];
        Pawn->CosmeticLoadout.Glider = Glider;
    }
    if (!Contrails.empty() && UKismetMathLibrary::GetDefaultObj()->RandomBoolWithWeight(0.95f))
    {
        auto Contrail = Contrails[UKismetMathLibrary::GetDefaultObj()->RandomIntegerInRange(
            0, static_cast<int>(Contrails.size()) - 1)];
        Pawn->CosmeticLoadout.SkyDiveContrail = Contrail;
    }
    for (int i = 0; i < Dances.size(); i++)
    {
        Pawn->CosmeticLoadout.Dances.Add(Dances.at(i));
    }

    Pawn->OnRep_CosmeticLoadout();
    PlayerState->OnRep_CharacterData();

    PC->BehaviorTree = BotData->BehaviorTree;
    if (!PC->Blackboard && BotData->BehaviorTree && BotData->BehaviorTree->BlackboardAsset)
    {
        UBlackboardComponent *Blackboard = nullptr;
        PC->UseBlackboard(BotData->BehaviorTree->BlackboardAsset, &Blackboard);
    }
    if (BotData->BehaviorTree)
        PC->RunBehaviorTree(BotData->BehaviorTree);

    Pawn->SetMaxHealth(100.f);
    Pawn->SetMaxShield(100.f);
    Pawn->SetHealth(100.f);
    Pawn->SetShield(0.f);

    if (!PC->Inventory)
    {
        // Until i hook SetUpInventoryBot
        // Log("Creating inv for bot!");
        PC->Inventory = SpawnActor<AFortInventory>({});
        if (!PC->Inventory)
        {
            BotPopulation::Faulted = true;
            Log("Bot inventory allocation failed; population paused to prevent orphaned retries.");
            return Pawn;
        }
        PC->Inventory->Owner = PC;
        PC->Inventory->OnRep_Owner();
    }

    PlayerBots::PlayerBot *Bot = new PlayerBots::PlayerBot(Pawn, PC, PlayerState);

    if (PC->Inventory)
    {
        if (BotData->StartupInventory)
            for (auto item : BotData->StartupInventory->Items)
            {
                if (item && !FortInventory::FindItemEntry(PC, item))
                {
                    Bot->GiveItem(item, 1, 0);
                }
            }
    }
    else
    {
        Log("Bot startup inventory unavailable");
    }

    auto AbilitySet = StaticLoadObject<UFortAbilitySet>(
        "/Game/Abilities/Player/Generic/Traits/DefaultPlayer/GAS_AthenaPlayer.GAS_AthenaPlayer");
    AbilitySystemComponent::GiveAbilitySet(AbilitySet, PlayerState);
    PC->CachedBotManager = This;
    PC->CachedGameMode = GameMode;
    PC->CachedBotMutator = This->CachedBotMutator;
    PlayerState->bHasFinishedLoading = true;
    PlayerState->bHasStartedPlaying = true;
    PlayerState->OnRep_bHasStartedPlaying();
    Bot->EquipPickaxe();

    Bot->ObservePhase(GameState->GamePhase);
    Log("Bot admitted: roster=" + std::to_string(BotPopulation::Admitted) +
        ", evaluators=" + std::to_string(PC->AIEvaluators.Num()));
    return Bot->Pawn;
}

void HookAll()
{
    InstallHook((LPVOID)(ImageBase + 0x12B1C50), SpawnBot, (LPVOID *)&SpawnBotOG);

    Log("ServerBotManager Hooked!");
}
} // namespace ServerBotManager
