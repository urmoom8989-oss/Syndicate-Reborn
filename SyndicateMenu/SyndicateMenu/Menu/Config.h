#pragma once

#include <windows.h>

struct Config {
    bool menuOpen;

    bool espEnabled;
    bool noSelfEsp;
    bool showEnemies;
    bool showTeammates;
    bool teamCheck;
    bool visibleCheck;
    bool onlyOnScreen;
    float maxDistance;

    bool espBox;
    bool boxOutline;
    bool boxFilled;
    bool cornerBox;
    float boxThickness;
    float boxWidth;
    float boxColor[4];
    float boxFill[4];

    bool espSkeleton;
    bool skeletonOutline;
    bool skeletonCurved;
    bool skeletonSpaghetti;
    float skeletonThickness;
    float skeletonColor[4];

    bool espName;
    bool espDist;
    bool espWeapon;
    bool espLine;
    int snapFrom;
    float nameColor[4];
    float distColor[4];
    float snapColor[4];

    bool healthBar;
    int healthBarSide;
    float healthColor[4];

    bool chinaHat;
    bool selfChinaHat;
    float chinaThickness;
    float chinaColor[4];

    bool chams;
    bool chamsWireframe;
    float chamsColor[4];

    bool espFov;
    float fovThickness;
    float fovColor[4];

    bool aimbot;
    bool silentAim;
    bool magicBullet;
    bool alwaysHeadshots; // server switch (menu-headshots.txt): every hit on a bot counts as a headshot
    bool sniperPump;     // server switch (menu-sniper-pump.txt): shotgun shots hit the bot you aim at, up to 100 m, bots only
    bool sniperPumpFov;  // only inside the aim FOV (off: any bot in sight in front of you)
    bool infiniteAmmo;
    bool infiniteMats;
    bool drawTarget;
    bool noSpread;
    bool noBloom;
    int aimKey;
    float aimFov;
    float aimSmooth;
    int aimBone;
    float noSpreadAmount;

    bool floorLoot;
    bool chestEsp;
    bool lootDistance;
    float lootRange;
    bool lootExcludeAmmo;
    bool lootExcludeMats;
    bool lootCommon;
    bool lootUncommon;
    bool lootRare;
    bool lootEpic;
    bool lootLegendary;
    bool lootMythic;

    bool vehicleFly;
    float vehicleSpeed;
    bool spinbot;
    bool debugOverlay; // Syndicate: live feature status on screen (Misc > Session)
    bool streamProof; // Syndicate: menu + ESP drawn on a window that recording/streaming apps skip (Stream tab)
    float spinSpeed;
    bool timeOfDay;    // your game only: the time of day stays at timeOfDayHour
    float timeOfDayHour;
    bool ziplineFly;
    bool fovChanger;
    float fovValue;
    bool autoRevive;
    bool instantRevive;
    bool adsInAir;
    bool fly;          // server switch (menu-fly.txt), toggle key flyKey
    float flySpeed;    // 0.5x..10x of 1200 units/s
    int flyKey;        // virtual key, 0 = none
    bool speedBoost;   // server switch (menu-speed.txt)
    float speedScale;  // 1x..3x
    bool godMode;      // server switch (menu-god.txt)
    bool healMoving;   // server command (menu-command.txt "heal"): the heal key uses your best heal at once, no animation
    int healKey;       // virtual key for healMoving, 0 = none
    int tpKey;         // virtual key for "Teleport to bot", 0 = none
    int menuKey;       // second key that opens the menu (Insert always does), 0 = none
    int spawnItem;     // index into kSpawnItems
    bool tpPending;    // set by the Teleport to bot button
    bool spawnPending; // set by the Spawn item button
    bool healPending;  // set by the heal key

    bool projectileTp;
    bool noRecoil;
    float noRecoilAmount;
    bool noReload;
    float noReloadAmount;
    bool noCharge;
    float noChargeAmount;
    bool rapidFire;
    float rapidFireAmount;
    bool damageMultiplier;
    float damageScale;
    bool fastPickaxe;
    float pickaxeSpeed;
    bool automaticWeapons;
    bool doublePump;
    bool distanceMultiplier;
    float distanceScale;

    char serverName[32];
    bool namePending;

    Config() {
        menuOpen = false;

        espEnabled = true;
        noSelfEsp = true;
        showEnemies = true;
        showTeammates = false;
        teamCheck = true;
        visibleCheck = false;
        onlyOnScreen = true;
        maxDistance = 0.f;

        espBox = true;
        boxOutline = true;
        boxFilled = false;
        cornerBox = false;
        boxThickness = 1.6f;
        boxWidth = 0.45f;
        boxColor[0] = 1.f; boxColor[1] = 0.24f; boxColor[2] = 0.24f; boxColor[3] = 1.f;
        boxFill[0] = 1.f; boxFill[1] = 0.24f; boxFill[2] = 0.24f; boxFill[3] = 0.15f;

        espSkeleton = true;
        skeletonOutline = true;
        skeletonCurved = false;
        skeletonSpaghetti = false;
        skeletonThickness = 1.4f;
        skeletonColor[0] = 1.f; skeletonColor[1] = 1.f; skeletonColor[2] = 1.f; skeletonColor[3] = 1.f;

        espName = true;
        espDist = true;
        espWeapon = false;
        espLine = false;
        snapFrom = 0;
        nameColor[0] = 1.f; nameColor[1] = 1.f; nameColor[2] = 1.f; nameColor[3] = 1.f;
        distColor[0] = 0.78f; distColor[1] = 0.78f; distColor[2] = 0.78f; distColor[3] = 1.f;
        snapColor[0] = 1.f; snapColor[1] = 0.24f; snapColor[2] = 0.24f; snapColor[3] = 1.f;

        healthBar = false;
        healthBarSide = 0;
        healthColor[0] = 0.12f; healthColor[1] = 0.86f; healthColor[2] = 0.32f; healthColor[3] = 1.f;

        chinaHat = false;
        selfChinaHat = false;
        chinaThickness = 1.5f;
        chinaColor[0] = 0.9f; chinaColor[1] = 0.9f; chinaColor[2] = 0.9f; chinaColor[3] = 1.f;

        chams = false;
        chamsWireframe = false;
        chamsColor[0] = 0.12f; chamsColor[1] = 0.76f; chamsColor[2] = 0.29f; chamsColor[3] = 1.f;

        espFov = true;
        fovThickness = 1.5f;
        fovColor[0] = 0.44f; fovColor[1] = 0.47f; fovColor[2] = 1.f; fovColor[3] = 1.f;

        aimbot = true;
        silentAim = false;
        magicBullet = false;
        alwaysHeadshots = false;
        sniperPump = false;
        sniperPumpFov = false;
        infiniteAmmo = false;
        infiniteMats = false;
        drawTarget = false;
        noSpread = false;
        noBloom = false;
        aimKey = VK_RBUTTON;
        aimFov = 10.f;
        aimSmooth = 8.f;
        aimBone = 0;
        noSpreadAmount = 1.f;

        floorLoot = false;
        chestEsp = false;
        lootDistance = true;
        lootRange = 0.f;
        lootExcludeAmmo = false;
        lootExcludeMats = false;
        lootCommon = true;
        lootUncommon = true;
        lootRare = true;
        lootEpic = true;
        lootLegendary = true;
        lootMythic = true;

        vehicleFly = false;
        vehicleSpeed = 4.f;
        spinbot = false;
        debugOverlay = false;
        streamProof = false;
        spinSpeed = 15.f;
        timeOfDay = false;
        timeOfDayHour = 12.f;
        ziplineFly = false;
        fovChanger = false;
        fovValue = 45.f;
        autoRevive = false;
        instantRevive = false;
        adsInAir = false;
        fly = false;
        flySpeed = 2.f;
        flyKey = 'F';
        speedBoost = false;
        speedScale = 2.5f;
        godMode = false;

        projectileTp = false;
        noRecoil = false;
        noRecoilAmount = 1.f;
        noReload = false;
        noReloadAmount = 1.f;
        noCharge = false;
        noChargeAmount = 1.f;
        rapidFire = false;
        rapidFireAmount = 2.f;
        damageMultiplier = false;
        damageScale = 2.f;
        fastPickaxe = false;
        pickaxeSpeed = 2.f;
        automaticWeapons = false;
        doublePump = false;
        distanceMultiplier = false;
        distanceScale = 10.f;

        serverName[0] = 0;
        namePending = false;
    }
};

extern Config cheat;

inline const int kKeyList[] = { VK_RBUTTON, VK_LBUTTON, VK_MBUTTON, VK_SHIFT, VK_MENU, 'X', 'C', 'V' };
inline const char* kKeyName[] = { "RMB", "LMB", "MMB", "SHIFT", "ALT", "X", "C", "V" };
// "Menu key": a second key that opens the menu, for keyboards without Insert (60% / laptop keyboards).
inline const int kMenuKeyList[] = { 0, VK_HOME, VK_PRIOR, VK_NEXT, VK_PAUSE, VK_F6, VK_F7, VK_F8, VK_F9, VK_F10, VK_F12 };
inline const int kFlyKeyList[] = { 0, 'F', 'G', 'H', 'J', 'K', 'L', 'Z', 'X', 'V', VK_CAPITAL, VK_XBUTTON1, VK_XBUTTON2 };
// "Spawn item" (menu-command.txt give|<key>): the key the match server knows, and what the dropdown shows.
inline const char* kSpawnItemKeys[] = { "ar", "pump", "tac", "smg", "sniper", "rocket", "lmg", "missile", "shield", "mini", "medkit", "bandage", "chug", "boogie", "fort" };
inline const char* kBoneName[] = { "Head", "Neck", "Chest", "Pelvis" };
inline const char* kSnapFrom[] = { "Bottom", "Center", "Top" };
inline const char* kBarSide[] = { "Left", "Right" };

inline bool KeyDown(int vk) { return (GetAsyncKeyState(vk) & 0x8000) != 0; }

inline unsigned ColorU32(const float c[4]) {
    auto b = [](float v) -> int {
        int n = (int)(v * 255.f + 0.5f);
        if (n < 0) n = 0;
        if (n > 255) n = 255;
        return n;
    };
    return (unsigned)((b(c[3]) << 24) | (b(c[2]) << 16) | (b(c[1]) << 8) | b(c[0]));
}
