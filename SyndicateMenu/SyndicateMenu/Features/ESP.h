#pragma once

#include <windows.h>

void DrawESP();
void NoteRenderThread();
bool InstallChamsHook();
bool ChamsOnGameThread();
void TickHudChamsHook();
void UninstallHudChamsHook();
void DrawDebugOverlay();
extern volatile LONG HudHookFires;
