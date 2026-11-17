using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Net;
using System.Net.Http;
using System.Net.Http.Json;
using System.Net.Sockets;
using System.Text.Json;
using System.Threading.Tasks;

namespace SyndicateLauncher.Services;

/// <summary>
/// Brings players from a Project Tilted install on this PC into Syndicate, once, after the backend starts.
/// Tilted kept its accounts in a MongoDB at %LOCALAPPDATA%\ProjectTilted\UserData\Database. That folder is
/// only read: it is copied to %LOCALAPPDATA%\SyndicateLauncher\tilted-import, a second MongoDB is started
/// on the copy, and the backend (POST /launcher/import-legacy) adds the players it does not have yet.
/// The copy is deleted afterwards. A name already used in Syndicate comes in as "&lt;name&gt; Tilted".
/// The import runs again only when the Tilted database has changed since the last one.
/// </summary>
public static class TiltedImport
{
    /// <summary>A player added from Tilted: its name here and whether its locker had Unlock All on.</summary>
    public sealed record ImportedPlayer(string Name, bool UnlockAll);

    /// <summary>Raised (on a worker thread) with the players that were added.</summary>
    public static event Action<IReadOnlyList<ImportedPlayer>>? PlayersImported;

    private static bool _attempted;

    private static string Local => Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData);
    private static string WorkDirectory => Path.Combine(LegacyData.LocalDirectory, "tilted-import");
    private static string MarkerPath => Path.Combine(LegacyData.LocalDirectory, "tilted-import.json");
    private static string LogPath => Path.Combine(LegacyData.LocalDirectory, "Logs", "tilted-import.log");

    private sealed record Marker(string Source, long Stamp);

    /// <summary>The Tilted account database, or null when this PC has none.</summary>
    public static string? FindSource()
    {
        string root = Path.Combine(Local, "ProjectTilted", "UserData");
        var candidates = new List<string>();
        if (File.Exists(Path.Combine(root, "use-tilted-database.txt"))) candidates.Add(Path.Combine(root, "Database-Tilted"));
        candidates.Add(Path.Combine(root, "Database"));
        foreach (var candidate in candidates)
            if (File.Exists(Path.Combine(candidate, "WiredTiger")) && File.Exists(Path.Combine(candidate, "_mdb_catalog.wt")))
                return candidate;
        return null;
    }

    /// <summary>True when this PC has a Tilted database that has not been imported (or changed since).</summary>
    public static bool Pending
    {
        get
        {
            try
            {
                string? source = FindSource();
                if (source is null || SamePath(source, LegacyData.DatabaseDirectory)) return false;
                return !(ReadMarker() is { } marker && SamePath(marker.Source, source) && marker.Stamp == Stamp(source));
            }
            catch { return false; }
        }
    }

    /// <summary>Called by SyndicateBackend once the API answers. Never throws.</summary>
    internal static async Task RunOnceAsync(string? mongod, HttpClient http, Action<string> status)
    {
        if (_attempted) return;
        _attempted = true;
        try
        {
            string? source = FindSource();
            if (source is null || mongod is null) return;
            if (SamePath(source, LegacyData.DatabaseDirectory)) return; // Syndicate already runs on it

            long stamp = Stamp(source);
            if (ReadMarker() is { } marker && SamePath(marker.Source, source) && marker.Stamp == stamp) return;
            if (TiltedDatabaseRunning(source))
            {
                Log("Project Tilted's database is running; the import is tried again on the next start.");
                return;
            }

            status("Importing your Project Tilted players...");
            var names = await ImportAsync(source, mongod, http);
            if (names is null) return;
            WriteMarker(new Marker(source, stamp));
            Log(names.Count == 0 ? "No new players in " + source + "." : "Imported " + string.Join(", ", names.Select(n => n.Name)) + " from " + source + ".");
            if (names.Count > 0) PlayersImported?.Invoke(names);
        }
        catch (Exception e)
        {
            Log("Import failed: " + e.Message);
        }
    }

    private static async Task<List<ImportedPlayer>?> ImportAsync(string source, string mongod, HttpClient http)
    {
        string work = WorkDirectory;
        DeleteQuietly(work);
        CopyDatabase(source, work);

        int port = FreePort();
        var info = new ProcessStartInfo(mongod)
        {
            UseShellExecute = false,
            CreateNoWindow = true,
            WindowStyle = ProcessWindowStyle.Hidden,
            WorkingDirectory = Path.GetDirectoryName(mongod)!
        };
        foreach (var argument in new[]
        {
            "--dbpath", work,
            "--bind_ip", "127.0.0.1",
            "--port", port.ToString(),
            "--logpath", Path.Combine(work, "mongodb.log")
        }) info.ArgumentList.Add(argument);

        using var process = Process.Start(info) ?? throw new InvalidOperationException("Could not start MongoDB for the import.");
        try
        {
            var clock = Stopwatch.StartNew();
            while (!PortOpen(port))
            {
                if (process.HasExited) throw new InvalidOperationException("MongoDB could not open the copy of the Tilted database.");
                if (clock.Elapsed > TimeSpan.FromSeconds(30)) throw new TimeoutException("MongoDB did not start for the import.");
                await Task.Delay(200);
            }

            using var response = await http.PostAsJsonAsync("launcher/import-legacy", new { port });
            string text = await response.Content.ReadAsStringAsync();
            if (response.StatusCode == HttpStatusCode.NotFound)
            {
                Log("This backend has no import route yet; the import is tried again after an update.");
                return null;
            }
            if (!response.IsSuccessStatusCode)
            {
                Log("The backend refused the import (" + (int)response.StatusCode + "): " + text);
                return null;
            }
            using var document = JsonDocument.Parse(text);
            var players = new List<ImportedPlayer>();
            foreach (var entry in document.RootElement.GetProperty("imported").EnumerateArray())
            {
                string name = entry.TryGetProperty("username", out var u) ? u.GetString() ?? "" : "";
                bool unlockAll = entry.TryGetProperty("unlockAll", out var a) && a.ValueKind == JsonValueKind.True;
                if (name.Length > 0) players.Add(new ImportedPlayer(name, unlockAll));
            }
            return players;
        }
        finally
        {
            try { if (!process.HasExited) { process.Kill(true); process.WaitForExit(10000); } } catch { }
            for (int i = 0; i < 10 && Directory.Exists(work); i++)
            {
                DeleteQuietly(work);
                if (Directory.Exists(work)) await Task.Delay(300);
            }
        }
    }

    private static readonly HashSet<string> SkippedFiles = new(StringComparer.OrdinalIgnoreCase)
        { "mongod.lock", "WiredTiger.lock", "mongodb.log" };
    private static readonly HashSet<string> SkippedFolders = new(StringComparer.OrdinalIgnoreCase)
        { "diagnostic.data", "_tmp" };

    /// <summary>Reads the Tilted folder (shared, never locked or changed) into the work folder.</summary>
    private static void CopyDatabase(string from, string to)
    {
        Directory.CreateDirectory(to);
        foreach (var file in Directory.EnumerateFiles(from))
        {
            string name = Path.GetFileName(file);
            if (SkippedFiles.Contains(name) || name.StartsWith("mongodb.log", StringComparison.OrdinalIgnoreCase)) continue;
            if ((File.GetAttributes(file) & FileAttributes.ReparsePoint) != 0) continue;
            using var input = new FileStream(file, FileMode.Open, FileAccess.Read, FileShare.ReadWrite | FileShare.Delete);
            using var output = new FileStream(Path.Combine(to, name), FileMode.CreateNew, FileAccess.Write, FileShare.None);
            input.CopyTo(output);
        }
        foreach (var folder in Directory.EnumerateDirectories(from))
        {
            string name = Path.GetFileName(folder);
            if (SkippedFolders.Contains(name) || (File.GetAttributes(folder) & FileAttributes.ReparsePoint) != 0) continue;
            CopyDatabase(folder, Path.Combine(to, name));
        }
    }

    /// <summary>The newest change to the database files (logs and diagnostics left out).</summary>
    private static long Stamp(string folder)
    {
        long newest = 0;
        foreach (var file in Directory.EnumerateFiles(folder, "*", SearchOption.AllDirectories))
        {
            string name = Path.GetFileName(file);
            if (SkippedFiles.Contains(name) || name.StartsWith("mongodb.log", StringComparison.OrdinalIgnoreCase)) continue;
            string relative = Path.GetRelativePath(folder, file);
            if (SkippedFolders.Any(f => relative.StartsWith(f + Path.DirectorySeparatorChar, StringComparison.OrdinalIgnoreCase))) continue;
            newest = Math.Max(newest, File.GetLastWriteTimeUtc(file).Ticks);
        }
        return newest;
    }

    /// <summary>mongod writes its process ID into mongod.lock while it runs and empties it on a clean stop.</summary>
    private static bool TiltedDatabaseRunning(string folder)
    {
        try
        {
            string path = Path.Combine(folder, "mongod.lock");
            if (!File.Exists(path)) return false;
            string text;
            using (var stream = new FileStream(path, FileMode.Open, FileAccess.Read, FileShare.ReadWrite | FileShare.Delete))
            using (var reader = new StreamReader(stream))
                text = reader.ReadToEnd().Trim();
            if (!int.TryParse(text, out int pid)) return false;
            using var process = Process.GetProcessById(pid);
            return !process.HasExited && process.ProcessName.Contains("mongod", StringComparison.OrdinalIgnoreCase);
        }
        catch (ArgumentException) { return false; } // that process is gone (left by a crash)
        catch (IOException) { return true; }       // locked: play safe and try next time
        catch (UnauthorizedAccessException) { return true; }
        catch (InvalidOperationException) { return false; }
    }

    private static Marker? ReadMarker()
    {
        try { return File.Exists(MarkerPath) ? JsonSerializer.Deserialize<Marker>(File.ReadAllText(MarkerPath)) : null; }
        catch { return null; }
    }

    private static void WriteMarker(Marker marker)
    {
        Directory.CreateDirectory(LegacyData.LocalDirectory);
        File.WriteAllText(MarkerPath, JsonSerializer.Serialize(marker));
    }

    private static bool SamePath(string a, string b)
    {
        try
        {
            return string.Equals(Path.GetFullPath(a).TrimEnd(Path.DirectorySeparatorChar),
                Path.GetFullPath(b).TrimEnd(Path.DirectorySeparatorChar), StringComparison.OrdinalIgnoreCase);
        }
        catch { return false; }
    }

    private static int FreePort()
    {
        var listener = new TcpListener(IPAddress.Loopback, 0);
        listener.Start();
        try { return ((IPEndPoint)listener.LocalEndpoint).Port; }
        finally { listener.Stop(); }
    }

    private static bool PortOpen(int port)
    {
        try
        {
            using var client = new TcpClient();
            return client.ConnectAsync("127.0.0.1", port).Wait(250) && client.Connected;
        }
        catch { return false; }
    }

    private static void DeleteQuietly(string folder)
    {
        try { if (Directory.Exists(folder)) Directory.Delete(folder, true); }
        catch (IOException) { }
        catch (UnauthorizedAccessException) { }
    }

    private static void Log(string line)
    {
        try
        {
            Directory.CreateDirectory(Path.GetDirectoryName(LogPath)!);
            File.AppendAllText(LogPath, DateTime.Now.ToString("yyyy-MM-dd HH:mm:ss") + " " + line + Environment.NewLine);
        }
        catch { }
    }
}
