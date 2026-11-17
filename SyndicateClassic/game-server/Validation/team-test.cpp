#include <cassert>
#include <map>
#include <iostream>
// Each population fills its own squad; team IDs come from one shared sequence.
struct SyndicateTeamAllocator
{
    int NextTeam=3;
    int HumanTeam=0, BotTeam=0;
    int HumanCount=0, BotCount=0;
    int Pick(bool IsBot,int SquadSize)
    {
        if(SquadSize<1)SquadSize=1;
        int& Team=IsBot?BotTeam:HumanTeam;
        int& Count=IsBot?BotCount:HumanCount;
        if(!Team || Count>=SquadSize){Team=NextTeam++;Count=0;}
        ++Count;
        return Team;
    }
};


int main(){for(int Size:{1,2,4})for(bool BotFirst:{false,true}){SyndicateTeamAllocator Teams;std::map<int,bool> Owners;std::map<int,int> Counts;for(int I=0;I<100;++I){bool Bot=(I%5!=0)^BotFirst;int Team=Teams.Pick(Bot,Size);assert(Team>=3 && Team<255);if(Owners.count(Team))assert(Owners[Team]==Bot);Owners[Team]=Bot;assert(++Counts[Team]<=Size);}}SyndicateTeamAllocator T;int Human=T.Pick(false,4);int Bot=T.Pick(true,4);assert(Human!=Bot);for(int I=0;I<3;++I)assert(T.Pick(true,4)==Bot);assert(T.Pick(false,4)==Human);assert(T.Pick(true,4)!=Bot);std::cout<<"600 interleaved human/bot assignments: no mixed teams; solo/duo/squad capacities and partial squads passed\n";}
