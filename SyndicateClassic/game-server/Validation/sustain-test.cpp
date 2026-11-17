// Unit tests for bot consumables, quick heals and farming (Erbium/Private/SpectraBots/BotSustain.h).
// Build and run:  g++ -std=c++17 -O1 -Wall -Wextra -o sustain-test sustain-test.cpp && ./sustain-test
#include "../Erbium/Erbium/Private/SpectraBots/BotSustain.h"
#include <cstdio>
using namespace SyndicateSustain;

static int Failures = 0;
#define CHECK(c, msg) do { if (!(c)) { std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, msg); ++Failures; } } while (0)

static Counts Kit() { Counts C; C.Mini = 3; C.Big = 1; C.Bandage = 5; C.Medkit = 1; return C; }
static Situation Quiet() { Situation S; return S; }

int main() {
    const Tuning T = ForSkill(2);

    { // Item table: real use times and effects.
        CHECK(Info(Item::MiniShield).UseTime == 2.f && Info(Item::BigShield).UseTime == 5.f, "minis 2 s, bigs 5 s");
        CHECK(Info(Item::Bandage).UseTime == 3.5f && Info(Item::Medkit).UseTime == 10.f && Info(Item::ChugJug).UseTime == 15.f, "bandage 3.5, medkit 10, chug 15");
        float H = 70, S = 40; Apply(Item::MiniShield, H, S); CHECK(S == 50, "mini caps at 50 shield");
        H = 70; S = 0; Apply(Item::Bandage, H, S); CHECK(H == 75, "bandage caps at 75");
        H = 30; S = 0; Apply(Item::ChugJug, H, S); CHECK(H == 100 && S == 100, "chug fills both");
        CHECK(FromName("Athena_ShieldSmall") == Item::MiniShield && FromName("Athena_PurpleStuff") == Item::ChugJug && FromName("x") == Item::None, "names");
        CHECK(!Useful(Item::MiniShield, 100, 50) && Useful(Item::BigShield, 100, 50), "minis useless above 50 shield");
    }
    { // Choice rules.
        CHECK(Choose(100, 0, Kit(), 99, T) == Item::MiniShield, "full health, no shield: minis first");
        CHECK(Choose(100, 50, Kit(), 99, T) == Item::BigShield, "50 shield: big shield");
        CHECK(Choose(30, 50, Kit(), 99, T) == Item::Medkit, "critical health with time: medkit");
        CHECK(Choose(30, 50, Kit(), 3.5f, T) == Item::Bandage, "critical health, short window: bandage");
        CHECK(Choose(65, 100, Kit(), 99, T) == Item::Bandage, "mid health: bandage up to 75");
        CHECK(Choose(80, 100, Kit(), 99, T) == Item::Medkit, "health 80, shields full: medkit for the rest");
        Counts Chug; Chug.Chug = 1; CHECK(Choose(40, 0, Chug, 99, T) == Item::ChugJug, "both low, long window: chug");
        CHECK(Choose(40, 0, Chug, 5.f, T) == Item::None, "no time for a chug");
        CHECK(Choose(100, 100, Kit(), 99, T) == Item::None, "full: nothing");
    }
    { // Safety window.
        Situation S = Quiet(); CHECK(SafeUseTime(S, T) > 15.f, "nobody around: any item");
        S.EnemyVisible = true; CHECK(SafeUseTime(S, T) == 0, "opponent in view: nothing");
        S = Quiet(); S.UnderFire = true; CHECK(SafeUseTime(S, T) == 0, "under fire: nothing");
        S = Quiet(); S.InStorm = true; CHECK(SafeUseTime(S, T) == 0, "storm: nothing");
        S = Quiet(); S.EnemyKnown = true; S.EnemyDistance = 4000; S.EnemyClosing = 600; S.SincePressure = 20; S.SinceDamage = 20;
        float W = SafeUseTime(S, T); CHECK(W > 5.f && W < 6.f, "opponent 4000 away closing at 600: about 5 s (big shield ok, medkit not)");
        S = Quiet(); S.ZoneSlack = 4.f; CHECK(SafeUseTime(S, T) < 3.f, "zone forces a move soon: short window only");
        S = Quiet(); S.SinceDamage = 2.f; CHECK(SafeUseTime(S, T) == 0, "just hit, no cover: wait");
        S.InCover = true; CHECK(SafeUseTime(S, T) <= T.QuickMaxUse && SafeUseTime(S, T) > 0, "just hit but in cover: quick items only");
    }
    { // Mid-fight quick heal: boxed, pressure stops for 1.75 s -> mini/bandage, never a medkit.
        Situation S; S.InCombat = true; S.EnemyKnown = true; S.EnemyDistance = 1500; S.InCover = true; S.SincePressure = 1.f; S.SinceDamage = 1.f;
        CHECK(SafeUseTime(S, T) == 0, "pressure still on: no heal");
        S.SincePressure = 1.8f; S.SinceDamage = 1.8f;
        float W = SafeUseTime(S, T); CHECK(W > 0 && W <= 3.5f, "pressure stopped: quick window");
        HealState H; auto D = PlanHeal(H, 40, 0, 0, 0, Kit(), S, T, 1.f / 30);
        CHECK(D.Do == HealAction::Start && (D.Kind == Item::MiniShield || D.Kind == Item::Bandage), "quick pop of a mini or bandage");
        S.InCover = false; HealState H2; CHECK(PlanHeal(H2, 40, 0, 0, 0, Kit(), S, T, 1.f / 30).Do == HealAction::None, "not in cover: no mid-fight heal");
    }
    { // Use timer: completes after the real time, cancels when moving / hit / seen.
        HealState H; Situation S = Quiet();
        auto D = PlanHeal(H, 100, 0, 0, 0, Kit(), S, T, 0); CHECK(D.Do == HealAction::Start && D.Kind == Item::MiniShield, "start mini");
        float Time = 0; HealDecision Last;
        while ((Last = PlanHeal(H, 100, 0, 0, 0, Kit(), S, T, .1f)).Do == HealAction::Continue) Time += .1f;
        CHECK(Last.Do == HealAction::Complete && Time > 1.7f && Time < 2.1f, "mini completes after ~2 s");
        HealState M; PlanHeal(M, 100, 0, 0, 0, Kit(), S, T, 0);
        CHECK(PlanHeal(M, 100, 0, 100, 0, Kit(), S, T, .1f).Do == HealAction::Cancel, "moving cancels");
        HealState G; PlanHeal(G, 100, 0, 0, 0, Kit(), S, T, 0);
        CHECK(PlanHeal(G, 90, 0, 0, 0, Kit(), S, T, .1f).Do == HealAction::Cancel, "damage cancels");
        HealState V; PlanHeal(V, 100, 0, 0, 0, Kit(), S, T, 0); Situation Seen = S; Seen.EnemyVisible = true;
        CHECK(PlanHeal(V, 100, 0, 0, 0, Kit(), Seen, T, .1f).Do == HealAction::Cancel, "opponent in view cancels");
    }
    { // Shields before fights: shields topped to the minimum even at full health.
        HealState H; Situation S; S.InCombat = true; S.EnemyKnown = true; S.InCover = true; S.SincePressure = 3; S.SinceDamage = 3; S.EnemyDistance = 2500;
        auto D = PlanHeal(H, 100, 25, 0, 0, Kit(), S, T, 0);
        CHECK(D.Do == HealAction::Start && D.Kind == Item::MiniShield, "below minShieldsBeforeFight: mini in a lull");
        HealState H2; CHECK(PlanHeal(H2, 100, 60, 0, 0, Kit(), S, T, 0).Do == HealAction::None, "above the minimum mid-fight: keep fighting");
    }
    { // Farming thresholds with hysteresis.
        Materials M; M.Wood = 200; M.Stone = 100; bool Farming = false;
        CHECK(WantFarm(M, Farming, false, false, 1e9f, true, T), "300 total < 500: farm");
        M.Wood = 400; M.Stone = 200; CHECK(WantFarm(M, Farming, false, false, 1e9f, true, T), "600: keep farming until 750");
        M.Wood = 500; M.Stone = 300; CHECK(!WantFarm(M, Farming, false, false, 1e9f, true, T), "800: stop");
        M.Wood = 100; M.Stone = 0; CHECK(!WantFarm(M, Farming, true, false, 1e9f, true, T), "never in a fight");
        CHECK(!WantFarm(M, Farming, false, true, 1e9f, true, T), "never in the storm");
        CHECK(!WantFarm(M, Farming, false, false, 10.f, true, T), "never on a tight rotation");
        CHECK(!WantFarm(M, Farming, false, false, 1e9f, false, T), "loot a weapon first");
    }
    { // Material priority.
        Materials M; M.Wood = 50; CHECK(Wanted(M, 1, false, T) == Mat::Wood, "early, low wood: wood");
        M.Wood = 400; M.Stone = 50; M.Metal = 100; CHECK(Wanted(M, 5, false, T) == Mat::Stone, "late game: stone/metal");
        M.Wood = 300; CHECK(Wanted(M, 1, true, T) == Mat::Stone, "holding a box: stone");
        Mat Out; CHECK(PropMaterial("BP_Tree_Pine_C", Out) && Out == Mat::Wood, "tree = wood");
        CHECK(PropMaterial("Rock_Large_03", Out) && Out == Mat::Stone, "rock = stone");
        CHECK(PropMaterial("Car_Sedan_01_C", Out) && Out == Mat::Metal, "car = metal");
        CHECK(!PropMaterial("Scarecrow_C", Out) && !PropMaterial("Brick_Pillar_House_C", Out) && !PropMaterial("Carpet_Rug_C", Out) && !PropMaterial("Rocket_Prop_C", Out), "no substring false positives");
        CHECK(!PropMaterial("Mystery_Thing_C", Out), "truly unknown class: skip");
        CHECK(PropExcluded("Prop_Fireplace_C") && PropExcluded("Vending_Machine_C") && !PropExcluded("BP_Tree_Oak_C"), "structure/interactables excluded");
        CHECK(FromResourceType(2, Out) && Out == Mat::Metal && !FromResourceType(5, Out), "ResourceType mapping");
        CHECK(!PropMaterial("Athena_Floor_Player", Out), "unknown class: skip");
        CHECK(!PropMaterial("Wall_Wood_Suburban_C", Out) && !PropMaterial("Tiered_Chest_6_Parent_C", Out), "house walls and chests are never farmed");
        CHECK(PropScore(Mat::Wood, Mat::Wood, 500, 300, false, T) > PropScore(Mat::Stone, Mat::Wood, 400, 300, false, T), "wanted material beats a slightly closer other");
        CHECK(PropScore(Mat::Wood, Mat::Wood, 500, 300, true, T) < 0, "props another bot farms are skipped");
    }
    { // Long heal cancelled when an unseen opponent will arrive before it finishes.
        HealState H; Situation S; S.EnemyKnown = true; S.EnemyDistance = 8000; S.SincePressure = 30; S.SinceDamage = 30;
        auto D = PlanHeal(H, 40, 100, 0, 0, Kit(), S, T, 0); CHECK(D.Do == HealAction::Start && D.Kind == Item::Medkit, "far opponent: medkit");
        S.EnemyDistance = 2000; S.EnemyClosing = 600;
        CHECK(PlanHeal(H, 40, 100, 0, 0, Kit(), S, T, .1f).Do == HealAction::Cancel, "opponent closing in: cancel");
    }
    { // In-combat shield skip (shield above the minimum, health fine): no heal even with time.
        HealState H; Situation S; S.InCombat = true; S.EnemyKnown = true; S.InCover = true; S.SincePressure = 3; S.SinceDamage = 3; S.EnemyDistance = 2500;
        Counts Big; Big.Big = 1; Big.Mini = 0;
        CHECK(PlanHeal(H, 100, 60, 0, 0, Big, S, T, 0).Do == HealAction::None, "mid-fight: shields already above minimum");
        Situation St; St.InStorm = true; HealState G; PlanHeal(G, 100, 0, 0, 0, Kit(), Situation{}, T, 0);
        CHECK(PlanHeal(G, 100, 0, 0, 0, Kit(), St, T, .1f).Do == HealAction::Cancel, "storm cancels");
        Situation Z; Z.ZoneSlack = .5f; HealState R; PlanHeal(R, 100, 0, 0, 0, Kit(), Situation{}, T, 0);
        CHECK(PlanHeal(R, 100, 0, 0, 0, Kit(), Z, T, .1f).Do == HealAction::Cancel, "must rotate cancels");
        Situation O; O.EnemyKnown = true; O.EnemyDistance = 2000; O.SincePressure = 30; O.SinceDamage = 30;
        CHECK(SafeUseTime(O, T) <= T.QuickMaxUse, "known opponent within 3000, no cover: quick items only");
    }
    { // Boxed after a fight (box built to heal): a medkit fits with an opponent 3500 away (the box adds BoxDelay).
        Situation S; S.EnemyKnown = true; S.EnemyDistance = 3500; S.SincePressure = 5; S.SinceDamage = 5; S.InCover = true; S.Boxed = true;
        CHECK(SafeUseTime(S, T) >= Info(Item::Medkit).UseTime, "boxed, quiet for 5 s, opponent 3000 away: long heal allowed");
        Situation Open = S; Open.Boxed = false; Open.InCover = false; Open.EnemyDistance = 2500;
        CHECK(SafeUseTime(Open, T) <= T.QuickMaxUse, "same spot without the box: quick items only");
        Counts Med; Med.Medkit = 1; Med.Mini = 0; Med.Big = 0; Med.Bandage = 0; HealState H;
        CHECK(PlanHeal(H, 30, 0, 0, 0, Med, S, T, 0).Kind == Item::Medkit, "boxed at 30 HP with a medkit: uses it");
        Situation Shot = S; Shot.UnderFire = true;
        CHECK(SafeUseTime(Shot, T) == 0.f, "boxed but under fire: nothing");
    }
    std::printf(Failures ? "%d FAILED\n" : "all sustain tests passed\n", Failures);
    return Failures ? 1 : 0;
}
