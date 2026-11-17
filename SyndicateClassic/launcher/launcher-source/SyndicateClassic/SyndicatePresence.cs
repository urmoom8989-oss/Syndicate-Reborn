// Syndicate © 2026 Syndicate. All rights reserved. Proprietary — no copying, distribution, or derivative works. See LICENSE.
using System;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Text;
using System.Text.Json;
using System.Threading.Tasks;
using System.Windows.Threading;

namespace SyndicateClassic;

// Small per-PC launcher preferences (%LOCALAPPDATA%\ProjectTilted\launcher-prefs.json).
internal static class LauncherPrefs
{
    private sealed class Data { public bool discordStatus { get; set; } = true; public bool playStationControllers { get; set; } = true; }
    private static readonly string FilePath = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "ProjectTilted", "launcher-prefs.json");
    private static Data? _cached;
    private static Data Load() { if (_cached != null) return _cached; try { _cached = JsonSerializer.Deserialize<Data>(File.ReadAllText(FilePath)); } catch { } return _cached ??= new Data(); }
    private static void Save() { try { Directory.CreateDirectory(Path.GetDirectoryName(FilePath)!); File.WriteAllText(FilePath, JsonSerializer.Serialize(Load())); } catch { } }
    internal static bool DiscordStatus { get => Load().discordStatus; set { Load().discordStatus = value; Save(); } }
    internal static bool PlayStationControllers { get => Load().playStationControllers; set { Load().playStationControllers = value; Save(); } }
}

// Discord status and controller bridge: what the launcher tells Discord, and when PlayStation pads
// are mirrored (only while a game started from Syndicate runs).
//   launcher open                "Edwin · Syndicate v0.4.3"                  "Picking a season: Chapter 1 Season 4 (4.5)"
//   game running, no match       "Chapter 1 Season 4 (4.5) · Edwin"       "In the lobby"
//   match in progress            "Chapter 1 Season 4 (4.5) · Edwin"       "In a match · 37 players left"
// The name is the signed-in Syndicate account's display name; signed out, the launcher version shows instead.
// Match state comes from the match server's own event file (Runtime\Server\telemetry): an open file
// written in the last 90 s is a running match, and its latest "alive" count is the players left.
public partial class MainWindow
{
    private DiscordPresence? _presence;
    private ControllerService? _controllers;
    private DispatcherTimer? _presenceTimer;
    private readonly DateTimeOffset _launcherStarted = DateTimeOffset.UtcNow;
    private DateTimeOffset _gameStarted;
    private string? _playingBuild;
    private bool _presenceBusy;

    private void StartPresenceAndControllers()
    {
        _presence = new DiscordPresence(message => Dispatcher.InvokeAsync(() => Log(message)));
        _presence.Start(true); // Discord status is always on: the Settings tab and toggle were removed, so it can never be turned off. It still only shows when the Discord app is running.
        _presenceTimer = new DispatcherTimer { Interval = TimeSpan.FromSeconds(4) };
        _presenceTimer.Tick += async (_, __) => await UpdatePresence();
        _presenceTimer.Start();
        _ = UpdatePresence();
        StartControllers();
        Closed += (_, __) => { _presenceTimer?.Stop(); _presence?.Dispose(); try { _controllers?.Dispose(); } catch { } };
    }

    // Separate method: if the controller libraries are missing (older install), only this fails.
    private void StartControllers()
    {
        try
        {
            _controllers = new ControllerService(message => Dispatcher.InvokeAsync(() => Log(message)));
            _controllers.Changed += () => Dispatcher.InvokeAsync(() => ControllersChanged?.Invoke());
            _controllers.Start(LauncherPrefs.PlayStationControllers);
        }
        catch (Exception error)
        {
            _controllers = null;
            Log("Controller support is unavailable: " + error.GetBaseException().Message + " (Xbox controllers still work in game).");
        }
    }

    // Raised on the UI thread when a controller is connected or removed (Settings > Controllers listens).
    internal event Action? ControllersChanged;

    private void OnGameStarted(Process game, string? build)
    {
        _gameStarted = DateTimeOffset.UtcNow;
        _playingBuild = build;
        try { _controllers?.SetGameRunning(true); } catch { }
        _ = UpdatePresence();
    }

    private void OnGameExited()
    {
        _playingBuild = null;
        try { _controllers?.SetGameRunning(false); } catch { }
        _ = UpdatePresence();
    }

    private string SeasonLabel(string? build)
    {
        var season = build == null ? null : ModernSeasons.FirstOrDefault(s => s.Build == build || build.StartsWith(s.Build + ".", StringComparison.Ordinal));
        if (season == null) return build == null ? "Fortnite" : "Fortnite " + build;
        // Numbers 0-10 are Chapter 1 (0 = Pre-season), 11 and up Chapter 2.
        return "Chapter " + (season.Number >= 11 ? 2 : 1) + " " + (season.Number == 0 ? "Pre-season" : "Season " + SeasonName(season.Number)) + " (" + season.Build + ")";
    }

    private async Task UpdatePresence()
    {
        if (_presence == null || _presenceBusy) return;
        _presenceBusy = true;
        try
        {
            string version = "Syndicate v" + LauncherVersion();
            string? name = string.IsNullOrWhiteSpace(_account.Username) ? null : _account.Username!.Trim();
            var game = _game;
            bool playing = game != null && !game.HasExited;
            if (!playing)
            {
                var hero = ModernSeasons.FirstOrDefault(s => s.Number == _heroSeason);
                _presence.Set(new PresenceInfo(name != null ? name + " · " + version : version + " · Launcher", "Picking a season" + (hero != null ? ": " + SeasonLabel(hero.Build) : ""), _launcherStarted));
                return;
            }
            var folder = MatchTelemetry.Folder(_paths);
            var live = await Task.Run(() => ReadLiveMatch(folder));
            string details = SeasonLabel(_playingBuild) + " · " + (name ?? version);
            string state = live.InMatch ? (live.Alive is int alive && alive > 0 ? $"In a match · {alive} players left" : "In a match") : "In the lobby";
            _presence.Set(new PresenceInfo(details, state, _gameStarted, OwnerPid: game!.Id));
        }
        catch { }
        finally { _presenceBusy = false; }
    }

    // Reads the end of the newest match file: running if it has no match_end and was written recently.
    internal static (bool InMatch, int? Alive) ReadLiveMatch(string? folder)
    {
        try
        {
            if (folder == null || !Directory.Exists(folder)) return (false, null);
            var newest = new DirectoryInfo(folder).EnumerateFiles("*.jsonl").OrderByDescending(f => f.LastWriteTimeUtc).FirstOrDefault();
            if (newest == null || DateTime.UtcNow - newest.LastWriteTimeUtc > TimeSpan.FromSeconds(90)) return (false, null);
            using var stream = new FileStream(newest.FullName, FileMode.Open, FileAccess.Read, FileShare.ReadWrite | FileShare.Delete);
            long start = Math.Max(0, stream.Length - 64 * 1024);
            stream.Seek(start, SeekOrigin.Begin);
            using var reader = new StreamReader(stream, Encoding.UTF8);
            var lines = reader.ReadToEnd().Split('\n');
            int? alive = null;
            for (int i = lines.Length - 1; i >= 0; i--)
            {
                var line = lines[i].Trim();
                if (line.Length == 0 || line[0] != '{') continue;
                if (line.Contains("\"type\":\"match_end\"")) return (false, null);
                if (alive == null)
                    try
                    {
                        using var doc = JsonDocument.Parse(line);
                        if (doc.RootElement.TryGetProperty("alive", out var a) && a.TryGetInt32(out var n)) alive = n;
                    }
                    catch (JsonException) { } // the first line of the tail can be cut in half
                if (alive != null) break;
            }
            return (true, alive);
        }
        catch { return (false, null); }
    }
}