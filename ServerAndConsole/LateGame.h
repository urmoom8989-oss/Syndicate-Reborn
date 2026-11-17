#pragma once
// Late game for Season X (Settings > Bots > Late game in the launcher, which passes -SyndicateLateGame=1 to the
// match host). Off unless that switch is on: a normal match never runs any of this.
//
// What it does, in the game's own systems (no teleports, no protection):
// - The battle bus keeps its own speed, heading and timers, but its route is moved so the middle of the drop
//   window passes over the fourth storm circle.
// - Bots pick their landing spot inside that circle.
// - The storm forms as the bus sets off and circles 1 to 3 close in one second each. The bus doors stay shut until
//   circle 4 is on the map (at the latest 3 s before the bus is over it), so nobody jumps before they can see it.
// - Everyone (players and bots) gets a random loadout as they leave the bus: rifle, shotgun, SMG (30%) or heals,
//   sniper/rocket/grenade launcher/grenades, a consumable, maybe a trap (50%), ammo, 500 of each material, full
//   health and shield. Same rules for players and bots.
#include "SyndicateRuntime.h"
#include "FortInventory.h"
#include "Looting.h"
#include "MatchLaunchOptions.h"
#include <algorithm>
#include <initializer_list>
#include <map>
#include <string>
#include <vector>

namespace LateGame
{
inline constexpr int Zone = 4;               // The storm phase the match starts in.
inline constexpr float LandRadius = 15000.f; // Bots land within 150 m of the circle's centre.

inline bool Enabled()
{
    static int Value = -1;
    if (Value < 0)
    {
        auto Option = MatchLaunchOptions::Number(GetCommandLineW(), L"-SyndicateLateGame=");
        Value = Option && *Option == 1 ? 1 : 0;
        if (Value) Log("Late game: on (bus over storm circle " + std::to_string(Zone) + ").");
    }
    return Value == 1;
}

inline UWorld *World = nullptr;
inline bool BusAimed = false, CenterSet = false, CenterFromZoneList = false, StormStarted = false, ZoneLogged = false;
inline FVector Center{};
inline int LastSkippedPhase = -1, Skips = 0;
inline float AimGiveUpAt = 0.f, HoldUntil = 0.f;
inline bool ZoneReached = false, HoldReleased = false, HoldLogged = false;
inline std::map<AFortPlayerStateAthena *, bool> Equipped;

inline void Reset(UWorld *W)
{
    if (World == W) return;
    World = W;
    BusAimed = CenterSet = CenterFromZoneList = StormStarted = ZoneLogged = false;
    Center = {};
    LastSkippedPhase = -1;
    Skips = 0;
    AimGiveUpAt = HoldUntil = 0.f;
    ZoneReached = HoldReleased = HoldLogged = false;
    Equipped.clear();
}

// Bots: the late-game circle is known and the landing spot should be inside it.
inline bool Active() { return Enabled() && CenterSet; }
inline bool InDropCircle(const FVector &At)
{
    return Active() && Syndicate::Finite(At) && Syndicate::Distance2D(At, Center) <= LandRadius;
}
inline FVector RandomDropPoint(float Z)
{
    float Angle = Syndicate::Between(0.f, 6.2831853f), Distance = LandRadius * std::sqrt(Syndicate::Between(0.f, 1.f)) * 0.9f;
    return FVector(Center.X + std::cos(Angle) * Distance, Center.Y + std::sin(Angle) * Distance, Z);
}

// ---------------------------------------------------------------------------------------------------------------
// Loadout
// ---------------------------------------------------------------------------------------------------------------
inline UFortItemDefinition *Load(const std::string &Path)
{
    static std::map<std::string, UFortItemDefinition *> Cache;
    auto Found = Cache.find(Path);
    if (Found != Cache.end()) return Found->second;
    auto Item = StaticLoadObject<UFortItemDefinition>(Path); // nullptr when Season X does not have it
    Cache[Path] = Item;
    return Item;
}
inline std::string Weapon(const char *Name) { return std::string("/Game/Athena/Items/Weapons/") + Name + "." + Name; }
inline std::string Consumable(const char *Folder, const char *Name)
{ return std::string("/Game/Athena/Items/Consumables/") + Folder + "/" + Name + "." + Name; }
inline std::string Trap(const char *Name) { return std::string("/Game/Athena/Items/Traps/") + Name + "." + Name; }

struct Item { UFortItemDefinition *Definition = nullptr; int Count = 0; };
using Kind = std::initializer_list<const char *>; // one weapon kind, every rarity of it

// A kind at random among those Season X has (each equally likely), then a rarity at random among those it has.
inline Item RollKinds(std::initializer_list<Kind> Kinds)
{
    std::vector<std::vector<UFortItemDefinition *>> Have;
    for (auto &K : Kinds)
    {
        std::vector<UFortItemDefinition *> Rarities;
        for (auto Name : K)
            if (auto D = Load(Weapon(Name))) Rarities.push_back(D);
        if (!Rarities.empty()) Have.push_back(std::move(Rarities));
    }
    if (Have.empty()) return {};
    auto &Pick = Have[Syndicate::Pick((int)Have.size())];
    return {Pick[Syndicate::Pick((int)Pick.size())], 1};
}

inline Item Rifle()
{
    return RollKinds({
        {"WID_Assault_AutoHigh_Athena_VR_Ore_T03", "WID_Assault_AutoHigh_Athena_SR_Ore_T03"},                      // SCAR
        {"WID_Assault_Auto_Athena_R_Ore_T03"},                                                                       // blue AR
        {"WID_Assault_SemiAuto_Athena_C_Ore_T03", "WID_Assault_SemiAuto_Athena_UC_Ore_T03", "WID_Assault_SemiAuto_Athena_R_Ore_T03",
         "WID_Assault_SemiAuto_Athena_VR_Ore_T03", "WID_Assault_SemiAuto_Athena_SR_Ore_T03"},                       // burst
        {"WID_Assault_Surgical_Athena_R_Ore_T03", "WID_Assault_Surgical_Athena_VR_Ore_T03"}});                     // scoped
}
inline Item Shotgun()
{
    return RollKinds({
        {"WID_Shotgun_Standard_Athena_C_Ore_T03", "WID_Shotgun_Standard_Athena_UC_Ore_T03", "WID_Shotgun_Standard_Athena_R_Ore_T03",
         "WID_Shotgun_Standard_Athena_VR_Ore_T03", "WID_Shotgun_Standard_Athena_SR_Ore_T03"},                       // pump
        {"WID_Shotgun_SemiAuto_Athena_C_Ore_T03", "WID_Shotgun_SemiAuto_Athena_UC_Ore_T03", "WID_Shotgun_SemiAuto_Athena_R_Ore_T03",
         "WID_Shotgun_SemiAuto_Athena_VR_Ore_T03"},                                                                  // tactical
        {"WID_Shotgun_SlugFire_Athena_VR", "WID_Shotgun_SlugFire_Athena_SR"}});                                     // heavy
}
inline Item Healable()
{
    struct Entry { const char *Folder, *Name; int Count; };
    static const Entry Entries[] = {
        {"Shields", "Athena_Shields", 3},   {"ShieldSmall", "Athena_ShieldSmall", 6}, {"PurpleStuff", "Athena_PurpleStuff", 2},
        {"ChillBronco", "Athena_ChillBronco", 6}, {"Medkit", "Athena_Medkit", 3},   {"Bandage", "Athena_Bandage", 15},
        {"SuperMedkit", "Athena_SuperMedkit", 1}};
    std::vector<Item> Have;
    for (auto &E : Entries)
        if (auto D = Load(Consumable(E.Folder, E.Name))) Have.push_back({D, E.Count});
    return Have.empty() ? Item{} : Have[Syndicate::Pick((int)Have.size())];
}
inline Item SmgOrHeal()
{
    if (Syndicate::Between(0.f, 1.f) < 0.3f)
        if (auto Smg = RollKinds({
                {"WID_Pistol_AutoHeavy_Athena_C_Ore_T03", "WID_Pistol_AutoHeavy_Athena_UC_Ore_T03", "WID_Pistol_AutoHeavy_Athena_R_Ore_T03"},
                {"WID_Pistol_AutoHeavySuppressed_Athena_C_Ore_T03", "WID_Pistol_AutoHeavySuppressed_Athena_UC_Ore_T03",
                 "WID_Pistol_AutoHeavySuppressed_Athena_R_Ore_T03"},
                {"WID_Pistol_Scavenger_Athena_UC_Ore_T03", "WID_Pistol_Scavenger_Athena_R_Ore_T03", "WID_Pistol_Scavenger_Athena_VR_Ore_T03"}});
            Smg.Definition)
            return Smg;
    return Healable();
}
inline Item ExplosiveOrSniper()
{
    auto Grenade = Load(Consumable("Grenade", "Athena_Grenade"));
    if (Grenade && Syndicate::Between(0.f, 1.f) < 0.25f) return {Grenade, 4};
    auto Gun = RollKinds({
        {"WID_Sniper_NoScope_Athena_UC_Ore_T03", "WID_Sniper_NoScope_Athena_R_Ore_T03", "WID_Sniper_BoltAction_Scope_Athena_R_Ore_T03",
         "WID_Sniper_BoltAction_Scope_Athena_VR_Ore_T03", "WID_Sniper_BoltAction_Scope_Athena_SR_Ore_T03"},          // sniper
        {"WID_Launcher_Rocket_Athena_R_Ore_T03", "WID_Launcher_Rocket_Athena_VR_Ore_T03", "WID_Launcher_Rocket_Athena_SR_Ore_T03"}, // RPG
        {"WID_Launcher_Grenade_Athena_R_Ore_T03", "WID_Launcher_Grenade_Athena_VR_Ore_T03", "WID_Launcher_Grenade_Athena_SR_Ore_T03"}});
    if (Gun.Definition) return Gun;
    return Grenade ? Item{Grenade, 4} : Item{};
}
inline Item RandomTrap()
{
    if (Syndicate::Between(0.f, 1.f) < 0.5f) return {};
    auto Damage = Load(Trap("TID_Floor_Spikes_Athena_R_T03"));
    std::vector<UFortItemDefinition *> Movement;
    for (auto Name : {"TID_Floor_Player_Launch_Pad_Athena", "TID_Floor_Player_Jump_Pad_Athena", "TID_Context_BouncePad_Athena"})
        if (auto D = Load(Trap(Name))) Movement.push_back(D);
    bool WantDamage = Syndicate::Between(0.f, 1.f) < 0.5f;
    if ((WantDamage || Movement.empty()) && Damage) return {Damage, 1};
    if (!Movement.empty()) return {Movement[Syndicate::Pick((int)Movement.size())], 1};
    return {};
}

inline int AmmoFor(UFortItemDefinition *Ammo)
{
    std::string N = Ammo ? Ammo->GetName() : "";
    if (N.contains("Shells")) return 30;
    if (N.contains("Light")) return 150;
    if (N.contains("Medium")) return 180;
    if (N.contains("Heavy")) return 24;
    if (N.contains("Rocket")) return 8;
    return 30;
}

inline std::string NameOf(const Item &I) { return I.Definition ? I.Definition->GetName() : std::string("none"); }

inline void GiveLoadout(AController *C, AFortPlayerPawnAthena *Pawn, AFortPlayerStateAthena *State, bool Bot)
{
    auto Slot1 = Rifle(), Slot2 = Shotgun(), Slot3 = SmgOrHeal(), Slot4 = ExplosiveOrSniper(), Slot5 = Healable();
    for (int Try = 0; Try < 4 && Slot5.Definition && Slot5.Definition == Slot3.Definition; ++Try) Slot5 = Healable();
    auto TrapItem = RandomTrap();
    std::vector<UFortItemDefinition *> AmmoGiven;
    for (auto *I : {&Slot1, &Slot2, &Slot3, &Slot4, &Slot5})
    {
        if (!I->Definition || I->Count <= 0) continue;
        auto Ranged = Cast<UFortWeaponRangedItemDefinition>(I->Definition);
        int Clip = 0;
        if (Ranged && !I->Definition->IsStackable())
        {
            auto Ammo = Ranged->GetAmmoWorldItemDefinition_BP();
            if (Ammo && Ammo != I->Definition && std::find(AmmoGiven.begin(), AmmoGiven.end(), Ammo) == AmmoGiven.end())
            {
                AmmoGiven.push_back(Ammo);
                FortInventory::Add(C, Ammo, AmmoFor(Ammo));
            }
            Clip = 1; // loaded to the weapon's own clip size below
        }
        FortInventory::GiveItem(C, I->Definition, I->Count, 0, I->Definition->IsStackable());
        if (Clip)
            if (auto Entry = FortInventory::FindItemEntry(C, I->Definition))
            {
                Entry->LoadedAmmo = std::max(1, Looting::GetClipSize(I->Definition));
                FortInventory::Update(C, Entry);
            }
    }
    if (TrapItem.Definition) FortInventory::GiveItem(C, TrapItem.Definition, TrapItem.Count, 0, true);
    for (auto Type : {EFortResourceType::Wood, EFortResourceType::Stone, EFortResourceType::Metal})
        if (auto Resource = UFortKismetLibrary::K2_GetResourceItemDefinition(Type))
            FortInventory::Add(C, Resource, 500);
    Syndicate::Ref<AFortPlayerPawnAthena> Guard(Pawn);
    Pawn->SetMaxHealth(100.f);
    if ((Pawn = Guard.Get())) Pawn->SetHealth(100.f);
    if ((Pawn = Guard.Get())) Pawn->SetMaxShield(100.f);
    if ((Pawn = Guard.Get())) Pawn->SetShield(100.f);
    static int Told = 0;
    if (Told++ < 70)
        Log(std::string("Late game loadout") + (Bot ? " (bot) " : " (player) ") + State->GetPlayerName().ToString() + ": " + NameOf(Slot1) +
            ", " + NameOf(Slot2) + ", " + NameOf(Slot3) + ", " + NameOf(Slot4) + ", " + NameOf(Slot5) + ", trap: " + NameOf(TrapItem));
}

// ---------------------------------------------------------------------------------------------------------------
// Bus route
// ---------------------------------------------------------------------------------------------------------------
inline bool PickCenter(AFortGameModeAthena *GM, float Now)
{
    if (GM->bSafeZoneLocationsInitialized && GM->SafeZoneLocations.Num() >= Zone)
    {
        Center = GM->SafeZoneLocations[Zone - 1];
        CenterFromZoneList = true;
        return Syndicate::Finite(Center);
    }
    if (Now < AimGiveUpAt) return false; // the storm's circle list can be filled a moment after the bus starts
    // No circle list: a random foundation in the inner half of the island. The storm keeps its own circles then.
    auto Foundations = Syndicate::ActorsOf<ABuildingFoundation>();
    if (Foundations.empty()) return false;
    FVector Mid{};
    int Count = 0;
    for (auto &F : Foundations)
        if (auto A = F.Get()) { auto P = A->K2_GetActorLocation(); Mid.X += P.X; Mid.Y += P.Y; ++Count; }
    if (!Count) return false;
    Mid.X /= Count; Mid.Y /= Count;
    float Far = 0.f;
    for (auto &F : Foundations)
        if (auto A = F.Get()) Far = std::max(Far, Syndicate::Distance2D(A->K2_GetActorLocation(), Mid));
    for (int Try = 0; Try < 200; ++Try)
        if (auto A = Foundations[Syndicate::Pick((int)Foundations.size())].Get())
        {
            auto P = A->K2_GetActorLocation();
            if (Syndicate::Distance2D(P, Mid) < Far * 0.5f || Try == 199) { Center = P; return true; }
        }
    return false;
}

inline void AimBus(AFortGameModeAthena *GM, AFortGameStateAthena *GS, float Now)
{
    auto Bus = GS->Aircrafts.Num() > 0 ? GS->Aircrafts[0] : nullptr;
    if (!Bus) return;
    if (AimGiveUpAt == 0.f) AimGiveUpAt = Now + 3.f;
    if (!CenterSet && !PickCenter(GM, Now)) return;
    CenterSet = true;
    BusAimed = true;
    // Keep the bus's own speed, heading and timers; move its route so the middle of the drop window is over the
    // circle. The native drop-zone timers then still fire when they always do.
    auto &Flight = Bus->FlightInfo;
    float Speed = Flight.FlightSpeed;
    if (!std::isfinite(Speed) || Speed < 500.f || Speed > 50000.f)
    {
        Log("Late game: the bus speed looks wrong (" + std::to_string(Speed) + "); its route was left as it was.");
        return;
    }
    float Yaw = Flight.FlightStartRotation.Yaw * 3.14159265f / 180.f;
    FVector Direction(std::cos(Yaw), std::sin(Yaw), 0.f);
    float Middle = (Flight.TimeTillDropStart + Flight.TimeTillDropEnd) * 0.5f;
    if (!std::isfinite(Middle) || Middle <= 0.f) Middle = Flight.TimeTillFlightEnd * 0.5f;
    FVector Old = Flight.FlightStartLocation;
    FVector Start(Center.X - Direction.X * Speed * Middle, Center.Y - Direction.Y * Speed * Middle, Old.Z);
    Flight.FlightStartLocation.X = Start.X;
    Flight.FlightStartLocation.Y = Start.Y;
    float Elapsed = Bus->FlightStartTime > 0.f ? std::max(0.f, Now - Bus->FlightStartTime) : 0.f;
    FVector Here(Start.X + Direction.X * Speed * Elapsed, Start.Y + Direction.Y * Speed * Elapsed, Bus->K2_GetActorLocation().Z);
    Bus->K2_SetActorLocation(Here, false, nullptr, true);
    Bus->ForceNetUpdate();
    // The storm forms now, while the bus is still flying, so the circle is on the map before anyone jumps. Everyone
    // is kept on the bus until circle 4 shows (HoldBus), at the latest until 3 s before the bus is over the circle.
    HoldUntil = Now + std::max(5.f, Middle - Elapsed - 3.f);
    {
        float Was = GS->SafeZonesStartTime;
        if (Was <= 0.f || Was > Now) GS->SafeZonesStartTime = Now;
        Log("Late game: storm start " + std::to_string(Was) + " -> " + std::to_string(GS->SafeZonesStartTime) + " at bus start; jumping opens by " +
            std::to_string(HoldUntil) + " (now " + std::to_string(Now) + ").");
    }
    GS->ForceNetUpdate();
    Log("Late game: bus route moved over storm circle " + std::to_string(Zone) + " at " + std::to_string((int)Center.X) + ", " +
        std::to_string((int)Center.Y) + (CenterFromZoneList ? "" : " (no circle list yet: random foundation)") + "; start " +
        std::to_string((int)Old.X) + ", " + std::to_string((int)Old.Y) + " -> " + std::to_string((int)Start.X) + ", " +
        std::to_string((int)Start.Y) + ", drop middle at " + std::to_string(Middle) + " s, speed " + std::to_string((int)Speed) + ".");
}

// ---------------------------------------------------------------------------------------------------------------
// Storm
// ---------------------------------------------------------------------------------------------------------------
// Called after the game's own OnAircraftExitedDropZone: the storm forms now instead of a while later.
inline void OnDropZoneExited(AFortGameStateAthena *GS)
{
    if (!Enabled() || !GS) return;
    float Now = Syndicate::Now(), Was = GS->SafeZonesStartTime;
    if (Was > Now || Was <= 0.f) { GS->SafeZonesStartTime = Now; GS->ForceNetUpdate(); }
    StormStarted = true;
    Log("Late game: storm start " + std::to_string(Was) + " -> " + std::to_string(GS->SafeZonesStartTime) + " (now " + std::to_string(Now) + ").");
}

inline void TickStorm(AFortGameModeAthena *GM, AFortGameStateAthena *GS, float Now)
{
    auto Zone_ = GS->SafeZoneIndicator;
    if (GS->GamePhase < EAthenaGamePhase::Aircraft || !Zone_) return; // the storm forms during the bus in late game
    int Phase = GM->SafeZonePhase;
    if (Phase >= 1 && Phase < Zone)
    {
        // Circles before the late-game one close in one second each. Not shorter: the size the storm damages with
        // is only updated while a circle is shrinking.
        if (Phase != LastSkippedPhase && Zone_->SafeZoneFinishShrinkTime > Now + 1.5f && Skips < 12)
        {
            LastSkippedPhase = Phase;
            ++Skips;
            float Start = Now, Finish = Now + 1.f;
            Zone_->SafeZoneStartShrinkTime = Start;
            Zone_->SafeZoneFinishShrinkTime = Finish;
            Zone_->ForceNetUpdate();
            Log("Late game: storm phase " + std::to_string(Phase) + " closes now (radius " + std::to_string((int)Zone_->NextRadius) + ").");
        }
    }
    else if (Phase >= Zone && !ZoneLogged)
    {
        ZoneLogged = ZoneReached = true;
        FVector Next = Zone_->NextCenter;
        Log("Late game: storm reached phase " + std::to_string(Phase) + ", centre " + std::to_string((int)Next.X) + ", " +
            std::to_string((int)Next.Y) + ", radius " + std::to_string((int)Zone_->NextRadius) + "; " +
            std::to_string((int)Syndicate::Distance2D(Next, Center)) + " from the bus circle.");
    }
}

// Keep players and bots on the bus until the late-game circle is on the map (jumping early left people far from where
// the circle then appeared). Uses the game's own aircraft lock, the same one that keeps the doors shut at the start.
inline void HoldBus(AFortGameStateAthena *GS, float Now)
{
    if (HoldReleased || !BusAimed) return;
    if (GS->GamePhase != EAthenaGamePhase::Aircraft || ZoneReached || Now >= HoldUntil)
    {
        auto Bus = GS->Aircrafts.Num() > 0 ? GS->Aircrafts[0] : nullptr;
        if (Bus && GS->GamePhase == EAthenaGamePhase::Aircraft && Now < Bus->DropStartTime) return; // the game's own door timer still runs
        HoldReleased = true;
        if (GS->bAircraftIsLocked) { GS->bAircraftIsLocked = false; GS->ForceNetUpdate(); }
        Log(std::string("Late game: bus doors open, ") + (ZoneReached ? "storm circle " + std::to_string(Zone) + " is on the map." :
            "the circle was not on the map in time (storm phase " + std::to_string(Zone) + " not reached)."));
        return;
    }
    if (!GS->bAircraftIsLocked) { GS->bAircraftIsLocked = true; GS->ForceNetUpdate(); }
    if (!HoldLogged) { HoldLogged = true; Log("Late game: holding everyone on the bus until storm circle " + std::to_string(Zone) + " shows."); }
}

// ---------------------------------------------------------------------------------------------------------------
inline void Equip(AController *C, AFortPlayerStateAthena *State, bool Bot)
{
    if (!C || !State || State->IsPlayerDead() || State->bInAircraft || Equipped.count(State)) return;
    auto Pawn = Cast<AFortPlayerPawnAthena>(C->Pawn);
    if (!Pawn || Pawn->IsDead()) return;
    Equipped[State] = true;
    GiveLoadout(C, Pawn, State, Bot);
}

// Every server tick, before the bots think.
inline void Tick(AFortGameModeAthena *GM, AFortGameStateAthena *GS)
{
    if (!Enabled() || !GM || !GS) return;
    Reset(UWorld::GetWorld());
    if (GS->GamePhase < EAthenaGamePhase::Aircraft) return;
    float Now = Syndicate::Now();
    if (!BusAimed && GS->GamePhase == EAthenaGamePhase::Aircraft) AimBus(GM, GS, Now);
    TickStorm(GM, GS, Now);
    HoldBus(GS, Now);
    for (auto PC : GM->AlivePlayers)
        if (PC) Equip(PC, Cast<AFortPlayerStateAthena>(PC->PlayerState), false);
    for (auto Bot : GM->AliveBots)
        if (Bot) Equip(Bot, Cast<AFortPlayerStateAthena>(Bot->PlayerState), true);
}
} // namespace LateGame
