#include "../framework/headers/includes.h"
#include "Menu.h"
#include "Config.h"
#include "Overlay.h"

static bool g_wantOpen = false;
static float g_motion = 0.f;
static bool g_introPlayed = false;
static bool g_introActive = false;
static float g_introT = 0.f;

static bool Flag(const char* name) {
    checkbox_t* data = cfg->fill<checkbox_t>(name);
    return data && data->callback;
}

static float SliderF(const char* name, float fallback = 0.f) {
    slider_float_t* data = cfg->fill<slider_float_t>(name);
    return data ? data->callback : fallback;
}

static int SliderI(const char* name, int fallback = 0) {
    slider_int_t* data = cfg->fill<slider_int_t>(name);
    return data ? data->callback : fallback;
}

static int Drop(const char* name) {
    dropdown_t* data = cfg->fill<dropdown_t>(name);
    return data ? data->callback : 0;
}

static void CopyColor(const char* name, float out[4]) {
    color_edit_t* color = cfg->fill<color_edit_t>(name);
    if (!color)
        return;
    out[0] = color->color[0];
    out[1] = color->color[1];
    out[2] = color->color[2];
    out[3] = color->color[3];
}

static float ClampF(float value, float min, float max) {
    if (value < min)
        return min;
    if (value > max)
        return max;
    return value;
}

static void SyncOptions() {
    cheat.aimbot = Flag("Enable aim assist");
    cheat.silentAim = Flag("Silent aim");
    cheat.magicBullet = Flag("Magic Bullet");
    cheat.alwaysHeadshots = Flag("Always headshots");
    cheat.sniperPump = Flag("Sniper pump");
    cheat.sniperPumpFov = Flag("Sniper pump uses aim FOV");
    cheat.noSpread = Flag("No spread");
    cheat.noBloom = Flag("No bloom");
    cheat.espFov = Flag("Draw FOV circle");
    cheat.drawTarget = Flag("Draw target highlight");

    cheat.aimFov = ClampF(SliderF("Aim FOV", 10.f), 1.f, 18.4f);
    cheat.aimSmooth = ClampF(SliderF("Smoothness", 8.f), 1.f, 20.f);
    cheat.fovThickness = SliderF("FOV thickness", 1.5f);
    cheat.noSpreadAmount = ClampF(SliderF("Spread amount", 1.f), 0.f, 1.f);

    int keyIdx = Drop("Aim key");
    if (keyIdx < 0) keyIdx = 0;
    if (keyIdx > 7) keyIdx = 7;
    cheat.aimKey = kKeyList[keyIdx];
    int bone = Drop("Aim bone");
    if (bone < 0) bone = 0;
    if (bone > 3) bone = 3;
    cheat.aimBone = bone;
    CopyColor("FOV color", cheat.fovColor);

    cheat.espEnabled = Flag("Enable ESP");
    cheat.espBox = Flag("Player box");
    cheat.healthBar = Flag("Player health bar");
    cheat.espName = Flag("Player name");
    cheat.espSkeleton = Flag("Player skeleton");
    cheat.skeletonCurved = Flag("Curved skeleton");
    cheat.skeletonSpaghetti = Flag("Spaghetti skeleton");
    cheat.cornerBox = Flag("Corner box");
    cheat.boxOutline = Flag("Box outline");
    cheat.boxFilled = Flag("Box fill");
    cheat.skeletonOutline = Flag("Skeleton outline");
    cheat.espLine = Flag("Snapline");
    cheat.espDist = Flag("Distance");
    cheat.chinaHat = Flag("China hat");
    cheat.selfChinaHat = Flag("Self china hat");
    cheat.chams = Flag("Chams");
    cheat.chamsWireframe = Flag("Wireframe chams");
    cheat.noSelfEsp = Flag("No self ESP");
    if (cheat.chams) {
        checkbox_t* selfEsp = cfg->fill<checkbox_t>("No self ESP");
        if (selfEsp)
            selfEsp->callback = true;
        cheat.noSelfEsp = true;
    }
    cheat.teamCheck = Flag("Team check");
    cheat.visibleCheck = Flag("Visible check");
    cheat.showTeammates = Flag("Show on teammates");
    cheat.showEnemies = Flag("Show on enemies");
    cheat.onlyOnScreen = Flag("Only show on screen");

    cheat.boxThickness = SliderF("Box thickness", 1.6f);
    cheat.boxWidth = SliderF("Box width", 0.45f);
    cheat.skeletonThickness = SliderF("Skeleton thickness", 1.4f);
    cheat.chinaThickness = SliderF("China thickness", 1.5f);
    cheat.maxDistance = (float)SliderI("Render distance", 0);
    cheat.snapFrom = Drop("Snapline from");
    cheat.healthBarSide = Drop("Health bar side");

    CopyColor("Player box##color", cheat.boxColor);
    CopyColor("Player skeleton##color", cheat.skeletonColor);
    CopyColor("Player health bar##color", cheat.healthColor);
    CopyColor("Box fill##color", cheat.boxFill);
    CopyColor("Name color", cheat.nameColor);
    CopyColor("Distance color", cheat.distColor);
    CopyColor("Snapline color", cheat.snapColor);
    CopyColor("China hat##color", cheat.chinaColor);
    CopyColor("Chams color", cheat.chamsColor);

    cheat.floorLoot = Flag("Floor loot");
    cheat.chestEsp = Flag("Chests");
    cheat.lootDistance = Flag("Loot distance");
    cheat.lootRange = SliderF("Loot range", 0.f);
    if (cheat.lootRange < 0.f)
        cheat.lootRange = 0.f;
    cheat.lootExcludeAmmo = Flag("Exclude ammo");
    cheat.lootExcludeMats = Flag("Exclude materials");
    cheat.lootCommon = Flag("Common");
    cheat.lootUncommon = Flag("Uncommon");
    cheat.lootRare = Flag("Rare");
    cheat.lootEpic = Flag("Epic");
    cheat.lootLegendary = Flag("Legendary");
    cheat.lootMythic = Flag("Mythic");

    cheat.vehicleFly = Flag("Vehicle fly");
    cheat.vehicleSpeed = ClampF(SliderF("Vehicle speed", 4.f), 1.f, 15.f);
    cheat.spinbot = Flag("Spinbot");
    cheat.debugOverlay = Flag("Debug overlay");
    cheat.streamProof = Flag("Stream proof");
    cheat.spinSpeed = ClampF(SliderF("Spin speed", 15.f), 1.f, 30.f);
    cheat.timeOfDay = Flag("Time of day");
    cheat.timeOfDayHour = ClampF(SliderF("Hour of day", 12.f), 0.f, 24.f);
    cheat.ziplineFly = Flag("Zipline fly");
    cheat.fovChanger = Flag("FOV changer");
    cheat.fovValue = ClampF(SliderF("FOV value", 45.f), 0.f, 180.f);
    cheat.autoRevive = Flag("Auto revive");
    cheat.instantRevive = Flag("Instant revive");
    cheat.adsInAir = Flag("ADS in air");
    cheat.flySpeed = ClampF(SliderF("Fly speed", 2.f), 0.5f, 10.f);
    int flyIdx = Drop("Fly key");
    if (flyIdx < 0 || flyIdx >= (int)(sizeof(kFlyKeyList) / sizeof(kFlyKeyList[0])))
        flyIdx = 0;
    cheat.flyKey = kFlyKeyList[flyIdx];
    cheat.fly = Flag("Fly");
    cheat.speedBoost = Flag("Speed boost");
    cheat.speedScale = ClampF(SliderF("Speed multiplier", 2.5f), 1.f, 3.f);
    cheat.godMode = Flag("God mode");
    cheat.healMoving = Flag("Heal while moving");
    {
        const int keys = (int)(sizeof(kFlyKeyList) / sizeof(kFlyKeyList[0]));
        int heal = Drop("Heal key"), tp = Drop("Teleport key"), item = Drop("Spawn item");
        cheat.healKey = heal >= 0 && heal < keys ? kFlyKeyList[heal] : 0;
        cheat.tpKey = tp >= 0 && tp < keys ? kFlyKeyList[tp] : 0;
        const int menu = Drop("Menu key");
        cheat.menuKey = menu > 0 && menu < (int)(sizeof(kMenuKeyList) / sizeof(kMenuKeyList[0])) ? kMenuKeyList[menu] : 0;
        cheat.spawnItem = item >= 0 && item < (int)(sizeof(kSpawnItemKeys) / sizeof(kSpawnItemKeys[0])) ? item : 0;
    }

    cheat.projectileTp = Flag("Projectile TP");
    cheat.noRecoil = Flag("No recoil");
    cheat.noRecoilAmount = ClampF(SliderF("Recoil amount", 1.f), 0.f, 1.f);
    cheat.noReload = Flag("No reload");
    cheat.noReloadAmount = ClampF(SliderF("Reload amount", 1.f), 0.f, 1.f);
    cheat.noCharge = Flag("No charge");
    cheat.noChargeAmount = ClampF(SliderF("Charge amount", 1.f), 0.f, 1.f);
    cheat.rapidFire = Flag("Rapid fire");
    cheat.rapidFireAmount = ClampF(SliderF("Fire speed", 2.f), 1.f, 3.f);
    cheat.damageMultiplier = Flag("Damage multiplier");
    cheat.damageScale = ClampF(SliderF("Damage scale", 2.f), 1.f, 10.f);
    cheat.fastPickaxe = Flag("Fast pickaxe");
    cheat.pickaxeSpeed = ClampF(SliderF("Pickaxe speed", 2.f), 1.f, 50.f);
    cheat.automaticWeapons = Flag("Automatic weapons");
    cheat.doublePump = Flag("Double pump");
    cheat.infiniteAmmo = Flag("Infinite ammo");
    cheat.infiniteMats = Flag("Infinite materials");
    cheat.distanceMultiplier = false;
}

// The fly key toggles Fly while you play (menu closed, game window in front). ImGui's own
// keybinds only see keys while the menu is open, so this reads the key directly, every frame.
void TickMenuHotkeys() {
    cfg->autosave_tick(); // remembers changed settings (last session and the loaded config)
    static bool wasDown = false;
    DWORD pid = 0;
    if (HWND front = GetForegroundWindow())
        GetWindowThreadProcessId(front, &pid);
    const bool down = cheat.flyKey && !g_wantOpen && pid == GetCurrentProcessId() && KeyDown(cheat.flyKey);
    if (down && !wasDown) {
        if (checkbox_t* box = cfg->fill<checkbox_t>("Fly")) {
            box->callback = !box->callback;
            cheat.fly = box->callback;
            printf("[menu] fly key: fly %s\n", cheat.fly ? "on" : "off");
        }
    }
    wasDown = down;
    // Heal key (Heal while moving) and Teleport key: one command per press, while you play.
    static bool healWas = false, tpWas = false;
    const bool inGame = !g_wantOpen && pid == GetCurrentProcessId();
    const bool healDown = cheat.healMoving && cheat.healKey && inGame && KeyDown(cheat.healKey);
    const bool tpDown = cheat.tpKey && inGame && KeyDown(cheat.tpKey);
    if (healDown && !healWas) cheat.healPending = true;
    if (tpDown && !tpWas) cheat.tpPending = true;
    healWas = healDown;
    tpWas = tpDown;
}

void SyncMenuOptions() {
    SyncOptions();
}

void ToggleMenu() {
    if (g_introActive) {
        g_introActive = false;
        g_introPlayed = true;
        g_wantOpen = false;
        cheat.menuOpen = false;
        return;
    }
    if (!g_introPlayed && !g_wantOpen) {
        g_wantOpen = true;
        g_introActive = true;
        g_introT = 0.f;
        g_motion = 0.f;
        cheat.menuOpen = true;
        return;
    }
    g_wantOpen = !g_wantOpen;
    cheat.menuOpen = g_wantOpen;
}

void TickMenuMotion() {
    cheat.menuOpen = g_wantOpen;
    float dt = ImGui::GetIO().DeltaTime;
    if (dt < 0.f)
        dt = 0.f;
    if (dt > 0.05f)
        dt = 0.05f;
    float speed = var->gui.anim_speed;
    if (speed < 0.1f)
        speed = 0.1f;

    if (g_introActive) {
        g_introT += dt * speed / 1.15f;
        if (g_introT >= 1.f) {
            g_introT = 1.f;
            g_introActive = false;
            g_introPlayed = true;
            g_motion = 1.f;
        } else if (g_introT < 0.38f) {
            g_motion = 0.f;
        } else {
            float appear = (g_introT - 0.38f) / 0.50f;
            if (appear > 1.f)
                appear = 1.f;
            g_motion = appear * appear * (3.f - 2.f * appear);
        }
        return;
    }

    const float step = dt * (1.f / 0.20f) * speed;
    if (g_wantOpen) {
        g_motion += step;
        if (g_motion > 1.f)
            g_motion = 1.f;
    } else {
        g_motion -= step;
        if (g_motion < 0.f)
            g_motion = 0.f;
    }
}

float MenuMotion() {
    return g_motion;
}

float MenuIntroT() {
    return g_introT;
}

bool MenuIntroActive() {
    return g_introActive;
}

bool MenuVisible() {
    return g_motion > 0.001f || g_introActive;
}

bool MenuCapturesInput() {
    return g_wantOpen;
}

void InitMenu() {
    static bool once = false;
    if (once)
        return;
    once = true;
    cfg->init_config();
    font->get(onest_medium_data, 14.f);
    font->get(onest_medium_data, 18.f);
    font->get(onest_medium_data, 20.f);
    font->get(onest_bold_data, 14.f);
    font->get(icons_data, 5.f);
    font->get(icons_data, 6.f);
    font->get(icons_data, 8.f);
    font->get(icons_data, 10.f);
    font->get(icons_data, 12.f);
    font->get(icons_data, 28.f);
    font->get(icons_data, 48.f);
    font->get(onest_bold_data, 22.f);
    var->gui.dpi_changed = true;
    cfg->load_last_session(); // the settings you had last time
    // Stream proof always starts off: if the overlay ever fails on a PC, the game stays visible after a restart.
    if (checkbox_t* streamProof = cfg->fill<checkbox_t>("Stream proof"))
        streamProof->callback = false;
    SyncOptions();
}

void UpdateMenuFonts() {
    font->update();
}

void DrawMenu() {
    if (var->gui.dpi_changed)
        return;
    cfg->init_config();
    gui->render();
    if (MenuCapturesInput())
        SyncOptions();
    if (var->request_unload) {
        var->request_unload = false;
        RequestUnload();
    }
}
