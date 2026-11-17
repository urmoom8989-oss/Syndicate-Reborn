#pragma once
// Who placed each trap. Native traps do not always keep an owner the match server can read (trap kills on
// bots were credited to nobody), so every player trap placement is remembered here: the place, the
// player's state and team, and when. The bots look the owner up by the trap's position.
#include <vector>
#include <cmath>
#include <windows.h>
namespace SyndicateTrapOwners {
    struct Placement { float X, Y, Z; void* State; int Team; ULONGLONG At; };
    inline std::vector<Placement> Placements;
    inline void Record(float X, float Y, float Z, void* State, int Team) {
        if (Placements.size() > 512) Placements.erase(Placements.begin(), Placements.begin() + 256);
        Placements.push_back({ X, Y, Z, State, Team, GetTickCount64() });
    }
    // Closest placement within 450 units (a trap snaps to the tile it was aimed at), newest first on a tie.
    inline void* Find(float X, float Y, float Z, int Team) {
        void* Best = nullptr; float BestD = 450.f * 450.f;
        for (auto It = Placements.rbegin(); It != Placements.rend(); ++It) {
            if (Team >= 0 && It->Team != Team) continue;
            const float DX = It->X - X, DY = It->Y - Y, DZ = It->Z - Z, D = DX * DX + DY * DY + DZ * DZ;
            if (D < BestD) { BestD = D; Best = It->State; }
        }
        return Best;
    }
    inline void Clear() { Placements.clear(); }
}
