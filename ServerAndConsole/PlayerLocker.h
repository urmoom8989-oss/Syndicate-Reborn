#pragma once
// The player's own locker in Season X matches.
//
// Syndicate used to run on LawinServer, which hands every account the same profile, so the host's own
// profile already held the player's outfit. Syndicate's backend (Syndicate's) has a profile per
// account: the 10.40 host signs in as "SyndicateHost" and never reads the player's profile, so players
// spawned in the default outfit even though the lobby showed theirs.
//
// The backend already writes the selected locker for Syndicate's match server, one file per
// player: Runtime\Server\locker\<hex of the lowercase username>.loadout, lines like
//   character=CID_029_Athena_Commando_F_Halloween
//   backpack=BID_...   pickaxe=DefaultPickaxe   glider=DefaultGlider   contrail=...
// This reads that file (folder from SYNDICATE_LOCKER_DIR, set by the launcher) and puts the outfit,
// back bling, pickaxe, glider and contrail on the player's loadout, player state and pawn before the
// game applies the character customization.
#include "framework.h"
#include "SyndicateRuntime.h"
#include <fstream>
#include <map>

namespace PlayerLocker
{
inline std::wstring Folder()
{
    wchar_t Buffer[1024]{};
    DWORD Length = GetEnvironmentVariableW(L"SYNDICATE_LOCKER_DIR", Buffer, 1024);
    if (Length > 0 && Length < 1024) return Buffer;
    Length = GetEnvironmentVariableW(L"USERPROFILE", Buffer, 1024);
    if (Length > 0 && Length < 1024) return std::wstring(Buffer) + L"\\Documents\\Syndicate\\Runtime\\Server\\locker";
    return L"";
}

inline bool ValidId(const std::string &Id)
{
    if (Id.empty() || Id.size() > 120) return false;
    for (unsigned char C : Id)
        if (!std::isalnum(C) && C != '_') return false;
    return true;
}

template <class T> inline T *Load(const char *Folder, const std::string &Id)
{
    if (!ValidId(Id)) return nullptr;
    return StaticLoadObject<T>(std::string("/Game/Athena/Items/Cosmetics/") + Folder + "/" + Id + "." + Id);
}

inline std::map<std::string, std::string> Read(const std::string &PlayerName)
{
    std::map<std::string, std::string> Out;
    if (PlayerName.empty()) return Out;
    static const char *Digits = "0123456789abcdef";
    std::wstring Hex;
    for (unsigned char C : PlayerName)
    {
        C = (unsigned char)std::tolower(C);
        Hex += (wchar_t)Digits[C >> 4];
        Hex += (wchar_t)Digits[C & 15];
    }
    auto Dir = Folder();
    if (Dir.empty()) return Out;
    std::ifstream File(Dir + L"\\" + Hex + L".loadout");
    std::string Line;
    while (File && std::getline(File, Line))
    {
        while (!Line.empty() && (Line.back() == '\r' || Line.back() == '\n')) Line.pop_back();
        auto Equal = Line.find('=');
        if (Equal != std::string::npos) Out[Line.substr(0, Equal)] = Line.substr(Equal + 1);
    }
    return Out;
}

inline void SetParts(AFortPlayerStateAthena *State, TArray<UCustomCharacterPart *> &Parts)
{
    for (int I = 0; I < Parts.Num(); I++)
        if (auto Part = Parts[I]; Part && static_cast<size_t>(Part->CharacterPartType) < std::size(State->CharacterData.Parts))
            State->CharacterData.Parts[(uintptr_t)Part->CharacterPartType] = Part;
}

inline UWorld *World = nullptr;
struct Worn
{
    Syndicate::Ref<AFortPlayerPawnAthena> Pawn;
    UFortHeroType *Hero = nullptr;
    bool Found = false;
    UCustomCharacterPart *Parts[6]{};
    float ShowUntil = 0.f;
};
inline std::map<AFortPlayerStateAthena *, Worn> Players;

// Returns true when a saved locker was found and applied.
inline bool Apply(AFortPlayerControllerAthena *PC, AFortPlayerStateAthena *State, AFortPlayerPawnAthena *Pawn)
{
    if (!PC || !State || !Pawn) return false;
    auto Name = State->GetPlayerName().ToString();
    auto Saved = Read(Name);
    if (Saved.empty())
    {
        static int Missing = 0;
        if (Missing++ < 3) Log("Player locker: no saved locker for " + Name);
        return false;
    }

    auto Character = Load<UAthenaCharacterItemDefinition>("Characters", Saved["character"]);
    // A Save the World hero pick (hero=HID_..., launcher Unlock all) replaces the outfit's own hero: its head and
    // body go on the player, the outfit underneath stays for everything else.
    UFortHeroType *StwHero = nullptr;
    if (auto Id = Saved["hero"]; Id.rfind("HID_", 0) == 0 && ValidId(Id))
    {
        for (auto Folder : {"Commando", "Constructor", "Ninja", "Outlander"})
            if ((StwHero = StaticLoadObject<UFortHeroType>(std::string("/Game/Heroes/") + Folder + "/ItemDefinition/" + Id + "." + Id))) break;
        static int ToldHero = 0;
        if (ToldHero++ < 5) Log("Player locker: STW hero " + Id + (StwHero ? " loaded" : " is not in this build; keeping the outfit"));
    }
    auto Backpack = Load<UAthenaBackpackItemDefinition>("Backpacks", Saved["backpack"]);
    auto Pickaxe = Load<UAthenaPickaxeItemDefinition>("Pickaxes", Saved["pickaxe"]);
    auto Glider = Load<UAthenaGliderItemDefinition>("Gliders", Saved["glider"]);
    auto Contrail = Load<UAthenaSkyDiveContrailItemDefinition>("Contrails", Saved["contrail"]);

    if (Character)
    {
        PC->CosmeticLoadoutPC.Character = Character;
        PC->CosmeticLoadoutPC.bIsDefaultCharacter = false;
        Pawn->CosmeticLoadout.Character = Character;
        Pawn->CosmeticLoadout.bIsDefaultCharacter = false;
        if (auto Hero = StwHero ? StwHero : Character->HeroDefinition)
        {
            State->HeroType = Hero;
            for (auto &Slot : State->CharacterData.Parts) Slot = nullptr;
            for (int I = 0; I < Hero->Specializations.Num(); I++)
            {
                auto Path = UKismetStringLibrary::GetDefaultObj()->Conv_NameToString(Hero->Specializations[I].ObjectID.AssetPathName).ToString();
                if (auto Spec = StaticLoadObject<UFortHeroSpecialization>(Path))
                    for (int J = 0; J < Spec->CharacterParts.Num(); J++)
                    {
                        auto PartPath = UKismetStringLibrary::GetDefaultObj()->Conv_NameToString(Spec->CharacterParts[J].ObjectID.AssetPathName).ToString();
                        auto Part = StaticLoadObject<UCustomCharacterPart>(PartPath);
                        if (Part && static_cast<size_t>(Part->CharacterPartType) < std::size(State->CharacterData.Parts))
                            State->CharacterData.Parts[(uintptr_t)Part->CharacterPartType] = Part;
                    }
            }
        }
    }
    if (Backpack)
    {
        PC->CosmeticLoadoutPC.Backpack = Backpack;
        Pawn->CosmeticLoadout.Backpack = Backpack;
        SetParts(State, Backpack->CharacterParts);
    }
    if (Pickaxe) { PC->CosmeticLoadoutPC.Pickaxe = Pickaxe; Pawn->CosmeticLoadout.Pickaxe = Pickaxe; }
    if (Glider) { PC->CosmeticLoadoutPC.Glider = Glider; Pawn->CosmeticLoadout.Glider = Glider; }
    if (Contrail) { PC->CosmeticLoadoutPC.SkyDiveContrail = Contrail; Pawn->CosmeticLoadout.SkyDiveContrail = Contrail; }

    if (World != UWorld::GetWorld()) { World = UWorld::GetWorld(); Players.clear(); }
    auto &Entry = Players[State];
    Entry.Pawn.Set(Pawn);
    Entry.Hero = State->HeroType;
    Entry.Found = true;
    for (size_t I = 0; I < 6 && I < std::size(State->CharacterData.Parts); I++) Entry.Parts[I] = State->CharacterData.Parts[I];
    Entry.ShowUntil = Syndicate::Now() + 15.f;

    Pawn->OnRep_CosmeticLoadout();
    State->OnRep_CharacterData();
    static int Told = 0;
    if (Told++ < 5)
        Log("Player locker: " + Name + " outfit=" + (Character ? Character->GetName() : "default (" + Saved["character"] + ")") +
            " backpack=" + (Backpack ? Backpack->GetName() : "none") + " pickaxe=" + (Pickaxe ? Pickaxe->GetName() : "default") +
            " glider=" + (Glider ? Glider->GetName() : "default"));
    return true;
}

// Push the saved parts onto what clients actually see. Like the Erbium server (and Syndicate's own
// bot outfits): rebuild the visualization from the player state, then choose every part on the pawn.
// Only setting the player state's parts left the player in the default outfit.
inline void Show(AFortPlayerStateAthena *State, AFortPlayerPawnAthena *Pawn)
{
    auto It = Players.find(State);
    if (!State || !Pawn || It == Players.end() || !It->second.Found) return;
    UFortKismetLibrary::UpdatePlayerCustomCharacterPartsVisualization(State);
    for (size_t I = 0; I < 6 && I < std::size(State->CharacterData.Parts); I++)
    {
        auto Part = It->second.Parts[I];
        if (!Part) continue;
        State->CharacterData.Parts[I] = Part;
        Pawn->ServerChoosePart(static_cast<EFortCustomPartType>(I), Part);
    }
    State->OnRep_CharacterData();
    State->ForceNetUpdate();
    Pawn->ForceNetUpdate();
}

// The game can put its own default outfit back after the pawn is possessed (the host has no profile for
// the player). Called twice a second for every living human: re-applies the saved locker when the pawn or
// hero changed or a part no longer matches, and keeps re-showing it for 15 seconds after each new pawn.
inline void Keep(AFortPlayerControllerAthena *PC, AFortPlayerPawnAthena *Pawn)
{
    auto State = PC ? Cast<AFortPlayerStateAthena>(PC->PlayerState) : nullptr;
    if (!State || !Pawn) return;
    if (World != UWorld::GetWorld()) { World = UWorld::GetWorld(); Players.clear(); }
    auto It = Players.find(State);
    if (It != Players.end())
    {
        auto &E = It->second;
        if (!E.Found) return; // no saved locker for this player
        bool PartsDiffer = false;
        for (size_t I = 0; I < 6 && I < std::size(State->CharacterData.Parts); I++)
            if (E.Parts[I] && State->CharacterData.Parts[I] != E.Parts[I]) PartsDiffer = true;
        bool NewPawn = E.Pawn.Get() != Pawn, HeroChanged = State->HeroType != E.Hero;
        if (!NewPawn && !HeroChanged && !PartsDiffer)
        {
            if (Syndicate::Now() < E.ShowUntil) Show(State, Pawn);
            return;
        }
        static int Told = 0;
        if (Told++ < 8)
            Log(std::string("Player locker: re-applying (") + (NewPawn ? "new pawn" : HeroChanged ? "hero changed" : "parts changed") + ")");
    }
    if (!Apply(PC, State, Pawn))
    {
        Players[State].Found = false;
        return;
    }
    ApplyCharacterCustomization(State, Pawn);
    Show(State, Pawn);
}
} // namespace PlayerLocker
