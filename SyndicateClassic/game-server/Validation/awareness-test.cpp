// Bot awareness (BotAwareness.h): hearing gunfire, near misses, reactions and the "go and check" walk.
// cl /EHsc /std:c++17 awareness-test.cpp /Feawareness-test.exe   (or g++ -std=c++17 awareness-test.cpp)
#include "../Erbium/Erbium/Private/SpectraBots/BotAwareness.h"
#include <cstdio>
using namespace SyndicateAwareness;
static int Fails=0;
#define CHECK(X) do{if(!(X)){printf("FAIL %s (line %d)\n",#X,__LINE__);++Fails;}}while(0)
int main(){
 // Near misses: a shot along +X passes the bot 100 units to the side -> noticed; one fired the other way -> not.
 const Vec Shooter{0,0,0},Bot{1500,100,0};
 CHECK(NearMiss(MissDistance(Bot,Shooter,{1,0,0},20000)));
 CHECK(MissDistance(Bot,Shooter,{-1,0,0},20000)<0);                 // fired away from the bot
 CHECK(MissDistance(Bot,Shooter,{1,0,0},800)<0);                    // shot ended well before the bot
 CHECK(MissDistance({1500,600,0},Shooter,{1,0,0},20000)>NearMissRadius); // 6 m off the line: not a near miss
 CHECK(MissDistance(Bot,Shooter,{0,0,0},20000)<0);                  // no direction
 // Hearing: louder guns carry farther, better bots hear farther, never beyond the gun's own range.
 CHECK(HearingRange(2,Weapon::Sniper)>HearingRange(2,Weapon::Smg));
 CHECK(HearingRange(0,Weapon::Rifle)<HearingRange(3,Weapon::Rifle));
 CHECK(HearingRange(3,Weapon::Pistol)<=ShotLoudness(Weapon::Pistol));
 // Reactions.
 Situation S;S.Armed=true;S.Distance=1200;S.Hearing=3500;S.KitReady=true;S.Roll=0;
 CHECK(React(S)==Reaction::Investigate);                            // close, armed, kitted: go and check
 S.Roll=.999f;CHECK(React(S)==Reaction::TurnToFace);                // the roll said stay: still turns toward it
 S.Roll=0;S.Distance=5000;CHECK(React(S)==Reaction::None);          // out of hearing
 S.Distance=1200;S.SourceVisible=true;CHECK(React(S)==Reaction::Engage);
 S.SourceVisible=false;S.Fighting=true;CHECK(React(S)==Reaction::None); // busy fighting someone in sight
 S.Fighting=false;S.Armed=false;CHECK(React(S)==Reaction::None);    // unarmed: own hide logic
 S.Armed=true;S.LowHealth=true;CHECK(React(S)==Reaction::TurnToFace); // hurt: never walks toward gunfire
 S.LowHealth=false;S.Incoming=true;CHECK(React(S)==Reaction::TurnToFace); // shot at from out of sight
 S.SourceVisible=true;CHECK(React(S)==Reaction::Engage);
 S.Fighting=true;S.SourceVisible=false;CHECK(React(S)==Reaction::TurnToFace); // shot at always reacts, even mid-fight
 // Styles: an aggressive bot checks out shots more often than a passive one.
 {int Aggro=0,Passive=0;for(int I=0;I<1000;++I){Situation T;T.Armed=true;T.KitReady=true;T.Distance=2500;T.Hearing=3500;T.Roll=I/1000.f;
   T.Press=1.6f;Aggro+=React(T)==Reaction::Investigate;T.Press=.45f;Passive+=React(T)==Reaction::Investigate;}
  printf("investigate at 25 m: aggressive %d/1000 passive %d/1000\n",Aggro,Passive);CHECK(Aggro>Passive*2);CHECK(Passive>0);}
 // Alert reactions are quicker but never instant.
 CHECK(AlertedDelay(.5f,true)<.5f);CHECK(AlertedDelay(.5f,false)==.5f);CHECK(AlertedDelay(.1f,true)>=.12f);
 // Investigation: one walk at a time, refreshed by nearby shots, rest afterwards, gives up when stalled.
 Investigation I;CHECK(I.Offer({100,0,0}));CHECK(!I.Offer({900,0,0}));CHECK(I.Refresh({600,0,0}) && I.Point.X==600);
 CHECK(!I.Refresh({9000,0,0}));
 I.Tick(1.f);CHECK(I.Active() && I.Elapsed>.9f);
 CHECK(!GiveUp(3.5f,1.f));CHECK(GiveUp(3.5f,2.f));CHECK(GiveUp(0,12.f));CHECK(!GiveUp(1.f,5.f));
 I.Clear(6.f);CHECK(!I.Active() && !I.Offer({0,0,0}));I.Tick(6.1f);CHECK(I.Offer({0,0,0}));
 CHECK(Arrived({0,0,0},{200,0,100}));CHECK(!Arrived({0,0,0},{200,0,600}));CHECK(!Arrived({0,0,0},{500,0,0}));
 if(Fails){printf("%d FAILED\n",Fails);return 1;}
 printf("awareness tests passed\n");return 0;
}
