#pragma once
#include "SyndicateRuntime.h"
namespace BotRoute
{
struct Memory
{
    struct Failure { FVector Goal; float Until; };
    struct Sample { FVector At; float Time, Travel; };
    std::vector<Failure> Failures;
    std::vector<Sample> Trail;
    float NextSample = 0.f;
    void Reset() { Failures.clear(); Trail.clear(); NextSample = 0.f; }
    bool Blocked(const FVector &Goal, float Now)
    {
        std::erase_if(Failures,[Now](const Failure &F){return Now >= F.Until;});
        for (auto &F : Failures)
            if (Syndicate::Distance2D(Goal,F.Goal)<400.f && std::abs(Goal.Z-F.Goal.Z)<180.f) return true;
        return false;
    }
    void Fail(const FVector &Goal, float Now)
    {
        if (!Syndicate::Finite(Goal)) return;
        if (Blocked(Goal,Now)) return;
        if (Failures.size()>=8) Failures.erase(Failures.begin());
        Failures.push_back({Goal,Now+30.f});
        Trail.clear(); NextSample=Now+0.75f;
    }
    bool Loop(const FVector &Here, float Now, bool Check)
    {
        if (!Check) { Trail.clear(); return false; }
        if (Now<NextSample || !Syndicate::Finite(Here)) return false;
        NextSample=Now+0.75f;
        if (!Trail.empty() && Now-Trail.back().Time>3.f) Trail.clear();
        float Travel=Trail.empty()?0.f:Trail.back().Travel+Syndicate::Distance2D(Here,Trail.back().At);
        // This history survives objective/path changes. Returning to a recent
        // physical point after substantial travel is a loop, not new progress.
        for (auto &S : Trail)
            if (Now-S.Time>=2.f && Travel-S.Travel>=500.f &&
                Syndicate::Distance2D(Here,S.At)<90.f && std::abs(Here.Z-S.At.Z)<100.f) return true;
        if (Trail.size()>=12) Trail.erase(Trail.begin());
        Trail.push_back({Here,Now,Travel});
        return false;
    }
};
} // namespace BotRoute
