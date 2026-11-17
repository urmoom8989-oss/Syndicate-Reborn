#pragma once
#include "EngineThread.h"
#include "Elimination.h"
#include "SDK/SDK/FortniteGame_parameters.hpp"
namespace PE
{
inline void (*ProcessEventOG)(UObject *, UFunction *, void *) = nullptr;
inline std::atomic<DWORD> GameThread{0};
inline std::atomic<DWORD> EngineThreadId{0};
inline std::atomic<unsigned long long> StartupEvents{0};
inline std::atomic<bool (*)(void)> Bootstrap{nullptr};
inline bool Installed = false;
inline void (*EventObserver)(UObject *, UFunction *, void *, bool) = nullptr;
inline bool (*HandledEvent)(UObject *, UFunction *, void *) = nullptr;
inline void (*DamageObserver)(AFortPlayerPawnAthena *,AController *,float,const FVector &) = nullptr;
inline void (*StormDamageObserver)(AFortPlayerPawnAthena *,AController *,float,const FGameplayTagContainer &,AActor *) = nullptr;
inline UFunction *DamageEvent = nullptr;
inline UFunction *DeathEvent = nullptr;
inline void ProcessEvent(UObject *Object, UFunction *Function, void *Parameters)
{
    if (HandledEvent && HandledEvent(Object, Function, Parameters)) return;
    // Blueprint overrides have distinct UFunction pointers but the same FName/layout.
    bool IsDeath = Function && DeathEvent && Function->Name == DeathEvent->Name;
    bool IsDamage = Function && DamageEvent && Function->Name == DamageEvent->Name;
    auto Pawn = (IsDamage || IsDeath) ? Cast<AFortPlayerPawnAthena>(Object) : nullptr;
    Syndicate::Ref<AFortPlayerPawnAthena> Guard(Pawn);
    if (Pawn && Parameters)
    {
        if (IsDeath)
        {
            auto Event = static_cast<SDK::Params::FortPawn_OnDeathServer *>(Parameters);
            Elimination::Observe(Pawn, Event->InstigatedBy, true, &Event->DamageTags, Event->DamageCauser);
        }
        else
        {
            auto Event = static_cast<SDK::Params::FortPawn_OnDamageServer *>(Parameters);
            Elimination::Observe(Pawn, Event->InstigatedBy, false, &Event->DamageTags, Event->DamageCauser);
            if (StormDamageObserver && GameThread.load() && GetCurrentThreadId()==GameThread.load() &&
                std::isfinite(Event->Damage) && Event->Damage>0.f)
                StormDamageObserver(Pawn,Event->InstigatedBy,Event->Damage,Event->DamageTags,Event->DamageCauser);
            if (DamageObserver && GameThread.load() && GetCurrentThreadId()==GameThread.load() &&
                std::isfinite(Event->Damage) && Event->Damage > 0.f)
                DamageObserver(Pawn,Event->InstigatedBy,Event->Damage,Event->Momentum);
        }
    }
    if (EventObserver)
        EventObserver(Object, Function, Parameters, true);
    if (ProcessEventOG)
        ProcessEventOG(Object, Function, Parameters);
    if (EventObserver)
        EventObserver(Object, Function, Parameters, false);
    if (auto P = Guard.Get())
        Elimination::Finish(P);
    static thread_local bool Booting = false;
    auto Start = Bootstrap.load();
    if (Start && !Booting && Syndicate::EngineThread::IsEngineThread(EngineThreadId.load()))
    {
        ++StartupEvents;
        // Accept the actor tick only on the engine window thread. Threaded actor
        // tasks must not install hooks, mutate UObjects, or start map travel.
        if (!GameThread.load() && Object && Function && Function->Name.ToString() == "ReceiveTick" &&
            Cast<AActor>(Object))
        {
            DWORD Unassigned = 0;
            if (GameThread.compare_exchange_strong(Unassigned, GetCurrentThreadId()))
                Log("Native actor tick found; game-thread startup dispatcher is active.");
        }
        if (GameThread.load() && GetCurrentThreadId() == GameThread.load())
        {
            Booting = true;
            if (Start())
                Bootstrap.store(nullptr);
            Booting = false;
        }
    }
}
inline bool InstallDispatcher()
{
    if (Installed)
        return true;
    Installed = InstallHook(reinterpret_cast<void *>(ImageBase + Offsets::ProcessEvent), ProcessEvent,
                            reinterpret_cast<void **>(&ProcessEventOG)) == MH_OK;
    return Installed;
}
inline void HookAll()
{
    DamageEvent = AFortPawn::StaticClass()->GetFunction("FortPawn", "OnDamageServer");
    DeathEvent = AFortPawn::StaticClass()->GetFunction("FortPawn", "OnDeathServer");
    if (!DamageEvent || !DeathEvent)
    {
        HookFailure = true;
        Log("Native pawn observer functions missing");
    }
    InstallDispatcher();
}
} // namespace PE
