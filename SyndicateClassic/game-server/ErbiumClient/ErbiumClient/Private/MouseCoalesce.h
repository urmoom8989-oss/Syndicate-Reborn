#pragma once
// High polling rate mice (2000-8000 Hz) make old Unreal builds stutter: the engine handles every raw mouse
// message (WM_INPUT) one by one, so an 8000 Hz mouse floods each frame with thousands of them. This merges
// mouse movement only for those mice:
//  - The rate is measured first. A mouse that sends 1500 messages a second or fewer (every normal 125-1000 Hz
//    mouse) is not touched at all.
//  - For a faster mouse, a movement-only message is merged only while more raw input is already waiting in the
//    queue: its movement is added to the next mouse message the game reads, which comes later in the same pump.
//    The last message of each burst always reaches the game, so movement is never held back to a later frame.
//    (The first version merged by time and carried the movement over to the next frame's first message: a
//    1000 Hz mouse was merged too, and mouse look felt stuck and jumpy, mostly left/right.)
// Button and wheel messages always go through at once. Off when %LOCALAPPDATA%\ProjectTilted\mouse-coalesce-off.txt
// exists. The same file is in ServerAndConsole (10.40) and SyndicateClassic/game-server/ErbiumClient (other seasons):
// keep the two copies the same.
#include <windows.h>
// MinHook comes from the project's precompiled header (framework.h / pch.h).

namespace MouseCoalesce
{
using GetRawInputDataFn = UINT(WINAPI *)(HRAWINPUT, UINT, LPVOID, PUINT, UINT);
inline GetRawInputDataFn Original = nullptr;
inline DWORD GameThread = 0;
inline bool Active = false;
inline LONG PendingX = 0, PendingY = 0; // game thread only
inline LONGLONG LastSeen = 0, MaxHold = 0; // QPC ticks: a merge never holds movement longer than MaxHold
inline LONGLONG RateStart = 0, Second = 0;
inline LONG RateCount = 0;
inline bool HighRate = false;               // measured: this mouse sends more than ~1500 messages a second
inline LONG Dropped = 0;

// The game reads a mouse message: hand it the movement of the messages that were merged into it.
inline UINT WINAPI ReadDetour(HRAWINPUT Input, UINT Command, LPVOID Data, PUINT Size, UINT HeaderSize)
{
    const UINT Result = Original(Input, Command, Data, Size, HeaderSize);
    if (Command == RID_INPUT && Data && Result != UINT(-1) && Result >= sizeof(RAWINPUTHEADER) + sizeof(RAWMOUSE) &&
        (PendingX || PendingY) && GetCurrentThreadId() == GameThread)
    {
        auto Raw = static_cast<RAWINPUT *>(Data);
        if (Raw->header.dwType == RIM_TYPEMOUSE && !(Raw->data.mouse.usFlags & MOUSE_MOVE_ABSOLUTE))
        {
            Raw->data.mouse.lLastX += PendingX; Raw->data.mouse.lLastY += PendingY;
            PendingX = PendingY = 0;
        }
    }
    return Result;
}

// Call for every message the game thread takes off its queue (WH_GETMESSAGE with PM_REMOVE). A merged message
// becomes WM_NULL.
inline void Filter(MSG *Message)
{
    if (!Active || !Message || Message->message != WM_INPUT || GetCurrentThreadId() != GameThread) return;
    RAWINPUT Raw{}; UINT Size = sizeof(Raw);
    if (Original((HRAWINPUT)Message->lParam, RID_INPUT, &Raw, &Size, sizeof(RAWINPUTHEADER)) == UINT(-1)) return;
    if (Raw.header.dwType != RIM_TYPEMOUSE || (Raw.data.mouse.usFlags & MOUSE_MOVE_ABSOLUTE)) return;
    LARGE_INTEGER Now; QueryPerformanceCounter(&Now);
    // Measure the mouse's rate once a second (on above 1500 a second, off again below 1200).
    ++RateCount;
    if (Now.QuadPart - RateStart >= Second)
    {
        const LONGLONG Rate = RateStart ? RateCount * Second / (Now.QuadPart - RateStart) : 0;
        if (RateStart) HighRate = HighRate ? Rate >= 1200 : Rate > 1500;
        RateStart = Now.QuadPart; RateCount = 0;
    }
    if (!HighRate || Raw.data.mouse.usButtonFlags) { LastSeen = Now.QuadPart; return; } // the game sees this one
    // Merge only when another raw input message is already waiting: that one carries the movement, in this same
    // pump. Also let one through at least every MaxHold so a non-stop stream still moves the camera smoothly.
    if (!(HIWORD(GetQueueStatus(QS_RAWINPUT)) & QS_RAWINPUT) || Now.QuadPart - LastSeen >= MaxHold) { LastSeen = Now.QuadPart; return; }
    PendingX += Raw.data.mouse.lLastX; PendingY += Raw.data.mouse.lLastY;
    // Every WM_INPUT must reach DefWindowProc so Windows frees its raw input buffer. Turning merged messages into
    // WM_NULL skipped that: the unfreed buffers piled up and after a while the mouse stopped moving the camera
    // ("mouse locked"). Free it here, then hide it from the game.
    DefWindowProcW(Message->hwnd, WM_INPUT, Message->wParam, Message->lParam);
    Message->message = WM_NULL;
    ++Dropped;
}

// Hooks GetRawInputData for the game thread. Needs MH_Initialize first (a second call is harmless).
inline bool Install(DWORD Thread)
{
    if (Active || !Thread) return Active;
    wchar_t Off[MAX_PATH]{};
    if (GetEnvironmentVariableW(L"LOCALAPPDATA", Off, MAX_PATH))
    {
        wcscat_s(Off, L"\\ProjectTilted\\mouse-coalesce-off.txt");
        if (GetFileAttributesW(Off) != INVALID_FILE_ATTRIBUTES) return false;
    }
    LARGE_INTEGER Frequency; QueryPerformanceFrequency(&Frequency);
    MaxHold = Frequency.QuadPart * 2 / 1000; // 2 ms
    Second = Frequency.QuadPart;
    GameThread = Thread;
    MH_Initialize();
    void *Target = reinterpret_cast<void *>(GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetRawInputData"));
    if (!Target || MH_CreateHook(Target, reinterpret_cast<void *>(&ReadDetour), reinterpret_cast<void **>(&Original)) != MH_OK) return false;
    if (MH_EnableHook(Target) != MH_OK) { MH_RemoveHook(Target); Original = nullptr; return false; }
    Active = true;
    return true;
}
}
