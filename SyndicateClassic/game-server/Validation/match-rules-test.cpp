#include <vector>
#include <cassert>
#include <cstdio>
struct AFortPlayerStateAthena { int TeamIndex; };
struct AFortPlayerControllerAthena { AFortPlayerStateAthena* PlayerState; };
struct Roster : std::vector<AFortPlayerControllerAthena*> { using std::vector<AFortPlayerControllerAthena*>::operator=; int Num()const{return (int)size();} };
struct ModeType { Roster AlivePlayers; };
struct Bot { bool Eliminated; AFortPlayerStateAthena* State; };
bool winner(ModeType* Mode, std::vector<Bot> Bots) {
        int SurvivingTeam = -1; bool MultipleTeams = false;
        auto CountTeam = [&](AFortPlayerStateAthena* State) {
            if (!State) { MultipleTeams = true; return; }
            const int Team = State->TeamIndex;
            if (SurvivingTeam == -1) SurvivingTeam = Team;
            else if (SurvivingTeam != Team) MultipleTeams = true;
        };
        for (int I = 0; I < Mode->AlivePlayers.Num(); ++I) {
            auto PC = (AFortPlayerControllerAthena*)Mode->AlivePlayers[I];
            CountTeam(PC ? (AFortPlayerStateAthena*)PC->PlayerState : nullptr);
        }
        // The bot ledger also guards against incomplete native roster entries.
        for (auto& Other : Bots) if (!Other.Eliminated) CountTeam(Other.State);

return Mode->AlivePlayers.Num()>0 && SurvivingTeam>=0 && !MultipleTeams;
}
int main(){
AFortPlayerStateAthena a{3}, b{4}; AFortPlayerControllerAthena human{&a}, ally{&a}, enemy{&b};
ModeType m; m.AlivePlayers={&human};
std::vector<Bot> bots(98,{false,&b}); bots.push_back({true,&b});
assert(!winner(&m,bots)); // first finish, many bots omitted from native roster
assert(!winner(&m,{{false,&b}})); // downed enemy still alive
assert(winner(&m,{{true,&b}})); // final enemy eliminated
m.AlivePlayers={&human,&ally}; assert(winner(&m,{{true,&b}})); // squad victory
m.AlivePlayers.push_back(&enemy); assert(!winner(&m,{}));
m.AlivePlayers.clear(); assert(!winner(&m,{}));
m.AlivePlayers={&human}; assert(!winner(&m,{{false,nullptr}}));
m.AlivePlayers={&ally,&enemy}; assert(!winner(&m,{{false,&a},{false,&b}})); // human gone, two bot teams still alive
 m.AlivePlayers={&ally}; assert(winner(&m,{{false,&a},{true,&b}})); // human gone, one bot team
 puts("9 roster victory regression checks passed");
}
