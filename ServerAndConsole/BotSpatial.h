#pragma once
#include "SyndicateRuntime.h"
#include <unordered_map>
namespace BotSpatial
{
template <class T> class Index
{
    struct Entry { Syndicate::Ref<T> Actor; FVector Position; };
    std::unordered_map<uint64_t, std::vector<Entry>> Cells;
    static constexpr float CellSize = 2000.f;
    static int Cell(float V) { return static_cast<int>(std::floor(V / CellSize)); }
    static uint64_t Key(int X, int Y)
    {
        return (static_cast<uint64_t>(static_cast<uint32_t>(X)) << 32) | static_cast<uint32_t>(Y);
    }
  public:
    void Clear() { Cells.clear(); }
    void Build(const std::vector<Syndicate::Ref<T>> &Actors)
    {
        Clear();
        for (auto &Ref : Actors)
            if (auto Actor = Ref.Get())
            {
                auto At = Actor->K2_GetActorLocation();
                if (Syndicate::Finite(At) && std::abs(At.X) < 10000000.f && std::abs(At.Y) < 10000000.f)
                    Cells[Key(Cell(At.X), Cell(At.Y))].push_back({Ref, At});
            }
    }
    std::vector<Syndicate::Ref<T>> Nearby(const FVector &Here, float Radius) const
    {
        std::vector<Syndicate::Ref<T>> Out;
        if (!Syndicate::Finite(Here) || !std::isfinite(Radius) || Radius < 0.f || Radius > 20000.f ||
            std::abs(Here.X) > 10000000.f || std::abs(Here.Y) > 10000000.f) return Out;
        float Square = Radius * Radius;
        for (int X = Cell(Here.X - Radius); X <= Cell(Here.X + Radius); ++X)
            for (int Y = Cell(Here.Y - Radius); Y <= Cell(Here.Y + Radius); ++Y)
                if (auto It = Cells.find(Key(X,Y)); It != Cells.end())
                    for (auto &E : It->second)
                    {
                        float DX = E.Position.X - Here.X, DY = E.Position.Y - Here.Y;
                        if (DX*DX + DY*DY <= Square) Out.push_back(E.Actor);
                    }
        // Validate only selected local objects at the point of use. Index
        // queries never dereference cached actors or invoke engine functions.
        return Out;
    }
};
inline Index<ABuildingSMActor> Resources;
inline Index<AFortPickup> Pickups;
inline Index<ABuildingContainer> Containers;
inline Index<AFortAthenaSupplyDrop> Supplies;
inline Index<AFortPlayerPawnAthena> Pawns;
inline void Reset() { Pickups.Clear(); Containers.Clear(); Supplies.Clear(); Pawns.Clear(); Resources.Clear(); }
} // namespace BotSpatial
