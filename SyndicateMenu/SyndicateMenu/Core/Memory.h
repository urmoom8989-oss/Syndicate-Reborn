#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdint>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

#include "../SDK/Types.h"
#include "../Utils/memory.h"

extern uintptr_t baseAddr;
extern uintptr_t imageSize;

template <typename T>
inline T Read(uintptr_t a) {
    __try { return *(T*)a; }
    __except (1) { return T{}; }
}

template <typename T>
inline void Write(uintptr_t a, const T& v) {
    __try { *(T*)a = v; }
    __except (1) {}
}

bool PtrOk(uintptr_t a);
bool InImage(uintptr_t a);
uintptr_t GetImageSize();
PIMAGE_SECTION_HEADER GetTextSection();
PIMAGE_SECTION_HEADER GetSection(const char* name);
void GetSectionRange(const char* name, uintptr_t& start, uint64_t& size);

uintptr_t PatternScanRange(uintptr_t start, uint64_t size, const char* pat,
                           size_t relOff = (size_t)-1, bool relative = false, bool backwards = false);
uintptr_t PatternScan(const char* pat, size_t relOff = (size_t)-1, bool relative = false);
uintptr_t FindStringRef(const wchar_t* text);

template <typename T>
inline uint32_t FindMatchingValueOffset(const std::vector<std::pair<void*, T>>& pairs,
                                        uint32_t minOff = 0x28, uint32_t maxOff = 0x1A0, int step = 4) {
    for (uint32_t off = minOff; off <= maxOff; off += step) {
        size_t hits = 0;
        for (const auto& p : pairs) {
            if (!p.first) continue;
            if (Read<T>((uintptr_t)p.first + off) == p.second) hits++;
        }
        if (hits == pairs.size()) return off;
    }
    return 0;
}
