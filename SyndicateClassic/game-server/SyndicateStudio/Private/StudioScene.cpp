#include "pch.h"
#include "../Public/StudioScene.h"
#include <cmath>
#include <algorithm>
#include <unordered_map>

extern void StudioLog(const char* Text);

// Scene query for SyndicateStudio: actor enumeration, world-to-screen projection, raycasting,
// and actor manipulation. All code runs on the game thread (called from StudioPipe via the
// WH_GETMESSAGE hook). Patterns are taken directly from SyndicateClient.cpp.
//
// MSVC C2712 fix: __try/__except cannot appear in any function that has local variables
// (or a return type) with destructors. The pattern used here is:
//   _Impl()  — does the real work, free to use std::string/std::vector/etc. No __try.
//   _Safe()  — thin SEH wrapper with only POD locals and POD return type. Calls _Impl().
//   Public() — public API, may return std::string etc. Calls _Safe(). No __try.

namespace StudioScene {

    // ---- Cached actor list ----

    static std::vector<ActorInfo> CachedActors;   // the nearby actors sent to the launcher (ActorInfo.Index = slot)
    static std::vector<AActor*> RawActors;        // slot -> actor; a slot keeps its actor until it is gone (then nullptr)
    static std::unordered_map<AActor*, uint32_t> SlotOf;
    static UWorld* SlotWorld = nullptr;           // slots are reset when the world changes (main menu -> island)

    static uint32_t SlotFor(AActor* Actor) {
        auto It = SlotOf.find(Actor);
        if (It != SlotOf.end()) return It->second;
        uint32_t Slot = (uint32_t)RawActors.size();
        RawActors.push_back(Actor);
        SlotOf.emplace(Actor, Slot);
        return Slot;
    }

    // Reuse SyndicateClient's SyndicateAllActors pattern: GetAllActorsOfClass via ProcessEvent
    static UObject* AllActorsLib = nullptr;
    static UFunction* AllActorsFn = nullptr;
    static int32 AllActorsWorldOff = -1, AllActorsClassOff = -1, AllActorsOutOff = -1;
    static uint32 AllActorsSize = 0;
    static bool AllActorsTried = false;

    static void InitAllActors() {
        if (AllActorsTried) return;
        AllActorsTried = true;
        AllActorsLib = (UObject*)UGameplayStatics::GetDefaultObj();
        AllActorsFn = AllActorsLib ? AllActorsLib->GetFunction("GetAllActorsOfClass") : nullptr;
        if (AllActorsFn) {
            auto P = AllActorsFn->GetParamsNamed();
            AllActorsSize = uint32(P.Size);
            for (auto& M : P.NameOffsetMap) {
                if (M.Name == "WorldContextObject") AllActorsWorldOff = int32(M.Offset);
                else if (M.Name == "ActorClass") AllActorsClassOff = int32(M.Offset);
                else if (M.Name == "OutActors") AllActorsOutOff = int32(M.Offset);
            }
        }
    }

    static void GetAllActorsOfClass(UWorld* World, const UClass* Class, std::vector<AActor*>& Out) {
        Out.clear();
        InitAllActors();
        if (!AllActorsFn || !World || !Class || AllActorsWorldOff < 0 || AllActorsClassOff < 0 || AllActorsOutOff < 0 || AllActorsSize > 256) return;
        alignas(16) uint8_t Buf[256]{};
        *(UWorld**)(Buf + AllActorsWorldOff) = World;
        *(const UClass**)(Buf + AllActorsClassOff) = Class;
        AllActorsLib->ProcessEvent(AllActorsFn, Buf);
        auto& Arr = *(TArray<AActor*>*)(Buf + AllActorsOutOff);
        Out.reserve(size_t(Arr.Num() > 0 ? Arr.Num() : 0));
        for (int i = 0; i < Arr.Num(); ++i) Out.push_back(Arr[i]);
        Arr.Free();
    }

    // ---- Projection ----

    static UFunction* ProjectFn = nullptr;
    static UFunction* ViewportFn = nullptr;
    static int32 PjWorld = -1, PjScreen = -1, PjRelative = -1, PjReturn = -1, PjSize = 0;
    static int32 VpX = -1, VpY = -1, VpSize = 0;
    static bool ProjectTried = false;

    static UObject* GetPC() {
        auto World = UWorld::GetWorld();
        if (!World) return nullptr;
        auto Instance = World->OwningGameInstance;
        if (!Instance || !Instance->LocalPlayers.Num()) return nullptr;
        auto Player = Instance->LocalPlayers[0];
        return Player ? (UObject*)Player->PlayerController : nullptr;
    }

    static bool InitProject(UObject* PC) {
        if (ProjectTried) return ProjectFn && ViewportFn && PjWorld >= 0 && PjScreen >= 0 && PjReturn >= 0 && VpX >= 0 && VpY >= 0;
        ProjectTried = true;
        ProjectFn = PC->GetFunction("ProjectWorldLocationToScreen");
        ViewportFn = PC->GetFunction("GetViewportSize");
        if (!ProjectFn || !ViewportFn) return false;
        auto P = ProjectFn->GetParamsNamed(); PjSize = int32(P.Size);
        for (auto& Pm : P.NameOffsetMap) {
            if (Pm.Name == "WorldLocation") PjWorld = int32(Pm.Offset);
            else if (Pm.Name == "ScreenLocation") PjScreen = int32(Pm.Offset);
            else if (Pm.Name == "bPlayerViewportRelative") PjRelative = int32(Pm.Offset);
            else if (Pm.Name == "ReturnValue") PjReturn = int32(Pm.Offset);
        }
        auto V = ViewportFn->GetParamsNamed(); VpSize = int32(V.Size);
        for (auto& Pm : V.NameOffsetMap) {
            if (Pm.Name == "SizeX") VpX = int32(Pm.Offset);
            else if (Pm.Name == "SizeY") VpY = int32(Pm.Offset);
        }
        return PjWorld >= 0 && PjScreen >= 0 && PjReturn >= 0 && VpX >= 0 && VpY >= 0 && PjSize <= 64 && VpSize <= 32;
    }

    // ---- Camera manager access ----

    static int32 PcCameraOff = -2;
    class CameraManager : public UObject {
    public:
        DEFINE_FUNC(GetCameraLocation, FVector);
        DEFINE_FUNC(GetCameraRotation, FRotator);
        DEFINE_FUNC(GetFOVAngle, float);
    };

    static CameraManager* GetCameraManager(UObject* PC) {
        if (PcCameraOff == -2) PcCameraOff = int32(PC->GetOffset("PlayerCameraManager"));
        if (PcCameraOff < 0) return nullptr;
        return *(CameraManager**)(__int64(PC) + PcCameraOff);
    }

    // ---- _Impl functions: do the real work, use C++ types freely, NO __try ----

    static bool ReadActorInfo_Impl(AActor* Actor, uint32_t Slot, const float* Loc3) {
        ActorInfo info;
        info.Index = Slot;

        auto NameStr = ((UObject*)Actor)->Name.ToSDKString();
        info.Name = NameStr.c_str();

        if (((UObject*)Actor)->Class) {
            auto ClassStr = ((UObject*)Actor)->Class->Name.ToSDKString();
            info.ClassName = ClassStr.c_str();
        }

        info.X = Loc3[0]; info.Y = Loc3[1]; info.Z = Loc3[2];

        auto Rot = Actor->K2_GetActorRotation();
        info.Pitch = Rot.Pitch; info.Yaw = Rot.Yaw; info.Roll = Rot.Roll;

        CachedActors.push_back(std::move(info));
        return true;
    }

    static bool GetActorPropsJson_Impl(uint32_t index, AActor* Actor, char* buf, size_t bufSize) {
        auto Loc = Actor->K2_GetActorLocation();
        auto Rot = Actor->K2_GetActorRotation();

        auto NameStr = ((UObject*)Actor)->Name.ToSDKString();
        std::string name = NameStr.c_str();

        std::string className;
        if (((UObject*)Actor)->Class) {
            auto ClassStr = ((UObject*)Actor)->Class->Name.ToSDKString();
            className = ClassStr.c_str();
        }

        snprintf(buf, bufSize,
            R"({"i":%u,"n":"%s","c":"%s","x":%.2f,"y":%.2f,"z":%.2f,"p":%.4f,"w":%.4f,"r":%.4f})",
            index, name.c_str(), className.c_str(),
            Loc.X, Loc.Y, Loc.Z, Rot.Pitch, Rot.Yaw, Rot.Roll);
        return true;
    }

    // Island scenery (trees, rocks, cliffs, map buildings) is placed with Static mobility, and UE4 silently ignores
    // moving or rotating a Static root component. Switch the root to Movable first (EComponentMobility::Movable = 2).
    static int MovableResult = -1; // for the move log: 0 SetMobility, 4 Mobility property, 1 no RootComponent property, 2 no root, 3 neither
    static void MakeMovable_Impl(AActor* Actor) {
        static int32 RootOff = -2;
        if (RootOff == -2) RootOff = int32(((UObject*)Actor)->GetOffset("RootComponent"));
        if (RootOff < 0) { MovableResult = 1; return; }
        auto Root = *(UObject**)(__int64(Actor) + RootOff);
        if (!Root) { MovableResult = 2; return; }
        if (auto Fn = Root->GetFunction("SetMobility")) {
            alignas(16) uint8_t Buf[32]{};
            Buf[0] = 2;
            Root->ProcessEvent(Fn, Buf);
            MovableResult = 0;
            return;
        }
        // Early builds (1.8.2) don't expose SetMobility to scripts: set the Mobility property itself.
        static int32 MobOff = -2;
        if (MobOff == -2) MobOff = int32(Root->GetOffset("Mobility"));
        if (MobOff < 0) { MovableResult = 3; return; }
        *(uint8_t*)(__int64(Root) + MobOff) = 2;
        MovableResult = 4; // 4 = set through the property
    }

    static bool SetActorPosition_Impl(AActor* Actor, float x, float y, float z) {
        MakeMovable_Impl(Actor);
        auto Fn = ((UObject*)Actor)->GetFunction("K2_SetActorLocation");
        if (!Fn) return false;
        auto P = Fn->GetParamsNamed();
        if (P.Size > 512) return false;
        alignas(16) uint8_t Buf[512]{};
        for (auto& Pm : P.NameOffsetMap) {
            if (Pm.Name == "NewLocation") { float v[3] = { x, y, z }; memcpy(Buf + Pm.Offset, v, 12); }
            else if (Pm.Name == "bTeleport") { Buf[Pm.Offset] = 1; }
        }
        ((UObject*)Actor)->ProcessEvent(Fn, Buf);
        return true;
    }

    static bool SetActorRotation_Impl(AActor* Actor, float pitch, float yaw, float roll) {
        MakeMovable_Impl(Actor);
        auto Fn = ((UObject*)Actor)->GetFunction("K2_SetActorRotation");
        if (!Fn) return false;
        auto P = Fn->GetParamsNamed();
        if (P.Size > 256) return false;
        alignas(16) uint8_t Buf[256]{};
        for (auto& Pm : P.NameOffsetMap) {
            if (Pm.Name == "NewRotation") { float v[3] = { pitch, yaw, roll }; memcpy(Buf + Pm.Offset, v, 12); }
            else if (Pm.Name == "bTeleportPhysics") { Buf[Pm.Offset] = 1; }
        }
        ((UObject*)Actor)->ProcessEvent(Fn, Buf);
        return true;
    }

    static uint32_t SpawnActorAtLocation_Impl(const UClass* Class, float x, float y, float z) {
        FVector Loc = { x, y, z };
        FRotator Rot; memset(&Rot, 0, sizeof(Rot));
        auto Actor = UWorld::SpawnActor<AActor>(Class, Loc, Rot);
        if (!Actor) return 0xFFFFFFFF;

        ActorInfo info;
        info.Index = SlotFor(Actor);
        auto NameStr = ((UObject*)Actor)->Name.ToSDKString();
        info.Name = NameStr.c_str();
        if (((UObject*)Actor)->Class) {
            auto ClassStr = ((UObject*)Actor)->Class->Name.ToSDKString();
            info.ClassName = ClassStr.c_str();
        }
        info.X = x; info.Y = y; info.Z = z;
        info.Pitch = info.Yaw = info.Roll = 0;
        uint32_t Slot = info.Index;
        CachedActors.push_back(std::move(info));
        return Slot;
    }

    // ---- Look of a piece (House Plots copy it so a placed copy is the same house, not the class default) ----
    // Island houses are built from the game's building pieces with a mesh and texture sets picked per piece
    // (ABuildingSMActor::StaticMesh / TextureData[4] / AltMeshIdx / bMirrored), and some are scaled. A piece spawned
    // from its class alone gets the class's default mesh and textures, so they are read here as asset paths.
    struct LookFns {
        bool Tried = false;
        const UClass* Base = nullptr;
        int32 Mesh = -1, Tex = -1, Alt = -1, Mirror = -1; uint8_t MirrorMask = 0;
        UFunction* Scale = nullptr; int32 ScaleRet = -1; uint32 ScaleSize = 0;
    };
    static LookFns Lk;

    static void InitLook_Impl(AActor* Any) {
        Lk.Base = FindClass("BuildingSMActor");
        if (Lk.Base) {
            auto Cdo = (UObject*)Lk.Base->GetDefaultObj();
            if (Cdo) {
                Lk.Mesh = int32(Cdo->GetOffset("StaticMesh"));
                Lk.Tex = int32(Cdo->GetOffset("TextureData"));
                Lk.Alt = int32(Cdo->GetOffset("AltMeshIdx"));
                if (auto Prop = Cdo->GetProperty("bMirrored", 0x20000)) {
                    Lk.Mirror = int32(GetFromOffset<uint32>(Prop, Offsets::Offset_Internal));
                    Lk.MirrorMask = Prop->GetFieldMask();
                }
            }
        }
        Lk.Scale = ((UObject*)Any)->GetFunction("GetActorScale3D");
        if (Lk.Scale) {
            auto P = Lk.Scale->GetParamsNamed(); Lk.ScaleSize = uint32(P.Size);
            for (auto& M : P.NameOffsetMap) if (M.Name == "ReturnValue") Lk.ScaleRet = int32(M.Offset);
            if (Lk.ScaleRet < 0 || Lk.ScaleSize > 64) Lk.Scale = nullptr;
        }
        char Line[200];
        snprintf(Line, sizeof(Line), "piece look: base %d, mesh %d, textures %d, alt %d, mirror %d/%u, scale %d",
            Lk.Base ? 1 : 0, Lk.Mesh, Lk.Tex, Lk.Alt, Lk.Mirror, unsigned(Lk.MirrorMask), Lk.Scale ? 1 : 0);
        StudioLog(Line);
    }

    // Asset path the way the engine writes it: /Game/Folder/Package.Asset (a sub-object gets ":")
    static std::string PathOf_Impl(UObject* O) {
        if (!O) return "";
        UObject* Chain[16]; int N = 0;
        for (UObject* P = O; P && N < 16; P = P->Outer) Chain[N++] = P;
        std::string Out;
        for (int i = N - 1; i >= 0; i--) {
            if (i != N - 1) Out += (N - 1 - i == 2) ? ':' : '.';
            auto S = Chain[i]->Name.ToSDKString();
            Out += S.c_str();
        }
        return Out;
    }

    static void AppendQuoted(std::string& out, const std::string& s) {
        out += '"';
        for (unsigned char c : s) {
            if (c == '"' || c == '\\') { out += '\\'; out += (char)c; }
            else if (c >= 0x20) out += (char)c;
        }
        out += '"';
    }

    static bool GetLook_Impl(uint32_t index, AActor* Actor, std::string* Out) {
        if (!Lk.Tried) { Lk.Tried = true; InitLook_Impl(Actor); }
        float S[3] = { 1.f, 1.f, 1.f };
        if (Lk.Scale) {
            alignas(16) uint8_t Buf[64]{};
            ((UObject*)Actor)->ProcessEvent(Lk.Scale, Buf);
            memcpy(S, Buf + Lk.ScaleRet, 12);
        }
        char b[160];
        snprintf(b, sizeof(b), "{\"i\":%u,\"sx\":%.4f,\"sy\":%.4f,\"sz\":%.4f", index, S[0], S[1], S[2]);
        std::string J = b;
        if (Lk.Base && ((UObject*)Actor)->IsA(Lk.Base)) {
            auto Raw = (uint8_t*)Actor;
            if (Lk.Mesh >= 0) { J += ",\"m\":"; AppendQuoted(J, PathOf_Impl(*(UObject**)(Raw + Lk.Mesh))); }
            if (Lk.Tex >= 0) {
                J += ",\"t\":[";
                for (int k = 0; k < 4; k++) { if (k) J += ","; AppendQuoted(J, PathOf_Impl(((UObject**)(Raw + Lk.Tex))[k])); }
                J += "]";
            }
            if (Lk.Alt >= 0) { snprintf(b, sizeof(b), ",\"a\":%d", *(int32_t*)(Raw + Lk.Alt)); J += b; }
            if (Lk.Mirror >= 0 && Lk.MirrorMask) { J += ",\"mi\":"; J += (Raw[Lk.Mirror] & Lk.MirrorMask) ? "1" : "0"; }
        }
        J += "}";
        *Out = std::move(J);
        return true;
    }

    // ---- _Safe wrappers: ONLY POD locals, ONLY POD return types, contain __try ----

    static bool GetLook_Safe(uint32_t index, AActor* Actor, std::string* Out) {
        __try {
            return GetLook_Impl(index, Actor, Out);
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            return false;
        }
    }

    static bool DestroyActor_Safe(AActor* Actor) {
        __try {
            Actor->K2_DestroyActor();
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            return false;
        }
    }

    static bool ReadActorLocation_Safe(AActor* Actor, float* Out3) {
        __try {
            auto Loc = Actor->K2_GetActorLocation();
            Out3[0] = Loc.X; Out3[1] = Loc.Y; Out3[2] = Loc.Z;
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            return false;
        }
    }

    static bool ReadActorInfo_Safe(AActor* Actor, uint32_t Slot, const float* Loc3) {
        __try {
            return ReadActorInfo_Impl(Actor, Slot, Loc3);
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            return false;
        }
    }

    static bool GetActorPropsJson_Safe(uint32_t index, AActor* Actor, char* buf, size_t bufSize) {
        __try {
            return GetActorPropsJson_Impl(index, Actor, buf, bufSize);
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            buf[0] = '\0';
            return false;
        }
    }

    static bool SetActorPosition_Safe(AActor* Actor, float x, float y, float z) {
        __try {
            return SetActorPosition_Impl(Actor, x, y, z);
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            return false;
        }
    }

    static bool SetActorRotation_Safe(AActor* Actor, float pitch, float yaw, float roll) {
        __try {
            return SetActorRotation_Impl(Actor, pitch, yaw, roll);
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            return false;
        }
    }

    static uint32_t SpawnActorAtLocation_Safe(const UClass* Class, float x, float y, float z) {
        __try {
            return SpawnActorAtLocation_Impl(Class, x, y, z);
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            return 0xFFFFFFFF;
        }
    }

    // ---- Public API: may use C++ types in returns/locals, NO __try, call _Safe() ----

    uint32_t RefreshActors(bool haveCenter, float cx, float cy, float cz, float radius, uint32_t maxCount) {
        CachedActors.clear();
        auto World = UWorld::GetWorld();
        if (World != SlotWorld) { RawActors.clear(); SlotOf.clear(); SlotWorld = World; }
        if (!World) return 0;

        static const UClass* ActorClass = nullptr;
        if (!ActorClass) ActorClass = FindClass("Actor");
        if (!ActorClass) return 0;

        std::vector<AActor*> found;
        GetAllActorsOfClass(World, ActorClass, found);

        // Pass 1: slot + location for every actor (one cheap call each). Forget actors that are gone.
        struct NearActor { float D2; uint32_t Slot; float L[3]; };
        std::vector<NearActor> nearby;
        nearby.reserve(found.size());
        std::vector<uint8_t> seen(RawActors.size(), 0);
        const float r2 = radius > 0.f ? radius * radius : -1.f;
        for (auto Actor : found) {
            if (!Actor) continue;
            uint32_t Slot = SlotFor(Actor);
            if (Slot >= seen.size()) seen.resize(Slot + 1, 0);
            seen[Slot] = 1;
            NearActor n{}; n.Slot = Slot;
            if (!ReadActorLocation_Safe(Actor, n.L)) continue;
            float dx = n.L[0] - cx, dy = n.L[1] - cy, dz = n.L[2] - cz;
            n.D2 = haveCenter ? dx * dx + dy * dy + dz * dz : 0.f;
            if (haveCenter && r2 > 0.f && n.D2 > r2) continue;
            nearby.push_back(n);
        }
        for (uint32_t i = 0; i < (uint32_t)RawActors.size(); i++)
            if (RawActors[i] && (i >= seen.size() || !seen[i])) { SlotOf.erase(RawActors[i]); RawActors[i] = nullptr; }

        // Pass 2: names/classes/rotation only for the closest maxCount actors.
        if (maxCount == 0) maxCount = 800;
        if (nearby.size() > maxCount) {
            std::nth_element(nearby.begin(), nearby.begin() + maxCount, nearby.end(), [](const NearActor& a, const NearActor& b) { return a.D2 < b.D2; });
            nearby.resize(maxCount);
        }
        std::sort(nearby.begin(), nearby.end(), [](const NearActor& a, const NearActor& b) { return a.D2 < b.D2; });
        CachedActors.reserve(nearby.size());
        for (auto& n : nearby) ReadActorInfo_Safe(RawActors[n.Slot], n.Slot, n.L);
        return (uint32_t)CachedActors.size();
    }

    const std::vector<ActorInfo>& GetActors() {
        return CachedActors;
    }

    std::string GetActorPropsJson(uint32_t index) {
        if (index >= RawActors.size()) return "";
        auto Actor = RawActors[index];
        if (!Actor) return "";

        char buf[1024] = {};
        if (!GetActorPropsJson_Safe(index, Actor, buf, sizeof(buf))) return "";
        return std::string(buf);
    }

    bool SetActorPosition(uint32_t index, float x, float y, float z) {
        if (index >= RawActors.size()) { StudioLog("move: index out of range"); return false; }
        auto Actor = RawActors[index];
        if (!Actor) { StudioLog("move: actor is gone"); return false; }
        float Before[3]{}, After[3]{};
        ReadActorLocation_Safe(Actor, Before);
        MovableResult = -1;
        bool Ok = SetActorPosition_Safe(Actor, x, y, z);
        ReadActorLocation_Safe(Actor, After);
        // Verify instead of trusting the call: the game silently refuses some moves
        float dx = After[0] - x, dy = After[1] - y, dz = After[2] - z;
        bool Moved = Ok && dx * dx + dy * dy + dz * dz < 4.f;
        // Log failures always, successes at most once a second (a gizmo drag moves the piece every frame)
        static ULONGLONG NextOkLog = 0;
        if (!Moved || GetTickCount64() >= NextOkLog) {
            if (Moved) NextOkLog = GetTickCount64() + 1000;
            char Line[256];
            snprintf(Line, sizeof(Line), "move %u: call=%d movable=%d before=(%.0f,%.0f,%.0f) asked=(%.0f,%.0f,%.0f) after=(%.0f,%.0f,%.0f) -> %s",
                index, Ok ? 1 : 0, MovableResult, Before[0], Before[1], Before[2], x, y, z, After[0], After[1], After[2], Moved ? "moved" : "NOT MOVED");
            StudioLog(Line);
        }
        return Moved;
    }

    bool SetActorRotation(uint32_t index, float pitch, float yaw, float roll) {
        if (index >= RawActors.size()) return false;
        auto Actor = RawActors[index];
        if (!Actor) return false;
        return SetActorRotation_Safe(Actor, pitch, yaw, roll);
    }

    bool ProjectWorldToScreen(float wx, float wy, float wz, float& sx, float& sy) {
        auto PC = GetPC();
        if (!PC || !InitProject(PC)) return false;
        alignas(16) uint8_t Buf[64]{};
        float W[3] = { wx, wy, wz };
        memcpy(Buf + PjWorld, W, 12);
        PC->ProcessEvent(ProjectFn, Buf);
        const float* S = (const float*)(Buf + PjScreen);
        sx = S[0]; sy = S[1];
        return Buf[PjReturn] != 0;
    }

    void BatchProjectWorldToScreen(const float* worldPoints, uint32_t count, ScreenPoint* out) {
        auto PC = GetPC();
        if (!PC || !InitProject(PC)) {
            for (uint32_t i = 0; i < count; i++) { out[i] = { 0, 0, 0 }; }
            return;
        }
        for (uint32_t i = 0; i < count; i++) {
            alignas(16) uint8_t Buf[64]{};
            memcpy(Buf + PjWorld, worldPoints + i * 3, 12);
            PC->ProcessEvent(ProjectFn, Buf);
            const float* S = (const float*)(Buf + PjScreen);
            out[i].X = S[0];
            out[i].Y = S[1];
            out[i].OnScreen = Buf[PjReturn] != 0 ? 1 : 0;
        }
    }

    void GetViewportSize(uint32_t& width, uint32_t& height) {
        auto PC = GetPC();
        if (!PC || !InitProject(PC)) { width = height = 0; return; }
        alignas(16) uint8_t Vp[32]{};
        PC->ProcessEvent(ViewportFn, Vp);
        width = *(uint32_t*)(Vp + VpX);
        height = *(uint32_t*)(Vp + VpY);
    }

    // ---- Line trace under a screen point (what you click on in free cam) ----
    // DeprojectScreenPositionToWorld (the view's ray) -> KismetSystemLibrary LineTraceSingle on the Visibility channel ->
    // GameplayStatics BreakHitResult for the hit actor. All resolved by name, so it works on every Studio season.

    struct TraceFns {
        bool Tried = false;
        UFunction* Deproject = nullptr; int32 DpX = -1, DpY = -1, DpLoc = -1, DpDir = -1; uint32 DpSize = 0;
        UObject* Kismet = nullptr; UFunction* Trace = nullptr;
        int32 TrCtx = -1, TrStart = -1, TrEnd = -1, TrChannel = -1, TrComplex = -1, TrHit = -1, TrSelf = -1, TrRet = -1;
        uint32 TrSize = 0, TrHitSize = 0;
        UObject* Statics = nullptr; UFunction* Break = nullptr;
        int32 BrHit = -1, BrActor = -1, BrLocation = -1; uint32 BrSize = 0, BrHitSize = 0;
    };
    static TraceFns Tr;

    static bool InitTrace(UObject* PC) {
        if (Tr.Tried) return Tr.Deproject && Tr.Trace && Tr.Break;
        Tr.Tried = true;
        Tr.Deproject = PC->GetFunction("DeprojectScreenPositionToWorld");
        if (Tr.Deproject) {
            auto P = Tr.Deproject->GetParamsNamed(); Tr.DpSize = uint32(P.Size);
            for (auto& M : P.NameOffsetMap) {
                if (M.Name == "ScreenX") Tr.DpX = int32(M.Offset);
                else if (M.Name == "ScreenY") Tr.DpY = int32(M.Offset);
                else if (M.Name == "WorldLocation") Tr.DpLoc = int32(M.Offset);
                else if (M.Name == "WorldDirection") Tr.DpDir = int32(M.Offset);
            }
        }
        if (auto C = FindClass("KismetSystemLibrary")) {
            Tr.Kismet = (UObject*)C->GetDefaultObj();
            Tr.Trace = Tr.Kismet ? Tr.Kismet->GetFunction("LineTraceSingle") : nullptr;
            if (!Tr.Trace && Tr.Kismet) Tr.Trace = Tr.Kismet->GetFunction("LineTraceSingle_NEW");
        }
        if (Tr.Trace) {
            auto P = Tr.Trace->GetParamsNamed(); Tr.TrSize = uint32(P.Size);
            for (auto& M : P.NameOffsetMap) {
                if (M.Name == "WorldContextObject") Tr.TrCtx = int32(M.Offset);
                else if (M.Name == "Start") Tr.TrStart = int32(M.Offset);
                else if (M.Name == "End") Tr.TrEnd = int32(M.Offset);
                else if (M.Name == "TraceChannel") Tr.TrChannel = int32(M.Offset);
                else if (M.Name == "bTraceComplex") Tr.TrComplex = int32(M.Offset);
                else if (M.Name == "OutHit") { Tr.TrHit = int32(M.Offset); Tr.TrHitSize = M.ElementSize; }
                else if (M.Name == "bIgnoreSelf") Tr.TrSelf = int32(M.Offset);
                else if (M.Name == "ReturnValue") Tr.TrRet = int32(M.Offset);
            }
        }
        Tr.Statics = (UObject*)UGameplayStatics::GetDefaultObj();
        Tr.Break = Tr.Statics ? Tr.Statics->GetFunction("BreakHitResult") : nullptr;
        if (Tr.Break) {
            auto P = Tr.Break->GetParamsNamed(); Tr.BrSize = uint32(P.Size);
            for (auto& M : P.NameOffsetMap) {
                if (M.Name == "Hit") { Tr.BrHit = int32(M.Offset); Tr.BrHitSize = M.ElementSize; }
                else if (M.Name == "HitActor") Tr.BrActor = int32(M.Offset);
                else if (M.Name == "Location") Tr.BrLocation = int32(M.Offset);
            }
        }
        bool Ok = Tr.Deproject && Tr.DpX >= 0 && Tr.DpY >= 0 && Tr.DpLoc >= 0 && Tr.DpDir >= 0 && Tr.DpSize <= 64
            && Tr.Trace && Tr.TrCtx >= 0 && Tr.TrStart >= 0 && Tr.TrEnd >= 0 && Tr.TrHit >= 0 && Tr.TrRet >= 0 && Tr.TrSize <= 1024
            && Tr.Break && Tr.BrHit >= 0 && Tr.BrActor >= 0 && Tr.BrSize <= 1024 && Tr.TrHitSize > 0 && Tr.TrHitSize == Tr.BrHitSize;
        char Line[200];
        snprintf(Line, sizeof(Line), "click trace: %s (deproject %d, trace %d size %u, break %d, hit result %u/%u bytes)", Ok ? "ready" : "unavailable, using nearest actor",
            Tr.Deproject ? 1 : 0, Tr.Trace ? 1 : 0, Tr.TrSize, Tr.Break ? 1 : 0, Tr.TrHitSize, Tr.BrHitSize);
        StudioLog(Line);
        if (!Ok) { Tr.Deproject = nullptr; Tr.Trace = nullptr; Tr.Break = nullptr; }
        return Ok;
    }

    static AActor* TraceScreen_Impl(UObject* PC, float ScreenX, float ScreenY, float* HitLoc) {
        alignas(16) uint8_t Dp[64]{};
        *(float*)(Dp + Tr.DpX) = ScreenX;
        *(float*)(Dp + Tr.DpY) = ScreenY;
        PC->ProcessEvent(Tr.Deproject, Dp);
        float Loc[3], Dir[3];
        memcpy(Loc, Dp + Tr.DpLoc, 12); memcpy(Dir, Dp + Tr.DpDir, 12);
        if (Dir[0] == 0.f && Dir[1] == 0.f && Dir[2] == 0.f) return nullptr;

        alignas(16) uint8_t T[1024]{};
        *(UObject**)(T + Tr.TrCtx) = PC;
        const float Reach = 200000.f; // 2 km
        float End[3] = { Loc[0] + Dir[0] * Reach, Loc[1] + Dir[1] * Reach, Loc[2] + Dir[2] * Reach };
        memcpy(T + Tr.TrStart, Loc, 12);
        memcpy(T + Tr.TrEnd, End, 12);
        if (Tr.TrChannel >= 0) T[Tr.TrChannel] = 0;   // TraceTypeQuery1 = Visibility
        if (Tr.TrComplex >= 0) T[Tr.TrComplex] = 1;   // the real shape, not a rough collision box
        if (Tr.TrSelf >= 0) T[Tr.TrSelf] = 1;
        Tr.Kismet->ProcessEvent(Tr.Trace, T);
        if (!T[Tr.TrRet]) return nullptr;

        alignas(16) uint8_t B[1024]{};
        memcpy(B + Tr.BrHit, T + Tr.TrHit, Tr.TrHitSize);
        Tr.Statics->ProcessEvent(Tr.Break, B);
        if (HitLoc && Tr.BrLocation >= 0) { memcpy(HitLoc, B + Tr.BrLocation, 12); HitLoc[3] = 1.f; }
        return *(AActor**)(B + Tr.BrActor);
    }

    static AActor* TraceScreen_Safe(UObject* PC, float ScreenX, float ScreenY, float* HitLoc) {
        __try { return TraceScreen_Impl(PC, ScreenX, ScreenY, HitLoc); }
        __except (EXCEPTION_EXECUTE_HANDLER) { return nullptr; }
    }

    // Things a click should never pick: the ground, the sky, your own character and the free camera.
    static bool Pickable_Impl(UObject* PC, AActor* Actor) {
        if (!Actor || !((UObject*)Actor)->Class) return false;
        static int32 PawnOff = -2;
        if (PawnOff == -2) PawnOff = int32(PC->GetOffset("Pawn"));
        if (PawnOff >= 0 && *(AActor**)(__int64(PC) + PawnOff) == Actor) return false;
        auto ClassName = ((UObject*)Actor)->Class->Name.ToSDKString();
        const char* Skip[] = { "Landscape", "CameraActor", "SkyLight", "SkySphere", "AtmosphericFog", "WorldSettings", "Volume" };
        for (auto S : Skip) if (strstr(ClassName.c_str(), S)) return false;
        return true;
    }

    static bool Pickable_Safe(UObject* PC, AActor* Actor) {
        __try { return Pickable_Impl(PC, Actor); }
        __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    }

    uint32_t RaycastScreen(float screenX, float screenY) {
        auto PC = GetPC();
        if (!PC) return 0xFFFFFFFF;

        // What is actually under the cursor (walls, floors, props), first
        if (InitTrace(PC)) {
            AActor* Hit = TraceScreen_Safe(PC, screenX, screenY, nullptr);
            if (Hit && Pickable_Safe(PC, Hit)) {
                if (UWorld::GetWorld() != SlotWorld) { RawActors.clear(); SlotOf.clear(); SlotWorld = UWorld::GetWorld(); }
                return SlotFor(Hit);
            }
        }

        // Otherwise the listed actor whose origin is closest to the cursor
        if (CachedActors.empty() || !InitProject(PC)) return 0xFFFFFFFF;
        float bestDist = 28.f * 28.f;
        uint32_t bestIdx = 0xFFFFFFFF;

        for (size_t i = 0; i < CachedActors.size(); i++) {
            float sx = 0, sy = 0;
            if (!ProjectWorldToScreen(CachedActors[i].X, CachedActors[i].Y, CachedActors[i].Z, sx, sy)) continue;
            float dx = sx - screenX, dy = sy - screenY;
            float dist = dx * dx + dy * dy;
            if (dist < bestDist) { bestDist = dist; bestIdx = CachedActors[i].Index; }
        }
        return bestIdx;
    }

    bool TraceScreenPoint(float screenX, float screenY, float* outXYZ, uint32_t& outIndex) {
        outIndex = 0xFFFFFFFF;
        auto PC = GetPC();
        if (!PC || !InitTrace(PC)) return false;
        float Hit[4] = { 0, 0, 0, 0 };
        AActor* Actor = TraceScreen_Safe(PC, screenX, screenY, Hit);
        if (Hit[3] == 0.f) return false;
        outXYZ[0] = Hit[0]; outXYZ[1] = Hit[1]; outXYZ[2] = Hit[2];
        if (Actor && Pickable_Safe(PC, Actor)) {
            if (UWorld::GetWorld() != SlotWorld) { RawActors.clear(); SlotOf.clear(); SlotWorld = UWorld::GetWorld(); }
            outIndex = SlotFor(Actor);
        }
        return true;
    }

    uint32_t SpawnActorAtLocation(const char* classPath, float x, float y, float z) {
        auto World = UWorld::GetWorld();
        if (!World) return 0xFFFFFFFF;

        auto Class = FindClass(classPath);
        if (!Class) return 0xFFFFFFFF;

        return SpawnActorAtLocation_Safe(Class, x, y, z);
    }

    std::string GetLooksJson(const uint32_t* indices, uint32_t count) {
        std::string json = "[";
        bool first = true;
        for (uint32_t k = 0; k < count; k++) {
            uint32_t index = indices[k];
            if (index >= RawActors.size() || !RawActors[index]) continue;
            std::string one;
            if (!GetLook_Safe(index, RawActors[index], &one) || one.empty()) continue;
            if (!first) json += ",";
            json += one; first = false;
            if (json.size() > 900 * 1024) break;
        }
        json += "]";
        return json;
    }

    bool DestroyActor(uint32_t index) {
        if (index >= RawActors.size()) return false;
        auto Actor = RawActors[index];
        if (!Actor) return false;

        if (!DestroyActor_Safe(Actor)) return false;
        RawActors[index] = nullptr;
        SlotOf.erase(Actor);
        return true;
    }
}
