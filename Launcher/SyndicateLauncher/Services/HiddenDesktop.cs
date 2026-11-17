using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.Diagnostics;
using System.Linq;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using System.Text;
using System.Threading.Tasks;

namespace SyndicateLauncher.Services;

/// <summary>
/// Starts a process on its own, never-shown Windows desktop, so none of its windows can ever
/// appear on the player's screen (not even for a moment). This is how Syndicate starts its
/// headless Erbium match host (NativeMethods.StartHiddenHost); ServerWindowGuard can only hide a
/// window after it has already flashed up.
/// </summary>
internal static class HiddenDesktop
{
    private const uint DesktopAllAccess = 0x000F01FF;
    private const uint CreateNoWindow = 0x08000000;
    private const uint CreateUnicodeEnvironment = 0x00000400;
    private const uint StartfUseShowWindow = 0x00000001;
    private const uint Infinite = 0xFFFFFFFF;
    private const uint WaitObject0 = 0;
    private static readonly ConditionalWeakTable<Process, Task<uint?>> ExitCodes = new();

    internal static Task<uint?>? ExitCodeAsync(Process process) =>
        ExitCodes.TryGetValue(process, out var exitCode) ? exitCode : null;

    [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
    private struct StartupInfo
    {
        public int cb;
        public string? lpReserved;
        public string? lpDesktop;
        public string? lpTitle;
        public int dwX, dwY, dwXSize, dwYSize, dwXCountChars, dwYCountChars, dwFillAttribute;
        public uint dwFlags;
        public short wShowWindow;
        public short cbReserved2;
        public IntPtr lpReserved2, hStdInput, hStdOutput, hStdError;
    }

    [StructLayout(LayoutKind.Sequential)]
    private struct ProcessInformation
    {
        public IntPtr hProcess, hThread;
        public int dwProcessId, dwThreadId;
    }

    [DllImport("user32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    private static extern IntPtr CreateDesktop(string name, IntPtr device, IntPtr devMode, uint flags, uint access, IntPtr attributes);

    [DllImport("user32.dll", SetLastError = true)]
    private static extern bool CloseDesktop(IntPtr desktop);

    [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    private static extern bool CreateProcess(string application, StringBuilder commandLine, IntPtr processAttributes, IntPtr threadAttributes,
        bool inheritHandles, uint creationFlags, IntPtr environment, string? currentDirectory, ref StartupInfo startup, out ProcessInformation process);

    [DllImport("kernel32.dll", SetLastError = true)]
    private static extern bool CloseHandle(IntPtr handle);

    [DllImport("kernel32.dll", SetLastError = true)]
    private static extern uint WaitForSingleObject(IntPtr handle, uint milliseconds);

    [DllImport("kernel32.dll", SetLastError = true)]
    private static extern bool GetExitCodeProcess(IntPtr process, out uint exitCode);

    /// <summary>Starts <paramref name="info"/> (file, ArgumentList or Arguments, working directory,
    /// environment) on a new hidden desktop. The desktop is closed when the process exits.</summary>
    public static Process Start(ProcessStartInfo info)
    {
        string desktopName = "SyndicateHost_" + Guid.NewGuid().ToString("N");
        IntPtr desktop = CreateDesktop(desktopName, IntPtr.Zero, IntPtr.Zero, 0, DesktopAllAccess, IntPtr.Zero);
        if (desktop == IntPtr.Zero)
            throw new Win32Exception(Marshal.GetLastWin32Error(), "Could not create the hidden desktop for the game server.");

        // Unicode environment block: KEY=value\0 ... \0\0, sorted as Windows expects.
        string block = string.Join("\0", info.Environment
            .Where(pair => pair.Value is not null)
            .OrderBy(pair => pair.Key, StringComparer.OrdinalIgnoreCase)
            .Select(pair => pair.Key + "=" + pair.Value)) + "\0\0";
        IntPtr environment = Marshal.StringToHGlobalUni(block);
        try
        {
            var startup = new StartupInfo
            {
                cb = Marshal.SizeOf<StartupInfo>(),
                lpDesktop = desktopName,
                dwFlags = StartfUseShowWindow,
                wShowWindow = 0 // SW_HIDE
            };
            var commandLine = new StringBuilder(Quote(info.FileName));
            IEnumerable<string> arguments = info.ArgumentList.Count > 0 ? info.ArgumentList.Select(Quote) : new[] { info.Arguments };
            foreach (var argument in arguments)
                if (argument.Length > 0) commandLine.Append(' ').Append(argument);

            string? directory = string.IsNullOrEmpty(info.WorkingDirectory) ? null : info.WorkingDirectory;
            if (!CreateProcess(info.FileName, commandLine, IntPtr.Zero, IntPtr.Zero, false,
                    CreateNoWindow | CreateUnicodeEnvironment, environment, directory, ref startup, out var created))
                throw new Win32Exception(Marshal.GetLastWin32Error(), "Windows could not start the game server on its hidden desktop.");

            Process process;
            CloseHandle(created.hThread);
            try { process = Process.GetProcessById(created.dwProcessId); }
            catch
            {
                CloseHandle(created.hProcess);
                throw;
            }

            IntPtr owned = desktop;
            desktop = IntPtr.Zero; // now closed by the watcher below
            // Keep the original handle until the host exits. Reopening by PID can lose
            // the exit status if the process has already been removed by Windows.
            var exitCode = Task.Run<uint?>(() =>
            {
                try
                {
                    return WaitForSingleObject(created.hProcess, Infinite) == WaitObject0
                        && GetExitCodeProcess(created.hProcess, out uint code) ? code : null;
                }
                finally
                {
                    CloseHandle(created.hProcess);
                    CloseDesktop(owned);
                }
            });
            ExitCodes.Add(process, exitCode);
            return process;
        }
        finally
        {
            Marshal.FreeHGlobal(environment);
            if (desktop != IntPtr.Zero) CloseDesktop(desktop);
        }
    }

    // Windows command-line quoting (the same rules ProcessStartInfo.ArgumentList uses).
    private static string Quote(string argument)
    {
        if (argument.Length > 0 && argument.IndexOfAny(new[] { ' ', '\t', '"' }) < 0) return argument;
        var quoted = new StringBuilder("\"");
        int backslashes = 0;
        foreach (char ch in argument)
        {
            if (ch == '\\') { backslashes++; continue; }
            if (ch == '"') { quoted.Append('\\', backslashes * 2 + 1).Append('"'); backslashes = 0; continue; }
            quoted.Append('\\', backslashes).Append(ch);
            backslashes = 0;
        }
        quoted.Append('\\', backslashes * 2).Append('"');
        return quoted.ToString();
    }
}
