#pragma once
#include <string>
#include <unordered_set>
// Explicit launch-version 4.00 firearms; excludes vaulted and prototype assets.
inline bool SyndicateSeason4Gun(const std::string& Name, float Version=4.f) {
    if(Version>=4.5f && Version<5.f && (Name=="WID_DualPistol_SemiAuto_Athena_VR_Ore_T03" || Name=="WID_DualPistol_SemiAuto_Athena_SR_Ore_T03"))return true;
    static const std::unordered_set<std::string> Allowed={
        "WID_Sniper_Standard_Scope_Athena_VR_Ore_T03",
        "WID_Sniper_Standard_Scope_Athena_SR_Ore_T03",
        "WID_Sniper_BoltAction_Scope_Athena_VR_Ore_T03",
        "WID_Sniper_BoltAction_Scope_Athena_SR_Ore_T03",
        "WID_Sniper_BoltAction_Scope_Athena_R_Ore_T03",
        "WID_Shotgun_Standard_Athena_UC_Ore_T03",
        "WID_Shotgun_Standard_Athena_C_Ore_T03",
        "WID_Shotgun_SemiAuto_Athena_VR_Ore_T03",
        "WID_Shotgun_SemiAuto_Athena_UC_Ore_T03",
        "WID_Shotgun_SemiAuto_Athena_R_Ore_T03",
        "WID_Pistol_SixShooter_Athena_UC_Ore_T03",
        "WID_Pistol_SixShooter_Athena_R_Ore_T03",
        "WID_Pistol_SixShooter_Athena_C_Ore_T02",
        "WID_Pistol_SemiAuto_Athena_VR_Ore_T03",
        "WID_Pistol_SemiAuto_Athena_UC_Ore_T03",
        "WID_Pistol_SemiAuto_Athena_SR_Ore_T03",
        "WID_Pistol_SemiAuto_Athena_R_Ore_T03",
        "WID_Pistol_SemiAuto_Athena_C_Ore_T02",
        "WID_Pistol_Scavenger_Athena_VR_Ore_T03",
        "WID_Pistol_Scavenger_Athena_UC_Ore_T03",
        "WID_Pistol_Scavenger_Athena_R_Ore_T03",
        "WID_Pistol_AutoHeavySuppressed_Athena_UC_Ore_T03",
        "WID_Pistol_AutoHeavySuppressed_Athena_C_Ore_T02",
        "WID_Pistol_AutoHeavySuppressed_Athena_R_Ore_T03",
        "WID_Launcher_Rocket_Athena_VR_Ore_T03",
        "WID_Launcher_Rocket_Athena_R_Ore_T03",
        "WID_Launcher_Rocket_Athena_SR_Ore_T03",
        "WID_Launcher_Grenade_Athena_VR_Ore_T03",
        "WID_Launcher_Grenade_Athena_R_Ore_T03",
        "WID_Launcher_Grenade_Athena_SR_Ore_T03",
        "WID_Assault_Surgical_Athena_VR_Ore_T03",
        "WID_Assault_Surgical_Athena_R_Ore_T03",
        "WID_Assault_SemiAuto_Athena_UC_Ore_T03",
        "WID_Assault_SemiAuto_Athena_R_Ore_T03",
        "WID_Assault_SemiAuto_Athena_C_Ore_T02",
        "WID_Assault_AutoHigh_Athena_VR_Ore_T03",
        "WID_Assault_AutoHigh_Athena_SR_Ore_T03",
        "WID_Assault_Auto_Athena_UC_Ore_T03",
        "WID_Assault_Auto_Athena_R_Ore_T03",
        "WID_Assault_Auto_Athena_C_Ore_T02",
        "WID_Pistol_HandCannon_Athena_SR_Ore_T03",
        "WID_Pistol_HandCannon_Athena_VR_Ore_T03",
        "WID_Shotgun_SlugFire_Athena_SR",
        "WID_Shotgun_SlugFire_Athena_VR",
        "WID_Sniper_NoScope_Athena_R_Ore_T03",
        "WID_Sniper_NoScope_Athena_UC_Ore_T03"
    };
    return Allowed.count(Name)!=0;
}
