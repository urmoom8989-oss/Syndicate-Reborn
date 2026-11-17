#pragma once
#include "framework.h"
#include "LocalSession.h"
namespace Misc
{
inline void (*DispatchRequestOG)(void *, void *, int) = nullptr;
inline void DispatchRequest(void *Context, void *Data, int Type)
{
    if (DispatchRequestOG)
        DispatchRequestOG(Context, Data, 3);
}
inline int CanCreateContext(AActor *)
{
    return 1;
}
inline void HookAll()
{
    LocalSession::HookAll();
    InstallHook(reinterpret_cast<void *>(ImageBase + 0xBAED60), DispatchRequest,
                reinterpret_cast<void **>(&DispatchRequestOG));
    // Let native garbage collection run. Disabling it indefinitely leaks every match.
    InstallHook(reinterpret_cast<void *>(ImageBase + 0x15D0330), IsFalse, nullptr);
    InstallHook(reinterpret_cast<void *>(ImageBase + 0x22A30C0), CanCreateContext, nullptr);
    Log("Legacy local networking hooks installed; native garbage collection retained.");
}
} // namespace Misc
