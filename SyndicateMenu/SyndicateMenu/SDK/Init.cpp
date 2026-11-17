#include "Init.h"
#include "Objects.h"
#include "Offsets.h"
#include "../Core/Memory.h"
#include "../Utils/log.h"
#include "../Utils/memory.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

Offsets off{};
uintptr_t Class_Engine = 0;
uintptr_t Class_FortPawn = 0;
uintptr_t Class_PlayerState = 0;
uintptr_t Fn_GetCamLoc = 0;
uintptr_t Fn_GetCamRot = 0;
uintptr_t Fn_GetFOV = 0;
uintptr_t Fn_GetBoneIndex = 0;
uintptr_t Fn_GetCompToWorld = 0;

bool SetupGObjects() {
    if (GObjects.IsValid()) return true;

    static const std::vector<std::pair<const std::string, const std::string>> Patterns = {
        {"Chunked", "48 8B 05 ? ? ? ? 48 8B 0C C8 48 8D 04 D1"},
        {"Fixed", "48 8B 05 ? ? ? ? 48 8D 14 C8 EB 02"}};

    for (const auto& [Type, Pattern] : Patterns) {
        uintptr_t Address = Memory::PatternScan<uint32_t>(Pattern.c_str(), 3, true);
        if (Memory::IsAddressInsideImage(Address)) {
            LOG(LOG_INFO, "Found GObjects (%s) offset: 0x%p", Type.c_str(),
                reinterpret_cast<void*>(Address - Memory::GetImageBase()));
            GObjects.Initialize(reinterpret_cast<void*>(Address), Type == "Chunked");
            objectArray = Address;
            gobjectsChunked = Type == "Chunked";
            return true;
        }
    }

    LOG(LOG_ERROR, "Failed to find GObjects!");
    return false;
}

bool SetupFMemory() {
    if (FMemoryReallocFn) return true;
    uintptr_t FMemoryRealloc = Memory::PatternScan(
        "48 89 5C 24 ? 48 89 74 24 ? 57 48 83 EC 20 48 8B F1 41 8B D8 48 8B 0D ? ? ? ? 48 8B FA 48 85 C9");
    if (FMemoryRealloc) {
        FMemoryReallocFn = reinterpret_cast<decltype(FMemoryReallocFn)>(FMemoryRealloc);
        LOG(LOG_INFO, "Found FMemoryRealloc offset: 0x%p",
            reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(FMemoryReallocFn) - Memory::GetImageBase()));
        return true;
    }

    LOG(LOG_ERROR, "Failed to find FMemoryRealloc!");
    return false;
}

// Calls one FName::ToString candidate on a known name and checks the text it gives back.
// Everything that touches the game's memory is inside __try, so a wrong candidate that
// only faults is skipped instead of closing the game.
static bool TryNameCandidate(void* fn, FName name, wchar_t* buf, int bufLen) {
    FString out{};
    __try {
        reinterpret_cast<void (*)(const FName*, FString*)>(fn)(&name, &out);
        if (!out.Data || out.Num <= 1 || out.Num > bufLen) return false;
        for (int i = 0; i < out.Num; i++) buf[i] = out.Data[i];
        buf[bufLen - 1] = 0;
    } __except (1) {
        return false;
    }
    FStringFree(out);
    return true;
}

static bool NameCandidateGives(void* fn, FName name, const wchar_t* expected) {
    wchar_t buf[64]{};
    if (!TryNameCandidate(fn, name, buf, 64)) return false;
    return wcscmp(buf, expected) == 0;
}

// Finds FName::ToString (or FName::AppendString, which does the same into an empty
// FString). Older seasons (UE 4.16, 1.7.2) don't have the strings the first pattern
// relies on, and a wrong function picked by a fallback pattern used to be called
// blindly and closed the game. Every candidate is now tested on the names of two known
// objects ("Class" and "Object") before it is trusted. If none passes, the menu keeps
// working without object names (properties are matched by FName instead).
bool SetupFNameToString() {
    if (FNameToStringFn) return true;

    std::vector<std::pair<const char*, uintptr_t>> candidates;
    auto add = [&](const char* how, uintptr_t fn) {
        if (!fn || !Memory::IsAddressInsideImage(fn)) return;
        for (auto& c : candidates)
            if (c.second == fn) return;
        candidates.push_back({ how, fn });
    };

    if (uintptr_t ref = Memory::FindStringRef(L"TSoftObjectPtr<%s%s>"))
        add("TSoftObjectPtr", Memory::PatternScanRange<int32_t>(ref, 0x50, "E8 ? ? ? ? 48", true, 1, true));

    // FName::AppendString next to "ForwardShadingQuality_" (the way Erbium finds it).
    static const std::pair<const char*, size_t> appendSigs[] = {
        { "48 8D ? ? 48 8D ? ? E8", 9 },
        { "48 8D ? ? ? 48 8D ? ? E8", 10 },
        { "48 8D ? ? 49 8B ? E8", 8 },
        { "48 8D ? ? ? 49 8B ? E8", 9 },
        { "48 8D ? ? 48 8B ? E8", 8 },
    };
    uintptr_t fsq = Memory::FindStringRef(L"ForwardShadingQuality_");
    if (!fsq) fsq = Memory::FindStringRef("ForwardShadingQuality_");
    if (fsq) {
        for (const auto& [sig, len] : appendSigs) {
            uintptr_t fn = Memory::PatternScanRange<int32_t>(fsq, 0x50, sig, false, len, true);
            if (fn) { add("ForwardShadingQuality", fn); break; }
        }
    }

    add("ToString pattern A", Memory::PatternScan<int32_t>(
        "48 8B C4 48 89 58 ? 48 89 70 ? 48 89 78 ? 55 41 54 41 55 41 56 41 57 48 8D A8 ? ? ? ? 48 81 EC ? ? ? ? 48 8B 05 ? ? ? ? 48 33 C4 48 89 85 ? ? ? ? 45 33 ED 48 8B FA 4C 89 2A"));
    add("ToString pattern B", Memory::PatternScan<int32_t>(
        "48 89 5C 24 ? 48 89 6C 24 ? 48 89 74 24 ? 57 48 81 EC ? ? ? ? 48 8B 05 ? ? ? ? 48 33 C4 48 89 84 24 ? ? ? ? 33 ED 48 8B FA 48 89 2A 48 89 6A ? 8B 19"));

    if (uintptr_t ref = Memory::FindStringRef(L"Error uncompressing data.  Format: %f, Filename: %s"))
        add("Error uncompressing", Memory::PatternScanRange<int32_t>(ref, 0x50, "E8 ? ? ? ? EB", true, 1, true));

    // Two objects found by FName alone (no ToString needed) give names to test against.
    const uintptr_t classObj = FindObjectByName("Class", "Class");
    const uintptr_t objectObj = FindObjectByName("Object", "Class");
    if (!classObj || !objectObj) {
        printf("[sdk] FName::ToString: test objects not found; object names stay off\n");
        return true;
    }
    const FName className = ObjName(classObj);
    const FName objectName = ObjName(objectObj);

    for (const auto& [how, fn] : candidates) {
        printf("[sdk] testing FName::ToString candidate (%s) at 0x%llX\n", how,
            (unsigned long long)(fn - Memory::GetImageBase()));
        if (NameCandidateGives((void*)fn, className, L"Class") &&
            NameCandidateGives((void*)fn, objectName, L"Object")) {
            FNameToStringFn = reinterpret_cast<decltype(FNameToStringFn)>(fn);
            LOG(LOG_INFO, "Found FNameToString offset: 0x%p",
                reinterpret_cast<void*>(fn - Memory::GetImageBase()));
            return true;
        }
    }

    printf("[sdk] no working FName::ToString on this season; object names stay off\n");
    return true;
}

bool SetupFNameCtor() {
    if (FNameCtorW) return true;

    uintptr_t StringRef = Memory::FindStringRef(L"CanvasObject");
    if (!StringRef) {
        LOG(LOG_ERROR, "Failed to find FNameConstructorW: \"CanvasObject\" string");
        return false;
    }

    uintptr_t FNameConstructorW = Memory::PatternScanRange<int32_t>(StringRef, 0x50, "E8 ? ? ? ? 48", false, 1, true);
    if (!FNameConstructorW) {
        FNameConstructorW = Memory::PatternScanRange<int32_t>(StringRef, 0x50, "E8", false, 1, true);
        if (!FNameConstructorW) {
            LOG(LOG_ERROR, "Failed to find FNameConstructorW: Call FNameConstructorW instruction");
            return false;
        }
    }

    FNameCtorW = reinterpret_cast<decltype(FNameCtorW)>(FNameConstructorW);
    LOG(LOG_INFO, "Found FNameConstructorW offset: 0x%p",
        reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(FNameCtorW) - Memory::GetImageBase()));
    return true;
}

bool SetupStructOffsets() {
    uintptr_t Class = FindObjectByName("Class", "Class");
    uintptr_t Struct = FindObjectByName("Struct", "Class");
    if (!Struct) Struct = FindObjectByName("struct", "Class");
    uintptr_t Field = FindObjectByName("Field", "Class");
    uintptr_t Object = FindObjectByName("Object", "Class");
    uintptr_t DefaultObject = FindObjectByName("Default__Object");
    uintptr_t DefaultField = FindObjectByName("Default__Field");
    uintptr_t PC = FindObjectByName("PlayerController", "Class");
    uintptr_t WasReleased = FindObjectWithOuter("WasInputKeyJustReleased", "PlayerController");
    uintptr_t Vector = FindObjectByName("Vector", "ScriptStruct");
    if (!Vector) Vector = FindObjectByName("Vector");
    uintptr_t VecX = FindObjectWithOuter("X", "Vector");
    uintptr_t VecY = FindObjectWithOuter("Y", "Vector");
    uintptr_t Color = FindObjectByName("Color", "ScriptStruct");
    if (!Color) Color = FindObjectByName("Color");
    uintptr_t Guid = FindObjectByName("Guid", "ScriptStruct");
    if (!Guid) Guid = FindObjectByName("Guid");

    SuperStructOff = FindMatchingValueOffset<void*>({
        { (void*)Struct, (void*)Field },
        { (void*)Class, (void*)Struct }
    });
    if (!SuperStructOff) SuperStructOff = 0x30;

    ChildrenOff = FindMatchingValueOffset<void*>({
        { (void*)PC, (void*)WasReleased }
    });
    if (!ChildrenOff && Vector && VecX)
        ChildrenOff = FindMatchingValueOffset<void*>({ { (void*)Vector, (void*)VecX } });
    if (!ChildrenOff) ChildrenOff = 0x38;

    ClassDefaultOff = FindMatchingValueOffset<void*>({
        { (void*)Object, (void*)DefaultObject },
        { (void*)Field, (void*)DefaultField }
    });

    PropertiesSizeOff = FindMatchingValueOffset<int32_t>({
        { (void*)Color, 4 },
        { (void*)Guid, 0x10 }
    });

    if (Vector && VecX && VecY) {
        uint32_t n = FindMatchingValueOffset<void*>({ { (void*)VecX, (void*)VecY } }, 0x10, 0x40, 8);
        if (n) FieldNextOff = n;
    }

    OffsetInternalOff = 0x44;
    ByteMaskOff = 0x72;
    printf("Super=0x%X Children=0x%X Next=0x%X\n", SuperStructOff, ChildrenOff, FieldNextOff);
    return SuperStructOff && ChildrenOff;
}

bool SetupProcessEvent() {
    uintptr_t pressed = FindObjectByName("WasInputKeyJustPressed");
    uintptr_t toggle = FindObjectByName("ToggleSpeaking");
    uintptr_t swlvl = FindObjectByName("SwitchLevel");
    FunctionFlagsOff = Memory::FindMatchingValueOffset<uint32_t>({
        { (void*)pressed, 0x54020401u },
        { (void*)toggle, 0x20600u },
        { (void*)swlvl, 0x20600u }
    });
    if (!FunctionFlagsOff) {
        FunctionFlagsOff = Memory::FindMatchingValueOffset<uint32_t>({
            { (void*)pressed, 0x54020403u },
            { (void*)toggle, 0x20602u },
            { (void*)swlvl, 0x20602u }
        });
    }
    if (!FunctionFlagsOff) FunctionFlagsOff = 0x88;
    printf("FunctionFlags = 0x%X\n", FunctionFlagsOff);

    if (pressed) {
        auto text = GetTextSection();
        uintptr_t textStart = text ? Memory::GetImageBase() + text->VirtualAddress : 0;
        uintptr_t textEnd = text ? textStart + text->Misc.VirtualSize : 0;
        for (uint32_t i = FunctionFlagsOff; i < FunctionFlagsOff + 0x30; i++) {
            uintptr_t p = Read<uintptr_t>(pressed + i);
            if (p > textStart && p < textEnd) { FuncPtrOff = i; break; }
        }
    }

    uintptr_t first = GetObjectByIndex(0);
    if (!first) return false;
    uintptr_t vt = Read<uintptr_t>(first);
    if (!vt) return false;

    auto resolveJmp = [](void* fn) -> uintptr_t {
        uint8_t* a = (uint8_t*)fn;
        if (a && *a == 0xE9) {
            uintptr_t r = (uintptr_t)(a + 5 + *(int32_t*)(a + 1));
            if (Memory::IsAddressInsideImage(r)) return r;
        }
        return (uintptr_t)fn;
    };

    int32_t flags = (int32_t)FunctionFlagsOff;
    for (int i = 0; i < 0x150; i++) {
        void* fn = Read<void*>(vt + 8ull * i);
        if (!fn || !Memory::IsAddressInsideImage((uintptr_t)fn)) continue;
        uintptr_t body = resolveJmp(fn);
        const bool hitA =
            Memory::PatternScanRangeBytes<int32_t>(body, 0x400,
                { 0xF7, -1, flags, 0, 0, 0, 0x04, 0, 0, 0 }, false, (size_t)-1, false, false) &&
            Memory::PatternScanRangeBytes<int32_t>(body, 0x400,
                { 0xF7, -1, flags, 0, 0, 0, 0, 0x40, 0, 0 }, false, (size_t)-1, false, false);
        const bool hitB =
            Memory::PatternScanRangeBytes<int32_t>(body, 0x400,
                { 0xF7, -1, flags, 0, 0, 0, 0, 0x04, 0, 0 }, false, (size_t)-1, false, false) &&
            Memory::PatternScanRangeBytes<int32_t>(body, 0x400,
                { 0xF7, -1, flags, 0, 0, 0, 0, 0, 0x40, 0 }, false, (size_t)-1, false, false);
        if (hitA || hitB) {
            ProcessEventIdx = i;
            printf("Found UObject::ProcessEvent VFT index: 0x%X\n", i);
            return true;
        }
    }

    ProcessEventIdx = -1;
    printf("Failed to find ProcessEvent VFT index (camera will use CameraCache)\n");
    return true;
}

static int PropOff(const char* cls, const char* name, int fallback = 0) {
    PropInfo p = FindClassProp(cls, name);
    if (p.found && p.offset > 0) {
        printf("  %s.%s = 0x%X\n", cls, name, p.offset);
        return p.offset;
    }
    if (fallback) printf("  %s.%s fallback 0x%X\n", cls, name, fallback);
    return fallback;
}

void SetupGameOffsets() {
    off.GameViewport = PropOff("Engine", "GameViewport");
    off.ViewportWorld = PropOff("GameViewportClient", "World");
    off.ViewportGameInstance = PropOff("GameViewportClient", "GameInstance");
    off.LocalPlayers = PropOff("GameInstance", "LocalPlayers");
    off.PlayerController = PropOff("Player", "PlayerController");
    off.AcknowledgedPawn = PropOff("PlayerController", "AcknowledgedPawn");
    off.ControllerPawn = PropOff("Controller", "Pawn");
    off.ControllerPlayerState = PropOff("Controller", "PlayerState");
    off.PlayerCameraManager = PropOff("PlayerController", "PlayerCameraManager");
    off.CameraCachePrivate = PropOff("PlayerCameraManager", "CameraCachePrivate");
    off.ControlRotation = PropOff("Controller", "ControlRotation");
    off.PersistentLevel = PropOff("World", "PersistentLevel");
    off.Levels = PropOff("World", "Levels");
    off.GameState = PropOff("World", "GameState");
    off.OwningGameInstance = PropOff("World", "OwningGameInstance");
    off.RootComponent = PropOff("Actor", "RootComponent");
    off.PlayerState = PropOff("Pawn", "PlayerState");
    off.Mesh = PropOff("Character", "Mesh");
    off.PlayerName = PropOff("PlayerState", "PlayerName");
    off.RelativeLocation = PropOff("SceneComponent", "RelativeLocation");

    PropInfo team = FindClassProp("FortPlayerStateAthena", "TeamIndex");
    if (team.found) off.TeamIndex = team.offset;
    PropInfo bot = FindClassProp("PlayerState", "bIsABot");
    if (bot.found && bot.offset > 0) {
        off.bIsABot = bot.offset;
        off.bIsABotMask = bot.mask;
        printf("  PlayerState.bIsABot = 0x%X mask 0x%X\n", bot.offset, bot.mask);
    } else {
        printf("  PlayerState.bIsABot not found: ESP, aim and chams stay off\n");
    }
    off.PlayerArray = PropOff("GameStateBase", "PlayerArray");
    if (!off.PlayerArray)
        off.PlayerArray = PropOff("GameState", "PlayerArray");
    PropInfo dying = FindClassProp("FortPawn", "bIsDying");
    if (dying.found) {
        off.bIsDying = dying.offset;
        off.bIsDyingMask = dying.mask;
        printf("  FortPawn.bIsDying = 0x%X mask 0x%X\n", dying.offset, dying.mask);
    }

    Class_Engine = FindObjectByName("FortEngine", "Class");
    if (!Class_Engine) Class_Engine = FindObjectByName("Engine", "Class");
    Class_FortPawn = FindObjectByName("FortPawn", "Class");
    if (!Class_FortPawn) Class_FortPawn = FindObjectByName("FortPlayerPawnAthena", "Class");
    Class_PlayerState = FindObjectByName("FortPlayerStateAthena", "Class");

    Fn_GetCamLoc = GetFunction("PlayerCameraManager", "GetCameraLocation");
    Fn_GetCamRot = GetFunction("PlayerCameraManager", "GetCameraRotation");
    Fn_GetFOV = GetFunction("PlayerCameraManager", "GetFOVAngle");
    Fn_GetBoneIndex = GetFunction("SkinnedMeshComponent", "GetBoneIndex");
    Fn_GetCompToWorld = GetFunction("SceneComponent", "K2_GetComponentToWorld");

    if (Fn_GetCompToWorld && FuncPtrOff) {
        uintptr_t exec = Read<uintptr_t>(Fn_GetCompToWorld + FuncPtrOff);
        if (exec) {
            uintptr_t inner = PatternScanRange(exec, 0x50, "E8", 1, true);
            if (!InImage(inner)) inner = exec;
            uintptr_t hit = PatternScanRange(inner, 0x30, "0F 10");
            if (hit) off.ComponentToWorld = Read<int32_t>(hit + 3);
        }
    }
    if (!off.ComponentToWorld) off.ComponentToWorld = PropOff("SceneComponent", "ComponentToWorld", 0x1C0);

    PropInfo vertex = FindClassProp("SkinnedMeshComponent", "VertexOffsetUsage");
    if (vertex.found) off.BoneArray = vertex.offset + 0x10;
    else {
        PropInfo master = FindClassProp("SkinnedMeshComponent", "MasterPoseComponent");
        if (master.found) {
            off.MasterPose = master.offset;
            off.BoneArray = master.offset + 0x8;
        }
    }
    if (!off.BoneArray) off.BoneArray = 0x5A0;
    off.MasterPose = off.MasterPose ? off.MasterPose : PropOff("SkinnedMeshComponent", "MasterPoseComponent");
    off.CachedBounds = PropOff("SkinnedMeshComponent", "CachedWorldSpaceBounds", 0x600);
    off.LastRenderTime = PropOff("PrimitiveComponent", "LastRenderTimeOnScreen");
    if (!off.LastRenderTime)
        off.LastRenderTime = PropOff("PrimitiveComponent", "LastRenderTime");
    off.TimeSeconds = PropOff("World", "TimeSeconds");
    off.CurrentWeapon = PropOff("FortPawn", "CurrentWeapon");
    if (!off.CurrentWeapon)
        off.CurrentWeapon = PropOff("Pawn", "CurrentWeapon");
    off.WeaponData = PropOff("FortWeapon", "WeaponData");
    if (!off.WeaponData)
        off.WeaponData = PropOff("FortWeaponRanged", "WeaponData");

    printf("ComponentToWorld=0x%X BoneArray=0x%X LastRender=0x%X TimeSeconds=0x%X\n",
        off.ComponentToWorld, off.BoneArray, off.LastRenderTime, off.TimeSeconds);
}

uintptr_t FindEngine() {
    static uintptr_t cached = 0;
    if (cached && ObjClass(cached)) return cached;
    int n = ObjectCount();
    for (int i = 0; i < n; i++) {
        uintptr_t o = GetObjectByIndex(i);
        if (!o || IsDefaultObject(o)) continue;
        std::string cn = ObjClassName(o);
        if (cn == "FortEngine" || cn == "GameEngine" || cn == "Engine") {
            cached = o;
            return o;
        }
        if (Class_Engine && IsA(o, Class_Engine)) {
            cached = o;
            return o;
        }
    }
    return 0;
}

uintptr_t FindWorld() {
    uintptr_t engine = FindEngine();
    if (!engine || !off.GameViewport) return 0;
    uintptr_t vp = Read<uintptr_t>(engine + off.GameViewport);
    if (!vp || !off.ViewportWorld) return 0;
    return Read<uintptr_t>(vp + off.ViewportWorld);
}

void DiscoverLevelActors(uintptr_t level) {
    if (off.LevelActors || !level) return;
    PropInfo owning = FindClassProp("Level", "OwningWorld");
    int end = owning.found && owning.offset > 0x70 ? owning.offset : 0x140;
    for (int i = 0x70; i < end; i += 8) {
        TArrayPtr arr = Read<TArrayPtr>(level + i);
        if (!arr.Data || arr.Num <= 0 || arr.Max <= 0) continue;
        if (arr.Max < arr.Num || arr.Max > 4000000) continue;

        int check = arr.Num < 48 ? arr.Num : 48;
        bool looks = false;
        for (int k = 0; k < check; k++) {
            uintptr_t actor = Read<uintptr_t>(arr.Data + (uintptr_t)k * 8);
            if (!actor || !ObjClass(actor)) continue;
            if (!off.RootComponent || Read<uintptr_t>(actor + off.RootComponent)) {
                looks = true;
                break;
            }
        }
        if (!looks) continue;

        off.LevelActors = i;
        printf("Level.Actors = 0x%X (num=%d)\n", i, arr.Num);
        return;
    }
}

char g_gameBuild[64] = {};

// Every season keeps its build in a wide string such as "++Fortnite+Release-10.40-CL-9380822".
// The menu finds every offset at runtime (by name, from the game's own reflection data), so
// this is for the log: it says which season the offsets below belong to.
void DetectGameBuild() {
    if (g_gameBuild[0])
        return;
    static const wchar_t needle[] = L"++Fortnite+Release-";
    const size_t needleBytes = sizeof(needle) - sizeof(wchar_t);
    for (const char* section : { ".rdata", ".data" }) {
        uintptr_t start = 0;
        uint64_t size = 0;
        GetSectionRange(section, start, size);
        if (!start || size < needleBytes)
            continue;
        __try {
            for (uint64_t i = 0; i + needleBytes + 128 < size; i += 2) {
                if (memcmp(reinterpret_cast<const void*>(start + i), needle, needleBytes) != 0)
                    continue;
                const wchar_t* text = reinterpret_cast<const wchar_t*>(start + i + 11 * sizeof(wchar_t));
                int n = 0;
                while (n < 63 && text[n] >= 32 && text[n] < 127) {
                    g_gameBuild[n] = (char)text[n];
                    ++n;
                }
                g_gameBuild[n] = 0;
                if (n > 8)
                    return;
                g_gameBuild[0] = 0;
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            g_gameBuild[0] = 0;
        }
    }
}

bool InitSDK() {
    static bool structsDone = false;
    static bool peDone = false;
    static bool offsetsDone = false;

    DetectGameBuild();
    printf("[sdk] game build: %s\n", g_gameBuild[0] ? g_gameBuild : "unknown");
    printf("[sdk] GObjects\n");
    if (!SetupGObjects()) return false;
    printf("[sdk] FMemory\n");
    if (!SetupFMemory()) return false;
    printf("[sdk] FName constructor\n");
    if (!SetupFNameCtor()) return false;
    printf("[sdk] FName::ToString\n");
    SetupFNameToString();
    printf("[sdk] struct offsets / ProcessEvent / game offsets\n");
    if (!structsDone) {
        if (!SetupStructOffsets()) {
            printf("struct offset discovery failed, using 4.23 fallbacks\n");
            SuperStructOff = SuperStructOff ? SuperStructOff : 0x30;
            ChildrenOff = ChildrenOff ? ChildrenOff : 0x38;
        }
        structsDone = true;
    }
    if (!peDone) {
        SetupProcessEvent();
        peDone = true;
    }
    if (!offsetsDone) {
        SetupGameOffsets();
        offsetsDone = true;
    }
    if (!off.GameViewport || !off.ViewportWorld) {
        printf("Engine/World properties missing\n");
        return false;
    }
    return true;
}
