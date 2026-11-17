// Copy of the Fill / No Fill rules in FortGameMode.cpp PickTeam (shared-counter seasons: 3.5, 5.10-10.31,
// plus the fill pre-check every season but 4.x). Keep in sync. g++ -std=c++17 fill-pick-test.cpp
#include <cstdio>
#include <map>
#include <set>
#include <functional>
static int Fails=0;
#define CHECK(X) do{if(!(X)){printf("FAIL %s (line %d)\n",#X,__LINE__);++Fails;}}while(0)
struct Picker{
 int CurrentTeam=3,PlayersOnCurTeam=0;std::map<int,int> TeamMembers;std::set<int> HumanTeams;
 std::function<bool(int)> WantsFill;
 int Pick(bool FillIsBot,int FillSize){
  if(FillSize>1 && FillIsBot){for(int Team:HumanTeams)if(TeamMembers[Team]<FillSize && WantsFill(Team)){++TeamMembers[Team];return Team;}}
  int ret=CurrentTeam;
  auto Fresh=[&](){while(HumanTeams.count(CurrentTeam) || TeamMembers[CurrentTeam]>0)CurrentTeam++;PlayersOnCurTeam=0;};
  if(FillIsBot){if(HumanTeams.count(CurrentTeam))Fresh();}
  else{for(int Team:HumanTeams)if(TeamMembers[Team]<FillSize){++TeamMembers[Team];return Team;}
   if(TeamMembers[CurrentTeam]>0)Fresh();}
  ret=CurrentTeam;++TeamMembers[ret];if(!FillIsBot)HumanTeams.insert(ret);
  if(FillIsBot && ++PlayersOnCurTeam>=FillSize){CurrentTeam++;PlayersOnCurTeam=0;}
  else if(!FillIsBot && TeamMembers[ret]>=FillSize){CurrentTeam++;PlayersOnCurTeam=0;}
  return ret;}
};
int main(){
 for(int Size:{2,4})for(bool Fill:{false,true}){
  Picker P;P.WantsFill=[&](int){return Fill;};
  int Human=P.Pick(false,Size);int OnHuman=0;std::map<int,int> Count;
  for(int I=0;I<40;++I){int T=P.Pick(true,Size);++Count[T];if(T==Human)++OnHuman;}
  CHECK(OnHuman==(Fill?Size-1:0));
  for(auto& [T,N]:Count)CHECK(N<=Size);
  printf("size %d fill %d: %d bots on the player's team\n",Size,int(Fill),OnHuman);
 }
 { // second player (party) joins after bots spawned: same team as the first player
  Picker P;P.WantsFill=[](int){return false;};
  int A=P.Pick(false,4);for(int I=0;I<5;++I)P.Pick(true,4);int B=P.Pick(false,4);
  CHECK(A==B);
  int C=P.Pick(false,2+2);CHECK(C==A); // up to squad size
  int D=P.Pick(false,4);int E=P.Pick(false,4);CHECK(D==A);CHECK(E!=A); // 5th player starts a new team
 }
 { // duo, Fill: player A gets a fill bot, then player B arrives -> new team (no 3-player duo)
  Picker P;P.WantsFill=[](int){return true;};
  int A=P.Pick(false,2);int Bt=P.Pick(true,2);CHECK(Bt==A);int B=P.Pick(false,2);CHECK(B!=A);CHECK(P.TeamMembers[A]==2);
 }
 { // Fill with a partner: duo of two humans gets no bot; squad of two humans gets two bots
  Picker P;P.WantsFill=[](int){return true;};
  int A=P.Pick(false,4);P.Pick(false,4);int On=0;for(int I=0;I<10;++I)if(P.Pick(true,4)==A)++On;CHECK(On==2);
 }
 printf(Fails?"%d FAILED\n":"fill pick tests passed\n",Fails);return Fails?1:0;
}
