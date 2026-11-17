#include "Aim.h"
#include "ESP.h"
#include "Game.h"
#include "../Core/Memory.h"
#include "../Menu/Config.h"
#include "../Menu/Menu.h"
#include "../Menu/Overlay.h"
#include "../SDK/Objects.h"
#include "../SDK/Offsets.h"

#include <cfloat>
#include <cmath>
#include <cstdio>
#include <windows.h>

namespace {

float WrapDegrees(float degrees) {
    while (degrees > 180.f)
        degrees -= 360.f;
    while (degrees < -180.f)
        degrees += 360.f;
    return degrees;
}

bool FiniteVec(const Vec3& v) {
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}

bool FiniteRot(const FRotator& r) {
    return std::isfinite(r.Pitch) && std::isfinite(r.Yaw) && std::isfinite(r.Roll);
}

float PixelsPerDegree() {
    float fov = gCam.fov > 1.f ? gCam.fov : 80.f;
    float center = W > 1 ? (W * 0.5f) : 960.f;
    float pixels = center / (fov * 0.5f);
    if (pixels < 1.f)
        pixels = 16.f;
    return pixels;
}

Vec3 RootFallback(const Target& target, int slot) {
    Vec3 fallback{};
    if (target.mesh)
        fallback = GetMeshWorld(target.mesh);
    if ((!FiniteVec(fallback) || (fallback.x == 0.f && fallback.y == 0.f && fallback.z == 0.f)) &&
        target.pawn && off.RootComponent && off.RelativeLocation) {
        uintptr_t root = Read<uintptr_t>(target.pawn + off.RootComponent);
        if (root)
            fallback = Read<Vec3>(root + off.RelativeLocation);
    }
    if (FiniteVec(fallback))
        fallback.z += (slot == 0) ? 82.f : (slot == 1) ? 70.f : (slot == 2) ? 40.f : 10.f;
    return fallback;
}

Vec3 AimPoint(const Target& target, int slot, bool& ok) {
    ok = false;
    Vec3 fallback = RootFallback(target, slot);
    int bone = AimBoneIndex(target.bones, slot);
    if (bone <= 0)
        bone = 0;
    Vec3 aim{};
    if (target.mesh)
        aim = GetBoneWorld(target.mesh, bone);
    if (!FiniteVec(aim) || (aim.x == 0.f && aim.y == 0.f && aim.z == 0.f))
        aim = fallback;
    if (!FiniteVec(aim) || (aim.x == 0.f && aim.y == 0.f && aim.z == 0.f))
        return {};
    if (slot == 0 && target.mesh) {
        int neck = AimBoneIndex(target.bones, 1);
        Vec3 neckPos = neck > 0 ? GetBoneWorld(target.mesh, neck) : Vec3{};
        if (FiniteVec(neckPos) && (neckPos.x || neckPos.y || neckPos.z)) {
            Vec3 lift{ aim.x - neckPos.x, aim.y - neckPos.y, aim.z - neckPos.z };
            float liftLen = sqrtf(lift.x * lift.x + lift.y * lift.y + lift.z * lift.z);
            if (std::isfinite(liftLen) && liftLen > 2.f && liftLen < 45.f) {
                // The head bone sits at the base of the skull; a small push along the
                // neck->head line puts the point in the middle of the head hitbox.
                float extra = liftLen * 0.25f;
                if (extra < 2.f) extra = 2.f;
                if (extra > 5.f) extra = 5.f;
                float scale = extra / liftLen;
                aim.x += lift.x * scale;
                aim.y += lift.y * scale;
                aim.z += lift.z * scale;
            } else {
                aim.z += 3.f;
            }
        } else {
            aim.z += 3.f;
        }
    }
    if (!FiniteVec(aim))
        aim = fallback;
    if (!FiniteVec(aim))
        return {};
    ok = true;
    return aim;
}

struct Pick {
    bool ok;
    Vec3 point;
    float yawError;
    float pitchError;
    float desiredYaw;
    float desiredPitch;
};

__declspec(noinline) bool ConsiderTarget(const Target& target, const Vec3& forward, float fovCos, float bestDot,
                                         float& outDot, float& outYaw, float& outPitch, float& outDesiredYaw,
                                         float& outDesiredPitch, Vec3& outPoint, int boneSlot) {
    __try {
        if (!target.mesh || (localPawn && target.pawn == localPawn))
            return false;
        if (cheat.visibleCheck && !target.visible)
            return false;
        bool pointOk = false;
        Vec3 aim = AimPoint(target, boneSlot, pointOk);
        if (!pointOk || !FiniteVec(aim))
            return false;
        Vec3 delta{ aim.x - gCam.loc.x, aim.y - gCam.loc.y, aim.z - gCam.loc.z };
        float len = sqrtf(delta.x * delta.x + delta.y * delta.y + delta.z * delta.z);
        if (len < 5.f || !std::isfinite(len))
            return false;
        Vec3 dir{ delta.x / len, delta.y / len, delta.z / len };
        float dot = dir.x * forward.x + dir.y * forward.y + dir.z * forward.z;
        if (!std::isfinite(dot) || dot < fovCos || dot < bestDot)
            return false;
        float desiredYaw = atan2f(dir.y, dir.x) * 57.2957795f;
        float flat = sqrtf(dir.x * dir.x + dir.y * dir.y);
        float desiredPitch = atan2f(dir.z, flat) * 57.2957795f;
        if (!std::isfinite(desiredYaw) || !std::isfinite(desiredPitch))
            return false;
        outDot = dot;
        outDesiredYaw = desiredYaw;
        outDesiredPitch = desiredPitch;
        outYaw = WrapDegrees(desiredYaw - gCam.rot.Yaw);
        outPitch = WrapDegrees(desiredPitch - gCam.rot.Pitch);
        outPoint = aim;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

uintptr_t g_lockedPawn = 0; // the bot the aim key is locked on (0 = none)
int g_lockChanges = 0;      // times the lock moved to a bot during the current hold (shown in the hold stats)

Pick PickCone(int boneSlot, float fovDeg) {
    Pick best{};
    if (!gCam.valid || !FiniteVec(gCam.loc) || !FiniteRot(gCam.rot))
        return best;
    if (fovDeg < 1.f)
        fovDeg = 1.f;
    if (fovDeg > 18.4f)
        fovDeg = 18.4f;

    const float fovCos = cosf(fovDeg * 0.01745329251f);
    const float camPitch = gCam.rot.Pitch * 0.01745329251f;
    const float camYaw = gCam.rot.Yaw * 0.01745329251f;
    const float cosPitch = cosf(camPitch);
    const Vec3 forward{ cosPitch * cosf(camYaw), cosPitch * sinf(camYaw), sinf(camPitch) };
    if (!FiniteVec(forward))
        return best;
    float bestDot = -2.f;

    // Sticky lock: while the aim key stays down, the bot you locked stays the target as long as it is alive, in
    // sight and within 2.5 times the FOV (moving bots slip out of a small FOV for a frame, and the aim jumped to
    // another bot or let go). A new bot is only picked when the locked one is gone.
    // A locked bot that is still in the match but misses a frame or two (a bone read fails, it steps behind
    // something) keeps the lock for a short grace time instead of handing it to the next bot. Season 5 log,
    // 2026-11-12: the lock was dropped and taken again many times a second, so the aim never settled.
    static DWORD lockMissSince = 0;
    if (g_lockedPawn) {
        float lockFov = fovDeg * 2.5f;
        if (lockFov < 10.f) lockFov = 10.f;
        if (lockFov > 45.f) lockFov = 45.f;
        const float lockCos = cosf(lockFov * 0.01745329251f);
        bool stillHere = false;
        for (const Target& target : targets) {
            if (target.pawn != g_lockedPawn)
                continue;
            stillHere = true;
            float dot = 0.f, yaw = 0.f, pitch = 0.f, desiredYaw = 0.f, desiredPitch = 0.f;
            Vec3 point{};
            if (ConsiderTarget(target, forward, lockCos, -2.f, dot, yaw, pitch, desiredYaw, desiredPitch, point, boneSlot)) {
                lockMissSince = 0;
                best = Pick{ true, point, yaw, pitch, desiredYaw, desiredPitch };
                return best;
            }
            break;
        }
        const DWORD now = GetTickCount();
        if (stillHere && (!lockMissSince || now - lockMissSince < 250)) {
            if (!lockMissSince)
                lockMissSince = now;
            return best;
        }
        lockMissSince = 0;
        g_lockedPawn = 0;
    }

    uintptr_t bestPawn = 0;
    for (const Target& target : targets) {
        float dot = 0.f, yaw = 0.f, pitch = 0.f, desiredYaw = 0.f, desiredPitch = 0.f;
        Vec3 point{};
        if (!ConsiderTarget(target, forward, fovCos, bestDot, dot, yaw, pitch, desiredYaw, desiredPitch, point, boneSlot))
            continue;
        bestDot = dot;
        bestPawn = target.pawn;
        best.ok = true;
        best.point = point;
        best.yawError = yaw;
        best.pitchError = pitch;
        best.desiredYaw = desiredYaw;
        best.desiredPitch = desiredPitch;
    }
    if (best.ok && bestPawn != g_lockedPawn) {
        g_lockedPawn = bestPawn;
        ++g_lockChanges;
        printf("[aim] locked on a new bot (%llX)\n", (unsigned long long)bestPawn);
    }
    return best;
}

float g_carryX = 0.f;
float g_carryY = 0.f;
float g_lastYaw = 0.f;
float g_lastPitch = 0.f;
float g_aimYaw = 0.f;
float g_aimPitch = 0.f;
bool g_aimPending = false;
DWORD g_aimStamp = 0;
uintptr_t g_aimController = 0;
// Controller rotation minus camera rotation, measured while the aim key is held (see DoAim). Cleared when the key
// is released so every new hold measures it again.
bool g_offsetInit = false;
float g_offsetYaw = 0.f;
float g_offsetPitch = 0.f;
// True once this match showed that writing the controller rotation does not move the view (Season 5): the aim then
// steers with the mouse from the camera. Cleared when your pawn changes.
bool g_controlIgnored = false;
const char* g_steerMode = "none";

Vec3 DirOf(float yawDeg, float pitchDeg) {
    const float y = yawDeg * 0.01745329251f, p = pitchDeg * 0.01745329251f;
    return Vec3{ cosf(p) * cosf(y), cosf(p) * sinf(y), sinf(p) };
}

float AngleBetween(float yawA, float pitchA, float yawB, float pitchB) {
    const Vec3 a = DirOf(yawA, pitchA), b = DirOf(yawB, pitchB);
    float c = a.x * b.x + a.y * b.y + a.z * b.z;
    if (c > 1.f) c = 1.f;
    if (c < -1.f) c = -1.f;
    return acosf(c) * 57.2957795f;
}

// Tracking stats for one hold of the aim key: how far the camera (what you see) is from the bot each frame.
// Shown live in the debug overlay ("Aim tracking" line) and written to SyndicateMenu.log when the key is released
// ("[aim] hold:" line), so "it doesn't stay on them" can be read as numbers.
struct HoldStats {
    DWORD start = 0;          // when the key went down
    int acquireMs = -1;       // key down -> camera first within 1 degree of the bot (-1 = never this hold)
    int noBotFrames = 0;      // frames with the key held and no bot to aim at (lock lost or nothing in FOV)
    int frames = 0, within1 = 0, within3 = 0;
    double sumErr = 0.0;
    float maxErr = 0.f, lastErr = 0.f;
    DWORD lastStatus = 0;
};
HoldStats g_hold;

void HoldTrack(const FRotator& view, float desiredYaw, float desiredPitch) {
    const float err = AngleBetween(view.Yaw, view.Pitch, desiredYaw, desiredPitch);
    if (!std::isfinite(err))
        return;
    const DWORD now = GetTickCount();
    if (!g_hold.start)
        g_hold.start = now;
    ++g_hold.frames;
    if (err <= 1.f && g_hold.acquireMs < 0)
        g_hold.acquireMs = (int)(now - g_hold.start);
    if (err <= 1.f) ++g_hold.within1;
    if (err <= 3.f) ++g_hold.within3;
    g_hold.sumErr += err;
    if (err > g_hold.maxErr) g_hold.maxErr = err;
    g_hold.lastErr = err;
    if (now - g_hold.lastStatus > 250) {
        g_hold.lastStatus = now;
        char acq[32];
        if (g_hold.acquireMs >= 0) snprintf(acq, sizeof(acq), "%d ms", g_hold.acquireMs);
        else snprintf(acq, sizeof(acq), "not yet");
        FeatureStatus("Aim tracking", "%s: off by %.2f deg now, avg %.2f, %d%% of frames within 1 deg, %d%% within 3, locked on in %s, no-bot frames %d, bot switches %d",
            g_steerMode, err, (float)(g_hold.sumErr / g_hold.frames), g_hold.within1 * 100 / g_hold.frames,
            g_hold.within3 * 100 / g_hold.frames, acq, g_hold.noBotFrames, g_lockChanges > 0 ? g_lockChanges - 1 : 0);
    }
}

// Called every frame the aim key is held, before a bot is picked: starts the hold clock at the key press, so the
// lock-on time includes any frames where no bot was found yet.
void HoldKeyHeld(bool haveBot) {
    if (!g_hold.start)
        g_hold.start = GetTickCount();
    if (!haveBot)
        ++g_hold.noBotFrames;
}

void HoldEnd() {
    if (g_hold.frames > 0) {
        const DWORD ms = GetTickCount() - g_hold.start;
        printf("[aim] hold: %lu ms, %d frames, steering %s, locked on (within 1 deg) after %d ms, camera off by avg %.2f max %.2f deg, %d%% of frames within 1 deg, %d%% within 3, no-bot frames %d, bot switches %d\n",
            (unsigned long)ms, g_hold.frames, g_steerMode, g_hold.acquireMs, (float)(g_hold.sumErr / g_hold.frames), g_hold.maxErr,
            g_hold.within1 * 100 / g_hold.frames, g_hold.within3 * 100 / g_hold.frames, g_hold.noBotFrames,
            g_lockChanges > 0 ? g_lockChanges - 1 : 0);
        FeatureStatus("Aim tracking", "last hold (%s): locked on in %d ms, avg %.2f deg off, max %.2f, %d%% of frames within 1 deg, no-bot frames %d, bot switches %d",
            g_steerMode, g_hold.acquireMs, (float)(g_hold.sumErr / g_hold.frames), g_hold.maxErr,
            g_hold.within1 * 100 / g_hold.frames, g_hold.noBotFrames, g_lockChanges > 0 ? g_lockChanges - 1 : 0);
    } else if (g_hold.noBotFrames > 0) {
        printf("[aim] hold: %lu ms, no bot inside the FOV the whole time (%d frames)\n",
            (unsigned long)(GetTickCount() - g_hold.start), g_hold.noBotFrames);
    }
    g_hold = HoldStats{};
    g_lockChanges = 0;
}

// True while your own pawn is skydiving or gliding (FortPlayerPawn.bIsSkydiving / bIsParachuteOpen).
// Season 5 (5.10) log, 2026-11-12: every aim attempt was made while skydiving. The skydive camera does not look
// where the controller rotation points (camera pitch -23 to -84, ControlRotation pitch +11 to +18, and the gap kept
// changing), so writing the controller rotation threw the view off the bot. You can't shoot while skydiving or
// gliding anyway, so the aim waits until you land.
bool LocalInAir() {
    static bool tried = false;
    static PropInfo sky{}, chute{};
    if (!tried) {
        tried = true;
        sky = FindClassProp("FortPlayerPawn", "bIsSkydiving");
        chute = FindClassProp("FortPlayerPawn", "bIsParachuteOpen");
        printf("[aim] skydive check: bIsSkydiving %s 0x%X mask 0x%X, bIsParachuteOpen %s 0x%X mask 0x%X\n",
            sky.found ? "found" : "MISSING", sky.offset, sky.mask, chute.found ? "found" : "MISSING", chute.offset, chute.mask);
    }
    if (!localPawn)
        return false;
    auto flag = [](const PropInfo& p) {
        if (!p.found || p.offset <= 0)
            return false;
        const uint8_t v = Read<uint8_t>(localPawn + p.offset);
        return p.mask ? (v & p.mask) != 0 : v != 0;
    };
    return flag(sky) || flag(chute);
}

// Does the controller rotation drive the view on this build? Checked every frame, key held or not, and decided once
// for the whole game session, so the first press of the aim key already uses the right steering.
// Season 5 log 2026-11-12 16:24: the verdict was reset for every new pawn (bus -> skydive -> landing) and only
// measured while the key was held, so after every landing and at the start of every match the first holds tried
// controller steering again: 3 to 8 frames of a kicking view, 7 to 10 degrees off, the lock dropping and
// re-locking the same bot, until 20 bad frames switched back to the mouse. That was the "stutters for a few, then
// works" at lock-on.
// Older builds keep controller and camera together (gap ~0), so they never trip this. A long emote or spectator
// camera could; then the aim steers with the mouse from the camera, which also tracks.
void WatchControlGap() {
    static int bigGapFrames = 0;
    static DWORD lastStatus = 0;
    if (g_controlIgnored || !localPC || !localPawn || !off.ControlRotation || !gCam.valid || !FiniteRot(gCam.rot))
        return;
    if (LocalInAir()) {
        bigGapFrames = 0;
        return;
    }
    const FRotator c = Read<FRotator>(localPC + off.ControlRotation);
    if (!FiniteRot(c))
        return;
    const float gapYaw = WrapDegrees(WrapDegrees(c.Yaw) - WrapDegrees(gCam.rot.Yaw));
    const float gapPitch = WrapDegrees(c.Pitch) - WrapDegrees(gCam.rot.Pitch);
    if (fabsf(gapYaw) > 15.f || fabsf(gapPitch) > 15.f) {
        if (++bigGapFrames >= 45) {
            g_controlIgnored = true;
            printf("[aim] steering decided: the controller rotation does not drive the view on this build (control-camera %.1f/%.1f deg for %d frames). "
                   "Steering with the mouse from the camera for the rest of this game session.\n", gapYaw, gapPitch, bigGapFrames);
            FeatureStatus("Aim steering", "mouse from the camera (controller rotation sits %.0f/%.0f deg from the view on this build)", gapYaw, gapPitch);
        }
    } else {
        bigGapFrames = 0;
        const DWORD now = GetTickCount();
        if (now - lastStatus > 1000) {
            lastStatus = now;
            FeatureStatus("Aim steering", "controller rotation (it follows the view here: gap %.1f/%.1f deg)", gapYaw, gapPitch);
        }
    }
}

// Steering while skydiving or gliding: moves the mouse, because the skydive camera follows mouse input (that's how
// you aim a dive) but not a written controller rotation on Season 5. Older seasons' skydive cameras follow the
// controller rotation, which is why they tracked fine in the air.
// It's closed-loop on what you see: each frame it turns a share of the remaining camera error. How far the camera
// moves per mouse pixel is measured separately for yaw and pitch (the skydive camera can turn pitch at a different
// rate) over ~40 ms windows. It starts high (0.15 deg/px), so the first moves are too small, never too big.
struct CameraSteerState {
    const char* name;
    float degPerPxYaw = 0.15f;
    float degPerPxPitch = 0.15f;
    float carryX = 0.f, carryY = 0.f;
    bool window = false;
    FRotator start{};
    DWORD startAt = 0;
    LONG sentX = 0, sentY = 0;
    DWORD lastLog = 0;
    // How fast the bot's direction moves (deg/s), so the aim leads a moving bot instead of trailing it.
    bool havePrev = false;
    float prevYaw = 0.f, prevPitch = 0.f;
    float rateYaw = 0.f, ratePitch = 0.f;
};
// Measured separately: the skydive camera turns at a different rate per mouse pixel than the ground camera.
CameraSteerState g_mouseGround{ "ground" };
CameraSteerState g_mouseAir{ "air" };

void CameraSteerResetOne(CameraSteerState& m) {
    m.carryX = m.carryY = 0.f;
    m.window = false;
    m.sentX = m.sentY = 0;
    m.havePrev = false;
    m.rateYaw = m.ratePitch = 0.f;
}

void CameraSteerReset() {
    CameraSteerResetOne(g_mouseGround);
    CameraSteerResetOne(g_mouseAir);
}

void CameraSteer(CameraSteerState& g_mouse, float desiredYaw, float desiredPitch, float dt) {
    const float camYaw = WrapDegrees(gCam.rot.Yaw), camPitch = WrapDegrees(gCam.rot.Pitch);
    const DWORD now = GetTickCount();

    // Measure how far the camera turned for the pixels sent in the last window.
    if (!g_mouse.window) {
        g_mouse.window = true;
        g_mouse.start = FRotator{ camPitch, camYaw, 0.f };
        g_mouse.startAt = now;
        g_mouse.sentX = g_mouse.sentY = 0;
    } else if (now - g_mouse.startAt >= 40) {
        if (now - g_mouse.startAt < 300) {
            const float turnedYaw = WrapDegrees(camYaw - g_mouse.start.Yaw);
            const float turnedPitch = camPitch - g_mouse.start.Pitch;
            if (g_mouse.sentX >= 4 || g_mouse.sentX <= -4) {
                const float r = turnedYaw / (float)g_mouse.sentX;
                if (std::isfinite(r) && r > 0.001f && r < 2.f)
                    g_mouse.degPerPxYaw = g_mouse.degPerPxYaw * 0.6f + r * 0.4f;
            }
            if (g_mouse.sentY >= 4 || g_mouse.sentY <= -4) {
                const float r = -turnedPitch / (float)g_mouse.sentY; // mouse down = pitch down
                if (std::isfinite(r) && r > 0.001f && r < 2.f)
                    g_mouse.degPerPxPitch = g_mouse.degPerPxPitch * 0.6f + r * 0.4f;
            }
        }
        g_mouse.window = false;
    }

    // Next view: a share of the way along the straight line from the camera to the bot (looking steeply down,
    // turning yaw and pitch on their own can spin the view), the same share per second at any frame rate.
    float smooth = cheat.aimSmooth < 1.f ? 1.f : cheat.aimSmooth;
    if (smooth > 20.f) smooth = 20.f;
    // Time constant 30 ms x Smoothness at high frame rates, never under 3 frames, so slow frame rates stay stable
    // (tuned in a simulation of the 2026-11-12 Season 5 numbers: 361 FPS, 0.16 deg/px, a few frames of camera lag).
    const float tau = (dt * 3.f > 0.03f ? dt * 3.f : 0.03f) * smooth;
    float frac = 1.f - expf(-dt / tau);
    if (!(frac > 0.f)) frac = 0.f;
    if (frac > 0.5f) frac = 0.5f;
    const Vec3 a = DirOf(camYaw, camPitch), b = DirOf(desiredYaw, desiredPitch);
    Vec3 v{ a.x + (b.x - a.x) * frac, a.y + (b.y - a.y) * frac, a.z + (b.z - a.z) * frac };
    const float len = sqrtf(v.x * v.x + v.y * v.y + v.z * v.z);
    if (!(len > 1e-4f) || !std::isfinite(len))
        return;
    v = Vec3{ v.x / len, v.y / len, v.z / len };
    float stepYaw = WrapDegrees(atan2f(v.y, v.x) * 57.2957795f - camYaw);
    float stepPitch = asinf(v.z) * 57.2957795f - camPitch;

    // Lead: add how far the bot's direction moves this frame, so a strafing bot doesn't pull ahead of the aim (a
    // plain "share of the remaining angle" always trails a moving target). A jump of more than 5 degrees in one frame
    // is a new target or a bad read, not movement, and clears the estimate.
    if (g_mouse.havePrev && dt > 0.f) {
        const float dYaw = WrapDegrees(desiredYaw - g_mouse.prevYaw), dPitch = desiredPitch - g_mouse.prevPitch;
        if (fabsf(dYaw) > 5.f || fabsf(dPitch) > 5.f) {
            g_mouse.rateYaw = g_mouse.ratePitch = 0.f;
        } else {
            const float k = 1.f - expf(-dt / (dt * 5.f > 0.04f ? dt * 5.f : 0.04f));
            g_mouse.rateYaw += (dYaw / dt - g_mouse.rateYaw) * k;
            g_mouse.ratePitch += (dPitch / dt - g_mouse.ratePitch) * k;
            if (g_mouse.rateYaw > 400.f) g_mouse.rateYaw = 400.f;
            if (g_mouse.rateYaw < -400.f) g_mouse.rateYaw = -400.f;
            if (g_mouse.ratePitch > 400.f) g_mouse.ratePitch = 400.f;
            if (g_mouse.ratePitch < -400.f) g_mouse.ratePitch = -400.f;
        }
    }
    g_mouse.havePrev = true;
    g_mouse.prevYaw = desiredYaw;
    g_mouse.prevPitch = desiredPitch;
    stepYaw += g_mouse.rateYaw * dt;
    stepPitch += g_mouse.ratePitch * dt;

    g_mouse.carryX += stepYaw / g_mouse.degPerPxYaw;
    g_mouse.carryY += -stepPitch / g_mouse.degPerPxPitch;
    LONG moveX = (LONG)g_mouse.carryX, moveY = (LONG)g_mouse.carryY;
    g_mouse.carryX -= (float)moveX;
    g_mouse.carryY -= (float)moveY;
    if (moveX > 60) moveX = 60;
    if (moveX < -60) moveX = -60;
    if (moveY > 60) moveY = 60;
    if (moveY < -60) moveY = -60;
    if (moveX || moveY) {
        INPUT input{};
        input.type = INPUT_MOUSE;
        input.mi.dx = moveX;
        input.mi.dy = moveY;
        input.mi.dwFlags = MOUSEEVENTF_MOVE;
        SendInput(1, &input, sizeof(INPUT));
        g_mouse.sentX += moveX;
        g_mouse.sentY += moveY;
    }

    if (now - g_mouse.lastLog > 1000) {
        g_mouse.lastLog = now;
        printf("[aim] mouse (%s): camera off by yaw %.2f pitch %.2f, camera %.1f/%.1f, %.4f deg/px yaw %.4f pitch (measured), step %.3f, bot moving %.1f/%.1f deg/s\n",
            g_mouse.name, WrapDegrees(desiredYaw - camYaw), desiredPitch - camPitch, camYaw, camPitch, g_mouse.degPerPxYaw,
            g_mouse.degPerPxPitch, frac, g_mouse.rateYaw, g_mouse.ratePitch);
    }
}

void ResetAimFilter() {
    g_carryX = 0.f;
    g_carryY = 0.f;
    g_lastYaw = 0.f;
    g_lastPitch = 0.f;
}

void MoveAim(float yawError, float pitchError) {
    float smooth = cheat.aimSmooth;
    if (smooth < 1.f)
        smooth = 1.f;
    if (smooth > 20.f)
        smooth = 20.f;

    if (fabsf(yawError) < 0.2f)
        yawError = 0.f;
    if (fabsf(pitchError) < 0.2f)
        pitchError = 0.f;
    if (g_lastYaw * yawError < 0.f)
        yawError = 0.f;
    if (g_lastPitch * pitchError < 0.f)
        pitchError = 0.f;
    g_lastYaw = yawError;
    g_lastPitch = pitchError;
    if (yawError == 0.f && pitchError == 0.f) {
        g_carryX = 0.f;
        g_carryY = 0.f;
        return;
    }

    const float div = smooth * smooth;
    const float cap = 4.f / smooth;
    float stepYaw = yawError / div;
    float stepPitch = pitchError / div;
    if (stepYaw > cap)
        stepYaw = cap;
    if (stepYaw < -cap)
        stepYaw = -cap;
    if (stepPitch > cap)
        stepPitch = cap;
    if (stepPitch < -cap)
        stepPitch = -cap;

    const float pixels = PixelsPerDegree();
    g_carryX += stepYaw * pixels;
    g_carryY += -stepPitch * pixels;
    const LONG moveX = (LONG)g_carryX;
    const LONG moveY = (LONG)g_carryY;
    g_carryX -= (float)moveX;
    g_carryY -= (float)moveY;
    if (moveX == 0 && moveY == 0)
        return;

    INPUT input{};
    input.type = INPUT_MOUSE;
    input.mi.dx = moveX;
    input.mi.dy = moveY;
    input.mi.dwFlags = MOUSEEVENTF_MOVE;
    SendInput(1, &input, sizeof(INPUT));
}

int CachedFloatProp(uintptr_t obj, int& cached, const char* name) {
    if (cached)
        return cached;
    if (!obj)
        return 0;
    PropInfo prop = FindProperty(ObjClass(obj), name);
    if (prop.found && prop.offset > 0)
        cached = prop.offset;
    return cached;
}

void ZeroFloat(uintptr_t obj, int off) {
    if (!obj || !off)
        return;
    Write<float>(obj + off, 0.f);
}

}

bool ConeTarget(int boneSlot, float fovDeg, Vec3& out) {
    Pick pick = PickCone(boneSlot, fovDeg);
    if (!pick.ok)
        return false;
    out = pick.point;
    return true;
}

void DoWeaponMods() {
    if (!cheat.noSpread && !cheat.noBloom)
        return;
    if (!localPawn || !off.CurrentWeapon)
        return;

    uintptr_t weapon = Read<uintptr_t>(localPawn + off.CurrentWeapon);
    if (!weapon || !ObjClass(weapon))
        return;

    static int spreadOff = 0;
    static int maxSpreadOff = 0;
    static int heatOff = 0;
    static int overheatOff = 0;
    static int reticleOff = 0;

    CachedFloatProp(weapon, spreadOff, "CurrentSpread");
    CachedFloatProp(weapon, maxSpreadOff, "CurrentMaxSpread");
    CachedFloatProp(weapon, heatOff, "CurrentHeat");
    CachedFloatProp(weapon, overheatOff, "CurrentOverheatValue");
    CachedFloatProp(weapon, reticleOff, "CurrentReticleSpread");
    if (!reticleOff)
        CachedFloatProp(weapon, reticleOff, "ReticleSpread");

    if (cheat.noBloom)
        FeatureStatus("No bloom", (heatOff || spreadOff || reticleOff) ? "active on %s (heat %s, spread %s, reticle %s)"
                                                                       : "does nothing on this season: %s has no CurrentHeat/CurrentSpread/ReticleSpread",
            ObjClassName(weapon).c_str(), heatOff ? "yes" : "no", spreadOff ? "yes" : "no", reticleOff ? "yes" : "no");
    if (cheat.noSpread) {
        ZeroFloat(weapon, spreadOff);
        ZeroFloat(weapon, maxSpreadOff);
        ZeroFloat(weapon, reticleOff);
    }
    if (cheat.noBloom) {
        ZeroFloat(weapon, heatOff);
        ZeroFloat(weapon, overheatOff);
        ZeroFloat(weapon, spreadOff);
        ZeroFloat(weapon, reticleOff);
        // Fixed 10.40 offset: only write it on Season X (no cobalt.dll), never on other builds.
        static const bool seasonX = GetModuleHandleW(L"cobalt.dll") == nullptr;
        if (seasonX)
            Write<float>(weapon + 0x64, FLT_MAX);
    }
}

void LockAim(uintptr_t controller, float yaw, float pitch) {
    if (!controller || !std::isfinite(yaw) || !std::isfinite(pitch))
        return;
    FRotator rot{ pitch, yaw, 0.f };
    if (off.ControlRotation)
        Write<FRotator>(controller + off.ControlRotation, rot);
    static uintptr_t fnSet = 0;
    static bool tried = false;
    if (!tried) {
        tried = true;
        fnSet = GetFunction("Controller", "SetControlRotation");
    }
    if (fnSet) {
        struct { FRotator NewRotation; } params{ rot };
        ProcessEvent(controller, fnSet, &params);
    }
}

void DoAim() {
    g_aimPending = false;
    if (MenuCapturesInput()) {
        ResetAimFilter();
        return;
    }
    if (!localPC || !cheat.aimbot) {
        ResetAimFilter();
        return;
    }
    WatchControlGap();
    if (!KeyDown(cheat.aimKey)) {
        ResetAimFilter();
        HoldEnd();
        g_lockedPawn = 0;
        g_offsetInit = false;
        CameraSteerReset();
        return;
    }
    if (!gCam.valid || !FiniteVec(gCam.loc) || !FiniteRot(gCam.rot)) {
        ResetAimFilter();
        return;
    }
    const char* feature = cheat.silentAim ? "Silent aim" : "Aim assist";
    Pick pick = PickCone(cheat.aimBone, cheat.aimFov);
    HoldKeyHeld(pick.ok);
    if (!pick.ok) {
        FeatureStatus(feature, "key held, no bot inside the %.1f degree FOV (visible check %s)", cheat.aimFov,
            cheat.visibleCheck ? "on" : "off");
        ResetAimFilter();
        return;
    }
    FeatureStatus(feature, cheat.silentAim ? "bot in FOV and sight (projectiles bend here; hitscan shots are hit by the server switch)"
                                           : "locked on a bot");
    g_aimController = localPC;
    g_aimYaw = pick.desiredYaw;
    g_aimPitch = pick.desiredPitch;
    g_aimStamp = GetTickCount();
    if (cheat.silentAim)
        return;
    // Steering: the controller's rotation is turned toward the target directly, a share of the
    // remaining angle each frame (Smoothness 1 = instant). It used to move the mouse by a guessed
    // number of pixels per degree, which ignores the player's sensitivity and made the aim swing
    // past bots and back on Season 3 and later. The mouse path is only a fallback for a build where
    // the controller rotation can't be written.
    // Frame time from the performance counter: GetTickCount only ticks every ~16 ms, so at high frame rates most
    // frames read 0 ms and were treated as 16 ms, which made the step size jump from frame to frame (jitter).
    static LARGE_INTEGER freq{}, lastCount{};
    if (!freq.QuadPart)
        QueryPerformanceFrequency(&freq);
    LARGE_INTEGER count{};
    QueryPerformanceCounter(&count);
    float dt = lastCount.QuadPart && freq.QuadPart ? float(double(count.QuadPart - lastCount.QuadPart) / double(freq.QuadPart)) : 0.016f;
    lastCount = count;
    if (!(dt > 0.f) || dt > 0.1f)
        dt = 0.016f;
    const DWORD now = GetTickCount();
    static uintptr_t fnSet = 0;
    static bool fnTried = false;
    if (!fnTried) {
        fnTried = true;
        fnSet = GetFunction("Controller", "SetControlRotation");
        printf("[aim] steering: ControlRotation %s, SetControlRotation %s\n", off.ControlRotation ? "found" : "MISSING",
            fnSet ? "found" : "MISSING");
    }
    if (!off.ControlRotation && !fnSet) {
        MoveAim(pick.yawError, pick.pitchError);
        return;
    }
    // Season 5 (5.10), log 2026-11-12 15:52: the controller rotation does not drive the view there, not even on
    // the ground. ControlRotation sat 65 to 84 degrees from the camera in yaw with its pitch stuck at +18 to +20,
    // and controller steering left the camera 10 degrees off the bot (the jitter on the ground and on the spawn
    // island). Mouse steering held it within 0.6 to 3 degrees, but the old self-check kept flipping between the two.
    // So: where the controller rotation is far from the camera for a while (not in the air), the menu decides the
    // controller does not drive the view on this build and steers with the mouse from the camera, the way it
    // already does in the air. Older builds keep the two together and keep controller steering.
    const FRotator view{ WrapDegrees(gCam.rot.Pitch), WrapDegrees(gCam.rot.Yaw), 0.f };
    FRotator control = view;
    bool haveControl = false;
    if (off.ControlRotation) {
        const FRotator c = Read<FRotator>(localPC + off.ControlRotation);
        if (FiniteRot(c) && fabsf(WrapDegrees(c.Pitch)) <= 90.5f) {
            control = FRotator{ WrapDegrees(c.Pitch), WrapDegrees(c.Yaw), 0.f };
            haveControl = true;
        }
    }
    const bool inAir = LocalInAir();
    {
        static bool wasInAir = false;
        if (inAir != wasInAir) {
            wasInAir = inAir;
            printf("[aim] %s\n", inAir ? "skydiving or gliding: steering with the mouse from the camera"
                                       : "landed");
            g_offsetInit = false;
            CameraSteerReset();
        }
    }
    if (inAir || g_controlIgnored) {
        g_steerMode = inAir ? "mouse, in the air" : "mouse (controller rotation does not drive the view here)";
        HoldTrack(view, pick.desiredYaw, pick.desiredPitch);
        CameraSteer(inAir ? g_mouseAir : g_mouseGround, pick.desiredYaw, pick.desiredPitch, dt);
        return;
    }
    CameraSteerReset();
    g_steerMode = "controller rotation";
    HoldTrack(view, pick.desiredYaw, pick.desiredPitch);

    // Controller steering (older builds, where the controller rotation drives the view). The target is the bot
    // direction plus any real controller/camera offset (under 6 degrees counts as camera lag and is ignored), and
    // steering starts from the controller's own rotation, so a lagging camera can't make a turn count twice (#50).
    {
        const float instYaw = haveControl ? WrapDegrees(control.Yaw - view.Yaw) : 0.f;
        const float instPitch = haveControl ? WrapDegrees(control.Pitch - view.Pitch) : 0.f;
        if (!g_offsetInit) {
            g_offsetYaw = instYaw;
            g_offsetPitch = instPitch;
            g_offsetInit = true;
        } else {
            const float k = 1.f - expf(-dt / 0.3f);
            g_offsetYaw = WrapDegrees(g_offsetYaw + WrapDegrees(instYaw - g_offsetYaw) * k);
            g_offsetPitch += (instPitch - g_offsetPitch) * k;
        }
    }
    const float useYaw = fabsf(g_offsetYaw) < 6.f ? 0.f : g_offsetYaw;
    const float usePitch = fabsf(g_offsetPitch) < 6.f ? 0.f : g_offsetPitch;
    float targetYaw = WrapDegrees(pick.desiredYaw + useYaw);
    float targetPitch = pick.desiredPitch + usePitch;
    if (targetPitch > 89.f) targetPitch = 89.f;
    if (targetPitch < -89.f) targetPitch = -89.f;
    const FRotator current = haveControl ? control : FRotator{ view.Pitch, WrapDegrees(view.Yaw + useYaw), 0.f };

    // Turn along the straight line between where you look and the target (looking steeply up or down, turning yaw
    // and pitch on their own can spin the view).
    const Vec3 from = DirOf(current.Yaw, current.Pitch);
    const Vec3 to = DirOf(targetYaw, targetPitch);
    const float angle = AngleBetween(current.Yaw, current.Pitch, targetYaw, targetPitch);
    float smooth = cheat.aimSmooth < 1.f ? 1.f : cheat.aimSmooth;
    // Share of the remaining angle per frame, the same at any frame rate; the last 1.5 degrees snap.
    float frac = (smooth <= 1.05f) ? 1.f : 1.f - expf(-dt * 60.f / (smooth * smooth * 0.5f));
    if (angle < 1.5f)
        frac = 1.f;
    if (frac > 1.f) frac = 1.f;
    float nextYaw = targetYaw, nextPitch = targetPitch;
    if (frac < 1.f) {
        Vec3 v{ from.x + (to.x - from.x) * frac, from.y + (to.y - from.y) * frac, from.z + (to.z - from.z) * frac };
        const float len = sqrtf(v.x * v.x + v.y * v.y + v.z * v.z);
        if (len >= 1e-4f && std::isfinite(len)) {
            v = Vec3{ v.x / len, v.y / len, v.z / len };
            nextYaw = atan2f(v.y, v.x) * 57.2957795f;
            nextPitch = asinf(v.z) * 57.2957795f;
        }
    }

    // Self-check, one way only: if the camera does not move with the controller turns, switch this match to mouse
    // steering. (It used to flip back and forth, which was part of the ground jitter on Season 5.)
    static bool checkPending = false;
    static FRotator before{}, expect{};
    static DWORD checkAt = 0;
    static int checks = 0, followed = 0;
    if (checkPending && now - checkAt >= 25) {
        checkPending = false;
        if (now - checkAt < 400) {
            // Followed = the camera moved at least half the planned turn in the planned direction (turning further,
            // because the aim kept turning until the check ran, counts as followed).
            const float planYaw = WrapDegrees(expect.Yaw - before.Yaw), planPitch = expect.Pitch - before.Pitch;
            const float movedYaw = WrapDegrees(view.Yaw - before.Yaw), movedPitch = view.Pitch - before.Pitch;
            const float planned2 = planYaw * planYaw + planPitch * planPitch;
            if (planned2 > 0.5f * 0.5f) {
                ++checks;
                if (movedYaw * planYaw + movedPitch * planPitch >= 0.5f * planned2)
                    ++followed;
            }
        }
        if (checks >= 15) {
            printf("[aim] check (controller rotation): the view followed %d of %d turns\n", followed, checks);
            if (followed < 4) {
                g_controlIgnored = true;
                printf("[aim] the view did not follow the controller rotation: steering with the mouse for this match\n");
            }
            checks = followed = 0;
        }
    }
    if (!checkPending) {
        before = view;
        expect = FRotator{ nextPitch - usePitch, WrapDegrees(nextYaw - useYaw), 0.f };
        checkAt = now;
        checkPending = true;
    }

    {
        static DWORD lastLog = 0;
        if (now - lastLog > 1000) {
            lastLog = now;
            const Vec3 d{ pick.point.x - gCam.loc.x, pick.point.y - gCam.loc.y, pick.point.z - gCam.loc.z };
            printf("[aim] controller: target %.0fm bone %d, camera off by %.2f deg, controller off by %.2f deg; camera %.1f/%.1f control %.1f/%.1f%s (offset used %.2f/%.2f) step %.2f, camera from %s\n",
                sqrtf(d.x * d.x + d.y * d.y + d.z * d.z) / 100.f, cheat.aimBone,
                AngleBetween(view.Yaw, view.Pitch, pick.desiredYaw, pick.desiredPitch), angle, view.Yaw, view.Pitch,
                control.Yaw, control.Pitch, haveControl ? "" : " (not read)", useYaw, usePitch, frac, CameraSourceName());
        }
    }

    ResetAimFilter();
    // Written as the absolute next step toward the bot, never as a turn added on top of a fresh read.
    g_aimYaw = nextYaw;
    g_aimPitch = nextPitch;
    if (g_aimPitch > 89.f) g_aimPitch = 89.f;
    if (g_aimPitch < -89.f) g_aimPitch = -89.f;
    g_aimPending = true;
    if (!ChamsOnGameThread())
        ApplyAim();
}

void ApplyAim() {
    if (!g_aimPending || cheat.silentAim || MenuCapturesInput() || !cheat.aimbot)
        return;
    if (GetTickCount() - g_aimStamp > 120)
        return;
    uintptr_t controller = localPC ? localPC : g_aimController;
    if (!controller)
        return;
    LockAim(controller, g_aimYaw, g_aimPitch);
}
