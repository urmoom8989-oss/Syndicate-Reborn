#pragma once
#include "SyndicateRuntime.h"
namespace BotAppearance
{
struct Snapshot
{
    Syndicate::Ref<UFortHeroType> Hero;
    Syndicate::Ref<UCustomCharacterPart> Parts[6];
    Syndicate::Ref<UAthenaCharmItemDefinition> Charms[4];
    Syndicate::Ref<UCustomColorSwatch> Colors[2];
    EFortCustomGender Gender{};
    EFortCustomBodyType Body{};
    bool Captured = false;
    void Capture(AFortPlayerStateAthena *State)
    {
        if (!State)
            return;
        Hero.Set(State->HeroType);
        Gender = State->CharacterGender;
        Body = State->CharacterBodyType;
        for (size_t I = 0; I < std::size(Parts); ++I)
            Parts[I].Set(State->CharacterData.Parts[I]);
        for (size_t I = 0; I < std::size(Charms); ++I)
            Charms[I].Set(State->CharacterData.Charms[I]);
        for (size_t I = 0; I < std::size(Colors); ++I)
            Colors[I].Set(State->CharacterColorSwatches[I]);
        Captured = true;
    }
    void Restore(AFortPlayerStateAthena *State, AFortPlayerPawnAthena *Pawn) const
    {
        if (!Captured || !State || !Pawn)
            return;
        Syndicate::Ref<AFortPlayerStateAthena> StateGuard(State);
        Syndicate::Ref<AFortPlayerPawnAthena> PawnGuard(Pawn);
        // Restore the warmup selection before native customization reads it.
        State->HeroType = Hero.Get();
        State->CharacterGender = Gender;
        State->CharacterBodyType = Body;
        for (size_t I = 0; I < std::size(Parts); ++I)
            State->CharacterData.Parts[I] = Parts[I].Get();
        for (size_t I = 0; I < std::size(Charms); ++I)
            State->CharacterData.Charms[I] = Charms[I].Get();
        for (size_t I = 0; I < std::size(Colors); ++I)
            State->CharacterColorSwatches[I] = Colors[I].Get();
        ApplyCharacterCustomization(State, Pawn);
        for (size_t I = 0; I < std::size(Parts); ++I)
        {
            if (!StateGuard.Get() || !PawnGuard.Get())
                return;
            Pawn->ServerChoosePart(static_cast<EFortCustomPartType>(I), Parts[I].Get());
        }
        if (!StateGuard.Get() || !PawnGuard.Get())
            return;
        State->OnRep_CharacterData();
        State->ForceNetUpdate();
        Pawn->ForceNetUpdate();
    }
};
} // namespace BotAppearance
