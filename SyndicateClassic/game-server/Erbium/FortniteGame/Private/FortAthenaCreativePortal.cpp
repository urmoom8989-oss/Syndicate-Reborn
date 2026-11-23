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

// The game's own code, logged once per Creative server so it can be read offline: the client's
// OnRep_ClientPlaysetData (what makes a client stream an island in) and SetPlayset, then the LoadPlayset the scan found.
static void SyndicateCreativeDumpBytes(const char* Name, uintptr_t At, size_t Size)
{
	auto Base = (uintptr_t)GetModuleHandleW(nullptr);
	printf("[Creative] dump %s game+0x%llx size 0x%llx\n", Name, (unsigned long long)(At - Base), (unsigned long long)Size);
	for (size_t o = 0; o < Size; o += 32)
	{
		char Line[80]; int n = 0;
		for (size_t i = 0; i < 32 && o + i < Size; i++) n += snprintf(Line + n, sizeof(Line) - n, "%02X", ((const uint8_t*)At)[o + i]);
		printf("[Creative] %llx %s\n", (unsigned long long)(At - Base + o), Line);
	}
	fflush(stdout);
}
static void SyndicateCreativeDumpCode(UObject* Component, uintptr_t LoadPlayset)
{
	static bool Done = false;
	if (Done) return;
	Done = true;
	for (auto Name : { "OnRep_ClientPlaysetData", "SetPlayset" })
	{
		auto Fn = Component->GetFunction(Name);
		auto Exec = Fn ? (uintptr_t)Fn->ExecFunction : 0;
		if (!Exec) continue;
		SyndicateCreativeDumpBytes(Name, Exec, 0x80);
		int Bodies = 0; // the implementation: the first calls or jumps in the exec thunk
		for (size_t o = 0; o < 0x78 && Bodies < 2; o++)
		{
			auto P = (const uint8_t*)Exec + o;
			if (*P != 0xE8 && *P != 0xE9) continue;
			int32_t Rel; memcpy(&Rel, P + 1, 4);
			auto Target = (uintptr_t)(P + 5 + Rel), Base = (uintptr_t)GetModuleHandleW(nullptr);
			if (Target < Base || Target > Base + 0x8000000) continue;
			SyndicateCreativeDumpBytes((std::string(Name) + " call").c_str(), Target, 0x500);
			Bodies++;
			o += 4;
		}
	}
	if (LoadPlayset) SyndicateCreativeDumpBytes("LoadPlayset", LoadPlayset, 0x900);
}

// The island's level, streamed in on the server with the engine's own LoadLevelInstance and handed to the clients
// through the component's ClientPlaysetData, which is what LoadPlayset does. Used where the scanned LoadPlayset is not
// safe (the UE4 builds, Seasons 7 to X): before this the island existed nowhere and the player fell into the sea.
static bool SyndicateStreamPlayset(UPlaysetLevelStreamComponent* Component, UObject* Playset, AActor* Volume)
{
	auto World = UWorld::GetWorld();
	if (!World || !Component || !Playset || !Volume)
		return false;
	auto PlaysetOff = Playset->GetOffset("PlaysetToSpawn");
	if (PlaysetOff == uint32(-1))
	{
		printf("[Creative] stream: the playset has no PlaysetToSpawn\n");
		return false;
	}
	// TSoftObjectPtr<UWorld>: weak pointer and tag, then the soft object path, whose first field is the asset's FName.
	std::string AssetPath = ((FName*)((uint8*)Playset + PlaysetOff + 0x10))->ToString().c_str();
	std::string Package = AssetPath.substr(0, AssetPath.find('.'));
	if (Package.empty() || Package == "None")
	{
		printf("[Creative] stream: the playset names no level\n");
		return false;
	}

	FVector Location = Volume->K2_GetActorLocation();
	FRotator Rotation = Volume->K2_GetActorRotation();
	{
		auto UseOff = Playset->GetOffset("bUseLocationOffset"), OffsetOff = Playset->GetOffset("LocationOffset");
		if (UseOff != uint32(-1) && OffsetOff != uint32(-1) && *((const uint8*)Playset + UseOff))
		{
			auto Offset = *(const FVector*)((const uint8*)Playset + OffsetOff);
			Location.X += Offset.X; Location.Y += Offset.Y; Location.Z += Offset.Z;
		}
	}

	auto LSClass = FindClass("LevelStreamingDynamic");
	auto LSDefault = LSClass ? LSClass->GetDefaultObj() : nullptr;
	auto Fn = LSDefault ? LSDefault->GetFunction("LoadLevelInstance") : nullptr;
	if (!Fn)
	{
		printf("[Creative] stream: LoadLevelInstance is missing\n");
		return false;
	}
	auto Meta = Fn->GetParamsNamed();
	std::vector<uint8> Buf(Meta.Size, 0);
	std::wstring WidePackage(Package.begin(), Package.end());
	uint32 SuccessOff = uint32(-1), ReturnOff = uint32(-1);
	for (auto& P : Meta.NameOffsetMap)
	{
		if (P.Name == "WorldContextObject" && P.Offset + sizeof(void*) <= Buf.size())
			*(UObject**)(Buf.data() + P.Offset) = World;
		else if (P.Name == "LevelName" && P.Offset + sizeof(FString) <= Buf.size())
			new (Buf.data() + P.Offset) FString(WidePackage.c_str());
		else if (P.Name == "Location" && P.Offset + sizeof(FVector) <= Buf.size())
			memcpy(Buf.data() + P.Offset, &Location, sizeof(FVector));
		else if (P.Name == "Rotation" && P.Offset + sizeof(FRotator) <= Buf.size())
			memcpy(Buf.data() + P.Offset, &Rotation, sizeof(FRotator));
		else if (P.Name == "bOutSuccess")
			SuccessOff = P.Offset;
		else if (P.Name == "ReturnValue")
			ReturnOff = P.Offset;
	}
	LSDefault->ProcessEvent(Fn, Buf.data());
	bool Success = SuccessOff != uint32(-1) && SuccessOff < Buf.size() && Buf[SuccessOff] != 0;
	UObject* Streamed = ReturnOff != uint32(-1) && ReturnOff + sizeof(void*) <= Buf.size() ? *(UObject**)(Buf.data() + ReturnOff) : nullptr;
	if (!Success || !Streamed)
	{
		printf("[Creative] stream: LoadLevelInstance(%s) failed\n", Package.c_str());
		return false;
	}
	// The instance's own package name ("<level>_LevelInstance_N"): clients must stream it under the same name so the
	// island's actors replicate onto their copy.
	std::string Unique = Package;
	{
		auto WorldAssetOff = Streamed->GetOffset("WorldAsset");
		if (WorldAssetOff != uint32(-1))
		{
			std::string Name = ((FName*)((uint8*)Streamed + WorldAssetOff + 0x10))->ToString().c_str();
			if (!Name.empty() && Name != "None") Unique = Name.substr(0, Name.find('.'));
		}
	}
	// Load it now, so the island has collision before the player comes through the rift.
	if (auto Statics = FindClass("GameplayStatics"))
		if (auto Flush = Statics->GetDefaultObj()->GetFunction("FlushLevelStreaming"))
			Statics->GetDefaultObj()->Call<void>(Flush, World);

	// The clients stream the island in themselves when the component's ClientPlaysetData reaches them (OnRep). The
	// hub volumes' playset component is not a replicated component until the game's own LoadPlayset makes it one,
	// so with LoadPlayset skipped the data never left the server and the island never loaded for the player.
	if (auto SetReplicated = Component->GetFunction("SetIsReplicated"))
	{
		auto Info = SetReplicated->GetParamsNamed();
		std::vector<uint8> Params(Info.Size < 8 ? 8 : Info.Size, 0);
		Params[Info.NameOffsetMap.size() ? Info.NameOffsetMap[0].Offset : 0] = 1;
		Component->ProcessEvent(SetReplicated, Params.data());
		printf("[Creative] stream: the island's playset component now replicates\n");
	}
	else printf("[Creative] stream: SetIsReplicated is missing\n");

	auto& Data = Component->ClientPlaysetData;
	std::wstring WideUnique(Unique.begin(), Unique.end());
	if (FFortPlaysetStreamingData::HasPackageName()) Data.PackageName = FName(WidePackage.c_str());
	if (FFortPlaysetStreamingData::HasUniquePackageName()) Data.UniquePackageName = FName(WideUnique.c_str());
	if (FFortPlaysetStreamingData::HasLocation()) Data.Location = Location;
	if (FFortPlaysetStreamingData::HasRotation()) Data.Rotation = Rotation;
	Data.bValid = true;
	if (auto StreamedOff = Component->GetOffset("StreamedLevel"); StreamedOff != uint32(-1))
		*(UObject**)((uint8*)Component + StreamedOff) = Streamed;
	Volume->ForceNetUpdate();
	printf("[Creative] stream: island level %s loaded as %s at (%.0f,%.0f,%.0f)\n", Package.c_str(), Unique.c_str(),
		(double)Location.X, (double)Location.Y, (double)Location.Z);
	return true;
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
	// Season 8 crashes there too, with CurrentPlayset set: the call jumps to a bad address and the host exits right
	// after "match ready" (8.50, 2026-11-23). So it stays off on every UE4 build.
	bool Allowed = VersionInfo.EngineVersion >= 5.0 || GetEnvironmentVariableW(L"TILTED_CREATIVE_LOADPLAYSET", nullptr, 0) != 0;
	SyndicateCreativeDumpCode((UObject*)LevelStreamComponent, (uintptr_t)LoadPlayset);
	if (Allowed && LoadPlayset && IslandPlayset && LevelStreamComponent->HasCurrentPlayset() && LevelStreamComponent->CurrentPlayset)
	{
		printf("[Creative] calling the game's LoadPlayset\n");
		fflush(stdout);
		Loaded = SafeLoadPlayset(LoadPlayset, LevelStreamComponent);
		printf("[Creative] LoadPlayset %s (client data valid right after: %d)\n", Loaded ? "returned" : "faulted",
			LevelStreamComponent->HasClientPlaysetData() ? (int)LevelStreamComponent->ClientPlaysetData.bValid : -1);
		fflush(stdout);
	}
	printf("[Creative] playset set by SetPlayset=%d, LoadPlayset %s\n", (int)HadPlayset, !LoadPlayset ? "not found" : !Allowed ? "skipped on this build" : Loaded ? "ran" : "FAULTED or skipped (island not loaded)");
	// Where the game's LoadPlayset can't be used, stream the island ourselves.
	if (!Loaded && IslandPlayset)
		Loaded = SyndicateStreamPlayset(LevelStreamComponent, (UObject*)IslandPlayset, Portal->LinkedVolume);
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
