#include <cassert>
#include <cstring>
#include <iostream>
    // Copy of SpectraBotsIntegration.h MatchRestartReason (keep in sync).
    inline const char* MatchRestartReason(bool HadPlayer, int ConnectedPlayers, bool MatchEnded, bool HadAlive=false, int HumansAlive=-1)
    {
        if(!HadPlayer)return nullptr;
        if(ConnectedPlayers==0)return "left";
        if(MatchEnded)return HumansAlive>0?"win":"eliminated";
        if(HadAlive && HumansAlive==0)return "eliminated";
        return nullptr;
    }

static bool Is(const char* R,const char* W){return R && std::strcmp(R,W)==0;}
int main(){
    assert(!MatchRestartReason(false,0,false));assert(!MatchRestartReason(false,0,true));
    assert(!MatchRestartReason(true,1,false));assert(!MatchRestartReason(true,4,false));
    assert(Is(MatchRestartReason(true,0,false),"left"));
    assert(Is(MatchRestartReason(true,1,true,true,1),"win"));
    assert(Is(MatchRestartReason(true,4,true,true,2),"win"));
    assert(Is(MatchRestartReason(true,1,true,true,0),"eliminated"));
    // Every real player eliminated while bots fight on: recycle.
    assert(Is(MatchRestartReason(true,1,false,true,0),"eliminated"));
    // Never seen alive after the bus (warm-up island / build without an alive list): keep it.
    assert(!MatchRestartReason(true,1,false,false,0));
    // A real player (or a knocked one) is still alive: keep it.
    assert(!MatchRestartReason(true,2,false,true,1));
    std::cout<<"11 match lifecycle cases passed\n";
}
