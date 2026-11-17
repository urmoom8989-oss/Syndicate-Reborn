#include <vector>
#include <algorithm>
#include <cassert>
#include <cstdio>
struct UWorld{}; float Clock=0; bool HumanPresent=true; int Target=99;
struct UGameplayStatics{static float GetTimeSeconds(UWorld*){return Clock;}};
struct {float FortniteVersion=4;} VersionInfo;
int GetEnvironmentVariableW(const wchar_t*,void*,int){return 1;}
struct FVector{}; bool HumanPlayerLocation(FVector&){return HumanPresent;}
struct Pawn{bool bActorIsBeingDestroyed=false;}; struct Ref{Pawn* p; Pawn* Get(){return p;}};
struct Bot{bool Eliminated=false; Ref PawnRef;}; std::vector<Bot> Bots;
int DesiredBotCount(){return Target;} void AppendBotLog(const char*){}
struct State{float WarmupCountdownStartTime=0, WarmupCountdownEndTime=0; void ForceNetUpdate(){}};
bool tick(UWorld* World,State* GameState,int Phase=2){
        static UWorld* WarmupWorld = nullptr;
        static float CountdownEnds = 0.f;
        static bool BusRequested = false;
        const float Now = (float)UGameplayStatics::GetTimeSeconds(World);
        static float PreviousWarmupTime = 0.f;
        if (WarmupWorld != World || Now < PreviousWarmupTime) {
            WarmupWorld = World; CountdownEnds = 0.f; BusRequested = false;
        }
        PreviousWarmupTime = Now;
        if (VersionInfo.FortniteVersion >= 4 && VersionInfo.FortniteVersion < 5 && Phase == 2 && !BusRequested
            && GetEnvironmentVariableW(L"TILTED_SERVER_READY_FILE", nullptr, 0))
        {
            FVector Human{};
            int IslandBots = 0;
            for (auto& B : Bots) if (!B.Eliminated) {
                auto Pawn = B.PawnRef.Get();
                if (Pawn && !Pawn->bActorIsBeingDestroyed) ++IslandBots;
            }
            const int RequiredBots = (std::min)(90, DesiredBotCount());
            if (CountdownEnds == 0.f && IslandBots >= RequiredBots && HumanPlayerLocation(Human)) {
                CountdownEnds = Now + 10.f;
                AppendBotLog("managed match: bot threshold reached; 10 second bus countdown\n");
            }
            // Keep the replicated warmup timer waiting until the roster is ready.
            if (GameState) {
                GameState->WarmupCountdownStartTime = CountdownEnds > 0.f ? CountdownEnds - 10.f : Now;
                GameState->WarmupCountdownEndTime = CountdownEnds > 0.f ? CountdownEnds : Now + 10.f;
                if (CountdownEnds > 0.f) GameState->ForceNetUpdate();
            }
            if (CountdownEnds > 0.f && Now >= CountdownEnds)
            {
                BusRequested = true;
                AppendBotLog("managed match: starting bus after bot countdown\n");

return true;
}
}
return false;
}
int main(){UWorld w,w2;State s;Pawn p;Bots.resize(89,{false,{&p}});Clock=100;assert(!tick(&w,&s));
Bots.push_back({false,{&p}});Clock=101;assert(!tick(&w,&s));assert(s.WarmupCountdownEndTime==111);
Bots.resize(99,{false,{&p}});Clock=110.9f;assert(!tick(&w,&s));Clock=111;assert(tick(&w,&s));assert(!tick(&w,&s));
Clock=1;HumanPresent=false;assert(!tick(&w2,&s));HumanPresent=true;assert(!tick(&w2,&s));assert(s.WarmupCountdownEndTime==11);
puts("Countdown checks passed: threshold, 10 seconds, one launch, world reset, human required");}
