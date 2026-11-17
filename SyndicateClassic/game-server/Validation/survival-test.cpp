// Unit tests for the bot survival module (Erbium/Private/SpectraBots/BotSurvival.h).
// Build and run:  g++ -std=c++17 -O1 -Wall -Wextra -o survival-test survival-test.cpp && ./survival-test
#include "../Erbium/Erbium/Private/SpectraBots/BotSurvival.h"
#include <cstdio>
using namespace SyndicateSurvival;

static int Failures = 0;
#define CHECK(c, msg) do { if (!(c)) { std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, msg); ++Failures; } } while (0)

static Snapshot Fight() { Snapshot S; S.Health = 100; S.Shield = 0; S.HasEnemy = true; S.EnemyVisible = true; S.EnemyDistance = 1500; S.Mats = 300; S.EnemyVitals = 150; return S; }
template <class F> static void Run(Memory& M, Snapshot& S, const Tuning& T, float Seconds, F&& Each) {
    for (float t = 0; t < Seconds; t += 1.f / 30) { auto D = Decide(M, S, T, 1.f / 30); Each(D); }
}

int main() {
    const Tuning Hard = ForSkill(3), Medium = ForSkill(2), Easy = ForSkill(1), Off = ForSkill(0);

    { // 1. Priority: healthy and engaged -> build fight; burst damage unseen -> box up; nothing -> patrol.
        Memory M; Snapshot S = Fight();
        CHECK(Arbitrate(M, S, Medium) == Mode::BuildFight, "engaged and healthy: build fight");
        Snapshot B; B.RecentDamage = 50; CHECK(Arbitrate(M, B, Medium) == Mode::BoxUp, "sudden burst from unseen shooter: box up");
        Snapshot P; CHECK(Arbitrate(M, P, Medium) == Mode::Patrol, "nothing happening: patrol");
    }
    { // 2. Critical health interrupts the fight immediately (same tick).
        Memory M; Snapshot S = Fight(); S.Health = 22; S.Shield = 0;
        auto D = Decide(M, S, Medium, 1.f / 30);
        CHECK(D.M == Mode::FleeOrHide && D.SuppressCombat, "health < 30: flee-or-hide on the same tick, combat suppressed");
        CHECK(D.Do == Action::FallbackWall, "outdoors and exposed: wall before turning away");
    }
    { // 3. Shield buffer: low health but solid shield is not critical.
        Memory M; Snapshot S = Fight(); S.Health = 20; S.Shield = 75;
        CHECK(Arbitrate(M, S, Medium) == Mode::BuildFight, "20 hp + 75 shield keeps fighting");
    }
    { // 4. Cornered / point-blank / weaker opponent: fight instead of turning around.
        Memory M; Snapshot S = Fight(); S.Health = 20; S.EnemyDistance = 300;
        CHECK(Arbitrate(M, S, Medium) == Mode::BuildFight, "visible opponent point blank: fight");
        Snapshot C = Fight(); C.Health = 20; C.Cornered = true;
        CHECK(Arbitrate(M, C, Medium) == Mode::BuildFight, "no escape route: fight");
        Snapshot W = Fight(); W.Health = 25; W.EnemyVitals = 5;
        CHECK(Arbitrate(M, W, Medium) == Mode::BuildFight, "opponent even weaker: finish them");
    }
    { // 5. Full flee sequence outdoors: wall, move (double walls), hide, heal, recover, re-engage.
        Memory M; Snapshot S = Fight(); S.Health = 20; S.Shield = 0; S.UnderFire = true; S.HasHealItem = true; S.HasSpot = true;
        int Fallback = 0, Retreat = 0, Move = 0, Hide = 0, Heal = 0; float Moved = 0;
        Run(M, S, Hard, 3.f, [&](const Decision& D) {
            Fallback += D.Do == Action::FallbackWall; Retreat += D.Do == Action::RetreatWall; Move += D.Do == Action::MoveToSpot;
            if (D.Do == Action::MoveToSpot) { Moved += 20; M.LastWallDistance += 20; } });
        CHECK(Fallback == 1, "one fallback wall first");
        CHECK(Retreat >= 1, "double walls while retreating under fire");
        CHECK(Move > 0, "moves to the retreat spot");
        // Reach a hidden spot: shots stop, the bot hides then heals.
        S.AtSpot = true; S.SpotHidden = true; S.EnemyVisible = false; S.UnderFire = false;
        Run(M, S, Hard, 2.f, [&](const Decision& D) { Hide += D.Do == Action::Hide; Heal += D.Do == Action::Heal; });
        CHECK(Heal > 0, "heals while hidden");
        bool Crouched = false; { auto D = Decide(M, S, Hard, 1.f / 30); Crouched = D.Crouch; } CHECK(Crouched, "crouches while hidden/healing");
        // Healed up: back to normal.
        S.Healing = false; S.Health = 75; S.Shield = 50; S.HasHealItem = false;
        auto D = Decide(M, S, Hard, 1.f / 30);
        CHECK(D.M != Mode::FleeOrHide && !D.SuppressCombat, "recovered: leaves flee-or-hide");
    }
    { // 6. Indoors: no building, prefers inside spots, hides.
        Memory M; Snapshot S = Fight(); S.Health = 15; S.Indoors = true; S.UnderFire = true; S.HasSpot = true;
        bool Built = false; Run(M, S, Medium, 2.f, [&](const Decision& D) { Built |= D.Do == Action::FallbackWall || D.Do == Action::RetreatWall; });
        CHECK(!Built, "indoors it uses rooms and walls, it does not build");
        std::vector<Candidate> C(3);
        C[0] = {300, 1500, .9f, true, true, true, false};   // hidden outdoors
        C[1] = {350, 1500, .8f, true, true, true, true};    // hidden inside
        C[2] = {200, 900, -.8f, true, true, true, true};    // toward the opponent
        CHECK(Pick(C, true) == 1, "inside a house: picks the inside hidden spot");
        CHECK(Pick(C, false) == 0 || Pick(C, false) == 1, "never the spot toward the opponent");
    }
    { // 7. Spot scoring: hidden beats exposed, reachable only, not toward the shooter.
        std::vector<Candidate> C(3);
        C[0] = {800, 2500, 1.f, true, false, false, false};
        C[1] = {1200, 2200, .7f, true, true, true, false};
        C[2] = {500, 2600, 1.f, false, true, true, false};
        CHECK(Pick(C, false) == 1, "hidden behind natural cover wins; unreachable ignored");
        std::vector<Candidate> None(1); None[0] = {300, 300, -1.f, true, true, true, false};
        CHECK(Pick(None, false) == -1, "no acceptable spot: -1 (caller treats as cornered)");
    }
    { // 8. Leaves after hiding with nothing to heal, and after the time cap.
        Memory M; Snapshot S = Fight(); S.Health = 20; S.UnderFire = true; S.HasSpot = true;
        Decide(M, S, Medium, 1.f / 30);
        S.UnderFire = false; S.EnemyVisible = false; S.AtSpot = true; S.SpotHidden = true; S.HasHealItem = false;
        Mode Last = Mode::FleeOrHide; Run(M, S, Medium, Medium.HideHold + 1.f, [&](const Decision& D) { Last = D.M; });
        CHECK(Last != Mode::FleeOrHide, "unseen and nothing to heal with: re-engages");
        Memory T2; Snapshot X = Fight(); X.Health = 20; X.EnemyVisible = false; X.UnderFire = true;
        Run(T2, X, Medium, Medium.MaxFleeTime + 1.f, [](const Decision&) {});
        CHECK(T2.M != Mode::FleeOrHide, "flee time is capped");
    }
    { // 9. Storm and disabled tier: no fleeing into the storm, "bot" skill unchanged.
        Memory M; Snapshot S = Fight(); S.Health = 10; S.InStorm = true;
        CHECK(Arbitrate(M, S, Medium) != Mode::FleeOrHide, "in the storm the rotation wins");
        Memory M2; Snapshot S2 = Fight(); S2.Health = 10;
        CHECK(Arbitrate(M2, S2, Off) != Mode::FleeOrHide, "bot tier keeps old behaviour");
    }
    { // 10. Fairness clamps.
        Tuning T = Medium; T.RetreatSpeedMultiplier = 3.f; T.BuildAggressionRate = 9.f; Clamp(T);
        CHECK(T.RetreatSpeedMultiplier <= 1.f, "retreat speed can never exceed normal sprint");
        CHECK(T.BuildAggressionRate <= 2.f, "build aggression is bounded");
        Memory M; Snapshot S = Fight(); S.Health = 10; auto D = Decide(M, S, T, 1.f / 30);
        CHECK(D.SpeedFraction <= 1.f, "speed fraction <= 1");
    }
    { // 11. Easy bots flee later and never build while fleeing.
        Memory M; Snapshot S = Fight(); S.Health = 28; S.UnderFire = true;
        CHECK(Arbitrate(M, S, Easy) != Mode::FleeOrHide, "easy threshold is 25");
        S.Health = 20; S.HasSpot = true; bool Built = false;
        Run(M, S, Easy, 2.f, [&](const Decision& D) { Built |= D.Do == Action::FallbackWall || D.Do == Action::RetreatWall; });
        CHECK(!Built, "easy bots do not build while fleeing");
    }
    { // 12. No endless re-entry: after leaving (nothing to heal), unseen and not shot -> stays out.
        Memory M; Snapshot S = Fight(); S.Health = 20; S.UnderFire = true; S.HasSpot = true;
        Decide(M, S, Medium, 1.f / 30);
        CHECK(M.M == Mode::FleeOrHide, "entered");
        S.UnderFire = false; S.EnemyVisible = false; S.AtSpot = true; S.SpotHidden = true; S.RecentDamage = 1.f; // small residual damage
        int Entries = 0; Mode Prev = M.M;
        for (float t = 0; t < 30.f; t += 1.f / 30) { auto D = Decide(M, S, Medium, 1.f / 30); if (D.M == Mode::FleeOrHide && Prev != Mode::FleeOrHide) ++Entries; Prev = D.M; }
        CHECK(Entries == 0 && M.M != Mode::FleeOrHide, "no re-entry while unseen and not under fire");
        S.UnderFire = true; Decide(M, S, Medium, 1.f / 30);
        CHECK(M.M == Mode::FleeOrHide, "being shot again re-triggers it");
    }
    { // 13. A remembered but unseen far opponent alone is not a reason to flee.
        Memory M; Snapshot S = Fight(); S.Health = 20; S.EnemyVisible = false; S.RecentDamage = 0;
        CHECK(Arbitrate(M, S, Medium) != Mode::FleeOrHide, "no live threat: no flee");
    }
    { // 14. Hidden at the spot: stays there, no periodic replanning.
        Memory M; Snapshot S = Fight(); S.Health = 20; S.UnderFire = true; S.HasSpot = true;
        Decide(M, S, Medium, 1.f / 30);
        S.UnderFire = false; S.EnemyVisible = false; S.AtSpot = true; S.SpotHidden = true; S.HasHealItem = false;
        int Replans = 0; for (float t = 0; t < 3.f; t += 1.f / 30) Replans += Decide(M, S, Medium, 1.f / 30).Replan;
        CHECK(Replans == 0, "no replanning while hidden at the spot");
    }
    { // 15. No spot found: replan is throttled, not every tick.
        Memory M; Snapshot S = Fight(); S.Health = 20; S.UnderFire = true; S.HasSpot = false;
        int Replans = 0; for (float t = 0; t < 3.f; t += 1.f / 30) Replans += Decide(M, S, Medium, 1.f / 30).Replan;
        CHECK(Replans >= 3 && Replans <= 6, "no-spot replans about every 0.75 s");
    }
    { // 16. Rotation: fleeing bot follows the zone (no hiding outside the circle), fire stays off.
        Memory M; Snapshot S = Fight(); S.Health = 20; S.UnderFire = true; S.HasSpot = true; S.Rotating = true;
        auto D = Decide(M, S, Medium, 1.f / 30);
        CHECK(D.M == Mode::FleeOrHide && D.SuppressCombat && D.Do == Action::None && !D.Replan, "rotating: no spot search, rotation movement runs");
    }
    { // 17. Cornered (caller-provided) means fight; once out of flee the caller clears it (glue rule).
        Memory M; Snapshot S = Fight(); S.Health = 20; S.UnderFire = true; S.HasSpot = true;
        Decide(M, S, Medium, 1.f / 30);
        S.Cornered = true; auto D = Decide(M, S, Medium, 1.f / 30);
        CHECK(D.M == Mode::BuildFight, "cornered while fleeing: fight");
        CHECK(M.Cooldown > 0, "and a cooldown stops instant flip-flopping");
    }
    std::printf(Failures ? "%d FAILED\n" : "all survival tests passed\n", Failures);
    return Failures ? 1 : 0;
}
