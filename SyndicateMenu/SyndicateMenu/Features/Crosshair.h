#pragma once

// Syndicate custom crosshair: drawn on the game's own frame (the Present hook), at the exact centre
// of what the game renders, so it stays centred in windowed, fullscreen and stretched resolutions.
// The launcher (Settings > Display > Crosshair) writes crosshair.ini to %LOCALAPPDATA%\ProjectTilted;
// changes there show up in game within half a second, even mid-match.
//
// The launcher loads SyndicateMenu.dll for the crosshair alone when the menu setting is off. It then
// writes menu-load.txt = "crosshair" first, and the DLL runs in crosshair-only mode: no game-object
// scan, no hooks besides Present/ResizeBuffers, no keys (Insert and End go to the game), no menu.

bool CrosshairOnlyMode();     // read once from menu-load.txt
void TickCrosshairConfig();   // reloads crosshair.ini when it changed (cheap, checks twice a second)
void DrawCrosshair(float width, float height, bool menuOpen);
