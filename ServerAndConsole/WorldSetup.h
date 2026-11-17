#pragma once
#include "Looting.h"
#include "Vehicles.h"
#include "BotWork.h"
// Map population is game-thread work, but it must never run inside the loading RPC.
namespace WorldSetup
{
inline Syndicate::Ref<UWorld> ActiveWorld;
inline std::vector<Syndicate::Ref<ABuildingContainer>> Floors;
inline std::vector<Syndicate::Ref<ABuildingContainer>> DeferredFloors;
inline bool RetriedFloors = false;
inline std::vector<Syndicate::Ref<AFortAthenaVehicleSpawner>> Spawners;
inline size_t FloorIndex = 0, VehicleIndex = 0;
inline int Pickups = 0, VehiclesSpawned = 0;
inline int Stage = 0;
inline bool Pending = false;
inline void Request(UWorld *W)
{
    if (!W || ActiveWorld.Get() == W)
        return;
    ActiveWorld.Set(W);
    Floors.clear();
    DeferredFloors.clear(); RetriedFloors = false;
    Spawners.clear();
    FloorIndex = VehicleIndex = 0;
    Pickups = VehiclesSpawned = Stage = 0;
    Pending = true;
    Log("Map population queued after native player startup.");
}
inline void Tick(UWorld *W)
{
    if (!Pending)
        return;
    if (!W || ActiveWorld.Get() != W)
    {
        Pending = false;
        Floors.clear();
        DeferredFloors.clear();
        Spawners.clear();
        return;
    }
    if (Stage == 0)
    {
        BotWork::Scope Measure(BotWork::MapSetup);
        Floors = Syndicate::ActorsOf<ABuildingContainer>();
        std::erase_if(Floors, [](const auto &Ref) {
            auto Actor = Ref.Get();
            return !Actor || !Actor->GetName().contains("FloorLoot") || Actor->bAlreadySearched;
        });
        auto Starts = Syndicate::ActorsOf<AFortPlayerStartWarmup>();
        auto Start = Starts.empty() ? nullptr : Starts.front().Get();
        if (Start)
        {
            auto Anchor = Start->K2_GetActorLocation();
            std::vector<std::pair<float,Syndicate::Ref<ABuildingContainer>>> Ranked;
            Ranked.reserve(Floors.size());
            for (auto Ref : Floors)
                if (auto Actor = Ref.Get()) Ranked.push_back({Syndicate::Distance2D(Actor->K2_GetActorLocation(),Anchor),Ref});
            std::stable_sort(Ranked.begin(), Ranked.end(), [](const auto &A,const auto &B) { return A.first < B.first; });
            Floors.clear();
            for (auto &Entry : Ranked) Floors.push_back(Entry.second);
            Log("Warmup floor-loot markers queued before the rest of the island.");
        }
        Stage = 1;
        return;
    }
    if (Stage == 1)
    {
        Spawners = Syndicate::ActorsOf<AFortAthenaVehicleSpawner>();
        Stage = 2;
        return;
    }
    UDataTable *Tier = nullptr, *Package = nullptr;
    BotWork::Scope Measure(BotWork::MapSetup);
    bool FloorReady = FloorIndex >= Floors.size() || Looting::Tables(Tier, Package);
    auto Started = std::chrono::steady_clock::now();
    // A single native spawn/load can exceed this cooperative budget. Never repeat
    // all map work in one frame; cap both jobs and elapsed time between jobs.
    for (int Work = 0; Work < 4; ++Work)
    {
        if (FloorReady && FloorIndex < Floors.size())
        {
            auto Ref = Floors[FloorIndex++];
            auto Floor = Ref.Get();
            int Spawned = Looting::SpawnFloorLootAt(Floor);
            Pickups += Spawned;
            // Retry failed markers once after the initial sweep. Occupied
            // spawn points never fall back to pickups inside a character.
            if (!Spawned && Floor && !Floor->bAlreadySearched && !RetriedFloors)
                DeferredFloors.push_back(Ref);
        }
        else if (!RetriedFloors && !DeferredFloors.empty())
        {
            Floors.swap(DeferredFloors); DeferredFloors.clear();
            FloorIndex = 0; RetriedFloors = true;
        }
        else if (VehicleIndex < Spawners.size())
            VehiclesSpawned += Vehicles::SpawnVehicleAt(Spawners[VehicleIndex++].Get()) ? 1 : 0;
        else if (FloorIndex < Floors.size())
            break; // Missing tables delay only floor loot; vehicles proceed normally.
        else
        {
            Pending = false;
            Floors.clear();
            Spawners.clear();
            Log("Map population completed: pickups=" + std::to_string(Pickups) +
                ", vehicles=" + std::to_string(VehiclesSpawned));
            break;
        }
        if (std::chrono::steady_clock::now() - Started >= std::chrono::milliseconds(2))
            break;
    }
}
} // namespace WorldSetup
