// Syndicate © 2026 Syndicate. All rights reserved. Proprietary — no copying, distribution, or derivative works. See LICENSE.
using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Security.Cryptography;
using System.Text.Json;

namespace SyndicateClassic;

internal static class ComponentUpdates
{
	private sealed record Entry(string Path, string Sha256);

    // Set when optional files could not be restored. The launcher still opens and
    // matches still work; only the named feature (for example a shop rotation) is affected.
    internal static string? Warning;

    internal static string Destination(string root, string relative)
    {
        relative=BackendFolder.Logical(relative.Replace('\\','/'));
        if(relative.Split('/').Any(p=>p.Length==0 || p=="." || p==".." || p.Contains(':')))
            throw new IOException("Invalid component update path.");
        bool server=relative=="Erbium-Quality-of-life-changes-main/Erbium/x64/Release/Erbium.dll.pending";
        bool node=relative=="Dependencies/Node/node.exe" || relative=="Dependencies/Node/LICENSE" || relative=="Dependencies/Node/msvcp140.dll" || relative=="Dependencies/Node/vcruntime140.dll" || relative=="Dependencies/Node/vcruntime140_1.dll";
        bool backend=(relative.StartsWith("Helix-backend-main/src/",StringComparison.Ordinal)
                      ||relative.StartsWith("Helix-backend-main/Matchmaker/",StringComparison.Ordinal))
                     && !relative.Contains("/ClientSettings/",StringComparison.OrdinalIgnoreCase)
                     && (relative.EndsWith(".js",StringComparison.Ordinal)||relative.EndsWith(".cjs",StringComparison.Ordinal)||relative.EndsWith(".json",StringComparison.Ordinal));
        bool dependency=(relative.StartsWith("Dependencies/MongoDB/",StringComparison.Ordinal) || relative.StartsWith("Helix-backend-main/node_modules/",StringComparison.Ordinal) || relative.StartsWith("Helix-backend-main/Matchmaker/node_modules/",StringComparison.Ordinal)) && !relative.Split('/').Any(p=>p.StartsWith(".") && p!=".bin");
        bool backendRoot=relative is "Helix-backend-main/package.json" or "Helix-backend-main/package-lock.json" or "Helix-backend-main/start.bat";
        bool staticAsset=relative is "Helix-backend-main/src/CloudDir/Full.ini" or "Helix-backend-main/src/CloudDir/Nebula.chunk" or "Helix-backend-main/src/CloudDir/Nebula.manifest" or "Helix-backend-main/src/CloudDir/Syndicate.chunk" or "Helix-backend-main/src/CloudDir/Syndicate.manifest" or "Helix-backend-main/src/CloudStorage/DefaultEngine.ini" or "Helix-backend-main/src/CloudStorage/DefaultGame.ini" or "Helix-backend-main/src/CloudStorage/DefaultInput.ini" or "Helix-backend-main/src/CloudStorage/DefaultRuntimeOptions.ini";
        if(!server && !node && !backend && !dependency && !backendRoot && !staticAsset)throw new IOException("Invalid component update path.");
        return Path.Combine(root,BackendFolder.Map(root,relative).Replace('/',Path.DirectorySeparatorChar));
    }

    // Files without which a match or the account service cannot start.
    // Everything else (shop pools, pass data, cosmetics lists) only degrades one feature.
    private static bool Critical(string relative)
    {
        relative=BackendFolder.Logical(relative.Replace('\\','/'));
        if(relative.EndsWith("Erbium.dll.pending",StringComparison.Ordinal))return true;
        if(relative.StartsWith("Dependencies/",StringComparison.Ordinal))return true;
        if(relative.EndsWith(".js",StringComparison.Ordinal) || relative.EndsWith(".cjs",StringComparison.Ordinal))return true;
        return relative is "Helix-backend-main/package.json";
    }

    private static string Hash(string file)=>Convert.ToHexString(SHA256.HashData(File.ReadAllBytes(file)));

    // The bundled payload is the first choice. If one file is missing from it (an
    // interrupted update or a rollback can remove a single file), reuse an identical
    // copy kept by an earlier update backup instead of refusing to start.
    private static string? FindSource(string payloadRoot, string backupRoot, Entry entry)
    {
        var relative=entry.Path.Replace('/',Path.DirectorySeparatorChar);
        var primary=Path.Combine(payloadRoot,relative);
        try{if(File.Exists(primary) && Hash(primary).Equals(entry.Sha256,StringComparison.OrdinalIgnoreCase))return primary;}catch(IOException){}
        if(!Directory.Exists(backupRoot))return null;
        foreach(var folder in Directory.EnumerateDirectories(backupRoot).OrderByDescending(Directory.GetLastWriteTimeUtc))
        {
            var candidate=Path.Combine(folder,relative);
            try{if(File.Exists(candidate) && Hash(candidate).Equals(entry.Sha256,StringComparison.OrdinalIgnoreCase))return candidate;}catch(IOException){}
        }
        return null;
    }

	public static void Apply()=>Apply(false);

	public static void Apply(bool forceRepair)
	{
        Warning=null;
		bool repair=forceRepair || File.Exists(Path.Combine(AppContext.BaseDirectory,"repair.pending"));
        string path = Path.Combine(AppContext.BaseDirectory, "components");
		string path2 = Path.Combine(path, "manifest.json");
		if (!File.Exists(path2)) return;
        if (!Launcher.TryAutoLocate(out LauncherPaths paths, out string _))
            throw new IOException("Syndicate could not find its backend folder next to the launcher. Keep the Launcher folder inside your Syndicate folder, then reopen it.");
		string text = Convert.ToHexString(SHA256.HashData(File.ReadAllBytes(path2)));
		string path3 = Path.Combine(paths.Root, "Runtime", "components-version.txt");
        string backupRoot = Path.Combine(paths.Root, "Runtime", "UpdateBackups");
		Entry[] array = JsonSerializer.Deserialize<Entry[]>(File.ReadAllText(path2)) ?? throw new IOException("Missing component manifest.");
        bool Matches(Entry entry) {
            var target=Destination(paths.Root,entry.Path);
            if(target.EndsWith(".pending",StringComparison.Ordinal) && !File.Exists(target))target=target.Substring(0,target.Length-8);
            try{return File.Exists(target) && Hash(target).Equals(entry.Sha256,StringComparison.OrdinalIgnoreCase);}catch(IOException){return false;}
        }
        // Fast start: when this exact manifest was already installed and every file is still there,
        // open straight away. Hashing every file (node_modules, MongoDB...) on each start took a long
        // time. Settings > Repair Local Services still checks every file's contents.
        bool Present(Entry entry) {
            var target=Destination(paths.Root,entry.Path);
            if(target.EndsWith(".pending",StringComparison.Ordinal) && !File.Exists(target))target=target.Substring(0,target.Length-8);
            return File.Exists(target);
        }
        if (!repair && File.Exists(path3) && File.ReadAllText(path3) == text && array.Where(entry => Critical(entry.Path)).All(Present))
		{
			return;
		}
		if (SessionProcesses.ClientRunning(paths))
		{
			throw new IOException("Fortnite is still open. Close Fortnite, then reopen Syndicate to finish updating.");
		}
		var pending = new List<(Entry Entry, string Source)>();
        var missing = new List<string>();
		foreach (Entry entry in array.Where(entry => repair || !Matches(entry)))
		{
			Destination(paths.Root, entry.Path);
            if (Matches(entry)) continue; // repair: already correct, nothing to copy
			var source = FindSource(path, backupRoot, entry);
            if (source == null) { missing.Add(entry.Path); continue; }
            pending.Add((entry, source));
		}
        var criticalMissing = missing.Where(Critical).ToList();
        if (criticalMissing.Count > 0)
            throw new IOException("Some required Syndicate files are missing:\n  " + string.Join("\n  ", criticalMissing.Take(5)) +
                (criticalMissing.Count > 5 ? "\n  …and " + (criticalMissing.Count - 5) + " more" : "") +
                "\n\nReopen the Syndicate EXE (or extract the latest Syndicate package again) to restore them. Your accounts are safe.");
		if (pending.Count > 0) SessionProcesses.StopAll(paths);
		string path4 = Path.Combine(backupRoot, Guid.NewGuid().ToString("N"));
		var list = new List<(string Target, string Saved, bool Existed)>();
		try
		{
			foreach (var (entry2, source) in pending)
			{
				string text2 = Destination(paths.Root, entry2.Path);
				string text3 = Path.Combine(path4, entry2.Path);
				bool flag = File.Exists(text2);
				if (flag)
				{
					Directory.CreateDirectory(Path.GetDirectoryName(text3)!);
					File.Copy(text2, text3, overwrite: true);
				}
				list.Add((text2, text3, flag));
				Directory.CreateDirectory(Path.GetDirectoryName(text2)!);
				File.Copy(source, text2 + ".update", overwrite: true);
				File.Move(text2 + ".update", text2, overwrite: true);
			}
            // Verify what was copied; a missing optional file must not roll back everything else.
			if(!pending.All(p=>Matches(p.Entry)))throw new IOException("Installed match components did not pass verification; restoring the previous files.");
            // Only optional files can be missing here (critical ones stop above). Mark this version as
            // installed anyway: retrying on every start made launching slow and showed the same
            // note each time. Settings > Repair Local Services tries them again.
            Directory.CreateDirectory(Path.GetDirectoryName(path3)!);
            File.WriteAllText(path3, text);
		}
		catch
		{
			foreach (var item in list.AsEnumerable().Reverse())
			{
				try
				{
					if (item.Existed)
					{
						File.Copy(item.Saved, item.Target, overwrite: true);
					}
					else
					{
						File.Delete(item.Target);
					}
				}
				catch
				{
				}
			}
			throw;
		}
	}
}