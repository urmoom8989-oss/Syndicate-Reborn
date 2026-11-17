#include "pch.h"
#include "../Public/FortAthenaCreativePortal.h"
#include "../Public/FortGameStateAthena.h"

void AFortMinigameSettingsBuilding::BeginPlay(AFortMinigameSettingsBuilding* Settings)
{
	if (Settings->HasSettingsVolume())
	{
	}

	return BeginPlayOG(Settings);
}

// LoadPlayset is found by scanning the game's code; if it still faults on some build (wrong state or
// a different signature), the island stays unloaded instead of the whole server crashing.
static bool SafeLoadPlayset(void (*Load)(UPlaysetLevelStreamComponent*), UPlaysetLevelStreamComponent* Component)
{
	__try { Load(Component); return true; }
	__except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

AFortAthenaCreativePortal* AFortAthenaCreativePortal::Create(AFortPlayerControllerAthena* PlayerController)
{
	auto GameState = (AFortGameStateAthena*)UWorld::GetWorld()->GameState;
	auto PlayerState = PlayerController->PlayerState;

	AFortAthenaCreativePortal* Portal = nullptr;

	if (!GameState->CreativePortalManager->HasAvailablePortals())
	{
		static TArray<AFortAthenaCreativePortal*> AvailablePortals, UsedPortals;
		if (AvailablePortals.Num() == 0 && UsedPortals.Num() == 0)
			for (auto& Portal__Uncasted : GameState->CreativePortalManager->AllPortals)
			{
				auto Portal = (AFortAthenaCreativePortal*)Portal__Uncasted;

				if (Portal->OwningPlayer.ReplicationBytes.Num() > 0) UsedPortals.Add(Portal);
				else AvailablePortals.Add(Portal);
			}

		if (AvailablePortals.Num() > 0)
		{
			Portal = (AFortAthenaCreativePortal*)AvailablePortals[0];
			AvailablePortals.Remove(0);
			UsedPortals.Add(Portal);
		}
	}
	else
	{
		if (GameState->CreativePortalManager->AvailablePortals.Num() > 0)
		{
			Portal = (AFortAthenaCreativePortal*)GameState->CreativePortalManager->AvailablePortals[0];
			GameState->CreativePortalManager->AvailablePortals.Remove(0);
			GameState->CreativePortalManager->UsedPortals.Add(Portal);
		}
	}

	if (!Portal)
		return nullptr;

	printf("[Creative] Assigned portal %s to %s\n", Portal->Name.ToString().c_str(), PlayerController->Name.ToString().c_str());

	if (Portal->HasbIsPublishedPortal())
	{
		Portal->bIsPublishedPortal = false;
		Portal->OnRep_PublishedPortal();
	}

	Portal->OwningPlayer = PlayerState->UniqueId;
	Portal->OnRep_OwningPlayer();
	Portal->bPortalOpen = true;
	Portal->OnRep_PortalOpen();
	// The rift is only drawn for a portal whose owner is "ready", whose island load was
	// started by a user and that is not in an error state (it was audible but invisible).
	if (Portal->HasPlayersReady())
	{
		Portal->PlayersReady.Add(PlayerState->UniqueId, FUniqueNetIdRepl::Size());
		Portal->OnRep_PlayersReady();
	}
	if (Portal->HasbUserInitiatedLoad())
		Portal->bUserInitiatedLoad = true;
	if (Portal->HasbInErrorState())
		Portal->bInErrorState = false;

	auto RestrictedPlotDefinition = FindObject<UFortCreativeRealEstatePlotItemDefinition>(L"/Game/Playgrounds/Items/Plots/Temperate_Medium.Temperate_Medium");

	if (!RestrictedPlotDefinition)
		RestrictedPlotDefinition = FindObject<UFortCreativeRealEstatePlotItemDefinition>(L"/CR_Legacy/Playgrounds/Items/Plots/Temperate_Medium.Temperate_Medium");

	auto IslandPlayset = RestrictedPlotDefinition->BasePlayset.Get();

	auto LevelSaveComponent = (UFortLevelSaveComponent*)Portal->LinkedVolume->GetComponentByClass(UFortLevelSaveComponent::StaticClass());

	if (!LevelSaveComponent->RestrictedPlotDefinition)
	{
		LevelSaveComponent->AccountIdOfOwner = PlayerState->UniqueId;
		LevelSaveComponent->bIsLoaded = true;
		LevelSaveComponent->bLoadPlaysetFromPlot = true;
		LevelSaveComponent->bAutoLoadFromRestrictedPlotDefinition = true;
		LevelSaveComponent->RestrictedPlotDefinition = RestrictedPlotDefinition;
		if (LevelSaveComponent->HasPlotPermissions())
			LevelSaveComponent->PlotPermissions.Permission = 1;
		else if (Portal->LinkedVolume->HasPlayspace())
		{
			auto PermissionComponent = (UPlayspaceComponent_CreativeToolsPermission*)Portal->LinkedVolume->Playspace->GetComponentByClass(UPlayspaceComponent_CreativeToolsPermission::StaticClass());

			PermissionComponent->AccountIdOfOwner = PlayerState->UniqueId;
			PermissionComponent->PlotPermissions.Permission = 1;
		}
		LevelSaveComponent->OnRep_AccountIdOfOwner();
	}

	auto LevelStreamComponent = (UPlaysetLevelStreamComponent*)Portal->LinkedVolume->GetComponentByClass(UPlaysetLevelStreamComponent::StaticClass());
	LevelStreamComponent->SetPlayset(IslandPlayset);
	// SetPlayset is a script function that may not exist in older builds; LoadPlayset reads the
	// component's CurrentPlayset (a null one was the 7.40 crash), so set it directly too.
	bool HadPlayset = LevelStreamComponent->HasCurrentPlayset() && LevelStreamComponent->CurrentPlayset;
	if (LevelStreamComponent->HasCurrentPlayset() && !LevelStreamComponent->CurrentPlayset)
		LevelStreamComponent->CurrentPlayset = IslandPlayset;
	
	static auto SettingsMachineClass = FindObject<UClass>("/Game/Athena/Items/Gameplay/MinigameSettingsControl/MinigameSettingsMachine.MinigameSettingsMachine_C");
	auto SettingsMachine = UWorld::SpawnActor<AMinigameSettingsMachine_C>(SettingsMachineClass, Portal->LinkedVolume->K2_GetActorLocation(), {}, Portal->LinkedVolume);

	if (SettingsMachine)
	{
		if (SettingsMachine->HasSettingsVolume())
		{
			SettingsMachine->SettingsVolume = Portal->LinkedVolume;
			SettingsMachine->OnRep_SettingsVolume();
		}
		if (SettingsMachine->HasCreativeLinkComponent())
			SettingsMachine->CreativeLinkComponent = (UFortCreativeVolumeLinkComponent*)Portal->LinkedVolume->GetComponentByClass(UFortCreativeVolumeLinkComponent::StaticClass());

		auto MinigameVolumeComponent = (UFortMinigameVolumeComponent*)Portal->LinkedVolume->GetComponentByClass(UFortMinigameVolumeComponent::StaticClass());
		if (MinigameVolumeComponent)
			MinigameVolumeComponent->CurrentMinigameSettingsMachine = SettingsMachine;
	}
	
	auto LoadPlayset = (void (*)(UPlaysetLevelStreamComponent*)) FindLoadPlayset();
	bool Loaded = false;
	// On the UE4 builds (Seasons 7-X) the function found by the string scan crashes inside (null
	// read, 7.40), and the crash reporter ends the process before the __except can catch it. Off
	// there unless TILTED_CREATIVE_LOADPLAYSET=1 is set for testing.
	bool Allowed = VersionInfo.EngineVersion >= 5.0 || GetEnvironmentVariableW(L"TILTED_CREATIVE_LOADPLAYSET", nullptr, 0) != 0;
	if (Allowed && LoadPlayset && IslandPlayset && LevelStreamComponent->HasCurrentPlayset() && LevelStreamComponent->CurrentPlayset)
		Loaded = SafeLoadPlayset(LoadPlayset, LevelStreamComponent);
	printf("[Creative] playset set by SetPlayset=%d, LoadPlayset %s\n", (int)HadPlayset, !LoadPlayset ? "not found" : !Allowed ? "skipped on this build" : Loaded ? "ran" : "FAULTED or skipped (island not loaded)");
	// The island volume's own playset is what the client streams in; it was never set, so the
	// island existed on the server only and the player fell into empty sea.
	if (Portal->LinkedVolume->HasCurrentPlayset())
	{
		Portal->LinkedVolume->CurrentPlayset = IslandPlayset;
		Portal->LinkedVolume->OnRep_CurrentPlayset();
	}
	{
		auto V = Portal->LinkedVolume->K2_GetActorLocation(), P = Portal->K2_GetActorLocation();
		printf("[Creative] portal %s at (%.0f,%.0f,%.0f) volume %s at (%.0f,%.0f,%.0f) playset=%s loadPlayset=%d clientDataValid=%d\n",
			Portal->Name.ToString().c_str(), (double)P.X, (double)P.Y, (double)P.Z, Portal->LinkedVolume->Name.ToString().c_str(), (double)V.X, (double)V.Y, (double)V.Z,
			IslandPlayset ? IslandPlayset->Name.ToString().c_str() : "none", LoadPlayset != nullptr,
			LevelStreamComponent->HasClientPlaysetData() ? (int)LevelStreamComponent->ClientPlaysetData.bValid : -1);
	}

	//PlayerController->CreativePlotLinkedVolume = Portal->LinkedVolume;
	//PlayerController->OnRep_CreativePlotLinkedVolume();

	Portal->LinkedVolume->VolumeState = 3;
	Portal->LinkedVolume->OnRep_VolumeState();

	// Island info (creator name, code): the rift shows these and stays hidden without them.
	{
		auto Prop = Portal->GetProperty("IslandInfo");
		auto Off = Portal->GetOffset("IslandInfo");
		auto Struct = Prop ? GetFromOffset<const UStruct*>(Prop, 0x70) : nullptr;
		if (Struct && Off != uint32(-1))
		{
			auto Base = (uint8*)Portal + Off;
			for (auto Field : { "CreatorName", "SupportCode" })
			{
				auto F = Struct->GetOffset(Field);
				if (F != uint32(-1)) *(FString*)(Base + F) = FString(L"Syndicate");
			}
			auto M = Struct->GetOffset("Mnemonic");
			if (M != uint32(-1)) *(FString*)(Base + M) = FString(L"0000-0000-0000");
		}
		// Season 7 has no IslandInfo struct: the same fields sit on the portal itself.
		else
		{
			for (auto Field : { "CreatorName", "UserDescription" })
			{
				auto F = Portal->GetOffset(Field);
				if (F != uint32(-1)) *(FString*)((uint8*)Portal + F) = FString(L"Syndicate");
			}
			auto M = Portal->GetOffset("Mnemonic");
			if (M != uint32(-1)) *(FString*)((uint8*)Portal + M) = FString(L"0000-0000-0000");
		}
		static bool Dumped = false;
		if (!Dumped)
		{
			Dumped = true;
			for (const UStruct* C = Portal->Class; C; C = (const UStruct*)C->GetSuper())
			{
				std::string CN = C->Name.ToString().c_str();
				if (CN == "Actor") break;
				for (const UField* F = C->GetChildren(); F; F = F->GetNext())
					printf("[Creative] portal %s.%s (%s)\n", CN.c_str(), F->GetName().ToString().c_str(), F->Class->Name.ToString().c_str());
			}
			if (Struct)
				for (const UField* F = Struct->GetChildren(); F; F = F->GetNext())
					printf("[Creative] IslandInfo.%s (%s)\n", F->GetName().ToString().c_str(), F->Class->Name.ToString().c_str());
		}
	}
	if (auto IslandInfo = Portal->GetFunction("OnRep_IslandInfo"))
		if (IslandInfo->GetParamsNamed().Size == 0)
			Portal->ProcessEvent(IslandInfo, nullptr);
	Portal->bAlwaysRelevant = true;
	// Hub portals start dormant: the server changed the portal (open, owner, island info) but a dormant
	// actor sends none of it, so the client kept the closed portal: the rift's sound plays locally but
	// its visuals never appear. Wake the portal and its island volume so the changes replicate.
	for (AActor* Actor : { (AActor*)Portal, (AActor*)Portal->LinkedVolume })
	{
		if (!Actor) continue;
		if (auto Flush = Actor->GetFunction("FlushNetDormancy")) Actor->ProcessEvent(Flush, nullptr);
		Actor->SetNetDormancy(1); // DORM_Awake
		Actor->ForceNetUpdate();
	}

	PlayerController->OwnedPortal = Portal;

	printf("[Creative] Setup portal!\n");

	PlayerController->CreativePlotLinkedVolume = PlayerController->GetCurrentVolume();
	PlayerController->OnRep_CreativePlotLinkedVolume();

	return nullptr;
}

void AFortAthenaCreativePortal::TeleportPlayerToLinkedVolume(UObject* Context, FFrame& Stack)
{
	AFortPlayerPawnAthena* PlayerPawn = nullptr;
	bool bUseSpawnTags;

	Stack.StepCompiledIn(&PlayerPawn);
	Stack.StepCompiledIn(&bUseSpawnTags);
	Stack.IncrementCode();
	auto Portal = (AFortAthenaCreativePortal*)Context;

	if (!PlayerPawn)
		return;

	auto PlayerController = (AFortPlayerControllerAthena*)PlayerPawn->Controller;

	// credit to andrew

	static auto CreativePhone = FindObject<UFortWeaponItemDefinition>(L"/Game/Athena/Items/Weapons/Prototype/WID_CreativeTool.WID_CreativeTool");

	auto ItemEntry = PlayerController->WorldInventory->Inventory.ReplicatedEntries.Search([&](FFortItemEntry& entry)
		{
			return entry.ItemDefinition == CreativePhone;
		}, FFortItemEntry::Size());

	if (!ItemEntry)
	{
		PlayerController->WorldInventory->GiveItem(CreativePhone, 1, AFortInventory::GetStats(CreativePhone)->ClipSize);
		PlayerController->ClientCreativePhoneCreated();
	}

	if (PlayerController->HasbIsCreativeQuickbarEnabled())
	{
		auto OldbIsCreativeQuickbarEnabled = PlayerController->bIsCreativeQuickbarEnabled;
		PlayerController->bIsCreativeQuickbarEnabled = true;
		PlayerController->OnRep_IsCreativeQuickbarEnabled(OldbIsCreativeQuickbarEnabled);
	}
	if (PlayerController->HasbIsCreativeQuickmenuEnabled())
		PlayerController->bIsCreativeQuickmenuEnabled = true;
	if (PlayerController->HasbIsCreativeModeEnabled())
	{
		PlayerController->bIsCreativeModeEnabled = true;
		PlayerController->OnRep_IsCreativeModeEnabled();
	}

	auto Location = Portal->LinkedVolume->K2_GetActorLocation();
	auto VolumeZ = Location.Z;
	Location.Z = 10000;
	printf("[Creative] teleporting %s through %s to volume %s (%.0f,%.0f, volume z %.0f)\n", PlayerController->Name.ToString().c_str(),
		Portal->Name.ToString().c_str(), Portal->LinkedVolume->Name.ToString().c_str(), (double)Location.X, (double)Location.Y, (double)VolumeZ);

	PlayerController->CreativePlotLinkedVolume = Portal->LinkedVolume;
	PlayerController->OnRep_CreativePlotLinkedVolume();

	PlayerPawn->K2_TeleportTo(Location, FRotator());
	PlayerPawn->BeginSkydiving(false);
}

void AFortAthenaCreativePortal::Hook()
{
	if (!GetDefaultObj())
		return;
	
	Utils::ExecHook(GetDefaultObj()->GetFunction("TeleportPlayerToLinkedVolume"), TeleportPlayerToLinkedVolume);
}

void AFortMinigameSettingsBuilding::Hook()
{
	if (!GetDefaultObj())
		return;
	
	//Utils::Hook(FindMinigameSettingsBuilding__BeginPlay(), BeginPlay, BeginPlayOG);
}
