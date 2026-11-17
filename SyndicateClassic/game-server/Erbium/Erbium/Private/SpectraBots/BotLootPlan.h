#pragma once
// Syndicate early-game looting: loadout goals, inventory upgrades, POI loot phase and the instant
// combat interrupt. Pure logic, no engine types (tested by Validation/loot-plan-test.cpp).
//
// Bot tick priority:
//   FLEE/HEAL  >  FIGHT/BUILD_FIGHT  >  BOX_UP  >  LOOT_POI  >  FARM_MATERIALS  >  PATROL/ROTATE
//
// A bot lands with nothing. It loots its POI until it has a complete loadout (AR/SMG + shotgun +
// a heal stack + some materials) and the POI is cleared, then farms and rotates. Any threat (damage,
// being shot at, an opponent in sight or close gunfire) interrupts looting on that same tick.
#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

namespace SyndicateLoot {

enum class Role : uint8_t { None, Primary /*AR, SMG*/, Shotgun, Sniper, Pistol, Explosive };
// Class/definition name -> role. Within Primary, ARs rank above SMGs (category rank 2 vs 1).
inline Role RoleOf(const std::string& Name, int* CategoryRank = nullptr) {
    auto Has = [&](const char* S) { return Name.find(S) != std::string::npos; };
    int Rank = 0; Role R = Role::None;
    if (Has("Shotgun")) { R = Role::Shotgun; Rank = Has("Pump") || Has("Heavy") ? 2 : 1; }
    else if (Has("Sniper") || Has("Hunting")) { R = Role::Sniper; Rank = 1; }
    else if (Has("Assault") || Has("Scar") || Has("LMG") || Has("Minigun")) { R = Role::Primary; Rank = Has("Assault") ? 2 : 1; }
    else if (Has("SMG") || Has("MachinePistol") || Has("AutoHeavy") || Has("Scavenger")) { R = Role::Primary; Rank = 1; } // early SMGs are WID_Pistol_AutoHeavy* / WID_Pistol_Scavenger*
    else if (Has("Pistol") || Has("Revolver")) { R = Role::Pistol; Rank = 1; }
    else if (Has("Launcher") || Has("RPG") || Has("Rocket")) { R = Role::Explosive; Rank = 1; }
    if (CategoryRank) *CategoryRank = Rank;
    return R;
}

struct Gun { Role R = Role::None; int Rarity = 0; int Category = 0; };
// Higher is better within a role: rarity first, then category (AR over SMG, pump over tactical).
inline int Value(const Gun& G) { return G.Rarity * 10 + G.Category; }

struct Tuning {
    bool  Enabled = true;
    bool  NoFreeLoadout = true;    // no starting guns/heals/utility, no first-zone grant
    bool  EmergencyGun = true;     // backup loadout for a bot still unarmed after EmergencyAfter seconds (or circle 2)
    float EmergencyAfter = 150.f;  // about when the first zone is nearly over
    int   StartMaterials = 50;     // materials each type at landing (old behaviour: 400; 0 for a while)
    int   MinMaterials = 100;      // "basic materials" part of the loadout (total)
    int   MinHealItems = 1;        // at least one heal stack
    int   Slots = 5;
    float PoiRadius = 3000.f;      // looting stays around the landing spot / POI
    float MaxLootTime = 240.f;     // leave the POI after this even if incomplete (2017: shorter)
    float HearRadius = 2500.f;     // gunfire this close interrupts looting
    float ThreatQuiet = 3.f;       // seconds without threat before going back to looting
    float CloseRange = 1200.f;     // combat readiness: shotgun when the threat is this close
    float LootFirstRange = 1500.f; // missing core gear: an opponent only interrupts looting this close...
    float LootFirstHear = 1200.f;  // ...or gunfire this close (being shot or hurt always interrupts)
};

struct Loadout {
    std::vector<Gun> Guns;
    int HealItems = 0;
    int Materials = 0;
    bool Has(Role R) const { for (auto& G : Guns) if (G.R == R) return true; return false; }
    bool Complete(const Tuning& T) const { return Has(Role::Primary) && Has(Role::Shotgun) && HealItems >= T.MinHealItems && Materials >= T.MinMaterials; }
    // What is still missing, for logging and loot scoring.
    std::string Missing(const Tuning& T) const {
        std::string M;
        if (!Has(Role::Primary)) M += "AR/SMG ";
        if (!Has(Role::Shotgun)) M += "shotgun ";
        if (HealItems < T.MinHealItems) M += "heals ";
        if (Materials < T.MinMaterials) M += "materials ";
        return M.empty() ? "nothing" : M;
    }
};

// Pickup decision for a gun on the floor.
//   Take    - free slot and it is useful (missing role, or an extra role while slots remain)
//   Replace - swap out Replace index (same role and better, or a full inventory and it fills a
//             missing core role / beats the weakest gun)
//   Skip
enum class Pick : uint8_t { Skip, Take, Replace };
struct PickDecision { Pick Do = Pick::Skip; int Replace = -1; const char* Why = ""; };
inline PickDecision Consider(const Loadout& L, const Gun& C, const Tuning& T) {
    PickDecision D;
    if (C.R == Role::None) { if (L.Guns.empty()) { D.Do = Pick::Take; D.Why = "unarmed: any gun beats none"; } return D; }
    // Same role already carried: only a better one, replacing the worst of that role.
    int Same = -1;
    for (size_t I = 0; I < L.Guns.size(); ++I) if (L.Guns[I].R == C.R && (Same < 0 || Value(L.Guns[I]) < Value(L.Guns[Same]))) Same = int(I);
    if (Same >= 0) {
        if (Value(C) > Value(L.Guns[Same])) { D.Do = Pick::Replace; D.Replace = Same; D.Why = "upgrade: better gun of the same role"; }
        return D;
    }
    const bool Core = C.R == Role::Primary || C.R == Role::Shotgun;
    if (int(L.Guns.size()) < T.Slots) {
        // Free slot: core roles always; extras (sniper/pistol/explosive) only once both core roles exist
        // or the bot has nothing at all (any gun beats none).
        if (Core || L.Guns.empty() || (L.Has(Role::Primary) && L.Has(Role::Shotgun))) { D.Do = Pick::Take; D.Why = Core ? "loadout: fills a core role" : "loadout: extra role"; }
        return D;
    }
    // Full: a missing core role replaces the weakest non-core gun.
    if (Core) {
        int Worst = -1;
        for (size_t I = 0; I < L.Guns.size(); ++I) {
            const bool OtherCore = L.Guns[I].R == Role::Primary || L.Guns[I].R == Role::Shotgun;
            if (OtherCore) continue;
            if (Worst < 0 || Value(L.Guns[I]) < Value(L.Guns[Worst])) Worst = int(I);
        }
        if (Worst >= 0) { D.Do = Pick::Replace; D.Replace = Worst; D.Why = "swap: core role over an extra"; }
    }
    return D;
}

// ---- loot target scoring -----------------------------------------------------------------------
enum class Kind : uint8_t { Gun, Heal, Ammo, Materials, Chest, AmmoBox };
// Lower is better (a "cost" in units). Missing-role guns and the first heals pull hardest.
inline float LootCost(Kind K, float Distance, float HeightDelta, bool FillsMissing, bool Upgrade, const Loadout& L, const Tuning& T) {
    float C = Distance + (std::max)(0.f, HeightDelta - 150.f) * 1.5f; // stairs/floors cost more than flat ground
    switch (K) {
    case Kind::Gun: C *= FillsMissing ? .45f : Upgrade ? .7f : 1.2f; break;
    case Kind::Heal: C *= L.HealItems < T.MinHealItems ? .6f : 1.3f; break;
    case Kind::Chest: C *= L.Complete(T) ? 1.4f : .8f; break;  // chests hold several items
    case Kind::AmmoBox: C *= 1.3f; break;
    case Kind::Ammo: C *= 1.5f; break;
    case Kind::Materials: C *= L.Materials < T.MinMaterials ? .9f : 1.6f; break;
    }
    return C;
}

// ---- phases and the combat interrupt ----------------------------------------------------------
enum class Phase : uint8_t { LootPoi, Farm, Rotate, Combat };
struct Signals {
    bool  Damaged = false;          // lost health/shield within the last second
    bool  UnderFire = false;        // being shot at / aimed at (incoming fire)
    bool  EnemyInSight = false;     // an opponent in line of sight
    float NearestGunfire = 1e9f;    // distance to the closest opponent shooting right now
    float NearestEnemy = 1e9f;      // distance to the closest known opponent
    bool  PoiCleared = false;       // no unsearched chest / useful floor loot left around the POI
    bool  ZoneUrgent = false;       // the storm forces a move now (in storm / late or far rotation)
    bool  HasGun = false;
    float SinceLanding = 0;         // seconds since this bot landed
    bool  WantMaterials = false;    // below the farming target (BotSustain)
    bool  LootFirst = false;        // armed but missing an AR/SMG, shotgun or heals: finish the kit before fighting far opponents
};
struct Memory { Phase P = Phase::LootPoi; float Quiet = 1e9f, PhaseTime = 0; bool Interrupted = false; };
struct Decision { Phase P = Phase::LootPoi; bool Interrupt = false; bool ReadyShotgun = false; const char* Why = ""; };

inline bool Threat(const Signals& S, const Tuning& T) {
    if (S.LootFirst) return S.Damaged || S.UnderFire || (S.EnemyInSight && S.NearestEnemy < T.LootFirstRange) || S.NearestGunfire < T.LootFirstHear;
    return S.Damaged || S.UnderFire || S.EnemyInSight || S.NearestGunfire < T.HearRadius;
}

// Called every decision. Interrupt is true exactly on the tick looting must stop for combat.
inline Decision Next(Memory& M, const Signals& S, const Loadout& L, const Tuning& T, float DT) {
    Decision D;
    M.PhaseTime += DT;
    const bool Danger = Threat(S, T);
    M.Quiet = Danger ? 0.f : M.Quiet + DT;
    const Phase Before = M.P;
    if (Danger) {
        // Instant override, whatever the bot was doing.
        M.P = Phase::Combat;
        D.Interrupt = Before != Phase::Combat;
        D.ReadyShotgun = L.Has(Role::Shotgun) && S.NearestEnemy < T.CloseRange;
        D.Why = S.Damaged ? "took damage" : S.UnderFire ? "being shot at" : S.EnemyInSight ? "opponent in sight" : "gunfire nearby";
    } else if (M.P == Phase::Combat && M.Quiet < T.ThreatQuiet) {
        D.Why = "waiting for the fight to settle";
    } else if (S.ZoneUrgent) {
        M.P = Phase::Rotate; D.Why = "zone: must move";
    } else if (S.HasGun && L.Has(Role::Primary) && L.Has(Role::Shotgun) && L.HealItems >= T.MinHealItems && L.Materials < T.MinMaterials
               && (S.PoiCleared || S.SinceLanding >= T.MaxLootTime * .5f)) {
        M.P = Phase::Farm; D.Why = "guns and heals done: farming the missing materials";
    } else if (!S.HasGun || !L.Complete(T)) {
        // Incomplete loadout: keep looting this POI (or search wider if it is empty), within the time cap.
        if (S.SinceLanding < T.MaxLootTime) { M.P = Phase::LootPoi; D.Why = S.PoiCleared ? "loadout incomplete: searching wider" : "loadout incomplete: looting POI"; }
        else { M.P = S.WantMaterials ? Phase::Farm : Phase::Rotate; D.Why = "loot time over: moving on"; }
    } else if (!S.PoiCleared && S.SinceLanding < T.MaxLootTime * .5f) {
        M.P = Phase::LootPoi; D.Why = "loadout complete: finishing the POI";
    } else {
        M.P = S.WantMaterials ? Phase::Farm : Phase::Rotate; D.Why = S.WantMaterials ? "looted: farming materials" : "looted: rotating";
    }
    if (M.P != Before) M.PhaseTime = 0;
    D.P = M.P;
    return D;
}

inline const char* PhaseName(Phase P) {
    switch (P) { case Phase::LootPoi: return "loot-poi"; case Phase::Farm: return "farm"; case Phase::Rotate: return "rotate"; default: return "combat"; }
}

} // namespace SyndicateLoot
