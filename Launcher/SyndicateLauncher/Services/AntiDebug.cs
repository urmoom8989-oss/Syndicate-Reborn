using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Runtime.InteropServices;
using System.Text;
using System.Threading;

namespace SyndicateLauncher.Services;

/// <summary>
/// Runtime anti-debugging and anti-reverse-engineering guards.
/// All checks compile to nothing in DEV_BUILD — dev machines may legitimately
/// run Process Hacker, debuggers, and other tools.
/// </summary>
public static class AntiDebug
{
#if !DEV_BUILD
    // ------------------------------------------------------------------ P/Invoke

    [DllImport("kernel32.dll", SetLastError = false)]
    private static extern bool IsDebuggerPresent();

    [DllImport("kernel32.dll", SetLastError = false)]
    private static extern bool CheckRemoteDebuggerPresent(IntPtr hProcess, ref bool pbDebuggerPresent);

    [DllImport("kernel32.dll", SetLastError = false)]
    private static extern IntPtr GetCurrentProcess();

    [DllImport("ntdll.dll")]
    private static extern int NtQueryInformationProcess(
        IntPtr processHandle, int processInformationClass,
        ref IntPtr processInformation, int processInformationLength,
        ref int returnLength);

    [DllImport("user32.dll")]
    private static extern bool EnumWindows(EnumWindowsProc lpEnumFunc, IntPtr lParam);
    private delegate bool EnumWindowsProc(IntPtr hWnd, IntPtr lParam);

    [DllImport("user32.dll", CharSet = CharSet.Unicode)]
    private static extern int GetWindowText(IntPtr hWnd, StringBuilder lpString, int nMaxCount);

    // ------------------------------------------------------------------ known tools

    // Matched against a process's exact name (no .exe), not as a substring: a substring test made
    // unrelated software trip the guard (idagent, idastore, fiddlercore, cutterbackup, charlesproxyhelper
    // all contain one of these tokens), which hard-killed the launcher for innocent users.
    private static readonly HashSet<string> BadProcessNames = new(StringComparer.OrdinalIgnoreCase)
    {
        "dnspy", "dnspy-x86", "ilspy", "dotpeek", "reflexil", "de4dot", "justdecompile",
        "x64dbg", "x32dbg", "ollydbg", "windbg", "ida", "ida64", "idaq", "idaq64",
        "radare2", "ghidra", "cutter",
        "processhacker", "processhacker2", "procmon", "procmon64",
        "wireshark", "fiddler", "charles",
        "cheatengine", "cheatengine-x86_64",
        "reclass", "reclass64",
    };

    private static readonly string[] BadWindowTitles =
    {
        "dnSpy", "ILSpy", "x64dbg", "IDA ", "Ghidra",
        "de4dot", "Process Hacker", "Cheat Engine", "ReClass",
    };

    private static volatile bool _monitoring;

    // ------------------------------------------------------------------ public API

    /// <summary>
    /// Run all checks once synchronously. Returns immediately if clean.
    /// Calls Environment.FailFast if any check fires — no graceful path.
    /// </summary>
    public static void RunChecks()
    {
        try
        {
            if (IsManagedDebuggerAttached()  ||
                IsNativeDebuggerPresent()     ||
                IsAnalysisProcessRunning()    ||
                IsAnalysisWindowPresent())
                Terminate();
        }
        catch (Exception ex) when (ex is not OutOfMemoryException)
        {
            // Never let a protection check crash the launcher; Terminate() is intentional.
        }
    }

    /// <summary>
    /// Start a background daemon thread that calls RunChecks every
    /// <paramref name="interval"/>. Safe to call multiple times — only the
    /// first call starts the thread.
    /// </summary>
    public static void StartMonitor(TimeSpan interval)
    {
        if (_monitoring) return;
        _monitoring = true;
        var thread = new Thread(() =>
        {
            while (_monitoring)
            {
                try
                {
                    Thread.Sleep(interval);
                    RunChecks();
                }
                catch (ThreadInterruptedException) { break; }
                catch { /* keep looping on any other exception */ }
            }
        })
        {
            IsBackground = true,
            Name        = "SyndicateGuard",
            Priority    = ThreadPriority.BelowNormal,
        };
        thread.Start();
    }

    // ------------------------------------------------------------------ individual checks

    private static bool IsManagedDebuggerAttached()
        => Debugger.IsAttached;

    private static bool IsNativeDebuggerPresent()
    {
        // Win32 IsDebuggerPresent
        if (IsDebuggerPresent()) return true;

        // Check remote debugger
        bool remote = false;
        CheckRemoteDebuggerPresent(GetCurrentProcess(), ref remote);
        if (remote) return true;

        // ProcessDebugPort (class 7): non-zero means a kernel debugger is attached.
        IntPtr port = IntPtr.Zero;
        int len = 0;
        if (NtQueryInformationProcess(GetCurrentProcess(), 7, ref port, IntPtr.Size, ref len) == 0
            && port != IntPtr.Zero)
            return true;

        // ProcessDebugFlags (class 31): 0 means the process is being debugged.
        IntPtr flags = (IntPtr)1;
        len = 0;
        if (NtQueryInformationProcess(GetCurrentProcess(), 31, ref flags, IntPtr.Size, ref len) == 0
            && flags == IntPtr.Zero)
            return true;

        return false;
    }

    private static bool IsAnalysisProcessRunning()
    {
        try
        {
            foreach (var proc in Process.GetProcesses())
            {
                try
                {
                    if (BadProcessNames.Contains(proc.ProcessName))
                        return true;
                }
                catch { /* access denied on some system processes — skip */ }
            }
        }
        catch { }
        return false;
    }

    private static bool IsAnalysisWindowPresent()
    {
        bool found = false;
        try
        {
            EnumWindows((hWnd, _) =>
            {
                var sb = new StringBuilder(256);
                if (GetWindowText(hWnd, sb, 256) > 0)
                {
                    var title = sb.ToString();
                    foreach (var bad in BadWindowTitles)
                    {
                        if (title.Contains(bad, StringComparison.OrdinalIgnoreCase))
                        {
                            found = true;
                            return false; // stop enumeration early
                        }
                    }
                }
                return true;
            }, IntPtr.Zero);
        }
        catch { }
        return found;
    }

    private static void Terminate()
        => Environment.FailFast(null); // hard kill — cannot be hooked or caught
#endif
}
