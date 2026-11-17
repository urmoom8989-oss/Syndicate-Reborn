#include "Game.h"
#include "../Core/Math.h"
#include "../Core/Memory.h"
#include "../SDK/Init.h"
#include "../SDK/Objects.h"
#include "../SDK/Offsets.h"

#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <unordered_map>
#include <algorithm>
#include <vector>
#include <windows.h>

uintptr_t world = 0;
uintptr_t localPC = 0;
uintptr_t localPawn = 0;
uintptr_t camMgr = 0;
uintptr_t localPS = 0;
Camera gCam{};
std::vector<Target> targets;

static bool FiniteVec(const Vec3& v) {
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}

static bool FiniteRot(const FRotator& r) {
    return std::isfinite(r.Pitch) && std::isfinite(r.Yaw) && std::isfinite(r.Roll);
}

static void FinishCamera(Camera& c) {
    if (!std::isfinite(c.fov) || c.fov < 1.f || c.fov > 179.f)
        c.fov = 80.f;
    if (!FiniteVec(c.loc) || !FiniteRot(c.rot)) {
        c.valid = false;
        return;
    }
    c.valid = (c.loc.x != 0.f || c.loc.y != 0.f || c.loc.z != 0.f);
}

Camera GetCamera() {
    Camera c{};
    if (!camMgr) return c;

    if (ProcessEventIdx > 0) {
        if (Fn_GetCamLoc) {
            struct { Vec3 ReturnValue; } loc{};
            if (ProcessEvent(camMgr, Fn_GetCamLoc, &loc))
                c.loc = loc.ReturnValue;
        }
        if (Fn_GetCamRot) {
            struct { FRotator ReturnValue; } rot{};
            if (ProcessEvent(camMgr, Fn_GetCamRot, &rot))
                c.rot = rot.ReturnValue;
        }
        if (Fn_GetFOV) {
            struct { float ReturnValue; } fov{};
            if (ProcessEvent(camMgr, Fn_GetFOV, &fov))
                c.fov = fov.ReturnValue;
        }
    }

    if ((!c.loc.x && !c.loc.y && !c.loc.z) && off.CameraCachePrivate) {
        uintptr_t pov = camMgr + off.CameraCachePrivate + 0x10;
        c.loc = Read<Vec3>(pov + 0x00);
        c.rot = Read<FRotator>(pov + 0x0C);
        c.fov = Read<float>(pov + 0x18);
        if (!c.loc.x && !c.loc.y && !c.loc.z) {
            uintptr_t raw = camMgr + off.CameraCachePrivate;
            c.loc = Read<Vec3>(raw + 0x00);
            c.rot = Read<FRotator>(raw + 0x0C);
            c.fov = Read<float>(raw + 0x18);
        }
    }

    FinishCamera(c);
    return c;
}

bool WorldToScreen(const Vec3& wpos, Vec2& out, int w, int h) {
    Camera c = gCam;
    if (!c.valid) return false;
    if (c.fov < 1.f || c.fov > 179.f) c.fov = 80.f;

    FTransform view{};
    view.Rotation = RotatorToQuat(c.rot);
    view.Scale3D = { 1.f, 1.f, 1.f };
    FMatrix m = view.ToMatrixWithScale();
    Vec3 xAxis{ m.M[0][0], m.M[0][1], m.M[0][2] };
    Vec3 yAxis{ m.M[1][0], m.M[1][1], m.M[1][2] };
    Vec3 zAxis{ m.M[2][0], m.M[2][1], m.M[2][2] };
    Vec3 delta = wpos - c.loc;
    Vec3 t{ delta.dot(yAxis), delta.dot(zAxis), delta.dot(xAxis) };
    if (t.z < 1.f) return false;

    float cx = w / 2.f;
    float cy = h / 2.f;
    float sc = cx / tanf(c.fov * 0.017453292f / 2.f);
    out.x = cx + t.x * sc / t.z;
    out.y = cy - t.y * sc / t.z;
    return true;
}

static bool LooksLikeBoneArray(const TArrayPtr& bones);
static bool PoseLooksAlive(const TArrayPtr& bones, const FTransform& c2w, int head, int pelvis);

static bool MeshPoseLive(uintptr_t mesh) {
    if (!mesh || !off.BoneArray)
        return false;
    const BoneSet& set = BonesFor(mesh);
    if (set.Head <= 0 || set.Pelvis <= 0)
        return false;
    FTransform c2w = ReadComponentToWorld(mesh);
    TArrayPtr first = Read<TArrayPtr>(mesh + off.BoneArray);
    TArrayPtr second = Read<TArrayPtr>(mesh + off.BoneArray + (int)sizeof(TArrayPtr));
    if (LooksLikeBoneArray(first) && PoseLooksAlive(first, c2w, set.Head, set.Pelvis))
        return true;
    if (LooksLikeBoneArray(second) && PoseLooksAlive(second, c2w, set.Head, set.Pelvis))
        return true;
    return false;
}

static uintptr_t MeshWithBones(uintptr_t mesh) {
    if (!mesh || !off.BoneArray)
        return mesh;
    if (MeshPoseLive(mesh))
        return mesh;
    if (!off.MasterPose)
        return mesh;

    uintptr_t master = Read<uintptr_t>(mesh + off.MasterPose);
    if (master && master != mesh && ObjClass(master) && MeshPoseLive(master))
        return master;

    int32_t idx = Read<int32_t>(mesh + off.MasterPose);
    master = GetObjectByIndex(idx);
    if (master && master != mesh && ObjClass(master) && MeshPoseLive(master))
        return master;
    return mesh;
}

static bool LooksLikeBoneArray(const TArrayPtr& bones) {
    return bones.Data && bones.Num >= 20 && bones.Num <= 512 && bones.Max >= bones.Num && bones.Max <= 1024;
}

FTransform ReadComponentToWorld(uintptr_t mesh) {
    FTransform c2w{};
    if (mesh && off.ComponentToWorld)
        c2w = Read<FTransform>(mesh + off.ComponentToWorld);
    if ((c2w.Translation.x || c2w.Translation.y || c2w.Translation.z) &&
        (c2w.Scale3D.x || c2w.Scale3D.y || c2w.Scale3D.z))
        return c2w;
    if (mesh && Fn_GetCompToWorld) {
        struct { FTransform ReturnValue; } p{};
        if (ProcessEvent(mesh, Fn_GetCompToWorld, &p))
            return p.ReturnValue;
    }
    return c2w;
}

Vec3 GetMeshWorld(uintptr_t mesh) {
    return ReadComponentToWorld(mesh).Translation;
}

static bool BoneFrom(const TArrayPtr& bones, int idx, const FTransform& c2w, Vec3& out) {
    if (!bones.Data || idx < 0 || idx >= bones.Num) return false;
    FTransform bone = Read<FTransform>(bones.Data + (uintptr_t)idx * 0x30);
    out = MatrixOrigin(MulMatrix(bone.ToMatrixWithScale(), c2w.ToMatrixWithScale()));
    return true;
}

static bool PoseLooksAlive(const TArrayPtr& bones, const FTransform& c2w, int head, int pelvis) {
    Vec3 h, p;
    if (head > 0 && pelvis > 0 && BoneFrom(bones, head, c2w, h) && BoneFrom(bones, pelvis, c2w, p)) {
        float rise = h.z - p.z;
        Vec3 d = h - p;
        float span = sqrtf(d.dot(d));
        return rise >= 35.f && rise <= 160.f && span >= 40.f && span <= 190.f;
    }
    if (head > 0 && BoneFrom(bones, head, c2w, h)) {
        float rise = h.z - c2w.Translation.z;
        return rise >= 40.f && rise <= 200.f;
    }
    return false;
}

static TArrayPtr PickBoneArray(uintptr_t mesh, const FTransform& c2w, bool liveOnly) {
    TArrayPtr a = off.BoneArray ? Read<TArrayPtr>(mesh + off.BoneArray) : TArrayPtr{};
    TArrayPtr b = off.BoneArray ? Read<TArrayPtr>(mesh + off.BoneArray + sizeof(TArrayPtr)) : TArrayPtr{};
    const BoneSet& set = BonesFor(mesh);
    bool aLive = LooksLikeBoneArray(a) && PoseLooksAlive(a, c2w, set.Head, set.Pelvis);
    bool bLive = LooksLikeBoneArray(b) && PoseLooksAlive(b, c2w, set.Head, set.Pelvis);
    if (aLive && bLive) {
        Vec3 ah, bh;
        BoneFrom(a, set.Head > 0 ? set.Head : 0, c2w, ah);
        BoneFrom(b, set.Head > 0 ? set.Head : 0, c2w, bh);
        return ah.z >= bh.z ? a : b;
    }
    if (aLive) return a;
    if (bLive) return b;
    if (liveOnly) return {};
    if (LooksLikeBoneArray(a)) return a;
    if (LooksLikeBoneArray(b)) return b;
    return {};
}

bool MeshPoseAlive(uintptr_t mesh) {
    if (!mesh || !off.BoneArray) return false;
    FTransform c2w = ReadComponentToWorld(mesh);
    return PickBoneArray(mesh, c2w, true).Data != 0;
}

Vec3 GetBoneWorld(uintptr_t mesh, int idx) {
    if (!mesh || idx < 0) return {};
    FTransform c2w = ReadComponentToWorld(mesh);
    if (!off.BoneArray) return c2w.Translation;

    TArrayPtr bones = PickBoneArray(mesh, c2w, false);
    if (!bones.Data) {
        TArrayPtr first = Read<TArrayPtr>(mesh + off.BoneArray);
        if (first.Data) bones = first;
        else bones = Read<TArrayPtr>(mesh + off.BoneArray + sizeof(TArrayPtr));
    }

    Vec3 out;
    if (BoneFrom(bones, idx, c2w, out))
        return out;
    return idx <= 0 ? c2w.Translation : Vec3{};
}

Vec3 GetStableHead(uintptr_t mesh) {
    static std::unordered_map<uintptr_t, Vec3> last;
    FTransform c2w = ReadComponentToWorld(mesh);
    Vec3 meshPos = c2w.Translation;
    const BoneSet& set = BonesFor(mesh);
    TArrayPtr live = PickBoneArray(mesh, c2w, true);
    Vec3 head{};
    if (set.Head > 0 && BoneFrom(live, set.Head, c2w, head) && head.z >= meshPos.z + 45.f) {
        last[mesh] = head;
        if (last.size() > 64) last.clear();
        return head;
    }
    auto it = last.find(mesh);
    if (it != last.end())
        return it->second;
    if (meshPos.x || meshPos.y || meshPos.z) {
        head = meshPos;
        head.z += 90.f;
        return head;
    }
    return GetBoneWorld(mesh, set.Head > 0 ? set.Head : 0);
}

static int FirstBone(uintptr_t mesh, std::initializer_list<const wchar_t*> names) {
    if (!mesh || !Fn_GetBoneIndex || !FNameCtorW) return -1;
    for (const wchar_t* n : names) {
        struct { FName name; int32_t ret; } p{};
        p.name = MakeName(n);
        if (!p.name.ComparisonIndex) continue;
        if (!ProcessEvent(mesh, Fn_GetBoneIndex, &p)) continue;
        if (p.ret > 0) return p.ret;
    }
    return -1;
}

static int BoneCountOf(uintptr_t mesh) {
    if (!mesh || !off.BoneArray)
        return 0;
    TArrayPtr first = Read<TArrayPtr>(mesh + off.BoneArray);
    if (first.Num > 1 && first.Num <= 512)
        return first.Num;
    TArrayPtr second = Read<TArrayPtr>(mesh + off.BoneArray + (int)sizeof(TArrayPtr));
    if (second.Num > 1 && second.Num <= 512)
        return second.Num;
    return 0;
}

static FName BoneNameAt(uintptr_t mesh, int index) {
    static uintptr_t fn = 0;
    static bool tried = false;
    if (!tried) {
        tried = true;
        fn = GetFunction("SkinnedMeshComponent", "GetBoneName");
    }
    if (!fn || index <= 0)
        return {};
    struct { int32_t index; int32_t pad; FName name; } params{};
    params.index = index;
    if (!ProcessEvent(mesh, fn, &params))
        return {};
    return params.name;
}

static bool BoneSetReady(const BoneSet& b) {
    return b.Head > 0 && b.Pelvis > 0 && b.LUpper > 0 && b.RUpper > 0 && b.LThigh > 0 && b.RThigh > 0;
}

static uintptr_t MasterComponent(uintptr_t mesh) {
    if (!mesh || !off.MasterPose)
        return 0;
    uintptr_t raw = Read<uintptr_t>(mesh + off.MasterPose);
    if (raw > 0x10000 && raw != mesh && ObjClass(raw))
        return raw;
    int32_t idx = Read<int32_t>(mesh + off.MasterPose);
    uintptr_t object = GetObjectByIndex(idx);
    if (object && object != mesh && ObjClass(object))
        return object;
    return 0;
}

static void FillBones(BoneSet& b, uintptr_t mesh, bool scan) {
    FName scanned[160] = {};
    int scannedCount = 0;
    if (scan) {
        scannedCount = BoneCountOf(mesh);
        if (scannedCount > 160)
            scannedCount = 160;
        for (int i = 1; i < scannedCount; ++i)
            scanned[i] = BoneNameAt(mesh, i);
    }
    auto take = [&](std::initializer_list<const wchar_t*> names) {
        int found = FirstBone(mesh, names);
        if (found > 0 || !scan)
            return found;
        for (const wchar_t* n : names) {
            FName want = MakeName(n);
            if (!want.ComparisonIndex)
                continue;
            for (int i = 1; i < scannedCount; ++i) {
                if (scanned[i] == want)
                    return i;
            }
        }
        return -1;
    };
    b.Head = take({ L"head" });
    b.Neck = take({ L"neck_01", L"neck_02", L"neck" });
    b.Chest = take({ L"spine_05", L"spine_04", L"spine_03", L"spine_02" });
    b.Pelvis = take({ L"pelvis" });
    b.LUpper = take({ L"upperarm_l", L"clavicle_l" });
    b.LLower = take({ L"lowerarm_l" });
    b.LHand = take({ L"hand_l" });
    b.RUpper = take({ L"upperarm_r", L"clavicle_r" });
    b.RLower = take({ L"lowerarm_r" });
    b.RHand = take({ L"hand_r" });
    b.LThigh = take({ L"thigh_l" });
    b.LCalf = take({ L"calf_l" });
    b.LFoot = take({ L"foot_l", L"ball_l" });
    b.RThigh = take({ L"thigh_r" });
    b.RCalf = take({ L"calf_r" });
    b.RFoot = take({ L"foot_r", L"ball_r" });
}

static BoneSet ResolveBones(uintptr_t mesh) {
    BoneSet b;
    FillBones(b, mesh, false);
    if (!BoneSetReady(b))
        FillBones(b, mesh, true);
    if (!BoneSetReady(b)) {
        uintptr_t master = MasterComponent(mesh);
        if (master && master != mesh) {
            FillBones(b, master, false);
            if (!BoneSetReady(b))
                FillBones(b, master, true);
        }
    }
    return b;
}

const BoneSet& BonesFor(uintptr_t mesh) {
    static BoneSet empty;
    static std::unordered_map<uintptr_t, BoneSet> cache;
    // Meshes whose bones could not be named yet: tried again after 2 s, not on every call (each
    // try is up to ~200 ProcessEvent calls, and this runs several times per bot per frame).
    static std::unordered_map<uintptr_t, DWORD> failed;
    auto it = cache.find(mesh);
    if (it != cache.end()) return it->second;
    const DWORD now = GetTickCount();
    auto miss = failed.find(mesh);
    if (miss != failed.end() && now - miss->second < 2000)
        return empty;
    BoneSet r = ResolveBones(mesh);
    if (r.Head <= 0 || r.Pelvis <= 0 || r.LUpper <= 0 || r.RUpper <= 0 || r.LThigh <= 0 || r.RThigh <= 0) {
        if (failed.size() > 256) failed.clear();
        failed[mesh] = now;
        return empty;
    }
    failed.erase(mesh);
    if (cache.size() > 64) cache.clear();
    return cache.emplace(mesh, r).first->second;
}

// Some seasons (3.5, 4.x, 5.10) store player names with a key that the game's own GetPlayerName
// removes on read, so the raw PlayerName field showed random letters over bots. The ESP runs on the
// render thread, where engine functions must not be called, so names are resolved on the game thread
// (ResolveNamesOnGameThread, called from the HUD / ProcessEvent hooks) and cached per PlayerState.
// SRWLOCK, not std::mutex (Season X's old msvcp140.dll).
struct CachedName { std::string text; bool resolved; };
static SRWLOCK gNameLock = SRWLOCK_INIT;
static std::unordered_map<uintptr_t, CachedName> gNameCache;
static std::vector<uintptr_t> gNamePending;

static std::string ConvertName(const FString& fs) {
    if (!fs.Data || fs.Num <= 1 || fs.Num > 96) return {};
    std::string s;
    s.reserve(fs.Num);
    for (int i = 0; i < fs.Num - 1; i++) {
        wchar_t c = Read<wchar_t>((uintptr_t)fs.Data + i * 2);
        if (c < 32 || c > 126) continue;
        s.push_back((char)c);
    }
    return s;
}

void ResolveNamesOnGameThread() {
    static uintptr_t fnGetName = 0;
    static bool tried = false;
    if (!tried) { tried = true; fnGetName = GetFunction("PlayerState", "GetPlayerName"); }
    std::vector<uintptr_t> work;
    AcquireSRWLockExclusive(&gNameLock);
    work.swap(gNamePending);
    ReleaseSRWLockExclusive(&gNameLock);
    int done = 0;
    for (uintptr_t ps : work) {
        if (done >= 24) break; // the rest are queued again by the ESP next frame
        ++done;
        std::string name;
        bool ok = false;
        if (fnGetName && ps && (!Class_PlayerState || IsA(ps, Class_PlayerState))) {
            struct { FString ReturnValue; } params{};
            if (ProcessEvent(ps, fnGetName, &params)) { name = ConvertName(params.ReturnValue); ok = !name.empty(); }
        }
        AcquireSRWLockExclusive(&gNameLock);
        if (gNameCache.size() > 512) gNameCache.clear();
        gNameCache[ps] = CachedName{ name, ok };
        ReleaseSRWLockExclusive(&gNameLock);
    }
}

static std::string ReadName(uintptr_t ps) {
    if (!ps || !off.PlayerName) return {};
    if (Class_PlayerState && !IsA(ps, Class_PlayerState))
        return {};
    bool known = false;
    std::string cached;
    AcquireSRWLockShared(&gNameLock);
    auto it = gNameCache.find(ps);
    if (it != gNameCache.end()) { known = true; if (it->second.resolved) cached = it->second.text; }
    ReleaseSRWLockShared(&gNameLock);
    if (!cached.empty()) return cached;
    if (!known) {
        AcquireSRWLockExclusive(&gNameLock);
        if (gNamePending.size() < 256 && std::find(gNamePending.begin(), gNamePending.end(), ps) == gNamePending.end())
            gNamePending.push_back(ps);
        ReleaseSRWLockExclusive(&gNameLock);
    }
    FString fs = Read<FString>(ps + off.PlayerName);
    if (!fs.Data || fs.Num <= 1 || fs.Num > 96) return {};
    std::string s;
    s.reserve(fs.Num);
    for (int i = 0; i < fs.Num - 1; i++) {
        wchar_t c = Read<wchar_t>((uintptr_t)fs.Data + i * 2);
        if (c < 32 || c > 126) continue;
        s.push_back((char)c);
    }
    if (s.find("TimeToGo") != std::string::npos || s.find("Spectator") != std::string::npos || s.find("Default__") != std::string::npos)
        return {};
    return s;
}

static bool PawnDying(uintptr_t pawn) {
    if (!pawn || !off.bIsDying) return false;
    uint8_t v = Read<uint8_t>(pawn + off.bIsDying);
    if (off.bIsDyingMask) return (v & off.bIsDyingMask) != 0;
    return v != 0;
}

// Render time of the local player's own mesh this frame. Used as "now" when
// World.TimeSeconds isn't a reflected property (1.7.2 and other old seasons): our own
// character is always drawn, so a mesh rendered within 0.08 s of it is on screen.
static float refRenderTime = 0.f;

static void UpdateRefRenderTime() {
    refRenderTime = 0.f;
    if (!localPawn || !off.Mesh || !off.LastRenderTime)
        return;
    uintptr_t mesh = Read<uintptr_t>(localPawn + off.Mesh);
    if (mesh)
        refRenderTime = Read<float>(mesh + off.LastRenderTime);
}

static bool MeshVisible(uintptr_t mesh) {
    if (!mesh || !off.LastRenderTime)
        return true;
    float last = Read<float>(mesh + off.LastRenderTime);
    // Never rendered at all: this season may draw the character parts and not the body mesh,
    // so the render time says nothing. Don't hide the bot over it.
    if (!(last > 0.f))
        return true;
    if (off.TimeSeconds && world) {
        float now = Read<float>(world + off.TimeSeconds);
        return (now - last) <= 0.08f;
    }
    // No reference time (no own pawn yet, or this season doesn't update the body mesh's
    // render time): don't hide anyone. Hiding every bot was the 1.7.2 bug.
    if (refRenderTime <= 0.f)
        return true;
    return (refRenderTime - last) <= 0.08f;
}

int AimBoneIndex(const BoneSet& b, int menuBone) {
    if (menuBone == 1) return b.Neck > 0 ? b.Neck : -1;
    if (menuBone == 2) return b.Chest > 0 ? b.Chest : -1;
    if (menuBone == 3) return b.Pelvis > 0 ? b.Pelvis : -1;
    return b.Head > 0 ? b.Head : -1;
}

// Answer per class, cached: the main island has tens of thousands of actors and this runs on
// each of them every frame, so it must not call FName::ToString for every non-pawn actor.
static bool IsFortPawn(uintptr_t pawn) {
    if (!pawn || IsDefaultObject(pawn)) return false;
    const uintptr_t cls = ObjClass(pawn);
    if (!cls) return false;
    static std::unordered_map<uintptr_t, bool> known;
    static uintptr_t knownWorld = 0;
    if (knownWorld != world) {
        known.clear();
        knownWorld = world;
    }
    auto it = known.find(cls);
    if (it != known.end())
        return it->second;
    bool yes = false;
    if (Class_FortPawn && IsA(pawn, Class_FortPawn)) {
        yes = true;
    } else {
        std::string cn = GetName(cls);
        if (cn.find("Spectator") == std::string::npos && cn.find("TimeToGo") == std::string::npos)
            yes = cn.find("FortPlayerPawn") != std::string::npos || cn.find("FortPawn") != std::string::npos;
    }
    if (known.size() < 20000)
        known.emplace(cls, yes);
    return yes;
}

// Counts for the once-every-10-seconds pawn summary in SyndicateMenu.log.
static int statPawns = 0, statDying = 0, statNoMesh = 0;

static void AddFortPawn(uintptr_t pawn, int w, int h, int aimBone) {
    if (!pawn) return;
    statPawns++;
    if (PawnDying(pawn)) { statDying++; return; }
    for (const Target& existing : targets) {
        if (existing.pawn == pawn) return;
    }

    if (off.RootComponent && !Read<uintptr_t>(pawn + off.RootComponent))
        return;

    uintptr_t mesh = off.Mesh ? Read<uintptr_t>(pawn + off.Mesh) : 0;
    if (!mesh) { statNoMesh++; return; }
    mesh = MeshWithBones(mesh);

    Target t;
    t.pawn = pawn;
    t.ps = off.PlayerState ? Read<uintptr_t>(pawn + off.PlayerState) : 0;
    t.mesh = mesh;
    t.name = ReadName(t.ps);
    t.team = (t.ps && off.TeamIndex) ? Read<uint8_t>(t.ps + off.TeamIndex) : 0;
    t.visible = MeshVisible(mesh);
    t.bones = BonesFor(mesh);
    int bone = AimBoneIndex(t.bones, aimBone);
    Vec3 meshPos = GetMeshWorld(mesh);
    t.aimPos = bone > 0 ? GetBoneWorld(mesh, bone) : meshPos;
    if (!std::isfinite(t.aimPos.x) || !std::isfinite(t.aimPos.y) || !std::isfinite(t.aimPos.z) ||
        (t.aimPos.x == 0.f && t.aimPos.y == 0.f && t.aimPos.z == 0.f))
        t.aimPos = meshPos;
    if (!std::isfinite(t.aimPos.x) || !std::isfinite(t.aimPos.y) || !std::isfinite(t.aimPos.z) ||
        (t.aimPos.x == 0.f && t.aimPos.y == 0.f && t.aimPos.z == 0.f))
        t.aimPos = GetBoneWorld(mesh, 0);
    if ((!std::isfinite(t.aimPos.x) || !std::isfinite(t.aimPos.y) || !std::isfinite(t.aimPos.z) ||
         (t.aimPos.x == 0.f && t.aimPos.y == 0.f && t.aimPos.z == 0.f)) &&
        off.RootComponent && off.RelativeLocation) {
        uintptr_t root = Read<uintptr_t>(pawn + off.RootComponent);
        if (root)
            t.aimPos = Read<Vec3>(root + off.RelativeLocation);
    }
    Vec3 dd = (meshPos.x || meshPos.y || meshPos.z) ? (meshPos - gCam.loc) : (t.aimPos - gCam.loc);
    t.dist = sqrtf(dd.dot(dd));
    WorldToScreen(t.aimPos, t.screen, w, h);
    targets.push_back(t);
}

bool IsBotState(uintptr_t playerState) {
    if (!playerState || !off.bIsABot || !ObjClass(playerState))
        return false;
    uint8_t v = Read<uint8_t>(playerState + off.bIsABot);
    if (off.bIsABotMask) return (v & off.bIsABotMask) != 0;
    return v != 0;
}

bool IsBotPawn(uintptr_t pawn) {
    if (!pawn || !off.PlayerState)
        return false;
    return IsBotState(Read<uintptr_t>(pawn + off.PlayerState));
}

int CopyPointerArray(uintptr_t base, int offset, uintptr_t* out, int capacity, int start) {
    if (!base || offset < 0x28 || !out || capacity <= 0 || start < 0)
        return 0;
    TArrayPtr arr = Read<TArrayPtr>(base + offset);
    // The main island's persistent level holds far more than 30000 actors once the match has
    // started (floor loot, containers, builds). The old 30000 limit rejected the whole array
    // after the bus, so no bot was found: the Pre-season "ESP stops after the bus" bug.
    if (!arr.Data || arr.Num <= 0 || arr.Num > 2000000 || arr.Max < arr.Num || arr.Max > 4000000 || start >= arr.Num)
        return 0;
    int count = arr.Num - start;
    if (count > capacity)
        count = capacity;
    for (int i = 0; i < count; ++i)
        out[i] = Read<uintptr_t>(arr.Data + (uintptr_t)(start + i) * 8);
    return count;
}

// Per-scan counts for the pawn summary in SyndicateMenu.log.
static int statLevels = 0, statActors = 0, statPersistentActors = 0;

static void ForEachActorInLevel(uintptr_t level, void (*visit)(uintptr_t actor, void* user), void* user) {
    if (!level || !visit)
        return;
    DiscoverLevelActors(level);
    if (!off.LevelActors)
        return;
    statLevels++;
    uintptr_t raw[512];
    for (int start = 0; start < 2000000; ) {
        const int got = CopyPointerArray(level, off.LevelActors, raw, 512, start);
        if (got <= 0)
            break;
        for (int i = 0; i < got; ++i) {
            if (raw[i])
                visit(raw[i], user);
        }
        statActors += got;
        start += got;
        if (got < 512)
            break;
    }
}

static void CollectFromLevel(uintptr_t level, int w, int h, int aimBone) {
    if (!level || (int)targets.size() >= 4096)
        return;
    struct Pass { int w; int h; int aimBone; };
    Pass pass{ w, h, aimBone };
    ForEachActorInLevel(level, [](uintptr_t actor, void* user) {
        if ((int)targets.size() >= 4096)
            return;
        auto* p = (Pass*)user;
        if (!IsFortPawn(actor))
            return;
        AddFortPawn(actor, p->w, p->h, p->aimBone);
    }, &pass);
}

// The lobby is the frontend world (its map is called Frontend). This used to be "no
// acknowledged pawn", which is also true on the bus and, on old seasons, after the jump when the
// server possesses the new pawn without the client acknowledging it: the menu then kept only
// the pawns within 50 m and took the nearest bot for your own pawn.
static bool InLobby() {
    static uintptr_t lastWorld = 0;
    static bool lastLobby = false;
    if (!world)
        return false;
    if (world != lastWorld) {
        lastWorld = world;
        const std::string name = GetName(world);
        uintptr_t level = off.PersistentLevel ? Read<uintptr_t>(world + off.PersistentLevel) : 0;
        const std::string outer = level ? GetName(ObjOuter(level)) : std::string();
        lastLobby = name.find("Frontend") != std::string::npos || outer.find("Frontend") != std::string::npos;
        printf("[game] world %s (0x%llX): %s\n", name.empty() ? "?" : name.c_str(), (unsigned long long)world,
            lastLobby ? "lobby" : "match");
    }
    return lastLobby;
}

static void KeepNearbyLobbyPawns() {
    if (targets.empty())
        return;

    size_t closest = 0;
    for (size_t i = 1; i < targets.size(); i++) {
        if (targets[i].dist < targets[closest].dist)
            closest = i;
    }

    constexpr float lobbyKeep = 50.f * 100.f;
    std::vector<Target> kept;
    kept.reserve(targets.size());
    for (const Target& t : targets) {
        if (t.dist <= lobbyKeep)
            kept.push_back(t);
    }
    if (kept.empty())
        kept.push_back(targets[closest]);

    if (!localPawn)
        localPawn = kept.size() == 1 ? kept[0].pawn : targets[closest].pawn;
    targets.swap(kept);
}

// Once every 10 seconds: how many pawns the menu saw and why they were dropped, so a
// season where the bot check fails can be told apart from one where no pawns are found.
static void LogPawnSummary(size_t botCount) {
    static DWORD last = 0;
    const DWORD now = GetTickCount();
    if (last && now - last < 10000)
        return;
    last = now;
    int withState = 0, visible = 0;
    int sampleByte = -1;
    for (const Target& t : targets) {
        if (t.visible) visible++;
        if (!t.ps) continue;
        withState++;
        if (sampleByte < 0 && t.pawn != localPawn && off.bIsABot)
            sampleByte = Read<uint8_t>(t.ps + off.bIsABot);
    }
    printf("[esp] pawns %d, dying %d, no mesh %d, kept %d, with PlayerState %d, bots %d "
           "(bIsABot 0x%X mask 0x%X, sample byte %d, local pawn %s, visible %d, ref time %.2f)\n",
        statPawns, statDying, statNoMesh, (int)targets.size(), withState, (int)botCount,
        off.bIsABot, off.bIsABotMask, sampleByte, localPawn ? "yes" : "no", visible, refRenderTime);
    printf("[esp] scanned %d levels, %d actors (persistent level %d), %s, Level.Actors 0x%X, camera %s\n",
        statLevels, statActors, statPersistentActors, InLobby() ? "lobby" : "match", off.LevelActors,
        gCam.valid ? "ok" : "MISSING");
}

// ---- Offsets checked against the running game ---------------------------------------------
// Every season moves things around. The menu guesses SceneComponent.ComponentToWorld and the
// skinned mesh's bone array from the reflection data, and on old builds (Pre-season, UE 4.16)
// those guesses were wrong: 0x1C0 is inside RelativeRotation there, so every mesh position,
// bone and aim point was garbage while the box (drawn from the root's RelativeLocation) still
// looked right. Both are now checked against the game itself and moved when they don't match.
static int c2wState = 0;  // 0 not checked yet, 1 checked against the game, 2 asking the game each time
static int boneState = 0; // 0 not checked yet, 1 checked on a bot's live pose

static bool NearF(float a, float b, float tol) {
    return std::isfinite(a) && std::isfinite(b) && fabsf(a - b) <= tol;
}

static bool SameTransform(const FTransform& a, const FTransform& b) {
    return NearF(a.Translation.x, b.Translation.x, 5.f) && NearF(a.Translation.y, b.Translation.y, 5.f) &&
           NearF(a.Translation.z, b.Translation.z, 5.f) && NearF(a.Scale3D.x, b.Scale3D.x, 0.01f) &&
           NearF(a.Scale3D.y, b.Scale3D.y, 0.01f) && NearF(a.Scale3D.z, b.Scale3D.z, 0.01f) &&
           NearF(fabsf(a.Rotation.x), fabsf(b.Rotation.x), 0.05f) && NearF(fabsf(a.Rotation.y), fabsf(b.Rotation.y), 0.05f) &&
           NearF(fabsf(a.Rotation.z), fabsf(b.Rotation.z), 0.05f) && NearF(fabsf(a.Rotation.w), fabsf(b.Rotation.w), 0.05f);
}

// Asks the game for our own root component's transform (K2_GetComponentToWorld) and finds where
// the component keeps it. Falls back to asking the game every time when no offset matches.
static void VerifyComponentToWorld() {
    if (c2wState || !Fn_GetCompToWorld || !localPawn || !off.RootComponent)
        return;
    static DWORD last = 0;
    static int tries = 0;
    const DWORD now = GetTickCount();
    if (last && now - last < 500)
        return;
    last = now;
    const uintptr_t comp = Read<uintptr_t>(localPawn + off.RootComponent);
    if (!comp || !ObjClass(comp))
        return;
    struct { FTransform ReturnValue; } p{};
    if (!ProcessEvent(comp, Fn_GetCompToWorld, &p))
        return;
    const FTransform want = p.ReturnValue;
    if (!FiniteVec(want.Translation) || (!want.Translation.x && !want.Translation.y && !want.Translation.z) ||
        !(want.Scale3D.x > 0.01f))
        return;
    if (off.ComponentToWorld && SameTransform(Read<FTransform>(comp + off.ComponentToWorld), want)) {
        printf("[sdk] ComponentToWorld 0x%X checked against the game: ok\n", off.ComponentToWorld);
        c2wState = 1;
        return;
    }
    for (int o = 0x80; o <= 0x600; o += 0x10) {
        if (SameTransform(Read<FTransform>(comp + o), want)) {
            printf("[sdk] ComponentToWorld: guessed 0x%X, the game keeps it at 0x%X; using 0x%X\n",
                off.ComponentToWorld, o, o);
            off.ComponentToWorld = o;
            c2wState = 1;
            return;
        }
    }
    if (++tries >= 20) {
        printf("[sdk] ComponentToWorld not found (guessed 0x%X); asking the game for each mesh instead\n",
            off.ComponentToWorld);
        off.ComponentToWorld = 0;
        c2wState = 2;
    }
}

static bool LiveBonesAt(uintptr_t mesh, int o, const FTransform& c2w, const BoneSet& set) {
    const TArrayPtr a = Read<TArrayPtr>(mesh + o);
    const int need = (set.Head > set.Pelvis ? set.Head : set.Pelvis) + 1;
    return LooksLikeBoneArray(a) && a.Num >= need && PoseLooksAlive(a, c2w, set.Head, set.Pelvis);
}

// Finds the bone array on a bot that is standing (a skydiving or knocked bot fails the pose
// test, so this keeps trying once a second until one is on its feet).
static void VerifyBoneArray(const std::vector<Target>& bots) {
    if (boneState || !c2wState || bots.empty())
        return;
    static DWORD last = 0;
    static int tries = 0;
    const DWORD now = GetTickCount();
    if (last && now - last < 1000)
        return;
    last = now;
    ++tries;
    for (const Target& t : bots) {
        uintptr_t meshes[2] = { t.mesh, MasterComponent(t.mesh) };
        for (uintptr_t mesh : meshes) {
            if (!mesh)
                continue;
            const BoneSet& set = BonesFor(mesh);
            if (set.Head <= 0 || set.Pelvis <= 0)
                continue;
            const FTransform c2w = ReadComponentToWorld(mesh);
            if (off.BoneArray && (LiveBonesAt(mesh, off.BoneArray, c2w, set) ||
                                  LiveBonesAt(mesh, off.BoneArray + (int)sizeof(TArrayPtr), c2w, set))) {
                printf("[bones] bone array 0x%X checked on a bot: ok\n", off.BoneArray);
                boneState = 1;
                return;
            }
            for (int o = 0x200; o <= 0xC00; o += 8) {
                if (!LiveBonesAt(mesh, o, c2w, set))
                    continue;
                // The game keeps two bone buffers side by side; start at the first one.
                int pick = o;
                const TArrayPtr cur = Read<TArrayPtr>(mesh + o);
                const TArrayPtr prev = Read<TArrayPtr>(mesh + o - (int)sizeof(TArrayPtr));
                if (LooksLikeBoneArray(prev) && prev.Num == cur.Num)
                    pick = o - (int)sizeof(TArrayPtr);
                printf("[bones] bone array: guessed 0x%X, found a live pose at 0x%X; using 0x%X\n", off.BoneArray, o, pick);
                off.BoneArray = pick;
                boneState = 1;
                return;
            }
        }
    }
    if (tries % 15 == 1) {
        const Target& t = bots[0];
        const BoneSet& set = BonesFor(t.mesh);
        const FTransform c2w = ReadComponentToWorld(t.mesh);
        const TArrayPtr a = off.BoneArray ? Read<TArrayPtr>(t.mesh + off.BoneArray) : TArrayPtr{};
        Vec3 root{};
        if (off.RootComponent && off.RelativeLocation) {
            const uintptr_t rc = Read<uintptr_t>(t.pawn + off.RootComponent);
            if (rc) root = Read<Vec3>(rc + off.RelativeLocation);
        }
        printf("[bones] no live pose yet (try %d): mesh %s, master %s, head %d pelvis %d, GetBoneIndex %s, "
               "array 0x%X num %d, mesh at %.0f %.0f %.0f, bot root at %.0f %.0f %.0f\n",
            tries, GetName(ObjClass(t.mesh)).c_str(), MasterComponent(t.mesh) ? "yes" : "no", set.Head, set.Pelvis,
            Fn_GetBoneIndex ? "yes" : "MISSING", off.BoneArray, a.Num, c2w.Translation.x, c2w.Translation.y,
            c2w.Translation.z, root.x, root.y, root.z);
    }
}

void DescribeOffsetChecks(char* out, int size) {
    snprintf(out, (size_t)size, "mesh position 0x%X %s  bones 0x%X %s", off.ComponentToWorld,
        c2wState == 1 ? "(checked)" : c2wState == 2 ? "(asking the game)" : "(not checked yet)", off.BoneArray,
        boneState ? "(checked)" : "(not checked yet)");
}

// ---- Real players ------------------------------------------------------------------------
// A real player is a pawn in the match whose PlayerState is not a bot, not a spectator and
// not ours. The old check walked GameState.PlayerArray, which on Erbium also holds a
// PlayerState with no pawn that is not a bot: on Pre-season that paused every weapon mod and
// Magic Bullet with nobody else in the match. A real player without a pawn (dead, spectating)
// can't be hit, so only pawns count; the PlayerArray entries are still logged.
static DWORD lastHumanSeen = 0;
static bool humanScanDone = false;

static bool IsSpectatorState(uintptr_t ps) {
    static PropInfo spectator = FindClassProp("PlayerState", "bIsSpectator");
    static PropInfo onlySpectator = FindClassProp("PlayerState", "bOnlySpectator");
    auto bit = [&](const PropInfo& prop) {
        if (!ps || !prop.found || prop.offset <= 0)
            return false;
        const uint8_t v = Read<uint8_t>(ps + prop.offset);
        return prop.mask ? (v & prop.mask) != 0 : v != 0;
    };
    return bit(spectator) || bit(onlySpectator);
}

static void NoteHumanPawns() {
    humanScanDone = true;
    for (const Target& t : targets) {
        if (!t.ps || t.pawn == localPawn || t.ps == localPS)
            continue;
        if (IsBotState(t.ps) || IsSpectatorState(t.ps))
            continue;
        if (!lastHumanSeen || GetTickCount() - lastHumanSeen > 3000)
            printf("[humans] real player in the match: %s (pawn %s)\n", t.name.empty() ? "?" : t.name.c_str(),
                GetName(ObjClass(t.pawn)).c_str());
        lastHumanSeen = GetTickCount();
        return;
    }
}

// What GameState.PlayerArray holds besides bots and us, once per world and whenever it changes.
static void LogPlayerArray() {
    static uintptr_t loggedWorld = 0;
    static int loggedCount = -1;
    static DWORD last = 0;
    const DWORD now = GetTickCount();
    if (last && now - last < 5000 && loggedWorld == world)
        return;
    last = now;
    if (!world || !off.GameState || !off.PlayerArray)
        return;
    const uintptr_t gameState = Read<uintptr_t>(world + off.GameState);
    if (!gameState || !ObjClass(gameState))
        return;
    uintptr_t states[256] = {};
    const int n = CopyPointerArray(gameState, off.PlayerArray, states, 256, 0);
    int bots = 0, others = 0;
    for (int i = 0; i < n; ++i) {
        if (!states[i] || states[i] == localPS || !ObjClass(states[i]))
            continue;
        if (IsBotState(states[i])) bots++;
        else others++;
    }
    if (loggedWorld == world && loggedCount == others)
        return;
    loggedWorld = world;
    loggedCount = others;
    printf("[humans] PlayerArray: %d entries, %d bots, %d others besides you\n", n, bots, others);
    for (int i = 0, shown = 0; i < n && shown < 8; ++i) {
        const uintptr_t ps = states[i];
        if (!ps || ps == localPS || !ObjClass(ps) || IsBotState(ps))
            continue;
        shown++;
        bool hasPawn = false;
        for (const Target& t : targets) {
            if (t.ps == ps) { hasPawn = true; break; }
        }
        printf("[humans]   %s: class %s, spectator %s, pawn %s\n", ReadName(ps).empty() ? "?" : ReadName(ps).c_str(),
            GetName(ObjClass(ps)).c_str(), IsSpectatorState(ps) ? "yes" : "no", hasPawn ? "yes" : "no");
    }
}

bool OtherHumansInMatch() {
    // Fail safe: without the bot flag the menu can't tell bots from players, so assume players.
    if (!world || !off.bIsABot || !off.PlayerState)
        return true;
    if (InLobby())
        return false;
    if (!humanScanDone)
        return true;
    return lastHumanSeen && GetTickCount() - lastHumanSeen < 3000;
}

void CollectTargets(int w, int h, int aimBone) {
    targets.clear();
    statPawns = statDying = statNoMesh = 0;
    statLevels = statActors = statPersistentActors = 0;
    if (!world)
        return;
    UpdateRefRenderTime();

    uintptr_t persistent = 0;
    if (off.PersistentLevel) {
        persistent = Read<uintptr_t>(world + off.PersistentLevel);
        CollectFromLevel(persistent, w, h, aimBone);
        statPersistentActors = statActors;
    }
    if (off.Levels) {
        // Bots live in the persistent level; the first sublevels are enough (the island streams
        // hundreds of sublevels full of static meshes, too many to walk every frame).
        uintptr_t extra[32] = {};
        const int n = CopyPointerArray(world, off.Levels, extra, 32, 0);
        for (int i = 0; i < n && (int)targets.size() < 4096; ++i) {
            if (!extra[i] || extra[i] == persistent)
                continue;
            CollectFromLevel(extra[i], w, h, aimBone);
        }
    }

    if (!localPawn && localPS) {
        for (const Target& t : targets) {
            if (t.ps == localPS) {
                localPawn = t.pawn;
                break;
            }
        }
    }

    if (InLobby())
        KeepNearbyLobbyPawns();

    const bool lobby = InLobby();
    if (!lobby) {
        NoteHumanPawns();
        LogPlayerArray();
    }

    // Bots only: drop every pawn that isn't a bot (real players and ourselves).
    std::vector<Target> bots;
    bots.reserve(targets.size());
    for (const Target& t : targets) {
        if (t.pawn != localPawn && IsBotState(t.ps))
            bots.push_back(t);
    }
    LogPawnSummary(bots.size());
    if (!lobby)
        VerifyBoneArray(bots);
    targets.swap(bots);
}

// ---- Camera source picker ---------------------------------------------------------------------
// Season 3 (3.5) log, 2026-11-10: the camera read from the render thread (GetCameraLocation /
// CameraCachePrivate) held a new random spot within 5000 units of the map centre and a random
// rotation (pitch up to 180) on almost every frame, so ESP labels clumped in one place and aim
// steered toward nothing. The game itself renders fine, so the real view is there at the right
// moment. The menu now reads several sources and uses the first one that agrees with your pawn
// and where you are aiming:
//   1. game thread: the same calls made from the HUD hook, right after the game drew the frame
//   2. render thread: GetCameraLocation / CameraCachePrivate (what every season used before)
//   3. camera actor: the camera manager moves its own root to the view every frame
//   4. ViewTarget.POV and LastFrameCameraCachePrivate.POV
//   5. estimate: your aim direction from a spot behind your pawn's head (last resort)
// "[camera] source" lines say which one is in use and why the others were skipped.

static SRWLOCK gGameThreadCamLock = SRWLOCK_INIT;
static Camera gGameThreadCam{};
static ULONGLONG gGameThreadCamTick = 0;
static volatile LONG gGameThreadCamSamples = 0;

void SampleCameraOnGameThread() {
    const uintptr_t mgr = camMgr;
    if (!mgr)
        return;
    Camera c{};
    __try {
        c = GetCamera();
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return;
    }
    if (!c.valid)
        return;
    AcquireSRWLockExclusive(&gGameThreadCamLock);
    gGameThreadCam = c;
    gGameThreadCamTick = GetTickCount64();
    ReleaseSRWLockExclusive(&gGameThreadCamLock);
    InterlockedIncrement(&gGameThreadCamSamples);
}

static float NormAxis(float a) {
    if (!std::isfinite(a))
        return a;
    a = fmodf(a, 360.f);
    if (a > 180.f) a -= 360.f;
    if (a < -180.f) a += 360.f;
    return a;
}

static FRotator NormRot(FRotator r) {
    r.Pitch = NormAxis(r.Pitch);
    r.Yaw = NormAxis(r.Yaw);
    r.Roll = NormAxis(r.Roll);
    return r;
}

enum CamSource { CamGameThread, CamRender, CamActor, CamViewTarget, CamLastFrame, CamEstimate, CamSourceCount };
static const char* const kCamSourceNames[CamSourceCount] = {
    "game thread", "render thread", "camera actor", "ViewTarget.POV", "LastFrameCameraCache", "estimate from your pawn"
};
static int gCamSource = -1;

const char* CameraSourceName() {
    return gCamSource >= 0 && gCamSource < CamSourceCount ? kCamSourceNames[gCamSource] : "none";
}

static Camera ReadPov(uintptr_t pov, float fallbackFov) {
    Camera c{};
    c.loc = Read<Vec3>(pov + 0x00);
    c.rot = Read<FRotator>(pov + 0x0C);
    c.fov = Read<float>(pov + 0x18);
    if (!std::isfinite(c.fov) || c.fov < 1.f || c.fov > 179.f)
        c.fov = fallbackFov;
    FinishCamera(c);
    return c;
}

Camera PickCamera() {
    static bool offsetsTried = false;
    static int offViewTarget = 0, offLastFrame = 0, offRelRotation = 0;
    if (!offsetsTried) {
        offsetsTried = true;
        PropInfo p = FindClassProp("PlayerCameraManager", "ViewTarget");
        if (p.found && p.offset > 0) offViewTarget = p.offset;
        p = FindClassProp("PlayerCameraManager", "LastFrameCameraCachePrivate");
        if (p.found && p.offset > 0) offLastFrame = p.offset;
        p = FindClassProp("SceneComponent", "RelativeRotation");
        if (p.found && p.offset > 0) offRelRotation = p.offset;
        printf("[camera] extra sources: ViewTarget 0x%X, LastFrameCameraCachePrivate 0x%X, RelativeRotation 0x%X, ComponentToWorld 0x%X\n",
            offViewTarget, offLastFrame, offRelRotation, off.ComponentToWorld);
    }

    Camera none{};
    if (!camMgr) {
        gCamSource = -1;
        return none;
    }

    // What the camera should agree with: your pawn's position and where you are aiming.
    bool havePawn = false, haveCtrl = false;
    Vec3 pawnLoc{};
    FRotator ctrl{};
    if (localPawn && off.RootComponent) {
        uintptr_t root = Read<uintptr_t>(localPawn + off.RootComponent);
        if (root) {
            FTransform t = ReadComponentToWorld(root);
            pawnLoc = t.Translation;
            if (!pawnLoc.x && !pawnLoc.y && !pawnLoc.z && off.RelativeLocation)
                pawnLoc = Read<Vec3>(root + off.RelativeLocation);
            havePawn = FiniteVec(pawnLoc) && (pawnLoc.x || pawnLoc.y || pawnLoc.z);
        }
    }
    if (localPC && off.ControlRotation) {
        ctrl = NormRot(Read<FRotator>(localPC + off.ControlRotation));
        haveCtrl = FiniteRot(ctrl) && fabsf(ctrl.Pitch) <= 90.5f;
    }

    Camera cand[CamSourceCount]{};
    bool tried[CamSourceCount]{};

    {
        AcquireSRWLockShared(&gGameThreadCamLock);
        const Camera gt = gGameThreadCam;
        const ULONGLONG at = gGameThreadCamTick;
        ReleaseSRWLockShared(&gGameThreadCamLock);
        if (at && GetTickCount64() - at < 300) {
            cand[CamGameThread] = gt;
            tried[CamGameThread] = true;
        }
    }
    cand[CamRender] = GetCamera();
    tried[CamRender] = true;
    const float fovGuess = (cand[CamGameThread].valid ? cand[CamGameThread].fov
                          : cand[CamRender].fov > 1.f ? cand[CamRender].fov
                          : gCam.fov > 1.f ? gCam.fov : 80.f);
    if (off.RootComponent) {
        uintptr_t root = Read<uintptr_t>(camMgr + off.RootComponent);
        if (root) {
            Camera c{};
            c.loc = off.RelativeLocation ? Read<Vec3>(root + off.RelativeLocation) : Vec3{};
            if (offRelRotation)
                c.rot = Read<FRotator>(root + offRelRotation);
            c.fov = fovGuess;
            FinishCamera(c);
            cand[CamActor] = c;
            tried[CamActor] = offRelRotation != 0;
        }
    }
    if (offViewTarget) {
        cand[CamViewTarget] = ReadPov(camMgr + offViewTarget + 0x10, fovGuess);
        tried[CamViewTarget] = true;
    }
    if (offLastFrame) {
        cand[CamLastFrame] = ReadPov(camMgr + offLastFrame + 0x10, fovGuess);
        tried[CamLastFrame] = true;
    }
    if (havePawn && haveCtrl) {
        // Fortnite's third-person camera sits behind and a bit above the pawn, looking where you aim.
        const float p = ctrl.Pitch * 0.01745329251f, y = ctrl.Yaw * 0.01745329251f;
        const Vec3 fwd{ cosf(p) * cosf(y), cosf(p) * sinf(y), sinf(p) };
        Camera c{};
        c.loc = { pawnLoc.x - fwd.x * 250.f, pawnLoc.y - fwd.y * 250.f, pawnLoc.z + 70.f - fwd.z * 250.f };
        c.rot = ctrl;
        c.fov = fovGuess;
        FinishCamera(c);
        cand[CamEstimate] = c;
        tried[CamEstimate] = true;
    }

    // A source must give the same view two frames in a row (the bad 3.5 read never did).
    static Vec3 prevLoc[CamSourceCount]{};
    static bool prevOk[CamSourceCount]{};
    bool steady[CamSourceCount]{};
    for (int i = 0; i < CamSourceCount; ++i) {
        cand[i].rot = NormRot(cand[i].rot);
        const bool ok = tried[i] && cand[i].valid;
        if (ok && prevOk[i]) {
            const Vec3 d = cand[i].loc - prevLoc[i];
            steady[i] = d.dot(d) < 1500.f * 1500.f;
        }
        prevOk[i] = ok;
        prevLoc[i] = cand[i].loc;
    }

    const char* why[CamSourceCount]{};
    auto problem = [&](int i, bool strict) -> const char* {
        const Camera& c = cand[i];
        if (!tried[i]) return "not available";
        if (!c.valid) return "empty";
        if (fabsf(c.rot.Pitch) > 90.5f) return "pitch out of range";
        if (i == CamEstimate) return nullptr;
        if (!steady[i]) return "changes every frame";
        if (havePawn) {
            const Vec3 d = c.loc - pawnLoc;
            if (d.dot(d) > 2500.f * 2500.f) return "far from your pawn";
            if (strict && haveCtrl && fabsf(NormAxis(c.rot.Yaw - ctrl.Yaw)) > 50.f) return "faces away from your aim";
        }
        return nullptr;
    };

    int pick = -1;
    for (int pass = 0; pass < 2 && pick < 0; ++pass) {
        for (int i = 0; i < CamSourceCount; ++i) {
            if (i == CamEstimate) continue;
            why[i] = problem(i, pass == 0);
            if (!why[i]) { pick = i; break; }
        }
    }
    if (pick < 0) {
        why[CamEstimate] = problem(CamEstimate, true);
        if (!why[CamEstimate]) pick = CamEstimate;
    }

    static int lastPick = -2;
    static ULONGLONG lastLog = 0;
    const ULONGLONG now = GetTickCount64();
    if (pick != lastPick || now - lastLog > 10000) {
        lastLog = now;
        printf("[camera] source: %s (pawn %s at %.0f %.0f %.0f, aim %s %.1f %.1f, game-thread samples %ld)\n",
            pick >= 0 ? kCamSourceNames[pick] : "NONE (ESP and aim wait)",
            havePawn ? "yes" : "no", pawnLoc.x, pawnLoc.y, pawnLoc.z,
            haveCtrl ? "yes" : "no", ctrl.Pitch, ctrl.Yaw, (long)gGameThreadCamSamples);
        for (int i = 0; i < CamSourceCount; ++i) {
            const Camera& c = cand[i];
            printf("[camera]   %-22s %s  loc %.0f %.0f %.0f rot %.1f %.1f fov %.1f\n", kCamSourceNames[i],
                i == pick ? "USED" : (why[i] ? why[i] : "ok"), c.loc.x, c.loc.y, c.loc.z, c.rot.Pitch, c.rot.Yaw, c.fov);
        }
    }
    lastPick = pick;
    gCamSource = pick;
    if (pick < 0)
        return none;
    Camera out = cand[pick];
    out.valid = true;
    return out;
}

static uintptr_t FindCameraManager() {
    if (localPC && off.PlayerCameraManager) {
        uintptr_t mgr = Read<uintptr_t>(localPC + off.PlayerCameraManager);
        if (mgr && ObjClass(mgr)) return mgr;
    }

    static uintptr_t classPCM = 0;
    static uintptr_t cached = 0;
    if (cached && ObjClass(cached)) return cached;
    if (!classPCM) classPCM = FindObjectByName("PlayerCameraManager", "Class");
    if (!classPCM) return 0;

    int n = ObjectCount();
    for (int i = 0; i < n; i++) {
        uintptr_t o = GetObjectByIndex(i);
        if (!o || IsDefaultObject(o)) continue;
        if (!IsA(o, classPCM)) continue;
        cached = o;
        return o;
    }
    return 0;
}

void TickGame() {
    world = FindWorld();
    localPC = 0;
    localPawn = 0;
    camMgr = 0;
    localPS = 0;
    uintptr_t engine = FindEngine();
    if (!engine || !world) return;

    uintptr_t vp = off.GameViewport ? Read<uintptr_t>(engine + off.GameViewport) : 0;
    uintptr_t gi = (vp && off.ViewportGameInstance) ? Read<uintptr_t>(vp + off.ViewportGameInstance) : 0;
    if (!gi && world && off.OwningGameInstance)
        gi = Read<uintptr_t>(world + off.OwningGameInstance);

    if (gi && off.LocalPlayers) {
        TArrayPtr lps = Read<TArrayPtr>(gi + off.LocalPlayers);
        uintptr_t lp = (lps.Data && lps.Num > 0) ? Read<uintptr_t>(lps.Data) : 0;
        localPC = (lp && off.PlayerController) ? Read<uintptr_t>(lp + off.PlayerController) : 0;
    }

    localPawn = (localPC && off.AcknowledgedPawn) ? Read<uintptr_t>(localPC + off.AcknowledgedPawn) : 0;
    if (!localPawn && localPC && off.ControllerPawn)
        localPawn = Read<uintptr_t>(localPC + off.ControllerPawn);
    camMgr = FindCameraManager();
    static uintptr_t humanWorld = 0;
    if (humanWorld != world) {
        humanWorld = world;
        humanScanDone = false;
        lastHumanSeen = 0;
    }
    localPS = (localPawn && off.PlayerState) ? Read<uintptr_t>(localPawn + off.PlayerState) : 0;
    if (!localPS && localPC && off.ControllerPlayerState)
        localPS = Read<uintptr_t>(localPC + off.ControllerPlayerState);
    Camera cam = PickCamera();
    if (cam.valid) {
        // Debug for "ESP/aim all over the place": a camera that jumps far between two frames
        // means the camera read is wrong on this build (logged at most every 2 s).
        static DWORD lastJumpLog = 0;
        static int jumps = 0;
        const Vec3 move = cam.loc - gCam.loc;
        float turn = fabsf(cam.rot.Yaw - gCam.rot.Yaw);
        turn = fmodf(turn, 360.f);
        if (turn > 180.f) turn = 360.f - turn;
        const float moved = sqrtf(move.dot(move));
        if (gCam.valid && (moved > 3000.f || (turn > 45.f && fabsf(cam.rot.Pitch) < 70.f))) {
            ++jumps;
            const DWORD now = GetTickCount();
            if (now - lastJumpLog > 2000) {
                lastJumpLog = now;
                printf("[camera] jump #%d: moved %.0f units, yaw turned %.1f deg in one frame (loc %.0f %.0f %.0f rot %.1f %.1f fov %.1f, source %s)\n",
                    jumps, moved, turn, cam.loc.x, cam.loc.y, cam.loc.z, cam.rot.Pitch, cam.rot.Yaw, cam.fov, CameraSourceName());
            }
        }
    }
    gCam = cam;
    VerifyComponentToWorld();
}

void ForEachLevelActor(void (*visit)(uintptr_t actor, void* user), void* user) {
    if (!visit || !world)
        return;

    uintptr_t seen[48] = {};
    int seenCount = 0;
    auto scan = [&](uintptr_t level) {
        if (!level || seenCount >= 48)
            return;
        for (int i = 0; i < seenCount; i++) {
            if (seen[i] == level)
                return;
        }
        seen[seenCount++] = level;
        ForEachActorInLevel(level, visit, user);
    };

    if (off.PersistentLevel)
        scan(Read<uintptr_t>(world + off.PersistentLevel));
    if (off.Levels) {
        uintptr_t extra[32] = {};
        const int n = CopyPointerArray(world, off.Levels, extra, 32, 0);
        for (int i = 0; i < n; ++i)
            scan(extra[i]);
    }
}

// ---- Feature status (debug) -----------------------------------------------------------------
// Each feature reports what it is doing in one short line ("active on B_Assault_...: 6 of 8
// fields", "not in this season: no ZiplineState"). A line is written to SyndicateMenu.log as
// "[feature] ..." when it changes (at most every 1.5 s per feature), and the debug overlay
// lists the lines reported in the last 3 s.
namespace {
struct FeatureLine {
    const char* name = nullptr;
    char text[160] = {};
    char logged[160] = {};
    DWORD when = 0;
    DWORD loggedWhen = 0;
};
FeatureLine g_features[48];
}

void FeatureStatus(const char* feature, const char* fmt, ...) {
    if (!feature || !fmt)
        return;
    FeatureLine* line = nullptr;
    for (FeatureLine& f : g_features) {
        if (f.name == feature || (f.name && strcmp(f.name, feature) == 0)) {
            line = &f;
            break;
        }
        if (!f.name) {
            f.name = feature;
            line = &f;
            break;
        }
    }
    if (!line)
        return;
    va_list args;
    va_start(args, fmt);
    vsnprintf(line->text, sizeof(line->text), fmt, args);
    va_end(args);
    const DWORD now = GetTickCount();
    line->when = now;
    if (strcmp(line->text, line->logged) != 0 && (!line->loggedWhen || now - line->loggedWhen >= 1500)) {
        printf("[feature] %s: %s\n", feature, line->text);
        memcpy(line->logged, line->text, sizeof(line->logged));
        line->loggedWhen = now;
    }
}

int FeatureStatusLines(char (*out)[160], int max) {
    const DWORD now = GetTickCount();
    int n = 0;
    for (const FeatureLine& f : g_features) {
        if (!f.name || n >= max)
            break;
        if (now - f.when > 3000)
            continue;
        snprintf(out[n++], 160, "%s: %s", f.name, f.text);
    }
    return n;
}
