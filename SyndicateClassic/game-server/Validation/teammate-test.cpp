#include <vector>
#include <cmath>
#include <cassert>
#include <cstdio>
struct Point{double X=0,Y=0;};double FlatDistance(Point a,Point b){return hypot(a.X-b.X,a.Y-b.Y);}
struct AFortPlayerStateAthena{int TeamIndex;};
struct AFortPlayerPawnAthena{AFortPlayerStateAthena* PlayerState;bool bActorIsBeingDestroyed=false;bool Down=false;float Health=100;Point P;bool IsDBNO(){return Down;}float GetHealth(){return Health;}Point K2_GetActorLocation(){return P;}};
struct Ref{AFortPlayerPawnAthena* p;auto Get(){return p;}};struct Bot{bool Eliminated;Ref PawnRef;};std::vector<Bot>Bots;
struct Controller{AFortPlayerPawnAthena* MyFortPawn;};struct Connection{Controller* PlayerController;};struct UNetDriver{std::vector<Connection*>ClientConnections;};struct UWorld{UNetDriver* NetDriver;static UWorld* GetWorld();};UNetDriver net;UWorld world{&net};UWorld* UWorld::GetWorld(){return &world;}
    inline AFortPlayerPawnAthena* StandingTeammate(AFortPlayerPawnAthena* Pawn)
    {
        auto State=Pawn?(AFortPlayerStateAthena*)Pawn->PlayerState:nullptr;
        if(!State)return nullptr;
        AFortPlayerPawnAthena* Best=nullptr; double Distance=1.e30;
        const auto Origin=Pawn->K2_GetActorLocation();
        auto Consider=[&](AFortPlayerPawnAthena* Other) {
            if(!Other || Other==Pawn || Other->bActorIsBeingDestroyed || Other->IsDBNO() || Other->GetHealth()<=0)return;
            auto OtherState=(AFortPlayerStateAthena*)Other->PlayerState;
            if(!OtherState || OtherState->TeamIndex!=State->TeamIndex)return;
            auto D=FlatDistance(Origin,Other->K2_GetActorLocation());
            if(D<Distance){Distance=D;Best=Other;}
        };
        for(auto& Other:Bots)if(!Other.Eliminated)Consider(Other.PawnRef.Get());
        auto World=UWorld::GetWorld();auto Net=World?(UNetDriver*)World->NetDriver:nullptr;
        if(Net)for(auto Conn:Net->ClientConnections)if(Conn && Conn->PlayerController)Consider(Conn->PlayerController->MyFortPawn);
        return Best;
    }


int main(){AFortPlayerStateAthena a{3},b{4};AFortPlayerPawnAthena victim{&a},mate{&a},enemy{&b};mate.P={1000,0};enemy.P={10,0};Bots={{false,{&victim}},{false,{&mate}},{false,{&enemy}}};assert(StandingTeammate(&victim)==&mate);mate.Down=true;assert(!StandingTeammate(&victim));mate.Down=false;Bots[1].Eliminated=true;assert(!StandingTeammate(&victim));Controller c{&mate};Connection conn{&c};net.ClientConnections={&conn};assert(StandingTeammate(&victim)==&mate);mate.Health=0;assert(!StandingTeammate(&victim));assert(!StandingTeammate(nullptr));puts("6 teammate selection checks passed");}
