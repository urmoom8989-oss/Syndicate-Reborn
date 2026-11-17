#include "pch.h"
#include "../Public/Misc.h"
#include "../Public/Finders.h"
#include <algorithm>
#include "../Public/Configuration.h"
#include "../../FortniteGame/Public/FortPlayerControllerAthena.h"
#include "../../Engine/Public/NetDriver.h"
#include "../../FortniteGame/Public/FortGameMode.h"
#include "../../FortniteGame/Public/FortWeapon.h"
#include "../../FortniteGame/Public/FortKismetLibrary.h"

int Misc::GetNetMode() 
{
	return 1;
}


void* Misc::SendRequestNow(void* Arg1, void* MCPData, int)
{
	if (VersionInfo.EngineVersion < 4.23)
		*(int*)(__int64(MCPData) + (VersionInfo.FortniteVersion >= 4.2 ? 0x28 : 0x60)) = 3; // CXC_Public

	return SendRequestNowOG(Arg1, MCPData, 3); // CXC_Public
}


// One step handed to the game thread by Main (dllmain.cpp). The engine asks for its tick rate once at the start of
// every frame, on the game thread, before anything ticks: a safe point to change the world.
void (*volatile SyndicateRunOnGameThread)() = nullptr;

float Misc::GetMaxTickRate(UEngine* Engine, float DeltaTime, bool bAllowFrameRateSmoothing)
{
	if (SyndicateRunOnGameThread)
		if (auto Step = (void (*)())InterlockedExchangePointer((PVOID volatile*)&SyndicateRunOnGameThread, nullptr))
			Step();

	// improper, DS is supposed to do hitching differently
	return (float)FConfiguration::MaxTickRate;
	//return std::clamp(1.f / DeltaTime, 1.f, FConfiguration::MaxTickRate);
}

uint32 Misc::CheckCheckpointHeartBeat()
{
	return -1;
}

void Misc::ApplyHomebaseEffectsOnPlayerSetup(
	__int64* a1,
	__int64 a2,
	__int64 a3,
	__int64 a4,
	UObject* a5,
	char a6,
	unsigned __int8 a7)
{
	auto GameMode = (AFortGameModeAthena*) UWorld::GetWorld()->AuthorityGameMode;
	if (GameMode->HasWarmupRequiredPlayerCount())
	{
		static auto ItemDefOffset = a5->GetOffset("ItemDefinition");
		static auto Commando = FindObject<UObject>(L"/Game/Athena/Heroes/HID_001_Athena_Commando_F.HID_001_Athena_Commando_F");
		static auto Commando2 = FindObject<UObject>(L"/Game/Athena/Heroes/HID_Commando_Athena_01.HID_Commando_Athena_01");
		GetFromOffset<const UObject*>(a5, ItemDefOffset) = Commando ? Commando : Commando2;
	}

	return ApplyHomebaseEffectsOnPlayerSetupOG(a1, a2, a3, a4, a5, a6, a7);
}

bool bEOREnabled = false;
inline void* (*SelectResetOG)(void*) = nullptr;
inline void* (*SelectEditOG)(void*) = nullptr;
inline char (*CompleteBuildingEditInteraction)(void*) = nullptr;

void* SelectEdit(void* a1)
{
	void* result = SelectEditOG(a1);

	if (bEOREnabled)
		CompleteBuildingEditInteraction(a1);

	return result;
}

void* SelectReset(void* a1)
{
	void* result = SelectResetOG(a1);

	if (bEOREnabled)
		CompleteBuildingEditInteraction(a1);

	return result;
}

class UIpNetDriver : public UNetDriver
{
public:
	UCLASS_COMMON_MEMBERS(UIpNetDriver);
};

void PatchAllNetModes(uintptr_t AttemptDeriveFromURL)
{
	Memcury::PE::Address add{ nullptr };

	const auto sizeOfImage = Memcury::PE::GetNTHeaders()->OptionalHeader.SizeOfImage;
	const auto scanBytes = reinterpret_cast<std::uint8_t*>(Memcury::PE::GetModuleBase());

	for (auto i = 0ul; i < sizeOfImage - 5; ++i)
	{
		if (scanBytes[i] == 0xE8 || scanBytes[i] == 0xE9)
		{
			if (Memcury::PE::Address(&scanBytes[i]).RelativeOffset(1).GetAs<void*>() == (void*)AttemptDeriveFromURL)
			{
				add = Memcury::PE::Address(&scanBytes[i]);

				// scan for the read of World->NetDriver

				for (auto j = 0; j > -0x100000; j--) // so we find everything. no func is actually 1mb
				{
					if ((scanBytes[i + j] & 0xF8) == 0x48 && ((scanBytes[i + j + 1] & 0xFC) == 0x80 || (scanBytes[i + j + 1] & 0xF8) == 0x38) && (scanBytes[i + j + 2] & 0xF0) != 0xC0 && (scanBytes[i + j + 2] & 0xF0) != 0xE0 && scanBytes[i + j + 2] != 0x65 && scanBytes[i + j + 2] != 0xBB && scanBytes[i + j + 3] == 0x38 && ((scanBytes[i + j + 1] & 0xFC) != 0x80 || scanBytes[i + j + 4] == 0x0))
					{
						// now, scan for if (NetDriver) return NM_Client;

						bool found = false;
						for (auto k = 4; k < 0x104; k++)
						{
							if (scanBytes[i + j + k] == 0x75)
							{
								auto Scuffness = __int64(&scanBytes[i + j + k + 5]);

								if (*(uint32_t*)Scuffness != 0xF0 && (scanBytes[i + j + k + 4] != 0xC || scanBytes[i + j + k + 5] != 0xB) && scanBytes[i + j + k + 4] != 0x09)
									continue;

								Utils::Patch<uint16_t>(__int64(&scanBytes[i + j + k]), 0x9090);
								if ((scanBytes[i + j + 1] & 0xF8) == 0x38)
									Utils::Patch<uint32_t>(__int64(&scanBytes[i + j]), 0x90909090);
								else if ((scanBytes[i + j + 1] & 0xFC) == 0x80)
								{
									DWORD og;
									VirtualProtect(&scanBytes[i + j], 5, PAGE_EXECUTE_READWRITE, &og);
									*(uint32*)(&scanBytes[i + j]) = 0x90909090;
									*(uint8*)(&scanBytes[i + j + 4]) = 0x90;
									VirtualProtect(&scanBytes[i + j], 5, og, &og);
								}
								FlushInstructionCache(GetCurrentProcess(), &scanBytes[i + j], 5);
								FlushInstructionCache(GetCurrentProcess(), &scanBytes[i + j + k], 2);
								found = true;
								break;
							}
							else if (scanBytes[i + j + k] == 0x74)	
							{
								auto Scuffness = __int64(&scanBytes[i + j + k]);
								Scuffness = (Scuffness + 2) + *(int8_t*)(Scuffness + 1);
								
								if (*(uint32_t*)(Scuffness + 3) != 0xF0 && (*(uint8_t*)(Scuffness + 2) != 0xC || *(uint8_t*)(Scuffness + 3) != 0xB) && *(uint8_t*)(Scuffness + 2) != 0x09)
									continue;

								Utils::Patch<uint8_t>(__int64(&scanBytes[i + j + k]), 0xeb);
								if ((scanBytes[i + j + 1] & 0xF8) == 0x38)
									Utils::Patch<uint32_t>(__int64(&scanBytes[i + j]), 0x90909090);
								else if ((scanBytes[i + j + 1] & 0xFC) == 0x80)
								{
									DWORD og;
									VirtualProtect(&scanBytes[i + j], 5, PAGE_EXECUTE_READWRITE, &og);
									*(uint32*)(&scanBytes[i + j]) = 0x90909090;
									*(uint8*)(&scanBytes[i + j + 4]) = 0x90;
									VirtualProtect(&scanBytes[i + j], 5, og, &og);
								}
								FlushInstructionCache(GetCurrentProcess(), &scanBytes[i + j], 5);
								FlushInstructionCache(GetCurrentProcess(), &scanBytes[i + j + k], 1);
								found = true;
								break;
							}
							else if (scanBytes[i + j + k] == 0x0F  && scanBytes[i + j + k + 1] == 0x85)
							{
								auto Scuffness = __int64(&scanBytes[i + j + k + 9]);

								if (*(uint32_t*)Scuffness != 0xF0 && (scanBytes[i + j + k + 8] != 0xC || scanBytes[i + j + k + 9] != 0xB) && scanBytes[i + j + k + 8] != 0x09)
									continue;

								DWORD og;
								VirtualProtect(&scanBytes[i + j + k], 6, PAGE_EXECUTE_READWRITE, &og);
								*(uint32*)(&scanBytes[i + j + k]) = 0x90909090;
								*(uint16*)(&scanBytes[i + j + k + 4]) = 0x9090;
								VirtualProtect(&scanBytes[i + j + k], 6, og, &og);
								if ((scanBytes[i + j + 1] & 0xF8) == 0x38)
									Utils::Patch<uint32_t>(__int64(&scanBytes[i + j]), 0x90909090);
								else if ((scanBytes[i + j + 1] & 0xFC) == 0x80)
								{
									DWORD og;
									VirtualProtect(&scanBytes[i + j], 5, PAGE_EXECUTE_READWRITE, &og);
									*(uint32*)(&scanBytes[i + j]) = 0x90909090;
									*(uint8*)(&scanBytes[i + j + 4]) = 0x90;
									VirtualProtect(&scanBytes[i + j], 5, og, &og);
								}
								FlushInstructionCache(GetCurrentProcess(), &scanBytes[i + j], 5);
								FlushInstructionCache(GetCurrentProcess(), &scanBytes[i + j + k], 6);
								found = true;
								break;
							}
							else if (scanBytes[i + j + k] == 0x0F && scanBytes[i + j + k + 1] == 0x84)
							{
								auto Scuffness = __int64(&scanBytes[i + j + k]);
								Scuffness = (Scuffness + 6) + *(int32_t*)(Scuffness + 2);

								if (*(uint32_t*)(Scuffness + 3) != 0xF0 && (*(uint8_t*)(Scuffness + 2) != 0xC || *(uint8_t*)(Scuffness + 3) != 0xB) && *(uint8_t*)(Scuffness + 2) != 0x09)
									continue;

								Utils::Patch<uint16_t>(__int64(&scanBytes[i + j + k]), 0xe990);
								if ((scanBytes[i + j + 1] & 0xF8) == 0x38)
									Utils::Patch<uint32_t>(__int64(&scanBytes[i + j]), 0x90909090);
								else if ((scanBytes[i + j + 1] & 0xFC) == 0x80)
								{
									DWORD og;
									VirtualProtect(&scanBytes[i + j], 5, PAGE_EXECUTE_READWRITE, &og);
									*(uint32*)(&scanBytes[i + j]) = 0x90909090;
									*(uint8*)(&scanBytes[i + j + 4]) = 0x90;
									VirtualProtect(&scanBytes[i + j], 5, og, &og);
								}
								FlushInstructionCache(GetCurrentProcess(), &scanBytes[i + j], 5);
								FlushInstructionCache(GetCurrentProcess(), &scanBytes[i + j + k], 2);
								found = true;
								break;
							}
						}
						if (found)
							break;
					}
				}
			}
		}
	}
}

bool RetFalse()
{
	return false;
}

class AFortTeamMemberPedestal : public AActor
{
public:
	UCLASS_COMMON_MEMBERS(AFortTeamMemberPedestal);
};

__int64 (*CrashSomethingOG)(__int64 a1, __int64 a2);
__int64 CrashSomething(__int64 a1, __int64 a2)
{
	if (!a1)
		return 0;

	return CrashSomethingOG(a1, a2);
}

class AFortLightweightProjectileManager : public AActor
{
public:
	UCLASS_COMMON_MEMBERS(AFortLightweightProjectileManager);
};

class AFortLightweightProjectileConfig : public AActor
{
public:
	UCLASS_COMMON_MEMBERS(AFortLightweightProjectileConfig);

	DEFINE_PROP(Speed, FScalableFloat);
	DEFINE_PROP(GravityScale, FScalableFloat);
};

struct FSpawnProjectileParams
{
public:
	USCRIPTSTRUCT_COMMON_MEMBERS(FSpawnProjectileParams);

	DEFINE_STRUCT_PROP(SpawnLocation, FVector);
	DEFINE_STRUCT_PROP(SpawnDirection, FRotator);
	DEFINE_STRUCT_PROP(OptionalAssociatedItemDef, UFortItemDefinition*);
	DEFINE_STRUCT_PROP(InitialSpeed, float);
	DEFINE_STRUCT_PROP(MaxSpeed, float);
	DEFINE_STRUCT_PROP(GravityScale, float);
};

class AFortProjectileAthena : public AActor
{
public:
	UCLASS_COMMON_MEMBERS(AFortProjectileAthena);
};

void (*TestOG)(AFortLightweightProjectileManager* ProjectileManager, TWeakObjectPtr<AFortPlayerPawnAthena> WeakPawn, TWeakObjectPtr<AFortWeapon> WeakWeapon, TSubclassOf<AFortLightweightProjectileConfig>& ConfigClass, FVector Location, FVector Direction, uint8_t Type, int a8, int a9);
void Test(AFortLightweightProjectileManager* ProjectileManager, TWeakObjectPtr<AFortPlayerPawnAthena> WeakPawn, TWeakObjectPtr<AFortWeapon> WeakWeapon, TSubclassOf<AFortLightweightProjectileConfig>& ConfigClass, FVector Location, FVector Direction, uint8_t Type, int a8, int a9)
{
	TestOG(ProjectileManager, WeakPawn, WeakWeapon, ConfigClass, Location, Direction, Type, a8, a9);

	auto Config = (AFortLightweightProjectileConfig*)ConfigClass->GetDefaultObj();

	auto Params = (FSpawnProjectileParams*)malloc(FSpawnProjectileParams::Size());
	memset(Params, 0, FSpawnProjectileParams::Size());

	Params->SpawnLocation = Location;

	const double RAD_TO_DEG = 57.29577951308232;

	auto Weapon = WeakWeapon.Get();

	auto Pitch = asin(Direction.Z) * RAD_TO_DEG;
	auto Yaw = atan2(Direction.Y, Direction.X) * RAD_TO_DEG;
	Params->SpawnDirection.Pitch = Pitch;
	Params->SpawnDirection.Yaw = Yaw;
	Params->SpawnDirection.Roll = 0;
	Params->OptionalAssociatedItemDef = Weapon->WeaponData;
	Params->InitialSpeed = Config->Speed.Evaluate();
	Params->MaxSpeed = Params->InitialSpeed;
	Params->GravityScale = Config->GravityScale.Evaluate();

	UFortKismetLibrary::SpawnProjectileWithParams(AFortProjectileAthena::StaticClass(), Weapon, *Params);
	free(Params);
}

struct FTickFunction
{
public:
	USCRIPTSTRUCT_COMMON_MEMBERS(FTickFunction);

	DEFINE_STRUCT_BITFIELD_PROP(bAllowTickOnDedicatedServer);
};

void (*Test2OG)(FTickFunction* _this, ULevel* Level);
void Test2(FTickFunction* _this, ULevel* Level)
{
	if (!_this->bAllowTickOnDedicatedServer)
	{
		return;
	}
	return Test2OG(_this, Level);
}

void Ohio(ABuildingProp_LockDevice* _this, AFortPlayerControllerAthena* ControllerInstigator)
{
	printf("Called[LockProp: %s]\n", _this->Name.ToString().c_str());
}

// Syndicate crash tracer: when the server jumps into invalid code (privileged/illegal
// instruction), print who called it. [rsp] holds the caller's return address, and the
// stack scan lists every return address into the game or Erbium, so the crash can be
// matched to code. Runs before the normal crash reporter and does not handle the crash.
static uintptr_t SyndicateModuleEnd(uintptr_t Base){
	auto Dos=(IMAGE_DOS_HEADER*)Base;auto Nt=(IMAGE_NT_HEADERS*)(Base+Dos->e_lfanew);return Base+Nt->OptionalHeader.SizeOfImage;
}
struct SyndicateRanges{uintptr_t Exe,ExeEnd,Self,SelfEnd;};
static void SyndicateWhere(const SyndicateRanges& R,uintptr_t V,char* Out,size_t N){
	if(V>=R.Exe&&V<R.ExeEnd)snprintf(Out,N,"game+0x%llx",(unsigned long long)(V-R.Exe));
	else if(R.Self&&V>=R.Self&&V<R.SelfEnd)snprintf(Out,N,"erbium+0x%llx",(unsigned long long)(V-R.Self));
	else snprintf(Out,N,"%p",(void*)V);
}
static void SyndicateScanStack(const SyndicateRanges& R,uintptr_t Rsp){
	char B[64];
	__try{
		auto Stack=(uintptr_t*)Rsp;SyndicateWhere(R,Stack[0],B,sizeof(B));printf("[SyndicateCrash] caller=%s\n",B);
		int Shown=0;for(int I=0;I<1024 && Shown<24;I++){auto V=Stack[I];if((V>=R.Exe&&V<R.ExeEnd)||(R.Self&&V>=R.Self&&V<R.SelfEnd)){SyndicateWhere(R,V,B,sizeof(B));printf("[SyndicateCrash] stack[%d]=%s\n",I,B);Shown++;}}
	}__except(EXCEPTION_EXECUTE_HANDLER){printf("[SyndicateCrash] stack unreadable\n");}
}
static LONG CALLBACK SyndicateCrashTrace(PEXCEPTION_POINTERS E){
	auto Code=E->ExceptionRecord->ExceptionCode;
	if(Code!=0xC0000096 && Code!=0xC000001D)return EXCEPTION_CONTINUE_SEARCH;
	static volatile LONG Count=0;if(InterlockedIncrement(&Count)>2)return EXCEPTION_CONTINUE_SEARCH;
	HMODULE SelfModule=nullptr;
	GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,(LPCWSTR)&SyndicateCrashTrace,&SelfModule);
	SyndicateRanges R{};R.Exe=(uintptr_t)GetModuleHandleW(nullptr);R.ExeEnd=SyndicateModuleEnd(R.Exe);R.Self=(uintptr_t)SelfModule;R.SelfEnd=R.Self?SyndicateModuleEnd(R.Self):0;
	auto C=E->ContextRecord;char A[64];SyndicateWhere(R,C->Rip,A,sizeof(A));
	printf("[SyndicateCrash] code=0x%08lx rip=%s rax=%p rcx=%p rdx=%p r8=%p\n",Code,A,(void*)C->Rax,(void*)C->Rcx,(void*)C->Rdx,(void*)C->R8);
	SyndicateScanStack(R,C->Rsp);
	fflush(stdout);return EXCEPTION_CONTINUE_SEARCH;
}

extern bool SyndicateListening;
bool Listen()
{
	printf("UWorld::Listen\n");
	auto World = UWorld::GetWorld();
	auto Engine = UEngine::GetEngine();
	auto NetDriverName = FName(L"GameNetDriver");
	auto GameMode = (AFortGameModeAthena*)World->AuthorityGameMode;

	if (GameMode->HasbEnableReplicationGraph())
		GameMode->bEnableReplicationGraph = true;

	UNetDriver* NetDriver = nullptr;
	if (VersionInfo.FortniteVersion >= 16.00)
	{
		void* WorldCtx = ((void* (*)(UEngine*, UWorld*)) FindGetWorldContext())(Engine, World);
		World->NetDriver = NetDriver = ((UNetDriver * (*)(UEngine*, void*, FName, int)) FindCreateNetDriverWorldContext())(Engine, WorldCtx, NetDriverName, 0);
	}
	else
		World->NetDriver = NetDriver = ((UNetDriver * (*)(UEngine*, UWorld*, FName)) FindCreateNetDriver())(Engine, World, NetDriverName);

	if (!NetDriver)
		return false;

	if (VersionInfo.FortniteVersion >= 20)
		NetDriver->NetServerMaxTickRate = FConfiguration::MaxTickRate;

	NetDriver->NetDriverName = NetDriverName;
	NetDriver->World = World;

	if (VersionInfo.EngineVersion >= 5.3 && FConfiguration::bEnableIris)
	{
		*(bool*)(__int64(&NetDriver->ReplicationDriver) + 0x11) = true;
	}

	NetDriver->NetDriverName = NetDriverName;
	NetDriver->World = World;

	auto InitListen = (bool (*)(UNetDriver*, UWorld*, FURL*, bool, FString&)) FindInitListen();
	auto SetWorld = (void (*)(UNetDriver*, UWorld*)) FindSetWorld();

	SetWorld(NetDriver, World);
	for (int i = 0; i < World->LevelCollections.Num(); i++)
	{
		auto& LevelCollection = World->LevelCollections.Get(i, FLevelCollection::Size());

		LevelCollection.NetDriver = NetDriver;
	}

	auto URL = (FURL*)malloc(FURL::Size());
	memset((PBYTE)URL, 0, FURL::Size());
	URL->Port = FConfiguration::Port;
            // Matchmaking advertises this exact port; do not silently bind another.
            if(auto O=NetDriver->GetOffset("MaxPortCountToTry");O!=uint32(-1))GetFromOffset<int>(NetDriver,O)=0;


	FString Err;
	if (!InitListen(NetDriver, World, URL, false, Err))
	{
		printf("Failed to listen (map load, port %d): %s\n", int(FConfiguration::Port), Err.ToString().c_str());

		free(URL);

		return false;
	}
	SetWorld(NetDriver, World);
	SyndicateListening = true;

	free(URL);

	void SyndicateStwBeaconStart();
	SyndicateStwBeaconStart();

	return true;
}

struct FFortBotCosmeticItemSetDataTableRow
{
public:
	USCRIPTSTRUCT_COMMON_MEMBERS(FFortBotCosmeticItemSetDataTableRow);

	DEFINE_STRUCT_PROP(SetTag, FGameplayTag);
	DEFINE_STRUCT_PROP(CharacterAssetId, FPrimaryAssetId);
	DEFINE_STRUCT_PROP(BackpackAssetId, FPrimaryAssetId);
	DEFINE_STRUCT_PROP(Weight, float);
};

class UFortAthenaAIBotCosmeticLibraryData : public UObject
{
public:
	UCLASS_COMMON_MEMBERS(UFortAthenaAIBotCosmeticLibraryData);

	DEFINE_PROP(PredefineSetsDataTable, TSoftObjectPtr<UDataTable>);
};

class UFortAthenaAIBotCharacterCustomization : public UObject
{
public:
	UCLASS_COMMON_MEMBERS(UFortAthenaAIBotCharacterCustomization);

	DEFINE_PROP(CustomizationLoadout, FFortAthenaLoadout);
};

class UFortAthenaAIBotCustomizationData : public UObject
{
public:
	UCLASS_COMMON_MEMBERS(UFortAthenaAIBotCustomizationData);

	DEFINE_PROP(CosmeticCustomizationLibrary, TSoftObjectPtr<UFortAthenaAIBotCosmeticLibraryData>);
	DEFINE_PROP(OverrideCosmeticMode, uint8_t);
	DEFINE_PROP(CharacterCustomization, UFortAthenaAIBotCharacterCustomization*);
};

template <typename T>
std::pair<FName, T*> PickWeighted(UEAllocatedMap<FName, T*>& Map, float (*RandFunc)(float), bool bCheckZero = true)
{
	float TotalWeight = std::accumulate(Map.begin(), Map.end(), 0.0f, [&](float acc, std::pair<FName, T*> p)
		{ return acc + p.second->Weight; });
	float RandomNumber = RandFunc(TotalWeight);

	for (auto& Element : Map)
	{
		float Weight = Element.second->Weight;
		if (bCheckZero && Weight == 0)
			continue;

		if (RandomNumber <= Weight) return Element;

		RandomNumber -= Weight;
	}

	std::pair<FName, T*> None;
	return None;
}

void InitializeCosmeticLoadout(UFortAthenaAIBotCustomizationData* BotData, AFortPlayerPawnAthena* Pawn, FFortAthenaLoadout& OutLoadout, FGameplayTag* PredefinedCosmeticSetTag)
{
	//auto OutLoadout = BotData->CharacterCustomization->CustomizationLoadout;

	if (BotData->OverrideCosmeticMode == 1)
	{
		UEAllocatedMap<FName, FFortBotCosmeticItemSetDataTableRow*> LibraryRowMap;
		FName& CosmeticTag = PredefinedCosmeticSetTag->TagName;

		for (auto& [Key, Val] : (TMap<FName, FFortBotCosmeticItemSetDataTableRow*>&)BotData->CosmeticCustomizationLibrary->PredefineSetsDataTable->RowMap)
			if (Val->SetTag.TagName == CosmeticTag)
				LibraryRowMap[Key] = Val;

		auto LibraryRowPair = PickWeighted(LibraryRowMap, [](float Total)
			{ return ((float)rand() / 32767) * Total; });
		auto& LibraryRow = LibraryRowPair.second;

		OutLoadout.Character = (UAthenaCharacterItemDefinition*)UKismetSystemLibrary::GetObjectFromPrimaryAssetId(LibraryRow->CharacterAssetId);
		if (LibraryRow->BackpackAssetId.PrimaryAssetType.IsValid())
			OutLoadout.Backpack = (UAthenaCharacterPartItemDefinition*)UKismetSystemLibrary::GetObjectFromPrimaryAssetId(LibraryRow->BackpackAssetId);
	}
	else
	{
		OutLoadout.Character = BotData->CharacterCustomization->CustomizationLoadout.Character;
		OutLoadout.Backpack = BotData->CharacterCustomization->CustomizationLoadout.Backpack;
	}

	UEAllocatedMap<uint8_t, const UCustomCharacterPart*> PartMap;

	if (OutLoadout.Character)
		if (auto HeroDefinition = OutLoadout.Character->HeroDefinition)
			for (auto& SoftSpec : HeroDefinition->Specializations)
			{
				auto Specialization = SoftSpec.Get();

				if (Specialization)
					for (auto& PartSoft : Specialization->CharacterParts)
					{
						auto Part = PartSoft.Get();

						PartMap[Part->CharacterPartType] = Part;
					}
			}

	if (OutLoadout.Backpack)
		for (auto& Part : OutLoadout.Backpack->CharacterParts)
			PartMap[Part->CharacterPartType] = Part;

	for (int i = 0; i < OutLoadout.CharacterVariantChannels.Num(); i++)
	{
		auto& VariantChannel = OutLoadout.CharacterVariantChannels.Get(i, FMcpVariantChannelInfo::Size());
		auto CosmeticForVariant = (UAthenaCosmeticItemDefinition*)VariantChannel.ItemVariantIsUsedFor;

		for (auto& ItemVariant : CosmeticForVariant->ItemVariants)
			if (auto PartVariant = ItemVariant->Cast<UFortCosmeticCharacterPartVariant>())
				for (int i = 0; i < PartVariant->PartOptions.Num(); i++)
				{
					auto& PartOption = PartVariant->PartOptions.Get(i, FPartVariantDef::Size());

					if (VariantChannel.ActiveVariantTag.TagName == PartOption.CustomizationVariantTag.TagName)
						for (auto& PartSoft : PartOption.VariantParts)
						{
							auto Part = PartSoft.Get();

							PartMap[Part->CharacterPartType] = Part;
						}
				}
	}

	for (auto& [PartType, Part] : PartMap)
		Pawn->ServerChoosePart(PartType, Part);
}

void Misc::Hook()
{
	if (VersionInfo.FortniteVersion == 23.00 || (VersionInfo.FortniteVersion >= 24.30 && VersionInfo.FortniteVersion != 28.30 && VersionInfo.FortniteVersion != 29.40) || VersionInfo.FortniteVersion >= 30)
	{
		auto AttemptDeriveFromURL = Memcury::Scanner::FindPattern("48 89 5C 24 ? 55 56 57 41 54 41 55 41 56 41 57 48 81 EC ? ? ? ? 4C 8B C1").Get();
		if (!AttemptDeriveFromURL)
			AttemptDeriveFromURL = Memcury::Scanner::FindPattern("48 89 5C 24 ? 55 56 57 41 54 41 55 41 56 41 57 48 81 EC ? ? ? ? 48 8B D1").Get();
		if (!AttemptDeriveFromURL)
			AttemptDeriveFromURL = Memcury::Scanner::FindPattern("48 89 5C 24 ? 55 56 57 41 54 41 55 41 56 41 57 48 81 EC ? ? ? ? 4C 8B D1").Get();
		if (!AttemptDeriveFromURL)
			AttemptDeriveFromURL = Memcury::Scanner::FindPattern("48 89 5C 24 ? 48 89 6C 24 ? 48 89 74 24 ? 57 41 54 41 55 41 56 41 57 48 81 EC ? ? ? ? 4C 8B D1").Get();

		Utils::Hook(AttemptDeriveFromURL, GetNetMode);
		PatchAllNetModes(AttemptDeriveFromURL);
	}
	else if (VersionInfo.FortniteVersion >= 28)
	{
		auto AttemptDeriveFromURL = Memcury::Scanner::FindPattern("48 89 5C 24 ? 55 56 57 41 54 41 55 41 56 41 57 48 81 EC ? ? ? ? 4C 8B C1").Get();
		if (!AttemptDeriveFromURL)
			AttemptDeriveFromURL = Memcury::Scanner::FindPattern("48 89 5C 24 ? 55 56 57 41 54 41 55 41 56 41 57 48 81 EC ? ? ? ? 48 8B D1").Get();
		if (!AttemptDeriveFromURL)
			AttemptDeriveFromURL = Memcury::Scanner::FindPattern("48 89 5C 24 ? 55 56 57 41 54 41 55 41 56 41 57 48 81 EC ? ? ? ? 4C 8B D1").Get();

		Utils::Hook(AttemptDeriveFromURL, GetNetMode);
		PatchAllNetModes(AttemptDeriveFromURL);

		Utils::Hook(FindGetNetMode(), GetNetMode);
	}
	else
		Utils::Hook(FindGetNetMode(), GetNetMode);

	Utils::Hook(FindSendRequestNow(), SendRequestNow, SendRequestNowOG);
	Utils::Hook(FindGetMaxTickRate(), GetMaxTickRate);
	if (VersionInfo.FortniteVersion >= 17)
	{
		auto pattern = Memcury::Scanner::FindPattern("48 89 5C 24 10 48 89 6C 24 20 56 57 41 54 41 56 41 57 48 81 EC ? ? ? ? 65 48 8B 04 25 ? ? ? ? 4C 8B F9").Get();

		if (!pattern)
			pattern = Memcury::Scanner::FindPattern("48 89 5C 24 ? 48 89 6C 24 ? 48 89 74 24 ? 57 41 54 41 55 41 56 41 57 48 81 EC ? ? ? ? 65 48 8B 04 25 ? ? ? ? 4C 8B E9").Get();

		if (!pattern)
			pattern = Memcury::Scanner::FindPattern("48 89 5C 24 ? 48 89 6C 24 ? 56 57 41 54 41 55 41 56 48 81 EC ? ? ? ? 65 48 8B 04 25").Get();

		Utils::Hook(pattern, CheckCheckpointHeartBeat);
	}
	if (VersionInfo.EngineVersion < 4.20)
	{
		auto ApplyHomebaseEffectsOnPlayerSetupAddr = Memcury::Scanner::FindPattern("40 55 53 57 41 54 41 56 41 57 48 8D 6C 24 ? 48 81 EC ? ? ? ? 48 8B 05 ? ? ? ? 48 33 C4 48 89 45 00 4C 8B").Get();

		Utils::Hook(ApplyHomebaseEffectsOnPlayerSetupAddr, ApplyHomebaseEffectsOnPlayerSetup, ApplyHomebaseEffectsOnPlayerSetupOG);
	}
	if (VersionInfo.FortniteVersion >= 25 && VersionInfo.FortniteVersion < 28)
	{
		Utils::Hook(Memcury::Scanner::FindPattern("48 89 5C ? ? 57 48 83 EC ? 48 8B D1 48 85 C9 74 ?").Get(), RetFalse);
	}


	auto PedestalBeginPlay = Memcury::Scanner::FindStringRef(L"AFortTeamMemberPedestal::BeginPlay - Begun play on pedestal %s", true, 0, VersionInfo.EngineVersion >= 5.0).Get();

	if (PedestalBeginPlay)
	{
		uint64_t RealBeginPlay = 0;
		for (int i = 0; i < 1000; i++)
		{
			auto Ptr = (uint8_t*)(PedestalBeginPlay - i);

			if (*Ptr == 0x48 && *(Ptr + 1) == 0x89 && *(Ptr + 2) == 0x5c)
			{
				RealBeginPlay = (uint64_t)Ptr;
				break;
			}
			else if (*Ptr == 0x40 && *(Ptr + 1) == 0x53 && *(Ptr + 2) == 0x41 && *(Ptr + 3) == 0x56)
			{
				RealBeginPlay = (uint64_t)Ptr;
				break;
			}
		}

		auto ActorVft = AFortTeamMemberPedestal::GetDefaultObj()->Vft;

		for (int i = 0; i < 0x500; i++)
		{
			if (ActorVft[i] == (void*)RealBeginPlay)
			{
				Utils::Hook<AFortTeamMemberPedestal>(uint32_t(i), AActor::GetDefaultObj()->Vft[i]);
				break;
			}
		}
	}
	
	if (VersionInfo.FortniteVersion >= 23)
	{
		auto pattern = Memcury::Scanner::FindPattern("48 8B 01 FF 90 ? ? ? ? 48 8B 8B ? ? ? ? 48 85 C9 74 ? 48 8B 01 FF 90 ? ? ? ? 48 8D 8B");

		auto patchPoint = pattern.ScanFor(VersionInfo.EngineVersion < 5.5 ? std::vector<uint8_t>{ 0x48, 0x89, 0x5C } : std::vector<uint8_t>{ 0x40, 0x53 }, false).ScanFor({0x83, 0xF8, 0x02}).Get();

		Utils::Patch<uint8_t>(patchPoint + 2, 0x1);
	}

	if (VersionInfo.FortniteVersion >= 24.30)
	{
		auto sig = VersionInfo.EngineVersion == 5.3 ? Memcury::Scanner::FindPattern("40 53 48 83 EC ? 48 8B DA 49 8B D0 E8 ? ? ? ? 48 85 C0 0F 85 ? ? ? ? 48 39 83").Get() : Memcury::Scanner::FindPattern("48 89 5C 24 ? 48 89 6C 24 ? 48 89 74 24 ? 57 41 56 41 57 48 83 EC ? 4C 8D B1 ? ? ? ? 33 DB 49 8D 7E").Get();

		if (!sig)
			sig = Memcury::Scanner::FindPattern("40 53 48 83 EC ? 48 8B DA 48 8B D1 48 81 C1 ? ? ? ? E8 ? ? ? ? 48 85 C0 74 ? 4C 8B 0B 45 33 C0").Get();
		
		Utils::Hook(sig, CrashSomething, CrashSomethingOG);
	}

	//Utils::Hook(ImageBase + 0x1CE85F4, Test);
	//Utils::Hook(ImageBase + 0x2788BEC, Test, TestOG);
	//Utils::Hook(Memcury::Scanner::FindPattern("48 89 5C 24 ?? 57 48 83 EC ?? 48 8B DA 48 8B F9 E8 ?? ?? ?? ?? 84 C0 75 ?? 48 83 79").Get(), Test2, Test2OG);
	if (ABuildingProp_LockDevice::StaticClass())
	{
		auto Fn = ABuildingProp_LockDevice::GetDefaultObj()->GetFunction("UnlockObject");

		Utils::Hook<ABuildingProp_LockDevice>(Fn->GetVTableIndex(), Ohio);
	}

	AddVectoredExceptionHandler(1, SyndicateCrashTrace);

	auto ListenCall = FindListenCall();

	if (ListenCall)
	{
		auto OverrideFunc = __int64(DefaultObjImpl("FortHUDContext")->GetFunction("EnterCameraMode")->ExecFunction);

		Utils::Hook(OverrideFunc, Listen);

		auto NewRel = uint32(OverrideFunc - (ListenCall + 5));

		Utils::Patch<uint32>(ListenCall + 1, NewRel);
	}

	if (VersionInfo.FortniteVersion >= 11 && VersionInfo.FortniteVersion < 16)
	{
		auto NearGetOverrideCosmeticLoadout = Memcury::Scanner::FindPattern("4D 8B CD 4C 8D 45 ? 48 8B D6");

		if (!NearGetOverrideCosmeticLoadout.IsValid())
			NearGetOverrideCosmeticLoadout = Memcury::Scanner::FindPattern("4C 8D 45 ? 48 8B D3 48 8B CF E8 ? ? ? ? 0F B6 57");

		if (!NearGetOverrideCosmeticLoadout.IsValid())
			NearGetOverrideCosmeticLoadout = Memcury::Scanner::FindPattern("4C 8D 45 ? 48 8B D3 48 8B CF E8 ? ? ? ? 0F B6 4F");

		if (NearGetOverrideCosmeticLoadout.IsValid())
		{
			auto Rel32 = NearGetOverrideCosmeticLoadout.ScanFor({ 0xE8 }).Get();

			auto OverrideFunc = __int64(DefaultObjImpl("FortHUDContext")->GetFunction("EnterCursorMode")->ExecFunction);

			Utils::Hook(OverrideFunc, InitializeCosmeticLoadout);

			auto NewRel = uint32(OverrideFunc - (Rel32 + 5));

			Utils::Patch<uint32>(Rel32 + 1, NewRel);
		}
	}

}

// Save the World (dev launcher, Pre-season and Season 1 only). Before the tutorial or a mission travels,
// the client reserves an "empty server" through that server's beacon on port 15009 (BEACONPORT_i in the
// backend's matchMakingRequest answer). Nothing listened there, so the reservation timed out after 10 s.
// Step 1: run the engine's own beacon host on 15009, so the connection is answered. Its LogBeacon lines
// then show which beacon the client asks for, which is the next piece to build.
static uintptr_t SyndicateFunctionUsingString(const wchar_t* Text, bool Prefix = false)
{
	auto Base = (uint8_t*)GetModuleHandleW(nullptr); auto Nt = (IMAGE_NT_HEADERS64*)(Base + ((IMAGE_DOS_HEADER*)Base)->e_lfanew);
	auto Sections = IMAGE_FIRST_SECTION(Nt);
	const size_t Len = wcslen(Text) * sizeof(wchar_t) + (Prefix ? 0 : sizeof(wchar_t)); // with the terminator: whole string only
	uint8_t* String = nullptr;
	for (int i = 0; i < Nt->FileHeader.NumberOfSections && !String; i++)
	{
		if (Sections[i].Characteristics & IMAGE_SCN_MEM_EXECUTE) continue;
		auto Start = Base + Sections[i].VirtualAddress; size_t Size = Sections[i].Misc.VirtualSize;
		for (size_t o = 0; o + Len <= Size; o += 2) if (memcmp(Start + o, Text, Len) == 0) { String = Start + o; break; }
	}
	if (!String) return 0;
	for (int i = 0; i < Nt->FileHeader.NumberOfSections; i++)
	{
		if (!(Sections[i].Characteristics & IMAGE_SCN_MEM_EXECUTE)) continue;
		auto Start = Base + Sections[i].VirtualAddress; size_t Size = Sections[i].Misc.VirtualSize;
		for (size_t o = 0; o + 7 <= Size; o++)
		{
			if ((Start[o] != 0x48 && Start[o] != 0x4C) || Start[o + 1] != 0x8D || (Start[o + 2] & 0xC7) != 0x05) continue;
			int32_t Disp; memcpy(&Disp, Start + o + 3, 4);
			if (Start + o + 7 + Disp != String) continue;
			DWORD64 ImageBase = 0; auto Function = RtlLookupFunctionEntry((DWORD64)(Start + o), &ImageBase, nullptr); if (!Function) return 0;
			for (int Depth = 0; Depth < 8; Depth++)
			{
				auto Info = (uint8_t*)(ImageBase + Function->UnwindData);
				if (!((Info[0] >> 3) & 4)) break; // not a chained fragment
				Function = (PRUNTIME_FUNCTION)(Info + 4 + ((Info[2] + 1) & ~1) * 2);
			}
			return (uintptr_t)(ImageBase + Function->BeginAddress);
		}
	}
	return 0;
}
static void SyndicateStwDump(const char* Name, uintptr_t Fn);
static void SyndicateStwLog(const char* Format, ...);
static uintptr_t SyndicateFunctionUsingStringSafe(const wchar_t* Text, bool Prefix = false) { __try { return SyndicateFunctionUsingString(Text, Prefix); } __except (1) { return 0; } }
// Save the World dev notes go to syndicate-stw.log in the host's working folder (Documents\Syndicate\Runtime\Server),
// because this host's stdout does not reach stdout.log reliably.
static void SyndicateStwLog(const char* Format, ...)
{
	char Line[1024]; va_list Args; va_start(Args, Format); vsnprintf(Line, sizeof(Line), Format, Args); va_end(Args);
	printf("%s", Line); fflush(stdout);
	FILE* F = nullptr; if (fopen_s(&F, "syndicate-stw.log", "a") == 0 && F) { fputs(Line, F); fclose(F); }
}
// Dump a function's bytes (as hex, with exe offsets) so its checks can be read and patched.
static void SyndicateStwDump(const char* Name, uintptr_t Fn)
{
	auto Exe = (uintptr_t)GetModuleHandleW(nullptr);
	DWORD64 ImageBase = 0; auto Entry = RtlLookupFunctionEntry((DWORD64)Fn, &ImageBase, nullptr);
	// The unwind entry covers only the first fragment of a big function (0x43 bytes here), so dump a fixed block.
	size_t Size = strcmp(Name, "InitHostBeacon") == 0 ? 0x180 : strncmp(Name, "hang", 4) == 0 ? 0x380 : 0x700; (void)Entry;
	SyndicateStwLog("[Syndicate STW] dump %s exe+0x%llx size 0x%llx\n", Name, (unsigned long long)(Fn - Exe), (unsigned long long)Size);
	for (size_t o = 0; o < Size; o += 16)
	{
		char Hex[128]; int n = 0;
		for (size_t i = o; i < o + 16 && i < Size; i++) n += snprintf(Hex + n, sizeof(Hex) - n, "%02X", ((uint8_t*)Fn)[i]);
		SyndicateStwLog("%llx %s\n", (unsigned long long)(Fn - Exe + o), Hex);
	}
}
static int SyndicateCallWithBool(uintptr_t Fn, void* Self, bool Value) { __try { ((void (*)(void*, bool))Fn)(Self, Value); return 0; } __except (1) { return -1; } }
static int SyndicateCallWithPtr(uintptr_t Fn, void* Self, void* Arg) { __try { ((void (*)(void*, void*))Fn)(Self, Arg); return 0; } __except (1) { return -1; } }
static int SyndicateCallInitPartyBeacon(uintptr_t Fn, void* Self, int Teams, int TeamSize, int MaxReservations, uint64_t SessionName, int ForceTeam)
{ __try { return ((bool (*)(void*, int, int, int, uint64_t, int))Fn)(Self, Teams, TeamSize, MaxReservations, SessionName, ForceTeam) ? 1 : 0; } __except (1) { return -1; } }
static int SyndicateCallWithU64(uintptr_t Fn, void* Self, uint64_t Arg) { __try { return ((bool (*)(void*, uint64_t))Fn)(Self, Arg) ? 1 : 0; } __except (1) { return -1; } }
static int SyndicateCallInitHost(uintptr_t Fn, void* Host) { __try { return ((bool (*)(void*))Fn)(Host) ? 1 : 0; } __except (1) { return -1; } }

// The client always sends an encryption token with its beacon Hello on this build. Fortnite's host handler
// (ReceivedNetworkEncryptionToken) needs a logged-in host user and an online session, which an Erbium host
// does not have, so it answers "[InvalidParams] Unable to get user or session" and the beacon is closed.
// The client got no usable key either (the backend's key is "none": "NewKey is not 32 bytes long,
// ignoring"), so both sides run with the same empty key. Turn the host's failure into success so it
// sends Welcome instead of NMT_Failure. FEncryptionKeyResponse starts with EEncryptionResponse (uint8),
// where Success is 1.
// Find every wide string containing Needle; log each with the function that uses it. Returns the function of
// the first string that starts with one of the Prefer prefixes.
static uintptr_t SyndicateStwFindStrings(const wchar_t* Needle, const wchar_t* const* Prefer, int PreferCount)
{
	auto Base = (uint8_t*)GetModuleHandleW(nullptr); auto Nt = (IMAGE_NT_HEADERS64*)(Base + ((IMAGE_DOS_HEADER*)Base)->e_lfanew);
	auto Sections = IMAGE_FIRST_SECTION(Nt);
	const size_t NeedleLen = wcslen(Needle) * sizeof(wchar_t);
	uintptr_t Chosen = 0; int Found = 0;
	for (int i = 0; i < Nt->FileHeader.NumberOfSections && Found < 24; i++)
	{
		if (Sections[i].Characteristics & IMAGE_SCN_MEM_EXECUTE) continue;
		auto Start = Base + Sections[i].VirtualAddress; size_t Size = Sections[i].Misc.VirtualSize;
		for (size_t o = 0; o + NeedleLen <= Size && Found < 24; o += 2)
		{
			if (memcmp(Start + o, Needle, NeedleLen) != 0) continue;
			// walk back to the start of this string
			size_t b = o; while (b >= 2 && *(wchar_t*)(Start + b - 2) >= 0x20 && *(wchar_t*)(Start + b - 2) < 0x7f && o - b < 400) b -= 2;
			auto Str = (const wchar_t*)(Start + b);
			char Narrow[200]; int n = 0; for (; n < 199 && Str[n]; n++) Narrow[n] = (char)Str[n]; Narrow[n] = 0;
			uintptr_t Fn = 0;
			for (int k = 0; k < Nt->FileHeader.NumberOfSections && !Fn; k++)
			{
				if (!(Sections[k].Characteristics & IMAGE_SCN_MEM_EXECUTE)) continue;
				auto CS = Base + Sections[k].VirtualAddress; size_t CSize = Sections[k].Misc.VirtualSize;
				for (size_t c = 0; c + 7 <= CSize; c++)
				{
					if ((CS[c] != 0x48 && CS[c] != 0x4C) || CS[c + 1] != 0x8D || (CS[c + 2] & 0xC7) != 0x05) continue;
					int32_t Disp; memcpy(&Disp, CS + c + 3, 4);
					if (CS + c + 7 + Disp != (uint8_t*)Str) continue;
					DWORD64 IB = 0; auto F = RtlLookupFunctionEntry((DWORD64)(CS + c), &IB, nullptr); if (!F) break;
					for (int d = 0; d < 8; d++) { auto Info = (uint8_t*)(IB + F->UnwindData); if (!((Info[0] >> 3) & 4)) break; F = (PRUNTIME_FUNCTION)(Info + 4 + ((Info[2] + 1) & ~1) * 2); }
					Fn = (uintptr_t)(IB + F->BeginAddress); break;
				}
			}
			SyndicateStwLog("[Syndicate STW] string \"%s\" used by exe+0x%llx\n", Narrow, (unsigned long long)(Fn ? Fn - (uintptr_t)Base : 0));
			Found++;
			for (int pi = 0; pi < PreferCount && !Chosen; pi++) if (Fn && wcsncmp(Str, Prefer[pi], wcslen(Prefer[pi])) == 0) Chosen = Fn;
			o = b + n * 2; // skip past this string
		}
	}
	return Chosen;
}
static uintptr_t SyndicateStwFindStringsSafe(const wchar_t* Needle, const wchar_t* const* Prefer, int PreferCount) { __try { return SyndicateStwFindStrings(Needle, Prefer, PreferCount); } __except (1) { return 0; } }

// Dev Save the World: the empty-server reservation (exe+0x743d30 in 1.7.2) keeps answering ReservationDenied (9).
// Run it, log its answer, and report ReservationAccepted (8) so the client goes on to the next step.
// APartyBeaconHost::DoesSessionMatch: the host has no online session, so every request (the empty-server
// reservation and the client's reconnect after it) fails with BadSessionId. Dev host: every session matches.
static bool SyndicateSessionMatchHook(void*, void*) { return true; }
bool SyndicateStwZoneTravel = false;            // set once the host leaves for the tutorial zone
static void SyndicateStwListFunctions(const char* Name);
static UObject* SyndicateStwLobbyState = nullptr;   // the lobby beacon state this host spawned
UObject* SyndicateStwLobbyStateForRep() { return SyndicateStwLobbyState; }
static UObject* SyndicateStwJoinClient = nullptr;   // a lobby client that logged in and still has to be sent to the game
static ULONGLONG SyndicateStwJoinAt = 0;
static ULONGLONG SyndicateStwTravelAt = 0;     // when to leave for the zone (after the client's beacon reconnect)
static std::wstring SyndicateStwZoneMap, SyndicateStwZoneMode; // from the onboarding zone theme (recon)
void SyndicateStwTravelTick()
{
	// A second after a lobby login: start the lobby and tell the client to join the game (ALobbyBeaconClient::
	// ClientJoinGame). The client then travels to the session's address, this zone's game port.
	if (SyndicateStwJoinClient && GetTickCount64() >= SyndicateStwJoinAt)
	{
		auto Client = SyndicateStwJoinClient;
		SyndicateStwJoinClient = nullptr;
		if (SyndicateStwLobbyState)
		{
			if (auto O = SyndicateStwLobbyState->GetOffset("bLobbyStarted"); O != uint32(-1)) GetFromOffset<bool>(SyndicateStwLobbyState, O) = true;
			// The client only joins once the lobby state says the server's world record is loaded (1.7.2: +0x659).
			auto O = SyndicateStwLobbyState->GetOffset("bWorldRecordLoaded");
			SyndicateStwLog("[Syndicate STW] lobby: bWorldRecordLoaded at +0x%x\n", O);
			GetFromOffset<bool>(SyndicateStwLobbyState, O != uint32(-1) ? O : 0x659) = true;
		}
		if (auto Join = Client->GetFunction("ClientJoinGame"))
		{
			uint8_t Params[64]{};
			Client->ProcessEvent(Join, Params);
			SyndicateStwLog("[Syndicate STW] lobby: lobby started, told the client to join the game\n");
		}
		else
		{
			SyndicateStwLog("[Syndicate STW] lobby: ClientJoinGame not found\n");
			SyndicateStwListFunctions("FortLobbyBeaconClientPvE");
		}
	}
	if (!SyndicateStwTravelAt || SyndicateStwZoneTravel || GetTickCount64() < SyndicateStwTravelAt) return;
	SyndicateStwZoneTravel = true;
	SyndicateListening = false;
	// "open" through ExecuteConsoleCommand needs a local player, which this host has none of, so use
	// UGameplayStatics::OpenLevel(WorldContext, LevelName, bAbsolute, Options) instead.
	std::wstring Options = SyndicateStwZoneMode.empty() ? L"" : L"game=" + SyndicateStwZoneMode;
	SyndicateStwLog("[Syndicate STW] travelling to the tutorial zone: %ls ?%ls\n", SyndicateStwZoneMap.c_str(), Options.c_str());
	struct { UObject* WorldContext; FName LevelName; bool bAbsolute; uint8_t Pad[7]; FString Options; } Params{};
	Params.WorldContext = (UObject*)UWorld::GetWorld();
	Params.LevelName = FName(SyndicateStwZoneMap.c_str());
	Params.bAbsolute = true;
	Params.Options = FString(Options.c_str());
	auto Statics = (UObject*)UGameplayStatics::GetDefaultObj();
	if (auto OpenLevel = Statics->GetFunction("OpenLevel")) Statics->ProcessEvent(OpenLevel, &Params);
	else SyndicateStwLog("[Syndicate STW] OpenLevel not found\n");
}

static int (*SyndicateReserveOG)(void*, void*, void*) = nullptr;
static int SyndicateReserveHook(void* Host, void* Request, void* Client)
{
	int Result = SyndicateReserveOG(Host, Request, Client);
	SyndicateStwLog("[Syndicate STW] beacon: empty-server reservation returned %d%s\n", Result, Result == 8 ? "" : ", reported as accepted (8)");
	// The empty server now becomes the tutorial. The client reconnects to this beacon a few seconds later to
	// confirm the reservation, so travel only after that (SyndicateStwTravelTick, called from TickFlush).
	if (!SyndicateStwZoneTravel && !SyndicateStwTravelAt && !SyndicateStwZoneMap.empty())
		SyndicateStwTravelAt = GetTickCount64() + 12000;
	return 8;
}
static void (*SyndicateWelcomeOG)(void*, uint8_t*, uint64_t) = nullptr;
static void SyndicateWelcomeHook(void* Host, uint8_t* Response, uint64_t WeakConnection)
{
	if (Response && *Response != 1)
	{
		// An empty key fails every packet ("Key size 0 is not the expected size 32"), so give the host the
		// same fixed dev key the backend hands the client (matchmaking.js, /matchmaking/account/:id/session/:id).
		// FEncryptionKeyResponse: Response (uint8) @0, FString ErrorMsg @8, TArray<uint8> EncryptionKey @24.
		static const char Key[] = "SyndicateStwDevKey0123456789ABCD"; // 32 bytes
		struct FByteArray { uint8_t* Data; int32_t Num; int32_t Max; };
		auto& Array = *(FByteArray*)(Response + 24);
		if (Array.Max < 32) { Array.Data = (uint8_t*)FMemory::Realloc(Array.Data, 32, 0); Array.Max = 32; }
		memcpy(Array.Data, Key, 32);
		Array.Num = 32;
		SyndicateStwLog("[Syndicate STW] beacon: encryption result %d replaced with success and the dev key\n", int(*Response));
		fflush(stdout);
		*Response = 1;
	}
	SyndicateWelcomeOG(Host, Response, WeakConnection);
}

// Save the World recon: list a class's reflected properties (with object / asset values on its default object)
// so the tutorial zone theme shows which map and game mode it uses.
static void SyndicateStwDumpClass(const wchar_t* WPath)
{
	char Path[300]; snprintf(Path, sizeof(Path), "%ls", WPath);
	auto Cls = (const UClass*)StaticLoadObject(WPath, UClass::StaticClass());
	if (!Cls) { SyndicateStwLog("[Syndicate STW] recon: %s not loaded\n", Path); return; }
	std::string DefaultName = std::string("Default__") + std::string(Cls->Name.ToSDKString().c_str());
	auto Cdo = TUObjectArray::FindObject(DefaultName.c_str());
	SyndicateStwLog("[Syndicate STW] recon: class %s (default object %p)\n", Path, Cdo);
	int Count = 0;
	for (auto C = Cls; C && Count < 400; C = GetFromOffset<const UClass*>(C, 0x30))
	{
		for (const UField* Prop = C->GetChildren(); Prop && Count < 400; Prop = Prop->GetNext(), Count++)
		{
			std::string Type = Prop->Class ? std::string(Prop->Class->GetName().ToSDKString().c_str()) : std::string("?");
			if (Type == "Function") continue;
			uint32 Off = GetFromOffset<uint32>(Prop, SDK::Offsets::Offset_Internal);
			std::string Value;
			if (Cdo && (Type == "ObjectProperty" || Type == "ClassProperty"))
			{
				auto V = GetFromOffset<const UObject*>(Cdo, Off);
				Value = V ? std::string(V->Name.ToSDKString().c_str()) : std::string("null");
			}
			else if (Cdo && (Type == "AssetObjectProperty" || Type == "AssetClassProperty" || Type == "SoftObjectProperty" || Type == "SoftClassProperty"))
			{
				auto& Str = GetFromOffset<FString>(Cdo, Off + 0x10);
				Value = std::string(Str.ToString().c_str());
			}
			else if (Cdo && Type == "NameProperty") Value = std::string(GetFromOffset<FName>(Cdo, Off).ToSDKString().c_str());
			std::string PropName(Prop->GetName().ToSDKString().c_str());
			SyndicateStwLog("[Syndicate STW] recon:   %s %s +0x%x %s\n", Type.c_str(), PropName.c_str(), Off, Value.c_str());
			auto Package = [](const std::string& V) { auto Dot = V.find('.'); return std::wstring(V.begin(), Dot == std::string::npos ? V.end() : V.begin() + Dot); };
			if (PropName == "ZoneToUse" && !Value.empty() && SyndicateStwZoneMap.empty()) SyndicateStwZoneMap = Package(Value);
			if (PropName == "ZoneGameMode" && !Value.empty() && SyndicateStwZoneMode.empty()) SyndicateStwZoneMode = std::wstring(Value.begin(), Value.end());
		}
	}
}
// Log the text literals a function uses (the function holding exe+Rva), to tell what a hang stack frame is.
static void SyndicateStwExplain(uintptr_t Rva)
{
	auto Base = (uint8_t*)GetModuleHandleW(nullptr);
	DWORD64 IB = 0; auto F = RtlLookupFunctionEntry((DWORD64)(Base + Rva), &IB, nullptr);
	if (!F) { SyndicateStwLog("[Syndicate STW] explain exe+0x%llx: no function\n", (unsigned long long)Rva); return; }
	auto Start = Base + F->BeginAddress, End = Base + F->EndAddress;
	for (int d = 0; d < 8; d++) { auto Info = (uint8_t*)(IB + F->UnwindData); if (!((Info[0] >> 3) & 4)) break; F = (PRUNTIME_FUNCTION)(Info + 4 + ((Info[2] + 1) & ~1) * 2); }
	SyndicateStwLog("[Syndicate STW] explain exe+0x%llx: function exe+0x%llx, fragment 0x%llx-0x%llx\n", (unsigned long long)Rva, (unsigned long long)F->BeginAddress,
		(unsigned long long)(Start - Base), (unsigned long long)(End - Base));
	int Shown = 0;
	for (auto P = Start; P + 7 <= End && Shown < 6; P++)
	{
		if ((P[0] != 0x48 && P[0] != 0x4C) || P[1] != 0x8D || (P[2] & 0xC7) != 0x05) continue;
		int32_t Disp; memcpy(&Disp, P + 3, 4);
		auto T = P + 7 + Disp;
		if (T < Base || T > Base + 0x8000000) continue;
		char Text[120]; int n = 0;
		if (T[0] >= 0x20 && T[0] < 0x7f && T[1] == 0) // wide
			for (; n < 119 && ((wchar_t*)T)[n] >= 0x20 && ((wchar_t*)T)[n] < 0x7f; n++) Text[n] = (char)((wchar_t*)T)[n];
		else
			for (; n < 119 && T[n] >= 0x20 && T[n] < 0x7f; n++) Text[n] = (char)T[n];
		Text[n] = 0;
		if (n >= 6) { SyndicateStwLog("[Syndicate STW]   \"%s\"\n", Text); Shown++; }
	}
}
static void SyndicateStwExplainSafe(uintptr_t Rva) { __try { SyndicateStwExplain(Rva); } __except (1) {} }

static void SyndicateStwListFunctions(const char* Name)
{
	auto Cls = FindClass(Name);
	if (!Cls) { SyndicateStwLog("[Syndicate STW] functions of %s: class missing\n", Name); return; }
	for (auto C = Cls; C; C = GetFromOffset<const UClass*>(C, 0x30))
	{
		std::string CName(C->Name.ToSDKString().c_str());
		if (CName == "Actor" || CName == "Object") break;
		for (const UField* F = C->GetChildren(); F; F = F->GetNext())
		{
			if (!F->Class || std::string(F->Class->Name.ToSDKString().c_str()) != "Function") continue;
			SyndicateStwLog("[Syndicate STW] functions of %s: %s::%s\n", Name, CName.c_str(), std::string(F->Name.ToSDKString().c_str()).c_str());
		}
	}
}
static void SyndicateStwRecon()
{
	for (auto Name : { "FortGameModeZone", "FortGameModeOutpost", "FortGameModeCampaign", "FortGameModeOnboarding", "FortGameStateZone", "FortZoneTheme", "FortMissionInfo", "FortGameModeZoneOnboarding" })
		SyndicateStwLog("[Syndicate STW] recon: class %s %s\n", Name, FindClass(Name) ? "found" : "missing");
	SyndicateStwDumpClass(L"/Game/World/ZoneThemes/Onboarding/ZT_OnboardingFort/BP_ZT_Onboarding_Fort.BP_ZT_Onboarding_Fort_C");
	SyndicateStwDumpClass(L"/Game/World/MissionGens/MissionGen_Onboarding_Fort.MissionGen_Onboarding_Fort_C");
	for (auto Name : { "FortPartyBeaconClient", "FortPartyBeaconHost", "FortOnlineBeaconHost", "FortGameModeZone", "FortGameSessionDedicated", "FortGameSession" })
		SyndicateStwListFunctions(Name);
	// Frames of the game-thread hang seen right after the zone loaded (SyndicateHang, 1.7.2).
	for (uintptr_t Rva : { 0x120b712, 0x24688f6, 0x1313140, 0x26ad2f, 0x273de3, 0x123e5f1, 0x17274d0, 0x12dd740, 0x26c96c, 0x6658d70, 0x26c9ca, 0x274881 })
		SyndicateStwExplainSafe(Rva);
	{
		auto Base = (uintptr_t)GetModuleHandleW(nullptr);
		for (uintptr_t Rva : { 0x120b6e0, 0x2468660, 0x1313140 })
			SyndicateStwDump("hang", Base + Rva);
	}
}
static void SyndicateStwReconSafe() { __try { SyndicateStwRecon(); } __except (1) { SyndicateStwLog("[Syndicate STW] recon crashed (caught)\n"); } }

// After the client's reconnect is answered, the dedicated server tells it to come in with the client RPC
// FortPartyBeaconClient::ClientAllowedToProceedFromReservation; without it the client waits forever.
static void (*SyndicateReconnectOG)(UObject*, FFrame&, void*) = nullptr;
static void SyndicateReconnectHook(UObject* Context, FFrame& Stack, void* Result)
{
	SyndicateReconnectOG(Context, Stack, Result);
	if (auto Proceed = Context->GetFunction("ClientAllowedToProceedFromReservation"))
	{
		uint8_t Params[64]{};
		Context->ProcessEvent(Proceed, Params);
		SyndicateStwLog("[Syndicate STW] beacon: reconnect answered, told the client to proceed\n");
	}
	else SyndicateStwLog("[Syndicate STW] beacon: ClientAllowedToProceedFromReservation not found\n");
}

static void* SyndicateStwSpawnSafe(const UClass* Class) { __try { return (void*)UWorld::SpawnActor(Class, FVector{}, FRotator{}); } __except (1) { return nullptr; } }

// The lobby login registers the player with the host's online session "Game", which this host never created:
// "OSS: No game present to join for session (Game)" and the login fails. Every function that logs that text
// is made to report success on the dev host. (x64: a detour that ignores its arguments and returns 1 fits any
// of their signatures.)
static uint64_t SyndicateStwReturnTrue() { return 1; }
static void SyndicateStwPassNoGameChecks()
{
	auto Base = (uint8_t*)GetModuleHandleW(nullptr); auto Nt = (IMAGE_NT_HEADERS64*)(Base + ((IMAGE_DOS_HEADER*)Base)->e_lfanew);
	auto Sections = IMAGE_FIRST_SECTION(Nt);
	const wchar_t* Needle = L"No game present to join for session";
	const size_t Len = wcslen(Needle) * sizeof(wchar_t);
	std::vector<uint8_t*> Strings;
	for (int i = 0; i < Nt->FileHeader.NumberOfSections; i++)
	{
		if (Sections[i].Characteristics & IMAGE_SCN_MEM_EXECUTE) continue;
		auto Start = Base + Sections[i].VirtualAddress; size_t Size = Sections[i].Misc.VirtualSize;
		for (size_t o = 0; o + Len <= Size; o += 2) if (memcmp(Start + o, Needle, Len) == 0) Strings.push_back(Start + o);
	}
	std::vector<uintptr_t> Functions;
	for (int i = 0; i < Nt->FileHeader.NumberOfSections; i++)
	{
		if (!(Sections[i].Characteristics & IMAGE_SCN_MEM_EXECUTE)) continue;
		auto CS = Base + Sections[i].VirtualAddress; size_t CSize = Sections[i].Misc.VirtualSize;
		for (size_t c = 0; c + 7 <= CSize; c++)
		{
			if ((CS[c] != 0x48 && CS[c] != 0x4C) || CS[c + 1] != 0x8D || (CS[c + 2] & 0xC7) != 0x05) continue;
			int32_t Disp; memcpy(&Disp, CS + c + 3, 4);
			auto T = CS + c + 7 + Disp;
			if (std::find(Strings.begin(), Strings.end(), T) == Strings.end()) continue;
			DWORD64 IB = 0; auto F = RtlLookupFunctionEntry((DWORD64)(CS + c), &IB, nullptr); if (!F) continue;
			for (int d = 0; d < 8; d++) { auto Info = (uint8_t*)(IB + F->UnwindData); if (!((Info[0] >> 3) & 4)) break; F = (PRUNTIME_FUNCTION)(Info + 4 + ((Info[2] + 1) & ~1) * 2); }
			auto Fn = (uintptr_t)(IB + F->BeginAddress);
			if (std::find(Functions.begin(), Functions.end(), Fn) == Functions.end()) Functions.push_back(Fn);
		}
	}
	SyndicateStwLog("[Syndicate STW] session: %d copies of the no-game text, %d functions use it\n", (int)Strings.size(), (int)Functions.size());
	for (auto Fn : Functions)
	{
		Utils::Hook((uint64_t)Fn, SyndicateStwReturnTrue);
		MH_EnableHook((LPVOID)Fn);
		SyndicateStwLog("[Syndicate STW] session: exe+0x%llx now reports success\n", (unsigned long long)(Fn - (uintptr_t)Base));
	}
}
static void SyndicateStwPassNoGameChecksSafe() { __try { SyndicateStwPassNoGameChecks(); } __except (1) { SyndicateStwLog("[Syndicate STW] session: patch crashed (caught)\n"); } }

// ALobbyBeaconHost::ProcessLogin (1.7.2): after the session check it calls the host's virtual PreLogin
// (vtable +0x628, (this, UniqueId, ErrorMsg) -> bool) and then HandlePlayerLogin (-> player state). On the dev
// host PreLogin refuses (no backend login for the dedicated server). Log both answers; PreLogin always passes.
static bool (*SyndicateLobbyPreLoginOG)(void*, void*, void*) = nullptr;
static bool SyndicateLobbyPreLoginHook(void* Host, void* UniqueId, void* ErrorMsg)
{
	bool Ok = SyndicateLobbyPreLoginOG(Host, UniqueId, ErrorMsg);
	auto& Err = *(FString*)ErrorMsg;
	SyndicateStwLog("[Syndicate STW] lobby: PreLogin said %s%s%s\n", Ok ? "yes" : "no", Err.Num() > 1 ? ": " : "", Err.Num() > 1 ? Err.ToString().c_str() : "");
	if (!Ok) Err = FString(L"");
	return true;
}
static void* (*SyndicateLobbyHandleLoginOG)(void*, void*, void*, void*) = nullptr;
static void* SyndicateLobbyHandleLoginHook(void* Host, void* Client, void* UniqueId, void* Options)
{
	void* Player = SyndicateLobbyHandleLoginOG(Host, Client, UniqueId, Options);
	SyndicateStwLog("[Syndicate STW] lobby: HandlePlayerLogin returned %p\n", Player);
	if (Player)
	{
		// The client's replicated LobbyState / PlayerState are how it reads the lobby (world record, mission).
		auto C = (UObject*)Client;
		if (auto O = C->GetOffset("LobbyState"); O != uint32(-1) && SyndicateStwLobbyState) GetFromOffset<UObject*>(C, O) = SyndicateStwLobbyState;
		if (auto O = C->GetOffset("PlayerState"); O != uint32(-1)) GetFromOffset<void*>(C, O) = Player;
		SyndicateStwJoinClient = C; SyndicateStwJoinAt = GetTickCount64() + 3000;
	}
	return Player;
}

// AFortLobbyBeaconState::CreateNewPlayer (1.7.2 exe+0x6c6be0) calls Super::CreateNewPlayer (spawns the player
// state), then asks the zone game mode which team the player gets and refuses team 0x67. Log both answers;
// an unknown player goes on the Save the World team.
static void* (*SyndicateLobbySuperCreateOG)(void*, void*, void*) = nullptr;
static void* SyndicateLobbySuperCreateHook(void* State, void* Name, void* UniqueId)
{
	void* R = SyndicateLobbySuperCreateOG(State, Name, UniqueId);
	SyndicateStwLog("[Syndicate STW] lobby: base CreateNewPlayer returned %p\n", R);
	return R;
}
static uint8_t (*SyndicateLobbyTeamOG)(void*, void*) = nullptr;
static uint8_t SyndicateLobbyTeamHook(void* Teams, void* UniqueId)
{
	uint8_t R = SyndicateLobbyTeamOG(Teams, UniqueId);
	// No matchmaking service told the zone about this player, so it has no team (0x67) and the login is refused.
	// Put them on EFortTeam::HumanCampaign (0), the Save the World players' team.
	SyndicateStwLog("[Syndicate STW] lobby: team for the player (teams %p) = 0x%x%s\n", Teams, R, R == 0x67 ? ", using HumanCampaign (0)" : "");
	return R == 0x67 ? 0 : R;
}

// FLobbyPlayerStateInfoArray::AddPlayer (1.7.2 exe+0x3af7690, called by HandlePlayerLogin with
// (&LobbyState->Players, PlayerName, UniqueId)): spawns the lobby player state. Log what it gets and returns.
static void* (*SyndicateLobbyAddPlayerOG)(void*, void*, void*) = nullptr;
static void* SyndicateLobbyAddPlayerHook(void* Players, void* Name, void* UniqueId)
{
	auto Q = (uintptr_t*)Players;
	SyndicateStwLog("[Syndicate STW] lobby: AddPlayer players %p: %p %p %p %p %p %p %p %p\n", Players, (void*)Q[0], (void*)Q[1], (void*)Q[2], (void*)Q[3], (void*)Q[4], (void*)Q[5], (void*)Q[6], (void*)Q[7]);
	void* R = SyndicateLobbyAddPlayerOG(Players, Name, UniqueId);
	SyndicateStwLog("[Syndicate STW] lobby: AddPlayer returned %p\n", R);
	return R;
}

// McpProfile::SendRequestNow (1.7.2 exe+0x3a11c00) signs the zone's profile requests with this host's own account
// (identity local user 0). The launcher gives the STW host an exchange code (-AUTH_*); sign in with it through
// IOnlineIdentity::AutoLogin(0) (vtable +0xa8; GetUniquePlayerId is +0xc0) the first time a request is made.
static uint64_t (*SyndicateSendRequestOG)(void*, void*, void*, void*, void*, void*, void*, void*) = nullptr;
static bool SyndicateStwHostLoginTried = false;
static void SyndicateStwHostLogin(void* Profile)
{
	auto Identity = *(void***)((uint8_t*)Profile + 0x1d8);
	if (!Identity) { SyndicateStwLog("[Syndicate STW] host login: no identity\n"); return; }
	auto VT = *(uintptr_t**)Identity;
	auto Exe = (uintptr_t)GetModuleHandleW(nullptr);
	struct { void* Object; void* Ref; } Id{};
	((void(*)(void*, void*, int))VT[0xc0 / 8])(Identity, &Id, 0);
	SyndicateStwLog("[Syndicate STW] host login: local user 0 is %p; Login exe+0x%llx AutoLogin exe+0x%llx\n", Id.Object, (unsigned long long)(VT[0x98 / 8] - Exe), (unsigned long long)(VT[0xa8 / 8] - Exe));
	if (Id.Object) return;
	bool Ok = ((bool(*)(void*, int))VT[0xa8 / 8])(Identity, 0);
	SyndicateStwLog("[Syndicate STW] host login: AutoLogin(0) started: %d\n", int(Ok));
}
static void SyndicateStwHostLoginSafe(void* Profile) { __try { SyndicateStwHostLogin(Profile); } __except (1) { SyndicateStwLog("[Syndicate STW] host login crashed (caught)\n"); } }
static uint64_t SyndicateSendRequestHook(void* A, void* B, void* C, void* D, void* E, void* F, void* G, void* H)
{
	if (!SyndicateStwHostLoginTried) { SyndicateStwHostLoginTried = true; SyndicateStwHostLoginSafe(A); }
	return SyndicateSendRequestOG(A, B, C, D, E, F, G, H);
}

void SyndicateStwBeaconStart()
{
	static bool Done = false;
	if (Done) return;
	Done = true;
	wchar_t Flag[4]{};
	if (GetEnvironmentVariableW(L"SYNDICATE_STW_DEV", Flag, 4) != 1 || Flag[0] != L'1')
	{
		SyndicateStwLog("[Syndicate STW] beacon: off (not started by the dev launcher)\n");
		fflush(stdout);
		return;
	}
	if (VersionInfo.FortniteVersion >= 2.0)
	{
		SyndicateStwLog("[Syndicate STW] beacon: skipped, Save the World dev mode is Pre-season/Season 1 only\n");
		return;
	}

	SyndicateStwReconSafe();
	SyndicateStwPassNoGameChecksSafe();
	if (auto Client = TUObjectArray::FindObject("Default__FortPartyBeaconClient"))
		if (auto Fn = Client->GetFunction("ServerReconnectExistingReservation"))
		{
			Utils::ExecHook((UFunction*)Fn, SyndicateReconnectHook, SyndicateReconnectOG);
			SyndicateStwLog("[Syndicate STW] beacon: reconnect hooked\n");
		}
	const char* Names[] = { "FortOnlineBeaconHost", "OnlineBeaconHost" };
	const UClass* HostClass = nullptr;
	const char* HostName = "";
	for (auto Name : Names) if ((HostClass = FindClass(Name))) { HostName = Name; break; }
	SyndicateStwLog("[Syndicate STW] beacon: host class %s\n", HostClass ? HostName : "(none found)");
	for (auto Extra : { "PartyBeaconHost", "FortPartyBeaconHost", "FortPartyBeaconClient", "PartyBeaconClient" })
		SyndicateStwLog("[Syndicate STW] beacon: class %s %s\n", Extra, FindClass(Extra) ? "found" : "missing");
	if (!HostClass) return;

	auto Exe = (uintptr_t)GetModuleHandleW(nullptr);
	auto InitHost = SyndicateFunctionUsingStringSafe(L"AOnlineBeaconHost::InitHost failed");
	auto ByPort = SyndicateFunctionUsingStringSafe(L"BeaconPort=");
	SyndicateStwLog("[Syndicate STW] beacon: InitHost at exe+0x%llx (BeaconPort= is used by exe+0x%llx)\n",
		(unsigned long long)(InitHost ? InitHost - Exe : 0), (unsigned long long)(ByPort ? ByPort - Exe : 0));
	if (!InitHost) return;

	auto Welcome = SyndicateFunctionUsingStringSafe(L"OnlineBeaconHost::SendWelcomeControlMessage: encryption failure [%s] %s");
	SyndicateStwLog("[Syndicate STW] beacon: SendWelcomeControlMessage at exe+0x%llx\n", (unsigned long long)(Welcome ? Welcome - Exe : 0));
	if (Welcome)
	{
		Utils::Hook(Welcome, SyndicateWelcomeHook, SyndicateWelcomeOG);
		MH_EnableHook((LPVOID)Welcome);
	}

	auto Host = UWorld::SpawnActor(HostClass, FVector{}, FRotator{});
	if (!Host)
	{
		SyndicateStwLog("[Syndicate STW] beacon: could not spawn the beacon host\n");
		return;
	}
	if (auto O = Host->GetOffset("ListenPort"); O != uint32(-1)) GetFromOffset<int>(Host, O) = 15009;
	else SyndicateStwLog("[Syndicate STW] beacon: no ListenPort property, the host keeps its default port\n");

	int Result = SyndicateCallInitHost(InitHost, Host);
	SyndicateStwLog("[Syndicate STW] beacon: InitHost %s\n", Result == 1 ? "listening" : Result == 0 ? "failed" : "crashed (caught)");
	if (Result != 1) { fflush(stdout); return; }
	if (auto O = Host->GetOffset("ListenPort"); O != uint32(-1))
		SyndicateStwLog("[Syndicate STW] beacon: listening on port %d\n", GetFromOffset<int>(Host, O));

	// A new beacon host refuses every connection ("NotifyAcceptingConnection: Server FortOnlineBeaconHost_0
	// refused") until requests are resumed. AOnlineBeacon::PauseBeaconRequests(false) does that; it is found
	// through its own log line. Fallback: BeaconState (not a reflected property) sits right after NetDriver.
	auto Pause = SyndicateFunctionUsingStringSafe(L"All beacon requests resumed.");
	SyndicateStwLog("[Syndicate STW] beacon: PauseBeaconRequests at exe+0x%llx\n", (unsigned long long)(Pause ? Pause - Exe : 0));
	if (Pause && SyndicateCallWithBool(Pause, Host, false) == 0)
		SyndicateStwLog("[Syndicate STW] beacon: requests resumed\n");
	else if (auto O = Host->GetOffset("NetDriver"); O != uint32(-1))
	{
		auto& State = GetFromOffset<int>(Host, O + 8);
		SyndicateStwLog("[Syndicate STW] beacon: BeaconState was %d\n", State);
		if (State == 1) { State = 0; SyndicateStwLog("[Syndicate STW] beacon: requests allowed (state set by hand)\n"); }
	}

	// The client's beacon (FortPartyBeaconClient) joins a host object of its type. Register a party beacon
	// host so the join is routed; how it answers the reservation shows what to build next.
	const UClass* PartyClass = FindClass("FortPartyBeaconHost");
	if (!PartyClass) PartyClass = FindClass("PartyBeaconHost");
	auto Register = SyndicateFunctionUsingStringSafe(L"Beacon host type %s already exists");
	SyndicateStwLog("[Syndicate STW] beacon: RegisterHost at exe+0x%llx\n", (unsigned long long)(Register ? Register - Exe : 0));
	if (PartyClass && Register)
	{
		auto Party = UWorld::SpawnActor(PartyClass, FVector{}, FRotator{});
		int R = Party ? SyndicateCallWithPtr(Register, Host, Party) : -2;
		SyndicateStwLog("[Syndicate STW] beacon: party host %s\n", R == 0 ? "registered" : R == -2 ? "could not be spawned" : "register crashed (caught)");
		// The reservation is denied while the party host has no State (UPartyBeaconState), which only
		// APartyBeaconHost::InitHostBeacon(TeamCount, TeamSize, MaxReservations, SessionName, ForceTeamNum) creates.
		// One team of up to four players, one reservation, session "GameSession".
		if (Party && R == 0)
		{
			static const wchar_t* const Prefer[] = { L"InitPartyBeacon", L"InitHostBeacon", L"Init party beacon", L"InitPartyBeaconHost" };
			auto InitParty = SyndicateStwFindStringsSafe(L"TeamCount", Prefer, 4);
			SyndicateStwLog("[Syndicate STW] beacon: InitHostBeacon at exe+0x%llx\n", (unsigned long long)(InitParty ? InitParty - Exe : 0));
			if (InitParty)
			{
				FName Session(L"GameSession"); uint64_t SessionRaw = 0; memcpy(&SessionRaw, &Session, sizeof(SessionRaw) < sizeof(Session) ? sizeof(SessionRaw) : sizeof(Session));
				int Ok = SyndicateCallInitPartyBeacon(InitParty, Party, 1, 4, 1, SessionRaw, 0);
				SyndicateStwLog("[Syndicate STW] beacon: InitHostBeacon %s\n", Ok == 1 ? "ok" : Ok == 0 ? "returned false" : "crashed (caught)");
				// The reservation reads its UPartyBeaconState at this+0x510 and denies when that is null. Log the
				// pointers around it and dump InitHostBeacon to see where the state it made was stored.
				for (uint32_t Off = 0x4C0; Off <= 0x540; Off += 8)
					SyndicateStwLog("[Syndicate STW] party+0x%x = %p\n", Off, *(void**)((uint8_t*)Party + Off));
				if (auto O = ((UObject*)Party)->GetOffset("State"); O != uint32(-1))
				{
					SyndicateStwLog("[Syndicate STW] party State property at +0x%x\n", O);
					// The empty-server reservation reads a second state pointer at +0x510 (null: "Reservation
					// Denied", and the client's reconnect then finds nothing). Point it at the same state.
					auto& Empty = *(void**)((uint8_t*)Party + 0x510);
					auto State = *(void**)((uint8_t*)Party + O);
					if (!Empty && State) { Empty = State; SyndicateStwLog("[Syndicate STW] party+0x510 set to the party state %p\n", State); }
				}
				SyndicateStwDump("InitHostBeacon", InitParty);
			}
		}
		// Next: the empty-server reservation is answered "Bad Session Id" (the host has no online session with
		// the id the backend gave the client). Dump the function that decides, to find that check.
		// In 1.7.2 it reads (exe+0x76683f): mov ebx,13 (BadSessionId); call DoesSessionMatch; test al,al;
		// je skip; ...call the reservation itself. The host has no online session, so the match always fails.
		// Turn that one je into two NOPs, after checking the exact bytes are there.
		if (auto Fn = SyndicateFunctionUsingStringSafe(L"ProcessEmptyServerReservationRequest result", true))
		{
			bool Patched = false;
			for (size_t o = 0; o < 0x400 && !Patched; o++)
			{
				auto P = (uint8_t*)Fn + o;
				if (P[0] == 0xBB && P[1] == 0x0D && P[2] == 0 && P[3] == 0 && P[4] == 0 && P[5] == 0xE8 && P[10] == 0x84 && P[11] == 0xC0 && P[12] == 0x74)
				{
					DWORD Old; VirtualProtect(P + 12, 2, PAGE_EXECUTE_READWRITE, &Old);
					P[12] = 0x90; P[13] = 0x90;
					VirtualProtect(P + 12, 2, Old, &Old);
					FlushInstructionCache(GetCurrentProcess(), P + 12, 2);
					Patched = true;
					SyndicateStwLog("[Syndicate STW] beacon: empty-server session check skipped at exe+0x%llx\n", (unsigned long long)((uintptr_t)P + 12 - Exe));
					{
						int32_t MatchRel; memcpy(&MatchRel, P + 6, 4);
						auto Match = (uintptr_t)(P + 10 + MatchRel);
						Utils::Hook((uint64_t)Match, SyndicateSessionMatchHook);
						MH_EnableHook((LPVOID)Match);
						SyndicateStwLog("[Syndicate STW] beacon: DoesSessionMatch at exe+0x%llx always matches\n", (unsigned long long)(Match - Exe));
					}
					// The reservation itself (call at P+23) then answers "Reservation Denied": dump it to see why.
					// The reservation itself (call at P+23) then answers "Reservation Denied" (9). In 1.7.2 four
					// conditional jumps lead there (exe+0x743d30 + 0x31/0xC8/0xD5/0xED): a beacon-state check and
					// three UPartyBeaconState checks. A dev host serves one party, so skip all four; each is
					// checked (0F 8x, target = the deny block) before it is replaced with NOPs.
					if (P[23] == 0xE8)
					{
						int32_t Rel; memcpy(&Rel, P + 24, 4);
						auto R = (uint8_t*)(P + 28 + Rel);
						Utils::Hook((uint64_t)R, SyndicateReserveHook, SyndicateReserveOG);
						MH_EnableHook((LPVOID)R);
						SyndicateStwLog("[Syndicate STW] beacon: reservation hooked at exe+0x%llx\n", (unsigned long long)((uintptr_t)R - Exe));
						// +0x9E: "existing reservation for this leader" (-> Denied unless duplicate); the State reports one even
						// when it holds none, so the reservation was never recorded and the client's reconnect got NotFound.
						struct { uint32_t At; uint32_t Target; } Jumps[] = { { 0x31, 0x2F6 }, { 0x9E, 0x2B6 }, { 0xC8, 0x2AF }, { 0xD5, 0x2AF }, { 0xED, 0x2AF } };
						for (auto& J : Jumps)
						{
							auto Q = R + J.At; int32_t D; memcpy(&D, Q + 2, 4);
							if (Q[0] == 0x0F && (Q[1] & 0xF0) == 0x80 && (uint32_t)(Q + 6 + D - R) == J.Target)
							{
								DWORD Old2; VirtualProtect(Q, 6, PAGE_EXECUTE_READWRITE, &Old2); memset(Q, 0x90, 6); VirtualProtect(Q, 6, Old2, &Old2);
								FlushInstructionCache(GetCurrentProcess(), Q, 6);
								SyndicateStwLog("[Syndicate STW] beacon: reservation deny check at exe+0x%llx skipped\n", (unsigned long long)((uintptr_t)Q - Exe));
							}
							else SyndicateStwLog("[Syndicate STW] beacon: reservation deny check at +0x%x not as expected, left alone\n", J.At);
						}
					}
				}
			}
			if (!Patched) { SyndicateStwLog("[Syndicate STW] beacon: session check pattern not found\n"); SyndicateStwDump("ProcessEmptyServerReservationRequest", Fn); }
		}
		else SyndicateStwLog("[Syndicate STW] beacon: ProcessEmptyServerReservationRequest not found\n");
		if (Party) SyndicateStwLog("[Syndicate STW] beacon: party host vtable exe+0x%llx\n", (unsigned long long)(*(uintptr_t*)Party - Exe));
		// After "allowed to proceed" the client opens a lobby beacon (FortLobbyBeaconClientPvE) on the same port;
		// without a matching host object the join fails with "Couldn't spawn beacon". Register a lobby host too.
		const UClass* LobbyClass = nullptr; const char* LobbyName = "";
		for (auto Name : { "FortLobbyBeaconHostPvE", "FortLobbyBeaconHost", "LobbyBeaconHost" }) if ((LobbyClass = FindClass(Name))) { LobbyName = Name; break; }
		SyndicateStwLog("[Syndicate STW] beacon: lobby host class %s\n", LobbyClass ? LobbyName : "(none found)");
		for (auto Name : { "FortLobbyBeaconClientPvE", "FortLobbyBeaconStatePvE", "FortLobbyBeaconState", "LobbyBeaconState", "FortLobbyBeaconPlayerState" })
			SyndicateStwLog("[Syndicate STW] beacon: class %s %s\n", Name, FindClass(Name) ? "found" : "missing");
		// The zone's profile requests for the player: "Invalid user auth for id=... Need to login first".
		// McpProfile::SendRequestNow: sign the host in before its first profile request (SyndicateSendRequestHook).
		if (auto Send = SyndicateFunctionUsingStringSafe(L"Invalid user auth for id", true))
		{
			Utils::Hook((uint64_t)Send, SyndicateSendRequestHook, SyndicateSendRequestOG);
			MH_EnableHook((LPVOID)Send);
			SyndicateStwLog("[Syndicate STW] beacon: profile requests (exe+0x%llx) sign the host in first\n", (unsigned long long)(Send - Exe));
		}
		if (LobbyClass)
		{
			SyndicateStwListFunctions(LobbyName);
			auto Lobby = UWorld::SpawnActor(LobbyClass, FVector{}, FRotator{});
			if (Lobby)
			{
				// ALobbyBeaconHost::Init(SessionName): spawn LobbyStateClass into LobbyState and set SessionName.
				// Without it every login fails ("No lobby beacon state"). Done here through reflection.
				{
					auto L = (UObject*)Lobby;
					auto OCls = L->GetOffset("LobbyStateClass"), OState = L->GetOffset("LobbyState"), OSession = L->GetOffset("SessionName");
					SyndicateStwLog("[Syndicate STW] beacon: lobby offsets class +0x%x state +0x%x session +0x%x\n", OCls, OState, OSession);
					const UClass* StateClass = FindClass("FortLobbyBeaconStatePvE");
					if (!StateClass) StateClass = FindClass("FortLobbyBeaconState");
					SyndicateStwLog("[Syndicate STW] beacon: lobby state class %p, spawning\n", StateClass);
					void* State = StateClass && OState != uint32(-1) ? SyndicateStwSpawnSafe(StateClass) : nullptr;
					SyndicateStwLog("[Syndicate STW] beacon: lobby state %p\n", State);
					if (State) { GetFromOffset<void*>(L, OState) = State; SyndicateStwLobbyState = (UObject*)State; }
					// ALobbyBeaconHost::SetupLobbyState(MaxPlayers) also sets the state's MaxPlayers (0 refuses every
					// player). Set it, and the player state class if the default object left it empty.
					if (State)
					{
						auto St = (UObject*)State;
						// HandlePlayerLogin only adds a player when the state's Role is ROLE_Authority (3).
						if (auto O = St->GetOffset("Role"); O != uint32(-1))
						{
							auto& Role = GetFromOffset<uint8_t>(St, O);
							SyndicateStwLog("[Syndicate STW] beacon: lobby state Role was %d (+0x%x), set to 3\n", int(Role), O);
							Role = 3;
						}
						// FLobbyPlayerStateInfoArray::AddPlayer calls the state's CreateNewPlayer (vtable +0x5f0).
						{
							auto Create = (*(uintptr_t**)St)[0x5f0 / 8];
							SyndicateStwLog("[Syndicate STW] beacon: lobby state CreateNewPlayer exe+0x%llx, players parent %p\n", (unsigned long long)(Create - Exe), *(void**)((uint8_t*)St + 0x3a8 + 0xc0));
							SyndicateStwDump("CreateNewPlayer", Create);
							auto C = (uint8_t*)Create;
							if (C[0x15] == 0xE8 && C[0xC6] == 0xE8)
							{
								int32_t R1, R2; memcpy(&R1, C + 0x16, 4); memcpy(&R2, C + 0xC7, 4);
								auto Super = (uintptr_t)(C + 0x1A + R1), Team = (uintptr_t)(C + 0xCB + R2);
								Utils::Hook((uint64_t)Super, SyndicateLobbySuperCreateHook, SyndicateLobbySuperCreateOG); MH_EnableHook((LPVOID)Super);
								Utils::Hook((uint64_t)Team, SyndicateLobbyTeamHook, SyndicateLobbyTeamOG); MH_EnableHook((LPVOID)Team);
								SyndicateStwLog("[Syndicate STW] beacon: base CreateNewPlayer exe+0x%llx and team pick exe+0x%llx hooked\n", (unsigned long long)(Super - Exe), (unsigned long long)(Team - Exe));
							}
							else SyndicateStwLog("[Syndicate STW] beacon: CreateNewPlayer calls not where expected\n");
						}
						// The client decides the world record is loaded from the replicated MissionGeneratorDetailsRequirement:
						// 1 (no mission generator needed), or 2 with MissionGeneratorClassPath set. Give it the tutorial's
						// mission generator, the way AFortLobbyBeaconState's setter does once the zone is ready.
						{
							auto OPath = St->GetOffset("MissionGeneratorClassPath"), OReq = St->GetOffset("MissionGeneratorDetailsRequirement");
							if (OPath != uint32(-1) && OReq != uint32(-1))
							{
								GetFromOffset<FString>(St, OPath) = FString(L"/Game/World/MissionGens/MissionGen_Onboarding_Fort.MissionGen_Onboarding_Fort_C");
								GetFromOffset<uint8_t>(St, OReq) = 2;
								SyndicateStwLog("[Syndicate STW] beacon: lobby mission generator set (+0x%x, requirement +0x%x = 2)\n", OPath, OReq);
							}
							else SyndicateStwLog("[Syndicate STW] beacon: lobby state has no mission generator properties\n");
						}
						if (auto O = St->GetOffset("bWorldRecordLoaded"); O != uint32(-1)) GetFromOffset<bool>(St, O) = true;
						if (auto O = St->GetOffset("MaxPlayers"); O != uint32(-1)) { GetFromOffset<int>(St, O) = 4; SyndicateStwLog("[Syndicate STW] beacon: lobby MaxPlayers set to 4 (+0x%x)\n", O); }
						else SyndicateStwLog("[Syndicate STW] beacon: lobby state has no MaxPlayers property\n");
						if (auto O = St->GetOffset("LobbyBeaconPlayerStateClass"); O != uint32(-1))
						{
							auto& Cls = GetFromOffset<const UClass*>(St, O);
							SyndicateStwLog("[Syndicate STW] beacon: lobby player state class %s\n", Cls ? std::string(Cls->Name.ToSDKString().c_str()).c_str() : "(empty)");
							if (!Cls) Cls = FindClass("FortLobbyBeaconPlayerState");
						}
						for (auto Name : { "bLobbyStarted", "LobbyStarted" }) if (auto O = St->GetOffset(Name); O != uint32(-1)) SyndicateStwLog("[Syndicate STW] beacon: lobby state has %s at +0x%x\n", Name, O);
					}
					// ALobbyBeaconHost::ProcessLogin (1.7.2 exe+0x3afab70) first checks the client's session id
					// against the host's online session (mov rdx,rax; mov rcx,r14; xor r12b,r12b; call; test al,al; je).
					// This host has no online session: make that session check always pass.
					if (auto Login = SyndicateFunctionUsingStringSafe(L"ProcessLogin %s SessionId", true))
					{
						bool Hooked = false;
						for (size_t o = 0; o < 0x400 && !Hooked; o++)
						{
							auto P = (uint8_t*)Login + o;
							if (P[0] == 0x48 && P[1] == 0x8B && P[2] == 0xD0 && P[3] == 0x49 && P[4] == 0x8B && P[5] == 0xCE && P[6] == 0x45 && P[7] == 0x32 && P[8] == 0xE4 && P[9] == 0xE8 && P[14] == 0x84 && P[15] == 0xC0)
							{
								int32_t Rel; memcpy(&Rel, P + 10, 4);
								auto Match = (uintptr_t)(P + 14 + Rel);
								Utils::Hook((uint64_t)Match, SyndicateSessionMatchHook);
								MH_EnableHook((LPVOID)Match);
								SyndicateStwLog("[Syndicate STW] beacon: lobby session check at exe+0x%llx always matches\n", (unsigned long long)(Match - Exe));
								Hooked = true;
								// PreLogin: vtable slot +0x628 of the lobby host.
								auto Slot = (void**)(*(uintptr_t*)Lobby + 0x628);
								SyndicateLobbyPreLoginOG = (bool (*)(void*, void*, void*))*Slot;
								DWORD OldP; VirtualProtect(Slot, 8, PAGE_READWRITE, &OldP); *Slot = (void*)SyndicateLobbyPreLoginHook; VirtualProtect(Slot, 8, OldP, &OldP);
								SyndicateStwLog("[Syndicate STW] beacon: lobby PreLogin (exe+0x%llx) hooked\n", (unsigned long long)((uintptr_t)SyndicateLobbyPreLoginOG - Exe));
								// HandlePlayerLogin: lea r9,[rsp+50]; mov r8,r13; mov rdx,r15; mov rcx,r14; call
								for (size_t h = 0; h < 0x600; h++)
								{
									auto Q = (uint8_t*)Login + h;
									if (Q[0] == 0x4C && Q[1] == 0x8D && Q[2] == 0x4C && Q[3] == 0x24 && Q[4] == 0x50 && Q[5] == 0x4D && Q[6] == 0x8B && Q[7] == 0xC5 && Q[8] == 0x49 && Q[9] == 0x8B && Q[10] == 0xD7 && Q[14] == 0xE8)
									{
										int32_t R3; memcpy(&R3, Q + 15, 4);
										auto Handle = (uintptr_t)(Q + 19 + R3);
										Utils::Hook((uint64_t)Handle, SyndicateLobbyHandleLoginHook, SyndicateLobbyHandleLoginOG);
										MH_EnableHook((LPVOID)Handle);
										SyndicateStwLog("[Syndicate STW] beacon: HandlePlayerLogin at exe+0x%llx hooked\n", (unsigned long long)(Handle - Exe));
										// add rcx,0x3a8; lea rdx,[rbp+68]; mov r8,r13; call AddPlayer
										for (size_t a = 0; a < 0x700; a++)
										{
											auto A = (uint8_t*)Handle + a;
											if (A[0] == 0x48 && A[1] == 0x81 && A[2] == 0xC1 && A[7] == 0x48 && A[8] == 0x8D && A[9] == 0x55 && A[11] == 0x4D && A[12] == 0x8B && A[13] == 0xC5 && A[14] == 0xE8)
											{
												int32_t R4; memcpy(&R4, A + 15, 4);
												auto Add = (uintptr_t)(A + 19 + R4);
												SyndicateStwDump("AddPlayer", Add);
												Utils::Hook((uint64_t)Add, SyndicateLobbyAddPlayerHook, SyndicateLobbyAddPlayerOG);
												MH_EnableHook((LPVOID)Add);
												SyndicateStwLog("[Syndicate STW] beacon: AddPlayer at exe+0x%llx hooked\n", (unsigned long long)(Add - Exe));
												break;
											}
										}
										break;
									}
								}
							}
						}
						if (!Hooked) SyndicateStwLog("[Syndicate STW] beacon: lobby session check pattern not found\n");
					}
					else SyndicateStwLog("[Syndicate STW] beacon: ProcessLogin not found by text\n");
				}
				int R2 = SyndicateCallWithPtr(Register, Host, Lobby);
				SyndicateStwLog("[Syndicate STW] beacon: lobby host %s\n", R2 == 0 ? "registered" : "register crashed (caught)");
			}
		}
	}
	fflush(stdout);
}
