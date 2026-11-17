#include <algorithm>
#include <cassert>
#include <iostream>
    inline bool AdvanceBotRecovery(float& Elapsed,float& Quiet,float DT,bool Busy)
    {
        DT=(std::max)(0.f,DT);
        Elapsed=(std::min)(60.f,Elapsed+DT);
        Quiet=Busy?0.f:Quiet+DT;
        if(Elapsed<60.f || Quiet<8.f)return false;
        Elapsed=0;Quiet=0;return true;
    }
    inline int BotEliminationMaterials(int Zone)
    {
        return 50+50*(std::max)(0,(std::min)(4,Zone));
    }

int main(){float E=0,Q=0;for(int I=0;I<59;++I)assert(!AdvanceBotRecovery(E,Q,1,false));assert(AdvanceBotRecovery(E,Q,1,false));assert(E==0 && Q==0);for(int I=0;I<90;++I)assert(!AdvanceBotRecovery(E,Q,1,true));assert(E==60 && Q==0);for(int I=0;I<7;++I)assert(!AdvanceBotRecovery(E,Q,1,false));assert(AdvanceBotRecovery(E,Q,1,false));assert(E==0);E=59;Q=7;assert(!AdvanceBotRecovery(E,Q,1,true));assert(E==60 && Q==0);assert(!AdvanceBotRecovery(E,Q,7,false));assert(AdvanceBotRecovery(E,Q,1,false));for(int Z=0;Z<=4;++Z)assert(BotEliminationMaterials(Z)==50+50*Z);std::cout<<"Recovery minute cadence, busy deferral, 8-second combat grace and material progression passed\n";}
