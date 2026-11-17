#include <cassert>
#include <iostream>
    inline bool NeedsSquadRegroup(bool CircleReady,bool HasLeader,bool WasRegrouping,double Distance)
    {
        return CircleReady && HasLeader && Distance>(WasRegrouping?1000.0:2200.0);
    }

int main(){
assert(!NeedsSquadRegroup(false,true,false,90000));
assert(!NeedsSquadRegroup(true,false,false,90000));
assert(!NeedsSquadRegroup(true,true,false,2200));
assert(NeedsSquadRegroup(true,true,false,2201));
assert(NeedsSquadRegroup(true,true,true,1500));
assert(!NeedsSquadRegroup(true,true,true,1000));
assert(!NeedsSquadRegroup(false,true,true,90000));
std::cout<<"7 production regroup cases passed: first-circle gating, missing leader, separation and hysteresis\n";}
