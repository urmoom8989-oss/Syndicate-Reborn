#pragma once
#include "framework.h"
#include <iomanip>
namespace BotWork
{
enum Stage { Frame, Scan, Sense, LootSearch, Steering, MapSetup, ServerWork, Count };
struct Metric { uint64_t Calls = 0; double Total = 0, Maximum = 0; };
inline Metric Metrics[Count]{};
struct Scope
{
    Stage Work;
    std::chrono::steady_clock::time_point Start = std::chrono::steady_clock::now();
    explicit Scope(Stage S) : Work(S) {}
    ~Scope()
    {
        double Ms = std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-Start).count();
        auto &M = Metrics[Work]; ++M.Calls; M.Total += Ms; M.Maximum = std::max(M.Maximum,Ms);
    }
};
inline void Report()
{
    std::ostringstream Out;
    Out << std::fixed << std::setprecision(2) << "Bot work ms [average/max]:";
    const char *Names[] = {"frame","scan","sense","lootSearch","steering","mapSetup","serverWork"};
    for (int I=0; I<Count; ++I)
    {
        auto &M=Metrics[I];
        Out << " " << Names[I] << "=" << (M.Calls ? M.Total/M.Calls : 0) << "/" << M.Maximum;
        M = {};
    }
    Log(Out.str());
}
} // namespace BotWork
