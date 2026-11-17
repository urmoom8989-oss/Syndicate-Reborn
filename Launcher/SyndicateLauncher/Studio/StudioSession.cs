using System;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Text.Json;
using SyndicateLauncher.Services;

namespace SyndicateLauncher.Studio
{
    /// <summary>Where the Studio finds the match: the Runtime\Server folder Erbium writes its live files to, and the
    /// season (Fortnite version) selected in the launcher.</summary>
    internal sealed record LauncherPaths(string ServerDir, string? BuildVersion);

    /// <summary>
    /// The link between Syndicate Studio and the launcher's Play. While the Studio window is open it rewrites
    /// studio-session.json every 0.7 s; the launcher starts the Erbium match server with the Studio's options while
    /// that file is fresh (a file older than 20 s means the Studio closed):
    /// TILTED_DEV_LIVE=1 (live match files, map editor, dev commands, fly), the Studio's playlist, and for a Studio
    /// match TILTED_STUDIO=1 with no bots (Erbium keeps the match on the warm-up island: no bus, no storm).
    /// The file lives in the shared %LOCALAPPDATA%\ProjectTilted folder with the other dev switches Erbium and the
    /// client modules read (dev-fly-on.txt, dev-fly-speed.txt, ...).
    /// </summary>
    public static class StudioSession
    {
        public sealed record Options(bool StudioMatch, string Playlist);

        public const string ModuleName = "SyndicateStudio.dll";

        static string Folder => Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "ProjectTilted");
        static string SessionFile => Path.Combine(Folder, "studio-session.json");
        static string PlaylistFile => Path.Combine(Folder, "studio-playlist.txt");
        static readonly string[] Playlists = { "solo", "duo", "squad", "50v50", "playground", "arenasolo", "creative" };

        /// <summary>The Studio's options while its window is open, otherwise null (a normal launch).</summary>
        public static Options? Current()
        {
            try
            {
                if (!File.Exists(SessionFile) || DateTime.UtcNow - File.GetLastWriteTimeUtc(SessionFile) > TimeSpan.FromSeconds(20)) return null;
                return JsonSerializer.Deserialize<Options>(File.ReadAllText(SessionFile));
            }
            catch (Exception) { return null; }
        }

        internal static void Write(bool studioMatch)
        {
            Directory.CreateDirectory(Folder);
            File.WriteAllText(SessionFile, JsonSerializer.Serialize(new Options(studioMatch, ReadMode())));
        }

        internal static void Clear()
        {
            try { File.Delete(SessionFile); } catch (Exception) { }
        }

        /// <summary>Adds the Studio's settings to an Erbium match server about to start. Returns the playlist it runs.</summary>
        public static string Apply(ProcessStartInfo host, Options? studio, Action<string>? record = null)
        {
            if (studio is null)
            {
                host.Environment.Remove("TILTED_STUDIO");
                return host.Environment.TryGetValue("TILTED_PLAYLIST", out var current) && current is not null ? current : "solo";
            }
            host.Environment["TILTED_DEV_LIVE"] = "1";
            host.Environment["TILTED_PLAYLIST"] = studio.Playlist;
            if (studio.StudioMatch)
            {
                host.Environment["TILTED_STUDIO"] = "1";
                host.Environment["TILTED_BOT_COUNT"] = "0";
            }
            else host.Environment.Remove("TILTED_STUDIO");
            record?.Invoke("Syndicate Studio is open: " + studio.Playlist + (studio.StudioMatch ? ", Studio match (no bots, endless warm-up)" : ", normal match") + ", dev live data on");
            return studio.Playlist;
        }

        // ---- playlist ------------------------------------------------------------------------------------
        internal static string ReadMode()
        {
            try
            {
                var mode = File.ReadAllText(PlaylistFile).Trim().ToLowerInvariant();
                return Playlists.Contains(mode) ? mode : "solo";
            }
            catch (Exception) { return "solo"; }
        }

        internal static void SaveMode(string mode)
        {
            Directory.CreateDirectory(Folder);
            File.WriteAllText(PlaylistFile, Playlists.Contains(mode) ? mode : "solo");
        }

        // ---- the launcher's session ----------------------------------------------------------------------
        /// <summary>Studio matches run on Erbium, which serves the Chapter 1 seasons.</summary>
        internal static bool SupportsBuild(string? version) => version is not null && GameLauncher.IsSyndicateBuild(version);

        /// <summary>True while the launcher's game client is running.</summary>
        internal static bool ClientRunning() => GameLauncher.Client is { } client && !SafeExited(client);

        internal static bool OwnsProcess(int pid) => GameLauncher.OwnsProcess(pid);

        /// <summary>The match server's engine log (-abslog) for the current session, if any.</summary>
        internal static string? HostEngineLog() => GameLauncher.HostLog is { } log && File.Exists(log) ? log : null;

        /// <summary>Ends the launcher's session (server, game, backend) without waiting (UI thread: closing, cancel).</summary>
        internal static void EndMatchNow() => GameLauncher.StopOwned();

        /// <summary>Ends the launcher's session (server, game, backend) and waits up to 15 s for Fortnite to close, so
        /// Restart's new launch doesn't find the old processes still exiting.</summary>
        internal static void StopMatch()
        {
            GameLauncher.StopOwned();
            var waited = Stopwatch.StartNew();
            while (waited.Elapsed < TimeSpan.FromSeconds(15))
            {
                var left = Process.GetProcessesByName("FortniteClient-Win64-Shipping");
                bool any = left.Length > 0;
                foreach (var p in left) p.Dispose();
                if (!any) return;
                System.Threading.Thread.Sleep(250);
            }
        }

        static bool SafeExited(Process process)
        {
            try { return process.HasExited; } catch (Exception) { return true; }
        }

        // ---- SyndicateStudio.dll -------------------------------------------------------------------------
        /// <summary>Runtime\dlls\SyndicateStudio.dll (built by UPDATE FROM GIT).</summary>
        internal static string StudioModule => Path.Combine(GameLauncher.RuntimeFolder, "dlls", ModuleName);

        /// <summary>Where the loaded copies go (named by hash, so the original can be replaced while a game runs).</summary>
        internal static string ModuleCopyFolder => Path.Combine(SettingsService.DataDirectory, "StudioModules");
    }
}
