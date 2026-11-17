#pragma once
#include "framework.h"
namespace LocalSession
{
inline bool ShouldAllowDirectJoin(bool LocalAthenaSession, bool HasController, std::string Reason)
{
    if (!LocalAthenaSession || !HasController)
        return false;
    for (auto &C : Reason)
        C = static_cast<char>(std::tolower(static_cast<unsigned char>(C)));
    auto First = Reason.find_first_not_of(" \t\r\n");
    if (First == std::string::npos)
        return false;
    auto Last = Reason.find_last_not_of(" \t\r\n.");
    return Last != std::string::npos && Reason.substr(First, Last - First + 1) == "no reservation in game";
}
inline bool AllowMissingReservation(AGameSession *Session, APlayerController *Controller, const FText &Reason)
{
    auto World = UWorld::GetWorld();
    auto Mode = World ? Cast<AFortGameModeAthena>(World->AuthorityGameMode) : nullptr;
    bool LocalAthena = !Globals::bIsProdServer && Mode && Session && Mode->GameSession == Session;
    return ShouldAllowDirectJoin(LocalAthena, Controller != nullptr,
                                 Reason.TextData ? Reason.ToString() : std::string{});
}
using Kick = bool (*)(AGameSession *, APlayerController *, const FText &);
inline Kick EngineKickOG = nullptr;
inline Kick FortKickOG = nullptr;
inline bool DispatchKick(Kick Original, AGameSession *Session, APlayerController *Controller, const FText &Reason)
{
    if (AllowMissingReservation(Session, Controller, Reason))
    {
        Log("Local direct join allowed before native disconnect: no matchmaking reservation required.");
        return false; // Unreal's KickPlayer returns whether a player was actually kicked.
    }
    return Original ? Original(Session, Controller, Reason) : false;
}
inline bool EngineKick(AGameSession *Session, APlayerController *Controller, const FText &Reason)
{
    return DispatchKick(EngineKickOG, Session, Controller, Reason);
}
inline bool FortKick(AGameSession *Session, APlayerController *Controller, const FText &Reason)
{
    return DispatchKick(FortKickOG, Session, Controller, Reason);
}
inline bool ResolveFortKickTarget(void **BaseTable, void **FortTable, size_t Slots, void *EngineTarget, void *&Target)
{
    if (!BaseTable || !FortTable || !EngineTarget)
        return false;
    for (size_t I = 0; I < Slots; ++I)
        if (BaseTable[I] == EngineTarget)
        {
            Target = FortTable[I];
            return Target != nullptr;
        }
    return false;
}
inline void HookAll()
{
    auto EngineClass = AGameSession::StaticClass();
    auto FortClass = AFortGameSession::StaticClass();
    auto EngineDefault = EngineClass ? EngineClass->DefaultObject : nullptr;
    auto FortDefault = FortClass ? FortClass->DefaultObject : nullptr;
    auto EngineTarget = reinterpret_cast<void *>(ImageBase + 0x30B2B70);
    void *FortTarget = nullptr;
    if (!EngineDefault || !FortDefault ||
        !ResolveFortKickTarget(EngineDefault->VTable, FortDefault->VTable, 256, EngineTarget, FortTarget) ||
        FortTarget != reinterpret_cast<void *>(ImageBase + 0x17E5740))
    {
        HookFailure = true;
        Log("Local join startup refused: the 10.40 native session kick override did not match.");
        return;
    }
    // Verified read-only against the running CL-9380822 server: AGameSession slot
    // 206 resolves to AFortGameSession's override at RVA 0x17E5740. It marks the
    // controller as disconnecting before calling the engine implementation.
    InstallHook(FortTarget, FortKick, reinterpret_cast<void **>(&FortKickOG));
    InstallHook(EngineTarget, EngineKick, reinterpret_cast<void **>(&EngineKickOG));
    Log("Native local-session reservation handling installed before disconnect side effects.");
}
} // namespace LocalSession
