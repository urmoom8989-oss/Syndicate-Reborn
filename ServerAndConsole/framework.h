#pragma once

#define NOMINMAX
#define WIN32_LEAN_AND_MEAN // Exclude rarely-used stuff from Windows headers
// Windows Header Files
#include <windows.h>
#include <string>
#include <mutex>
#include <cmath>
#include <atomic>
#include <cctype>
#include <iostream>
#include <fstream>
#include <thread>
#include <chrono>
#include <vector>
#include <map>
#include <algorithm>
#include <numeric>
#include <intrin.h>
#include <sstream>
#include <array>
#include <tlhelp32.h>
#include <future>
#include <set>
#include <variant>
#include <unordered_set>

#include "minhook/MinHook.h"
#include "SDK/SDK.hpp"
#include "Globals.h"

#pragma comment(lib, "minhook/minhook.lib")

static_assert(sizeof(void *) == 8, "Syndicate 10.40 hooks require x64");
using namespace SDK;

static auto ImageBase = InSDKUtils::GetImageBase();

inline UFortBuildingItemDefinition *Floor = nullptr;
inline UFortBuildingItemDefinition *Roof = nullptr;
inline UFortBuildingItemDefinition *Stair = nullptr;
inline UFortBuildingItemDefinition *Wall = nullptr;
inline UFortEditToolItemDefinition *EditTool = nullptr;

static std::vector<UAthenaCharacterItemDefinition *> Characters{};
static std::vector<UAthenaPickaxeItemDefinition *> Pickaxes{};
static std::vector<UAthenaBackpackItemDefinition *> Backpacks{};
static std::vector<UAthenaGliderItemDefinition *> Gliders{};
static std::vector<UAthenaSkyDiveContrailItemDefinition *> Contrails{};
inline std::vector<UAthenaDanceItemDefinition *> Dances{};

static UNetDriver *(*CreateNetDriver)(UEngine *This, UWorld *InWorld,
                                      FName NetDriverDefinition) = decltype(CreateNetDriver)(ImageBase + 0x347FAF0);
static bool (*InitListen)(UNetDriver *This, UWorld *InWorld, FURL &ListenURL, bool bReuseAddressAndPort,
                          FString &Error) = decltype(InitListen)(ImageBase + 0x6F5F90);
static void (*SetWorld)(UNetDriver *This, UWorld *InWorld) = decltype(SetWorld)(ImageBase + 0x31EDF40);
static bool (*InitHost)(UObject *Beacon) = decltype(InitHost)(ImageBase + 0x6F5A30);
static void (*PauseBeaconRequests)(UObject *Beacon, bool bPause) = decltype(PauseBeaconRequests)(ImageBase + 0x17F03D0);

static void (*GiveAbilityOG)(UAbilitySystemComponent *, FGameplayAbilitySpecHandle *,
                             FGameplayAbilitySpec) = decltype(GiveAbilityOG)(ImageBase + 0x935010);
static void (*AbilitySpecConstructor)(FGameplayAbilitySpec *, UGameplayAbility *, int, int,
                                      UObject *) = decltype(AbilitySpecConstructor)(ImageBase + 0x958F90);
static bool (*InternalTryActivateAbility)(
    UAbilitySystemComponent *AbilitySystemComp, FGameplayAbilitySpecHandle AbilityToActivate,
    FPredictionKey InPredictionKey, UGameplayAbility **OutInstancedAbility, void *OnGameplayAbilityEndedDelegate,
    const FGameplayEventData *TriggerEventData) = decltype(InternalTryActivateAbility)(ImageBase + 0x9367F0);
static FGameplayAbilitySpecHandle (*GiveAbilityAndActivateOnce)(UAbilitySystemComponent *ASC,
                                                                FGameplayAbilitySpecHandle *, FGameplayAbilitySpec) =
    decltype(GiveAbilityAndActivateOnce)(ImageBase + 0x935130);

static FVector *(*PickSupplyDropLocationOG)(AFortAthenaMapInfo *MapInfo, FVector *outLocation, __int64 Center,
                                            float Radius) = decltype(PickSupplyDropLocationOG)(ImageBase + 0x1177CE0);

inline static ABuildingSMActor *(*ReplaceBuildingActor)(
    ABuildingSMActor *BuildingSMActor, unsigned int a2, UObject *a3, unsigned int a4, int a5, bool bMirrored,
    AFortPlayerControllerAthena *PC) = decltype(ReplaceBuildingActor)(ImageBase + 0x13D0DE0);
static __int64 (*CantBuild)(UWorld *, UObject *, FVector, FRotator, char, void *,
                            char *) = decltype(CantBuild)(ImageBase + 0x1601820);

static void *(*ApplyCharacterCustomization)(void *a1,
                                            void *a2) = decltype(ApplyCharacterCustomization)(ImageBase + 0x1A1F5F0);

static void (*BotManagerSetup)(__int64 BotManaager, __int64 Pawn, __int64 BehaviorTree, __int64 a4, DWORD *SkillLevel,
                               __int64 a7, __int64 StartupInventory, __int64 BotNameSettings, __int64 a10,
                               BYTE *CanRespawnOnDeath, unsigned __int8 BitFieldDataThing,
                               BYTE *CustomSquadId) = decltype(BotManagerSetup)(ImageBase + 0x12AA150);

static void (*RemoveFromAlivePlayers)(AFortGameModeAthena *, AFortPlayerControllerAthena *, APlayerState *,
                                      AFortPlayerPawn *, UFortWeaponItemDefinition *, uint8_t DeathCause,
                                      char) = decltype(RemoveFromAlivePlayers)(ImageBase + 0x11D95E0);
static void (*AddToAlivePlayers)(AFortGameModeAthena *GameMode,
                                 AFortPlayerControllerAthena *Player) = decltype(AddToAlivePlayers)(ImageBase +
                                                                                                    0x11B47F0);

static void *(*StaticFindObjectOG)(UClass *, UObject *Package, const wchar_t *OrigInName,
                                   bool ExactClass) = decltype(StaticFindObjectOG)(ImageBase + 0x22FB1E0);
static void *(*StaticLoadObjectOG)(UClass *Class, UObject *InOuter, const TCHAR *Name, const TCHAR *Filename,
                                   uint32_t LoadFlags, UObject *Sandbox, bool bAllowObjectReconciliation,
                                   void *) = decltype(StaticLoadObjectOG)(ImageBase + 0x22FC4C0);

static void (*OnRep_ZiplineState)(AFortPlayerPawn *a1) = decltype(OnRep_ZiplineState)(ImageBase + 0x1967D30);

AFortAthenaMutator_Bots *BotMutator = nullptr;
TArray<FVector> PickedSupplyDropLocations;
TArray<APlayerController *> GivenLootPlayers;

static TArray<AActor *> BuildingFoundations;
static TArray<AActor *> PlayerStarts;

// text manipulation utils
namespace TextManipUtils
{
// Found this from stack overflow :fire:
std::vector<std::string> SplitWhitespace(std::string const &input)
{
    std::istringstream buffer(input);
    std::vector<std::string> ret;

    std::copy(std::istream_iterator<std::string>(buffer), std::istream_iterator<std::string>(),
              std::back_inserter(ret));
    return ret;
}
} // namespace TextManipUtils

inline void Log(const std::string &msg)
{
    static std::mutex Mutex;
    std::lock_guard<std::mutex> Guard(Mutex);
    std::ofstream File("Syndicate_log.txt", std::ios::app);
    if (File)
        File << "[SYNDICATE]: " << msg << '\n';
    std::cout << "[SYNDICATE]: " << msg << std::endl;
}

inline bool HookFailure = false;
struct VTableChange
{
    void **Slot;
    void *Original;
};
inline std::vector<VTableChange> VTableChanges;
inline void RollBackVTables()
{
    for (auto I = VTableChanges.rbegin(); I != VTableChanges.rend(); ++I)
    {
        DWORD Protection = 0;
        if (VirtualProtect(I->Slot, sizeof(void *), PAGE_READWRITE, &Protection))
        {
            *I->Slot = I->Original;
            DWORD Ignored = 0;
            VirtualProtect(I->Slot, sizeof(void *), Protection, &Ignored);
        }
    }
    VTableChanges.clear();
}

inline MH_STATUS InstallHook(void *Target, void *Detour, void **Original)
{
    MEMORY_BASIC_INFORMATION Page{};
    if (!Target || !Detour || !VirtualQuery(Target, &Page, sizeof(Page)) || Page.State != MEM_COMMIT ||
        (Page.Protect & PAGE_GUARD) ||
        !(Page.Protect & (PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY)))
    {
        HookFailure = true;
        Log("Hook rejected: target is not executable memory");
        return MH_ERROR_NOT_EXECUTABLE;
    }
    auto Status = MH_CreateHook(Target, Detour, Original);
    if (Status != MH_OK)
    {
        HookFailure = true;
        Log("Hook installation failed: " + std::to_string(static_cast<int>(Status)));
    }
    return Status;
}
inline std::map<UObject *, std::pair<int, FName>> AssetIdentity;
inline void RegisterCachedAsset(UObject *P)
{
    if (P)
        AssetIdentity[P] = {P->Index, P->Name};
}
inline bool CachedAssetValid(UObject *P)
{
    auto I = AssetIdentity.find(P);
    return P && I != AssetIdentity.end() && UObject::GObjects->GetByIndex(I->second.first) == P &&
           P->Name == I->second.second;
}
inline void PruneCosmetics()
{
    auto Invalid = [](auto P) { return !CachedAssetValid(P); };
    std::erase_if(Characters, Invalid);
    std::erase_if(Backpacks, Invalid);
    std::erase_if(Pickaxes, Invalid);
    std::erase_if(Gliders, Invalid);
    std::erase_if(Contrails, Invalid);
    std::erase_if(Dances, Invalid);
}

template <typename T> void HookVTable(int Idx, void *Detour, void **OG = nullptr)
{
    auto Class = T::StaticClass();
    auto CDO = Class ? Class->DefaultObject : nullptr;
    if (!CDO || !CDO->VTable || !Detour || Idx < 0)
    {
        HookFailure = true;
        Log("VTable hook rejected: missing class or detour");
        return;
    }
    auto Table = reinterpret_cast<void **>(CDO->VTable);
    // Different reflected classes can share a native vtable. A repeated install
    // must not capture our own detour as the original and recurse indefinitely.
    if (Table[Idx] == Detour)
        return;
    DWORD Previous = 0;
    if (!VirtualProtect(&Table[Idx], sizeof(void *), PAGE_READWRITE, &Previous))
    {
        HookFailure = true;
        Log("VTable hook protection failed");
        return;
    }
    if (OG)
        *OG = Table[Idx];
    VTableChanges.push_back({&Table[Idx], Table[Idx]});
    Table[Idx] = Detour;
    DWORD Ignored = 0;
    VirtualProtect(&Table[Idx], sizeof(void *), Previous, &Ignored);
}

inline FQuat RotatorToQuat(FRotator Rotation)
{
    FQuat Quat;
    const float DEG_TO_RAD = 3.14159f / 180.0f;
    const float DIVIDE_BY_2 = DEG_TO_RAD / 2.0f;

    float SP = sin(Rotation.Pitch * DIVIDE_BY_2);
    float CP = cos(Rotation.Pitch * DIVIDE_BY_2);
    float SY = sin(Rotation.Yaw * DIVIDE_BY_2);
    float CY = cos(Rotation.Yaw * DIVIDE_BY_2);
    float SR = sin(Rotation.Roll * DIVIDE_BY_2);
    float CR = cos(Rotation.Roll * DIVIDE_BY_2);

    Quat.X = CR * SP * SY - SR * CP * CY;
    Quat.Y = -CR * SP * CY - SR * CP * SY;
    Quat.Z = CR * CP * SY - SR * SP * CY;
    Quat.W = CR * CP * CY + SR * SP * SY;

    return Quat;
}

template <typename T> static inline T *StaticFindObject(std::wstring ObjectName)
{
    return (T *)StaticFindObjectOG(T::StaticClass(), nullptr, ObjectName.c_str(), false);
}

template <typename T> inline T *Cast(UObject *Object)
{
    if (!Object || !Object->IsA(T::StaticClass()))
        return nullptr;
    return (T *)Object;
}

template <typename T = UObject> static inline T *StaticLoadObject(const std::string &Name)
{
    auto ConvName = std::wstring(Name.begin(), Name.end());

    T *Object = StaticFindObject<T>(ConvName);

    if (!Object)
    {
        Object =
            (T *)StaticLoadObjectOG(T::StaticClass(), nullptr, ConvName.c_str(), nullptr, 0, nullptr, false, nullptr);
    }

    return Object;
}

inline void ResolveBuildingAssets()
{
    Floor = StaticLoadObject<UFortBuildingItemDefinition>(
        "/Game/Items/Weapons/BuildingTools/BuildingItemData_Floor.BuildingItemData_Floor");
    if (!Floor)
        Floor = UObject::FindObject<UFortBuildingItemDefinition>(
            "FortBuildingItemDefinition BuildingItemData_Floor.BuildingItemData_Floor");
    Roof = StaticLoadObject<UFortBuildingItemDefinition>(
        "/Game/Items/Weapons/BuildingTools/BuildingItemData_RoofS.BuildingItemData_RoofS");
    if (!Roof)
        Roof = UObject::FindObject<UFortBuildingItemDefinition>(
            "FortBuildingItemDefinition BuildingItemData_RoofS.BuildingItemData_RoofS");
    Stair = StaticLoadObject<UFortBuildingItemDefinition>(
        "/Game/Items/Weapons/BuildingTools/BuildingItemData_Stair_W.BuildingItemData_Stair_W");
    if (!Stair)
        Stair = UObject::FindObject<UFortBuildingItemDefinition>(
            "FortBuildingItemDefinition BuildingItemData_Stair_W.BuildingItemData_Stair_W");
    Wall = StaticLoadObject<UFortBuildingItemDefinition>(
        "/Game/Items/Weapons/BuildingTools/BuildingItemData_Wall.BuildingItemData_Wall");
    if (!Wall)
        Wall = UObject::FindObject<UFortBuildingItemDefinition>(
            "FortBuildingItemDefinition BuildingItemData_Wall.BuildingItemData_Wall");
    EditTool = StaticLoadObject<UFortEditToolItemDefinition>("/Game/Items/Weapons/BuildingTools/EditTool.EditTool");
}
template <typename T> T *GetDefaultObject()
{
    auto C = T::StaticClass();
    return C ? Cast<T>(C->DefaultObject) : nullptr;
}

static inline FQuat FRotToQuat(FRotator Rotation)
{
    FQuat Quat;
    const float DEG_TO_RAD = 3.14159f / 180.0f;
    const float DIVIDE_BY_2 = DEG_TO_RAD / 2.0f;

    float SP = sin(Rotation.Pitch * DIVIDE_BY_2);
    float CP = cos(Rotation.Pitch * DIVIDE_BY_2);
    float SY = sin(Rotation.Yaw * DIVIDE_BY_2);
    float CY = cos(Rotation.Yaw * DIVIDE_BY_2);
    float SR = sin(Rotation.Roll * DIVIDE_BY_2);
    float CR = cos(Rotation.Roll * DIVIDE_BY_2);

    Quat.X = CR * SP * SY - SR * CP * CY;
    Quat.Y = -CR * SP * CY - SR * CP * SY;
    Quat.Z = CR * CP * SY - SR * SP * CY;
    Quat.W = CR * CP * CY + SR * SP * SY;

    return Quat;
}

template <typename T>
inline T *SpawnActor(
    FVector Loc, FRotator Rot = {}, AActor *Owner = nullptr, UClass *Class = T::StaticClass(),
    ESpawnActorCollisionHandlingMethod Handle = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn)
{
    if (!UWorld::GetWorld() || !Class || !std::isfinite(Loc.X) || !std::isfinite(Loc.Y) || !std::isfinite(Loc.Z))
        return nullptr;
    FTransform Transform{};
    Transform.Scale3D = FVector(1, 1, 1);
    Transform.Translation = Loc;
    Transform.Rotation = FRotToQuat(Rot);
    auto Actor =
        UGameplayStatics::BeginDeferredActorSpawnFromClass(UWorld::GetWorld(), Class, Transform, Handle, Owner);
    return Actor ? Cast<T>(UGameplayStatics::FinishSpawningActor(Actor, Actor->GetTransform())) : nullptr;
}

template <typename T> static inline T *SpawnAActor(FVector Loc = {}, FRotator Rot = {}, AActor *Owner = nullptr)
{
    return SpawnActor<T>(Loc, Rot, Owner);
}
template <typename T>
static inline T *SpawnActorClass(FVector Loc = {}, FRotator Rot = {}, UClass *Class = nullptr, AActor *Owner = nullptr)
{
    return SpawnActor<T>(Loc, Rot, Owner, Class);
}

template <typename T>
T *Actors(UClass *Class = T::StaticClass(), FVector Loc = {}, FRotator Rot = {}, AActor *Owner = nullptr)
{
    return SpawnActor<T>(Loc, Rot, Owner, Class);
}

inline AActor *DuplicateActor(AActor *Original)
{
    if (!Original || Original->IsActorBeingDestroyed())
        return nullptr;
    auto Copy = SpawnActor<AActor>(Original->K2_GetActorLocation(), Original->K2_GetActorRotation(),
                                   Original->GetOwner(), Original->Class);
    if (Copy)
    {
        for (auto Tag : Original->Tags)
        {
            bool HasTag = false;
            for (auto ExistingTag : Copy->Tags)
                if (ExistingTag == Tag)
                {
                    HasTag = true;
                    break;
                }
            if (!HasTag)
                Copy->Tags.Add(Tag);
        }
    }
    return Copy;
}

inline FVector FloorPickupOrigin(const FVector &Marker, bool *OccupiedWithoutFloor = nullptr)
{
    if (OccupiedWithoutFloor) *OccupiedWithoutFloor = false;
    if (!UWorld::GetWorld()) return Marker;
    FHitResult Hit{};
    TArray<AActor *> Ignored;
    auto Probe = Marker;
    bool Found = false;
    for (int Attempt = 0; Attempt < 8; ++Attempt)
    {
        Hit = {};
        Found = UKismetSystemLibrary::LineTraceSingleByProfile(UWorld::GetWorld(),
            Probe + FVector(0, 0, 100), Probe - FVector(0, 0, 200),
            UKismetStringLibrary::Conv_StringToName(L"Pawn"), false, Ignored,
            EDrawDebugTrace::None, &Hit, true, {}, {}, 0);
        auto Actor = Found ? Hit.Actor.Get() : nullptr;
        if (auto Pawn = Cast<APawn>(Actor))
        {
              if (OccupiedWithoutFloor) *OccupiedWithoutFloor = true;
            // Floor markers can be occupied during deferred warmup setup.
            // A capsule is not a floor. Choose a nearby initial pickup spot;
            // never move the pawn or relocate an existing pickup.
            auto At = Pawn->K2_GetActorLocation();
            if (!std::isfinite(At.X) || !std::isfinite(At.Y)) { Found = false; break; }
            float DX = Probe.X - At.X, DY = Probe.Y - At.Y;
            float Length = std::hypot(DX,DY);
            if (Length < 1.f) { DX = 1.f; DY = 0.f; Length = 1.f; }
            Probe.X = At.X + DX/Length * 180.f;
            Probe.Y = At.Y + DY/Length * 180.f;
            Ignored.Add(Pawn);
            Found = false;
            continue;
        }
        if (Cast<AFortPickup>(Actor))
        {
            Ignored.Add(Actor); Found = false; continue;
        }
        break;
    }
    Ignored.Free();
    if (!Found || Hit.bStartPenetrating || Hit.ImpactNormal.Z < 0.65f ||
        !std::isfinite(Hit.ImpactPoint.X) || !std::isfinite(Hit.ImpactPoint.Y) ||
        !std::isfinite(Hit.ImpactPoint.Z)) return Marker;
    // Pickup visuals rotate around their center, so placing that center at
    // floor height embeds half the mesh. Keep a small resting clearance;
    // leave container/player drops in their normal native toss simulation.
      if (OccupiedWithoutFloor) *OccupiedWithoutFloor = false;
      return Hit.ImpactPoint + FVector(0, 0, 25);
}
inline AFortPickup *SpawnPickup(UFortItemDefinition *ItemDef, int Count, int LoadedAmmo, FVector Loc,
                                EFortPickupSourceTypeFlag SourceType, EFortPickupSpawnSource Source,
                                AFortPawn *Pawn = nullptr)
{
    if (!ItemDef || Count <= 0 || !std::isfinite(Loc.X) || !std::isfinite(Loc.Y) || !std::isfinite(Loc.Z))
        return nullptr;
    bool FloorLoot = SourceType == EFortPickupSourceTypeFlag::FloorLoot;
      if (FloorLoot)
      {
          bool OccupiedWithoutFloor = false;
          Loc = FloorPickupOrigin(Loc, &OccupiedWithoutFloor);
          if (OccupiedWithoutFloor) return nullptr;
      }
    auto Pickup = SpawnActor<AFortPickup>(Loc);
    if (!Pickup)
        return nullptr;
    Pickup->bRandomRotation = true;
    Pickup->PawnWhoDroppedPickup = Pawn;
    auto &Entry = Pickup->PrimaryPickupItemEntry;
    Entry.ItemDefinition = ItemDef;
    Entry.Count = Count;
    Entry.LoadedAmmo = std::max(0, LoadedAmmo);
    ++Entry.ReplicationKey;
    Pickup->OnRep_PrimaryPickupItemEntry();
    Pickup->TossPickup(Loc, Pawn, -1, !FloorLoot, SourceType, Source);
    Pickup->SetReplicateMovement(true);
    // TossPickup creates/configures the native movement component. Do not replace it.
    if (SourceType == EFortPickupSourceTypeFlag::Container)
    {
        Pickup->bTossedFromContainer = true;
        Pickup->OnRep_TossedFromContainer();
    }
    return Pickup;
}

std::map<AFortPickup *, float> PickupLifetimes;
inline AFortPickup *SpawnStack(APlayerPawn_Athena_C *Pawn, UFortItemDefinition *Def, int Count, bool GiveAmmo = false,
                               int Ammo = 0)
{
    return Pawn ? SpawnPickup(Def, Count, GiveAmmo ? Ammo : 0, Pawn->K2_GetActorLocation(),
                              EFortPickupSourceTypeFlag::Other, EFortPickupSpawnSource::Unset, Pawn)
                : nullptr;
}

static AFortPickup *SpawnPickup(FFortItemEntry ItemEntry, FVector Location,
                                EFortPickupSourceTypeFlag PickupSource = EFortPickupSourceTypeFlag::Other,
                                EFortPickupSpawnSource SpawnSource = EFortPickupSpawnSource::Unset)
{
    auto Pickup = SpawnPickup(ItemEntry.ItemDefinition, ItemEntry.Count, ItemEntry.LoadedAmmo, Location, PickupSource,
                              SpawnSource);
    return Pickup;
}

inline void ShowFoundation(ABuildingFoundation *BuildingFoundation)
{
    if (!BuildingFoundation)
        return;

    BuildingFoundation->bServerStreamedInLevel = true;
    BuildingFoundation->DynamicFoundationType = EDynamicFoundationType::Static;
    BuildingFoundation->OnRep_ServerStreamedInLevel();

    BuildingFoundation->FoundationEnabledState = EDynamicFoundationEnabledState::Enabled;
    BuildingFoundation->DynamicFoundationRepData.EnabledState = EDynamicFoundationEnabledState::Enabled;
    BuildingFoundation->DynamicFoundationTransform = BuildingFoundation->GetTransform();
    BuildingFoundation->OnRep_DynamicFoundationRepData();
}

FVector PickSupplyDropLocation(SDK::AFortAthenaMapInfo *MapInfo, SDK::FVector Center, float Radius)
{
    if (!PickSupplyDropLocationOG)
        return SDK::FVector(0, 0, 0);

    const float MinDistance = 10000.0f;

    for (int i = 0; i < 20; i++)
    {
        SDK::FVector loc = FVector(0, 0, 0);
        PickSupplyDropLocationOG(MapInfo, &loc, (__int64)&Center, Radius);

        bool bTooClose = false;
        for (const auto &other : PickedSupplyDropLocations)
        {
            float dx = loc.X - other.X;
            float dy = loc.Y - other.Y;
            float dz = loc.Z - other.Z;

            float distSquared = dx * dx + dy * dy + dz * dz;

            if (distSquared < MinDistance * MinDistance)
            {
                bTooClose = true;
                break;
            }
        }

        if (!bTooClose)
        {
            PickedSupplyDropLocations.Add(loc);
            return loc;
        }
    }

    return SDK::FVector(0, 0, 0);
}

template <typename T> inline std::vector<T *> GetAllObjectsOfClass(UClass *Class = T::StaticClass())
{
    std::vector<T *> Objects{};

    for (int i = 0; i < UObject::GObjects->Num(); ++i)
    {
        UObject *Object = UObject::GObjects->GetByIndex(i);

        if (!Object)
            continue;

        if (!Object->IsA(Class) || Object->IsDefaultObject())
            continue;
        if (Object->GetName().contains("Test"))
            continue;

        if (Object->IsA(Class) && !Object->IsDefaultObject())
        {
            Objects.push_back((T *)Object);
        }
    }

    return Objects;
}

int CountActorsWithName(FName TargetName, UClass *Class)
{
    TArray<AActor *> FoundActors;
    auto *Statics = (UGameplayStatics *)UGameplayStatics::StaticClass()->DefaultObject;
    Statics->GetAllActorsOfClass(UWorld::GetWorld(), Class, &FoundActors);

    int Count = 0;
    for (AActor *Actor : FoundActors)
    {
        if (Actor && Actor->GetName() == TargetName.ToString())
            Count++;
    }
    FoundActors.Free();
    return Count;
}

AFortPlayerControllerAthena *GetPCFromId(FUniqueNetIdRepl &ID)
{
    if (!UWorld::GetWorld() || !UWorld::GetWorld()->GameState)
        return nullptr;
    for (auto &PlayerState : UWorld::GetWorld()->GameState->PlayerArray)
    {
        auto PlayerStateAthena = Cast<AFortPlayerStateAthena>(PlayerState);
        if (!PlayerStateAthena)
            continue;
        if (PlayerStateAthena->AreUniqueIDsIdentical(ID, PlayerState->UniqueId))
            return Cast<AFortPlayerControllerAthena>(PlayerState->Owner);
    }

    return nullptr;
}

static inline void *_NpFH = nullptr;
template <typename _Ot = void *> __forceinline static void ExecHook(UFunction *_Fn, void *_Detour, _Ot &_Orig = _NpFH)
{
    if (!_Fn)
        return;
    if (!std::is_same_v<_Ot, void *>)
        _Orig = (_Ot)_Fn->ExecFunction;

    _Fn->ExecFunction = reinterpret_cast<UFunction::FNativeFuncPtr>(_Detour);
}

template <typename _Ot = void *>
__forceinline static void ExecHook(const char *_Name, void *_Detour, _Ot &_Orig = _NpFH)
{
    UFunction *_Fn = StaticLoadObject<UFunction>(_Name);
    ExecHook(_Fn, _Detour, _Orig);
}

enum EFortGamePlaylistAthena
{
    SOLO,
    DUOS,
    SQUADS
};

/** Types of net modes that we know about - synced with EngineBaseTypes.h */
enum class ENetMode
{
    Standalone,
    DedicatedServer,
    ListenServer,
    Client,

    MAX,
};

int IsTrue()
{
    return TRUE;
}

int IsFalse()
{
    return FALSE;
}

void nullFunc() {}

// Quaternion helpers above are shared by real actor spawns. Vehicle movement stays native.
