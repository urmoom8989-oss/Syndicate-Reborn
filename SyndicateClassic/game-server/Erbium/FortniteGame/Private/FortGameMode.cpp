#include "pch.h"
bool SyndicateHumanTeamWantsFill(int Team); // NetDriver.cpp (SpectraBots): every real player on Team chose Fill
bool SyndicateServerReady = false; // the island has finished loading (set where PROJECT_TILTED_SERVER_READY is printed)
bool SyndicateListening = false; // also set by the map-load Listen hook (Misc.cpp, Seasons 6+)
#include "../Public/FortGameMode.h"
#include "../Public/LevelStreamingDynamic.h"
#include "../../Erbium/Public/Finders.h"
#include "../../Engine/Public/NetDriver.h"
#include "../../Engine/Public/AbilitySystemComponent.h"
#include "../Public/FortPlayerControllerAthena.h"
#include "../Public/FortKismetLibrary.h"
#include "../../Engine/Public/CurveTable.h"
#include "../Public/FortSafeZoneIndicator.h"
#include "../../Engine/Public/DataTableFunctionLibrary.h"
#include "../../Erbium/Public/Configuration.h"
#include "../Public/FortLootPackage.h"
#include "../Public/BuildingFoundation.h"
#include "../Public/BuildingContainer.h"
#include "../../Erbium/Public/LateGame.h"
#include "../Public/BuildingItemCollectorActor.h"
#include "../../Erbium/Public/GUI.h"
#include <random>
#include <set>
#include <map>
#include <unordered_map>
#include <thread>
#include "../../Erbium/Public/Misc.h"
#include "../../Erbium/Public/Events.h"
#include "../Public/BattleRoyaleGamePhaseLogic.h"
#include "../Public/FortAthenaCreativePortal.h"
#include "../Public/SyndicateQuickbarTrace.h"
#include "../Public/FortPhysicsPawn.h"
#include <objbase.h>

// Native 12.50 henchman test heartbeat. It proves the manager-spawned bot exists in the
// world, moves, and takes damage, even when the player is not standing next to it.
AFortPlayerPawnAthena* SyndicateNativeHenchmanPawn = nullptr;
static UObject* SyndicateNativeHenchmanBotData = nullptr;
static FVector SyndicateNativeHenchmanLastLoc{};
static ULONGLONG SyndicateNativeHenchmanNextLog = 0;
static int SyndicateNativeHenchmanSamples = 0;
static bool SyndicateNativeHenchmanPatrolDone = false;
static AActor* SyndicateNativeHenchmanPatrolPath = nullptr;

struct FSyndicateRuntimeBotData
{
    FName TagName;
    float CullDistanceSquared;
    uint8 bCheckForOverlaps;
    uint8 Flags;
    uint8 CustomSquadId;
    uint8 Pad;
};

struct SyndicateDropItem
{
    UFortItemDefinition* Def = nullptr;
    int Count = 1;
    int Ammo = 0;
};

struct SyndicateHenchmanRecord
{
    AFortPlayerPawnAthena* Pawn = nullptr;
    UObject* BotData = nullptr;
    AActor* PatrolPath = nullptr;
    UFortItemDefinition* WeaponDef = nullptr;
    std::vector<SyndicateDropItem> InventoryDrops;
    FVector LastLoc{};
    ULONGLONG NextLog = 0;
    int Samples = 0;
    int PatrolIndex = 0;
    bool Engaged = false;
    int BurstLeft = 0;
    int BurstSize = 7;
    ULONGLONG NextShot = 0;
    ULONGLONG PauseUntil = 0;
    ULONGLONG SuspiciousUntil = 0;
    ULONGLONG AimReady = 0;
    ULONGLONG NextFire = 0;
    ULONGLONG NextStopFire = 0;
    bool PatrolDone = false;
    bool MoveProbed = false;
    FVector DriveLastLoc{};
    int StuckTicks = 0;
    float LastHealth = -1.f;
    bool WasKnocked = false;
    bool WitnessAlerted = false;   // alerted by seeing an ally downed/killed: its own death must not chain
    ULONGLONG ForcedAlertUntil = 0;
    ULONGLONG NoLosSince = 0;
    ULONGLONG LoseAggroUntil = 0;
    ULONGLONG CloseSince = 0;
    TWeakObjectPtr<AFortPlayerPawnAthena> ForcedTarget;
    ULONGLONG ForcedTargetUntil = 0;
    std::string BossKey;
};
static UFortItemDefinition* SyndicateLastHenchmanWeapon = nullptr;
static int SyndicateNativeSpawnerCount = 0;
static bool SyndicateCattyValid = false;
static FVector SyndicateCattyLoc{};
static bool SyndicateCattyMeowsclesSpawned = false;
static std::vector<SyndicateHenchmanRecord> SyndicateHenchmen;
// Human pawns refreshed a few times per second so each henchman does not rescan every
// player controller (99 SpectraBots) on every network tick.
static std::vector<AFortPlayerPawnAthena*> SyndicateHumanPawns;

// Pick a ranged weapon from the bot data's startup inventory, then fall back to the common
// Chapter 2 assault rifle. The Deadpool boss data has no usable starter weapon of its own.
static UFortItemDefinition* SyndicateHenchmanWeaponDef(UObject* BotData)
{
    if (BotData)
    {
        if (auto Off = BotData->GetOffset("StartupInventory"); Off != uint32(-1))
        {
            auto Startup = GetFromOffset<UObject*>(BotData, Off);
            if (Startup)
            {
                if (auto ItemsOff = Startup->GetOffset("Items"); ItemsOff != uint32(-1))
                {
                    auto& Items = GetFromOffset<TArray<UFortItemDefinition*>>(Startup, ItemsOff);
                    for (int I = 0; I < Items.Num(); ++I)
                        if (Items[I] && Items[I]->IsA(UFortWeaponRangedItemDefinition::StaticClass()))
                            return Items[I];
                }
            }
        }
    }
    for (const char* Candidate : {
        "WID_Assault_Auto_Athena_C_Ore_T02",
        "WID_Assault_Auto_Athena_UC_Ore_T03",
        "WID_Shotgun_Standard_Athena_UC_Ore_T03",
        "WID_Pistol_Standard_Athena_UC_Ore_T03" })
        if (auto Loaded = TUObjectArray::FindObject<UFortWeaponItemDefinition>(Candidate))
            return const_cast<UFortItemDefinition*>((const UFortItemDefinition*)Loaded);
    return (UFortItemDefinition*)StaticLoadObject(
        L"/Game/Athena/Items/Weapons/Assault/WID_Assault_Auto_Athena_C_Ore_T02.WID_Assault_Auto_Athena_C_Ore_T02",
        UFortWeaponItemDefinition::StaticClass());
}

// The native manager normally creates the bot's AFortInventory through SetUpInventoryBot.
// That step does not run for a manager-spawned test bot, so create it the same way OGS does.
static AFortInventory* SyndicateEnsureHenchmanInventory(AActor* PC)
{
    if (!PC) return nullptr;
    auto Off = PC->GetOffset("Inventory");
    if (Off == uint32(-1)) return nullptr;
    auto Inv = GetFromOffset<AFortInventory*>(PC, Off);
    if (Inv) return Inv;

    auto InvClass = FindClass("FortInventory");
    if (!InvClass) return nullptr;
    Inv = (AFortInventory*)UWorld::SpawnActor(InvClass, FVector{}, FRotator{}, PC);
    if (!Inv) return nullptr;
    Inv->InventoryType = 0;
    GetFromOffset<AFortInventory*>(PC, Off) = Inv;
    Inv->ForceNetUpdate();
    printf("[Syndicate][AITest] created inventory=%s\n", Inv->Name.ToString().c_str());
    return Inv;
}

// Grant the rest of the BotData startup inventory. The boss data carries the Agency keycard
// alongside its Midas weapon; the native manager never added it on our manual spawn path.
static void SyndicateGiveStartupExtras(AFortInventory* Inv, UObject* BotData, UFortItemDefinition* Equipped)
{
    if (!Inv || !BotData) return;
    auto Off = BotData->GetOffset("StartupInventory");
    if (Off == uint32(-1)) return;
    auto Startup = GetFromOffset<UObject*>(BotData, Off);
    if (!Startup) return;
    auto ItemsOff = Startup->GetOffset("Items");
    if (ItemsOff == uint32(-1)) return;
    auto& Items = GetFromOffset<TArray<UFortItemDefinition*>>(Startup, ItemsOff);
    printf("[Syndicate][AITest] startup extras=%d\n", Items.Num());
    for (int I = 0; I < Items.Num(); ++I)
    {
        auto Def = Items[I];
        if (!Def || Def == Equipped) continue;
        printf("[Syndicate][AITest] startup extra %s\n", Def->Name.ToString().c_str());
        int Ammo = Def->IsA(UFortWeaponRangedItemDefinition::StaticClass()) ? 30 : 0;
        Inv->GiveItem(Def, 1, Ammo, 0, false, true);
    }
}

// Last-resort gun for bot data with no usable ranged item (or a failed equip).
static void SyndicateForceGun(AActor* PC, AFortPlayerPawnAthena* Pawn)
{
    if (!PC || !Pawn || Pawn->CurrentWeapon) return;
    auto Def = TUObjectArray::FindObject<UFortWeaponItemDefinition>("WID_Assault_Auto_Athena_C_Ore_T02");
    if (!Def)
        Def = (UFortWeaponItemDefinition*)StaticLoadObject(
            L"/Game/Athena/Items/Weapons/Assault/WID_Assault_Auto_Athena_C_Ore_T02.WID_Assault_Auto_Athena_C_Ore_T02",
            UFortWeaponItemDefinition::StaticClass());
    if (!Def) { printf("[Syndicate][AITest] force gun missing\n"); return; }
    auto Inv = SyndicateEnsureHenchmanInventory(PC);
    if (!Inv) return;
    auto Item = Inv->GiveItem((UFortItemDefinition*)Def, 1, 30, 0, false, true);
    FGuid Guid = Item ? Item->ItemEntry.ItemGuid : FGuid();
    if (Guid.A == 0 && Guid.B == 0 && Guid.C == 0 && Guid.D == 0)
        CoCreateGuid((GUID*)&Guid);
    Pawn->EquipWeaponDefinition(Def, Guid, FGuid(), false);
    printf("[Syndicate][AITest] force gun equipped=%d\n", Pawn->CurrentWeapon != nullptr);
}

// Give the bot a real weapon in its inventory and equip it. Without this the native attack
// branch runs with a null CurrentWeapon and dereferences it when the bot sees a player.
// Returns true once a ranged weapon is actually equipped. The controller's inventory is
// created late, so the heartbeat retries this until it succeeds.
static bool SyndicateGiveHenchmanWeapon(AActor* PC, AFortPlayerPawnAthena* Pawn, UObject* BotData)
{
    if (!PC || !Pawn) return false;
    auto Inv = SyndicateEnsureHenchmanInventory(PC);
    if (!Inv)
    {
        printf("[Syndicate][AITest] hench weapon: inventory=null\n");
        return false;
    }

    // If the native setup already added a ranged weapon, equip that entry instead of adding
    // a duplicate.
    UFortItemDefinition* Def = nullptr;
    FGuid Guid{};
    for (int I = 0; I < Inv->Inventory.ReplicatedEntries.Num(); ++I)
    {
        auto& Entry = Inv->Inventory.ReplicatedEntries.Get(I, FFortItemEntry::Size());
        if (Entry.ItemDefinition && Entry.ItemDefinition->IsA(UFortWeaponRangedItemDefinition::StaticClass()))
        {
            Def = const_cast<UFortItemDefinition*>(Entry.ItemDefinition);
            Guid = Entry.ItemGuid;
            break;
        }
    }

    if (!Def)
    {
        Def = SyndicateHenchmanWeaponDef(BotData);
        if (!Def)
        {
            printf("[Syndicate][AITest] hench weapon: definition=null\n");
            return false;
        }
        auto Item = Inv->GiveItem(Def, 1, 30, 0, false, true);
        Guid = Item ? Item->ItemEntry.ItemGuid : FGuid();
        if (Guid.A == 0 && Guid.B == 0 && Guid.C == 0 && Guid.D == 0)
            CoCreateGuid((GUID*)&Guid);

        if (auto Ranged = (UFortWeaponItemDefinition*)Def)
            if (auto Ammo = Ranged->GetAmmoWorldItemDefinition_BP())
                Inv->GiveItem(Ammo, 999, 0, 0, false, true);

        // Keep a harvest tool in the inventory so the behavior tree can switch to it without
        // finding an empty slot.
        if (auto Pick = TUObjectArray::FindObject<UFortWeaponMeleeItemDefinition>("WID_Harvest_Pickaxe_Athena_C_T01"))
            Inv->GiveItem(const_cast<UFortItemDefinition*>((const UFortItemDefinition*)Pick), 1, 0, 0, false, true);
    }

    Pawn->EquipWeaponDefinition((UFortWeaponItemDefinition*)Def, Guid, FGuid(), false);
    SyndicateLastHenchmanWeapon = Def;
    SyndicateGiveStartupExtras(Inv, BotData, Def);
    bool Equipped = Pawn->CurrentWeapon != nullptr;
    printf("[Syndicate][AITest] hench weapon=%s equipped=%d guid=%08x\n",
        Def->Name.ToString().c_str(), (int)Equipped, Guid.A);
    return Equipped;
}

// Rebuild the Deadpool nameplate as a normal henchman name. PlayerNamePrivate is what
// GetPlayerName() returns (kill feed / death report); PlayerNameCustomOverride is what the
// in-world nameplate reads.
static void SyndicateSetHenchmanName(AFortPlayerStateAthena* State, AActor* PC)
{
    if (!State) return;
    // Keep the native MANG names (Ghost Henchman / Shadow Henchman / Midas). The only name
    // that must be replaced is Deadpool's old HDP boss name.
    std::string Current;
    if (auto Fn = State->GetFunction("GetPlayerName"))
        Current = std::string(State->Call<FString>(Fn).ToString().c_str());
    std::string Low;
    for (char C : Current) Low += (char)tolower((unsigned char)C);
    if (Low.find("deadpool") == std::string::npos) return;

    FString Name(L"Henchman");
    if (auto Off = State->GetOffset("PlayerNamePrivate"); Off != uint32(-1))
        GetFromOffset<FString>(State, Off) = Name;
    if (auto Off = State->GetOffset("PlayerNameCustomOverride"); Off != uint32(-1))
        GetFromOffset<FText>(State, Off) = UKismetTextLibrary::Conv_StringToText(Name);
    if (auto Fn = State->GetFunction("OnRep_PlayerName")) State->ProcessEvent(Fn, nullptr);
    State->ForceNetUpdate();
    if (PC)
    {
        if (auto Off = PC->GetOffset("BotPlayerName"); Off != uint32(-1))
            GetFromOffset<FString>(PC, Off) = Name;
        if (auto Off = PC->GetOffset("BotPlayerNameWithSkillRating"); Off != uint32(-1))
            GetFromOffset<FString>(PC, Off) = Name;
        if (auto Off = PC->GetOffset("BotIDSuffix"); Off != uint32(-1))
            GetFromOffset<FString>(PC, Off) = FString(L"");
    }
    printf("[Syndicate][AITest] hench name=Henchman (was Deadpool)\n");
}

// OGS spawns every MANG bot at the first point of a level-authored FortAthenaPatrolPath.
// Pick the path closest to the requested POI and return that exact spawn point.
static AActor* SyndicateFindHenchmanPatrolPath(const FVector& Anchor, FVector& OutSpawn, double MaxDist = 1.0e18)
{
    auto PathClass = FindClass("FortAthenaPatrolPath");
    if (!PathClass) return nullptr;

    TArray<AActor*> Paths;
    Utils::GetAll(PathClass, Paths);
    AActor* Best = nullptr;
    double BestDist = MaxDist * MaxDist;
    for (int I = 0; I < Paths.Num(); ++I)
    {
        auto A = Paths[I]; if (!A) continue;
        auto PointsOff = A->GetOffset("PatrolPoints");
        if (PointsOff == uint32(-1)) continue;
        auto& Pts = GetFromOffset<TArray<AActor*>>(A, PointsOff);
        if (Pts.Num() == 0 || !Pts[0]) continue;
        auto L = Pts[0]->K2_GetActorLocation();
        double DX = L.X - Anchor.X, DY = L.Y - Anchor.Y;
        double D = DX * DX + DY * DY;
        if (D < BestDist) { BestDist = D; Best = A; OutSpawn = L; }
    }
    printf("[Syndicate][AITest] spawnpath chosen=%s loc=%.0f,%.0f,%.0f dist=%.0f paths=%d\n",
        Best ? Best->Name.ToString().c_str() : "none",
        OutSpawn.X, OutSpawn.Y, OutSpawn.Z, sqrt(BestDist), Paths.Num());
    Paths.Free();
    return Best;
}

// The custom server never creates a navigation system, so native AI has no navmesh to path
// on. Create AthenaNavSystem, register every NavMeshBoundsVolume with it, and return its main
// nav data. Called once before the henchman spawn and again when the patrol is attached.
static UObject* SyndicateEnsureNavigation()
{
    static UObject* CachedNav = nullptr;
    if (CachedNav) return CachedNav;

    auto World = UWorld::GetWorld();
    if (!World) return nullptr;
    auto NavSys = (UObject*)World->NavigationSystem;
    if (!NavSys)
    {
        auto NavClass = FindClass("AthenaNavSystem");
        if (!NavClass) NavClass = FindClass("FortNavSystem");
        if (!NavClass) NavClass = FindClass("NavigationSystem");
        if (NavClass)
        {
            NavSys = UGameplayStatics::SpawnObject(NavClass, World);
            if (NavSys) World->NavigationSystem = NavSys;
        }
    }
    if (!NavSys)
    {
        printf("[Syndicate][AITest] navsys=none\n");
        return nullptr;
    }

    auto SetBit = [&](const char* Name) {
        auto Prop = NavSys->GetProperty(Name);
        auto Off = NavSys->GetOffset(Name);
        if (!Prop || Off == uint32(-1)) return;
        auto& Bits = GetFromOffset<uint8>(NavSys, Off);
        Bits |= Prop->GetFieldMask();
    };
    SetBit("bAutoCreateNavigationData");
    SetBit("bSupportRebuilding");

    TArray<AActor*> Bounds;
    if (auto BoundsClass = FindClass("NavMeshBoundsVolume"))
        Utils::GetAll(BoundsClass, Bounds);
    printf("[Syndicate][AITest] navsys=%s class=%s bounds=%d\n",
        NavSys->Name.ToString().c_str(), NavSys->Class ? NavSys->Class->Name.ToString().c_str() : "?",
        Bounds.Num());
    if (auto Fn = NavSys->GetFunction("OnNavigationBoundsUpdated"))
        for (int I = 0; I < Bounds.Num(); ++I)
            if (Bounds[I]) NavSys->Call<void>(Fn, Bounds[I]);
    Bounds.Free();

    // OnNavigationBoundsUpdated built a small "Deimos" navmesh, which does not cover the
    // Apollo POIs. Prefer an existing Athena/Apollo navmesh actor for the AI path following.
    UObject* BestNav = nullptr;
    if (auto MeshClass = FindClass("AthenaNavMesh"))
    {
        TArray<AActor*> Meshes;
        Utils::GetAll(MeshClass, Meshes);
        printf("[Syndicate][AITest] navmeshes=%d\n", Meshes.Num());
        for (int I = 0; I < Meshes.Num(); ++I)
        {
            auto A = Meshes[I]; if (!A) continue;
            std::string N = std::string(A->Name.ToString().c_str());
            auto L = A->K2_GetActorLocation();
            printf("[Syndicate][AITest] navmesh %s loc=%.0f,%.0f,%.0f\n", N.c_str(), L.X, L.Y, L.Z);
            if (N.find("Deimos") != std::string::npos) continue;
            if (!BestNav && (N.find("Apollo") != std::string::npos || N.find("Athena") != std::string::npos))
                BestNav = A;
        }
        Meshes.Free();
    }

    UObject* CurrentNav = nullptr;
    if (auto Off = NavSys->GetOffset("MainNavData"); Off != uint32(-1))
        CurrentNav = GetFromOffset<UObject*>(NavSys, Off);
    if (BestNav && BestNav != CurrentNav)
    {
        if (auto Off = NavSys->GetOffset("MainNavData"); Off != uint32(-1))
            GetFromOffset<UObject*>(NavSys, Off) = BestNav;
        CurrentNav = BestNav;
        printf("[Syndicate][AITest] nav switched to %s\n", BestNav->Name.ToString().c_str());
    }
    CachedNav = CurrentNav;
    printf("[Syndicate][AITest] nav main=%s\n", CachedNav ? CachedNav->Name.ToString().c_str() : "null");
    return CachedNav;
}

// Create the native patrol component and route the manager did not give this bot. OGS does
// the same in BotManagerSetup; without a patrol path the MANG behavior tree idles forever.
static void SyndicateAttachHenchmanPatrol(AFortPlayerPawnAthena* Pawn, AActor* PrechosenPath, bool& Done)
{
    if (!Pawn || Done) return;
    auto PC = (AActor*)Pawn->Controller;
    if (!PC) return;

    auto PathClass = FindClass("FortAthenaPatrolPath");
    auto PointClass = FindClass("FortAthenaPatrolPoint");
    if (!PathClass || !PointClass) { printf("[Syndicate][AITest] patrol classes missing\n"); return; }

    FVector Base = Pawn->K2_GetActorLocation();
    AActor* Path = PrechosenPath;
    if (Path)
        printf("[Syndicate][AITest] patrol prechosen=%s\n", Path->Name.ToString().c_str());
    else
    {
        // Fallback: reuse a level-authored path only when its first point is on this bot's
        // floor. Picking a path elsewhere leaves the patrol evaluator with an unreachable
        // destination.
        TArray<AActor*> Paths;
        Utils::GetAll(PathClass, Paths);
        double BestDist = 500.0 * 500.0;
        for (int I = 0; I < Paths.Num(); ++I)
        {
            auto A = Paths[I]; if (!A) continue;
            auto PointsOff = A->GetOffset("PatrolPoints");
            if (PointsOff == uint32(-1)) continue;
            auto& Pts = GetFromOffset<TArray<AActor*>>(A, PointsOff);
            if (Pts.Num() == 0) continue;
            auto L = Pts[0] ? Pts[0]->K2_GetActorLocation() : A->K2_GetActorLocation();
            double D = (L.X - Base.X) * (L.X - Base.X) + (L.Y - Base.Y) * (L.Y - Base.Y);
            if (D < BestDist) { BestDist = D; Path = A; }
        }
        printf("[Syndicate][AITest] patrol paths=%d chosen=%s\n", Paths.Num(),
            Path ? Path->Name.ToString().c_str() : "none");
        Paths.Free();
    }

    if (!Path)
    {
        Path = (AActor*)UWorld::SpawnActor(PathClass, FVector(Base.X, Base.Y, Base.Z));
        if (!Path) { printf("[Syndicate][AITest] patrol spawn failed\n"); return; }
        auto PointsOff = Path->GetOffset("PatrolPoints");
        if (PointsOff == uint32(-1)) PointsOff = 0x228;
        auto& Points = GetFromOffset<TArray<AActor*>>(Path, PointsOff);
        const double R = 320.0;
        FVector Offsets[4] = {
            FVector(Base.X + R, Base.Y, Base.Z),
            FVector(Base.X, Base.Y + R, Base.Z),
            FVector(Base.X - R, Base.Y, Base.Z),
            FVector(Base.X, Base.Y - R, Base.Z)
        };
        for (int I = 0; I < 4; ++I)
        {
            auto Pt = (AActor*)UWorld::SpawnActor(PointClass, Offsets[I]);
            if (Pt) Points.Add(Pt);
        }
        printf("[Syndicate][AITest] patrol spawned path=%s points=%d\n",
            Path->Name.ToString().c_str(), Points.Num());
    }

    // Reuse the controller's existing patrolling component. Replacing a registered native
    // component with a freshly spawned one leaves it unticked, which is why manual bots
    // stood still while the adopted native bots walked their patrols.
    UObject* Comp = nullptr;
    if (auto Off = PC->GetOffset("CachedPatrollingComponent"); Off != uint32(-1))
        Comp = GetFromOffset<UObject*>(PC, Off);
    if (!Comp)
    {
        auto CompClass = FindClass("FortAthenaNpcPatrollingComponent");
        Comp = CompClass ? UGameplayStatics::SpawnObject(CompClass, PC) : nullptr;
        if (!Comp) { printf("[Syndicate][AITest] patrol component missing\n"); return; }
        if (auto Off = PC->GetOffset("CachedPatrollingComponent"); Off != uint32(-1))
            GetFromOffset<UObject*>(PC, Off) = Comp;
    }
    if (auto Fn = Comp->GetFunction("Activate")) Comp->Call<void>(Fn, true);
    if (auto Off = Comp->GetOffset("CachedBotController"); Off != uint32(-1))
        GetFromOffset<AActor*>(Comp, Off) = PC;
    if (auto Off = Comp->GetOffset("PatrolPath"); Off != uint32(-1))
        GetFromOffset<UObject*>(Comp, Off) = Path;
    if (auto Fn = Comp->GetFunction("SetPatrolPath")) Comp->Call<void>(Fn, Path);

    // The controller was spawned outside the normal spawner, so its path-following component
    // has no navigation data. Bind the world nav data and activate it (OGS does the same).
    if (auto PfOff = PC->GetOffset("PathFollowingComponent"); PfOff != uint32(-1))
    {
        auto Pf = GetFromOffset<UObject*>(PC, PfOff);
        UObject* MainNav = SyndicateEnsureNavigation();
        if (Pf && MainNav)
        {
            if (auto Off = Pf->GetOffset("MyNavData"); Off != uint32(-1))
                GetFromOffset<UObject*>(Pf, Off) = MainNav;
            if (auto Fn = Pf->GetFunction("OnNavDataRegistered")) Pf->Call<void>(Fn, MainNav);
        }
        if (Pf)
        {
            if (auto Fn = Pf->GetFunction("Activate")) Pf->Call<void>(Fn, true);
            if (auto Fn = Pf->GetFunction("SetActive")) Pf->Call<void>(Fn, true, true);
            if (auto Fn = Pf->GetFunction("OnRep_IsActive")) Pf->ProcessEvent(Fn, nullptr);
        }
        printf("[Syndicate][AITest] nav pf=%s nav=%s\n",
            Pf ? Pf->Name.ToString().c_str() : "null",
            MainNav ? MainNav->Name.ToString().c_str() : "null");
    }

    // Let the patrol evaluator run even though this bot was not spawned by a spawner.
    if (auto Offset = PC->GetOffset("Blackboard"); Offset != uint32(-1))
    {
        auto BB = GetFromOffset<UObject*>(PC, Offset);
        if (BB)
        {
            if (auto SetEnum = BB->GetFunction("SetValueAsEnum"))
                BB->Call<void>(SetEnum, FName(L"AIEvaluator_Patrolling_ExecutionStatus"), (uint8)4);
            if (auto SetBool = BB->GetFunction("SetValueAsBool"))
                BB->Call<void>(SetBool, FName(L"AIEvaluator_Patrolling_ShouldMove"), true);
        }
    }

    Done = true;
    printf("[Syndicate][AITest] patrol attached comp=%s path=%s\n",
        Comp->Name.ToString().c_str(), Path->Name.ToString().c_str());
}

// The native manager created a blackboard, but the client log shows the leash evaluator cannot
// find its keys. Re-bind BB_MANG2 explicitly, log every key, and seed the leash/object values
// the evaluator needs.
// Explicitly run BT_MANG2. The native manager sets the BehaviorTree pointer but our spawn
// path never called RunBehaviorTree, so AIEvaluators stayed at 0 and the bot only idled.
static void SyndicateStartHenchmanTree(AActor* PC)
{
    if (!PC) return;
    auto TreeClass = FindClass("BehaviorTree");
    auto Tree = TreeClass ? (UObject*)StaticLoadObject(
        L"/Game/Athena/AI/MANG/BehaviorTree/BT_MANG2.BT_MANG2", TreeClass) : nullptr;
    if (!Tree)
    {
        printf("[Syndicate][AITest] runtree tree=null\n");
        return;
    }
    UObject* BBAsset = nullptr;
    if (auto Off = Tree->GetOffset("BlackboardAsset"); Off != uint32(-1))
        BBAsset = GetFromOffset<UObject*>(Tree, Off);
    printf("[Syndicate][AITest] runtree tree=%s bb=%s\n",
        Tree->Name.ToString().c_str(), BBAsset ? BBAsset->Name.ToString().c_str() : "null");

    bool Ok = false;
    if (auto Fn = PC->GetFunction("RunBehaviorTree"))
    {
        Ok = PC->Call<bool>(Fn, Tree);
        printf("[Syndicate][AITest] runtree=%d\n", (int)Ok);
    }

    // OGS fallback: RunBehaviorTree returns false in this build, but setting the brain's
    // default asset and starting logic runs the tree and creates the AI evaluators.
    if (!Ok)
        if (auto Off = PC->GetOffset("BrainComponent"); Off != uint32(-1))
        {
            auto Brain = GetFromOffset<UObject*>(PC, Off);
            if (Brain)
            {
                if (auto AOff = Brain->GetOffset("DefaultBehaviorTreeAsset"); AOff != uint32(-1))
                    GetFromOffset<UObject*>(Brain, AOff) = Tree;
                if (auto Fn = Brain->GetFunction("StartLogic"))
                {
                    Brain->ProcessEvent(Fn, nullptr);
                    printf("[Syndicate][AITest] startlogic=ok\n");
                }
                if (auto Fn = Brain->GetFunction("RestartLogic"))
                {
                    Brain->ProcessEvent(Fn, nullptr);
                    printf("[Syndicate][AITest] restartlogic=ok\n");
                }
            }
        }

    if (auto Fn = PC->GetFunction("BlueprintOnBehaviorTreeStarted")) PC->ProcessEvent(Fn, nullptr);
    if (auto Fn = PC->GetFunction("OnRep_Pawn")) PC->ProcessEvent(Fn, nullptr);
    if (auto Off = PC->GetOffset("AIEvaluators"); Off != uint32(-1))
    {
        auto& Evals = GetFromOffset<TArray<UObject*>>(PC, Off);
        printf("[Syndicate][AITest] evals after tree=%d\n", Evals.Num());
    }
}

static void SyndicateEnsureHenchmanBlackboard(AActor* PC, UObject* BotData, const FVector& Base)
{
    if (!PC) return;
    UObject* Comp = nullptr;
    if (auto Off = PC->GetOffset("Blackboard"); Off != uint32(-1))
        Comp = GetFromOffset<UObject*>(PC, Off);

    auto BBClass = FindClass("BlackboardData");
    auto BBAsset = BBClass ? (UObject*)StaticLoadObject(
        L"/Game/Athena/AI/MANG/BehaviorTree/BB_MANG2.BB_MANG2", BBClass) : nullptr;

    if (!Comp && BBAsset)
    {
        UObject* NewComp = nullptr;
        if (auto Fn = PC->GetFunction("UseBlackboard")) PC->Call<bool>(Fn, BBAsset, &NewComp);
        if (NewComp)
        {
            Comp = NewComp;
            if (auto Off = PC->GetOffset("Blackboard"); Off != uint32(-1))
                GetFromOffset<UObject*>(PC, Off) = Comp;
        }
    }
    if (!Comp)
    {
        printf("[Syndicate][AITest] bb missing\n");
        return;
    }

    UObject* CurrentAsset = nullptr;
    if (auto Off = Comp->GetOffset("BlackboardAsset"); Off != uint32(-1))
        CurrentAsset = GetFromOffset<UObject*>(Comp, Off);
    printf("[Syndicate][AITest] bb comp=%s asset=%s want=%s\n",
        Comp->Name.ToString().c_str(),
        CurrentAsset ? CurrentAsset->Name.ToString().c_str() : "null",
        BBAsset ? BBAsset->Name.ToString().c_str() : "null");

    // If the component is bound to the wrong asset, swap it for BB_MANG2.
    if (BBAsset && CurrentAsset != BBAsset)
    {
        UObject* NewComp = nullptr;
        if (auto Fn = PC->GetFunction("UseBlackboard")) PC->Call<bool>(Fn, BBAsset, &NewComp);
        if (NewComp)
        {
            Comp = NewComp;
            if (auto Off = PC->GetOffset("Blackboard"); Off != uint32(-1))
                GetFromOffset<UObject*>(PC, Off) = Comp;
            if (auto Off = Comp->GetOffset("BlackboardAsset"); Off != uint32(-1))
                CurrentAsset = GetFromOffset<UObject*>(Comp, Off);
        }
    }

    if (!CurrentAsset) return;
    auto KeysOff = CurrentAsset->GetOffset("Keys");
    if (KeysOff == uint32(-1)) return;
    auto& Keys = GetFromOffset<TArray<uint8>>(CurrentAsset, KeysOff);
    int EntrySize = 0x18;

    for (int I = 0; I < Keys.Num() && I < 128; ++I)
    {
        auto Elem = (uint8*)Keys.GetData() + (size_t)I * EntrySize;
        FName KeyName = GetFromOffset<FName>(Elem, 0);
        auto KeyType = GetFromOffset<UObject*>(Elem, 8);
        std::string Key = std::string(KeyName.ToString().c_str());
        std::string Type = KeyType && KeyType->Class ? std::string(KeyType->Class->Name.ToString().c_str()) : "null";
        printf("[Syndicate][AITest] bb key %s type=%s\n", Key.c_str(), Type.c_str());

        std::string Low;
        for (char C : Key) Low += (char)tolower((unsigned char)C);
        bool IsVector = Type.find("Vector") != std::string::npos;
        bool IsFloat = Type.find("Float") != std::string::npos;
        bool IsObject = Type.find("Object") != std::string::npos;

        if (Low == "botcontroller" && IsObject)
        {
            if (auto Fn = Comp->GetFunction("SetValueAsObject")) Comp->Call<void>(Fn, KeyName, (UObject*)PC);
        }
        else if (Low == "botdata" && IsObject)
        {
            if (auto Fn = Comp->GetFunction("SetValueAsObject")) Comp->Call<void>(Fn, KeyName, BotData);
        }
        else if (Low.find("leash") != std::string::npos)
        {
            if (Low.find("radius") != std::string::npos && IsFloat)
            {
                if (auto Fn = Comp->GetFunction("SetValueAsFloat")) Comp->Call<void>(Fn, KeyName, 5000.f);
            }
            else if (Low.find("location") != std::string::npos && IsVector)
            {
                if (auto Fn = Comp->GetFunction("SetValueAsVector")) Comp->Call<void>(Fn, KeyName, Base);
            }
            else if (IsVector)
            {
                if (auto Fn = Comp->GetFunction("SetValueAsVector")) Comp->Call<void>(Fn, KeyName, Base);
            }
        }
    }

    // The evaluator status keys are present in BB_MANG2 but default to denied. The native
    // spawner sets these during BotManagerSetup; our manual spawn path has to do it.
    if (auto SetEnum = Comp->GetFunction("SetValueAsEnum"))
    {
        Comp->Call<void>(SetEnum, FName(L"AIEvaluator_Patrolling_ExecutionStatus"), (uint8)4);
        Comp->Call<void>(SetEnum, FName(L"AIEvaluator_Leash_ExecutionStatus"), (uint8)4);
        Comp->Call<void>(SetEnum, FName(L"AIEvaluator_DynamicBlueprint_ExecutionStatus"), (uint8)4);
    }
    if (auto SetBool = Comp->GetFunction("SetValueAsBool"))
    {
        Comp->Call<void>(SetBool, FName(L"AIEvaluator_Patrolling_ShouldMove"), true);
        Comp->Call<void>(SetBool, FName(L"AIEvaluator_IsStressed"), false);
    }

    if (auto Fn = PC->GetFunction("OnUsingBlackBoard")) PC->Call<void>(Fn, Comp, CurrentAsset);
}

// Cache every real item in the bot inventory (weapons, mythics, keycards, consumables) so
// the native bot death path can be replaced with explicit, complete loot drops. Ammo and the
// harvest tool are skipped to avoid the old 999-ammo and pickaxe junk.
static std::vector<SyndicateDropItem> SyndicateCollectInventoryDrops(AActor* PC)
{
    std::vector<SyndicateDropItem> Out;
    if (!PC) return Out;
    auto Off = PC->GetOffset("Inventory");
    if (Off == uint32(-1)) return Out;
    auto Inv = GetFromOffset<AFortInventory*>(PC, Off);
    if (!Inv) return Out;
    for (int I = 0; I < Inv->Inventory.ReplicatedEntries.Num(); ++I)
    {
        auto& Entry = Inv->Inventory.ReplicatedEntries.Get(I, FFortItemEntry::Size());
        auto Def = Entry.ItemDefinition;
        if (!Def) continue;
        if (Def->IsA(UFortAmmoItemDefinition::StaticClass())) continue;
        if (Def->IsA(UFortWeaponMeleeItemDefinition::StaticClass())) continue;
        SyndicateDropItem Drop;
        Drop.Def = const_cast<UFortItemDefinition*>(Def);
        Drop.Count = Entry.Count > 0 ? Entry.Count : 1;
        Drop.Ammo = Entry.LoadedAmmo;
        Out.push_back(Drop);
    }
    return Out;
}

static void SyndicateApplyMangAnim(AFortPlayerPawnAthena* P);

// Spawn one additional MANG bot through the same native manager path used by the primary
// test bot. The native manager handles the pawn/controller; we only add the inventory,
// name, blackboard, patrol, and game-state registration it misses.
static AFortPlayerPawnAthena* SyndicateSpawnExtraHenchman(
    UFortServerBotManagerAthena* BotManager, UObject* BotData,
    const FVector& Location, const FRotator& Rotation, AActor* PatrolPath,
    AFortGameMode* GameMode, AActor* GameState, int HumanTeam)
{
    if (!BotManager || !BotData) return nullptr;
    auto Fn = BotManager->GetFunction("SpawnBot");
    if (!Fn) return nullptr;

    FSyndicateRuntimeBotData Runtime{};
    Runtime.TagName = FName(L"Athena.Faction.Ego");
    std::string BotName = std::string(BotData->Name.ToString().c_str());
    // Brutus (Grotto/MountainBase) and TnTina (Oil Rig) are SHADOW bosses. The native
    // spawners carry that faction; our manual SpawnBot path must pass it explicitly or the
    // same BotData comes out as a GHOST henchman.
    if (BotName.find("_Alter") != std::string::npos ||
        BotName.find("POI_MountainBase") != std::string::npos ||
        BotName.find("POI_OilRig") != std::string::npos)
        Runtime.TagName = FName(L"Athena.Faction.Alter");
    Runtime.Flags = 1;
    Runtime.CustomSquadId = 10;

    auto Params = Fn->GetParamsNamed();
    std::vector<uint8> Buffer(Params.Size, 0);
    int ReturnOffset = -1;
    for (auto& P : Params.NameOffsetMap)
    {
        if (P.PropertyFlags & 0x400) { ReturnOffset = (int)P.Offset; continue; }
        if (P.Name == "InSpawnLocation" && P.Offset + sizeof(FVector) <= Buffer.size()) memcpy(Buffer.data() + P.Offset, &Location, sizeof(FVector));
        else if (P.Name == "InSpawnRotation" && P.Offset + sizeof(FRotator) <= Buffer.size()) memcpy(Buffer.data() + P.Offset, &Rotation, sizeof(FRotator));
        else if (P.Name == "InBotData" && P.Offset + sizeof(void*) <= Buffer.size()) { auto Value = (const UObject*)BotData; memcpy(Buffer.data() + P.Offset, &Value, sizeof(void*)); }
        else if (P.Name == "InRuntimeBotData" && P.Offset + sizeof(Runtime) <= Buffer.size()) memcpy(Buffer.data() + P.Offset, &Runtime, sizeof(Runtime));
    }
    BotManager->ProcessEvent(Fn, Buffer.data());
    auto NewPawn = (ReturnOffset >= 0 && ReturnOffset + sizeof(void*) <= (int)Buffer.size()) ? *(AFortPlayerPawnAthena**)(Buffer.data() + ReturnOffset) : nullptr;
    if (!NewPawn)
    {
        printf("[Syndicate][AITest] extra hench spawn null botdata=%s\n", BotName.c_str());
        return nullptr;
    }

    // The manager spawned the pawn/controller but left AIEvaluators empty, so the bot can
    // only idle. Call OGS's BotManagerSetup when the SpawnBot RVA proves this is the same
    // executable layout; guard the call on a plausible x64 function prologue.
    {
        auto SpawnFn = BotManager->GetFunction("SpawnBot");
        void* SpawnNative = SpawnFn ? SpawnFn->GetNativeFunc() : nullptr;
        auto ModuleBase = (uint8*)GetModuleHandleA(nullptr);
        unsigned long long SpawnRva = (unsigned long long)((uint8*)SpawnNative - ModuleBase);
        printf("[Syndicate][AITest] spawnbot rva=0x%llx\n", SpawnRva);
        if (SpawnRva == 0x2A902F0ull)
        {
            // 12.50 SpawnBot is at 0x2A902F0. OGS's 12.41 setup function sits 0x12020 bytes
            // earlier; scan SpawnBot's direct calls for that target and fall back to the
            // relative offset only if the prologue looks like a real x64 function.
            auto Code = (uint8*)SpawnNative;
            uintptr_t DirectSetup = 0;
            for (int I = 0; I + 5 < 0x400; ++I)
            {
                if (Code[I] != 0xE8) continue;
                int32_t Rel = *(int32_t*)(Code + I + 1);
                auto Target = (uintptr_t)(Code + I + 5 + Rel);
                unsigned long long Rva = (unsigned long long)((uint8*)Target - ModuleBase);
                printf("[Syndicate][AITest] spawnbot call +0x%x -> 0x%llx\n", I, Rva);
                if (Rva == 0x2A7E2D0ull) DirectSetup = Target;
            }
            uintptr_t SetupAddr = DirectSetup;
            if (!SetupAddr)
            {
                auto Guess = (uintptr_t)(ModuleBase + 0x2A7E2D0);
                uint8 P0 = *(uint8*)Guess;
                bool Plausible = (P0 == 0x48 || P0 == 0x40 || P0 == 0x55 || P0 == 0x53 || P0 == 0x56 || P0 == 0x57 || P0 == 0x41);
                printf("[Syndicate][AITest] botsetup guess=%p prologue=%02X plausible=%d\n",
                    (void*)Guess, (unsigned)P0, (int)Plausible);
                if (Plausible) SetupAddr = Guess;
            }
            if (SetupAddr)
            {
                auto BehaviorTree = BotData->GetOffset("BehaviorTree") != uint32(-1)
                    ? GetFromOffset<UObject*>(BotData, BotData->GetOffset("BehaviorTree")) : nullptr;
                auto Startup = BotData->GetOffset("StartupInventory") != uint32(-1)
                    ? GetFromOffset<UObject*>(BotData, BotData->GetOffset("StartupInventory")) : nullptr;
                auto NameSettings = BotData->GetOffset("BotNameSettings") != uint32(-1)
                    ? GetFromOffset<UObject*>(BotData, BotData->GetOffset("BotNameSettings")) : nullptr;
                using FBotManagerSetup = void(*)(__int64, __int64, __int64, __int64, DWORD*, __int64, __int64, __int64, __int64, BYTE*, uint8, BYTE*, FSyndicateRuntimeBotData);
                auto Setup = (FBotManagerSetup)SetupAddr;
                DWORD Skill = 10;
                BYTE CanRespawn = 0;
                BYTE Squad = 1;
                printf("[Syndicate][AITest] botsetup calling addr=0x%llx tree=%s inv=%s name=%s\n",
                    (unsigned long long)((uint8*)SetupAddr - ModuleBase),
                    BehaviorTree ? BehaviorTree->Name.ToString().c_str() : "null",
                    Startup ? Startup->Name.ToString().c_str() : "null",
                    NameSettings ? NameSettings->Name.ToString().c_str() : "null");
                Setup((__int64)BotManager, (__int64)NewPawn, (__int64)BehaviorTree, 0, &Skill, 0,
                    (__int64)Startup, (__int64)NameSettings, 0, &CanRespawn, 0, &Squad, Runtime);
                printf("[Syndicate][AITest] BotManagerSetup returned\n");
            }
        }
    }

    auto PC = (AActor*)NewPawn->Controller;
    auto State = (AFortPlayerStateAthena*)NewPawn->PlayerState;
    NewPawn->bReplicates = true; NewPawn->bAlwaysRelevant = true; NewPawn->bCanBeDamaged = true;
    NewPawn->NetCullDistanceSquared = 625000000.f;
    bool IsBoss = BotName.find("POI") != std::string::npos || BotName.find("Boss") != std::string::npos;
    float SpawnHealth = IsBoss ? 400.f : 100.f;
    NewPawn->SetMaxHealth(SpawnHealth); NewPawn->SetHealth(SpawnHealth);
    if (IsBoss)
    {
        if (auto Fn = NewPawn->GetFunction("SetMaxShield")) NewPawn->Call<void>(Fn, 100.f);
        NewPawn->SetShield(100.f);
    }
    NewPawn->ForceNetUpdate();

    if (State)
    {
        if (State->HasbIsSpectator()) State->bIsSpectator = false;
        if (State->HasbIsABot()) State->bIsABot = true;
        if (HumanTeam >= 0 && State->TeamIndex == HumanTeam) { State->TeamIndex = 10; State->SquadId = 10; }
        State->ForceNetUpdate();
        if (GameState)
        {
            if (auto Off = GameState->GetOffset("PlayerArray"); Off != uint32(-1))
            {
                auto& Players = GetFromOffset<TArray<AActor*>>(GameState, Off);
                bool Found = false;
                for (int I = 0; I < Players.Num(); ++I) if (Players[I] == (AActor*)State) { Found = true; break; }
                if (!Found) Players.Add((AActor*)State);
            }
        }
    }
    // Register the AI controller in the game mode's AliveBots array. The native death
    // cleanup removes from this array by index; if the bot was never added, the remove
    // uses index -1 and crashes the server at 0x1926ff4.
    if (GameMode && PC)
    {
        if (auto Off = GameMode->GetOffset("AliveBots"); Off != uint32(-1))
        {
            auto& Bots = GetFromOffset<TArray<AActor*>>(GameMode, Off);
            bool Found = false;
            for (int I = 0; I < Bots.Num(); ++I) if (Bots[I] == PC) { Found = true; break; }
            if (!Found) Bots.Add(PC);
        }
    }

    SyndicateSetHenchmanName(State, PC);
    SyndicateGiveHenchmanWeapon(PC, NewPawn, BotData);
    SyndicateApplyMangAnim(NewPawn);
    SyndicateStartHenchmanTree(PC);
    SyndicateEnsureHenchmanBlackboard(PC, BotData, NewPawn->K2_GetActorLocation());

    SyndicateHenchmanRecord Rec;
    Rec.Pawn = NewPawn;
    Rec.BotData = BotData;
    Rec.PatrolPath = PatrolPath;
    if (BotName.find("POI_Agency") != std::string::npos) Rec.BossKey = "Midas";
    else if (BotName.find("POI_MountainBase") != std::string::npos) Rec.BossKey = "Brutus";
    else if (BotName.find("POI_OilRig") != std::string::npos) Rec.BossKey = "TnTina";
    else if (BotName.find("POI_SJI") != std::string::npos) Rec.BossKey = "Skye";
    else if (BotName.find("POI_Yacht") != std::string::npos) Rec.BossKey = "Meowscles";
    else if (BotName.find("POI_HDP") != std::string::npos) Rec.BossKey = "Deadpool";
    Rec.WeaponDef = SyndicateLastHenchmanWeapon;
    Rec.InventoryDrops = SyndicateCollectInventoryDrops(PC);
    Rec.LastLoc = NewPawn->K2_GetActorLocation();
    Rec.NextLog = GetTickCount64() + 5000;
    SyndicateHenchmen.push_back(Rec);
    printf("[Syndicate][AITest] extra hench pawn=%s botdata=%s\n",
        NewPawn->Name.ToString().c_str(), BotName.c_str());
    return NewPawn;
}

// Build a wide four-point patrol loop around a spawn point so the bots do not pace in one
// tiny native path loop.
static AActor* SyndicateCreatePatrolLoop(const FVector& Center, double Radius)
{
    auto PathClass = FindClass("FortAthenaPatrolPath");
    auto PointClass = FindClass("FortAthenaPatrolPoint");
    if (!PathClass || !PointClass) return nullptr;
    auto Path = (AActor*)UWorld::SpawnActor(PathClass, Center);
    if (!Path) return nullptr;
    auto PointsOff = Path->GetOffset("PatrolPoints");
    if (PointsOff == uint32(-1)) return nullptr;
    auto& Points = GetFromOffset<TArray<AActor*>>(Path, PointsOff);
    for (int I = 0; I < 4; ++I)
    {
        double Angle = (3.141592653589793 / 2.0) * I;
        FVector P(Center.X + cos(Angle) * Radius, Center.Y + sin(Angle) * Radius, Center.Z);
        auto Pt = (AActor*)UWorld::SpawnActor(PointClass, P);
        if (Pt) Points.Add(Pt);
    }
    printf("[Syndicate][AITest] patrol loop center=%.0f,%.0f,%.0f radius=%.0f points=%d\n",
        Center.X, Center.Y, Center.Z, Radius, Points.Num());
    return Path;
}

// Load the native POI MANG level assets. They contain the placed BP_MANG_Spawner actors and
// their own patrol/bot-data configs, which the persistent Apollo map does not include.
static int SyndicateLoadPoiMangLevels(UWorld* World)
{
    if (!World) return 0;
    const wchar_t* LevelNames[] = {
        L"/Game/Athena/AI/MANG/Apollo_POI_Agency_MANG",
        L"/Game/Athena/AI/MANG/Apollo_POI_MANG_HMW",
        L"/Game/Athena/AI/MANG/Apollo_POI_MANG_OilRig",
        L"/Game/Athena/AI/MANG/Apollo_POI_MANG_MountainBase",
        L"/Game/Athena/AI/MANG/Apollo_POI_MANG_SJI",
        L"/Game/Athena/AI/MANG/Apollo_POI_MANG_Yacht",
        L"/Game/Athena/AI/MANG/Apollo_POI_OilRig_MANG",
        L"/Game/Athena/AI/MANG/Apollo_POI_MountainBase_MANG",
        L"/Game/Athena/AI/MANG/Apollo_POI_SJI_MANG",
        L"/Game/Athena/AI/MANG/Apollo_POI_Yacht_MANG"
    };

    auto LSClass = FindClass("LevelStreamingDynamic");
    auto LSDefault = LSClass ? LSClass->GetDefaultObj() : nullptr;
    auto Fn = LSDefault ? LSDefault->GetFunction("LoadLevelInstance") : nullptr;
    if (Fn)
    {
        for (auto Name : LevelNames)
        {
            auto Asset = StaticLoadObject(Name, UObject::StaticClass());
            printf("[Syndicate][AITest] mang level %ls asset=%s class=%s\n", Name,
                Asset ? Asset->Name.ToString().c_str() : "null",
                Asset && Asset->Class ? Asset->Class->Name.ToString().c_str() : "-");

            auto Meta = Fn->GetParamsNamed();
            std::vector<uint8> Buf(Meta.Size, 0);
            FString LevelName(Name);
            FVector Loc{};
            FRotator Rot{};
            bool Success = false;
            FString Override{};
            for (auto& P : Meta.NameOffsetMap)
            {
                if (P.Name == "WorldContextObject" && P.Offset + sizeof(void*) <= Buf.size())
                    *(UObject**)(Buf.data() + P.Offset) = World;
                else if (P.Name == "LevelName" && P.Offset + sizeof(FString) <= Buf.size())
                    *(FString*)(Buf.data() + P.Offset) = LevelName;
                else if (P.Name == "Location" && P.Offset + sizeof(FVector) <= Buf.size())
                    memcpy(Buf.data() + P.Offset, &Loc, sizeof(FVector));
                else if (P.Name == "Rotation" && P.Offset + sizeof(FRotator) <= Buf.size())
                    memcpy(Buf.data() + P.Offset, &Rot, sizeof(FRotator));
                else if (P.Name == "bOutSuccess" && P.Offset + sizeof(void*) <= Buf.size())
                    *(bool**)(Buf.data() + P.Offset) = &Success;
                else if (P.Name == "OptionalLevelNameOverride" && P.Offset + sizeof(FString) <= Buf.size())
                    *(FString*)(Buf.data() + P.Offset) = Override;
            }
            LSDefault->ProcessEvent(Fn, Buf.data());
            printf("[Syndicate][AITest] mang level load success=%d\n", (int)Success);
        }
    }
    else
    {
        printf("[Syndicate][AITest] mang level loader missing\n");
    }

    if (auto GS = FindClass("GameplayStatics"))
        if (auto Flush = GS->GetDefaultObj()->GetFunction("FlushLevelStreaming"))
            GS->GetDefaultObj()->Call<void>(Flush, World);

    int Count = 0;
    if (auto SpawnerClass = (UClass*)StaticLoadObject(
        L"/Game/Athena/AI/MANG/BP_MANG_Spawner.BP_MANG_Spawner_C", UClass::StaticClass()))
    {
        TArray<AActor*> Spawners; Utils::GetAll(SpawnerClass, Spawners);
        printf("[Syndicate][AITest] native mang spawners=%d\n", Spawners.Num());
        for (int I = 0; I < Spawners.Num() && I < 20; ++I)
        {
            if (!Spawners[I]) continue;
            auto L = Spawners[I]->K2_GetActorLocation();
            printf("[Syndicate][AITest] native spawner %s loc=%.0f,%.0f,%.0f\n",
                Spawners[I]->Name.ToString().c_str(), L.X, L.Y, L.Z);
        }
        // Dump the Blueprint spawner API/config and all boss BotData style variants so the
        // fallback can spawn the same Ghost/Shadow boss look as the native spawner.
        {
            auto SClass = (UStruct*)SpawnerClass;
            std::string Chain;
            for (auto S = SClass; S; S = (UStruct*)S->GetSuper())
            { Chain += "/"; Chain += ((UObject*)S)->Name.ToString().c_str(); }
            printf("[Syndicate][AITest] spawner-class chain=%s\n", Chain.c_str());
            int FnCount = 0;
            for (const UField* F = SClass->GetChildren(); F && FnCount < 60; F = F->GetNext(), ++FnCount)
                printf("[Syndicate][AITest] spawner-fn %s\n", F->GetName().ToString().c_str());
            int PropCount = 0;
            for (const UField* P = SClass->GetChildProperties(); P && PropCount < 60; P = P->FField_GetNext(), ++PropCount)
                printf("[Syndicate][AITest] spawner-prop %s\n", P->FField_GetName().ToString().c_str());
        }
        if (Spawners.Num() > 0 && Spawners[0])
        {
            auto A = Spawners[0];
            for (auto S = (UStruct*)A->Class; S; S = (UStruct*)S->GetSuper())
                for (const UField* P = S->GetChildProperties(); P; P = P->FField_GetNext())
                {
                    std::string PN = P->FField_GetName().ToString().c_str();
                    auto Off = A->GetOffset(PN.c_str());
                    if (Off == uint32(-1)) continue;
                    std::string Kind = P->Class->Name.ToString().c_str();
                    if (Kind.find("ObjectProperty") != std::string::npos)
                    {
                        auto O = GetFromOffset<UObject*>(A, Off);
                        printf("[Syndicate][AITest] spawner-val %s = %s\n", PN.c_str(), O ? O->Name.ToString().c_str() : "null");
                    }
                    else if (Kind.find("ArrayProperty") != std::string::npos)
                    {
                        auto& Arr = GetFromOffset<TArray<uint8>>(A, Off);
                        printf("[Syndicate][AITest] spawner-val %s array=%d\n", PN.c_str(), Arr.Num());
                    }
                    else if (Kind.find("NameProperty") != std::string::npos)
                        printf("[Syndicate][AITest] spawner-val %s = %s\n", PN.c_str(), GetFromOffset<FName>(A, Off).ToString().c_str());
                    else if (Kind.find("StructProperty") != std::string::npos)
                        printf("[Syndicate][AITest] spawner-val %s (struct)\n", PN.c_str());
                }
        }
        if (auto BDClass = FindClass("FortAthenaAIBotCustomizationData"))
        {
            for (const wchar_t* Base : { L"Agency", L"OilRig", L"MountainBase", L"SJI", L"Yacht", L"HDP" })
                for (const wchar_t* Suffix : { L"", L"_Alter", L"_Ego", L"_Ghost", L"_Shadow" })
                {
                    wchar_t Path[256]{};
                    swprintf_s(Path, L"/Game/Athena/AI/MANG/BotData/BotData_MANG_POI_%s%s.BotData_MANG_POI_%s%s", Base, Suffix, Base, Suffix);
                    if (auto Obj = StaticLoadObject(Path, (UClass*)BDClass))
                        printf("[Syndicate][AITest] bossdata %ls exists (%s)\n", Path, Obj->Name.ToString().c_str());
                }
        }
        Count = Spawners.Num();
        Spawners.Free();
    }
    return Count;
}

// Spawn the Agency MANG group only after the island and its patrol paths are loaded. The
// earlier inline spawn ran during ReadyToStartMatch, when paths=0, so every bot landed at the
// POI center inside geometry. OGS spawns each MANG bot at a patrol path's first point.
static bool SyndicateSpawnAgencyGroup()
{
    auto World = UWorld::GetWorld();
    if (!World) return false;
    auto GameMode = (AFortGameMode*)World->AuthorityGameMode;
    auto GameState = (AActor*)World->GameState;
    auto BotManager = GameMode ? (UFortServerBotManagerAthena*)GameMode->ServerBotManager : nullptr;
    if (!BotManager) { printf("[Syndicate][AITest] deferred spawn: no bot manager\n"); return false; }

    static bool MangLevelsLoaded = false;
    static ULONGLONG MangLoadedAt = 0;
    static bool PoiDumped = false;

    if (!MangLevelsLoaded)
    {
        MangLevelsLoaded = true;
        int Loaded = SyndicateLoadPoiMangLevels(World);
        SyndicateNativeSpawnerCount = Loaded;
        MangLoadedAt = GetTickCount64();
        printf("[Syndicate][AITest] native level spawners=%d\n", Loaded);
    }

    int NativeSpawners = SyndicateNativeSpawnerCount;

    if (NativeSpawners > 0 && !PoiDumped)
    {
        PoiDumped = true;
        // One-time POI dump: some POIs (e.g. Catty Corner) have no native MANG spawner in the
        // loaded levels, so we need their real brush-bounds center for the fallback group.
        if (auto VolumeClass = FindClass("FortPoiVolume"))
        {
            TArray<AActor*> Volumes; Utils::GetAll(VolumeClass, Volumes);
            for (int V = 0; V < Volumes.Num(); ++V)
            {
                auto Vol = Volumes[V]; if (!Vol) continue;
                auto TagsOff = Vol->GetOffset("LocationTags");
                if (TagsOff == uint32(-1)) continue;
                auto& Tags = GetFromOffset<TArray<FName>>(Vol, TagsOff);
                std::string TagStr;
                bool PoiTag = false;
                for (int T = 0; T < Tags.Num(); ++T)
                {
                    std::string Tag = std::string(Tags[T].ToString().c_str());
                    if (Tag.find("Athena.Location.POI") != std::string::npos) PoiTag = true;
                    if (T < 4) { if (T) TagStr += "|"; TagStr += Tag; }
                }
                std::string VN = std::string(Vol->Name.ToString().c_str());
                if (!PoiTag && VN.find("Catty") == std::string::npos) continue;
                auto ActorLoc = Vol->K2_GetActorLocation();
                FVector BrushLoc{};
                if (auto BO = Vol->GetOffset("BrushComponent"); BO != uint32(-1))
                    if (auto Brush = GetFromOffset<UObject*>(Vol, BO))
                        if (auto BB = Brush->GetOffset("Bounds"); BB != uint32(-1))
                            BrushLoc = GetFromOffset<FVector>(Brush, BB);
                printf("[Syndicate][AITest] poi dump %s actor=%.0f,%.0f,%.0f brush=%.0f,%.0f,%.0f tags=[%s]\n",
                    VN.c_str(), ActorLoc.X, ActorLoc.Y, ActorLoc.Z,
                    BrushLoc.X, BrushLoc.Y, BrushLoc.Z, TagStr.c_str());
                if (VN.find("Catty") != std::string::npos || TagStr.find("Catty") != std::string::npos)
                {
                    auto CL = (BrushLoc.X != 0.0 || BrushLoc.Y != 0.0 || BrushLoc.Z != 0.0)
                        ? BrushLoc : ActorLoc;
                    if (CL.X != 0.0 || CL.Y != 0.0 || CL.Z != 0.0)
                    {
                        SyndicateCattyValid = true;
                        SyndicateCattyLoc = CL;
                        printf("[Syndicate][AITest] Catty Corner loc=%.0f,%.0f,%.0f\n",
                            CL.X, CL.Y, CL.Z);
                    }
                }
            }
            Volumes.Free();
        }
        // Bots near the loaded level are created by BP_MANG_Spawner and adopted in the tick.
        // Spawners for POIs whose level is not streamed do not create pawns, so the manual
        // pass below fills those POIs at the native spawner coordinates.
        printf("[Syndicate][AITest] native spawners present; adopting native bots\n");
    }

    // Give the native spawners and the adoption pass time to create the groups that do
    // activate. Then top up every missing authored patrol spawn below.
    if (NativeSpawners > 0 && GetTickCount64() - MangLoadedAt < 8000)
        return false;

    int HumanTeam = -1;
    if (auto PCClass = FindClass("FortPlayerControllerAthena"))
    {
        TArray<AActor*> Controllers; Utils::GetAll(PCClass, Controllers);
        for (auto A : Controllers)
        {
            auto HPC = (AFortPlayerControllerAthena*)A;
            auto HPS = HPC ? (AFortPlayerStateAthena*)HPC->PlayerState : nullptr;
            if (HPC && HPC->MyFortPawn && HPS && !(HPS->HasbIsABot() && HPS->bIsABot)) { HumanTeam = HPS->TeamIndex; break; }
        }
        Controllers.Free();
    }

    // ---- Multi-POI MANG spawn pass -------------------------------------------------
    // Each named POI volume owns one boss plus two henchmen. The boss BotData carries its
    // mythic weapon, keycard, pawn class and boss controller; the henchmen use the generic
    // Ego loadout data. Spawn points come from the native patrol paths nearest the volume.
    auto BotDataClass = FindClass("FortAthenaAIBotCustomizationData");
    struct FBossEntry { const char* Needle; const char* Needle2; const char* Needle3; const wchar_t* DataPath; };
    // Use the faction-specific boss data. The plain POI asset gives the default outfit;
    // _Ego is the GHOST look and _Alter the SHADOW look (same split as the henchmen).
    const FBossEntry BossEntries[] = {
        { "Agency",       "",        "",        L"/Game/Athena/AI/MANG/BotData/BotData_MANG_POI_Agency_Ego.BotData_MANG_POI_Agency_Ego" },
        { "OilRig",       "Rig",     "",        L"/Game/Athena/AI/MANG/BotData/BotData_MANG_POI_OilRig_Alter.BotData_MANG_POI_OilRig_Alter" },
        { "MountainBase", "Grotto",  "",        L"/Game/Athena/AI/MANG/BotData/BotData_MANG_POI_MountainBase_Alter.BotData_MANG_POI_MountainBase_Alter" },
        { "HMW",          "Hydro",   "",        L"/Game/Athena/AI/MANG/BotData/BotData_MANG_POI_HMW.BotData_MANG_POI_HMW" },
        { "SJI",          "Shark",   "",        L"/Game/Athena/AI/MANG/BotData/BotData_MANG_POI_SJI_Ego.BotData_MANG_POI_SJI_Ego" },
        { "Yacht",        "",        "",        L"/Game/Athena/AI/MANG/BotData/BotData_MANG_POI_Yacht_Ego.BotData_MANG_POI_Yacht_Ego" },
        { "HDP",          "Deadpool","",        L"/Game/Athena/AI/MANG/BotData/BotData_MANG_POI_HDP.BotData_MANG_POI_HDP" }
    };
    const wchar_t* HenchmanPaths[] = {
        L"/Game/Athena/AI/MANG/BotData/BotData_MANG_Assault_Ego.BotData_MANG_Assault_Ego",
        L"/Game/Athena/AI/MANG/BotData/BotData_MANG_SMG_Ego.BotData_MANG_SMG_Ego",
        L"/Game/Athena/AI/MANG/BotData/BotData_MANG_Sniper_Ego.BotData_MANG_Sniper_Ego",
        L"/Game/Athena/AI/MANG/BotData/BotData_MANG_Pistol_Ego.BotData_MANG_Pistol_Ego"
    };
    const wchar_t* HenchmanAlterPaths[] = {
        L"/Game/Athena/AI/MANG/BotData/BotData_MANG_Assault_Alter.BotData_MANG_Assault_Alter",
        L"/Game/Athena/AI/MANG/BotData/BotData_MANG_SMG_Alter.BotData_MANG_SMG_Alter",
        L"/Game/Athena/AI/MANG/BotData/BotData_MANG_Sniper_Alter.BotData_MANG_Sniper_Alter",
        L"/Game/Athena/AI/MANG/BotData/BotData_MANG_Pistol_Alter.BotData_MANG_Pistol_Alter"
    };
    const int BossCount = sizeof(BossEntries) / sizeof(BossEntries[0]);
    bool BossUsed[sizeof(BossEntries) / sizeof(BossEntries[0])]{};

    struct FPathPoint { AActor* Path; FVector Loc; FRotator Rot; };
    std::vector<FPathPoint> PathPoints;
    if (auto PathClass = FindClass("FortAthenaPatrolPath"))
    {
        TArray<AActor*> Paths; Utils::GetAll(PathClass, Paths);
        for (int I = 0; I < Paths.Num(); ++I)
        {
            auto A = Paths[I]; if (!A) continue;
            auto POff = A->GetOffset("PatrolPoints");
            if (POff == uint32(-1)) continue;
            auto& Pts = GetFromOffset<TArray<AActor*>>(A, POff);
            if (Pts.Num() == 0 || !Pts[0]) continue;
            PathPoints.push_back({ A, Pts[0]->K2_GetActorLocation(), Pts[0]->K2_GetActorRotation() });
        }
        Paths.Free();
    }
    std::vector<bool> PathUsed(PathPoints.size(), false);
    auto TakeNearest = [&](const FVector& AnchorPoint) -> int
    {
        int Best = -1; double BestD = 1.0e18;
        for (int I = 0; I < (int)PathPoints.size(); ++I)
        {
            if (PathUsed[I]) continue;
            double DX = PathPoints[I].Loc.X - AnchorPoint.X, DY = PathPoints[I].Loc.Y - AnchorPoint.Y;
            double D = DX * DX + DY * DY;
            if (D < BestD) { BestD = D; Best = I; }
        }
        if (Best >= 0) PathUsed[Best] = true;
        return Best;
    };

    int MultiSpawned = 0, BossesSpawned = 0;

    // The loaded MANG levels expose the author-placed spawners with the real POI transforms.
    // Use those directly instead of the POI-volume/patrol guesses.
    if (NativeSpawners > 0)
    {
        if (auto SpawnerClass = (UClass*)StaticLoadObject(
            L"/Game/Athena/AI/MANG/BP_MANG_Spawner.BP_MANG_Spawner_C", UClass::StaticClass()))
        {
            TArray<AActor*> SpawnerList;
            Utils::GetAll(SpawnerClass, SpawnerList);
            std::set<std::string> SeenSpawners;

            // Streamed levels only wake the spawners near the loaded POI. Fill the other POIs
            // manually, but skip any boss whose native pawn is already alive.
            std::set<std::string> AliveBossNames;
            if (auto CtrlClass = FindClass("FortAthenaAIBotController"))
            {
                TArray<AActor*> Ctrls2; Utils::GetAll(CtrlClass, Ctrls2);
                for (auto C2 : Ctrls2)
                {
                    auto PawnOff = C2 ? C2->GetOffset("Pawn") : uint32(-1);
                    auto P2 = PawnOff != uint32(-1) ? (AFortPlayerPawnAthena*)GetFromOffset<AActor*>(C2, PawnOff) : nullptr;
                    if (!P2) continue;
                    std::string N;
                    if (auto PS2 = (AFortPlayerStateAthena*)P2->PlayerState)
                        if (auto Fn2 = PS2->GetFunction("GetPlayerName"))
                            N = std::string(PS2->Call<FString>(Fn2).ToString().c_str());
                    N += " ";
                    N += std::string(P2->Class->Name.ToString().c_str());
                    for (char& Ch : N) Ch = (char)tolower((unsigned char)Ch);
                    if (N.find("midas") != std::string::npos) AliveBossNames.insert("Midas");
                    else if (N.find("brutus") != std::string::npos) AliveBossNames.insert("Brutus");
                    else if (N.find("skye") != std::string::npos) AliveBossNames.insert("Skye");
                    else if (N.find("tntina") != std::string::npos) AliveBossNames.insert("TnTina");
                    else if (N.find("meowscles") != std::string::npos) AliveBossNames.insert("Meowscles");
                    else if (N.find("deadpool") != std::string::npos || N.find("hdp") != std::string::npos) AliveBossNames.insert("Deadpool");
                }
                Ctrls2.Free();
            }
            for (auto& R : SyndicateHenchmen)
                if (R.Pawn && !R.BossKey.empty()) AliveBossNames.insert(R.BossKey);

            // Count living MANG pawns near a point so we only add the patrol spawns that
            // the native spawner did not already create.
            auto MangPawnsNear = [&](const FVector& P, double R, bool BossOnly) -> int
            {
                int N = 0;
                if (auto CtrlClass = FindClass("FortAthenaAIBotController"))
                {
                    TArray<AActor*> Ctrls3; Utils::GetAll(CtrlClass, Ctrls3);
                    for (auto C3 : Ctrls3)
                    {
                        auto PO = C3 ? C3->GetOffset("Pawn") : uint32(-1);
                        auto P3 = PO != uint32(-1) ? (AFortPlayerPawnAthena*)GetFromOffset<AActor*>(C3, PO) : nullptr;
                        if (!P3) continue;
                        std::string CN3 = std::string(P3->Class->Name.ToString().c_str());
                        if (CN3.find("MangPlayerPawn") == std::string::npos) continue;
                        if (BossOnly && CN3.find("Boss") == std::string::npos) continue;
                        auto L3 = P3->K2_GetActorLocation();
                        double dx = L3.X - P.X, dy = L3.Y - P.Y, dz = fabs(L3.Z - P.Z);
                        if (dx * dx + dy * dy <= R * R && dz <= 1500.0) ++N;
                    }
                    Ctrls3.Free();
                }
                return N;
            };

            for (int I = 0; I < SpawnerList.Num(); ++I)
            {
                auto Spawner = SpawnerList[I]; if (!Spawner) continue;
                std::string SN = std::string(Spawner->Name.ToString().c_str());
                int BossIdx = -1;
                bool HenchOnly = false;
                bool SkipBoss = false;
                bool CattySpawner = false;
                if (SN.find("Boss_POI_Agency") != std::string::npos) BossIdx = 0;
                else if (SN.find("Boss_POI_OilRig") != std::string::npos) BossIdx = 1;
                else if (SN.find("Boss_POI_MountainCave") != std::string::npos) BossIdx = 2;
                else if (SN.find("Boss_POI_SJI") != std::string::npos) BossIdx = 4;
                else if (SN.find("Boss_POI_Yacht") != std::string::npos) { BossIdx = 5; SkipBoss = true; }
                else if (SN.find("POI_HDP") != std::string::npos) BossIdx = 6;
                else if (SN.find("POI_HMW") != std::string::npos) { HenchOnly = true; CattySpawner = true; }
                else continue;
                if (!SeenSpawners.insert(SN).second) continue;

                const char* WantedBoss = nullptr;
                switch (BossIdx)
                {
                    case 0: WantedBoss = "Midas"; break;
                    case 1: WantedBoss = "TnTina"; break;
                    case 2: WantedBoss = "Brutus"; break;
                    case 4: WantedBoss = "Skye"; break;
                    case 5: WantedBoss = "Meowscles"; break;
                    case 6: WantedBoss = "Deadpool"; break;
                }
                if (WantedBoss && AliveBossNames.count(WantedBoss))
                    printf("[Syndicate][AITest] native boss %s already present; skip %s\n",
                        WantedBoss, SN.c_str());

                auto Loc = Spawner->K2_GetActorLocation();
                auto Rot = Spawner->K2_GetActorRotation();
                bool ShadowGroup = (BossIdx == 1 || BossIdx == 2);

                // One bot per authored patrol spawn. Natives use a path point per bot, so
                // this reproduces their real count and floor positions.
                std::vector<std::pair<double, FPathPoint*>> Cand;
                for (auto& PP : PathPoints)
                {
                    double dx = PP.Loc.X - Loc.X, dy = PP.Loc.Y - Loc.Y, dz = fabs(PP.Loc.Z - Loc.Z);
                    double d2 = dx * dx + dy * dy;
                    if (d2 <= 6000.0 * 6000.0 && dz <= 1200.0) Cand.push_back({ d2, &PP });
                }
                std::sort(Cand.begin(), Cand.end(), [](auto& A, auto& B) { return A.first < B.first; });

                // Catty Corner is the Box Factory landmark in 12.50, not a named POI, so it has
                // no FortPoiVolume. Its native MANG spawner is POI_HMW: the same spawner that
                // creates the GHOST henchmen there. Use its nearest authored patrol point,
                // which sits on the factory floor (the spawner itself is 700 units above it).
                if (CattySpawner)
                {
                    SyndicateCattyValid = true;
                    SyndicateCattyLoc = Cand.empty() ? Loc : Cand[0].second->Loc;
                    printf("[Syndicate][AITest] Catty Corner (Box Factory spawner) loc=%.0f,%.0f,%.0f paths=%d\n",
                        SyndicateCattyLoc.X, SyndicateCattyLoc.Y, SyndicateCattyLoc.Z, (int)Cand.size());
                }

                printf("[Syndicate][AITest] native spawner use %s loc=%.0f,%.0f,%.0f paths=%d faction=%s\n",
                    SN.c_str(), Loc.X, Loc.Y, Loc.Z, (int)Cand.size(),
                    ShadowGroup ? "SHADOW" : "GHOST");

                if (!HenchOnly && !SkipBoss && BossIdx >= 0 &&
                    (!WantedBoss || !AliveBossNames.count(WantedBoss)) &&
                    MangPawnsNear(Loc, 3000.0, true) == 0)
                {
                    FVector BossLoc = Cand.empty() ? Loc : Cand[0].second->Loc;
                    AActor* BossPath = Cand.empty() ? SyndicateCreatePatrolLoop(BossLoc, 1000.0) : Cand[0].second->Path;
                    auto Data = (UObject*)StaticLoadObject(BossEntries[BossIdx].DataPath, BotDataClass);
                    if (Data && SyndicateSpawnExtraHenchman(BotManager, Data, BossLoc, Rot,
                        BossPath ? BossPath : Spawner, GameMode, GameState, HumanTeam))
                    {
                        ++MultiSpawned;
                        ++BossesSpawned;
                        if (WantedBoss) AliveBossNames.insert(WantedBoss);
                    }
                }

                int SpawnedHere = 0;
                for (int C = 0; C < (int)Cand.size() && SpawnedHere < 10; ++C)
                {
                    if (MangPawnsNear(Cand[C].second->Loc, 800.0, false) > 0) continue;
                    auto HenchData = (UObject*)StaticLoadObject(
                        (ShadowGroup ? HenchmanAlterPaths : HenchmanPaths)[SpawnedHere % 4], BotDataClass);
                    if (!HenchData && ShadowGroup)
                        HenchData = (UObject*)StaticLoadObject(HenchmanPaths[SpawnedHere % 4], BotDataClass);
                    if (HenchData && SyndicateSpawnExtraHenchman(BotManager, HenchData,
                        Cand[C].second->Loc, Cand[C].second->Rot, Cand[C].second->Path,
                        GameMode, GameState, HumanTeam))
                    { ++MultiSpawned; ++SpawnedHere; }
                }
                if (Cand.empty())
                {
                    for (int H = 0; H < 2; ++H)
                    {
                        FVector HLoc(Loc.X + (H == 0 ? 260.0 : -260.0), Loc.Y + (H == 0 ? 80.0 : -80.0), Loc.Z);
                        auto HenchData = (UObject*)StaticLoadObject(
                            (ShadowGroup ? HenchmanAlterPaths : HenchmanPaths)[H % 4], BotDataClass);
                        if (!HenchData && ShadowGroup)
                            HenchData = (UObject*)StaticLoadObject(HenchmanPaths[H % 4], BotDataClass);
                        auto Loop = SyndicateCreatePatrolLoop(HLoc, 750.0);
                        if (HenchData && SyndicateSpawnExtraHenchman(BotManager, HenchData, HLoc, Rot,
                            Loop ? Loop : Spawner, GameMode, GameState, HumanTeam)) ++MultiSpawned;
                    }
                }
            }
            // Freeze the population: destroy the placed spawners so a late activation
            // cannot add a second group or look like a dead bot respawning.
            for (int I = 0; I < SpawnerList.Num(); ++I)
                if (SpawnerList[I]) SpawnerList[I]->K2_DestroyActor();
            SpawnerList.Free();
        }
        printf("[Syndicate][AITest] native spawner groups bosses=%d bots=%d\n", BossesSpawned, MultiSpawned);
        return true;
    }

    if (auto VolumeClass = FindClass("FortPoiVolume"))
    {
        TArray<AActor*> Volumes; Utils::GetAll(VolumeClass, Volumes);
        printf("[Syndicate][AITest] poi volumes=%d\n", Volumes.Num());
        for (int V = 0; V < Volumes.Num(); ++V)
        {
            auto Vol = Volumes[V]; if (!Vol) continue;
            auto TagsOff = Vol->GetOffset("LocationTags");
            if (TagsOff == uint32(-1)) continue;
            auto& Tags = GetFromOffset<TArray<FName>>(Vol, TagsOff);
            // The volume actor and its PoiCollisionComp are authored at 0,0,0; the brush
            // geometry itself is in world space. Read the brush component's world bounds.
            FVector VolLoc{};
            if (auto BrushOff = Vol->GetOffset("BrushComponent"); BrushOff != uint32(-1))
            {
                auto Brush = GetFromOffset<UObject*>(Vol, BrushOff);
                if (Brush)
                {
                    if (auto BOff = Brush->GetOffset("Bounds"); BOff != uint32(-1))
                        VolLoc = GetFromOffset<FVector>(Brush, BOff);
                    if (VolLoc.X == 0.0 && VolLoc.Y == 0.0 && VolLoc.Z == 0.0)
                        if (auto COff = Brush->GetOffset("CachedWorldSpaceBounds"); COff != uint32(-1))
                            VolLoc = GetFromOffset<FVector>(Brush, COff);
                }
            }
            if (VolLoc.X == 0.0 && VolLoc.Y == 0.0 && VolLoc.Z == 0.0)
            {
                if (auto CompOff = Vol->GetOffset("PoiCollisionComp"); CompOff != uint32(-1))
                {
                    auto PoiComp = GetFromOffset<UObject*>(Vol, CompOff);
                    if (PoiComp)
                    {
                        if (auto Fn = PoiComp->GetFunction("K2_GetComponentLocation"))
                            VolLoc = PoiComp->Call<FVector>(Fn);
                        else if (auto Fn = PoiComp->GetFunction("GetComponentLocation"))
                            VolLoc = PoiComp->Call<FVector>(Fn);
                    }
                }
            }
            if (VolLoc.X == 0.0 && VolLoc.Y == 0.0 && VolLoc.Z == 0.0)
                VolLoc = Vol->K2_GetActorLocation();
            if (V < 20)
            {
                std::string All;
                for (int T = 0; T < Tags.Num(); ++T)
                {
                    if (T) All += " | ";
                    All += Tags[T].ToString().c_str();
                }
                printf("[Syndicate][AITest] poi volume %s tags=[%s]\n", Vol->Name.ToString().c_str(), All.c_str());
            }
            std::string ChosenTag;
            int BossIndex = -1;
            for (int B = 0; B < BossCount; ++B)
            {
                if (BossUsed[B]) continue;
                for (int T = 0; T < Tags.Num(); ++T)
                {
                    std::string Tag = std::string(Tags[T].ToString().c_str());
                    bool Match = false;
                    for (const char* Needle : { BossEntries[B].Needle, BossEntries[B].Needle2, BossEntries[B].Needle3 })
                        if (Needle && Needle[0] && Tag.find(Needle) != std::string::npos) { Match = true; break; }
                    if (Match)
                    {
                        ChosenTag = Tag;
                        BossIndex = B;
                        break;
                    }
                }
                if (BossIndex >= 0) break;
            }
            if (BossIndex < 0) continue;
            if (V < 30)
                printf("[Syndicate][AITest] poi match %s tag=%s loc=%.0f,%.0f,%.0f\n",
                    BossEntries[BossIndex].Needle, ChosenTag.c_str(), VolLoc.X, VolLoc.Y, VolLoc.Z);

            BossUsed[BossIndex] = true;
            auto BossData = BotDataClass ? (UObject*)StaticLoadObject(BossEntries[BossIndex].DataPath, BotDataClass) : nullptr;
            int BossPoint = TakeNearest(VolLoc);
            if (!BossData || BossPoint < 0) continue;

            auto BossLoop = SyndicateCreatePatrolLoop(PathPoints[BossPoint].Loc, 1000.0);
            auto UseBossPath = BossLoop ? BossLoop : PathPoints[BossPoint].Path;
            printf("[Syndicate][AITest] boss spawn %s loc=%.0f,%.0f,%.0f\n",
                BossEntries[BossIndex].Needle, PathPoints[BossPoint].Loc.X, PathPoints[BossPoint].Loc.Y, PathPoints[BossPoint].Loc.Z);
            if (SyndicateSpawnExtraHenchman(BotManager, BossData, PathPoints[BossPoint].Loc, PathPoints[BossPoint].Rot,
                UseBossPath, GameMode, GameState, HumanTeam))
            {
                ++MultiSpawned;
                ++BossesSpawned;
            }

            for (int H = 0; H < 2; ++H)
            {
                int HPoint = TakeNearest(VolLoc);
                if (HPoint < 0) break;
                auto HenchData = (UObject*)StaticLoadObject(HenchmanPaths[H % 4], BotDataClass);
                if (!HenchData) continue;
                auto HenchLoop = SyndicateCreatePatrolLoop(PathPoints[HPoint].Loc, 800.0);
                auto UseHenchPath = HenchLoop ? HenchLoop : PathPoints[HPoint].Path;
                printf("[Syndicate][AITest] hench spawn %s loc=%.0f,%.0f,%.0f\n",
                    BossEntries[BossIndex].Needle, PathPoints[HPoint].Loc.X, PathPoints[HPoint].Loc.Y, PathPoints[HPoint].Loc.Z);
                if (SyndicateSpawnExtraHenchman(BotManager, HenchData, PathPoints[HPoint].Loc, PathPoints[HPoint].Rot,
                    UseHenchPath, GameMode, GameState, HumanTeam)) ++MultiSpawned;
            }
        }
        Volumes.Free();
    }

    // The Agency does not always expose a matching POI volume tag; fall back to its known
    // map anchor so Midas + two henchmen still spawn.
    if (!BossUsed[0])
    {
        FVector AgencyAnchor(5632.0, 5120.0, -1860.0);
        int AP = TakeNearest(AgencyAnchor);
        if (AP >= 0)
        {
            auto BossData = BotDataClass ? (UObject*)StaticLoadObject(BossEntries[0].DataPath, BotDataClass) : nullptr;
            auto BossLoop = SyndicateCreatePatrolLoop(PathPoints[AP].Loc, 1000.0);
            printf("[Syndicate][AITest] agency fallback boss loc=%.0f,%.0f,%.0f\n",
                PathPoints[AP].Loc.X, PathPoints[AP].Loc.Y, PathPoints[AP].Loc.Z);
            if (BossData && SyndicateSpawnExtraHenchman(BotManager, BossData, PathPoints[AP].Loc, PathPoints[AP].Rot,
                BossLoop ? BossLoop : PathPoints[AP].Path, GameMode, GameState, HumanTeam))
            {
                ++MultiSpawned;
                ++BossesSpawned;
                BossUsed[0] = true;
            }
            for (int H = 0; H < 2; ++H)
            {
                int HP = TakeNearest(AgencyAnchor);
                if (HP < 0) break;
                auto HenchData = BotDataClass ? (UObject*)StaticLoadObject(HenchmanPaths[H % 4], BotDataClass) : nullptr;
                if (!HenchData) continue;
                auto HenchLoop = SyndicateCreatePatrolLoop(PathPoints[HP].Loc, 800.0);
                if (SyndicateSpawnExtraHenchman(BotManager, HenchData, PathPoints[HP].Loc, PathPoints[HP].Rot,
                    HenchLoop ? HenchLoop : PathPoints[HP].Path, GameMode, GameState, HumanTeam)) ++MultiSpawned;
            }
        }
    }

    if (BossesSpawned > 0)
    {
        printf("[Syndicate][AITest] multi-poi spawned bosses=%d bots=%d\n", BossesSpawned, MultiSpawned);
        return MultiSpawned > 0;
    }

    auto PathClass = FindClass("FortAthenaPatrolPath");
    TArray<AActor*> Paths;
    if (PathClass) Utils::GetAll(PathClass, Paths);

    FVector Anchor(5632.0, 5120.0, -1860.0);
    struct FCandidate { AActor* Path; FVector Loc; FRotator Rot; double Dist; };
    std::vector<FCandidate> Candidates;
    std::vector<FCandidate> AllPoints;
    for (int I = 0; I < Paths.Num(); ++I)
    {
        auto A = Paths[I]; if (!A) continue;
        auto PointsOff = A->GetOffset("PatrolPoints");
        if (PointsOff == uint32(-1)) continue;
        auto& Pts = GetFromOffset<TArray<AActor*>>(A, PointsOff);
        if (Pts.Num() == 0 || !Pts[0]) continue;
        auto L = Pts[0]->K2_GetActorLocation();
        double DX = L.X - Anchor.X, DY = L.Y - Anchor.Y;
        double D = sqrt(DX * DX + DY * DY);
        FCandidate Cand{ A, L, Pts[0]->K2_GetActorRotation(), D };
        AllPoints.push_back(Cand);
        // Keep spawns off the exact POI center; use the patrol points around the building.
        if (D >= 300.0 && D <= 7000.0) Candidates.push_back(Cand);
    }
    printf("[Syndicate][AITest] deferred paths=%d candidates=%d\n", Paths.Num(), (int)Candidates.size());
    std::sort(Candidates.begin(), Candidates.end(), [](const FCandidate& A, const FCandidate& B) { return A.Dist < B.Dist; });
    std::sort(AllPoints.begin(), AllPoints.end(), [](const FCandidate& A, const FCandidate& B) { return A.Dist < B.Dist; });

    std::vector<FCandidate> Chosen;
    for (int I = 0; I < Candidates.size() && Chosen.size() < 4; ++I)
    {
        bool TooClose = false;
        for (auto& C : Chosen)
        {
            double DX = Candidates[I].Loc.X - C.Loc.X, DY = Candidates[I].Loc.Y - C.Loc.Y;
            if (DX * DX + DY * DY < 350.0 * 350.0) { TooClose = true; break; }
        }
        if (!TooClose) Chosen.push_back(Candidates[I]);
    }
    // If the filtered set was empty, fall back to any paths with points.
    if (Chosen.empty())
        for (int I = 0; I < AllPoints.size() && Chosen.size() < 4; ++I) Chosen.push_back(AllPoints[I]);

    if (Chosen.empty())
    {
        printf("[Syndicate][AITest] deferred spawn: no patrol points found\n");
        Paths.Free();
        return false;
    }

    const wchar_t* BotPaths[] = {
        L"/Game/Athena/AI/MANG/BotData/BotData_MANG_Sniper_Ego.BotData_MANG_Sniper_Ego",
        L"/Game/Athena/AI/MANG/BotData/BotData_MANG_Assault_Ego.BotData_MANG_Assault_Ego",
        L"/Game/Athena/AI/MANG/BotData/BotData_MANG_SMG_Ego.BotData_MANG_SMG_Ego",
        L"/Game/Athena/AI/MANG/BotData/BotData_MANG_POI_Agency.BotData_MANG_POI_Agency"
    };
    int Spawned = 0;
    for (int I = 0; I < (int)Chosen.size() && I < 4; ++I)
    {
        auto Data = BotDataClass ? (UObject*)StaticLoadObject(BotPaths[I], BotDataClass) : nullptr;
        if (!Data) continue;
        auto Loop = SyndicateCreatePatrolLoop(Chosen[I].Loc, 900.0);
        auto UsePath = Loop ? Loop : Chosen[I].Path;
        printf("[Syndicate][AITest] deferred spawn[%d] path=%s loc=%.0f,%.0f,%.0f\n", I,
            UsePath->Name.ToString().c_str(), Chosen[I].Loc.X, Chosen[I].Loc.Y, Chosen[I].Loc.Z);
        if (SyndicateSpawnExtraHenchman(BotManager, Data, Chosen[I].Loc, Chosen[I].Rot,
            UsePath, GameMode, GameState, HumanTeam)) ++Spawned;
    }
    Paths.Free();
    printf("[Syndicate][AITest] deferred spawned=%d\n", Spawned);
    return Spawned > 0;
}

// The manager-spawned pawn uses the default locomotion animation. Apply the MANG patrol
// anim layer so the custom walking animation is used.
static void SyndicateApplyMangAnim(AFortPlayerPawnAthena* P)
{
    if (!P) return;
    auto MeshOff = P->GetOffset("Mesh");
    if (MeshOff == uint32(-1)) { printf("[Syndicate][AITest] anim mesh offset missing\n"); return; }
    auto Mesh = GetFromOffset<UObject*>(P, MeshOff);
    auto Anim = StaticLoadObject(
        L"/Game/Athena/AI/MANG/AnimSet/MANG_PatrolLayerAnimBP.MANG_PatrolLayerAnimBP_C",
        UClass::StaticClass());
    if (!Mesh || !Anim)
    {
        printf("[Syndicate][AITest] anim mesh=%s anim=%s\n",
            Mesh ? Mesh->Name.ToString().c_str() : "null",
            Anim ? "ok" : "null");
        return;
    }
    // MANG_PatrolLayerAnimBP is an animation *layer*, not a full base AnimBP. Replacing the
    // base AnimClass left the normal player animation; link the layer instead.
    bool Linked = false;
    if (auto Fn = Mesh->GetFunction("LinkAnimClassLayers"))
    {
        Mesh->Call<void>(Fn, Anim);
        Linked = true;
    }
    if (auto Fn = P->GetFunction("OnRep_AnimBPOverride")) P->ProcessEvent(Fn, nullptr);
    printf("[Syndicate][AITest] anim linked=%d mesh=%s anim=%s\n",
        (int)Linked, Mesh->Name.ToString().c_str(), Anim->Name.ToString().c_str());
}

// Hard-stop the pawn so a question-mark stare does not slide on residual velocity.
// While falling, only kill horizontal speed: zeroing the whole velocity every tick was
// cancelling gravity and making airborne henchmen/bosses float down slowly.
static void SyndicateStopPawn(AFortPlayerPawnAthena* P)
{
    if (!P || !P->CharacterMovement) return;
    bool Falling = false;
    if (auto Off = P->CharacterMovement->GetOffset("MovementMode"); Off != uint32(-1))
        Falling = GetFromOffset<uint8>(P->CharacterMovement, Off) == 3; // MOVE_Falling
    if (auto Off = P->CharacterMovement->GetOffset("Velocity"); Off != uint32(-1))
    {
        auto& V = GetFromOffset<FVector>(P->CharacterMovement, Off);
        if (Falling)
        {
            V.X = 0.0;
            V.Y = 0.0;
            return;
        }
    }
    if (auto Fn = P->CharacterMovement->GetFunction("StopMovementImmediately"))
        P->CharacterMovement->ProcessEvent(Fn, nullptr);
    if (auto Off = P->CharacterMovement->GetOffset("Velocity"); Off != uint32(-1))
        GetFromOffset<FVector>(P->CharacterMovement, Off) = FVector{};
}

// A player still on the bus, skydiving or gliding must not be a ground target: otherwise
// henchmen aim and fire at them before they have landed.
static bool SyndicatePawnIsAirborne(AFortPlayerPawnAthena* P)
{
    if (!P) return true;
    if (auto C = P->Controller)
        if (auto Fn = C->GetFunction("IsInAircraft"))
            if (C->Call<bool>(Fn)) return true;
    for (const char* Name : { "bIsSkydiving", "bIsParachuteOpen" })
    {
        auto Prop = P->GetProperty(Name);
        auto Off = P->GetOffset(Name);
        if (Prop && Off != uint32(-1))
            if (GetFromOffset<uint8>(P, Off) & Prop->GetFieldMask()) return true;
    }
    if (P->CharacterMovement)
        if (auto Off = P->CharacterMovement->GetOffset("MovementMode"); Off != uint32(-1))
            if (GetFromOffset<uint8>(P->CharacterMovement, Off) == 3) return true; // MOVE_Falling
    return false;
}

// Once a henchman is Threatened (red exclamation mark), walk it toward the player it is
// targeting instead of leaving it rooted in place. The native move requests are unreliable
// in this build, so drive AddMovementInput directly, like the patrol fallback below.
static bool SyndicateChaseHenchman(SyndicateHenchmanRecord& B, AActor* PC, AFortPlayerPawnAthena* P,
    AFortPlayerPawnAthena* Target, double StopDistance, bool AlwaysMove = false)
{
    if (!PC || !P || !Target) return false;
    auto Loc = P->K2_GetActorLocation();
    auto TL = Target->K2_GetActorLocation();
    double DX = TL.X - Loc.X, DY = TL.Y - Loc.Y;
    double Flat = sqrt(DX * DX + DY * DY);
    if (Flat < 1.0) return false;
    if (!AlwaysMove && Flat <= StopDistance) return false;

    // Patrol leaves MaxWalkSpeed at the slow walk value; alert should close the distance.
    if (P->CharacterMovement)
        if (auto Off = P->CharacterMovement->GetOffset("MaxWalkSpeed"); Off != uint32(-1))
            GetFromOffset<float>(P->CharacterMovement, Off) = 450.f;

    FVector Dir(DX / Flat, DY / Flat, 0.0);
    if (auto Fn = P->GetFunction("AddMovementInput"))
        P->Call<void>(Fn, Dir, 1.0f, true);

    FRotator Look{};
    Look.Yaw = (float)(atan2(DY, DX) * 180.0 / 3.141592653589793);
    if (auto Fn = PC->GetFunction("SetControlRotation")) PC->Call<void>(Fn, Look);
    FRotator Body{}; Body.Yaw = Look.Yaw;
    if (auto Fn = P->GetFunction("K2_SetActorRotation")) P->Call<bool>(Fn, Body, true);

    // Boss pawns can ignore AddMovementInput; if the pawn has not actually moved for ~1 s,
    // step it directly toward the player.
    auto NowLoc = P->K2_GetActorLocation();
    double MDX = NowLoc.X - B.DriveLastLoc.X, MDY = NowLoc.Y - B.DriveLastLoc.Y, MDZ = NowLoc.Z - B.DriveLastLoc.Z;
    double Moved = sqrt(MDX * MDX + MDY * MDY + MDZ * MDZ);
    if (Moved < 0.5) ++B.StuckTicks; else B.StuckTicks = 0;
    B.DriveLastLoc = NowLoc;
    if (B.StuckTicks > 30)
    {
        FVector Step(NowLoc.X + Dir.X * 6.0, NowLoc.Y + Dir.Y * 6.0, NowLoc.Z);
        if (auto Fn = P->GetFunction("K2_SetActorLocation"))
            P->Call<bool>(Fn, Step, true, (void*)nullptr, true);
        B.StuckTicks = 0;
    }
    return true;
}

// Ask the local backend for the account's current season level. The server-side XP
// component is not initialised with the profile level in 12.50, so the top-left badge
// stayed at 1; the profile is the source of truth.
static size_t SyndicateCurlWrite(char* Ptr, size_t Size, size_t Nmemb, void* Userdata)
{
    ((std::string*)Userdata)->append(Ptr, Size * Nmemb);
    return Size * Nmemb;
}
static int SyndicateFetchProfileLevel(const std::string& PlayerName)
{
    if (PlayerName.empty()) return 0;
    auto Curl = curl_easy_init();
    if (!Curl) return 0;
    std::string Url = "http://127.0.0.1:3551/tilted/level/";
    if (char* Escaped = curl_easy_escape(Curl, PlayerName.c_str(), (int)PlayerName.size()))
    {
        Url += Escaped;
        curl_free(Escaped);
    }
    else { curl_easy_cleanup(Curl); return 0; }
    std::string Body;
    curl_easy_setopt(Curl, CURLOPT_URL, Url.c_str());
    curl_easy_setopt(Curl, CURLOPT_WRITEFUNCTION, SyndicateCurlWrite);
    curl_easy_setopt(Curl, CURLOPT_WRITEDATA, &Body);
    curl_easy_setopt(Curl, CURLOPT_CONNECTTIMEOUT_MS, 200L);
    curl_easy_setopt(Curl, CURLOPT_TIMEOUT_MS, 400L);
    curl_easy_setopt(Curl, CURLOPT_NOSIGNAL, 1L);
    const CURLcode Result = curl_easy_perform(Curl);
    curl_easy_cleanup(Curl);
    if (Result != CURLE_OK) return 0;
    auto Pos = Body.find("\"level\"");
    if (Pos == std::string::npos) return 0;
    Pos = Body.find(':', Pos);
    if (Pos == std::string::npos) return 0;
    return (std::max)(0, atoi(Body.c_str() + Pos + 1));
}
struct SyndicateLevelSyncState { int Level = 0; ULONGLONG Next = 0; };
static std::unordered_map<AFortPlayerStateAthena*, SyndicateLevelSyncState> SyndicateLevelSyncCache;

// Set the native alert level (and fire its blueprint event) so the black question mark /
// red exclamation marker above the henchman matches the manual aggro.
static void SyndicateSetHenchmanAlert(AActor* PC, uint8 NewLevel)
{
    if (!PC) return;
    auto Off = PC->GetOffset("CurrentAlertLevel");
    if (Off == uint32(-1)) return;
    auto& Alert = GetFromOffset<uint8>(PC, Off);
    if (Alert == NewLevel) return;
    uint8 Old = Alert;
    Alert = NewLevel;
    if (auto Fn = PC->GetFunction("OnAlertLevelChanged"))
    {
        struct { uint8 OldLevel; uint8 NewLevel; } Params{Old, NewLevel};
        PC->ProcessEvent(Fn, &Params);
    }
    PC->ForceNetUpdate();
}

// A player downing or killing a henchman/boss in front of other henchmen draws a reaction
// from at most two witnesses: the closest ones actually facing the victim with line of sight.
static void SyndicateAlertKillWitnesses(SyndicateHenchmanRecord& Victim, AFortPlayerPawnAthena* Attacker,
    const FVector& VictimLoc, ULONGLONG Now)
{
    if (!Attacker) return;
    struct FCandidate { SyndicateHenchmanRecord* Rec; double Dist2; };
    std::vector<FCandidate> Candidates;
    auto VictimPawn = Victim.Pawn;

    for (auto& H : SyndicateHenchmen)
    {
        if (&H == &Victim || !H.Pawn || H.Pawn->bActorIsBeingDestroyed || !H.BossKey.empty()) continue;
        auto L = H.Pawn->K2_GetActorLocation();
        double DX = L.X - VictimLoc.X, DY = L.Y - VictimLoc.Y;
        double D2 = DX * DX + DY * DY;
        if (D2 > 3500.0 * 3500.0) continue;
        if (fabs(L.Z - VictimLoc.Z) > 1200.0) continue;

        // "In front of them": only a henchman facing the victim notices the kill.
        FVector Fwd{};
        if (auto Fn = H.Pawn->GetFunction("GetActorForwardVector")) Fwd = H.Pawn->Call<FVector>(Fn);
        double FL = sqrt(Fwd.X * Fwd.X + Fwd.Y * Fwd.Y);
        double D = sqrt(D2);
        if (FL > 0.1 && D > 1.0)
        {
            double Dot = (DX * Fwd.X + DY * Fwd.Y) / (FL * D);
            if (Dot < 0.2) continue;
        }

        auto PC = (AActor*)H.Pawn->Controller;
        if (!PC) continue;
        // A downed/dead henchman cannot react, and already-alerted ones do not count as
        // new witnesses for this kill.
        if (auto Fn = H.Pawn->GetFunction("IsDBNO"))
            if (H.Pawn->Call<bool>(Fn)) continue;
        if (auto Fn = H.Pawn->GetFunction("GetHealth"))
            if (H.Pawn->Call<float>(Fn) <= 0.f) continue;
        if (auto Off = PC->GetOffset("CurrentAlertLevel"); Off != uint32(-1))
            if (GetFromOffset<uint8>(PC, Off) >= 3) continue;
        auto LOSFn = PC->GetFunction("LineOfSightTo");
        if (!LOSFn) continue;
        FVector Eye(L.X, L.Y, L.Z + 60.0);
        bool LOS = VictimPawn ? PC->Call<bool>(LOSFn, VictimPawn, Eye, false) : false;
        if (!LOS && Attacker)
            LOS = PC->Call<bool>(LOSFn, Attacker, Eye, false);
        if (!LOS) continue;
        Candidates.push_back({ &H, D2 });
    }

    std::sort(Candidates.begin(), Candidates.end(), [](auto& A, auto& B) { return A.Dist2 < B.Dist2; });
    int Alerts = 0;
    for (auto& C : Candidates)
    {
        if (Alerts >= 2) break;
        auto& H = *C.Rec;
        H.WitnessAlerted = true;
        H.ForcedTarget = Attacker;
        H.ForcedTargetUntil = Now + 10000;
        H.ForcedAlertUntil = Now + 10000;
        H.NoLosSince = 0;
        H.LoseAggroUntil = 0;
        H.CloseSince = 0;
        SyndicateSetHenchmanAlert((AActor*)H.Pawn->Controller, 3);
        ++Alerts;
    }
    if (Alerts > 0)
        printf("[Syndicate][AITest] witness alert victim=%s witnesses=%d\n",
            Victim.BossKey.empty() ? "henchman" : Victim.BossKey.c_str(), Alerts);
}

static AFortPlayerPawnAthena* SyndicateNearestHumanTo(const FVector& Loc, double MaxDist)
{
    AFortPlayerPawnAthena* Best = nullptr;
    double BestD = MaxDist * MaxDist;
    for (auto HP : SyndicateHumanPawns)
    {
        if (!HP || SyndicatePawnIsAirborne(HP)) continue;
        if (auto Fn = HP->GetFunction("IsDBNO"))
            if (HP->Call<bool>(Fn)) continue;
        if (auto Fn = HP->GetFunction("GetHealth"))
            if (HP->Call<float>(Fn) <= 0.f) continue;
        auto HL = HP->K2_GetActorLocation();
        double DX = HL.X - Loc.X, DY = HL.Y - Loc.Y;
        double D = DX * DX + DY * DY;
        if (D < BestD) { BestD = D; Best = HP; }
    }
    return Best;
}

// Manual fallback while the native AIEvaluators are unavailable: walk the pawn toward the
// next patrol point, and if a human is nearby, aim the controller and fire the weapon.
static void SyndicateDriveHenchman(SyndicateHenchmanRecord& B, AFortPlayerPawnAthena* P)
{
    auto PC = P ? (AActor*)P->Controller : nullptr;
    if (!PC) return;
    auto Loc = P->K2_GetActorLocation();

    // Cache the hot UFunction pointers; GetFunction walks the class chain by name.
    static UFunction* FnSetControl = nullptr;
    static UFunction* FnSetBodyRot = nullptr;
    static UFunction* FnAddMove = nullptr;
    static UFunction* FnForward = nullptr;
    static UFunction* FnStartFire = nullptr;
    static UFunction* FnStopFire = nullptr;
    static UFunction* FnSetLoc = nullptr;
    if (!FnSetControl) FnSetControl = PC->GetFunction("SetControlRotation");
    if (!FnSetBodyRot) FnSetBodyRot = P->GetFunction("K2_SetActorRotation");
    if (!FnAddMove) FnAddMove = P->GetFunction("AddMovementInput");
    if (!FnForward) FnForward = P->GetFunction("GetActorForwardVector");
    if (!FnStartFire) FnStartFire = P->GetFunction("PawnStartFire");
    if (!FnStopFire) FnStopFire = P->GetFunction("PawnStopFire");
    if (!FnSetLoc) FnSetLoc = P->GetFunction("K2_SetActorLocation");

    uint8 Alert = 0;
    if (auto Off = PC->GetOffset("CurrentAlertLevel"); Off != uint32(-1))
        Alert = GetFromOffset<uint8>(PC, Off);
    // Forced aggro (proximity, boss guards, damage) must also show the red exclamation.
    if (GetTickCount64() < B.ForcedAlertUntil && Alert < 3)
    {
        SyndicateSetHenchmanAlert(PC, 3);
        Alert = 3;
    }
    if (Alert == 0) B.WitnessAlerted = false;

    // Patrol-only pace: a slow henchman walk. TILTED_HENCHMAN_SPEED overrides the default.
    static const float PatrolSpeed = [] {
        wchar_t B[16]{};
        float V = 120.f;
        if (GetEnvironmentVariableW(L"TILTED_HENCHMAN_SPEED", B, 16))
        {
            V = (float)_wtof(B);
            if (V < 50.f) V = 50.f;
            if (V > 600.f) V = 600.f;
        }
        return V;
    }();
    if (Alert < 3 && P->CharacterMovement)
    {
        if (auto Off = P->CharacterMovement->GetOffset("MaxWalkSpeed"); Off != uint32(-1))
            GetFromOffset<float>(P->CharacterMovement, Off) = PatrolSpeed;
        if (auto Off = P->CharacterMovement->GetOffset("MaxWalkSpeedCrouched"); Off != uint32(-1))
            GetFromOffset<float>(P->CharacterMovement, Off) = PatrolSpeed + 20.f;
        if (auto Off = P->CharacterMovement->GetOffset("Velocity"); Off != uint32(-1))
        {
            auto& V = GetFromOffset<FVector>(P->CharacterMovement, Off);
            double HS = sqrt(V.X * V.X + V.Y * V.Y);
            if (HS > PatrolSpeed) { V.X *= PatrolSpeed / HS; V.Y *= PatrolSpeed / HS; }
        }
    }

    AFortPlayerPawnAthena* Target = nullptr;
    double BestDist = 4000.0 * 4000.0;
    auto Forced = B.ForcedTarget.Get();
    if (Forced && GetTickCount64() < B.ForcedTargetUntil && !SyndicatePawnIsAirborne(Forced))
    {
        Target = Forced;
        auto L = Forced->K2_GetActorLocation();
        double DX = L.X - Loc.X, DY = L.Y - Loc.Y;
        BestDist = DX * DX + DY * DY;
    }
    else
    {
        for (auto HumanPawn : SyndicateHumanPawns)
        {
            if (!HumanPawn || SyndicatePawnIsAirborne(HumanPawn)) continue;
            auto L = HumanPawn->K2_GetActorLocation();
            double DX = L.X - Loc.X, DY = L.Y - Loc.Y;
            double D = DX * DX + DY * DY;
            if (D < BestDist) { BestDist = D; Target = HumanPawn; }
        }
    }

    // Only engage at Threatened (the red exclamation mark). Alerted/LKP (question mark)
    // keep patrolling. Also require actual line of sight and a close target so distant
    // group members do not all open fire at once.
    bool HasLOS = false;
    if (Target)
        if (auto Fn = PC->GetFunction("LineOfSightTo"))
            HasLOS = PC->Call<bool>(Fn, Target, FVector(Loc.X, Loc.Y, Loc.Z + 60.0), false);

    ULONGLONG Now = GetTickCount64();

    // The native group alert can mark every henchman in a POI. Only a henchman that can
    // actually see the player (or was explicitly alerted by damage/proximity/a witness)
    // should track them; the rest keep their alert marker but do not home in through walls.
    bool HasForcedTarget = false;
    if (auto F = B.ForcedTarget.Get())
        if (F == Target && Now < B.ForcedTargetUntil) HasForcedTarget = true;
    if (Alert >= 3 && Target && !HasLOS && !HasForcedTarget)
        Target = nullptr;

    // Lose aggro after 10 s without the player in view, then patrol for a few seconds
    // even if the player is still close (for example behind a wall).
    if (Target && HasLOS) B.NoLosSince = 0;
    else if (B.NoLosSince == 0) B.NoLosSince = Now;
    if (B.NoLosSince != 0 && Now - B.NoLosSince >= 10000)
    {
        B.NoLosSince = 0;
        B.LoseAggroUntil = Now + 5000;
        B.Engaged = false;
        B.BurstLeft = 0;
        B.AimReady = 0;
        B.NextShot = 0;
        B.SuspiciousUntil = 0;
        B.ForcedAlertUntil = 0;
        if (FnStopFire) P->Call<void>(FnStopFire, (uint8)0);
        SyndicateSetHenchmanAlert(PC, 0);
        Alert = 0;
        B.WitnessAlerted = false;
    }
    if (Now < B.LoseAggroUntil)
    {
        Target = nullptr;
        B.Engaged = false;
        B.BurstLeft = 0;
        B.AimReady = 0;
        if (FnStopFire) P->Call<void>(FnStopFire, (uint8)0);
        SyndicateSetHenchmanAlert(PC, 0);
        Alert = 0;
        B.WitnessAlerted = false;
    }

    // Standing within 3 m of a henchman for 5 seconds makes it notice and engage.
    AFortPlayerPawnAthena* CloseHuman = nullptr;
    double CloseDist2 = 300.0 * 300.0;
    for (auto HumanPawn : SyndicateHumanPawns)
    {
        if (!HumanPawn || SyndicatePawnIsAirborne(HumanPawn)) continue;
        auto CL = HumanPawn->K2_GetActorLocation();
        double CX = CL.X - Loc.X, CY = CL.Y - Loc.Y;
        double CD = CX * CX + CY * CY;
        if (CD <= CloseDist2) { CloseDist2 = CD; CloseHuman = HumanPawn; }
    }
    if (CloseHuman)
    {
        if (B.CloseSince == 0) B.CloseSince = Now;
        if (Now - B.CloseSince >= 5000)
        {
            B.ForcedTarget = CloseHuman;
            B.ForcedTargetUntil = Now + 10000;
            B.ForcedAlertUntil = Now + 10000;
            B.NoLosSince = 0;
            B.LoseAggroUntil = 0;
            B.CloseSince = 0;
            Target = CloseHuman;
            SyndicateSetHenchmanAlert(PC, 3);
            Alert = 3;
            if (auto Fn = PC->GetFunction("LineOfSightTo"))
                HasLOS = PC->Call<bool>(Fn, Target, FVector(Loc.X, Loc.Y, Loc.Z + 60.0), false);
        }
    }
    else B.CloseSince = 0;

    // Question-mark states: stop and stare at the player for a couple of seconds. If the
    // alert does not escalate to Threatened, resume patrol.
    if (Target && Alert >= 1 && Alert < 3 && Now >= B.ForcedAlertUntil)
    {
        if (B.SuspiciousUntil == 0) B.SuspiciousUntil = Now + 2200;
        if (Now < B.SuspiciousUntil)
        {
            auto TL = Target->K2_GetActorLocation();
            double DX = TL.X - Loc.X, DY = TL.Y - Loc.Y;
            FRotator Look{};
            Look.Yaw = (float)(atan2(DY, DX) * 180.0 / 3.141592653589793);
            if (FnSetControl) PC->Call<void>(FnSetControl, Look);
            FRotator Body{}; Body.Yaw = Look.Yaw;
            if (FnSetBodyRot) P->Call<bool>(FnSetBodyRot, Body, true);
            B.Engaged = false;
            B.BurstLeft = 0;
            SyndicateStopPawn(P);
            return;
        }
    }
    else if (Alert == 0)
    {
        B.SuspiciousUntil = 0;
    }

    // A player standing right behind a patrolling henchman should be noticed: stop and
    // face them. No shooting until the native alert or the damage reaction fires.
    if (Target && Alert < 3 && Now >= B.ForcedAlertUntil && BestDist <= 900.0 * 900.0)
    {
        auto TL0 = Target->K2_GetActorLocation();
        double DX0 = TL0.X - Loc.X, DY0 = TL0.Y - Loc.Y;
        FRotator Look0{};
        Look0.Yaw = (float)(atan2(DY0, DX0) * 180.0 / 3.141592653589793);
        if (FnSetControl) PC->Call<void>(FnSetControl, Look0);
        FRotator Body0{}; Body0.Yaw = Look0.Yaw;
        if (FnSetBodyRot) P->Call<bool>(FnSetBodyRot, Body0, true);
        SyndicateStopPawn(P);
        B.Engaged = false;
        B.BurstLeft = 0;
        return;
    }

    if (Alert >= 3 || Now < B.ForcedAlertUntil)
    {
        if (!B.Engaged)
        {
            B.Engaged = true;
            B.PauseUntil = Now + 600; // reaction delay before the first burst
            B.BurstLeft = 0;
            B.NextShot = 0;
        }

        if (!Target)
        {
            SyndicateStopPawn(P);
            B.BurstLeft = 0;
            B.AimReady = 0;
            if (FnStopFire) P->Call<void>(FnStopFire, (uint8)0);
            return;
        }

        // Threatened henchmen advance on the player instead of standing rooted. They keep
        // closing in while they have no shot, then stop around 5 m away and fight.
        auto TL = Target->K2_GetActorLocation();
        double DX = TL.X - Loc.X, DY = TL.Y - Loc.Y;
        double Flat = sqrt(DX * DX + DY * DY);
        const double ChaseStop = 500.0;
        bool Advancing = (Flat > ChaseStop) || !HasLOS;
        if (Advancing)
            SyndicateChaseHenchman(B, PC, P, Target, ChaseStop, !HasLOS);
        else
            SyndicateStopPawn(P);

        if (!HasLOS)
        {
            // No line of sight yet: keep moving toward the player and re-check next tick.
            B.BurstLeft = 0;
            B.AimReady = 0;
            if (FnStopFire) P->Call<void>(FnStopFire, (uint8)0);
            return;
        }

        TL.Z += 45.0;
        DX = TL.X - Loc.X; DY = TL.Y - Loc.Y;
        Flat = sqrt(DX * DX + DY * DY);
        if (Flat < 1.0) Flat = 1.0;
        FRotator Look{};
        Look.Yaw = (float)(atan2(DY, DX) * 180.0 / 3.141592653589793);
        Look.Pitch = (float)(atan2(TL.Z - Loc.Z, Flat) * 180.0 / 3.141592653589793);

        // Wide per-shot spread; henchmen are deliberately sloppy.
        bool IsBoss = B.BotData && std::string(B.BotData->Name.ToString().c_str()).find("POI") != std::string::npos;
        std::string WeaponName = P->CurrentWeapon ? std::string(P->CurrentWeapon->Name.ToString().c_str()) : "";
        bool IsSmg = WeaponName.find("SMG") != std::string::npos ||
                     WeaponName.find("Submachine") != std::string::npos ||
                     WeaponName.find("Drum") != std::string::npos ||
                     (WeaponName.find("Suppressed") != std::string::npos && WeaponName.find("Sniper") == std::string::npos);
        float Spread = IsBoss ? 25.0f : 18.0f;
        // At point-blank range keep some spread. SMGs were deleting players instantly, so they
        // stay deliberately loose even when the target is close.
        if (Flat < 500.0)
            Spread = IsSmg ? (IsBoss ? 11.0f : 9.0f) : (IsBoss ? 8.0f : 5.0f);
        else if (IsSmg)
            Spread = IsBoss ? 22.0f : 16.0f;
        unsigned long long Seed = GetTickCount64() >> 3;
        float Sy = (float)((int)(Seed % 2001) - 1000) / 1000.0f * Spread;
        float Sp = (float)((int)((Seed / 2001) % 2001) - 1000) / 1000.0f * Spread;
        Look.Yaw += Sy;
        Look.Pitch += Sp;
        if (FnSetControl) PC->Call<void>(FnSetControl, Look);
        // Only yaw the body; pitching the actor makes the whole character lean upward.
        FRotator Body{}; Body.Yaw = Look.Yaw;
        if (FnSetBodyRot) P->Call<bool>(FnSetBodyRot, Body, true);

        if (Now < B.PauseUntil)
        {
            // Reaction/burst pause: keep advancing but hold fire.
            B.AimReady = 0;
            if (FnStopFire) P->Call<void>(FnStopFire, (uint8)0);
            return;
        }
        else
        {
            // Plant the feet only once inside combat range; otherwise keep walking while
            // turning toward the target. This prevents shots into the wall while turning.
            if (!Advancing) SyndicateStopPawn(P);
            if (B.AimReady == 0) B.AimReady = Now + 150;
            bool Facing = true;
            if (FnForward)
            {
                auto Fwd = P->Call<FVector>(FnForward);
                double FL = sqrt(Fwd.X * Fwd.X + Fwd.Y * Fwd.Y);
                if (FL > 0.1)
                {
                    double Dot = (DX * Fwd.X + DY * Fwd.Y) / (FL * Flat);
                    Facing = Dot > 0.5;
                }
            }

            if (Now >= B.AimReady && Facing)
            {
                int Burst = 7;
                int Interval = 120;
                if (WeaponName.find("Sniper") != std::string::npos) { Burst = 1; Interval = 750; }
                else if (WeaponName.find("Shotgun") != std::string::npos) { Burst = 2; Interval = 850; }
                else if (WeaponName.find("Pistol") != std::string::npos) { Burst = 4; Interval = 260; }
                else if (IsSmg) { Burst = 5; Interval = 180; }
                if (IsBoss) Interval += 60;
                B.BurstSize = Burst;

                if (B.BurstLeft <= 0)
                {
                    B.BurstLeft = B.BurstSize;
                    B.NextShot = Now;
                }
                if (B.BurstLeft > 0 && Now >= B.NextShot)
                {
                    if (FnStartFire) P->Call<void>(FnStartFire, (uint8)0);
                    --B.BurstLeft;
                    B.NextShot = Now + Interval;
                    if (B.BurstLeft == 0)
                    {
                        if (FnStopFire) P->Call<void>(FnStopFire, (uint8)0);
                        B.PauseUntil = Now + (IsBoss ? 1400 : (IsSmg ? 1250 : 1000));
                        B.AimReady = 0;
                    }
                }
            }
            return;
        }
    }
    else
    {
        B.Engaged = false;
        B.BurstLeft = 0;
        B.AimReady = 0;
    }

    if (!B.PatrolPath) return;
    auto PointsOff = B.PatrolPath->GetOffset("PatrolPoints");
    if (PointsOff == uint32(-1)) return;
    auto& Pts = GetFromOffset<TArray<AActor*>>(B.PatrolPath, PointsOff);
    if (Pts.Num() == 0) return;
    if (B.PatrolIndex < 0) B.PatrolIndex = 0;
    auto Point = Pts[B.PatrolIndex % Pts.Num()];
    if (!Point) { ++B.PatrolIndex; return; }
    auto TL = Point->K2_GetActorLocation();
    double DX = TL.X - Loc.X, DY = TL.Y - Loc.Y, DZ = TL.Z - Loc.Z;
    double Dist = sqrt(DX * DX + DY * DY + DZ * DZ);
    if (Dist < 180.0) { ++B.PatrolIndex; return; }
    if (Dist < 1.0) return;

    FVector Dir(DX / Dist, DY / Dist, 0.0);
    if (FnAddMove) P->Call<void>(FnAddMove, Dir, 1.0f, true);
    FRotator Look{};
    Look.Yaw = (float)(atan2(DY, DX) * 180.0 / 3.141592653589793);
    if (FnSetControl) PC->Call<void>(FnSetControl, Look);

    // Boss pawns use a different movement component and can ignore AddMovementInput.
    // If the pawn has not actually moved for ~1s, step it directly along the patrol line.
    auto NowLoc = P->K2_GetActorLocation();
    double MDX = NowLoc.X - B.DriveLastLoc.X, MDY = NowLoc.Y - B.DriveLastLoc.Y, MDZ = NowLoc.Z - B.DriveLastLoc.Z;
    double Moved = sqrt(MDX * MDX + MDY * MDY + MDZ * MDZ);
    if (Moved < 0.5) ++B.StuckTicks; else B.StuckTicks = 0;
    B.DriveLastLoc = NowLoc;
    if (B.StuckTicks > 30)
    {
        FVector Step(NowLoc.X + Dir.X * 6.0, NowLoc.Y + Dir.Y * 6.0, NowLoc.Z);
        if (FnSetLoc)
            P->Call<bool>(FnSetLoc, Step, true, (void*)nullptr, true);
        B.StuckTicks = 0;
    }
}

static UObject* SyndicateBotDataForPawn(const std::string& PN)
{
    struct FMap { const char* Needle; const wchar_t* Path; };
    static const FMap Maps[] = {
        { "Boss_Midas",          L"/Game/Athena/AI/MANG/BotData/BotData_MANG_POI_Agency.BotData_MANG_POI_Agency" },
        { "Boss_HeadOfSecurity", L"/Game/Athena/AI/MANG/BotData/BotData_MANG_POI_MountainBase.BotData_MANG_POI_MountainBase" },
        { "Boss_TnTina",         L"/Game/Athena/AI/MANG/BotData/BotData_MANG_POI_OilRig.BotData_MANG_POI_OilRig" },
        { "Boss_AdventureGirl",  L"/Game/Athena/AI/MANG/BotData/BotData_MANG_POI_SJI.BotData_MANG_POI_SJI" },
        { "Boss_HDP",            L"/Game/Athena/AI/MANG/BotData/BotData_MANG_POI_HDP.BotData_MANG_POI_HDP" },
        { "Boss_Meowscles",      L"/Game/Athena/AI/MANG/BotData/BotData_MANG_POI_Yacht.BotData_MANG_POI_Yacht" },
        { "Default",             L"/Game/Athena/AI/MANG/BotData/BotData_MANG_Assault_Ego.BotData_MANG_Assault_Ego" }
    };
    auto Class = FindClass("FortAthenaAIBotCustomizationData");
    for (auto& M : Maps)
        if (PN.find(M.Needle) != std::string::npos)
            return Class ? (UObject*)StaticLoadObject(M.Path, Class) : nullptr;
    return nullptr;
}

static int SyndicateCurrentHumanTeam()
{
    int HumanTeam = -1;
    if (auto PCClass = FindClass("FortPlayerControllerAthena"))
    {
        TArray<AActor*> Controllers; Utils::GetAll(PCClass, Controllers);
        for (auto A : Controllers)
        {
            auto HPC = (AFortPlayerControllerAthena*)A;
            auto HPS = HPC ? (AFortPlayerStateAthena*)HPC->PlayerState : nullptr;
            if (HPC && HPC->MyFortPawn && HPS && !(HPS->HasbIsABot() && HPS->bIsABot))
            { HumanTeam = HPS->TeamIndex; break; }
        }
        Controllers.Free();
    }
    return HumanTeam;
}

// Adopt the bots created by the native BP_MANG_Spawner actors: keep their native pawn/
// appearance, give them a weapon and inventory, then let the manual drive handle patrol and
// combat. Duplicate boss classes are destroyed so POIs do not show two of the same boss.
static int SyndicateAdoptNativeBots()
{
    auto CtrlClass = FindClass("FortAthenaAIBotController");
    if (!CtrlClass) return 0;
    TArray<AActor*> Ctrls;
    Utils::GetAll(CtrlClass, Ctrls);
    std::set<std::string> SeenBoss;
    for (auto& R : SyndicateHenchmen)
        if (!R.BossKey.empty()) SeenBoss.insert(R.BossKey);
    int Adopted = 0;
    for (int I = 0; I < Ctrls.Num(); ++I)
    {
        auto PC = Ctrls[I]; if (!PC) continue;
        auto PawnOff = PC->GetOffset("Pawn");
        if (PawnOff == uint32(-1)) continue;
        auto P = (AFortPlayerPawnAthena*)GetFromOffset<AActor*>(PC, PawnOff);
        if (!P) continue;
        std::string PN = std::string(P->Class->Name.ToString().c_str());
        std::string CN = std::string(PC->Class->Name.ToString().c_str());
        if (PN.find("MangPlayerPawn") == std::string::npos && CN.find("MangPlayerController") == std::string::npos)
            continue;

        bool Tracked = false;
        for (auto& R : SyndicateHenchmen) if (R.Pawn == P) { Tracked = true; break; }
        if (Tracked) continue;

        bool IsBoss = PN.find("Boss_") != std::string::npos;
        std::string BossKey;
        if (IsBoss)
        {
            // Key duplicates by the native display name. All boss pawns share the same
            // controller class, and in some builds the same pawn base class, so the class
            // name alone would wrongly collapse different bosses into one.
            if (auto PS = (AFortPlayerStateAthena*)P->PlayerState)
                if (auto Fn = PS->GetFunction("GetPlayerName"))
                    BossKey = std::string(PS->Call<FString>(Fn).ToString().c_str());
            if (BossKey.empty()) BossKey = PN;

            // Meowscles is duplicated at the Yacht by the HDP/Deadpool spawner; keep Deadpool
            // there and drop only the Yacht copy. A Meowscles spawned at Catty Corner is kept.
            bool IsMeowscles = PN.find("Boss_Meowscles") != std::string::npos;
            std::string BossKeyLow;
            for (char C : BossKey) BossKeyLow += (char)tolower((unsigned char)C);
            if (BossKeyLow.find("meowscles") != std::string::npos) IsMeowscles = true;
            if (IsMeowscles)
            {
                auto PL = P->K2_GetActorLocation();
                const FVector Yacht(113678.0, 113656.0, -2264.0);
                double YDX = PL.X - Yacht.X, YDY = PL.Y - Yacht.Y;
                if (YDX * YDX + YDY * YDY < 12000.0 * 12000.0)
                {
                    printf("[Syndicate][AITest] duplicate Meowscles removed at Yacht\n");
                    P->K2_DestroyActor();
                    PC->K2_DestroyActor();
                    continue;
                }
            }
            if (!SeenBoss.insert(BossKey).second)
            {
                printf("[Syndicate][AITest] duplicate boss removed name=%s class=%s\n",
                    BossKey.c_str(), PN.c_str());
                P->K2_DestroyActor();
                PC->K2_DestroyActor();
                continue;
            }
        }

        auto BotData = SyndicateBotDataForPawn(PN);
        AActor* NativePath = nullptr;
        if (auto COff = PC->GetOffset("CachedPatrollingComponent"); COff != uint32(-1))
        {
            auto PatrolComp = GetFromOffset<UObject*>(PC, COff);
            if (PatrolComp)
                if (auto POff = PatrolComp->GetOffset("PatrolPath"); POff != uint32(-1))
                    NativePath = (AActor*)GetFromOffset<UObject*>(PatrolComp, POff);
        }
        auto Loop = NativePath ? NativePath : SyndicateCreatePatrolLoop(P->K2_GetActorLocation(), 900.0);
        SyndicateSetHenchmanName((AFortPlayerStateAthena*)P->PlayerState, PC);
        SyndicateGiveHenchmanWeapon(PC, P, BotData);
        SyndicateForceGun(PC, P);
        SyndicateApplyMangAnim(P);
        P->bAlwaysRelevant = true;
        P->NetCullDistanceSquared = 625000000.f;

        SyndicateHenchmanRecord Rec;
        Rec.Pawn = P;
        Rec.BotData = BotData;
        Rec.PatrolPath = Loop;
        Rec.BossKey = BossKey;
        Rec.WeaponDef = SyndicateLastHenchmanWeapon;
        Rec.InventoryDrops = SyndicateCollectInventoryDrops(PC);
        Rec.LastLoc = P->K2_GetActorLocation();
        Rec.DriveLastLoc = Rec.LastLoc;
        Rec.NextLog = GetTickCount64() + 5000;
        SyndicateHenchmen.push_back(Rec);
        printf("[Syndicate][AITest] adopted native pawn=%s ctrl=%s data=%s nativepath=%s pts=%d loc=%.0f,%.0f,%.0f\n",
            PN.c_str(), CN.c_str(), BotData ? BotData->Name.ToString().c_str() : "fallback",
            NativePath ? NativePath->Name.ToString().c_str() : "synthetic",
            (NativePath && NativePath->GetOffset("PatrolPoints") != uint32(-1))
                ? GetFromOffset<TArray<AActor*>>(NativePath, NativePath->GetOffset("PatrolPoints")).Num() : -1,
            Rec.LastLoc.X, Rec.LastLoc.Y, Rec.LastLoc.Z);
        ++Adopted;
    }
    Ctrls.Free();
    return Adopted;
}

static void SyndicateDropHenchmanLoot(SyndicateHenchmanRecord& B, const FVector& Loc)
{
    if (!B.InventoryDrops.empty())
    {
        for (auto& Drop : B.InventoryDrops)
            if (Drop.Def)
                AFortInventory::SpawnPickup(Loc, Drop.Def, Drop.Count, Drop.Ammo);
        printf("[Syndicate][AITest] hench dead inventory=%d first=%s\n",
            (int)B.InventoryDrops.size(),
            B.InventoryDrops[0].Def ? B.InventoryDrops[0].Def->Name.ToString().c_str() : "null");
    }
    else if (B.WeaponDef)
    {
        AFortInventory::SpawnPickup(Loc, B.WeaponDef, 1, 30);
        printf("[Syndicate][AITest] hench dead fallback=%s\n", B.WeaponDef->Name.ToString().c_str());
    }
    else
    {
        printf("[Syndicate][AITest] hench dead no items\n");
    }
}


void SyndicateNativeHenchmanTick()
{
    // MANG henchmen/bosses only exist in Chapter 2 Season 2 (12.x). Do not touch older
    // seasons' nav system, spawners, inventories or bots.
    if (VersionInfo.FortniteVersion < 12.0 || VersionInfo.FortniteVersion >= 13.0)
        return;

    LARGE_INTEGER PerfC, PerfF;
    QueryPerformanceCounter(&PerfC); QueryPerformanceFrequency(&PerfF);
    const double PerfStart = 1000.0 * double(PerfC.QuadPart) / double(PerfF.QuadPart);

    static bool SyndicateGroupSpawned = false;
    if (!SyndicateGroupSpawned && SyndicateServerReady && SyndicateEnsureNavigation())
    {
        if (SyndicateSpawnAgencyGroup()) SyndicateGroupSpawned = true;
    }

    auto Now = GetTickCount64();
    if (SyndicateNativeSpawnerCount > 0)
    {
        static ULONGLONG NextAdopt = 0;
        if (Now >= NextAdopt)
        {
            SyndicateAdoptNativeBots();
            NextAdopt = Now + 5000;
        }
    }

    // The Yacht spawner drops Meowscles next to Deadpool, but he belongs at Catty Corner in
    // this season. The POI volume dump above gives us the real Catty transform.
    if (SyndicateCattyValid && !SyndicateCattyMeowsclesSpawned && SyndicateNativeSpawnerCount > 0)
    {
        bool ExistingMeowscles = false;
        for (auto& R : SyndicateHenchmen)
            if (R.Pawn && R.BossKey == "Meowscles") { ExistingMeowscles = true; break; }
        if (ExistingMeowscles)
        {
            SyndicateCattyMeowsclesSpawned = true;
        }
        else
        {
            static ULONGLONG CattyAt = 0;
            if (CattyAt == 0) CattyAt = Now + 9000;
            if (Now >= CattyAt)
            {
                auto World = UWorld::GetWorld();
                auto GM = World ? (AFortGameMode*)World->AuthorityGameMode : nullptr;
                auto BM = GM ? (UFortServerBotManagerAthena*)GM->ServerBotManager : nullptr;
                auto BDClass = FindClass("FortAthenaAIBotCustomizationData");
                auto Data = BDClass ? (UObject*)StaticLoadObject(
                    L"/Game/Athena/AI/MANG/BotData/BotData_MANG_POI_Yacht_Ego.BotData_MANG_POI_Yacht_Ego",
                    BDClass) : nullptr;
                auto Loop = SyndicateCreatePatrolLoop(SyndicateCattyLoc, 900.0);
                if (BM && Data && SyndicateSpawnExtraHenchman(BM, Data, SyndicateCattyLoc, FRotator{},
                    Loop, GM, (AActor*)World->GameState, SyndicateCurrentHumanTeam()))
                {
                    SyndicateCattyMeowsclesSpawned = true;
                    printf("[Syndicate][AITest] spawned Meowscles at Catty loc=%.0f,%.0f,%.0f\n",
                        SyndicateCattyLoc.X, SyndicateCattyLoc.Y, SyndicateCattyLoc.Z);
                }
                else CattyAt = Now + 5000;
            }
        }
    }

    // 12.50 HUD: SeasonLevelUIDisplay is written once at spawn, before the XP component has
    // the profile level, so the top-left badge stayed at "Level 0". Re-sync it and keep a
    // sane floor of 1 while the profile/XP component catches up.
    static ULONGLONG NextLevelSync = 0;
    if (Now >= NextLevelSync)
    {
        NextLevelSync = Now + 2000;
        static UWorld* LevelWorld = nullptr;
        auto LevelNowWorld = UWorld::GetWorld();
        if (LevelNowWorld != LevelWorld) { LevelWorld = LevelNowWorld; SyndicateLevelSyncCache.clear(); }
        if (auto PCClass = FindClass("FortPlayerControllerAthena"))
        {
            TArray<AActor*> Controllers; Utils::GetAll(PCClass, Controllers);
            for (auto A : Controllers)
            {
                auto HPC = (AFortPlayerControllerAthena*)A;
                auto HPS = HPC ? (AFortPlayerStateAthena*)HPC->PlayerState : nullptr;
                if (!HPC || !HPS || (HPS->HasbIsABot() && HPS->bIsABot)) continue;
                int Level = 1;
                if (VersionInfo.FortniteVersion >= 12.0 && HPS->HasSeasonLevelUIDisplay())
                {
                    auto& Cache = SyndicateLevelSyncCache[HPS];
                    if (Cache.Next == 0 || Now >= Cache.Next)
                    {
                        Cache.Next = Now + (Cache.Level > 0 ? 30000 : 5000);
                        std::string Name;
                        if (auto NameFn = HPS->GetFunction("GetPlayerName"))
                        {
                            auto N = HPS->Call<FString>(NameFn);
                            Name = N.ToString().c_str();
                            N.Free();
                        }
                        int ProfileLevel = SyndicateFetchProfileLevel(Name);
                        if (ProfileLevel > 0) Cache.Level = ProfileLevel;
                    }
                    if (Cache.Level > 0) Level = Cache.Level;
                }
                if (Level <= 1 && HPC->HasXPComponent() && HPC->XPComponent && HPC->XPComponent->HasCurrentLevel())
                    Level = (std::max)(1, HPC->XPComponent->CurrentLevel);
                if (HPC->HasXPComponent() && HPC->XPComponent && HPC->XPComponent->HasCurrentLevel() && HPC->XPComponent->CurrentLevel != Level)
                    HPC->XPComponent->CurrentLevel = Level;
                if (HPS->HasSeasonLevelUIDisplay() && HPS->SeasonLevelUIDisplay != Level)
                {
                    HPS->SeasonLevelUIDisplay = Level;
                    HPS->OnRep_SeasonLevelUIDisplay();
                    HPS->ForceNetUpdate();
                }
            }
            Controllers.Free();
        }
    }

    // Refresh the human pawn list a few times per second instead of scanning all ~100
    // controllers once per henchman per tick.
    static ULONGLONG NextHumanScan = 0;
    if (Now >= NextHumanScan)
    {
        NextHumanScan = Now + 250;
        SyndicateHumanPawns.clear();
        if (auto PCClass = FindClass("FortPlayerControllerAthena"))
        {
            TArray<AActor*> Controllers; Utils::GetAll(PCClass, Controllers);
            for (auto A : Controllers)
            {
                auto HPC = (AFortPlayerControllerAthena*)A;
                auto HPS = HPC ? (AFortPlayerStateAthena*)HPC->PlayerState : nullptr;
                if (HPC && HPC->MyFortPawn && HPS && !(HPS->HasbIsABot() && HPS->bIsABot))
                    SyndicateHumanPawns.push_back(HPC->MyFortPawn);
            }
            Controllers.Free();
        }
    }

    // Cache the two per-bot health functions once; GetFunction walks the class chain.
    static UFunction* FnGetHealth = nullptr;
    static UFunction* FnIsDBNO = nullptr;
    if ((!FnGetHealth || !FnIsDBNO) && !SyndicateHenchmen.empty() && SyndicateHenchmen[0].Pawn)
    {
        FnGetHealth = SyndicateHenchmen[0].Pawn->GetFunction("GetHealth");
        FnIsDBNO = SyndicateHenchmen[0].Pawn->GetFunction("IsDBNO");
    }

    for (size_t I = 0; I < SyndicateHenchmen.size(); ++I)
    {
        auto& B = SyndicateHenchmen[I];
        auto P = B.Pawn;
        if (!P) continue;

        // Poll death every network tick so loot appears immediately instead of waiting for
        // the five-second status log. DBNO pawns also report zero health, so only drop once
        // the bot is actually finished, not while it is knocked.
        float LiveHealth = -1.f;
        if (FnGetHealth) LiveHealth = P->Call<float>(FnGetHealth);
        bool Knocked = false;
        if (FnIsDBNO) Knocked = P->Call<bool>(FnIsDBNO);
        // Taking damage from behind must make the henchman turn around even if the native
        // alert level never rose. Only do this when a player is close enough to be the
        // attacker; otherwise storm damage made every henchman on the map hunt the player.
        if (B.LastHealth >= 0.f && LiveHealth >= 0.f && LiveHealth < B.LastHealth - 0.1f)
        {
            double Best2 = 1.0e18;
            AFortPlayerPawnAthena* Attacker = nullptr;
            auto PL2 = P->K2_GetActorLocation();
            for (auto HP : SyndicateHumanPawns)
            {
                if (!HP || SyndicatePawnIsAirborne(HP)) continue;
                auto HL = HP->K2_GetActorLocation();
                double hx = HL.X - PL2.X, hy = HL.Y - PL2.Y;
                double d = hx * hx + hy * hy;
                if (d < Best2) { Best2 = d; Attacker = HP; }
            }
            if (Best2 <= 1500.0 * 1500.0 && Attacker)
            {
                // The damaged henchman knows who shot it, even from behind.
                B.ForcedTarget = Attacker;
                B.ForcedTargetUntil = Now + 10000;
                B.ForcedAlertUntil = Now + 6000;
                SyndicateSetHenchmanAlert((AActor*)P->Controller, 3);
            }
            // Bosses call their guards: the two closest henchmen aggro the player who
            // damaged the boss, even if the shot came from outside the 1500-unit radius.
            if (!B.BossKey.empty() && Attacker)
            {
                B.ForcedTarget = Attacker;
                B.ForcedTargetUntil = Now + 10000;
                B.ForcedAlertUntil = Now + 10000;
                SyndicateSetHenchmanAlert((AActor*)P->Controller, 3);
                B.NoLosSince = 0;
                B.LoseAggroUntil = 0;
                SyndicateHenchmanRecord* Picked[2]{};
                for (int Pick = 0; Pick < 2; ++Pick)
                {
                    SyndicateHenchmanRecord* BestH = nullptr;
                    double BestHD = 1.0e18;
                    for (auto& H : SyndicateHenchmen)
                    {
                        if (&H == &B || !H.Pawn || H.Pawn->bActorIsBeingDestroyed || !H.BossKey.empty()) continue;
                        bool Already = false;
                        for (int S = 0; S < Pick; ++S) if (Picked[S] == &H) { Already = true; break; }
                        if (Already) continue;
                        auto HL = H.Pawn->K2_GetActorLocation();
                        double hx = HL.X - PL2.X, hy = HL.Y - PL2.Y;
                        double d = hx * hx + hy * hy;
                        if (d < BestHD) { BestHD = d; BestH = &H; }
                    }
                    if (!BestH) break;
                    Picked[Pick] = BestH;
                    BestH->ForcedTarget = Attacker;
                    BestH->ForcedTargetUntil = Now + 10000;
                    BestH->ForcedAlertUntil = Now + 10000;
                    SyndicateSetHenchmanAlert((AActor*)BestH->Pawn->Controller, 3);
                    BestH->NoLosSince = 0;
                    BestH->LoseAggroUntil = 0;
                }
            }
        }
        // A henchman/boss being downed in view of its allies pulls 1-2 of them in.
        if (Knocked && !B.WasKnocked)
        {
            // A henchman that was itself alerted by witnessing a kill does not start a chain.
            if (!B.WitnessAlerted)
            {
                auto VictimLoc = P->K2_GetActorLocation();
                SyndicateAlertKillWitnesses(B, SyndicateNearestHumanTo(VictimLoc, 4000.0), VictimLoc, Now);
            }
        }
        B.WasKnocked = Knocked;

        if (LiveHealth >= 0.f) B.LastHealth = LiveHealth;
        if (LiveHealth == 0.f && !Knocked)
        {
            auto DeathLoc = P->K2_GetActorLocation();
            // Finishing a henchman/boss in view of its allies does the same, unless it was a witness.
            if (!B.WitnessAlerted)
                SyndicateAlertKillWitnesses(B, SyndicateNearestHumanTo(DeathLoc, 4000.0), DeathLoc, Now);
            SyndicateDropHenchmanLoot(B, DeathLoc);
            B.Pawn = nullptr;
            continue;
        }

        SyndicateDriveHenchman(B, P);

        if (Now < B.NextLog) continue;
        B.NextLog = Now + 5000;

        // Stop after 10 minutes so a destroyed pawn cannot be read forever.
        if (++B.Samples > 120)
        {
            B.Pawn = nullptr;
            printf("[Syndicate][AITest] hench[%d] heartbeat stopped\n", (int)I);
            continue;
        }

        auto Loc = P->K2_GetActorLocation();
        double dx = Loc.X - B.LastLoc.X;
        double dy = Loc.Y - B.LastLoc.Y;
        double dz = Loc.Z - B.LastLoc.Z;
        double Moved = sqrt(dx * dx + dy * dy + dz * dz);

        // The pawn has had time to land by the first sample, so the patrol points use real ground.
        if (!B.PatrolDone) SyndicateAttachHenchmanPatrol(P, B.PatrolPath, B.PatrolDone);

        // The controller creates its inventory a few frames after SpawnBot; retry until a gun sticks.
        if (!P->CurrentWeapon)
        {
            SyndicateGiveHenchmanWeapon((AActor*)P->Controller, P, B.BotData);
            if (!P->CurrentWeapon) SyndicateForceGun((AActor*)P->Controller, P);
        }

        // Keep the bot's blackboard in sync with the live match phase.
        auto PCNow = (AActor*)P->Controller;
        if (PCNow)
        {
            if (auto Off = PCNow->GetOffset("Blackboard"); Off != uint32(-1))
            {
                auto BB = GetFromOffset<UObject*>(PCNow, Off);
                auto World = UWorld::GetWorld();
                auto GS = World ? (AFortGameStateAthena*)World->GameState : nullptr;
                if (BB && GS)
                {
                    if (auto Fn = BB->GetFunction("SetValueAsEnum"))
                    {
                        BB->Call<void>(Fn, FName(L"AIEvaluator_Global_GamePhaseStep"), GS->GamePhaseStep);
                        BB->Call<void>(Fn, FName(L"AIEvaluator_Global_GamePhase"), GS->GamePhase);
                    }
                }
            }
        }

        float Health = -1.f;
        if (FnGetHealth) Health = P->Call<float>(FnGetHealth);

        auto PC = (AActor*)P->Controller;
        auto State = (AFortPlayerStateAthena*)P->PlayerState;
        float Speed = 0.f;
        if (auto Fn = P->GetFunction("GetVelocity"))
        {
            auto V = P->Call<FVector>(Fn);
            Speed = (float)sqrt(V.X * V.X + V.Y * V.Y + V.Z * V.Z);
        }
        int MoveStatus = -1;
        if (PC)
            if (auto Fn = PC->GetFunction("GetMoveStatus")) MoveStatus = (int)PC->Call<uint8>(Fn);
        int MovementMode = -1;
        if (P->CharacterMovement)
            if (auto Off = P->CharacterMovement->GetOffset("MovementMode"); Off != uint32(-1))
                MovementMode = (int)GetFromOffset<uint8>(P->CharacterMovement, Off);
        int EvalCount = -1;
        if (PC)
            if (auto Off = PC->GetOffset("AIEvaluators"); Off != uint32(-1))
            {
                auto& Evals = GetFromOffset<TArray<UObject*>>(PC, Off);
                EvalCount = Evals.Num();
            }
        std::string Patrol = "none";
        if (PC)
            if (auto Off = PC->GetOffset("CachedPatrollingComponent"); Off != uint32(-1))
            {
                auto C = GetFromOffset<UObject*>(PC, Off);
                if (C)
                {
                    auto POff = C->GetOffset("PatrolPath");
                    auto Path = POff != uint32(-1) ? GetFromOffset<UObject*>(C, POff) : nullptr;
                    Patrol = Path ? std::string(Path->Name.ToString().c_str()) : std::string(C->Name.ToString().c_str());
                }
            }

        // Keep the bot hostile after the human joins. Spawn-time team assignment cannot see
        // a player who has not connected yet, which left the bot on the player's team.
        if (State && PC)
        {
            int HumanTeam = -1;
            if (auto PCClass = FindClass("FortPlayerControllerAthena"))
            {
                TArray<AActor*> Controllers; Utils::GetAll(PCClass, Controllers);
                for (auto A : Controllers)
                {
                    auto HPC = (AFortPlayerControllerAthena*)A;
                    auto HPS = HPC ? (AFortPlayerStateAthena*)HPC->PlayerState : nullptr;
                    if (HPC && HPC->MyFortPawn && HPS && !(HPS->HasbIsABot() && HPS->bIsABot)) { HumanTeam = HPS->TeamIndex; break; }
                }
                Controllers.Free();
            }
            if (HumanTeam >= 0 && State->TeamIndex == HumanTeam)
            {
                State->TeamIndex = (HumanTeam == 10) ? 11 : 10;
                State->SquadId = State->TeamIndex;
                State->ForceNetUpdate();
                printf("[Syndicate][AITest] hench[%d] hostile team -> %d\n", (int)I, (int)State->TeamIndex);
            }
        }

        // One-shot movement probe: if the AI evaluates patrol but never issues a move, this
        // proves whether the nav/path-following layer itself works.
        if (B.Samples >= 2 && !B.MoveProbed && B.PatrolPath && PC && Speed < 20.f && MoveStatus == 0)
        {
            B.MoveProbed = true;
            auto PointsOff = B.PatrolPath->GetOffset("PatrolPoints");
            if (PointsOff != uint32(-1))
            {
                auto& Pts = GetFromOffset<TArray<AActor*>>(B.PatrolPath, PointsOff);
                if (Pts.Num() > 0 && Pts[0])
                {
                    auto Dest = Pts[0]->K2_GetActorLocation();
                    uint8 Result = 255;
                    if (auto Fn = PC->GetFunction("MoveToLocation"))
                        Result = PC->Call<uint8>(Fn, Dest, 50.f, false, true, true, false, (UClass*)nullptr, true);
                    printf("[Syndicate][AITest] hench[%d] move probe dest=%.0f,%.0f,%.0f result=%d\n",
                        (int)I, Dest.X, Dest.Y, Dest.Z, (int)Result);
                }
            }
        }

        std::string Name = "?";
        if (State)
            if (auto Fn = State->GetFunction("GetPlayerName")) Name = std::string(State->Call<FString>(Fn).ToString().c_str());
        std::string Weapon = "none";
        if (P->CurrentWeapon) Weapon = std::string(P->CurrentWeapon->Name.ToString().c_str());
        int AlertLevel = -1;
        if (PC)
            if (auto Off = PC->GetOffset("CurrentAlertLevel"); Off != uint32(-1))
                AlertLevel = (int)GetFromOffset<uint8>(PC, Off);
        printf("[Syndicate][AITest] hench[%d] loc=%.0f,%.0f,%.0f moved=%.0f hp=%.0f team=%d speed=%.0f move=%d mode=%d alert=%d evals=%d patrol=%s ctrl=%s weapon=%s name=%s\n",
            (int)I, Loc.X, Loc.Y, Loc.Z, Moved, Health, State ? (int)State->TeamIndex : -1,
            Speed, MoveStatus, MovementMode, AlertLevel, EvalCount, Patrol.c_str(),
            PC && PC->Class ? PC->Class->Name.ToString().c_str() : "none", Weapon.c_str(), Name.c_str());

        B.LastLoc = Loc;
        bool KnockedNow = false;
        if (FnIsDBNO) KnockedNow = P->Call<bool>(FnIsDBNO);
        if (Health == 0.f && !KnockedNow)
        {
            SyndicateDropHenchmanLoot(B, Loc);
            B.Pawn = nullptr;
        }
    }

    static double PerfAcc = 0;
    static unsigned PerfFrames = 0;
    static ULONGLONG PerfNext = 0;
    QueryPerformanceCounter(&PerfC);
    PerfAcc += 1000.0 * double(PerfC.QuadPart) / double(PerfF.QuadPart) - PerfStart;
    ++PerfFrames;
    if (GetTickCount64() >= PerfNext)
    {
        PerfNext = GetTickCount64() + 10000;
        printf("[Syndicate][AITest] mang tick avg=%.2fms bots=%d\n",
            PerfAcc / (PerfFrames ? PerfFrames : 1), (int)SyndicateHenchmen.size());
        PerfAcc = 0; PerfFrames = 0;
    }
}

// Read the object path out of one FAssetData element.
static std::string SyndicateAssetPath(uint8* Elem, int PathOffset)
{
    return std::string(GetFromOffset<FName>(Elem, PathOffset).ToString().c_str());
}

// Ask the asset registry for every AI bot-data asset and pick a normal henchman instead of
// the Deadpool boss (BotData_MANG_POI_HDP), whose boss death path crashes the server.
static std::string SyndicateFindHenchmanBotData()
{
    auto Helpers = FindClass("AssetRegistryHelpers");
    auto Interface = FindClass("AssetRegistry");
    auto DataStruct = FindStruct("AssetData");
    if (!Helpers || !Interface || !DataStruct) return "";

    auto HelperObj = Helpers->GetDefaultObj();
    auto GetRegistry = HelperObj ? HelperObj->GetFunction("GetAssetRegistry") : nullptr;
    if (!GetRegistry) return "";

    uint8 RegistryReturn[32]{};
    HelperObj->ProcessEvent(GetRegistry, RegistryReturn);
    auto Registry = *(UObject**)RegistryReturn;
    if (!Registry) return "";

    // The registry is normally populated asynchronously. The first spawn can happen before
    // the scan reaches the MANG folder, which is why only one unrelated asset was returned.
    // Force the synchronous scan once so real henchman data can be found.
    for (auto F = Interface->GetChildren(); F; F = F->GetNext())
    {
        if (F->Name.ToString() != "SearchAllAssets") continue;
        bool Sync = true;
        printf("[Syndicate][AITest] botdata registry scan start\n");
        Registry->ProcessEvent((UFunction*)F, &Sync);
        printf("[Syndicate][AITest] botdata registry scan done\n");
        break;
    }

    int ElemSize = DataStruct->GetPropertiesSize();
    int PathOffset = (int)DataStruct->GetOffset("ObjectPath");
    if (PathOffset < 0) PathOffset = (int)DataStruct->GetOffset("PackageName");
    if (PathOffset < 0 || ElemSize <= 0) return "";

    // List everything under the MANG folder so we can see the real henchman/boss data names.
    for (auto F = Interface->GetChildren(); F; F = F->GetNext())
    {
        if (F->Name.ToString() != "GetAssetsByPath") continue;
        auto Fn = (UFunction*)F;
        auto Meta = Fn->GetParamsNamed();
        std::vector<uint8> Params(Meta.Size, 0);
        FName Folder(L"/Game/Athena/AI/MANG");
        int Arr = -1;
        for (auto& P : Meta.NameOffsetMap)
        {
            if (P.Name == "PackagePath" && P.Offset + sizeof(FName) <= Params.size())
                memcpy(Params.data() + P.Offset, &Folder, sizeof(FName));
            else if (P.Name == "OutAssetData") Arr = (int)P.Offset;
            else if (P.Name == "bRecursive" && P.Offset < Params.size()) *(bool*)(Params.data() + P.Offset) = true;
            else if (P.Name == "bIncludeOnlyOnDiskAssets" && P.Offset < Params.size()) *(bool*)(Params.data() + P.Offset) = true;
        }
        if (Arr >= 0 && Arr + 16 <= (int)Params.size())
        {
            Registry->ProcessEvent(Fn, Params.data());
            auto List = (TArray<uint8>*)(Params.data() + Arr);
            printf("[Syndicate][AITest] mang assets=%d\n", List->Num());
            for (int I = 0; I < List->Num() && I < 200; ++I)
            {
                auto Elem = (uint8*)List->GetData() + (size_t)I * ElemSize;
                printf("[Syndicate][AITest] mang asset %s\n", SyndicateAssetPath(Elem, PathOffset).c_str());
            }
        }
        break;
    }

    UFunction* Query = nullptr;
    for (auto F = Interface->GetChildren(); F; F = F->GetNext())
        if (F->Name.ToString() == "GetAssetsByClass") Query = (UFunction*)F;
    if (!Query) return "";

    auto Meta = Query->GetParamsNamed();
    std::vector<uint8> Params(Meta.Size, 0);
    FName ClassName(L"FortAthenaAIBotCustomizationData");
    int ArrayOffset = -1;
    for (auto& P : Meta.NameOffsetMap)
    {
        if (P.Name == "ClassName" && P.Offset + sizeof(FName) <= Params.size())
            memcpy(Params.data() + P.Offset, &ClassName, sizeof(FName));
        else if (P.Name == "OutAssetData")
            ArrayOffset = (int)P.Offset;
        else if (P.Name == "bSearchSubClasses" && P.Offset < Params.size())
            *(bool*)(Params.data() + P.Offset) = true;
    }
    if (ArrayOffset < 0 || ArrayOffset + 16 > (int)Params.size()) return "";

    Registry->ProcessEvent(Query, Params.data());

    auto Assets = (TArray<uint8>*)(Params.data() + ArrayOffset);
    printf("[Syndicate][AITest] botdata assets=%d\n", Assets->Num());
    std::string Best;
    for (int I = 0; I < Assets->Num(); ++I)
    {
        auto Elem = (uint8*)Assets->GetData() + (size_t)I * ElemSize;
        std::string Path = SyndicateAssetPath(Elem, PathOffset);
        if (Path.empty()) continue;
        std::string Low;
        for (char C : Path) Low += (char)tolower((unsigned char)C);
        if (I < 200) printf("[Syndicate][AITest] botdata asset %s\n", Path.c_str());
        bool Boss = Low.find("boss") != std::string::npos || Low.find("hdp") != std::string::npos ||
                    Low.find("deadpool") != std::string::npos || Low.find("yacht") != std::string::npos;
        if (!Boss && Low.find("henchman") != std::string::npos) { Best = Path; break; }
        if (Best.empty() && !Boss && Low.find("mang") != std::string::npos) Best = Path;
    }
    if (Best.empty()) Best = "/Game/Athena/AI/MANG/BotData/BotData_MANG_POI_HDP.BotData_MANG_POI_HDP";
    printf("[Syndicate][AITest] botdata chosen=%s\n", Best.c_str());
    return Best;
}

void ShowFoundation(const ABuildingFoundation* Foundation)
{
    if (!Foundation) return;

    /*Foundation->StreamingData.BoundingBox = Foundation->StreamingBoundingBox;
    Foundation->StreamingData.FoundationLocation = Foundation->GetTransform().Translation;
    Foundation->SetDynamicFoundationEnabled(true);*/
    //Foundation->SetDynamicFoundationTransform(Foundation->GetTransform());

    if (Foundation->HasDynamicFoundationType())
        Foundation->DynamicFoundationType = 0;
    if (Foundation->HasbServerStreamedInLevel())
    {
        Foundation->bServerStreamedInLevel = true;
        Foundation->OnRep_ServerStreamedInLevel();
    }
    if (Foundation->HasDynamicFoundationRepData())
    {
        Foundation->DynamicFoundationRepData.EnabledState = 1;
        Foundation->OnRep_DynamicFoundationRepData();
    }
    if (Foundation->HasFoundationEnabledState())
        Foundation->FoundationEnabledState = 1;

    Foundation->SetDynamicFoundationEnabled(true);
}

void HideFoundation(const ABuildingFoundation* Foundation)
{
    if (!Foundation) return;
    if (Foundation->HasDynamicFoundationType())
        Foundation->DynamicFoundationType = 3; // StartDisabled, so it is not streamed back in
    if (Foundation->HasbServerStreamedInLevel())
    {
        Foundation->bServerStreamedInLevel = false;
        Foundation->OnRep_ServerStreamedInLevel();
    }
    if (Foundation->HasDynamicFoundationRepData())
    {
        Foundation->DynamicFoundationRepData.EnabledState = 2; // Disabled
        Foundation->OnRep_DynamicFoundationRepData();
    }
    if (Foundation->HasFoundationEnabledState())
        Foundation->FoundationEnabledState = 2;
    Foundation->SetDynamicFoundationEnabled(false);
}

// Season X 10.31: Gotham City replaced Tilted Town during this patch and the map already labels the
// spot Gotham City, but only Tilted Town was streamed in. Turn the Gotham City foundation on and the
// Tilted Town one off. Every foundation is written to Runtime\Server\foundations-<version>.tsv so
// the names can be checked if a build names them differently.
void SyndicateFindBatmanItems(); // FortLootPackage.cpp
static bool SyndicateGothamActive = false;

// Name of the map (level) an actor was placed in, e.g. Athena_POI_City_003.
static std::string SyndicateActorMap(UObject* Actor)
{
    for (UObject* O = Actor ? Actor->Outer : nullptr; O; O = O->Outer)
    {
        std::string Name = O->Name.ToString().c_str();
        if (Name != "PersistentLevel") return Name;
    }
    return "";
}

// Second pass once the island has streamed in: Tilted Town's base level (Athena_POI_City_003) and its
// *_WEST building levels were still loaded under Gotham. Turn off every foundation in them (unloads the
// buildings for everyone) and remove their containers, building pieces and floor loot spawners.
static std::wstring TiltedTownPackage; // e.g. /Temp/Game/Athena/Maps/POI/Athena_POI_City_003_e25ea707

// Players' games stream Tilted Town's base level in by themselves, so its ground, roads and props stayed
// under Gotham. Tell every player's game to unload that level (sent on join and repeated every 15 s).
void SyndicateSeasonXClientTick()
{
    if (TiltedTownPackage.empty()) return;
    static ULONGLONG Next = 0; auto Now = GetTickCount64(); if (Now < Next) return; Next = Now + 1000;
    static std::unordered_map<UObject*, ULONGLONG> Sent;
    auto World = UWorld::GetWorld(); auto Net = World ? (UNetDriver*)World->NetDriver : nullptr; if (!Net) return;
    for (auto Conn : Net->ClientConnections)
    {
        auto PC = Conn ? (UObject*)Conn->PlayerController : nullptr; if (!PC) continue;
        auto& Last = Sent[PC]; if (Last && Now - Last < 15000) continue; Last = Now;
        auto Fn = PC->GetFunction("ClientUpdateLevelStreamingStatus"); if (!Fn) continue;
        auto Meta = Fn->GetParamsNamed(); std::vector<uint8> Params(Meta.Size + 16, 0); FName Package(TiltedTownPackage.c_str());
        for (auto& P : Meta.NameOffsetMap)
        {
            if (P.Name == "PackageName" && P.ElementSize == sizeof(FName)) memcpy(Params.data() + P.Offset, &Package, sizeof(FName));
            else if (P.Name == "LODIndex" && P.ElementSize == sizeof(int32)) *(int32*)(Params.data() + P.Offset) = -1;
            // bNewShouldBeLoaded / bNewShouldBeVisible / bNewShouldBlockOnLoad stay false
        }
        PC->ProcessEvent(Fn, Params.data());
    }
}

void SyndicateSeasonXRemoveTiltedTown()
{
    if (!SyndicateGothamActive) return;
    int Foundations = 0, Removed = 0;
    auto IsSyndicate = [](const std::string& Map) { return Map.find("Athena_POI_City_003") != std::string::npos || (Map.size() > 5 && Map.compare(Map.size() - 5, 5, "_WEST") == 0); };
    {
        TArray<ABuildingFoundation*> All; Utils::GetAll<ABuildingFoundation>(All);
        for (int i = 0; i < All.Num(); i++) { auto F = All[i]; if (F && IsSyndicate(SyndicateActorMap(F))) { HideFoundation(F); ++Foundations; } }
        All.Free();
    }
    for (auto Name : { "BuildingContainer", "BuildingSMActor" })
    {
        auto Class = FindClass(Name); if (!Class) continue;
        TArray<AActor*> Actors; Utils::GetAll(Class, Actors);
        for (auto Actor : Actors) if (Actor && !Actor->bActorIsBeingDestroyed && IsSyndicate(SyndicateActorMap(Actor)))
        {
            if (TiltedTownPackage.empty() && SyndicateActorMap(Actor).find("Athena_POI_City_003") != std::string::npos)
            {
                UObject* Outermost = Actor; while (Outermost->Outer) Outermost = Outermost->Outer;
                TiltedTownPackage = Outermost->Name.ToWString().c_str();
            }
            Actor->K2_DestroyActor(); ++Removed;
        }
        Actors.Free();
    }
    printf("[Syndicate] Season X map: removed Tilted Town (%d foundations off, %d pieces removed); players unload %ls\n", Foundations, Removed, TiltedTownPackage.empty() ? L"(not found)" : TiltedTownPackage.c_str());
}

// Season X: find the real names of the Batman/Gotham event flags. They live in the game's data, not the
// executable, so once the island has loaded, search this process's memory (read-only, in the background)
// for text containing these words and write every unique hit to Runtime\Server\event-strings-<version>.txt.
static void SyndicateDumpEventStrings()
{
    wchar_t Ready[32768]{}; if (!GetEnvironmentVariableW(L"TILTED_SERVER_READY_FILE", Ready, 32768)) return;
    std::wstring Folder = Ready; auto Slash = Folder.find_last_of(L"\\/"); if (Slash == std::wstring::npos) return; Folder.resize(Slash + 1);
    wchar_t Version[32]; swprintf(Version, 32, L"%.2f", VersionInfo.FortniteVersion);
    std::wstring OutPath = Folder + L"event-strings-" + Version + L".txt";
    std::thread([OutPath]() {
        static const char* Words[] = { "EventFlag", "BlackMonday", "Gotham", "RiftZone" };
        std::set<std::string> Found;
        std::vector<uint8_t> Buffer(4 << 20);
        auto IsChar = [](unsigned c) { return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_' || c == '.' || c == '/' || c == ':' || c == '-'; };
        MEMORY_BASIC_INFORMATION Info{};
        for (uintptr_t At = 0x10000; Found.size() < 5000 && VirtualQuery((void*)At, &Info, sizeof(Info)) == sizeof(Info);)
        {
            uintptr_t Next = (uintptr_t)Info.BaseAddress + Info.RegionSize; if (Next <= At) break;
            bool Readable = Info.State == MEM_COMMIT && !(Info.Protect & (PAGE_GUARD | PAGE_NOACCESS)) && (Info.Protect & (PAGE_READONLY | PAGE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE));
            for (uintptr_t Chunk = (uintptr_t)Info.BaseAddress; Readable && Chunk < Next; Chunk += Buffer.size() - 512)
            {
                SIZE_T Want = (SIZE_T)(std::min<uintptr_t>)(Buffer.size(), Next - Chunk), Got = 0;
                if (!ReadProcessMemory(GetCurrentProcess(), (void*)Chunk, Buffer.data(), Want, &Got) || Got < 16) break;
                auto Data = Buffer.data();
                for (auto Word : Words)
                {
                    size_t Len = strlen(Word);
                    for (int Wide = 0; Wide < 2; Wide++)
                    {
                        size_t Step = Wide ? 2 : 1;
                        for (size_t i = 0; i + Len * Step < Got; i++)
                        {
                            if (Data[i] != (uint8_t)Word[0]) continue;
                            bool Match = true; for (size_t k = 1; k < Len && Match; k++) Match = Data[i + k * Step] == (uint8_t)Word[k] && (!Wide || Data[i + k * Step - 1] == 0);
                            if (!Match || (Wide && Data[i + 1] != 0)) continue;
                            size_t Start = i; while (Start >= Step && IsChar(Data[Start - Step]) && (!Wide || Data[Start - Step + 1] == 0)) Start -= Step;
                            size_t End = i; while (End + Step <= Got && IsChar(Data[End]) && (!Wide || (End + 1 < Got && Data[End + 1] == 0))) End += Step;
                            std::string Text; for (size_t c = Start; c < End && Text.size() < 160; c += Step) Text += (char)Data[c];
                            if (Text.size() >= 6) Found.insert(Text);
                            i = End;
                        }
                    }
                }
                if (Want < Buffer.size()) break;
            }
            At = Next;
        }
        FILE* Out = nullptr;
        if (_wfopen_s(&Out, OutPath.c_str(), L"w") == 0 && Out) { for (auto& Line : Found) fprintf(Out, "%s\n", Line.c_str()); fclose(Out); }
        printf("[Syndicate] event strings: %d found (event-strings file in Runtime\\Server)\n", (int)Found.size());
    }).detach();
}

static void SyndicateSeasonXGotham()
{
    TArray<ABuildingFoundation*> All; Utils::GetAll<ABuildingFoundation>(All);
    std::vector<ABuildingFoundation*> Gotham, Syndicate;
    FILE* Log = nullptr;
    wchar_t Ready[32768]{};
    if (GetEnvironmentVariableW(L"TILTED_SERVER_READY_FILE", Ready, 32768))
    {
        std::wstring Folder = Ready; auto Slash = Folder.find_last_of(L"\\/");
        if (Slash != std::wstring::npos) { Folder.resize(Slash + 1); wchar_t Version[32]; swprintf(Version, 32, L"%.2f", VersionInfo.FortniteVersion); _wfopen_s(&Log, (Folder + L"foundations-" + Version + L".tsv").c_str(), L"w"); }
    }
    for (int i = 0; i < All.Num(); i++)
    {
        auto Foundation = All[i]; if (!Foundation) continue;
        std::string Name = Foundation->Name.ToString().c_str(), Lower = Name;
        for (auto& C : Lower) C = (char)tolower((unsigned char)C);
        auto P = Foundation->K2_GetActorLocation();
        int Streamed = Foundation->HasbServerStreamedInLevel() ? (Foundation->bServerStreamedInLevel ? 1 : 0) : -1;
        if (Log) fprintf(Log, "%s\t%s\t%.0f\t%.0f\t%.0f\tstreamed=%d\n", Name.c_str(), Foundation->Class ? Foundation->Class->Name.ToString().c_str() : "?", P.X, P.Y, P.Z, Streamed);
        // 10.31 names the two rift-beacon POIs at Syndicate's spot LF_Athena_POI_50x53_RBBM (BatMan,
        // Gotham City) and LF_Athena_POI_50x53_RBWW (Wild West, Tilted Town).
        bool IsGotham = Lower.find("gotham") != std::string::npos || Lower.find("batman") != std::string::npos || (Lower.size() >= 4 && Lower.compare(Lower.size() - 4, 4, "rbbm") == 0);
        bool IsSyndicate = Lower.find("tilted") != std::string::npos || Lower.find("wildwest") != std::string::npos || (Lower.size() >= 4 && Lower.compare(Lower.size() - 4, 4, "rbww") == 0);
        if (IsGotham) Gotham.push_back(Foundation);
        else if (IsSyndicate) Syndicate.push_back(Foundation);
    }
    All.Free();
    int Hidden = 0;
    for (auto Foundation : Gotham) ShowFoundation(Foundation);
    if (!Gotham.empty())
        for (auto Foundation : Syndicate)
        {
            auto P = Foundation->K2_GetActorLocation(); bool Near = false;
            for (auto G : Gotham) { auto Q = G->K2_GetActorLocation(); float DX = P.X - Q.X, DY = P.Y - Q.Y; if (DX * DX + DY * DY < 20000.f * 20000.f) Near = true; }
            if (Near) { HideFoundation(Foundation); ++Hidden; }
        }
    SyndicateGothamActive = !Gotham.empty();
    if (Log) { fprintf(Log, "# gotham=%d tiltedTownHidden=%d\n", (int)Gotham.size(), Hidden); fclose(Log); }
    printf("[Syndicate] Season X map: Gotham City foundations=%d, Tilted Town foundations hidden=%d\n", (int)Gotham.size(), Hidden);
}

bool bIsLargeTeamGame = false;

// Finds the selected playlist; falls back to its short name (e.g. Playlist_ShowdownAlt_Solo)
// when the asset folder in the configured path does not match this build.
extern std::wstring SyndicatePlaylistPath(const std::string& Name); // FortQuestManager.cpp: from the asset registry
static auto SyndicatePlaylist()
{
    // Season 6 (6.20): the cube fiends ("zombies") only spawn in the Fortnitemares versions of Solo/Duo/Squad
    // (Playlist_Deimos_*, same rules plus the fiends). FConfiguration::Playlist keeps the Default name, so every
    // team-size check elsewhere still reads it; only the playlist the match really runs is swapped here.
    if (VersionInfo.FortniteVersion >= 6.19 && VersionInfo.FortniteVersion < 6.3)
        for (auto [Default, Deimos] : { std::pair{ L"Playlist_DefaultSolo", L"/Game/Athena/Playlists/Deimos/Playlist_Deimos_Solo.Playlist_Deimos_Solo" },
                                        std::pair{ L"Playlist_DefaultDuo", L"/Game/Athena/Playlists/Deimos/Playlist_Deimos_Duo.Playlist_Deimos_Duo" },
                                        std::pair{ L"Playlist_DefaultSquad", L"/Game/Athena/Playlists/Deimos/Playlist_Deimos_Squad.Playlist_Deimos_Squad" } })
            if (wcsstr(FConfiguration::Playlist, Default))
            {
                if (auto Fiends = FindObject<UFortPlaylistAthena>(Deimos)) { static bool Told = false; if (!Told) { Told = true; printf("[Syndicate] Season 6: running %ls (cube fiends on)\n", Deimos); } return Fiends; }
                printf("[Syndicate] Season 6: %ls did not load; normal playlist\n", Deimos);
                break;
            }
    auto Playlist = FindObject<UFortPlaylistAthena>(FConfiguration::Playlist);
    if (!Playlist)
        if (auto Dot = wcsrchr(FConfiguration::Playlist, L'.'))
        {
            std::string Name;
            for (auto C = Dot + 1; *C; ++C) Name += char(*C);
            if (auto Found = TUObjectArray::FindObject<UFortPlaylistAthena>(Name.c_str()))
                Playlist = (decltype(Playlist))Found;
            // Not loaded yet and in another folder: the asset registry knows where it is.
            else if (auto Path = SyndicatePlaylistPath(Name); !Path.empty())
            {
                Playlist = FindObject<UFortPlaylistAthena>(Path.c_str());
                printf("[Syndicate] playlist %s found at %ls: %s\n", Name.c_str(), Path.c_str(), Playlist ? "loaded" : "failed to load");
            }
            else printf("[Syndicate] playlist %s is not in this build; using Solo\n", Name.c_str());
        }
    return Playlist;
}

static int SyndicateSelectedTeamSize()
{
    if(wcsstr(FConfiguration::Playlist,L"_Trios"))return 3;
    if(wcsstr(FConfiguration::Playlist,L"Playlist_DefaultSquad"))return 4;
    if(wcsstr(FConfiguration::Playlist,L"Playlist_DefaultDuo"))return 2;
    return 1;
}

void SetupPlaylist(AFortGameMode* GameMode, AFortGameStateAthena* GameState)
{
    auto Playlist = SyndicatePlaylist();

    if (!Playlist)
        Playlist = FindObject<UFortPlaylistAthena>(L"/Game/Athena/Playlists/Playlist_DefaultSolo.Playlist_DefaultSolo");

    if (Playlist)
    {
        // Lock standard playlist team sizes before replication and team assignment.
        if (Playlist->HasMaxSquadSize()) {
            if (wcsstr(FConfiguration::Playlist,L"Playlist_DefaultDuo")) Playlist->MaxSquadSize=2;
            else if (wcsstr(FConfiguration::Playlist,L"Playlist_DefaultSquad")) Playlist->MaxSquadSize=4;
            else if (wcsstr(FConfiguration::Playlist,L"Playlist_DefaultSolo")) Playlist->MaxSquadSize=1;
        }
        if (FConfiguration::bForceRespawns)
        {
            if (Playlist->HasbRespawnInAir())
                Playlist->bRespawnInAir = true;
            if (Playlist->HasRespawnHeight())
            {
                Playlist->RespawnHeight.Curve.CurveTable = nullptr;
                Playlist->RespawnHeight.Curve.RowName = FName();
                Playlist->RespawnHeight.Value = 20000;
            }
            if (Playlist->HasRespawnTime())
            {
                Playlist->RespawnTime.Curve.CurveTable = nullptr;
                Playlist->RespawnTime.Curve.RowName = FName();
                Playlist->RespawnTime.Value = 3;
            }
            Playlist->RespawnType = 2; // InfiniteRespawnExceptStorm
            //if (Playlist->HasbForceRespawnLocationInsideOfVolume())
            //    Playlist->bForceRespawnLocationInsideOfVolume = true;
        }
        if (FConfiguration::bJoinInProgress)
        {
            if (Playlist->HasbAllowJoinInProgress())
                Playlist->bAllowJoinInProgress = true;
            if (Playlist->HasJoinInProgressMatchType())
                Playlist->JoinInProgressMatchType = UKismetTextLibrary::Conv_StringToText(FString(L"Creative"));
        }
        //if (VersionInfo.FortniteVersion >= 16)
        {
            if (Playlist->HasGarbageCollectionFrequency())
                Playlist->GarbageCollectionFrequency = 9999999999999999.f; // easier than hooking collectgarbage
            if (GameMode->HasPlaylistHotfixOriginalGCFrequency())
                GameMode->PlaylistHotfixOriginalGCFrequency = 9999999999999999.f;
            if (GameMode->HasbDisableGCOnServerDuringMatch())
                GameMode->bDisableGCOnServerDuringMatch = true;
            if (GameMode->HasbPlaylistHotfixChangedGCDisabling())
                GameMode->bPlaylistHotfixChangedGCDisabling = true;
        }
        if (GameState->HasCurrentPlaylistInfo())
        {
            //if (VersionInfo.EngineVersion >= 4.27)
            GameState->CurrentPlaylistInfo.BasePlaylist = Playlist;
            GameState->CurrentPlaylistInfo.PlaylistReplicationKey++;
            GameState->CurrentPlaylistInfo.MarkArrayDirty();
            GameState->OnRep_CurrentPlaylistInfo();
        }
        else if (GameState->HasCurrentPlaylistData())
        {
            GameState->CurrentPlaylistData = Playlist;
            GameState->OnRep_CurrentPlaylistData();
        }

        GameMode->CurrentPlaylistId = Playlist->PlaylistId;
        if (GameState->HasCurrentPlaylistId())
            GameState->CurrentPlaylistId = Playlist->PlaylistId;
        if (GameMode->HasCurrentPlaylistName())
            GameMode->CurrentPlaylistName = Playlist->PlaylistName;

        if (GameMode->GameSession->HasMaxPlayers())
            GameMode->GameSession->MaxPlayers = Playlist->MaxPlayers;


        if (GameState->HasAirCraftBehavior() && Playlist->HasAirCraftBehavior())
            GameState->AirCraftBehavior = Playlist->AirCraftBehavior;
        if (GameState->HasCachedSafeZoneStartUp() && Playlist->HasSafeZoneStartUp())
            GameState->CachedSafeZoneStartUp = Playlist->SafeZoneStartUp;

        if (GameMode->HasbEnableDBNO())
            GameMode->bEnableDBNO = Playlist->MaxSquadSize > 1;

        bIsLargeTeamGame = Playlist->bIsLargeTeamGame;

        //if (GameState->HasAdditionalPlaylistLevelsStreamed())
        //    GameState->OnRep_AdditionalPlaylistLevelsStreamed();
    }
    else
    {
        GameState->CurrentPlaylistId = GameMode->CurrentPlaylistId = 0;

        if (GameMode->GameSession->HasMaxPlayers())
            GameMode->GameSession->MaxPlayers = 100;
    }
}


void (*VendWobble__FinishedFuncOG)(UObject* Context, FFrame& Stack);
void VendWobble__FinishedFunc(UObject* Context, FFrame& Stack)
{
    auto CollectorActor = (ABuildingItemCollectorActor*)Context;
    auto PlayerController = CollectorActor->ControllingPlayer;

    if (!PlayerController)
        return VendWobble__FinishedFuncOG(Context, Stack);

    auto Collection = CollectorActor->ItemCollections.Search([&](FCollectorUnitInfo& Coll)
        {
            return Coll.InputItem == CollectorActor->ClientPausedActiveInputItem;
        }, FCollectorUnitInfo::Size());

    if (!Collection)
        return VendWobble__FinishedFuncOG(Context, Stack);

    CollectorActor->ClientPausedActiveInputItem = nullptr;

    float Cost = Collection->InputCount.Evaluate();

    auto VMLoc = CollectorActor->K2_GetActorLocation();
    auto& SpawnLocation = CollectorActor->LootSpawnLocation;
    auto Loc = VMLoc + (CollectorActor->GetActorForwardVector() * SpawnLocation.X) + (CollectorActor->GetActorRightVector() * SpawnLocation.Y) + (CollectorActor->GetActorUpVector() * SpawnLocation.Z);

    for (int i = 0; i < Collection->OutputItemEntry.Num(); i++)
    {
        auto& Item = Collection->OutputItemEntry.Get(i, FFortItemEntry::Size());

        AFortInventory::SpawnPickup(Loc, Item);
        if (CollectorActor->HasPickupSpawned())
            CollectorActor->PickupSpawned.Process();
    }

    /*if (Cost == 0)
    {
        CollectorActor->DoVendDeath();
        CollectorActor->K2_DestroyActor();
    }*/

    return VendWobble__FinishedFuncOG(Context, Stack);
}

std::unordered_map<int, float> WeightMap;
float Sum = 0;
float Weight;
float TotalWeight;

class AFortAthenaLivingWorldStaticPointProvider : public AActor
{
public:
    UCLASS_COMMON_MEMBERS(AFortAthenaLivingWorldStaticPointProvider);

    DEFINE_PROP(FiltersTags, FGameplayTagContainer);
};

class AFortAthenaVehicleSpawner : public AActor
{
public:
    UCLASS_COMMON_MEMBERS(AFortAthenaVehicleSpawner);

    DEFINE_FUNC(GetVehicleClass, UClass*);
};

static bool SyndicateListenNow(AFortGameMode* GameMode)
{
        bool Ok = false;
        auto World = UWorld::GetWorld();
        auto Engine = UEngine::GetEngine();
        auto NetDriverName = FName(L"GameNetDriver");

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
        if (VersionInfo.FortniteVersion >= 20)
            NetDriver->NetServerMaxTickRate = 30;

        NetDriver->NetDriverName = NetDriverName;
        NetDriver->World = World;

        if (VersionInfo.EngineVersion >= 5.3 && FConfiguration::bEnableIris)
        {
            *(bool*)(__int64(&NetDriver->ReplicationDriver) + 0x11) = true;
        }

        NetDriver->NetDriverName = NetDriverName;
        NetDriver->World = World;

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

        auto InitListen = (bool (*)(UNetDriver*, UWorld*, FURL*, bool, FString&)) FindInitListen();
        auto SetWorld = (void (*)(UNetDriver*, UWorld*)) FindSetWorld();

        SetWorld(NetDriver, World);
        FString Err;
        if (InitListen(NetDriver, World, URL, false, Err)) { SetWorld(NetDriver, World); SyndicateListening = true; Ok = true; void SyndicateStwBeaconStart(); SyndicateStwBeaconStart(); }
        else
            printf("Failed to listen (match start, port %d): %s\n", int(FConfiguration::Port), Err.ToString().c_str());

        free(URL);
        return Ok;
}

// Pre-season (1.7.2): keep the island in daylight. Its day/night clock starts when the match server loads the map,
// and fast queue loads the next match in the background while you play, so a server can sit idle for many minutes
// before its bus. By then it is night, and 1.7.2's night has almost no light: players, the bus, gliders and trees
// draw as black silhouettes. Set late morning and stop the clock when players join and when the bus starts.
static void SyndicateKeepPreseasonDaylight(const char* Where)
{
    if (VersionInfo.FortniteVersion != 1.72)
        return;
    auto World = UWorld::GetWorld();
    if (!World)
        return;
    UFortKismetLibrary::SetTimeOfDaySpeed(World, 0.f);
    UFortKismetLibrary::SetTimeOfDay(World, 11.f);
    static int Logged = 0;
    if (Logged++ < 4)
        printf("[Syndicate] Pre-season daylight: time of day 11:00, clock stopped (%s)\n", Where);
}

void AFortGameMode::ReadyToStartMatch_(UObject* Context, FFrame& Stack, bool* Ret)
{
    Stack.IncrementCode();

    static auto FrontendMode = FindClass("FortGameModeFrontend");
    if (Context->IsA(FrontendMode))
    {
        *Ret = callOGWithRet(((AFortGameMode*)Context), Stack.GetCurrentNativeFunction(), ReadyToStartMatch);
        return;
    }
    // Save the World (dev launcher): after the tutorial reservation the host travels to the zone map
    // (Misc.cpp SyndicateReserveHook). There, only listen again and leave the match flow to the zone game mode.
    extern bool SyndicateStwZoneTravel;
    static auto ZoneMode = FindClass("FortGameModeZone");
    if (SyndicateStwZoneTravel && ZoneMode && Context->IsA(ZoneMode))
    {
        // LoadMap shuts the island's GameNetDriver down and its own Listen fails, so make a fresh one here.
        // The port can still be held for a moment after the old socket closes: retry on later polls.
        static int Tries = 0;
        if (!SyndicateListening && Tries < 600)
        {
            auto World = UWorld::GetWorld();
            if (World && World->NetDriver)
            {
                SyndicateListening = true;
                printf("[Syndicate STW] zone already has a net driver, using it\n");
            }
            else if (SyndicateListenNow((AFortGameMode*)Context))
                printf("[Syndicate STW] zone game mode is up, listening for the player (try %d)\n", Tries + 1);
            Tries++;
        }
        *Ret = callOGWithRet(((AFortGameMode*)Context), Stack.GetCurrentNativeFunction(), ReadyToStartMatch);
        return;
    }

    auto GameMode = Context->Cast<AFortGameMode>();

    auto GameState = GameMode->GameState;

    static bool setup = false;
    if (GameMode->HasWarmupRequiredPlayerCount() ? GameMode->WarmupRequiredPlayerCount != 1 : !setup)
    {
        setup = true;

        // Seasons 6+ already listen during map load (Misc.cpp Listen hook); a second bind on the same port fails.
        if (!SyndicateListening)
            SyndicateListenNow(GameMode);

        if (GameMode->HasWarmupRequiredPlayerCount())
            GameMode->WarmupRequiredPlayerCount = 1;

        if (VersionInfo.FortniteVersion > 4.0/* && (VersionInfo.EngineVersion != 4.25)*/)
            SetupPlaylist(GameMode, GameState);


        if(VersionInfo.FortniteVersion==1.11 || VersionInfo.FortniteVersion==2.5 || (VersionInfo.FortniteVersion==1.72 || VersionInfo.FortniteVersion==1.8 || VersionInfo.FortniteVersion==1.81 || VersionInfo.FortniteVersion==1.82))
        {
            const int Size=SyndicateSelectedTeamSize();
            if(GameMode->HasbEnableDBNO())GameMode->bEnableDBNO=Size>1;
            if(GameState->HasCurrentPlaylistId())GameState->CurrentPlaylistId=Size==4?9:Size==2?10:2;
            printf("[Syndicate] Legacy playlist team size: %d\n",Size);
        }

        auto Playlist = SyndicatePlaylist();

        if (!Playlist)
            Playlist = FindObject<UFortPlaylistAthena>(L"/Game/Athena/Playlists/Playlist_DefaultSolo.Playlist_DefaultSolo");

        if (Playlist)
        {
            auto AdditionalPlaylistLevelsStreamed__Off = GameState->GetOffset("AdditionalPlaylistLevelsStreamed");
            auto AdditionalLevelStruct = FAdditionalLevelStreamed::StaticStruct();
            if (Playlist->HasAdditionalLevels())
                for (auto& Level : Playlist->AdditionalLevels)
                {
                    bool Success = false;
                    ULevelStreamingDynamic::LoadLevelInstanceBySoftObjectPtr(UWorld::GetWorld(), Level, FVector(), FRotator(), &Success, FString(), nullptr);
                    if (AdditionalLevelStruct)
                    {
                        auto level = (FAdditionalLevelStreamed*)malloc(FAdditionalLevelStreamed::Size());
                        memset((PBYTE)level, 0, FAdditionalLevelStreamed::Size());
                        level->bIsServerOnly = false;
                        level->LevelName = Level.ObjectID.AssetPathName;
                        if (Success)
                            GameState->AdditionalPlaylistLevelsStreamed.Add(*level, FAdditionalLevelStreamed::Size());
                        free(level);
                    }
                    else
                        GetFromOffset<TArray<FName>>(GameState, AdditionalPlaylistLevelsStreamed__Off).Add(Level.ObjectID.AssetPathName);
                }

            if (Playlist->HasAdditionalLevelsServerOnly())
                for (auto& Level : Playlist->AdditionalLevelsServerOnly)
                {
                    bool Success = false;
                    ULevelStreamingDynamic::LoadLevelInstanceBySoftObjectPtr(UWorld::GetWorld(), Level, FVector(), FRotator(), &Success, FString(), nullptr);

                    if (AdditionalLevelStruct)
                    {

                        auto level = (FAdditionalLevelStreamed*)malloc(FAdditionalLevelStreamed::Size());
                        memset((PBYTE)level, 0, FAdditionalLevelStreamed::Size());
                        level->bIsServerOnly = true;
                        level->LevelName = Level.ObjectID.AssetPathName;
                        if (Success)
                            GameState->AdditionalPlaylistLevelsStreamed.Add(*level, FAdditionalLevelStreamed::Size());
                        free(level);
                    }
                    else
                        GetFromOffset<TArray<FName>>(GameState, AdditionalPlaylistLevelsStreamed__Off).Add(Level.ObjectID.AssetPathName);
                }
        }

        // misc C1 poi things
        if (VersionInfo.FortniteVersion >= 6 && VersionInfo.FortniteVersion < 7)
        {
            if (VersionInfo.FortniteVersion > 6.10)
                ShowFoundation(VersionInfo.FortniteVersion <= 6.21 ? FindObject<ABuildingFoundation>(L"/Game/Athena/Maps/Athena_POI_Foundations.Athena_POI_Foundations.PersistentLevel.LF_Lake1") : FindObject<ABuildingFoundation>(L"/Game/Athena/Maps/Athena_POI_Foundations.Athena_POI_Foundations.PersistentLevel.LF_Lake2"));
            else
                ShowFoundation(FindObject<ABuildingFoundation>(L"/Game/Athena/Maps/Athena_POI_Foundations.Athena_POI_Foundations.PersistentLevel.LF_Athena_StreamingTest12"));

            ShowFoundation(VersionInfo.FortniteVersion <= 6.10 ? FindObject<ABuildingFoundation>(L"/Game/Athena/Maps/Athena_POI_Foundations.Athena_POI_Foundations.PersistentLevel.LF_Athena_StreamingTest13") : FindObject<ABuildingFoundation>(L"/Game/Athena/Maps/Athena_POI_Foundations.Athena_POI_Foundations.PersistentLevel.LF_FloatingIsland"));

            auto IslandScripting = TUObjectArray::FindFirstObject("BP_IslandScripting_C");
            if (IslandScripting)
            {
                auto UpdateMapOffset = IslandScripting->GetOffset("UpdateMap");
                if (UpdateMapOffset != -1)
                {
                    *(bool*)(__int64(IslandScripting) + UpdateMapOffset) = true;
                    IslandScripting->ProcessEvent(IslandScripting->GetFunction("OnRep_UpdateMap"), nullptr);
                }
            }
        }
        else if (VersionInfo.FortniteVersion >= 7 && VersionInfo.FortniteVersion < 8)
        {
            ShowFoundation(FindObject<ABuildingFoundation>("/Game/Athena/Maps/Athena_POI_Foundations.Athena_POI_Foundations.PersistentLevel.LF_Athena_POI_25x36"));
            ShowFoundation(FindObject<ABuildingFoundation>("/Game/Athena/Maps/Athena_POI_Foundations.Athena_POI_Foundations.PersistentLevel.ShopsNew"));
        }
        else if (VersionInfo.FortniteVersion >= 8 && VersionInfo.FortniteVersion < 10)
            ShowFoundation(FindObject<ABuildingFoundation>(L"/Game/Athena/Maps/Athena_POI_Foundations.Athena_POI_Foundations.PersistentLevel.LF_Athena_POI_50x53_Volcano"));
        else if (VersionInfo.FortniteVersion >= 10.20 && VersionInfo.FortniteVersion < 11)
            ShowFoundation(FindObject<ABuildingFoundation>(L"/Game/Athena/Maps/Athena_POI_Foundations.Athena_POI_Foundations.PersistentLevel.LF_Athena_POI_50x53_Volcano"));

        if (VersionInfo.FortniteVersion >= 7 && VersionInfo.FortniteVersion <= 10)
            ShowFoundation(FindObject<ABuildingFoundation>(L"/Game/Athena/Maps/Athena_POI_Foundations.Athena_POI_Foundations.PersistentLevel.SLAB_2"));
        else if (VersionInfo.EngineVersion == 4.23) // rest of S10
            ShowFoundation(FindObject<ABuildingFoundation>(L"/Game/Athena/Maps/Athena_POI_Foundations.Athena_POI_Foundations.PersistentLevel.SLAB_4"));

        // Gotham City swap is off: Season X stays on Tilted Town (set to true to switch to Gotham City).
        constexpr bool SyndicateUseGothamCity = false;
        if (SyndicateUseGothamCity && VersionInfo.FortniteVersion >= 10.30 && VersionInfo.FortniteVersion < 11)
            SyndicateSeasonXGotham();

        bool bEvent = false;
        if (Playlist && Playlist->HasGameplayTagContainer())
        {
            for (int i = 0; i < Playlist->GameplayTagContainer.GameplayTags.Num(); i++)
            {
                auto& PlaylistTag = Playlist->GameplayTagContainer.GameplayTags.Get(i, FGameplayTag::Size());

                if (PlaylistTag.TagName.ToString() == "Athena.Playlist.SpecialEvent")
                {
                    bEvent = true;
                    if (VersionInfo.FortniteVersion == 7.30)
                        ShowFoundation(FindObject<ABuildingFoundation>("/Game/Athena/Apollo/Maps/Athena_POI_Foundations.Athena_POI_Foundations.PersistentLevel.PleasentParkFestivus"));

                    break;
                }
            }
        }

        if (VersionInfo.FortniteVersion == 12.41)
        {
            ShowFoundation(FindObject<ABuildingFoundation>("/Game/Athena/Apollo/Maps/Apollo_POI_Foundations.Apollo_POI_Foundations.PersistentLevel.LF_Athena_POI_19x19_2"));
            ShowFoundation(FindObject<ABuildingFoundation>("/Game/Athena/Apollo/Maps/Apollo_POI_Foundations.Apollo_POI_Foundations.PersistentLevel.BP_Jerky_Head6_18"));
            ShowFoundation(FindObject<ABuildingFoundation>("/Game/Athena/Apollo/Maps/Apollo_POI_Foundations.Apollo_POI_Foundations.PersistentLevel.BP_Jerky_Head5_14"));
            ShowFoundation(FindObject<ABuildingFoundation>("/Game/Athena/Apollo/Maps/Apollo_POI_Foundations.Apollo_POI_Foundations.PersistentLevel.BP_Jerky_Head3_8"));
            ShowFoundation(FindObject<ABuildingFoundation>("/Game/Athena/Apollo/Maps/Apollo_POI_Foundations.Apollo_POI_Foundations.PersistentLevel.BP_Jerky_Head_2"));
            ShowFoundation(FindObject<ABuildingFoundation>("/Game/Athena/Apollo/Maps/Apollo_POI_Foundations.Apollo_POI_Foundations.PersistentLevel.BP_Jerky_Head4_11"));
        }

        if (VersionInfo.FortniteVersion == 7.30 && !bEvent)
            ShowFoundation(FindObject<ABuildingFoundation>("/Game/Athena/Apollo/Maps/Athena_POI_Foundations.Athena_POI_Foundations.PersistentLevel.PleasentParkDefault"));


        if (VersionInfo.FortniteVersion == 17.50)
        {
            //ShowFoundation(FindObject<ABuildingFoundation>(L"/Game/Athena/Apollo/Maps/Apollo_Mother.Apollo_Mother.PersistentLevel.farmbase_2"));
            ShowFoundation(FindObject<ABuildingFoundation>(L"/Game/Athena/Apollo/Maps/Apollo_Mother.Apollo_Mother.PersistentLevel.Farm_Phase_03"));
        }

        if (VersionInfo.EngineVersion >= 4.27 && std::floor(VersionInfo.FortniteVersion) != 20) // on 20 it does some weird stuff
        {
            auto MeshNetworkSubsystem = TUObjectArray::FindFirstObject("MeshNetworkSubsystem");

            if (MeshNetworkSubsystem)
                *(uint8_t*)(__int64(MeshNetworkSubsystem) + MeshNetworkSubsystem->GetOffset("NodeType")) = 2;
        }

        auto AIDirectorClass = GameMode->HasWarmupRequiredPlayerCount() ? FindClass("AthenaAIDirector") : FindObject<UClass>("/Game/AIDirector/AIDirector_Fortnite.AIDirector_Fortnite_C");
        if (!AIDirectorClass)
            AIDirectorClass = FindClass("FortAIDirector");

        GameMode->AIDirector = UWorld::SpawnActor(AIDirectorClass, FVector{}, GameMode);
        if (GameMode->AIDirector)
            GameMode->AIDirector->Call(GameMode->AIDirector->GetFunction("Activate"));

        // Chapter 2 henchmen/bosses (The Agency etc. — defeating a boss drops the keycard that opens its vault):
        // the native AI Director is active above, but Syndicate does not yet spawn the POI henchmen/bosses. Before
        // building that spawner, list — once — the AI spawner-data and henchmen/sentinel classes this build actually
        // exposes (printed to the server stdout log), so it can be built against real class names instead of guesses.
        if (VersionInfo.FortniteVersion >= 11)
        {
            static bool LoggedAI = false;
            if (!LoggedAI)
            {
                LoggedAI = true;
                std::unordered_map<std::string, int> Counts;
                for (int I = 0; I < TUObjectArray::Num(); I++)
                {
                    auto Obj = TUObjectArray::GetObjectByIndex(I);
                    if (!Obj || !Obj->Class) continue;
                    auto Name = std::string(Obj->Class->Name.ToString().c_str());
                    std::string Low; for (char c : Name) Low += char(tolower((unsigned char)c));
                    if (Low.find("aispawner") != std::string::npos || Low.find("spawnerdata") != std::string::npos ||
                        Low.find("aicharacter") != std::string::npos || Low.find("henchman") != std::string::npos ||
                        Low.find("sentinel") != std::string::npos || Low.find("guarddata") != std::string::npos ||
                        (Low.find("spawner") != std::string::npos && Low.find("ai") != std::string::npos))
                        Counts[Name]++;
                }
                for (auto& Pair : Counts) printf("[Syndicate][AIDiag] class %s x%d\n", Pair.first.c_str(), Pair.second);
                printf("[Syndicate][AIDiag] listed %d AI spawner/henchmen class kinds for season %d\n", (int)Counts.size(), (int)VersionInfo.FortniteVersion);

                // Dump the spawn API of the key classes (functions + config fields + the base-class chain) so the
                // real henchman/boss spawner can be built against actual names rather than guesses. Read-only.
                auto DumpClass = [&](const char* ClassName)
                {
                    auto C = FindClass(ClassName);
                    if (!C) { printf("[Syndicate][AIDiag] %s: not found\n", ClassName); return; }
                    std::string Chain; for (auto S = (const UStruct*)C; S; S = (const UStruct*)S->GetSuper()) { Chain += "/"; Chain += ((const UObject*)S)->Name.ToString().c_str(); }
                    printf("[Syndicate][AIDiag] %s super-chain %s\n", ClassName, Chain.c_str());
                    int fn = 0; for (const UField* F = ((const UStruct*)C)->GetChildren(); F && fn < 70; F = F->GetNext(), ++fn)
                        printf("[Syndicate][AIDiag] %s fn %s\n", ClassName, F->GetName().ToString().c_str());
                    if (VersionInfo.FortniteVersion >= 12.10)
                    { int pr = 0; for (const UField* P = ((const UStruct*)C)->GetChildProperties(); P && pr < 80; P = P->FField_GetNext(), ++pr)
                        printf("[Syndicate][AIDiag] %s prop %s\n", ClassName, P->FField_GetName().ToString().c_str()); }
                };
                for (const char* Cn : { "FortAthenaAISpawnerData", "FortAthenaCharacterAISpawnerData", "FortAthenaNPCSpawnerData",
                                        "FortAISpawnerActorBase", "BuildingProp_AISpawner", "FortAthenaAISpawnerDataComponent",
                                        "AthenaAIDirector", "FortAIDirector" })
                    DumpClass(Cn);
                // The spawner-data INSTANCES carry the per-POI config (which henchman, boss flag, count, location).
                // List them (path + whether it is just the class default) so the configured ones can be found.
                if (auto DataClass = FindClass("FortAthenaAISpawnerData"))
                {
                    int shown = 0;
                    for (int I = 0; I < TUObjectArray::Num() && shown < 40; I++)
                    {
                        auto Obj = TUObjectArray::GetObjectByIndex(I);
                        if (!Obj || !Obj->Class || !Obj->IsA(DataClass)) continue;
                        printf("[Syndicate][AIDiag] spawnerdata instance %s class=%s cdo=%d\n", Obj->Name.ToString().c_str(), Obj->Class->Name.ToString().c_str(), (int)Obj->IsDefaultObject());
                        ++shown;
                    }
                    printf("[Syndicate][AIDiag] %d FortAthenaAISpawnerData instances\n", shown);
                }

                // What the default data objects point at (pawn class, behaviour tree, spawn params). This tells us
                // the native pawn/controller to use when we create a henchman ourselves.
                if (VersionInfo.FortniteVersion >= 12.0)
                {
                    for (const char* Cn : { "FortAthenaAISpawnerData", "FortAthenaNPCSpawnerData", "FortAthenaPlayerBotSpawnerData" })
                    {
                        auto C = FindClass(Cn); auto CDO = C ? C->GetDefaultObj() : nullptr; if (!CDO) continue;
                        for (const char* Prop : { "PawnClass", "BotData", "BehaviorComponent", "SpawnParamsComponent" })
                        {
                            auto Off = CDO->GetOffset(Prop); if (Off == uint32(-1)) continue;
                            auto Value = GetFromOffset<const UObject*>(CDO, Off);
                            printf("[Syndicate][AIDiag] %s default %s=%s class=%s\n", Cn, Prop,
                                Value ? Value->Name.ToString().c_str() : "null", Value && Value->Class ? Value->Class->Name.ToString().c_str() : "-");
                        }
                    }
                    if (auto C = FindClass("FortAthenaAISpawnerDataComponent_Behavior"))
                    {
                        auto CDO = C->GetDefaultObj();
                        if (CDO && CDO->GetOffset("BehaviorTree") != uint32(-1))
                        {
                            auto Tree = GetFromOffset<const UObject*>(CDO, CDO->GetOffset("BehaviorTree"));
                            printf("[Syndicate][AIDiag] Behavior default BehaviorTree=%s class=%s\n",
                                Tree ? Tree->Name.ToString().c_str() : "null", Tree && Tree->Class ? Tree->Class->Name.ToString().c_str() : "-");
                        }
                    }

                    // Native spawner data + bot manager. This is the same path the game uses for POI henchmen:
                    // SpawnAI builds the pawn, controller, behaviour tree, cosmetics and inventory itself.
                    static bool TriedNativeHenchman = false;
                    // Disabled: the half-initialised native bot later crashes the match server. The dump above
                    // stays (read-only); re-enable only after the full OGS BotManagerSetup/blackboard/nav port.
                    if (false && !TriedNativeHenchman)
                    {
                        TriedNativeHenchman = true;
                        auto BotManager = (UFortServerBotManagerAthena*)GameMode->ServerBotManager;
                        if (!BotManager)
                        {
                            BotManager = (UFortServerBotManagerAthena*)UGameplayStatics::SpawnObject(UFortServerBotManagerAthena::StaticClass(), GameMode);
                            if (BotManager)
                            {
                                GameMode->ServerBotManager = BotManager;
                                BotManager->CachedGameMode = GameMode;
                                BotManager->CachedGameState = GameState;
                                auto MutatorClass = FindClass("FortAthenaMutator_Bots");
                                if (MutatorClass)
                                {
                                    auto Mutator = (AActor*)UWorld::SpawnActor(MutatorClass, FVector{});
                                    BotManager->CachedBotMutator = Mutator;
                                    if (Mutator)
                                    {
                                        if (auto Off = Mutator->GetOffset("CachedGameMode"); Off != uint32(-1)) GetFromOffset<AActor*>(Mutator, Off) = GameMode;
                                        if (auto Off = Mutator->GetOffset("CachedGameState"); Off != uint32(-1)) GetFromOffset<AActor*>(Mutator, Off) = GameState;
                                    }
                                }
                                auto GoalClass = FindClass("FortAIGoalManager");
                                if (GoalClass) GameMode->AIGoalManager = (AActor*)UWorld::SpawnActor(GoalClass, FVector{});
                            }
                        }

                        auto DataClass = FindClass("FortAthenaNPCSpawnerData");
                        auto Data = DataClass ? (UObject*)UGameplayStatics::SpawnObject(DataClass, GameMode) : nullptr;
                        auto PawnClass = FindClass("FortPlayerPawnAthena");
                        if (Data && PawnClass)
                        {
                            if (auto Off = Data->GetOffset("PawnClass"); Off != uint32(-1)) GetFromOffset<const UClass*>(Data, Off) = PawnClass;
                            auto BotDataClass = FindClass("FortAthenaAIBotCustomizationData");
                            auto BotData = BotDataClass ? (UObject*)StaticLoadObject(L"/Game/Athena/AI/MANG/BotData/BotData_MANG_POI_HDP.BotData_MANG_POI_HDP", BotDataClass) : nullptr;
                            if (BotData) if (auto Off = Data->GetOffset("BotData"); Off != uint32(-1)) GetFromOffset<const UObject*>(Data, Off) = BotData;

                            auto BehaviorClass = FindClass("AthenaAISpawnerDataComponent_Behavior");
                            if (!BehaviorClass) BehaviorClass = FindClass("FortAthenaAISpawnerDataComponent_Behavior");
                            auto Behavior = BehaviorClass ? (UObject*)UGameplayStatics::SpawnObject(BehaviorClass, Data) : nullptr;
                            auto TreeClass = FindClass("BehaviorTree");
                            auto Tree = TreeClass ? (UObject*)StaticLoadObject(L"/Game/Athena/AI/MANG/BehaviorTree/BT_MANG2.BT_MANG2", TreeClass) : nullptr;
                            if (Behavior && Tree)
                            {
                                if (auto Off = Behavior->GetOffset("BehaviorTree"); Off != uint32(-1)) GetFromOffset<const UObject*>(Behavior, Off) = Tree;
                                if (auto Off = Data->GetOffset("BehaviorComponent"); Off != uint32(-1)) GetFromOffset<const UObject*>(Data, Off) = Behavior;
                            }
                            auto ParamsClass = FindClass("FortAthenaAISpawnerDataComponent_SpawnParams");
                            if (ParamsClass)
                            {
                                auto Params = (UObject*)UGameplayStatics::SpawnObject(ParamsClass, Data);
                                if (Params) if (auto Off = Data->GetOffset("SpawnParamsComponent"); Off != uint32(-1)) GetFromOffset<const UObject*>(Data, Off) = Params;
                            }
                            if (auto Off = Data->GetOffset("ComponentsList"); Off != uint32(-1))
                            {
                                auto& List = GetFromOffset<TArray<UObject*>>(Data, Off);
                                if (Behavior) List.Add(Behavior);
                                if (auto POff = Data->GetOffset("SpawnParamsComponent"); POff != uint32(-1))
                                    if (auto P = GetFromOffset<const UObject*>(Data, POff)) List.Add((UObject*)P);
                                printf("[Syndicate][AITest] components=%d\n", List.Num());
                            }

                            printf("[Syndicate][AITest] native spawner data=%s botdata=%s behavior=%s tree=%s botmanager=%s mutator=%s\n",
                                Data->Class->Name.ToString().c_str(), BotData ? BotData->Name.ToString().c_str() : "missing",
                                Behavior ? Behavior->Class->Name.ToString().c_str() : "missing", Tree ? Tree->Name.ToString().c_str() : "missing",
                                BotManager ? "ok" : "missing", BotManager && BotManager->CachedBotMutator ? "ok" : "missing");

                            auto BotMutator = BotManager ? (AActor*)BotManager->CachedBotMutator : nullptr;
                            auto Fn = BotMutator ? BotMutator->GetFunction("SpawnBot") : nullptr;
                            if (Fn)
                            {
                                FVector Location(5632.0, 5120.0, -1200.0);
                                if (auto PCClass = FindClass("FortPlayerControllerAthena"))
                                {
                                    TArray<AActor*> Controllers; Utils::GetAll(PCClass, Controllers);
                                    for (auto A : Controllers)
                                    {
                                        auto PC = (AFortPlayerControllerAthena*)A;
                                        if (PC && PC->MyFortPawn)
                                        {
                                            Location = PC->MyFortPawn->K2_GetActorLocation();
                                            Location.X += 250.0; Location.Z += 60.0;
                                            break;
                                        }
                                    }
                                    Controllers.Free();
                                }
                                printf("[Syndicate][AITest] spawn location=%.0f,%.0f,%.0f\n", Location.X, Location.Y, Location.Z);
                                auto DefClass = FindClass("DefaultPawn");
                                auto Locator = DefClass ? (AActor*)UWorld::SpawnActor(DefClass, Location) : nullptr;
                                if (!Locator) Locator = (AActor*)GameMode;

                                auto Params = Fn->GetParamsNamed();
                                std::vector<uint8> Buffer(Params.Size, 0);
                                FRotator Rotation{}; uint8 Snap = 1;
                                int ReturnOffset = -1;
                                for (auto& P : Params.NameOffsetMap)
                                {
                                    if (P.PropertyFlags & 0x400) { ReturnOffset = (int)P.Offset; continue; }
                                    if (P.Name == "BotPawnClass" && P.Offset + sizeof(void*) <= Buffer.size()) { auto Value = PawnClass; memcpy(Buffer.data() + P.Offset, &Value, sizeof(void*)); }
                                    else if (P.Name == "InSpawnLocator" && P.Offset + sizeof(void*) <= Buffer.size()) { auto Value = Locator; memcpy(Buffer.data() + P.Offset, &Value, sizeof(void*)); }
                                    else if (P.Name == "InSpawnLocation" && P.Offset + sizeof(FVector) <= Buffer.size()) memcpy(Buffer.data() + P.Offset, &Location, sizeof(FVector));
                                    else if (P.Name == "InSpawnRotation" && P.Offset + sizeof(FRotator) <= Buffer.size()) memcpy(Buffer.data() + P.Offset, &Rotation, sizeof(FRotator));
                                    else if (P.Name == "bSnapToGround" && P.Offset < Buffer.size()) Buffer[P.Offset] = Snap;
                                }
                                BotMutator->ProcessEvent(Fn, Buffer.data());
                                auto NewPawn = (ReturnOffset >= 0 && ReturnOffset + sizeof(void*) <= (int)Buffer.size()) ? *(AFortPlayerPawnAthena**)(Buffer.data() + ReturnOffset) : nullptr;
                                if (NewPawn)
                                {
                                    NewPawn->bReplicates = true; NewPawn->bAlwaysRelevant = true; NewPawn->bCanBeDamaged = true;
                                    NewPawn->NetCullDistanceSquared = 625000000.f; NewPawn->SetHealth(100.f); NewPawn->ForceNetUpdate();
                                    if (!NewPawn->Controller)
                                    {
                                        auto BotPCClass = FindClass("FortAthenaAIBotController");
                                        if (!BotPCClass) BotPCClass = FindClass("AthenaAIController");
                                        if (BotPCClass)
                                        {
                                            NewPawn->AIControllerClass = BotPCClass;
                                            if (auto Spawn = NewPawn->GetFunction("SpawnDefaultController")) NewPawn->ProcessEvent(Spawn, nullptr);
                                        }
                                    }
                                    auto PC = NewPawn->Controller;
                                    auto State = (AFortPlayerStateAthena*)NewPawn->PlayerState;
                                    if (State) { State->TeamIndex = 10; State->SquadId = 10; if (State->HasbIsABot()) State->bIsABot = true; State->ForceNetUpdate(); }
                                    if (State)
                                    {
                                        auto Outfit = (UAthenaCharacterItemDefinition*)StaticLoadObject(
                                            L"/Game/Athena/Items/Cosmetics/Characters/CID_NPC_Athena_Commando_M_HenchmanBad.CID_NPC_Athena_Commando_M_HenchmanBad",
                                            UAthenaCharacterItemDefinition::StaticClass());
                                        auto Hero = Outfit ? Outfit->HeroDefinition : nullptr;
                                        if (Hero)
                                        {
                                            if (State->HasHeroType()) State->HeroType = Hero;
                                            for (auto& SoftSpec : Hero->Specializations)
                                            {
                                                auto Spec = SoftSpec.Get(); if (!Spec) continue;
                                                for (auto& SoftPart : Spec->CharacterParts)
                                                {
                                                    auto Part = SoftPart.Get();
                                                    if (Part && Part->CharacterPartType < 6) NewPawn->ServerChoosePart(Part->CharacterPartType, Part);
                                                }
                                            }
                                        }
                                        printf("[Syndicate][AITest] henchman outfit=%s hero=%s\n",
                                            Outfit ? Outfit->Name.ToString().c_str() : "missing", Hero ? Hero->Name.ToString().c_str() : "missing");
                                        State->ForceNetUpdate();
                                    }
                                    if (PC && Tree)
                                    {
                                        if (auto Off = PC->GetOffset("BehaviorTree"); Off != uint32(-1)) GetFromOffset<const UObject*>(PC, Off) = Tree;
                                        bool started = false;
                                        if (auto RT = PC->GetFunction("RunBehaviorTree")) started = PC->Call<bool>(RT, (UObject*)Tree);
                                        printf("[Syndicate][AITest] behavior tree started=%d\n", (int)started);
                                    }

                                    // The native AI anim instance is what makes henchmen use their patrol/combat
                                    // animations instead of standing in the default pose.
                                    if (auto MeshOff = NewPawn->GetOffset("Mesh"); MeshOff != uint32(-1))
                                    {
                                        auto Mesh = GetFromOffset<UObject*>(NewPawn, MeshOff);
                                        auto Anim = StaticLoadObject(L"/Game/Athena/AI/MANG/AnimSet/MANG_PatrolLayerAnimBP.MANG_PatrolLayerAnimBP_C", UClass::StaticClass());
                                        if (Mesh && Anim)
                                        {
                                            if (auto Off = Mesh->GetOffset("AnimBlueprintGeneratedClass"); Off != uint32(-1)) GetFromOffset<const UObject*>(Mesh, Off) = Anim;
                                            if (auto Fn = NewPawn->GetFunction("OnRep_AnimBPOverride")) NewPawn->ProcessEvent(Fn, nullptr);
                                            printf("[Syndicate][AITest] anim bp applied\n");
                                        }
                                    }

                                    if (State)
                                    {
                                        if (State->HasbIsSpectator()) State->bIsSpectator = false;
                                        if (State->HasbIsABot()) State->bIsABot = true;
                                    }
                                    if (PC && GameMode->GetOffset("AlivePlayers") != uint32(-1))
                                    {
                                        auto& Alive = GetFromOffset<TArray<AActor*>>(GameMode, GameMode->GetOffset("AlivePlayers"));
                                        Alive.Add((AActor*)PC);
                                    }
                                    NewPawn->bCanBeDamaged = true;
                                    NewPawn->OnRep_PlayerState();
                                    NewPawn->ForceNetUpdate();
                                    printf("[Syndicate][AITest] SpawnBot pawn=%s controller=%s state=%s\n",
                                        NewPawn->Name.ToString().c_str(), PC ? PC->Class->Name.ToString().c_str() : "none",
                                        State ? State->Class->Name.ToString().c_str() : "none");
                                }
                                else printf("[Syndicate][AITest] SpawnBot returned null\n");
                            }
                            else printf("[Syndicate][AITest] SpawnBot function not found on bot mutator\n");
                        }
                        else printf("[Syndicate][AITest] native spawner data=%s pawnclass=%s\n", Data ? "ok" : "missing", PawnClass ? "ok" : "missing");
                    }

                    // === Native 12.50 henchman via UFortServerBotManagerAthena::SpawnBot ===
                    // OGS hooks this exact manager function. It is the native entry point that runs the
                    // full BotManagerSetup (controller, blackboard, nav data, patrol component, inventory
                    // and behaviour tree). The old test called the mutator directly and skipped all of
                    // that, which is why the bot froze and then crashed once its AI ticked.
                    static bool TriedNativeHenchmanV2 = false;
                    if (!TriedNativeHenchmanV2)
                    {
                        TriedNativeHenchmanV2 = true;
                        auto BotManager = (UFortServerBotManagerAthena*)GameMode->ServerBotManager;
                        if (!BotManager)
                        {
                            BotManager = (UFortServerBotManagerAthena*)UGameplayStatics::SpawnObject(UFortServerBotManagerAthena::StaticClass(), GameMode);
                            if (BotManager)
                            {
                                GameMode->ServerBotManager = BotManager;
                                if (auto Off = BotManager->GetOffset("CachedGameMode"); Off != uint32(-1)) GetFromOffset<AActor*>(BotManager, Off) = GameMode;
                                if (auto Off = BotManager->GetOffset("CachedGameState"); Off != uint32(-1)) GetFromOffset<AActor*>(BotManager, Off) = GameState;
                            }
                        }
                        if (auto Off = GameMode->GetOffset("ServerBotManagerClass"); Off != uint32(-1)) GetFromOffset<UClass*>(GameMode, Off) = const_cast<UClass*>(UFortServerBotManagerAthena::StaticClass());
                        if (BotManager)
                        {
                            if (!BotManager->CachedGameMode) BotManager->CachedGameMode = GameMode;
                            if (!BotManager->CachedGameState) BotManager->CachedGameState = GameState;
                            if (!BotManager->CachedBotMutator)
                            {
                                auto MutatorClass = FindClass("FortAthenaMutator_Bots");
                                if (MutatorClass)
                                {
                                    // Prefer the mutator the game mode already registered. A second
                                    // manually spawned mutator has no gameplay mutator registration
                                    // and crashes when the native bot director processes it.
                                    TArray<AActor*> ExistingMutators;
                                    Utils::GetAll(MutatorClass, ExistingMutators);
                                    auto Mutator = ExistingMutators.Num() > 0 ? ExistingMutators[0] : nullptr;
                                    printf("[Syndicate][AITest] bot mutators existing=%d\n", ExistingMutators.Num());
                                    ExistingMutators.Free();
                                    if (!Mutator) Mutator = (AActor*)UWorld::SpawnActor(MutatorClass, FVector{});
                                    BotManager->CachedBotMutator = Mutator;
                                    if (Mutator)
                                    {
                                        if (auto Off = Mutator->GetOffset("CachedGameMode"); Off != uint32(-1)) GetFromOffset<AActor*>(Mutator, Off) = GameMode;
                                        if (auto Off = Mutator->GetOffset("CachedGameState"); Off != uint32(-1)) GetFromOffset<AActor*>(Mutator, Off) = GameState;
                                        printf("[Syndicate][AITest] bot mutator=%s\n", Mutator->Name.ToString().c_str());
                                    }
                                }
                            }
                            if (!GameMode->AIDirector)
                            {
                                auto DirectorClass = FindClass("AthenaAIDirector");
                                if (!DirectorClass) DirectorClass = FindClass("FortAIDirector");
                                if (DirectorClass)
                                {
                                    auto Director = (AActor*)UWorld::SpawnActor(DirectorClass, FVector{});
                                    GameMode->AIDirector = Director;
                                    if (Director) if (auto Activate = Director->GetFunction("Activate")) Director->ProcessEvent(Activate, nullptr);
                                }
                            }
                            if (!GameMode->AIGoalManager)
                            {
                                auto GoalClass = FindClass("FortAIGoalManager");
                                if (GoalClass) GameMode->AIGoalManager = (AActor*)UWorld::SpawnActor(GoalClass, FVector{});
                            }

                            auto BotDataClass = FindClass("FortAthenaAIBotCustomizationData");
                            // Fixed path: the old synchronous asset-registry scan stalled the
                            // game thread for minutes and delayed matchmaking. The deferred
                            // group already loads its four BotData assets by path.
                            std::string BotDataPath = "/Game/Athena/AI/MANG/BotData/BotData_MANG_Sniper_Ego.BotData_MANG_Sniper_Ego";
                            std::wstring BotDataWide(BotDataPath.begin(), BotDataPath.end());
                            auto BotData = BotDataClass && !BotDataPath.empty() ? (UObject*)StaticLoadObject(BotDataWide.c_str(), BotDataClass) : nullptr;
                            if (BotData)
                            {
                                auto PropName = [&](const char* Name) -> std::string {
                                    auto Off = BotData->GetOffset(Name);
                                    if (Off == uint32(-1)) return "none";
                                    auto Obj = GetFromOffset<UObject*>(BotData, Off);
                                    return Obj ? std::string(Obj->Name.ToString().c_str()) : std::string("null");
                                };
                                printf("[Syndicate][AITest] botdata bt=%s inv=%s name=%s pawn=%s\n",
                                    PropName("BehaviorTree").c_str(), PropName("StartupInventory").c_str(),
                                    PropName("BotNameSettings").c_str(), PropName("PawnClass").c_str());
                                if (auto InvOff = BotData->GetOffset("StartupInventory"); InvOff != uint32(-1))
                                {
                                    auto Startup = GetFromOffset<UObject*>(BotData, InvOff);
                                    if (Startup)
                                        if (auto ItemsOff = Startup->GetOffset("Items"); ItemsOff != uint32(-1))
                                        {
                                            auto& Items = GetFromOffset<TArray<UFortItemDefinition*>>(Startup, ItemsOff);
                                            printf("[Syndicate][AITest] botdata inv items=%d\n", Items.Num());
                                            for (int I = 0; I < Items.Num() && I < 12; ++I)
                                                printf("[Syndicate][AITest] botdata item %s\n",
                                                    Items[I] ? Items[I]->Name.ToString().c_str() : "null");
                                        }
                                }
                            }
                            // OGS gets its MANG spawn points from the level spawner actors. List
                            // them so the test spawn uses the same POIs instead of the map center.
                            if (auto SpawnerClass = (UClass*)StaticLoadObject(
                                L"/Game/Athena/AI/MANG/BP_MANG_Spawner.BP_MANG_Spawner_C", UClass::StaticClass()))
                            {
                                TArray<AActor*> Spawners;
                                Utils::GetAll(SpawnerClass, Spawners);
                                printf("[Syndicate][AITest] mang spawners=%d\n", Spawners.Num());
                                for (int I = 0; I < Spawners.Num() && I < 30; ++I)
                                {
                                    auto A = Spawners[I]; if (!A) continue;
                                    auto L = A->K2_GetActorLocation();
                                    printf("[Syndicate][AITest] mang spawner %s loc=%.0f,%.0f,%.0f\n",
                                        A->Name.ToString().c_str(), L.X, L.Y, L.Z);
                                }
                                Spawners.Free();
                            }

                            auto Fn = BotManager->GetFunction("SpawnBot");
                            printf("[Syndicate][AITest] native manager=%s botdata=%s spawnfn=%s director=%s goal=%s\n",
                                BotManager ? "ok" : "missing", BotData ? BotData->Name.ToString().c_str() : "missing",
                                Fn ? "ok" : "missing", GameMode->AIDirector ? "ok" : "missing", GameMode->AIGoalManager ? "ok" : "missing");
                            if (false && BotData && Fn)
                            {
                                FVector Location(5632.0, 5120.0, -1400.0);
                                FRotator Rotation{};
                                FVector PatrolSpawn = Location;
                                SyndicateNativeHenchmanPatrolPath = SyndicateFindHenchmanPatrolPath(Location, PatrolSpawn);
                                if (SyndicateNativeHenchmanPatrolPath) Location = PatrolSpawn;
                                SyndicateEnsureNavigation();
                                struct FRuntimeBotData { FName TagName; float CullDistanceSquared; uint8 bCheckForOverlaps; uint8 Flags; uint8 CustomSquadId; uint8 Pad; };
                                FRuntimeBotData Runtime{};
                                Runtime.TagName = FName(L"Athena.Faction.Alter"); // Shadow henchman
                                Runtime.Flags = 1; // bHasCustomSquadId
                                Runtime.CustomSquadId = 10;
                                std::string ChosenBotName = std::string(BotData->Name.ToString().c_str());
                                if (ChosenBotName.find("_Ego") != std::string::npos)
                                    Runtime.TagName = FName(L"Athena.Faction.Ego");
                                else if (ChosenBotName.find("_Alter") != std::string::npos)
                                    Runtime.TagName = FName(L"Athena.Faction.Alter");

                                auto Params = Fn->GetParamsNamed();
                                std::vector<uint8> Buffer(Params.Size, 0);
                                int ReturnOffset = -1;
                                for (auto& P : Params.NameOffsetMap)
                                {
                                    if (P.PropertyFlags & 0x400) { ReturnOffset = (int)P.Offset; continue; }
                                    if (P.Name == "InSpawnLocation" && P.Offset + sizeof(FVector) <= Buffer.size()) memcpy(Buffer.data() + P.Offset, &Location, sizeof(FVector));
                                    else if (P.Name == "InSpawnRotation" && P.Offset + sizeof(FRotator) <= Buffer.size()) memcpy(Buffer.data() + P.Offset, &Rotation, sizeof(FRotator));
                                    else if (P.Name == "InBotData" && P.Offset + sizeof(void*) <= Buffer.size()) { auto Value = (const UObject*)BotData; memcpy(Buffer.data() + P.Offset, &Value, sizeof(void*)); }
                                    else if (P.Name == "InRuntimeBotData" && P.Offset + sizeof(Runtime) <= Buffer.size()) memcpy(Buffer.data() + P.Offset, &Runtime, sizeof(Runtime));
                                }
                                BotManager->ProcessEvent(Fn, Buffer.data());
                                auto NewPawn = (ReturnOffset >= 0 && ReturnOffset + sizeof(void*) <= (int)Buffer.size()) ? *(AFortPlayerPawnAthena**)(Buffer.data() + ReturnOffset) : nullptr;
                                if (NewPawn)
                                {
                                    auto PC = (AActor*)NewPawn->Controller;
                                    auto State = (AFortPlayerStateAthena*)NewPawn->PlayerState;
                                    NewPawn->bReplicates = true; NewPawn->bAlwaysRelevant = true; NewPawn->bCanBeDamaged = true;
                                    NewPawn->NetCullDistanceSquared = 625000000.f;
                                    NewPawn->SetMaxHealth(100.f); NewPawn->SetHealth(100.f); NewPawn->ForceNetUpdate();
                                    int HumanTeam = -1;
                                    if (State)
                                    {
                                        if (State->HasbIsSpectator()) State->bIsSpectator = false;
                                        if (State->HasbIsABot()) State->bIsABot = true;
                                        // Keep the henchman hostile even if the manager put it on the human team.
                                        if (auto PCClass = FindClass("FortPlayerControllerAthena"))
                                        {
                                            TArray<AActor*> Controllers; Utils::GetAll(PCClass, Controllers);
                                            for (auto A : Controllers)
                                            {
                                                auto HPC = (AFortPlayerControllerAthena*)A;
                                                auto HPS = HPC ? (AFortPlayerStateAthena*)HPC->PlayerState : nullptr;
                                                if (HPC && HPC->MyFortPawn && HPS && !(HPS->HasbIsABot() && HPS->bIsABot)) { HumanTeam = HPS->TeamIndex; break; }
                                            }
                                            Controllers.Free();
                                        }
                                        if (HumanTeam >= 0 && State->TeamIndex == HumanTeam) { State->TeamIndex = 10; State->SquadId = 10; }
                                        State->ForceNetUpdate();

                                        // The native phase transition iterates the game state's
                                        // player array and the AI director's controller list. A
                                        // manager-spawned bot missing from either one crashes the
                                        // server when the match starts.
                                        if (GameState)
                                        {
                                            if (auto Off = GameState->GetOffset("PlayerArray"); Off != uint32(-1))
                                            {
                                                auto& Players = GetFromOffset<TArray<AActor*>>(GameState, Off);
                                                bool Found = false;
                                                for (int I = 0; I < Players.Num(); ++I) if (Players[I] == (AActor*)State) { Found = true; break; }
                                                if (!Found) Players.Add((AActor*)State);
                                            }
                                        }
                                    }
                                    printf("[Syndicate][AITest] native SpawnBot pawn=%s controller=%s state=%s team=%d\n",
                                        NewPawn->Name.ToString().c_str(), PC ? PC->Class->Name.ToString().c_str() : "none",
                                        State ? State->Class->Name.ToString().c_str() : "none", State ? (int)State->TeamIndex : -1);
                                    SyndicateNativeHenchmanPawn = NewPawn;
                                    SyndicateNativeHenchmanBotData = BotData;
                                    SyndicateNativeHenchmanLastLoc = NewPawn->K2_GetActorLocation();
                                    SyndicateNativeHenchmanNextLog = GetTickCount64() + 5000;
                                    SyndicateNativeHenchmanSamples = 0;
                                    SyndicateNativeHenchmanPatrolDone = false;

                                    // The native manager leaves this boss-data bot without a weapon
                                    // and with Deadpool's nameplate. Fix both before the first tick.
                                    SyndicateSetHenchmanName(State, PC);
                                    SyndicateGiveHenchmanWeapon(PC, NewPawn, BotData);
                                    SyndicateEnsureHenchmanBlackboard(PC, BotData, NewPawn->K2_GetActorLocation());

                                    SyndicateHenchmanRecord PrimaryRec;
                                    PrimaryRec.Pawn = NewPawn;
                                    PrimaryRec.BotData = BotData;
                                    PrimaryRec.PatrolPath = SyndicateNativeHenchmanPatrolPath;
                                    PrimaryRec.WeaponDef = SyndicateLastHenchmanWeapon;
                                    PrimaryRec.LastLoc = NewPawn->K2_GetActorLocation();
                                    PrimaryRec.NextLog = GetTickCount64() + 5000;
                                    SyndicateHenchmen.push_back(PrimaryRec);

                                    // Populate the rest of the Agency MANG group at nearby offsets,
                                    // matching the boss + henchman composition OGS gets from the
                                    // native POI spawners.
                                    if (auto ExtraClass = FindClass("FortAthenaAIBotCustomizationData"))
                                    {
                                        const wchar_t* ExtraPaths[] = {
                                            L"/Game/Athena/AI/MANG/BotData/BotData_MANG_Assault_Ego.BotData_MANG_Assault_Ego",
                                            L"/Game/Athena/AI/MANG/BotData/BotData_MANG_SMG_Ego.BotData_MANG_SMG_Ego",
                                            L"/Game/Athena/AI/MANG/BotData/BotData_MANG_POI_Agency.BotData_MANG_POI_Agency"
                                        };
                                        int ExtraIndex = 1;
                                        for (auto ExtraPath : ExtraPaths)
                                        {
                                            auto ExtraData = (UObject*)StaticLoadObject(ExtraPath, ExtraClass);
                                            if (!ExtraData) continue;
                                            FVector ExtraLoc = Location;
                                            ExtraLoc.X += 180.0 * ExtraIndex;
                                            ExtraLoc.Y += (ExtraIndex % 2) ? 90.0 : -90.0;
                                            SyndicateSpawnExtraHenchman(BotManager, ExtraData, ExtraLoc, Rotation,
                                                SyndicateNativeHenchmanPatrolPath, GameMode, (AActor*)GameState, HumanTeam);
                                            ++ExtraIndex;
                                        }
                                    }

                                    if (PC)
                                    {
                                        if (auto StartupOff = PC->GetOffset("StartupInventory"); StartupOff != uint32(-1))
                                            if (auto BotStartupOff = BotData->GetOffset("StartupInventory"); BotStartupOff != uint32(-1))
                                                GetFromOffset<UObject*>(PC, StartupOff) = GetFromOffset<UObject*>(BotData, BotStartupOff);
                                        auto PcObj = [&](const char* Name) -> std::string {
                                            auto Off = PC->GetOffset(Name);
                                            if (Off == uint32(-1)) return "none";
                                            auto Obj = GetFromOffset<UObject*>(PC, Off);
                                            return Obj ? std::string(Obj->Name.ToString().c_str()) : std::string("null");
                                        };
                                        printf("[Syndicate][AITest] pc brain=%s bt=%s bb=%s patrol=%s\n",
                                            PcObj("BrainComponent").c_str(), PcObj("BehaviorTree").c_str(),
                                            PcObj("Blackboard").c_str(), PcObj("CachedPatrollingComponent").c_str());
                                        bool HasBrain = false;
                                        if (auto Off = PC->GetOffset("BrainComponent"); Off != uint32(-1)) HasBrain = GetFromOffset<UObject*>(PC, Off) != nullptr;
                                        if (!HasBrain)
                                        {
                                            auto TreeClass = FindClass("BehaviorTree");
                                            auto Tree = TreeClass ? (UObject*)StaticLoadObject(L"/Game/Athena/AI/MANG/BehaviorTree/BT_MANG2.BT_MANG2", TreeClass) : nullptr;
                                            if (Tree) if (auto RT = PC->GetFunction("RunBehaviorTree")) PC->Call<bool>(RT, Tree);
                                        }
                                        if (auto Off = PC->GetOffset("Blackboard"); Off != uint32(-1))
                                        {
                                            auto BB = GetFromOffset<UObject*>(PC, Off);
                                            if (BB)
                                            {
                                                if (auto SetEnum = BB->GetFunction("SetValueAsEnum"))
                                                {
                                                    BB->Call<void>(SetEnum, FName(L"AIEvaluator_Global_GamePhaseStep"), (uint8)2);
                                                    BB->Call<void>(SetEnum, FName(L"AIEvaluator_Global_GamePhase"), (uint8)2);
                                                    BB->Call<void>(SetEnum, FName(L"AIEvaluator_AvoidThreat_ExecutionStatus"), (uint8)4);
                                                    BB->Call<void>(SetEnum, FName(L"AIEvaluator_Leash_ExecutionStatus"), (uint8)4);
                                                    BB->Call<void>(SetEnum, FName(L"AIEvaluator_Patrolling_ExecutionStatus"), (uint8)4);
                                                    BB->Call<void>(SetEnum, FName(L"AIEvaluator_DynamicBlueprint_ExecutionStatus"), (uint8)4);
                                                    BB->Call<void>(SetEnum, FName(L"AIEvaluator_CharacterLaunched_ExecutionStatus"), (uint8)4);
                                                }
                                                if (auto SetBool = BB->GetFunction("SetValueAsBool"))
                                                    BB->Call<void>(SetBool, FName(L"AIEvaluator_Global_IsMovementBlocked"), false);
                                            }
                                        }
                                        if (auto OnRep = PC->GetFunction("OnRep_Pawn")) PC->ProcessEvent(OnRep, nullptr);
                                        if (auto Started = PC->GetFunction("BlueprintOnBehaviorTreeStarted")) PC->ProcessEvent(Started, nullptr);
                                    }

                                    if (State)
                                    {
                                        auto Outfit = (UAthenaCharacterItemDefinition*)StaticLoadObject(
                                            L"/Game/Athena/Items/Cosmetics/Characters/CID_NPC_Athena_Commando_M_HenchmanBad.CID_NPC_Athena_Commando_M_HenchmanBad",
                                            UAthenaCharacterItemDefinition::StaticClass());
                                        auto Hero = Outfit ? Outfit->HeroDefinition : nullptr;
                                        if (Hero)
                                        {
                                            if (State->HasHeroType()) State->HeroType = Hero;
                                            for (auto& SoftSpec : Hero->Specializations)
                                            {
                                                auto Spec = SoftSpec.Get(); if (!Spec) continue;
                                                for (auto& SoftPart : Spec->CharacterParts)
                                                {
                                                    auto Part = SoftPart.Get();
                                                    if (Part && Part->CharacterPartType < 6) NewPawn->ServerChoosePart(Part->CharacterPartType, Part);
                                                }
                                            }
                                        }
                                    }
                                    if (auto MeshOff = NewPawn->GetOffset("Mesh"); MeshOff != uint32(-1))
                                    {
                                        auto Mesh = GetFromOffset<UObject*>(NewPawn, MeshOff);
                                        auto Anim = StaticLoadObject(L"/Game/Athena/AI/MANG/AnimSet/MANG_PatrolLayerAnimBP.MANG_PatrolLayerAnimBP_C", UClass::StaticClass());
                                        if (Mesh && Anim)
                                        {
                                            if (auto Off = Mesh->GetOffset("AnimBlueprintGeneratedClass"); Off != uint32(-1)) GetFromOffset<const UObject*>(Mesh, Off) = Anim;
                                            if (auto Fn2 = NewPawn->GetFunction("OnRep_AnimBPOverride")) NewPawn->ProcessEvent(Fn2, nullptr);
                                        }
                                    }

                                    if (PC && GameMode->GetOffset("AlivePlayers") != uint32(-1))
                                    {
                                        auto& Alive = GetFromOffset<TArray<AActor*>>(GameMode, GameMode->GetOffset("AlivePlayers"));
                                        bool Found = false;
                                        for (int I = 0; I < Alive.Num(); ++I) if (Alive[I] == PC) { Found = true; break; }
                                        if (!Found) Alive.Add(PC);
                                    }
                                    NewPawn->OnRep_PlayerState();
                                    NewPawn->ForceNetUpdate();
                                }
                                else printf("[Syndicate][AITest] native SpawnBot returned null\n");
                            }
                        }
                    }
                }
            }
        }

        // Chapter 2 (11.x) ships Fortnite's own bots; Syndicate uses its own (SpectraBots), so the native bot
        // manager is not created there and only Syndicate's bots play.
        if (UFortServerBotManagerAthena::StaticClass() && VersionInfo.FortniteVersion < 11)
        {
            if (auto BotManager = (UFortServerBotManagerAthena*)UGameplayStatics::SpawnObject(UFortServerBotManagerAthena::StaticClass(), GameMode))
            {
                GameMode->ServerBotManager = BotManager;
                // A missing property offset (uint32(-1)) wrote 4 GB past the object and crashed the server while
                // it started ("Erbium exited before the island was ready", Season 8). Only write fields that exist.
                const uint32 StateOff = BotManager->GetOffset("CachedGameState"), ModeOff = BotManager->GetOffset("CachedGameMode");
                if (StateOff != uint32(-1)) *(void**)(__int64(BotManager) + StateOff) = (void*)GameState;
                if (ModeOff != uint32(-1)) *(void**)(__int64(BotManager) + ModeOff) = (void*)GameMode;
                if (StateOff == uint32(-1) || ModeOff == uint32(-1)) printf("[Syndicate] bot manager: CachedGameState/CachedGameMode not found (%u/%u)\n", StateOff, ModeOff);
            }
            else
            {
                printf("BotManager is nullptr!\n");
            }
        }

        auto GoalManagerClass = GameMode->HasWarmupRequiredPlayerCount() ? FindClass("FortAIGoalManager") : FindObject<UClass>("/Game/AI/GoalSelection/AIGoalManager.AIGoalManager_C");

        GameMode->AIGoalManager = UWorld::SpawnActor(GoalManagerClass, FVector{}, GameMode);

        auto MissionManagerClass = GameMode->HasWarmupRequiredPlayerCount() ? nullptr : FindObject<UClass>("/Game/Blueprints/MissionManager.MissionManager_C");

        if (MissionManagerClass)
        {
            GameState->MissionManager = UWorld::SpawnActor(MissionManagerClass, FVector{}, GameState);
            GameState->OnRep_MissionManager();

            auto MissionInfo = FindObject<UFortMissionInfo>(L"/Game/Missions/Primary/EvacuateTheSurvivors/EvacuteTheSurvivors.EvacuteTheSurvivors");

            if (!MissionInfo)
                MissionInfo = FindObject<UFortMissionInfo>(L"/SaveTheWorld/Missions/Primary/EvacuateTheSurvivors/EvacuteTheSurvivors.EvacuteTheSurvivors");

            if (MissionInfo)
            {
                MissionInfo->bStartPlayingOnLoad = true; // bad hack, we should find a better way to do this later
                // startplayingmission

                UFortMissionLibrary::LoadMission(UWorld::GetWorld(), MissionInfo);
            }
            // we need to spawn bluglo manager too?
        }

        /* if (VersionInfo.EngineVersion == 4.16)
         {
             if (!UWorld::GetWorld()->NavigationSystem)
             {
                 UWorld::GetWorld()->NavigationSystem = UGameplayStatics::SpawnObject(FindClass("FortNavSystem"), UWorld::GetWorld());
                 auto OnWorldInitDone = (void(*)(UObject*, char))(ImageBase + 0x1f6fc40);
                 OnWorldInitDone(UWorld::GetWorld()->NavigationSystem, 1);
             }
         }*/

         //if (!GameMode->HasWarmupRequiredPlayerCount())
         //    UWorld::SpawnActor(FindClass("FortPlayerStart"), FVector{0, 0, 3000});

        *Ret = false;
        return;
    }

    if (!GameMode->bWorldIsReady)
    {
        static auto WarmupStartClass = FindClass("PlayerStart");
        TArray<AActor*> Starts;
        Utils::GetAll(WarmupStartClass, Starts);
        auto StartsNum = Starts.Num();
        Starts.Free();

        if (StartsNum == 0 || !Misc::bHookedAll)
        {
            *Ret = false;
            return;
        }

        TArray<AFortAthenaMapInfo*> AllMapInfos;
        Utils::GetAll<AFortAthenaMapInfo>(AllMapInfos);

        if (AllMapInfos.Num() > 0 && !GameState->MapInfo)
        {
            *Ret = false;
            return;
        }
        AllMapInfos.Free();



        if ((VersionInfo.FortniteVersion >= 3.5 && VersionInfo.FortniteVersion <= 4.0))
            SetupPlaylist(GameMode, GameState);
        else if (VersionInfo.EngineVersion >= 4.22 && VersionInfo.EngineVersion < 4.26)
            GameState->OnRep_CurrentPlaylistInfo();

        if (VersionInfo.FortniteVersion >= 25.20 && GameState->HasMapInfo() && GameState->MapInfo)
        {
            auto GamePhaseLogic = UFortGameStateComponent_BattleRoyaleGamePhaseLogic::Get(GameState);

            if (GamePhaseLogic)
            {
                auto InitializeFlightPath = (void(*)(AFortAthenaMapInfo*, AFortGameStateAthena*, UFortGameStateComponent_BattleRoyaleGamePhaseLogic*, bool, double, float, float)) FindInitializeFlightPath();
                if (InitializeFlightPath)
                    InitializeFlightPath(GameState->MapInfo, GameState, GamePhaseLogic, false, 0.f, 0.f, 360.f);
                UFortGameStateComponent_BattleRoyaleGamePhaseLogic::GenerateStormCircles(GameState->MapInfo);
            }
        }

        auto Playlist = VersionInfo.FortniteVersion >= 3.5 && GameMode->HasWarmupRequiredPlayerCount() ? (GameMode->GameState->HasCurrentPlaylistInfo() ? GameMode->GameState->CurrentPlaylistInfo.BasePlaylist : GameMode->GameState->CurrentPlaylistData) : nullptr;

        if (Playlist && Playlist->HasbSkipWarmup())
            UFortGameStateComponent_BattleRoyaleGamePhaseLogic::bSkipWarmup = Playlist->bSkipWarmup;
        if (Playlist && Playlist->HasbSkipAircraft())
            UFortGameStateComponent_BattleRoyaleGamePhaseLogic::bSkipAircraft = Playlist->bSkipAircraft;

        if (Playlist && Playlist->HasGameplayTagContainer())
        {

            for (int i = 0; i < Playlist->GameplayTagContainer.GameplayTags.Num(); i++)
            {
                auto& PlaylistTag = Playlist->GameplayTagContainer.GameplayTags.Get(i, FGameplayTag::Size());

                if (PlaylistTag.TagName.ToString() == "Athena.Playlist.SpecialEvent")
                {
                    for (auto& Event : Events::EventsArray)
                    {
                        if (Event.EventVersion != VersionInfo.FortniteVersion)
                            continue;

                        UObject* LoaderObject = nullptr;
                        if (Event.LoaderClass)
                            if (const UClass* LoaderClass = FindObject<UClass>(Event.LoaderClass))
                            {
                                TArray<AActor*> AllLoaders;
                                Utils::GetAll(LoaderClass, AllLoaders);
                                LoaderObject = AllLoaders.Num() > 0 ? AllLoaders[0] : nullptr;
                                AllLoaders.Free();
                            }

                        if (Event.LoaderFuncPath != nullptr && LoaderObject)
                            if (const UFunction* LoaderFunction = FindObject<UFunction>(Event.LoaderFuncPath))
                            {
                                int Param = 1;
                                LoaderObject->ProcessEvent(const_cast<UFunction*>(LoaderFunction), &Param);
                                printf("[Events] Loaded event level!\n");
                            }
                            else
                                printf("[Events] Failed to load event level!\n");

                        if (GameMode->HasSafeZoneLocations())
                            GameMode->SafeZoneLocations.Free();
                        else
                            UFortGameStateComponent_BattleRoyaleGamePhaseLogic::bEnableZones = false;
                        break;
                    }

                    break;
                }
            }
        }

        auto AbilitySet = VersionInfo.FortniteVersion > 8.30 ? FindObject<UFortAbilitySet>(L"/Game/Abilities/Player/Generic/Traits/DefaultPlayer/GAS_AthenaPlayer.GAS_AthenaPlayer") : FindObject<UFortAbilitySet>(L"/Game/Abilities/Player/Generic/Traits/DefaultPlayer/GAS_DefaultPlayer.GAS_DefaultPlayer");
        AbilitySet->AddToRoot();
        AbilitySets.Add(AbilitySet);

        if (VersionInfo.FortniteVersion >= 20)
        {
            auto TacticalSprintAbility = FindObject<UFortAbilitySet>(L"/TacticalSprintGame/Gameplay/AS_TacticalSprint.AS_TacticalSprint");

            if (!TacticalSprintAbility)
                TacticalSprintAbility = FindObject<UFortAbilitySet>(L"/TacticalSprint/Gameplay/AS_TacticalSprint.AS_TacticalSprint");
            TacticalSprintAbility->AddToRoot();
            AbilitySets.Add(TacticalSprintAbility);

            auto AscenderAbility = FindObject<UFortAbilitySet>(L"/Ascender/Gameplay/Ascender/AS_Ascender.AS_Ascender");
            AscenderAbility->AddToRoot();
            AbilitySets.Add(AscenderAbility);

            auto DoorBashAbility = FindObject<UFortAbilitySet>(L"/DoorBashContent/Gameplay/AS_DoorBash.AS_DoorBash");
            DoorBashAbility->AddToRoot();
            AbilitySets.Add(DoorBashAbility);

            auto HillScrambleAbility = FindObject<UFortAbilitySet>(L"/HillScramble/Gameplay/AS_HillScramble.AS_HillScramble");
            HillScrambleAbility->AddToRoot();
            AbilitySets.Add(HillScrambleAbility);

            auto SlideImpulseAbility = FindObject<UFortAbilitySet>(L"/SlideImpulse/Gameplay/AS_SlideImpulse.AS_SlideImpulse");
            SlideImpulseAbility->AddToRoot();
            AbilitySets.Add(SlideImpulseAbility);

            if (std::floor(VersionInfo.FortniteVersion) == 21)
            {
                auto RealitySaplingAbility = FindObject<UFortAbilitySet>(L"/RealitySeedGameplay/Environment/Foliage/GAS_Athena_RealitySapling.GAS_Athena_RealitySapling");
                AbilitySets.Add(RealitySaplingAbility);
            }
        }

        for (auto& Set : AbilitySets)
            if (Set)
                Set->AddToRoot();

        if (Playlist && Playlist->HasModifierList())
            for (int i = 0; i < Playlist->ModifierList.Num(); i++)
            {
                auto Modifier = Playlist->ModifierList.Get(i, FSoftObjectPtr::Size()).Get();

                if (!Modifier)
                    continue;

                for (int j = 0; j < Modifier->PersistentAbilitySets.Num(); j++)
                {
                    auto& DeliveryInfo = Modifier->PersistentAbilitySets.Get(j, FFortAbilitySetDeliveryInfo::Size());

                    if (!DeliveryInfo.DeliveryRequirements.bApplyToPlayerPawns)
                        continue;

                    for (int k = 0; k < DeliveryInfo.AbilitySets.Num(); k++)
                    {
                        auto AbilitySet = DeliveryInfo.AbilitySets.Get(k, FSoftObjectPtr::Size()).Get();

                        AbilitySets.Add(AbilitySet);
                    }
                }
            }


        /*if (floor(VersionInfo.FortniteVersion) != 20)
        {
            UFortLootPackage::SpawnFloorLootForContainer(FindObject<UClass>(L"/Game/Athena/Environments/Blueprints/Tiered_Athena_FloorLoot_Warmup.Tiered_Athena_FloorLoot_Warmup_C"));
            UFortLootPackage::SpawnFloorLootForContainer(FindObject<UClass>(L"/Game/Athena/Environments/Blueprints/Tiered_Athena_FloorLoot_01.Tiered_Athena_FloorLoot_01_C"));
        }

        auto ConsumableSpawners = Utils::GetAll<ABGAConsumableSpawner>();

        for (auto& Spawner : ConsumableSpawners)
            UFortLootPackage::SpawnConsumableActor(Spawner);*/

        if (VersionInfo.EngineVersion >= 4.27)
        {
            if (GameState->HasDefaultParachuteDeployTraceForGroundDistance())
                GameState->DefaultParachuteDeployTraceForGroundDistance = 10000;
        }

        // Season 7: glider redeploy was part of the normal modes (open the glider again when falling from a height).
        // The real servers filled these replicated values from the playlist; Erbium left them at 0, so 7.40 had no
        // redeploy after landing.
        if (VersionInfo.FortniteVersion >= 7.0 && VersionInfo.FortniteVersion < 8.0 && GameState->HasDefaultGliderRedeployCanRedeploy())
        {
            GameState->DefaultGliderRedeployCanRedeploy = 1.f;
            if (GameState->HasDefaultRedeployGliderLateralVelocityMult() && GameState->DefaultRedeployGliderLateralVelocityMult <= 0.f)
                GameState->DefaultRedeployGliderLateralVelocityMult = 1.f;
            printf("[Syndicate] glider redeploy on (Season 7)\n");
        }

        if (VersionInfo.FortniteVersion >= 27)
        {
            // fix grind rails
            auto GameData = FindObject<UCurveTable>("/GrindRail/DataTables/GrindRailGameData.GrindRailGameData");

            if (GameData)
            {
                static FName UseGrindingMME = FName(L"Default.GrindRails.UseGrindingMME");

                for (const auto& [RowName, RowPtr] : GameData->RowMap)
                {
                    if (RowName != UseGrindingMME)
                        continue;

                    FSimpleCurve* Row = (FSimpleCurve*)RowPtr;

                    if (!Row)
                        continue;

                    for (int i = 0; i < Row->Keys.Num(); i++)
                    {
                        auto& Key = Row->Keys.Get(i, FSimpleCurveKey::Size());

                        Key.Value = 0.f;
                    }
                }
            }
        }

        const UObject* BattleBusDef = nullptr;
        const UClass* SupplyDropClass = nullptr;
        if (VersionInfo.FortniteVersion == 18.40)
            BattleBusDef = FindObject<UObject>(L"/Game/Athena/Items/Cosmetics/BattleBuses/BBID_HeadbandBus.BBID_HeadbandBus");
        else if (VersionInfo.FortniteVersion == 1.11 || VersionInfo.FortniteVersion == 7.30 || VersionInfo.FortniteVersion == 11.31 || VersionInfo.FortniteVersion == 15.10 || VersionInfo.FortniteVersion == 19.01 || VersionInfo.FortniteVersion == 28.01)
        {
            BattleBusDef = FindObject<UObject>(L"/Game/Athena/Items/Cosmetics/BattleBuses/BBID_WinterBus.BBID_WinterBus");

            if (VersionInfo.FortniteVersion == 1.11)
                SupplyDropClass = FindObject<UClass>(L"/Game/Athena/SupplyDrops/B_AthenaSupplyDrop_Gift.B_AthenaSupplyDrop_Gift_C");
            else
                SupplyDropClass = FindObject<UClass>(L"/Game/Athena/SupplyDrops/AthenaSupplyDrop_Holiday.AthenaSupplyDrop_Holiday_C");
        }
        else if (VersionInfo.FortniteVersion == 23.10)
        {
            BattleBusDef = FindObject<UObject>(L"/Game/Athena/Items/Cosmetics/BattleBuses/BBID_BattleBus_Booster_Winter.BBID_BattleBus_Booster_Winter");
            SupplyDropClass = FindObject<UClass>(L"/Game/Athena/SupplyDrops/AthenaSupplyDrop_Holiday.AthenaSupplyDrop_Holiday_C");
        }
        else if (VersionInfo.FortniteVersion == 5.10 || VersionInfo.FortniteVersion == 9.41 || VersionInfo.FortniteVersion == 14.20 || VersionInfo.FortniteVersion == 18.00 || VersionInfo.FortniteVersion == 22.00 || VersionInfo.FortniteVersion == 26.20)
        {
            if (VersionInfo.FortniteVersion == 5.10)
                BattleBusDef = FindObject<UObject>(L"/Game/Athena/Items/Cosmetics/BattleBuses/BBID_BirthdayBus.BBID_BirthdayBus");
            else if (VersionInfo.FortniteVersion == 9.41)
                BattleBusDef = FindObject<UObject>(L"/Game/Athena/Items/Cosmetics/BattleBuses/BBID_BirthdayBus2nd.BBID_BirthdayBus2nd");
            else if (VersionInfo.FortniteVersion == 14.20)
                BattleBusDef = FindObject<UObject>(L"/Game/Athena/Items/Cosmetics/BattleBuses/BBID_BirthdayBus3rd.BBID_BirthdayBus3rd");
            else if (VersionInfo.FortniteVersion == 18.00)
                BattleBusDef = FindObject<UObject>(L"/Game/Athena/Items/Cosmetics/BattleBuses/BBID_BirthdayBus4th.BBID_BirthdayBus4th");
            else if (VersionInfo.FortniteVersion == 22.00)
                BattleBusDef = FindObject<UObject>(L"/Game/Athena/Items/Cosmetics/BattleBuses/BBID_BirthdayBus5th.BBID_BirthdayBus5th");
            else if (VersionInfo.FortniteVersion == 26.20)
                BattleBusDef = FindObject<UObject>(L"/Game/Athena/Items/Cosmetics/BattleBuses/BBID_BirthdayBus6th.BBID_BirthdayBus6th");

            SupplyDropClass = FindObject<UClass>(L"/Game/Athena/SupplyDrops/AthenaSupplyDrop_BDay.AthenaSupplyDrop_BDay_C");
        }
        else if (VersionInfo.FortniteVersion == 6.20 || VersionInfo.FortniteVersion == 6.21 || VersionInfo.FortniteVersion == 11.10 || VersionInfo.FortniteVersion == 14.40 || VersionInfo.FortniteVersion == 18.21)
            BattleBusDef = FindObject<UObject>(L"/Game/Athena/Items/Cosmetics/BattleBuses/BBID_HalloweenBus.BBID_HalloweenBus");
        else if (VersionInfo.FortniteVersion == 26.30)
            BattleBusDef = FindObject<UObject>(L"/Game/Athena/Items/Cosmetics/BattleBuses/BBID_HalloweenBus_Booster.BBID_HalloweenBus_Booster");
        else if (VersionInfo.FortniteVersion == 14.30)
            BattleBusDef = FindObject<UObject>(L"/Game/Athena/Items/Cosmetics/BattleBuses/BBID_BusUpgrade1.BBID_BusUpgrade1");
        else if (VersionInfo.FortniteVersion == 14.50)
            BattleBusDef = FindObject<UObject>(L"/Game/Athena/Items/Cosmetics/BattleBuses/BBID_BusUpgrade2.BBID_BusUpgrade2");
        else if (VersionInfo.FortniteVersion == 14.60)
            BattleBusDef = FindObject<UObject>(L"/Game/Athena/Items/Cosmetics/BattleBuses/BBID_BusUpgrade3.BBID_BusUpgrade3");
        else if (VersionInfo.FortniteVersion >= 12.30 && VersionInfo.FortniteVersion <= 12.61)
        {
            BattleBusDef = FindObject<UObject>(L"/Game/Athena/Items/Cosmetics/BattleBuses/BBID_DonutBus.BBID_DonutBus");
            SupplyDropClass = FindObject<UClass>(L"/Game/Athena/SupplyDrops/AthenaSupplyDrop_Donut.AthenaSupplyDrop_Donut_C");
        }
        else if (VersionInfo.FortniteVersion == 9.30)
            BattleBusDef = FindObject<UObject>(L"/Game/Athena/Items/Cosmetics/BattleBuses/BBID_WorldCupBus.BBID_WorldCupBus");
        else if (VersionInfo.FortniteVersion == 21.00)
            BattleBusDef = FindObject<UObject>(L"/Game/Athena/Items/Cosmetics/BattleBuses/BBID_CelebrationBus.BBID_CelebrationBus");
        else if (std::floor(VersionInfo.FortniteVersion) == 27)
            BattleBusDef = FindObject<UObject>(L"/Game/Athena/Items/Cosmetics/BattleBuses/BBID_DefaultBus.BBID_DefaultBus");

        if (BattleBusDef)
        {
            if (GameState->HasDefaultBattleBus())
                GameState->DefaultBattleBus = BattleBusDef;

            TArray<AFortAthenaAircraft*> Aircrafts;
            Utils::GetAll<AFortAthenaAircraft>(Aircrafts);
            for (auto& Aircraft : Aircrafts)
            {
                Aircraft->DefaultBusSkin = BattleBusDef;

                if (Aircraft->SpawnedCosmeticActor)
                {
                    static auto Offset = Aircraft->SpawnedCosmeticActor->GetOffset("ActiveSkin");

                    GetFromOffset<const UObject*>(Aircraft->SpawnedCosmeticActor, Offset) = BattleBusDef;
                }
            }
            Aircrafts.Free();
        }

        if (GameState->HasMapInfo() && GameState->MapInfo)
        {
            if (SupplyDropClass)
            {
                if (GameState->MapInfo->HasSupplyDropInfoList())
                    for (auto& Info : GameState->MapInfo->SupplyDropInfoList)
                        Info->SupplyDropClass = SupplyDropClass;
                else
                    GameState->MapInfo->SupplyDropClass = SupplyDropClass;
            }

            if (VersionInfo.FortniteVersion >= 3.4)
            {
                GameData = Playlist ? Playlist->GameData : nullptr;
                if (!GameData)
                    GameData = FindObject<UCurveTable>(L"/Game/Athena/Balance/DataTables/AthenaGameData.AthenaGameData");

                for (int i = 0; i < 6; i++)
                {
                    float Weight;
                    UDataTableFunctionLibrary::EvaluateCurveTableRow(GameState->MapInfo->VendingMachineRarityCount.Curve.CurveTable, GameState->MapInfo->VendingMachineRarityCount.Curve.RowName, (float)i, nullptr, &Weight, FString());

                    WeightMap[i] = Weight;
                    Sum += Weight;
                }

                UDataTableFunctionLibrary::EvaluateCurveTableRow(GameState->MapInfo->VendingMachineRarityCount.Curve.CurveTable, GameState->MapInfo->VendingMachineRarityCount.Curve.RowName, 0.f, nullptr, &Weight, FString());

                TotalWeight = std::accumulate(WeightMap.begin(), WeightMap.end(), 0.0f, [&](float acc, const std::pair<int, float>& p)
                    { return acc + p.second; });
            }

            if (VersionInfo.FortniteVersion >= 3.3 && VersionInfo.FortniteVersion < 17 && GameState->MapInfo->LlamaClass)
            {
                auto PickSupplyDropLocation = (FVector * (*)(AFortAthenaMapInfo*, FVector*, FVector*, float, bool, float, float)) FindPickSupplyDropLocation();

                if (PickSupplyDropLocation)
                {
                    FFortSafeZoneDefinition& SafeZoneDefinition = GameState->MapInfo->SafeZoneDefinition;

                    auto LlamaMin = GameState->MapInfo->LlamaQuantityMin.Evaluate();
                    auto LlamaMax = GameState->MapInfo->LlamaQuantityMax.Evaluate();
                    auto LlamaCount = UKismetMathLibrary::RandomIntegerInRange((int)LlamaMin, (int)LlamaMax);
                    auto Radius = GameState->MapInfo->HasSafeZoneDefinition() ? SafeZoneDefinition.Radius.Evaluate(0) : 0;

                    if (Radius == 0)
                        Radius = 120000;
                    auto Center = GameState->MapInfo->GetMapCenter();
                    Center.Z = 10000;

                    for (int i = 0; i < LlamaCount; i++)
                    {
                        FVector Loc(0, 0, 0);
                        PickSupplyDropLocation(GameState->MapInfo, &Loc, &Center, Radius, 0, -1, -1);

                        if (Loc.X != 0 || Loc.Y != 0 || Loc.Z != 0)
                        {
                            FRotator Rot{};
                            Rot.Yaw = (float)rand() * 0.010986663f;

                            auto NewLlama = UWorld::SpawnActorUnfinished(GameState->MapInfo->LlamaClass, Loc, Rot);

                            static auto FindGroundLocationAt = NewLlama->GetFunction("FindGroundLocationAt");
                            auto GroundLoc = NewLlama->Call<FVector>(FindGroundLocationAt, Loc);

                            UWorld::FinishSpawnActor(NewLlama, GroundLoc, Rot);
                        }
                    }
                }
            }
        }

        GameMode->DefaultPawnClass = FindObject<UClass>(L"/Game/Athena/PlayerPawn_Athena.PlayerPawn_Athena_C");

        if (VersionInfo.EngineVersion == 4.16 && VersionInfo.FortniteVersion < 1.9)
        {
            auto sRef = Memcury::Scanner::FindStringRef(L"CollectGarbageInternal() is flushing async loading").Get();
            uint64_t CollectGarbage = 0;

            if (sRef)
            {
                for (int i = 0; i < 1000; i++)
                {
                    auto Ptr = (uint8_t*)(sRef - i);

                    if (*Ptr == 0x48 && *(Ptr + 1) == 0x89 && *(Ptr + 2) == 0x5C)
                    {
                        CollectGarbage = uint64_t(Ptr);
                        break;
                    }
                    else if (*Ptr == 0x40 && *(Ptr + 1) == 0x55)
                    {
                        CollectGarbage = uint64_t(Ptr);
                        break;
                    }
                    else if (*Ptr == 0x48 && *(Ptr + 1) == 0x8B && *(Ptr + 2) == 0xC4)
                    {
                        CollectGarbage = uint64_t(Ptr);
                        break;
                    }
                }

                Utils::Patch<uint8_t>(CollectGarbage, 0xC3);
            }
        }
        else if (VersionInfo.EngineVersion <= 4.20)
        {
            auto pattern = VersionInfo.FortniteVersion > 3.2 ? Memcury::Scanner::FindPattern("E8 ? ? ? ? EB 26 40 38 3D ? ? ? ?") : Memcury::Scanner::FindPattern("E8 ? ? ? ? F0 FF 0D ? ? ? ? 0F B6 C3");

            if (pattern.IsValid())
                Utils::Patch<uint8_t>(pattern.RelativeOffset(1).Get(), 0xC3);
        }

        if (GameState->HasAllPlayerBuildableClassesIndexLookup())
            for (auto& [Class, Handle] : GameState->AllPlayerBuildableClassesIndexLookup)
                AFortGameStateAthena::BuildingClassMap[Handle] = Class;

        if constexpr (FConfiguration::WebhookURL && *FConfiguration::WebhookURL)
        {
            auto curl = curl_easy_init();

            curl_easy_setopt(curl, CURLOPT_URL, FConfiguration::WebhookURL);
            curl_slist* headers = curl_slist_append(NULL, "Content-Type: application/json");
            curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

            char version[6];

            sprintf_s(version, VersionInfo.FortniteVersion >= 5.00 || VersionInfo.FortniteVersion < 1.2 ? "%.2f" : "%.1f", VersionInfo.FortniteVersion);

            auto payload = UEAllocatedString("{\"embeds\": [{\"title\": \"Server is joinable!\", \"fields\": [{\"name\":\"Version\",\"value\":\"") + version + "\"}, {\"name\":\"Playlist\",\"value\":\"" + (Playlist ? Playlist->PlaylistName.ToString() : "Playlist_DefaultSolo") + "\"}], \"color\": " + "\"7237230\", \"footer\": {\"text\":\"Erbium\", \"icon_url\":\"https://cdn.discordapp.com/attachments/1341168629378584698/1436803905119064105/L0WnFa.png.png?ex=6910ef69&is=690f9de9&hm=01a0888b46647959b38ee58df322048ab49e2a5a678e52d4502d9c5e3978d805&\"}, \"timestamp\":\"" + iso8601() + "\"}] }";

            curl_easy_setopt(curl, CURLOPT_POSTFIELDS, payload.c_str());

            curl_easy_perform(curl);

            curl_easy_cleanup(curl);
        }

        // for some reason it doesnt like when u do it earlier
        if (!Playlist && VersionInfo.FortniteVersion <= 4)
            if (GameMode->GameSession->HasMaxPlayers())
                GameMode->GameSession->MaxPlayers = 100;

        GUI::gsStatus = Joinable;
        sprintf_s(GUI::windowTitle, VersionInfo.EngineVersion >= 5.0 ? "Erbium (FN %.2f, UE %.1f): Joinable" : (VersionInfo.FortniteVersion >= 5.00 || VersionInfo.FortniteVersion < 1.2 ? "Erbium (FN %.2f, UE %.2f): Joinable" : "Erbium (FN %.1f, UE %.2f): Joinable"), VersionInfo.FortniteVersion, VersionInfo.EngineVersion);
        SetConsoleTitleA(GUI::windowTitle);
        GameMode->bWorldIsReady = true;
        if (SyndicateListening)
        {
            wchar_t readyPath[32768]{};
            if (GetEnvironmentVariableW(L"TILTED_SERVER_READY_FILE", readyPath, 32768))
            {
                FILE* ready = nullptr;
                if (_wfopen_s(&ready, readyPath, L"w") == 0 && ready)
                {
                    fprintf(ready, "%lu", GetCurrentProcessId());
                    fclose(ready);
                }
            }
            SyndicateSeasonXRemoveTiltedTown();
            if (VersionInfo.FortniteVersion >= 10.30 && VersionInfo.FortniteVersion < 12) SyndicateFindBatmanItems(); // Season X, and Chapter 2 Season 1 if its files still have them
            // Small, once-per-host map index for validating custom loot landmarks.
            wchar_t LandmarkReady[32768]{};
            if(GetEnvironmentVariableW(L"TILTED_SERVER_READY_FILE",LandmarkReady,32768)){
                auto FilePath=std::wstring(LandmarkReady)+L".landmarks.tsv";FILE* MapFile=nullptr;
                if(_wfopen_s(&MapFile,FilePath.c_str(),L"w")==0 && MapFile){
                    for(auto Type:{ABuildingFoundation::StaticClass(),ABuildingContainer::StaticClass()}){
                        TArray<AActor*> Actors;Utils::GetAll(Type,Actors);
                        for(auto Actor:Actors){if(!Actor)continue;auto P=Actor->K2_GetActorLocation();std::string Name;
                            for(UObject* O=Actor;O;O=O->Outer){Name=std::string(O->Name.ToString().c_str())+"/"+Name;}
                            fprintf(MapFile,"%s\t%.1f\t%.1f\t%.1f\n",Name.c_str(),P.X,P.Y,P.Z);
                        }Actors.Free();
                    }fclose(MapFile);
                }
            }
            SyndicateServerReady = true;
            printf("PROJECT_TILTED_SERVER_READY port=%d\n", FConfiguration::Port);
            fflush(stdout);
        }
    }

    if (VersionInfo.EngineVersion >= 4.24 && GameMode->IsA<AFortGameModeAthena>())
    {
        int ReadyPlayers = 0;
        TArray<AFortPlayerControllerAthena*> PlayerList;
        Utils::GetAll<AFortPlayerControllerAthena>(PlayerList);

        for (auto& PlayerController : PlayerList)
        {
            auto PlayerState = PlayerController->PlayerState;

            if (!PlayerState->bIsSpectator && PlayerController->bReadyToStartMatch)
                ReadyPlayers++;
        }

        PlayerList.Free();

        auto VolumeManager = GameState->HasVolumeManager() ? GameState->VolumeManager : nullptr;

        bool bAllLevelsFinishedStreaming = true;
        if (GameState->HasAdditionalPlaylistLevelsStreamed())
        {
            TArray<FPlaylistStreamedLevelData>& AdditionalPlaylistLevels = *(TArray<FPlaylistStreamedLevelData>*) (__int64(GameState) + GameState->GetOffset("AdditionalPlaylistLevelsStreamed") - 0x10);
            for (int i = 0; i < AdditionalPlaylistLevels.Num(); i++)
            {
                auto& AdditionalPlaylistLevel = AdditionalPlaylistLevels.Get(i, FPlaylistStreamedLevelData::Size());

                if (!AdditionalPlaylistLevel.bIsFinishedStreaming || !AdditionalPlaylistLevel.StreamingLevel || !AdditionalPlaylistLevel.StreamingLevel->LoadedLevel->bIsVisible)
                {
                    bAllLevelsFinishedStreaming = false;
                    break;
                }
            }
        }

        static auto WaitingToStart = FName(L"WaitingToStart");
        *Ret = GameMode->bWorldIsReady && (GameState->HasbPlaylistDataIsLoaded() ? GameState->bPlaylistDataIsLoaded : true) && GameMode->MatchState == WaitingToStart && bAllLevelsFinishedStreaming && (!VolumeManager || !(VolumeManager->HasbInSpawningStartup() ? VolumeManager->bInSpawningStartup : GameState->bInSpawningStartup)) && ReadyPlayers >= (GameMode->HasWarmupRequiredPlayerCount() ? GameMode->WarmupRequiredPlayerCount : 1);
    }
    else
        *Ret = callOGWithRet(GameMode, Stack.GetCurrentNativeFunction(), ReadyToStartMatch);

    if (VersionInfo.FortniteVersion >= 11.00 && VersionInfo.FortniteVersion < 25.20 && !*Ret)
    {
        auto Time = (float)UGameplayStatics::GetTimeSeconds(UWorld::GetWorld());
        auto WarmupDuration = 60.f;

        if (GameState->HasWarmupCountdownEndTime()) // gamephaselogic builds
        {
            GameState->WarmupCountdownStartTime = Time;
            GameState->WarmupCountdownEndTime = Time + WarmupDuration;
            GameMode->WarmupCountdownDuration = WarmupDuration;
            GameMode->WarmupEarlyCountdownDuration = WarmupDuration;
        }
    }
    return;
}

auto SpawnDefaultPawnForIdx = 0;
uint64_t ApplyCharacterCustomization;

void AFortGameMode::SpawnDefaultPawnFor(UObject* Context, FFrame& Stack, AActor** Ret)
{
    AFortPlayerControllerAthena* NewPlayer;
    AActor* StartSpot;
    Stack.StepCompiledIn(&NewPlayer);
    Stack.StepCompiledIn(&StartSpot);
    Stack.IncrementCode();
    auto GameMode = (AFortGameMode*)Context;

    if (!NewPlayer || !StartSpot)
        return;

    auto GameState = GameMode->GameState;
    AFortPlayerPawnAthena* Pawn = nullptr;

    Pawn = (AFortPlayerPawnAthena*)UWorld::SpawnActor(GameMode->GetDefaultPawnClassForController(NewPlayer), StartSpot->GetTransform(), NewPlayer, 3);

    while (!Pawn)
    {
        auto PlayerStart = GameMode->ChoosePlayerStart();
        if (PlayerStart)
            Pawn = (AFortPlayerPawnAthena*)UWorld::SpawnActor(GameMode->GetDefaultPawnClassForController(NewPlayer), PlayerStart->GetTransform(), NewPlayer, 3);
    }
    // they only stripped it on athena for some reason
    /*static auto FortGMSpawnDefaultPawnFor = (AFortPlayerPawnAthena * (*)(AFortGameMode*, AFortPlayerControllerAthena*, AActor*)) DefaultObjImpl("FortGameMode")->Vft[SpawnDefaultPawnForIdx];
    Pawn = FortGMSpawnDefaultPawnFor(GameMode, NewPlayer, StartSpot);

    if (!Pawn)
    {
        auto Transform = StartSpot->GetTransform();
        Transform.Translation.Z += 200.f;
        Pawn = GameMode->SpawnDefaultPawnAtTransform(NewPlayer, Transform);
    }*/

    *Ret = Pawn;
	AFortPlayerPawnAthena::RefreshSeasonTwoAppearance(Pawn, NewPlayer->PlayerState);

    auto Num = NewPlayer->WorldInventory ? NewPlayer->WorldInventory->Inventory.ReplicatedEntries.Num() : 0;
    if (Num == 0)
    {
        // Bots jumping from the bus have no hero (StrongMyHero is null), which crashed 1.7.2/1.8.x.
        if (VersionInfo.FortniteVersion <= 1.91 && VersionInfo.FortniteVersion != 1.1 && VersionInfo.FortniteVersion != 1.11 && NewPlayer->HasStrongMyHero() && NewPlayer->StrongMyHero && NewPlayer->PlayerState)
        {
            static auto HeroCharPartsOffset = NewPlayer->StrongMyHero->GetOffset("CharacterParts");
            auto& HeroCharParts = GetFromOffset<TArray<UObject*>>(NewPlayer->StrongMyHero, HeroCharPartsOffset);
            static auto CharacterPartsOffset = NewPlayer->PlayerState->GetOffset("CharacterParts");
            auto& CharacterParts = GetFromOffset<const UObject * [0x6]>(NewPlayer->PlayerState, CharacterPartsOffset);

            if (HeroCharParts.Num() > 0)
            {
                for (auto& Part : HeroCharParts)
                {
                    static auto PartTypeOffset = Part->GetOffset("CharacterPartType");
                    CharacterParts[GetFromOffset<uint8>(Part, PartTypeOffset)] = Part;
                }
            }
            else
            {

                static auto Head = FindObject<UObject>(L"/Game/Characters/CharacterParts/Female/Medium/Heads/F_Med_Head1.F_Med_Head1");
                static auto Body = FindObject<UObject>(L"/Game/Characters/CharacterParts/Female/Medium/Bodies/F_Med_Soldier_01.F_Med_Soldier_01");
                static auto Backpack = FindObject<UObject>(L"/Game/Characters/CharacterParts/Backpacks/NoBackpack.NoBackpack");

                CharacterParts[0] = Head;
                CharacterParts[1] = Body;
                CharacterParts[3] = Backpack;
            }
        }

        if (NewPlayer->HasXPComponent() && NewPlayer->XPComponent && NewPlayer->PlayerState)
        {
            if (NewPlayer->XPComponent->HasbRegisteredWithQuestManager())
            {
                NewPlayer->XPComponent->bRegisteredWithQuestManager = true;
                NewPlayer->XPComponent->OnRep_bRegisteredWithQuestManager();
            }

            if (NewPlayer->PlayerState->HasSeasonLevelUIDisplay())
            {
                NewPlayer->PlayerState->SeasonLevelUIDisplay = NewPlayer->XPComponent->CurrentLevel;
                NewPlayer->PlayerState->OnRep_SeasonLevelUIDisplay();
            }
            //NewPlayer->XPComponent->OnProfileUpdated();
        }
    }
    else
    {
        //NewPlayer->WorldInventory->Inventory.ReplicatedEntries.ResetNum();
        //NewPlayer->WorldInventory->Inventory.ItemInstances.ResetNum();

        /*for (int i = 0; i < NewPlayer->WorldInventory->Inventory.ItemInstances.Num(); i++)
        {
            auto& Entry = NewPlayer->WorldInventory->Inventory.ItemInstances[i]->ItemEntry;

            if (AFortInventory::IsPrimaryQuickbar(Entry.ItemDefinition) || Entry.ItemDefinition->IsA(AmmoClass) || Entry.ItemDefinition->IsA(ResourceClass))
            {
                NewPlayer->WorldInventory->Inventory.ItemInstances.Remove(i);
                i--;
            }
        }

        NewPlayer->WorldInventory->Update(nullptr);*/
    }
}


// Late game: lay the bus's remaining drop window as a straight pass over Center (keeping its height, speed and
// heading). Used when the storm's real late-game circle becomes known after the bus set off (1.7.2).
static void SyndicateLateGameAimBus(AFortGameStateAthena* GameState, FVector Center)
{
    auto Aircraft = GameState->HasAircrafts() ? (GameState->Aircrafts.Num() > 0 ? GameState->Aircrafts[0] : nullptr) : (GameState->HasAircraft() ? GameState->Aircraft : nullptr);
    float Now = (float)UGameplayStatics::GetTimeSeconds(UWorld::GetWorld());
    if (!Aircraft || Aircraft->bActorIsBeingDestroyed || !Aircraft->HasDropEndTime() || Aircraft->DropEndTime - Now < 6.f)
    {
        printf("[Syndicate] late game: storm circle at %.0f, %.0f; bus not re-aimed (%s)\n", Center.X, Center.Y, Aircraft ? "drop window nearly over" : "no bus");
        return;
    }
    float Speed = Aircraft->HasFlightInfo() ? (float)Aircraft->FlightInfo.FlightSpeed : (Aircraft->HasFlightSpeed() ? (float)Aircraft->FlightSpeed : 6000.f);
    if (!std::isfinite(Speed) || Speed < 1000.f || Speed > 20000.f)
        Speed = 6000.f;
    const float Left = Aircraft->DropEndTime - Now;
    const float Yaw = (float)Aircraft->K2_GetActorRotation().Yaw * 3.14159265f / 180.f;
    FVector Start = Center;
    Start.Z = Aircraft->K2_GetActorLocation().Z;
    Start.X -= cosf(Yaw) * Speed * Left * 0.5f;
    Start.Y -= sinf(Yaw) * Speed * Left * 0.5f;
    // Season 2+ aircraft keep their route in FlightInfo (Season 3 kept flying its first route: only the 1.7.2
    // fields were written here).
    if (Aircraft->HasFlightInfo())
    {
        Aircraft->FlightInfo.FlightStartLocation = Start;
        float Remaining = Left;
        Aircraft->FlightInfo.TimeTillFlightEnd = Remaining;
        Aircraft->FlightInfo.TimeTillDropEnd = Remaining;
        Aircraft->FlightInfo.TimeTillDropStart = 0.f;
    }
    if (Aircraft->HasFlightStartLocation())
        Aircraft->FlightStartLocation = Start;
    if (Aircraft->HasFlightStartTime())
        Aircraft->FlightStartTime = Now;
    Aircraft->K2_SetActorLocation(Start, false, nullptr, true);
    printf("[Syndicate] late game: storm circle at %.0f, %.0f; bus re-aimed over it (%.0f s left)\n", Center.X, Center.Y, Left);
}

// 1.7.2 late game, called from the storm phase hook and from the bot tick (the hook's phase number did not reach
// the late-game phase on 1.7.2, so it never ran there): once the storm is at the late-game circle, that circle
// becomes the drop circle, the bus's remaining pass is re-aimed over it, and the match goes back to its bus phase.
// The storm phase was entered early only so the storm would form at bus start; left on, the game refused the
// player's jump and the drop-window failsafe then put the player under the map.
void SyndicateLateGameAdoptStormCenter()
{
    if (!FConfiguration::bLateGame || !FConfiguration::bLateGameAdoptStormCenter || FConfiguration::bLateGameCenterAdopted)
        return;
    auto World = UWorld::GetWorld();
    auto GameMode = World ? (AFortGameMode*)World->AuthorityGameMode : nullptr;
    auto GameState = GameMode ? (AFortGameStateAthena*)GameMode->GameState : nullptr;
    if (!GameState || !GameMode->SafeZoneIndicator || !GameMode->HasSafeZonePhase() || GameMode->SafeZonePhase < FConfiguration::LateGameZone || !GameMode->SafeZoneIndicator->HasNextCenter())
        return;
    FVector Center = GameMode->SafeZoneIndicator->NextCenter;
    FConfiguration::LateGameCenterX = (float)Center.X;
    FConfiguration::LateGameCenterY = (float)Center.Y;
    FConfiguration::bLateGameCenterSet = true;
    FConfiguration::bLateGameCenterAdopted = true;
    FConfiguration::LateGameCenterVersion++;
    SyndicateLateGameAimBus(GameState, Center);
    // Back to the bus phase once the storm is up, so players jump themselves (the game refuses the jump and its bus
    // camera breaks in the storm phase). The storm's in/out state and damage in late game are kept right by
    // SpectraBots::LateGameStormFix (the stuck storm damage that hurt everyone in the air before is removed there).
    if (GameState->HasGamePhase() && GameState->GamePhase == 4)
    {
        auto Aircraft = GameState->HasAircrafts() ? (GameState->Aircrafts.Num() > 0 ? GameState->Aircrafts[0] : nullptr) : (GameState->HasAircraft() ? GameState->Aircraft : nullptr);
        if (Aircraft && !Aircraft->bActorIsBeingDestroyed)
        {
            GameState->GamePhase = 3;
            GameState->OnRep_GamePhase(4);
            printf("[Syndicate] late game: storm is up, game phase back to 3 (bus) so players can jump\n");
        }
    }
}

void AFortGameMode::HandlePostSafeZonePhaseChanged(AFortGameMode* GameMode, int NewSafeZonePhase_Inp)
{
    if (!GameMode->SafeZoneIndicator)
        return;

    auto NewSafeZonePhase = NewSafeZonePhase_Inp >= 0 ? NewSafeZonePhase_Inp : ((GameMode->HasSafeZonePhase() ? GameMode->SafeZonePhase : GameMode->SafeZoneIndicator->CurrentPhase) + 1);
    auto GameState = (AFortGameStateAthena*)GameMode->GameState;

    float TimeSeconds = (float)UGameplayStatics::GetTimeSeconds(GameState);

    if (VersionInfo.FortniteVersion >= 21.50)
    {
        if (HandlePostSafeZonePhaseChangedOG)
            HandlePostSafeZonePhaseChangedOG(GameMode, NewSafeZonePhase_Inp);

        return;
    }


    constexpr static std::array<float, 8> LateGameDurations{
        0.f,
        120.f,
        90.f,
        60.f,
        50.f,
        35.f,
        30.f,
        40.f,
    };

    constexpr static std::array<float, 8> LateGameHoldDurations{
        0.f,
        90.f,
        75.f,
        60.f,
        45.f,
        30.f,
        0.f,
        0.f,
    };

    static auto DurationsOffset = 0;
    if (DurationsOffset == 0)
    {
        DurationsOffset = 0x258;

        if (VersionInfo.FortniteVersion >= 18)
            DurationsOffset = 0x248;
        else if (VersionInfo.FortniteVersion < 15.20)
            DurationsOffset = 0x1f8;
    }

    auto SafeZoneDefinition = &GameState->MapInfo->SafeZoneDefinition;
    TArray<float>& Durations = *(TArray<float>*)(SafeZoneDefinition + DurationsOffset);
    TArray<float>& HoldDurations = *(TArray<float>*)(SafeZoneDefinition + DurationsOffset - 0x10);

    if (VersionInfo.FortniteVersion >= 13.00)
    {
        static bool bSetDurations = false;
        if (!bSetDurations)
        {
            bSetDurations = true;

            auto GameData = GameMode->HasAthenaGameDataTable() ? GameMode->AthenaGameDataTable : GameState->AthenaGameDataTable;

            auto ShrinkTime = FName(L"Default.SafeZone.ShrinkTime");
            auto HoldTime = FName(L"Default.SafeZone.WaitTime");

            for (int i = 0; i < Durations.Num(); i++)
            {
                UDataTableFunctionLibrary::EvaluateCurveTableRow(GameData, ShrinkTime, (float)i, nullptr, &Durations[i], FString());
            }
            for (int i = 0; i < HoldDurations.Num(); i++)
            {
                UDataTableFunctionLibrary::EvaluateCurveTableRow(GameData, HoldTime, (float)i, nullptr, &HoldDurations[i], FString());
            }
        }

        if (!FConfiguration::bLateGame || GameMode->SafeZonePhase > FConfiguration::LateGameZone)
        {
            auto Duration = Durations[NewSafeZonePhase];
            auto HoldDuration = HoldDurations[NewSafeZonePhase];

            GameMode->SafeZoneIndicator->SafeZoneStartShrinkTime = TimeSeconds + HoldDuration;
            GameMode->SafeZoneIndicator->SafeZoneFinishShrinkTime = GameMode->SafeZoneIndicator->SafeZoneStartShrinkTime + Duration;
        }
    }

    HandlePostSafeZonePhaseChangedOG(GameMode, NewSafeZonePhase_Inp);

    /*if (FConfiguration::bLateGame && GameMode->SafeZonePhase > FConfiguration::LateGameZone)
    {
        auto newIdx = GameMode->SafeZonePhase - FConfiguration::LateGameZone + 1;
        auto Duration = newIdx >= LateGameDurations.size() ? 0.f : LateGameDurations[newIdx];
        auto HoldDuration = newIdx >= LateGameHoldDurations.size() ? 0.f : LateGameHoldDurations[newIdx];

        GameMode->SafeZoneIndicator->SafeZoneStartShrinkTime = TimeSeconds + HoldDuration;
        GameMode->SafeZoneIndicator->SafeZoneFinishShrinkTime = GameMode->SafeZoneIndicator->SafeZoneStartShrinkTime + Duration;
    }*/


    if (FConfiguration::bLateGame && GameMode->SafeZonePhase < FConfiguration::LateGameZone)
    {
        GameMode->SafeZoneIndicator->SafeZoneStartShrinkTime = TimeSeconds;
        // Pre-season / Season 1: 1 s per skipped circle, not 0.15 s. The game only updates the storm size it
        // damages with while a circle is shrinking; a 0.15 s shrink could pass between server ticks and left that
        // size stale, so the storm hurt everyone (bots and players in the air, inside the circle too).
        GameMode->SafeZoneIndicator->SafeZoneFinishShrinkTime = GameMode->SafeZoneIndicator->SafeZoneStartShrinkTime + (VersionInfo.FortniteVersion < 11.00 ? 1.0f : 0.15f);
        return;
    }
    else if (FConfiguration::bLateGame && GameMode->SafeZonePhase == FConfiguration::LateGameZone)
    {
        //auto Duration = Durations[FConfiguration::LateGameZone];
        //auto HoldDuration = HoldDurations[FConfiguration::LateGameZone];

        if (FConfiguration::bLateGame && FConfiguration::bLateGameLongZone)
            GameMode->SafeZoneIndicator->SafeZoneStartShrinkTime = 676767.f;
        if (VersionInfo.FortniteVersion >= 13)
            GameMode->SafeZoneIndicator->SafeZoneFinishShrinkTime = GameMode->SafeZoneIndicator->SafeZoneStartShrinkTime + Durations[FConfiguration::LateGameZone];
    }

    if (FConfiguration::bLateGame && (SafeZoneLoc.X != 0 || SafeZoneLoc.Y != 0 || SafeZoneLoc.Z != 0))
    {
        GameMode->SafeZoneIndicator->NextCenter = SafeZoneLoc;
        if (GameMode->SafeZoneIndicator->HasLastCenter())
            GameMode->SafeZoneIndicator->LastCenter = SafeZoneLoc;
    }

    // 1.7.2 late game: take the storm's own late-game circle as the drop circle and re-aim the bus pass over it.
    SyndicateLateGameAdoptStormCenter();

    if (NewSafeZonePhase > (FConfiguration::bLateGame ? FConfiguration::LateGameZone : 1))
    {
        for (auto& UncastedPlayer : GameMode->AlivePlayers)
        {
            auto PlayerController = (AFortPlayerControllerAthena*)UncastedPlayer;

            // Spectra bots carry no quest manager; they only fill the lobby.
            if (!PlayerController || PlayerController->bActorIsBeingDestroyed || !PlayerController->PlayerState) continue;
            auto State = (AFortPlayerStateAthena*)PlayerController->PlayerState;
            if (State->HasbIsABot() && State->bIsABot) continue;
            if (auto QuestManager = PlayerController->GetQuestManager(1))
                QuestManager->SendStatEvent(PlayerController, EFortQuestObjectiveStatEvent::GetStormPhase(), 1, false);
        }
    }
}


uint64_t NotifyGameMemberAdded_ = 0;
int16_t WorldPlayerId = 0;
void AFortGameMode::HandleStartingNewPlayer_(UObject* Context, FFrame& Stack)
{
    AFortPlayerControllerAthena* NewPlayer;
    Stack.StepCompiledIn(&NewPlayer);
    Stack.IncrementCode();
    auto GameMode = (AFortGameMode*)Context;
    auto GameState = (AFortGameStateAthena*)GameMode->GameState;
    AFortPlayerStateAthena* PlayerState = (AFortPlayerStateAthena*)NewPlayer->PlayerState;

    SyndicateKeepPreseasonDaylight("player joined");

    if (VersionInfo.FortniteVersion <= 2.5)
    {
        NewPlayer->QuickBars = UWorld::SpawnActor<AFortQuickBars>(FVector{});
        NewPlayer->QuickBars->SetOwner(NewPlayer);
        SyndicateQuickbarTrace::Install(NewPlayer->QuickBars, NewPlayer); // inventory Unequip / rearranging (SyndicateQuickbarTrace.h)
    }

    if (PlayerState->HasSquadId())
    {
        PlayerState->SquadId = PlayerState->TeamIndex - (VersionInfo.FortniteVersion<9.f||VersionInfo.EngineVersion<4.23?0:VersionInfo.EngineVersion<4.24?3:2); // engines before 4.23 (Seasons 1-8): squad id == team index (as Project Reboot does for engines before 4.23); 0 made the squad HUD empty
        PlayerState->OnRep_SquadId();
    }

    if (GameState->HasGameMemberInfoArray())
    {
        auto Member = (FGameMemberInfo*)malloc(FGameMemberInfo::Size());
        memset((PBYTE)Member, 0, FGameMemberInfo::Size());

        Member->MostRecentArrayReplicationKey = -1;
        Member->ReplicationID = -1;
        Member->ReplicationKey = -1;
        Member->TeamIndex = PlayerState->TeamIndex;
        Member->SquadId = PlayerState->SquadId;
        Member->MemberUniqueId = PlayerState->HasUniqueID() ? PlayerState->UniqueID : PlayerState->UniqueId;
        // The roster owns a second native shared reference (SDK assignment is a raw copy).
        if((VersionInfo.FortniteVersion==1.11 || VersionInfo.FortniteVersion==2.5 || VersionInfo.FortniteVersion==3.5 || VersionInfo.FortniteVersion==4.0 || VersionInfo.FortniteVersion==4.5 || VersionInfo.FortniteVersion==5.10 || VersionInfo.FortniteVersion==6.20) && FUniqueNetIdRepl::Size()==40)
            if(auto Ref=GetFromOffset<uint8*>(&Member->MemberUniqueId,16))InterlockedIncrement((volatile LONG*)(Ref+8));


        auto& NewMember = GameState->GameMemberInfoArray.Members.Add(*Member, FGameMemberInfo::Size());
        GameState->GameMemberInfoArray.MarkItemDirty(NewMember);

        auto NotifyGameMemberAdded = (void(*)(AFortGameStateAthena*, uint8_t, uint8_t, FUniqueNetIdRepl*)) NotifyGameMemberAdded_;
        if (NotifyGameMemberAdded)
            NotifyGameMemberAdded(GameState, Member->SquadId, Member->TeamIndex, &Member->MemberUniqueId);

        free(Member);
    }

    if (NewPlayer->HasbBuildFree())
        NewPlayer->bBuildFree = FConfiguration::bInfiniteMats;

    if (!NewPlayer->WorldInventory)
    {
        NewPlayer->WorldInventory = UWorld::SpawnActor<AFortInventory>(NewPlayer->WorldInventoryClass, FVector{}, FRotator{}, NewPlayer);
        NewPlayer->WorldInventory->InventoryType = 0;
    }

    if (wcsstr(FConfiguration::Playlist, L"/Game/Athena/Playlists/Creative/Playlist_PlaygroundV2.Playlist_PlaygroundV2"))
        AFortAthenaCreativePortal::Create(NewPlayer);

    // Each connected human needs a distinct world identity (bots use 1000+).
    PlayerState->WorldPlayerId = WorldPlayerId++;
    PlayerState->ForceNetUpdate();

    return callOG(GameMode, Stack.GetCurrentNativeFunction(), HandleStartingNewPlayer, NewPlayer);
}


// Each population fills its own squad; team IDs come from one shared sequence.
struct SyndicateTeamAllocator
{
    int NextTeam=3;
    int HumanTeam=0, BotTeam=0;
    int HumanCount=0, BotCount=0;
    int Pick(bool IsBot,int SquadSize)
    {
        if(SquadSize<1)SquadSize=1;
        int& Team=IsBot?BotTeam:HumanTeam;
        int& Count=IsBot?BotCount:HumanCount;
        if(!Team || Count>=SquadSize){Team=NextTeam++;Count=0;}
        ++Count;
        return Team;
    }
};

uint8_t AFortGameMode::PickTeam(AFortGameMode* GameMode, uint8_t PreferredTeam, AFortPlayerControllerAthena* Controller)
{
    if (!GameMode->HasWarmupRequiredPlayerCount())
        return 0;

    uint8_t ret = CurrentTeam;
    auto Playlist = VersionInfo.FortniteVersion >= 3.5 && GameMode->HasWarmupRequiredPlayerCount() ? (GameMode->GameState->HasCurrentPlaylistInfo() ? GameMode->GameState->CurrentPlaylistInfo.BasePlaylist : GameMode->GameState->CurrentPlaylistData) : nullptr;

    const bool SyndicateLegacyTeams=VersionInfo.FortniteVersion==1.11 || VersionInfo.FortniteVersion==2.5 || (VersionInfo.FortniteVersion==1.72 || VersionInfo.FortniteVersion==1.8 || VersionInfo.FortniteVersion==1.81 || VersionInfo.FortniteVersion==1.82);
    // Fill / No Fill (every season but Season 4, where SyncHumanBotFill moves bots onto human teams later,
    // and 50v50). Before, the choice was only read in Season 4: Season 3 and 5-X put the next bots on the
    // player's team even with No Fill (one shared team counter), and the legacy seasons never filled.
    // Now: a bot joins a real player's team only when every real player on it chose Fill and it has room;
    // otherwise bots never land on a team that has a real player, and a real player starts a fresh team
    // instead of joining a half-full bot team.
    static AFortGameMode* FillOwner=nullptr;static std::map<int,int> TeamMembers;static std::set<int> HumanTeams;
    if(FillOwner!=GameMode){FillOwner=GameMode;TeamMembers.clear();HumanTeams.clear();}
    const bool FillSeason=!bIsLargeTeamGame && !(VersionInfo.FortniteVersion>=4 && VersionInfo.FortniteVersion<5);
    const int FillSize=SyndicateLegacyTeams?SyndicateSelectedTeamSize():(Playlist?int(Playlist->MaxSquadSize):1);
    auto FillState=Controller?(AFortPlayerStateAthena*)Controller->PlayerState:nullptr;
    const bool FillIsBot=FillState && FillState->HasbIsABot() && FillState->bIsABot;
    if(FillSeason && FillSize>1 && FillIsBot){
        for(int Team:HumanTeams)if(TeamMembers[Team]<FillSize && SyndicateHumanTeamWantsFill(Team)){
            ++TeamMembers[Team];printf("Picked bot team %d (fill for a player)\n",Team);return uint8_t(Team);}
    }
    if((SyndicateLegacyTeams || (VersionInfo.FortniteVersion>=4 && VersionInfo.FortniteVersion<5)) && !bIsLargeTeamGame)
    {
        static AFortGameMode* Owner=nullptr;
        static SyndicateTeamAllocator Teams;
        if(Owner!=GameMode){Owner=GameMode;Teams=SyndicateTeamAllocator{};}
        auto State=Controller?(AFortPlayerStateAthena*)Controller->PlayerState:nullptr;
        const bool IsBot=State && State->HasbIsABot() && State->bIsABot;
        const int LegacySize=SyndicateLegacyTeams?SyndicateSelectedTeamSize():(Playlist?int(Playlist->MaxSquadSize):1);
        // Fill bots joined this human team outside the allocator: count them so a second player doesn't overfill it.
        if(!IsBot && Teams.HumanTeam && TeamMembers[Teams.HumanTeam]>=LegacySize)Teams.HumanCount=LegacySize;
        ret=uint8_t(Teams.Pick(IsBot,LegacySize));
        ++TeamMembers[ret];if(!IsBot)HumanTeams.insert(ret);
        printf("Picked %s team %d\n",IsBot?"bot":"human",ret);
        return ret;
    }

    printf("Picked team %d %d\n", ret, Playlist ? Playlist->MaxSquadSize : 1);
    if (bIsLargeTeamGame)
    {
        if (CurrentTeam == 4)
            CurrentTeam = 3;
        else
            CurrentTeam = 4;
    }
    else
    {
        if(FillSeason && FillSize>1){
            // Keep real players and bots apart unless Fill said otherwise (handled above for bots).
            auto Fresh=[&](){while(HumanTeams.count(CurrentTeam) || TeamMembers[CurrentTeam]>0)CurrentTeam++;PlayersOnCurTeam=0;};
            if(FillIsBot){if(HumanTeams.count(CurrentTeam))Fresh();}
            else {
                // A second real player (party member / Radmin friend) joins a real player's team that has room.
                for(int Team:HumanTeams)if(TeamMembers[Team]<FillSize){++TeamMembers[Team];printf("Picked human team %d (with another player)\n",Team);return uint8_t(Team);}
                if(TeamMembers[CurrentTeam]>0)Fresh(); // human teams with room were handled above: never overfill, never join a bot squad
            }
            ret=CurrentTeam;++TeamMembers[ret];if(!FillIsBot)HumanTeams.insert(ret);
            if(FillIsBot && ++PlayersOnCurTeam>=FillSize){CurrentTeam++;PlayersOnCurTeam=0;}
            else if(!FillIsBot && TeamMembers[ret]>=FillSize){CurrentTeam++;PlayersOnCurTeam=0;}
            printf("Picked %s team %d\n",FillIsBot?"bot":"human",ret);
            return ret;
        }
        if (++PlayersOnCurTeam >= (Playlist ? Playlist->MaxSquadSize : 1))
        {
            CurrentTeam++;
            PlayersOnCurTeam = 0;
        }
    }

    return ret;
}

extern bool CanStartSyndicateManagedAircraft();
bool AFortGameMode::StartAircraftPhase(AFortGameMode* GameMode, char a2)
{
    if(VersionInfo.FortniteVersion>=4 && VersionInfo.FortniteVersion<5
        && GetEnvironmentVariableW(L"TILTED_SERVER_READY_FILE",nullptr,0)
        && !CanStartSyndicateManagedAircraft())return false;
    auto Ret = StartAircraftPhaseOG(GameMode, a2);
    SyndicateKeepPreseasonDaylight("bus");

    auto GameState = (AFortGameStateAthena*)GameMode->GameState;


    auto Playlist = VersionInfo.FortniteVersion >= 3.5 && GameMode->HasWarmupRequiredPlayerCount() ? (GameMode->GameState->HasCurrentPlaylistInfo() ? GameMode->GameState->CurrentPlaylistInfo.BasePlaylist : GameMode->GameState->CurrentPlaylistData) : nullptr;
    if constexpr (FConfiguration::WebhookURL && *FConfiguration::WebhookURL)
    {
        auto curl = curl_easy_init();

        curl_easy_setopt(curl, CURLOPT_URL, FConfiguration::WebhookURL);
        curl_slist* headers = curl_slist_append(NULL, "Content-Type: application/json");
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

        char version[6];

        sprintf_s(version, VersionInfo.FortniteVersion >= 5.00 || VersionInfo.FortniteVersion < 1.2 ? "%.2f" : "%.1f", VersionInfo.FortniteVersion);

        auto payload = UEAllocatedString("{\"embeds\": [{\"title\": \"Match has started!\", \"fields\": [{\"name\":\"Version\",\"value\":\"") + version + "\"}, {\"name\":\"Playlist\",\"value\":\"" + (Playlist ? Playlist->PlaylistName.ToString() : "Playlist_DefaultSolo") + "\"},{\"name\":\"Players\",\"value\":\"" + std::to_string(GameMode->AlivePlayers.Num()).c_str() + "\"}], \"color\": " + "\"7237230\", \"footer\": {\"text\":\"Erbium\", \"icon_url\":\"https://cdn.discordapp.com/attachments/1341168629378584698/1436803905119064105/L0WnFa.png.png?ex=6910ef69&is=690f9de9&hm=01a0888b46647959b38ee58df322048ab49e2a5a678e52d4502d9c5e3978d805&\"}, \"timestamp\":\"" + iso8601() + "\"}] }";

        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, payload.c_str());

        curl_easy_perform(curl);

        curl_easy_cleanup(curl);
    }
    GUI::gsStatus = StartedMatch;
    sprintf_s(GUI::windowTitle, VersionInfo.EngineVersion >= 5.0 ? "Erbium (FN %.2f, UE %.1f): Match started" : (VersionInfo.FortniteVersion >= 5.00 || VersionInfo.FortniteVersion < 1.2 ? "Erbium (FN %.2f, UE %.2f): Match started" : "Erbium (FN %.1f, UE %.2f): Match started"), VersionInfo.FortniteVersion, VersionInfo.EngineVersion);
    SetConsoleTitleA(GUI::windowTitle);

    // credit to heliato
    if (FConfiguration::bJoinInProgress || (Playlist && (Playlist->HasbAllowJoinInProgress() ? Playlist->bAllowJoinInProgress : false)))
        *(bool*)(uint64_t(&GameMode->WarmupRequiredPlayerCount) - 4) = false;

    if (FConfiguration::bLateGame && VersionInfo.FortniteVersion < 25.20)
    {
        auto Aircraft = GameState->HasAircrafts() ? (GameState->Aircrafts.Num() > 0 ? GameState->Aircrafts[0] : nullptr) : GameState->Aircraft;

        if (!Aircraft)
            return Ret;

        FVector Loc;
        bool bScuffed = false;
        // HasSafeZoneLocations: a build without the property would read (and index) memory before the game mode.
        if (!GameMode->HasSafeZoneLocations() || GameMode->SafeZoneLocations.Num() < 4)
        {
            bScuffed = true;

            TArray<ABuildingFoundation*> Foundations;
            Utils::GetAll<ABuildingFoundation>(Foundations);
            if (Foundations.Num() == 0)
            {
                Foundations.Free();
                printf("[Syndicate] late game: no safe zone locations and no foundations, normal bus route\n");
                return Ret;
            }
            // Any foundation could be on the edge of the island (the last match's circle was in a far corner).
            // Keep to the inner half: within half the farthest foundation's distance from their average position.
            FVector Mid{};
            for (int i = 0; i < Foundations.Num(); i++)
            {
                auto P = Foundations[i]->K2_GetActorLocation();
                Mid.X += P.X / Foundations.Num();
                Mid.Y += P.Y / Foundations.Num();
            }
            double Far = 0;
            for (int i = 0; i < Foundations.Num(); i++)
            {
                auto P = Foundations[i]->K2_GetActorLocation();
                Far = (std::max)(Far, (double)sqrt((P.X - Mid.X) * (P.X - Mid.X) + (P.Y - Mid.Y) * (P.Y - Mid.Y)));
            }
            ABuildingFoundation* Foundation = nullptr;
            for (int Try = 0; Try < 200 && !Foundation; Try++)
            {
                auto Pick = Foundations[rand() % Foundations.Num()];
                auto P = Pick->K2_GetActorLocation();
                if (sqrt((P.X - Mid.X) * (P.X - Mid.X) + (P.Y - Mid.Y) * (P.Y - Mid.Y)) < Far * 0.5)
                    Foundation = Pick;
            }
            if (!Foundation)
                Foundation = Foundations[rand() % Foundations.Num()];

            Foundations.Free();

            // Only a first aim for the bus: the storm keeps its own circle (moving its centres on the server did
            // not move the storm the player sees or the damage, so the player sat in a storm that did not hurt).
            // The bus and the bots move over to the storm's real late-game circle once it is known.
            Loc = Foundation->K2_GetActorLocation();
            SafeZoneLoc = FVector{};
            FConfiguration::bLateGameAdoptStormCenter = true;
            FConfiguration::bLateGameCenterAdopted = false;

            //FConfiguration::bLateGame = false;
            //printf("LateGame is not supported on this version!\n");
            //return Ret;
        }
        else
        {
            int Index = FConfiguration::LateGameZone + (VersionInfo.FortniteVersion >= 24 ? 3 : 0) - 1;
            Index = (std::max)(0, (std::min)(Index, GameMode->SafeZoneLocations.Num() - 1));
            Loc = GameMode->SafeZoneLocations.Get(Index, FVector::Size());
        }

        Loc.Z = 17500.f;
        // Pre-season / Season 1 fly the bus at about 81,500 units. Keep that height: the bot passenger spawn there
        // relies on it (below 20,000 units a bot at the bus counted as landed, fought in the air and fell to its death).
        // Other seasons: keep the game's own bus height too when it is a sensible one.
        {
            const float BusZ = (float)Aircraft->K2_GetActorLocation().Z;
            if (std::isfinite(BusZ) && BusZ > (VersionInfo.FortniteVersion < 2.00 ? 20000.f : 10000.f))
                Loc.Z = BusZ;
        }

        if (GameState->HasDefaultParachuteDeployTraceForGroundDistance())
        {
            GameState->DefaultParachuteDeployTraceForGroundDistance = 2500.f;
        }

        FConfiguration::LateGameCenterX = (float)Loc.X;
        FConfiguration::LateGameCenterY = (float)Loc.Y;
        FConfiguration::bLateGameCenterSet = true;

        // The bus flies a short straight pass over the late-game circle instead of sitting still: it keeps the
        // game's own speed and heading, starts LateGameHalfPass units before the centre and ends the same distance
        // past it, and the drop window is that whole pass (15-30 s).
        float Speed = 0.f;
        if (Aircraft->HasFlightInfo())
            Speed = Aircraft->FlightInfo.FlightSpeed;
        else if (Aircraft->HasFlightSpeed())
            Speed = Aircraft->FlightSpeed;
        if (!std::isfinite(Speed) || Speed < 1000.f || Speed > 20000.f)
            Speed = 6000.f;
        float PassTime = (std::max)(20.f, (std::min)(40.f, 2.f * 35000.f / Speed));
        const float HalfPass = Speed * PassTime * 0.5f;
        const float Yaw = (float)Aircraft->K2_GetActorRotation().Yaw * 3.14159265f / 180.f;
        FVector Start = Loc;
        Start.X -= cosf(Yaw) * HalfPass;
        Start.Y -= sinf(Yaw) * HalfPass;

        if (Aircraft->HasFlightInfo())
        {
            Aircraft->FlightInfo.FlightSpeed = Speed;

            Aircraft->FlightInfo.FlightStartLocation = Start;

            Aircraft->FlightInfo.TimeTillFlightEnd = PassTime;
            Aircraft->FlightInfo.TimeTillDropEnd = PassTime;
            Aircraft->FlightInfo.TimeTillDropStart = 0.f;
        }
        else
        {
            // Pre-season / Season 1 aircraft: write only the fields this build has.
            if (Aircraft->HasFlightSpeed())
                Aircraft->FlightSpeed = Speed;

            if (Aircraft->HasFlightStartLocation())
                Aircraft->FlightStartLocation = Start;

            if (Aircraft->HasTimeTillFlightEnd())
            {
                Aircraft->TimeTillFlightEnd = PassTime;
                Aircraft->TimeTillDropEnd = PassTime;
                Aircraft->TimeTillDropStart = 0.f;
            }
        }
        Aircraft->K2_SetActorLocation(Start, false, nullptr, true);
        // Not const: the property setters take float& / float&&, and a const float binds to neither (C2665).
        float Now = (float)UGameplayStatics::GetTimeSeconds(UWorld::GetWorld());
        if (Aircraft->HasDropStartTime())
            Aircraft->DropStartTime = Now;
        if (Aircraft->HasDropEndTime())
            Aircraft->DropEndTime = Now + PassTime;
        if (Aircraft->HasFlightStartTime())
            Aircraft->FlightStartTime = Now;
        if (Aircraft->HasFlightEndTime())
            Aircraft->FlightEndTime = Now + PassTime;
        // The storm forms as the bus sets off, not two minutes after the drop (set again when the drop zone is left,
        // in case the game resets it there).
        if (GameState->HasSafeZonesStartTime())
            GameState->SafeZonesStartTime = Now;
        // 1.7.2 only forms the storm once the match is in its storm phase, which the game enters when the drop zone
        // ends (so the storm came in as the bus left). Enter it now: the storm forms while the bus is still flying.
        // The bus and the jump keep working (they follow the aircraft's own drop window); players still aboard when
        // the window closes are dropped by SpectraBots::LateGameBusFailsafe.
        // Only when the circle is not known yet (no zone list: 1.7.2). In the storm phase the player's game does not
        // show the bus, and the failsafe drops players over the circle, so with a zone list (Season 2) the match stays
        // in the bus phase: the player sees the bus and jumps when they want; the storm forms as the bus leaves.
        if (VersionInfo.FortniteVersion < 11.00 && GameState->HasGamePhase() && bScuffed)
        {
            const auto WasPhase = (int)GameState->GamePhase;
            GameState->GamePhase = 4;
            GameState->OnRep_GamePhase(3);
            printf("[Syndicate] late game: game phase %d -> 4 (storm) at bus start\n", WasPhase);
        }
        printf("[Syndicate] late game: bus pass over zone %d centre %.0f, %.0f, height %.0f (speed %.0f, %.0f s)%s\n", FConfiguration::LateGameZone, Loc.X, Loc.Y, Loc.Z, Speed, PassTime, bScuffed ? " (no zone list, random foundation)" : "");
        //GameState->bAircraftIsLocked = false;
        //GameState->SafeZonesStartTime = (float)UGameplayStatics::GetTimeSeconds(UWorld::GetWorld()) + 7.6f;
    }

    return Ret;
}


extern void SyndicateBotsBeforeDropZoneExit();
void AFortGameMode::OnAircraftExitedDropZone_(UObject* Context, FFrame& Stack)
{
    AFortAthenaAircraft* Aircraft;
    Stack.StepCompiledIn(&Aircraft);
    Stack.IncrementCode();

    auto GameMode = (AFortGameMode*)Context;
    auto GameState = (AFortGameStateAthena*)GameMode->GameState;

    if (FConfiguration::bLateGame)
    {
        static auto CompClass = FindClass("FortControllerComponent_Aircraft");

        if (CompClass)
        {
            for (auto& Player : GameMode->AlivePlayers)
            {
                if (((AFortPlayerControllerAthena*)Player)->IsInAircraft())
                {
                    ((AFortPlayerControllerAthena*)Player)->GetAircraftComponent()->ServerAttemptAircraftJump(FRotator{});
                }
            }
        }
        else
        {
            for (auto& Player : GameMode->AlivePlayers)
            {
                auto PC = (AFortPlayerControllerAthena*)Player;
                // Spectra bots jump with their own logic (and are removed if still aboard); forcing the
                // player-jump path on them gave them a second pawn.
                if (!PC || (PC->PlayerState && ((AFortPlayerStateAthena*)PC->PlayerState)->bIsABot))
                    continue;
                if (PC->IsInAircraft())
                {
                    PC->ServerAttemptAircraftJump(FRotator{});
                }
            }
        }
    }

    if (FConfiguration::bLateGame)
    {
        GameState->GamePhase = 4;
        if (GameState->HasGamePhaseStep())
            GameState->GamePhaseStep = 7;
        GameState->OnRep_GamePhase(3);
    }

    // Players still on the bus at the end of its route are dropped by the game itself, which can
    // skip ServerAttemptAircraftJump_; reset them to the match start values there too (100 health,
    // no shield). Only those still on the bus: anyone who jumped earlier keeps what they have now.
    std::vector<AFortPlayerControllerAthena*> StillOnBus;
    if (!FConfiguration::bLateGame)
        for (auto& Player : GameMode->AlivePlayers)
            if (Player && Player->IsA(AFortPlayerControllerAthena::StaticClass()) && ((AFortPlayerControllerAthena*)Player)->IsInAircraft())
                StillOnBus.push_back((AFortPlayerControllerAthena*)Player);

    SyndicateBotsBeforeDropZoneExit();
    callOG(GameMode, Stack.GetCurrentNativeFunction(), OnAircraftExitedDropZone, Aircraft);

    // Late game: the game sets the storm to form a while after the drop; form it now (the circle then jumps
    // straight to the late-game phase in HandlePostSafeZonePhaseChanged).
    if (FConfiguration::bLateGame && GameState->HasSafeZonesStartTime())
    {
        float Now = (float)UGameplayStatics::GetTimeSeconds(UWorld::GetWorld());
        const float Was = GameState->SafeZonesStartTime;
        if (Was > Now)
            GameState->SafeZonesStartTime = Now;
        printf("[Syndicate] late game: storm start %.1f -> %.1f (now %.1f)\n", Was, (float)GameState->SafeZonesStartTime, Now);
    }

    for (auto PlayerController : StillOnBus)
        if (PlayerController->MyFortPawn)
        {
            PlayerController->MyFortPawn->SetHealth(100.f);
            PlayerController->MyFortPawn->SetShield(0.f);
            printf("[Syndicate] bus drop-zone reset: shield now %.0f\n", PlayerController->MyFortPawn->GetShield());
        }
}

TArray<FFortSafeZonePhaseInfo> Phases;

AFortSafeZoneIndicator* SetupSafeZoneIndicator(AFortGameMode* GameMode)
{
    // thanks heliato
    auto GameState = (AFortGameStateAthena*)GameMode->GameState;

    if (!GameMode->SafeZoneIndicator)
    {
        AFortSafeZoneIndicator* SafeZoneIndicator = UWorld::SpawnActor<AFortSafeZoneIndicator>(GameMode->SafeZoneIndicatorClass, FVector{});

        if (SafeZoneIndicator)
        {
            FFortSafeZoneDefinition& SafeZoneDefinition = GameState->MapInfo->SafeZoneDefinition;
            float SafeZoneCount = SafeZoneDefinition.Count.Evaluate();

            auto& Array = SafeZoneIndicator->HasSafeZonePhases() ? SafeZoneIndicator->SafeZonePhases : Phases;


            if (Array.IsValid())
                Array.Free();

            const float Time = (float)UGameplayStatics::GetTimeSeconds(GameState);

            for (float i = 0; i < SafeZoneCount; i++)
            {
                auto PhaseInfo = (FFortSafeZonePhaseInfo*)malloc(FFortSafeZonePhaseInfo::Size());
                memset((PBYTE)PhaseInfo, 0, FFortSafeZonePhaseInfo::Size());

                PhaseInfo->Radius = SafeZoneDefinition.Radius.Evaluate(i);
                PhaseInfo->WaitTime = SafeZoneDefinition.WaitTime.Evaluate(i);
                PhaseInfo->ShrinkTime = SafeZoneDefinition.ShrinkTime.Evaluate(i);
                PhaseInfo->PlayerCap = (int)SafeZoneDefinition.PlayerCapSolo.Evaluate(i);

                UDataTableFunctionLibrary::EvaluateCurveTableRow(GameState->AthenaGameDataTable, FName(L"Default.SafeZone.Damage"), i, nullptr, &PhaseInfo->DamageInfo.Damage, FString());
                if (i == 0.f)
                    PhaseInfo->DamageInfo.Damage = 0.01f;
                PhaseInfo->DamageInfo.bPercentageBasedDamage = true;
                PhaseInfo->TimeBetweenStormCapDamage = GameMode->TimeBetweenStormCapDamage.Evaluate(i);
                PhaseInfo->StormCapDamagePerTick = GameMode->StormCapDamagePerTick.Evaluate(i);
                PhaseInfo->StormCampingIncrementTimeAfterDelay = GameMode->StormCampingIncrementTimeAfterDelay.Evaluate(i);
                PhaseInfo->StormCampingInitialDelayTime = GameMode->StormCampingInitialDelayTime.Evaluate(i);
                PhaseInfo->MegaStormGridCellThickness = (int)SafeZoneDefinition.MegaStormGridCellThickness.Evaluate(i);

                if (FFortSafeZonePhaseInfo::HasUsePOIStormCenter())
                    PhaseInfo->UsePOIStormCenter = false;

                if (GameMode->SafeZoneLocations.GetData() && GameMode->SafeZoneLocations.Num() > i)
                    PhaseInfo->Center = GameMode->SafeZoneLocations.Get((int)i, FVector::Size());

                Array.Add(*PhaseInfo, FFortSafeZonePhaseInfo::Size());
                free(PhaseInfo);

                SafeZoneIndicator->PhaseCount++;
            }

            SafeZoneIndicator->OnRep_PhaseCount();

            SafeZoneIndicator->SafeZoneStartShrinkTime = Time + Array[0].WaitTime;
            SafeZoneIndicator->SafeZoneFinishShrinkTime = SafeZoneIndicator->SafeZoneStartShrinkTime + Array[0].ShrinkTime;

            SafeZoneIndicator->CurrentPhase = 0;
            SafeZoneIndicator->OnRep_CurrentPhase();
        }

        GameMode->SafeZoneIndicator = SafeZoneIndicator;
        GameState->SafeZoneIndicator = SafeZoneIndicator;
        GameState->OnRep_SafeZoneIndicator();
    }

    return GameMode->SafeZoneIndicator;
}

void StartNewSafeZonePhase(AFortGameMode* GameMode, int NewSafeZonePhase, bool bInitial = false)
{
    auto GameState = (AFortGameStateAthena*)GameMode->GameState;
    float TimeSeconds = (float)UGameplayStatics::GetTimeSeconds(GameState);
    auto& Array = GameMode->SafeZoneIndicator->HasSafeZonePhases() ? GameMode->SafeZoneIndicator->SafeZonePhases : Phases;

    if (Array.IsValidIndex(NewSafeZonePhase))
    {
        if (Array.IsValidIndex(NewSafeZonePhase - 1))
        {
            auto& PreviousPhaseInfo = Array.Get(NewSafeZonePhase - 1, FFortSafeZonePhaseInfo::Size());

            GameMode->SafeZoneIndicator->PreviousCenter = PreviousPhaseInfo.Center;
            GameMode->SafeZoneIndicator->PreviousRadius = PreviousPhaseInfo.Radius;
        }

        auto& PhaseInfo = Array.Get(NewSafeZonePhase, FFortSafeZonePhaseInfo::Size());

        GameMode->SafeZoneIndicator->NextCenter = PhaseInfo.Center;
        GameMode->SafeZoneIndicator->NextRadius = PhaseInfo.Radius;
        GameMode->SafeZoneIndicator->NextMegaStormGridCellThickness = PhaseInfo.MegaStormGridCellThickness;

        if (Array.IsValidIndex(NewSafeZonePhase + 1))
        {
            auto& NextPhaseInfo = Array.Get(NewSafeZonePhase + 1, FFortSafeZonePhaseInfo::Size());

            GameMode->SafeZoneIndicator->FutureReplicator->NextNextCenter = NextPhaseInfo.Center;
            GameMode->SafeZoneIndicator->FutureReplicator->NextNextRadius = NextPhaseInfo.Radius;

            GameMode->SafeZoneIndicator->NextNextCenter = NextPhaseInfo.Center;
            GameMode->SafeZoneIndicator->NextNextRadius = NextPhaseInfo.Radius;
            GameMode->SafeZoneIndicator->NextNextMegaStormGridCellThickness = NextPhaseInfo.MegaStormGridCellThickness;
        }

        GameMode->SafeZoneIndicator->SafeZoneStartShrinkTime = FConfiguration::bLateGame && FConfiguration::bLateGameLongZone ? 676767.f : TimeSeconds + PhaseInfo.WaitTime;
        GameMode->SafeZoneIndicator->SafeZoneFinishShrinkTime = GameMode->SafeZoneIndicator->SafeZoneStartShrinkTime + PhaseInfo.ShrinkTime;

        GameMode->SafeZoneIndicator->CurrentDamageInfo = PhaseInfo.DamageInfo;
        GameMode->SafeZoneIndicator->OnRep_CurrentDamageInfo();

        GameMode->SafeZoneIndicator->CurrentPhase = NewSafeZonePhase;
        GameMode->SafeZoneIndicator->OnRep_CurrentPhase();

        GameMode->SafeZoneIndicator->OnSafeZonePhaseChanged.Process();

        auto& SafeZoneState = *(uint8_t*)(__int64(&GameMode->SafeZoneIndicator->FutureReplicator) - 0x4);
        SafeZoneState = 2;

        GameMode->SafeZoneIndicator->OnSafeZoneStateChange(2, false);
        if (GameMode->SafeZoneIndicator->HasSafezoneStateChangedDelegate())
            GameMode->SafeZoneIndicator->SafezoneStateChangedDelegate.Process(GameMode->SafeZoneIndicator, 2);

        if (!bInitial)
            for (auto& UncastedPlayer : GameMode->AlivePlayers)
            {
                auto PlayerController = (AFortPlayerControllerAthena*)UncastedPlayer;

                // Spectra bots carry no quest manager; they only fill the lobby.
                if (PlayerController->PlayerState && ((AFortPlayerStateAthena*)PlayerController->PlayerState)->bIsABot)
                    continue;

                PlayerController->GetQuestManager(1)->SendStatEvent(PlayerController, EFortQuestObjectiveStatEvent::GetStormPhase(), 1, false);
            }
    }
}

void (*SpawnInitialSafeZoneOG)(AFortGameMode* GameMode);
void SpawnInitialSafeZone(AFortGameMode* GameMode)
{
    //return;
    GameMode->bSafeZoneActive = true;
    auto SafeZoneIndicator = SetupSafeZoneIndicator(GameMode);

    SafeZoneIndicator->OnSafeZonePhaseChanged.Bind(GameMode, FName(L"HandlePostSafeZonePhaseChanged"));
    GameMode->OnSafeZoneIndicatorSpawned.Process(SafeZoneIndicator);

    StartNewSafeZonePhase(GameMode, FConfiguration::bLateGame ? (FConfiguration::LateGameZone + (VersionInfo.FortniteVersion >= 24 ? 3 : 0)) : 1, true);


    //return SpawnInitialSafeZoneOG(GameMode);
}

void (*UpdateSafeZonesPhaseOG)(AFortGameMode* GameMode);
void UpdateSafeZonesPhase(AFortGameMode* GameMode)
{
    auto& Array = GameMode->SafeZoneIndicator && GameMode->SafeZoneIndicator->HasSafeZonePhases() ? GameMode->SafeZoneIndicator->SafeZonePhases : Phases;
    if (GameMode->bSafeZoneActive && UGameplayStatics::GetTimeSeconds(GameMode) >= GameMode->SafeZoneIndicator->SafeZoneFinishShrinkTime && !GameMode->bSafeZonePaused && Array.IsValidIndex(GameMode->SafeZoneIndicator->CurrentPhase + 1))
        StartNewSafeZonePhase(GameMode, GameMode->SafeZoneIndicator->CurrentPhase + 1);

    return UpdateSafeZonesPhaseOG(GameMode);
}


void GetPhaseInfo(UObject* Context, FFrame& Stack, bool* Ret)
{
    auto& OutSafeZonePhase = Stack.StepCompiledInRef<FFortSafeZonePhaseInfo>();
    int32 InPhaseToGet;
    Stack.StepCompiledIn(&InPhaseToGet);
    Stack.IncrementCode();
    auto SafeZoneIndicator = (AFortSafeZoneIndicator*)Context;
    auto& Array = SafeZoneIndicator->HasSafeZonePhases() ? SafeZoneIndicator->SafeZonePhases : Phases;

    if (Array.IsValidIndex(InPhaseToGet))
    {
        OutSafeZonePhase = Array[InPhaseToGet];

        *Ret = true;
        return;
    }
    *Ret = false;
}

class AFortNavMesh : public AActor
{
public:
    UCLASS_COMMON_MEMBERS(AFortNavMesh);

    DEFINE_PROP(HotSpotManager, const UObject*);
};
void (*OnWorldInitDoneOG)(UNavigationSystem* NavSys, char Mode);
void OnWorldInitDone(UNavigationSystem* NavSys, char Mode)
{
    printf("OnWorldInitDone\n");
    /*NavSys->bAutoCreateNavigationData = true;
    NavSys->bAllowClientSideNavigation = true;
    NavSys->bSupportRebuilding = true;

    OnWorldInitDoneOG(NavSys, Mode);

    auto AllBounds = Utils::GetAll(FindClass("NavMeshBoundsVolume"));
    auto AllNavmeshes = Utils::GetAll<AFortNavMesh>();
    auto HotSpotMgr = TUObjectArray::FindFirstObject("FortAIHotSpotManager");

    //auto Test = (void(*)(UNavigationSystem*)) (ImageBase + 0x1F5C290);
    //Test(NavSys);

    NavSys->OnNavigationBoundsUpdated(AllBounds[0]);
    AllNavmeshes[0]->HotSpotManager = HotSpotMgr;
    //printf("NavGraphData: %llx, AllBounds.Num() = %d\n", NavSys->NavGraphData, AllBounds.Num());
    AllBounds.Free();
    AllNavmeshes.Free();*/
}

void AFortGameMode::FinishWorldInitialization(AFortGameMode* _this, AActor* WorldManager)
{
    auto GameMode = (AFortGameModeAthena*)_this;
    auto GameState = (AFortGameStateAthena*)GameMode->GameState;

    //if (VersionInfo.EngineVersion == 4.25)
    //    SetupPlaylist(GameMode, GameState);

    printf("[GameMode] FinishWorldInitialization\n");
    FinishWorldInitializationOG(_this, WorldManager);


    auto AddToTierData = [&](const UDataTable* Table, TArray<FFortLootTierData*>& TempArr)
        {
            if (!Table)
                return;

            Table->AddToRoot();
            if (VersionInfo.FortniteVersion >= 20)
            {
                if (auto CompositeTable = Table->Cast<UCompositeDataTable>())
                    for (auto& ParentTable : CompositeTable->ParentTables)
                        if (ParentTable)
                            for (auto& [Key, Val] : *(TMap<int32, FFortLootTierData*>*) (__int64(ParentTable) + 0x30))
                                TempArr.Add(Val);

                for (auto& [Key, Val] : *(TMap<int32, FFortLootTierData*>*) (__int64(Table) + 0x30))
                {
                    bool bFound = false;

                    for (auto& TierData : TempArr)
                        if (TierData->TierGroup == Val->TierGroup && TierData->LootPackage == Val->LootPackage)
                        {
                            TierData = Val;
                            bFound = true;
                            break;
                        }

                    if (!bFound)
                        TempArr.Add(Val);
                }
            }
            else
            {
                if (auto CompositeTable = Table->Cast<UCompositeDataTable>())
                    for (auto& ParentTable : CompositeTable->ParentTables)
                        if (ParentTable)
                            for (auto& [Key, Val] : (TMap<FName, FFortLootTierData*>) ParentTable->RowMap)
                                TempArr.Add(Val);

                for (auto& [Key, Val] : (TMap<FName, FFortLootTierData*>) Table->RowMap)
                {
                    bool bFound = false;

                    for (auto& TierData : TempArr)
                        if (TierData->TierGroup == Val->TierGroup && TierData->LootPackage == Val->LootPackage)
                        {
                            TierData = Val;
                            bFound = true;
                            break;
                        }

                    if (!bFound)
                        TempArr.Add(Val);
                }
            }
        };

    auto AddToPackages = [&](const UDataTable* Table, std::unordered_map<int32, FFortLootPackageData*>& TempArr)
        {
            if (!Table)
                return;

            Table->AddToRoot();
            if (VersionInfo.FortniteVersion >= 20)
            {
                if (auto CompositeTable = Table->Cast<UCompositeDataTable>())
                    for (auto& ParentTable : CompositeTable->ParentTables)
                        if (ParentTable)
                            for (auto& [Key, Val] : *(TMap<int32, FFortLootPackageData*>*) (__int64(ParentTable) + 0x30))
                                TempArr[Key] = Val;

                for (auto& [Key, Val] : *(TMap<int32, FFortLootPackageData*>*) (__int64(Table) + 0x30))
                    TempArr[Key] = Val;
            }
            else
            {
                if (auto CompositeTable = Table->Cast<UCompositeDataTable>())
                    for (auto& ParentTable : CompositeTable->ParentTables)
                        if (ParentTable)
                            for (auto& [Key, Val] : (TMap<FName, FFortLootPackageData*>) ParentTable->RowMap)
                                TempArr[Key.ComparisonIndex] = Val;

                for (auto& [Key, Val] : (TMap<FName, FFortLootPackageData*>) Table->RowMap)
                {
                    TempArr[Key.ComparisonIndex] = Val;
                }
            }
        };

    auto Playlist = SyndicatePlaylist();

    // Chapter 2 entrance: the intro playlist has no floor loot rows of its own ("no loot rows for
    // tier=Loot_AthenaFloorLoot"), so its match uses Solo's loot tables.
    if (!Playlist || wcsstr(FConfiguration::Playlist, L"Intro_Apollo"))
        Playlist = FindObject<UFortPlaylistAthena>(L"/Game/Athena/Playlists/Playlist_DefaultSolo.Playlist_DefaultSolo");

    TArray<FFortLootTierData*> LootTierDataTempArr;
    auto LootTierData = Playlist ? Playlist->LootTierData.Get() : nullptr;
    if (!LootTierData)
        LootTierData = FindObject<UDataTable>(GameMode->HasWarmupRequiredPlayerCount() ? L"/Game/Items/Datatables/AthenaLootTierData_Client.AthenaLootTierData_Client" : L"/Game/Items/Datatables/LootTierData_Client.LootTierData_Client");
    if (LootTierData)
        AddToTierData(LootTierData, LootTierDataTempArr);

    for (auto& Val : LootTierDataTempArr)
        TierDataMap[Val->TierGroup.ComparisonIndex].Add(Val);

    std::unordered_map<int32, FFortLootPackageData*> LootPackageTempArr;
    auto LootPackages = Playlist ? Playlist->LootPackages.Get() : nullptr;
    if (!LootPackages)
        LootPackages = FindObject<UDataTable>(GameMode->HasWarmupRequiredPlayerCount() ? L"/Game/Items/Datatables/AthenaLootPackages_Client.AthenaLootPackages_Client" : L"/Game/Items/Datatables/LootPackages_Client.LootPackages_Client");
    if (LootPackages)
        AddToPackages(LootPackages, LootPackageTempArr);

    for (auto& [_, Val] : LootPackageTempArr)
        LootPackageMap[Val->LootPackageID.ComparisonIndex].Add(Val);

    auto GameFeatureDataClass = FindClass("FortGameFeatureData");
    if (GameFeatureDataClass)
        for (int i = 0; i < TUObjectArray::Num(); i++)
        {
            auto Object = TUObjectArray::GetObjectByIndex(i);

            if (!Object || !Object->Class || Object->IsDefaultObject())
                continue;

            if (Object->IsA(GameFeatureDataClass))
            {
                static auto DefaultLootTableDataOffset = Object->GetOffset("DefaultLootTableData");
                static auto PlaylistOverrideLootTableDataOffset = Object->GetOffset("PlaylistOverrideLootTableData");

                auto& LootTableData = GetFromOffset<FFortGameFeatureLootTableData>(Object, DefaultLootTableDataOffset);
                auto& LootTableDataUE53 = GetFromOffset<FFortGameFeatureLootTableData_UE53>(Object, DefaultLootTableDataOffset);
                auto& PlaylistOverrideLootTableData = GetFromOffset<TMap<FGameplayTag, FFortGameFeatureLootTableData>>(Object, PlaylistOverrideLootTableDataOffset);
                auto& PlaylistOverrideLootTableDataLWC = GetFromOffset<TMap<int32, FFortGameFeatureLootTableData>>(Object, PlaylistOverrideLootTableDataOffset);
                auto& PlaylistOverrideLootTableDataUE53 = GetFromOffset<TMap<int32, FFortGameFeatureLootTableData_UE53>>(Object, PlaylistOverrideLootTableDataOffset);
                auto LTDFeatureData = VersionInfo.EngineVersion >= 5.3 ? LootTableDataUE53.LootTierData.Get() : LootTableData.LootTierData.Get();
                auto LootPackageData = VersionInfo.EngineVersion >= 5.3 ? LootTableDataUE53.LootPackageData.Get() : LootTableData.LootPackageData.Get();

                if (LTDFeatureData)
                {
                    TArray<FFortLootTierData*> LTDTempData;

                    AddToTierData(LTDFeatureData, LTDTempData);

                    if (Playlist)
                    {
                        if (VersionInfo.EngineVersion >= 5.3)
                        {
                            /*for (auto& Tag : Playlist->GameplayTagContainer.GameplayTags)
                                for (auto& Override : PlaylistOverrideLootTableDataUE52)
                                    if (Tag.TagName.ComparisonIndex == Override.First)
                                        AddToTierData(Override.Second.LootTierData.Get(), LTDTempData);*/
                        }
                        else if (VersionInfo.FortniteVersion < 20.00)
                        {
                            for (auto& Tag : Playlist->GameplayTagContainer.GameplayTags)
                                for (auto& Override : PlaylistOverrideLootTableData)
                                    if (Tag.TagName == Override.First.TagName)
                                        AddToTierData(Override.Second.LootTierData.Get(), LTDTempData);
                        }
                        else
                            for (auto& Tag : Playlist->GameplayTagContainer.GameplayTags)
                                for (auto& Override : PlaylistOverrideLootTableDataLWC)
                                    if (Tag.TagName.ComparisonIndex == Override.First)
                                        AddToTierData(Override.Second.LootTierData.Get(), LTDTempData);
                    }

                    //for (auto& [_, Val] : LTDTempData)
                    //    TierDataAllGroups.Add(Val);

                    for (auto& Val : LTDTempData)
                        TierDataMap[Val->TierGroup.ComparisonIndex].Add(Val);
                }

                if (LootPackageData)
                {
                    std::unordered_map<int32, FFortLootPackageData*> LPTempData;

                    AddToPackages(LootPackageData, LPTempData);

                    if (Playlist)
                    {
                        if (VersionInfo.EngineVersion >= 5.3)
                        {
                            /*for (auto& Tag : Playlist->GameplayTagContainer.GameplayTags)
                                for (auto& Override : PlaylistOverrideLootTableDataUE52)
                                    if (Tag.TagName.ComparisonIndex == Override.First)
                                        AddToPackages(Override.Second.LootPackageData.Get(), LPTempData);*/
                        }
                        else if (VersionInfo.FortniteVersion < 20.00)
                        {
                            for (auto& Tag : Playlist->GameplayTagContainer.GameplayTags)
                                for (auto& Override : PlaylistOverrideLootTableData)
                                    if (Tag.TagName == Override.First.TagName)
                                        AddToPackages(Override.Second.LootPackageData.Get(), LPTempData);
                        }
                        else
                            for (auto& Tag : Playlist->GameplayTagContainer.GameplayTags)
                                for (auto& Override : PlaylistOverrideLootTableDataLWC)
                                    if (Tag.TagName.ComparisonIndex == Override.First)
                                        AddToPackages(Override.Second.LootPackageData.Get(), LPTempData);
                    }


                    for (auto& [_, Val] : LPTempData)
                        LootPackageMap[Val->LootPackageID.ComparisonIndex].Add(Val);
                }
            }
        }

    UFortLootPackage::SpawnFloorLootForContainer(FindObject<UClass>(L"/Game/Athena/Environments/Blueprints/Tiered_Athena_FloorLoot_Warmup.Tiered_Athena_FloorLoot_Warmup_C"));
    UFortLootPackage::SpawnFloorLootForContainer(FindObject<UClass>(L"/Game/Athena/Environments/Blueprints/Tiered_Athena_FloorLoot_01.Tiered_Athena_FloorLoot_01_C"));

    TArray<ABGAConsumableSpawner*> ConsumableSpawners{};
    Utils::GetAll<ABGAConsumableSpawner>(ConsumableSpawners);

    for (auto& Spawner : ConsumableSpawners)
        UFortLootPackage::SpawnConsumableActor(Spawner);

    ConsumableSpawners.Free();

    if (AFortAthenaLivingWorldStaticPointProvider::StaticClass())
    {
        TArray<AFortAthenaLivingWorldStaticPointProvider*> Spawners;
        Utils::GetAll<AFortAthenaLivingWorldStaticPointProvider>(Spawners);
        UEAllocatedMap<FName, const UClass*> VehicleSpawnerMap =
        {
            { FName(L"Athena.Vehicle.SpawnLocation.Motorcycle.Dirtbike"), FindObject<UClass>(L"/Dirtbike/Vehicle/Motorcycle_DirtBike_Vehicle.Motorcycle_DirtBike_Vehicle_C") },
            { FName(L"Athena.Vehicle.SpawnLocation.Motorcycle.Sportbike"), FindObject<UClass>(L"/Sportbike/Vehicle/Motorcycle_Sport_Vehicle.Motorcycle_Sport_Vehicle_C") },
            { FName(L"Athena.Vehicle.SpawnLocation.Valet.BasicCar.Taxi"), FindObject<UClass>(L"/Valet/TaxiCab/Valet_TaxiCab_Vehicle.Valet_TaxiCab_Vehicle_C") },
            { FName(L"Athena.Vehicle.SpawnLocation.Valet.BasicCar.Modded"), FindObject<UClass>(L"/ModdedBasicCar/Vehicle/Valet_BasicCar_Vehicle_SuperSedan.Valet_BasicCar_Vehicle_SuperSedan_C") },
            { FName(L"Athena.Vehicle.SpawnLocation.Valet.BasicTruck.Upgraded"), FindObject<UClass>(L"/Valet/BasicTruck/Valet_BasicTruck_Vehicle_Upgrade.Valet_BasicTruck_Vehicle_Upgrade_C") },
            { FName(L"Athena.Vehicle.SpawnLocation.Valet.BigRig.Upgraded"), FindObject<UClass>(L"/Valet/BigRig/Valet_BigRig_Vehicle_Upgrade.Valet_BigRig_Vehicle_Upgrade_C") },
            { FName(L"Athena.Vehicle.SpawnLocation.Valet.SportsCar.Upgraded"), FindObject<UClass>(L"/Valet/SportsCar/Valet_SportsCar_Vehicle_Upgrade.Valet_SportsCar_Vehicle_Upgrade_C") },
            { FName(L"Athena.Vehicle.SpawnLocation.Valet.BasicCar.Upgraded"), FindObject<UClass>(L"/Valet/BasicCar/Valet_BasicCar_Vehicle_Upgrade.Valet_BasicCar_Vehicle_Upgrade_C") }
        };

        for (auto& Spawner : Spawners)
        {
            const UClass* VehicleClass = nullptr;
            for (int i = 0; i < Spawner->FiltersTags.GameplayTags.Num(); i++)
            {
                auto& Tag = Spawner->FiltersTags.GameplayTags.Get(i, FGameplayTag::Size());

                if (VehicleSpawnerMap.contains(Tag.TagName))
                {
                    VehicleClass = VehicleSpawnerMap[Tag.TagName];
                    break;
                }
            }

            if (VehicleClass)
            {
                auto Vehicle = UWorld::SpawnActor<AFortAthenaVehicle>(VehicleClass, Spawner->K2_GetActorLocation(), Spawner->K2_GetActorRotation());

                if (auto Car = Vehicle->Cast<AFortDagwoodVehicle>())
                    Car->SetFuel(100.f);
                //printf("Spawned a %s\n", Spawner->Name.ToString().c_str());
            }
            else
            {
                for (auto& Tag : Spawner->FiltersTags.GameplayTags)
                    printf("Fix: Tag: %s\n", Tag.TagName.ToString().c_str());
            }
        }
        Spawners.Free();
    }
    // not an else here because they still use spawners for boats, and fully on s27
    if (VersionInfo.FortniteVersion >= 4.23 && std::floor(VersionInfo.FortniteVersion) != 20 && std::floor(VersionInfo.FortniteVersion) != 21) // its auto on s20 & s21
    {
        TArray<AFortAthenaVehicleSpawner*> Spawners{};
        Utils::GetAll<AFortAthenaVehicleSpawner>(Spawners);

        for (auto& Spawner : Spawners)
        {
            auto Vehicle = UWorld::SpawnActor<AFortAthenaVehicle>(Spawner->GetVehicleClass(), Spawner->K2_GetActorLocation(), Spawner->K2_GetActorRotation());

            if (auto Car = Vehicle->Cast<AFortDagwoodVehicle>())
                Car->SetFuel(100.f);
        }

        Spawners.Free();
    }

    if (VersionInfo.FortniteVersion > 3.4)
    {
        TArray<ABuildingItemCollectorActor*> Collectors{};
        Utils::GetAll<ABuildingItemCollectorActor>(Collectors);
        for (auto& CollectorActor : Collectors)
        {
            if (Sum > Weight)
            {
            PickNum:
                auto RandomNum = (float)rand() / (RAND_MAX / TotalWeight);

                int Rarity = 0;
                bool found = false;

                for (auto& Element : WeightMap)
                {
                    float Weight = Element.second;

                    if (Weight == 0)
                        continue;

                    if (RandomNum <= Weight)
                    {
                        Rarity = Element.first;

                        found = true;
                        break;
                    }

                    RandomNum -= Weight;
                }

                if (!found)
                    goto PickNum;

                if (Rarity == 0)
                {
                    CollectorActor->K2_DestroyActor();
                    continue;
                }

                int AttemptsToGetItem = 0;
                for (int i = 0; i < CollectorActor->ItemCollections.Num(); i++)
                {
                    if (AttemptsToGetItem > 10)
                    {
                        AttemptsToGetItem = 0;
                        goto PickNum;
                    }

                    auto& Collection = CollectorActor->ItemCollections.Get(i, FCollectorUnitInfo::Size());

                    if (Collection.bUseDefinedOutputItem)
                        continue;

                    TArray<FFortItemEntry*> LootDrops{};

                    UFortLootPackage::ChooseLootForContainer(LootDrops, CollectorActor->DefaultItemLootTierGroupName, Rarity);

                    if (Collection.OutputItemEntry.Num() > 0)
                    {
                        Collection.OutputItemEntry.ResetNum();
                        Collection.OutputItem = nullptr;
                    }

                    for (auto& LootDrop : LootDrops)
                    {
                        if (!Collection.OutputItem && AFortInventory::IsPrimaryQuickbar(LootDrop->ItemDefinition))
                            Collection.OutputItem = LootDrop->ItemDefinition;

                        Collection.OutputItemEntry.Add(*LootDrop, FFortItemEntry::Size());
                        free(LootDrop);
                    }

                    if (!Collection.OutputItem)
                    {
                        i--;
                        AttemptsToGetItem++;

                        continue;
                    }

                    AttemptsToGetItem = 0;
                }

                CollectorActor->StartingGoalLevel = Rarity;
            }
            else
                CollectorActor->K2_DestroyActor();
        }
        Collectors.Free();

        Utils::ExecHook((UFunction*)FindObject<UFunction>(L"/Game/Athena/Items/Gameplay/VendingMachine/B_Athena_VendingMachine.B_Athena_VendingMachine_C:VendWobble__FinishedFunc"), VendWobble__FinishedFunc, VendWobble__FinishedFuncOG);
    }
    //Utils::ExecHook((UFunction*)FindObject<UFunction>(L"/Game/Athena/Items/Consumables/Parents/GA_Athena_MedConsumable_Parent.GA_Athena_MedConsumable_Parent_C:Triggered_4C02BFB04B18D9E79F84848FFE6D2C32"), AFortPlayerPawnAthena::Athena_MedConsumable_Triggered, AFortPlayerPawnAthena::Athena_MedConsumable_TriggeredOG);

}

void AFortGameMode::Hook()
{
    Utils::ExecHook(GetDefaultObj()->GetFunction("ReadyToStartMatch"), ReadyToStartMatch_, ReadyToStartMatch_OG);
    Utils::Hook(FindFinishWorldInitialization(), FinishWorldInitialization, FinishWorldInitializationOG);
    //if (VersionInfo.EngineVersion == 4.16)
    //    Utils::Hook(Memcury::Scanner::FindPattern("40 55 53 56 41 56 48 8D 6C 24 ? 48 81 EC ? ? ? ? 48 8B 05 ? ? ? ? 48 33 C4 48 89 45 ? 48 8B 01 48 8B F1").Get(), OnWorldInitDone, OnWorldInitDoneOG);
}

void AFortGameMode::PostLoadHook()
{
    ApplyCharacterCustomization = FindApplyCharacterCustomization();
    NotifyGameMemberAdded_ = FindNotifyGameMemberAdded();

    auto spdf = GetDefaultObj()->GetFunction("SpawnDefaultPawnFor");
    SpawnDefaultPawnForIdx = spdf->GetVTableIndex();

    Utils::ExecHook(spdf, SpawnDefaultPawnFor);
    Utils::ExecHook(GetDefaultObj()->GetFunction("HandleStartingNewPlayer"), HandleStartingNewPlayer_, HandleStartingNewPlayer_OG);
    Utils::Hook(FindPickTeam(), PickTeam, PickTeamOG);
    if (VersionInfo.FortniteVersion < 25.20)
    {
        Utils::Hook(FindStartAircraftPhase(), StartAircraftPhase, StartAircraftPhaseOG);
        Utils::Hook(FindHandlePostSafeZonePhaseChanged(), HandlePostSafeZonePhaseChanged, HandlePostSafeZonePhaseChangedOG);
    }
    Utils::ExecHook(AFortGameModeAthena::GetDefaultObj()->GetFunction("OnAircraftExitedDropZone"), OnAircraftExitedDropZone_, OnAircraftExitedDropZone_OG);

    if (VersionInfo.FortniteVersion >= 21.50)
    {
        if (VersionInfo.FortniteVersion < 25.20)
        {
            Utils::Hook(FindSpawnInitialSafeZone(), SpawnInitialSafeZone, SpawnInitialSafeZoneOG);
            Utils::Hook(FindUpdateSafeZonesPhase(), UpdateSafeZonesPhase, UpdateSafeZonesPhaseOG);
        }
        Utils::ExecHook(L"/Script/FortniteGame.FortSafeZoneIndicator.GetPhaseInfo", GetPhaseInfo);
    }
}
