#pragma once

struct ID3D11Device;

void LoadSyndicateLogo(ID3D11Device* Device);
void InitMenu();
void UpdateMenuFonts();
void DrawMenu();
void SyncMenuOptions();
void ToggleMenu();
void TickMenuMotion();
float MenuMotion();
float MenuIntroT();
bool MenuIntroActive();
bool MenuVisible();
bool MenuCapturesInput();
void TickMenuHotkeys(); // fly key, read every frame even with the menu closed
