#include <cassert>
#include <iostream>
#include <vector>
using uint8=unsigned char;
struct AFortPlayerStateAthena {int TeamIndex=3;};
struct Pawn {bool bActorIsBeingDestroyed=false; bool Downed=false; AFortPlayerStateAthena* PlayerState; int Writes=0; bool Allowed=false; bool IsDBNO(){return Downed;} int GetFunction(const char*){return 1;} template<class T> void Call(int,bool Value){++Writes;Allowed=Value;}};
bool KnockEnabled=true; bool TeamKnocksEnabled(){return KnockEnabled;}
    inline bool ShouldUseTeamKnock(bool Enabled,bool Downed,int StandingTeammatesIncludingSelf)
    {
        return Enabled && StandingTeammatesIncludingSelf>(Downed?0:1);
    }


void Poll(std::vector<Pawn*> Pawns,int (&Standing)[256]) {
        const bool Enabled=TeamKnocksEnabled();
        for(auto P:Pawns){
            // Eligibility only controls the next knock. A transient roster snapshot must
            // never disable DBNO or force death on a pawn that is already crawling.
            if(P->bActorIsBeingDestroyed || P->IsDBNO())continue;
            auto State=(AFortPlayerStateAthena*)P->PlayerState;
            bool Allowed=ShouldUseTeamKnock(Enabled,false,Standing[uint8(State->TeamIndex)]);
            if(auto Set=P->GetFunction("SetUsesDBNO"))P->Call<void>(Set,Allowed);
        }
}
int main(){AFortPlayerStateAthena S; int Standing[256]{}; Pawn P; P.PlayerState=&S;
for(bool Enabled:{false,true})for(int Count:{0,1,4}){KnockEnabled=Enabled;Standing[3]=Count;P.Downed=true;P.Writes=0;for(int I=0;I<120;++I)Poll({&P},Standing);assert(P.Writes==0);}
KnockEnabled=true;P.Downed=false;Standing[3]=1;Poll({&P},Standing);assert(!P.Allowed);Standing[3]=4;Poll({&P},Standing);assert(P.Allowed);
P.Downed=true;int Writes=P.Writes;Standing[3]=0;Poll({&P},Standing);assert(P.Writes==Writes && P.Allowed);
P.Downed=false;Poll({&P},Standing);assert(!P.Allowed);
std::cout<<"Production eligibility loop: knocked pawns preserved across 720 polls; solo, squad, knock and revive transitions passed\n";}
