#pragma once
#include "ServerBotManager.h"
#include "BotNames.h"
namespace Bots
{
inline UWorld *NameWorld = nullptr;
inline std::unordered_set<int> UsedNames;
inline int NextWarmupStart = 0;
inline bool SpawnPlayerBot(AActor *Override = nullptr, PlayerBots::EBotState State = PlayerBots::EBotState::Warmup)
{
    auto W = UWorld::GetWorld();
    auto GM = W ? Cast<AFortGameModeAthena>(W->AuthorityGameMode) : nullptr;
    if (!GM || !GM->ServerBotManager)
        return false;
    if (NameWorld != W)
    {
        UsedNames.clear();
        NameWorld = W;
        NextWarmupStart = 0;
    }
    ResolveBuildingAssets();
    auto Tree = StaticLoadObject<UBehaviorTree>("/Game/Athena/AI/Phoebe/BehaviorTrees/BT_Phoebe.BT_Phoebe");
    auto Class =
        StaticLoadObject<UClass>("/Game/Athena/AI/Phoebe/BP_PlayerPawn_Athena_Phoebe.BP_PlayerPawn_Athena_Phoebe_C");
    if (!Tree || !Class)
    {
        Log("Required native Phoebe assets unavailable");
        return false;
    }
    if (!Override && PlayerStarts.Num() == 0)
        UGameplayStatics::GetAllActorsOfClass(W, AFortPlayerStartWarmup::StaticClass(), &PlayerStarts);
    auto Start =
        Override ? Override : (PlayerStarts.Num() ? PlayerStarts[(NextWarmupStart++) % PlayerStarts.Num()] : nullptr);
    if (!Start || Start->IsActorBeingDestroyed())
    {
        Log("Native bot spawn failed: no valid warmup player start.");
        return false;
    }
    auto Data = Cast<UFortAthenaAIBotCustomizationData>(
        UGameplayStatics::SpawnObject(UFortAthenaAIBotCustomizationData::StaticClass(), GM));
    if (!Data)
        return false;
    Data->BehaviorTree = Tree;
    Data->PawnClass = Class;
    Data->bOverrideBehaviorTree = true;
    Data->bOverrideStartupInventory = true;
    Data->bOverrideCharacterCustomization = false;
    Data->bOverrideDBNOPlayStyle = true;
    Data->DBNOPlayStyle = EDBNOPlayStyle::Default;
    Data->bOverrideSkillLevel = false;
    Data->bOverrideSkillSets = false;
    Data->SkillLevel = 1;
    Data->StartupInventory = Cast<UFortAthenaAIBotInventoryItems>(
        UGameplayStatics::SpawnObject(UFortAthenaAIBotInventoryItems::StaticClass(), Data));
    if (!Data->StartupInventory)
        return false;
    auto Pick = StaticLoadObject<UFortWeaponMeleeItemDefinition>(
        "/Game/Athena/Items/Weapons/WID_Harvest_Pickaxe_Athena_C_T01.WID_Harvest_Pickaxe_Athena_C_T01");
    PruneCosmetics();
    if (!Pickaxes.empty())
    {
        auto Cosmetic = Pickaxes[Syndicate::Pick(static_cast<int>(Pickaxes.size()))];
        if (Cosmetic && Cosmetic->WeaponDefinition)
            Pick = Cosmetic->WeaponDefinition;
    }
    if (Pick)
        Data->StartupInventory->Items.Add(Pick);
    for (auto Tool : {static_cast<UFortItemDefinition *>(Floor), static_cast<UFortItemDefinition *>(Roof),
                      static_cast<UFortItemDefinition *>(Stair), static_cast<UFortItemDefinition *>(Wall),
                      static_cast<UFortItemDefinition *>(EditTool)})
        if (Tool)
            Data->StartupInventory->Items.Add(Tool);
    auto Pawn = GM->ServerBotManager->SpawnBot(Start->K2_GetActorLocation(), Start->K2_GetActorRotation(), Data);
    if (!Pawn)
    {
        Log("Native bot manager returned no pawn; spawn will retry at a different warmup start.");
        return false;
    }
    auto Bot = PlayerBots::Find(Pawn);
    if (!Bot)
    {
        Log("Native bot pawn has no registered Syndicate controller/inventory wrapper.");
        return false;
    }
    std::vector<int> Available;
    for (int i = 0; i < static_cast<int>(BotDisplayNames.size()); ++i)
        if (!UsedNames.contains(i))
            Available.push_back(i);
    if (!Available.empty())
    {
        int Index = Available[Syndicate::Pick(static_cast<int>(Available.size()))];
        UsedNames.insert(Index);
        auto Name = BotDisplayNames[Index];
        int Size = MultiByteToWideChar(CP_UTF8, 0, Name.data(), static_cast<int>(Name.size()), nullptr, 0);
        if (Size > 0)
        {
            std::wstring Wide(Size, 0);
            MultiByteToWideChar(CP_UTF8, 0, Name.data(), static_cast<int>(Name.size()), Wide.data(), Size);
            GM->ChangeName(Bot->PC, FString(Wide.c_str()), true);
            Bot->PlayerState->OnRep_PlayerName();
        }
    }
    Bot->BotState = State;
    Bot->bHasJumpedFromBus = State == PlayerBots::EBotState::Landed;
    return true;
}
} // namespace Bots
