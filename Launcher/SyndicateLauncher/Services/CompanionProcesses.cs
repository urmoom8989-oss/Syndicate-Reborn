using System;
using System.ComponentModel;
using System.Diagnostics;
using System.IO;
using System.Runtime.InteropServices;
using SyndicateLauncher.Models;

namespace SyndicateLauncher.Services;

internal static class CompanionProcesses
{
    // Reboot creates these installation-provided companions before each game
    // instance and keeps their primary threads paused for the session lifetime.
    private static readonly string[] Names = { "FortniteLauncher.exe", "FortniteClient-Win64-Shipping_EAC.exe" };

    public static void Validate(BuildInfo build)
    {
        foreach (var name in Names)
            if (!File.Exists(Path.Combine(Path.GetDirectoryName(build.ExecutablePath)!, name)))
                throw new FileNotFoundException("The selected Fortnite installation is missing its launcher companion: " + name);
    }

    public static void Start(BuildInfo build, string arguments, Action<Process> register)
    {
        Validate(build);
        var directory = Path.GetDirectoryName(build.ExecutablePath)!;
        foreach (var name in Names)
        {
            var path = Path.Combine(directory, name);
            var startup = new StartupInfo { Size = Marshal.SizeOf<StartupInfo>(), Flags = 1, ShowWindow = 0 };
            var command = new System.Text.StringBuilder("\"" + path + "\"" + arguments);
            if (!CreateProcess(path, command.ToString(), IntPtr.Zero, IntPtr.Zero, false,
                0x00000004 | 0x08000000, IntPtr.Zero, directory, ref startup, out var info))
                throw new Win32Exception(Marshal.GetLastWin32Error(), "Could not start Fortnite companion " + name);
            try
            {
                var process = Process.GetProcessById(info.ProcessId);
                try { register(process); }
                catch { if (!process.HasExited) process.Kill(); process.Dispose(); throw; }
            }
            finally
            {
                CloseHandle(info.Thread);
                CloseHandle(info.Process);
            }
        }
    }

    [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
    private struct StartupInfo
    {
        public int Size;
        public string? Reserved, Desktop, Title;
        public int X, Y, XSize, YSize, XCountChars, YCountChars, FillAttribute, Flags;
        public short ShowWindow, ReservedSize;
        public IntPtr ReservedData, StandardInput, StandardOutput, StandardError;
    }
    [StructLayout(LayoutKind.Sequential)]
    private struct ProcessInfo { public IntPtr Process, Thread; public int ProcessId, ThreadId; }
    [DllImport("kernel32.dll", EntryPoint = "CreateProcessW", CharSet = CharSet.Unicode, SetLastError = true)]
    private static extern bool CreateProcess(string application, string? commandLine, IntPtr processAttributes,
        IntPtr threadAttributes, bool inheritHandles, uint flags, IntPtr environment, string directory,
        ref StartupInfo startup, out ProcessInfo process);
    [DllImport("kernel32.dll")] private static extern bool CloseHandle(IntPtr handle);
}
