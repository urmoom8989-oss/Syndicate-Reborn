#include "ESP.h"
#include "Aim.h"
#include "CustomOutfits.h"
#include "Game.h"
#include "../Core/Math.h"
#include "../Core/Memory.h"
#include "../Menu/Config.h"
#include "../Menu/Overlay.h"
#include "../SDK/Init.h"
#include "../SDK/Objects.h"
#include "../SDK/Offsets.h"

#include <imgui.h>
#include <MinHook.h>
#include <windows.h>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <string>
#include <vector>

extern bool HudHookMode; // defined below; true on Chapter 1 seasons (HUD vtable hook)

namespace {

ImFont* EspFont() {
    ImGuiIO& io = ImGui::GetIO();
    if (io.FontDefault && io.FontDefault->FontSize >= 13.f)
        return io.FontDefault;
    for (int i = 0; i < io.Fonts->Fonts.Size; i++) {
        ImFont* f = io.Fonts->Fonts[i];
        if (f && f->FontSize >= 13.f)
            return f;
    }
    return io.FontDefault;
}

void DrawTextEsp(ImDrawList* dl, ImVec2 pos, ImU32 col, const char* text) {
    ImFont* font = EspFont();
    if (!font || !text || !*text)
        return;
    dl->AddText(font, font->FontSize, pos, col, text);
}

FTransform MeshToWorld(uintptr_t mesh) {
    return mesh ? ReadComponentToWorld(mesh) : FTransform{};
}

bool CopyBone(const TArrayPtr& array, int bone, FTransform* out) {
    if (!array.Data || !out || bone < 0 || bone >= array.Num || array.Num > 512)
        return false;
    __try {
        *out = Read<FTransform>(array.Data + (uintptr_t)bone * 0x30);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool BoneWorld(const TArrayPtr& array, int bone, const FTransform& world, Vec3& out) {
    FTransform boneXf{};
    if (!CopyBone(array, bone, &boneXf))
        return false;
    out = MatrixOrigin(MulMatrix(boneXf.ToMatrixWithScale(), world.ToMatrixWithScale()));
    return true;
}

int GatherBoneBuffers(uintptr_t mesh, TArrayPtr out[2]) {
    if (!mesh || !off.BoneArray)
        return 0;
    int count = 0;
    for (int i = 0; i < 2 && count < 2; i++) {
        TArrayPtr arr = Read<TArrayPtr>(mesh + off.BoneArray + i * (int)sizeof(TArrayPtr));
        if (arr.Data && arr.Num > 1 && arr.Num <= 512)
            out[count++] = arr;
    }
    return count;
}

float Dist3(const Vec3& a, const Vec3& b) {
    Vec3 d = a - b;
    return sqrtf(d.dot(d));
}

bool NearSegment(const Vec3& point, const Vec3& start, const Vec3& end, float slack) {
    const float dx = end.x - start.x;
    const float dy = end.y - start.y;
    const float dz = end.z - start.z;
    const float len2 = dx * dx + dy * dy + dz * dz;
    if (len2 < 1.f)
        return Dist3(point, start) <= slack;
    float t = ((point.x - start.x) * dx + (point.y - start.y) * dy + (point.z - start.z) * dz) / len2;
    if (t < 0.f) t = 0.f;
    if (t > 1.f) t = 1.f;
    Vec3 closest{ start.x + dx * t, start.y + dy * t, start.z + dz * t };
    return Dist3(point, closest) <= slack;
}

bool PoseOk(const TArrayPtr& array, const FTransform& world, const BoneSet& bones) {
    Vec3 head, pelvis;
    if (!BoneWorld(array, bones.Head, world, head) || !BoneWorld(array, bones.Pelvis, world, pelvis))
        return false;
    const float rise = head.z - pelvis.z;
    const float span = Dist3(head, pelvis);
    if (rise < 35.f || rise > 160.f || span < 40.f || span > 190.f)
        return false;

    auto limbNear = [&](int bone, const Vec3& anchor, float maxDist, float minZ) {
        if (bone <= 0)
            return true;
        Vec3 point;
        if (!BoneWorld(array, bone, world, point))
            return false;
        return point.z >= minZ && Dist3(point, anchor) <= maxDist;
    };

    if (!limbNear(bones.LUpper, pelvis, 120.f, pelvis.z - 20.f) || !limbNear(bones.RUpper, pelvis, 120.f, pelvis.z - 20.f))
        return false;
    if (!limbNear(bones.LThigh, pelvis, 90.f, pelvis.z - 120.f) || !limbNear(bones.RThigh, pelvis, 90.f, pelvis.z - 120.f))
        return false;

    auto jointBetween = [&](int parent, int joint, int child) {
        if (parent <= 0 || joint <= 0 || child <= 0)
            return true;
        Vec3 a, b, c;
        if (!BoneWorld(array, parent, world, a) || !BoneWorld(array, joint, world, b) || !BoneWorld(array, child, world, c))
            return false;
        if (Dist3(a, b) > 100.f || Dist3(b, c) > 100.f)
            return false;
        return NearSegment(b, a, c, 60.f);
    };

    if (!jointBetween(bones.LUpper, bones.LLower, bones.LHand) || !jointBetween(bones.RUpper, bones.RLower, bones.RHand))
        return false;
    if (!jointBetween(bones.LThigh, bones.LCalf, bones.LFoot) || !jointBetween(bones.RThigh, bones.RCalf, bones.RFoot))
        return false;
    return true;
}

bool BonesReady(const BoneSet& bones) {
    return bones.Head > 0 && bones.Pelvis > 0 && bones.LUpper > 0 && bones.RUpper > 0 && bones.LThigh > 0 && bones.RThigh > 0;
}

void StrokeChain(ImDrawList* draw, const std::vector<ImVec2>& points, ImU32 color, float thickness, bool curved) {
    if (points.size() < 2)
        return;
    if (!curved || points.size() < 3) {
        draw->AddPolyline(points.data(), (int)points.size(), color, ImDrawFlags_None, thickness);
        return;
    }

    std::vector<ImVec2> smooth;
    smooth.reserve(points.size() * 8);
    for (size_t i = 0; i + 1 < points.size(); ++i) {
        const ImVec2 p0 = i == 0 ? points[i] : points[i - 1];
        const ImVec2 p1 = points[i];
        const ImVec2 p2 = points[i + 1];
        const ImVec2 p3 = (i + 2 < points.size()) ? points[i + 2] : points[i + 1];
        for (int step = 0; step < 8; ++step) {
            const float t = step / 8.f;
            const float t2 = t * t;
            const float t3 = t2 * t;
            ImVec2 q;
            q.x = 0.5f * ((2.f * p1.x) + (-p0.x + p2.x) * t + (2.f * p0.x - 5.f * p1.x + 4.f * p2.x - p3.x) * t2 +
                          (-p0.x + 3.f * p1.x - 3.f * p2.x + p3.x) * t3);
            q.y = 0.5f * ((2.f * p1.y) + (-p0.y + p2.y) * t + (2.f * p0.y - 5.f * p1.y + 4.f * p2.y - p3.y) * t2 +
                          (-p0.y + 3.f * p1.y - 3.f * p2.y + p3.y) * t3);
            smooth.push_back(q);
        }
    }
    smooth.push_back(points.back());
    draw->AddPolyline(smooth.data(), (int)smooth.size(), color, ImDrawFlags_None, thickness);
}

void StrokeSpaghetti(ImDrawList* draw, const ImVec2& start, const ImVec2& end, ImU32 color, float thickness) {
    const float dx = end.x - start.x;
    const float dy = end.y - start.y;
    const float len = sqrtf(dx * dx + dy * dy);
    if (len < 1.f)
        return;
    const float dirX = dx / len;
    const float dirY = dy / len;
    const float perpX = -dirY;
    const float perpY = dirX;
    ImVec2 points[13];
    for (int i = 0; i <= 12; ++i) {
        const float t = i / 12.f;
        const float offset = 5.f * sinf(t * 3.f * 6.2831853f);
        points[i] = ImVec2(start.x + dx * t + perpX * offset, start.y + dy * t + perpY * offset);
    }
    draw->AddPolyline(points, 13, color, ImDrawFlags_None, thickness);
}

void DrawChinaHat(const Vec3& head, ImU32 color, float thickness) {
    constexpr int segments = 20;
    constexpr float radius = 25.f;
    constexpr float height = 20.f;

    Vec3 tip = head;
    tip.z += height;
    Vec2 tip2{};
    if (!WorldToScreen(tip, tip2, W, H))
        return;

    ImVec2 base[segments];
    bool ok[segments] = {};
    for (int i = 0; i < segments; i++) {
        const float angle = (6.2831853f / segments) * i;
        Vec3 point = head;
        point.x += cosf(angle) * radius;
        point.y += sinf(angle) * radius;
        point.z += 5.f;
        Vec2 screen{};
        if (!WorldToScreen(point, screen, W, H))
            continue;
        base[i] = ImVec2(screen.x, screen.y);
        ok[i] = true;
    }

    ImDrawList* draw = ImGui::GetBackgroundDrawList();
    ImVec2 tipIm(tip2.x, tip2.y);
    for (int i = 0; i < segments; i++) {
        if (!ok[i])
            continue;
        draw->AddLine(tipIm, base[i], color, thickness);
        const int next = (i + 1) % segments;
        if (ok[next])
            draw->AddLine(base[i], base[next], color, thickness);
    }
}

void DrawCorners(ImDrawList* dl, ImVec2 a, ImVec2 b, ImU32 col, float thick) {
    float width = b.x - a.x;
    float size = width * 0.3f;
    dl->AddLine(a, ImVec2(a.x + size, a.y), col, thick);
    dl->AddLine(a, ImVec2(a.x, a.y + size), col, thick);
    dl->AddLine(ImVec2(b.x - size, a.y), ImVec2(b.x, a.y), col, thick);
    dl->AddLine(ImVec2(b.x, a.y), ImVec2(b.x, a.y + size), col, thick);
    dl->AddLine(ImVec2(a.x, b.y - size), ImVec2(a.x, b.y), col, thick);
    dl->AddLine(ImVec2(a.x, b.y), ImVec2(a.x + size, b.y), col, thick);
    dl->AddLine(ImVec2(b.x - size, b.y), b, col, thick);
    dl->AddLine(ImVec2(b.x, b.y - size), b, col, thick);
}

bool CopyItemText(uintptr_t item, int offset, wchar_t* out, int cap) {
    if (out && cap > 0)
        out[0] = 0;
    if (!item || offset < 0 || !out || cap < 2)
        return false;
    __try {
        uintptr_t data = Read<uintptr_t>(item + offset);
        if (data < 0x10000)
            return false;
        FString value = Read<FString>(data + 0x28);
        if (!value.Data || (uintptr_t)value.Data < 0x10000 || value.Num < 2 || value.Num > 96)
            return false;
        int count = value.Num - 1;
        if (count >= cap)
            count = cap - 1;
        for (int i = 0; i < count; ++i) {
            wchar_t ch = Read<wchar_t>((uintptr_t)value.Data + (uintptr_t)i * 2);
            if (ch == 0) {
                out[i] = 0;
                return i > 0;
            }
            out[i] = ch;
            out[i + 1] = 0;
        }
        return out[0] != 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        out[0] = 0;
        return false;
    }
}

std::string PrettyItem(std::string name) {
    if (name.rfind("WID_", 0) == 0)
        name.erase(0, 4);
    else if (name.rfind("Athena_", 0) == 0)
        name.erase(0, 7);
    for (;;) {
        size_t at = name.find("Athena");
        if (at == std::string::npos)
            break;
        name.erase(at, 6);
    }
    for (char& c : name) {
        if (c == '_')
            c = ' ';
    }
    std::string clean;
    bool space = false;
    for (char c : name) {
        if (c == ' ') {
            space = !clean.empty();
            continue;
        }
        if (space) {
            clean.push_back(' ');
            space = false;
        }
        clean.push_back(c);
    }
    return clean;
}

ImU32 TierColor(int tier) {
    switch (tier) {
    case 2: return IM_COL32(90, 196, 74, 255);
    case 3: return IM_COL32(55, 148, 232, 255);
    case 4: return IM_COL32(168, 84, 242, 255);
    case 5: return IM_COL32(232, 176, 48, 255);
    case 6:
    case 7: return IM_COL32(232, 122, 42, 255);
    default: return IM_COL32(210, 210, 210, 255);
    }
}

bool BitOn(uintptr_t base, const PropInfo& prop) {
    if (!base || !prop.found || prop.offset < 0)
        return false;
    uint8_t byte = Read<uint8_t>(base + prop.offset);
    return prop.mask ? (byte & prop.mask) != 0 : byte != 0;
}

struct LootPass {
    bool chests;
};

void VisitLoot(uintptr_t actor, void* user) {
    LootPass* pass = (LootPass*)user;
    if (!actor || !pass || !off.RootComponent || !off.RelativeLocation)
        return;
    uintptr_t root = Read<uintptr_t>(actor + off.RootComponent);
    if (!root)
        return;
    Vec3 worldPos = Read<Vec3>(root + off.RelativeLocation);
    Vec3 delta{ worldPos.x - gCam.loc.x, worldPos.y - gCam.loc.y, worldPos.z - gCam.loc.z };
    float meters = sqrtf(delta.x * delta.x + delta.y * delta.y + delta.z * delta.z) / 100.f;
    if (cheat.lootRange > 0.f && meters > cheat.lootRange)
        return;
    Vec2 screen{};
    if (!WorldToScreen(worldPos, screen, W, H))
        return;

    std::string className = ObjClassName(actor);
    ImDrawList* draw = ImGui::GetBackgroundDrawList();
    if (pass->chests && className.find("Chest") != std::string::npos) {
        static PropInfo searched = FindClassProp("BuildingContainer", "bAlreadySearched");
        if (BitOn(actor, searched))
            return;
        std::string label = "Chest";
        if (cheat.lootDistance) {
            char dist[24] = {};
            snprintf(dist, sizeof(dist), " %.0fm", meters);
            label += dist;
        }
        ImVec2 pos(screen.x, screen.y);
        draw->AddText(ImVec2(pos.x + 1.f, pos.y + 1.f), IM_COL32(0, 0, 0, 220), label.c_str());
        draw->AddText(pos, IM_COL32(232, 176, 48, 255), label.c_str());
        return;
    }

    if (!cheat.floorLoot)
        return;
    static uintptr_t pickupClass = 0;
    static bool classTried = false;
    if (!classTried) {
        classTried = true;
        pickupClass = FindObjectByName("FortPickup", "Class");
    }
    if (pickupClass && !IsA(actor, pickupClass) && className.find("FortPickup") == std::string::npos)
        return;
    if (!pickupClass && className.find("FortPickup") == std::string::npos)
        return;

    static PropInfo entry = FindClassProp("FortPickup", "PrimaryPickupItemEntry");
    static PropInfo definition = FindClassProp("FortItemEntry", "ItemDefinition");
    static PropInfo countInfo = FindClassProp("FortItemEntry", "Count");
    static PropInfo picked = FindClassProp("FortPickup", "bPickedUp");
    static PropInfo tierInfo = FindClassProp("FortItemDefinition", "Tier");
    static PropInfo itemType = FindClassProp("FortItemDefinition", "ItemType");
    static PropInfo display = FindClassProp("FortItemDefinition", "DisplayName");
    if (!entry.found || !definition.found)
        return;
    if (BitOn(actor, picked))
        return;
    uintptr_t item = Read<uintptr_t>(actor + entry.offset + definition.offset);
    if (!item || !ObjClass(item))
        return;
    int type = itemType.found ? (int)Read<uint8_t>(item + itemType.offset) : -1;
    std::string raw = GetName(item);
    bool ammo = type == 1 || raw.find("Ammo") != std::string::npos;
    bool material = type == 23 || raw.find("WoodItem") != std::string::npos || raw.find("StoneItem") != std::string::npos || raw.find("MetalItem") != std::string::npos;
    int tier = tierInfo.found ? (int)Read<uint8_t>(item + tierInfo.offset) : 0;
    bool showTier = (tier <= 1 && cheat.lootCommon) || (tier == 2 && cheat.lootUncommon) || (tier == 3 && cheat.lootRare) ||
                    (tier == 4 && cheat.lootEpic) || (tier == 5 && cheat.lootLegendary) || (tier >= 6 && cheat.lootMythic);
    if (!showTier || (cheat.lootExcludeAmmo && ammo) || (cheat.lootExcludeMats && material))
        return;
    std::string label;
    if (display.found) {
        wchar_t wide[96] = {};
        if (CopyItemText(item, display.offset, wide, 96)) {
            char narrow[160] = {};
            WideCharToMultiByte(CP_UTF8, 0, wide, -1, narrow, sizeof(narrow), nullptr, nullptr);
            label = narrow;
        }
    }
    if (label.empty())
        label = PrettyItem(raw);
    if (label.empty())
        return;
    if (countInfo.found) {
        int count = Read<int32_t>(actor + entry.offset + countInfo.offset);
        if (count > 1) {
            char extra[16] = {};
            snprintf(extra, sizeof(extra), " x%d", count);
            label += extra;
        }
    }
    if (cheat.lootDistance) {
        char dist[24] = {};
        snprintf(dist, sizeof(dist), " %.0fm", meters);
        label += dist;
    }
    ImVec2 pos(screen.x, screen.y);
    ImU32 color = TierColor(tier);
    draw->AddText(ImVec2(pos.x + 1.f, pos.y + 1.f), IM_COL32(0, 0, 0, 220), label.c_str());
    draw->AddText(pos, color, label.c_str());
}

void DrawWorldEsp() {
    if (!cheat.floorLoot && !cheat.chestEsp)
        return;
    LootPass pass{};
    pass.chests = cheat.chestEsp;
    ForEachLevelActor(VisitLoot, &pass);
}

void WriteBitLocal(uintptr_t base, const PropInfo& prop, bool value) {
    if (!base || !prop.found || prop.offset < 0)
        return;
    uint8_t byte = Read<uint8_t>(base + prop.offset);
    if (prop.mask)
        byte = value ? (uint8_t)(byte | prop.mask) : (uint8_t)(byte & ~prop.mask);
    else
        byte = value ? 1 : 0;
    Write<uint8_t>(base + prop.offset, byte);
}

// Counters for the chams status line ("[feature] Chams: ...").
struct ChamsStats {
    int pawns = 0, meshesSet = 0, meshesFailed = 0, noParent = 0;
};
ChamsStats gChamsStats;
const char* gChamsParentName = "";

// The parent material chams are made from. RezIn_Master (the spawn-in wireframe) is the
// best one, but it isn't loaded on every season or before the match, so the search is
// repeated every 5 s until something is found and falls back on other loaded materials.
uintptr_t ChamsParent() {
    static uintptr_t cached = 0;
    static DWORD lastSearch = 0;
    static int searches = 0;
    if (cached)
        return cached;
    const DWORD now = GetTickCount();
    if (lastSearch && now - lastSearch < 5000)
        return 0;
    lastSearch = now;
    ++searches;
    uintptr_t materialClass = FindObjectByName("Material", "Class");
    if (!materialClass) {
        printf("[chams] search %d: Material class not found\n", searches);
        return 0;
    }
    static const char* const wanted[] = { "RezIn_Master", "M_Wireframe", "WireframeMaterial",
                                          "M_Highlight", "DefaultMaterial", "WorldGridMaterial" };
    constexpr int wantedCount = sizeof(wanted) / sizeof(wanted[0]);
    uintptr_t found[wantedCount] = {};
    const int count = ObjectCount();
    for (int i = 0; i < count; i++) {
        uintptr_t object = GetObjectByIndex(i);
        if (!object || !IsA(object, materialClass))
            continue;
        const std::string name = GetName(object);
        for (int w = 0; w < wantedCount; ++w) {
            if (!found[w] && name == wanted[w])
                found[w] = object;
        }
        if (found[0])
            break;
    }
    for (int w = 0; w < wantedCount; ++w) {
        if (found[w]) {
            cached = found[w];
            gChamsParentName = wanted[w];
            printf("[chams] material: %s at %p (search %d, %d objects)\n", wanted[w], (void*)cached, searches, count);
            return cached;
        }
    }
    printf("[chams] search %d: none of RezIn_Master/M_Wireframe/WireframeMaterial/M_Highlight/DefaultMaterial/"
           "WorldGridMaterial loaded yet (%d objects); retrying in 5 s\n", searches, count);
    return 0;
}

void PrepareChamsMaterial(uintptr_t material, bool wireframe) {
    static PropInfo blend = FindClassProp("Material", "BlendMode");
    static PropInfo depth = FindClassProp("Material", "bDisableDepthTest");
    static PropInfo wire = FindClassProp("Material", "Wireframe");
    static bool wireRetried = false;
    if (!wire.found && !wireRetried) {
        wireRetried = true;
        wire = FindClassProp("Material", "WireFrame");
    }
    if (blend.found)
        Write<uint8_t>(material + blend.offset, 3);
    WriteBitLocal(material, depth, true);
    WriteBitLocal(material, wire, wireframe);
}

__declspec(noinline) uintptr_t MakeCham(uintptr_t parent) {
    static uintptr_t klass = 0;
    static uintptr_t fn = 0;
    if (!klass)
        klass = FindObjectByName("KismetMaterialLibrary", "Class");
    if (!fn)
        fn = GetFunction("KismetMaterialLibrary", "CreateDynamicMaterialInstance");
    if (!klass || !fn || !world || !parent)
        return 0;
    struct Params {
        uintptr_t worldObj;
        uintptr_t parentMat;
        FName name;
        uintptr_t ret;
    } params{};
    params.worldObj = world;
    params.parentMat = parent;
    ProcessEvent(klass, fn, &params);
    return params.ret;
}

uintptr_t SafeMakeCham(uintptr_t parent) {
    __try {
        return MakeCham(parent);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
}

void PaintCham(uintptr_t instance) {
    if (!instance)
        return;
    static uintptr_t fn = 0;
    if (!fn)
        fn = GetFunction("MaterialInstanceDynamic", "SetVectorParameterValue");
    if (!fn)
        return;
    const wchar_t* names[] = { L"WireFrameParameterHighlight", L"WireFrameFadeOffColor", L"Top Color", L"Bottom Color" };
    const float alphas[] = { 0.75f, 0.25f, 0.75f, cheat.chamsColor[3] };
    for (int n = 0; n < 4; ++n) {
        struct Params {
            FName name;
            float r, g, b, a;
        } params{};
        params.name = MakeName(names[n]);
        params.r = cheat.chamsColor[0];
        params.g = cheat.chamsColor[1];
        params.b = cheat.chamsColor[2];
        params.a = alphas[n];
        ProcessEvent(instance, fn, &params);
    }
}

bool SafePaintOnly(uintptr_t instance) {
    if (!instance)
        return false;
    __try {
        PaintCham(instance);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

void SetMeshMaterial(uintptr_t mesh, int slot, uintptr_t instance) {
    static uintptr_t fn = 0;
    if (!fn)
        fn = GetFunction("PrimitiveComponent", "SetMaterial");
    if (!fn || !mesh || !instance)
        return;
    struct Params {
        int32_t index;
        uint8_t pad[4];
        uintptr_t material;
    } params{};
    params.index = slot;
    params.material = instance;
    ProcessEvent(mesh, fn, &params);
}

__declspec(noinline) void PaintAndSet(uintptr_t mesh, uintptr_t instance, int slot) {
    PaintCham(instance);
    SetMeshMaterial(mesh, slot, instance);
}

bool SafePaintAndSet(uintptr_t mesh, uintptr_t instance, int slot) {
    __try {
        PaintAndSet(mesh, instance, slot);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool LiveObject(uintptr_t object) {
    if (!object || object < 0x10000)
        return false;
    __try {
        if (!ObjClass(object))
            return false;
        const int index = Read<int32_t>(object + 0xC);
        const int count = ObjectCount();
        if (index < 0 || index >= count)
            return false;
        return GetObjectByIndex(index) == object;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool HasSkeletalMesh(uintptr_t mesh, int offset) {
    if (!mesh || offset <= 0)
        return false;
    __try {
        uintptr_t asset = *reinterpret_cast<uintptr_t*>(reinterpret_cast<uint8_t*>(mesh) + offset);
        if (!asset || asset < 0x10000)
            return false;
        const int index = *reinterpret_cast<int32_t*>(asset + 0xC);
        const int count = ObjectCount();
        if (index < 0 || index >= count)
            return false;
        return GetObjectByIndex(index) == asset;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

uintptr_t MeshForChams(uintptr_t mesh) {
    if (!mesh)
        return 0;
    if (off.BoneArray) {
        TArrayPtr local = Read<TArrayPtr>(mesh + off.BoneArray);
        if (local.Data && local.Num > 1)
            return mesh;
    }
    if (!off.MasterPose)
        return mesh;
    const int32_t serial = Read<int32_t>(mesh + off.MasterPose + 4);
    const int32_t idx = Read<int32_t>(mesh + off.MasterPose);
    if (serial == 0 || idx <= 0)
        return mesh;
    uintptr_t master = GetObjectByIndex(idx);
    if (!LiveObject(master) || master == mesh || !off.BoneArray)
        return mesh;
    TArrayPtr bones = Read<TArrayPtr>(master + off.BoneArray);
    if (bones.Data && bones.Num > 1)
        return master;
    return mesh;
}

__declspec(noinline) int MaterialCount(uintptr_t mesh) {
    static PropInfo skel = FindClassProp("SkinnedMeshComponent", "SkeletalMesh");
    static PropInfo materials = FindClassProp("SkeletalMesh", "Materials");
    if (!mesh || !skel.found || !materials.found)
        return 0;
    uintptr_t asset = Read<uintptr_t>(mesh + skel.offset);
    if (!asset || asset < 0x10000)
        return 0;
    const int count = Read<int32_t>(asset + materials.offset + (int)sizeof(void*));
    if (count <= 0 || count > 16)
        return 0;
    return count;
}

int SafeMaterialCount(uintptr_t mesh) {
    __try {
        return MaterialCount(mesh);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return -1;
    }
}

struct ChamEntry {
    uintptr_t mesh = 0;
    std::vector<uintptr_t> instances;
    bool failed = false;
    DWORD lastSet = 0;
    float painted[4] = { -1.f, -1.f, -1.f, -1.f }; // colour last painted on the instances
};

bool SameColor(const float a[4], const float b[4]) {
    return a[0] == b[0] && a[1] == b[1] && a[2] == b[2] && a[3] == b[3];
}

std::vector<ChamEntry>& ChamCache() {
    static std::vector<ChamEntry> cache;
    return cache;
}

void ApplyChamsToMesh(uintptr_t mesh, uintptr_t parent) {
    if (!LiveObject(mesh) || !parent || !world)
        return;
    static PropInfo skel = FindClassProp("SkinnedMeshComponent", "SkeletalMesh");
    if (skel.found && !HasSkeletalMesh(mesh, skel.offset))
        return;

    ChamEntry* entry = nullptr;
    for (ChamEntry& cached : ChamCache()) {
        if (cached.mesh == mesh) {
            entry = &cached;
            break;
        }
    }
    const DWORD now = GetTickCount();
    // A failed mesh is retried every 3 s (a material instance can fail while the pawn is
    // still being set up), instead of being given up on for the rest of the match.
    if (entry && entry->failed && now - entry->lastSet < 3000)
        return;
    // Performance: the materials are put back on the mesh once a second (the game can swap
    // them), and repainted only when the chams colour changes. Repainting every bot's every
    // material 30 times a second was thousands of engine calls a second with a full lobby.
    if (entry && entry->lastSet && now - entry->lastSet < 1000) {
        if (!SameColor(entry->painted, cheat.chamsColor)) {
            for (uintptr_t instance : entry->instances)
                SafePaintOnly(instance);
            memcpy(entry->painted, cheat.chamsColor, sizeof(entry->painted));
        }
        gChamsStats.meshesSet++;
        return;
    }

    const int slots = SafeMaterialCount(mesh);
    if (slots < 0) {
        if (!entry) {
            ChamCache().push_back(ChamEntry{});
            entry = &ChamCache().back();
            entry->mesh = mesh;
        }
        entry->failed = true;
        return;
    }
    if (slots == 0)
        return;

    if (!entry) {
        ChamCache().push_back(ChamEntry{});
        entry = &ChamCache().back();
        entry->mesh = mesh;
    }

    bool ok = true;
    while ((int)entry->instances.size() < slots)
        entry->instances.push_back(0);
    for (uintptr_t& instance : entry->instances) {
        if (!instance)
            instance = SafeMakeCham(parent);
        if (!instance)
            ok = false;
    }
    for (int slot = 0; slot < slots; ++slot) {
        uintptr_t instance = entry->instances[(size_t)slot];
        if (!instance || !SafePaintAndSet(mesh, instance, slot))
            ok = false;
    }
    entry->failed = !ok;
    entry->lastSet = now;
    memcpy(entry->painted, cheat.chamsColor, sizeof(entry->painted));
    if (ok)
        gChamsStats.meshesSet++;
    else
        gChamsStats.meshesFailed++;
}

int ReadPartPointers(uintptr_t pawn, int offset, int asArray, uintptr_t* out, int cap) {
    int written = 0;
    if (!pawn || !out || cap <= 0 || offset <= 0)
        return 0;
    __try {
        uint8_t* at = reinterpret_cast<uint8_t*>(pawn) + offset;
        if (asArray) {
            uintptr_t* data = *reinterpret_cast<uintptr_t**>(at);
            const int num = *reinterpret_cast<int32_t*>(at + 8);
            const int max = *reinterpret_cast<int32_t*>(at + 12);
            if (!data || num <= 0 || num > 24 || max < num || max > 64)
                return 0;
            for (int i = 0; i < num && written < cap; ++i) {
                if (data[i])
                    out[written++] = data[i];
            }
            return written;
        }
        uintptr_t* inlineParts = reinterpret_cast<uintptr_t*>(at);
        for (int i = 0; i < 8 && written < cap; ++i) {
            if (inlineParts[i])
                out[written++] = inlineParts[i];
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return written;
    }
    return written;
}

int PartsAreArray() {
    static int cached = -1;
    if (cached >= 0)
        return cached;
    cached = 1;
    uintptr_t clss = FindObjectByName("FortPlayerPawn", "Class");
    if (!clss || !ChildrenOff || !FieldNextOff)
        return cached;
    for (uintptr_t child = Read<uintptr_t>(clss + ChildrenOff); child; child = Read<uintptr_t>(child + FieldNextOff)) {
        if (GetName(child) != "CharacterPartSkeletalMeshComponents")
            continue;
        const std::string kind = ObjClassName(child);
        cached = kind.find("Array") != std::string::npos ? 1 : 0;
        break;
    }
    return cached;
}

void ApplyChams(uintptr_t pawn, uintptr_t mesh, uintptr_t parent) {
    if (!parent || !pawn)
        return;
    PrepareChamsMaterial(parent, cheat.chamsWireframe);
    ApplyChamsToMesh(mesh, parent);
    uintptr_t animated = MeshForChams(mesh);
    if (animated && animated != mesh)
        ApplyChamsToMesh(animated, parent);

    static PropInfo parts = FindClassProp("FortPlayerPawn", "CharacterPartSkeletalMeshComponents");
    static uintptr_t playerClass = 0;
    static uintptr_t meshClass = 0;
    if (!playerClass)
        playerClass = FindObjectByName("FortPlayerPawn", "Class");
    if (!meshClass)
        meshClass = FindObjectByName("SkeletalMeshComponent", "Class");
    if (!parts.found || !playerClass || !meshClass || !IsA(pawn, playerClass))
        return;

    uintptr_t raw[8] = {};
    const int found = ReadPartPointers(pawn, parts.offset, PartsAreArray(), raw, 8);
    for (int i = 0; i < found; ++i) {
        if (!LiveObject(raw[i]) || !IsA(raw[i], meshClass))
            continue;
        if (raw[i] == mesh || raw[i] == animated)
            continue;
        ApplyChamsToMesh(raw[i], parent);
    }
}

void VisitChamPawn(uintptr_t actor, void*) {
    uintptr_t self = localPawn;
    if (!self && localPC && off.AcknowledgedPawn)
        self = Read<uintptr_t>(localPC + off.AcknowledgedPawn);
    if (!actor || actor == self || !LiveObject(actor))
        return;
    if (Class_FortPawn && !IsA(actor, Class_FortPawn))
        return;
    if (!IsBotPawn(actor)) // bots only, never a real player
        return;
    if (off.bIsDying) {
        const uint8_t flag = Read<uint8_t>(actor + off.bIsDying);
        if (off.bIsDyingMask ? (flag & off.bIsDyingMask) != 0 : flag != 0)
            return;
    }
    uintptr_t mesh = off.Mesh ? Read<uintptr_t>(actor + off.Mesh) : 0;
    if (!LiveObject(mesh))
        return;
    if (cheat.maxDistance > 0.f && off.RootComponent && off.RelativeLocation) {
        uintptr_t root = Read<uintptr_t>(actor + off.RootComponent);
        if (root) {
            const Vec3 pos = Read<Vec3>(root + off.RelativeLocation);
            const Vec3 delta = pos - gCam.loc;
            if (sqrtf(delta.dot(delta)) / 100.f > cheat.maxDistance)
                return;
        }
    }
    uintptr_t parent = ChamsParent();
    gChamsStats.pawns++;
    if (!parent) {
        gChamsStats.noParent++;
        return;
    }
    ApplyChams(actor, mesh, parent);
}

void ApplyPawnChams() {
    if (!cheat.espEnabled || !cheat.chams || !world)
        return;
    // Chapter 1 (HUD vtable hook): off. Fortnite loads a bot's outfit parts in the background during the
    // match and then applies its own materials to them; a part already wearing a chams material crashed
    // 9.40 every match right after "FortCustomizationAssetLoader CP_Head_... Finished loading" (access
    // violations on the game thread and in render tasks). Season X (inline ProcessEvent hook) keeps chams.
    if (HudHookMode) {
        FeatureStatus("Chams", "off on Chapter 1 seasons (it crashed the game when bots' outfits finished loading); ESP still works");
        return;
    }
    if (ObjectCount() <= 0)
        return;
    static DWORD last = 0;
    const DWORD now = GetTickCount();
    if (now - last < 50)
        return;
    last = now;
    gChamsStats = {};
    ForEachLevelActor(VisitChamPawn, nullptr);
    const ChamsStats& c = gChamsStats;
    if (!c.pawns)
        FeatureStatus("Chams", "no live bot pawns found");
    else if (c.noParent)
        FeatureStatus("Chams", "%d bots, but no chams material is loaded yet (see [chams] lines)", c.pawns);
    else
        FeatureStatus("Chams", "%d bots, material %s, meshes painted %d, failed %d", c.pawns, gChamsParentName,
            c.meshesSet, c.meshesFailed);
}

struct StickyBone {
    int bone = 0;
    Vec3 world{};
};

struct StickyEsp {
    uintptr_t pawn = 0;
    Vec3 head{};
    Vec3 root{};
    DWORD when = 0;
    DWORD skelWhen = 0;
    int skelCount = 0;
    StickyBone skel[18]{};
};

StickyEsp StickySlots[32];

StickyEsp* StickyFor(uintptr_t pawn) {
    StickyEsp* freeSlot = nullptr;
    for (StickyEsp& slot : StickySlots) {
        if (slot.pawn == pawn)
            return &slot;
        if (!freeSlot && !slot.pawn)
            freeSlot = &slot;
    }
    if (!freeSlot)
        freeSlot = &StickySlots[0];
    *freeSlot = {};
    freeSlot->pawn = pawn;
    return freeSlot;
}

}

bool ChamsOnGameThread();

// Why bots were or weren't drawn, counted per frame, for the debug overlay and a log line
// every 10 s ("[esp-draw] ...").
struct EspDrawStats {
    int targets = 0, hiddenNotVisible = 0, bodyFromBones = 0, bodyFromRoot = 0, noBody = 0;
    int tooFar = 0, offScreen = 0, drawn = 0;
};
static EspDrawStats gDrawStats;

static void LogDrawStats() {
    static DWORD last = 0;
    const DWORD now = GetTickCount();
    if (last && now - last < 10000)
        return;
    last = now;
    const EspDrawStats& d = gDrawStats;
    printf("[esp-draw] bots %d, drawn %d, hidden by visible check %d, body from bones %d, body from position %d, "
           "no body %d, too far %d, off screen %d, HUD hook calls %ld\n",
        d.targets, d.drawn, d.hiddenNotVisible, d.bodyFromBones, d.bodyFromRoot, d.noBody, d.tooFar, d.offScreen,
        (long)HudHookFires);
    printf("[esp-draw] camera loc %.0f %.0f %.0f rot %.1f %.1f fov %.1f (%s, source %s)\n", gCam.loc.x, gCam.loc.y, gCam.loc.z,
        gCam.rot.Pitch, gCam.rot.Yaw, gCam.fov, gCam.valid ? "valid" : "INVALID", CameraSourceName());
}

// Bones on old seasons (1.7.2) don't always pass the body checks below; the bot's position
// still gives a box, the way Tilted's client drew its bot markers (actor location, -90 to +88).
static bool BodyFromRoot(uintptr_t pawn, Vec3& bottom, Vec3& head) {
    if (!pawn || !off.RootComponent || !off.RelativeLocation)
        return false;
    const uintptr_t root = Read<uintptr_t>(pawn + off.RootComponent);
    if (!root)
        return false;
    const Vec3 pos = Read<Vec3>(root + off.RelativeLocation);
    if (!std::isfinite(pos.x) || !std::isfinite(pos.y) || !std::isfinite(pos.z) || (!pos.x && !pos.y && !pos.z))
        return false;
    bottom = pos;
    bottom.z -= 90.f;
    head = pos;
    head.z += 70.f;
    return true;
}

void DrawESP() {
    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    // Log the finished counts of the frame before; logging after the reset printed only zeros.
    LogDrawStats();
    if (cheat.espEnabled)
        FeatureStatus("ESP", !gDrawStats.targets ? "no bots found near you" : !gDrawStats.drawn ? "bots found, none on screen"
                             : gDrawStats.bodyFromBones ? "drawing bots (skeleton from bones)" : "drawing bots (no bones: box from position)");
    gDrawStats = EspDrawStats{};
    gDrawStats.targets = (int)targets.size();

    if (cheat.espFov && gCam.fov > 1.f && H > 0) {
        float r = tanf(cheat.aimFov * 0.017453292f / 2.f) / tanf(gCam.fov * 0.017453292f / 2.f) * (W / 2.f);
        dl->AddCircle(ImVec2(W / 2.f, H / 2.f), r, ColorU32(cheat.fovColor), 96, cheat.fovThickness);
    }

    DrawWorldEsp();

    if (!cheat.espEnabled)
        return;

    for (size_t ti = 0; ti < targets.size(); ti++) {
        Target& t = targets[ti];
        if (!t.mesh)
            continue;

        const bool isSelf = localPawn && t.pawn == localPawn;
        const bool hideSelf = cheat.noSelfEsp || cheat.chams;
        const bool selfHat = cheat.chinaHat && cheat.selfChinaHat;
        if (isSelf && hideSelf && !selfHat)
            continue;
        StickyEsp* sticky = StickyFor(t.pawn);
        const DWORD now = GetTickCount();
        const bool recent = sticky->when && now - sticky->when < 90;
        if (cheat.visibleCheck && !t.visible && !isSelf && !recent) {
            gDrawStats.hiddenNotVisible++;
            continue;
        }

        const BoneSet& bones = t.bones;
        FTransform meshToWorld = MeshToWorld(t.mesh);

        TArrayPtr buffers[2] = {};
        const int bufferCount = GatherBoneBuffers(t.mesh, buffers);
        TArrayPtr pose{};
        float bestPose = 1.0e9f;
        for (int i = 0; i < bufferCount; i++) {
            if (!BonesReady(bones) || !PoseOk(buffers[i], meshToWorld, bones))
                continue;
            Vec3 poseHead{};
            if (!BoneWorld(buffers[i], bones.Head, meshToWorld, poseHead))
                continue;
            const float shift = sticky->skelWhen ? Dist3(poseHead, sticky->head) : 0.f;
            if (!pose.Data || shift < bestPose) {
                pose = buffers[i];
                bestPose = shift;
            }
        }

        Vec3 bottom3{}, head3{};
        bool haveBody = false;
        float bestShift = 1.0e9f;
        const bool haveLast = sticky->when != 0;
        const int headIdx = bones.Head > 0 ? bones.Head : 68;
        for (int i = 0; i < bufferCount; ++i) {
            if (headIdx <= 0 || headIdx >= buffers[i].Num)
                continue;
            Vec3 root{}, head{};
            if (!BoneWorld(buffers[i], 0, meshToWorld, root) || !BoneWorld(buffers[i], headIdx, meshToWorld, head))
                continue;
            const float rise = head.z - root.z;
            const float span = Dist3(head, root);
            if (rise < 40.f || rise > 230.f || span < 40.f || span > 260.f)
                continue;
            const float shift = haveLast ? Dist3(head, sticky->head) : 0.f;
            if (!haveBody || shift < bestShift) {
                bottom3 = root;
                head3 = head;
                bestShift = shift;
                haveBody = true;
            }
        }
        if (haveBody) {
            sticky->head = head3;
            sticky->root = bottom3;
            sticky->when = now;
            gDrawStats.bodyFromBones++;
        } else if (recent) {
            head3 = sticky->head;
            bottom3 = sticky->root;
            haveBody = true;
            gDrawStats.bodyFromBones++;
        } else if (BodyFromRoot(t.pawn, bottom3, head3)) {
            haveBody = true;
            gDrawStats.bodyFromRoot++;
        }
        if (!haveBody) {
            gDrawStats.noBody++;
            continue;
        }

        Vec3 delta = head3 - gCam.loc;
        float meters = t.dist > 1.f ? (t.dist / 100.f) : (sqrtf(delta.dot(delta)) / 100.f);
        if (cheat.maxDistance > 0.f && meters > cheat.maxDistance) {
            gDrawStats.tooFar++;
            continue;
        }
        if (!localPawn && meters > 80.f) {
            gDrawStats.tooFar++;
            continue;
        }

        if (cheat.chinaHat && (!isSelf || cheat.selfChinaHat))
            DrawChinaHat(head3, ColorU32(cheat.chinaColor), cheat.chinaThickness);

        if (isSelf && hideSelf)
            continue;

        Vec3 top3 = head3;
        top3.z += 15.f;

        Vec2 bottom2{}, top2{};
        if (!WorldToScreen(bottom3, bottom2, W, H) || !WorldToScreen(top3, top2, W, H)) {
            gDrawStats.offScreen++;
            continue;
        }
        if (bottom2.x == 0.f && bottom2.y == 0.f) {
            gDrawStats.offScreen++;
            continue;
        }
        gDrawStats.drawn++;

        float height = bottom2.y - top2.y;
        float width = height * (cheat.boxWidth > 0.01f ? cheat.boxWidth : 0.45f);
        ImVec2 topLeft(top2.x - width * 0.5f, top2.y);
        ImVec2 bottomRight(bottom2.x + width * 0.5f, bottom2.y);
        ImU32 color = ColorU32(cheat.boxColor);

        if (cheat.espName || cheat.espDist) {
            std::string label;
            if (cheat.espName && !t.name.empty() && t.name != "BOT")
                label = t.name;
            if (cheat.espDist) {
                char buf[32];
                snprintf(buf, sizeof(buf), "%.0fm", meters);
                if (!label.empty())
                    label += " ";
                label += buf;
            }
            if (!label.empty())
                DrawTextEsp(dl, ImVec2(top2.x, top2.y - 14.f), color, label.c_str());
        }

        if (cheat.boxFilled)
            dl->AddRectFilled(topLeft, bottomRight, ColorU32(cheat.boxFill));

        if (cheat.espBox) {
            if (cheat.boxOutline)
                dl->AddRect(topLeft, bottomRight, IM_COL32(0, 0, 0, 255), 0.f, 0, cheat.boxThickness + 2.f);
            dl->AddRect(topLeft, bottomRight, color, 0.f, 0, cheat.boxThickness);
        }

        if (cheat.espLine) {
            dl->AddLine(ImVec2((float)W / 2.f, (float)H), ImVec2(bottom2.x, bottom2.y), color, cheat.boxThickness);
        }

        if (cheat.cornerBox)
        {
            if (cheat.boxOutline)
                DrawCorners(dl, topLeft, bottomRight, IM_COL32(0, 0, 0, 255), cheat.boxThickness + 2.f);
            DrawCorners(dl, topLeft, bottomRight, color, cheat.boxThickness);
        }

        if (cheat.healthBar) {
            float hx = cheat.healthBarSide == 0 ? topLeft.x - 5.f : bottomRight.x + 3.f;
            dl->AddRectFilled(ImVec2(hx, topLeft.y), ImVec2(hx + 3.f, bottomRight.y), IM_COL32(0, 0, 0, 180));
            dl->AddRectFilled(ImVec2(hx, topLeft.y), ImVec2(hx + 3.f, bottomRight.y), ColorU32(cheat.healthColor));
        }

        const bool holdSkel = !pose.Data && cheat.espSkeleton && sticky->skelCount > 0 && sticky->skelWhen && now - sticky->skelWhen < 120;
        if ((pose.Data && cheat.espSkeleton && BonesReady(bones)) || holdSkel) {

            ImU32 skel = ColorU32(cheat.skeletonColor);
            struct Joint {
                ImVec2 screen;
                Vec3 world;
            };
            StickyBone fresh[18]{};
            int freshCount = 0;
            auto sample = [&](int bone, Joint& outj) {
                if (holdSkel) {
                    bool found = false;
                    for (int i = 0; i < sticky->skelCount; ++i) {
                        if (sticky->skel[i].bone != bone)
                            continue;
                        outj.world = sticky->skel[i].world;
                        found = true;
                        break;
                    }
                    if (!found)
                        return false;
                } else if (!BoneWorld(pose, bone, meshToWorld, outj.world)) {
                    return false;
                } else if (freshCount < 18) {
                    fresh[freshCount].bone = bone;
                    fresh[freshCount].world = outj.world;
                    freshCount++;
                }
                Vec2 projected{};
                if (!WorldToScreen(outj.world, projected, W, H))
                    return false;
                outj.screen = ImVec2(projected.x, projected.y);
                return true;
            };

            const int upper = bones.Chest > 0 ? bones.Chest : bones.Neck;
            Joint leftArm{}, rightArm{}, shoulder{};
            const bool hasLeft = sample(bones.LUpper, leftArm);
            const bool hasRight = sample(bones.RUpper, rightArm);
            bool hasShoulder = false;
            if (hasLeft && hasRight) {
                shoulder.world = {
                    (leftArm.world.x + rightArm.world.x) * 0.5f,
                    (leftArm.world.y + rightArm.world.y) * 0.5f,
                    (leftArm.world.z + rightArm.world.z) * 0.5f
                };
                shoulder.screen = ImVec2((leftArm.screen.x + rightArm.screen.x) * 0.5f, (leftArm.screen.y + rightArm.screen.y) * 0.5f);
                hasShoulder = true;
            } else if (sample(upper, shoulder) || sample(bones.Neck, shoulder)) {
                hasShoulder = true;
            }

            auto collect = [&](std::initializer_list<int> ids) {
                std::vector<std::vector<ImVec2>> runs;
                std::vector<ImVec2> run;
                Joint prev{};
                bool hasPrev = false;
                auto push = [&](const Joint& joint) {
                    if (hasPrev && Dist3(prev.world, joint.world) > 100.f) {
                        if (run.size() >= 2)
                            runs.push_back(run);
                        run.clear();
                    }
                    run.push_back(joint.screen);
                    prev = joint;
                    hasPrev = true;
                };
                for (int id : ids) {
                    if (id == -2) {
                        if (hasShoulder)
                            push(shoulder);
                        continue;
                    }
                    Joint joint{};
                    if (sample(id, joint))
                        push(joint);
                }
                if (run.size() >= 2)
                    runs.push_back(std::move(run));
                return runs;
            };

            std::vector<std::vector<ImVec2>> chains;
            auto addChain = [&](std::initializer_list<int> ids) {
                for (auto& run : collect(ids))
                    chains.push_back(std::move(run));
            };
            addChain({ bones.Head, bones.Neck, upper, bones.Pelvis });
            addChain({ -2, bones.LUpper, bones.LLower, bones.LHand });
            addChain({ -2, bones.RUpper, bones.RLower, bones.RHand });
            addChain({ bones.Pelvis, bones.LThigh, bones.LCalf, bones.LFoot });
            addChain({ bones.Pelvis, bones.RThigh, bones.RCalf, bones.RFoot });

            auto paint = [&](ImU32 line, float thick) {
                if (cheat.skeletonCurved) {
                    for (const std::vector<ImVec2>& chain : chains)
                        StrokeChain(dl, chain, line, thick, true);
                } else if (cheat.skeletonSpaghetti) {
                    for (const std::vector<ImVec2>& chain : chains) {
                        for (size_t i = 1; i < chain.size(); ++i)
                            StrokeSpaghetti(dl, chain[i - 1], chain[i], line, thick);
                    }
                } else {
                    for (const std::vector<ImVec2>& chain : chains)
                        StrokeChain(dl, chain, line, thick, false);
                }
            };
            if (cheat.skeletonOutline)
                paint(IM_COL32(0, 0, 0, 255), cheat.skeletonThickness + 2.f);
            paint(skel, cheat.skeletonThickness);
            if (!holdSkel && freshCount >= 4) {
                sticky->skelCount = freshCount;
                for (int i = 0; i < freshCount; ++i)
                    sticky->skel[i] = fresh[i];
                sticky->skelWhen = now;
            }
        }
    }
}

using ChamProcessEventFn = void (*)(uintptr_t, uintptr_t, void*);
ChamProcessEventFn OriginalChamProcessEvent = nullptr;
thread_local int ChamHookDepth = 0;
DWORD ChamRenderThread = 0;
bool ChamHookLive = false;

bool NamedGameThread(DWORD tid) {
    using GetThreadDescriptionFn = HRESULT(WINAPI*)(HANDLE, PWSTR*);
    static GetThreadDescriptionFn getDesc =
        reinterpret_cast<GetThreadDescriptionFn>(GetProcAddress(GetModuleHandleA("kernel32.dll"), "GetThreadDescription"));
    if (!getDesc)
        return false;
    static DWORD checked[12] = {};
    static bool match[12] = {};
    static int count = 0;
    for (int i = 0; i < count; ++i) {
        if (checked[i] == tid)
            return match[i];
    }
    if (count >= 12)
        return false;
    bool isGame = false;
    HANDLE thread = OpenThread(THREAD_QUERY_LIMITED_INFORMATION, FALSE, tid);
    if (thread) {
        PWSTR name = nullptr;
        if (SUCCEEDED(getDesc(thread, &name)) && name)
            isGame = _wcsicmp(name, L"GameThread") == 0;
        if (name)
            LocalFree(name);
        CloseHandle(thread);
    }
    checked[count] = tid;
    match[count] = isGame;
    ++count;
    return isGame;
}

DWORD ChamGameThread = 0;

void HookedChamProcessEvent(uintptr_t obj, uintptr_t fn, void* params) {
    if (ChamHookDepth > 0) {
        if (OriginalChamProcessEvent)
            OriginalChamProcessEvent(obj, fn, params);
        return;
    }
    const DWORD tid = GetCurrentThreadId();
    if (!ChamGameThread && ChamRenderThread && tid != ChamRenderThread) {
        static uintptr_t controllerClass = 0;
        static bool controllerTried = false;
        if (!controllerTried) { // once: a miss here would rescan every object on every engine call
            controllerTried = true;
            controllerClass = FindObjectByName("PlayerController", "Class");
        }
        const bool controllerCall = controllerClass && obj && IsA(obj, controllerClass);
        if (NamedGameThread(tid) || controllerCall)
            ChamGameThread = tid;
    }
    if (ChamGameThread && tid == ChamGameThread) {
        ++ChamHookDepth;
        SampleCameraOnGameThread();
        ApplyAim();
        ApplyPawnChams();
        TickPhantomLook();
        ResolveNamesOnGameThread();
        --ChamHookDepth;
    }
    if (OriginalChamProcessEvent)
        OriginalChamProcessEvent(obj, fn, params);
}

// ---- Chapter 1 (6.20, 7.40, ...): HUD vtable hook --------------------------------------------
// The inline hook above patches the engine's shared ProcessEvent, which crashed 6.20 and 7.40.
// There, only the local player's HUD object gets a private copy of its vtable with the ProcessEvent
// slot pointing at HudProcessEvent; no game code is patched. Right after the game runs the HUD's
// ReceiveDrawHUD for the frame (on the game thread), aim and chams are applied.
// A new HUD (lobby -> match) gets its own copy; old copies are never freed, since a HUD that is
// being destroyed still calls through its vtable.
ChamProcessEventFn OriginalHudProcessEvent = nullptr;
uintptr_t HudDrawFn = 0;
bool HudHookMode = false;
volatile ULONGLONG HudHookLastFire = 0;

volatile LONG HudHookFires = 0;   // ReceiveDrawHUD calls seen, for the 10 s debug line
volatile LONG HudHookInFlight = 0; // calls currently inside HookedHudProcessEvent (unload waits for 0)
static uintptr_t HudHookedObject = 0;
static uintptr_t HudHookedCopy = 0;
static uintptr_t HudOriginalTable = 0;

void HookedHudProcessEvent(uintptr_t obj, uintptr_t fn, void* params) {
    InterlockedIncrement(&HudHookInFlight);
    OriginalHudProcessEvent(obj, fn, params);
    if (fn != HudDrawFn || ChamHookDepth > 0) {
        InterlockedDecrement(&HudHookInFlight);
        return;
    }
    InterlockedIncrement(&HudHookFires);
    ++ChamHookDepth;
    __try {
        SampleCameraOnGameThread();
        ApplyAim();
        ApplyPawnChams();
        TickPhantomLook();
        ResolveNamesOnGameThread();
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
    --ChamHookDepth;
    HudHookLastFire = GetTickCount64();
    InterlockedDecrement(&HudHookInFlight);
}

// Puts the HUD's own vtable back before the menu unloads (End key). Without this the HUD kept
// calling HookedHudProcessEvent after the DLL was freed, and the game crashed a few seconds
// later (1.7.2 crash report: execute access violation at an address in no loaded module).
void UninstallHudChamsHook() {
    if (HudHookedObject && HudHookedCopy && HudOriginalTable) {
        __try {
            if (*reinterpret_cast<uintptr_t*>(HudHookedObject) == HudHookedCopy)
                *reinterpret_cast<uintptr_t*>(HudHookedObject) = HudOriginalTable;
        } __except (EXCEPTION_EXECUTE_HANDLER) {
        }
        printf("[chams] HUD vtable restored\n");
    }
    HudHookedObject = HudHookedCopy = HudOriginalTable = 0;
    // Let calls already inside the hook leave it before the code is freed.
    for (int i = 0; i < 50 && HudHookInFlight > 0; ++i)
        Sleep(10);
    HudHookMode = false;
}

// Called from the render thread every frame (cheap when the HUD is already hooked).
void TickHudChamsHook() {
    static uintptr_t hookedHud = 0;
    static uintptr_t hookedTable = 0;
    static bool failed = false;
    static const bool chapterOne = GetModuleHandleW(L"cobalt.dll") != nullptr; // Season X uses the inline hook
    if (!chapterOne || failed || ChamHookLive || ProcessEventIdx <= 0 || !localPC)
        return;
    static PropInfo hudProp = FindClassProp("PlayerController", "MyHUD");
    if (!hudProp.found) {
        printf("[chams] HUD hook off: PlayerController.MyHUD not found\n");
        failed = true;
        return;
    }
    const uintptr_t hud = Read<uintptr_t>(localPC + hudProp.offset);
    if (!hud)
        return;
    const uintptr_t table = Read<uintptr_t>(hud);
    if (hud == hookedHud && table == hookedTable)
        return;
    if (!HudDrawFn)
        HudDrawFn = GetFunction("HUD", "ReceiveDrawHUD");
    if (!HudDrawFn || !table) {
        failed = !HudDrawFn;
        if (failed) printf("[chams] HUD hook off: HUD.ReceiveDrawHUD not found\n");
        return;
    }
    // Copy as many entries as are readable (vtables sit in read-only data; stop at the end of the region).
    int count = 0;
    MEMORY_BASIC_INFORMATION info{};
    if (VirtualQuery(reinterpret_cast<void*>(table), &info, sizeof(info))) {
        const uintptr_t end = reinterpret_cast<uintptr_t>(info.BaseAddress) + info.RegionSize;
        while (count < 1024 && table + (count + 1) * sizeof(void*) <= end)
            ++count;
    }
    if (count <= ProcessEventIdx) {
        printf("[chams] HUD hook off: vtable has %d entries, ProcessEvent is %d\n", count, ProcessEventIdx);
        failed = true;
        return;
    }
    // One entry more in front: MSVC keeps the type information pointer just before the vtable.
    auto block = static_cast<void**>(VirtualAlloc(nullptr, sizeof(void*) * (count + 1), MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
    if (!block) {
        failed = true;
        return;
    }
    auto source = reinterpret_cast<void**>(table);
    block[0] = source[-1];
    void** copy = block + 1;
    memcpy(copy, source, sizeof(void*) * count);
    if (copy[ProcessEventIdx] != reinterpret_cast<void*>(&HookedHudProcessEvent))
        OriginalHudProcessEvent = reinterpret_cast<ChamProcessEventFn>(copy[ProcessEventIdx]);
    if (!OriginalHudProcessEvent) {
        failed = true;
        return;
    }
    copy[ProcessEventIdx] = reinterpret_cast<void*>(&HookedHudProcessEvent);
    DWORD old = 0;
    VirtualProtect(block, sizeof(void*) * (count + 1), PAGE_READONLY, &old);
    *reinterpret_cast<void***>(hud) = copy;
    hookedHud = hud;
    hookedTable = reinterpret_cast<uintptr_t>(copy);
    HudHookedObject = hud;
    HudHookedCopy = hookedTable;
    HudOriginalTable = table;
    HudHookMode = true;
    printf("[chams] HUD hook on (HUD 0x%llX, %d vtable entries)\n", (unsigned long long)hud, count);
}

bool ChamsOnGameThread() {
    // HUD hook: only while the game is actually drawing the HUD; otherwise aim falls back to the
    // render thread (lobby, loading screens, HUD hidden).
    if (HudHookMode)
        return GetTickCount64() - HudHookLastFire < 500;
    return ChamHookLive;
}

void NoteRenderThread() {
    ChamRenderThread = GetCurrentThreadId();
}

bool InstallChamsHook() {
    if (ChamHookLive || ProcessEventIdx < 0)
        return ChamHookLive;
    uintptr_t sample = 0;
    const int count = ObjectCount();
    for (int i = 0; i < count && i < 32; ++i) {
        sample = GetObjectByIndex(i);
        if (sample && Read<uintptr_t>(sample))
            break;
        sample = 0;
    }
    if (!sample)
        return false;
    uintptr_t table = Read<uintptr_t>(sample);
    void* target = table ? Read<void*>(table + (uintptr_t)ProcessEventIdx * 8) : nullptr;
    if (!target)
        return false;
    if (MH_CreateHook(target, &HookedChamProcessEvent, reinterpret_cast<void**>(&OriginalChamProcessEvent)) != MH_OK)
        return false;
    if (MH_EnableHook(target) != MH_OK)
        return false;
    ChamHookLive = true;
    return true;
}

// Misc > Session > "Debug overlay": what the menu sees right now, top left of the screen.
// Each line names the thing a feature needs, so "ESP shows nothing" can be read off the screen:
// no bots found, bots found but hidden by the visible check, no camera, HUD hook not firing, ...
void DrawDebugOverlay() {
    if (!cheat.debugOverlay)
        return;
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    const EspDrawStats& d = gDrawStats;
    char lines[14 + 24][160];
    int n = 0;
    snprintf(lines[n++], 160, "Syndicate menu debug  -  build %s", g_gameBuild[0] ? g_gameBuild : "unknown");
    snprintf(lines[n++], 160, "world %s  controller %s  pawn %s  camera %s (fov %.0f, %s)", world ? "yes" : "NO",
        localPC ? "yes" : "NO", localPawn ? "yes" : "NO", gCam.valid ? "yes" : "NO", gCam.fov, CameraSourceName());
    snprintf(lines[n++], 160, "bot flag PlayerState.bIsABot 0x%X mask 0x%X%s", off.bIsABot, off.bIsABotMask,
        off.bIsABot ? "" : "  (MISSING: ESP/aim/chams off)");
    snprintf(lines[n++], 160, "bots %d  drawn %d  hidden by visible check %d  off screen %d  too far %d", d.targets,
        d.drawn, d.hiddenNotVisible, d.offScreen, d.tooFar);
    snprintf(lines[n++], 160, "body from bones %d  from position %d  none %d", d.bodyFromBones, d.bodyFromRoot,
        d.noBody);
    snprintf(lines[n++], 160, "chams/aim hook: %s, HUD calls %ld", HudHookMode ? "HUD vtable" : (ChamHookLive ? "ProcessEvent" : "OFF"),
        (long)HudHookFires);
    snprintf(lines[n++], 160, "real players in match: %s (weapon mods %s)", OtherHumansInMatch() ? "yes" : "no",
        OtherHumansInMatch() ? "paused" : "active");
    snprintf(lines[n++], 160, "offsets: Mesh 0x%X  Root 0x%X  Bones 0x%X  LastRender 0x%X  Weapon 0x%X", off.Mesh,
        off.RootComponent, off.BoneArray, off.LastRenderTime, off.CurrentWeapon);
    DescribeOffsetChecks(lines[n++], 160);
    snprintf(lines[n++], 160, "Magic Bullet: %s", !cheat.magicBullet ? "off" : (OtherHumansInMatch() ? "paused (real player in match)" : "on (match server on this PC)"));
    n += FeatureStatusLines(lines + n, 24);
    snprintf(lines[n++], 160, "full detail: Runtime\\dlls\\SyndicateMenu.log  ([feature] [mods] [bones] [humans] lines)");

    float y = 8.f, width = 0.f;
    for (int i = 0; i < n; ++i) {
        const float w = ImGui::CalcTextSize(lines[i]).x;
        if (w > width) width = w;
    }
    const float lineH = ImGui::GetTextLineHeight() + 2.f;
    dl->AddRectFilled(ImVec2(4.f, 4.f), ImVec2(16.f + width, 12.f + lineH * n), IM_COL32(0, 0, 0, 170), 4.f);
    for (int i = 0; i < n; ++i) {
        dl->AddText(ImVec2(10.f, y), i == 0 ? IM_COL32(150, 140, 255, 255) : IM_COL32(235, 235, 235, 255), lines[i]);
        y += lineH;
    }
}
