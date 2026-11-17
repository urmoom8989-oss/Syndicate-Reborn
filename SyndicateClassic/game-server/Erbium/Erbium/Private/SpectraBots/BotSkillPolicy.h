#pragma once
#include <string>
#include <algorithm>
namespace SyndicateBotSkill {
struct Profile { float Spread, FireInterval, BuildInterval; int Walls; bool Ramp; };
inline bool MayRecover(bool HumanTeam,bool Storm,bool Fighting,float Quiet){return !HumanTeam && Storm && !Fighting && Quiet>=10.f;}
inline bool Follow(bool Following,double Distance){return Distance>(Following?900.0:2000.0);}
inline int Parse(const std::string& S){if(S=="bot")return 0;if(S=="easy")return 1;if(S=="hard")return 3;if(S=="variety")return 4;return 2;}
inline int Resolve(int Selected,unsigned Seed){return Selected==4?1+int(Seed%3):Selected;}
// Phase 7: realistic 99-player lobby. 15% beginner, 30% average, 30% above-average, 20% advanced, 5% pro.
// Returns the decision tier 0-3; a 4 means "advanced decisions + pro aim/reaction" (the caller sets B.Pro
// and keeps B.Skill at 3, so every build/push/height rule stays identical - a pro is an advanced bot that
// aims and reacts better, never a separate decision fork).
inline int ResolveVariety(unsigned Seed){int R=int(Seed%100);return R<15?0:R<45?1:R<75?2:R<95?3:4;}
inline Profile Get(int Level){switch(Level){case 0:return {5.f,5.f,12.f,1,false};case 1:return {3.f,1.8f,2.f,4,false};case 3:return {.85f,1.f,.55f,5,true};case 4:return {.6f,.92f,.4f,5,true};default:return {1.f,1.f,1.25f,4,true};}}
// Reaction time is independent of weapon rate; harder bots are faster, never instant. Pro (4) is quickest
// but still has a floor - no instant acquisition.
inline float ReactionDelay(int Level,float Roll){
 const float T=std::clamp(Roll,0.f,1.f);
 switch(Level){case 0:return 1.2f+.6f*T;case 1:return .75f+.4f*T;case 3:return .25f+.2f*T;case 4:return .18f+.15f*T;default:return .42f+.33f*T;}
}
// ---- Aim: chance that one shot is aimed onto the target ----
// Levels: 0 bot, 1 easy, 2 medium, 3 hard. The shot code rolls this per shot (per shotgun blast)
// and puts the shot on the body on success, otherwise 70-160 units off to the side or over the head,
// so misses look like real near-misses. Walls and other actors still block through the normal trace.
// Medium: human-like (about 45% at mid range, fewer while moving, hurt or shot at, and on the first
// shots at a new target). Hard: most shots land at close/mid range and it tracks quickly, but long
// range, fast strafing and pressure still cost it.
enum class AimWeapon { Automatic, Shotgun, Rifle /*AR, DMR, burst*/, Sniper, Pistol };
struct AimContext {
 double Distance = 1000;   // flat distance to the target
 double TargetSpeed = 0;   // target's horizontal speed (u/s)
 double OwnSpeed = 0;      // the bot's own horizontal speed
 bool UnderPressure = false; // being shot at, or low health
 float TrackTime = 0;      // seconds on this target with sight
 bool TargetAirborne = false; // target jumping / falling
 bool TargetJuked = false;    // target changed direction sharply in the last moment
};
inline float BaseHitChance(int Level,AimWeapon W,double Distance){
 // [level][band]: <500, <1500, <3000, beyond
 // Rows 0-3 are beginner..advanced; row 4 is pro: excellent but never perfect (final HitChance is still
 // capped at .92, so even a pro misses at close range sometimes).
 static const float Table[5][4]={{.22f,.14f,.08f,.04f},{.38f,.28f,.17f,.08f},{.60f,.46f,.32f,.18f},{.70f,.56f,.40f,.25f},{.80f,.66f,.50f,.34f}};
 const int L=std::clamp(Level,0,4);
 if(W==AimWeapon::Sniper){static const float S[5]={.08f,.18f,.35f,.48f,.58f};return S[L];}
 const int Band=Distance<500?0:Distance<1500?1:Distance<3000?2:3;
 float C=Table[L][Band];
 if(W==AimWeapon::Shotgun && Band>=2)C*=.6f;   // shotguns are not long-range weapons
 if(W==AimWeapon::Pistol)C*=.9f;
 if(W==AimWeapon::Rifle && Band>=1)C*=1.08f;    // ARs are steadier than SMGs at range
 return C;
}
inline float HitChance(int Level,AimWeapon W,const AimContext& C){
 const int L=std::clamp(Level,0,4);
 float P=BaseHitChance(L,W,C.Distance);
 const float Strafe=float((std::min)(1.0,C.TargetSpeed/900.0)),Run=float((std::min)(1.0,C.OwnSpeed/600.0));
 // How much each disadvantage costs: lower tiers lose more. Row 4 (pro) loses the least, but still loses -
 // a strafing, jumping or juking target costs even a pro, so there is no perfect tracking.
 static const float StrafeCost[5]={.45f,.40f,.32f,.25f,.20f},RunCost[5]={.35f,.30f,.22f,.10f,.08f},PressureCost[5]={.30f,.25f,.20f,.12f,.09f};
 static const float AirCost[5]={.50f,.45f,.38f,.30f,.24f},JukeCost[5]={.45f,.40f,.32f,.25f,.20f};
 P*=1.f-StrafeCost[L]*Strafe;
 P*=1.f-RunCost[L]*Run;
 if(C.UnderPressure)P*=1.f-PressureCost[L];
 // Jumping or sharply changing direction breaks tracking at every tier (no perfect re-acquisition).
 if(C.TargetAirborne)P*=1.f-AirCost[L];
 if(C.TargetJuked)P*=1.f-JukeCost[L];
 // Acquiring a new target: the first shots are worse until the bot has tracked it for a moment.
 static const float Settle[5]={2.f,1.6f,1.2f,.9f,.75f},FirstShot[5]={.4f,.5f,.6f,.65f,.72f};
 const float T=std::clamp(C.TrackTime/Settle[L],0.f,1.f);
 P*=FirstShot[L]+(1.f-FirstShot[L])*T;
 return std::clamp(P,.02f,.92f);
}
// A burst must not bypass the selected difficulty's firing cadence.
// Medium uses ramps to answer elevation or incoming fire; Hard also pushes open ground.
inline bool WantsRamp(int Level,double HeightDelta,bool UnderFire){return Level==3 || (Level==2 && (HeightDelta>150.0 || UnderFire));}
inline float BurstGap(int Level){return Level==0?2.5f:.16f*Get(Level).FireInterval;}

// Keep low tiers simple. Capable tiers position for the equipped weapon instead
// of closing to the same distance with a shotgun, rifle and sniper.
inline float CombatForward(int Level,double Distance,float WeaponRange){
 if(Level<2)return Distance>1800?.7f:Distance<600?-.45f:.15f;
 double Near=900,Far=1600;
 if(WeaponRange>=20000){Near=3500;Far=6500;}
 else if(WeaponRange>=10000){Near=1200;Far=2400;}
 else if(WeaponRange<=1800){Near=450;Far=900;}
 return Distance>Far?.7f:Distance<Near?-.45f:.05f;
}

}
