#pragma once
#include "../../pch.h"
#include "../../FortniteGame/Public/BuildingSMActor.h"
#include "../../FortniteGame/Public/FortInventory.h"

struct FLateGameItem
{
    uint32 Count;
    const UFortItemDefinition* Item;
};

enum class EAmmoType : uint8
{
    Assault = 0,
    Shotgun = 1,
    Submachine = 2,
    Rocket = 3,
    Sniper = 4
};

class LateGame
{
public:
    static FLateGameItem GetShotgun();
    static FLateGameItem GetAssaultRifle();
    static FLateGameItem GetUtility();
    static FLateGameItem GetHeal();

    // Chapter 1 late game loadout (Edwin's list, 2026-11-17). Each call rolls again:
    static FLateGameItem GetRifle();        // slot 1: SCAR, blue AR, burst AR or scoped AR
    static FLateGameItem GetAnyShotgun();   // slot 2: pump, tactical or heavy, random rarity
    static FLateGameItem GetSmgOrHeal();    // slot 3: 30% an SMG (random kind and rarity), 70% a healable
    static FLateGameItem GetExplosiveOrSniper(); // slot 4: sniper, rocket launcher, grenade launcher or grenades
    static FLateGameItem GetConsumable();   // slot 5: a random consumable / healable
    static FLateGameItem GetTrap();         // 50% nothing, else a damage trap or a movement trap (launch / bounce pad)

    static const UFortItemDefinition* GetAmmo(EAmmoType);
    static const UFortItemDefinition* GetResource(EFortResourceType);
};