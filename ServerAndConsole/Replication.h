#pragma once
#include "framework.h"
namespace Replication
{
inline int32 (*ServerReplicateActorsOG)(UReplicationDriver *, float) = nullptr;
struct Frame;
inline thread_local Frame *CurrentFrame = nullptr;
struct Frame
{
    UReplicationDriver *Graph;
    unsigned Calls = 0;
    Frame *Previous;
    explicit Frame(UReplicationDriver *InGraph) : Graph(InGraph), Previous(CurrentFrame) { CurrentFrame = this; }
    ~Frame() { CurrentFrame = Previous; }
    Frame(const Frame &) = delete;
    Frame &operator=(const Frame &) = delete;
};
inline int32 ServerReplicateActors(UReplicationDriver *Graph, float Delta)
{
    for (auto F = CurrentFrame; F; F = F->Previous)
        if (F->Graph == Graph)
            ++F->Calls;
    return ServerReplicateActorsOG ? ServerReplicateActorsOG(Graph, Delta) : 0;
}
inline bool CompleteFrame(Frame &F, UReplicationDriver *Graph, float Delta)
{
    if (!Graph || Graph != F.Graph || F.Calls || !ServerReplicateActorsOG)
        return false;
    ServerReplicateActors(Graph, Delta);
    return true;
}
inline void HookAll()
{
    InstallHook(reinterpret_cast<void *>(ImageBase + 0xA33E90), ServerReplicateActors,
                reinterpret_cast<void **>(&ServerReplicateActorsOG));
}
} // namespace Replication
