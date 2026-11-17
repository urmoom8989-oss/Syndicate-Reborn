// Syndicate custom outfits on the Season 3 (3.5) client.
//
// The backend (syndicateCustomOutfits.js) lists "AthenaCharacter:CID_Syndicate_Phantom" in the athena profile while
// Unlock all is on. Fortnite only shows a profile item whose template id the asset manager knows, so the menu makes
// an outfit (a new AthenaCharacterItemDefinition in the transient package) and registers it with
// UAssetManager::AddDynamicAsset under that id. When The Reaper's outfit (CID_084) is loaded it gets The Reaper's
// hero, rarity and icons, and its own name. In matches the server puts The Reaper's suit with the default Spitfire
// head on the player (Erbium RestoreSyndicateOutfit, custom=phantom).
//
// Same approach as the client module's Save the World locker (SyndicateStwLocker.inl), with two differences: the
// outfit is a fresh object (not a byte copy of another outfit), so it shares no arrays or strings with a real one
// and nothing is freed twice when the game closes; and the text fields are new FTexts the outfit owns.
// Every step writes a [outfits] line to the menu log.
//
// Stage 2, the white suit: when your own pawn wears The Reaper's body with Spitfire's head (what the match server
// puts on for the Phantom), TickPhantomLook repaints the body's materials white on the game thread. It is a client
// side look: only your game shows it. [outfits] look lines list the parts and the materials it changed.
#include "CustomOutfits.h"
#include "IconServer.h"
#include "Game.h"
#include "../Menu/Config.h"
#include "../Core/Memory.h"
#include "../SDK/Init.h"
#include "../SDK/Objects.h"
#include "../SDK/Offsets.h"

#include <cctype>
#include <intrin.h>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <string>

namespace {

const wchar_t* const kPhantomId = L"CID_Syndicate_Phantom";
const char* const kPhantomBase = "CID_084_Athena_Commando_M_Assassin";
const wchar_t* const kPhantomName = L"Spitfire: Phantom";
const wchar_t* const kPhantomDescription = L"The Reaper's suit with Spitfire's head. A Syndicate original.";

int g_stage = 0;          // 0 waiting for the engine, 1 waiting for The Reaper, 2 icon and preview, 3 done, -1 off or failed
int g_tries = 0;
ULONGLONG g_nextTry = 0;
uintptr_t g_item = 0;     // the registered outfit

bool Contains(const std::string& text, const char* word) {
    std::string lower = text;
    for (char& c : lower)
        c = (char)tolower((unsigned char)c);
    return lower.find(word) != std::string::npos;
}

bool Season3() {
    DetectGameBuild();
    return strncmp(g_gameBuild, "Release-3.5", 11) == 0;
}

// Start of the function that contains an address, from the exe's unwind table (follows chained entries).
uintptr_t FunctionStart(uintptr_t at) {
    DWORD64 base = 0;
    auto entry = RtlLookupFunctionEntry(at, &base, nullptr);
    for (int hops = 0; entry && hops < 8; ++hops) {
        auto info = reinterpret_cast<const uint8_t*>(base + entry->UnwindData);
        if (!((info[0] >> 3) & UNW_FLAG_CHAININFO))
            break;
        const uint8_t codes = info[2];
        entry = (PRUNTIME_FUNCTION)(info + 4 + ((codes + 1) & ~1) * 2);
    }
    return entry ? static_cast<uintptr_t>(base + entry->BeginAddress) : 0;
}

bool Readable(uintptr_t p, size_t bytes) {
    MEMORY_BASIC_INFORMATION m{};
    if (!p || !VirtualQuery(reinterpret_cast<void*>(p), &m, sizeof(m)) || m.State != MEM_COMMIT || (m.Protect & (PAGE_NOACCESS | PAGE_GUARD)))
        return false;
    return p + bytes <= reinterpret_cast<uintptr_t>(m.BaseAddress) + m.RegionSize;
}

// Keeps an object alive for the whole game: the RootSet flag (1 << 30) in its GObjects item (UE 4.20).
void AddToRoot(uintptr_t obj) {
    if (!obj || !objectArray)
        return;
    const int32_t index = Read<int32_t>(obj + 0xC);
    if (index < 0)
        return;
    uintptr_t item = 0;
    if (gobjectsChunked) {
        const uintptr_t chunks = Read<uintptr_t>(objectArray);
        const uintptr_t chunk = chunks ? Read<uintptr_t>(chunks + 8ull * (index / 0x10000)) : 0;
        item = chunk ? chunk + 0x18ull * (index % 0x10000) : 0;
    } else {
        const uintptr_t items = Read<uintptr_t>(objectArray);
        item = items ? items + 0x18ull * index : 0;
    }
    if (!item || Read<uintptr_t>(item) != obj)
        return;
    Write<int32_t>(item + 8, Read<int32_t>(item + 8) | (1 << 30));
}

uintptr_t DefaultObject(const char* className) {
    const uintptr_t cls = FindObjectByName(className, "Class");
    return cls && ClassDefaultOff ? Read<uintptr_t>(cls + ClassDefaultOff) : 0;
}

// FText from a string (KismetTextLibrary.Conv_StringToText). The FText's 0x18 bytes are written to `out`, which
// then owns it.
bool MakeText(const wchar_t* text, uintptr_t out) {
    static uintptr_t fn = GetFunction("KismetTextLibrary", "Conv_StringToText");
    static uintptr_t lib = DefaultObject("KismetTextLibrary");
    if (!fn || !lib || !out)
        return false;
    static PropInfo in = FindProperty(fn, "InString");
    static PropInfo ret = FindProperty(fn, "ReturnValue");
    if (!in.found || !ret.found)
        return false;
    alignas(8) uint8_t params[0x80]{};
    FString s{ const_cast<wchar_t*>(text), (int32_t)wcslen(text) + 1, (int32_t)wcslen(text) + 1 };
    memcpy(params + in.offset, &s, sizeof(s));
    if (!ProcessEvent(lib, fn, params))
        return false;
    memcpy(reinterpret_cast<void*>(out), params + ret.offset, 0x18);
    return true;
}

uintptr_t SpawnObject(uintptr_t cls, uintptr_t outer) {
    static uintptr_t fn = GetFunction("GameplayStatics", "SpawnObject");
    static uintptr_t lib = DefaultObject("GameplayStatics");
    if (!fn || !lib)
        return 0;
    static PropInfo clsProp = FindProperty(fn, "ObjectClass");
    static PropInfo outerProp = FindProperty(fn, "Outer");
    static PropInfo ret = FindProperty(fn, "ReturnValue");
    if (!clsProp.found || !outerProp.found || !ret.found)
        return 0;
    alignas(8) uint8_t params[0x40]{};
    memcpy(params + clsProp.offset, &cls, 8);
    memcpy(params + outerProp.offset, &outer, 8);
    if (!ProcessEvent(lib, fn, params))
        return 0;
    uintptr_t obj = 0;
    memcpy(&obj, params + ret.offset, 8);
    return obj;
}

// The AthenaCharacter type's data in the asset manager, and where its "dynamic" flag is, read from
// AddDynamicAsset's own code like the client module does on 3.5: "lea rsi,[rcx+disp32]" is the type map and
// "mov byte ptr [rax+disp32],1" marks a type it creates as dynamic.
uintptr_t FindTypeData(uintptr_t assetManager, uintptr_t addDynamicAsset, uint32_t& dynamicOff) {
    dynamicOff = 0;
    uint32_t mapOff = 0, flagOff = 0;
    if (Readable(addDynamicAsset, 0x340)) {
        auto code = reinterpret_cast<const uint8_t*>(addDynamicAsset);
        for (int i = 0; i + 7 <= 0x100 && !mapOff; ++i)
            if (code[i] == 0x48 && code[i + 1] == 0x8D && code[i + 2] == 0xB1)
                mapOff = *reinterpret_cast<const uint32_t*>(code + i + 3);
        for (int i = 0; i + 7 <= 0x340 && !flagOff; ++i)
            if (code[i] == 0xC6 && code[i + 1] == 0x80 && code[i + 6] == 0x01)
                flagOff = *reinterpret_cast<const uint32_t*>(code + i + 2);
        if (mapOff > 0x1000) mapOff = 0;
        if (flagOff > 0x200) flagOff = 0;
    }
    printf("[outfits] from AddDynamicAsset: type map +0x%X, dynamic flag +0x%X\n", mapOff, flagOff);
    const FName typeName = MakeName(L"AthenaCharacter");
    uintptr_t typeData = 0;
    auto scan = [&](uint32_t off, uint32_t stride) {
        if (!Readable(assetManager + off, 16))
            return;
        const uintptr_t elements = Read<uintptr_t>(assetManager + off);
        const int32_t count = Read<int32_t>(assetManager + off + 8);
        if (!elements || count <= 0 || count > 4096 || !Readable(elements, size_t(count) * stride))
            return;
        for (int32_t i = 0; i < count && !typeData; ++i) {
            const uintptr_t element = elements + size_t(i) * stride;
            if (Read<int32_t>(element) != typeName.ComparisonIndex || Read<int32_t>(element + 4) != typeName.Number)
                continue;
            const uintptr_t data = Read<uintptr_t>(element + 8);
            if (Readable(data, 0xC0))
                typeData = data;
        }
    };
    if (mapOff)
        scan(mapOff, 0x20);
    for (uint32_t off = 0x28; off < 0x200 && !typeData; off += 8)
        for (uint32_t stride : { 0x20u, 0x18u, 0x28u, 0x30u }) {
            scan(off, stride);
            if (typeData)
                break;
        }
    if (!typeData)
        return 0;
    dynamicOff = flagOff;
    if (!dynamicOff)
        for (uint32_t off : { 0x80u, 0x88u, 0x78u, 0x90u }) {
            const int32_t assets = Read<int32_t>(typeData + off + 4);
            if (Read<uint8_t>(typeData + off) <= 1 && assets > 0 && assets < 20000) {
                dynamicOff = off;
                break;
            }
        }
    return dynamicOff ? typeData : 0;
}

void Register() {
    if (++g_tries > 240) {
        g_stage = -1;
        printf("[outfits] the engine never became ready; the custom outfits are not in the locker\n");
        return;
    }
    const uintptr_t engine = FindEngine();
    if (!engine)
        return;
    static PropInfo amProp = FindClassProp("Engine", "AssetManager");
    if (!amProp.found) {
        g_stage = -1;
        printf("[outfits] no AssetManager property on the engine\n");
        return;
    }
    const uintptr_t assetManager = Read<uintptr_t>(engine + amProp.offset);
    if (!assetManager)
        return; // not made yet: try again
    static uintptr_t addDynamicAsset = 0;
    if (!addDynamicAsset) {
        const uintptr_t ref = FindStringRef(L"AddDynamicAsset on %s called with conflicting path. Path %s is replacing path %s");
        addDynamicAsset = ref ? FunctionStart(ref) : 0;
        if (!addDynamicAsset) {
            g_stage = -1;
            printf("[outfits] UAssetManager::AddDynamicAsset not found\n");
            return;
        }
    }
    const uintptr_t package = FindObjectByName("/Engine/Transient", "Package");
    const uintptr_t itemClass = FindObjectByName("AthenaCharacterItemDefinition", "Class");
    if (!package || !itemClass) {
        g_stage = -1;
        printf("[outfits] transient package (%s) or AthenaCharacterItemDefinition class (%s) missing\n", package ? "ok" : "missing", itemClass ? "ok" : "missing");
        return;
    }
    const uintptr_t item = SpawnObject(itemClass, package);
    if (!item) {
        g_stage = -1;
        printf("[outfits] could not make the outfit object\n");
        return;
    }
    AddToRoot(item);
    static PropInfo displayName = FindClassProp("FortItemDefinition", "DisplayName");
    static PropInfo description = FindClassProp("FortItemDefinition", "Description");
    if (displayName.found)
        MakeText(kPhantomName, item + displayName.offset);
    if (description.found)
        MakeText(kPhantomDescription, item + description.offset);

    uint32_t dynamicOff = 0;
    const uintptr_t typeData = FindTypeData(assetManager, addDynamicAsset, dynamicOff);
    if (typeData)
        printf("[outfits] AthenaCharacter asset type found (dynamic flag at +0x%X)\n", dynamicOff);
    else
        printf("[outfits] AthenaCharacter asset type not found; trying anyway\n");
    const uint8_t wasDynamic = typeData ? Read<uint8_t>(typeData + dynamicOff) : 0;
    if (typeData)
        Write<uint8_t>(typeData + dynamicOff, 1);

    // FPrimaryAssetId { Type, Name } and, on UE 4.20, FSoftObjectPath { FName AssetPathName; FString SubPathString }.
    struct { FName type; FName name; } id{ MakeName(L"AthenaCharacter"), MakeName(kPhantomId) };
    const std::string objName = GetName(item);
    wchar_t path[256];
    swprintf_s(path, L"/Engine/Transient.%hs", objName.c_str());
    struct { FName assetPath; FString subPath; } soft{ MakeName(path), FString{} };
    alignas(8) uint8_t bundle[32]{};
    using AddDynamicAssetFn = bool (*)(uintptr_t, const void*, const void*, const void*);
    const bool ok = reinterpret_cast<AddDynamicAssetFn>(addDynamicAsset)(assetManager, &id, &soft, bundle);
    if (typeData)
        Write<uint8_t>(typeData + dynamicOff, wasDynamic);
    printf("[outfits] AthenaCharacter:CID_Syndicate_Phantom %s (%ls)\n", ok ? "registered" : "refused by the asset manager", path);
    if (!ok) {
        g_stage = -1;
        return;
    }
    g_item = item;
    g_stage = 1;
}

// Once The Reaper's outfit is loaded, the custom outfit gets its hero (the locker preview), rarity and icons.
void Fill() {
    const uintptr_t reaper = FindObjectByName(kPhantomBase, "AthenaCharacterItemDefinition");
    if (!reaper)
        return;
    static PropInfo hero = FindClassProp("AthenaCharacterItemDefinition", "HeroDefinition");
    static PropInfo rarity = FindClassProp("FortItemDefinition", "Rarity");
    if (hero.found)
        Write<uintptr_t>(g_item + hero.offset, Read<uintptr_t>(reaper + hero.offset));
    if (rarity.found)
        Write<uint8_t>(g_item + rarity.offset, Read<uint8_t>(reaper + rarity.offset));
    // TSoftObjectPtr (0x28 bytes on 4.20): copied only while its sub-path string is empty, so no memory is shared.
    for (const char* field : { "SmallPreviewImage", "LargePreviewImage" }) {
        const PropInfo p = FindClassProp("FortItemDefinition", field);
        if (!p.found || Read<uintptr_t>(reaper + p.offset + 0x18))
            continue;
        for (int i = 0; i < 0x28; i += 8)
            Write<uint64_t>(g_item + p.offset + i, Read<uint64_t>(reaper + p.offset + i));
    }
    g_stage = 2;
    printf("[outfits] Spitfire: Phantom has The Reaper's hero and icons (hero %s)\n", hero.found ? "set" : "missing");
}

// ---- Stage 2: the Phantom's own icon and its locker preview -----------------------------------------------------

struct PartRef {
    uint8_t type = 255;     // CustomCharacterPart.CharacterPartType: 0 head, 1 body, 2 hat, 3 backpack, 4 charm, 5 face
    FName path{};           // its soft path
    std::string name;
};

int g_extraTries = 0;
bool g_iconDone = false;
bool g_heroDone = false;
std::string g_reaperBodies[4]; // names of The Reaper's body parts (the white suit looks for them in a match)

// A TSoftObjectPtr is 0x28 bytes on 4.20: FWeakObjectPtr (8), TagAtLastTest (4 + 4), FSoftObjectPath { FName, FString }.
FName SoftPath(uintptr_t soft) { return Read<FName>(soft + 0x10); }

void WriteSoft(uintptr_t soft, FName path) {
    for (int i = 0; i < 0x28; i += 8)
        Write<uint64_t>(soft + i, 0);
    Write<FName>(soft + 0x10, path);
}

// The engine's StaticLoadObject, found like Erbium finds it: the function that uses the "STAT_LoadObject" string
// (or, on builds without it, the "Calling StaticLoadObject during PostLoad" warning). Season 3 has neither
// LoadAsset_Blocking nor ImportFileAsTexture2D, so this is how the menu loads assets. Game thread only.
uintptr_t FindStaticLoadObject() {
    uintptr_t fn = 0;
    if (const uintptr_t ref = FindStringRef(L"STAT_LoadObject")) {
        for (int i = 0; i < 400 && !fn; ++i) {
            const uint8_t* p = reinterpret_cast<const uint8_t*>(ref - i);
            if ((p[0] == 0x4C && p[1] == 0x89 && p[2] == 0x4C) || (p[0] == 0x48 && p[1] == 0x8B && p[2] == 0xC4))
                fn = ref - i;
        }
        if (!fn)
            fn = FunctionStart(ref);
    }
    if (!fn)
        if (const uintptr_t ref = FindStringRef(L"Calling StaticLoadObject during PostLoad may result in hitches during streaming."))
            fn = FunctionStart(ref);
    printf("[outfits] StaticLoadObject %s\n", fn ? "found" : "not found (only objects already loaded are used)");
    return fn;
}

uintptr_t StaticLoad(const wchar_t* path) {
    static uintptr_t fn = FindStaticLoadObject();
    static uintptr_t objectClass = FindObjectByName("Object", "Class");
    if (!fn || !objectClass || !path || !path[0])
        return 0;
    using StaticLoadObjectFn = uintptr_t (*)(uintptr_t, uintptr_t, const wchar_t*, const wchar_t*, uint32_t, void*, bool, void*);
    return reinterpret_cast<StaticLoadObjectFn>(fn)(objectClass, 0, path, nullptr, 0, nullptr, false, nullptr);
}

// The object behind a soft path: KismetSystemLibrary.LoadAsset_Blocking where the build has it, else the engine's
// StaticLoadObject, else whatever with that name is already in memory.
uintptr_t LoadSoftPath(FName path) {
    static uintptr_t fn = GetFunction("KismetSystemLibrary", "LoadAsset_Blocking");
    static uintptr_t lib = DefaultObject("KismetSystemLibrary");
    static PropInfo asset = fn ? FindProperty(fn, "Asset") : PropInfo{};
    static PropInfo ret = fn ? FindProperty(fn, "ReturnValue") : PropInfo{};
    static bool logged = false;
    if (!logged) {
        logged = true;
        printf("[outfits] LoadAsset_Blocking %s\n", fn && asset.found && ret.found ? "found" : "missing (only objects already loaded are used)");
    }
    if (fn && lib && asset.found && ret.found) {
        alignas(8) uint8_t params[0x60]{};
        memcpy(params + asset.offset + 0x10, &path, sizeof(path));
        ProcessEvent(lib, fn, params);
        uintptr_t out = 0;
        memcpy(&out, params + ret.offset, 8);
        if (out)
            return out;
    }
    const std::string full = NameToString(path);
    if (full.empty() || full == "None")
        return 0;
    if (const uintptr_t loaded = StaticLoad(std::wstring(full.begin(), full.end()).c_str()))
        return loaded;
    const size_t dot = full.rfind('.');
    return dot == std::string::npos ? 0 : FindObjectByName(full.substr(dot + 1).c_str());
}

// Every character part of a hero (its specializations' parts), with their types.
int HeroParts(uintptr_t hero, PartRef* out, int max) {
    static PropInfo specs = FindClassProp("FortHeroType", "Specializations");
    static PropInfo parts = FindClassProp("FortHeroSpecialization", "CharacterParts");
    static PropInfo partType = FindClassProp("CustomCharacterPart", "CharacterPartType");
    if (!hero || !specs.found || !parts.found || !partType.found)
        return 0;
    int count = 0;
    const TArrayPtr specArray = Read<TArrayPtr>(hero + specs.offset);
    for (int i = 0; specArray.Data && i < specArray.Num && i < 8; ++i) {
        const uintptr_t spec = LoadSoftPath(SoftPath(specArray.Data + 0x28ull * i));
        if (!spec)
            continue;
        const TArrayPtr partArray = Read<TArrayPtr>(spec + parts.offset);
        for (int j = 0; partArray.Data && j < partArray.Num && j < 8 && count < max; ++j) {
            const FName path = SoftPath(partArray.Data + 0x28ull * j);
            const uintptr_t part = LoadSoftPath(path);
            if (!part)
                continue;
            out[count].type = Read<uint8_t>(part + partType.offset);
            out[count].path = path;
            out[count].name = GetName(part);
            ++count;
        }
    }
    return count;
}

uintptr_t HeroOf(uintptr_t item) {
    static PropInfo hero = FindClassProp("AthenaCharacterItemDefinition", "HeroDefinition");
    return item && hero.found ? Read<uintptr_t>(item + hero.offset) : 0;
}

// A TArray of soft pointers the game owns (FMemory), so it can free it like its own.
bool WriteSoftArray(uintptr_t field, const FName* paths, int count) {
    if (!FMemoryReallocFn || count <= 0)
        return false;
    const uintptr_t data = reinterpret_cast<uintptr_t>(FMemoryReallocFn(nullptr, 0x28ll * count, 0));
    if (!data)
        return false;
    for (int i = 0; i < count; ++i)
        WriteSoft(data + 0x28ull * i, paths[i]);
    Write<uintptr_t>(field, data);
    Write<int32_t>(field + 8, count);
    Write<int32_t>(field + 12, count);
    return true;
}

FName TransientPath(uintptr_t obj) {
    wchar_t path[256];
    swprintf_s(path, L"/Engine/Transient.%hs", GetName(obj).c_str());
    return MakeName(path);
}

// The copy's FText (bytes copied from the original) takes its own reference on the shared text data: an FText is
// a TSharedRef<ITextData> (object, reference controller) plus flags, and the controller's shared count is at +8.
void ShareText(uintptr_t text) {
    const uintptr_t data = Read<uintptr_t>(text);
    const uintptr_t controller = Read<uintptr_t>(text + 8);
    if (data && controller) {
        _InterlockedIncrement(reinterpret_cast<volatile long*>(controller + 8));
        return;
    }
    for (int i = 0; i < 0x18; i += 8)
        Write<uint64_t>(text + i, 0);
    MakeText(L"", text);
}

// A copy of a loaded asset object in the transient package: the property bytes are copied, and every container
// the copy would share with the original (arrays, strings, maps, sets, delegates, tag containers, soft pointers
// with a sub-path) is emptied, so the two never free the same memory; texts get their own reference (ShareText).
// Kept alive for the whole game.
uintptr_t CloneShallow(uintptr_t source, uintptr_t package) {
    const uintptr_t cls = source ? ObjClass(source) : 0;
    const int size = cls && PropertiesSizeOff ? Read<int32_t>(cls + PropertiesSizeOff) : 0;
    if (!cls || size <= 0x28 || size > 0x4000)
        return 0;
    const uintptr_t copy = SpawnObject(cls, package);
    if (!copy)
        return 0;
    AddToRoot(copy);
    for (int i = 0x28; i + 8 <= size; i += 8)
        Write<uint64_t>(copy + i, Read<uint64_t>(source + i));
    for (int i = size & ~7; i < size; ++i)
        Write<uint8_t>(copy + i, Read<uint8_t>(source + i));
    for (uintptr_t st = cls; st; st = SuperStructOff ? Read<uintptr_t>(st + SuperStructOff) : 0) {
        for (uintptr_t prop = Read<uintptr_t>(st + ChildrenOff); prop; prop = Read<uintptr_t>(prop + FieldNextOff)) {
            const std::string kind = ObjClassName(prop);
            if (kind.find("Property") == std::string::npos)
                continue; // a function
            const int at = Read<int32_t>(prop + OffsetInternalOff);
            if (at < 0x28 || at >= size)
                continue;
            int clear = 0;
            if (kind == "ArrayProperty" || kind == "StrProperty" || kind == "MulticastDelegateProperty")
                clear = 0x10;
            else if (kind == "TextProperty") {
                // An all-zero FText has no text data, and the game crashes reading it (the S3 lobby crashed at
                // exe+0x12A0B77 on the hero's texts). Give the copy its own reference to the same text instead.
                ShareText(copy + at);
                continue;
            }
            else if (kind == "MapProperty" || kind == "SetProperty")
                clear = 0x50;
            else if ((kind == "SoftObjectProperty" || kind == "SoftClassProperty") && Read<uintptr_t>(copy + at + 0x18))
                clear = 0x28;
            else if (kind == "StructProperty" && Contains(GetName(prop), "tag"))
                clear = 0x20; // FGameplayTagContainer: two arrays
            for (int i = 0; i < clear && at + i + 8 <= size; i += 8)
                Write<uint64_t>(copy + at + i, 0);
        }
    }
    return copy;
}

// Makes a soft pointer resolve straight to a live object (its weak pointer: GObjects index and serial number).
void PointSoftAt(uintptr_t soft, uintptr_t obj) {
    if (!soft || !obj || !objectArray)
        return;
    const int32_t index = Read<int32_t>(obj + 0xC);
    uintptr_t item = 0;
    if (gobjectsChunked) {
        const uintptr_t chunks = Read<uintptr_t>(objectArray);
        const uintptr_t chunk = chunks ? Read<uintptr_t>(chunks + 8ull * (index / 0x10000)) : 0;
        item = chunk ? chunk + 0x18ull * (index % 0x10000) : 0;
    } else {
        const uintptr_t items = Read<uintptr_t>(objectArray);
        item = items ? items + 0x18ull * index : 0;
    }
    const int32_t serial = item && Read<uintptr_t>(item) == obj ? Read<int32_t>(item + 0x10) : 0;
    if (serial) {
        Write<int32_t>(soft, index);
        Write<int32_t>(soft + 4, serial);
    }
}

// The locker preview: a hero of our own with The Reaper's body and the default Spitfire's head (BLK_Sydney), so the
// lobby shows the same character as the match.
bool MakePhantomHero() {
    const uintptr_t reaper = FindObjectByName(kPhantomBase, "AthenaCharacterItemDefinition");
    PartRef reaperParts[12], spitfireParts[12];
    const int reaperCount = HeroParts(HeroOf(reaper), reaperParts, 12);
    int bodies = 0;
    for (int i = 0; i < reaperCount; ++i)
        if (reaperParts[i].type == 1 && bodies < 4)
            g_reaperBodies[bodies++] = reaperParts[i].name;
    int spitfireCount = 0;
    for (int n : { 6, 5, 7, 8 }) {
        wchar_t path[200];
        swprintf_s(path, L"/Game/Athena/Items/Cosmetics/Characters/CID_%03d_Athena_Commando_M_Default.CID_%03d_Athena_Commando_M_Default", n, n);
        PartRef parts[12];
        const int count = HeroParts(HeroOf(LoadSoftPath(MakeName(path))), parts, 12);
        bool sydney = false;
        for (int i = 0; i < count; ++i)
            if (parts[i].type == 0 && Contains(parts[i].name, "sydney"))
                sydney = true;
        if (sydney) {
            for (int i = 0; i < count; ++i)
                spitfireParts[i] = parts[i];
            spitfireCount = count;
            break;
        }
    }
    static int lastReaper = -1, lastSpitfire = -1;
    if (reaperCount != lastReaper || spitfireCount != lastSpitfire) {
        lastReaper = reaperCount;
        lastSpitfire = spitfireCount;
        printf("[outfits] preview: The Reaper has %d parts, Spitfire %d\n", reaperCount, spitfireCount);
    }
    if (!bodies || !spitfireCount)
        return false;
    FName paths[12];
    int count = 0;
    for (int i = 0; i < reaperCount && count < 12; ++i)
        if (reaperParts[i].type != 0 && reaperParts[i].type != 2 && reaperParts[i].type != 5)
            paths[count++] = reaperParts[i].path;
    for (int i = 0; i < spitfireCount && count < 12; ++i)
        if (spitfireParts[i].type == 0 || spitfireParts[i].type == 2 || spitfireParts[i].type == 5) {
            paths[count++] = spitfireParts[i].path;
            printf("[outfits] preview: Spitfire part %s\n", spitfireParts[i].name.c_str());
        }

    const uintptr_t package = FindObjectByName("/Engine/Transient", "Package");
    static PropInfo specParts = FindClassProp("FortHeroSpecialization", "CharacterParts");
    static PropInfo heroSpecs = FindClassProp("FortHeroType", "Specializations");
    static PropInfo heroProp = FindClassProp("AthenaCharacterItemDefinition", "HeroDefinition");
    const uintptr_t reaperHero = HeroOf(reaper);
    const TArrayPtr reaperSpecs = reaperHero && heroSpecs.found ? Read<TArrayPtr>(reaperHero + heroSpecs.offset) : TArrayPtr{};
    const uintptr_t reaperSpec = reaperSpecs.Data && reaperSpecs.Num > 0 ? LoadSoftPath(SoftPath(reaperSpecs.Data)) : 0;
    if (!package || !reaperHero || !reaperSpec || !specParts.found || !heroSpecs.found || !heroProp.found)
        return false;
    // Copies of The Reaper's own hero and specialization (icons, gender, body type and the rest the locker and lobby
    // read), with only the parts changed. A fresh, empty hero (the first version) left a "?" icon and an empty pad.
    const uintptr_t spec = CloneShallow(reaperSpec, package);
    const uintptr_t hero = spec ? CloneShallow(reaperHero, package) : 0;
    if (!hero)
        return false;
    FName specPath = TransientPath(spec);
    if (!WriteSoftArray(spec + specParts.offset, paths, count) || !WriteSoftArray(hero + heroSpecs.offset, &specPath, 1))
        return false;
    PointSoftAt(Read<uintptr_t>(hero + heroSpecs.offset), spec);
    Write<uintptr_t>(g_item + heroProp.offset, hero);
    printf("[outfits] preview: Spitfire: Phantom has its own copy of The Reaper's hero (%d parts: The Reaper's suit, Spitfire's head)\n", count);
    return true;
}

// The locker icon: Runtime\dlls\syndicate-phantom-icon.png next to this DLL, made into a texture by the game.
bool LoadPhantomIcon() {
    static uintptr_t fn = GetFunction("KismetRenderingLibrary", "ImportFileAsTexture2D");
    static uintptr_t lib = DefaultObject("KismetRenderingLibrary");
    if (!fn || !lib) {
        printf("[outfits] icon: this build has no ImportFileAsTexture2D; the icon is downloaded instead\n");
        return true;
    }
    if (!world)
        return false;
    static wchar_t file[MAX_PATH]{};
    if (!file[0]) {
        HMODULE self = nullptr;
        GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCWSTR>(&LoadPhantomIcon), &self);
        GetModuleFileNameW(self, file, MAX_PATH);
        if (wchar_t* slash = wcsrchr(file, L'\\'))
            wcscpy_s(slash + 1, MAX_PATH - (slash + 1 - file), L"syndicate-phantom-icon.png");
    }
    if (GetFileAttributesW(file) == INVALID_FILE_ATTRIBUTES) {
        printf("[outfits] icon: %ls is missing; the locker keeps The Reaper's icon\n", file);
        return true;
    }
    static PropInfo ctx = FindProperty(fn, "WorldContextObject");
    static PropInfo name = FindProperty(fn, "Filename");
    static PropInfo ret = FindProperty(fn, "ReturnValue");
    if (!ctx.found || !name.found || !ret.found)
        return true;
    alignas(8) uint8_t params[0x40]{};
    const FString text{ file, (int32_t)wcslen(file) + 1, (int32_t)wcslen(file) + 1 };
    memcpy(params + ctx.offset, &world, 8);
    memcpy(params + name.offset, &text, sizeof(text));
    ProcessEvent(lib, fn, params);
    uintptr_t texture = 0;
    memcpy(&texture, params + ret.offset, 8);
    if (!texture) {
        printf("[outfits] icon: the game could not read %ls\n", file);
        return true;
    }
    AddToRoot(texture);
    const FName path = TransientPath(texture);
    for (const char* field : { "SmallPreviewImage", "LargePreviewImage" }) {
        const PropInfo p = FindClassProp("FortItemDefinition", field);
        if (p.found)
            WriteSoft(g_item + p.offset, path);
    }
    printf("[outfits] icon: Spitfire: Phantom uses its own icon (%s)\n", GetName(texture).c_str());
    return true;
}

void Extras() {
    if (!g_iconDone)
        g_iconDone = LoadPhantomIcon();
    if (!g_heroDone)
        g_heroDone = MakePhantomHero();
    if ((g_iconDone && g_heroDone) || ++g_extraTries >= 30) {
        if (!g_heroDone)
            printf("[outfits] preview: gave up; the locker shows The Reaper's head\n");
        g_stage = 3;
    }
}

void TickInner() {
    if (g_stage == 0) {
        if (!Season3()) {
            g_stage = -1;
            return;
        }
        if (GetTickCount64() < g_nextTry)
            return;
        g_nextTry = GetTickCount64() + 500;
        Register();
    } else if (g_stage == 1) {
        if (GetTickCount64() < g_nextTry)
            return;
        g_nextTry = GetTickCount64() + 2000;
        Fill();
    } else if (g_stage == 2) {
        if (GetTickCount64() < g_nextTry)
            return;
        g_nextTry = GetTickCount64() + 2000;
        Extras();
    }
}

// ---- Stage 2: the white suit --------------------------------------------------------------------------------

struct PhantomLook {
    uintptr_t mesh = 0;           // the body mesh last painted
    uintptr_t mids[16]{};         // our white instances, one per material slot (0 = slot left alone)
    uint32_t keptLogged = 0;      // slots already logged as kept
    ULONGLONG next = 0;           // next check
    char lastParts[200]{};        // for the log: only logged when the parts change
};
PhantomLook g_look;

// The six character parts on a player state (head, body, hat, backpack, charm, face). Newer builds keep them inside
// a CustomCharacterParts struct; 3.5 has the plain array.
bool ReadParts(uintptr_t state, uintptr_t (&parts)[6]) {
    static int offset = [] {
        const PropInfo p = FindClassProp("FortPlayerStateAthena", "CharacterParts");
        if (!p.found)
            return -1;
        if (const uintptr_t inner = FindObjectByName("CustomCharacterParts", "ScriptStruct")) {
            const PropInfo q = FindProperty(inner, "Parts");
            if (q.found)
                return (int)(p.offset + q.offset);
        }
        return (int)p.offset;
    }();
    if (offset < 0 || !state)
        return false;
    for (int i = 0; i < 6; ++i)
        parts[i] = Read<uintptr_t>(state + offset + 8 * i);
    return true;
}

uintptr_t GetMeshMaterial(uintptr_t mesh, int slot) {
    static uintptr_t fn = GetFunction("PrimitiveComponent", "GetMaterial");
    static PropInfo index = FindProperty(fn, "ElementIndex");
    static PropInfo ret = FindProperty(fn, "ReturnValue");
    if (!fn || !index.found || !ret.found)
        return 0;
    alignas(8) uint8_t params[0x20]{};
    memcpy(params + index.offset, &slot, 4);
    ProcessEvent(mesh, fn, params);
    uintptr_t out = 0;
    memcpy(&out, params + ret.offset, 8);
    return out;
}

int GetMeshMaterialCount(uintptr_t mesh) {
    static uintptr_t fn = GetFunction("PrimitiveComponent", "GetNumMaterials");
    static PropInfo ret = FindProperty(fn, "ReturnValue");
    if (!fn || !ret.found)
        return 0;
    alignas(8) uint8_t params[0x10]{};
    ProcessEvent(mesh, fn, params);
    int32_t count = 0;
    memcpy(&count, params + ret.offset, 4);
    return count < 0 ? 0 : (count > 16 ? 16 : count);
}

void SetMeshMaterialSlot(uintptr_t mesh, int slot, uintptr_t material) {
    static uintptr_t fn = GetFunction("PrimitiveComponent", "SetMaterial");
    static PropInfo index = FindProperty(fn, "ElementIndex");
    static PropInfo mat = FindProperty(fn, "Material");
    if (!fn || !index.found || !mat.found)
        return;
    alignas(8) uint8_t params[0x20]{};
    memcpy(params + index.offset, &slot, 4);
    memcpy(params + mat.offset, &material, 8);
    ProcessEvent(mesh, fn, params);
}

uintptr_t MakeInstance(uintptr_t parent) {
    static uintptr_t fn = GetFunction("KismetMaterialLibrary", "CreateDynamicMaterialInstance");
    static uintptr_t lib = DefaultObject("KismetMaterialLibrary");
    static PropInfo ctx = FindProperty(fn, "WorldContextObject");
    static PropInfo par = FindProperty(fn, "Parent");
    static PropInfo ret = FindProperty(fn, "ReturnValue");
    if (!fn || !lib || !ctx.found || !par.found || !ret.found || !world)
        return 0;
    alignas(8) uint8_t params[0x40]{};
    memcpy(params + ctx.offset, &world, 8);
    memcpy(params + par.offset, &parent, 8);
    ProcessEvent(lib, fn, params);
    uintptr_t out = 0;
    memcpy(&out, params + ret.offset, 8);
    return out;
}

void SetTextureParam(uintptr_t mid, const wchar_t* name, uintptr_t texture) {
    static uintptr_t fn = GetFunction("MaterialInstanceDynamic", "SetTextureParameterValue");
    static PropInfo pname = FindProperty(fn, "ParameterName");
    static PropInfo value = FindProperty(fn, "Value");
    if (!fn || !pname.found || !value.found || !texture)
        return;
    alignas(8) uint8_t params[0x20]{};
    const FName n = MakeName(name);
    memcpy(params + pname.offset, &n, sizeof(n));
    memcpy(params + value.offset, &texture, 8);
    ProcessEvent(mid, fn, params);
}

void SetColorParam(uintptr_t mid, const wchar_t* name, float r, float g, float b) {
    static uintptr_t fn = GetFunction("MaterialInstanceDynamic", "SetVectorParameterValue");
    static PropInfo pname = FindProperty(fn, "ParameterName");
    static PropInfo value = FindProperty(fn, "Value");
    if (!fn || !pname.found || !value.found)
        return;
    alignas(8) uint8_t params[0x30]{};
    const FName n = MakeName(name);
    const float color[4] = { r, g, b, 1.f };
    memcpy(params + pname.offset, &n, sizeof(n));
    memcpy(params + value.offset, color, sizeof(color));
    ProcessEvent(mid, fn, params);
}

// MaterialInstance.TextureParameterValues / VectorParameterValues: the parameters an instance sets, each starting
// with its FMaterialParameterInfo (whose first field is the name).
uintptr_t ParentMaterial(uintptr_t material) {
    static uintptr_t instanceClass = FindObjectByName("MaterialInstance", "Class");
    static PropInfo parent = FindClassProp("MaterialInstance", "Parent");
    return instanceClass && parent.found && IsA(material, instanceClass) ? Read<uintptr_t>(material + parent.offset) : 0;
}

int StructSize(const char* name) {
    const uintptr_t st = FindObjectByName(name, "ScriptStruct");
    return st && PropertiesSizeOff ? Read<int32_t>(st + PropertiesSizeOff) : 0;
}

void WhitenNamedParams(uintptr_t mid, uintptr_t material, uintptr_t white) {
    static uintptr_t instanceClass = FindObjectByName("MaterialInstance", "Class");
    static PropInfo textures = FindClassProp("MaterialInstance", "TextureParameterValues");
    static PropInfo vectors = FindClassProp("MaterialInstance", "VectorParameterValues");
    static int textureSize = StructSize("TextureParameterValue");
    static int vectorSize = StructSize("VectorParameterValue");
    if (!material || !instanceClass || !IsA(material, instanceClass))
        return;
    std::string names;
    if (textures.found && textureSize > 8 && textureSize < 0x100) {
        const TArrayPtr list = Read<TArrayPtr>(material + textures.offset);
        for (int i = 0; list.Data && i < list.Num && i < 64; ++i) {
            const FName name = Read<FName>(list.Data + uintptr_t(textureSize) * i);
            const std::string text = NameToString(name);
            names += " tex:" + text;
            const bool colour = Contains(text, "diffuse") || Contains(text, "base") || Contains(text, "albedo") || Contains(text, "color");
            const bool other = Contains(text, "normal") || Contains(text, "mask") || Contains(text, "rough") || Contains(text, "spec") ||
                               Contains(text, "emissive") || Contains(text, "_ao") || Contains(text, "occlusion");
            if (white && colour && !other)
                SetTextureParam(mid, std::wstring(text.begin(), text.end()).c_str(), white);
        }
    }
    if (vectors.found && vectorSize > 8 && vectorSize < 0x100) {
        const TArrayPtr list = Read<TArrayPtr>(material + vectors.offset);
        for (int i = 0; list.Data && i < list.Num && i < 64; ++i) {
            const FName name = Read<FName>(list.Data + uintptr_t(vectorSize) * i);
            const std::string text = NameToString(name);
            names += " vec:" + text;
            if ((Contains(text, "color") || Contains(text, "tint") || Contains(text, "diffuse")) &&
                !Contains(text, "emissive") && !Contains(text, "fresnel") && !Contains(text, "spec"))
                SetColorParam(mid, std::wstring(text.begin(), text.end()).c_str(), 0.92f, 0.92f, 0.95f);
        }
    }
    printf("[outfits] look: %s params:%s\n", GetName(material).c_str(), names.empty() ? " none" : names.c_str());
}

uintptr_t SuitTexture();

// A white version of one of the suit's materials: same shading and folds (normal map), white base colour.
uintptr_t WhiteInstance(uintptr_t parent) {
    const uintptr_t mid = MakeInstance(parent);
    if (!mid)
        return 0;
    const uintptr_t white = SuitTexture();
    for (const wchar_t* tex : { L"Diffuse", L"BaseColor", L"Base Color", L"Albedo", L"Diffuse Texture", L"DiffuseTexture" })
        SetTextureParam(mid, tex, white);
    for (const wchar_t* tint : { L"Diffuse Color", L"DiffuseColor", L"Tint", L"Color", L"BaseColorTint", L"Base Color Tint", L"Diffuse Tint" })
        SetColorParam(mid, tint, 0.92f, 0.92f, 0.95f);
    // The names the suit's own material instances use (logged once per material): every colour texture gets the
    // white one and every colour or tint vector gets white, whatever the artists named them.
    for (uintptr_t m = parent, depth = 0; m && depth < 4; m = ParentMaterial(m), ++depth)
        WhitenNamedParams(mid, m, white);
    return mid;
}

// The suit's colour: a small render target cleared to a soft off-white. The engine's pure white texture made the
// suit glow in the lobby lights. Falls back to that white texture if a render target can't be made.
uintptr_t SuitTexture() {
    static uintptr_t texture = 0;
    static bool tried = false;
    if (texture || tried || !world)
        return texture;
    // The render target's owner is its world context object. With the lobby world as owner, the rooted target kept
    // the lobby world alive into the match and the game crashed loading the map (write to 0x1F0). The game instance
    // lives for the whole game, so it owns the target instead; without one, no target is made.
    const uintptr_t owner = off.OwningGameInstance ? Read<uintptr_t>(world + off.OwningGameInstance) : 0;
    if (!owner)
        return 0;
    tried = true;
    static uintptr_t lib = DefaultObject("KismetRenderingLibrary");
    const uintptr_t create = GetFunction("KismetRenderingLibrary", "CreateRenderTarget2D");
    const uintptr_t clear = GetFunction("KismetRenderingLibrary", "ClearRenderTarget2D");
    if (lib && create && clear) {
        const PropInfo ctx = FindProperty(create, "WorldContextObject");
        const PropInfo width = FindProperty(create, "Width");
        const PropInfo height = FindProperty(create, "Height");
        const PropInfo format = FindProperty(create, "Format");
        const PropInfo ret = FindProperty(create, "ReturnValue");
        if (ctx.found && width.found && height.found && ret.found) {
            alignas(8) uint8_t params[0x40]{};
            const int32_t size = 4;
            memcpy(params + ctx.offset, &owner, 8);
            memcpy(params + width.offset, &size, 4);
            memcpy(params + height.offset, &size, 4);
            if (format.found)
                params[format.offset] = 5; // RTF_RGBA16f (linear)
            ProcessEvent(lib, create, params);
            uintptr_t target = 0;
            memcpy(&target, params + ret.offset, 8);
            const PropInfo cctx = FindProperty(clear, "WorldContextObject");
            const PropInfo ctarget = FindProperty(clear, "TextureRenderTarget");
            const PropInfo color = FindProperty(clear, "ClearColor");
            if (target && cctx.found && ctarget.found && color.found) {
                AddToRoot(target);
                alignas(8) uint8_t cp[0x40]{};
                const float c[4] = { 0.50f, 0.50f, 0.52f, 1.f }; // linear: a soft off-white, not a glowing one
                memcpy(cp + cctx.offset, &owner, 8);
                memcpy(cp + ctarget.offset, &target, 8);
                memcpy(cp + color.offset, c, sizeof(c));
                ProcessEvent(lib, clear, cp);
                texture = target;
            }
        }
    }
    if (!texture) {
        texture = FindObjectByName("WhiteSquareTexture", "Texture2D");
        if (!texture)
            texture = FindObjectByName("DefaultWhite", "Texture2D");
    }
    printf("[outfits] look: suit colour %s\n", !texture ? "missing" : (ObjClassName(texture) == "Texture2D" ? "engine white (render target failed)" : "soft white render target"));
    return texture;
}

// The game's dynamic instance of the suit painted white: the colour texture only, so its vector values (the skin tone
// the head sets for the hands) stay as they are.
void WhitenInPlace(uintptr_t mid) {
    const uintptr_t white = SuitTexture();
    if (!white)
        return;
    for (const wchar_t* tex : { L"Diffuse", L"BaseColor", L"Base Color", L"Albedo", L"Diffuse Texture", L"DiffuseTexture" })
        SetTextureParam(mid, tex, white);
}

// The pawn's component that draws a character part: CharacterPartSkeletalMeshComponents (a TArray or a fixed array,
// depending on the build), matched by the part's SkeletalMesh asset name.
uintptr_t BodyPartComponent(uintptr_t pawn, uintptr_t part) {
    static PropInfo comps = FindClassProp("FortPlayerPawn", "CharacterPartSkeletalMeshComponents");
    static PropInfo partMesh = FindClassProp("CustomCharacterPart", "SkeletalMesh");
    static PropInfo skelMesh = FindClassProp("SkinnedMeshComponent", "SkeletalMesh");
    static uintptr_t meshClass = FindObjectByName("SkeletalMeshComponent", "Class");
    static int isArray = [] {
        const uintptr_t cls = FindObjectByName("FortPlayerPawn", "Class");
        for (uintptr_t child = cls ? Read<uintptr_t>(cls + ChildrenOff) : 0; child; child = Read<uintptr_t>(child + FieldNextOff))
            if (GetName(child) == "CharacterPartSkeletalMeshComponents")
                return ObjClassName(child).find("Array") != std::string::npos ? 1 : 0;
        return 1;
    }();
    if (!pawn || !part || !comps.found || !partMesh.found || !skelMesh.found || !meshClass)
        return 0;
    const std::string path = NameToString(SoftPath(part + partMesh.offset));
    const size_t dot = path.rfind('.');
    const std::string want = dot == std::string::npos ? path : path.substr(dot + 1);
    uintptr_t list[8]{};
    int count = 0;
    if (isArray) {
        const TArrayPtr a = Read<TArrayPtr>(pawn + comps.offset);
        for (int i = 0; a.Data && i < a.Num && i < 8; ++i)
            list[count++] = Read<uintptr_t>(a.Data + 8ull * i);
    } else {
        for (int i = 0; i < 6; ++i)
            list[count++] = Read<uintptr_t>(pawn + comps.offset + 8 * i);
    }
    uintptr_t fallback = 0;
    for (int i = 0; i < count; ++i) {
        if (!list[i] || !IsA(list[i], meshClass))
            continue;
        const uintptr_t asset = Read<uintptr_t>(list[i] + skelMesh.offset);
        if (asset && !want.empty() && GetName(asset) == want)
            return list[i];
        if (!isArray && i == 1)
            fallback = list[i]; // fixed array: slot 1 is the body
    }
    static bool logged = false;
    if (!logged) {
        logged = true;
        printf("[outfits] look: no part component draws %s (%d components)\n", want.c_str(), count);
    }
    return fallback;
}

void PaintPhantomBody(uintptr_t mesh) {
    if (mesh != g_look.mesh) {
        memset(g_look.mids, 0, sizeof(g_look.mids));
        g_look.keptLogged = 0;
        g_look.mesh = mesh;
    }
    const int count = GetMeshMaterialCount(mesh);
    int painted = 0;
    for (int slot = 0; slot < count; ++slot) {
        const uintptr_t current = GetMeshMaterial(mesh, slot);
        if (!current)
            continue;
        if (current == g_look.mids[slot]) {
            ++painted;
            continue;
        }
        const std::string name = GetName(current);
        // Skin, eyes and hair keep their own colours (Spitfire's skin tone comes with his head).
        if (Contains(name, "skin") || Contains(name, "eye") || Contains(name, "hair") || Contains(name, "head")) {
            if (!(g_look.keptLogged & (1u << slot)))
                printf("[outfits] look: slot %d %s kept\n", slot, name.c_str());
            g_look.keptLogged |= 1u << slot;
            continue;
        }
        if (g_look.mids[slot] && current != g_look.mids[slot]) {
            // The game put the original back (respawn, part refresh): reuse our instance.
            SetMeshMaterialSlot(mesh, slot, g_look.mids[slot]);
            ++painted;
            continue;
        }
        // The game's own dynamic instance carries Spitfire's skin tone (set from the head) for the hands, which
        // share the suit's one material. A new instance made from it loses those values and the hands turned white,
        // so it is painted in place: only the colour texture is swapped, the skin tone values stay.
        static uintptr_t dynamicClass = FindObjectByName("MaterialInstanceDynamic", "Class");
        if (dynamicClass && IsA(current, dynamicClass)) {
            WhitenInPlace(current);
            g_look.mids[slot] = current;
            printf("[outfits] look: slot %d %s painted white in place (skin tone kept)\n", slot, name.c_str());
            ++painted;
            continue;
        }
        const uintptr_t mid = WhiteInstance(current);
        if (!mid) {
            printf("[outfits] look: slot %d %s: could not make a material instance\n", slot, name.c_str());
            continue;
        }
        AddToRoot(mid);
        g_look.mids[slot] = mid;
        SetMeshMaterialSlot(mesh, slot, mid);
        printf("[outfits] look: slot %d %s painted white\n", slot, name.c_str());
        ++painted;
    }
    FeatureStatus("phantom", "white suit on %d of %d body materials", painted, count);
}

// ---- Custom skins pilot: what a new outfit must match ------------------------------------------------------------
// Once per outfit you wear in a Season 3 match, "[skins]" lines list your character parts by full path, the body
// mesh, its skeleton and anim class, and the CustomCharacterPart fields. A skin built in Unreal 4.20 has to use that
// skeleton path so the game's own one is used at run time.

std::string FullPath(uintptr_t obj) {
    std::string path;
    for (int depth = 0; obj && depth < 8; ++depth) {
        const uintptr_t outer = ObjOuter(obj);
        const std::string name = GetName(obj);
        path = path.empty() ? name : name + (outer && !ObjOuter(outer) ? "." : ":") + path;
        obj = outer;
    }
    return path;
}

std::string g_skinsLogged;

void LogSkinDetails(const uintptr_t (&parts)[6], const std::string& key) {
    if (key == g_skinsLogged)
        return;
    g_skinsLogged = key;
    static const char* const slots[6] = { "head", "body", "hat", "backpack", "charm", "face" };
    for (int i = 0; i < 6; ++i)
        if (parts[i])
            printf("[skins] part %s: %s (%s)\n", slots[i], FullPath(parts[i]).c_str(), ObjClassName(parts[i]).c_str());
    static PropInfo skelMesh = FindClassProp("SkinnedMeshComponent", "SkeletalMesh");
    static PropInfo skeleton = FindClassProp("SkeletalMesh", "Skeleton");
    static PropInfo animClass = FindClassProp("SkeletalMeshComponent", "AnimClass");
    const uintptr_t mesh = Read<uintptr_t>(localPawn + off.Mesh);
    const uintptr_t asset = mesh && skelMesh.found ? Read<uintptr_t>(mesh + skelMesh.offset) : 0;
    const uintptr_t skel = asset && skeleton.found ? Read<uintptr_t>(asset + skeleton.offset) : 0;
    const uintptr_t anim = mesh && animClass.found ? Read<uintptr_t>(mesh + animClass.offset) : 0;
    printf("[skins] body mesh %s\n", asset ? FullPath(asset).c_str() : "none");
    printf("[skins] skeleton %s\n", skel ? FullPath(skel).c_str() : "none");
    printf("[skins] anim class %s\n", anim ? FullPath(anim).c_str() : "none");
    static bool fieldsLogged = false;
    if (!fieldsLogged && parts[1]) {
        fieldsLogged = true;
        static const char* const keys[] = { "Mesh", "Skeletal", "Material", "Anim", "Body", "Gender", "Type", "Additional", "Override", "Socket", "Skin" };
        LogStructFields("[skins] part field", ObjClass(parts[1]), keys, (int)(sizeof(keys) / sizeof(keys[0])));
        const uintptr_t data = FindObjectByName("CustomCharacterPartData", "Class");
        if (data)
            LogStructFields("[skins] part data field", data, keys, (int)(sizeof(keys) / sizeof(keys[0])));
    }
}

// The locker shows your outfit on a preview actor of its own, not on your pawn, so it stayed black. Every 2 s while
// you wear the Phantom, other skeletal mesh components that draw The Reaper's suit next to Spitfire's head (same
// actor) get the same in-place white paint. Only that pair matches, so a real Reaper outfit is never touched.
uintptr_t ObjectAt(int index) {
    if (!objectArray || index < 0)
        return 0;
    uintptr_t item = 0;
    if (gobjectsChunked) {
        const uintptr_t chunks = Read<uintptr_t>(objectArray);
        const uintptr_t chunk = chunks ? Read<uintptr_t>(chunks + 8ull * (index / 0x10000)) : 0;
        item = chunk ? chunk + 0x18ull * (index % 0x10000) : 0;
    } else {
        const uintptr_t items = Read<uintptr_t>(objectArray);
        item = items ? items + 0x18ull * index : 0;
    }
    return item ? Read<uintptr_t>(item) : 0;
}

int ObjectCount() {
    if (!objectArray)
        return 0;
    return Read<int32_t>(objectArray + (gobjectsChunked ? 0x14 : 0xC));
}

bool InFrontend() {
    return world && Contains(GetName(world), "frontend");
}

void PaintPhantomPreviews(uintptr_t own) {
    static ULONGLONG next = 0;
    // Only in the lobby (the locker preview lives there); never while a match loads or runs.
    if (GetTickCount64() < next || !InFrontend())
        return;
    next = GetTickCount64() + 2000;
    static uintptr_t meshClass = FindObjectByName("SkeletalMeshComponent", "Class");
    static uintptr_t dynamicClass = FindObjectByName("MaterialInstanceDynamic", "Class");
    static PropInfo skelMesh = FindClassProp("SkinnedMeshComponent", "SkeletalMesh");
    if (!meshClass || !dynamicClass || !skelMesh.found)
        return;
    const uintptr_t suitAsset = own ? Read<uintptr_t>(own + skelMesh.offset) : 0;
    if (!suitAsset)
        return;
    uintptr_t suits[16]{}, owners[16]{};
    int suitCount = 0;
    uintptr_t heads[32]{};
    int headCount = 0;
    const int count = ObjectCount();
    for (int i = 0; i < count && i < 2000000; ++i) {
        const uintptr_t obj = ObjectAt(i);
        if (!obj || obj == own || !IsA(obj, meshClass))
            continue;
        const uintptr_t asset = Read<uintptr_t>(obj + skelMesh.offset);
        if (!asset)
            continue;
        if (asset == suitAsset && suitCount < 16) {
            suits[suitCount] = obj;
            owners[suitCount++] = ObjOuter(obj);
        } else if (headCount < 32 && Contains(GetName(asset), "sydney")) {
            heads[headCount++] = ObjOuter(obj);
        }
    }
    int painted = 0;
    for (int i = 0; i < suitCount; ++i) {
        bool withHead = false;
        for (int h = 0; h < headCount && !withHead; ++h)
            withHead = owners[i] && heads[h] == owners[i];
        if (!withHead)
            continue;
        const int slots = GetMeshMaterialCount(suits[i]);
        for (int slot = 0; slot < slots && slot < 8; ++slot) {
            const uintptr_t m = GetMeshMaterial(suits[i], slot);
            const std::string name = m ? GetName(m) : std::string();
            if (!m || !IsA(m, dynamicClass) || Contains(name, "skin") || Contains(name, "eye") || Contains(name, "hair") || Contains(name, "head"))
                continue;
            WhitenInPlace(m);
            ++painted;
        }
    }
    static int lastLogged = -1;
    if (painted != lastLogged) {
        lastLogged = painted;
        printf("[outfits] look: %d preview suit material(s) painted white (locker/lobby copies)\n", painted);
    }
}

void PhantomLookInner() {
    if (!localPawn || !localPS || !off.Mesh || !world)
        return;
    uintptr_t parts[6]{};
    if (!ReadParts(localPS, parts))
        return;
    const std::string head = parts[0] ? GetName(parts[0]) : std::string("none");
    const std::string body = parts[1] ? GetName(parts[1]) : std::string("none");
    char line[200];
    snprintf(line, sizeof(line), "head %s body %s", head.c_str(), body.c_str());
    LogSkinDetails(parts, line);
    if (strcmp(line, g_look.lastParts) != 0) {
        strcpy_s(g_look.lastParts, line);
        printf("[outfits] look: your parts: %s\n", line);
    }
    // The Phantom: The Reaper's body (CID_084 "Assassin") with the default Spitfire head (BLK_Sydney).
    bool reaperBody = Contains(body, "assassin") || Contains(body, "reaper");
    for (const std::string& known : g_reaperBodies)
        if (!known.empty() && known == body)
            reaperBody = true;
    if (!Contains(head, "sydney") || !reaperBody)
        return;
    // The pawn's own Mesh is only the invisible base skeleton (SK_M_MALE_Base_Skeleton in the log): the suit is drawn
    // by the body part's own component, so that is the one painted.
    // The Phantom keeps The Reaper's own black suit (Edwin, 2026-11-11): the suit, shirt, tie, shoes and hands are
    // one material on Season 3, so any repaint also changed the hands. PaintPhantomBody / PaintPhantomPreviews stay
    // here unused in case a per-part recolour becomes possible.
    FeatureStatus("phantom", "black suit with Spitfire's head");
}

// ---- The Phantom's own locker icon --------------------------------------------------------------------------------
// Season 3 can't turn a file into a UTexture2D (no ImportFileAsTexture2D, and a cooked pak crashes it). What it can do
// is download an image: UMG's AsyncTaskDownloadImage makes a UTexture2DDynamic from a PNG at an http URL. So the menu
// serves syndicate-phantom-icon.png on 127.0.0.1 (IconServer.cpp) and asks the game to download it. On 3.5 the locker
// tile reads FortItemDefinition.SmallPreviewImage, a plain UTexture2D pointer (not a soft one), from the outfit's hero:
// the Phantom's own copy of The Reaper's hero gets the downloaded texture there. No widget holds the icon as an object
// (a scan of every live object found only the two heroes), so this is the one place to change. The Reaper's own hero
// is never touched. "[outfits] icon:" lines say what happened. Status 2026-11-11: the download, the UTexture2D and the
// hero hook all work, but the locker draws the icon blank; Edwin chose to leave it blank for now.

struct PhantomIcon {
    int state = 0;                // 0 start, 1 waiting for the server, 2 downloading, 3 have the texture, -1 off
    ULONGLONG next = 0;
    ULONGLONG deadline = 0;
    uintptr_t texture = 0;        // the downloaded UTexture2DDynamic
    uintptr_t before[16]{};       // Texture2DDynamic objects that existed before the download
    int beforeCount = 0;
    uintptr_t hero = 0;           // the Phantom's hero copy that got the texture
    uintptr_t icon = 0;           // the UTexture2D that draws the downloaded texture
};
PhantomIcon g_icon;

bool IconFile(wchar_t (&file)[MAX_PATH]) {
    HMODULE self = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        reinterpret_cast<LPCWSTR>(&IconFile), &self);
    if (!GetModuleFileNameW(self, file, MAX_PATH))
        return false;
    wchar_t* slash = wcsrchr(file, L'\\');
    if (!slash)
        return false;
    wcscpy_s(slash + 1, MAX_PATH - (slash + 1 - file), L"syndicate-phantom-icon.png");
    return GetFileAttributesW(file) != INVALID_FILE_ATTRIBUTES;
}

bool LiveObject(uintptr_t obj) {
    // Not a class default or archetype, and not being destroyed (RF_BeginDestroyed / RF_FinishDestroyed).
    return obj && !(ObjFlags(obj) & (0x10 | 0x20 | 0x8000 | 0x10000));
}

int CollectObjects(uintptr_t cls, uintptr_t* out, int max) {
    int found = 0;
    const int count = ObjectCount();
    for (int i = 0; i < count && i < 2000000 && found < max; ++i) {
        const uintptr_t obj = ObjectAt(i);
        if (obj && ObjClass(obj) == cls && LiveObject(obj))
            out[found++] = obj;
    }
    return found;
}

// A pointer that looks like a live UObject: aligned, readable, its class readable.
bool IsGObject(uintptr_t p) {
    if (!Readable(p, 0x10))
        return false;
    const int32_t index = Read<int32_t>(p + 0xC);
    return index >= 0 && index < ObjectCount() && ObjectAt(index) == p;
}

// A UTexture2D for the icon (the tile takes only a UTexture2D; a UTexture2DDynamic gave a "?"): a fresh one that gets
// the downloaded texture's UTexture::Resource and TextureReference, and The Reaper's PlatformData for its size. The
// field offsets are found by comparing the downloaded texture with The Reaper's loaded icon: Resource is the first
// heap pointer to objects with different vtables in the exe (FTexture2DDynamicResource / FTexture2DResource), the
// reference the next heap pointer to objects with the same vtable, PlatformData a heap pointer to { SizeX, SizeY, 1 }.
// On 3.5 the locker still draws this icon blank (Edwin, 2026-11-11: leave it blank).
uintptr_t MakeIconTexture(uintptr_t loaded) {
    static uintptr_t textureClass = FindObjectByName("Texture", "Class");
    static uintptr_t texture2DClass = FindObjectByName("Texture2D", "Class");
    static uintptr_t transient = FindObjectByName("/Engine/Transient", "Package");
    const int32_t size = textureClass && PropertiesSizeOff ? Read<int32_t>(textureClass + PropertiesSizeOff) : 0;
    const int32_t size2D = texture2DClass && PropertiesSizeOff ? Read<int32_t>(texture2DClass + PropertiesSizeOff) : 0;
    const uintptr_t exe = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    const auto nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(exe + reinterpret_cast<const IMAGE_DOS_HEADER*>(exe)->e_lfanew);
    const uintptr_t exeEnd = exe + nt->OptionalHeader.SizeOfImage;
    auto inExe = [&](uintptr_t p) { return p >= exe && p < exeEnd; };
    if (!texture2DClass || !transient || size < 0x30 || size > 0x400) {
        printf("[outfits] icon: Texture class not usable (size 0x%X)\n", size);
        return 0;
    }
    int resourceOff = 0, referenceOff = 0;
    for (int off = 0x28; off + 8 <= size + 0x40 && !referenceOff; off += 8) {
        const uintptr_t pd = Read<uintptr_t>(g_icon.texture + off);
        const uintptr_t pl = Read<uintptr_t>(loaded + off);
        if (!pd || !pl || (pd & 7) || (pl & 7) || inExe(pd) || inExe(pl) || !Readable(pd, 0x10) || !Readable(pl, 0x10) || IsGObject(pd) || IsGObject(pl))
            continue;
        const uintptr_t vd = Read<uintptr_t>(pd);
        const uintptr_t vl = Read<uintptr_t>(pl);
        if (!inExe(vd) || !inExe(vl))
            continue;
        if (!resourceOff && vd != vl)
            resourceOff = off;
        else if (resourceOff && vd == vl)
            referenceOff = off;
    }
    int platformOff = 0;
    for (int off = size & ~7; off + 8 <= size2D + 0x40 && off < 0x600 && !platformOff; off += 8) {
        const uintptr_t p = Read<uintptr_t>(loaded + off);
        if (!p || (p & 7) || inExe(p) || !Readable(p, 0x10) || IsGObject(p))
            continue;
        const int32_t x = Read<int32_t>(p), y = Read<int32_t>(p + 4), slices = Read<int32_t>(p + 8);
        if (x >= 16 && x <= 4096 && y >= 16 && y <= 4096 && slices == 1)
            platformOff = off;
    }
    if (!resourceOff) {
        printf("[outfits] icon: UTexture::Resource not found; the locker keeps The Reaper's icon\n");
        return 0;
    }
    const uintptr_t icon = SpawnObject(texture2DClass, transient);
    if (!icon) {
        printf("[outfits] icon: could not make a UTexture2D\n");
        return 0;
    }
    AddToRoot(icon);
    if (platformOff)
        Write<uintptr_t>(icon + platformOff, Read<uintptr_t>(loaded + platformOff));
    Write<uintptr_t>(icon + resourceOff, Read<uintptr_t>(g_icon.texture + resourceOff));
    if (referenceOff) {
        const uintptr_t reference = Read<uintptr_t>(g_icon.texture + referenceOff);
        _InterlockedIncrement(reinterpret_cast<volatile long*>(reference + 8)); // FRHIResource::NumRefs: the icon holds one
        Write<uintptr_t>(icon + referenceOff, reference);
    }
    printf("[outfits] icon: %s draws %s (Resource +0x%X, TextureReference +0x%X, PlatformData +0x%X)\n", GetName(icon).c_str(),
        GetName(g_icon.texture).c_str(), resourceOff, referenceOff, platformOff);
    return icon;
}

void PhantomIconInner() {
    if (g_icon.state < 0 || !g_item || g_stage < 2 || GetTickCount64() < g_icon.next)
        return;
    g_icon.next = GetTickCount64() + 1000;
    static uintptr_t dynClass = FindObjectByName("Texture2DDynamic", "Class");
    if (g_icon.state == 0) {
        static uintptr_t fn = GetFunction("AsyncTaskDownloadImage", "DownloadImage");
        wchar_t file[MAX_PATH]{};
        if (!fn || !dynClass) {
            printf("[outfits] icon: this build has no AsyncTaskDownloadImage; the locker keeps The Reaper's icon\n");
            g_icon.state = -1;
            return;
        }
        if (!IconFile(file)) {
            printf("[outfits] icon: syndicate-phantom-icon.png is not next to the menu; the locker keeps The Reaper's icon\n");
            g_icon.state = -1;
            return;
        }
        if (!StartIconServer(file)) {
            printf("[outfits] icon: could not read %ls\n", file);
            g_icon.state = -1;
            return;
        }
        g_icon.deadline = GetTickCount64() + 10000;
        g_icon.state = 1;
        return;
    }
    if (g_icon.state == 1) {
        const int port = IconServerPort();
        if (port < 0 || GetTickCount64() > g_icon.deadline) {
            printf("[outfits] icon: the local icon server did not start\n");
            g_icon.state = -1;
            return;
        }
        if (port == 0)
            return;
        static uintptr_t fn = GetFunction("AsyncTaskDownloadImage", "DownloadImage");
        static uintptr_t lib = DefaultObject("AsyncTaskDownloadImage");
        static PropInfo url = fn ? FindProperty(fn, "URL") : PropInfo{};
        static PropInfo ret = fn ? FindProperty(fn, "ReturnValue") : PropInfo{};
        if (!lib || !url.found) {
            printf("[outfits] icon: DownloadImage has no URL parameter\n");
            g_icon.state = -1;
            StopIconServer();
            return;
        }
        g_icon.beforeCount = CollectObjects(dynClass, g_icon.before, 16);
        wchar_t address[96];
        swprintf_s(address, L"http://127.0.0.1:%d/syndicate-phantom-icon.png", port);
        alignas(8) uint8_t params[0x40]{};
        const FString text{ address, (int32_t)wcslen(address) + 1, (int32_t)wcslen(address) + 1 };
        memcpy(params + url.offset, &text, sizeof(text));
        ProcessEvent(lib, fn, params);
        uintptr_t task = 0;
        if (ret.found)
            memcpy(&task, params + ret.offset, 8);
        AddToRoot(task); // the download only finishes if its task object is still alive
        printf("[outfits] icon: asked the game to download %ls (task %s)\n", address, task ? GetName(task).c_str() : "none");
        g_icon.deadline = GetTickCount64() + 20000;
        g_icon.state = 2;
        return;
    }
    if (g_icon.state == 2) {
        uintptr_t now[16]{};
        const int n = CollectObjects(dynClass, now, 16);
        for (int i = 0; i < n && !g_icon.texture; ++i) {
            bool old = false;
            for (int j = 0; j < g_icon.beforeCount && !old; ++j)
                old = now[i] == g_icon.before[j];
            if (!old)
                g_icon.texture = now[i];
        }
        if (!g_icon.texture) {
            if (GetTickCount64() > g_icon.deadline) {
                printf("[outfits] icon: no texture came back from the download; the locker keeps The Reaper's icon\n");
                g_icon.state = -1;
                StopIconServer();
            }
            return;
        }
        AddToRoot(g_icon.texture);
        StopIconServer();
        printf("[outfits] icon: the game made %s from the PNG\n", GetName(g_icon.texture).c_str());
        g_icon.state = 3;
        return;
    }
    // The Phantom's hero copy (made by MakePhantomHero in /Engine/Transient) gets the texture as its small icon. Checked
    // every second, so a hero made later or a value the game puts back is handled too.
    if (g_icon.state == 3) {
        static PropInfo heroProp = FindClassProp("AthenaCharacterItemDefinition", "HeroDefinition");
        static PropInfo smallIcon = FindClassProp("FortItemDefinition", "SmallPreviewImage");
        static uintptr_t transient = FindObjectByName("/Engine/Transient", "Package");
        if (!heroProp.found || !smallIcon.found) {
            printf("[outfits] icon: no HeroDefinition / SmallPreviewImage field; the locker keeps The Reaper's icon\n");
            g_icon.state = -1;
            return;
        }
        const uintptr_t hero = Read<uintptr_t>(g_item + heroProp.offset);
        if (!hero || ObjOuter(hero) != transient)
            return; // still The Reaper's own hero (or none): wait for the Phantom's copy
        const uintptr_t current = Read<uintptr_t>(hero + smallIcon.offset);
        if (!g_icon.icon) {
            // The tile only takes a UTexture2D (a UTexture2DDynamic gave a "?"), so the icon is a fresh UTexture2D
            // that draws the downloaded texture's GPU resource (UTexture::Resource, found by comparing it with The
            // Reaper's loaded icon).
            if (!current || current == g_icon.texture)
                return;
            AddToRoot(current);
            g_icon.icon = MakeIconTexture(current);
            if (!g_icon.icon) {
                g_icon.state = -1;
                return;
            }
        }
        if (current != g_icon.icon) {
            Write<uintptr_t>(hero + smallIcon.offset, g_icon.icon);
            printf("[outfits] icon: %s SmallPreviewImage (+0x%X) %s -> %s\n", GetName(hero).c_str(), (unsigned)smallIcon.offset,
                current ? GetName(current).c_str() : "none", GetName(g_icon.icon).c_str());
        }
        // The big picture in the locker's top panel: LargePreviewImage, a soft pointer whose weak pointer is used first.
        static PropInfo largeIcon = FindClassProp("FortItemDefinition", "LargePreviewImage");
        const uintptr_t largeTarget = g_icon.icon;
        if (largeIcon.found && Read<int32_t>(hero + largeIcon.offset) != Read<int32_t>(largeTarget + 0xC)) {
            PointSoftAt(hero + largeIcon.offset, largeTarget);
            static bool logged = false;
            if (!logged) {
                logged = true;
                printf("[outfits] icon: %s LargePreviewImage points at %s\n", GetName(hero).c_str(), GetName(largeTarget).c_str());
            }
        }
        g_icon.hero = hero;
    }
}

} // namespace

// The season check lives outside TickPhantomLook: a function-local static with a dynamic initializer needs
// object unwinding, which a function using __try can't have (C2712).
static bool g_iconOn = true;

static bool IsSeason3Cached() {
    static bool season3 = Season3();
    return season3;
}

void TickPhantomLook() {
    if (g_stage == 2 && GetTickCount64() >= g_nextTry) {
        g_nextTry = GetTickCount64() + 2000;
        __try {
            Extras();
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            g_stage = 3;
            printf("[outfits] icon/preview stopped after an error\n");
        }
    }
    if (!IsSeason3Cached())
        return;
    if (g_iconOn) {
        __try {
            PhantomIconInner();
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            g_iconOn = false;
            StopIconServer();
            printf("[outfits] icon: stopped after an error; the locker keeps The Reaper's icon\n");
        }
    }
    if (GetTickCount64() < g_look.next)
        return;
    g_look.next = GetTickCount64() + 1000;
    __try {
        PhantomLookInner();
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        g_look.next = GetTickCount64() + 10000;
        printf("[outfits] look: error, retrying in 10 s\n");
    }
}

void TickCustomOutfits() {
    if (g_stage < 0 || g_stage == 3)
        return;
    __try {
        TickInner();
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        g_stage = -1;
        printf("[outfits] stopped after an error\n");
    }
}
