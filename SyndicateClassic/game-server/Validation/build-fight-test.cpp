// Unit tests for the bot build-fight state machine (Erbium/Private/SpectraBots/BotBuildFight.h).
// Build and run:  g++ -std=c++17 -O1 -Wall -Wextra -o build-fight-test build-fight-test.cpp && ./build-fight-test
#include "../Erbium/Erbium/Private/SpectraBots/BotBuildFight.h"
#include <cstdio>
#include <cstdlib>
#include <vector>
using namespace SyndicateBuildFight;

static int Failures = 0;
#define CHECK(c, msg) do { if (!(c)) { std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, msg); ++Failures; } } while (0)

// Run decisions for Seconds at 30 Hz, returning every non-None action.
static std::vector<Decision> Run(Memory& M, Snapshot& S, const Tuning& T, float Seconds, float Roll = .1f,
                                 void (*Apply)(Memory&, Snapshot&, const Tuning&, const Decision&) = nullptr) {
    std::vector<Decision> Out;
    for (float t = 0; t < Seconds; t += 1.f / 30) {
        auto D = Decide(M, S, T, 1.f / 30, Roll);
        if (D.Do != Action::None) Out.push_back(D);
        if (Apply) Apply(M, S, T, D);
    }
    return Out;
}
static bool Has(const std::vector<Decision>& V, Action A) { for (auto& D : V) if (D.Do == A) return true; return false; }
static int IndexOf(const std::vector<Decision>& V, Action A) { for (size_t i = 0; i < V.size(); ++i) if (V[i].Do == A) return int(i); return -1; }

// A tiny "world": building a wall puts cover between, peeking opens it, swapping equips.
static void World(Memory& M, Snapshot& S, const Tuning& T, const Decision& D) {
    switch (D.Do) {
    case Action::BlockWall: case Action::BlockRamp: case Action::ReCover:
        S.CoverBetween = true; S.EnemyHasAngle = false; S.CanPeekEdit = true; S.Peeking = false; S.Mats -= 10; Report(M, T, D.Do, true); break;
    case Action::SwapShotgun: S.Equipped = Gun::Shotgun; Report(M, T, D.Do, true); break;
    case Action::SwapRifle: S.Equipped = Gun::Rifle; Report(M, T, D.Do, true); break;
    case Action::Peek: S.CoverBetween = false; S.EnemyHasAngle = true; S.Peeking = true; S.CanPeekEdit = false; Report(M, T, D.Do, true); break;
    case Action::ContestRamp: S.HeightDelta -= 384; S.Mats -= 10; Report(M, T, D.Do, true); break;
    case Action::ContestCone: case Action::TopCover: S.Mats -= 10; Report(M, T, D.Do, true); break;
    default: break;
    }
}

int main() {
    const Tuning Hard = ForSkill(3), Medium = ForSkill(2), Easy = ForSkill(1), Off = ForSkill(0), Pro = ForSkill(4);

    { // 1. Shot blocking: open angle + under fire -> wall before anything else, no open-field fire.
        Memory M; Snapshot S; S.Distance = 1500; S.EnemyVisible = true; S.UnderFire = true; S.EnemyHasAngle = true; S.Mats = 300;
        S.HasRifle = true; S.Equipped = Gun::Rifle;
        bool FiredOpen = false;
        auto V = Run(M, S, Hard, 1.5f, .1f, [](Memory& M, Snapshot& S, const Tuning& T, const Decision& D) {
            World(M, S, T, D); });
        for (float t = 0; t < .2f; t += 1.f / 30) { auto D = Decide(M, S, Hard, 1.f / 30, .1f); (void)D; }
        CHECK(!V.empty() && V.front().Do == Action::BlockWall, "first action under open fire must be a wall");
        Memory M2; Snapshot S2 = S; S2.CoverBetween = false; S2.EnemyHasAngle = true; S2.Peeking = false;
        for (float t = 0; t < 1.f; t += 1.f / 30) { auto D = Decide(M2, S2, Hard, 1.f / 30, .1f); if (D.Do == Action::Shoot || (!D.BlockFire && M2.React <= 0 && M2.S == State::ShotBlock)) FiredOpen = true; }
        CHECK(!FiredOpen, "no open-field firing while cover can still be built");
        CHECK(M.S == State::ShotBlock, "stays in shot-block behind its cover at rifle range");
        CHECK(Has(V, Action::Peek), "fights from cover by edit-peeking");
    }
    { // 2. Reaction delay: nothing happens before the delay, the wall comes after it.
        Memory M; Snapshot S; S.Distance = 1500; S.EnemyVisible = true; S.UnderFire = true; S.EnemyHasAngle = true; S.Mats = 300;
        float First = -1;
        for (float t = 0; t < 2.f; t += 1.f / 60) { auto D = Decide(M, S, Medium, 1.f / 60, .5f); if (D.Do != Action::None && First < 0) First = t; }
        CHECK(First >= Medium.ReactionDelay * .8f, "medium bots react no faster than their reaction delay");
        CHECK(First < Medium.ReactionDelay * 1.2f + Medium.DecisionInterval + .05f, "and do react soon after");
    }
    { // 3. Build probability: a failed roll means no building for that stimulus.
        Memory M; Snapshot S; S.Distance = 1500; S.EnemyVisible = true; S.UnderFire = true; S.EnemyHasAngle = true; S.Mats = 300;
        auto V = Run(M, S, Easy, 2.f, .9f); // .9 > Easy.BuildChance (.45)
        CHECK(!Has(V, Action::BlockWall), "easy bot that failed the build roll does not wall");
    }
    { // 4. Height contest: opponent ramps up above us -> ramps, cone denial, exit once height is won.
        Memory M; Snapshot S; S.Distance = 700; S.HeightDelta = 420; S.EnemyRise = 400; S.EnemyOnBuild = true; S.EnemyVisible = true; S.Mats = 400;
        auto V = Run(M, S, Hard, 4.f, .1f, World);
        CHECK(Has(V, Action::ContestRamp), "climbs with ramps when out-built");
        CHECK(Has(V, Action::ContestCone), "cones above the opponent to deny their climb");
        CHECK(S.HeightDelta < 0, "ends above the opponent");
        CHECK(M.S != State::HeightContest, "leaves the height contest once height is won");
    }
    { // 5. Height capped: no further climbing.
        Memory M; Snapshot S; S.Distance = 700; S.HeightDelta = 600; S.EnemyRise = 400; S.EnemyOnBuild = true; S.EnemyVisible = true; S.Mats = 400; S.HeightCapped = true;
        auto V = Run(M, S, Hard, 2.f);
        CHECK(!Has(V, Action::ContestRamp), "respects the ramp height cap");
    }
    { // 6. Close-quarters strict loop: Build -> Swap -> Peek -> Shoot -> ReCover, then repeats.
        Memory M; Snapshot S; S.Distance = 500; S.EnemyVisible = true; S.EnemyHasAngle = true; S.Mats = 400;
        S.HasShotgun = true; S.HasRifle = true; S.Equipped = Gun::Rifle;
        auto V = Run(M, S, Hard, 4.f, .1f, World);
        int b = IndexOf(V, Action::BlockWall), w = IndexOf(V, Action::SwapShotgun), p = IndexOf(V, Action::Peek), s = IndexOf(V, Action::Shoot), r = IndexOf(V, Action::ReCover);
        CHECK(b >= 0 && w > b && p > w && s > p && r > s, "box loop runs in order build, swap, peek, shoot, re-cover");
        int Peeks = 0; for (auto& D : V) Peeks += D.Do == Action::Peek;
        CHECK(Peeks >= 2, "loop repeats (peek again after re-cover)");
        CHECK(S.Equipped == Gun::Shotgun, "shotgun in hand for the box fight");
        bool ShotWhileClosed = false; for (auto& D : V) if (D.Do == Action::Shoot && D.BlockFire) ShotWhileClosed = true;
        CHECK(!ShotWhileClosed, "shoot steps never block fire");
    }
    { // 7. Weapon by distance: rifle outside box range, no swap spam.
        Memory M; Snapshot S; S.Distance = 1800; S.EnemyVisible = true; S.CoverBetween = true; S.Mats = 0; S.HasShotgun = true; S.HasRifle = true; S.Equipped = Gun::Shotgun;
        auto V = Run(M, S, Hard, 2.f, .1f, World);
        int Swaps = 0; for (auto& D : V) Swaps += D.Do == Action::SwapRifle;
        CHECK(Swaps == 1 && S.Equipped == Gun::Rifle, "swaps to rifle once at mid range");
    }
    { // 8. Breaking through an enemy wall: instant shotgun.
        Memory M; Snapshot S; S.Distance = 900; S.EnemyVisible = false; S.EnemyWallBetween = true; S.Mats = 200; S.HasShotgun = true; S.HasRifle = true; S.Equipped = Gun::Rifle;
        Run(M, S, Hard, .5f);
        S.EnemyWallBetween = false; S.EnemyHasAngle = true; S.EnemyVisible = true;
        Decision D{}; for (int i = 0; i < 10 && D.Do == Action::None; ++i) D = Decide(M, S, Hard, 1.f / 30, .1f);
        CHECK(D.Do == Action::SwapShotgun, "broke through their wall: shotgun first");
    }
    { // 9. No mats: never stuck, keeps shooting in a box fight.
        Memory M; Snapshot S; S.Distance = 400; S.EnemyVisible = true; S.EnemyHasAngle = true; S.Mats = 0; S.HasShotgun = true; S.Equipped = Gun::Shotgun;
        auto V = Run(M, S, Hard, 2.f, .1f, World);
        CHECK(Has(V, Action::Shoot) && !Has(V, Action::BlockWall), "without materials it fights instead of freezing");
    }
    { // 10. Disabled tier and far targets do nothing.
        Memory M; Snapshot S; S.Distance = 400; S.UnderFire = true; S.EnemyHasAngle = true; S.EnemyVisible = true; S.Mats = 300;
        CHECK(Run(M, S, Off, 2.f).empty(), "bot tier: module off");
        Memory M2; Snapshot S2; S2.Distance = 5000; S2.UnderFire = true; S2.EnemyHasAngle = true; S2.Mats = 300;
        CHECK(Run(M2, S2, Hard, 2.f).empty(), "outside engage range: normal combat code handles it");
    }
    { // 11. Decision pacing: at most one new action per decision interval.
        Memory M; Snapshot S; S.Distance = 500; S.EnemyHasAngle = true; S.EnemyVisible = true; S.Mats = 400; S.HasShotgun = true;
        auto V = Run(M, S, Hard, 3.f, .1f, World);
        CHECK(V.size() <= size_t(3.f / Hard.DecisionInterval) + 2, "decisions are throttled");
    }
    { // 12. Review bug 1: shot-block edit peek must shoot through the open edit (not try to wall over it).
        Memory M; Snapshot S; S.Distance = 1800; S.EnemyVisible = true; S.UnderFire = false; S.EnemyHasAngle = true; S.Mats = 300; S.HasRifle = true; S.Equipped = Gun::Rifle;
        Run(M, S, Hard, 1.5f, .1f, World); // walls up, then peeks
        S.UnderFire = false;
        int Shots = 0, WallsWhilePeeking = 0;
        for (float t = 0; t < 1.f; t += 1.f / 30) { auto D = Decide(M, S, Hard, 1.f / 30, .1f);
            if (S.Peeking && D.Do == Action::Shoot) ++Shots; if (S.Peeking && D.Do == Action::BlockWall) ++WallsWhilePeeking; World(M, S, Hard, D); }
        CHECK(Shots > 0 && WallsWhilePeeking == 0, "shoots through its own peek in shot-block");
    }
    { // 13. Review bug 2: failed placements never freeze the bot.
        Memory M; Snapshot S; S.Distance = 400; S.HeightDelta = 600; S.EnemyRise = 0; S.EnemyOnBuild = true; S.EnemyVisible = true; S.Mats = 400;
        int Blocked = 0, Total = 0;
        for (float t = 0; t < 8.f; t += 1.f / 30) { auto D = Decide(M, S, Hard, 1.f / 30, .1f); ++Total; Blocked += D.BlockFire;
            if (D.Do != Action::None && D.Do != Action::Shoot && D.Do != Action::SwapRifle && D.Do != Action::SwapShotgun) Report(M, Hard, D.Do, false); }
        CHECK(Blocked < Total / 3, "every placement failing: bot stops holding fire");
        CHECK(M.S != State::HeightContest, "and leaves the height contest");
    }
    { // 14. Review bug 2: someone standing on a hill (not building) does not cause an endless contest.
        Memory M; Snapshot S; S.Distance = 1000; S.HeightDelta = 500; S.EnemyOnBuild = false; S.EnemyVisible = true; S.Mats = 400;
        Run(M, S, Hard, 10.f, .1f, [](Memory& M, Snapshot&, const Tuning& T, const Decision& D) { if (D.Do != Action::None) Report(M, T, D.Do, true); });
        CHECK(M.S != State::HeightContest, "height contest ends against a non-building opponent");
    }
    { // 15. Review bug 6: cover we cannot edit does not hold fire forever.
        Memory M; Snapshot S; S.Distance = 1500; S.EnemyVisible = true; S.UnderFire = true; S.EnemyHasAngle = true; S.Mats = 300;
        Run(M, S, Hard, 1.f, .1f, [](Memory& M, Snapshot& S, const Tuning& T, const Decision& D) {
            if (D.Do == Action::BlockWall) { S.CoverBetween = true; S.EnemyHasAngle = false; S.CanPeekEdit = false; S.UnderFire = false; Report(M, T, D.Do, true); } });
        Run(M, S, Hard, 3.f);
        CHECK(M.S == State::Neutral, "uneditable cover: back to normal combat");
    }
    { // 16. Review bug 4: box fight without a shotgun leaves weapon choice to the normal picker.
        Memory M; Snapshot S; S.Distance = 400; S.EnemyVisible = true; S.EnemyHasAngle = true; S.Mats = 0; S.HasShotgun = false; S.HasRifle = true; S.Equipped = Gun::Sniper;
        bool Owned = false; for (float t = 0; t < 1.f; t += 1.f / 30) Owned |= Decide(M, S, Hard, 1.f / 30, .1f).OwnsWeapon;
        CHECK(!Owned, "no shotgun: module does not lock the weapon");
    }
    { // 17. Review bug 5: a failed peek still gets a full shoot window.
        Memory M; M.S = State::BoxFight; M.Loop = Step::Peek; M.ShootTime = 5.f;
        Report(M, Hard, Action::Peek, false);
        CHECK(M.Loop == Step::Shoot && M.ShootTime == 0, "shoot window resets after a failed peek");
    }
    { // 18. Ramp rush with no mats to contest: shoot their ramp out (all build tiers), then a cooldown.
        for (const Tuning* T : {&Easy, &Medium, &Hard}) {
            Memory M; Snapshot S; S.Distance = 900; S.HeightDelta = 250; S.EnemyRise = 400; S.EnemyOnBuild = true; S.EnemyVisible = true;
            S.Mats = 0; S.Equipped = Gun::Rifle; S.HasRifle = true;
            auto V = Run(M, S, *T, 2.f, .1f, [](Memory& M, Snapshot&, const Tuning& T, const Decision& D) { if (D.Do == Action::BreakRamp) Report(M, T, D.Do, true); });
            CHECK(Has(V, Action::BreakRamp), "pushed without mats: shoots the ramp out");
            int Breaks = 0; for (auto& D : V) Breaks += D.Do == Action::BreakRamp;
            CHECK(Breaks == 1, "ramp break has a cooldown (once per 2.5 s)");
            for (auto& D : V) if (D.Do == Action::BreakRamp) CHECK(!D.BlockFire, "breaking a ramp is a fire window");
        }
    }
    { // 19. Ramp rush with mats: Hard contests by building, and sometimes (low roll) shoots the ramp instead.
        Memory M; Snapshot S; S.Distance = 900; S.HeightDelta = 250; S.EnemyRise = 400; S.EnemyOnBuild = true; S.EnemyVisible = true;
        S.Mats = 400; S.Equipped = Gun::Rifle; S.HasRifle = true;
        auto Build = Run(M, S, Hard, 2.f, .9f, World);
        CHECK(Has(Build, Action::ContestRamp) || Has(Build, Action::ContestCone), "high roll: contests the height by building");
        CHECK(!Has(Build, Action::BreakRamp), "high roll: no ramp break");
        Memory M2; Snapshot S2 = S;
        auto Shoot = Run(M2, S2, Hard, 2.f, .1f, World);
        CHECK(Has(Shoot, Action::BreakRamp) || Has(Shoot, Action::ContestCone), "low roll: shoots the ramp out (or cones it)");
    }
    { // 20. A ramp rush is never answered with a box loop at range.
        Memory M; Snapshot S; S.Distance = 1200; S.HeightDelta = 200; S.EnemyRise = 400; S.EnemyOnBuild = true; S.EnemyVisible = true;
        S.Mats = 400; S.Equipped = Gun::Rifle;
        Run(M, S, Medium, 2.f, .9f, World);
        CHECK(M.S != State::BoxFight, "rush at range: no box fight");
    }
    { // 21. A ramp rush flickers "rising" (ramp steps, pauses to place the next ramp): the bot still
      //     finishes reacting and contests with ramps (before: endless "reacting", no ramps).
        for (const Tuning* T : {&Medium, &Hard}) {
            Memory M; Snapshot S; S.Distance = 1200; S.HeightDelta = 150; S.EnemyOnBuild = true; S.EnemyVisible = true;
            S.Mats = 400; S.Equipped = Gun::Rifle; S.HasRifle = true;
            int Ramps = 0, Frame = 0;
            for (float t = 0; t < 4.f; t += 1.f / 30, ++Frame) {
                S.EnemyRise = (Frame / 6) % 2 ? 0.f : 400.f; // climbing 0.2 s, pausing 0.2 s
                auto D = Decide(M, S, *T, 1.f / 30, .6f);
                if (D.Do == Action::ContestRamp) { ++Ramps; Report(M, *T, D.Do, true); }
                else if (D.Do != Action::None && D.Do != Action::Shoot) Report(M, *T, D.Do, true);
            }
            CHECK(Ramps >= 2, "flickering climb: bot contests with ramps");
        }
    }
    { // 22. Pro tier is a real step above Hard: sharper DECISIONS, not stats.
        CHECK(Pro.ReactionDelay < Hard.ReactionDelay, "pro reacts sooner than hard");
        CHECK(Pro.DecisionInterval < Hard.DecisionInterval, "pro decides faster than hard");
        CHECK(Pro.BuildInterval <= Hard.BuildInterval, "pro places pieces at least as fast as hard");
        CHECK(Pro.BuildChance >= Hard.BuildChance, "pro always answers a stimulus");
        CHECK(Pro.ContestTrigger < Hard.ContestTrigger, "pro contests height sooner");
        CHECK(Pro.ContestRange > Hard.ContestRange, "pro contests height from farther");
        CHECK(Pro.BreakRampChance > Hard.BreakRampChance, "pro takes the opponent's ramp out more readily");
        CHECK(Pro.PeekWindow < Hard.PeekWindow, "pro peeks tighter (less exposed)");
        CHECK(Pro.ReCoverDelay < Hard.ReCoverDelay, "pro re-covers faster");
    }
    { // 23. Open-angle fire: pro puts its first protective wall up sooner than hard (faster reaction).
        auto FirstWallTick = [](const Tuning& T) {
            Memory M; Snapshot S; S.Distance = 1500; S.EnemyVisible = true; S.UnderFire = true; S.EnemyHasAngle = true;
            S.Mats = 300; S.HasRifle = true; S.Equipped = Gun::Rifle;
            int tick = 0; for (float t = 0; t < 2.f; t += 1.f / 30, ++tick) {
                auto D = Decide(M, S, T, 1.f / 30, .1f);
                if (D.Do == Action::BlockWall) return tick;
                World(M, S, T, D);
            }
            return 9999;
        };
        int pro = FirstWallTick(Pro), hard = FirstWallTick(Hard);
        CHECK(pro < 9999, "pro does wall up under open fire");
        CHECK(pro < hard, "pro walls up faster than hard under open fire");
    }
    { // 24. Opponent 120u up at 2100u: pro's sharper height awareness contests it (hard's trigger/range ignore it).
        Memory M; Snapshot S; S.Distance = 2100; S.HeightDelta = 120; S.EnemyRise = 300; S.EnemyOnBuild = true;
        S.EnemyVisible = true; S.Mats = 400; S.HasRifle = true; S.Equipped = Gun::Rifle;
        auto V = Run(M, S, Pro, 2.f, .9f, World);
        CHECK(Has(V, Action::ContestRamp) || Has(V, Action::ContestCone) || Has(V, Action::BlockRamp),
              "pro contests a 120u height gap at 2100u");
    }
    std::printf(Failures ? "%d FAILED\n" : "all build-fight tests passed\n", Failures);
    return Failures ? 1 : 0;
}
