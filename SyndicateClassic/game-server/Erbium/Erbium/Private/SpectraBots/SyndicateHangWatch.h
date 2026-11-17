#pragma once
// Game-thread hang watchdog.
//
// When a match server stops ticking (a deadlock or an endless loop on the game thread), nothing
// is logged and the launcher only sees "Erbium is loading the island" until it gives up. This
// watchdog runs on its own thread. If the bot tick has not run for 25 s after it started, it
// pauses the game thread for a moment, copies its registers and the top of its stack, lets it
// run again and prints where it was to the server stdout log:
//
//   [SyndicateHang] game thread stuck 25 s at FortniteClient-Win64-Shipping.exe+0x1a2b3c
//   [SyndicateHang]   stack: Erbium.dll+0x4d5e6, FortniteClient-Win64-Shipping.exe+0x77aa10, ...
//
// The stack list is every value on the copied stack that points into a loaded module's code,
// so it holds the real return addresses (plus some stale ones). Nothing is resolved while the
// game thread is paused, so the watchdog can't deadlock on a lock that thread holds.
// Logs at most 3 times per stall (25 s, 55 s, 85 s) and again for a later stall.
#include <windows.h>
#include <psapi.h>
#include <algorithm>
#include <atomic>
#include <thread>
#include <vector>
#include <string>
#include <cstdio>

namespace SyndicateHangWatch
{
    inline std::atomic<unsigned long long> LastTick{0};
    inline std::atomic<bool> Started{false};
    inline HANDLE GameThread = nullptr;

    struct ModuleRange { uintptr_t Base = 0, End = 0; std::string Name; };

    inline std::vector<ModuleRange> Modules()
    {
        std::vector<ModuleRange> Out;
        HMODULE List[1024]; DWORD Needed = 0;
        if (!K32EnumProcessModules(GetCurrentProcess(), List, sizeof(List), &Needed)) return Out;
        const DWORD Count = (std::min)(DWORD(Needed / sizeof(HMODULE)), DWORD(1024));
        for (DWORD I = 0; I < Count; ++I)
        {
            MODULEINFO Info{};
            if (!K32GetModuleInformation(GetCurrentProcess(), List[I], &Info, sizeof(Info))) continue;
            char Name[MAX_PATH]{};
            K32GetModuleBaseNameA(GetCurrentProcess(), List[I], Name, MAX_PATH);
            Out.push_back({ uintptr_t(Info.lpBaseOfDll), uintptr_t(Info.lpBaseOfDll) + Info.SizeOfImage, Name });
        }
        return Out;
    }

    inline std::string Where(const std::vector<ModuleRange>& Mods, uintptr_t Address)
    {
        char Text[160];
        for (auto& M : Mods)
            if (Address >= M.Base && Address < M.End)
            {
                snprintf(Text, sizeof(Text), "%s+0x%llx", M.Name.c_str(), (unsigned long long)(Address - M.Base));
                return Text;
            }
        snprintf(Text, sizeof(Text), "0x%llx", (unsigned long long)Address);
        return Text;
    }

    // Runs on the watchdog thread.
    inline void Report(unsigned Seconds)
    {
        auto Mods = Modules(); // resolved before pausing: this takes the loader lock
        CONTEXT Context{}; Context.ContextFlags = CONTEXT_CONTROL | CONTEXT_INTEGER;
        static uint8_t Stack[16384];
        size_t Copied = 0;
        bool Got = false;
        if (SuspendThread(GameThread) != DWORD(-1))
        {
            if (GetThreadContext(GameThread, &Context))
            {
                Got = true;
                // Copy the top of the stack while the thread is paused (no allocation, no locks).
                MEMORY_BASIC_INFORMATION Region{};
                if (VirtualQuery((void*)Context.Rsp, &Region, sizeof(Region)) && Region.State == MEM_COMMIT)
                {
                    uintptr_t RegionEnd = uintptr_t(Region.BaseAddress) + Region.RegionSize;
                    Copied = (std::min)(size_t(RegionEnd - Context.Rsp), sizeof(Stack));
                    memcpy(Stack, (void*)Context.Rsp, Copied);
                }
            }
            ResumeThread(GameThread);
        }
        if (!Got) { printf("[SyndicateHang] game thread stuck %u s (its position could not be read)\n", Seconds); fflush(stdout); return; }

        auto IsCode = [&](uintptr_t A) {
            for (auto& M : Mods) if (A > M.Base + 0x1000 && A < M.End) return true;
            return false;
        };
        std::string Line = "[SyndicateHang] game thread stuck " + std::to_string(Seconds) + " s at " + Where(Mods, Context.Rip) + "\n[SyndicateHang]   stack:";
        int Frames = 0;
        for (size_t Off = 0; Off + 8 <= Copied && Frames < 120; Off += 8)
        {
            uintptr_t Value = *(uintptr_t*)(Stack + Off);
            if (!IsCode(Value)) continue;
            Line += (Frames ? ", " : " ") + Where(Mods, Value);
            if (++Frames % 6 == 0) Line += "\n[SyndicateHang]   stack:";
        }
        printf("%s\n", Line.c_str());
        fflush(stdout);
    }

    inline void Watch()
    {
        unsigned long long StallStart = 0; int Reports = 0;
        for (;;)
        {
            Sleep(5000);
            const unsigned long long Last = LastTick.load(), Now = GetTickCount64();
            if (Now - Last < 25000) { StallStart = 0; Reports = 0; continue; }
            if (StallStart != Last) { StallStart = Last; Reports = 0; }
            const unsigned Seconds = unsigned((Now - Last) / 1000);
            if (Reports < 3 && Seconds >= 25u + 30u * unsigned(Reports)) { ++Reports; Report(Seconds); }
        }
    }

    // Called every bot tick on the game thread.
    inline void Beat()
    {
        LastTick.store(GetTickCount64());
        if (Started.exchange(true)) return;
        if (GetEnvironmentVariableA("TILTED_HANG_WATCH", nullptr, 0)) { char V[8]{}; GetEnvironmentVariableA("TILTED_HANG_WATCH", V, 8); if (V[0] == '0') return; }
        if (!DuplicateHandle(GetCurrentProcess(), GetCurrentThread(), GetCurrentProcess(), &GameThread,
                THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION, FALSE, 0)) return;
        std::thread(Watch).detach();
    }
}
