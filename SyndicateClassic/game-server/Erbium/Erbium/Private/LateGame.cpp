#include "pch.h"
#include "../Public/LateGame.h"
#include "../../FortniteGame/Public/FortInventory.h"
#include "../Public/Utils.h"
#include "../Public/SyndicateSeasonAssets.h"

// Late-game items are given at the bus jump, before most of them have been loaded by loot: FindObject alone
// returned null for them, and an item missing from the build (1.7.2 has no grey pump, no tactical SMG, ...) put a
// null entry in the list, which crashed the loadout. Load each candidate once and keep only the ones this build has.
static const UFortItemDefinition* LateGameLoad(const wchar_t* Path)
{
    static std::unordered_map<std::wstring, const UFortItemDefinition*> Cache;
    auto It = Cache.find(Path);
    if (It != Cache.end())
        return It->second;
    const UFortItemDefinition* Item = nullptr;
    if (SyndicateAssetAvailable(Path))
    {
        Item = FindObject<UFortItemDefinition>(Path);
        if (!Item)
            Item = (const UFortItemDefinition*)SDK::StaticLoadObject(Path, UFortItemDefinition::StaticClass());
    }
    Cache.emplace(Path, Item);
    return Item;
}

// The first tier that has at least one item in this build; a later tier is only used when every earlier one is missing.
static UEAllocatedVector<FLateGameItem> LateGameTiers(std::initializer_list<std::initializer_list<const wchar_t*>> Tiers, uint32 Count = 1)
{
    UEAllocatedVector<FLateGameItem> Found;
    for (auto& Tier : Tiers)
    {
        for (auto Path : Tier)
            if (auto Item = LateGameLoad(Path))
                Found.push_back(FLateGameItem(Count, Item));
        if (!Found.empty())
            break;
    }
    return Found;
}

static FLateGameItem LateGamePick(const UEAllocatedVector<FLateGameItem>& Items)
{
    if (Items.empty())
        return FLateGameItem(0, nullptr);
    return Items[rand() % Items.size()];
}

#define LG_WEAPON(Name) L"/Game/Athena/Items/Weapons/" Name L"." Name

// Late game in Chapter 1 seasons: each weapon's rarity is rolled like floor loot, not always the best one the season
// has (mostly grey to blue; purple and gold rare): 30% common, 30% uncommon, 25% rare, 11% epic, 4% legendary. If the
// season has no weapon of that kind at the rolled rarity, the nearest rarity it does have is used (lower first).
static FLateGameItem LateGameRoll(std::initializer_list<std::pair<int, const wchar_t*>> Pool)
{
    UEAllocatedVector<const UFortItemDefinition*> ByRarity[5];
    for (auto& [Rarity, Path] : Pool)
        if (Rarity >= 0 && Rarity < 5)
            if (auto Item = LateGameLoad(Path))
                ByRarity[Rarity].push_back(Item);
    const int Roll = rand() % 100;
    const int Want = Roll < 30 ? 0 : Roll < 60 ? 1 : Roll < 85 ? 2 : Roll < 96 ? 3 : 4;
    for (int Step = 0; Step < 5; Step++)
        for (int Rarity : { Want - Step, Want + Step })
            if (Rarity >= 0 && Rarity < 5 && !ByRarity[Rarity].empty())
                return FLateGameItem(1, ByRarity[Rarity][rand() % ByRarity[Rarity].size()]);
    return FLateGameItem(0, nullptr);
}

FLateGameItem LateGame::GetShotgun()
{
    if (VersionInfo.FortniteVersion < 11.00)
        return LateGameRoll({
        { 0, LG_WEAPON(L"WID_Shotgun_Standard_Athena_C_Ore_T02") },
        { 0, LG_WEAPON(L"WID_Shotgun_Standard_Athena_C_Ore_T03") },
        { 1, LG_WEAPON(L"WID_Shotgun_Standard_Athena_UC_Ore_T02") },
        { 1, LG_WEAPON(L"WID_Shotgun_Standard_Athena_UC_Ore_T03") },
        { 2, LG_WEAPON(L"WID_Shotgun_Standard_Athena_R_Ore_T02") },
        { 2, LG_WEAPON(L"WID_Shotgun_Standard_Athena_R_Ore_T03") },
        { 3, LG_WEAPON(L"WID_Shotgun_Standard_Athena_VR_Ore_T02") },
        { 3, LG_WEAPON(L"WID_Shotgun_Standard_Athena_VR_Ore_T03") },
        { 4, LG_WEAPON(L"WID_Shotgun_Standard_Athena_SR_Ore_T02") },
        { 4, LG_WEAPON(L"WID_Shotgun_Standard_Athena_SR_Ore_T03") },
        { 0, LG_WEAPON(L"WID_Shotgun_SemiAuto_Athena_C_Ore_T02") },
        { 0, LG_WEAPON(L"WID_Shotgun_SemiAuto_Athena_C_Ore_T03") },
        { 1, LG_WEAPON(L"WID_Shotgun_SemiAuto_Athena_UC_Ore_T02") },
        { 1, LG_WEAPON(L"WID_Shotgun_SemiAuto_Athena_UC_Ore_T03") },
        { 2, LG_WEAPON(L"WID_Shotgun_SemiAuto_Athena_R_Ore_T02") },
        { 2, LG_WEAPON(L"WID_Shotgun_SemiAuto_Athena_R_Ore_T03") },
        { 3, LG_WEAPON(L"WID_Shotgun_SemiAuto_Athena_VR_Ore_T02") },
        { 3, LG_WEAPON(L"WID_Shotgun_SemiAuto_Athena_VR_Ore_T03") } });
    static UEAllocatedVector<FLateGameItem> Shotguns{};

    if (Shotguns.size() == 0)
    {
        if (VersionInfo.FortniteVersion < 6.31)
        {
            // Highest pump rarity this build has (Pre-season and Season 1 only have green and blue pumps),
            // then the tactical shotgun if there is no pump at all.
            Shotguns = LateGameTiers({
                { LG_WEAPON(L"WID_Shotgun_Standard_Athena_SR_Ore_T03"), LG_WEAPON(L"WID_Shotgun_Standard_Athena_VR_Ore_T03") },
                { LG_WEAPON(L"WID_Shotgun_Standard_Athena_R_Ore_T03"), LG_WEAPON(L"WID_Shotgun_Standard_Athena_UC_Ore_T03") },
                { LG_WEAPON(L"WID_Shotgun_SemiAuto_Athena_VR_Ore_T03"), LG_WEAPON(L"WID_Shotgun_SemiAuto_Athena_R_Ore_T03"), LG_WEAPON(L"WID_Shotgun_SemiAuto_Athena_UC_Ore_T03") },
            });
        }
        else if (VersionInfo.FortniteVersion >= 19.00 && VersionInfo.FortniteVersion <= 22.40)
        {
            if (VersionInfo.FortniteVersion >= 21.30)
            {
                Shotguns.push_back(FLateGameItem(1, FindObject<UFortItemDefinition>(L"/DaisyWeaponGameplay/Items/Weapons/Shotguns/OverLoadShotgun/WID_Shotgun_OverLoad_Athena_SR.WID_Shotgun_OverLoad_Athena_SR")));
                Shotguns.push_back(FLateGameItem(1, FindObject<UFortItemDefinition>(L"/DaisyWeaponGameplay/Items/Weapons/Shotguns/OverLoadShotgun/WID_Shotgun_OverLoad_Athena_VR.WID_Shotgun_OverLoad_Athena_VR")));
            }
            if (VersionInfo.FortniteVersion <= 22)
            {
                Shotguns.push_back(FLateGameItem(1, FindObject<UFortItemDefinition>(L"/FlipperGameplay/Items/Weapons/BurstShotgun/WID_Shotgun_CoreBurst_Athena_SR.WID_Shotgun_CoreBurst_Athena_SR")));
                Shotguns.push_back(FLateGameItem(1, FindObject<UFortItemDefinition>(L"/FlipperGameplay/Items/Weapons/BurstShotgun/WID_Shotgun_CoreBurst_Athena_VR.WID_Shotgun_CoreBurst_Athena_VR")));
            }
            // Shotguns.push_back(FLateGameItem(1,
            // FindObject<UFortItemDefinition>(L"/Game/Athena/Items/Weapons/WID_Shotgun_Standard_Athena_VR_Ore_T03.WID_Shotgun_Standard_Athena_VR_Ore_T03")));
            // Shotguns.push_back(FLateGameItem(1,
            // FindObject<UFortItemDefinition>(L"/Game/Athena/Items/Weapons/WID_Shotgun_Standard_Athena_SR_Ore_T03.WID_Shotgun_Standard_Athena_SR_Ore_T03")));
        }
        else if (VersionInfo.FortniteVersion > 22.40 && VersionInfo.FortniteVersion <= 26.30)
        {
            if (VersionInfo.FortniteVersion < 24)
            {
                Shotguns.push_back(FLateGameItem(1, FindObject<UFortItemDefinition>(L"/MusterCoreWeapons/Items/Weapons/MusterPumpShotgun/WID_Shotgun_MusterPump_Athena_UC.WID_Shotgun_MusterPump_Athena_UC")));
                Shotguns.push_back(FLateGameItem(1, FindObject<UFortItemDefinition>(L"/MusterCoreWeapons/Items/Weapons/MusterPumpShotgun/WID_Shotgun_MusterPump_Athena_VR.WID_Shotgun_MusterPump_Athena_VR")));
            }
            else if (VersionInfo.FortniteVersion >= 25.11)
            {
                Shotguns.push_back(FLateGameItem(1, FindObject<UFortItemDefinition>(L"/ChronoWeaponGameplay/Items/ChronoShotgun/WID_Shotgun_Chrono_Athena_SR.WID_Shotgun_Chrono_Athena_SR")));
                Shotguns.push_back(FLateGameItem(1, FindObject<UFortItemDefinition>(L"/ChronoWeaponGameplay/Items/ChronoShotgun/WID_Shotgun_Chrono_Athena_VR.WID_Shotgun_Chrono_Athena_VR")));
            }

            if (VersionInfo.FortniteVersion >= 24 && VersionInfo.FortniteVersion < 26.00)
            {
                Shotguns.push_back(FLateGameItem(1, FindObject<UFortItemDefinition>(L"/RadicalWeaponsGameplay/Weapons/RadicalShotgunPump/WID_Shotgun_RadicalPump_Athena_SR.WID_Shotgun_RadicalPump_Athena_SR")));
                Shotguns.push_back(FLateGameItem(1, FindObject<UFortItemDefinition>(L"/RadicalWeaponsGameplay/Weapons/RadicalShotgunPump/WID_Shotgun_RadicalPump_Athena_VR.WID_Shotgun_RadicalPump_Athena_VR")));
            }

            if (VersionInfo.FortniteVersion >= 23.40)
                Shotguns.push_back(FLateGameItem(1, FindObject<UFortItemDefinition>(L"/MusterCoreWeapons/Items/Weapons/Exotics/WID_Shotgun_Breach_Athena_X.WID_Shotgun_Breach_Athena_X")));

            // Shotguns.push_back(FLateGameItem(1,
            // FindObject<UFortItemDefinition>(L"/Game/Athena/Items/Weapons/WID_Shotgun_Standard_Athena_SR_Ore_T03.WID_Shotgun_Standard_Athena_VR_Ore_T03")));
            // Shotguns.push_back(FLateGameItem(1,
            // FindObject<UFortItemDefinition>(L"/Game/Athena/Items/Weapons/WID_Shotgun_Standard_Athena_VR_Ore_T03.WID_Shotgun_Standard_Athena_SR_Ore_T03")));
        }
        else if (VersionInfo.FortniteVersion >= 28.00)
        {
            Shotguns.push_back(FLateGameItem(1, FindObject<UFortItemDefinition>(L"/PaprikaCoreWeapons/Items/Weapons/PaprikaShotgun_Pump/WID_Shotgun_Pump_Paprika_Athena_SR.WID_Shotgun_Pump_Paprika_Athena_SR")));
            Shotguns.push_back(FLateGameItem(1, FindObject<UFortItemDefinition>(L"/PaprikaCoreWeapons/Items/Weapons/PaprikaShotgun_Pump/WID_Shotgun_Pump_Paprika_Athena_VR.WID_Shotgun_Pump_Paprika_Athena_VR")));

            // Shotguns.push_back(FLateGameItem(1,
            // FindObject<UFortItemDefinition>(L"/Game/Athena/Items/Weapons/WID_Shotgun_Standard_Athena_SR_Ore_T03.WID_Shotgun_Standard_Athena_VR_Ore_T03")));
            // Shotguns.push_back(FLateGameItem(1,
            // FindObject<UFortItemDefinition>(L"/Game/Athena/Items/Weapons/WID_Shotgun_Standard_Athena_VR_Ore_T03.WID_Shotgun_Standard_Athena_SR_Ore_T03")));
        }
        else
        {
            Shotguns.push_back(FLateGameItem(1, FindObject<UFortItemDefinition>(L"/Game/Athena/Items/Weapons/WID_Shotgun_Standard_Athena_SR_Ore_T03.WID_Shotgun_Standard_Athena_SR_Ore_T03")));
            Shotguns.push_back(FLateGameItem(1, FindObject<UFortItemDefinition>(L"/Game/Athena/Items/Weapons/WID_Shotgun_Standard_Athena_VR_Ore_T03.WID_Shotgun_Standard_Athena_VR_Ore_T03")));
        }
    }

    return LateGamePick(Shotguns);
}

FLateGameItem LateGame::GetAssaultRifle()
{
    if (VersionInfo.FortniteVersion < 11.00)
        return LateGameRoll({
        { 0, LG_WEAPON(L"WID_Assault_Auto_Athena_C_Ore_T02") },
        { 0, LG_WEAPON(L"WID_Assault_Auto_Athena_C_Ore_T03") },
        { 1, LG_WEAPON(L"WID_Assault_Auto_Athena_UC_Ore_T02") },
        { 1, LG_WEAPON(L"WID_Assault_Auto_Athena_UC_Ore_T03") },
        { 2, LG_WEAPON(L"WID_Assault_Auto_Athena_R_Ore_T02") },
        { 2, LG_WEAPON(L"WID_Assault_Auto_Athena_R_Ore_T03") },
        { 3, LG_WEAPON(L"WID_Assault_AutoHigh_Athena_VR_Ore_T02") },
        { 3, LG_WEAPON(L"WID_Assault_AutoHigh_Athena_VR_Ore_T03") },
        { 4, LG_WEAPON(L"WID_Assault_AutoHigh_Athena_SR_Ore_T02") },
        { 4, LG_WEAPON(L"WID_Assault_AutoHigh_Athena_SR_Ore_T03") },
        { 0, LG_WEAPON(L"WID_Assault_SemiAuto_Athena_C_Ore_T02") },
        { 0, LG_WEAPON(L"WID_Assault_SemiAuto_Athena_C_Ore_T03") },
        { 1, LG_WEAPON(L"WID_Assault_SemiAuto_Athena_UC_Ore_T02") },
        { 1, LG_WEAPON(L"WID_Assault_SemiAuto_Athena_UC_Ore_T03") },
        { 2, LG_WEAPON(L"WID_Assault_SemiAuto_Athena_R_Ore_T02") },
        { 2, LG_WEAPON(L"WID_Assault_SemiAuto_Athena_R_Ore_T03") } });
    static UEAllocatedVector<FLateGameItem> AssaultRifles{};

    if (AssaultRifles.size() == 0)
    {
        if (VersionInfo.FortniteVersion > 22.40 && VersionInfo.FortniteVersion <= 26.30)
        {
            if (VersionInfo.FortniteVersion < 25.00)
            {
                AssaultRifles.push_back(FLateGameItem(1, FindObject<UFortItemDefinition>(L"/MusterCoreWeapons/Items/Weapons/MusterScopedAR/WID_Assault_MusterScoped_Athena_SR.WID_Assault_MusterScoped_Athena_SR")));
                AssaultRifles.push_back(FLateGameItem(1, FindObject<UFortItemDefinition>(L"/MusterCoreWeapons/Items/Weapons/MusterScopedAR/WID_Assault_MusterScoped_Athena_VR.WID_Assault_MusterScoped_Athena_VR")));
            }
            else if (VersionInfo.FortniteVersion >= 26.00)
            {
                AssaultRifles.push_back(FLateGameItem(1, FindObject<UFortItemDefinition>(L"/HopscotchWeaponsGameplay/Items/FlipmagAR/WID_Assault_FlipMag_Athena_SR.WID_Assault_FlipMag_Athena_SR")));
                AssaultRifles.push_back(FLateGameItem(1, FindObject<UFortItemDefinition>(L"/HopscotchWeaponsGameplay/Items/FlipmagAR/WID_Assault_FlipMag_Athena_VR.WID_Assault_FlipMag_Athena_VR")));
            }
            else if (VersionInfo.FortniteVersion >= 25.00)
            {
                AssaultRifles.push_back(FLateGameItem(1, FindObject<UFortItemDefinition>(L"/ChronoWeaponGameplay/Items/PanRifle/WID_Assault_Chrono_Pan_Rifle_Athena_SR.WID_Assault_Chrono_Pan_Rifle_Athena_SR")));
                AssaultRifles.push_back(FLateGameItem(1, FindObject<UFortItemDefinition>(L"/ChronoWeaponGameplay/Items/PanRifle/WID_Assault_Chrono_Pan_Rifle_Athena_VR.WID_Assault_Chrono_Pan_Rifle_Athena_VR")));
            }

            AssaultRifles.push_back(FLateGameItem(1, FindObject<UFortItemDefinition>(L"/Game/Athena/Items/Weapons/WID_Assault_AutoHigh_Athena_SR_Ore_T03.WID_Assault_AutoHigh_Athena_SR_Ore_T03")));
            AssaultRifles.push_back(FLateGameItem(1, FindObject<UFortItemDefinition>(L"/Game/Athena/Items/Weapons/WID_Assault_AutoHigh_Athena_VR_Ore_T03.WID_Assault_AutoHigh_Athena_VR_Ore_T03")));
        }
        else if (VersionInfo.FortniteVersion >= 19.00 && VersionInfo.FortniteVersion <= 22.40)
        {
            if (VersionInfo.FortniteVersion < 21 && VersionInfo.FortniteVersion != 20.00)
            {
                AssaultRifles.push_back(FLateGameItem(1, FindObject<UFortItemDefinition>(L"/FlipperGameplay/Items/Weapons/RedDotAR/WID_Assault_RedDotAR_Athena_SR.WID_Assault_RedDotAR_Athena_SR")));
                AssaultRifles.push_back(FLateGameItem(1, FindObject<UFortItemDefinition>(L"/FlipperGameplay/Items/Weapons/RedDotAR/WID_Assault_RedDotAR_Athena_VR.WID_Assault_RedDotAR_Athena_VR")));
            }

            AssaultRifles.push_back(FLateGameItem(1, FindObject<UFortItemDefinition>(L"/FlipperGameplay/Items/Weapons/CoreAR/WID_Assault_CoreAR_Athena_SR.WID_Assault_CoreAR_Athena_SR")));
            AssaultRifles.push_back(FLateGameItem(1, FindObject<UFortItemDefinition>(L"/FlipperGameplay/Items/Weapons/CoreAR/WID_Assault_CoreAR_Athena_VR.WID_Assault_CoreAR_Athena_VR")));
        }
        else
        {
            AssaultRifles = LateGameTiers({
                { LG_WEAPON(L"WID_Assault_AutoHigh_Athena_SR_Ore_T03"), LG_WEAPON(L"WID_Assault_AutoHigh_Athena_VR_Ore_T03") },
                { LG_WEAPON(L"WID_Assault_Auto_Athena_R_Ore_T03"), LG_WEAPON(L"WID_Assault_Auto_Athena_UC_Ore_T03") },
            });
        }
    }

    return LateGamePick(AssaultRifles);
}

FLateGameItem LateGame::GetUtility()
{
    if (VersionInfo.FortniteVersion < 11.00)
        return LateGameRoll({
        { 2, LG_WEAPON(L"WID_Sniper_BoltAction_Scope_Athena_R_Ore_T02") },
        { 2, LG_WEAPON(L"WID_Sniper_BoltAction_Scope_Athena_R_Ore_T03") },
        { 3, LG_WEAPON(L"WID_Sniper_BoltAction_Scope_Athena_VR_Ore_T02") },
        { 3, LG_WEAPON(L"WID_Sniper_BoltAction_Scope_Athena_VR_Ore_T03") },
        { 4, LG_WEAPON(L"WID_Sniper_BoltAction_Scope_Athena_SR_Ore_T02") },
        { 4, LG_WEAPON(L"WID_Sniper_BoltAction_Scope_Athena_SR_Ore_T03") },
        { 1, LG_WEAPON(L"WID_Sniper_NoScope_Athena_UC_Ore_T02") },
        { 1, LG_WEAPON(L"WID_Sniper_NoScope_Athena_UC_Ore_T03") },
        { 2, LG_WEAPON(L"WID_Sniper_NoScope_Athena_R_Ore_T02") },
        { 2, LG_WEAPON(L"WID_Sniper_NoScope_Athena_R_Ore_T03") },
        { 0, LG_WEAPON(L"WID_Pistol_AutoHeavy_Athena_C_Ore_T02") },
        { 0, LG_WEAPON(L"WID_Pistol_AutoHeavy_Athena_C_Ore_T03") },
        { 1, LG_WEAPON(L"WID_Pistol_AutoHeavy_Athena_UC_Ore_T02") },
        { 1, LG_WEAPON(L"WID_Pistol_AutoHeavy_Athena_UC_Ore_T03") },
        { 2, LG_WEAPON(L"WID_Pistol_AutoHeavy_Athena_R_Ore_T02") },
        { 2, LG_WEAPON(L"WID_Pistol_AutoHeavy_Athena_R_Ore_T03") },
        { 0, LG_WEAPON(L"WID_Pistol_AutoHeavySuppressed_Athena_C_Ore_T02") },
        { 0, LG_WEAPON(L"WID_Pistol_AutoHeavySuppressed_Athena_C_Ore_T03") },
        { 1, LG_WEAPON(L"WID_Pistol_AutoHeavySuppressed_Athena_UC_Ore_T02") },
        { 1, LG_WEAPON(L"WID_Pistol_AutoHeavySuppressed_Athena_UC_Ore_T03") },
        { 2, LG_WEAPON(L"WID_Pistol_AutoHeavySuppressed_Athena_R_Ore_T02") },
        { 2, LG_WEAPON(L"WID_Pistol_AutoHeavySuppressed_Athena_R_Ore_T03") },
        { 1, LG_WEAPON(L"WID_Pistol_Scavenger_Athena_UC_Ore_T02") },
        { 1, LG_WEAPON(L"WID_Pistol_Scavenger_Athena_UC_Ore_T03") },
        { 2, LG_WEAPON(L"WID_Pistol_Scavenger_Athena_R_Ore_T02") },
        { 2, LG_WEAPON(L"WID_Pistol_Scavenger_Athena_R_Ore_T03") },
        { 3, LG_WEAPON(L"WID_Pistol_Scavenger_Athena_VR_Ore_T02") },
        { 3, LG_WEAPON(L"WID_Pistol_Scavenger_Athena_VR_Ore_T03") } });
    static UEAllocatedVector<FLateGameItem> Utilities{};

    static bool bAdded = false;
    if (!bAdded)
    {
        bAdded = true;

        // Gold bolt and the tactical SMG; builds without them (1.7.2) get the best bolt / SMG they have.
        Utilities = LateGameTiers({
            { LG_WEAPON(L"WID_Sniper_BoltAction_Scope_Athena_SR_Ore_T03"), LG_WEAPON(L"WID_Pistol_Scavenger_Athena_VR_Ore_T03") },
            { LG_WEAPON(L"WID_Sniper_BoltAction_Scope_Athena_VR_Ore_T03"), LG_WEAPON(L"WID_Pistol_AutoHeavy_Athena_R_Ore_T03") },
            { LG_WEAPON(L"WID_Sniper_BoltAction_Scope_Athena_R_Ore_T03"), LG_WEAPON(L"WID_Pistol_AutoHeavy_Athena_UC_Ore_T03"), LG_WEAPON(L"WID_Pistol_AutoHeavy_Athena_C_Ore_T02") },
        });

        if (VersionInfo.FortniteVersion >= 24.20 && VersionInfo.FortniteVersion < 25)
        {
            auto ODMGear = FLateGameItem(3, FindObject<UFortWeaponRangedItemDefinition>(L"/DryBox/Items/NyxGlass/AGID_NyxGlass.AGID_NyxGlass"));
            if (ODMGear.Item)
                Utilities.push_back(ODMGear);
        }
    }

    return LateGamePick(Utilities);
}

FLateGameItem LateGame::GetHeal()
{
    static UEAllocatedVector<FLateGameItem> Heals{};

    static bool bAdded = false;
    if (!bAdded)
    {
        bAdded = true;

        auto BigPots = FLateGameItem(3, LateGameLoad(L"/Game/Athena/Items/Consumables/Shields/Athena_Shields.Athena_Shields"));
        auto Minis = FLateGameItem(6, LateGameLoad(L"/Game/Athena/Items/Consumables/ShieldSmall/Athena_ShieldSmall.Athena_ShieldSmall"));
        auto ChugSplash = FLateGameItem(6, LateGameLoad(L"/Game/Athena/Items/Consumables/ChillBronco/Athena_ChillBronco.Athena_ChillBronco"));
        auto SlurpJuice = FLateGameItem(2, LateGameLoad(L"/Game/Athena/Items/Consumables/PurpleStuff/Athena_PurpleStuff.Athena_PurpleStuff"));

        if (BigPots.Item)
            Heals.push_back(BigPots);

        if (Minis.Item)
            Heals.push_back(Minis);

        if (ChugSplash.Item)
            Heals.push_back(ChugSplash);

        if (SlurpJuice.Item)
            Heals.push_back(SlurpJuice);

        if (VersionInfo.FortniteVersion >= 19 && VersionInfo.FortniteVersion < 27)
        {
            auto MedMist = FLateGameItem(2, FindObject<UFortItemDefinition>(L"/FlipperGameplay/Items/HealSpray/WID_Athena_HealSpray.WID_Athena_HealSpray"));

            if (MedMist.Item)
                Heals.push_back(MedMist);
        }

        // Fewer than two kinds (1.7.2 may only have big shields): add a medkit / bandages, so the two heal slots differ.
        if (Heals.size() < 2)
        {
            auto Medkit = FLateGameItem(3, LateGameLoad(L"/Game/Athena/Items/Consumables/Medkit/Athena_Medkit.Athena_Medkit"));
            auto Bandage = FLateGameItem(15, LateGameLoad(L"/Game/Athena/Items/Consumables/Bandage/Athena_Bandage.Athena_Bandage"));
            if (Medkit.Item)
                Heals.push_back(Medkit);
            if (Bandage.Item)
                Heals.push_back(Bandage);
        }
    }

    return LateGamePick(Heals);
}

// One kind is picked at random among the kinds this build has (each kind equally likely), then its rarity is rolled
// like floor loot (LateGameRoll). Kinds missing from the build are skipped, so every season gets a full slot.
static FLateGameItem LateGameRollKinds(std::initializer_list<std::initializer_list<std::pair<int, const wchar_t*>>> Kinds)
{
    std::vector<const std::initializer_list<std::pair<int, const wchar_t*>>*> Have;
    for (auto& Kind : Kinds)
    {
        bool Any = false;
        for (auto& [Rarity, Path] : Kind)
            if (LateGameLoad(Path)) { Any = true; break; }
        if (Any)
            Have.push_back(&Kind);
    }
    if (Have.empty())
        return FLateGameItem(0, nullptr);
    return LateGameRoll(*Have[rand() % Have.size()]);
}

#define LG_CONSUMABLE(Folder, Name) L"/Game/Athena/Items/Consumables/" Folder L"/" Name L"." Name
#define LG_TRAP(Name) L"/Game/Athena/Items/Traps/" Name L"." Name

FLateGameItem LateGame::GetRifle()
{
    if (VersionInfo.FortniteVersion >= 11.00)
        return GetAssaultRifle();
    auto Item = LateGameRollKinds({
        // SCAR
        { { 3, LG_WEAPON(L"WID_Assault_AutoHigh_Athena_VR_Ore_T03") }, { 4, LG_WEAPON(L"WID_Assault_AutoHigh_Athena_SR_Ore_T03") } },
        // Blue AR
        { { 2, LG_WEAPON(L"WID_Assault_Auto_Athena_R_Ore_T03") }, { 2, LG_WEAPON(L"WID_Assault_Auto_Athena_R_Ore_T02") } },
        // Burst AR
        { { 0, LG_WEAPON(L"WID_Assault_SemiAuto_Athena_C_Ore_T03") }, { 1, LG_WEAPON(L"WID_Assault_SemiAuto_Athena_UC_Ore_T03") },
          { 2, LG_WEAPON(L"WID_Assault_SemiAuto_Athena_R_Ore_T03") }, { 3, LG_WEAPON(L"WID_Assault_SemiAuto_Athena_VR_Ore_T03") },
          { 4, LG_WEAPON(L"WID_Assault_SemiAuto_Athena_SR_Ore_T03") } },
        // Scoped AR
        { { 2, LG_WEAPON(L"WID_Assault_Surgical_Athena_R_Ore_T03") }, { 3, LG_WEAPON(L"WID_Assault_Surgical_Athena_VR_Ore_T03") } } });
    return Item.Item ? Item : GetAssaultRifle();
}

FLateGameItem LateGame::GetAnyShotgun()
{
    if (VersionInfo.FortniteVersion >= 11.00)
        return GetShotgun();
    auto Item = LateGameRollKinds({
        // Pump
        { { 0, LG_WEAPON(L"WID_Shotgun_Standard_Athena_C_Ore_T03") }, { 1, LG_WEAPON(L"WID_Shotgun_Standard_Athena_UC_Ore_T03") },
          { 2, LG_WEAPON(L"WID_Shotgun_Standard_Athena_R_Ore_T03") }, { 3, LG_WEAPON(L"WID_Shotgun_Standard_Athena_VR_Ore_T03") },
          { 4, LG_WEAPON(L"WID_Shotgun_Standard_Athena_SR_Ore_T03") } },
        // Tactical
        { { 0, LG_WEAPON(L"WID_Shotgun_SemiAuto_Athena_C_Ore_T03") }, { 1, LG_WEAPON(L"WID_Shotgun_SemiAuto_Athena_UC_Ore_T03") },
          { 2, LG_WEAPON(L"WID_Shotgun_SemiAuto_Athena_R_Ore_T03") }, { 3, LG_WEAPON(L"WID_Shotgun_SemiAuto_Athena_VR_Ore_T03") } },
        // Heavy
        { { 3, LG_WEAPON(L"WID_Shotgun_SlugFire_Athena_VR") }, { 4, LG_WEAPON(L"WID_Shotgun_SlugFire_Athena_SR") } } });
    return Item.Item ? Item : GetShotgun();
}

static FLateGameItem LateGameHealable()
{
    struct Entry { const wchar_t* Path; uint32 Count; };
    static const Entry Entries[] = {
        { LG_CONSUMABLE(L"Shields", L"Athena_Shields"), 3 },
        { LG_CONSUMABLE(L"ShieldSmall", L"Athena_ShieldSmall"), 6 },
        { LG_CONSUMABLE(L"PurpleStuff", L"Athena_PurpleStuff"), 2 },
        { LG_CONSUMABLE(L"ChillBronco", L"Athena_ChillBronco"), 6 },
        { LG_CONSUMABLE(L"Medkit", L"Athena_Medkit"), 3 },
        { LG_CONSUMABLE(L"Bandage", L"Athena_Bandage"), 15 },
        { LG_CONSUMABLE(L"SuperMedkit", L"Athena_SuperMedkit"), 1 },
    };
    UEAllocatedVector<FLateGameItem> Have;
    for (auto& E : Entries)
        if (auto Item = LateGameLoad(E.Path))
            Have.push_back(FLateGameItem(E.Count, Item));
    return Have.empty() ? LateGame::GetHeal() : LateGamePick(Have);
}

FLateGameItem LateGame::GetSmgOrHeal()
{
    if (VersionInfo.FortniteVersion < 11.00 && rand() % 100 < 30)
    {
        auto Smg = LateGameRollKinds({
            // SMG
            { { 0, LG_WEAPON(L"WID_Pistol_AutoHeavy_Athena_C_Ore_T03") }, { 1, LG_WEAPON(L"WID_Pistol_AutoHeavy_Athena_UC_Ore_T03") },
              { 2, LG_WEAPON(L"WID_Pistol_AutoHeavy_Athena_R_Ore_T03") } },
            // Suppressed SMG
            { { 0, LG_WEAPON(L"WID_Pistol_AutoHeavySuppressed_Athena_C_Ore_T03") }, { 1, LG_WEAPON(L"WID_Pistol_AutoHeavySuppressed_Athena_UC_Ore_T03") },
              { 2, LG_WEAPON(L"WID_Pistol_AutoHeavySuppressed_Athena_R_Ore_T03") } },
            // Tactical SMG
            { { 1, LG_WEAPON(L"WID_Pistol_Scavenger_Athena_UC_Ore_T03") }, { 2, LG_WEAPON(L"WID_Pistol_Scavenger_Athena_R_Ore_T03") },
              { 3, LG_WEAPON(L"WID_Pistol_Scavenger_Athena_VR_Ore_T03") } } });
        if (Smg.Item)
            return Smg;
    }
    return LateGameHealable();
}

FLateGameItem LateGame::GetExplosiveOrSniper()
{
    if (VersionInfo.FortniteVersion >= 11.00)
        return GetUtility();
    // Grenades are a kind of their own (a stack, no rarity).
    const bool HaveGrenades = LateGameLoad(LG_CONSUMABLE(L"Grenade", L"Athena_Grenade")) != nullptr;
    if (HaveGrenades && rand() % 4 == 0)
        return FLateGameItem(4, LateGameLoad(LG_CONSUMABLE(L"Grenade", L"Athena_Grenade")));
    auto Item = LateGameRollKinds({
        // Sniper
        { { 1, LG_WEAPON(L"WID_Sniper_NoScope_Athena_UC_Ore_T03") }, { 2, LG_WEAPON(L"WID_Sniper_NoScope_Athena_R_Ore_T03") },
          { 2, LG_WEAPON(L"WID_Sniper_BoltAction_Scope_Athena_R_Ore_T03") }, { 3, LG_WEAPON(L"WID_Sniper_BoltAction_Scope_Athena_VR_Ore_T03") },
          { 4, LG_WEAPON(L"WID_Sniper_BoltAction_Scope_Athena_SR_Ore_T03") } },
        // Rocket launcher
        { { 2, LG_WEAPON(L"WID_Launcher_Rocket_Athena_R_Ore_T03") }, { 3, LG_WEAPON(L"WID_Launcher_Rocket_Athena_VR_Ore_T03") },
          { 4, LG_WEAPON(L"WID_Launcher_Rocket_Athena_SR_Ore_T03") } },
        // Grenade launcher
        { { 2, LG_WEAPON(L"WID_Launcher_Grenade_Athena_R_Ore_T03") }, { 3, LG_WEAPON(L"WID_Launcher_Grenade_Athena_VR_Ore_T03") },
          { 4, LG_WEAPON(L"WID_Launcher_Grenade_Athena_SR_Ore_T03") } } });
    if (Item.Item)
        return Item;
    return HaveGrenades ? FLateGameItem(4, LateGameLoad(LG_CONSUMABLE(L"Grenade", L"Athena_Grenade"))) : GetUtility();
}

FLateGameItem LateGame::GetConsumable()
{
    return LateGameHealable();
}

FLateGameItem LateGame::GetTrap()
{
    if (rand() % 2 == 0)
        return FLateGameItem(0, nullptr);
    auto Damage = LateGameLoad(LG_TRAP(L"TID_Floor_Spikes_Athena_R_T03"));
    std::vector<const UFortItemDefinition*> Movement;
    for (auto Path : { LG_TRAP(L"TID_Floor_Player_Launch_Pad_Athena"), LG_TRAP(L"TID_Floor_Player_Jump_Pad_Athena"), LG_TRAP(L"TID_Context_BouncePad_Athena") })
        if (auto Item = LateGameLoad(Path))
            Movement.push_back(Item);
    const bool WantDamage = rand() % 2 == 0;
    if ((WantDamage || Movement.empty()) && Damage)
        return FLateGameItem(1, Damage);
    if (!Movement.empty())
        return FLateGameItem(1, Movement[rand() % Movement.size()]);
    return FLateGameItem(0, nullptr);
}

const UFortItemDefinition* LateGame::GetAmmo(EAmmoType AmmoType)
{
    static UEAllocatedVector<const UFortItemDefinition*> Ammos{ LateGameLoad(L"/Game/Athena/Items/Ammo/AthenaAmmoDataBulletsLight.AthenaAmmoDataBulletsLight"),
                                                                LateGameLoad(L"/Game/Athena/Items/Ammo/AthenaAmmoDataShells.AthenaAmmoDataShells"),
                                                                LateGameLoad(L"/Game/Athena/Items/Ammo/AthenaAmmoDataBulletsMedium.AthenaAmmoDataBulletsMedium"),
                                                                LateGameLoad(L"/Game/Athena/Items/Ammo/AmmoDataRockets.AmmoDataRockets"),
                                                                LateGameLoad(L"/Game/Athena/Items/Ammo/AthenaAmmoDataBulletsHeavy.AthenaAmmoDataBulletsHeavy") };

    return Ammos[(uint8)AmmoType];
}

const UFortItemDefinition* LateGame::GetResource(EFortResourceType ResourceType)
{
    static UEAllocatedVector<const UFortItemDefinition*> Resources{ LateGameLoad(L"/Game/Items/ResourcePickups/WoodItemData.WoodItemData"),
                                                                    LateGameLoad(L"/Game/Items/ResourcePickups/StoneItemData.StoneItemData"),
                                                                    LateGameLoad(L"/Game/Items/ResourcePickups/MetalItemData.MetalItemData") };

    return Resources[(uint8)ResourceType];
}
