// Syndicate © 2026 Syndicate. All rights reserved. Proprietary — no copying, distribution, or derivative works. See LICENSE.
using System;
using System.IO;
using System.Text;
using System.Text.RegularExpressions;
using System.Collections.Concurrent;
namespace SyndicateClassic;
internal static class GameBuild
{
    // Chapter 2 Season 1 (11.00) is supported in public and Dev Kit builds.
    internal const bool Chapter2=true;
    // Chapter 3 Season 1 (19.00): Dev Kit only, EXPERIMENTAL. 19.00 is Unreal Engine 5; the Erbium match
    // server is UE4-only, so selecting this build lets you boot-test but will NOT host a working match yet.
    // Never enabled in public builds.
#if TILTED_DEVKIT
    internal const bool Chapter3=true;
#else
    internal const bool Chapter3=false;
#endif
    private static readonly ConcurrentDictionary<string,(long Length,long Written,string Build)> Cache=new(StringComparer.OrdinalIgnoreCase);
    internal static string Detect(string executable)
    {
        var file=new FileInfo(executable);
        if(!file.Exists)throw new IOException("Choose an extracted Fortnite game folder first.");
        if(Cache.TryGetValue(file.FullName,out var cached)&&cached.Length==file.Length&&cached.Written==file.LastWriteTimeUtc.Ticks)return cached.Build;
        using var stream=File.OpenRead(file.FullName);
        var buffer=new byte[1024*1024+256];int carry=0,read;string found="";
        while((read=stream.Read(buffer,carry,buffer.Length-carry))>0){
            int count=carry+read;
            foreach(var text in new[]{Encoding.Unicode.GetString(buffer,0,count-count%2),Encoding.ASCII.GetString(buffer,0,count)}){
                string build=DetectMarker(text);
                if(build.Length>0){if(found.Length>0&&found!=build)throw new IOException("Conflicting Fortnite build markers.");found=build;}
            }
            carry=Math.Min(256,count);Buffer.BlockCopy(buffer,count-carry,buffer,0,carry);
        }
        if(found.Length==0)throw new IOException("This game build is not supported. Select Fortnite 1.7.2, 1.8.2, 1.11, 3.5.1, 4.5, 5.10, 6.20, 7.40, 8.50, 9.40, 10.31"+(Chapter2?" or 11.00 or 12.50":"")+(Chapter3?" or 19.00 (experimental)":"")+".");
        Cache[file.FullName]=(file.Length,file.LastWriteTimeUtc.Ticks,found);return found;
    }
    internal static string DetectMarker(string text)
    {
        if(Regex.IsMatch(text,@"[+][+]Fortnite[+]Release-3\.5-CL-4000805(?![0-9])",RegexOptions.CultureInvariant))return "3.5.1";
        if(text.Contains("++Fortnite+Release-Cert-CL-3807424",StringComparison.Ordinal))return "1.11";
        if(text.Contains("++Fortnite+Release-4.5-CL-4166199",StringComparison.Ordinal))return "4.5";
        if(text.Contains("++Fortnite+Release-Live-CL-3741772",StringComparison.Ordinal) || text.Contains("++Fortnite+Release-Cert-CL-3741772",StringComparison.Ordinal))return "1.8.2";
        if(text.Contains("++Fortnite+Release-Live-CL-3724489",StringComparison.Ordinal))return "1.8";
        // Pre-season: 1.7.2 is changelist 3700114 (the exe names no version number).
        if(text.Contains("++Fortnite+Release-Live-CL-3700114",StringComparison.Ordinal) || text.Contains("++Fortnite+Release-Cert-CL-3700114",StringComparison.Ordinal))return "1.7.2";
        // Chapter 2 Season 1: 11.00 (CL 9603448). Supported in every launcher build.
        if(Chapter2 && Regex.IsMatch(text,@"[+][+]Fortnite[+]Release-11\.00-CL-[0-9]+",RegexOptions.CultureInvariant))return "11.00";
        // Chapter 2 Season 2: 12.50 (TOP SECRET). Match any 12.xx point release in the 12.50 line.
        if(Chapter2 && Regex.IsMatch(text,@"[+][+]Fortnite[+]Release-12\.[0-9]+-CL-[0-9]+",RegexOptions.CultureInvariant))return "12.50";
        // Chapter 3 Season 1: 19.00 (UE5). Dev Kit only, experimental (no UE5 server yet).
        if(Chapter3 && Regex.IsMatch(text,@"[+][+]Fortnite[+]Release-19\.00-CL-[0-9]+",RegexOptions.CultureInvariant))return "19.00";
        var later=Regex.Match(text,@"[+][+]Fortnite[+]Release-(6\.20|7\.40|8\.50|9\.40|10\.31)-CL-[0-9]+",RegexOptions.CultureInvariant);
        if(later.Success)return later.Groups[1].Value;
        var extra=Regex.Match(text,@"[+][+]Fortnite[+]Release-(1\.7\.2|1\.8\.2|1\.8|3\.5\.1|5\.10)-CL-[0-9]+",RegexOptions.CultureInvariant);
        return extra.Success?extra.Groups[1].Value:"";
    }
}

// Season 6 and later quit at login ("app environment code 4" / "Fortnite was not started correctly")
// unless they look like they were started by the Epic launcher with anti-cheat. Give them the launcher
// arguments and start FortniteLauncher.exe and the EAC stub frozen (they never run), then close those
// two again when the game exits.
internal static class EpicLaunchStub
{
    private const string Arguments = " -nobe -fromfl=eac -fltoken=3db3ba5dcbd2e16703f3978d -caldera=eyJhbGciOiJFUzI1NiIsInR5cCI6IkpXVCJ9.eyJhY2NvdW50X2lkIjoiYmU5ZGE1YzJmYmVhNDQwN2IyZjQwZWJhYWQ4NTlhZDQiLCJnZW5lcmF0ZWQiOjE2Mzg3MTcyNzgsImNhbGRlcmFHdWlkIjoiMzgxMGI4NjMtMmE2NS00NDU3LTliNTgtNGRhYjNiNDgyYTg2IiwiYWNQcm92aWRlciI6IkVhc3lBbnRpQ2hlYXQiLCJub3RlcyI6IiIsImZhbGxiYWNrIjpmYWxzZX0.VAWQB67RTxhiWOxx7DBjnzDnXyyEnX7OljJm-j2d88G_WgwQ9wrE6lwMEHZHjBd1ISJdUO1UVUqkfLdU5nofBQ";

    internal static bool Needed(string? build)
    {
        if (string.IsNullOrEmpty(build)) return false;
        var major = build.Split('.')[0];
        return int.TryParse(major, out var number) && number >= 6;
    }

    internal static string ArgumentsFor(string? build) => Needed(build) ? Arguments : "";

    internal static string? BuildOf(string gameExe)
    {
        try { return GameBuild.Detect(gameExe); } catch { return null; }
    }

    // Call right before starting the game; pass the started game to Attach afterwards.
    internal static System.Collections.Generic.List<System.Diagnostics.Process> StartFrozen(string gameExe, string? build)
    {
        var started = new System.Collections.Generic.List<System.Diagnostics.Process>();
        if (!Needed(build)) return started;
        var folder = Path.GetDirectoryName(gameExe)!;
        foreach (var name in new[] { "FortniteLauncher.exe", "FortniteClient-Win64-Shipping_EAC.exe" })
        {
            var path = Path.Combine(folder, name);
            if (!File.Exists(path)) continue;
            try
            {
                var startup = new StartupInfo { cb = System.Runtime.InteropServices.Marshal.SizeOf<StartupInfo>() };
                var command = new StringBuilder("\"" + path + "\"" + Arguments);
                const uint CreateSuspended = 0x4, CreateNoWindow = 0x08000000;
                if (CreateProcessW(path, command, IntPtr.Zero, IntPtr.Zero, false, CreateSuspended | CreateNoWindow, IntPtr.Zero, folder, ref startup, out var info))
                {
                    CloseHandle(info.hThread); CloseHandle(info.hProcess);
                    try { started.Add(System.Diagnostics.Process.GetProcessById((int)info.dwProcessId)); } catch { }
                }
            }
            catch { }
        }
        return started;
    }

    internal static void Attach(System.Diagnostics.Process? game, System.Collections.Generic.List<System.Diagnostics.Process> stubs)
    {
        if (stubs.Count == 0) return;
        void Close() { foreach (var stub in stubs) { try { if (!stub.HasExited) stub.Kill(); } catch { } } }
        if (game == null) { Close(); return; }
        System.Threading.Tasks.Task.Run(() => { try { game.WaitForExit(); } catch { } Close(); });
    }

    [System.Runtime.InteropServices.StructLayout(System.Runtime.InteropServices.LayoutKind.Sequential, CharSet = System.Runtime.InteropServices.CharSet.Unicode)]
    private struct StartupInfo
    {
        public int cb; public string? lpReserved, lpDesktop, lpTitle;
        public int dwX, dwY, dwXSize, dwYSize, dwXCountChars, dwYCountChars, dwFillAttribute, dwFlags;
        public short wShowWindow, cbReserved2; public IntPtr lpReserved2, hStdInput, hStdOutput, hStdError;
    }

    [System.Runtime.InteropServices.StructLayout(System.Runtime.InteropServices.LayoutKind.Sequential)]
    private struct ProcessInformation { public IntPtr hProcess, hThread; public uint dwProcessId, dwThreadId; }

    [System.Runtime.InteropServices.DllImport("kernel32.dll", CharSet = System.Runtime.InteropServices.CharSet.Unicode, SetLastError = true)]
    private static extern bool CreateProcessW(string application, StringBuilder command, IntPtr processAttributes, IntPtr threadAttributes, bool inheritHandles, uint flags, IntPtr environment, string directory, ref StartupInfo startup, out ProcessInformation info);

    [System.Runtime.InteropServices.DllImport("kernel32.dll")]
    private static extern bool CloseHandle(IntPtr handle);
}