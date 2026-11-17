#include <algorithm>
#include <cassert>
#include <iostream>
#include <unordered_map>
using uint32=unsigned int;
struct Gun { int Ammo=0; uint32 GetOffset(const char*) {return 0;} void ForceNetUpdate() {} };
struct Def {int AmmoId=1;int* GetAmmoWorldItemDefinition_BP(){return &AmmoId;}};
struct Pawn {Gun* CurrentWeapon=nullptr;bool bActorIsBeingDestroyed=false;float Health=100;float GetHealth(){return Health;}int K2_GetActorLocation(){return 0;}};
struct Ref {Pawn* Value=nullptr;Pawn* Get(){return Value;}};
struct Bot {Pawn* Pawn;Def* LootedWeapon;std::unordered_map<int*,int> AmmoReserves;float ReloadRemaining=0;int BurstShot=0;Ref Enemy;float CombatSense=1;int LootedAmmo=30;bool HasGun=true;};
struct Stats {float Reload=.4f;int ClipSize=30;};
Stats stats;
Stats WeaponStats(Def*){return stats;}
struct AFortInventory {static Stats* GetStats(Def*){return &stats;}};
template<class T>T& GetFromOffset(Gun* g,uint32){return reinterpret_cast<T&>(g->Ammo);}
void SyncBotInventory(Bot&){} void StopBotFire(Bot&){} bool EquipStockedInventoryGun(Bot&){return false;} void CombatMovement(Bot&,int,int,float){} void AppendBotLog(const char*){}
bool Step(Bot& B,float DT){
        auto Equipped=B.Pawn->CurrentWeapon;auto AmmoOff=Equipped->GetOffset("AmmoCount");
        if(AmmoOff!=uint32(-1) && GetFromOffset<int>(Equipped,AmmoOff)<=0) {
            auto AmmoDef=B.LootedWeapon?B.LootedWeapon->GetAmmoWorldItemDefinition_BP():nullptr;
            auto& Reserve=B.AmmoReserves[AmmoDef];
            if(!AmmoDef || Reserve<=0){SyncBotInventory(B);B.HasGun=false;StopBotFire(B);return EquipStockedInventoryGun(B);}
            if(B.ReloadRemaining<=0)B.ReloadRemaining=WeaponStats(B.LootedWeapon).Reload;
            B.ReloadRemaining-=DT;StopBotFire(B);B.BurstShot=0;
            auto ReloadEnemy=B.Enemy.Get();
            if(ReloadEnemy && !ReloadEnemy->bActorIsBeingDestroyed && ReloadEnemy->GetHealth()>0)
                CombatMovement(B,B.Pawn->K2_GetActorLocation(),ReloadEnemy->K2_GetActorLocation(),DT);
            else B.CombatSense=0; // Losing a target must not prevent the magazine refill.
            if(B.ReloadRemaining<=0) {
                auto Stats=AFortInventory::GetStats(B.LootedWeapon);
                int Clip=Stats?(int)Stats->ClipSize:B.LootedAmmo;
                int Loaded=(std::min)(Reserve,(std::max)(1,Clip));Reserve-=Loaded;
                GetFromOffset<int>(Equipped,AmmoOff)=Loaded;Equipped->ForceNetUpdate();
                AppendBotLog("loot: reloaded using collected ammunition\n");
            }
            return true;
        }

return false;}
int main(){
Gun gun;Pawn pawn{&gun};Pawn target;Def def;Bot bot{&pawn,&def};bot.AmmoReserves[&def.AmmoId]=50;
// Target disappears during reload: completion must still transfer ammo once.
bot.Enemy.Value=&target;Step(bot,.2f);bot.Enemy.Value=nullptr;Step(bot,.25f);assert(gun.Ammo==30);assert(bot.AmmoReserves[&def.AmmoId]==20);
// A destroyed target cannot trap the bot in perpetual reload.
gun.Ammo=0;bot.ReloadRemaining=0;bot.Enemy.Value=&target;target.bActorIsBeingDestroyed=true;Step(bot,.5f);assert(gun.Ammo==20);assert(bot.AmmoReserves[&def.AmmoId]==0);
// No reserve ammo creates none, and selects another inventory weapon.
gun.Ammo=0;bot.ReloadRemaining=0;Step(bot,.5f);assert(gun.Ammo==0);assert(!bot.HasGun);
// Existing magazine is untouched.
gun.Ammo=7;Step(bot,1.f);assert(gun.Ammo==7);
std::cout<<"4 production reload regression cases passed\n";
}
