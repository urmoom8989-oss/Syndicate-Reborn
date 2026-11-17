#include "Memory.h"

uintptr_t baseAddr = 0;
uintptr_t imageSize = 0;

bool PtrOk(uintptr_t a) {
    return a > 0x10000 && (!imageSize || (a >= baseAddr && a < baseAddr + imageSize) || a > 0x100000);
}

bool InImage(uintptr_t a) {
    return Memory::IsAddressInsideImage(a);
}

uintptr_t GetImageSize() {
    return Memory::GetImageSize();
}

PIMAGE_SECTION_HEADER GetSection(const char* name) {
    return Memory::GetSectionFromName(name, Memory::GetImageBase());
}

PIMAGE_SECTION_HEADER GetTextSection() {
    return Memory::GetImageTextSection();
}

void GetSectionRange(const char* name, uintptr_t& start, uint64_t& size) {
    start = 0;
    size = 0;
    auto sect = GetSection(name);
    if (!sect) return;
    start = Memory::GetImageBase() + sect->VirtualAddress;
    size = sect->Misc.VirtualSize;
}

uintptr_t PatternScanRange(uintptr_t start, uint64_t size, const char* pat, size_t relOff, bool relative, bool backwards) {
    return Memory::PatternScanRange<int32_t>(start, size, pat, backwards, relOff, relative);
}

uintptr_t PatternScan(const char* pat, size_t relOff, bool relative) {
    return Memory::PatternScan<int32_t>(pat, relOff, relative);
}

uintptr_t FindStringRef(const wchar_t* text) {
    return Memory::FindStringRef(text);
}
