// Bot aim model (BotSkillPolicy.h): tier ordering and the effect of distance, movement and pressure.
// cl /EHsc /std:c++17 aim-test.cpp /Feaim-test.exe   (or g++ -std=c++17 aim-test.cpp)
#include "../Erbium/Erbium/Private/SpectraBots/BotSkillPolicy.h"
#include <cstdio>
using namespace SyndicateBotSkill;
static int Fails=0;
#define CHECK(X) do{if(!(X)){printf("FAIL %s (line %d)\n",#X,__LINE__);++Fails;}}while(0)
int main(){
 AimContext Mid;Mid.Distance=1000;Mid.TrackTime=5;
 const float E=HitChance(1,AimWeapon::Automatic,Mid),M=HitChance(2,AimWeapon::Automatic,Mid),H=HitChance(3,AimWeapon::Automatic,Mid);
 printf("mid-range automatic: easy %.2f medium %.2f hard %.2f\n",E,M,H);
 CHECK(E<M && M<H);
 CHECK(M>=.35f && M<=.55f);          // medium: average human
 CHECK(H>=.50f && H<=.62f);          // hard: still the best tier, clearly not a laser (lowered 2026-10-29)
 AimContext Far=Mid;Far.Distance=4000;CHECK(HitChance(3,AimWeapon::Automatic,Far)<H);
 AimContext Strafe=Mid;Strafe.TargetSpeed=900;
 CHECK(HitChance(2,AimWeapon::Automatic,Strafe)<M*.75f);
 CHECK(HitChance(3,AimWeapon::Automatic,Strafe)<H*.8f && HitChance(3,AimWeapon::Automatic,Strafe)>H*.7f);
 AimContext Jump=Mid;Jump.TargetAirborne=true;Jump.TargetJuked=true;
 CHECK(HitChance(3,AimWeapon::Automatic,Jump)<H*.6f); // jumping + direction change: hard misses much more
 CHECK(HitChance(2,AimWeapon::Automatic,Jump)<HitChance(3,AimWeapon::Automatic,Jump));
 AimContext Pressure=Mid;Pressure.UnderPressure=true;Pressure.OwnSpeed=600;
 CHECK(HitChance(2,AimWeapon::Automatic,Pressure)<.30f); // medium misses under pressure
 AimContext Fresh=Mid;Fresh.TrackTime=0;
 CHECK(HitChance(2,AimWeapon::Automatic,Fresh)<M && HitChance(3,AimWeapon::Automatic,Fresh)>HitChance(2,AimWeapon::Automatic,Fresh));
 AimContext Close=Mid;Close.Distance=300;
 CHECK(HitChance(3,AimWeapon::Shotgun,Close)>HitChance(2,AimWeapon::Shotgun,Close));
 CHECK(HitChance(3,AimWeapon::Shotgun,Far)<HitChance(3,AimWeapon::Automatic,Far));
 for(int L=0;L<4;++L)for(double D:{100.,800.,2000.,6000.}){AimContext C;C.Distance=D;C.TargetSpeed=1200;C.OwnSpeed=800;C.UnderPressure=true;
   float P=HitChance(L,AimWeapon::Sniper,C);CHECK(P>=.02f && P<=.92f);}
 printf(Fails?"%d FAILED\n":"aim tests passed\n",Fails);
 return Fails?1:0;
}
