#include "pch.h"
#include "../Public/SyndicateDBNORecovery.h"
#include "../Public/FortPlayerPawnAthena.h"
#include "../Public/FortInventory.h"
#include "../Public/FortPlayerControllerAthena.h"
#include "../Public/FortWeapon.h"
#include "../Public/FortPhysicsPawn.h"
#include "../Public/FortKismetLibrary.h"

extern uint64_t ApplyCharacterCustomization;

void AFortPlayerPawnAthena::RefreshSeasonTwoAppearance(AFortPlayerPawnAthena* Pawn, AActor* State)
{
    if ((VersionInfo.FortniteVersion < 2.0 && VersionInfo.FortniteVersion != 1.11) || VersionInfo.FortniteVersion >= 3.0 || !Pawn || !State)
        return;

    // Season 2 stores a fixed character-part array on the player state.
    // Preserve selected cosmetics; supply defaults only for missing pieces.
    auto Offset = State->GetOffset("CharacterParts");
    if (Offset == (uint32)-1) Offset = State->GetOffset("LocalCharacterParts");
    if (Offset == (uint32)-1) return;
    auto& Parts = GetFromOffset<const UObject*[6]>(State, Offset);
    auto PartClass = FindClass("CustomCharacterPart");
    if (!PartClass) return;
    if (!Parts[0]) Parts[0] = StaticLoadObject(L"/Game/Characters/CharacterParts/Female/Medium/Heads/F_Med_Head1.F_Med_Head1", PartClass);
    if (!Parts[1]) Parts[1] = StaticLoadObject(L"/Game/Characters/CharacterParts/Female/Medium/Bodies/F_Med_Soldier_01.F_Med_Soldier_01", PartClass);
    if (!Parts[0] || !Parts[1]) return;

    Pawn->PlayerState = State;
    Pawn->OnRep_PlayerState();
    // Use the pawn's own part setter as well as player-state visualization:
    // this initializes replicated pawn appearance for the first warmup spawn.
    for (uint8 Part = 0; Part < 6; ++Part)
        if (Parts[Part]) Pawn->ServerChoosePart(Part, Parts[Part]);
    UFortKismetLibrary::UpdatePlayerCustomCharacterPartsVisualization(State);
    if (ApplyCharacterCustomization)
        ((void (*)(AActor*, AFortPlayerPawnAthena*))ApplyCharacterCustomization)(State, Pawn);
    if (auto SetHidden = Pawn->GetFunction("SetActorHiddenInGame"))
        Pawn->Call<void>(SetHidden, false);
    State->ForceNetUpdate();
    Pawn->ForceNetUpdate();
}

struct _Pad_0xC
{
	uint8_t Padding[0xC];
};

struct _Pad_0x18
{
	uint8_t Padding[0x18];
};

struct FFortPickupRequestInfo final
{
public:
	struct FGuid                                  SwapWithItem;                                      // 0x0000(0x0010)(ZeroConstructor, IsPlainOldData, NoDestructor, HasGetValueTypeHash, NativeAccessSpecifierPublic)
	float                                         FlyTime;                                           // 0x0010(0x0004)(ZeroConstructor, IsPlainOldData, NoDestructor, HasGetValueTypeHash, NativeAccessSpecifierPublic)
	struct _Pad_0xC                               Direction;                                         // 0x0014(0x000C)(ZeroConstructor, IsPlainOldData, NoDestructor, HasGetValueTypeHash, NativeAccessSpecifierPublic)
	uint8                                         bPlayPickupSound : 1;                              // 0x0020(0x0001)(BitIndex: 0x00, PropSize: 0x0001 (NoDestructor, HasGetValueTypeHash, NativeAccessSpecifierPublic))
	uint8                                         bIsAutoPickup : 1;                                 // 0x0020(0x0001)(BitIndex: 0x01, PropSize: 0x0001 (NoDestructor, HasGetValueTypeHash, NativeAccessSpecifierPublic))
	uint8                                         bUseRequestedSwap : 1;                             // 0x0020(0x0001)(BitIndex: 0x02, PropSize: 0x0001 (NoDestructor, HasGetValueTypeHash, NativeAccessSpecifierPublic))
	uint8                                         bTrySwapWithWeapon : 1;                            // 0x0020(0x0001)(BitIndex: 0x03, PropSize: 0x0001 (NoDestructor, HasGetValueTypeHash, NativeAccessSpecifierPublic))
	uint8                                         Pad_21[0x3];                                       // 0x0021(0x0003)(Fixing Struct Size After Last Property [ Dumper-7 ])
};

struct alignas(0x8) FFortPickupRequestInfoNew final
{
public:
	struct FGuid                                  SwapWithItem;                                      // 0x0000(0x0010)(ZeroConstructor, IsPlainOldData, NoDestructor, HasGetValueTypeHash, NativeAccessSpecifierPublic)
	float                                         FlyTime;                                           // 0x0010(0x0004)(ZeroConstructor, IsPlainOldData, NoDestructor, HasGetValueTypeHash, NativeAccessSpecifierPublic)
	uint8 Pad_1[0x4];
	struct _Pad_0x18                              Direction;                                        // 0x0014(0x000C)(ZeroConstructor, IsPlainOldData, NoDestructor, HasGetValueTypeHash, NativeAccessSpecifierPublic)
	uint8                                         bPlayPickupSound : 1;                              // 0x0020(0x0001)(BitIndex: 0x00, PropSize: 0x0001 (NoDestructor, HasGetValueTypeHash, NativeAccessSpecifierPublic))
	uint8                                         bIsAutoPickup : 1;                                 // 0x0020(0x0001)(BitIndex: 0x01, PropSize: 0x0001 (NoDestructor, HasGetValueTypeHash, NativeAccessSpecifierPublic))
	uint8                                         bUseRequestedSwap : 1;                             // 0x0020(0x0001)(BitIndex: 0x02, PropSize: 0x0001 (NoDestructor, HasGetValueTypeHash, NativeAccessSpecifierPublic))
	uint8                                         bTrySwapWithWeapon : 1;                            // 0x0020(0x0001)(BitIndex: 0x03, PropSize: 0x0001 (NoDestructor, HasGetValueTypeHash, NativeAccessSpecifierPublic))
	uint8                                         Pad_2[0x7];                                       // 0x0021(0x0003)(Fixing Struct Size After Last Property [ Dumper-7 ])
};

uint64_t SetPickupTarget_ = 0;
// Used when the game's SetPickupTarget could not be found: does what it does by hand.
static void SyndicateManualPickupTarget(AFortPickupAthena* Pickup, AFortPlayerPawnAthena* Pawn, float FlyTime, FVector& Direction, bool bPlayPickupSound)
{
	if (FFortPickupLocationData::HasbPlayPickupSound())
		Pickup->PickupLocationData.bPlayPickupSound = bPlayPickupSound;
	Pickup->PickupLocationData.PickupTarget = Pawn;
	Pickup->PickupLocationData.StartDirection = Direction;
	Pickup->PickupLocationData.FlyTime = FlyTime;
	Pickup->OnRep_PickupLocationData();
	Pickup->bPickedUp = true;
	Pickup->OnRep_bPickedUp();
	Pawn->IncomingPickups.Add(Pickup);
}

void SyndicateLeanMarkPickup(UObject* PawnObject);
void AFortPlayerPawnAthena::ServerHandlePickup_(UObject* Context, FFrame& Stack)
{
	SyndicateLeanMarkPickup(Context); // picking up with a full inventory swaps the held item out: not a Lean drink
	AFortPickupAthena* Pickup;
	float InFlyTime;
	FVector InStartDirection;
	bool bPlayPickupSound;
	Stack.StepCompiledIn(&Pickup);
	Stack.StepCompiledIn(&InFlyTime);
	Stack.StepCompiledIn(&InStartDirection);
	Stack.StepCompiledIn(&bPlayPickupSound);
	Stack.IncrementCode();
	auto Pawn = (AFortPlayerPawnAthena*)Context;
	if (!Pawn || !Pickup || Pickup->bPickedUp)
		return;

	/*Pickup->SetLifeSpan(5.f);
	if (FFortPickupLocationData::HasbPlayPickupSound())
		Pickup->PickupLocationData.bPlayPickupSound = bPlayPickupSound;
	Pickup->PickupLocationData.PickupTarget = Pawn;
	Pickup->PickupLocationData.StartDirection = InStartDirection;
	Pickup->PickupLocationData.FlyTime /= Pawn->PickupSpeedMultiplier;
	Pickup->OnRep_PickupLocationData();

	Pickup->bPickedUp = true;
	Pickup->OnRep_bPickedUp();


	Pawn->IncomingPickups.Add(Pickup);*/
	auto SetPickupTarget = (void(*&)(AFortPickupAthena*, AFortPlayerPawnAthena*, float, FVector, bool))SetPickupTarget_;

	if (!SetPickupTarget_) { float T = InFlyTime / (Pawn->HasPickupSpeedMultiplier() ? Pawn->PickupSpeedMultiplier : 1); SyndicateManualPickupTarget(Pickup, Pawn, T, InStartDirection, bPlayPickupSound); return; }
	SetPickupTarget(Pickup, Pawn, InFlyTime / (Pawn->HasPickupSpeedMultiplier() ? Pawn->PickupSpeedMultiplier : 1), InStartDirection, bPlayPickupSound);
}

void AFortPlayerPawnAthena::ServerHandlePickupInfo(UObject* Context, FFrame& Stack)
{
	SyndicateLeanMarkPickup(Context); // picking up with a full inventory swaps the held item out: not a Lean drink
	bool bTrySwapWithWeapon;
	bool bUseRequestedSwap;
	bool bPlayPickupSound;
	FGuid SwapWithItem;
	float FlyTime;
	FVector Direction;

	AFortPickupAthena* Pickup;
	Stack.StepCompiledIn(&Pickup);
	if (VersionInfo.FortniteVersion >= 20.00)
	{
		FFortPickupRequestInfoNew Params;
		Stack.StepCompiledIn(&Params);
		bTrySwapWithWeapon = Params.bTrySwapWithWeapon;
		bUseRequestedSwap = Params.bUseRequestedSwap;
		bPlayPickupSound = Params.bPlayPickupSound;
		SwapWithItem = Params.SwapWithItem;
		FlyTime = Params.FlyTime;
		Direction = *(FVector*)&Params.Direction;
	}
	else
	{
		FFortPickupRequestInfo Params;
		Stack.StepCompiledIn(&Params);
		bTrySwapWithWeapon = Params.bTrySwapWithWeapon;
		bUseRequestedSwap = Params.bUseRequestedSwap;
		bPlayPickupSound = Params.bPlayPickupSound;
		SwapWithItem = Params.SwapWithItem;
		FlyTime = Params.FlyTime;
		Direction = *(FVector*)&Params.Direction;
	}
	Stack.IncrementCode();
	auto Pawn = (AFortPlayerPawnAthena*)Context;

	if (!Pawn || !Pickup || Pickup->bPickedUp)
		return;

	if (bUseRequestedSwap && Pawn->CurrentWeapon && ((AFortWeapon*)Pawn->CurrentWeapon)->WeaponData && AFortInventory::IsPrimaryQuickbar(((AFortWeapon*)Pawn->CurrentWeapon)->WeaponData) && AFortInventory::IsPrimaryQuickbar(Pickup->PrimaryPickupItemEntry.ItemDefinition))
	{
		auto PlayerController = (AFortPlayerControllerAthena*)Pawn->Controller;
		/*auto SwapEntry = PlayerController->WorldInventory->Inventory.ReplicatedEntries.Search([&](FFortItemEntry& entry)
			{ return entry.ItemGuid == SwapWithItem; }, FFortItemEntry::Size());
		PlayerController->SwappingItemDefinition = SwapEntry; // proper af*/
		PlayerController->bTryPickupSwap = true;
	}

	auto SetPickupTarget = (void(*&)(AFortPickupAthena*, AFortPlayerPawnAthena*, float, FVector&, bool))SetPickupTarget_;

	if (!SetPickupTarget_) { float T = FlyTime / (Pawn->HasPickupSpeedMultiplier() ? Pawn->PickupSpeedMultiplier : 1); SyndicateManualPickupTarget(Pickup, Pawn, T, Direction, bPlayPickupSound); return; }
	SetPickupTarget(Pickup, Pawn, FlyTime / (Pawn->HasPickupSpeedMultiplier() ? Pawn->PickupSpeedMultiplier : 1), Direction, bPlayPickupSound);
	/*Pickup->SetLifeSpan(5.f);
	Pickup->PickupLocationData.bPlayPickupSound = bPlayPickupSound;
	Pickup->PickupLocationData.PickupGuid = Pickup->PrimaryPickupItemEntry.ItemGuid;
	Pickup->PickupLocationData.PickupTarget = Pawn;
	Pickup->PickupLocationData.FlyTime /= Pawn->PickupSpeedMultiplier;
	//Pickup->PickupLocationData.StartDirection = Params.Direction.QuantizeNormal();
	Pickup->OnRep_PickupLocationData();

	Pickup->bPickedUp = true;
	Pickup->OnRep_bPickedUp();


	Pawn->IncomingPickups.Add(Pickup);*/
}


void AFortPlayerPawnAthena::ServerHandlePickupWithRequestedSwap(UObject* Context, FFrame& Stack)
{
	SyndicateLeanMarkPickup(Context); // picking up with a full inventory swaps the held item out: not a Lean drink
	AFortPickupAthena* Pickup;
	FGuid Swap;
	float InFlyTime;
	FVector InStartDirection;
	bool bPlayPickupSound;
	Stack.StepCompiledIn(&Pickup);
	Stack.StepCompiledIn(&Swap);
	Stack.StepCompiledIn(&InFlyTime);
	Stack.StepCompiledIn(&InStartDirection);
	Stack.StepCompiledIn(&bPlayPickupSound);
	Stack.IncrementCode();

	auto Pawn = (AFortPlayerPawnAthena*)Context;

	if (!Pawn || !Pickup || Pickup->bPickedUp)
		return;
	auto PlayerController = (AFortPlayerControllerAthena*)Pawn->Controller;

	// This RPC is also used for ammo/resources. Those never occupy a weapon slot.
	if (PlayerController && PlayerController->HasbTryPickupSwap())
		PlayerController->bTryPickupSwap = Pawn->CurrentWeapon && ((AFortWeapon*)Pawn->CurrentWeapon)->WeaponData && Pickup->PrimaryPickupItemEntry.ItemDefinition &&
			AFortInventory::IsPrimaryQuickbar(Pickup->PrimaryPickupItemEntry.ItemDefinition) &&
			AFortInventory::IsPrimaryQuickbar(((AFortWeapon*)Pawn->CurrentWeapon)->WeaponData);

	auto SetPickupTarget = (void(*&)(AFortPickupAthena*, AFortPlayerPawnAthena*, float, FVector&, bool))SetPickupTarget_;

	if (!SetPickupTarget_) { float T = InFlyTime / (Pawn->HasPickupSpeedMultiplier() ? Pawn->PickupSpeedMultiplier : 1); SyndicateManualPickupTarget(Pickup, Pawn, T, InStartDirection, bPlayPickupSound); return; }
	SetPickupTarget(Pickup, Pawn, InFlyTime / (Pawn->HasPickupSpeedMultiplier() ? Pawn->PickupSpeedMultiplier : 1), InStartDirection, bPlayPickupSound);
	/*Pickup->SetLifeSpan(5.f);
	Pickup->PickupLocationData.bPlayPickupSound = bPlayPickupSound;
	Pickup->PickupLocationData.PickupGuid = Pickup->PrimaryPickupItemEntry.ItemGuid;
	Pickup->PickupLocationData.PickupTarget = Pawn;
	Pickup->PickupLocationData.FlyTime /= Pawn->PickupSpeedMultiplier;
	//Pickup->PickupLocationData.StartDirection = Params.Direction.QuantizeNormal();
	Pickup->OnRep_PickupLocationData();

	Pickup->bPickedUp = true;
	Pickup->OnRep_bPickedUp();


	Pawn->IncomingPickups.Add(Pickup);*/
}


bool AFortPlayerPawnAthena::FinishedTargetSpline(void* _Pickup)
{
	auto Pickup = (AFortPickupAthena*)_Pickup;

	auto Pawn = (AFortPlayerPawnAthena*)Pickup->PickupLocationData.PickupTarget;
	if (!Pawn)
		return FinishedTargetSplineOG(Pickup);

	auto PlayerController = (AFortPlayerControllerAthena*)Pawn->Controller;
	if (!PlayerController || !PlayerController->WorldInventory || !Pickup->PrimaryPickupItemEntry.ItemDefinition)
		return FinishedTargetSplineOG(Pickup);

	// Consume a swap request once. A stale request must never eject secondary-bar supplies.
	const bool RequestedSwap = PlayerController->HasbTryPickupSwap() && PlayerController->bTryPickupSwap;
	if (PlayerController->HasbTryPickupSwap()) PlayerController->bTryPickupSwap = false;

	//if (auto entry = PlayerController->HasSwappingItemDefinition() ? (FFortItemEntry*)PlayerController->SwappingItemDefinition : nullptr)
	if (RequestedSwap && Pawn->CurrentWeapon && ((AFortWeapon*)Pawn->CurrentWeapon)->WeaponData &&
		AFortInventory::IsPrimaryQuickbar(Pickup->PrimaryPickupItemEntry.ItemDefinition) &&
		AFortInventory::IsPrimaryQuickbar(((AFortWeapon*)Pawn->CurrentWeapon)->WeaponData))
	{
		FVector FinalLoc = Pawn->K2_GetActorLocation();

		FVector ForwardVector = Pawn->GetActorForwardVector();
		ForwardVector.Z = 0.0f;
		ForwardVector.Normalize();

		FinalLoc = FinalLoc + ForwardVector * 450.f;
		FinalLoc.Z += 50.f;

		const float RandomAngleVariation = ((float)rand() * 0.00109866634f) - 18.f;
		const float FinalAngle = RandomAngleVariation * 0.017453292519943295f;

		FinalLoc.X += cos(FinalAngle) * 100.f;
		FinalLoc.Y += sin(FinalAngle) * 100.f;

		if (AFortInventory::IsPrimaryQuickbar(((AFortWeapon*)Pawn->CurrentWeapon)->WeaponData) && AFortInventory::IsPrimaryQuickbar(Pickup->PrimaryPickupItemEntry.ItemDefinition))
		{
			PlayerController->bTryPickupSwap = false;

			auto entry = PlayerController->WorldInventory->Inventory.ReplicatedEntries.Search([&](FFortItemEntry& entry)
				{ return entry.ItemGuid == ((AFortWeapon*)Pawn->CurrentWeapon)->ItemEntryGuid; }, FFortItemEntry::Size());
			if (!entry) {
				PlayerController->InternalPickup(&Pickup->PrimaryPickupItemEntry);
				return FinishedTargetSplineOG(Pickup);
			}

			AFortInventory::SpawnPickup(PlayerController->GetViewTarget()->K2_GetActorLocation(), *entry, EFortPickupSourceTypeFlag::GetPlayer(), EFortPickupSpawnSource::GetUnset(), PlayerController->MyFortPawn, -1, true, true, true, nullptr, FinalLoc);
			// SwapEntry(PC, *entry, Pickup->PrimaryPickupItemEntry);
			PlayerController->WorldInventory->Remove(entry->ItemGuid);
			auto Item = PlayerController->WorldInventory->GiveItem(Pickup->PrimaryPickupItemEntry);
			PlayerController->ServerExecuteInventoryItem(Item->ItemEntry.ItemGuid);
			/*if (VersionInfo.FortniteVersion < 3)
			{
				auto& QuickBar = (AFortInventory::IsPrimaryQuickbar(Item->ItemEntry.ItemDefinition) || Item->ItemEntry.ItemDefinition->ItemType == EFortItemType::GetWeaponHarvest()) ? PlayerController->QuickBars->PrimaryQuickBar : PlayerController->QuickBars->SecondaryQuickBar;
				int i = 0;
				for (i = 0; i < QuickBar.Slots.Num(); i++)
				{
					auto& Slot = QuickBar.Slots.Get(i, FQuickBarSlot::Size());

					for (auto& SlotItem : Slot.Items)
						if (SlotItem == Item->ItemEntry.ItemGuid)
						{
							PlayerController->QuickBars->ServerActivateSlotInternal(!(AFortInventory::IsPrimaryQuickbar(Item->ItemEntry.ItemDefinition) || Item->ItemEntry.ItemDefinition->ItemType == EFortItemType::GetWeaponHarvest()), i, 0.f, true);
							break;
						}
				}
			}
			else
				PlayerController->ClientEquipItem(Item->ItemEntry.ItemGuid, true);*/
		}
		else
			AFortInventory::SpawnPickup(PlayerController->GetViewTarget()->K2_GetActorLocation(), Pickup->PrimaryPickupItemEntry, EFortPickupSourceTypeFlag::GetPlayer(), EFortPickupSpawnSource::GetUnset(), PlayerController->MyFortPawn, -1, true, true, true, nullptr, FinalLoc);

	}
	else
		PlayerController->InternalPickup(&Pickup->PrimaryPickupItemEntry);

	return FinishedTargetSplineOG(Pickup);
}


uint64_t OnRep_ZiplineState = 0;
void AFortPlayerPawnAthena::ServerSendZiplineState(UObject* Context, FFrame& Stack)
{
	FZiplinePawnState State;

	Stack.StepCompiledIn(&State);
	Stack.IncrementCode();

	auto Pawn = (AFortPlayerPawnAthena*)Context;

	if (!Pawn)
		return;

	auto Zipline = Pawn->GetActiveZipline();

	auto PreviousState = Pawn->ZiplineState;

	memcpy((PBYTE)&Pawn->ZiplineState, (const PBYTE)&State, FZiplinePawnState::Size());

	if (OnRep_ZiplineState)
		((void (*)(AFortPlayerPawnAthena*)) OnRep_ZiplineState)(Pawn);

	if (State.bJumped)
	{
		auto Velocity = Pawn->CharacterMovement->Velocity;
		auto VelocityX = Velocity.X * -0.5f;
		auto VelocityY = Velocity.Y * -0.5f;
		Pawn->LaunchCharacterJump(FVector{ VelocityX >= -750 ? min(VelocityX, 750) : -750, VelocityY >= -750 ? min(VelocityY, 750) : -750, 1200 }, false, false, true, true);
	}

	auto NewZipline = Pawn->GetActiveZipline();

	static auto ZipLineClass = FindObject<UClass>(L"/Ascender/Gameplay/Ascender/B_Athena_Zipline_Ascender.B_Athena_Zipline_Ascender_C");
	if (auto Ascender = Zipline->Cast<AFortAscenderZipline>(ZipLineClass))
	{
		Ascender->PawnUsingHandle = nullptr;
		Ascender->PreviousPawnUsingHandle = Pawn;
		Ascender->OnRep_PawnUsingHandle();
	}
	else if (auto Ascender = NewZipline->Cast<AFortAscenderZipline>(ZipLineClass))
	{
		Ascender->PawnUsingHandle = Pawn;
		Ascender->PreviousPawnUsingHandle = nullptr;
		Ascender->OnRep_PawnUsingHandle();
	}
}


void AFortPlayerPawnAthena::OnCapsuleBeginOverlap_(UObject* Context, FFrame& Stack)
{
    // Preserve the engine frame: the reflected HitResult layout varies by build.
    auto OriginalStack = Stack;
    auto ForwardOriginal = [&]() { Stack = OriginalStack; if (Context && OnCapsuleBeginOverlap_OG) OnCapsuleBeginOverlap_OG(Context, Stack); };
	UObject* OverlappedComp;
	AActor* OtherActor;
	UObject* OtherComp;
	int32 OtherBodyIndex;
	bool bFromSweep;
	struct { uint8_t Padding[0x100]; } SweepResult{};
	Stack.StepCompiledIn(&OverlappedComp);
	Stack.StepCompiledIn(&OtherActor);
	Stack.StepCompiledIn(&OtherComp);
	Stack.StepCompiledIn(&OtherBodyIndex);
	Stack.StepCompiledIn(&bFromSweep);
	Stack.StepCompiledIn(&SweepResult);
	Stack.IncrementCode();

	auto Pawn = (AFortPlayerPawnAthena*)Context;

	static auto FortPCClass = FindClass("FortPlayerController");

	auto Pickup = OtherActor ? OtherActor->Cast<AFortPickupAthena>() : nullptr;
    if (!Pickup) return ForwardOriginal();
    // Pickup overlaps are handled by the server inventory path below. Do not
    // also execute Fortnite's local-player auto-pickup path for synthetic bots,
    // or re-enter it after ServerHandlePickup has consumed the pickup.
    if (!Pawn || !Pawn->Controller || !Pawn->Controller->IsA(FortPCClass) ||
        !Pickup->PrimaryPickupItemEntry.ItemDefinition ||
        !((AFortPlayerControllerAthena*)Pawn->Controller)->WorldInventory) return;
    if (Pickup->bPickedUp) return;

	auto MaxStack = Pickup->PrimaryPickupItemEntry.ItemDefinition->GetMaxStackSize();
	auto itemEntry = ((AFortPlayerControllerAthena*)Pawn->Controller)->WorldInventory->Inventory.ReplicatedEntries.Search([&](FFortItemEntry& entry)
		{ return entry.ItemDefinition == Pickup->PrimaryPickupItemEntry.ItemDefinition && entry.Count <= MaxStack; }, FFortItemEntry::Size());

	if (Pickup && Pickup->PawnWhoDroppedPickup != Pawn)
	{
		if ((!itemEntry && ((Pickup->PrimaryPickupItemEntry.ItemDefinition->HasbForceAutoPickup() && (Pickup->PrimaryPickupItemEntry.ItemDefinition->HasbForceAutoPickup() ? Pickup->PrimaryPickupItemEntry.ItemDefinition->bForceAutoPickup : (Pickup->PrimaryPickupItemEntry.ItemDefinition->GetPickupComponent() ? Pickup->PrimaryPickupItemEntry.ItemDefinition->GetPickupComponent()->bForceAutoPickup : false))) || !AFortInventory::IsPrimaryQuickbar(Pickup->PrimaryPickupItemEntry.ItemDefinition))) || (itemEntry && itemEntry->Count < MaxStack))
			Pawn->ServerHandlePickup(Pickup, Pickup->PickupLocationData.FlyTime, FVector(), true);
	}

    // Exactly one pickup handler per overlap; non-pickup events retain the engine callback.
}


void AFortPlayerPawnAthena::MovingEmoteStopped(UObject* Context, FFrame& Stack)
{
	Stack.IncrementCode();
	auto Pawn = (AFortPlayerPawnAthena*)Context;

	if (Pawn->HasbIsPlayingEmote() && Pawn->bIsPlayingEmote)
		return;

	static auto HasbMovingEmote = Pawn->HasbMovingEmote();
	if (HasbMovingEmote)
		Pawn->bMovingEmote = false;

	static auto HasbMovingEmoteForwardOnly = Pawn->HasbMovingEmoteForwardOnly();
	if (HasbMovingEmoteForwardOnly)
		Pawn->bMovingEmoteForwardOnly = false;

	static auto HasbMovingEmoteFollowingOnly = Pawn->HasbMovingEmoteFollowingOnly();
	if (HasbMovingEmoteFollowingOnly)
		Pawn->bMovingEmoteFollowingOnly = false;

	if (Pawn->HasLastReplicatedEmoteExecuted())
	{
		auto OldEmote = Pawn->LastReplicatedEmoteExecuted;
		Pawn->LastReplicatedEmoteExecuted = nullptr;
		Pawn->OnRep_LastReplicatedEmoteExecuted(OldEmote);
	}
}

class UGA_Athena_MedConsumable_Parent_C : public UObject
{
public:
	UCLASS_COMMON_MEMBERS(UGA_Athena_MedConsumable_Parent_C);

	DEFINE_PROP(PlayerPawn, AFortPlayerPawnAthena*);
	DEFINE_PROP(HealsShields, bool);
	DEFINE_PROP(HealsHealth, bool);
	DEFINE_PROP(HealthHealAmount, float);
};

void AFortPlayerPawnAthena::Athena_MedConsumable_Triggered(UObject* Context, FFrame& Stack)
{
	UGA_Athena_MedConsumable_Parent_C* Consumable = (UGA_Athena_MedConsumable_Parent_C*)Context;

	printf("Called yo\n");
	if (!Consumable || (!Consumable->HealsShields && !Consumable->HealsHealth) || !Consumable->PlayerPawn)
		return Athena_MedConsumable_TriggeredOG(Context, Stack);

	auto PlayerState = (AFortPlayerStateAthena*)Consumable->PlayerPawn->PlayerState;
	static auto ShieldCue = FName(L"GameplayCue.Shield.PotionConsumed");
	static auto HealthCue = FName(L"GameplayCue.Athena.Health.HealUsed");

	auto Handle = PlayerState->AbilitySystemComponent->MakeEffectContext();
	FGameplayTag Tag{};
	FName CueName = Consumable->HealsShields ? ShieldCue : HealthCue;

	if (Consumable->HealsHealth && Consumable->HealsShields)
	{
		static auto HealthHealAmountOffset = Consumable->GetOffset("HealthHealAmount");
		auto HealthHealAmount = Consumable->HasHealthHealAmount() ? *(float*)(__int64(Consumable) + HealthHealAmountOffset) : *(double*)(__int64(Consumable) + HealthHealAmountOffset);
		if (Consumable->PlayerPawn->GetHealth() + HealthHealAmount <= 100)
			CueName = HealthCue;
	}
	Tag.TagName = CueName;

	auto PredictionKey = (FPredictionKey*)malloc(FPredictionKey::Size());
	memset((PBYTE)PredictionKey, 0, FPredictionKey::Size());

	PlayerState->AbilitySystemComponent->NetMulticast_InvokeGameplayCueAdded(Tag, *PredictionKey, Handle);
	PlayerState->AbilitySystemComponent->NetMulticast_InvokeGameplayCueExecuted(Tag, *PredictionKey, Handle);

	free(PredictionKey);

	return Athena_MedConsumable_TriggeredOG(Context, Stack);
}

void AFortPlayerPawnAthena::ServerOnExitVehicle_(UObject* Context, FFrame& Stack, AActor** Ret)
{
	struct FVehicleExitData { uint8_t Pad[0x30]; };

	FVehicleExitData VehicleExitData;  
	uint8_t ExitForceBehavior;
	bool bDestroyVehicleWhenForced;
	if (VersionInfo.FortniteVersion >= 29)
		Stack.StepCompiledIn(&VehicleExitData);
	else
	{
		Stack.StepCompiledIn(&ExitForceBehavior);
		Stack.StepCompiledIn(&bDestroyVehicleWhenForced);
	}

	Stack.IncrementCode();

	auto Pawn = (AFortPlayerPawnAthena*)Context;

	static auto GetVehicleFunc = Pawn->GetFunction("GetVehicleActor");
	if (!GetVehicleFunc)
		GetVehicleFunc = Pawn->GetFunction("GetVehicle");
	auto Vehicle = Pawn->Call<AActor*>(GetVehicleFunc);

	if (!Vehicle && Pawn->IsA<AFortCharacterVehicle>())
		Vehicle = Pawn;

	if (!Vehicle)
	{
		if (VersionInfo.FortniteVersion >= 29)
			return callOG(Pawn, Stack.GetCurrentNativeFunction(), ServerOnExitVehicle, VehicleExitData);
		else
			return callOG(Pawn, Stack.GetCurrentNativeFunction(), ServerOnExitVehicle, ExitForceBehavior, bDestroyVehicleWhenForced);
	}

	UFortVehicleSeatWeaponComponent* SeatWeaponComponent = (UFortVehicleSeatWeaponComponent*)Vehicle->GetComponentByClass(UFortVehicleSeatWeaponComponent::StaticClass());

	if (!SeatWeaponComponent)
	{
		printf("nop %s\n", Pawn ? Pawn->Class->Name.ToString().c_str() : nullptr);
		if (VersionInfo.FortniteVersion >= 29)
			return callOG(Pawn, Stack.GetCurrentNativeFunction(), ServerOnExitVehicle, VehicleExitData);
		else
			return callOG(Pawn, Stack.GetCurrentNativeFunction(), ServerOnExitVehicle, ExitForceBehavior, bDestroyVehicleWhenForced);
	}

	UFortVehicleSeatComponent* SeatComponent = (UFortVehicleSeatComponent*)Vehicle->GetComponentByClass(UFortVehicleSeatComponent::StaticClass());

	auto PlayerController = (AFortPlayerControllerAthena*)Pawn->Controller;
	if (!SeatComponent || !PlayerController || !PlayerController->WorldInventory)
	{
		if (VersionInfo.FortniteVersion >= 29)
			return callOG(Pawn, Stack.GetCurrentNativeFunction(), ServerOnExitVehicle, VehicleExitData);
		else
			return callOG(Pawn, Stack.GetCurrentNativeFunction(), ServerOnExitVehicle, ExitForceBehavior, bDestroyVehicleWhenForced);
	}

	auto SeatIdx = SeatComponent->FindSeatIndex(Pawn);

	UFortWeaponItemDefinition* Weapon = nullptr;
	if (SeatWeaponComponent)
	{
		for (int i = 0; i < SeatWeaponComponent->WeaponSeatDefinitions.Num(); i++)
		{
			auto& WeaponDefinition = SeatWeaponComponent->WeaponSeatDefinitions.Get(i, FWeaponSeatDefinition::Size());

			if (WeaponDefinition.SeatIndex != SeatIdx)
				continue;

			Weapon = WeaponDefinition.VehicleWeapon;
			break;
		}

		//printf("Weapon: %s\n", Weapon ? Weapon->Name.ToString().c_str() : "<null>");
		if (Weapon)
		{
			auto ItemEntry = PlayerController->WorldInventory->Inventory.ReplicatedEntries.Search([Weapon](FFortItemEntry& entry)
				{ return entry.ItemDefinition == Weapon; }, FFortItemEntry::Size());

			if (ItemEntry)
				PlayerController->WorldInventory->Remove(ItemEntry->ItemGuid);
		}
	}
	if (VersionInfo.FortniteVersion >= 29)
		*Ret = callOGWithRet(Pawn, Stack.GetCurrentNativeFunction(), ServerOnExitVehicle, VehicleExitData);
	else
		*Ret = callOGWithRet(Pawn, Stack.GetCurrentNativeFunction(), ServerOnExitVehicle, ExitForceBehavior, bDestroyVehicleWhenForced);

	if (Weapon)
	{
		auto LastItem = Pawn->HasPreviousWeapon() ? (AFortWeapon*)Pawn->PreviousWeapon : nullptr;
		// The weapon held before getting in may be gone by now (dropped, used up, or its actor destroyed while seated):
		// only go back to it while it is still a live actor that is still in the inventory, else fall back to the pickaxe.
		if (LastItem && (LastItem->bActorIsBeingDestroyed || !PlayerController->WorldInventory->Inventory.ReplicatedEntries.Search([&](FFortItemEntry& entry)
			{ return entry.ItemGuid == LastItem->ItemEntryGuid; }, FFortItemEntry::Size())))
			LastItem = nullptr;

		if (LastItem)
		{
			PlayerController->ServerExecuteInventoryItem(LastItem->ItemEntryGuid);
			PlayerController->ClientEquipItem(LastItem->ItemEntryGuid, true);
		}
		else
		{
			auto pickaxeEntry = PlayerController->WorldInventory->Inventory.ReplicatedEntries.Search([](FFortItemEntry& entry)
				{ return entry.ItemDefinition->IsA<UFortWeaponMeleeItemDefinition>(); }, FFortItemEntry::Size());

			if (pickaxeEntry)
			{
				PlayerController->ServerExecuteInventoryItem(pickaxeEntry->ItemGuid);
				PlayerController->ClientEquipItem(pickaxeEntry->ItemGuid, true);
			}
		}
	}
}

void AFortPlayerPawnAthena::EmoteStopped_(UObject* Context, FFrame& Stack)
{
	UObject* MontageItemDef;

	Stack.StepCompiledIn(&MontageItemDef);
	Stack.IncrementCode();
	auto Pawn = (AFortPlayerPawnAthena*)Context;

	if (Pawn->HasLastReplicatedEmoteExecuted() && Pawn->LastReplicatedEmoteExecuted == MontageItemDef)
	{
		auto OldEmote = Pawn->LastReplicatedEmoteExecuted;
		Pawn->LastReplicatedEmoteExecuted = nullptr;
		Pawn->OnRep_LastReplicatedEmoteExecuted(OldEmote);
	}

	return callOG(Pawn, Stack.GetCurrentNativeFunction(), EmoteStopped, MontageItemDef);
}

static bool SafeEndSkydivingNative(AFortPlayerPawnAthena* Pawn)
{
	__try
	{
		AFortPlayerPawnAthena::EndSkydivingOG(Pawn);
		return true;
	}
	__except (EXCEPTION_EXECUTE_HANDLER)
	{
		printf("[Syndicate] EndSkydiving native failed (0x%08lx); applying safe landing\n", GetExceptionCode());
		fflush(stdout);
		return false;
	}
}

static void ForceEndSkydiving(AFortPlayerPawnAthena* Pawn)
{
	if (!Pawn) return;
	for (auto Name : { "bIsSkydiving", "bIsSkydivingFromBus", "bIsParachuteOpen" })
	{
		auto Prop = Pawn->GetProperty(Name);
		auto Offset = Pawn->GetOffset(Name);
		if (Prop && Offset != uint32(-1))
		{
			auto& Bits = GetFromOffset<uint8>(Pawn, Offset);
			Bits &= ~Prop->GetFieldMask();
		}
	}
	if (auto Movement = Pawn->CharacterMovement)
	{
		if (auto SetMode = Movement->GetFunction("SetMovementMode"))
		{
			uint8 Args[2] = { 1, 0 }; // MOVE_Walking
			Movement->ProcessEvent(SetMode, Args);
		}
		else
		{
			auto Prop = Movement->GetProperty("MovementMode");
			auto Offset = Movement->GetOffset("MovementMode");
			if (Prop && Offset != uint32(-1))
				GetFromOffset<uint8>(Movement, Offset) = 1;
		}
	}
	for (auto Name : { "OnRep_IsSkydiving", "OnRep_IsParachuteOpen" })
	{
		auto Function = Pawn->GetFunction(Name);
		if (!Function) continue;
		auto Size = Function->GetParamsNamed().Size;
		if (Size == 0) Pawn->ProcessEvent(Function, nullptr);
		else if (Size <= 8) { uint8 Args[8]{}; Pawn->ProcessEvent(Function, Args); }
	}
	Pawn->ForceNetUpdate();
}

void AFortPlayerPawnAthena::EndSkydiving(AFortPlayerPawnAthena* Pawn)
{
	// The game can call this while a player is being replaced during login/respawn. The
	// original handler then dereferences a null object and takes the whole server down
	// with 0xC0000005 (7.40 EndSkydiving). Guard the call; if the native handler still
	// faults, finish the landing by hand instead of crashing the match.
	if (!Pawn || !Pawn->Controller || !Pawn->PlayerState) return;
	if (Pawn->Controller->IsA(AFortPlayerControllerAthena::StaticClass()))
	{
		auto EarlyController = (AFortPlayerControllerAthena*)Pawn->Controller;
		if (!EarlyController->PlayerState) return;
	}
	// Keep the original landing path for human players. Some bot pawns reach this
	// handler in a state the native code cannot read (the 0x3B0 null read), so only
	// bots use the manual landing state.
	bool Bot = false;
	if (auto State = Pawn->PlayerState ? Pawn->PlayerState->Cast<AFortPlayerStateAthena>() : nullptr)
		Bot = State->HasbIsABot() && State->bIsABot;
	if (!Bot && Pawn->Controller && !Pawn->Controller->IsA(AFortPlayerControllerAthena::StaticClass()))
		Bot = true;
	if (Bot)
		ForceEndSkydiving(Pawn);
	else if (!SafeEndSkydivingNative(Pawn))
		ForceEndSkydiving(Pawn);

	if (!Pawn || !Pawn->Controller || !Pawn->Controller->IsA(AFortPlayerControllerAthena::StaticClass())) return;
	auto PlayerController = (AFortPlayerControllerAthena*)Pawn->Controller;
	if (!PlayerController->PlayerState || (PlayerController->PlayerState->HasbIsABot() && PlayerController->PlayerState->bIsABot)) return;
	auto Quests = PlayerController->GetQuestManager(1);
	if (!Quests) return;

	if (PlayerController && Pawn->bIsSkydiving)
		Quests->SendStatEvent(PlayerController, EFortQuestObjectiveStatEvent::GetLand(), 1, Pawn);

	if (Pawn->bIsSkydivingFromBus)
	{
		Quests->SendStatEvent(PlayerController, EFortQuestObjectiveStatEvent::GetVisit(), 1, Pawn);
	}
}


void AFortPlayerPawnAthena::ServerReviveFromDBNO(UObject* Context, FFrame& Stack)
{
	AFortPlayerControllerAthena* EventInstigator = nullptr;

	Stack.StepCompiledIn(&EventInstigator);
	Stack.IncrementCode();
	auto Pawn = (AFortPlayerPawnAthena*)Context;
    auto Function=Stack.GetCurrentNativeFunction();
    if(!SyndicateDBNO::RevivePathSupported()){
        // Syndicate fix: Seasons 1-2 use the game's own revive. The old early
        // return swallowed the call, so reviving a teammate did nothing in Season 2.
        if(Pawn && ServerReviveFromDBNOOG && Function){
            Function->ExecFunction=ServerReviveFromDBNOOG;
            Pawn->Call<void>(Function,EventInstigator);
            Function->ExecFunction=ServerReviveFromDBNO;
        }
        return;
    }
    SyndicateDBNO::ReviveLog("ServerReviveFromDBNO received");
    if(!SyndicateDBNO::ValidReviver(Pawn,EventInstigator))return;
    // Preserve native interaction handling before repairing an incomplete transition.
    if(ServerReviveFromDBNOOG && Function){
        Function->ExecFunction=ServerReviveFromDBNOOG;
        Pawn->Call<void>(Function,EventInstigator);
        Function->ExecFunction=ServerReviveFromDBNO;
    }
    if(Pawn->IsDBNO())SyndicateDBNO::CompleteRevive(Pawn,EventInstigator);
}

void AFortPlayerPawnAthena::ServerThrowCarriedPlayer_(UObject* Context, FFrame& Stack)
{
	Stack.IncrementCode();
	auto Pawn = (AFortPlayerPawnAthena*)Context;

	callOG(Pawn, Stack.GetCurrentNativeFunction(), ServerThrowCarriedPlayer);
	Pawn->LocalThrowCarriedPlayer();
}


void AFortPlayerPawnAthena::PostLoadHook()
{
	OnRep_ZiplineState = FindOnRep_ZiplineState();
	SetPickupTarget_ = FindSetPickupTarget();
	// Seasons 6-X: the text-search finder above cannot be trusted on these builds (it gave a
	// mid-instruction address, then the wrong function); always set the pickup target by hand.
	if (VersionInfo.FortniteVersion >= 6 && VersionInfo.FortniteVersion < 13)
		SetPickupTarget_ = 0;
	printf("[Syndicate] SetPickupTarget at game+0x%llx\n", SetPickupTarget_ ? (unsigned long long)(SetPickupTarget_ - (uint64_t)GetModuleHandleW(nullptr)) : 0ull);

	auto ServerHandlePickupInfoFn = GetDefaultObj()->GetFunction("ServerHandlePickupInfo");

	if (ServerHandlePickupInfoFn)
		Utils::ExecHook(ServerHandlePickupInfoFn, ServerHandlePickupInfo);
	else
	{
		Utils::ExecHook(GetDefaultObj()->GetFunction("ServerHandlePickup"), ServerHandlePickup_);
		Utils::ExecHook(GetDefaultObj()->GetFunction("ServerHandlePickupWithRequestedSwap"), ServerHandlePickupWithRequestedSwap);
	}

	Utils::Hook(FindFinishedTargetSpline(), FinishedTargetSpline, FinishedTargetSplineOG);
	Utils::ExecHook(GetDefaultObj()->GetFunction("OnCapsuleBeginOverlap"), OnCapsuleBeginOverlap_, OnCapsuleBeginOverlap_OG);

	Utils::ExecHook(GetDefaultObj()->GetFunction("ServerSendZiplineState"), ServerSendZiplineState);
	Utils::ExecHook(GetDefaultObj()->GetFunction("MovingEmoteStopped"), MovingEmoteStopped);

	Utils::ExecHook(GetDefaultObj()->GetFunction("ServerOnExitVehicle"), ServerOnExitVehicle_, ServerOnExitVehicle_OG);

	Utils::ExecHook(GetDefaultObj()->GetFunction("EmoteStopped"), EmoteStopped_, EmoteStopped_OG);

	auto EndSkydivingFn = GetDefaultObj()->GetFunction("EndSkydiving");

	// 7.40: keep the hook installed so a fault in the native landing handler can be
	// caught and converted into a safe landing instead of a 0xC0000005 match crash.
	if (EndSkydivingFn)
		Utils::Hook<AFortPlayerPawnAthena>(EndSkydivingFn->GetVTableIndex(), EndSkydiving, EndSkydivingOG);


	// Preserve the native handler for Season 4 before applying recovery.
	Utils::ExecHook(GetDefaultObj()->GetFunction("ServerReviveFromDBNO"), ServerReviveFromDBNO, ServerReviveFromDBNOOG);
	Utils::ExecHook(GetDefaultObj()->GetFunction("ServerThrowCarriedPlayer"), ServerThrowCarriedPlayer_, ServerThrowCarriedPlayer_OG);
}
