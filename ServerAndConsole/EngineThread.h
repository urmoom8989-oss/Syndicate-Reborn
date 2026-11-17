#pragma once
#include <Windows.h>
namespace Syndicate::EngineThread
{
struct WindowSearch { DWORD Process; DWORD Thread; };
inline BOOL CALLBACK FindWindowThread(HWND Window, LPARAM Context)
{
    auto &Search = *reinterpret_cast<WindowSearch *>(Context);
    DWORD Process = 0;
    DWORD Thread = GetWindowThreadProcessId(Window, &Process);
    wchar_t Class[128]{};
    if (Process == Search.Process && GetClassNameW(Window, Class, 128) &&
        lstrcmpW(Class, L"UnrealWindow") == 0)
    {
        Search.Thread = Thread;
        return FALSE;
    }
    return TRUE;
}
inline DWORD FindGameWindowThread()
{
    WindowSearch Search{GetCurrentProcessId(), 0};
    EnumWindows(FindWindowThread, reinterpret_cast<LPARAM>(&Search));
    return Search.Thread;
}
inline bool IsEngineThread(DWORD Expected)
{
    return Expected != 0 && GetCurrentThreadId() == Expected;
}
}
