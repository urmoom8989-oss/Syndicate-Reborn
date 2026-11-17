#include "pch.h"
#include "../Public/FortQuestManager.h"
#include "../Public/FortGameMode.h"
#include "../../Erbium/Public/SyndicateSeasonAssets.h"
#include <unordered_set>

struct FParseConditionResult
{
    bool bMatch;
    size_t NextStart;
};

static FParseConditionResult ParseCondition(UEAllocatedString Condition, const FGameplayTagContainer& TargetTags, const FGameplayTagContainer& SourceTags, const FGameplayTagContainer& ContextTags)
{
    size_t CondTypeStart = -1, CondTypeEnd = -1, NextCond = -1;

    for (auto& c : Condition)
    {
        if (CondTypeStart == -1)
        {
            bool ud = c == '>' || c == '<' || c == '=' || c == '&' || c == '|';
            if (!ud && strncmp((char*)&c, "HasTag", 6) == 0)
            {
                CondTypeStart = __int64(&c - Condition.data());
                CondTypeEnd = __int64(&c - Condition.data() + 6);
                continue;
            }
            else if (!ud && strncmp((char*)&c, "MissingTag", 10) == 0)
            {
                CondTypeStart = __int64(&c - Condition.data());
                CondTypeEnd = __int64(&c - Condition.data() + 10);
                continue;
            }
            else if (ud)
                CondTypeStart = __int64(&c - Condition.data());
        }
        else if (CondTypeStart != -1 && CondTypeEnd == -1 && (c != '>' && c != '<' && c != '=' && c != '&' && c != '|'))
            CondTypeEnd = __int64(&c - Condition.data());
        else if (CondTypeEnd != -1 && (c == '=' || c == '&' || c == '|'))
            NextCond = __int64(&c - Condition.data());

        if (CondTypeStart != -1 && CondTypeEnd != -1 && NextCond != -1)
            break;
    }

    if (CondTypeStart == Condition.npos)
    {
        CondTypeStart = Condition.find(" ");

        if (CondTypeStart == Condition.npos)
            return { false, NextCond };

        CondTypeStart++;

        if (CondTypeEnd == Condition.npos)
            CondTypeEnd = Condition.find(" ", CondTypeStart);

        NextCond = Condition.find("&&", CondTypeEnd + 1);
        if (NextCond == Condition.npos)
            NextCond = Condition.find("||", CondTypeEnd + 1);
    }
    else if (CondTypeEnd == Condition.npos)
        CondTypeEnd = CondTypeStart + 1;

    auto Left = Condition.substr(0, CondTypeStart - 1);
    auto CondType = Condition.substr(CondTypeStart, CondTypeEnd - CondTypeStart);
    auto Right = Condition.substr(CondTypeEnd + 1, NextCond == Condition.npos ? NextCond : (Condition.substr(NextCond - 1, 1) == " " ? NextCond - 1 : NextCond) - CondTypeEnd - 1);

    if (CondType == "HasTag" || CondType == "MissingTag")
    {
        FGameplayTagContainer Container;

        if (Left == "Target.Tags")
            Container = TargetTags;
        else if (Left == "Source.Tags")
            Container = SourceTags;
        else if (Left == "Context.Tags")
            Container = ContextTags;
        else
            return { false, NextCond };

        FGameplayTag Tag(FName(UEAllocatedWString(Right.begin(), Right.end()).c_str()));

        if (CondType == "HasTag")
            return { Container.HasTag(Tag), NextCond };
        else
            return { !Container.HasTag(Tag), NextCond };
    }
    else
    {
        printf("Condition: %s\n", Condition.c_str());
        printf("Hit unimplemented condition: %s, %s, %s\n", Left.c_str(), CondType.c_str(), Right.c_str());
    }

    return { false, NextCond };
}

static bool IsConditionMet(const FString& InCondition, const FGameplayTagContainer& TargetTags, const FGameplayTagContainer& SourceTags, const FGameplayTagContainer& ContextTags)
{
    if (InCondition.Num() <= 0)
        return true;

    auto Condition = InCondition.ToString();


    FParseConditionResult Result = ParseCondition(Condition, TargetTags, SourceTags, ContextTags);

    if (Result.NextStart != Condition.npos)
    {
    loop:
        if (Result.NextStart == Condition.npos)
            return Result.bMatch;
        auto Start = Condition.substr(Result.NextStart, 2);

        if (Start == "&&")
        {
            Condition = Condition.substr(Result.NextStart + 2);

            if (Condition.substr(0, 1) == " ")
                Condition = Condition.substr(1);

            auto LastResult = Result;

            Result = ParseCondition(Condition, TargetTags, SourceTags, ContextTags);

            if (!LastResult.bMatch || !Result.bMatch)
                Result.bMatch = false;

            goto loop;
        }
        else if (Start == "||")
        {
            Condition = Condition.substr(Result.NextStart + 2);

            if (Condition.substr(0, 1) == " ")
                Condition = Condition.substr(1);

            auto LastResult = Result;

            Result = ParseCondition(Condition, TargetTags, SourceTags, ContextTags);

            if (LastResult.bMatch || Result.bMatch)
                Result.bMatch = true;

            goto loop;
        }
        else
            return Result.bMatch;
    }

    return Result.bMatch;
}

void GiveAccolade(AFortPlayerControllerAthena* PlayerController, UFortAccoladeItemDefinition* Accolade, FPrimaryAssetId AssetId)
{
    printf("GiveAccolade %s\n", Accolade->Name.ToString().c_str());

    auto Info = (FXPEventInfo*)malloc(FXPEventInfo::Size());
    memset(Info, 0, FXPEventInfo::Size());

    Info->Accolade = AssetId;

    float XpValue = Accolade->XpRewardAmount.Evaluate();

    if (XpValue == 0)
        UDataTableFunctionLibrary::EvaluateCurveTableRow(Accolade->XpRewardAmount.Curve.CurveTable, FName(L"Default_Medal"), Accolade->XpRewardAmount.Value, nullptr, &XpValue, FString());

    Info->EventXpValue = (int32)XpValue;
    Info->RestedValuePortion = Info->EventXpValue;
    Info->RestedXPRemaining = Info->EventXpValue;
    Info->TotalXpEarnedInMatch = Info->EventXpValue + PlayerController->XPComponent->TotalXpEarned;
    Info->Priority = Accolade->Priority;
    Info->SimulatedText = Accolade->HasDescription() ? Accolade->Description : Accolade->ItemDescription;
    if (FXPEventInfo::HasSimulatedName())
        Info->SimulatedName = Accolade->HasDisplayName() ? Accolade->DisplayName : Accolade->ItemName;
    Info->EventName = Accolade->Name;
    Info->SeasonBoostValuePortion = 0;
    if (FXPEventInfo::HasPlayerController())
        Info->PlayerController = PlayerController;

    PlayerController->XPComponent->MatchXp += Info->EventXpValue;
    PlayerController->XPComponent->TotalXpEarned += Info->EventXpValue;

    PlayerController->XPComponent->OnXPEvent(*Info);
    free(Info);

    /*for (auto& SoftAccoladeToReplace : Accolade->AccoladeToReplace)
    {
        auto AccoladeToReplace = SoftAccoladeToReplace.Get();
        auto AthenaAccoladeIndex = PlayerController->XPComponent->PlayerAccolades.SearchIndex([&](FAthenaAccolades& item)
            { return item.AccoladeDef == AccoladeToReplace; });

        auto MedalIndex = PlayerController->XPComponent->MedalsEarned.SearchIndex([&](UFortAccoladeItemDefinition* item)
            { return item == AccoladeToReplace; });

        if (AthenaAccoladeIndex != -1)
            PlayerController->XPComponent->PlayerAccolades.Remove(AthenaAccoladeIndex);

        if (MedalIndex != -1)
            PlayerController->XPComponent->MedalsEarned.Remove(MedalIndex);
    }*/


    /*for (auto& AthenaAccolade : PlayerController->XPComponent->PlayerAccolades)
    {
        if (AthenaAccolade.AccoladeDef == Accolade)
        {
            AthenaAccolade.Count++;
            return;
        }
    }

    FAthenaAccolades AthenaAccolade{};
    AthenaAccolade.AccoladeDef = Accolade;
    AthenaAccolade.Count = 1;
    auto str = UEAllocatedWString(L"AthenaAccolade:") + Accolade->Name.ToWString();
    FString TemplateId;
    TemplateId.Reserve((int)str.size() + 1);
    for (auto& c : str)
    {
        TemplateId.Add(c);
    }
    AthenaAccolade.TemplateId = TemplateId;
    PlayerController->XPComponent->PlayerAccolades.Add(AthenaAccolade);*/

    if (Accolade->AccoladeType == 2)
    {
        PlayerController->XPComponent->MedalsEarned.Add(Accolade);
        PlayerController->XPComponent->ClientMedalsRecived(PlayerController->XPComponent->PlayerAccolades);
    }
}

std::unordered_map<UFortQuestManager*, std::unordered_set<UFortQuestItemDefinition*>> OncePerMatchMap;
void ProgressQuest(UFortQuestManager* _this, AFortPlayerControllerAthena* PlayerController, UFortQuestItem* QuestItem, FName BackendName, int Count)
{
    auto QuestDefinition = QuestItem->ItemDefinition;
    auto QuestObjectivePtr = QuestItem->Objectives.Search([&](UFortQuestObjectiveInfo* Obj) { return Obj->BackendName == BackendName; });

    if (!QuestObjectivePtr)
        return; // if this somehow happens, we are just fucked

    if (QuestDefinition->HasbAthenaUpdateObjectiveOncePerMatch() && QuestDefinition->bAthenaUpdateObjectiveOncePerMatch)
    {
        auto& OncePerMatchSet = OncePerMatchMap[_this];
        if (OncePerMatchSet.contains(QuestDefinition))
            return;

        OncePerMatchSet.emplace(QuestDefinition);
    }

    auto QuestObjective = *QuestObjectivePtr;

    auto AcheivedCount = QuestObjective->AchievedCount;

    bool bAllObjectivesCompleted = AcheivedCount + Count == QuestObjective->RequiredCount;

    if (bAllObjectivesCompleted)
    {
        for (auto& Objective : QuestItem->Objectives)
        {
            if (Objective == QuestObjective)
                continue;

            bAllObjectivesCompleted = Objective->AchievedCount == Objective->RequiredCount;

            if (!bAllObjectivesCompleted)
                break;
        }
    }

    printf("[Quests] %s Completed: %s\n", BackendName.ToString().c_str(), (AcheivedCount + Count == QuestObjective->RequiredCount) ? "true" : "false");
    // keep it the same damnit
    _this->SelfCompletedUpdatedQuest(PlayerController, QuestDefinition, BackendName, AcheivedCount + Count, Count, nullptr, AcheivedCount + Count == QuestObjective->RequiredCount, bAllObjectivesCompleted);

    if (!UFortQuestManager::SelfCompletedUpdatedQuest__Ptr)
        _this->HandleQuestUpdated(PlayerController, QuestDefinition, BackendName, AcheivedCount + Count, Count, nullptr, AcheivedCount + Count == QuestObjective->RequiredCount, bAllObjectivesCompleted);
    else if (!UFortQuestManager::HandleQuestUpdated__Ptr)
        _this->HandleQuestObjectiveUpdated(PlayerController, QuestDefinition, BackendName, AcheivedCount + Count, Count, nullptr, AcheivedCount + Count == QuestObjective->RequiredCount, bAllObjectivesCompleted);
    else if (!UFortQuestManager::HandleQuestObjectiveUpdated__Ptr)
    {
        UFortQuestManagerComponent_Athena* QuestManagerComp = nullptr;

        for (auto& Component : _this->Components)
        {
            if (auto CastedComp = Component->Cast<UFortQuestManagerComponent_Athena>())
            {
                QuestManagerComp = CastedComp;
                break;
            }
        }
        
        if (QuestManagerComp)
            QuestManagerComp->HandleQuestObjectiveUpdated(PlayerController, QuestDefinition, BackendName, AcheivedCount + Count, Count, nullptr, AcheivedCount + Count == QuestObjective->RequiredCount, bAllObjectivesCompleted);
    }

    if (PlayerController->HasXPComponent())
    {
        PlayerController->XPComponent->QuestObjectiveUpdated(PlayerController, QuestDefinition, BackendName, Count, AcheivedCount + Count == QuestObjective->RequiredCount, bAllObjectivesCompleted);
    }
}

std::unordered_map<AActor*, std::unordered_map<int32_t, int32_t>> CountThresholdMap;
std::unordered_set<UFortAccoladeItemDefinition*> OnlyOnceMap;

void SyndicateRecordStat(AActor* PlayerController, long long StatEvent, int32 Count, const FGameplayTagContainer& TargetTags, const FGameplayTagContainer& SourceTags, const FGameplayTagContainer& ContextTags);
void UFortQuestManager::SendStatEvent__Internal(AActor* PlayerController, long long StatEvent, int32 Count, UObject* TargetObject, FGameplayTagContainer TargetTags, FGameplayTagContainer SourceTags, FGameplayTagContainer ContextTags, bool* QuestActive, bool* QuestCompleted)
{
    if (!this)
        return;
    // Syndicate: every human stat event (build, interact, kill, land, visit, damage...) is also
    // recorded for the backend, which progresses season challenges from it.
    SyndicateRecordStat(PlayerController, StatEvent, Count, TargetTags, SourceTags, ContextTags);

	static auto XPTable = FindObject<UDataTable>(L"/Game/Athena/Items/Quests/AthenaObjectiveStatXPTable.AthenaObjectiveStatXPTable");
    auto FortPC = (AFortPlayerControllerAthena*)PlayerController;

	if (XPTable && FortPC->HasXPComponent())
	{
		for (const auto& [ Key, Value ] : XPTable->RowMap)
		{
			auto Row = (FFortQuestObjectiveStatXPTableRow*)Value;

			if (Row->Type != (uint8_t)StatEvent) // todo: fix this count threshold stuff
				continue;

			if (!Row->HasAccoladePrimaryAssetId())
				break;
			
            auto Accolade = (UFortAccoladeItemDefinition*)UKismetSystemLibrary::GetObjectFromPrimaryAssetId(Row->AccoladePrimaryAssetId);

            if (!Accolade)
                continue;

            if (Row->bOnceOnly && OnlyOnceMap.contains(Accolade))
                continue;

			if (!TargetTags.HasAll(Row->TargetTags))
				continue;

			if (!SourceTags.HasAll(Row->SourceTags))
				continue;

			if (!ContextTags.HasAll(Row->ContextTags))
				continue;

			if (FFortQuestObjectiveStatXPTableRow::HasExcludeTargetTags() && TargetTags.HasAny(Row->ExcludeTargetTags))
				continue;

			if (FFortQuestObjectiveStatXPTableRow::HasExcludeSourceTags() && SourceTags.HasAny(Row->ExcludeSourceTags))
				continue;

			if (FFortQuestObjectiveStatXPTableRow::HasExcludeContextTags() && ContextTags.HasAny(Row->ExcludeContextTags))
				continue;

            if (!IsConditionMet(Row->Condition, TargetTags, SourceTags, ContextTags))
                continue;

            if (PlayerController)
            {
                auto& PrimaryAssetName = *(int32*)(__int64(&Row->AccoladePrimaryAssetId) + (VersionInfo.FortniteVersion >= 20 ? 4 : 8));
                if (PlayerController)
                    CountThresholdMap[PlayerController][PrimaryAssetName] += Count;

                if (Row->CountThreshhold > 0 && CountThresholdMap[PlayerController][PrimaryAssetName] != Row->CountThreshhold)
                    continue;

                auto AccoladeCount = 0;
                for (auto& AthenaAccolade : FortPC->XPComponent->PlayerAccolades)
                {
                    if (AthenaAccolade.AccoladeDef == Accolade)
                    {
                        AccoladeCount = AthenaAccolade.Count;
                        break;
                    }
                }

                printf("%s %d\n", Accolade->Name.ToString().c_str(), AccoladeCount);
                if (AccoladeCount > Row->MaxCount)
                    continue;

                if (Row->bOnceOnly)
                    OnlyOnceMap.emplace(Accolade);

                GiveAccolade(FortPC, Accolade, Row->AccoladePrimaryAssetId);
            }
		}
	}

    if (VersionInfo.FortniteVersion < 12 && StatEvent == EFortQuestObjectiveStatEvent::GetComplexCustom())
        return; // js crashes idk why

    for (auto& Quest : CurrentQuests)
    {
        if (Quest->HasCompletedQuest())
            continue;

        auto QuestDefinition = Quest->ItemDefinition;


        for (int i = 0; i < QuestDefinition->Objectives.Num(); i++)
        {
            auto& Objective = QuestDefinition->Objectives.Get(i, FFortMcpQuestObjectiveInfo::Size());

            if (Quest->HasCompletedObjectiveWithName(Objective.BackendName))
                continue;

            if (Objective.ObjectiveStatHandle.RowName.IsValid() && Objective.ObjectiveStatHandle.DataTable)
            {
                for (auto& [Key, Value] : Objective.ObjectiveStatHandle.DataTable->RowMap)
                {
                    if (Key != Objective.ObjectiveStatHandle.RowName)
                        continue;

                    auto Row = (FFortQuestObjectiveStatTableRow*)Value;

                    if (Row->Type != StatEvent)
                        continue;

                    //printf("USA is not in europe %s %d\n", Objective.BackendName.ToString().c_str(), Row->TargetTagContainer.GameplayTags.Num());
                    if (!TargetTags.HasAll(Row->TargetTagContainer))
                        continue;
                    //printf("Passed TT\n");

                    if (!SourceTags.HasAll(Row->SourceTagContainer))
                        continue;
                    //printf("Passed ST\n");

                    if (VersionInfo.FortniteVersion >= 10 && !ContextTags.HasAll(Row->ContextTagContainer))
                        continue;
                    //printf("Passed CT\n");

                    if (!IsConditionMet(Row->Condition, TargetTags, SourceTags, ContextTags))
                        continue;

                    printf("[Quests] Update: %s\n", Objective.BackendName.ToString().c_str());
                    ProgressQuest(this, FortPC, Quest, Objective.BackendName, Count);
                }
            }
            else
             {
                if (FFortMcpQuestObjectiveInfo::HasInlineObjectiveStats())
                {
                    for (int i = 0; i < Objective.InlineObjectiveStats.Num(); i++)
                    {
                        auto& ObjectiveStat = Objective.InlineObjectiveStats.Get(i, FFortQuestObjectiveStat::Size());

                        if (ObjectiveStat.Type != StatEvent)
                            continue;

                        bool bFound = true;
                        for (int i = 0; i < ObjectiveStat.TagConditions.Num(); i++)
                        {
                            auto& Condition = ObjectiveStat.TagConditions.Get(i, FInlineObjectiveStatTagCheckEntry::Size());

                            auto& Tag = FInlineObjectiveStatTagCheckEntry::HasTag() ? Condition.Tag : Condition.tag;

                            bool bHasTag = false;
                            switch (Condition.Type)
                            {
                            case Target:
                                bHasTag = TargetTags.HasTag(Tag);
                                break;
                            case Source:
                                bHasTag = SourceTags.HasTag(Tag);
                                break;
                            case Context:
                                bHasTag = ContextTags.HasTag(Tag);
                                break;
                            }

                            if (Condition.Require ? !bHasTag : bHasTag)
                            {
                                bFound = false;
                                break;
                            }
                        }

                        if (!bFound)
                            continue;

                        if (!IsConditionMet(ObjectiveStat.Condition, TargetTags, SourceTags, ContextTags))
                            continue;

                        ProgressQuest(this, FortPC, Quest, Objective.BackendName, Count);
                    }
                }
            }
        }
    }
}

bool bHasQueueStatEvent = false;

void UFortQuestManager::SendStatEvent(AActor* PlayerController, long long StatEvent, int32 Count, bool bAllowQueueStatEvent, UObject* TargetObject, FGameplayTagContainer TargetTags, FGameplayTagContainer AdditionalSourceTags, bool* QuestActive, bool* QuestCompleted)
{
    if ((bHasQueueStatEvent && !bAllowQueueStatEvent) || !this)
        return;

    FGameplayTagContainer PlayerSourceTags;
    FGameplayTagContainer ContextTags;

    GetSourceAndContextTags(&PlayerSourceTags, &ContextTags);

    printf("[QuestManager] SendStatEvent (Event: %lld)\n", StatEvent);

    auto GameMode = (AFortGameModeAthena*)UWorld::GetWorld()->AuthorityGameMode;
    auto GameState = (AFortGameStateAthena*)GameMode->GameState;

    auto Playlist = VersionInfo.FortniteVersion >= 3.5 && GameMode->HasWarmupRequiredPlayerCount() ? (GameMode->GameState->HasCurrentPlaylistInfo() ? GameMode->GameState->CurrentPlaylistInfo.BasePlaylist : GameMode->GameState->CurrentPlaylistData) : nullptr;

    if (Playlist && Playlist->HasGameplayTagContainer())
        ContextTags.AppendTags(Playlist->GameplayTagContainer);
    AdditionalSourceTags.AppendTags(PlayerSourceTags);

    return SendStatEvent__Internal(PlayerController, StatEvent, Count, TargetObject, TargetTags, AdditionalSourceTags, ContextTags, QuestActive, QuestCompleted);
}

bool bHasQuestActive = false;
bool bHasQuestCompleted = false;
void SendComplexCustomStatEvent(UObject* Context, FFrame& Stack)
{
    UObject* TargetObject;
    FGameplayTagContainer AdditionalSourceTags;
    FGameplayTagContainer TargetTags;
    bool* QuestActive = nullptr;
    bool* QuestCompleted = nullptr;
    int32 Count;

    Stack.StepCompiledIn(&TargetObject);
    Stack.StepCompiledIn(&AdditionalSourceTags);
    Stack.StepCompiledIn(&TargetTags);
    if (bHasQuestActive)
        Stack.StepCompiledIn(&QuestActive);
    if (bHasQuestCompleted)
        Stack.StepCompiledIn(&QuestCompleted);
    Stack.StepCompiledIn(&Count);
    Stack.IncrementCode();
    auto QuestManager = (UFortQuestManager*)Context;

    auto PlayerController = (AFortPlayerControllerAthena*)QuestManager->GetPlayerControllerBP();

    printf("SendComplexCustomStatEvent\n");
       QuestManager->SendStatEvent(PlayerController, EFortQuestObjectiveStatEvent::GetComplexCustom(), Count, false, TargetObject, TargetTags, AdditionalSourceTags);
}

void QueueStatEvent(UFortQuestManager* QuestManager, uint8_t InType, UObject* InTargetObject, FGameplayTagContainer* InTargetTags, FGameplayTagContainer* InSourceTags, FGameplayTagContainer* InContextTags, void* InObjectiveStat, FName InObjectiveBackendName, int InCount)
{
    printf("[QuestManager] QueueStatEvent (Event: %d)\n", InType);

    auto GameMode = (AFortGameModeAthena*)UWorld::GetWorld()->AuthorityGameMode;
    auto GameState = (AFortGameStateAthena*)GameMode->GameState;

    auto Playlist = VersionInfo.FortniteVersion >= 3.5 && GameMode->HasWarmupRequiredPlayerCount() ? (GameMode->GameState->HasCurrentPlaylistInfo() ? GameMode->GameState->CurrentPlaylistInfo.BasePlaylist : GameMode->GameState->CurrentPlaylistData) : nullptr;

    if (Playlist && Playlist->HasGameplayTagContainer())
        (*InContextTags).AppendTags(Playlist->GameplayTagContainer);

    return QuestManager->SendStatEvent__Internal(QuestManager->GetPlayerControllerBP(), InType, InCount, InTargetObject, *InTargetTags, *InSourceTags, *InContextTags, nullptr, nullptr);
}

void UFortQuestManager::PostLoadHook()
{

    auto QueueStatEventAddr = FindQueueStatEvent();

    bHasQueueStatEvent = QueueStatEventAddr != 0;
    Utils::Hook(QueueStatEventAddr, QueueStatEvent);

    if (!bHasQueueStatEvent)
    {
        auto SendComplexCustomStatEventFn = GetDefaultObj()->GetFunction("SendComplexCustomStatEvent");

        if (SendComplexCustomStatEventFn)
            for (auto& Param : SendComplexCustomStatEventFn->GetParamsNamed().NameOffsetMap)
            {
                if (Param.Name == "QuestActive")
                    bHasQuestActive = true;
                else if (Param.Name == "QuestCompleted")
                    bHasQuestCompleted = true;
            }

        Utils::ExecHook(SendComplexCustomStatEventFn, SendComplexCustomStatEvent);
    }
}


// ============================================================================================
// Syndicate season challenges (Seasons 2-X)
//  1. Once per game-server process, the season's challenge data (schedules -> bundles -> quests
//     -> objectives with their stat rows/tags) is read from this build's own assets and written to
//     Runtime\Server\challenge-data\season-N.json for the backend.
//  2. Human stat events are batched and sent to the backend every few seconds ("stats" events),
//     which matches them against the objective stat rows exactly like the native quest manager.
// ============================================================================================
#include <fstream>
#include <functional>
#include <iterator>
#include <cctype>
#include <algorithm>
extern void QueueSyndicateStats(AFortPlayerControllerAthena* PC, const std::string& StatsJson);
extern void SyndicateChallengeLog(const char* Text);

namespace SyndicateChallengeData
{
    static std::string Esc(const std::string& In) { std::string O; for (unsigned char C : In) { if (C == '"' || C == '\\') O += '\\'; if (C >= 32) O += (char)C; } return O; }
    static std::string Str(FName Name) { return std::string(Name.ToString().c_str()); }
    static std::string Lower(std::string S) { for (auto& C : S) C = (char)tolower((unsigned char)C); return S; }
    static std::string TagList(const FGameplayTagContainer& C, bool Parents)
    {
        std::string O = "["; bool First = true;
        auto Add = [&](const TArray<FGameplayTag>& A) { for (int i = 0; i < A.Num(); i++) { auto& T = A.Get(i, FGameplayTag::Size()); if (!First) O += ","; First = false; O += "\"" + Esc(Str(T.TagName)) + "\""; } };
        Add(C.GameplayTags); if (Parents) Add(C.ParentTags);
        return O + "]";
    }
    static int SeasonNumber()
    {
        auto V = VersionInfo.FortniteVersion;
        // 1.72 / 1.8x are numerically above 1.11 but belong to the pre-season and Season 1.
        if (V == 1.72 || V == 1.8 || V == 1.81 || V == 1.82) return 1;
        if (V < 1.11) return 1;
        if (V < 3.0) return 2;
        if (V < 4.0) return 3;
        return (int)V;
    }
    // True when Name contains Token followed by a non-digit (so "season1" does not match "season10").
    static bool HasToken(const std::string& Name, const std::string& Token)
    {
        for (size_t At = Name.find(Token); At != std::string::npos; At = Name.find(Token, At + 1))
        {
            auto End = At + Token.size();
            if (End >= Name.size() || !isdigit((unsigned char)Name[End])) return true;
        }
        return false;
    }
    static bool SeasonName(const std::string& Raw, int Season)
    {
        auto Name = Lower(Raw); auto N = std::to_string(Season);
        return HasToken(Name, "season" + N) || HasToken(Name, "season_" + N) || HasToken(Name, "_s" + N + "_") || HasToken(Name, "_s" + N);
    }

    struct Asset { std::wstring Path; std::string Name; };
    static std::vector<Asset> AssetsOfClass(const wchar_t* ClassName)
    {
        std::vector<Asset> Out;
        auto Helpers = FindClass("AssetRegistryHelpers"); auto Interface = FindClass("AssetRegistry"); auto Data = FindStruct("AssetData");
        if (!Helpers || !Interface || !Data) return Out;
        auto Obj = Helpers->GetDefaultObj(); auto Get = Obj->GetFunction("GetAssetRegistry"); if (!Get) return Out;
        uint8 Return[32]{}; Obj->ProcessEvent(Get, Return); auto Registry = *(UObject**)Return; if (!Registry) return Out;
        UFunction* Query = nullptr;
        for (auto F = Interface->GetChildren(); F; F = F->GetNext()) if (F->Name.ToString() == "GetAssetsByClass") Query = (UFunction*)F;
        if (!Query) return Out;
        auto Meta = Query->GetParamsNamed(); std::vector<uint8> Params(Meta.Size + 16, 0); FName Name(ClassName); uint32 ArrayOffset = uint32(-1);
        for (auto& P : Meta.NameOffsetMap)
        {
            if (P.Name == "ClassName" && P.ElementSize == sizeof(FName) && P.Offset + sizeof(FName) <= Meta.Size) memcpy(Params.data() + P.Offset, &Name, sizeof(FName));
            else if (P.Name == "OutAssetData") ArrayOffset = P.Offset;
            else if (P.Name == "bSearchSubClasses" && P.Offset < Meta.Size) Params[P.Offset] = 1;
        }
        if (ArrayOffset == uint32(-1)) return Out;
        Registry->ProcessEvent(Query, Params.data());
        auto& Found = *(TArray<uint8>*)(Params.data() + ArrayOffset);
        auto Size = Data->GetPropertiesSize(); auto PathOffset = Data->GetOffset("ObjectPath"); auto NameOffset = Data->GetOffset("AssetName");
        if (PathOffset == uint32(-1) || Size <= 0 || Found.Num() < 0 || Found.Num() > 200000) return Out;
        for (int i = 0; i < Found.Num(); i++)
        {
            auto Element = &Found.Get(i, Size);
            auto Path = GetFromOffset<FName>(Element, PathOffset);
            if (Path.ComparisonIndex == 0) continue;
            Asset A; A.Path = std::wstring(Path.ToWString().c_str());
            A.Name = NameOffset != uint32(-1) ? Str(GetFromOffset<FName>(Element, NameOffset)) : "";
            if (A.Name.empty()) { std::string P(A.Path.begin(), A.Path.end()); auto Dot = P.find_last_of('.'); A.Name = Dot == std::string::npos ? P : P.substr(Dot + 1); }
            Out.push_back(A);
        }
        return Out;
    }

    static std::unordered_map<std::string, std::string> QuestJson;
    static std::unordered_map<std::string, std::wstring> QuestPaths;
    static bool QuestPathsLoaded = false;

    static UObject* LoadPath(const std::wstring& Path, const UClass* Class) { return Path.empty() ? nullptr : const_cast<UObject*>(FindObject(Path.c_str(), Class)); }
    // Quests that are not in memory yet (daily challenges, early seasons' challenges) are loaded from the game files.
    static UObject* LoadQuestPath(const std::wstring& Path, const UClass* Class)
    {
        if (auto Found = LoadPath(Path, Class)) return Found;
        return Path.empty() || !SDK::Offsets::StaticLoadObject ? nullptr : const_cast<UObject*>(SDK::StaticLoadObject(Path.c_str(), Class));
    }

    // Style tokens (VTID_...): which cosmetic and which style they unlock, so the backend can add
    // the style to the cosmetic when a challenge rewards it.
    static std::unordered_map<std::string, std::wstring> VariantPaths;
    static bool VariantPathsLoaded = false;
    static std::string VariantInfo(const std::string& Name)
    {
        if (!VariantPathsLoaded) { VariantPathsLoaded = true; for (auto& A : AssetsOfClass(L"FortVariantTokenType")) VariantPaths[Lower(A.Name)] = A.Path; }
        auto It = VariantPaths.find(Lower(Name)); if (It == VariantPaths.end()) return "";
        static auto Class = FindClass("FortVariantTokenType"); if (!Class) return "";
        auto Token = LoadPath(It->second, Class); if (!Token) return "";
        auto ItemOffset = Token->GetOffset("cosmetic_item"), ChannelOffset = Token->GetOffset("VariantChanelTag"), TagOffset = Token->GetOffset("VariantNameTag");
        if (ItemOffset == uint32(-1) || ChannelOffset == uint32(-1) || TagOffset == uint32(-1)) return "";
        auto Item = GetFromOffset<UObject*>(Token, ItemOffset); if (!Item || !Item->Class) return "";
        auto Type = Str(Item->Class->Name);
        for (auto Suffix : { "ItemDefinition", "Definition" }) { auto At = Type.rfind(Suffix); if (At != std::string::npos && At + strlen(Suffix) == Type.size()) { Type.resize(At); break; } }
        return ",\"item\":\"" + Esc(Type + ":" + Str(Item->Name)) + "\",\"channel\":\"" + Esc(Str(GetFromOffset<FGameplayTag>(Token, ChannelOffset).TagName)) +
            "\",\"tag\":\"" + Esc(Str(GetFromOffset<FGameplayTag>(Token, TagOffset).TagName)) + "\"";
    }

    static void AddQuest(UFortQuestItemDefinition* Def, int Depth)
    {
        if (!Def) return;
        auto Id = Str(Def->Name);
        if (QuestJson.count(Id)) return;
        QuestJson[Id] = "";
        static auto ObjectiveStruct = FindStruct("FortMcpQuestObjectiveInfo");
        auto CountOffset = ObjectiveStruct ? ObjectiveStruct->GetOffset("Count") : uint32(-1);
        std::string Objectives = "[";
        for (int i = 0; i < Def->Objectives.Num(); i++)
        {
            auto& Objective = Def->Objectives.Get(i, FFortMcpQuestObjectiveInfo::Size());
            int Count = CountOffset != uint32(-1) ? *(int32*)((uint8*)&Objective + CountOffset) : 1;
            std::string Stats = "[";
            auto AddStat = [&](const std::string& S) { if (Stats.size() > 1) Stats += ","; Stats += S; };
            if (Objective.ObjectiveStatHandle.RowName.IsValid() && Objective.ObjectiveStatHandle.DataTable)
            {
                for (auto& [Key, Value] : Objective.ObjectiveStatHandle.DataTable->RowMap)
                {
                    if (Key != Objective.ObjectiveStatHandle.RowName) continue;
                    auto Row = (FFortQuestObjectiveStatTableRow*)Value;
                    AddStat("{\"type\":" + std::to_string((int)Row->Type) + ",\"target\":" + TagList(Row->TargetTagContainer, false) + ",\"source\":" + TagList(Row->SourceTagContainer, false) +
                        ",\"context\":" + TagList(Row->ContextTagContainer, false) + ",\"cond\":\"" + Esc(Row->Condition.Num() > 0 ? std::string(Row->Condition.ToString().c_str()) : std::string()) + "\"}");
                }
            }
            else if (FFortMcpQuestObjectiveInfo::HasInlineObjectiveStats())
            {
                for (int k = 0; k < Objective.InlineObjectiveStats.Num(); k++)
                {
                    auto& Stat = Objective.InlineObjectiveStats.Get(k, FFortQuestObjectiveStat::Size());
                    std::string Inline = "[";
                    for (int c = 0; c < Stat.TagConditions.Num(); c++)
                    {
                        auto& Condition = Stat.TagConditions.Get(c, FInlineObjectiveStatTagCheckEntry::Size());
                        auto& Tag = FInlineObjectiveStatTagCheckEntry::HasTag() ? Condition.Tag : Condition.tag;
                        if (Inline.size() > 1) Inline += ",";
                        Inline += "{\"tag\":\"" + Esc(Str(Tag.TagName)) + "\",\"kind\":" + std::to_string((int)Condition.Type) + ",\"require\":" + (Condition.Require ? "true" : "false") + "}";
                    }
                    Inline += "]";
                    AddStat("{\"type\":" + std::to_string((int)Stat.Type) + ",\"inline\":" + Inline + ",\"cond\":\"" + Esc(Stat.Condition.Num() > 0 ? std::string(Stat.Condition.ToString().c_str()) : std::string()) + "\"}");
                }
            }
            Stats += "]";
            if (Objectives.size() > 1) Objectives += ",";
            Objectives += "{\"name\":\"" + Esc(Lower(Str(Objective.BackendName))) + "\",\"count\":" + std::to_string(Count < 1 ? 1 : Count) + ",\"stats\":" + Stats + "}";
        }
        Objectives += "]";

        // Rewards: battle stars / XP, and the next stage of staged challenges.
        std::string Rewards = "[", Next = "[";
        std::vector<std::string> NextIds;
        auto RewardsOffset = Def->GetOffset("Rewards"); static auto PairStruct = FindStruct("FortItemQuantityPair");
        if (RewardsOffset != uint32(-1) && PairStruct)
        {
            auto& Array = GetFromOffset<TArray<uint8>>(Def, RewardsOffset); auto Size = PairStruct->GetPropertiesSize();
            auto IdOffset = PairStruct->GetOffset("ItemPrimaryAssetId"), QuantityOffset = PairStruct->GetOffset("Quantity");
            if (Size > 0 && IdOffset != uint32(-1) && Array.Num() >= 0 && Array.Num() < 64)
                for (int r = 0; r < Array.Num(); r++)
                {
                    auto Element = &Array.Get(r, Size);
                    auto Type = Str(GetFromOffset<FName>(Element, IdOffset)), Name = Str(GetFromOffset<FName>(Element, IdOffset + FGameplayTag::Size()));
                    int Quantity = QuantityOffset != uint32(-1) ? GetFromOffset<int32>(Element, QuantityOffset) : 1;
                    if (Rewards.size() > 1) Rewards += ",";
                    auto Variant = Lower(Type).find("varianttoken") != std::string::npos && !Name.empty() ? VariantInfo(Name) : std::string();
                    Rewards += "{\"type\":\"" + Esc(Type) + "\",\"name\":\"" + Esc(Name) + "\",\"qty\":" + std::to_string(Quantity) + Variant + "}";
                    if (Lower(Type) == "quest" && !Name.empty()) NextIds.push_back(Name);
                }
        }
        Rewards += "]";
        for (auto& N : NextIds) { if (Next.size() > 1) Next += ","; Next += "\"" + Esc(N) + "\""; }
        Next += "]";
        // "Complete N of these" challenges (e.g. ammo boxes in 7 of 19 named locations).
        auto NeededOffset = Def->GetOffset("ObjectiveCompletionCount");
        int Needed = NeededOffset != uint32(-1) ? GetFromOffset<int32>(Def, NeededOffset) : 0;
        bool Once = Def->HasbAthenaUpdateObjectiveOncePerMatch() && Def->bAthenaUpdateObjectiveOncePerMatch;
        bool Single = Def->HasbAthenaMustCompleteInSingleMatch() && Def->bAthenaMustCompleteInSingleMatch;
        QuestJson[Id] = "{\"objectives\":" + Objectives + ",\"rewards\":" + Rewards + ",\"next\":" + Next + ",\"needed\":" + std::to_string(Needed < 0 ? 0 : Needed) + ",\"oncePerMatch\":" + (Once ? "true" : "false") + ",\"singleMatch\":" + (Single ? "true" : "false") + "}";

        if (Depth >= 8 || NextIds.empty()) return;
        if (!QuestPathsLoaded) { QuestPathsLoaded = true; for (auto& A : AssetsOfClass(L"FortQuestItemDefinition")) QuestPaths[Lower(A.Name)] = A.Path; }
        for (auto& N : NextIds)
        {
            auto It = QuestPaths.find(Lower(N)); if (It == QuestPaths.end()) continue;
            AddQuest((UFortQuestItemDefinition*)LoadPath(It->second, UFortQuestItemDefinition::StaticClass()), Depth + 1);
        }
    }

    static std::wstring ServerFolder()
    {
        wchar_t Ready[32768]{}; if (!GetEnvironmentVariableW(L"TILTED_SERVER_READY_FILE", Ready, 32768)) return L"";
        std::wstring Folder = Ready; auto Slash = Folder.find_last_of(L"\\/"); if (Slash == std::wstring::npos) return L"";
        Folder.resize(Slash + 1); return Folder;
    }

    // Every cosmetic in this build (for the launcher's "Unlock all cosmetics" option):
    // Runtime\Server\cosmetics\season-N.json with MCP template ids such as AthenaCharacter:CID_001_...
    static void DumpCosmetics(int Season)
    {
        auto Folder = ServerFolder(); if (Folder.empty()) return;
        Folder += L"cosmetics"; CreateDirectoryW(Folder.c_str(), nullptr);
        auto File = Folder + L"\\season-" + std::to_wstring(Season) + L".json";
        char VersionText[32]; snprintf(VersionText, sizeof(VersionText), "%.2f", VersionInfo.FortniteVersion);
        { std::ifstream Existing(File); std::string Head((std::istreambuf_iterator<char>(Existing)), {}); if (Head.find(std::string("\"version\":\"") + VersionText + "\"") != std::string::npos) return; }
        static const std::pair<const wchar_t*, const char*> Types[] = {
            { L"AthenaCharacterItemDefinition", "AthenaCharacter" }, { L"AthenaBackpackItemDefinition", "AthenaBackpack" },
            { L"AthenaPickaxeItemDefinition", "AthenaPickaxe" }, { L"AthenaGliderItemDefinition", "AthenaGlider" },
            { L"AthenaDanceItemDefinition", "AthenaDance" }, { L"AthenaItemWrapDefinition", "AthenaItemWrap" },
            { L"AthenaSkyDiveContrailItemDefinition", "AthenaSkyDiveContrail" }, { L"AthenaLoadingScreenItemDefinition", "AthenaLoadingScreen" },
            { L"AthenaMusicPackItemDefinition", "AthenaMusicPack" } };
        std::string Items = "["; int Count = 0; std::unordered_map<std::string, bool> Seen;
        for (auto& [Class, Prefix] : Types)
            for (auto& A : AssetsOfClass(Class))
            {
                auto Lowered = Lower(A.Name);
                if (A.Name.empty() || Lowered.find("test") != std::string::npos || Lowered.find("npc") != std::string::npos || Lowered.find("_tbd") != std::string::npos || Lowered.find("random") != std::string::npos || Lowered.find("_dev_") != std::string::npos) continue;
                auto Id = std::string(Prefix) + ":" + A.Name;
                if (Seen.count(Lower(Id))) continue; Seen[Lower(Id)] = true;
                if (Items.size() > 1) Items += ",";
                Items += "\"" + Esc(Id) + "\""; ++Count;
            }
        Items += "]";
        if (!Count) return;
        auto Temp = File + L".tmp";
        { std::ofstream Stream(Temp, std::ios::binary | std::ios::trunc); Stream << "{\"version\":\"" << VersionText << "\",\"season\":" << Season << ",\"items\":" << Items << "}"; }
        MoveFileExW(Temp.c_str(), File.c_str(), MOVEFILE_REPLACE_EXISTING);
        char Line[160]; snprintf(Line, sizeof(Line), "cosmetics: exported %d cosmetics for season %d\n", Count, Season); SyndicateChallengeLog(Line);
    }

    // Save the World heroes in this build (for the launcher's STW hero pick, which the match server puts on the
    // player in Battle Royale): Runtime\Server\cosmetics\heroes-season-N.json = {"heroes":[{"id":"HID_...","path":"/Game/Heroes/..."}]}.
    // Only heroes under /Game/Heroes (Battle Royale's own HID_*_Athena_* heroes live under /Game/Athena).
    static bool StwHeroName(const std::string& Name)
    {
        auto L = Lower(Name);
        return L.rfind("hid_", 0) == 0 && L.find("test") == std::string::npos && L.find("npc") == std::string::npos && L.find("_tbd") == std::string::npos && L.find("_dev_") == std::string::npos;
    }
    static void DumpHeroes(int Season)
    {
        auto Folder = ServerFolder(); if (Folder.empty()) return;
        Folder += L"cosmetics"; CreateDirectoryW(Folder.c_str(), nullptr);
        auto File = Folder + L"\\heroes-season-" + std::to_wstring(Season) + L".json";
        char VersionText[32]; snprintf(VersionText, sizeof(VersionText), "%.2f", VersionInfo.FortniteVersion);
        { std::ifstream Existing(File); std::string Head((std::istreambuf_iterator<char>(Existing)), {}); if (Head.find(std::string("\"version\":\"") + VersionText + "\"") != std::string::npos) return; }
        std::string Heroes = "["; int Count = 0;
        for (auto& A : AssetsOfClass(L"FortHeroType"))
        {
            std::string Path(A.Path.begin(), A.Path.end());
            if (Path.rfind("/Game/Heroes/", 0) != 0 || !StwHeroName(A.Name)) continue;
            if (Heroes.size() > 1) Heroes += ",";
            Heroes += "{\"id\":\"" + Esc(A.Name) + "\",\"path\":\"" + Esc(Path) + "\"}"; ++Count;
        }
        Heroes += "]";
        if (!Count) return;
        auto Temp = File + L".tmp";
        { std::ofstream Stream(Temp, std::ios::binary | std::ios::trunc); Stream << "{\"version\":\"" << VersionText << "\",\"season\":" << Season << ",\"heroes\":" << Heroes << "}"; }
        MoveFileExW(Temp.c_str(), File.c_str(), MOVEFILE_REPLACE_EXISTING);
        char Line[160]; snprintf(Line, sizeof(Line), "cosmetics: exported %d Save the World heroes for season %d\n", Count, Season); SyndicateChallengeLog(Line);
    }

    // Every playlist in this build (Runtime\Server\cosmetics\playlists-season-N.txt, one "name path" per line), to see
    // which limited time modes a build still has.
    static void DumpPlaylists(int Season)
    {
        auto Folder = ServerFolder(); if (Folder.empty()) return;
        Folder += L"cosmetics"; CreateDirectoryW(Folder.c_str(), nullptr);
        auto File = Folder + L"\\playlists-season-" + std::to_wstring(Season) + L".txt";
        std::string Out; int Count = 0;
        for (auto& A : AssetsOfClass(L"FortPlaylistAthena")) { Out += A.Name + " " + std::string(A.Path.begin(), A.Path.end()) + "\n"; ++Count; }
        if (!Count) return;
        { std::ofstream Stream(File, std::ios::binary | std::ios::trunc); Stream << Out; }
        char Line[120]; snprintf(Line, sizeof(Line), "cosmetics: listed %d playlists for season %d\n", Count, Season); SyndicateChallengeLog(Line);
    }

    // Styles of every outfit, for "Unlock all cosmetics" (all styles owned):
    // Runtime\Server\cosmetics\styles-season-N.json = {"AthenaCharacter:CID_...":[{"channel":"Material","options":["Mat1",...]}]}.
    // Loading the outfit definitions is spread over many ticks (about 6 ms per tick) so the server
    // never stalls; it runs once per build version.
    struct StyleExportState { bool Done = true; size_t Next = 0; std::vector<Asset> List; std::vector<int> Kind; std::string Json; int Count = 0; int Season = 0; std::wstring File; std::string Version; };
    // Outfits, back blings, pickaxes and gliders (format 2). Format 1 had outfits only, so back bling and pickaxe edit
    // styles were never unlocked by "Unlock all"; a format 1 file is exported again.
    static const std::pair<const wchar_t*, const char*> StyleTypes[] = {
        { L"AthenaCharacterItemDefinition", "AthenaCharacter" }, { L"AthenaBackpackItemDefinition", "AthenaBackpack" },
        { L"AthenaPickaxeItemDefinition", "AthenaPickaxe" }, { L"AthenaGliderItemDefinition", "AthenaGlider" } };
    static StyleExportState StyleExport;
    static std::string TagTail(FName Tag)
    {
        auto T = Str(Tag);
        for (auto P : { "Cosmetics.Variant.Channel.", "Cosmetics.Variant.Property." }) if (T.rfind(P, 0) == 0) { T.erase(0, strlen(P)); break; }
        return T;
    }
    static void StartStyleExport(int Season)
    {
        if (VersionInfo.FortniteVersion < 4.0) return; // no outfit styles before Season 4
        auto Folder = ServerFolder(); if (Folder.empty()) return;
        Folder += L"cosmetics"; CreateDirectoryW(Folder.c_str(), nullptr);
        auto File = Folder + L"\\styles-season-" + std::to_wstring(Season) + L".json";
        char VersionText[32]; snprintf(VersionText, sizeof(VersionText), "%.2f", VersionInfo.FortniteVersion);
        { std::ifstream Existing(File); std::string Head((std::istreambuf_iterator<char>(Existing)), {}); if (Head.find(std::string("\"version\":\"") + VersionText + "\",\"format\":2") != std::string::npos) return; }
        StyleExport = StyleExportState{}; StyleExport.Done = false; StyleExport.Season = Season; StyleExport.File = File; StyleExport.Version = VersionText;
        for (int Kind = 0; Kind < int(std::size(StyleTypes)); ++Kind)
            for (auto& A : AssetsOfClass(StyleTypes[Kind].first)) { StyleExport.List.push_back(A); StyleExport.Kind.push_back(Kind); }
        StyleExport.Json = "{";
    }
    static void StyleExportStep()
    {
        if (StyleExport.Done) return;
        LARGE_INTEGER Freq, Start, Now; QueryPerformanceFrequency(&Freq); QueryPerformanceCounter(&Start);
        while (StyleExport.Next < StyleExport.List.size())
        {
            QueryPerformanceCounter(&Now);
            if (double(Now.QuadPart - Start.QuadPart) * 1000.0 / double(Freq.QuadPart) > 6.0) return;
            const int Kind = StyleExport.Kind[StyleExport.Next];
            auto& A = StyleExport.List[StyleExport.Next++];
            std::string ClassName(StyleTypes[Kind].first, StyleTypes[Kind].first + wcslen(StyleTypes[Kind].first));
            auto ItemClass = (const UClass*)FindClass(ClassName.c_str());
            auto Lowered = Lower(A.Name);
            if (A.Name.empty() || Lowered.find("test") != std::string::npos || Lowered.find("npc") != std::string::npos || Lowered.find("_tbd") != std::string::npos || Lowered.find("random") != std::string::npos) continue;
            auto Def = LoadPath(A.Path, ItemClass); if (!Def) continue;
            auto Off = Def->GetOffset("ItemVariants"); if (Off == uint32(-1)) continue;
            auto& Variants = GetFromOffset<TArray<UObject*>>(Def, Off); if (Variants.Num() <= 0 || Variants.Num() > 16) continue;
            std::string Channels;
            for (auto V : Variants)
            {
                if (!V) continue;
                auto ChannelOff = V->GetOffset("VariantChannelTag"); auto Fn = V->GetFunction("GetAllVariantTags");
                if (ChannelOff == uint32(-1) || !Fn || Fn->GetParamsNamed().Size != sizeof(FGameplayTagContainer)) continue;
                auto Channel = TagTail(GetFromOffset<FName>(V, ChannelOff));
                auto Tags = V->Call<FGameplayTagContainer>(Fn);
                std::string Options; int N = 0;
                for (auto Tag : Tags.GameplayTags) { auto O = TagTail(Tag.TagName); if (O.empty() || N >= 128) continue; Options += (N++ ? ",\"" : "\"") + Esc(O) + "\""; }
                Tags.GameplayTags.Free(); Tags.ParentTags.Free();
                if (Channel.empty() || !N) continue;
                Channels += (Channels.empty() ? "" : ",") + std::string("{\"channel\":\"") + Esc(Channel) + "\",\"options\":[" + Options + "]}";
            }
            if (Channels.empty()) continue;
            StyleExport.Json += (StyleExport.Count++ ? "," : "") + std::string("\"") + StyleTypes[Kind].second + ":" + Esc(A.Name) + "\":[" + Channels + "]";
        }
        StyleExport.Done = true; StyleExport.Json += "}";
        auto Temp = StyleExport.File + L".tmp";
        { std::ofstream Stream(Temp, std::ios::binary | std::ios::trunc); Stream << "{\"version\":\"" << StyleExport.Version << "\",\"format\":2,\"season\":" << StyleExport.Season << ",\"styles\":" << StyleExport.Json << "}"; }
        MoveFileExW(Temp.c_str(), StyleExport.File.c_str(), MOVEFILE_REPLACE_EXISTING);
        char Line[160]; snprintf(Line, sizeof(Line), "cosmetics: exported styles of %d outfits, back blings, pickaxes and gliders for season %d\n", StyleExport.Count, StyleExport.Season); SyndicateChallengeLog(Line);
        StyleExport.List.clear(); StyleExport.Json.clear();
    }

    static void Dump()
    {
        int Season = SeasonNumber(); if (VersionInfo.FortniteVersion == 1.72) Season = 0; // pre-season
        if (Season < 0 || Season > 12) return; // 12 = Chapter 2 Season 2 (12.50 Dev Kit); 11 = Chapter 2 Season 1
        // Pre-season and Season 1 export their cosmetics too (the backend shows only those in the
        // locker); they have no challenge data.
        DumpCosmetics(Season);
        DumpHeroes(Season);
        DumpPlaylists(Season);
        StartStyleExport(Season);
        auto Folder = ServerFolder(); if (Folder.empty()) return;
        Folder += L"challenge-data"; CreateDirectoryW(Folder.c_str(), nullptr);
        // Every quest name this build has (to see how a season names its challenges), once per build.
        {
            auto NamesFile = Folder + L"\\quest-names-season-" + std::to_wstring(Season) + L".txt";
            if (GetFileAttributesW(NamesFile.c_str()) == INVALID_FILE_ATTRIBUTES) {
                auto All = AssetsOfClass(L"FortQuestItemDefinition");
                std::vector<std::string> Names; for (auto& A : All) Names.push_back(A.Name);
                std::sort(Names.begin(), Names.end());
                std::ofstream Stream(NamesFile, std::ios::binary | std::ios::trunc);
                Stream << "# " << Names.size() << " quests (FortQuestItemDefinition) in this build\n";
                for (auto& N : Names) Stream << N << "\n";
                char Line[160]; snprintf(Line, sizeof(Line), "challenges: listed %d quest names for season %d\n", (int)Names.size(), Season); SyndicateChallengeLog(Line);
            }
        }
        if (Season < 2) return;
        auto File = Folder + L"\\season-" + std::to_wstring(Season) + L".json";
        char VersionText[32]; snprintf(VersionText, sizeof(VersionText), "%.2f", VersionInfo.FortniteVersion);
        { std::ifstream Existing(File); std::string Head((std::istreambuf_iterator<char>(Existing)), {}); if (Head.find(std::string("\"dump\":6,\"version\":\"") + VersionText + "\"") != std::string::npos) { SyndicateChallengeLog("challenges: season data already exported\n"); return; } }

        auto ScheduleClass = FindClass("FortChallengeBundleScheduleDefinition");
        auto BundleClass = FindClass("FortChallengeBundleItemDefinition");
        auto EntryStruct = FindStruct("FortChallengeBundleScheduleEntry");
        auto QuestEntryStruct = FindStruct("FortChallengeBundleQuestEntry");
        std::string Schedules = "[", Bundles = "[", Loose = "[";
        std::unordered_map<std::string, bool> BundleSeen;
        int ScheduleCount = 0;
        if (ScheduleClass && BundleClass && EntryStruct && QuestEntryStruct)
        {
            int32 EntrySize = (int32)EntryStruct->GetPropertiesSize(); uint32 BundleOffset = EntryStruct->GetOffset("ChallengeBundle"), UnlockTypeOffset = EntryStruct->GetOffset("UnlockType"), UnlockValueOffset = EntryStruct->GetOffset("UnlockValue");
            int32 QuestEntrySize = (int32)QuestEntryStruct->GetPropertiesSize(); uint32 QuestOffset = QuestEntryStruct->GetOffset("QuestDefinition");
            for (auto& A : AssetsOfClass(L"FortChallengeBundleScheduleDefinition"))
            {
                if (!SeasonName(A.Name, Season)) continue;
                auto Schedule = LoadPath(A.Path, ScheduleClass); if (!Schedule) continue;
                auto EntriesOffset = Schedule->GetOffset("ScheduleEntries"); if (EntriesOffset == uint32(-1) || BundleOffset == uint32(-1)) continue;
                auto& Entries = GetFromOffset<TArray<uint8>>(Schedule, EntriesOffset); if (Entries.Num() < 0 || Entries.Num() > 256) continue;
                std::string EntryJson = "[";
                for (int e = 0; e < Entries.Num(); e++)
                {
                    auto Element = &Entries.Get(e, EntrySize);
                    auto Bundle = const_cast<UObject*>(((FSoftObjectPtr*)(Element + BundleOffset))->InternalGet(BundleClass)); if (!Bundle) continue;
                    auto BundleId = Str(Bundle->Name);
                    int UnlockType = UnlockTypeOffset != uint32(-1) ? Element[UnlockTypeOffset] : 0;
                    int UnlockValue = UnlockValueOffset != uint32(-1) ? GetFromOffset<int32>(Element, UnlockValueOffset) : 0;
                    if (EntryJson.size() > 1) EntryJson += ",";
                    EntryJson += "{\"bundle\":\"" + Esc(BundleId) + "\",\"unlockType\":" + std::to_string(UnlockType) + ",\"unlockValue\":" + std::to_string(UnlockValue) + "}";
                    if (BundleSeen.count(BundleId)) continue;
                    BundleSeen[BundleId] = true;
                    auto InfosOffset = Bundle->GetOffset("QuestInfos"); std::string QuestIds = "[";
                    if (InfosOffset != uint32(-1) && QuestOffset != uint32(-1))
                    {
                        auto& Infos = GetFromOffset<TArray<uint8>>(Bundle, InfosOffset);
                        if (Infos.Num() >= 0 && Infos.Num() < 256)
                            for (int q = 0; q < Infos.Num(); q++)
                            {
                                auto Quest = (UFortQuestItemDefinition*)const_cast<UObject*>(((FSoftObjectPtr*)(&Infos.Get(q, QuestEntrySize) + QuestOffset))->InternalGet(UFortQuestItemDefinition::StaticClass()));
                                if (!Quest) continue;
                                AddQuest(Quest, 0);
                                if (QuestIds.size() > 1) QuestIds += ",";
                                QuestIds += "\"" + Esc(Str(Quest->Name)) + "\"";
                            }
                    }
                    QuestIds += "]";
                    if (Bundles.size() > 1) Bundles += ",";
                    Bundles += "{\"id\":\"" + Esc(BundleId) + "\",\"quests\":" + QuestIds + "}";
                }
                EntryJson += "]";
                // Calendar flag the schedule needs (Chapter 2 missions only show while it is an active calendar event).
                std::string Calendar;
                if (auto CalendarOffset = Schedule->GetOffset("CalendarEventTag"); CalendarOffset != uint32(-1)) Calendar = GetFromOffset<FString>(Schedule, CalendarOffset).ToString();
                if (Schedules.size() > 1) Schedules += ",";
                Schedules += "{\"id\":\"" + Esc(A.Name) + "\",\"calendar\":\"" + Esc(Calendar) + "\",\"entries\":" + EntryJson + "}";
                ++ScheduleCount;
            }
        }
        if (!ScheduleCount)
        {
            // No challenge bundle schedules in this build (early seasons): export this season's
            // Battle Pass quests directly.
            int Added = 0;
            for (auto& A : AssetsOfClass(L"FortQuestItemDefinition"))
            {
                if (Added >= 300 || !SeasonName(A.Name, Season)) continue;
                auto Quest = (UFortQuestItemDefinition*)LoadQuestPath(A.Path, UFortQuestItemDefinition::StaticClass()); if (!Quest) continue;
                AddQuest(Quest, 0); ++Added;
                if (Loose.size() > 1) Loose += ",";
                Loose += "\"" + Esc(Str(Quest->Name)) + "\"";
            }
        }
        // Daily challenges (AthenaDailyQuest_*, AthenaDaily_*): the backend hands out three a day.
        std::string Daily = "["; int DailyCount = 0;
        for (auto& A : AssetsOfClass(L"FortQuestItemDefinition"))
        {
            if (DailyCount >= 200 || Lower(A.Name).rfind("athenadaily", 0) != 0) continue;
            auto Quest = (UFortQuestItemDefinition*)LoadQuestPath(A.Path, UFortQuestItemDefinition::StaticClass()); if (!Quest) continue;
            AddQuest(Quest, 0); ++DailyCount;
            if (Daily.size() > 1) Daily += ",";
            Daily += "\"" + Esc(Str(Quest->Name)) + "\"";
        }
        Daily += "]";
        Schedules += "]"; Bundles += "]"; Loose += "]";
        std::string Quests = "{"; for (auto& [Id, Json] : QuestJson) { if (Json.empty()) continue; if (Quests.size() > 1) Quests += ","; Quests += "\"" + Esc(Id) + "\":" + Json; } Quests += "}";
        std::string Out = std::string("{\"dump\":6,\"version\":\"") + VersionText + "\",\"season\":" + std::to_string(Season) + ",\"schedules\":" + Schedules + ",\"bundles\":" + Bundles + ",\"loose\":" + Loose + ",\"daily\":" + Daily + ",\"quests\":" + Quests + "}";
        if (QuestJson.empty()) { SyndicateChallengeLog("challenges: no challenge data found in this build\n"); return; }
        auto Temp = File + L".tmp";
        { std::ofstream Stream(Temp, std::ios::binary | std::ios::trunc); Stream << Out; }
        MoveFileExW(Temp.c_str(), File.c_str(), MOVEFILE_REPLACE_EXISTING);
        char Line[200]; snprintf(Line, sizeof(Line), "challenges: exported season %d (%d schedules, %d quests, %d daily)\n", Season, ScheduleCount, (int)QuestJson.size(), DailyCount); SyndicateChallengeLog(Line);
    }

    struct PendingStat { long long Type; int Count; std::string Target, Source, Context; };
    static std::unordered_map<AActor*, std::vector<PendingStat>> Pending;
}

// A playlist's full path by its name (Playlist_...), from the asset registry; empty when this build has none.
std::wstring SyndicatePlaylistPath(const std::string& Name)
{
    using namespace SyndicateChallengeData;
    for (auto& A : AssetsOfClass(L"FortPlaylistAthena")) if (Lower(A.Name) == Lower(Name)) return A.Path;
    return L"";
}

// A Save the World hero (HID_...) by name, for the player's STW hero pick in Battle Royale. Looked up in the
// asset registry (any hero folder), else in the four class folders; null when this build lacks it.
UObject* SyndicateLoadStwHero(const std::string& Id)
{
    using namespace SyndicateChallengeData;
    if (Id.size() < 5 || Id.size() > 120 || !StwHeroName(Id)) return nullptr;
    for (unsigned char C : Id) if (!std::isalnum(C) && C != '_') return nullptr;
    auto Class = (const UClass*)FindClass("FortHeroType"); if (!Class) return nullptr;
    static std::unordered_map<std::string, std::wstring> Paths; static bool Loaded = false;
    if (!Loaded) { Loaded = true; for (auto& A : AssetsOfClass(L"FortHeroType")) if (StwHeroName(A.Name)) Paths[Lower(A.Name)] = A.Path; }
    std::vector<std::wstring> Candidates;
    if (auto It = Paths.find(Lower(Id)); It != Paths.end()) Candidates.push_back(It->second);
    std::wstring Wide(Id.begin(), Id.end());
    for (auto Folder : { L"Commando", L"Constructor", L"Ninja", L"Outlander" })
        Candidates.push_back(std::wstring(L"/Game/Heroes/") + Folder + L"/ItemDefinition/" + Wide + L"." + Wide);
    for (auto& Path : Candidates)
    {
        if (!SyndicateAssetAvailable(Path.c_str())) continue; // 1.11 asserts on a missing package
        if (auto Hero = LoadQuestPath(Path, Class)) return Hero;
    }
    return nullptr;
}

void SyndicateRecordStat(AActor* PlayerController, long long StatEvent, int32 Count, const FGameplayTagContainer& TargetTags, const FGameplayTagContainer& SourceTags, const FGameplayTagContainer& ContextTags)
{
    using namespace SyndicateChallengeData;
    if (!PlayerController || Count <= 0 || Count > 100000 || StatEvent < 0) return;
    auto PC = (AFortPlayerControllerAthena*)PlayerController;
    if (!PC->PlayerState) return;
    auto& List = Pending[PlayerController];
    if (List.size() >= 400) return;
    PendingStat Stat{ StatEvent, (int)Count, TagList(TargetTags, true), TagList(SourceTags, true), TagList(ContextTags, true) };
    for (auto& Existing : List)
        if (Existing.Type == Stat.Type && Existing.Target == Stat.Target && Existing.Source == Stat.Source && Existing.Context == Stat.Context) { Existing.Count += Stat.Count; return; }
    List.push_back(std::move(Stat));
}

// A stat for the backend's challenge progress with tags given as text (JSON arrays), for actions the
// engine never reports as quest stats on these servers (harvesting materials).
void SyndicateRecordStatText(AActor* PlayerController, long long StatEvent, int32 Count, const std::string& TargetJson, const std::string& SourceJson, const std::string& ContextJson)
{
    using namespace SyndicateChallengeData;
    if (!PlayerController || Count <= 0 || Count > 100000 || StatEvent < 0) return;
    auto PC = (AFortPlayerControllerAthena*)PlayerController;
    if (!PC->PlayerState) return;
    auto& List = Pending[PlayerController];
    if (List.size() >= 400) return;
    for (auto& Existing : List)
        if (Existing.Type == StatEvent && Existing.Target == TargetJson && Existing.Source == SourceJson && Existing.Context == ContextJson) { Existing.Count += Count; return; }
    List.push_back(PendingStat{ StatEvent, (int)Count, TargetJson, SourceJson, ContextJson });
}

// Kills/damage on bots are resolved by the bot system, not the engine; report them as stat events.
void SyndicateHumanStat(AFortPlayerControllerAthena* PC, AActor* Victim, int Kind, int Count)
{
    if (!PC || !Victim || Count <= 0) return;
    auto Quests = PC->GetQuestManager(1); if (!Quests) return;
    FGameplayTagContainer TargetTags{};
    auto Interface = (IGameplayTagAssetInterface*)Victim->GetInterface(IGameplayTagAssetInterface::StaticClass());
    if (Interface) { auto GetOwnedGameplayTags = (void(*)(IGameplayTagAssetInterface*, FGameplayTagContainer*))Interface->Vft[0x2]; GetOwnedGameplayTags(Interface, &TargetTags); }
    Quests->SendStatEvent(PC, Kind == 0 ? EFortQuestObjectiveStatEvent::GetKill() : EFortQuestObjectiveStatEvent::GetDamage(), Count, true, Victim, TargetTags);
    TargetTags.GameplayTags.Free(); TargetTags.ParentTags.Free();
}

void SyndicateChallengeTick(float DeltaSeconds)
{
    using namespace SyndicateChallengeData;
    static UWorld* World = nullptr; static float Elapsed = 0, Flush = 0; static bool Dumped = false;
    auto Now = UWorld::GetWorld();
    if (Now != World) { World = Now; Elapsed = 0; Pending.clear(); }
    if (!Now || !Now->GameState) return;
    Elapsed += DeltaSeconds; Flush += DeltaSeconds;
    if (!Dumped && Elapsed > 12.f) { Dumped = true; Dump(); }
    StyleExportStep();
    if (Flush < 3.f) return;
    Flush = 0;
    for (auto& [Actor, List] : Pending)
    {
        if (List.empty()) continue;
        std::string Json = "[";
        for (auto& S : List)
        {
            if (Json.size() > 1) Json += ",";
            Json += "{\"t\":" + std::to_string(S.Type) + ",\"n\":" + std::to_string(S.Count) + ",\"tt\":" + S.Target + ",\"st\":" + S.Source + ",\"ct\":" + S.Context + "}";
        }
        Json += "]";
        List.clear();
        QueueSyndicateStats((AFortPlayerControllerAthena*)Actor, Json);
    }
}
