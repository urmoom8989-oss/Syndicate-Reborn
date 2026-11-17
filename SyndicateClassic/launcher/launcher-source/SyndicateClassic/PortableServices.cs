// Syndicate © 2026 Syndicate. All rights reserved. Proprietary — no copying, distribution, or derivative works. See LICENSE.
using System;
using System.Diagnostics;
using System.IO;
using System.Net.Sockets;

namespace SyndicateClassic;

internal static class PortableServices
{
	internal static string Node(LauncherPaths paths)
    {
        string bundled=Path.Combine(paths.Root,"Dependencies","Node","node.exe");
        if(File.Exists(bundled))return bundled;
        foreach(string folder in (Environment.GetEnvironmentVariable("PATH")??"").Split(Path.PathSeparator))
        {
            try { string candidate=Path.Combine(folder.Trim().Trim('"'),"node.exe");if(File.Exists(candidate))return candidate; }
            catch(ArgumentException) { }
        }
        throw new FileNotFoundException("Node.js is missing. Restore Dependencies/Node/node.exe from the complete Syndicate runtime.",bundled);
    }

    // Syndicate: every install now keeps players in the shared store under
    // %LOCALAPPDATA%\ProjectTilted\UserData (the bundled database on port 27028).
    internal const string SharedMongoUri = "mongodb://127.0.0.1:27028/project_tilted";

    // The MONGO_URI written in .env, used once to import players from an older database.
    internal static string? ConfiguredMongoUri(string envText)
    {
        foreach(string raw in envText.Split('\n'))
        {
            string line=raw.Trim();int equals=line.IndexOf('=');
            if(equals<0 || !line[..equals].Trim().Equals("MONGO_URI",StringComparison.OrdinalIgnoreCase))continue;
            string value=line[(equals+1)..].Trim().Trim('"','\'');
            return value.StartsWith("mongodb://",StringComparison.OrdinalIgnoreCase)?value:null;
        }
        return null;
    }

    internal static bool UsesBundledDatabase(string envText)
    {
        foreach(string raw in envText.Split('\n'))
        {
            string line=raw.Trim();int equals=line.IndexOf('=');
            if(equals<0 || !line[..equals].Trim().Equals("MONGO_URI",StringComparison.OrdinalIgnoreCase))continue;
            string value=line[(equals+1)..].Trim().Trim('"','\'');
            return Uri.TryCreate(value,UriKind.Absolute,out var uri)
                && uri.Scheme=="mongodb" && uri.Port==27028
                && (uri.Host.Equals("127.0.0.1",StringComparison.OrdinalIgnoreCase)||uri.Host.Equals("localhost",StringComparison.OrdinalIgnoreCase));
        }
        return false;
    }

    // Run before either updater can touch the installation, even without pressing Launch.
    // forceSnapshot: a repair takes the account copy up front, as before. A normal start only takes it when the
    // newest copy is overdue; otherwise it is taken when Syndicate closes (AccountStorage.SnapshotAfterStop).
    internal static void PreserveExistingAccounts(bool forceSnapshot = false)
    {
        if (!Launcher.TryAutoLocate(out var paths, out _)) return;
        string env = Path.Combine(paths.BackendDir, ".env");
        using var client = new TcpClient();
        bool running = false;
        try { running = client.ConnectAsync("127.0.0.1", 27028).Wait(250) && client.Connected; }
        catch (AggregateException) { }
        if (running)
        {
            // Never copy live WiredTiger files. A migrated store already survives deletion.
            if (File.Exists(Path.Combine(AccountStorage.Database, "WiredTiger"))) return;
            throw new IOException("Close the other Syndicate launcher and its local services, then reopen this launcher so it can preserve your existing accounts before updating. Your original database has not been changed.");
        }
        AccountStorage.PrepareDatabase(Path.Combine(paths.Root, "Runtime", "Database"), snapshot: forceSnapshot || AccountStorage.SnapshotOverdue());
    }

	internal static void Prepare(LauncherPaths paths)
	{
        // Safety net for any caller that did not wait: the background component update must finish
        // before services start from those files. Apply runs on the thread pool, so this cannot deadlock.
        App.ComponentsReady.Wait();
        if(App.ComponentFailure != null) throw new IOException("Repair is required before starting local services. Open Settings > Repair Local Services.\n" + App.ComponentFailure);
		string text = Path.Combine(paths.Root, "Dependencies", "MongoDB", "mongod.exe");
		Directory.CreateDirectory(Path.Combine(paths.BackendDir, "src", "ClientSettings"));
		string path = Path.Combine(paths.BackendDir, "tokens.json");
		if (!File.Exists(path))
		{
			File.WriteAllText(path, "{\"accessTokens\":[],\"refreshTokens\":[],\"clientTokens\":[]}");
		}
		string path2 = Path.Combine(paths.BackendDir, ".env");
		if (!File.Exists(path2))
		{
			File.WriteAllText(path2, "PORT=3551\nXMPP_PORT=70\nMONGO_URI=mongodb://127.0.0.1:27028/project_tilted\nMATCHMAKER_IP=127.0.0.1:6969\nGAMESERVER_IP=127.0.0.1:7777\nMAIN_SEASON=4\nEVENT_TYPE=Arena\nBACKEND_NAME=Syndicate\nBOT_TOKEN=\nDISCORD_TOKEN=\n");
		}
		// Always run the shared account database; players from an older external database
		// are imported by the backend (LEGACY_MONGO_URI), never moved or deleted.
		if (!File.Exists(text)) throw new FileNotFoundException("The configured bundled database is missing. Restore Dependencies/MongoDB/mongod.exe.", text);
		if (SessionProcesses.OwnedRunning(paths, "database"))
		{
			return;
		}
		using (TcpClient tcpClient = new TcpClient())
		{
			try
			{
				if (tcpClient.ConnectAsync("127.0.0.1", 27028).Wait(250) && tcpClient.Connected)
				{
					throw new InvalidOperationException("Port 27028 is already used by another database. Close the other Syndicate installation first.");
				}
			}
			catch (AggregateException)
			{
			}
		}
		string text2 = AccountStorage.PrepareDatabase(Path.Combine(paths.Root, "Runtime", "Database"), snapshot: AccountStorage.SnapshotOverdue());
        // The shared account folder can belong to another launcher (for example Project Springs) that
        // made it with a different MongoDB version. Then Syndicate uses its own folder instead and remembers
        // that; the shared folder is never changed.
        string ownDatabase = Path.Combine(AccountStorage.Root, "Database-Tilted");
        string ownMarker = Path.Combine(AccountStorage.Root, "use-tilted-database.txt");
        if (File.Exists(ownMarker)) text2 = ownDatabase;
		Directory.CreateDirectory(text2);
        string log = Path.Combine(text2,"mongodb.log");
        string primary = text;
        string alternate = Path.Combine(paths.Root,"Dependencies","MongoDB","v9","mongod.exe");
        if (RequiresVersion9(log) && File.Exists(alternate)) text = alternate;
        for(int attempt=0;attempt<3;attempt++)
        {
            var info = new ProcessStartInfo(text) { UseShellExecute=false, CreateNoWindow=true, WindowStyle=ProcessWindowStyle.Hidden, WorkingDirectory=Path.GetDirectoryName(text)! };
            foreach(var argument in new[]{"--dbpath",text2,"--bind_ip","127.0.0.1","--port","27028","--logpath",log,"--logappend"}) info.ArgumentList.Add(argument);
            using var process=Process.Start(info) ?? throw new InvalidOperationException("Could not start the local database.");
            SessionProcesses.Track(paths,"database",process);
            var deadline=Stopwatch.StartNew();
            while(!process.HasExited && deadline.Elapsed < TimeSpan.FromSeconds(30))
            {
                using var probe=new TcpClient();
                try { if(probe.ConnectAsync("127.0.0.1",27028).Wait(100) && probe.Connected) return; } catch(AggregateException) {}
                System.Threading.Thread.Sleep(100);
            }
            if(!process.HasExited) throw new InvalidOperationException("MongoDB is still starting after 30 seconds. No database files were removed. Log: "+log);
            uint code=unchecked((uint)process.ExitCode);
            // Exit code 14 (0xE): the most common "won't start" — a stale mongod.lock or an index/catalog left
            // dirty by an unclean shutdown (power loss, or the launcher/PC was killed). Clear the stale lock and
            // retry; if it persists, run a repair pass. The account data itself is never deleted.
            if(code==0xE)
            {
                try { var lockFile=Path.Combine(text2,"mongod.lock"); if(File.Exists(lockFile)) File.Delete(lockFile); } catch {}
                if(attempt>=1)
                {
                    try {
                        var repair=new ProcessStartInfo(text){ UseShellExecute=false, CreateNoWindow=true, WindowStyle=ProcessWindowStyle.Hidden, WorkingDirectory=Path.GetDirectoryName(text)! };
                        foreach(var a in new[]{"--dbpath",text2,"--repair","--bind_ip","127.0.0.1","--port","27028","--logpath",log,"--logappend"}) repair.ArgumentList.Add(a);
                        using var rp=Process.Start(repair); rp?.WaitForExit(90000);
                    } catch {}
                }
                if(attempt<2) continue;
            }
            if(code==62 && text!=alternate && RequiresVersion9(log) && File.Exists(alternate)) { text=alternate; continue; }
            if(code==62 && !string.Equals(text2, ownDatabase, StringComparison.OrdinalIgnoreCase))
            {
                text2 = ownDatabase; Directory.CreateDirectory(text2); log = Path.Combine(text2,"mongodb.log"); text = primary;
                try { File.WriteAllText(ownMarker, "The shared account folder was made by a different MongoDB version, so Syndicate keeps its players in Database-Tilted. Delete this file to try the shared folder again."); } catch { }
                continue;
            }
            string reason=code switch {
                62 => "The saved database uses a different MongoDB version. It was preserved; do not delete it or reinstall over the account folder.",
                0xC0000139 => "MongoDB could not load a required Windows function. Run the latest Syndicate repair to install the compatible database executable.",
                0xC000001D => "MongoDB encountered an unsupported CPU instruction.",
                0xC0000135 => "MongoDB is missing a runtime DLL. Run Repair Local Services.",
                0xE => "MongoDB could not start after an unclean shutdown. Syndicate tried to clear the stale lock and repair it automatically; if it keeps happening, run Repair Local Services. Your account database was not deleted.",
                _ => "MongoDB exited during startup."
            };
            throw new InvalidOperationException(reason+" (code 0x"+code.ToString("X8")+").\nDatabase log: "+log+"\nYour account database has not been deleted or reset.");
        }
    }

    internal static bool RequiresVersion9(string log)
    {
        if(!File.Exists(log))return false;
        using var stream=new FileStream(log,FileMode.Open,FileAccess.Read,FileShare.ReadWrite);
        stream.Seek(Math.Max(0,stream.Length-262144),SeekOrigin.Begin);
        using var reader=new StreamReader(stream);
        string? line; bool requires=false;
        while((line=reader.ReadLine())!=null)try {
            using var json=System.Text.Json.JsonDocument.Parse(line);
            var entry=json.RootElement;
            if(entry.TryGetProperty("msg",out var msg) && msg.GetString()=="Wrong mongod version" && entry.TryGetProperty("attr",out var attr) && attr.TryGetProperty("error",out var error))
                requires=error.GetString()?.Contains("version: \"9.0\"",StringComparison.Ordinal)==true;
            if(entry.TryGetProperty("msg",out msg) && msg.GetString()=="Build Info" && entry.TryGetProperty("attr",out attr) && attr.TryGetProperty("buildInfo",out var build) && build.TryGetProperty("version",out var version) && version.GetString()?.StartsWith("9.0.",StringComparison.Ordinal)==true) requires=true;
        }catch(System.Text.Json.JsonException){}
        return requires;
    }
}