#pragma once

#include <cstdint>
#include <string>

#include "ObjectArray.h"
#include "Types.h"

extern SDK::TUObjectArray GObjects;
extern bool gobjectsChunked;
extern uintptr_t objectArray;

extern void (*FNameToStringFn)(const FName*, FString*);
extern void (*FNameCtorW)(FName*, const wchar_t*, int);
extern void* (*FMemoryReallocFn)(void*, int64_t, uint32_t);
extern int ProcessEventIdx;

extern uint32_t SuperStructOff;
extern uint32_t ChildrenOff;
extern uint32_t FieldNextOff;
extern uint32_t OffsetInternalOff;
extern uint32_t ByteMaskOff;
extern uint32_t ClassDefaultOff;
extern uint32_t PropertiesSizeOff;
extern uint32_t FuncPtrOff;
extern uint32_t FunctionFlagsOff;

uintptr_t GetObjectByIndex(int i);
int32_t ObjectCount();

void FStringFree(FString& s);
std::string NameToString(FName name);
FName MakeName(const wchar_t* w);

uintptr_t ObjClass(uintptr_t o);
FName ObjName(uintptr_t o);
uintptr_t ObjOuter(uintptr_t o);
int32_t ObjFlags(uintptr_t o);
bool IsDefaultObject(uintptr_t o);
std::string GetName(uintptr_t o);
std::string ObjClassName(uintptr_t o);

uintptr_t FindObjectByName(const char* name, const char* className = nullptr);
uintptr_t FindObjectWithOuter(const char* name, const char* outerName);
bool IsA(uintptr_t obj, uintptr_t clss);

PropInfo FindProperty(uintptr_t ustruct, const char* propName);
PropInfo FindClassProp(const char* className, const char* propName);
uintptr_t FindFunction(uintptr_t ustruct, const char* fnName);
uintptr_t GetFunction(const char* className, const char* fnName);
bool ProcessEvent(uintptr_t obj, uintptr_t fn, void* params);
// Debug: logs the fields of ustruct and its parents whose names contain one of the keywords.
void LogStructFields(const char* tag, uintptr_t ustruct, const char* const* keywords, int keywordCount);
