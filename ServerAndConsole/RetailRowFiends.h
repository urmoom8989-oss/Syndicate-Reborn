#pragma once
// Season X Retail Row Fiends (the "zombies").
//
// Patch 10.10 turned Mega Mall back into Retail Row through a Rift Zone. In that zone the game spawns
// hordes of Fiends that charge at players, may drop a weapon, consumable or ammo, and cannot leave the
// zone. The game runs them through its AI Director: AI encounters whose rifts (ABuildingRift) spawn the
// fiend pawns (AFortAIPawn subclasses such as EnemyPawn_Parent_C). Syndicate spawned the director in
// GameMode.h but never activated it, so no encounter ever ran and Retail Row stayed empty.
//
// The fiends come from BuildingProp_DeimosSpawner props in Retail Row. In Creative they switch on at a
// minigame start or on a "turn on" message; a Battle Royale match sends neither, so they stay idle. This
// switches them on (OnTurnOnReceived) together with the director.
//
// This activates the director once the match leaves warmup (bus phase or later), so the director can
// never add its own pawns to Syndicate's warmup bot population. It then reports every 30 seconds to
// Syndicate_log.txt how many rifts, fiend pawns and native bots exist, so a test match shows whether the
// fiends spawn. Set SYNDICATE_RETAIL_FIENDS=0 (Windows environment variable) to switch this off.
//
// A test showed no Deimos spawner and no AI spawner at all in the server's world, so the level that holds
// them is not loaded on the server. Syndicate never streams the playlist's AdditionalLevels (Erbium does)
// and never touches POI foundations, so at warmup this:
//   - loads the playlist's AdditionalLevels and lists them in AdditionalPlaylistLevelsStreamed for clients;
//   - asks any streaming sublevel named after Retail Row / Mega Mall / Deimos / rifts that is not loaded
//     to load and show;
//   - writes every foundation (name, map tag, enabled state, level, extra worlds) and every matching
//     streaming level to Syndicate_log.txt, so the next test shows where the rift zone lives.
//
// The next test showed Retail Row (Athena_POI_StripMall) and the fiends' own nav level (Athena_Nav_Mall)
// are loaded, the playlist has no extra levels, and still no spawner or rift exists. So nothing in 10.40's
// match sets the horde off on its own. Fallback: Syndicate spawns the fiends itself.
//   - While a real player is within 20,000 units of Retail Row, keep up to 10 fiends
//     (EnemyPawn_Parent_Deimos_C) alive on random points of the fiends' nav mesh inside Retail Row, a new
//     one every 4 seconds. Their own AI controller and behaviour run them.
//   - Fiends that leave Retail Row are removed ("Fiends cannot go outside the Retail Row Rift Zone").
//   - It also writes, once, the actor classes found in the Retail Row levels and every loaded class whose
//     name mentions Deimos, fiends, rifts or the horde, for finding the game's own trigger later.
#include "framework.h"
#include "SyndicateRuntime.h"
#include "NativeNavigation.h"
#include <map>

namespace RetailRowFiends
{
inline UWorld *World = nullptr;
inline bool Activated = false, AtBus = false;
inline float NextReport = 0.f;
inline int Reports = 0;

inline bool Enabled()
{
    static const bool Value = [] {
        char Buffer[8]{};
        DWORD Length = GetEnvironmentVariableA("SYNDICATE_RETAIL_FIENDS", Buffer, sizeof(Buffer));
        return !(Length > 0 && Buffer[0] == '0');
    }();
    return Value;
}

inline int Spawners = 0, TurnedOn = 0;
inline bool LevelsDone = false;

inline std::string Lower(std::string Text)
{
    for (auto &C : Text) C = (char)tolower((unsigned char)C);
    return Text;
}

inline bool IsRiftZoneName(const std::string &Text)
{
    auto L = Lower(Text);
    for (auto Word : {"retail", "mall", "deimos", "rift", "fiend", "zombie"})
        if (L.find(Word) != std::string::npos) return true;
    return false;
}

inline std::string SoftName(const TSoftObjectPtr<UWorld> &Level)
{
    return Level.ObjectID.AssetPathName.ToString();
}

// The playlist's AdditionalLevels: Erbium streams these at match start, Syndicate never did.
inline void LoadPlaylistLevels(UWorld *W, AFortGameStateAthena *GS)
{
    auto Playlist = GS->CurrentPlaylistInfo.BasePlaylist;
    if (!Playlist) { Log("Retail Row fiends: no playlist, no additional levels"); return; }
    int Count = Playlist->AdditionalLevels.Num(), Loaded = 0;
    Log("Retail Row fiends: playlist " + Playlist->GetName() + " has " + std::to_string(Count) + " additional levels");
    for (int i = 0; i < Count; i++)
    {
        auto &Level = Playlist->AdditionalLevels[i];
        auto Name = SoftName(Level);
        bool Already = false;
        for (int j = 0; j < GS->AdditionalPlaylistLevelsStreamed.Num(); j++)
            if (GS->AdditionalPlaylistLevelsStreamed[j].ToString() == Name) Already = true;
        if (Already) { Log("  additional level already streamed: " + Name); continue; }
        bool Success = false;
        ULevelStreamingDynamic::LoadLevelInstanceBySoftObjectPtr(W, Level, FVector(), FRotator(), &Success);
        Log("  additional level " + Name + (Success ? ": loading" : ": failed to load"));
        if (Success)
        {
            if (GS->AdditionalPlaylistLevelsStreamed.Add(Level.ObjectID.AssetPathName)) ++Loaded;
            else Log("  (no room to tell clients about " + Name + "; loaded on the server only)");
        }
    }
    if (Loaded) GS->OnRep_AdditionalPlaylistLevelsStreamed();
}

// Sublevels already listed in the world: report the rift-zone ones and make sure they load.
inline void LoadRiftZoneSublevels(UWorld *W)
{
    int Total = W->StreamingLevels.Num(), Matching = 0, Requested = 0;
    for (int i = 0; i < Total; i++)
    {
        auto Streaming = W->StreamingLevels[i];
        if (!Streaming) continue;
        std::string Name = Streaming->PackageNameToLoad.ToString();
        if (Name.empty() || Name == "None") Name = Streaming->WorldAsset.ObjectID.AssetPathName.ToString();
        if (!IsRiftZoneName(Name)) continue;
        ++Matching;
        bool Loaded = Streaming->IsLevelLoaded(), Visible = Streaming->IsLevelVisible();
        Log("  streaming level " + Name + " loaded=" + (Loaded ? "yes" : "no") + " visible=" + (Visible ? "yes" : "no") +
            " shouldLoad=" + (Streaming->bShouldBeLoaded ? "yes" : "no"));
        if (!Loaded || !Visible)
        {
            Streaming->SetShouldBeLoaded(true);
            Streaming->SetShouldBeVisible(true);
            ++Requested;
        }
    }
    Log("Retail Row fiends: " + std::to_string(Total) + " streaming levels, " + std::to_string(Matching) +
        " rift-zone ones, " + std::to_string(Requested) + " asked to load");
}

// Every foundation, once, so the log shows which one is the Retail Row rift zone and what it streams.
inline void ListFoundations()
{
    int Count = 0;
    for (auto &Ref : Syndicate::ActorsOf<ABuildingFoundation>())
    {
        auto F = Ref.Get();
        if (!F) continue;
        ++Count;
        std::string Line = "  foundation " + F->GetName() + " tag=" + F->MapLocationTag.TagName.ToString() +
                           " state=" + std::to_string((int)F->FoundationEnabledState) +
                           " type=" + std::to_string((int)F->DynamicFoundationType) +
                           " streamed=" + (F->bServerStreamedInLevel ? "1" : "0") +
                           " level=" + F->LevelToStream.ToString();
        auto P = F->K2_GetActorLocation();
        Line += " at " + std::to_string((int)P.X) + "," + std::to_string((int)P.Y);
        for (int i = 0; i < F->AdditionalWorlds.Num(); i++) Line += " +world " + SoftName(F->AdditionalWorlds[i]);
        Log(Line);
    }
    Log("Retail Row fiends: " + std::to_string(Count) + " foundations listed");
}

// Retail Row's middle, from its POI foundation (25088,43008 on 10.40).
inline FVector Center{25088.f, 43008.f, 0.f};
inline constexpr float ZoneRadius = 9000.f, PlayerRange = 20000.f;
inline constexpr int MaxFiends = 10;
inline float NextSpawn = 0.f;
inline int SpawnedTotal = 0, SpawnFailures = 0, Removed = 0;
inline std::vector<Syndicate::Ref<AFortAIPawn>> Fiends;

inline void FindCenter()
{
    for (auto &Ref : Syndicate::ActorsOf<ABuildingFoundation>())
        if (auto F = Ref.Get(); F && F->MapLocationTag.TagName.ToString() == "Athena.Location.POI.RetailRow")
        {
            Center = F->K2_GetActorLocation();
            Log("Retail Row fiends: Retail Row centre " + std::to_string((int)Center.X) + "," + std::to_string((int)Center.Y));
            return;
        }
}

// The fiends' nav mesh: any nav data that is not Syndicate's player-bot "Phoebe" mesh.
inline ANavigationData *FiendNav()
{
    auto W = UWorld::GetWorld();
    auto Nav = W ? Cast<UFortNavSystem>(W->NavigationSystem) : nullptr;
    if (!Nav) return nullptr;
    for (auto Data : Nav->NavDataSet)
        if (Data && Syndicate::Ref<ANavigationData>(Data).Get() && Data->NavDataConfig.Name.ToString() != "Phoebe")
            return Data;
    return nullptr;
}

inline UClass *FiendClass()
{
    static UClass *Class = nullptr;
    static bool Tried = false;
    if (!Tried)
    {
        Tried = true;
        Class = AEnemyPawn_Parent_Deimos_C::StaticClass();
        Log(std::string("Retail Row fiends: fiend class ") + (Class ? Class->GetName() : "not loaded"));
    }
    return Class;
}

inline bool PlayerNearby()
{
    for (auto &Ref : Syndicate::ActorsOf<AFortPlayerPawnAthena>())
    {
        auto P = Ref.Get();
        if (!P || !P->Controller || !P->Controller->IsA(AFortPlayerControllerAthena::StaticClass())) continue;
        if (Syndicate::Distance2D(P->K2_GetActorLocation(), Center) < PlayerRange) return true;
    }
    return false;
}

// A spot on the ground inside Retail Row. 10.40 has no separate fiend nav mesh at run time, so use the
// player-bot ("Phoebe") nav mesh Syndicate builds, then any nav data, then a straight trace to the ground.
inline bool SpawnPoint(FVector &At)
{
    if (auto Nav = FiendNav();
        Nav && UNavigationSystemV1::K2_GetRandomReachablePointInRadius(UWorld::GetWorld(), Center, &At, ZoneRadius * 0.8f, Nav, nullptr))
        return true;
    float Angle = (float)(rand() % 360) * 0.0174533f, Distance = (float)(rand() % (int)(ZoneRadius * 0.7f));
    FVector Pick{Center.X + cosf(Angle) * Distance, Center.Y + sinf(Angle) * Distance, Center.Z};
    if (NativeNavigation::RandomReachable(Pick, 2500.f, At)) return true;
    FVector Projected{};
    if (UWorld::GetWorld()->NavigationSystem &&
        UNavigationSystemV1::K2_ProjectPointToNavigation(UWorld::GetWorld(), Pick, &Projected, nullptr, nullptr, FVector(600, 600, 8000)))
    {
        At = Projected;
        return true;
    }
    TArray<AActor *> Ignored;
    FHitResult Hit{};
    bool Ground = UKismetSystemLibrary::LineTraceSingle(UWorld::GetWorld(), FVector(Pick.X, Pick.Y, Pick.Z + 20000.f),
                                                         FVector(Pick.X, Pick.Y, Pick.Z - 20000.f), ETraceTypeQuery::TraceTypeQuery1,
                                                         true, Ignored, EDrawDebugTrace::None, &Hit, true, {}, {}, 0.f);
    Ignored.Free();
    if (!Ground) return false;
    At = FVector(Hit.ImpactPoint.X, Hit.ImpactPoint.Y, Hit.ImpactPoint.Z);
    return true;
}

inline void SpawnOne()
{
    auto Class = FiendClass();
    if (!Class) return;
    FVector At{};
    if (!SpawnPoint(At))
    {
        if (++SpawnFailures <= 3) Log("Retail Row fiends: no ground point found to spawn on");
        return;
    }
    At.Z += 100.f;
    FRotator Facing{0.f, (float)(rand() % 360), 0.f};
    auto Pawn = SpawnActor<AFortAIPawn>(At, Facing, nullptr, Class, ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn);
    if (!Pawn)
    {
        if (++SpawnFailures <= 3) Log("Retail Row fiends: spawning a fiend failed");
        return;
    }
    if (!Pawn->Controller) Pawn->SpawnDefaultController();
    Fiends.emplace_back(Pawn);
    if (++SpawnedTotal <= 3)
        Log("Retail Row fiends: spawned " + Pawn->GetName() + " at " + std::to_string((int)At.X) + "," + std::to_string((int)At.Y) + "," +
            std::to_string((int)At.Z) + " controller=" + (Pawn->Controller ? Pawn->Controller->Class->GetName() : "none") +
            " health=" + std::to_string((int)Pawn->GetHealth()));
}

inline bool SpawnTest()
{
    static const bool Value = [] {
        char Buffer[8]{};
        DWORD Length = GetEnvironmentVariableA("SYNDICATE_FIEND_SPAWN", Buffer, sizeof(Buffer));
        return Length > 0 && Buffer[0] == '1';
    }();
    return Value;
}

inline void Horde()
{
    // Drop dead or removed fiends; send home any that left the zone.
    for (auto It = Fiends.begin(); It != Fiends.end();)
    {
        auto P = It->Get();
        if (!P || P->bActorIsBeingDestroyed || P->GetHealth() <= 0.f) { It = Fiends.erase(It); continue; }
        if (Syndicate::Distance2D(P->K2_GetActorLocation(), Center) > ZoneRadius)
        {
            if (auto C = P->Controller) C->K2_DestroyActor();
            P->K2_DestroyActor();
            ++Removed;
            It = Fiends.erase(It);
            continue;
        }
        ++It;
    }
    float Now = Syndicate::Now();
    if ((int)Fiends.size() >= MaxFiends || Now < NextSpawn) return;
    NextSpawn = Now + 4.f;
    if (SpawnFailures >= 30 || !PlayerNearby()) return;
    SpawnOne();
}

// One-off scan: what the Retail Row levels contain, and which fiend-related classes the game has loaded.
inline void Survey()
{
    std::map<std::string, int> Classes;
    for (auto &Ref : Syndicate::ActorsOf<AActor>())
    {
        auto A = Ref.Get();
        if (!A || !A->Class) continue;
        UObject *Outer = A;
        while (Outer->Outer) Outer = Outer->Outer;
        auto Package = Outer->GetName();
        if (Package.find("StripMall") == std::string::npos && Package.find("Nav_Mall") == std::string::npos) continue;
        ++Classes[(Package.find("Nav_Mall") != std::string::npos ? "NavMall " : "StripMall ") + A->Class->GetName()];
    }
    for (auto &[Name, Count] : Classes) Log("  retail level actor " + Name + " x" + std::to_string(Count));
    int Listed = 0;
    for (int I = 0; UObject::GObjects && I < UObject::GObjects->Num() && Listed < 80; ++I)
    {
        auto Object = UObject::GObjects->GetByIndex(I);
        if (!Object || !Object->IsA(UClass::StaticClass()) || Object->IsDefaultObject()) continue;
        auto Name = Object->GetName();
        auto L = Lower(Name);
        for (auto Word : {"deimos", "fiend", "rift", "horde", "enemypawn", "riftzone", "encounter"})
            if (L.find(Word) != std::string::npos) { Log("  loaded class " + Object->GetFullName()); ++Listed; break; }
    }
    Log("Retail Row fiends: survey done (" + std::to_string(Classes.size()) + " actor classes in Retail Row levels, " +
        std::to_string(Listed) + " related classes loaded)");
}

// One-off scan of the game's asset registry (every asset in the pak files, loaded or not) for anything
// that looks like part of the fiend system: Deimos, fiends, horde, rift zones, encounter settings, AI
// mutators, Retail Row. The SDK's AssetRegistry wrappers are not compiled, so this calls the reflected
// functions by hand. FAssetData on 4.23 is 0x50 bytes: ObjectPath 0x00, PackageName 0x08,
// PackagePath 0x10, AssetName 0x18, AssetClass 0x20.
inline std::map<std::string, std::string> Found; // asset name -> full object path

inline void ScanAssets()
{
    static bool Done = false;
    if (Done) return;
    Done = true;
    auto Helpers = UObject::FindObject<UFunction>("Function AssetRegistry.AssetRegistryHelpers.GetAssetRegistry");
    auto ByPath = UObject::FindObject<UFunction>("Function AssetRegistry.AssetRegistry.GetAssetsByPath");
    auto HelpersObject = UObject::FindClassFast("AssetRegistryHelpers");
    if (!Helpers || !ByPath || !HelpersObject || !HelpersObject->DefaultObject)
    {
        Log("Retail Row fiends: asset registry functions not found; no asset scan");
        return;
    }
    struct { UObject *Object; void *Interface; } Registry{};
    HelpersObject->DefaultObject->ProcessEvent(Helpers, &Registry);
    if (!Registry.Object) { Log("Retail Row fiends: no asset registry object"); return; }
    struct RawArray { uint8 *Data; int32 Num; int32 Max; };
    struct
    {
        FName PackagePath;
        RawArray Out;
        bool bRecursive;
        bool bIncludeOnlyOnDiskAssets;
        bool ReturnValue;
        uint8 Pad[5];
    } Params{};
    Params.PackagePath = UKismetStringLibrary::Conv_StringToName(L"/Game");
    Params.bRecursive = true;
    auto Flags = ByPath->FunctionFlags;
    ByPath->FunctionFlags |= 0x400;
    Registry.Object->ProcessEvent(ByPath, &Params);
    ByPath->FunctionFlags = Flags;
    int Total = Params.Out.Num, Listed = 0;
    if (!Params.Out.Data || Total <= 0 || Total > 2000000)
    {
        Log("Retail Row fiends: asset scan returned " + std::to_string(Total) + " assets");
        return;
    }
    // The Dumper SDK's FName::ToString drops everything up to the last '/', so full paths come from the
    // engine's own conversion. Only names of interest are converted (there are ~140,000 assets).
    for (int I = 0; I < Total; I++)
    {
        auto Entry = Params.Out.Data + (size_t)I * 0x50;
        auto Name = reinterpret_cast<FName *>(Entry + 0x18)->ToString();
        auto L = Lower(Name);
        if (L.find("pinkoatmeal") == std::string::npos && L.find("deimosencounter") == std::string::npos &&
            !(L.find("deimos") != std::string::npos && L.find("modifier") != std::string::npos))
            continue;
        auto Class = reinterpret_cast<FName *>(Entry + 0x20)->ToString();
        auto Path = UKismetStringLibrary::Conv_NameToString(*reinterpret_cast<FName *>(Entry + 0x00)).ToString();
        Found[Name] = Path;
        ++Listed;
        Log("  asset " + Class + " " + Path);
    }
    Log("Retail Row fiends: asset scan done (" + std::to_string(Total) + " assets, " + std::to_string(Listed) + " listed)");
}

// The Retail Row rift zone's horde is "PinkOatmeal" in the game files: an encounter gameplay modifier
// (PinkOatmealDeimosEncounterModifier) whose mutator runs the Deimos encounter with
// PinkOatmeal_EncounterSettings_Static, plus a PinkOatmeal_Moss_Location actor in Athena_Nav_Mall.
// In 10.40's default playlists the modifier is no longer listed, so the game never starts it. Add it to
// the playlist's ModifierList before the playlist is applied (OnRep_CurrentPlaylistInfo registers
// playlist modifiers and spawns their mutators), so the game runs the horde itself.
inline void AddRetailRowHorde(UFortPlaylistAthena *Playlist)
{
    if (!Playlist || !Enabled()) return;
    ScanAssets();
    auto It = Found.find("PinkOatmealDeimosEncounterModifier");
    if (It == Found.end()) { Log("Retail Row fiends: PinkOatmealDeimosEncounterModifier is not in the game files"); return; }
    auto Modifier = StaticLoadObject<UFortGameplayModifierItemDefinition>(It->second);
    if (!Modifier) { Log("Retail Row fiends: " + It->second + " did not load"); return; }
    std::string Line = "Retail Row fiends: horde modifier " + It->second;
    for (int M = 0; M < Modifier->Mutators.Num(); M++)
        Line += " mutator=" + UKismetStringLibrary::Conv_NameToString(Modifier->Mutators[M].ObjectID.AssetPathName).ToString();
    Log(Line);
    for (int M = 0; M < Playlist->ModifierList.Num(); M++)
        if (Playlist->ModifierList[M].ObjectID.AssetPathName == UKismetStringLibrary::Conv_StringToName(FString(std::wstring(It->second.begin(), It->second.end()).c_str())))
        {
            Log("Retail Row fiends: playlist already lists the horde modifier");
            return;
        }
    TSoftObjectPtr<UFortGameplayModifierItemDefinition> Entry{};
    Entry.ObjectID.AssetPathName = UKismetStringLibrary::Conv_StringToName(FString(std::wstring(It->second.begin(), It->second.end()).c_str()));
    Playlist->ModifierList.Add(Entry);
    Log("Retail Row fiends: added the horde modifier to " + Playlist->GetName() + " (" + std::to_string(Playlist->ModifierList.Num()) + " modifiers)");
}

// The horde modifier lists two mutators (Mutator_PinkDeimosEncounter and Mutator_AISupport), but adding
// the modifier to the playlist did not spawn them: Syndicate's playlist setup never reaches the game's
// mutator spawning. Spawn them here, at Retail Row, the moment the bus leaves. The encounter mutator only
// starts its encounter when ShouldSpawnEncounter is above 0; that value reads a row of the playlist's game
// data curve table, which the default playlist may not have, so it is forced to 1.
inline bool MutatorsSpawned = false;
inline float WarmupSeen = 0.f;
inline Syndicate::Ref<AFortAthenaMutator_Encounter> HordeMutator;

inline AActor *SpawnDeferred(UClass *Class, const FVector &At)
{
    FTransform Transform{};
    Transform.Scale3D = FVector(1, 1, 1);
    Transform.Translation = At;
    Transform.Rotation = FRotToQuat(FRotator());
    return UGameplayStatics::BeginDeferredActorSpawnFromClass(UWorld::GetWorld(), Class, Transform,
                                                              ESpawnActorCollisionHandlingMethod::AlwaysSpawn, nullptr);
}

inline FVector GroundAtCenter()
{
    FVector At = Center;
    TArray<AActor *> Ignored;
    FHitResult Hit{};
    if (UKismetSystemLibrary::LineTraceSingle(UWorld::GetWorld(), FVector(Center.X, Center.Y, Center.Z + 20000.f),
                                              FVector(Center.X, Center.Y, Center.Z - 20000.f), ETraceTypeQuery::TraceTypeQuery1, true,
                                              Ignored, EDrawDebugTrace::None, &Hit, true, {}, {}, 0.f))
        At = FVector(Hit.ImpactPoint.X, Hit.ImpactPoint.Y, Hit.ImpactPoint.Z + 50.f);
    Ignored.Free();
    return At;
}

inline std::string Describe(AFortAthenaMutator_Encounter *E)
{
    auto &S = E->ShouldSpawnEncounter;
    return E->GetName() + " shouldSpawn value=" + std::to_string(S.Value) + " table=" +
           (S.Curve.CurveTable ? S.Curve.CurveTable->GetName() : std::string("none")) + " row=" + S.Curve.RowName.ToString() +
           " settings=" + (E->EncounterSettings ? E->EncounterSettings->GetName() : std::string("none")) +
           " encounter=" + (E->CurrentEncounter ? "running" : "none");
}

// Test result: the playlist modifier DID make the game spawn Mutator_PinkDeimosEncounter (twice), but its
// encounter never started. Its ShouldSpawnEncounter reads a curve row that the default playlist's game data
// most likely lacks, so it comes out 0. During warmup, replace the game's copies with one whose
// ShouldSpawnEncounter is set to 1 before it begins play (a deferred spawn), placed at Retail Row.
inline void SpawnHordeMutators(bool Again = false)
{
    if (MutatorsSpawned && !Again) return;
    MutatorsSpawned = true;
    int Replaced = 0;
    for (auto &Ref : Syndicate::ActorsOf<AFortAthenaMutator_Encounter>())
        if (auto E = Ref.Get())
        {
            Log("Retail Row fiends: game's mutator " + Describe(E));
            if (E->CurrentEncounter) { HordeMutator.Set(E); continue; }
            if (Again && E == HordeMutator.Get()) HordeMutator.Clear();
            if (E->Class && E->Class->GetName() == "Mutator_PinkDeimosEncounter_C") { E->K2_DestroyActor(); ++Replaced; }
        }
    if (HordeMutator.Get()) { Log("Retail Row fiends: the game's own encounter is already running"); return; }
    bool HaveSupport = false;
    for (auto &Ref : Syndicate::ActorsOf<AFortAthenaMutator>())
        if (auto M = Ref.Get(); M && M->Class && M->Class->GetName() == "Mutator_AISupport_C") HaveSupport = true;
    FVector At = GroundAtCenter();
    for (auto Path : {"/Game/Athena/Playlists/GameplayMutators/Mutator_AISupport.Mutator_AISupport_C",
                      "/Game/Athena/Playlists/GameplayMutators/Mutator_PinkDeimosEncounter.Mutator_PinkDeimosEncounter_C"})
    {
        bool Support = std::string(Path).find("AISupport") != std::string::npos;
        if (Support && HaveSupport) continue;
        auto Class = StaticLoadObject<UClass>(Path);
        if (!Class) { Log(std::string("Retail Row fiends: ") + Path + " did not load"); continue; }
        auto Actor = SpawnDeferred(Class, At);
        if (!Actor) { Log(std::string("Retail Row fiends: could not spawn ") + Path); continue; }
        if (auto Encounter = Cast<AFortAthenaMutator_Encounter>(Actor))
        {
            Encounter->ShouldSpawnEncounter.Value = 1.f;
            Encounter->ShouldSpawnEncounter.Curve.CurveTable = nullptr;
            HordeMutator.Set(Encounter);
        }
        UGameplayStatics::FinishSpawningActor(Actor, Actor->GetTransform());
        Log("Retail Row fiends: spawned " + Actor->GetName() + " at " + std::to_string((int)At.X) + "," + std::to_string((int)At.Y) + "," +
            std::to_string((int)At.Z) + (Support ? "" : " (replaced " + std::to_string(Replaced) + " of the game's copies)"));
    }
}

// The encounter mutator now sits at Retail Row with ShouldSpawnEncounter = 1 (row
// Default.PinkOatmeal.Moss.Enabled), yet its encounter never starts. "Moss" is Retail Row's rift zone: the
// files hold the zone itself (EnvironmentalItems/PinkOatmeal/PinkOatmeal_Moss) and a placed marker for it,
// PinkOatmeal_Moss_Location, which IS loaded in Athena_Nav_Mall. The zone (the bubble that tells the game a
// player is inside Retail Row's rift zone) was never spawned, so no player is ever "in the zone" and the
// encounter has no one to spawn fiends around. Spawn the zone on its marker.
inline bool ZoneSpawned = false;

inline std::string ClassChain(UObject *Object)
{
    std::string Out;
    for (UStruct *C = Object ? Object->Class : nullptr; C; C = C->Super) Out += (Out.empty() ? "" : " < ") + C->GetName();
    return Out;
}

inline void SpawnMossZone()
{
    if (ZoneSpawned) return;
    ZoneSpawned = true;
    AActor *Marker = nullptr;
    for (auto &Ref : Syndicate::ActorsOf<AActor>())
        if (auto A = Ref.Get(); A && A->Class && A->Class->GetName().find("PinkOatmeal") != std::string::npos)
        {
            auto P = A->K2_GetActorLocation();
            Log("Retail Row fiends: rift-zone actor " + A->GetName() + " (" + ClassChain(A) + ") at " + std::to_string((int)P.X) + "," +
                std::to_string((int)P.Y) + "," + std::to_string((int)P.Z));
            if (A->Class->GetName() == "PinkOatmeal_Moss_Location_C") Marker = A;
            if (A->Class->GetName() == "PinkOatmeal_Moss_C") { Log("Retail Row fiends: the Moss rift zone already exists"); return; }
        }
    auto Class = StaticLoadObject<UClass>("/Game/Athena/Items/EnvironmentalItems/PinkOatmeal/PinkOatmeal_Moss.PinkOatmeal_Moss_C");
    if (!Class) { Log("Retail Row fiends: PinkOatmeal_Moss did not load"); return; }
    FTransform Transform{};
    if (Marker) Transform = Marker->GetTransform();
    else
    {
        Transform.Scale3D = FVector(1, 1, 1);
        Transform.Translation = GroundAtCenter();
        Transform.Rotation = FRotToQuat(FRotator());
    }
    auto Zone = UGameplayStatics::BeginDeferredActorSpawnFromClass(UWorld::GetWorld(), Class, Transform,
                                                                   ESpawnActorCollisionHandlingMethod::AlwaysSpawn, nullptr);
    if (!Zone) { Log("Retail Row fiends: could not spawn the Moss rift zone"); return; }
    UGameplayStatics::FinishSpawningActor(Zone, Transform);
    auto P = Zone->K2_GetActorLocation();
    Log("Retail Row fiends: spawned the Moss rift zone " + Zone->GetName() + " (" + ClassChain(Zone) + ") at " + std::to_string((int)P.X) + "," +
        std::to_string((int)P.Y) + "," + std::to_string((int)P.Z) + (Marker ? " on its marker" : " at Retail Row's centre (no marker found)"));
}

inline void SetUpLevels(UWorld *W, AFortGameStateAthena *GS)
{
    LevelsDone = true;
    FindCenter();
    ScanAssets();
    LoadRiftZoneSublevels(W);
    LoadPlaylistLevels(W, GS);
}

// Retail Row's fiend spawners are BuildingProp_DeimosSpawner props placed in the level. In Creative a
// minigame start (bEnabledOnMinigameStart) or a "turn on" message switches them on; a Battle Royale match
// has neither, so they stay idle. Switch every one of them on, the way their own receiver would.
inline void TurnOnSpawners()
{
    Spawners = TurnedOn = 0;
    for (auto &Ref : Syndicate::ActorsOf<ABuildingProp_DeimosSpawner>())
    {
        auto Spawner = Ref.Get();
        if (!Spawner) continue;
        ++Spawners;
        Spawner->OnTurnOnReceived(nullptr);
        ++TurnedOn;
    }
    int OtherSpawners = 0;
    for (auto &Ref : Syndicate::ActorsOf<ABuildingProp_AISpawner>())
        if (Ref.Get() && !Ref.Get()->IsA(ABuildingProp_DeimosSpawner::StaticClass())) ++OtherSpawners;
    Log("Retail Row fiends: " + std::to_string(Spawners) + " Deimos spawners found, " + std::to_string(TurnedOn) +
        " switched on; other AI spawners in the world: " + std::to_string(OtherSpawners));
}

inline void Report(AFortGameModeAthena *GM)
{
    int Rifts = 0, FiendPawns = 0, OtherAI = 0;
    for (auto &Rift : Syndicate::ActorsOf<ABuildingRift>())
        if (Rift.Get()) ++Rifts;
    for (auto &Pawn : Syndicate::ActorsOf<AFortAIPawn>())
    {
        auto P = Pawn.Get();
        if (!P || !P->Class) continue;
        std::string Name = P->Class->GetName();
        if (Name.find("EnemyPawn") != std::string::npos || Name.find("Deimos") != std::string::npos ||
            Name.find("Fiend") != std::string::npos || Name.find("Husk") != std::string::npos)
            ++FiendPawns;
        else
            ++OtherAI;
    }
    Log("Retail Row fiends: director " + std::string(GM->AIDirector ? (Activated ? "active" : "not active") : "missing") +
        ", spawners " + std::to_string(Spawners) + ", rifts " + std::to_string(Rifts) + ", fiend pawns " + std::to_string(FiendPawns) +
        ", other AI pawns " + std::to_string(OtherAI) + ", native bots alive " + std::to_string(GM->AliveBots.Num()) +
        "; horde encounter " + std::string(HordeMutator.Get() ? (HordeMutator.Get()->CurrentEncounter ? "running" : "not started") : "no mutator") +
        (GM->AIDirector ? "; director encounters active " + std::to_string(GM->AIDirector->ActiveEncounters.Num()) + " inactive " +
                          std::to_string(GM->AIDirector->InactiveEncounters.Num()) + " maxAI " + std::to_string(GM->AIDirector->MaxEncounterActiveAliveAI)
                        : std::string()) +
        "; our fiends alive " + std::to_string(Fiends.size()) + ", spawned " + std::to_string(SpawnedTotal) +
        ", left the zone " + std::to_string(Removed));
}

inline void Tick(UWorld *W, AFortGameModeAthena *GM, AFortGameStateAthena *GS)
{
    if (!W || !GM || !GS) return;
    if (World != W)
    {
        World = W;
        AtBus = false;
        NextReport = 0.f;
        Reports = 0;
        Spawners = TurnedOn = 0;
        LevelsDone = false;
        MutatorsSpawned = false;
        ZoneSpawned = false;
        HordeMutator.Clear();
        WarmupSeen = 0.f;
        Fiends.clear();
        NextSpawn = 0.f;
        SpawnedTotal = SpawnFailures = Removed = 0;
    }
    if (!Enabled()) return;

    // Levels first, during warmup, so they have streamed in by the time the bus leaves.
    if (!LevelsDone && GS->GamePhase >= EAthenaGamePhase::Warmup) SetUpLevels(W, GS);
    // Swap in the horde mutator a little into warmup, after the game has spawned its own copies.
    if (GS->GamePhase == EAthenaGamePhase::Warmup && WarmupSeen == 0.f) WarmupSeen = Syndicate::Now();
    if (!MutatorsSpawned && GS->GamePhase >= EAthenaGamePhase::Warmup && WarmupSeen > 0.f && Syndicate::Now() - WarmupSeen > 10.f)
        SpawnMossZone(), SpawnHordeMutators();

    // Only after warmup: the director must not spawn into the warmup bot population.
    if (!AtBus && GM->AIDirector && GS->GamePhase >= EAthenaGamePhase::Aircraft)
    {
        AtBus = true;
        if (!Activated) GM->AIDirector->Activate();
        Activated = true;
        Log("Retail Row fiends: AI director activated (phase " + std::to_string((int)GS->GamePhase) + ").");
        // The encounter mutator spawned during warmup never started its encounter: it most likely asks the AI
        // director for one when it begins play, and the director was not active yet (Syndicate only activates
        // it when the bus leaves). Now that it is, respawn the mutator so it begins play with an active director.
        SpawnHordeMutators(true);
        if (auto E = HordeMutator.Get()) Log("Retail Row fiends: at the bus: " + Describe(E));
        TurnOnSpawners();
    }

    // The game's own spawners never showed up in 10.40, so run the horde ourselves until they do.
    // OFF by default: the first test crashed the server (exit code 3) right after the first fiend
    // spawned. A fiend made outside the game's own encounter system is not safe yet. Set
    // SYNDICATE_FIEND_SPAWN=1 (Windows environment variable) to try it anyway.
    if (Activated && Spawners == 0 && GS->GamePhase >= EAthenaGamePhase::SafeZones && SpawnTest()) Horde();

    // Diagnostics for the first 10 minutes after activation, every 30 seconds.
    float Now = Syndicate::Now();
    if (Activated && Reports < 20 && Now >= NextReport)
    {
        NextReport = Now + 30.f;
        ++Reports;
        if (Spawners == 0 && Reports > 1) TurnOnSpawners(); // the Retail Row sublevel may stream in late
        Report(GM);
    }
}
} // namespace RetailRowFiends
