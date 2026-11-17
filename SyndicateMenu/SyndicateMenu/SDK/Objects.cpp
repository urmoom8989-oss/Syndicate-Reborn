#include "Objects.h"
#include "../Core/Memory.h"

#include <cctype>
#include <cstdio>
#include <string>
#include <unordered_set>

SDK::TUObjectArray GObjects;
bool gobjectsChunked = false;
uintptr_t objectArray = 0;

void (*FNameToStringFn)(const FName*, FString*) = nullptr;
void (*FNameCtorW)(FName*, const wchar_t*, int) = nullptr;
void* (*FMemoryReallocFn)(void*, int64_t, uint32_t) = nullptr;
int ProcessEventIdx = -1;

uint32_t SuperStructOff = 0;
uint32_t ChildrenOff = 0;
uint32_t FieldNextOff = 0x28;
uint32_t OffsetInternalOff = 0x44;
uint32_t ByteMaskOff = 0x72;
uint32_t ClassDefaultOff = 0;
uint32_t PropertiesSizeOff = 0;
uint32_t FuncPtrOff = 0;
uint32_t FunctionFlagsOff = 0;

uintptr_t GetObjectByIndex(int i) {
    if (!GObjects.IsValid()) return 0;
    return reinterpret_cast<uintptr_t>(GObjects.GetByIndex(i));
}

int32_t ObjectCount() {
    return GObjects.IsValid() ? GObjects.Num() : 0;
}

void FStringFree(FString& s) {
    if (s.Data && FMemoryReallocFn) {
        __try { FMemoryReallocFn(s.Data, 0, 0); }
        __except (1) {}
    }
    s.Data = nullptr;
    s.Num = 0;
    s.Max = 0;
}

static bool CallFNameToString(FName name, FString* out) {
    __try {
        FNameToStringFn(&name, out);
        return true;
    } __except (1) {
        return false;
    }
}

// Copies the FString the game gave back. The copy is guarded too: a bad pointer from
// the game must not close it.
static int CopyNameChars(const FString& out, char* buf, int cap) {
    __try {
        if (!out.Data || out.Num <= 1) return 0;
        int n = out.Num - 1;
        if (n > cap) n = cap;
        for (int i = 0; i < n; i++) buf[i] = (char)out.Data[i];
        return n;
    } __except (1) {
        return 0;
    }
}

std::string NameToString(FName name) {
    if (!FNameToStringFn || !name.ComparisonIndex) return {};
    FString out{};
    if (!CallFNameToString(name, &out)) return {};
    char buf[256];
    int n = CopyNameChars(out, buf, 256);
    FStringFree(out);
    return std::string(buf, (size_t)n);
}

FName MakeName(const wchar_t* w) {
    FName n{};
    if (FNameCtorW && w) {
        __try { FNameCtorW(&n, w, 1); }
        __except (1) {}
    }
    return n;
}

static std::wstring NarrowToWide(const char* s) {
    if (!s || !*s) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s, -1, nullptr, 0);
    if (n <= 1) return {};
    std::wstring w((size_t)n - 1, 0);
    MultiByteToWideChar(CP_UTF8, 0, s, -1, w.data(), n);
    return w;
}

uintptr_t ObjClass(uintptr_t o) { return o ? Read<uintptr_t>(o + 0x10) : 0; }
FName ObjName(uintptr_t o) { return o ? Read<FName>(o + 0x18) : FName{}; }
uintptr_t ObjOuter(uintptr_t o) { return o ? Read<uintptr_t>(o + 0x20) : 0; }
int32_t ObjFlags(uintptr_t o) { return o ? Read<int32_t>(o + 0x8) : 0; }
bool IsDefaultObject(uintptr_t o) { return (ObjFlags(o) & 0x10) == 0x10; }
std::string GetName(uintptr_t o) { return NameToString(ObjName(o)); }
std::string ObjClassName(uintptr_t o) { return GetName(ObjClass(o)); }

uintptr_t FindObjectByName(const char* name, const char* className) {
    int n = ObjectCount();
    if (n <= 0 || n > 5000000) return 0;

    FName want = MakeName(NarrowToWide(name).c_str());
    FName wantClass = className ? MakeName(NarrowToWide(className).c_str()) : FName{};
    const bool fast = want.ComparisonIndex != 0;

    for (int i = 0; i < n; i++) {
        uintptr_t o = GetObjectByIndex(i);
        if (!o) continue;
        if (fast) {
            if (!(ObjName(o) == want)) continue;
            if (className) {
                uintptr_t cls = ObjClass(o);
                if (!cls || !(ObjName(cls) == wantClass)) continue;
            }
            return o;
        }
        if (GetName(o) != name) continue;
        if (className && ObjClassName(o) != className) continue;
        return o;
    }
    return 0;
}

uintptr_t FindObjectWithOuter(const char* name, const char* outerName) {
    int n = ObjectCount();
    if (n <= 0 || n > 5000000) return 0;

    FName want = MakeName(NarrowToWide(name).c_str());
    FName wantOuter = MakeName(NarrowToWide(outerName).c_str());
    const bool fast = want.ComparisonIndex != 0 && wantOuter.ComparisonIndex != 0;

    for (int i = 0; i < n; i++) {
        uintptr_t o = GetObjectByIndex(i);
        if (!o) continue;
        uintptr_t outer = ObjOuter(o);
        if (!outer) continue;
        if (fast) {
            if (ObjName(o) == want && ObjName(outer) == wantOuter)
                return o;
            continue;
        }
        if (GetName(o) != name) continue;
        if (GetName(outer) == outerName)
            return o;
    }
    return 0;
}

bool IsA(uintptr_t obj, uintptr_t clss) {
    if (!obj || !clss || !SuperStructOff) return false;
    for (uintptr_t s = ObjClass(obj); s; s = Read<uintptr_t>(s + SuperStructOff)) {
        if (s == clss) return true;
    }
    return false;
}

// Name match without calling FName::ToString when the FName constructor works.
static bool NameIs(uintptr_t o, const char* name, FName want) {
    if (want.ComparisonIndex) return ObjName(o) == want;
    return GetName(o) == name;
}

static PropInfo FindPropertyBy(uintptr_t ustruct, const char* propName, FName want, int depth) {
    PropInfo r{};
    if (!ustruct || !ChildrenOff || !FieldNextOff || depth > 64) return r;
    int guard = 0;
    for (uintptr_t child = Read<uintptr_t>(ustruct + ChildrenOff); child && guard < 100000;
         child = Read<uintptr_t>(child + FieldNextOff), guard++) {
        if (!NameIs(child, propName, want)) continue;
        r.found = true;
        r.offset = Read<int32_t>(child + OffsetInternalOff);
        r.mask = Read<uint8_t>(child + ByteMaskOff);
        return r;
    }
    if (SuperStructOff) {
        uintptr_t super = Read<uintptr_t>(ustruct + SuperStructOff);
        if (super) return FindPropertyBy(super, propName, want, depth + 1);
    }
    return r;
}

// Logs each missing class, property or function once, so SyndicateMenu.log lists every
// feature input this season doesn't have (old seasons lack many newer properties). Keyed by
// the struct pointer and the name text, so repeat misses (some features look up every frame)
// cost one set lookup and no FName::ToString call.
//
// The lock is a Windows SRWLOCK, not std::mutex: Season X loads the old msvcp140.dll that
// ships in its own Binaries\Win64 folder, and a std::mutex built with a current Visual Studio
// crashes inside that older runtime's lock call (it closed 10.40 at the lobby on the first
// missing property).
static bool FirstMiss(uintptr_t owner, const char* name) {
    static SRWLOCK lock = SRWLOCK_INIT;
    static std::unordered_set<std::string> seen;
    char key[200];
    snprintf(key, sizeof(key), "%llx:%s", (unsigned long long)owner, name ? name : "");
    AcquireSRWLockExclusive(&lock);
    bool first = seen.size() <= 4096 && seen.insert(key).second;
    ReleaseSRWLockExclusive(&lock);
    return first;
}

static void LogMissing(const char* kind, uintptr_t ownerObj, const char* ownerName, const char* name) {
    if (!FirstMiss(ownerObj ? ownerObj : (uintptr_t)ownerName, name))
        return;
    std::string owner = ownerName ? std::string(ownerName) : GetName(ownerObj);
    printf("[sdk] missing %s %s.%s\n", kind, owner.empty() ? "?" : owner.c_str(), name ? name : "");
}

PropInfo FindProperty(uintptr_t ustruct, const char* propName) {
    PropInfo r = FindPropertyBy(ustruct, propName, MakeName(NarrowToWide(propName).c_str()), 0);
    if (!r.found && ustruct)
        LogMissing("property", ustruct, nullptr, propName);
    return r;
}

PropInfo FindClassProp(const char* className, const char* propName) {
    uintptr_t clss = FindObjectByName(className, "Class");
    if (!clss) clss = FindObjectByName(className);
    if (!clss) {
        LogMissing("class", 0, className, propName);
        return {};
    }
    PropInfo r = FindPropertyBy(clss, propName, MakeName(NarrowToWide(propName).c_str()), 0);
    if (!r.found)
        LogMissing("property", clss, className, propName);
    return r;
}

static uintptr_t FindFunctionBy(uintptr_t ustruct, const char* fnName, FName want, int depth) {
    if (!ustruct || !ChildrenOff || depth > 64) return 0;
    int guard = 0;
    for (uintptr_t child = Read<uintptr_t>(ustruct + ChildrenOff); child && guard < 100000;
         child = Read<uintptr_t>(child + FieldNextOff), guard++) {
        if (NameIs(child, fnName, want)) return child;
    }
    if (SuperStructOff) {
        uintptr_t super = Read<uintptr_t>(ustruct + SuperStructOff);
        if (super) return FindFunctionBy(super, fnName, want, depth + 1);
    }
    return 0;
}

uintptr_t FindFunction(uintptr_t ustruct, const char* fnName) {
    uintptr_t fn = FindFunctionBy(ustruct, fnName, MakeName(NarrowToWide(fnName).c_str()), 0);
    if (!fn && ustruct)
        LogMissing("function", ustruct, nullptr, fnName);
    return fn;
}

uintptr_t GetFunction(const char* className, const char* fnName) {
    uintptr_t clss = FindObjectByName(className, "Class");
    if (!clss) clss = FindObjectByName(className);
    if (!clss) {
        LogMissing("class", 0, className, fnName);
        return 0;
    }
    return FindFunction(clss, fnName);
}

bool ProcessEvent(uintptr_t obj, uintptr_t fn, void* params) {
    if (!obj || !fn || ProcessEventIdx < 0) return false;
    uintptr_t vt = Read<uintptr_t>(obj);
    if (!vt) return false;
    auto pe = Read<void*>(vt + 8ull * ProcessEventIdx);
    if (!pe) return false;
    using PE_t = void(*)(uintptr_t, uintptr_t, void*);
    __try { ((PE_t)pe)(obj, fn, params); return true; }
    __except (1) { return false; }
}

// Debug: lists the fields of a class (and its parents) whose names contain one of the keywords,
// with their type and offset, so a season that lacks a property the menu wants shows what it
// has instead. Used once per weapon class by the weapon mods ("[mods]" lines).
static bool HasKeyword(const std::string& name, const char* const* keywords, int count) {
    if (!keywords || count <= 0)
        return true;
    std::string lower = name;
    for (char& c : lower)
        c = (char)tolower((unsigned char)c);
    for (int i = 0; i < count; ++i) {
        std::string key = keywords[i];
        for (char& c : key)
            c = (char)tolower((unsigned char)c);
        if (lower.find(key) != std::string::npos)
            return true;
    }
    return false;
}

void LogStructFields(const char* tag, uintptr_t ustruct, const char* const* keywords, int keywordCount) {
    if (!ustruct || !ChildrenOff || !FieldNextOff)
        return;
    int printed = 0;
    std::string chain;
    for (uintptr_t s = ustruct; s && printed < 160; s = SuperStructOff ? Read<uintptr_t>(s + SuperStructOff) : 0) {
        const std::string owner = GetName(s);
        if (!chain.empty())
            chain += " > ";
        chain += owner;
        int guard = 0;
        for (uintptr_t child = Read<uintptr_t>(s + ChildrenOff); child && guard < 4000 && printed < 160;
             child = Read<uintptr_t>(child + FieldNextOff), guard++) {
            const std::string name = GetName(child);
            if (name.empty() || !HasKeyword(name, keywords, keywordCount))
                continue;
            const std::string type = ObjClassName(child);
            if (type == "Function")
                printf("[%s]   %s.%s (function)\n", tag, owner.c_str(), name.c_str());
            else
                printf("[%s]   %s.%s (%s) at 0x%X\n", tag, owner.c_str(), name.c_str(), type.c_str(),
                    Read<int32_t>(child + OffsetInternalOff));
            printed++;
        }
        if (owner == "Object" || owner == "Actor")
            break;
    }
    printf("[%s]   (class chain: %s; %d matching fields)\n", tag, chain.c_str(), printed);
}
