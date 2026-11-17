// Unit tests for bot early-game looting (Erbium/Private/SpectraBots/BotLootPlan.h).
// Build and run:  g++ -std=c++17 -O1 -Wall -Wextra -o loot-plan-test loot-plan-test.cpp && ./loot-plan-test
#include "../Erbium/Erbium/Private/SpectraBots/BotLootPlan.h"
#include <cstdio>
using namespace SyndicateLoot;

static int Failures = 0;
#define CHECK(c, msg) do { if (!(c)) { std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, msg); ++Failures; } } while (0)

static Gun G(const char* Name, int Rarity) { Gun X; X.R = RoleOf(Name, &X.Category); X.Rarity = Rarity; return X; }

int main() {
    const Tuning T;
    { // Roles.
        CHECK(RoleOf("WID_Assault_Auto_Athena_R_Ore_T03") == Role::Primary, "AR = primary");
        CHECK(RoleOf("WID_Pistol_AutoHeavy_Athena_C_Ore_T02") == Role::Primary, "SMG (auto heavy pistol) = primary");
        CHECK(RoleOf("WID_Shotgun_Standard_Athena_UC_Ore_T03") == Role::Shotgun, "pump = shotgun");
        CHECK(RoleOf("WID_Sniper_BoltAction_Scope_Athena_R_Ore_T03") == Role::Sniper, "sniper");
        CHECK(RoleOf("WID_Pistol_SixShooter_Athena_C_Ore_T02") == Role::Pistol, "pistol");
        int A = 0, S = 0; RoleOf("WID_Assault_Auto", &A); RoleOf("WID_Pistol_AutoHeavy_SMG", &S); CHECK(A > S, "AR ranks above SMG");
    }
    { // Loadout completeness.
        Loadout L; CHECK(!L.Complete(T), "empty");
        L.Guns = { G("WID_Assault_Auto", 1), G("WID_Shotgun_Standard", 1) }; L.HealItems = 1; L.Materials = 150;
        CHECK(L.Complete(T), "AR + shotgun + heal + mats");
        L.HealItems = 0; CHECK(!L.Complete(T) && L.Missing(T) == "heals ", "heals missing");
    }
    { // Pickup decisions.
        Loadout L;
        CHECK(Consider(L, G("WID_Sniper_BoltAction", 1), T).Do == Pick::Take, "unarmed: any gun");
        CHECK(Consider(L, G("WID_Unknown_Thing", 1), T).Do == Pick::Take, "unarmed: even an unrecognised gun");
        L.Guns = { G("WID_Assault_Auto", 1) };
        CHECK(Consider(L, G("WID_Sniper_BoltAction", 3), T).Do == Pick::Skip, "extras wait until both core roles");
        CHECK(Consider(L, G("WID_Shotgun_Standard", 0), T).Do == Pick::Take, "missing shotgun: take");
        auto Up = Consider(L, G("WID_Assault_Auto", 3), T); CHECK(Up.Do == Pick::Replace && Up.Replace == 0, "rare AR replaces common AR");
        CHECK(Consider(L, G("WID_Assault_Auto", 1), T).Do == Pick::Skip, "same rarity: skip");
        L.Guns = { G("WID_Pistol_AutoHeavy_SMG", 2) };
        CHECK(Consider(L, G("WID_Assault_Auto", 2), T).Do == Pick::Replace, "AR over SMG at equal rarity (better category)");
        L.Guns = { G("WID_Assault_Auto", 1), G("WID_Sniper", 1), G("WID_Pistol_SixShooter", 0), G("WID_Launcher_Rocket", 2), G("WID_Sniper_Hunting", 1) };
        L.Guns[4].R = Role::Pistol; // make the extras distinct roles for the test
        auto Full = Consider(L, G("WID_Shotgun_Standard", 1), T);
        CHECK(Full.Do == Pick::Replace && L.Guns[Full.Replace].R != Role::Primary, "full inventory: shotgun replaces the weakest extra, never the AR");
    }
    { // Loot scoring: a missing-role gun beats a closer chest; heals pull while none.
        Loadout L; L.Guns = { G("WID_Assault_Auto", 1) };
        CHECK(LootCost(Kind::Gun, 1500, 0, true, false, L, T) < LootCost(Kind::Chest, 1000, 0, false, false, L, T), "missing shotgun on the floor before a nearer chest");
        CHECK(LootCost(Kind::Heal, 800, 0, false, false, L, T) < LootCost(Kind::Ammo, 600, 0, false, false, L, T), "first heal before ammo");
        CHECK(LootCost(Kind::Chest, 500, 400, false, false, L, T) > LootCost(Kind::Chest, 500, 0, false, false, L, T), "upstairs costs more");
    }
    { // Phases: loot until complete, then farm/rotate.
        Memory M; Signals S; S.HasGun = true; Loadout L; L.Guns = { G("WID_Assault_Auto", 1) }; L.Materials = 0;
        CHECK(Next(M, S, L, T, .2f).P == Phase::LootPoi, "incomplete: loot");
        L.Guns.push_back(G("WID_Shotgun_Standard", 1)); L.HealItems = 1; L.Materials = 150; S.PoiCleared = true; S.WantMaterials = true;
        CHECK(Next(M, S, L, T, .2f).P == Phase::Farm, "complete + cleared + low mats: farm");
        S.WantMaterials = false; CHECK(Next(M, S, L, T, .2f).P == Phase::Rotate, "complete + cleared: rotate");
        Memory N; Signals U; U.HasGun = true; U.ZoneUrgent = true; CHECK(Next(N, U, L, T, .2f).P == Phase::Rotate, "urgent zone beats looting");
        Memory O; Signals V; V.HasGun = true; V.SinceLanding = 300; Loadout Bad;  Bad.Guns = { G("WID_Assault_Auto", 1) };
        CHECK(Next(O, V, Bad, T, .2f).P == Phase::Rotate, "loot time cap: move on even if incomplete");
        Memory R; Signals X; X.HasGun = true; X.PoiCleared = true; Loadout NoMats; NoMats.Guns = { G("WID_Assault_Auto", 1), G("WID_Shotgun_Standard", 1) }; NoMats.HealItems = 2;
        CHECK(Next(R, X, NoMats, T, .2f).P == Phase::Farm, "only materials missing and POI cleared: farm instead of searching forever");
        Memory Q; Signals W; W.HasGun = false; W.SinceLanding = 300; CHECK(Next(Q, W, Bad, T, .2f).P == Phase::Rotate, "loot time cap applies to unarmed bots too (no endless loot at the storm edge)");
        Memory Q2; Signals W2; W2.HasGun = false; W2.SinceLanding = 100; Loadout None; CHECK(Next(Q2, W2, None, T, .2f).P == Phase::LootPoi, "unarmed within the loot window keeps searching");
        { // Missing core gear: far opponents do not stop looting, close ones and damage do.
            Loadout One; One.Guns.push_back(Gun{Role::Primary, 1, 0}); One.HealItems = 0;
            Memory A; Signals F; F.HasGun = true; F.LootFirst = true; F.EnemyInSight = true; F.NearestEnemy = 3000; F.SinceLanding = 60;
            CHECK(Next(A, F, One, T, .2f).P == Phase::LootPoi, "loot-first: opponent at 3000 does not interrupt");
            F.NearestEnemy = 900; CHECK(Next(A, F, One, T, .2f).P == Phase::Combat, "loot-first: opponent at 900 interrupts");
            Memory B2; Signals G2 = F; G2.EnemyInSight = false; G2.NearestEnemy = 1e9f; G2.Damaged = true; CHECK(Next(B2, G2, One, T, .2f).P == Phase::Combat, "loot-first: taking damage interrupts");
            Memory C2; Signals H2 = F; H2.LootFirst = false; H2.NearestEnemy = 3000; CHECK(Next(C2, H2, One, T, .2f).P == Phase::Combat, "without loot-first any opponent in sight interrupts");
        }
    }
    { // Instant interrupt and resume.
        Memory M; Signals S; S.HasGun = true; Loadout L; L.Guns = { G("WID_Shotgun_Standard", 1) };
        Next(M, S, L, T, .2f);
        S.Damaged = true; S.NearestEnemy = 600;
        auto D = Next(M, S, L, T, .05f);
        CHECK(D.P == Phase::Combat && D.Interrupt && D.ReadyShotgun, "damage: combat on the same tick, shotgun ready (enemy close)");
        S.Damaged = false; auto D2 = Next(M, S, L, T, .05f); CHECK(D2.P == Phase::Combat && !D2.Interrupt, "stays in combat while it settles");
        for (int I = 0; I < 80; ++I) Next(M, S, L, T, .05f);
        CHECK(M.P == Phase::LootPoi, "quiet for 3 s: back to looting");
        Memory G2; Signals H; H.HasGun = true; H.NearestGunfire = 1800; CHECK(Next(G2, H, L, T, .1f).Interrupt, "gunfire within 2500 interrupts");
        Memory G3; Signals F; F.HasGun = true; F.NearestGunfire = 5000; CHECK(!Next(G3, F, L, T, .1f).Interrupt, "distant gunfire does not");
        Memory G4; Signals E; E.HasGun = true; E.EnemyInSight = true; E.NearestEnemy = 3000; auto D4 = Next(G4, E, L, T, .1f);
        CHECK(D4.Interrupt && !D4.ReadyShotgun, "opponent far: interrupt, rifle ready rather than shotgun");
    }
    std::printf(Failures ? "%d FAILED\n" : "all loot-plan tests passed\n", Failures);
    return Failures ? 1 : 0;
}
