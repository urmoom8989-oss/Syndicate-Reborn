#pragma once
// Syndicate bot sustain: consumable healing/shielding with real use times, the mid-fight quick heal,
// and material farming. Pure logic, no engine types (tested by Validation/sustain-test.cpp).
//
// Priority in the bot tick:  FLEE_OR_HIDE / HEAL  >  BUILD_FIGHT  >  BOX_UP  >  FARM_MATERIALS  >  PATROL/LOOT
//
// Healing is never instant: an item is chosen, the bot stands still (crouched) for the item's real
// use time, and only then is the health/shield applied. Moving, taking damage, an opponent coming
// into view or renewed pressure cancels the use and the item is kept.
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

namespace SyndicateSustain {

// ---- consumables ------------------------------------------------------------------------------
enum class Item : uint8_t { None, MiniShield, BigShield, Bandage, Medkit, ChugJug };

struct ItemInfo { Item Kind; const char* Name; float UseTime, Heal, HealCap, Shield, ShieldCap; };
// Battle Royale values: minis +25 shield up to 50 (2 s), big +50 shield (5 s), bandage +15 health
// up to 75 (3.5 s), medkit to 100 health (10 s), chug jug to 100 health and 100 shield (15 s).
inline const ItemInfo& Info(Item K) {
    static const ItemInfo Table[] = {
        { Item::None,       "",                    0.f,   0.f,   0.f,  0.f,   0.f },
        { Item::MiniShield, "Athena_ShieldSmall",  2.f,   0.f,   0.f, 25.f,  50.f },
        { Item::BigShield,  "Athena_Shields",      5.f,   0.f,   0.f, 50.f, 100.f },
        { Item::Bandage,    "Athena_Bandage",      3.5f, 15.f,  75.f,  0.f,   0.f },
        { Item::Medkit,     "Athena_Medkit",      10.f, 100.f, 100.f,  0.f,   0.f },
        { Item::ChugJug,    "Athena_PurpleStuff", 15.f, 100.f, 100.f,100.f, 100.f },
    };
    return Table[size_t(K)];
}
inline Item FromName(const std::string& N) {
    for (Item K : { Item::MiniShield, Item::BigShield, Item::Bandage, Item::Medkit, Item::ChugJug })
        if (N == Info(K).Name) return K;
    return Item::None;
}
// Carry limits per stack (as in Battle Royale).
inline int Cap(Item K) {
    switch (K) { case Item::MiniShield: return 6; case Item::Bandage: return 15; case Item::BigShield: case Item::Medkit: return 3; case Item::ChugJug: return 1; default: return 0; }
}
// Health/shield after using the item (never lowers anything).
inline void Apply(Item K, float& Health, float& Shield) {
    const auto& I = Info(K);
    if (I.Heal > 0) Health = (std::max)(Health, (std::min)(I.HealCap, Health + I.Heal));
    if (I.Shield > 0) Shield = (std::max)(Shield, (std::min)(I.ShieldCap, Shield + I.Shield));
}
inline bool Useful(Item K, float Health, float Shield) {
    float H = Health, S = Shield; Apply(K, H, S);
    return H > Health + .5f || S > Shield + .5f;
}

struct Counts { int Mini = 0, Big = 0, Bandage = 0, Medkit = 0, Chug = 0;
    int Of(Item K) const { switch (K) { case Item::MiniShield: return Mini; case Item::BigShield: return Big; case Item::Bandage: return Bandage; case Item::Medkit: return Medkit; case Item::ChugJug: return Chug; default: return 0; } } };

struct Tuning {
    bool  Enabled = true;
    float HealThreshold = 65.f;        // health at or below this is "critical": health items first (was 50)
    float MinShieldsBeforeFight = 75.f;// top shields up to this before (re-)engaging when possible (was 50)
    float QuickQuiet = 1.25f;          // mid-fight quick heal: seconds without pressure while in cover (was 1.75)
    float LongQuiet = 3.f;             // long items (big/medkit/chug): seconds since last damage/pressure (was 4)
    float AssumedPushSpeed = 550.f;    // units/s an unseen opponent could close at (sprint)
    float SafetyMargin = 1.f;          // seconds of slack kept on top of the use time (was 1.5)
    float CoverDelay = 2.5f;           // extra seconds an opponent needs to get around cover
    float BoxDelay = 6.f;              // ...and to break into a full box the bot built around itself
    float CancelMove = 60.f;           // moving this far during a use cancels it
    float QuickMaxUse = 3.5f;          // items this fast count as "quick" (minis, bandages)
    bool  PassiveRegen = false;        // old behaviour: instant refills after long quiet periods
    // Farming
    int   TargetMaterialCount = 500;   // farm when wood+stone+metal is below this...
    int   FarmUntil = 750;             // ...until this
    float FarmRadius = 2500.f;         // props searched within this distance
    float LateGameCircle = 4;          // from this circle stone/metal are valued too
};

inline Tuning ForSkill(int Level) {
    Tuning T;
    switch (Level) {
    case 0: T.MinShieldsBeforeFight = 0; T.QuickQuiet = 3.f; T.TargetMaterialCount = 300; T.FarmUntil = 450; break;
    case 1: T.MinShieldsBeforeFight = 50; T.QuickQuiet = 2.f; T.TargetMaterialCount = 400; T.FarmUntil = 600; break;
    case 3: T.MinShieldsBeforeFight = 100; T.QuickQuiet = 1.f; T.TargetMaterialCount = 600; T.FarmUntil = 900; break;
    default: break;
    }
    return T;
}

// Item choice. MaxUse limits the use time the current situation allows (0 = none).
//  * critical health (<= HealThreshold): health first - medkit when there is time, else bandages;
//    a chug jug when both bars are low and there is a long window
//  * health above 75: shields first - minis up to 50, then big shields
//  * in between: bandages up to 75, then shields
inline Item Choose(float Health, float Shield, const Counts& Have, float MaxUse, const Tuning& T) {
    auto Ok = [&](Item K) { return Have.Of(K) > 0 && Info(K).UseTime <= MaxUse + 1e-3f && Useful(K, Health, Shield); };
    std::vector<Item> Order;
    if (Health <= T.HealThreshold) {
        if (Shield < 25 && Health <= 50) Order.push_back(Item::ChugJug);
        if (Health <= 60) Order.push_back(Item::Medkit);
        Order.insert(Order.end(), { Item::Bandage, Item::Medkit, Item::MiniShield, Item::BigShield });
    } else if (Health > 75) {
        Order = { Shield < 50 ? Item::MiniShield : Item::BigShield, Item::BigShield, Item::MiniShield };
        if (Health < 100) Order.push_back(Item::Medkit);
    } else {
        Order = { Item::Bandage, Shield < 50 ? Item::MiniShield : Item::BigShield, Item::BigShield, Item::MiniShield, Item::Medkit };
    }
    if (Health < 100 && Shield < 100) Order.push_back(Item::ChugJug);
    for (Item K : Order) if (Ok(K)) return K;
    return Item::None;
}

// ---- safety: how long a use the bot can commit to right now -----------------------------------
struct Situation {
    bool  EnemyKnown = false, EnemyVisible = false, UnderFire = false;
    float EnemyDistance = 1e9f;
    float EnemyClosing = 0;      // units/s the opponent is approaching (negative = moving away)
    float SinceDamage = 1e9f;    // seconds since the bot last lost health/shield
    float SincePressure = 1e9f;  // seconds since the opponent was last visible / aiming / shooting at us
    bool  InCover = false;       // boxed, indoors, or the opponent's line of sight is blocked
    bool  Boxed = false;         // standing in a box the bot just built (walls on every side)
    bool  InStorm = false;
    float ZoneSlack = 1e9f;      // seconds the bot can still stand before it must rotate
    bool  InCombat = false;      // an engagement is going on (build fight / boxed in a fight)
};
// Returns the longest use time that is safe (0 = do not start anything).
inline float SafeUseTime(const Situation& S, const Tuning& T) {
    if (S.UnderFire || S.EnemyVisible || S.InStorm) return 0.f;
    // Time an opponent needs to reach us: their actual closing speed, never assumed slower than a push.
    float Contact = 1e9f;
    if (S.EnemyKnown) Contact = S.EnemyDistance / (std::max)(S.EnemyClosing, T.AssumedPushSpeed) + (S.Boxed ? T.BoxDelay : S.InCover ? T.CoverDelay : 0.f);
    float Window = (std::min)(Contact, S.ZoneSlack) - T.SafetyMargin;
    // Mid-fight: only quick items, only in cover, only after the pressure has stopped for a moment.
    if (S.InCombat || (S.EnemyKnown && S.SincePressure < T.LongQuiet)) {
        if (!S.InCover || S.SincePressure < T.QuickQuiet || S.SinceDamage < T.QuickQuiet) return 0.f;
        return (std::min)(Window, T.QuickMaxUse);
    }
    // Long items need a longer quiet spell, or cover with a known opponent around.
    if (S.SinceDamage < T.LongQuiet || S.SincePressure < T.LongQuiet) Window = (std::min)(Window, S.InCover ? T.QuickMaxUse : 0.f);
    if (S.EnemyKnown && !S.InCover && S.EnemyDistance < 2000.f) Window = (std::min)(Window, T.QuickMaxUse); // was 3000
    return (std::max)(0.f, Window);
}

// ---- the heal planner ------------------------------------------------------------------------
enum class HealAction : uint8_t { None, Start, Continue, Complete, Cancel };
struct HealState { Item Using = Item::None; float Left = 0, Total = 0; float StartX = 0, StartY = 0, StartVitals = 0; };
struct HealDecision { HealAction Do = HealAction::None; Item Kind = Item::None; const char* Why = ""; };

// One tick. X/Y = current position, Vitals = health+shield now.
inline HealDecision PlanHeal(HealState& H, float Health, float Shield, float X, float Y, const Counts& Have,
                             const Situation& S, const Tuning& T, float DT) {
    HealDecision D;
    if (!T.Enabled) return D;
    const float Vitals = Health + Shield;
    if (H.Using != Item::None) {
        // Cancel: moved, hit, opponent in view, pressure back, storm, or the zone forces a move.
        const float Moved = std::hypot(X - H.StartX, Y - H.StartY);
        const char* Why = Moved > T.CancelMove ? "moved" : Vitals < H.StartVitals - .5f ? "took damage" :
            S.EnemyVisible ? "opponent in view" : S.UnderFire ? "under fire" : S.InStorm ? "storm" : S.ZoneSlack < H.Left ? "must rotate" :
            (S.EnemyKnown && S.EnemyDistance / (std::max)(S.EnemyClosing, T.AssumedPushSpeed) + (S.Boxed ? T.BoxDelay : S.InCover ? T.CoverDelay : 0.f) < H.Left) ? "opponent closing in" : nullptr;
        if (Why) { D.Do = HealAction::Cancel; D.Kind = H.Using; D.Why = Why; H = HealState{}; return D; }
        H.Left -= DT;
        if (H.Left > 0) { D.Do = HealAction::Continue; D.Kind = H.Using; return D; }
        D.Do = HealAction::Complete; D.Kind = H.Using; D.Why = "use time finished"; H = HealState{}; return D;
    }
    // Want anything? Below max health, or shields under the pre-fight minimum (or simply not full
    // when there is no fight around).
    const bool WantHealth = Health < 100.f;
    const bool WantShield = Shield < T.MinShieldsBeforeFight || (!S.InCombat && Shield < 100.f);
    if (!WantHealth && !WantShield) return D;
    const float Window = SafeUseTime(S, T);
    if (Window <= 0) return D;
    Item K = Choose(Health, Shield, Have, Window, T);
    if (K == Item::None) return D;
    // In combat only top shields up to the pre-fight minimum / heal when health is really down.
    if (S.InCombat && Info(K).Shield > 0 && Shield >= T.MinShieldsBeforeFight && Health > T.HealThreshold) return D;
    H.Using = K; H.Left = H.Total = Info(K).UseTime; H.StartX = X; H.StartY = Y; H.StartVitals = Vitals;
    D.Do = HealAction::Start; D.Kind = K; D.Why = S.InCombat ? "quick heal: pressure stopped" : Info(K).UseTime > T.QuickMaxUse ? "long heal: safe window" : "heal: out of combat";
    return D;
}

// ---- material farming ------------------------------------------------------------------------
enum class Mat : uint8_t { Wood, Stone, Metal };
struct Materials { int Wood = 0, Stone = 0, Metal = 0; int Total() const { return Wood + Stone + Metal; } };

// Farm now? Only in loot/patrol, never in a fight, the storm, or a tight rotation.
inline bool WantFarm(const Materials& M, bool& Farming, bool Engaged, bool InStorm, float ZoneSlack, bool HasLoadout, const Tuning& T) {
    if (!T.Enabled || Engaged || InStorm || ZoneSlack < 20.f || !HasLoadout) { Farming = false; return false; }
    if (Farming) { if (M.Total() >= T.FarmUntil) Farming = false; }
    else if (M.Total() < T.TargetMaterialCount) Farming = true;
    return Farming;
}
// Which material to look for: wood first (fast boxes) until a solid wood stack; stone/metal when
// late game or holding a position, and whenever wood is already plentiful.
inline Mat Wanted(const Materials& M, int Circle, bool Stationary, const Tuning& T) {
    const bool Late = Circle >= int(T.LateGameCircle) || Stationary;
    if (M.Wood < T.TargetMaterialCount / 2 && !(Late && M.Wood >= 150)) return Mat::Wood;
    if (Late) return M.Stone <= M.Metal ? Mat::Stone : Mat::Metal;
    if (M.Wood < T.FarmUntil / 2) return Mat::Wood;
    return M.Stone <= M.Metal ? Mat::Stone : Mat::Metal;
}
// Map props: the material comes from the actor's own ResourceType (what the game pays out per hit).
// The class name is only used to keep house structure, containers and interactables standing, and
// as a narrow fallback when a build exposes no ResourceType.
inline std::string Lower(const std::string& S) { std::string N = S; for (auto& C : N) C = char(std::tolower((unsigned char)C)); return N; }
inline bool PropExcluded(const std::string& ClassName) {
    const std::string N = Lower(ClassName);
    for (const char* W : { "wall", "floor", "roof", "stair", "door", "window", "player", "chest", "ammo", "pillar", "beam", "column",
                           "corner", "rail", "ceiling", "foundation", "frame", "chimney", "fireplace", "vending", "container_loot", "supply",
                           "llama", "trap", "spawner", "barrier", "fortress" })
        if (N.find(W) != std::string::npos) return true;
    return false;
}
inline bool FromResourceType(int Type, Mat& Out) { if (Type < 0 || Type > 2) return false; Out = Mat(Type); return true; }
// Fallback by name: whole-word style tokens only (no "car" in "Scarecrow" or "Carpet").
inline bool PropMaterial(const std::string& ClassName, Mat& Out) {
    if (PropExcluded(ClassName)) return false;
    const std::string N = "_" + Lower(ClassName) + "_";
    auto Token = [&](const char* W) { return N.find(std::string("_") + W + "_") != std::string::npos; }; // exact word between '_'
    auto Starts = [&](const char* W) { return N.find(std::string("_") + W) != std::string::npos; };
    if (Starts("tree") || Starts("pallet") || Starts("fence") || Token("log") || Starts("woodpile") || Starts("crate")) { Out = Mat::Wood; return true; }
    if (Starts("rock") && !Starts("rocket")) { Out = Mat::Stone; return true; }
    if (Starts("boulder") || Starts("stone")) { Out = Mat::Stone; return true; }
    if (Starts("car_") || Token("car") || Starts("sedan") || Starts("vehicle") || Starts("truck") || Starts("shippingcontainer")) { Out = Mat::Metal; return true; }
    return false;
}
// Score of a candidate prop (higher is better; < 0 = skip).
inline float PropScore(Mat Gives, Mat Want, float Distance, float Health, bool TakenByOther, const Tuning& T) {
    if (TakenByOther || Distance > T.FarmRadius || Health <= 1) return -1;
    float S = 100.f - Distance / 30.f;
    if (Gives == Want) S += 60.f;
    S += (std::min)(Health, 800.f) / 40.f; // big props give more per walk
    return S;
}

} // namespace SyndicateSustain
