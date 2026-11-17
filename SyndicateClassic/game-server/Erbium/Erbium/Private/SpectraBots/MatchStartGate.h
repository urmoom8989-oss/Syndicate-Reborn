#pragma once
namespace SyndicateMatch {
struct StartGate {
    double Ends=0;
    bool Approved=false;
    void Reset(){Ends=0;Approved=false;}
    bool Update(double Now,bool HumanReady,bool RosterReady){
        if(!HumanReady){Reset();return false;}
        if(Approved)return true;
        if(!RosterReady){Ends=0;return false;}
        if(Ends==0)Ends=Now+10;
        Approved=Now>=Ends;
        return Approved;
    }
};
}
