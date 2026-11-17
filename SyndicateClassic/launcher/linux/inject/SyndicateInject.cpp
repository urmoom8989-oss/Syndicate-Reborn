// Syndicate © 2026 Syndicate. All rights reserved. Proprietary — see LICENSE.
//
// SyndicateInject — a tiny Win64 DLL injector for the Linux/Proton path.
//
// On Windows the launcher injects cobalt.dll + the client mods straight into Fortnite
// (NativeMethods.InjectDll). On Linux the launcher is a native process and cannot reach
// into the game's memory while the game runs under Wine/Proton. The fix is to run this
// small Windows injector *inside the same Proton prefix as the game*: Wine implements
// OpenProcess / VirtualAllocEx / WriteProcessMemory / CreateRemoteThread, and in-prefix
// CreateRemoteThread+LoadLibraryW injection is the standard, working technique under Wine.
//
// This mirrors NativeMethods.InjectDll exactly (same calls, same LoadLibraryW remote
// thread), so if it works under Proton, the whole Windows injection chain can be driven
// from the Linux launcher by running this tool under Wine once per DLL.
//
// Usage (inside the game's prefix, e.g. via `wine TiltedInject.exe ...`):
//     TiltedInject.exe <process.exe|pid> <path\to\dll>
// e.g.
//     TiltedInject.exe FortniteClient-Win64-Shipping.exe Z:\path\to\cobalt.dll
//
// Exit codes: 0 = injected OK; 1 = bad args; 2 = process not found;
//             3 = OpenProcess failed; 4 = alloc/write failed; 5 = remote LoadLibrary failed.
//
// Build (host PC, Developer Command Prompt for VS):
//     cl /EHsc /O2 /DUNICODE /D_UNICODE SyndicateInject.cpp /link /OUT:TiltedInject.exe
// Produces a plain Win64 exe that runs under Proton/Wine.

#include <windows.h>
#include <tlhelp32.h>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <cstdlib>

// Find a process id by executable name (case-insensitive, with or without ".exe").
static DWORD FindProcessId(const wchar_t* wanted)
{
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return 0;

    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    DWORD found = 0;

    if (Process32FirstW(snap, &entry))
    {
        do
        {
            const wchar_t* name = entry.szExeFile;
            if (_wcsicmp(name, wanted) == 0)
            {
                found = entry.th32ProcessID;
                break;
            }
            // Also match when the caller left ".exe" off.
            wchar_t withExe[MAX_PATH];
            swprintf(withExe, MAX_PATH, L"%s.exe", wanted);
            if (_wcsicmp(name, withExe) == 0)
            {
                found = entry.th32ProcessID;
                break;
            }
        } while (Process32NextW(snap, &entry));
    }

    CloseHandle(snap);
    return found;
}

static int Inject(DWORD pid, const wchar_t* dllPath)
{
    HANDLE proc = OpenProcess(PROCESS_ALL_ACCESS, FALSE, pid);
    if (!proc)
    {
        wprintf(L"[SyndicateInject] OpenProcess failed (err %lu).\n", GetLastError());
        return 3;
    }

    int result = 0;
    SIZE_T bytes = (wcslen(dllPath) + 1) * sizeof(wchar_t);
    LPVOID remote = VirtualAllocEx(proc, nullptr, bytes, MEM_COMMIT, PAGE_READWRITE);
    if (!remote)
    {
        wprintf(L"[SyndicateInject] VirtualAllocEx failed (err %lu).\n", GetLastError());
        CloseHandle(proc);
        return 4;
    }

    bool releaseBuffer = true;
    if (!WriteProcessMemory(proc, remote, dllPath, bytes, nullptr))
    {
        wprintf(L"[SyndicateInject] WriteProcessMemory failed (err %lu).\n", GetLastError());
        result = 4;
    }
    else
    {
        // LoadLibraryW lives in kernel32 at the same address in every process on a box,
        // so its address here is valid in the target too (same under Wine within a prefix).
        FARPROC loadLib = GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "LoadLibraryW");
        if (!loadLib)
        {
            wprintf(L"[SyndicateInject] GetProcAddress(LoadLibraryW) failed (err %lu).\n", GetLastError());
            result = 5;
        }
        else
        {
            HANDLE thread = CreateRemoteThread(proc, nullptr, 0,
                reinterpret_cast<LPTHREAD_START_ROUTINE>(loadLib), remote, 0, nullptr);
            if (!thread)
            {
                wprintf(L"[SyndicateInject] CreateRemoteThread failed (err %lu).\n", GetLastError());
                result = 5;
            }
            else
            {
                DWORD wait = WaitForSingleObject(thread, 15000);
                if (wait != WAIT_OBJECT_0)
                {
                    wprintf(L"[SyndicateInject] Remote LoadLibrary did not finish in time.\n");
                    releaseBuffer = false; // the remote thread may still be reading the path
                    result = 5;
                }
                else
                {
                    DWORD exitCode = 0;
                    GetExitCodeThread(thread, &exitCode);
                    if (exitCode == 0)
                    {
                        wprintf(L"[SyndicateInject] Game could not load the DLL (LoadLibrary returned 0). "
                                L"Check the DLL and its dependencies are in the prefix.\n");
                        result = 5;
                    }
                    else
                    {
                        wprintf(L"[SyndicateInject] Injected OK (module handle 0x%08lX).\n", exitCode);
                        result = 0;
                    }
                }
                CloseHandle(thread);
            }
        }
    }

    if (releaseBuffer) VirtualFreeEx(proc, remote, 0, MEM_RELEASE);
    CloseHandle(proc);
    return result;
}

int wmain(int argc, wchar_t** argv)
{
    if (argc != 3)
    {
        wprintf(L"Usage: TiltedInject.exe <process.exe|pid> <path\\to\\dll>\n");
        return 1;
    }

    const wchar_t* target = argv[1];
    const wchar_t* dll = argv[2];

    // Accept a numeric PID directly, otherwise resolve by name.
    DWORD pid = 0;
    wchar_t* end = nullptr;
    unsigned long parsed = wcstoul(target, &end, 10);
    if (end && *end == L'\0' && parsed != 0)
        pid = static_cast<DWORD>(parsed);
    else
        pid = FindProcessId(target);

    if (pid == 0)
    {
        wprintf(L"[SyndicateInject] Process '%s' not found (is the game running in this prefix?).\n", target);
        return 2;
    }

    wprintf(L"[SyndicateInject] Target pid %lu, dll '%s'.\n", pid, dll);
    return Inject(pid, dll);
}
