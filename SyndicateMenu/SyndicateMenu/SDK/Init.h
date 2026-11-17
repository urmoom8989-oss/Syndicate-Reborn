#pragma once

#include <cstdint>

bool SetupGObjects();
bool SetupFMemory();
bool SetupFNameToString();
bool SetupFNameCtor();
bool SetupStructOffsets();
bool SetupProcessEvent();
void SetupGameOffsets();
bool InitSDK();

// The Fortnite build this game is ("Release-10.40-CL-9380822"), read from the game's own
// build string; empty when not found. Logged at start and shown in the debug overlay.
extern char g_gameBuild[64];
void DetectGameBuild();

uintptr_t FindEngine();
uintptr_t FindWorld();
void DiscoverLevelActors(uintptr_t level);
