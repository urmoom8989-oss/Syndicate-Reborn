// Syndicate © 2026 Syndicate. All rights reserved. Proprietary — no copying, distribution, or derivative works. See LICENSE.
using System;
using System.Diagnostics;
using System.IO;
using System.Text.Json;
using System.Threading;
using System.ComponentModel;
using System.Runtime.InteropServices;
using System.Text;
using Microsoft.Win32.SafeHandles;

namespace SyndicateClassic;

internal static class SessionProcesses
{
	private sealed record Owned(int Pid, long Started, string Exe);
    [DllImport("kernel32.dll", SetLastError=true)]
    private static extern SafeProcessHandle OpenProcess(uint access, bool inherit, int pid);
    [DllImport("kernel32.dll", CharSet=CharSet.Unicode, SetLastError=true)]
    private static extern bool QueryFullProcessImageName(SafeProcessHandle process, uint flags, StringBuilder path, ref int size);
    private static string ImagePath(Process process)
    {
        // Query the executable directly; module enumeration can fail with error 299 during startup.
        using var handle=OpenProcess(0x1000,false,process.Id);
        if(handle.IsInvalid)throw new Win32Exception(Marshal.GetLastWin32Error());
        var path=new StringBuilder(32768);int size=path.Capacity;
        if(!QueryFullProcessImageName(handle,0,path,ref size))throw new Win32Exception(Marshal.GetLastWin32Error());
        return path.ToString();
    }

	private static string FileFor(LauncherPaths p, string name)
	{
		return Path.Combine(p.Root, "Runtime", "Server", name + ".owned.json");
	}

	public static void Track(LauncherPaths paths, string name, Process process)
	{
		string text = null;
		for (int i = 0; i < 40; i++)
		{
			if (text != null)
			{
				break;
			}
			process.Refresh();
			if (process.HasExited)
			{
				throw new InvalidOperationException(name + " exited during startup.");
			}
			try { text = ImagePath(process); }
            catch (Win32Exception) when (i < 39) { Thread.Sleep(25); continue; }
			if (text != null && !text.EndsWith(".exe", StringComparison.OrdinalIgnoreCase))
			{
				text = null;
			}
			if (text == null)
			{
				Thread.Sleep(25);
			}
		}
		if (text == null)
		{
			throw new InvalidOperationException("Could not identify " + name + " after startup.");
		}
		Directory.CreateDirectory(Path.GetDirectoryName(FileFor(paths, name)));
		File.WriteAllText(FileFor(paths, name), JsonSerializer.Serialize(new Owned(process.Id, process.StartTime.ToUniversalTime().Ticks, text)));
	}

	private static Process? Find(LauncherPaths paths, string name)
	{
		try
		{
			Owned owned = JsonSerializer.Deserialize<Owned>(File.ReadAllText(FileFor(paths, name)));
			if (owned == null)
			{
				return null;
			}
			Process processById = Process.GetProcessById(owned.Pid);
			return (!processById.HasExited && processById.StartTime.ToUniversalTime().Ticks == owned.Started && string.Equals(ImagePath(processById), owned.Exe, StringComparison.OrdinalIgnoreCase)) ? processById : null;
		}
		catch
		{
			return null;
		}
	}

    public static bool OwnedMatches(LauncherPaths paths,string name,int pid,long started)
    {
        using var process=Find(paths,name);
        return process!=null && process.Id==pid && process.StartTime.ToUniversalTime().Ticks==started;
    }

	public static bool OwnedRunning(LauncherPaths paths, string name)
	{
		return Find(paths, name) != null;
	}

	public static bool ClientRunning(LauncherPaths paths)
	{
		return Find(paths, "client") != null;
	}

	public static void StopHost(LauncherPaths paths, int pid, long started)
	{
		Process process = Find(paths, "host");
		if (process != null)
		{
			if (process.Id != pid || process.StartTime.ToUniversalTime().Ticks != started)
			{
				throw new InvalidOperationException("Host changed during restart.");
			}
			process.Kill(entireProcessTree: true);
			if (!process.WaitForExit(10000))
			{
				throw new InvalidOperationException("Host did not stop.");
			}
		}
	}

	public static void StopClient(LauncherPaths paths)
	{
		Process process = Find(paths, "client");
		if (process != null)
		{
			process.Kill(entireProcessTree: true);
			if (!process.WaitForExit(10000))
			{
				throw new InvalidOperationException("Fortnite did not stop.");
			}
		}
	}

    // Stops one process this installation started (used for a backend that is running but no longer answers).
    public static void StopOwned(LauncherPaths paths,string name)
    {
        using var process=Find(paths,name);
        if(process==null)return;
        try{process.Kill(entireProcessTree:true);}catch(InvalidOperationException) when(process.HasExited){return;}
        if(!process.WaitForExit(10000))throw new InvalidOperationException("Could not stop "+name+".");
    }

    public static void StopUnresponsiveMatchmaker(LauncherPaths paths)
    {
        using var process=Find(paths,"matchmaker");
        if(process==null)return;
        process.Kill(entireProcessTree:true);
        if(!process.WaitForExit(10000))throw new InvalidOperationException("Syndicate matchmaking did not stop. Close Syndicate and reopen it.");
    }

    public static void StopAll(LauncherPaths paths, bool keepAccountServices = false)
    {
        var errors=new System.Collections.Generic.List<Exception>();
        foreach(var name in keepAccountServices ? new[]{"client","host","matchmaker"} : new[]{"client","host","matchmaker","backend","database"})
        {
            try
            {
                using var process=Find(paths,name);
                if(process==null || process.HasExited)continue;
                try{
                    if(name=="database") {
                        // MongoDB's Windows shutdown signal flushes WiredTiger before exit.
                        using var signal=EventWaitHandle.OpenExisting(@"Global\Mongo_"+process.Id);
                        signal.Set();
                        if(!process.WaitForExit(30000))throw new IOException("MongoDB is still saving. Wait a moment and close Syndicate again; it has not been forcibly terminated.");
                    } else process.Kill(entireProcessTree:true);
                }
                catch(InvalidOperationException) when(process.HasExited){continue;}
                if(!process.WaitForExit(10000))throw new InvalidOperationException("Could not stop "+name+".");
            }
            catch(Exception error){errors.Add(new InvalidOperationException("Cleanup failed for "+name+".",error));}
        }
        if(keepAccountServices){if(errors.Count>0)throw new AggregateException("Some Syndicate game services could not be stopped.",errors);return;}
        // Catch orphaned backend/matchmaker instances even if an earlier crash lost
        // the ownership record. Only this installation's bundled Node is eligible.
        string bundledNode=Path.GetFullPath(Path.Combine(paths.Root,"Dependencies","Node","node.exe"));
        foreach(var process in Process.GetProcessesByName("node"))using(process) {
            bool belongs=false;
            try {
                if(process.HasExited || !string.Equals(ImagePath(process),bundledNode,StringComparison.OrdinalIgnoreCase))continue;
                belongs=true;
                process.Kill(entireProcessTree:true);
                if(!process.WaitForExit(10000))throw new IOException("A Syndicate Node process did not stop.");
            }catch(InvalidOperationException) when(process.HasExited){}
            catch(Win32Exception error) { if(belongs)errors.Add(error); /* Never stop unverified processes. */ }
            catch(Exception error){errors.Add(error);}
        }
        if(errors.Count>0)throw new AggregateException("Some Syndicate services could not be stopped.",errors);
    }

    // Another Syndicate installation's services (an older version folder, a second copy): its Node backend and MongoDB
    // hold ports 3551/27017, so this launcher could not load players ("Close the other launcher and its services")
    // and players had to end node.exe / mongod.exe in Task Manager by hand. Stop them here instead. Only processes
    // whose executable is a Syndicate bundle (...\Dependencies\Node\node.exe, ...\Dependencies\MongoDB\...\mongod.exe)
    // are touched, never a Node or MongoDB the player installed for something else, and never while a Fortnite
    // started by that other copy is still running. Returns how many processes were stopped.
    public static int StopOtherSyndicateServices(LauncherPaths paths, Action<string> log)
    {
        if (Process.GetProcessesByName("FortniteClient-Win64-Shipping").Length > 0) { log("Another Syndicate copy's services are running, but Fortnite is open: close Fortnite first."); return 0; }
        string ownNode = Path.GetFullPath(Path.Combine(paths.Root, "Dependencies", "Node", "node.exe"));
        string ownMongo = Path.GetFullPath(Path.Combine(paths.Root, "Dependencies", "MongoDB"));
        static bool SyndicateBundle(string image, string kind) {
            var p = image.Replace('/', '\\');
            return kind == "node" ? p.EndsWith("\\Dependencies\\Node\\node.exe", StringComparison.OrdinalIgnoreCase)
                                  : p.Contains("\\Dependencies\\MongoDB\\", StringComparison.OrdinalIgnoreCase) && p.EndsWith("mongod.exe", StringComparison.OrdinalIgnoreCase);
        }
        int stopped = 0;
        // Node first (it writes to the database), then MongoDB with its own clean shutdown signal.
        foreach (var kind in new[] { "node", "mongod" })
            foreach (var process in Process.GetProcessesByName(kind)) using (process) {
                try {
                    if (process.HasExited) continue;
                    var image = Path.GetFullPath(ImagePath(process));
                    if (!SyndicateBundle(image, kind)) continue;
                    // This installation's own services are handled by the normal checks.
                    if (kind == "node" ? string.Equals(image, ownNode, StringComparison.OrdinalIgnoreCase) : image.StartsWith(ownMongo + "\\", StringComparison.OrdinalIgnoreCase)) continue;
                    if (kind == "mongod") {
                        try { using var signal = EventWaitHandle.OpenExisting(@"Global\Mongo_" + process.Id); signal.Set(); } catch { }
                        if (!process.WaitForExit(30000)) process.Kill(entireProcessTree: true);
                    } else process.Kill(entireProcessTree: true);
                    process.WaitForExit(10000);
                    ++stopped;
                    log("Stopped another Syndicate copy's " + (kind == "node" ? "backend" : "database") + " (" + image + ").");
                }
                catch (InvalidOperationException) when (process.HasExited) { }
                catch (Exception error) { log("Could not stop another Syndicate copy's " + kind + ": " + error.Message); }
            }
        return stopped;
    }
}