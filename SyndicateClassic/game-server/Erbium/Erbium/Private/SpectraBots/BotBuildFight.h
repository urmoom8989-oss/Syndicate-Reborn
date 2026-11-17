#pragma once
// Syndicate build-fight module: the decision half of the bots' build-battle behaviour.
//
// Pure logic with no engine types, so it can be unit-tested on its own
// (see Validation/BotBuildFightTest.cpp). SpectraBotsIntegration.h fills a Snapshot from the
// world each decision, calls Decide(), and carries out the returned Action with the existing
// build/edit/weapon helpers (BuildCover, BuildCone, BotEditOwnPiece, inventory equip).
//
// States
//   Neutral        no build fight: the normal combat code moves, boxes and shoots.
//   ShotBlock      the opponent has an open angle (or is hitting us): put a wall/ramp between
//                  us first, then fight from that cover.
//   HeightContest  the opponent is building toward or above us: ramp up beside/ahead of them,
//                  wall off the front while climbing, cone above them to stop their climb.
//   BoxFight       close range (about 1-2 tiles) or we just broke through their wall:
//                  strict loop Build -> Swap to shotgun -> Peek (edit) -> Shoot -> Re-cover.
//
// Every bot reacts after a human-like reaction delay, only builds with a per-stimulus
// probability, and never gets protection it did not build (no invulnerability, no teleports).
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace SyndicateBuildFight {

enum class State : uint8_t { Neutral, ShotBlock, HeightContest, BoxFight };
enum class Gun : uint8_t { None, Shotgun, Rifle, Smg, Pistol, Sniper, Other };
enum class Action : uint8_t {
    None,         // nothing new this tick; existing combat code continues (may shoot)
    BlockWall,    // wall between us and the opponent
    BlockRamp,    // ramp toward the opponent (cover against fire from above / ahead)
    ContestRamp,  // ramp up beside/ahead of the opponent and climb it
    ContestCone,  // cone above the opponent's head: blocks their next ramp, steals height
    TopCover,     // cone above ourselves while climbing (no shots down into us)
    SwapShotgun,
    SwapRifle,
    Peek,         // edit our own wall open toward the opponent (window/arch)
    Shoot,        // fire window: the normal aim/fire code runs this tick
    ReCover,      // close the peek again (reset the edit, or place a new wall)
    BreakRamp,    // shoot the ramp the opponent is rushing on (fire window aimed at their ramp)
};
enum class Step : uint8_t { Build, Swap, Peek, Shoot, ReCover };

// Difficulty knobs. ForSkill() gives the defaults per bot skill; the integration layer can
// override any of them (environment variables TILTED_BF_*), see SpectraBotsIntegration.h.
struct Tuning {
    bool  Enabled = true;
    bool  ContestEnabled = true;     // height contest state
    bool  BoxLoopEnabled = true;     // strict close-quarters loop
    float ReactionDelay = .38f;      // seconds before reacting to a new threat/climb (was .45)
    float BuildChance = .85f;        // chance to answer a new stimulus with building at all (was .75)
    float DecisionInterval = .16f;   // seconds between decisions (performance + human pacing) (was .2)
    float BuildInterval = .45f;      // minimum gap between two placed pieces (was .55)
    float ShotgunRange = 640.f;      // swap to shotgun / box-fight below this (UE units, tile = 512)
    float RifleRange = 1400.f;       // prefer AR/rifle above this
    float EngageRange = 2200.f;      // build-fight logic only inside this distance
    float ContestTrigger = 200.f;    // opponent this much higher (or climbing) starts a height contest
    float ContestRange = 1600.f;     // ...if they are this close
    float WinMargin = 250.f;         // we are this much above them: height won
    float ClimbRate = 180.f;         // opponent rising faster than this (units/s) counts as climbing
    float PeekWindow = .8f;          // seconds the edit stays open for shooting
    float ReCoverDelay = .3f;        // pause after re-covering before the next peek
    int   MinMats = 30;              // keep at least this much material for a box
    float BreakRampChance = .3f;     // chance per decision to shoot a rushing opponent's ramp out instead of building
    float ContestChance = .9f;       // chance to answer a ramp rush (fresh climb) by building at all
};

inline Tuning ForSkill(int Level) {
    Tuning T;
    switch (Level) {
    case 0: // "bot": no build fights
        T.Enabled = false; break;
    case 1: // easy: blocks open shots sometimes, nothing fancier
        T.ContestEnabled = false; T.BoxLoopEnabled = false;
        T.ReactionDelay = .7f; T.BuildChance = .55f; T.DecisionInterval = .26f; T.BuildInterval = 1.f;
        T.EngageRange = 1800.f; T.MinMats = 40; T.BreakRampChance = .25f; break;
    case 3: // hard
        T.ReactionDelay = .28f; T.BuildChance = .95f; T.DecisionInterval = .12f; T.BuildInterval = .3f;
        T.ShotgunRange = 768.f; T.RifleRange = 1300.f; T.EngageRange = 2600.f;
        T.ContestTrigger = 150.f; T.ContestRange = 2000.f; T.WinMargin = 300.f;
        T.PeekWindow = .7f; T.ReCoverDelay = .22f; T.MinMats = 20; T.BreakRampChance = .45f; T.ContestChance = 1.f; break;
    case 4: // pro: a real step above hard - not better aim, better DECISIONS. Reacts sooner, always answers a
            // stimulus, decides and places pieces faster, contests height earlier and from farther, peeks tighter
            // (less exposed) and re-covers quicker, and takes an opponent's ramp out more readily. Build cadence is
            // still floored at 0.25 s in BuildFightTuning, so it is fast-but-human, never a build bot.
        T.ReactionDelay = .18f; T.BuildChance = 1.f; T.DecisionInterval = .08f; T.BuildInterval = .25f;
        T.ShotgunRange = 820.f; T.RifleRange = 1250.f; T.EngageRange = 2800.f;
        T.ContestTrigger = 110.f; T.ContestRange = 2200.f; T.WinMargin = 320.f;
        T.PeekWindow = .6f; T.ReCoverDelay = .15f; T.MinMats = 15; T.BreakRampChance = .6f; T.ContestChance = 1.f; break;
    default: break; // medium (2) uses the defaults above
    }
    return T;
}

// What the bot sees this decision. Distances in UE units, HeightDelta = opponent Z - our Z.
struct Snapshot {
    float Distance = 0, HeightDelta = 0, EnemyRise = 0; // EnemyRise: opponent vertical speed (units/s)
    bool  EnemyVisible = false;   // we can see them
    bool  UnderFire = false;      // took damage / a visible opponent is aiming at us
    bool  EnemyHasAngle = false;  // open line between us at weapon height (no piece in between)
    bool  CoverBetween = false;   // one of our own pieces blocks the line to them
    bool  EnemyWallBetween = false; // one of THEIR pieces blocks the line
    bool  EnemyOnBuild = false;   // they stand on a player-built ramp/floor (building up)
    bool  CanPeekEdit = false;    // the blocking piece is our own wall close enough to edit
    bool  Peeking = false;        // an edit peek is currently open
    bool  HeightCapped = false;   // ramp height cap reached (no higher ramps allowed)
    bool  BuildBlocked = false;   // cannot build right now (in water, mid-air, build backoff)
    bool  Alerted = false;        // heard shooting / was shot at a moment ago (BotAwareness.h): reacts sooner
    int   Mats = 0;
    bool  HasShotgun = false, HasRifle = false;
    Gun   Equipped = Gun::None;
};

// Per-bot memory kept between decisions.
struct Memory {
    State S = State::Neutral;
    Step  Loop = Step::Build;
    float StateTime = 0, Wait = 0, React = 0, BuildCooldown = 0, ShootTime = 0, ConeCooldown = 0, SwapCooldown = 0, BreakCooldown = 0;
    bool  WasThreatened = false, WasRising = false, WasEnemyWall = false;
    float SinceThreat = 1e9f, SinceRising = 1e9f; // seconds since the threat / the climb was last seen
    bool  WillBuild = true;       // result of the BuildChance roll for the current stimulus
    float BrokeThrough = 0;       // seconds left in which a just-broken enemy wall counts
    bool  HoldFire = false, HoldWeapon = false; // last decision's fire/weapon ownership, kept between decisions
    float ContestCooldown = 0;    // after a contest ends, no new one for a while unless they keep building
    int   BuildFails = 0, PeekFails = 0; // consecutive failed placements / edits (give up instead of retrying forever)
    float SinceDecision = 1e9f;
    unsigned Decisions = 0;
};

struct Decision {
    Action Do = Action::None;
    bool BlockFire = false;   // do not shoot this tick (building / swapping / covered)
    bool OwnsWeapon = false;  // the module picks the weapon; skip the generic distance picker
    State S = State::Neutral;
    const char* Why = "";
};

inline const char* StateName(State S) {
    switch (S) { case State::ShotBlock: return "shot-block"; case State::HeightContest: return "height-contest";
                 case State::BoxFight: return "box-fight"; default: return "neutral"; }
}

// Results of the executed action, reported back by the integration layer.
inline void Report(Memory& M, const Tuning& T, Action Done, bool Ok) {
    switch (Done) {
    case Action::BlockWall: case Action::BlockRamp: case Action::ContestRamp: case Action::TopCover: case Action::ReCover:
        M.BuildCooldown = Ok ? T.BuildInterval : T.BuildInterval * .5f;
        // Two failed placements in a row: stop building for this stimulus (fight without it)
        // instead of holding fire while retrying a piece that cannot go there.
        if (Ok) M.BuildFails = 0; else if (++M.BuildFails >= 2) M.WillBuild = false;
        if (Done == Action::ReCover) { M.Wait = T.ReCoverDelay; if (Ok || M.Loop == Step::ReCover) M.Loop = Step::Peek; }
        else if (Ok && M.S == State::BoxFight && M.Loop == Step::Build) M.Loop = Step::Swap;
        break;
    case Action::ContestCone: M.ConeCooldown = Ok ? 2.5f : 1.f; M.BuildCooldown = T.BuildInterval * (Ok ? 1.f : .5f);
        if (Ok) M.BuildFails = 0; else if (++M.BuildFails >= 2) M.WillBuild = false;
        break;
    case Action::BreakRamp: M.BreakCooldown = Ok ? 2.5f : 1.f; break;
    case Action::SwapShotgun: case Action::SwapRifle:
        M.SwapCooldown = .6f; if (M.S == State::BoxFight && M.Loop == Step::Swap) M.Loop = Step::Peek; break;
    case Action::Peek:
        M.Loop = Step::Shoot; M.ShootTime = 0; // a failed edit still gets a full shoot window
        if (Ok) M.PeekFails = 0; else ++M.PeekFails;
        break;
    default: break;
    }
}

namespace Detail {
inline void Enter(Memory& M, State S) {
    if (M.S == S) return;
    if (M.S == State::HeightContest) M.ContestCooldown = 8.f;
    M.S = S; M.StateTime = 0; M.PeekFails = 0;
    if (S == State::BoxFight) M.Loop = Step::Build;
}
}

// One decision. Call every tick with that tick's DT; it only re-evaluates every
// Tuning::DecisionInterval and otherwise returns a cached "hold" (None, no fire block change).
// Roll is a uniform random number in [0,1) for this call.
namespace Detail {
inline Decision DecideNow(Memory& M, const Snapshot& S, const Tuning& T, float DT, float Roll) {
    Decision D;
    M.StateTime += DT; M.Wait = (std::max)(0.f, M.Wait - DT); M.React = (std::max)(0.f, M.React - DT);
    M.BuildCooldown = (std::max)(0.f, M.BuildCooldown - DT); M.ConeCooldown = (std::max)(0.f, M.ConeCooldown - DT);
    M.SwapCooldown = (std::max)(0.f, M.SwapCooldown - DT); M.ContestCooldown = (std::max)(0.f, M.ContestCooldown - DT);
    M.BreakCooldown = (std::max)(0.f, M.BreakCooldown - DT);
    if (M.S == State::BoxFight && M.Loop == Step::Shoot) M.ShootTime += DT;
    D.S = M.S;
    if (!T.Enabled) { Detail::Enter(M, State::Neutral); D.S = M.S; return D; }

    // Stimuli: a fresh threat or a fresh climb starts a human reaction delay and a new build roll.
    // Our own open edit peek is not a new threat (the line is open because we opened it).
    const bool Threatened = S.UnderFire || (S.EnemyVisible && S.EnemyHasAngle && !S.CoverBetween && !S.Peeking);
    const bool Rising = T.ContestEnabled && S.Distance < T.ContestRange &&
        (S.HeightDelta > T.ContestTrigger || (S.EnemyOnBuild && S.EnemyRise > T.ClimbRate && S.HeightDelta > -T.ContestTrigger));
    // A stimulus is only new after a gap of 1.5 s. A ramp rush flickers "rising" between decisions
    // (each ramp step, a jump, a pause to place the next ramp); restarting the reaction delay and the
    // build roll on every flicker kept bots "reacting" through the whole push and they never built.
    M.SinceThreat += DT; M.SinceRising += DT;
    const bool FreshThreat = Threatened && M.SinceThreat > 1.5f, FreshRise = Rising && M.SinceRising > 1.5f;
    if (FreshThreat || FreshRise) {
        M.React = T.ReactionDelay * (.8f + .4f * Roll) * (S.Alerted ? .6f : 1.f);
        // Being ramp-rushed is answered by building more readily than a plain shot (Medium 90%, Hard always).
        const float Chance = FreshRise ? (std::max)(T.BuildChance, T.ContestChance) : T.BuildChance;
        M.WillBuild = Roll < Chance || Chance >= 1.f;
        M.BuildFails = 0;
    }
    if (Threatened) M.SinceThreat = 0;
    if (Rising) M.SinceRising = 0;
    M.WasThreatened = Threatened; M.WasRising = Rising;
    // Still being pushed: rose within the last 1.2 s and is still on their build within contest range.
    const bool Pushed = T.ContestEnabled && (Rising || (M.SinceRising < 1.2f && S.EnemyOnBuild && S.Distance < T.ContestRange));
    if (M.WasEnemyWall && !S.EnemyWallBetween) M.BrokeThrough = 1.f;
    M.BrokeThrough = (std::max)(0.f, M.BrokeThrough - DT);
    const bool BrokeThrough = M.BrokeThrough > 0 && S.EnemyHasAngle;
    M.WasEnemyWall = S.EnemyWallBetween;

    // Decision pacing (performance and human-like cadence).
    M.SinceDecision += DT;
    if (M.SinceDecision < T.DecisionInterval) {
        // Between decisions keep the last decision's fire/weapon ownership; a box-fight shoot
        // window stays open for its full length.
        D.BlockFire = M.HoldFire && !(M.S == State::BoxFight && M.Loop == Step::Shoot);
        D.OwnsWeapon = M.HoldWeapon;
        return D;
    }
    M.SinceDecision = 0; ++M.Decisions;

    if (S.Distance > T.EngageRange) { Detail::Enter(M, State::Neutral); D.S = M.S; return D; }

    const bool CanBuild = M.WillBuild && !S.BuildBlocked && S.Mats >= T.MinMats;
    const bool BuildReady = CanBuild && M.BuildCooldown <= 0;
    const float Shotgun = M.S == State::BoxFight ? T.ShotgunRange * 1.35f : T.ShotgunRange; // hysteresis
    // Ramp rush answer: shoot out the ramp an opponent is climbing toward us (they fall or lose the height).
    const bool CanBreakRamp = S.EnemyOnBuild && S.Equipped != Gun::None && S.Distance < 1400.f && M.BreakCooldown <= 0 &&
        (S.EnemyRise > T.ClimbRate || S.HeightDelta > T.ContestTrigger * .5f);

    // ---- choose the state -------------------------------------------------------------
    State Want = State::Neutral;
    const bool HeightWon = S.HeightDelta < -T.WinMargin;
    // A contest needs them actually building (or a fresh contest); someone simply standing on a
    // hill does not keep the bot climbing forever.
    if (Pushed && CanBuild && !S.HeightCapped && !HeightWon && (M.S == State::HeightContest ? (S.EnemyOnBuild && M.StateTime < 6.f) : (M.ContestCooldown <= 0 || (S.EnemyOnBuild && S.EnemyRise > T.ClimbRate))))
        Want = State::HeightContest;
    else if (M.S == State::HeightContest && !HeightWon && !S.HeightCapped && CanBuild && S.HeightDelta > 0 && M.StateTime < 6.f)
        Want = State::HeightContest; // keep climbing until we are level/above, not on every flicker
    else if (T.BoxLoopEnabled && (S.Distance < Shotgun || (BrokeThrough && S.Distance < T.ShotgunRange * 1.6f)) && std::abs(S.HeightDelta) < 420.f)
        Want = State::BoxFight;
    else if (CanBuild && Threatened) Want = State::ShotBlock;
    else if (M.S == State::ShotBlock && (S.CoverBetween || S.Peeking) && S.Distance < T.EngageRange && ((S.CanPeekEdit && M.PeekFails < 2) || S.Peeking || M.StateTime < 2.f))
        Want = State::ShotBlock; // fight from the cover we built (if we can peek it)
    Detail::Enter(M, Want);
    D.S = M.S;

    // Reaction delay: the bot keeps doing what it did (normal combat) until it "notices".
    if (M.React > 0 && M.S != State::BoxFight) { D.Why = "reacting"; return D; }

    switch (M.S) {
    case State::Neutral: {
        // Pushed up a ramp but not building (no contest for this tier, no mats, build roll failed): shoot it out.
        if (CanBreakRamp && Roll < T.BreakRampChance) { D.Do = Action::BreakRamp; D.Why = "pushed: shoot their ramp out"; return D; }
        // Mid/long range weapon choice: AR/rifle when tracking outside box range.
        if (S.Distance > T.RifleRange && S.HasRifle && S.Equipped != Gun::Rifle && S.Equipped != Gun::Sniper && M.SwapCooldown <= 0) {
            D.Do = Action::SwapRifle; D.Why = "range: rifle for mid/long distance"; D.OwnsWeapon = true;
        }
        return D;
    }
    case State::ShotBlock: {
        // Covered and peeking: our own edit is open, shoot through it (checked before the
        // open-angle branch, because the open edit reads as an open line).
        if (S.Peeking) { D.Do = Action::Shoot; D.OwnsWeapon = true; D.Why = "covered: shooting through peek"; return D; }
        if (!S.CoverBetween) {
            // Open angle on us: cover first, no open-field firing while building is possible.
            D.BlockFire = true;
            if (BuildReady) { D.Do = S.HeightDelta > T.ContestTrigger * .5f ? Action::BlockRamp : Action::BlockWall; D.Why = "open angle: block line of sight"; }
            else D.Why = "open angle: waiting to place cover";
            return D;
        }
        // Covered: peek our own wall to shoot, then close it (same loop as box fights, rifle range).
        if (M.Wait > 0) { D.BlockFire = true; D.Why = "covered: settling"; return D; }
        if (S.Distance > T.RifleRange && S.HasRifle && S.Equipped != Gun::Rifle && S.Equipped != Gun::Sniper && M.SwapCooldown <= 0) {
            D.Do = Action::SwapRifle; D.BlockFire = true; D.OwnsWeapon = true; D.Why = "covered: rifle for the peek"; return D;
        }
        if (S.CanPeekEdit && M.PeekFails < 2) { D.Do = Action::Peek; D.BlockFire = true; D.Why = "covered: edit peek"; M.Wait = T.PeekWindow + T.ReCoverDelay; return D; }
        D.BlockFire = true; D.Why = "covered by a piece we cannot edit"; return D;
    }
    case State::HeightContest: {
        D.BlockFire = true;
        // Deny their climb: cone right above them once we are close to their height.
        if (M.ConeCooldown <= 0 && BuildReady && S.Distance < 900.f && S.HeightDelta > 0 && S.HeightDelta < 500.f && S.EnemyOnBuild) {
            D.Do = Action::ContestCone; D.Why = "height: cone above opponent to stop their ramp"; return D;
        }
        // Being shot while climbing: protective wall toward them first.
        if (S.UnderFire && !S.CoverBetween && BuildReady) { D.Do = Action::BlockWall; D.Why = "height: protective wall while climbing"; return D; }
        // Directly under them: roof over our head before the next ramp.
        if (BuildReady && S.Distance < 450.f && S.HeightDelta > 250.f) { D.Do = Action::TopCover; D.Why = "height: top cover under opponent"; return D; }
        // Close and above us on their ramp: sometimes shoot the ramp out instead of out-building them.
        if (CanBreakRamp && S.HeightDelta > 100.f && Roll < T.BreakRampChance) { D.Do = Action::BreakRamp; D.BlockFire = false; D.Why = "height: shoot their ramp out"; return D; }
        if (BuildReady) { D.Do = Action::ContestRamp; D.Why = S.HeightDelta > 0 ? "height: ramp to retake height" : "height: ramp ahead of opponent"; return D; }
        D.Why = "height: waiting for build cooldown"; return D;
    }
    case State::BoxFight: {
        D.OwnsWeapon = S.HasShotgun; // without a shotgun the normal distance picker chooses
        // Breaking through their wall at close range: shotgun immediately.
        if (BrokeThrough && S.HasShotgun && S.Equipped != Gun::Shotgun && M.SwapCooldown <= 0) {
            D.Do = Action::SwapShotgun; D.BlockFire = true; D.Why = "box: broke through, shotgun"; return D;
        }
        for (int Guard = 0; Guard < 5; ++Guard) {
            switch (M.Loop) {
            case Step::Build:
                if (S.CoverBetween || S.EnemyWallBetween) { M.Loop = Step::Swap; continue; }
                if (BuildReady) { D.Do = Action::BlockWall; D.BlockFire = true; D.Why = "box: build"; return D; }
                if (!CanBuild) { M.Loop = Step::Swap; continue; } // no mats: fight without cover
                D.BlockFire = true; D.Why = "box: build cooldown"; return D;
            case Step::Swap:
                if (S.HasShotgun && S.Equipped != Gun::Shotgun) {
                    if (M.SwapCooldown > 0) { D.BlockFire = true; D.Why = "box: swapping"; return D; }
                    D.Do = Action::SwapShotgun; D.BlockFire = true; D.Why = "box: swap to shotgun"; return D;
                }
                M.Loop = Step::Peek; continue;
            case Step::Peek:
                if (M.Wait > 0) { D.BlockFire = true; D.Why = "box: re-cover pause"; return D; }
                if (S.CanPeekEdit && M.PeekFails < 2) { D.Do = Action::Peek; D.BlockFire = true; D.Why = "box: peek"; return D; }
                M.Loop = Step::Shoot; M.ShootTime = 0; continue; // open line or enemy wall: shoot now
            case Step::Shoot:
                if (M.ShootTime < T.PeekWindow) { D.Do = Action::Shoot; D.Why = "box: shoot"; return D; }
                M.Loop = Step::ReCover; continue;
            case Step::ReCover:
                if (S.CoverBetween) { M.Loop = Step::Peek; M.Wait = T.ReCoverDelay; D.BlockFire = true; D.Why = "box: covered again"; return D; }
                if (BuildReady || S.Peeking) { D.Do = Action::ReCover; D.BlockFire = true; D.Why = "box: re-cover"; return D; }
                if (!CanBuild) { M.Loop = Step::Shoot; M.ShootTime = 0; D.Do = Action::Shoot; D.Why = "box: no mats, keep shooting"; return D; }
                D.BlockFire = true; D.Why = "box: re-cover cooldown"; return D;
            }
        }
        return D;
    }
    }
    return D;
}
} // namespace Detail

// One decision per call (see DecideNow); remembers fire/weapon ownership for the ticks between
// decisions so a "cover first" decision cannot be undone by the next frame.
inline Decision Decide(Memory& M, const Snapshot& S, const Tuning& T, float DT, float Roll) {
    Decision D = Detail::DecideNow(M, S, T, DT, Roll);
    if (M.SinceDecision == 0) {
        M.HoldFire = D.BlockFire && D.Do != Action::Shoot; M.HoldWeapon = D.OwnsWeapon;
    }
    if (M.S == State::Neutral) { M.HoldFire = false; M.HoldWeapon = false; }
    return D;
}

} // namespace SyndicateBuildFight
