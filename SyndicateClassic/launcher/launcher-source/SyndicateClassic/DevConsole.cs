// Syndicate © 2026 Syndicate. All rights reserved. Proprietary — no copying, distribution, or derivative works. See LICENSE.
using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Runtime.InteropServices;
using System.Text.Json;
using System.Threading.Tasks;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Interop;
using System.Windows.Media;
using System.Windows.Threading;
using IOPath = System.IO.Path;

namespace SyndicateClassic;

// Dev Console (Dev Kit builds only). Debugging aid for the match server and the bots:
//   * Insert (while Fortnite runs) opens / closes the console over the game: Players (everyone in the match
//     with team, human or bot, health, shield, state and distance), Server (phase, counts, storm, timings),
//     Logs (live tail of the match server and bot logs).
//   * Live radar: a small top-down map at the left of the game; every player as a dot in their team colour
//     (yours green, bots hollow), the storm circle, turned so you face up.
//   * Player markers: name, team, health and distance drawn over the game at each player's position, also
//     behind builds, so bot behaviour can be followed.
// Data: Runtime\Server\dev-live.json, written only by a match server this launcher started on this PC with
// TILTED_DEV_LIVE=1 (Dev Kit only), and the camera shared by TiltedClient.dll while markers are on. Public
// builds do not contain any of this (TILTED_DEVKIT), and it never works on another player's host.
public partial class MainWindow
{
#if TILTED_DEVKIT
    internal static bool DevConsoleEnabled { get => ModFlagOn("dev-console-on.txt"); set => SetHudFlag("dev-console-on.txt", value); }
    // The radar and the markers both use your game's camera and player positions (dev-camera-on.txt).
    internal static bool DevRadarEnabled
    {
        get => ModFlagOn("dev-radar-on.txt");
        set { SetHudFlag("dev-radar-on.txt", value); SetHudFlag("dev-camera-on.txt", value || ModFlagOn("dev-markers-on.txt")); }
    }
    internal static bool DevMarkersEnabled
    {
        get => ModFlagOn("dev-markers-on.txt");
        set { SetHudFlag("dev-markers-on.txt", value); SetHudFlag("dev-camera-on.txt", value || ModFlagOn("dev-radar-on.txt")); }
    }
    private static bool ModFlagOn(string name) { try { return File.Exists(HudFlag(name)); } catch { return false; } }
    // Dev Console switches (files read by TiltedClient.dll and the Dev Kit match server).
    internal static bool DevFlag(string name) => ModFlagOn(name);
    internal static void SetDevFlag(string name, bool on) => SetHudFlag(name, on);
    internal static string DevFlyKey
    {
        get { try { var f = HudFlag("dev-fly-key.txt"); if (File.Exists(f)) { var v = File.ReadAllText(f).Trim(); if (v.Length > 0) return v; } } catch { } return "key:119"; } // F8
        set { try { var f = HudFlag("dev-fly-key.txt"); Directory.CreateDirectory(IOPath.GetDirectoryName(f)!); File.WriteAllText(f, value); } catch { } }
    }

    internal sealed record DevPlayer(string Name, int Team, bool Bot, double X, double Y, double Z, double Yaw, double Hp, double Sh, bool Alive, bool Knocked, string State,
        int Skill = -1, int Mat = 0, double NoProgress = 0, int StuckStrikes = 0, double TargetDist = -1, bool EnemyVisible = false, string Playstyle = "", bool Pro = false,
        int Inv = 0, double NavDist = -1, string Weapon = "", string Why = "");
    internal sealed record DevSnapshot(int Pid, int Phase, double ZoneX, double ZoneY, double ZoneR, List<DevPlayer> Players, DateTime At);
    // A player as drawn: Live = the position comes from your own game this frame (exact); otherwise it is the
    // match server's latest position, carried forward along its movement.
    // Projected: Head/Foot are where your game itself draws that player this frame (viewport pixels).
    internal sealed record DevShown(DevPlayer P, double X, double Y, double Z, bool Live, bool Projected = false, double HeadX = 0, double HeadY = 0, double FootX = 0, double FootY = 0, bool Local = false);

    private DevSnapshot? _devNow, _devBefore;
    private double _devNowAt, _devBeforeAt; // when each snapshot arrived (seconds, Stopwatch)
    private bool _devHotkey, _devHotkeyWarned, _devRendering, _devStarted;
    private DispatcherTimer? _devGate;
    private DateTime _devNextConsole, _devNextReport;
    private string _devStat = "";
    private bool _devFlagConsole, _devFlagRadar, _devFlagMarkers;
    private DevConsoleWindow? _devConsole;
    private DevRadarWindow? _devRadar;
    private DevMarkersWindow? _devMarkers;
    private readonly DevFeed _devFeed = new();
    private const int DevHotkeyId = 0x7E1, VK_INSERT = 0x2D, MOD_NOREPEAT = 0x4000, WM_HOTKEY = 0x0312;

    private void StartDevConsole()
    {
        if (_devStarted) return;
        _devStarted = true;
        // Keep the camera share in step with the switches (it is cleared when radar and markers are off).
        SetHudFlag("dev-camera-on.txt", DevMarkersEnabled || DevRadarEnabled);
        // Twice a second: read the switches. While a game runs with any of them on, the radar and markers are
        // drawn on every frame the screen draws (not a 33 ms timer), so they move with the game. Otherwise
        // nothing runs per frame.
        _devGate = new DispatcherTimer { Interval = TimeSpan.FromMilliseconds(500) };
        _devGate.Tick += (_, __) => DevGate();
        _devGate.Start();
        var source = HwndSource.FromHwnd(new WindowInteropHelper(this).EnsureHandle());
        source?.AddHook((IntPtr hwnd, int msg, IntPtr wParam, IntPtr lParam, ref bool handled) =>
        {
            if (msg == WM_HOTKEY && wParam.ToInt32() == DevHotkeyId) { ToggleDevConsole(); handled = true; }
            return IntPtr.Zero;
        });
        Closed += (_, __) =>
        {
            _devGate?.Stop(); SetDevRendering(false); SetDevHotkey(false); _devFeed.Dispose();
            try { _devConsole?.Close(); _devRadar?.Close(); _devMarkers?.Close(); } catch { }
        };
    }

    private bool DevActive => _sessionStarted && _paths != null && !RadminConnection.Remote;

    // Insert is only taken while the console is switched on and a game runs; otherwise it stays a normal key.
    private void SetDevHotkey(bool on)
    {
        if (on == _devHotkey) return;
        var hwnd = new WindowInteropHelper(this).Handle;
        if (on)
        {
            _devHotkey = RegisterHotKey(hwnd, DevHotkeyId, MOD_NOREPEAT, VK_INSERT);
            if (!_devHotkey && !_devHotkeyWarned) { _devHotkeyWarned = true; Log("Dev Console: Insert is taken by another program (or another launcher window), so it can't open the console. Close that program."); }
            if (_devHotkey) _devHotkeyWarned = false;
        }
        else { UnregisterHotKey(hwnd, DevHotkeyId); _devHotkey = false; }
    }

    // Exclusive fullscreen (the stretched resolution sets it) draws over every window, so the console opened
    // behind the game and never showed. Then the game is minimized while the console is open and comes back
    // when it closes.
    private IntPtr _devMinimizedGame;
    private void ToggleDevConsole()
    {
        if (!DevActive) return;
        if (_devConsole == null)
        {
            _devConsole = new DevConsoleWindow(this);
            _devConsole.IsVisibleChanged += (_, __) =>
            {
                if (_devConsole.IsVisible) return;
                var back = _devMinimizedGame != IntPtr.Zero ? _devMinimizedGame : ArenaHudWindow.FindGameWindow();
                if (_devMinimizedGame != IntPtr.Zero) { ShowWindow(_devMinimizedGame, SW_RESTORE); _devMinimizedGame = IntPtr.Zero; }
                if (back != IntPtr.Zero) SetForegroundWindow(back); // straight back into the game
            };
        }
        if (_devConsole.IsVisible) { _devConsole.Hide(); return; }
        var game = ArenaHudWindow.FindGameWindow();
        if (game != IntPtr.Zero && SHQueryUserNotificationState(out int state) == 0 && state == QUNS_RUNNING_D3D_FULL_SCREEN)
        {
            ShowWindow(game, SW_MINIMIZE); _devMinimizedGame = game;
            Log("Dev Console: Fortnite is in exclusive fullscreen; minimized while the console is open.");
            game = IntPtr.Zero; // centre on the screen instead of the minimized window
        }
        _devConsole.ShowOver(game); _devConsole.Refresh(_devNow, MyDevName());
    }
    private const int SW_MINIMIZE = 6, SW_RESTORE = 9, QUNS_RUNNING_D3D_FULL_SCREEN = 3;
    [DllImport("user32.dll")] private static extern bool ShowWindow(IntPtr hWnd, int nCmdShow);
    [DllImport("shell32.dll")] private static extern int SHQueryUserNotificationState(out int state);

    private string MyDevName() => (_account.Username ?? "").Trim();

    // Dev Console commands for the match server (Dev Kit only): Runtime\Server\dev-command.txt, one line
    // "command|target|you". The match server (TILTED_DEV_LIVE) runs it and answers in dev-command-result.txt.
    // Returns null when sent, or why it could not be.
    internal string? SendDevCommand(string command, string target, string me)
    {
        if (!DevActive || _paths == null) return "No match on this PC is running.";
        if (_devNow == null) return "The match server is not sending live data yet.";
        try
        {
            var dir = IOPath.Combine(_paths.Root, "Runtime", "Server");
            File.AppendAllText(IOPath.Combine(dir, "dev-command.txt"), $"{command}|{target.Replace("|", "")}|{me.Replace("|", "")}\n");
            Log($"Dev Console: {command} {target}");
            return null;
        }
        catch (Exception error) { return "Could not send: " + error.Message; }
    }

    // The match server's answer to the last command, once; null when there is none.
    internal string? TakeDevCommandResult()
    {
        try
        {
            if (_paths == null) return null;
            var file = IOPath.Combine(_paths.Root, "Runtime", "Server", "dev-command-result.txt");
            if (!File.Exists(file)) return null;
            var text = File.ReadAllText(file).Trim(); File.Delete(file);
            return text.Length == 0 ? null : text;
        }
        catch { return null; }
    }

    // The match server's snapshot: from shared memory (30 a second) when a Dev Kit server runs on this PC,
    // otherwise from dev-live.json (twice a second).
    private void ReadDevLive()
    {
        if (!DevActive || !(_devFlagConsole || _devFlagRadar || _devFlagMarkers)) { _devNow = _devBefore = null; return; }
        DevSnapshot? snap = null;
        var json = _devFeed.ReadServer();
        if (json != null) snap = ParseDevJson(json, DateTime.UtcNow);
        else if (!_devFeed.ServerShared && (_devNow == null || DateTime.UtcNow - _devNow.At > TimeSpan.FromMilliseconds(400)))
        {
            var file = IOPath.Combine(_paths!.Root, "Runtime", "Server", "dev-live.json");
            try
            {
                if (File.Exists(file))
                {
                    var written = File.GetLastWriteTimeUtc(file);
                    if (DateTime.UtcNow - written < TimeSpan.FromSeconds(5) && (_devNow == null || written > _devNow.At)) snap = ParseDevJson(ReadClientLog(file), written);
                }
            }
            catch { }
        }
        if (snap != null) { _devBefore = _devNow; _devBeforeAt = _devNowAt; _devNow = snap; _devNowAt = DevFeed.Seconds(); }
        else if (_devNow != null && DateTime.UtcNow - _devNow.At > TimeSpan.FromSeconds(5)) _devNow = _devBefore = null; // the match server stopped
    }

    private static DevSnapshot? ParseDevJson(string text, DateTime at)
    {
        using var doc = JsonDocument.Parse(text);
        var r = doc.RootElement;
        double Num(JsonElement e, string n) => e.TryGetProperty(n, out var v) && v.ValueKind == JsonValueKind.Number ? v.GetDouble() : 0;
        var players = new List<DevPlayer>();
        if (r.TryGetProperty("players", out var list) && list.ValueKind == JsonValueKind.Array)
            foreach (var p in list.EnumerateArray())
                players.Add(new DevPlayer(p.TryGetProperty("n", out var n) ? n.GetString() ?? "" : "", (int)Num(p, "t"), Num(p, "b") > 0,
                    Num(p, "x"), Num(p, "y"), Num(p, "z"), Num(p, "yaw"), Num(p, "hp"), Num(p, "sh"), Num(p, "a") > 0, Num(p, "k") > 0,
                    p.TryGetProperty("s", out var s) ? s.GetString() ?? "" : "",
                    p.TryGetProperty("sk", out var sk) ? (int)sk.GetDouble() : -1, (int)Num(p, "mat"), Num(p, "np"), (int)Num(p, "ss"), p.TryGetProperty("tgt", out var tg) ? tg.GetDouble() : -1, Num(p, "vis") > 0, p.TryGetProperty("ps", out var ps) ? ps.GetString() ?? "" : "", Num(p, "pro") > 0,
                    (int)Num(p, "inv"), p.TryGetProperty("nav", out var nv) ? nv.GetDouble() : -1, p.TryGetProperty("wp", out var wp) ? wp.GetString() ?? "" : "", p.TryGetProperty("why", out var wy) ? wy.GetString() ?? "" : ""));
        double zx = 0, zy = 0, zr = 0;
        if (r.TryGetProperty("zone", out var z)) { zx = Num(z, "x"); zy = Num(z, "y"); zr = Num(z, "r"); }
        return new DevSnapshot((int)Num(r, "pid"), (int)Num(r, "phase"), zx, zy, zr, players, at);
    }

    // Where to draw everyone this frame. Players your game has loaded use their position from your game (same
    // frame as the camera: exact). Others use the server's position, carried forward along their last movement
    // for up to 0.15 s so they don't stutter between snapshots.
    private List<DevShown> DevPositions(DevFeed.Pawn[] pawns)
    {
        var now = _devNow; if (now == null) return new();
        var before = _devBefore;
        var old = before?.Players.GroupBy(p => p.Name).ToDictionary(g => g.Key, g => g.First());
        double span = Math.Max(0.02, _devNowAt - _devBeforeAt), ahead = Math.Min(0.15, DevFeed.Seconds() - _devNowAt);
        var used = new bool[pawns.Length];
        var byName = new Dictionary<string, int>(StringComparer.OrdinalIgnoreCase);
        for (int i = 0; i < pawns.Length; i++) if (pawns[i].Name.Length > 0) byName.TryAdd(pawns[i].Name, i);
        var shown = new List<DevShown>(now.Players.Count);
        // Your own body first: the server player nearest to it is you (names can differ between server and game).
        int local = Array.FindIndex(pawns, q => q.Local);
        DevPlayer? mine = null;
        if (local >= 0)
        {
            double best = 900 * 900;
            foreach (var p in now.Players)
            {
                if (!p.Alive || p.Bot) continue;
                double d = Math.Pow(pawns[local].X - p.X, 2) + Math.Pow(pawns[local].Y - p.Y, 2) + Math.Pow(pawns[local].Z - p.Z, 2);
                if (string.Equals(p.Name, pawns[local].Name, StringComparison.OrdinalIgnoreCase)) d = -1;
                if (d < best) { best = d; mine = p; }
            }
            if (mine != null) { used[local] = true; var m = pawns[local]; shown.Add(new DevShown(mine, m.X, m.Y, m.Z, true, false, 0, 0, 0, 0, true)); }
        }
        foreach (var p in now.Players)
        {
            if (ReferenceEquals(p, mine) || !p.Bot || !p.Alive || (p.X == 0 && p.Y == 0 && p.Z == 0)) continue; // bots only; out, or no body yet (still on the bus)
            double x = p.X, y = p.Y, z = p.Z;
            if (old != null && before != null && old.TryGetValue(p.Name, out var o) && o.Alive)
            { double k = ahead / span; x += (p.X - o.X) * k; y += (p.Y - o.Y) * k; z += (p.Z - o.Z) * k; }
            int match = -1;
            if (byName.TryGetValue(p.Name, out int named) && !used[named]) match = named;
            else
            {
                double best = 900 * 900; // the same player can't be 9 m away from where the server has them
                for (int i = 0; i < pawns.Length; i++)
                {
                    if (used[i]) continue;
                    double d = Math.Pow(pawns[i].X - x, 2) + Math.Pow(pawns[i].Y - y, 2) + Math.Pow(pawns[i].Z - z, 2);
                    if (d < best) { best = d; match = i; }
                }
            }
            if (match >= 0)
            {
                used[match] = true; var m = pawns[match];
                shown.Add(new DevShown(p, m.X, m.Y, m.Z, true, m.OnScreen, m.HeadX, m.HeadY, m.FootX, m.FootY, m.Local));
            }
            else shown.Add(new DevShown(p, x, y, z, false));
        }
        return shown;
    }

    private void DevGate()
    {
        try
        {
            _devFlagConsole = DevConsoleEnabled; _devFlagRadar = false; _devFlagMarkers = DevMarkersEnabled; // the radar was removed
            SetDevHotkey(_devFlagConsole && DevActive);
            bool on = DevActive && (_devFlagConsole || _devFlagRadar || _devFlagMarkers);
            SetDevRendering(on);
            // Every 5 s while it runs: what the radar and markers are working from, plus the client mods log,
            // into Runtime\Server so a problem can be looked at without digging through AppData.
            if (on && DateTime.UtcNow >= _devNextReport && _paths != null)
            {
                _devNextReport = DateTime.UtcNow.AddSeconds(5);
                var dir = IOPath.Combine(_paths.Root, "Runtime", "Server");
                var stat = $"{DateTime.Now:HH:mm:ss} {_devStat}{Environment.NewLine}";
                _ = Task.Run(() =>
                {
                    try { File.AppendAllText(IOPath.Combine(dir, "dev-markers.log"), stat); } catch { }
                    try { File.Copy(HudFlag("tilted-client.log"), IOPath.Combine(dir, "tilted-client.log"), true); } catch { }
                });
            }
            // Markers need the game's camera: keep that switch on while markers are on (it went missing after a crash).
            if (_devFlagMarkers && !ModFlagOn("dev-camera-on.txt")) SetHudFlag("dev-camera-on.txt", true);
            if (!on) { _devNow = _devBefore = null; _devRadar?.Hide(); _devMarkers?.Hide(); }
        }
        catch { }
    }

    private void SetDevRendering(bool on)
    {
        if (on == _devRendering) return;
        if (on) System.Windows.Media.CompositionTarget.Rendering += DevFrame;
        else System.Windows.Media.CompositionTarget.Rendering -= DevFrame;
        _devRendering = on;
    }

    private void DevFrame(object? sender, EventArgs e)
    {
        try
        {
            ReadDevLive();
            if (_devConsole?.IsVisible == true && DateTime.UtcNow >= _devNextConsole) { _devNextConsole = DateTime.UtcNow.AddMilliseconds(250); _devConsole.Refresh(_devNow, MyDevName()); }
            bool show = DevActive && _devNow != null && (_devFlagRadar || _devFlagMarkers);
            var game = show ? ArenaHudWindow.FindGameWindow() : IntPtr.Zero;
            if (game == IntPtr.Zero)
            {
                _devRadar?.Hide(); _devMarkers?.Hide();
                _devStat = !DevActive ? "dev console not active" : _devNow == null ? "no match data from the match server yet" : !(_devFlagRadar || _devFlagMarkers) ? "markers off" : "game window not found (minimized, or another window in front)";
                return;
            }
            int clientPid = 0;
            try { GetWindowThreadProcessId(game, out uint pid); clientPid = (int)pid; } catch { }
            // Drawn inside the game by TiltedClient.dll: the overlay windows are only a fallback.
            if (_devFeed.DrawingInGame(clientPid)) { _devRadar?.Hide(); _devMarkers?.Hide(); _devStat = "drawing inside the game (overlay off)"; return; }
            bool haveCamera = _devFeed.ReadCamera(clientPid, out var cam);
            var players = DevPositions(_devFeed.ReadPawns(clientPid));
            string myName = MyDevName();
            int loaded = 0, projected = 0;
            foreach (var p in players) { if (p.Live) loaded++; if (p.Projected) projected++; }
            _devStat = $"server players {_devNow?.Players.Count ?? 0} (shared memory {(_devFeed.ServerShared ? "yes" : "no")}), drawn {players.Count}, loaded in game {loaded}, placed by the game {projected}, viewport {_devFeed.ViewW}x{_devFeed.ViewH}, camera {(haveCamera ? $"{cam.X:0},{cam.Y:0},{cam.Z:0} pitch {cam.Pitch:0} yaw {cam.Yaw:0} fov {cam.Fov:0}" : "none")}, you {(players.FirstOrDefault(p => p.Local)?.P.Name ?? "not found")}";
            var me = players.FirstOrDefault(p => p.Local) ?? players.FirstOrDefault(p => !p.P.Bot && string.Equals(p.P.Name, myName, StringComparison.OrdinalIgnoreCase));
            if (_devFlagRadar) { _devRadar ??= new DevRadarWindow(); _devRadar.Show(game, players, me, haveCamera ? cam : null, _devNow!); }
            else _devRadar?.Hide();
            if (_devFlagMarkers && haveCamera) { _devMarkers ??= new DevMarkersWindow(); _devMarkers.Show(game, players, me, cam, _devFeed.ViewW, _devFeed.ViewH); }
            else _devMarkers?.Hide();
        }
        catch (Exception error) { _devRadar?.Hide(); _devMarkers?.Hide(); _devStat = "error: " + error.Message; }
    }

    [DllImport("user32.dll")] private static extern bool RegisterHotKey(IntPtr hWnd, int id, int fsModifiers, int vk);
    [DllImport("user32.dll")] private static extern bool UnregisterHotKey(IntPtr hWnd, int id);
    [DllImport("user32.dll")] private static extern bool SetForegroundWindow(IntPtr hWnd);
#else
    private void StartDevConsole() { } // public builds: no Dev Console
#endif
}

#if TILTED_DEVKIT
// Team colours shared by the console, radar and markers: your team green, others from a fixed palette.
internal static class DevColors
{
    private static readonly Color[] Palette =
    {
        Color.FromRgb(0xFF, 0x5A, 0x6A), Color.FromRgb(0x4C, 0xC2, 0xFF), Color.FromRgb(0xFF, 0xB0, 0x3B), Color.FromRgb(0xB9, 0xA6, 0xFF),
        Color.FromRgb(0xFF, 0x7A, 0xD9), Color.FromRgb(0x5E, 0xE6, 0xD5), Color.FromRgb(0xF2, 0xE6, 0x5C), Color.FromRgb(0xFF, 0x8C, 0x5A)
    };
    public static readonly Color Mine = Color.FromRgb(0x3B, 0xE4, 0x8B);
    public static Color For(int team, int myTeam) => team == myTeam ? Mine : Palette[Math.Abs(team) % Palette.Length];
}

// The Dev Console: shown over the game with Insert, hidden with Insert (or Esc) again.
//   Players   every player and bot, searchable; bots have Teleport and Kill buttons.
//   Commands  Thanos snap (half the bots), Instant win (every bot not on your team), nearest-bot shortcuts.
//   Server    phase, counts, storm and start timings.
//   Logs      live match server and bot logs, with Copy logs and Open folder.
// Commands go to the match server through Runtime\Server\dev-command.txt; only a Dev Kit match server on this
// PC reads it, and they only ever act on bots.
internal sealed class DevConsoleWindow : Window
{
    private static readonly Color Bg = Color.FromArgb(0xF4, 0x0A, 0x0D, 0x1A), Card = Color.FromRgb(0x12, 0x16, 0x2B), Line = Color.FromRgb(0x24, 0x2A, 0x4A),
        Accent = Color.FromRgb(0x8B, 0x6C, 0xFF), Text = Color.FromRgb(0xF5, 0xF7, 0xFF), Muted = Color.FromRgb(0x8A, 0x92, 0xB8), Danger = Color.FromRgb(0xFF, 0x5A, 0x6A), Good = Color.FromRgb(0x3B, 0xE4, 0x8B), Gold = Color.FromRgb(0xFF, 0xD2, 0x3F);
    private static SolidColorBrush B(Color c) { var b = new SolidColorBrush(c); b.Freeze(); return b; }

    private readonly MainWindow _owner;
    private readonly StackPanel _chips = new() { Orientation = Orientation.Horizontal };
    private readonly StackPanel _rows = new();
    private readonly TextBox _search = new();
    private readonly TextBox _logs = new() { IsReadOnly = true, FontFamily = new FontFamily("Cascadia Mono, Consolas"), FontSize = 11.5, TextWrapping = TextWrapping.NoWrap, VerticalScrollBarVisibility = ScrollBarVisibility.Auto, HorizontalScrollBarVisibility = ScrollBarVisibility.Auto };
    private readonly TextBlock _server = new() { FontSize = 13, LineHeight = 21, TextWrapping = TextWrapping.Wrap };
    private readonly Border _toast = new() { CornerRadius = new CornerRadius(10), Padding = new Thickness(14, 9, 14, 9), Visibility = Visibility.Collapsed, HorizontalAlignment = HorizontalAlignment.Right, VerticalAlignment = VerticalAlignment.Bottom, Margin = new Thickness(0, 0, 18, 18) };
    private readonly TextBlock _toastText = new() { FontSize = 12.5, FontWeight = FontWeights.SemiBold, TextWrapping = TextWrapping.Wrap, MaxWidth = 420 };
    private readonly Grid _pages = new();
    private readonly List<Button> _tabButtons = new();
    private int _tab;
    private DateTime _nextLogs, _toastUntil, _armedUntil;
    private string _armed = "";
    private MainWindow.DevSnapshot? _snap;
    private string _me = "";
    private string _rowsKey = "";

    public DevConsoleWindow(MainWindow owner)
    {
        _owner = owner;
        WindowStyle = WindowStyle.None; ResizeMode = ResizeMode.CanResizeWithGrip; AllowsTransparency = true; Background = Brushes.Transparent;
        ShowInTaskbar = false; Topmost = true; Width = 880; Height = 560; MinWidth = 640; MinHeight = 420; Title = "Syndicate Dev Console";
        FontFamily = new FontFamily("Segoe UI Variable Text, Segoe UI"); Foreground = B(Text);
        var frame = new Border
        {
            CornerRadius = new CornerRadius(16), Background = B(Bg), BorderBrush = B(Color.FromArgb(0xAA, Accent.R, Accent.G, Accent.B)), BorderThickness = new Thickness(1.2), Padding = new Thickness(0),
            Effect = new System.Windows.Media.Effects.DropShadowEffect { BlurRadius = 28, ShadowDepth = 0, Opacity = .55, Color = Color.FromRgb(0x2A, 0x1A, 0x6A) }
        };
        var root = new Grid();
        root.RowDefinitions.Add(new RowDefinition { Height = GridLength.Auto });
        root.RowDefinitions.Add(new RowDefinition { Height = GridLength.Auto });
        root.RowDefinitions.Add(new RowDefinition { Height = new GridLength(1, GridUnitType.Star) });

        // Header: title, live chips, close.
        var head = new DockPanel { Margin = new Thickness(18, 14, 12, 10), Background = Brushes.Transparent };
        head.MouseLeftButtonDown += (_, __) => { try { DragMove(); } catch { } };
        var close = Pill("✕", Muted, Card, () => Hide()); close.Width = 32; close.ToolTip = "Close (Insert or Esc)";
        DockPanel.SetDock(close, Dock.Right); head.Children.Add(close);
        var titleStack = new StackPanel { Orientation = Orientation.Horizontal, VerticalAlignment = VerticalAlignment.Center };
        titleStack.Children.Add(new Border { Width = 10, Height = 10, CornerRadius = new CornerRadius(5), Background = B(Good), Margin = new Thickness(0, 0, 10, 0), VerticalAlignment = VerticalAlignment.Center });
        titleStack.Children.Add(new TextBlock { Text = "SYNDICATE", FontSize = 17, FontWeight = FontWeights.Black, Foreground = B(Gold), VerticalAlignment = VerticalAlignment.Center });
        titleStack.Children.Add(new TextBlock { Text = "  DEV CONSOLE", FontSize = 17, FontWeight = FontWeights.Bold, Foreground = B(Text), VerticalAlignment = VerticalAlignment.Center });
        titleStack.Children.Add(new Border { Margin = new Thickness(12, 0, 0, 0), Padding = new Thickness(8, 2, 8, 3), CornerRadius = new CornerRadius(6), Background = B(Color.FromArgb(0x33, Accent.R, Accent.G, Accent.B)), VerticalAlignment = VerticalAlignment.Center, Child = new TextBlock { Text = "DEV KIT · BOTS ONLY", FontSize = 10.5, FontWeight = FontWeights.Bold, Foreground = B(Accent) } });
        head.Children.Add(titleStack);
        Grid.SetRow(head, 0); root.Children.Add(head);

        // Chips + tabs.
        var bar = new DockPanel { Margin = new Thickness(18, 0, 18, 12) };
        var tabs = new StackPanel { Orientation = Orientation.Horizontal };
        string[] names = { "Players", "Commands", "Cheats", "Server", "Logs" };
        for (int i = 0; i < names.Length; i++) { int index = i; var t = Pill(names[i], Muted, Colors.Transparent, () => SelectTab(index)); t.Margin = new Thickness(0, 0, 6, 0); _tabButtons.Add(t); tabs.Children.Add(t); }
        DockPanel.SetDock(tabs, Dock.Left); bar.Children.Add(tabs);
        _chips.HorizontalAlignment = HorizontalAlignment.Right; bar.Children.Add(_chips);
        Grid.SetRow(bar, 1); root.Children.Add(bar);

        _pages.Margin = new Thickness(18, 0, 18, 18);
        _pages.Children.Add(PlayersPage());
        _pages.Children.Add(CommandsPage());
        _pages.Children.Add(CheatsPage());
        _pages.Children.Add(Panel(new ScrollViewer { Content = _server, VerticalScrollBarVisibility = ScrollBarVisibility.Auto, Padding = new Thickness(4) }));
        _pages.Children.Add(LogsPage());
        Grid.SetRow(_pages, 2); root.Children.Add(_pages);

        _toast.Child = _toastText; Grid.SetRow(_toast, 2); root.Children.Add(_toast);
        frame.Child = root; Content = frame;
        _logs.Background = Brushes.Transparent; _logs.Foreground = B(Color.FromRgb(0xC9, 0xCF, 0xEA)); _logs.BorderThickness = new Thickness(0);
        _server.Foreground = B(Color.FromRgb(0xC9, 0xCF, 0xEA));
        KeyDown += (_, e) => { if (e.Key == System.Windows.Input.Key.Escape) Hide(); };
        SelectTab(0);
    }

    // ---- building blocks -----------------------------------------------------------------------------------
    private static Button Pill(string text, Color fg, Color bg, Action click)
    {
        var b = new Button { Content = text, Foreground = B(fg), Background = B(bg), BorderThickness = new Thickness(0), Padding = new Thickness(14, 6, 14, 7), FontSize = 12.5, FontWeight = FontWeights.SemiBold, Cursor = System.Windows.Input.Cursors.Hand, Focusable = false };
        var template = new ControlTemplate(typeof(Button));
        var border = new FrameworkElementFactory(typeof(Border)); border.Name = "Box";
        border.SetValue(Border.CornerRadiusProperty, new CornerRadius(9));
        border.SetBinding(Border.BackgroundProperty, new System.Windows.Data.Binding("Background") { RelativeSource = System.Windows.Data.RelativeSource.TemplatedParent });
        border.SetBinding(Border.PaddingProperty, new System.Windows.Data.Binding("Padding") { RelativeSource = System.Windows.Data.RelativeSource.TemplatedParent });
        var content = new FrameworkElementFactory(typeof(ContentPresenter)); content.SetValue(ContentPresenter.HorizontalAlignmentProperty, HorizontalAlignment.Center); content.SetValue(ContentPresenter.VerticalAlignmentProperty, VerticalAlignment.Center);
        border.AppendChild(content); template.VisualTree = border;
        var hover = new Trigger { Property = UIElement.IsMouseOverProperty, Value = true }; hover.Setters.Add(new Setter(UIElement.OpacityProperty, .82)); template.Triggers.Add(hover);
        var press = new Trigger { Property = System.Windows.Controls.Primitives.ButtonBase.IsPressedProperty, Value = true }; press.Setters.Add(new Setter(UIElement.RenderTransformProperty, new ScaleTransform(.96, .96))); press.Setters.Add(new Setter(UIElement.RenderTransformOriginProperty, new Point(.5, .5))); template.Triggers.Add(press);
        b.Template = template;
        b.Click += (_, __) => click();
        return b;
    }

    private static Border Panel(UIElement child) => new() { CornerRadius = new CornerRadius(12), Background = B(Card), BorderBrush = B(Line), BorderThickness = new Thickness(1), Padding = new Thickness(12), Child = child };

    private static Border Chip(string label, string value, Color color) => new()
    {
        CornerRadius = new CornerRadius(8), Background = B(Color.FromArgb(0x22, color.R, color.G, color.B)), Padding = new Thickness(10, 4, 10, 5), Margin = new Thickness(6, 0, 0, 0),
        Child = new TextBlock { Inlines = { new System.Windows.Documents.Run(label + "  ") { Foreground = B(Muted), FontSize = 11 }, new System.Windows.Documents.Run(value) { Foreground = B(color), FontSize = 12, FontWeight = FontWeights.Bold } } }
    };

    private void SelectTab(int index)
    {
        _tab = index;
        for (int i = 0; i < _pages.Children.Count; i++) _pages.Children[i].Visibility = i == index ? Visibility.Visible : Visibility.Collapsed;
        for (int i = 0; i < _tabButtons.Count; i++) { _tabButtons[i].Background = B(i == index ? Accent : Colors.Transparent); _tabButtons[i].Foreground = B(i == index ? Colors.White : Muted); }
        _nextLogs = default; _rowsKey = "";
        Refresh(_snap, _me);
    }

    private UIElement PlayersPage()
    {
        var page = new DockPanel();
        var top = new DockPanel { Margin = new Thickness(0, 0, 0, 10) };
        _search.FontSize = 13; _search.Padding = new Thickness(10, 7, 10, 7); _search.Background = B(Card); _search.Foreground = B(Text); _search.BorderBrush = B(Line); _search.CaretBrush = B(Text);
        _search.TextChanged += (_, __) => { _rowsKey = ""; Refresh(_snap, _me); };
        var hint = new TextBlock { Text = "Search by name or team", Foreground = B(Muted), FontSize = 13, Margin = new Thickness(12, 0, 0, 0), VerticalAlignment = VerticalAlignment.Center, IsHitTestVisible = false };
        _search.TextChanged += (_, __) => hint.Visibility = _search.Text.Length == 0 ? Visibility.Visible : Visibility.Collapsed;
        var searchBox = new Grid(); searchBox.Children.Add(_search); searchBox.Children.Add(hint);
        top.Children.Add(searchBox);
        DockPanel.SetDock(top, Dock.Top); page.Children.Add(top);
        var header = Row(new[] { "PLAYER", "TEAM", "HP / SH", "STATE", "DIST", "" }, Muted, true);
        DockPanel.SetDock(header, Dock.Top); page.Children.Add(header);
        page.Children.Add(new ScrollViewer { Content = _rows, VerticalScrollBarVisibility = ScrollBarVisibility.Auto });
        return Panel(page);
    }

    private UIElement CommandsPage()
    {
        var grid = new System.Windows.Controls.Primitives.UniformGrid { Columns = 2 };
        grid.Children.Add(CommandCard("Thanos snap", "Half of the bots still standing are eliminated, at random. Press twice to confirm.", "🫰", Danger, () => Confirm("thanos", "Press Thanos snap again to confirm.", () => Send("thanos", ""))));
        grid.Children.Add(CommandCard("Instant win", "Every bot not on your team is eliminated, so your team wins the match. Press twice to confirm.", "🏆", Gold, () => Confirm("win", "Press Instant win again to confirm.", () => Send("win", ""))));
        grid.Children.Add(CommandCard("Teleport to nearest bot", "Puts you right behind the closest bot that is still standing.", "⇢", Accent, () => Send("tp", "@nearest")));
        grid.Children.Add(CommandCard("Kill nearest bot", "Eliminates the closest bot, credited to you.", "✦", Danger, () => Send("kill", "@nearest")));
        var note = new TextBlock { Text = "Commands only work in a match hosted by a Dev Kit match server on this PC, and only ever affect bots. Bot buttons are also on the Players tab.", Foreground = B(Muted), FontSize = 12, TextWrapping = TextWrapping.Wrap, Margin = new Thickness(4, 10, 4, 0) };
        var stack = new DockPanel(); DockPanel.SetDock(note, Dock.Bottom); stack.Children.Add(note); stack.Children.Add(grid);
        return stack;
    }

    private Border CommandCard(string title, string detail, string glyph, Color color, Action run)
    {
        var body = new DockPanel();
        var go = Pill("Run", Colors.White, color, run); go.VerticalAlignment = VerticalAlignment.Bottom; go.HorizontalAlignment = HorizontalAlignment.Left; go.Margin = new Thickness(0, 10, 0, 0);
        DockPanel.SetDock(go, Dock.Bottom); body.Children.Add(go);
        var head = new StackPanel { Orientation = Orientation.Horizontal, Margin = new Thickness(0, 0, 0, 6) };
        head.Children.Add(new Border { Width = 34, Height = 34, CornerRadius = new CornerRadius(10), Background = B(Color.FromArgb(0x2A, color.R, color.G, color.B)), Margin = new Thickness(0, 0, 10, 0), Child = new TextBlock { Text = glyph, FontSize = 16, Foreground = B(color), HorizontalAlignment = HorizontalAlignment.Center, VerticalAlignment = VerticalAlignment.Center } });
        head.Children.Add(new TextBlock { Text = title, FontSize = 15, FontWeight = FontWeights.Bold, VerticalAlignment = VerticalAlignment.Center });
        DockPanel.SetDock(head, Dock.Top); body.Children.Add(head);
        body.Children.Add(new TextBlock { Text = detail, Foreground = B(Muted), FontSize = 12.5, TextWrapping = TextWrapping.Wrap });
        var card = Panel(body); card.Margin = new Thickness(0, 0, 10, 10); card.Padding = new Thickness(16);
        return card;
    }


    // Cheats: dev testing only, Dev Kit match server on this PC only, never against real players.
    private UIElement CheatsPage()
    {
        var list = new StackPanel();
        // Aim assist strength.
        var aim = new StackPanel { Orientation = Orientation.Horizontal, Margin = new Thickness(0, 8, 0, 0) };
        var aimButtons = new List<Button>();
        void PaintAim() { int level = MainWindow.DevAimAssistLevel; for (int i = 0; i < aimButtons.Count; i++) { bool on = i == level; aimButtons[i].Background = B(on ? Accent : Line); aimButtons[i].Foreground = B(on ? Colors.White : Muted); } }
        foreach (var (level, label) in new[] { (0, "Off"), (1, "Light"), (2, "Medium"), (3, "Strong") })
        {
            var b = Pill(label, Muted, Line, () => { if (level > 0) MainWindow.DevAimAssistLastLevel = level; MainWindow.DevAimAssistLevel = level; PaintAim(); Toast(level == 0 ? "Aim assist off." : $"Aim assist: {label}.", Good); });
            b.Margin = new Thickness(0, 0, 6, 0); aimButtons.Add(b); aim.Children.Add(b);
        }
        PaintAim();
        list.Children.Add(CheatRow("🎯", "Aim assist", "While you aim down sights or shoot, a bot near your crosshair eases your aim toward its chest, also through walls. Light 3.5°, Medium 5.5°, Strong 8°.", aim));

        // Fly with its key.
        var flyRow = new StackPanel { Orientation = Orientation.Horizontal, Margin = new Thickness(0, 8, 0, 0) };
        var flyToggle = Switch("dev-fly-on.txt", on => Toast(on ? "Flying: Space up, Ctrl down. Press it again to land." : "Flying off.", Good));
        flyRow.Children.Add(flyToggle);
        Button? keyButton = null;
        keyButton = Pill("Key: " + KeyName(MainWindow.DevFlyKey), Text, Line, () =>
        {
            keyButton!.Content = "Press a key…";
            void Capture(object? s, System.Windows.Input.KeyEventArgs e)
            {
                PreviewKeyDown -= Capture; e.Handled = true;
                var key = e.Key == System.Windows.Input.Key.System ? e.SystemKey : e.Key;
                if (key == System.Windows.Input.Key.Escape) { keyButton!.Content = "Key: " + KeyName(MainWindow.DevFlyKey); return; }
                int vk = System.Windows.Input.KeyInterop.VirtualKeyFromKey(key);
                MainWindow.DevFlyKey = "key:" + vk; keyButton!.Content = "Key: " + KeyName(MainWindow.DevFlyKey);
                Toast("Fly key set to " + KeyName(MainWindow.DevFlyKey) + ". Press it in game to fly.", Good);
            }
            PreviewKeyDown += Capture; Activate();
        });
        keyButton.Margin = new Thickness(10, 0, 0, 0); flyRow.Children.Add(keyButton);
        list.Children.Add(CheatRow("🕊", "Fly", "Fly anywhere, through walls and the ground. Press your fly key in game (or switch it here). Space goes up, Ctrl goes down.", flyRow));

        list.Children.Add(CheatRow("➖", "No recoil", "The gun in your hands has no recoil (your game). Turn it off to get the normal recoil back.", Switch("dev-no-recoil.txt", on => Toast(on ? "No recoil on." : "Recoil back to normal.", Good))));
        list.Children.Add(CheatRow("✦", "Magic bullet", "Every shot also hits the bot your crosshair is on or near in the head, through walls (bots only, never real players).", Switch("dev-magic-bullet.txt", on => Toast(on ? "Magic bullet on: aim at or near a bot and shoot." : "Magic bullet off.", Good))));
        list.Children.Add(CheatRow("➝", "No bullet drop", "Your sniper bullets and rockets fly straight.", Switch("dev-no-drop.txt", on => Toast(on ? "No bullet drop on." : "Bullet drop back to normal.", Good))));
        list.Children.Add(CheatRow("∞", "Infinite ammo", "Your magazine stays full and no ammo is used.", Switch("dev-infinite-ammo.txt", on => Toast(on ? "Infinite ammo on." : "Infinite ammo off.", Good))));
        list.Children.Add(CheatRow("▣", "Bot markers", "A box, name, health and shield on every bot, drawn by the game itself (bots only).", Switch("dev-markers-on.txt", on => { MainWindow.SetDevFlag("dev-camera-on.txt", on); Toast(on ? "Bot markers on." : "Bot markers off.", Good); })));
        var note = new TextBlock { Text = "Dev testing only: these work in matches hosted by a Dev Kit match server on this PC, and never against real players. All can be switched while you play.", Foreground = B(Muted), FontSize = 12, TextWrapping = TextWrapping.Wrap, Margin = new Thickness(4, 6, 4, 0) };
        list.Children.Add(note);
        return new ScrollViewer { Content = list, VerticalScrollBarVisibility = ScrollBarVisibility.Auto };
    }

    private static string KeyName(string key)
    {
        if (key.StartsWith("key:") && int.TryParse(key[4..], out int vk))
        {
            var k = System.Windows.Input.KeyInterop.KeyFromVirtualKey(vk);
            return k == System.Windows.Input.Key.None ? "VK " + vk : k.ToString();
        }
        return key.Replace("mouse:", "Mouse ");
    }

    private Border CheatRow(string glyph, string title, string detail, UIElement control)
    {
        var body = new DockPanel();
        var icon = new Border { Width = 36, Height = 36, CornerRadius = new CornerRadius(10), Background = B(Color.FromArgb(0x2A, Accent.R, Accent.G, Accent.B)), Margin = new Thickness(0, 0, 12, 0), VerticalAlignment = VerticalAlignment.Top, Child = new TextBlock { Text = glyph, FontSize = 16, Foreground = B(Accent), HorizontalAlignment = HorizontalAlignment.Center, VerticalAlignment = VerticalAlignment.Center } };
        DockPanel.SetDock(icon, Dock.Left); body.Children.Add(icon);
        var text = new StackPanel();
        text.Children.Add(new TextBlock { Text = title, FontSize = 14.5, FontWeight = FontWeights.Bold });
        text.Children.Add(new TextBlock { Text = detail, Foreground = B(Muted), FontSize = 12, TextWrapping = TextWrapping.Wrap, Margin = new Thickness(0, 2, 0, 0) });
        text.Children.Add(control);
        body.Children.Add(text);
        var card = Panel(body); card.Margin = new Thickness(0, 0, 0, 8); card.Padding = new Thickness(14, 12, 14, 12);
        return card;
    }

    // An on/off pill bound to a switch file.
    private Button Switch(string flag, Action<bool> changed)
    {
        Button? b = null;
        void Paint() { bool on = MainWindow.DevFlag(flag); b!.Content = on ? "ON" : "OFF"; b.Background = B(on ? Good : Line); b.Foreground = B(on ? Color.FromRgb(0x06, 0x1A, 0x10) : Muted); }
        b = Pill("OFF", Muted, Line, () => { bool on = !MainWindow.DevFlag(flag); MainWindow.SetDevFlag(flag, on); Paint(); changed(on); });
        b.Width = 70; b.HorizontalAlignment = HorizontalAlignment.Left;
        _switchPainters.Add(Paint);
        Paint();
        return b;
    }
    private readonly List<Action> _switchPainters = new();

    private UIElement LogsPage()
    {
        var page = new DockPanel();
        var actions = new StackPanel { Orientation = Orientation.Horizontal, Margin = new Thickness(0, 0, 0, 10) };
        actions.Children.Add(Pill("Copy logs", Colors.White, Accent, CopyLogs));
        var open = Pill("Open folder", Text, Line, () => { try { if (Launcher.TryAutoLocate(out var paths, out _)) System.Diagnostics.Process.Start("explorer.exe", IOPath.Combine(paths.Root, "Runtime", "Server")); } catch { } });
        open.Margin = new Thickness(8, 0, 0, 0); actions.Children.Add(open);
        DockPanel.SetDock(actions, Dock.Top); page.Children.Add(actions);
        page.Children.Add(Panel(_logs));
        return page;
    }

    private static Grid Row(string[] cells, Color color, bool header, UIElement? actions = null)
    {
        var g = new Grid { Margin = new Thickness(0, 0, 0, 2), MinHeight = header ? 22 : 34 };
        foreach (var w in new[] { 3.0, 0.8, 1.2, 1.6, 0.9, 1.7 }) g.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(w, GridUnitType.Star) });
        for (int i = 0; i < cells.Length; i++)
        {
            var t = new TextBlock { Text = cells[i], Foreground = B(color), FontSize = header ? 10.5 : 12.5, FontWeight = header ? FontWeights.Bold : (i == 0 ? FontWeights.SemiBold : FontWeights.Normal), TextTrimming = TextTrimming.CharacterEllipsis, VerticalAlignment = VerticalAlignment.Center, Margin = new Thickness(i == 0 ? 8 : 0, 0, 6, 0) };
            Grid.SetColumn(t, i); g.Children.Add(t);
        }
        if (actions != null) { Grid.SetColumn(actions, 5); g.Children.Add(actions); }
        return g;
    }

    // ---- commands ------------------------------------------------------------------------------------------
    private void Confirm(string key, string ask, Action run)
    {
        if (_armed == key && DateTime.UtcNow < _armedUntil) { _armed = ""; run(); return; }
        _armed = key; _armedUntil = DateTime.UtcNow.AddSeconds(4);
        Toast(ask, Gold);
    }

    private void Send(string command, string target)
    {
        var sent = _owner.SendDevCommand(command, target, _me);
        Toast(sent ?? "Sent.", sent == null ? Good : Danger);
    }

    private void CopyLogs()
    {
        try
        {
            var text = "== Syndicate Dev Console · " + DateTime.Now.ToString("yyyy-MM-dd HH:mm:ss") + " ==\n" + ServerText() +
                       "\n\n== match server (stdout.log." + (_snap?.Pid ?? 0) + ".log) ==\n" + TailFile($"stdout.log.{_snap?.Pid ?? 0}.log", 300) +
                       "\n\n== key lines (whole bot log) ==\n" + GrepFile("ready.pid.bot.log", new[] { "squad health", "squad bars", "teamdata", "squad HUD", "dev console", "team HUD", "managed match" }, 120) +
                       "\n\n== bots (ready.pid.bot.log) ==\n" + TailFile("ready.pid.bot.log", 200) +
                       "\n\n== client mods (tilted-client.log) ==\n" + TailFile("tilted-client.log", 80) +
                       "\n\n== dev markers (dev-markers.log) ==\n" + TailFile("dev-markers.log", 20);
            Clipboard.SetText(text);
            Toast("Logs copied to the clipboard.", Good);
        }
        catch (Exception error) { Toast("Could not copy: " + error.Message, Danger); }
    }

    private void Toast(string text, Color color)
    {
        _toastText.Text = text; _toastText.Foreground = B(color);
        _toast.Background = B(Color.FromArgb(0xF0, 0x16, 0x1A, 0x33)); _toast.BorderBrush = B(color); _toast.BorderThickness = new Thickness(1);
        _toast.Visibility = Visibility.Visible; _toastUntil = DateTime.UtcNow.AddSeconds(4);
        _toast.BeginAnimation(OpacityProperty, new System.Windows.Media.Animation.DoubleAnimation(0, 1, TimeSpan.FromMilliseconds(160)));
    }

    // ---- refresh ----------------------------------------------------------------------------------------------
    public void ShowOver(IntPtr game)
    {
        if (game != IntPtr.Zero && GetWindowRect(game, out RECT r) && r.Left > -10000) { Left = r.Left + 40; Top = r.Top + 80; }
        else { var area = SystemParameters.WorkArea; Left = area.Left + (area.Width - Width) / 2; Top = area.Top + (area.Height - Height) / 2; }
        Show(); Activate();
        BeginAnimation(OpacityProperty, new System.Windows.Media.Animation.DoubleAnimation(0, 1, TimeSpan.FromMilliseconds(140)));
    }

    private string ServerText()
    {
        var snap = _snap;
        if (snap == null) return "No live data from a match server yet.";
        int alive = snap.Players.Count(p => p.Alive), humans = snap.Players.Count(p => !p.Bot), bots = snap.Players.Count(p => p.Bot), botsAlive = snap.Players.Count(p => p.Bot && p.Alive);
        int teams = snap.Players.Where(p => p.Alive).Select(p => p.Team).Distinct().Count();
        return $"Match server pid {snap.Pid} · phase {snap.Phase}\nPlayers {snap.Players.Count} ({humans} human, {bots} bots) · alive {alive} ({botsAlive} bots) · teams left {teams}\n" +
               $"Storm (next circle): {(snap.ZoneR > 0 ? $"centre {snap.ZoneX:0}, {snap.ZoneY:0} · radius {snap.ZoneR / 100:0} m" : "not known yet")}";
    }

    public void Refresh(MainWindow.DevSnapshot? snap, string me)
    {
        _snap = snap; _me = me;
        if (_toast.Visibility == Visibility.Visible && DateTime.UtcNow > _toastUntil) _toast.Visibility = Visibility.Collapsed;
        var result = _owner.TakeDevCommandResult();
        if (result != null) Toast(result, result.StartsWith("!") ? Danger : Good);
        var mine = snap?.Players.FirstOrDefault(p => !p.Bot && string.Equals(p.Name, me, StringComparison.OrdinalIgnoreCase));
        int myTeam = mine?.Team ?? int.MinValue;

        _chips.Children.Clear();
        if (snap == null) _chips.Children.Add(Chip("SERVER", "waiting", Muted));
        else
        {
            _chips.Children.Add(Chip("PHASE", snap.Phase.ToString(), Accent));
            _chips.Children.Add(Chip("ALIVE", snap.Players.Count(p => p.Alive).ToString(), Text));
            _chips.Children.Add(Chip("BOTS", snap.Players.Count(p => p.Bot && p.Alive).ToString(), Gold));
            _chips.Children.Add(Chip("TEAMS", snap.Players.Where(p => p.Alive).Select(p => p.Team).Distinct().Count().ToString(), Text));
            if (mine != null) _chips.Children.Add(Chip("YOU", mine.Alive ? $"{mine.Hp:0}/{mine.Sh:0}" : "out", mine.Alive ? Good : Danger));
        }

        if (_tab == 0)
        {
            string filter = _search.Text.Trim();
            var list = snap == null ? new List<MainWindow.DevPlayer>() : snap.Players
                .Where(p => filter.Length == 0 || p.Name.Contains(filter, StringComparison.OrdinalIgnoreCase) || ("t" + p.Team).Equals(filter, StringComparison.OrdinalIgnoreCase) || p.Team.ToString() == filter)
                .OrderByDescending(p => p.Team == myTeam).ThenByDescending(p => p.Alive).ThenBy(p => mine == null ? 0 : Dist(p, mine)).Take(120).ToList();
            // Rebuild only when something visible changed (keeps the buttons clickable).
            var key = string.Join(";", list.Select(p => $"{p.Name}|{p.Alive}|{p.Hp:0}|{p.Sh:0}|{p.Knocked}|{p.State}|{p.Skill}|{p.Pro}|{p.Playstyle}|{p.Why}|{p.Weapon}|{p.Inv}|{p.NavDist:0}|{p.Mat}|{p.TargetDist:0}|{p.EnemyVisible}|{(p.NoProgress > 2 || p.StuckStrikes > 0)}|{(mine == null ? 0 : Dist(p, mine) / 10):0}"));
            if (key != _rowsKey)
            {
                _rowsKey = key; _rows.Children.Clear();
                if (snap == null) _rows.Children.Add(new TextBlock { Text = "Waiting for live data from your match server (start a match on this PC).", Foreground = B(Muted), Margin = new Thickness(8) });
                foreach (var p in list)
                {
                    var c = DevColors.For(p.Team, myTeam);
                    var color = p.Alive ? c : Color.FromRgb(0x50, 0x56, 0x78);
                    StackPanel? actions = null;
                    if (p.Bot && p.Alive)
                    {
                        actions = new StackPanel { Orientation = Orientation.Horizontal, HorizontalAlignment = HorizontalAlignment.Right };
                        var name = p.Name;
                        var tp = Pill("Teleport", Text, Line, () => Send("tp", name)); tp.Padding = new Thickness(10, 3, 10, 4); tp.FontSize = 11.5;
                        var kill = Pill("Kill", Colors.White, Danger, () => Send("kill", name)); kill.Padding = new Thickness(10, 3, 10, 4); kill.FontSize = 11.5; kill.Margin = new Thickness(6, 0, 0, 0);
                        actions.Children.Add(tp); actions.Children.Add(kill);
                    }
                    var row = Row(new[] { p.Name + (p == mine ? "  (you)" : p.Bot ? "  · bot" : ""), "T" + p.Team, p.Alive ? $"{p.Hp:0} / {p.Sh:0}" : "out",
                        p.Knocked ? "knocked" : p.State, mine == null || !p.Alive || p == mine ? "" : $"{Dist(p, mine):0} m" }, color, false, actions);
                    UIElement content = row;
                    // Bot intelligence line: what the bot is thinking — skill tier, who it is targeting, its
                    // material count, and whether navigation is healthy or stuck. Makes bot behaviour readable.
                    if (p.Bot && p.Alive)
                    {
                        string tier = p.Skill switch { < 0 => "T? loading", 0 => "T1 beginner", 1 => "T2 average", 2 => "T3 above-avg", _ => p.Pro ? "T5 pro" : "T4 advanced" };
                        string style = string.IsNullOrEmpty(p.Playstyle) ? "" : $"   ·   {p.Playstyle}";
                        string tgt = p.TargetDist >= 0 ? (p.EnemyVisible ? $"target {p.TargetDist:0}m (seen)" : $"target {p.TargetDist:0}m (heard)") : "no target";
                        string nav = (p.NoProgress > 2 || p.StuckStrikes > 0) ? $"nav STUCK {p.NoProgress:0.0}s" : "nav ok";
                        var detail = new TextBlock { Text = $"{tier}{style}   ·   {tgt}   ·   {p.Mat} mats   ·   {nav}", FontSize = 11,
                            Foreground = B((p.NoProgress > 2 || p.StuckStrikes > 0) ? Danger : Muted), Margin = new Thickness(10, 0, 0, 2), TextWrapping = TextWrapping.Wrap };
                        // Second line: the decision "why", current weapon, gun count and how far the nav destination is.
                        string why = string.IsNullOrEmpty(p.Why) ? "" : p.Why;
                        string weapon = string.IsNullOrEmpty(p.Weapon) || p.Weapon == "none" ? "unarmed" : p.Weapon;
                        string navTo = p.NavDist >= 0 ? $"dest {p.NavDist:0}m" : "no dest";
                        var why2 = new TextBlock { Text = $"why: {(string.IsNullOrEmpty(why) ? "—" : why)}   ·   {weapon}   ·   {p.Inv} guns   ·   {navTo}", FontSize = 11,
                            Foreground = B(Accent), Margin = new Thickness(10, 0, 0, 3), TextWrapping = TextWrapping.Wrap };
                        var stack = new StackPanel(); stack.Children.Add(row); stack.Children.Add(detail); stack.Children.Add(why2); content = stack;
                    }
                    var bg = new Border { CornerRadius = new CornerRadius(8), Background = B(p == mine ? Color.FromArgb(0x22, 0x3B, 0xE4, 0x8B) : Color.FromArgb(0x10, 0xFF, 0xFF, 0xFF)), Margin = new Thickness(0, 0, 0, 3), Padding = new Thickness(0, 2, 6, 2), Child = content };
                    _rows.Children.Add(bg);
                }
            }
        }
        if (_tab == 2) foreach (var paint in _switchPainters) paint();
        if (_tab == 3) _server.Text = ServerText() + "\n\nLast match server start timings (startup-timing.log):\n" + TailFile("startup-timing.log", 8);
        if (_tab == 4 && DateTime.UtcNow >= _nextLogs)
        {
            _nextLogs = DateTime.UtcNow.AddSeconds(1);
            bool atEnd = _logs.VerticalOffset + _logs.ViewportHeight >= _logs.ExtentHeight - 4;
            _logs.Text = "== match server (stdout.log." + (snap?.Pid ?? 0) + ".log) ==\n" + TailFile($"stdout.log.{snap?.Pid ?? 0}.log", 80) + "\n\n== bots (ready.pid.bot.log) ==\n" + TailFile("ready.pid.bot.log", 60);
            if (atEnd) _logs.ScrollToEnd();
        }
    }

    private static double Dist(MainWindow.DevPlayer a, MainWindow.DevPlayer b) => Math.Sqrt(Math.Pow(a.X - b.X, 2) + Math.Pow(a.Y - b.Y, 2) + Math.Pow(a.Z - b.Z, 2)) / 100;

    // Every line of a log that contains one of the words (the last 'max' of them).
    private static string GrepFile(string name, string[] words, int max)
    {
        try
        {
            if (!Launcher.TryAutoLocate(out var paths, out _)) return "";
            var file = IOPath.Combine(paths.Root, "Runtime", "Server", name);
            if (!File.Exists(file)) return "(none yet)";
            using var stream = new FileStream(file, FileMode.Open, FileAccess.Read, FileShare.ReadWrite | FileShare.Delete);
            using var reader = new StreamReader(stream);
            var hits = new Queue<string>();
            for (string? line; (line = reader.ReadLine()) != null;)
                if (words.Any(w => line.Contains(w, StringComparison.OrdinalIgnoreCase))) { hits.Enqueue(line); if (hits.Count > max) hits.Dequeue(); }
            return hits.Count == 0 ? "(no matching lines)" : string.Join("\n", hits);
        }
        catch (Exception error) { return "(could not read: " + error.Message + ")"; }
    }

    private static string TailFile(string name, int lines)
    {
        try
        {
            if (!Launcher.TryAutoLocate(out var paths, out _)) return "";
            var file = IOPath.Combine(paths.Root, "Runtime", "Server", name);
            if (!File.Exists(file)) return "(none yet)";
            using var stream = new FileStream(file, FileMode.Open, FileAccess.Read, FileShare.ReadWrite | FileShare.Delete);
            stream.Seek(Math.Max(0, stream.Length - 64000), SeekOrigin.Begin);
            using var reader = new StreamReader(stream);
            return string.Join("\n", reader.ReadToEnd().Split('\n').TakeLast(lines)).TrimEnd();
        }
        catch (IOException) when (name.StartsWith("stdout.log")) { return "(the match server keeps this log locked while it runs; it can be read after the match)"; }
        catch (Exception error) { return "(could not read: " + error.Message + ")"; }
    }

    [StructLayout(LayoutKind.Sequential)] private struct RECT { public int Left, Top, Right, Bottom; }
    [DllImport("user32.dll")] private static extern bool GetWindowRect(IntPtr hWnd, out RECT lpRect);
}

// Shared base for the click-through overlays (radar, markers): never takes focus, not in Alt+Tab.
internal abstract class DevOverlay : Window
{
    protected DevOverlay(string title)
    {
        WindowStyle = WindowStyle.None; ResizeMode = ResizeMode.NoResize; AllowsTransparency = true; Background = Brushes.Transparent;
        ShowInTaskbar = false; ShowActivated = false; Topmost = true; Focusable = false; Title = title;
        WindowStartupLocation = WindowStartupLocation.Manual; Left = -32000; Top = -32000;
        SourceInitialized += (_, __) =>
        {
            var hwnd = new WindowInteropHelper(this).Handle;
            long style = GetWindowLongPtr(hwnd, -20).ToInt64() | 0x20 | 0x80000 | 0x80 | 0x08000000;
            SetWindowLongPtr(hwnd, -20, new IntPtr(style));
        };
    }

    // The game's client area on screen, or false when it is not the active window.
    protected static bool GameArea(IntPtr game, out int x, out int y, out int w, out int h)
    {
        x = y = w = h = 0;
        if (game == IntPtr.Zero || !GetClientRect(game, out RECT client)) return false;
        var origin = new POINT();
        if (!ClientToScreen(game, ref origin)) return false;
        GetWindowThreadProcessId(GetForegroundWindow(), out uint fg); GetWindowThreadProcessId(game, out uint gp);
        if (fg != gp) return false;
        x = origin.X; y = origin.Y; w = client.Right - client.Left; h = client.Bottom - client.Top;
        return w > 200 && h > 200;
    }

    protected void Place(int x, int y, int w, int h)
    {
        var hwnd = new WindowInteropHelper(this).EnsureHandle();
        if (!IsVisible) Show();
        SetWindowPos(hwnd, new IntPtr(-1), x, y, w, h, 0x0010 | 0x0040);
    }

    [StructLayout(LayoutKind.Sequential)] protected struct RECT { public int Left, Top, Right, Bottom; }
    [StructLayout(LayoutKind.Sequential)] protected struct POINT { public int X, Y; }
    [DllImport("user32.dll", EntryPoint = "GetWindowLongPtrW")] private static extern IntPtr GetWindowLongPtr(IntPtr hWnd, int nIndex);
    [DllImport("user32.dll", EntryPoint = "SetWindowLongPtrW")] private static extern IntPtr SetWindowLongPtr(IntPtr hWnd, int nIndex, IntPtr dwNewLong);
    [DllImport("user32.dll")] private static extern bool SetWindowPos(IntPtr hWnd, IntPtr after, int X, int Y, int cx, int cy, uint flags);
    [DllImport("user32.dll")] protected static extern bool GetClientRect(IntPtr hWnd, out RECT lpRect);
    [DllImport("user32.dll")] protected static extern bool ClientToScreen(IntPtr hWnd, ref POINT lpPoint);
    [DllImport("user32.dll")] private static extern IntPtr GetForegroundWindow();
    [DllImport("user32.dll")] private static extern uint GetWindowThreadProcessId(IntPtr hWnd, out uint pid);
}

// Reads the live data: the match server's snapshot (Local\SyndicateDevLive), and from your own game
// (TiltedClient.dll) the camera (Local\TiltedDevCamera_<pid>) and every loaded player's position
// (Local\TiltedDevPawns_<pid>). Each block has a sequence number that is odd while it is being written.
internal sealed class DevFeed : IDisposable
{
    internal readonly record struct Camera(float X, float Y, float Z, float Pitch, float Yaw, float Roll, float Fov);
    internal readonly record struct Pawn(string Name, float X, float Y, float Z, int Team, float HeadX, float HeadY, float FootX, float FootY, bool OnScreen, bool Local);
    public int ViewW { get; private set; }
    public int ViewH { get; private set; }

    private static readonly System.Diagnostics.Stopwatch Clock = System.Diagnostics.Stopwatch.StartNew();
    public static double Seconds() => Clock.Elapsed.TotalSeconds;

    private System.IO.MemoryMappedFiles.MemoryMappedFile? _serverMap, _cameraMap, _pawnMap;
    private System.IO.MemoryMappedFiles.MemoryMappedViewAccessor? _server, _camera, _pawns;
    private int _serverSeq = -1, _cameraPid, _pawnPid, _pawnSeq = -1;
    private DateTime _nextServerOpen, _nextCameraOpen, _nextPawnOpen;
    private byte[] _buffer = new byte[64 * 1024];
    private Pawn[] _lastPawns = Array.Empty<Pawn>();
    public bool ServerShared => _server != null;

    private static System.IO.MemoryMappedFiles.MemoryMappedViewAccessor? Open(string name, long size, ref System.IO.MemoryMappedFiles.MemoryMappedFile? map, ref DateTime next)
    {
        if (DateTime.UtcNow < next) return null;
        next = DateTime.UtcNow.AddSeconds(1); // not there: look again in a second, not every frame
        try
        {
            map = System.IO.MemoryMappedFiles.MemoryMappedFile.OpenExisting(name, System.IO.MemoryMappedFiles.MemoryMappedFileRights.Read);
            return map.CreateViewAccessor(0, size, System.IO.MemoryMappedFiles.MemoryMappedFileAccess.Read);
        }
        catch { map?.Dispose(); map = null; return null; }
    }

    // New JSON from the match server, or null when nothing changed.
    public string? ReadServer()
    {
        try
        {
            _server ??= Open("Local\\TiltedDevLive", 512 * 1024, ref _serverMap, ref _nextServerOpen);
            if (_server == null) return null;
            for (int attempt = 0; attempt < 3; attempt++)
            {
                int seq = _server.ReadInt32(0);
                if (seq == _serverSeq) return null;
                if ((seq & 1) != 0) { System.Threading.Thread.SpinWait(200); continue; }
                int length = _server.ReadInt32(4);
                if (length <= 0 || length > 512 * 1024 - 16) return null;
                if (_buffer.Length < length) _buffer = new byte[length * 2];
                _server.ReadArray(8, _buffer, 0, length);
                if (_server.ReadInt32(0) != seq) continue; // changed while reading: read again
                _serverSeq = seq;
                return System.Text.Encoding.UTF8.GetString(_buffer, 0, length);
            }
        }
        catch { _server?.Dispose(); _serverMap?.Dispose(); _server = null; _serverMap = null; _serverSeq = -1; }
        return null;
    }

    public bool ReadCamera(int pid, out Camera cam)
    {
        cam = default;
        try
        {
            if (_camera == null || pid != _cameraPid)
            {
                _camera?.Dispose(); _cameraMap?.Dispose(); _camera = null; _cameraMap = null;
                if (pid != _cameraPid) _nextCameraOpen = default;
                _cameraPid = pid;
                _camera = Open("Local\\TiltedDevCamera_" + pid, 32, ref _cameraMap, ref _nextCameraOpen);
                if (_camera == null) return false;
            }
            if (_camera.ReadInt32(0) == 0) return false; // nothing written yet
            cam = new Camera(_camera.ReadSingle(4), _camera.ReadSingle(8), _camera.ReadSingle(12), _camera.ReadSingle(16), _camera.ReadSingle(20), _camera.ReadSingle(24), _camera.ReadSingle(28));
            return cam.Fov > 1;
        }
        catch { _camera?.Dispose(); _cameraMap?.Dispose(); _camera = null; _cameraMap = null; return false; }
    }

    // TiltedClient.dll's block: seq, count, viewport width, height, then 160 players of 88 bytes
    // (name[48], x, y, z, team, headX, headY, footX, footY, onScreen, local).
    private const int PawnSize = 88, PawnMax = 160, PawnHeader = 16;
    public Pawn[] ReadPawns(int pid)
    {
        try
        {
            if (_pawns == null || pid != _pawnPid)
            {
                _pawns?.Dispose(); _pawnMap?.Dispose(); _pawns = null; _pawnMap = null; _lastPawns = Array.Empty<Pawn>(); _pawnSeq = -1;
                if (pid != _pawnPid) _nextPawnOpen = default;
                _pawnPid = pid;
                _pawns = Open("Local\\TiltedDevPawns_" + pid, PawnHeader + PawnSize * PawnMax, ref _pawnMap, ref _nextPawnOpen);
                if (_pawns == null) return _lastPawns;
            }
            for (int attempt = 0; attempt < 3; attempt++)
            {
                int seq = _pawns.ReadInt32(0);
                if (seq == _pawnSeq) return _lastPawns;
                if ((seq & 1) != 0) { System.Threading.Thread.SpinWait(200); continue; }
                int count = Math.Clamp(_pawns.ReadInt32(4), 0, PawnMax);
                var list = new Pawn[count];
                var name = new byte[48];
                for (int i = 0; i < count; i++)
                {
                    long at = PawnHeader + (long)i * PawnSize;
                    _pawns.ReadArray(at, name, 0, 48);
                    int end = Array.IndexOf(name, (byte)0); if (end < 0) end = 48;
                    list[i] = new Pawn(System.Text.Encoding.UTF8.GetString(name, 0, end).Trim(), _pawns.ReadSingle(at + 48), _pawns.ReadSingle(at + 52), _pawns.ReadSingle(at + 56), _pawns.ReadInt32(at + 60),
                        _pawns.ReadSingle(at + 64), _pawns.ReadSingle(at + 68), _pawns.ReadSingle(at + 72), _pawns.ReadSingle(at + 76), _pawns.ReadInt32(at + 80) != 0, _pawns.ReadInt32(at + 84) != 0);
                }
                int viewW = _pawns.ReadInt32(8), viewH = _pawns.ReadInt32(12);
                if (_pawns.ReadInt32(0) != seq) continue;
                _pawnSeq = seq; _lastPawns = list; ViewW = viewW; ViewH = viewH;
                return list;
            }
        }
        catch { _pawns?.Dispose(); _pawnMap?.Dispose(); _pawns = null; _pawnMap = null; _lastPawns = Array.Empty<Pawn>(); }
        return _lastPawns;
    }

    // TiltedClient.dll counts every frame it draws the markers and radar inside the game itself.
    private System.IO.MemoryMappedFiles.MemoryMappedFile? _drawMap;
    private System.IO.MemoryMappedFiles.MemoryMappedViewAccessor? _draw;
    private int _drawPid, _drawLast = -1;
    private DateTime _nextDrawOpen, _drawSeenAt;
    public bool DrawingInGame(int pid)
    {
        try
        {
            if (_draw == null || pid != _drawPid)
            {
                _draw?.Dispose(); _drawMap?.Dispose(); _draw = null; _drawMap = null;
                if (pid != _drawPid) { _nextDrawOpen = default; _drawLast = -1; }
                _drawPid = pid;
                _draw = Open("Local\\TiltedDevDrawing_" + pid, 4, ref _drawMap, ref _nextDrawOpen);
                if (_draw == null) return false;
            }
            int frames = _draw.ReadInt32(0);
            if (frames != _drawLast) { _drawLast = frames; _drawSeenAt = DateTime.UtcNow; }
            return DateTime.UtcNow - _drawSeenAt < TimeSpan.FromSeconds(1);
        }
        catch { _draw?.Dispose(); _drawMap?.Dispose(); _draw = null; _drawMap = null; return false; }
    }

    public void Dispose()
    {
        try { _draw?.Dispose(); _drawMap?.Dispose(); } catch { }
        foreach (var d in new IDisposable?[] { _server, _serverMap, _camera, _cameraMap, _pawns, _pawnMap }) try { d?.Dispose(); } catch { }
    }
}

// Draws straight into one visual each frame (no hundreds of controls rebuilt 30 times a second).
internal sealed class DevSurface : FrameworkElement
{
    public Action<DrawingContext>? Paint;
    protected override void OnRender(DrawingContext dc) => Paint?.Invoke(dc);
}

internal static class DevPaint
{
    private static readonly Dictionary<Color, SolidColorBrush> Brushes_ = new();
    public static SolidColorBrush BrushOf(Color c) { if (!Brushes_.TryGetValue(c, out var b)) { b = new SolidColorBrush(c); b.Freeze(); Brushes_[c] = b; } return b; }
    private static readonly Dictionary<(Color, double), Pen> Pens = new();
    public static Pen PenOf(Color c, double w) { if (!Pens.TryGetValue((c, w), out var p)) { p = new Pen(BrushOf(c), w); p.Freeze(); Pens[(c, w)] = p; } return p; }
    public static readonly Typeface Face = new(new FontFamily("Segoe UI"), FontStyles.Normal, FontWeights.SemiBold, FontStretches.Normal);
    public static Color Fade(Color c, byte a) => Color.FromArgb(a, c.R, c.G, c.B);
    public static FormattedText Text(string s, double size, Color c, double dpi) =>
        new(s, System.Globalization.CultureInfo.InvariantCulture, FlowDirection.LeftToRight, Face, size, BrushOf(c), dpi);
}

// Live radar: you in the middle, turned the way your camera faces, everyone within 250 m as a dot in their team
// colour (bots hollow), the next storm circle dashed.
internal sealed class DevRadarWindow : DevOverlay
{
    private const double Size = 240, RangeUnits = 25000; // 250 m from the centre to the edge
    private readonly DevSurface _surface = new() { Width = Size, Height = Size };
    private List<MainWindow.DevShown> _players = new();
    private MainWindow.DevShown? _me;
    private DevFeed.Camera? _cam;
    private MainWindow.DevSnapshot? _snap;
    private static readonly Pen Dashed = MakeDashed();

    private static Pen MakeDashed() { var p = new Pen(DevPaint.BrushOf(Color.FromArgb(0xCC, 0xFF, 0xFF, 0xFF)), 1.2) { DashStyle = new DashStyle(new double[] { 3, 3 }, 0) }; p.Freeze(); return p; }

    public DevRadarWindow() : base("Syndicate Dev Radar") { Width = Size; Height = Size; Content = new Viewbox { Child = _surface }; _surface.Paint = Paint; }

    public void Show(IntPtr game, List<MainWindow.DevShown> players, MainWindow.DevShown? me, DevFeed.Camera? cam, MainWindow.DevSnapshot snap)
    {
        if (!GameArea(game, out int x, out int y, out int w, out int h)) { Hide(); return; }
        int size = (int)(h * 0.24);
        Place(x + (int)(h * 0.02), y + (int)(h * 0.30), size, size);
        _players = players; _me = me; _cam = cam; _snap = snap;
        _surface.InvalidateVisual();
    }

    private void Paint(DrawingContext dc)
    {
        var centre = new Point(Size / 2, Size / 2);
        dc.DrawEllipse(DevPaint.BrushOf(Color.FromArgb(0xB8, 0x0B, 0x0E, 0x1C)), DevPaint.PenOf(Color.FromArgb(0xCC, 0x8B, 0x6C, 0xFF), 1.5), centre, Size / 2 - 1, Size / 2 - 1);
        foreach (var ring in new[] { 0.33, 0.66 }) dc.DrawEllipse(null, DevPaint.PenOf(Color.FromArgb(0x40, 0xFF, 0xFF, 0xFF), 1), centre, Size / 2 * ring, Size / 2 * ring);
        // Centre and facing from the camera (this frame); without it, from your player on the server.
        double cx = _cam?.X ?? _me?.X ?? 0, cy = _cam?.Y ?? _me?.Y ?? 0, yaw = (_cam?.Yaw ?? _me?.P.Yaw ?? 0) * Math.PI / 180;
        if (_cam == null && _me == null) return;
        int myTeam = _me?.P.Team ?? int.MinValue;
        double scale = (Size / 2) / RangeUnits, cos = Math.Cos(yaw), sin = Math.Sin(yaw);
        Point ToRadar(double wx, double wy)
        {
            double dx = wx - cx, dy = wy - cy;
            double fwd = dx * cos + dy * sin, right = -dx * sin + dy * cos;
            return new Point(Size / 2 + right * scale, Size / 2 - fwd * scale);
        }
        dc.PushClip(new EllipseGeometry(centre, Size / 2 - 1, Size / 2 - 1));
        if (_snap != null && _snap.ZoneR > 0) dc.DrawEllipse(null, Dashed, ToRadar(_snap.ZoneX, _snap.ZoneY), _snap.ZoneR * scale, _snap.ZoneR * scale);
        dc.Pop();
        foreach (var p in _players)
        {
            if (_me != null && ReferenceEquals(p, _me)) continue;
            var at = ToRadar(p.X, p.Y);
            double fromCentre = (at - centre).Length;
            if (fromCentre > Size / 2 - 5) at = centre + (at - centre) * ((Size / 2 - 5) / fromCentre); // out of range: pinned to the edge
            var c = DevColors.For(p.P.Team, myTeam);
            if (p.P.Knocked) c = DevPaint.Fade(c, 0x80);
            dc.DrawEllipse(p.P.Bot ? null : DevPaint.BrushOf(c), DevPaint.PenOf(c, 2), at, 3.5, 3.5);
            // Height: a small tick above (higher than you) or below (lower), more than 4 m apart.
            double dz = p.Z - (_me?.Z ?? p.Z);
            if (Math.Abs(dz) > 400) dc.DrawLine(DevPaint.PenOf(c, 1.5), new Point(at.X - 3, at.Y + (dz > 0 ? -6 : 6)), new Point(at.X + 3, at.Y + (dz > 0 ? -6 : 6)));
        }
        var arrow = new StreamGeometry();
        using (var g = arrow.Open()) { g.BeginFigure(new Point(Size / 2, Size / 2 - 8), true, true); g.LineTo(new Point(Size / 2 - 5.5, Size / 2 + 6), true, false); g.LineTo(new Point(Size / 2 + 5.5, Size / 2 + 6), true, false); }
        dc.DrawGeometry(System.Windows.Media.Brushes.White, null, arrow);
    }
}

// Player markers over the game: a box around each player with name, team, health and distance, also behind
// builds. Players your game has loaded are placed from your game's own positions (exact, same frame as the
// camera); players too far away for your game to load use the server's position and are drawn fainter.
internal sealed class DevMarkersWindow : DevOverlay
{
    private readonly DevSurface _surface = new();
    private List<MainWindow.DevShown> _players = new();
    private MainWindow.DevShown? _me;
    private DevFeed.Camera _cam;
    private int _w, _h, _viewW, _viewH;

    public DevMarkersWindow() : base("Syndicate Dev Markers") { Content = _surface; _surface.Paint = Paint; }

    public void Show(IntPtr game, List<MainWindow.DevShown> players, MainWindow.DevShown? me, DevFeed.Camera cam, int viewW, int viewH)
    {
        if (!GameArea(game, out int gx, out int gy, out int w, out int h)) { Hide(); return; }
        Place(gx, gy, w, h);
        _players = players; _me = me; _cam = cam; _w = w; _h = h; _viewW = viewW; _viewH = viewH;
        _surface.InvalidateVisual();
    }

    private void Paint(DrawingContext dc)
    {
        if (_w <= 0) return;
        var source = PresentationSource.FromVisual(this);
        double sx = source?.CompositionTarget?.TransformFromDevice.M11 ?? 1, sy = source?.CompositionTarget?.TransformFromDevice.M22 ?? 1;
        double dpi = VisualTreeHelper.GetDpi(this).PixelsPerDip;
        var cam = _cam; int w = _w, h = _h;
        double pitch = cam.Pitch * Math.PI / 180, yaw = cam.Yaw * Math.PI / 180;
        double cp = Math.Cos(pitch), spch = Math.Sin(pitch), cyw = Math.Cos(yaw), syw = Math.Sin(yaw);
        // Unreal axes: X forward, Y right, Z up (the game camera never rolls). FOV is horizontal.
        double fx = cp * cyw, fy = cp * syw, fz = spch;
        double rx = -syw, ry = cyw;
        double ux = -spch * cyw, uy = -spch * syw, uz = cp;
        double focal = (w / 2.0) / Math.Tan(Math.Clamp(cam.Fov, 10, 170) * Math.PI / 360);
        bool Project(double x, double y, double z, out Point at)
        {
            double dx = x - cam.X, dy = y - cam.Y, dz = z - cam.Z;
            double depth = dx * fx + dy * fy + dz * fz;
            at = default;
            if (depth < 30) return false; // behind the camera
            at = new Point((w / 2.0 + (dx * rx + dy * ry) * focal / depth) * sx, (h / 2.0 - (dx * ux + dy * uy + dz * uz) * focal / depth) * sy);
            return true;
        }
        int myTeam = _me?.P.Team ?? int.MinValue;
        // Far ones first, so near players are drawn on top.
        foreach (var p in _players.OrderByDescending(p => Math.Pow(p.X - cam.X, 2) + Math.Pow(p.Y - cam.Y, 2) + Math.Pow(p.Z - cam.Z, 2)))
        {
            if (p.Local || (_me != null && ReferenceEquals(p, _me))) continue;
            Point feet, head;
            if (p.Live)
            {
                // Loaded in your game: exactly where the game draws them (its own projection, this frame).
                if (!p.Projected || _viewW <= 0 || _viewH <= 0) continue; // behind you or off screen
                double kx = (double)w / _viewW * sx, ky = (double)h / _viewH * sy;
                head = new Point(p.HeadX * kx, p.HeadY * ky); feet = new Point(p.FootX * kx, p.FootY * ky);
            }
            // Too far for your game to load: from the server's position (feet ~90 below the middle, head ~90 above).
            else if (!Project(p.X, p.Y, p.Z - 90, out feet) || !Project(p.X, p.Y, p.Z + 90, out head)) continue;
            double tall = Math.Max(6, feet.Y - head.Y), wide = tall * 0.45;
            double cxs = (feet.X + head.X) / 2;
            if (cxs < -100 || cxs > w * sx + 100 || head.Y > h * sy + 100 || feet.Y < -100) continue;
            var c = DevColors.For(p.P.Team, myTeam);
            byte alpha = p.Live ? (byte)0xFF : (byte)0x90;
            var line = DevPaint.Fade(c, alpha);
            var box = new Rect(cxs - wide / 2, head.Y, wide, tall);
            dc.DrawRectangle(null, DevPaint.PenOf(Color.FromArgb((byte)(alpha / 2), 0, 0, 0), 3), box); // dark outline: readable on snow and sky
            dc.DrawRectangle(null, DevPaint.PenOf(line, 1.4), box);
            // Health (green) and shield (blue) bars at the left of the box.
            double hp = Math.Clamp(p.P.Hp / 100, 0, 1), sh = Math.Clamp(p.P.Sh / 100, 0, 1);
            dc.DrawRectangle(DevPaint.BrushOf(Color.FromArgb(0xA0, 0, 0, 0)), null, new Rect(box.Left - 7, box.Top, 4, tall));
            dc.DrawRectangle(DevPaint.BrushOf(Color.FromArgb(alpha, 0x3B, 0xE4, 0x8B)), null, new Rect(box.Left - 7, box.Bottom - tall * hp, 4, tall * hp));
            if (sh > 0) dc.DrawRectangle(DevPaint.BrushOf(Color.FromArgb(alpha, 0x4C, 0xC2, 0xFF)), null, new Rect(box.Left - 12, box.Bottom - tall * sh, 4, tall * sh));
            double metres = Math.Sqrt(Math.Pow(p.X - cam.X, 2) + Math.Pow(p.Y - cam.Y, 2) + Math.Pow(p.Z - cam.Z, 2)) / 100;
            double size = Math.Clamp(13 - metres / 60, 9.5, 13);
            var name = DevPaint.Text($"{p.P.Name}{(p.P.Bot ? " · bot" : "")}  T{p.P.Team}{(p.P.Knocked ? "  KNOCKED" : "")}", size, line, dpi);
            var dist = DevPaint.Text($"{metres:0} m  ·  {p.P.Hp:0}/{p.P.Sh:0}", size - 1, DevPaint.Fade(Color.FromRgb(0xE6, 0xE9, 0xF5), alpha), dpi);
            var label = new Rect(cxs - name.Width / 2 - 5, box.Top - name.Height - 5, name.Width + 10, name.Height + 2);
            dc.DrawRoundedRectangle(DevPaint.BrushOf(Color.FromArgb(0xB0, 0x0B, 0x0E, 0x1C)), DevPaint.PenOf(line, 1), label, 3, 3);
            dc.DrawText(name, new Point(label.Left + 5, label.Top + 1));
            dc.DrawText(dist, new Point(cxs - dist.Width / 2, box.Bottom + 2));
        }
    }
}
#endif