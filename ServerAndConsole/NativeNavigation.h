#pragma once
#include "SyndicateRuntime.h"
#include <unordered_set>
namespace NativeNavigation
{
inline void ConfigureConfig(UFortNavSystemConfig *Config)
{
    if (!Config)
        return;
    Config->bStrictlyStatic = false;
    Config->bCreateOnClient = true;
    Config->bAutoSpawnMissingNavData = true;
    Config->bAllowAutoRebuild = true;
    Config->bRebuildOnInitialUnlock = true;
    Config->bUsesStreamedInNavLevel = false;
    if (auto Athena = Cast<UAthenaNavSystemConfig>(Config))
    {
        Athena->bDiscardNavDataFromSublevels = false;
        Athena->bUseNavigationInvokers = true;
        Athena->bUseNavOctTreeInclusionBounds = false;
        // Keep native grid/link policy. Generation is local to
        // invokers, so a solo server never rebuilds the whole island at once.
    }
}
inline void ConfigureSystem(UFortNavSystem *Nav)
{
    if (!Nav)
        return;
    Nav->bAllowClientSideNavigation = true;
    Nav->bAutoCreateNavigationData = true;
    Nav->bShouldDiscardSubLevelNavData = false;
    Nav->bSupportRebuilding = true;
    Nav->bInitialBuildingLocked = false;
    Nav->bAllowAutoRebuild = true;
    Nav->bRebuildOnInitialUnlock = true;
    Nav->bUsesStreamedInNavLevel = false;
    Nav->SupportedAgentsMask.bSupportsAgent3 = true;
    Nav->bGenerateNavigationOnlyAroundNavigationInvokers = true;
    for (auto &Agent : Nav->SupportedAgents)
        if (Agent.Name.ToString() == "Phoebe")
        {
            auto Pawn = AFortPlayerPawnAthena::GetDefaultObj();
            if (Pawn && Pawn->CapsuleComponent)
            {
                if (!std::isfinite(Agent.AgentRadius) || Agent.AgentRadius <= 0.f)
                    Agent.AgentRadius = Pawn->CapsuleComponent->CapsuleRadius;
                if (!std::isfinite(Agent.AgentHeight) || Agent.AgentHeight <= 0.f)
                    Agent.AgentHeight = Pawn->CapsuleComponent->CapsuleHalfHeight * 2.f;
                if (Agent.AgentStepHeight < 0.f && Pawn->CharacterMovement)
                    Agent.AgentStepHeight = Pawn->CharacterMovement->MaxStepHeight;
                Agent.bCanWalk = true;
            }
            if (Agent.NavigationDataClass || Agent.NavDataClass.ObjectID.AssetPathName.ToString() != "None") continue;
            // This 10.40 config has a Phoebe agent but no data class. Supply
            // the shipped Athena mesh class through the native config.
            Agent.NavigationDataClass = AAthenaNavMesh::StaticClass();
            Agent.NavDataClass.WeakPtr.ObjectIndex = -1;
            Agent.NavDataClass.WeakPtr.ObjectSerialNumber = 0;
            Agent.NavDataClass.TagAtLastTest = 0;
            Agent.NavDataClass.ObjectID.AssetPathName =
                UKismetStringLibrary::Conv_StringToName(L"/Script/FortniteGame.AthenaNavMesh");
        }
}
inline void ConfigureMesh(ARecastNavMesh *Mesh)
{
    if (!Mesh)
        return;
    Mesh->RuntimeGeneration = ERuntimeGenerationType::Dynamic;
    Mesh->MaxSimultaneousTileGenerationJobsCount = 2;
}
inline bool ExpandNavigationBounds(AActor *Volume)
{
    if (!Volume) return false;
    FVector Origin{}, Extent{};
    Volume->GetActorBounds(false, &Origin, &Extent);
    auto Scale = Volume->GetActorScale3D();
    if (!Syndicate::Finite(Origin) || !Syndicate::Finite(Extent) || !Syndicate::Finite(Scale) ||
        Extent.X < 1.f || Extent.Y < 1.f || Extent.Z < 1.f) return false;
    // The shipped bounds cover only Mega Mall. Enlarge navigation eligibility,
    // not collision or actors: tiles are still generated only near invokers.
    auto Expanded = FVector(Scale.X * std::max(1.f, 250000.f / Extent.X),
                            Scale.Y * std::max(1.f, 250000.f / Extent.Y),
                            Scale.Z * std::max(1.f, 25000.f / Extent.Z));
    if (!Syndicate::Finite(Expanded)) return false;
    if (Extent.X < 250000.f || Extent.Y < 250000.f || Extent.Z < 25000.f)
        Volume->SetActorScale3D(Expanded);
    return true;
}
inline bool CoverIsland(ANavMeshBoundsVolume *Volume)
{
    if (!ExpandNavigationBounds(Volume)) return false;
    Volume->SupportedAgents.bSupportsAgent3 = true;
    return true;
}
inline AAthenaNavMesh *CreatePhoebeMesh(UWorld *W, UFortNavSystem *Nav)
{
    if (!W || !Nav) return nullptr;
    for (auto Data : Nav->NavDataSet)
        if (Data && Data->NavDataConfig.Name.ToString() == "Phoebe") return Cast<AAthenaNavMesh>(Data);
    for (auto Data : Nav->NavDataRegistrationQueue)
        if (Data && Data->NavDataConfig.Name.ToString() == "Phoebe") return Cast<AAthenaNavMesh>(Data);
    FNavDataConfig Config{}; bool Found = false;
    for (auto Agent : Nav->SupportedAgents)
        if (Agent.Name.ToString() == "Phoebe") { Config = Agent; Found = true; break; }
    if (!Found) return nullptr;
    FTransform Transform{}; Transform.Scale3D = FVector(1,1,1);
    auto Actor = UGameplayStatics::BeginDeferredActorSpawnFromClass(W, AAthenaNavMesh::StaticClass(),
        Transform, ESpawnActorCollisionHandlingMethod::AlwaysSpawn, nullptr);
    auto Mesh = Cast<AAthenaNavMesh>(Actor);
    if (!Mesh) return nullptr;
    // Configure before native PostInitializeComponents registers the actor.
    // Changing a config alone after InitWorld does not create missing data.
    Mesh->NavDataConfig = Config;
    Mesh->AgentRadius = Config.AgentRadius;
    Mesh->AgentHeight = Config.AgentHeight;
    if (Config.AgentStepHeight >= 0.f) Mesh->AgentMaxStepHeight = Config.AgentStepHeight;
    ConfigureMesh(Mesh);
    Mesh->bCanBeMainNavData = true;
    Mesh->bCanSpawnOnRebuild = true;
    Mesh->bForceRebuildOnLoad = true;
    return Cast<AAthenaNavMesh>(UGameplayStatics::FinishSpawningActor(Mesh, Transform));
}
inline AAthenaNavInvokerBox *CreateInvoker(AFortPlayerPawnAthena *Pawn)
{
    if (!Pawn || Pawn->IsDead()) return nullptr;
    FTransform Transform{}; Transform.Scale3D = FVector(1,1,1); Transform.Translation = Pawn->K2_GetActorLocation();
    auto Box = Cast<AAthenaNavInvokerBox>(UGameplayStatics::BeginDeferredActorSpawnFromClass(UWorld::GetWorld(),
        AAthenaNavInvokerBox::StaticClass(), Transform, ESpawnActorCollisionHandlingMethod::AlwaysSpawn, Pawn));
    if (!Box) return nullptr;
    Box->GenerationRange = 4500.f;
    if (Box->InvokerComp)
    {
        Box->InvokerComp->TileGenerationRadius = 4500.f;
        Box->InvokerComp->TileRemovalRadius = 6500.f;
    }
    Box = Cast<AAthenaNavInvokerBox>(UGameplayStatics::FinishSpawningActor(Box, Transform));
    if (Box)
    {
        Box->K2_AttachToActor(Pawn, {}, EAttachmentRule::SnapToTarget, EAttachmentRule::KeepWorld,
            EAttachmentRule::KeepWorld, false);
        Box->SetInvokerEnabled(true);
    }
    return Box;
}
inline void SelectMainData(UFortNavSystem *Nav)
{
    if (!Nav || Nav->MainNavData) return;
    // Explicit projection can succeed while native path following still has
    // no default data. Only select an already registered player-sized mesh.
    for (auto Data : Nav->NavDataSet)
        if (auto Mesh = Cast<AAthenaNavMesh>(Data); Mesh &&
            !Mesh->IsActorBeingDestroyed() && Mesh->NavDataConfig.Name.ToString() == "Phoebe")
        {
            Nav->MainNavData = Mesh;
            break;
        }
}
inline void PrepareBeforeTravel()
{
    // Load serialized map settings before InitWorld creates/configures the
    // navigation system. A late boolean change cannot release its lock.
    StaticLoadObject<UWorld>("/Game/Athena/Maps/Athena_Terrain.Athena_Terrain");
    // This streamed override otherwise reapplies the shipped static/locked
    // configuration later, after the terrain's InitWorld.
    StaticLoadObject<UWorld>("/Game/Athena/Maps/Athena_Nav_Mall.Athena_Nav_Mall");
    for (int I = 0; UObject::GObjects && I < UObject::GObjects->Num(); ++I)
    {
        auto Object = UObject::GObjects->GetByIndex(I);
        if (!Object || !Syndicate::Ref<UObject>(Object).Get())
            continue;
        if (auto Config = Cast<UFortNavSystemConfig>(Object))
            ConfigureConfig(Config);
        if (auto Nav = Cast<UFortNavSystem>(Object))
            ConfigureSystem(Nav);
        if (auto Mesh = Cast<ARecastNavMesh>(Object); Mesh && Mesh->IsDefaultObject())
            ConfigureMesh(Mesh);
    }
    Log("Native Phoebe navigation configured before map initialization; local generation, two jobs maximum.");
}
inline UWorld *World = nullptr;
inline Syndicate::Ref<UFortNavSystem> System;
inline float NextCheck = 0.f;
struct Invoker {Syndicate::Ref<AFortPlayerPawnAthena> Pawn; Syndicate::Ref<AAthenaNavInvokerBox> Box;};
inline std::vector<Invoker> Registered;
inline Syndicate::Ref<AAthenaNavMesh> PhoebeMesh;
inline int MeshAttempts = 0;
inline ANavigationData *Data()
{
    auto W = UWorld::GetWorld();
    auto Nav = W ? Cast<UFortNavSystem>(W->NavigationSystem) : nullptr;
    if (!Nav) return nullptr;
    for (auto Data : Nav->NavDataSet)
        if (Data && Syndicate::Ref<ANavigationData>(Data).Get() && Data->NavDataConfig.Name.ToString() == "Phoebe")
            return Data;
    return nullptr; // Never route a player-sized bot on the default Deimos monster mesh.
}
inline bool Project(const FVector &Goal, FVector &Out)
{
    auto Phoebe = Data();
    return Phoebe && Syndicate::Finite(Goal) && UNavigationSystemV1::K2_ProjectPointToNavigation(
        UWorld::GetWorld(), Goal, &Out, Phoebe, nullptr, FVector(160,160,600));
}
inline bool RandomReachable(const FVector &Here, float Radius, FVector &Out)
{
    auto Phoebe = Data();
    return Phoebe && UNavigationSystemV1::K2_GetRandomReachablePointInRadius(
        UWorld::GetWorld(), Here, &Out, Radius, Phoebe, nullptr);
}
inline void Tick(AFortGameModeAthena *GM)
{
    auto W = UWorld::GetWorld();
    if (!W || !GM)
        return;
    auto Nav = Cast<UFortNavSystem>(W->NavigationSystem);
    if (World != W || System.Get() != Nav)
    {
        for (auto Ref : Registered)
            if (auto Box = Ref.Box.Get()) { Box->SetInvokerEnabled(false); Box->K2_DestroyActor(); }
        World = W;
        System.Set(Nav);
        Registered.clear();
        PhoebeMesh.Clear(); MeshAttempts = 0;
        NextCheck = 0.f;
        if (Nav)
        {
            ConfigureSystem(Nav);
            Nav->SetMaxSimultaneousTileGenerationJobsCount(2);
            for (auto Bound : Syndicate::ActorsOf<AAthenaNavOctTreeInclusionBounds>())
                if (auto Volume = Bound.Get()) ExpandNavigationBounds(Volume);
            for (auto Bound : Syndicate::ActorsOf<ANavMeshBoundsVolume>())
                if (auto Volume = Bound.Get())
                {
                    CoverIsland(Volume);
                    Nav->OnNavigationBoundsUpdated(Volume);
                }
        }
    }
    if (!Nav || Syndicate::Now() < NextCheck)
        return;
    NextCheck = Syndicate::Now() + 1.f;
    SelectMainData(Nav);
    if (!PhoebeMesh.Get() && MeshAttempts < 3)
    {
        ++MeshAttempts;
        PhoebeMesh.Set(CreatePhoebeMesh(W, Nav));
        if (PhoebeMesh.Get()) Log("Native Phoebe mesh constructed with agent configuration before registration.");
    }
    for (int I = static_cast<int>(Registered.size()) - 1; I >= 0; --I)
    {
        auto Pawn = Registered[I].Pawn.Get();
        if (!Pawn || !Registered[I].Box.Get() || Pawn->IsDead() || Pawn->bIsSkydiving || Pawn->bIsParachuteOpen)
        {
            if (auto Box = Registered[I].Box.Get()) { Box->SetInvokerEnabled(false); Box->K2_DestroyActor(); }
            Registered.erase(Registered.begin() + I);
        }
    }
    std::unordered_set<AFortPlayerPawnAthena *> Known;
    for (auto &Existing : Registered)
        if (auto Pawn = Existing.Pawn.Get(); Pawn && Existing.Box.Get()) Known.insert(Pawn);
    auto Register = [&](AController *Controller)
    {
        auto Pawn = Controller ? Cast<AFortPlayerPawnAthena>(Controller->Pawn) : nullptr;
        Syndicate::Ref<AFortPlayerPawnAthena> Ref(Pawn);
        if (auto Pawn = Ref.Get(); Pawn && !Pawn->IsDead() && !Pawn->bIsSkydiving && !Pawn->bIsParachuteOpen)
        {
            if (!Known.contains(Pawn))
            {
                if (auto Box = CreateInvoker(Pawn))
                {
                    Registered.push_back({Ref, Syndicate::Ref<AAthenaNavInvokerBox>(Box)});
                    Known.insert(Pawn);
                }
            }
        }
    };
    for (auto Controller : GM->AlivePlayers)
        Register(Controller);
    for (auto Controller : GM->AliveBots)
        Register(Controller);
}
} // namespace NativeNavigation
