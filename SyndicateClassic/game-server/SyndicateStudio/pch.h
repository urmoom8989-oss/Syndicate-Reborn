#ifndef PCH_H
#define PCH_H

#include "framework.h"
#include "../SDK/Includes.h"
using namespace SDK;
// Note: do NOT include Utils.h here — it pulls in MinHook.h (#pragma comment(lib, "minhook.lib"))
// and SyndicateStudio doesn't hook anything. Everything we need (DEFINE_FUNC, GetOffset,
// GetParamsNamed, ToSDKString, VersionInfo) comes from the SDK includes above.
#include <numeric>
#include <algorithm>
#include <string>
#include <vector>
#include <tuple>
#include <unordered_map>
#include <mutex>
#include <atomic>
#include <cmath>

#endif //PCH_H
