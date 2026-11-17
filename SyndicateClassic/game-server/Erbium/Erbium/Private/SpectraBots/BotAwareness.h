#pragma once
// Syndicate bot awareness: hearing gunfire, noticing shots that pass close by or hit from someone the bot
// cannot see, and walking over to check where shooting was heard. Ported from Titanium's ReactToCues /
// Investigate / Hear (ServerAndConsole/PlayerBots.h) into the Erbium bots.
//
// Pure logic with no engine types, so it can be tested on its own (Validation/awareness-test.cpp).
// SpectraBotsIntegration.h records every gunshot (bots in UpdateCombat, players in SampleHumanFire) and asks
// these helpers what each bot should do about the shots around it.
//
// Fairness: a bot only learns where a shot was fired FROM (the muzzle at the moment of the shot), the way a
// player hears a gunshot or sees the hit direction. Shooting back still needs a real line of sight (the
// combat code traces every shot), so hearing never lets a bot shoot through walls.
#include <algorithm>
#include <cmath>

namespace SyndicateAwareness {

struct Vec { double X = 0, Y = 0, Z = 0; };

enum class Weapon : unsigned char { Unknown, Pistol, Smg, Rifle, Shotgun, Sniper, Explosive };

// How far a gunshot carries (UE units, 100 = 1 m), roughly what a player can hear in a match.
inline double ShotLoudness(Weapon W) {
    switch (W) {
    case Weapon::Pistol: return 3000; case Weapon::Smg: return 3200; case Weapon::Shotgun: return 3600;
    case Weapon::Rifle: return 4200; case Weapon::Sniper: return 7000; case Weapon::Explosive: return 6000;
    default: return 3500;
    }
}
// Tier 0..4 (beginner, easy, medium, hard, pro): how well a bot picks shots out of a busy match.
inline double HearingScale(int Tier) { static const double S[5] = { .55, .75, .9, 1.0, 1.1 }; return S[std::clamp(Tier, 0, 4)]; }
inline double HearingRange(int Tier, Weapon W) { return ShotLoudness(W) * HearingScale(Tier); }

// A shot passing this close (units) is noticed as "shot at": the crack of the bullet, the impact next to you.
constexpr double NearMissRadius = 260;

// Closest distance from point P (the bot's chest) to the shot segment Start -> Start + Dir * Length.
// Dir does not need to be normalized. Returns -1 when the bot is behind the shooter or past the shot's end,
// so a shot fired away from the bot never counts.
inline double MissDistance(const Vec& P, const Vec& Start, const Vec& Dir, double Length) {
    const double L = std::sqrt(Dir.X * Dir.X + Dir.Y * Dir.Y + Dir.Z * Dir.Z);
    if (!(L > 1e-6) || !(Length > 0)) return -1;
    const double UX = Dir.X / L, UY = Dir.Y / L, UZ = Dir.Z / L;
    const double WX = P.X - Start.X, WY = P.Y - Start.Y, WZ = P.Z - Start.Z;
    const double T = WX * UX + WY * UY + WZ * UZ;
    if (T < 0 || T > Length + NearMissRadius) return -1;
    const double CX = WX - UX * T, CY = WY - UY * T, CZ = WZ - UZ * T;
    return std::sqrt(CX * CX + CY * CY + CZ * CZ);
}
inline bool NearMiss(double Miss) { return Miss >= 0 && Miss <= NearMissRadius; }
inline double Flat(const Vec& A, const Vec& B) { const double X = A.X - B.X, Y = A.Y - B.Y; return std::sqrt(X * X + Y * Y); }

enum class Reaction : unsigned char {
    None,        // not heard, or busy with something more important
    Engage,      // the shooter is in sight: make it the target now
    TurnToFace,  // shot at by someone out of sight (or heard but staying put): face it, take cover, get ready
    Investigate, // heard close by: go to where the shots came from
};
inline const char* ReactionName(Reaction R) {
    switch (R) { case Reaction::Engage: return "engage"; case Reaction::TurnToFace: return "turn-to-face"; case Reaction::Investigate: return "investigate"; default: return "none"; }
}

struct Situation {
    bool Incoming = false;      // the shot passed within NearMissRadius, or the bot just lost health to it
    bool SourceVisible = false; // the bot can see the shooter right now
    double Distance = 0;        // flat distance from the bot to where the shot was fired
    double Hearing = 3500;      // HearingRange for this bot and weapon
    bool Armed = false;         // the bot holds a gun
    bool Fighting = false;      // already fighting an opponent it can see
    bool Busy = false;          // healing, reviving, in the storm, urgent rotation, regrouping
    bool LowHealth = false;     // health + shield low: no walking toward gunfire
    bool KitReady = false;      // has the guns it wants (looting phase done)
    float Press = 1.f;          // playstyle: how readily it presses fights (aggressive > 1, passive < 1)
    float Roll = 0;             // uniform random [0,1)
};

// What one bot does about one gunshot.
inline Reaction React(const Situation& S) {
    // Being shot at always gets a reaction, even mid-fight with someone else.
    if (S.Incoming) return S.SourceVisible && S.Armed ? Reaction::Engage : Reaction::TurnToFace;
    if (!(S.Distance <= S.Hearing)) return Reaction::None;
    if (S.Fighting) return Reaction::None; // heard shots elsewhere do not steal focus from a fight in sight
    if (S.SourceVisible && S.Armed) return Reaction::Engage;
    if (!S.Armed) return Reaction::None;   // unarmed bots keep their own hide-and-loot logic
    if (S.Busy || S.LowHealth) return Reaction::TurnToFace;
    // Go and check: closer shots and aggressive styles are likelier; a bot still looting for its kit goes less often.
    const double Near = 1.0 - std::clamp(S.Distance / (std::max)(1.0, S.Hearing), 0.0, 1.0);
    double Chance = std::clamp((.35 + .5 * Near) * double(S.Press), .1, .95);
    if (!S.KitReady) Chance *= .5;
    return S.Roll < Chance ? Reaction::Investigate : Reaction::TurnToFace;
}

// Reaction time once the bot is alert (it heard shooting or was shot at in the last few seconds): a player who is
// already looking for someone is quicker to fire. Never instant.
inline float AlertedDelay(float Delay, bool Alerted) { return Alerted ? (std::max)(.12f, Delay * .6f) : Delay; }
// Seconds a bot stays alert after hearing shots / being shot at.
inline float AlertTime(bool Incoming) { return Incoming ? 5.f : 3.f; }

// "Go and check": one point, a time limit, and a rest afterwards so a busy area does not pull the bot back and forth.
struct Investigation {
    Vec Point{}; float Left = 0, Cooldown = 0, Elapsed = 0;
    bool Active() const { return Left > 0; }
    bool Offer(const Vec& P, float Time = 12.f) {
        if (Active() || Cooldown > 0) return false;
        Point = P; Left = Time; Elapsed = 0; return true;
    }
    // A fresh shot from (about) the same fight keeps the walk going toward the newest position.
    bool Refresh(const Vec& P) {
        if (!Active() || Flat(P, Point) > 2000) return false;
        Point = P; Left = (std::max)(Left, 5.f); return true;
    }
    void Clear(float Rest = 6.f) { Left = 0; Cooldown = Rest; Elapsed = 0; }
    void Tick(float DT) { if (Left > 0) Elapsed += DT; Left = (std::max)(0.f, Left - DT); Cooldown = (std::max)(0.f, Cooldown - DT); }
};
// Close enough to look around the spot.
inline bool Arrived(const Vec& Bot, const Vec& Goal) { return Flat(Bot, Goal) < 300 && std::fabs(Bot.Z - Goal.Z) < 350; }
// Stalled on the way (no progress for 3 s once it has been walking a moment) or taking too long: give up instead of
// walking into the same wall.
inline bool GiveUp(float NoProgress, float Elapsed) { return (Elapsed >= 1.5f && NoProgress >= 3.f) || Elapsed >= 12.f; }

} // namespace SyndicateAwareness
