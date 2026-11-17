#pragma once
// Pre-season / Season 1 (and other builds up to 2.5 that still use AFortQuickBars): inventory rearranging.
//
// The inventory screen's "Unequip" (A on a controller) did nothing: the item stayed in its quickbar slot, so the
// quickbar could not be rearranged. Two parts:
//  1. AFortQuickBars::ServerRemoveItemInternal from the client is run with its "force" flag set. Athena weapons and
//     consumables are slot-required items, and the game's own removal refuses to take a slot-required item off the
//     quickbar unless forced (the same refusal shows as "did not add slot-required item" when adding). The server
//     already calls it forced itself when an item leaves the inventory (FortInventory.cpp).
//  2. A trace of every inventory / quickbar RPC the client sends (name and parameters), written next to the match's
//     ready file as <ready>.inventory.log (Runtime\Server), so the exact call the UI makes can be seen if (1) is not
//     the whole story. Capped at 600 lines a match.
#include <array>
#include <cstdio>
#include <string>
#include <vector>
#include <utility>
#include <filesystem>

namespace SyndicateQuickbarTrace
{
    using NativeFn = void (*)(UObject*, FFrame&, void*);
    constexpr int MaxSlots = 64;
    inline UFunction* Functions[MaxSlots]{};
    inline NativeFn Originals[MaxSlots]{};
    inline std::string Names[MaxSlots];
    inline int Count = 0;
    inline int Lines = 0;
    inline bool Installed = false;

    inline void Write(const std::string& Line)
    {
        if (Lines >= 600) return;
        ++Lines;
        wchar_t Ready[32768];
        if (!GetEnvironmentVariableW(L"TILTED_SERVER_READY_FILE", Ready, 32768)) return;
        FILE* File = nullptr;
        auto Path = std::wstring(Ready) + L".inventory.log";
        if (_wfopen_s(&File, Path.c_str(), L"a") == 0 && File) { fprintf(File, "%s\n", Line.c_str()); fclose(File); }
    }

    inline std::string Describe(UFunction* Fn, uint8* Locals)
    {
        std::string Out;
        if (!Fn || !Locals) return Out;
        for (auto& P : Fn->GetParamsNamed().NameOffsetMap)
        {
            char Buf[160];
            const uint8* At = Locals + P.Offset;
            if (P.ElementSize == 16)
            {
                auto* G = (const uint32*)At;
                snprintf(Buf, sizeof(Buf), " %s=%08X-%08X-%08X-%08X", P.Name.c_str(), G[0], G[1], G[2], G[3]);
            }
            else if (P.ElementSize == 1) snprintf(Buf, sizeof(Buf), " %s=%d", P.Name.c_str(), (int)*At);
            else if (P.ElementSize == 2) snprintf(Buf, sizeof(Buf), " %s=%d", P.Name.c_str(), (int)*(const int16_t*)At);
            else if (P.ElementSize == 4) snprintf(Buf, sizeof(Buf), " %s=%d/%g", P.Name.c_str(), *(const int32*)At, (double)*(const float*)At);
            else if (P.ElementSize == 8) snprintf(Buf, sizeof(Buf), " %s=0x%llx", P.Name.c_str(), (unsigned long long)*(const uint64*)At);
            else snprintf(Buf, sizeof(Buf), " %s=(%u bytes)", P.Name.c_str(), P.ElementSize);
            Out += Buf;
        }
        return Out;
    }

    // Which quickbar (0 primary, 1 secondary) and slot hold this item; -1 when none.
    inline std::pair<int, int> FindSlot(AFortQuickBars* Bars, const FGuid& Guid)
    {
        if (!Bars) return { -1, -1 };
        for (int Bar = 0; Bar < 2; Bar++)
        {
            auto& QuickBar = Bar == 0 ? Bars->PrimaryQuickBar : Bars->SecondaryQuickBar;
            for (int i = 0; i < QuickBar.Slots.Num(); i++)
            {
                auto& Slot = QuickBar.Slots.Get(i, FQuickBarSlot::Size());
                for (auto& Item : Slot.Items)
                    if (Item == Guid) return { Bar, i };
            }
        }
        return { -1, -1 };
    }

    inline void Run(int Index, UObject* Context, FFrame& Stack, void* Result)
    {
        auto Original = Originals[Index];
        auto Fn = Functions[Index];
        // Parameters are only readable straight from Locals when the call came through ProcessEvent (an RPC or a
        // native call), which is the case for everything the client sends.
        bool Direct = Stack.Node == Fn && !Stack.Code && Stack.Locals;
        if (Direct)
        {
            if (Names[Index] == "ServerRemoveItemInternal")
                for (auto& P : Fn->GetParamsNamed().NameOffsetMap)
                    if (P.ElementSize == 1 && P.Name.find("Force") != std::string::npos) Stack.Locals[P.Offset] = 1;
            if (Names[Index] != "ServerActivateSlotInternal" || Lines < 200) // slot switching is frequent
                Write(std::string(Context && Context->Class ? Context->Class->GetName().ToSDKString().c_str() : "?") + "::" + Names[Index] + Describe(Fn, Stack.Locals));
        }
        // Unequip: the game's own removal leaves slot-required items (every Athena weapon and consumable) in their
        // slot even when forced, so the slot is emptied here first, the same way FortInventory::Remove does it.
        bool Unequip = Direct && Names[Index] == "ServerRemoveItemInternal";
        FGuid Guid{};
        if (Unequip)
        {
            for (auto& P : Fn->GetParamsNamed().NameOffsetMap)
                if (P.ElementSize == sizeof(FGuid) && P.Name == "Item") Guid = *(FGuid*)(Stack.Locals + P.Offset);
            auto [Bar, At] = FindSlot((AFortQuickBars*)Context, Guid);
            if (Bar >= 0) ((AFortQuickBars*)Context)->EmptySlot(Bar == 1, At);
            Write("  emptied quickbar " + std::to_string(Bar) + " slot " + std::to_string(At));
        }
        if (Original) Original(Context, Stack, Result);
        if (Unequip)
        {
            auto [Bar, At] = FindSlot((AFortQuickBars*)Context, Guid);
            Write("  after removal: quickbar " + std::to_string(Bar) + " slot " + std::to_string(At) + (Bar >= 0 ? " (still equipped)" : " (off the quickbar)"));
        }
    }

    template <int N> void Slot(UObject* Context, FFrame& Stack, void* Result) { Run(N, Context, Stack, Result); }
    template <int... I> constexpr std::array<NativeFn, sizeof...(I)> MakeSlots(std::integer_sequence<int, I...>) { return { &Slot<I>... }; }
    inline const auto SlotFns = MakeSlots(std::make_integer_sequence<int, MaxSlots>{});

    inline bool Wanted(const std::string& Name, bool QuickBarsClass)
    {
        if (Name.rfind("Server", 0) != 0) return false;
        if (QuickBarsClass) return true;
        for (auto Word : { "Item", "Slot", "Quick", "Inventory", "Equip", "Swap", "Drop" })
            if (Name.find(Word) != std::string::npos) return true;
        return false;
    }

    inline void HookClass(const UStruct* Clss, bool QuickBarsClass)
    {
        for (const UField* Prop = Clss->GetChildren(); Prop && Count < MaxSlots; Prop = Prop->GetNext())
        {
            if (!(Prop->Class->GetCastFlags() & 0x80000)) continue; // functions only
            auto Fn = (UFunction*)Prop;
            std::string Name = Prop->GetName().ToSDKString().c_str();
            if (!Wanted(Name, QuickBarsClass)) continue;
            bool Seen = false;
            for (int i = 0; i < Count; i++) if (Functions[i] == Fn) Seen = true;
            if (Seen || !Fn->GetNativeFunc()) continue;
            Functions[Count] = Fn; Names[Count] = Name;
            Originals[Count] = (NativeFn)Fn->GetNativeFunc();
            Fn->GetNativeFunc() = (void*)SlotFns[Count];
            ++Count;
        }
    }

    // Once, when the first player's quickbars are spawned (the classes are loaded by then).
    inline void Install(AActor* QuickBars, AActor* Controller)
    {
        if (Installed || !QuickBars || !Controller) return;
        Installed = true;
        Lines = 0;
        HookClass(QuickBars->Class, true);
        // The controller's own classes up to (not including) the engine's PlayerController.
        for (const UStruct* Clss = Controller->Class; Clss; Clss = (const UStruct*)Clss->GetSuper())
        {
            std::string Name = Clss->GetName().ToSDKString().c_str();
            if (Name == "PlayerController" || Name == "Controller" || Name == "Actor") break;
            HookClass(Clss, false);
        }
        std::string List;
        for (int i = 0; i < Count; i++) List += " " + Names[i];
        Write("[quickbar trace] hooked " + std::to_string(Count) + ":" + List);
    }
}
