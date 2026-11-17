#pragma once
#include "framework.h"
#include <cmath>
#include <fstream>
#include <string>
// The Syndicate menu's server switches on Season X. The menu keeps files in
// %LOCALAPPDATA%\ProjectTilted fresh (rewritten every second) while an option is on;
// a file older than 3 s is ignored, so a closed or crashed game never leaves one on.
// Honored only while you are the one real player in the match: bots never get them.
namespace MenuSwitches
{
inline std::wstring SwitchPath(const wchar_t *Name)
{
    wchar_t Base[MAX_PATH]{};
    if (!GetEnvironmentVariableW(L"LOCALAPPDATA", Base, MAX_PATH))
        return {};
    return std::wstring(Base) + L"\\ProjectTilted\\" + Name;
}
inline bool Fresh(const wchar_t *Name)
{
    const std::wstring Path = SwitchPath(Name);
    WIN32_FILE_ATTRIBUTE_DATA A{};
    if (Path.empty() || !GetFileAttributesExW(Path.c_str(), GetFileExInfoStandard, &A))
        return false;
    FILETIME Now{};
    GetSystemTimeAsFileTime(&Now);
    const ULONGLONG N = (ULONGLONG(Now.dwHighDateTime) << 32) | Now.dwLowDateTime;
    const ULONGLONG W = (ULONGLONG(A.ftLastWriteTime.dwHighDateTime) << 32) | A.ftLastWriteTime.dwLowDateTime;
    return N >= W ? (N - W) / 10000 <= 3000 : true;
}
inline float Number(const wchar_t *Name, float Default, float Min, float Max)
{
    std::ifstream In(SwitchPath(Name));
    float V = 0;
    if (!(In >> V) || !std::isfinite(V))
        return Default;
    return V < Min ? Min : (V > Max ? Max : V);
}

inline bool Ammo = false, Mats = false, Fly = false, God = false;
inline float FlySpeed = 2400.f, Speed = 1.f, NextRead = 0.f;
inline AFortPlayerControllerAthena *Human = nullptr;
inline AFortPlayerPawnAthena *Flying = nullptr, *SpedUp = nullptr, *Immortal = nullptr;

// Infinite materials: the loadout sync sets bBuildFree from this every half second.
inline bool FreeBuild(AFortPlayerControllerAthena *PC) { return Mats && PC && PC == Human; }

inline void Tick(AFortGameModeAthena *GM)
{
    if (!GM)
        return;
    const float N = Syndicate::Now();
    if (N >= NextRead)
    {
        NextRead = N + 0.25f;
        const bool Alone = GM->AlivePlayers.Num() == 1;
        const bool WasAmmo = Ammo, WasMats = Mats, WasFly = Fly, WasGod = God;
        const float WasSpeed = Speed;
        Ammo = Alone && Fresh(L"menu-infinite-ammo.txt");
        Mats = Alone && Fresh(L"menu-infinite-mats.txt");
        Fly = Alone && Fresh(L"menu-fly.txt");
        if (Fly)
            FlySpeed = 1200.f * Number(L"menu-fly.txt", 2.f, 0.5f, 5.f);
        Speed = (Alone && Fresh(L"menu-speed.txt")) ? Number(L"menu-speed.txt", 2.5f, 1.f, 3.f) : 1.f;
        God = Alone && Fresh(L"menu-god.txt");
        if (Ammo != WasAmmo) Log(std::string("Menu switch: infinite ammo ") + (Ammo ? "on" : "off"));
        if (Mats != WasMats) Log(std::string("Menu switch: infinite materials ") + (Mats ? "on" : "off"));
        if (Fly != WasFly) Log(std::string("Menu switch: fly ") + (Fly ? "on, speed " + std::to_string((int)FlySpeed) : "off"));
        if (Speed != WasSpeed) Log("Menu switch: movement speed x" + std::to_string(Speed));
        if (God != WasGod) Log(std::string("Menu switch: god mode ") + (God ? "on" : "off"));
    }
    Human = GM->AlivePlayers.Num() == 1 ? GM->AlivePlayers[0] : nullptr;
    auto Pawn = Human ? Cast<AFortPlayerPawnAthena>(Human->Pawn) : nullptr;
    if (Pawn && Pawn->IsDead())
        Pawn = nullptr;

    if (Human && Human->bInfiniteAmmo != Ammo)
        Human->bInfiniteAmmo = Ammo;
    if (Ammo && Pawn && Pawn->CurrentWeapon)
    {
        auto Weapon = Pawn->CurrentWeapon;
        const int32 Clip = Weapon->GetBulletsPerClip();
        if (Clip > 0 && Weapon->AmmoCount < Clip)
        {
            Weapon->AmmoCount = Clip;
            Weapon->ForceNetUpdate();
        }
    }

    // Movement speed: the pawn's own multiplier; the menu sets the same value in your game.
    if (Pawn && Speed != 1.f && SpedUp != Pawn)
    {
        Pawn->SetMovementSpeedMultiplier(Speed);
        SpedUp = Pawn;
    }
    else if (Pawn && Speed != 1.f && SpedUp == Pawn)
    {
        static float Applied = 0.f;
        if (Applied != Speed) { Pawn->SetMovementSpeedMultiplier(Speed); Applied = Speed; }
    }
    else if (Speed == 1.f && SpedUp)
    {
        if (SpedUp == Pawn) Pawn->SetMovementSpeedMultiplier(1.f);
        SpedUp = nullptr;
    }

    // God mode: damage refused and health kept full.
    if (God && Pawn)
    {
        if (Pawn->bCanBeDamaged || Immortal != Pawn)
        {
            Pawn->bCanBeDamaged = false;
            Pawn->ForceNetUpdate();
            Immortal = Pawn;
        }
        if (Pawn->GetHealth() > 0.f && Pawn->GetHealth() < 100.f)
            Pawn->SetHealth(100.f);
    }
    else if (!God && Immortal)
    {
        if (Immortal == Pawn) { Pawn->bCanBeDamaged = true; Pawn->ForceNetUpdate(); }
        Immortal = nullptr;
    }

    // Fly: Unreal's "fly" cheat, on the server and by its client call in your game.
    auto Movement = Pawn ? Pawn->CharacterMovement : nullptr;
    if (Fly && Pawn && Movement)
    {
        if (Flying != Pawn)
        {
            Flying = Pawn;
            Pawn->ClientCheatFly();
            Movement->bCheatFlying = true;
            Movement->SetMovementMode(EMovementMode::MOVE_Flying, 0);
        }
        Movement->MaxFlySpeed = FlySpeed;
    }
    else if (!Fly && Flying)
    {
        if (Flying == Pawn && Movement)
        {
            Pawn->ClientCheatWalk();
            Movement->bCheatFlying = false;
            Movement->SetMovementMode(EMovementMode::MOVE_Falling, 0);
        }
        Flying = nullptr;
    }
}
} // namespace MenuSwitches
