#pragma once
// Syndicate bot survival: the low-health FLEE_OR_HIDE state and the top-level priority between the
// bot's combat modes. Pure logic, no engine types (tested by Validation/survival-test.cpp).
//
// Priority, evaluated every tick:
//   FleeOrHide (critical health)  >  BuildFight (active engagement)  >  BoxUp (sudden burst damage)  >  Patrol
//
// FleeOrHide steps:
//   Fallback  outdoors and exposed: wall between us and the shooter before turning away
//   Move      sprint to the best retreat spot (indoor corner/room or natural cover away from them);
//             outdoors under fire, a second wall behind us every FleeBuildInterval (double-wall retreat)
//   Hide      at the spot: crouch, stop, face the threat
//   Heal      shields/heals from the bot's own items while unseen (self-preservation before re-engaging)
// It ends when health+shield is back above RecoverHealth, when the bot is cornered (fights instead),
// after MaxFleeTime, or when the threat is gone and there is nothing left to heal with.
//
// Nothing here gives a bot anything a player would not have: retreat speed can only be lowered
// below normal sprint, healing uses carried items, every wall is a normal placed piece.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace SyndicateSurvival {

enum class Mode : uint8_t { Patrol, BoxUp, BuildFight, FleeOrHide };
enum class Step : uint8_t { Fallback, Move, Hide, Heal };
enum class Action : uint8_t { None, FallbackWall, RetreatWall, MoveToSpot, Hide, Heal, Fight };

struct Tuning {
    bool  Enabled = true;
    float LowHealthThreshold = 30.f;     // health below this...
    float ShieldBuffer = 25.f;           // ...with less shield than this is critical
    float RecoverHealth = 70.f;          // health+shield needed to leave the state
    float RetreatSpeedMultiplier = 1.f;  // fraction of normal sprint while fleeing (clamped to 0.5..1)
    float BuildAggressionRate = 1.f;     // scales build-fight build chance / speed (0.25..2); 1 = the build-fight skill defaults
    bool  FleeBuilds = true;             // fallback/double walls while retreating outdoors
    float FleeBuildInterval = 1.2f;      // seconds between retreat walls
    int   FleeMinMats = 20;
    float CornerDistance = 450.f;        // visible opponent this close: fight, do not turn your back
    float BurstDamage = 35.f;            // damage within ~1.5 s that counts as a sudden burst (box up)
    float MaxFleeTime = 25.f;
    float HideHold = 4.f;                // unseen this long with nothing to heal: re-engage
    float ReplanInterval = 1.5f;         // how often the retreat spot is re-evaluated
    float DecisionInterval = .2f;
    float EngageRange = 2600.f;          // build-fight engagement range (same scale as BotBuildFight)
};

inline Tuning ForSkill(int Level) {
    Tuning T;
    switch (Level) {
    case 0: T.Enabled = false; break;                                     // "bot": unchanged behaviour
    case 1: T.LowHealthThreshold = 25; T.FleeBuilds = false;
            T.CornerDistance = 600; T.ReplanInterval = 2.5f; T.DecisionInterval = .35f; break;
    case 3: T.LowHealthThreshold = 35; T.RecoverHealth = 80;
            T.FleeBuildInterval = .8f; T.CornerDistance = 350; T.ReplanInterval = 1.f; T.DecisionInterval = .12f; break;
    default: break;
    }
    return T;
}

inline void Clamp(Tuning& T) {
    T.RetreatSpeedMultiplier = std::clamp(T.RetreatSpeedMultiplier, .5f, 1.f); // never faster than a player can sprint
    T.BuildAggressionRate = std::clamp(T.BuildAggressionRate, .25f, 2.f);
    T.LowHealthThreshold = std::clamp(T.LowHealthThreshold, 0.f, 99.f);
    T.RecoverHealth = (std::max)(T.RecoverHealth, T.LowHealthThreshold + 10.f);
}

struct Snapshot {
    float Health = 100, Shield = 0;
    bool  HasEnemy = false, EnemyVisible = false, UnderFire = false;
    float EnemyDistance = 1e9f, EnemyVitals = -1;   // opponent health+shield, -1 unknown
    float RecentDamage = 0;                         // damage taken in the last ~1.5 s
    bool  Indoors = false, InStorm = false, Rotating = false;
    bool  HasHealItem = false, Healing = false;
    bool  HasSpot = false, AtSpot = false, SpotHidden = false; // retreat spot found / reached / out of their sight
    bool  Cornered = false;                         // no reachable retreat spot at all
    int   Mats = 0; bool BuildBlocked = false;
    bool  PanicBoxing = false;                      // an existing burst box is being built
    bool  BuildFightActive = false;                 // SyndicateBuildFight is not neutral
};

struct Memory {
    Mode  M = Mode::Patrol;
    Step  S = Step::Fallback;
    float ModeTime = 0, FleeTime = 0, BuildCooldown = 0, Replan = 0, Unseen = 0, Since = 1e9f;
    float Cooldown = 0;           // after a flee ends without recovering, no new flee for a while (unless shot)
    float LastWallDistance = 0;   // distance moved since the last retreat wall (filled by the caller)
    int   Walls = 0;
};

struct Decision {
    Mode   M = Mode::Patrol;
    Action Do = Action::None;
    bool   Crouch = false;        // hold crouch this tick
    bool   SuppressCombat = false;// do not run the normal combat/loot code this tick
    bool   Replan = false;        // caller should search for a (new) retreat spot now
    float  SpeedFraction = 1.f;   // fraction of normal sprint to use while moving
    const char* Why = "";
};

inline const char* ModeName(Mode M) {
    switch (M) { case Mode::FleeOrHide: return "flee-or-hide"; case Mode::BuildFight: return "build-fight";
                 case Mode::BoxUp: return "box-up"; default: return "patrol"; }
}

inline bool Critical(const Snapshot& S, const Tuning& T) { return S.Health < T.LowHealthThreshold && S.Shield < T.ShieldBuffer; }

// Should a critical bot stand and fight instead of running? A visible opponent right next to it
// (turning around gets it shot in the back), no retreat route at all, or an opponent even weaker.
inline bool MustFight(const Snapshot& S, const Tuning& T) {
    if (S.Cornered) return true;
    if (S.EnemyVisible && S.EnemyDistance < T.CornerDistance) return true;
    if (S.EnemyVisible && S.EnemyVitals >= 0 && S.EnemyVitals + 10.f < S.Health + S.Shield) return true;
    return false;
}

// Top-level priority. Health status is checked first, every tick, before any combat mode.
inline Mode Arbitrate(const Memory& M, const Snapshot& S, const Tuning& T) {
    const float Vitals = S.Health + S.Shield;
    // A live threat: someone we see, someone hitting us, or real damage in the last moments
    // (a remembered opponent far away does not count).
    const bool Threat = S.EnemyVisible || S.UnderFire || S.RecentDamage > 5.f;
    if (T.Enabled && !S.InStorm) {
        if (M.M == Mode::FleeOrHide) {
            // Hysteresis: once fleeing, stay until recovered (not just back over the trigger).
            const bool Recovered = Vitals >= T.RecoverHealth;
            const bool Timeout = M.FleeTime >= T.MaxFleeTime;
            const bool Over = M.Unseen >= T.HideHold && !S.HasHealItem && !S.Healing && !S.UnderFire;
            if (!Recovered && !Timeout && !Over && !MustFight(S, T)) return Mode::FleeOrHide;
        } else if (Critical(S, T) && Threat && !MustFight(S, T) && (M.Cooldown <= 0 || (S.UnderFire && M.Cooldown < 4.f))) return Mode::FleeOrHide; // fight at least 4 s before fleeing again
    }
    if (S.BuildFightActive || (S.HasEnemy && (S.EnemyVisible || S.UnderFire) && S.EnemyDistance < T.EngageRange)) return Mode::BuildFight;
    if (S.PanicBoxing || (S.RecentDamage >= T.BurstDamage && !S.EnemyVisible)) return Mode::BoxUp;
    return Mode::Patrol;
}

inline Decision Decide(Memory& M, const Snapshot& S, const Tuning& T, float DT) {
    Decision D;
    M.ModeTime += DT; M.BuildCooldown = (std::max)(0.f, M.BuildCooldown - DT); M.Replan -= DT;
    M.Cooldown = (std::max)(0.f, M.Cooldown - DT);
    M.Since += DT;
    if (M.M == Mode::FleeOrHide) {
        M.FleeTime += DT;
        M.Unseen = (S.EnemyVisible || S.UnderFire) ? 0.f : M.Unseen + DT;
    }
    const Mode Want = Arbitrate(M, S, T);
    if (Want != M.M) {
        // Left without recovering (timeout, nothing to heal, had to fight): wait before fleeing again.
        if (M.M == Mode::FleeOrHide && S.Health + S.Shield < T.RecoverHealth) M.Cooldown = 8.f;
        M.M = Want; M.ModeTime = 0;
        if (Want == Mode::FleeOrHide) { M.S = Step::Fallback; M.FleeTime = 0; M.Unseen = 0; M.Replan = 0; M.Walls = 0; M.LastWallDistance = 0; }
    }
    D.M = M.M;
    if (M.M != Mode::FleeOrHide) return D;

    D.SuppressCombat = true;
    D.SpeedFraction = std::clamp(T.RetreatSpeedMultiplier, .5f, 1.f);
    const bool Exposed = S.EnemyVisible || S.UnderFire;
    // Zone rotation outranks hiding spots: keep fire off but let the rotation movement run
    // (a heal already in progress finishes first).
    if (S.Rotating && !S.Healing) { D.Do = Action::None; D.Why = "rotating"; return D; }
    // Look for a spot while moving or when spotted; a bot hidden at its spot stays put.
    // Without any spot, retry every 0.75 s instead of every tick.
    if (M.Replan <= 0 && (!S.HasSpot || M.S == Step::Move || M.S == Step::Fallback || Exposed)) {
        D.Replan = true; M.Replan = S.HasSpot ? T.ReplanInterval : .75f;
    }

    // Healing in progress: keep going unless shot (the heal itself stops when hit).
    if (S.Healing) { D.Do = Action::Heal; D.Crouch = true; D.Why = "healing"; M.S = Step::Heal; return D; }

    const bool CanBuild = T.FleeBuilds && !S.Indoors && !S.BuildBlocked && S.Mats >= T.FleeMinMats;

    // Decisions are paced; between them keep moving/hiding as decided.
    if (M.Since < T.DecisionInterval) {
        D.Do = M.S == Step::Hide || M.S == Step::Heal ? Action::Hide : Action::MoveToSpot;
        D.Crouch = M.S == Step::Hide || M.S == Step::Heal;
        return D;
    }
    M.Since = 0;

    // 1. Outdoors and exposed: wall between us and the shooter before turning our back.
    if (M.S == Step::Fallback) {
        M.S = Step::Move;
        if (Exposed && CanBuild && M.BuildCooldown <= 0) {
            M.BuildCooldown = T.FleeBuildInterval; ++M.Walls; M.LastWallDistance = 0;
            D.Do = Action::FallbackWall; D.Why = "low health: wall before retreating"; return D;
        }
    }
    // 2. At a hidden spot: hide, then heal while unseen.
    if (S.HasSpot && S.AtSpot && (S.SpotHidden || !Exposed)) {
        if (!Exposed && S.HasHealItem) { M.S = Step::Heal; D.Do = Action::Heal; D.Crouch = true; D.Why = "hidden: healing before re-engaging"; return D; }
        M.S = Step::Hide; D.Do = Action::Hide; D.Crouch = true; D.Why = S.Indoors ? "hidden indoors" : "hidden behind cover"; return D;
    }
    // Spotted at the spot: look for another one right away.
    if (S.AtSpot && Exposed) { D.Replan = true; M.Replan = T.ReplanInterval; }
    // Unseen and somewhere quiet already: heal where we stand rather than keep running.
    if (!Exposed && M.Unseen > 1.5f && S.HasHealItem) { M.S = Step::Heal; D.Do = Action::Heal; D.Crouch = true; D.Why = "out of sight: healing"; return D; }
    // 3. Retreating under fire outdoors: double-wall behind us as we go.
    M.S = Step::Move;
    if (Exposed && CanBuild && M.BuildCooldown <= 0 && M.LastWallDistance > 250.f) {
        M.BuildCooldown = T.FleeBuildInterval; ++M.Walls; M.LastWallDistance = 0;
        D.Do = Action::RetreatWall; D.Why = "retreating: wall behind us"; return D;
    }
    if (!S.HasSpot) { D.Do = Action::Hide; D.Crouch = !Exposed; D.Why = "no retreat spot yet"; return D; }
    D.Do = Action::MoveToSpot; D.Why = S.Indoors ? "moving to an inside corner" : "moving to cover"; return D;
}

// ---- retreat spot scoring -----------------------------------------------------------------
// The caller samples points around the bot and fills one Candidate per point (two traces each);
// Pick() returns the best index or -1.
struct Candidate {
    float FromBot = 0;        // distance from the bot
    float FromEnemy = 0;      // distance from the opponent
    float AwayDot = 0;        // direction from bot to point · direction away from the opponent (-1..1)
    bool  Reachable = false;  // walkable start of the route, ground found, not water/lava, inside the next circle
    bool  Hidden = false;     // the opponent's eye line to it is blocked
    bool  NaturalCover = false; // blocked by terrain/trees/rocks/house walls (not a player piece that can be shot away)
    bool  Indoors = false;    // roof above the point
};
inline int Pick(const std::vector<Candidate>& C, bool BotIndoors) {
    int Best = -1; float Score = -1e9f;
    for (size_t I = 0; I < C.size(); ++I) {
        const auto& X = C[I];
        if (!X.Reachable || X.AwayDot < -.2f) continue;      // never run toward or past the opponent
        float Sc = 0;
        if (X.Hidden) Sc += 100;
        if (X.NaturalCover) Sc += 40;
        if (BotIndoors && X.Indoors) Sc += 30;               // stay inside: rooms and corners
        Sc += X.AwayDot * 25;
        Sc += (std::min)(X.FromEnemy, 4000.f) / 100.f;       // farther from them is better...
        Sc -= X.FromBot / 60.f;                              // ...but a short sprint beats a long one
        if (Sc > Score) { Score = Sc; Best = int(I); }
    }
    return Best;
}

} // namespace SyndicateSurvival
