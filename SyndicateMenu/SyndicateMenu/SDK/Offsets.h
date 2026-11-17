#pragma once

#include <cstdint>

#include "Types.h"

struct Offsets {
    int GameViewport = 0;
    int ViewportWorld = 0;
    int ViewportGameInstance = 0;
    int LocalPlayers = 0;
    int PlayerController = 0;
    int AcknowledgedPawn = 0;
    int ControllerPawn = 0;
    int ControllerPlayerState = 0;
    int PlayerCameraManager = 0;
    int CameraCachePrivate = 0;
    int ControlRotation = 0;
    int PersistentLevel = 0;
    int Levels = 0;
    int GameState = 0;
    int OwningGameInstance = 0;
    int LevelActors = 0;
    int RootComponent = 0;
    int PlayerState = 0;
    int Mesh = 0;
    int PlayerName = 0;
    int TeamIndex = 0;
    int bIsDying = 0;
    uint8_t bIsDyingMask = 0;
    int RelativeLocation = 0;
    int ComponentToWorld = 0;
    int BoneArray = 0;
    int MasterPose = 0;
    int CachedBounds = 0;
    int LastRenderTime = 0;
    int TimeSeconds = 0;
    int CurrentWeapon = 0;
    // Syndicate: bots-only targeting (PlayerState.bIsABot) and the human check (GameStateBase.PlayerArray).
    int bIsABot = 0;
    uint8_t bIsABotMask = 0;
    int PlayerArray = 0;
    int WeaponData = 0;
};

extern Offsets off;

extern uintptr_t Class_Engine;
extern uintptr_t Class_FortPawn;
extern uintptr_t Class_PlayerState;
extern uintptr_t Fn_GetCamLoc;
extern uintptr_t Fn_GetCamRot;
extern uintptr_t Fn_GetFOV;
extern uintptr_t Fn_GetBoneIndex;
extern uintptr_t Fn_GetCompToWorld;
