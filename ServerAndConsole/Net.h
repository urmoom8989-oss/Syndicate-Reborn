#pragma once
#include "framework.h"
namespace Net
{
// These two 10.40 hooks report the actual intended dedicated-server mode.
inline int GetNetMode()
{
    return static_cast<int>(ENetMode::DedicatedServer);
}
inline void HookAll()
{
    InstallHook(reinterpret_cast<void *>(ImageBase + 0x2E3E970), GetNetMode, nullptr);
    InstallHook(reinterpret_cast<void *>(ImageBase + 0x34D2140), GetNetMode, nullptr);
}
} // namespace Net
