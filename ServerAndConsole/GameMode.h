#pragma once
#include "framework.h"
#include "ServerBotManager.h"
#include "PlayerLocker.h"
#include "RetailRowFiends.h"
#include "WarmupSpawn.h"

#include "FortInventory.h"
#include "LateGame.h"
#include "AbilitySystemComponent.h"

namespace GameMode
{
bool (*ReadyToStartMatchOG)(AGameMode *);
bool ReadyToStartMatch(AFortGameModeAthena *GameMode)
{
    static bool bSetupPlaylist = false;
    static bool bInitialize = false;
    static bool bIsNetReady = false;
    static Syndicate::Ref<UNetDriver> ListeningDriver;
    static Syndicate::Ref<UWorld> SetupWorld;
    static Syndicate::Ref<AFortGameModeAthena> SetupMode;
    auto World = UWorld::GetWorld();
    if (!World)
        return false;
    if (SetupWorld.Get()!=World || SetupMode.Get()!=GameMode)
    {
        SetupWorld.Set(World); SetupMode.Set(GameMode);
        bSetupPlaylist = false;
        bInitialize = false;
        bIsNetReady = false;
        PlayerBots::ResetWorld();
        PlayerBots::ActiveWorld = World;
        Globals::ResetTeams();
        BotMutator = nullptr;
        PlayerStarts.Free();
        BuildingFoundations.Free();
        PickedSupplyDropLocations.Free();
        GivenLootPlayers.Free();
        PickupLifetimes.clear();
        AssetIdentity.clear();
        Characters.clear();
        Backpacks.clear();
        Pickaxes.clear();
        Gliders.clear();
        Contrails.clear();
        Dances.clear();
    }

    if (!GameMode)
    {
        return false;
    }

    AFortGameStateAthena *GameState = reinterpret_cast<AFortGameStateAthena *>(GameMode->GameState);
    if (!GameState)
    {
        return ReadyToStartMatchOG((AGameMode *)GameMode);
    }

    if (!bSetupPlaylist)
    {
        UFortPlaylistAthena *Playlist = nullptr;
        if (Globals::bEventEnabled)
        {
            Playlist = StaticLoadObject<UFortPlaylistAthena>(
                "/Game/Athena/Playlists/Music/Playlist_Music_High.Playlist_Music_High");
        }
        else
        {
            Playlist = StaticLoadObject<UFortPlaylistAthena>(
                "/Game/Athena/Playlists/Playlist_DefaultSolo.Playlist_DefaultSolo");
        }

        if (!Playlist)
        {
            Log("Failed to load Athena playlist; retrying on next ReadyToStartMatch tick.");
            return false;
        }

        bSetupPlaylist = true;
        Log(std::format("bSetupPlaylist = {}", bSetupPlaylist).c_str());

        int32 PlaylistId = Playlist->PlaylistId;
        Log(std::format("Playlist = {}", Playlist->GetName()).c_str());
        RetailRowFiends::AddRetailRowHorde(Playlist); // Retail Row fiends: before the playlist is applied

        GameState->CurrentPlaylistInfo.BasePlaylist = Playlist;
        GameState->CurrentPlaylistInfo.OverridePlaylist = Playlist;
        GameState->CurrentPlaylistInfo.PlaylistReplicationKey++;

        GameMode->CurrentPlaylistId = PlaylistId;
        GameMode->CurrentPlaylistName = Playlist->PlaylistName;
        if (!Globals::bEventEnabled)
        {
            GameMode->bDBNOEnabled = false;
            GameMode->bAlwaysDBNO = false;
            GameState->bCheatRespawnEnabled = false;
            GameState->bDBNOEnabledForGameMode = false;
        }
        GameMode->bNeverSpawnPickupsOnPawnDeath = false;
        ResolveBuildingAssets();

        GameState->bGameModeWillSkipAircraft = Playlist->bSkipAircraft;
        GameState->CachedSafeZoneStartUp = Playlist->SafeZoneStartUp;
        GameState->AirCraftBehavior = Playlist->AirCraftBehavior;
        GameState->CurrentPlaylistId = PlaylistId;

        if (GameMode->GameSession)
        {
            GameMode->GameSession->SessionName = UKismetStringLibrary::Conv_StringToName(L"GameSession");
            GameMode->GameSession->MaxPlayers = std::clamp(Playlist->MaxPlayers, 1, 100);
        }

        // Normal AI matches are solo: one player per team, no squad DBNO behavior.
        Globals::MaxPlayersPerTeam =
            Globals::bEventEnabled ? (Playlist->MaxSquadSize > 0 ? Playlist->MaxSquadSize : 1) : 1;
        Globals::NextTeamIndex = Playlist->DefaultFirstTeam;
        Globals::NextBotTeamIndex = 102; // 100 distinct solo teams (3 through 102).
    }

    if (!GameState->MapInfo)
    {
        return ReadyToStartMatchOG((AGameMode *)GameMode);
    }

    if (!bInitialize)
    {
        bInitialize = true;
        Log(std::format("bInitialize = {}", bInitialize).c_str());

        GameState->OnRep_CurrentPlaylistId();
        GameState->OnRep_CurrentPlaylistInfo();

        GameMode->bWorldIsReady = true;
        GameMode->bAllowSpectateAfterDeath = true;
        GameMode->MinRespawnDelay = 5.0f;
        GameMode->WarmupRequiredPlayerCount = 1;
        GameMode->WarmupCountdownDuration = 60.f;
        GameMode->WarmupEarlyCountdownDuration = 15.f;
        float EarlyPercent = GameMode->GameSession && GameMode->GameSession->MaxPlayers > 0
                                 ? std::clamp(static_cast<float>(Globals::MinPlayersForEarlyStart) /
                                                  GameMode->GameSession->MaxPlayers, 0.f, 1.f)
                                 : 0.95f;
        GameMode->DefaultWarmupEarlyRequiredPlayerPercent = EarlyPercent;
        if (auto Playlist = GameState->CurrentPlaylistInfo.BasePlaylist)
            Playlist->WarmupEarlyRequiredPlayerPercent = EarlyPercent;

        if (UWorld::GetWorld() && UWorld::GetWorld()->NavigationSystem)
        {
            if (auto Nav = Cast<UNavigationSystemV1>(UWorld::GetWorld()->NavigationSystem))
                Nav->bAutoCreateNavigationData = true;
        }

        if (auto BotManager = (UFortServerBotManagerAthena *)UGameplayStatics::SpawnObject(
                UFortServerBotManagerAthena::StaticClass(), GameMode))
        {
            GameMode->ServerBotManager = BotManager;
            BotManager->CachedGameState = GameState;
            BotManager->CachedGameMode = GameMode;

            BotMutator = SpawnActor<AFortAthenaMutator_Bots>({});
            if (BotMutator)
            {
                BotManager->CachedBotMutator = BotMutator;
                BotMutator->CachedGameMode = GameMode;
                BotMutator->CachedGameState = GameState;

                // Normal solo teams permit both human and bot opponents. Syndicate
                // chooses warmup roles; native damage rules still own warmup protection.
                BotMutator->bBotHostileToHumanPlayersOnly = false;
            }
            else
            {
                Log("BotMutator is nullptr!");
            }

            if (!GameMode->AIDirector)
            {
                Log("No AIDirector, Creating one automatically...");
                // Owned by the game mode and activated at once, as Erbium (SyndicateClassic) does (its Season 6
                // fiends work that way). Spawned without an owner and activated only at the bus, the director
                // stayed empty (no encounters, MaxEncounterActiveAliveAI 0), so the Retail Row horde never ran.
                // Syndicate still controls the bot population: the director runs encounters (fiends), not bots.
                AAthenaAIDirector *Director = SpawnActor<AAthenaAIDirector>({}, {}, GameMode);
                if (Director)
                {
                    GameMode->AIDirector = Director;
                    if (RetailRowFiends::Enabled())
                    {
                        Director->Activate();
                        RetailRowFiends::Activated = true;
                        Log("AI director spawned (owner: game mode) and activated for the Retail Row horde.");
                    }
                }
            }

            AFortAIGoalManager *GoalManager = SpawnActor<AFortAIGoalManager>({});
            if (GoalManager)
            {
                GameMode->AIGoalManager = GoalManager;
            }

            // Populate bot cosmetic pools once. The original OR/negative tests accepted
            // almost every object, including NPC/VIP/TBD and synced/owned-only emotes.
            for (int i = 0; i < UObject::GObjects->Num(); i++)
            {
                UObject *Obj = UObject::GObjects->GetByIndex(i);
                if (!Obj || Obj->IsDefaultObject())
                {
                    continue;
                }

                if (Obj->IsA(UAthenaCharacterItemDefinition::StaticClass()))
                {
                    std::string SkinsData = ((UAthenaCharacterItemDefinition *)Obj)->Name.ToString();
                    bool bLooksLikePlayableSkin =
                        SkinsData.contains("Athena_Commando") || SkinsData.contains("CID_Character");
                    bool bExcluded =
                        SkinsData.contains("CID_NPC") || SkinsData.contains("CID_VIP") || SkinsData.contains("CID_TBD");
                    if (bLooksLikePlayableSkin && !bExcluded)
                    {
                        Characters.push_back((UAthenaCharacterItemDefinition *)Obj);
                    }
                }
                else if (Obj->IsA(UAthenaBackpackItemDefinition::StaticClass()))
                {
                    Backpacks.push_back((UAthenaBackpackItemDefinition *)Obj);
                }
                else if (Obj->IsA(UAthenaPickaxeItemDefinition::StaticClass()))
                {
                    Pickaxes.push_back((UAthenaPickaxeItemDefinition *)Obj);
                }
                else if (Obj->IsA(UAthenaDanceItemDefinition::StaticClass()))
                {
                    std::string EmoteData = ((UAthenaDanceItemDefinition *)Obj)->Name.ToString();
                    if (EmoteData.contains("EID") && !EmoteData.contains("Sync") && !EmoteData.contains("Owned"))
                    {
                        Dances.push_back((UAthenaDanceItemDefinition *)Obj);
                    }
                }
                else if (Obj->IsA(UAthenaSkyDiveContrailItemDefinition::StaticClass()))
                {
                    Contrails.push_back((UAthenaSkyDiveContrailItemDefinition *)Obj);
                }
                else if (Obj->IsA(UAthenaGliderItemDefinition::StaticClass()))
                {
                    Gliders.push_back((UAthenaGliderItemDefinition *)Obj);
                }
            }

            for (auto P : Characters)
                RegisterCachedAsset(P);
            for (auto P : Backpacks)
                RegisterCachedAsset(P);
            for (auto P : Pickaxes)
                RegisterCachedAsset(P);
            for (auto P : Gliders)
                RegisterCachedAsset(P);
            for (auto P : Contrails)
                RegisterCachedAsset(P);
            for (auto P : Dances)
                RegisterCachedAsset(P);
            Log(std::format("Initialised Bots! Characters={} Backpacks={} Pickaxes={} Dances={} Gliders={}",
                            Characters.size(), Backpacks.size(), Pickaxes.size(), Dances.size(), Gliders.size())
                    .c_str());
        }
        else
        {
            Log("BotManager is nullptr!");
        }
    }

    if (!bIsNetReady)
    {
        FName NetDriverDefinition = UKismetStringLibrary::Conv_StringToName(L"GameNetDriver");
        // Native server travel may already attach a live listening driver.
        // Reuse it rather than attempting to bind a second socket on port 7777.
        auto Existing=World->NetDriver;
        if(Existing && Existing==ListeningDriver.Get() && Existing->World==World &&
           !Existing->ServerConnection && Existing->NetDriverName==NetDriverDefinition)
        {
            bIsNetReady=true;
            SetWorld(Existing,World);
            for(int I=0;I<World->LevelCollections.Num();++I) World->LevelCollections[I].NetDriver=Existing;
            Log("Native travel retained the listening driver for the renewed match.");
        }
        UNetDriver *Driver = bIsNetReady ? nullptr : CreateNetDriver(UEngine::GetEngine(), World, NetDriverDefinition);
        if (!Driver)
        {
            if(!bIsNetReady) Log("CreateNetDriver returned nullptr; retrying later.");
        }
        else
        {
            Driver->NetDriverName = NetDriverDefinition;
            Driver->World = UWorld::GetWorld();

            FString Error = FString();
            FURL NewURL{};
            NewURL.Port = 7777;

            if (!InitListen(Driver, UWorld::GetWorld(), NewURL, true, Error))
            {
                Log(std::format("UIpNetDriver::InitListen Error: {}", Error.ToString()).c_str());
            }
            else
            {
                bIsNetReady = true;
                ListeningDriver.Set(Driver); // Reuse only a driver whose listen call actually succeeded.
                Log(std::format("bIsNetReady = {}", bIsNetReady).c_str());

                UWorld::GetWorld()->NetDriver = Driver;
                SetWorld(Driver, UWorld::GetWorld());

                for (int i = 0; i < UWorld::GetWorld()->LevelCollections.Num(); i++)
                {
                    UWorld::GetWorld()->LevelCollections[i].NetDriver = Driver;
                }

                Log(std::format("IpNetDriver listening on port {}!", NewURL.Port).c_str());
                SetConsoleTitleW(
                    std::format(L"Syndicate 10.40 | IpNetDriver listening on port {}!", NewURL.Port).c_str());
            }
        }
    }

    bool bReady = ReadyToStartMatchOG((AGameMode *)GameMode);
    if (bReady)
    {
        if (PlayerStarts.Num() == 0)
        {
            UGameplayStatics::GetDefaultObj()->GetAllActorsOfClass(
                UWorld::GetWorld(), AFortPlayerStartWarmup::StaticClass(), &PlayerStarts);
        }
        if (BuildingFoundations.Num() == 0)
        {
            UGameplayStatics::GetAllActorsOfClass(UWorld::GetWorld(), ABuildingFoundation::StaticClass(),
                                                  &BuildingFoundations);
        }
    }

    return bReady;
}

APawn *SpawnDefaultPawnFor(AGameModeBase *GameMode, AController *NewPlayer, AActor *StartSpot)
{
    if (!GameMode || !NewPlayer || !StartSpot)
    {
        return nullptr;
    }

    auto PC = Cast<AFortPlayerControllerAthena>(NewPlayer);
    AFortPlayerStateAthena *PlayerState = PC ? (AFortPlayerStateAthena *)PC->PlayerState : nullptr;
    AFortPlayerPawnAthena *Pawn =
        Cast<AFortPlayerPawnAthena>(WarmupSpawn::Create(GameMode, NewPlayer, StartSpot));

    if (PC)
        Log(std::string("Native warmup pawn creation: ") + (Pawn ? "succeeded" : "failed"));
    if (PC && Pawn) Log(std::format("Warmup creation observation: pawn={} class={} health={} controller={} expected={}",Pawn->GetName(),Pawn->Class->GetName(),Pawn->GetHealth(),Pawn->Controller?Pawn->Controller->GetName():"none",PC->GetName()));

    if (PlayerState && Pawn)
    {
        // The player's own outfit, back bling, pickaxe and glider from the backend's saved locker.
        PlayerLocker::Apply(PC, PlayerState, Pawn);
        ApplyCharacterCustomization(PlayerState, Pawn);
        PlayerLocker::Show(PlayerState, Pawn);
    }

    return Pawn;
}

void (*HandleStartingNewPlayerOG)(AGameModeBase *This, AFortPlayerControllerAthena *NewPlayer);
void HandleStartingNewPlayer(AGameModeBase *This, AFortPlayerControllerAthena *NewPlayer)
{
    auto World = UWorld::GetWorld();
    auto GameMode = World ? Cast<AFortGameModeAthena>(World->AuthorityGameMode) : nullptr;
    auto GameState = World ? Cast<AFortGameStateAthena>(World->GameState) : nullptr;
    if (!NewPlayer || !GameMode || !GameState || !NewPlayer->PlayerState)
    {
        if (HandleStartingNewPlayerOG)
            HandleStartingNewPlayerOG(This, NewPlayer);
        return;
    }
    static std::vector<Syndicate::Ref<AFortPlayerControllerAthena>> Initialized;
    std::erase_if(Initialized, [](auto &R) { return !R.Get(); });
    for (auto &R : Initialized)
        if (R.Get() == NewPlayer)
        {
            if (HandleStartingNewPlayerOG)
                HandleStartingNewPlayerOG(This, NewPlayer);
            return;
        }
    Initialized.emplace_back(NewPlayer);

    AFortPlayerStateAthena *PlayerState = (AFortPlayerStateAthena *)NewPlayer->PlayerState;

    // Native loading RPCs own readiness. Pre-setting either loading flag suppresses
    // the native false-to-true transition that starts/possesses the warmup pawn.

    if (!NewPlayer->ClientQuickBars)
    {
        NewPlayer->ClientQuickBars = SpawnAActor<AFortQuickBars>();
        if (NewPlayer->ClientQuickBars)
        {
            NewPlayer->ClientQuickBars->SetOwner(NewPlayer);
        }
    }

    PlayerState->SquadId = PlayerState->TeamIndex - 3;
    PlayerState->OnRep_SquadId();

    FGameMemberInfo Member{};
    Member.MostRecentArrayReplicationKey = -1;
    Member.ReplicationID = -1;
    Member.ReplicationKey = -1;
    Member.TeamIndex = PlayerState->TeamIndex;
    Member.SquadId = PlayerState->SquadId;
    Member.MemberUniqueId = PlayerState->UniqueId;

    bool HasMember = false;
    for (auto &Existing : GameState->GameMemberInfoArray.Members)
        if (PlayerState->AreUniqueIDsIdentical(Existing.MemberUniqueId, Member.MemberUniqueId))
        {
            HasMember = true;
            break;
        }
    if (!HasMember)
    {
        GameState->GameMemberInfoArray.Members.Add(Member);
        GameState->GameMemberInfoArray.MarkItemDirty(
            GameState->GameMemberInfoArray.Members[GameState->GameMemberInfoArray.Members.Num() - 1]);
    }

    if (!NewPlayer->MatchReport)
    {
        NewPlayer->MatchReport = (UAthenaPlayerMatchReport *)UGameplayStatics::SpawnObject(
            UAthenaPlayerMatchReport::StaticClass(), NewPlayer);
    }

    for (int i = 0; i < GameMode->StartingItems.Num(); i++)
    {
        if (GameMode->StartingItems[i].Item && GameMode->StartingItems[i].Count > 0 &&
            !FortInventory::FindItemEntry(NewPlayer, GameMode->StartingItems[i].Item))
        {
            FortInventory::GiveItem(NewPlayer, GameMode->StartingItems[i].Item, GameMode->StartingItems[i].Count);
        }
    }

    auto DefaultPick = StaticLoadObject<UFortWeaponMeleeItemDefinition>(
        "/Game/Athena/Items/Weapons/WID_Harvest_Pickaxe_Athena_C_T01.WID_Harvest_Pickaxe_Athena_C_T01");
    FortInventory::SyncPlayerPickaxe(NewPlayer, Cast<AFortPlayerPawnAthena>(NewPlayer->Pawn), DefaultPick);

    UFortAbilitySet *DefaultAbilitySet = StaticLoadObject<UFortAbilitySet>(
        "/Game/Abilities/Player/Generic/Traits/DefaultPlayer/GAS_AthenaPlayer.GAS_AthenaPlayer");
    if (DefaultAbilitySet)
    {
        AbilitySystemComponent::GiveAbilitySet(DefaultAbilitySet, PlayerState);
    }

    FortInventory::Update(NewPlayer);
    // Prepare inventory/abilities before native start-location assignment. The
    // client's real loading acknowledgement completes the native spawn handshake.
    if (HandleStartingNewPlayerOG)
        HandleStartingNewPlayerOG(This, NewPlayer);
    Log(std::string("Native start location assigned; awaiting loading handshake: clientLoaded=") +
        (NewPlayer->bHasClientFinishedLoading ? "yes" : "no") + ", serverLoaded=" +
        (NewPlayer->bHasServerFinishedLoading ? "yes" : "no") + ", pawn=" +
        (NewPlayer->Pawn ? "yes" : "no"));
    return;
}

static inline void (*OriginalOnAircraftExitedDropZone)(AFortGameModeAthena *GameMode,
                                                       AFortAthenaAircraft *FortAthenaAircraft);
void OnAircraftExitedDropZone(AFortGameModeAthena *GM, AFortAthenaAircraft *Bus)
{
    if (Globals::bBotsEnabled && Bus)
        for (auto B : PlayerBots::PlayerBotArray)
            if (B && B->ControllerAlive() && !B->bHasJumpedFromBus)
                B->JumpFromBus(Bus, true);
    if (OriginalOnAircraftExitedDropZone)
        OriginalOnAircraftExitedDropZone(GM, Bus);
    LateGame::OnDropZoneExited(GM ? Cast<AFortGameStateAthena>(GM->GameState) : nullptr);
}

void (*OnAircraftEnteredDropZoneOG)(AFortGameModeAthena *, AFortAthenaAircraft *);
void OnAircraftEnteredDropZone(AFortGameModeAthena *GM, AFortAthenaAircraft *Bus)
{
    if (OnAircraftEnteredDropZoneOG)
        OnAircraftEnteredDropZoneOG(GM, Bus);
    if (Globals::bBotsEnabled && Bus)
        for (auto B : PlayerBots::PlayerBotArray)
            if (B && B->ControllerAlive() && !B->bHasJumpedFromBus)
            {
                B->ObservePhase(EAthenaGamePhase::Aircraft);
                B->JumpFromBus(Bus);
            }
}

EFortTeam (*PickTeamOG)(AFortGameModeAthena *GameMode, uint8_t PreferredTeam, AFortPlayerControllerAthena *Controller);
EFortTeam PickTeam(AFortGameModeAthena *GameMode, uint8_t PreferredTeam, AFortPlayerControllerAthena *Controller)
{
    if (!BotPopulation::SpawnInProgress && Globals::NextTeamIndex > Globals::NextBotTeamIndex)
        return PickTeamOG ? PickTeamOG(GameMode, PreferredTeam, Controller) : EFortTeam(Globals::NextTeamIndex);
    if (BotPopulation::SpawnInProgress && BotPopulation::ReservedTeam)
        return EFortTeam(BotPopulation::ReservedTeam);
    int Ret = Globals::NextTeamIndex;
    int MaxPerTeam = Globals::MaxPlayersPerTeam > 0 ? Globals::MaxPlayersPerTeam : 1;

    ++Globals::CurrentPlayersOnTeam;
    if (Globals::CurrentPlayersOnTeam >= MaxPerTeam)
    {
        Globals::NextTeamIndex++;
        Globals::CurrentPlayersOnTeam = 0;
    }

    return EFortTeam(Ret);
}

void HookAll()
{
    NativeBotAircraft::Initialise();
    InstallHook((LPVOID)(ImageBase + 0x11D8640), ReadyToStartMatch, (LPVOID *)&ReadyToStartMatchOG);

    InstallHook((LPVOID)(ImageBase + 0x11E1180), SpawnDefaultPawnFor, nullptr);

    InstallHook((LPVOID)(ImageBase + 0x15BEDE0), HandleStartingNewPlayer, (LPVOID *)&HandleStartingNewPlayerOG);

    InstallHook((LPVOID)(ImageBase + 0x11CF710), OnAircraftExitedDropZone, (LPVOID *)&OriginalOnAircraftExitedDropZone);

    InstallHook((LPVOID)(ImageBase + 0x11CF670), OnAircraftEnteredDropZone, (LPVOID *)&OnAircraftEnteredDropZoneOG);

    InstallHook((LPVOID)(ImageBase + 0x11D42B0), PickTeam, (LPVOID *)&PickTeamOG);

    Log("Hooked GameMode!");
}
} // namespace GameMode
