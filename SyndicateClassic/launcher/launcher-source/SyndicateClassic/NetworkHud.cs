// Syndicate © 2026 Syndicate. All rights reserved. Proprietary — no copying, distribution, or derivative works. See LICENSE.
using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Net.Http;
using System.Net.NetworkInformation;
using System.Runtime.InteropServices;
using System.Text.Json;
using System.Threading.Tasks;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Interop;
using System.Windows.Media;
using System.Windows.Threading;

namespace SyndicateClassic;

// Network HUD for Pre-season (1.7.2 has no net stats of its own). A small click-through panel at the
// top left of the Fortnite window:
//   PING to the match host (ICMP, once a second) with loss and jitter over the last 20 pings,
//   the host's server tick rate and worst frame, and the game ping the server reports for you
//   (backend /tilted/net/live, fed by the game server's Runtime\Server\net-live.json).
// Only while Pre-season runs and Fortnite is the active window. Toggle: Settings > Modded
// (off = %LOCALAPPDATA%\ProjectTilted\net-hud-off.txt).
public partial class MainWindow
{
    private NetHudWindow? _netHud;
    private DispatcherTimer? _netTimer;
    private bool _netBusy;
    private readonly List<long> _netPings = new(); // -1 = lost
    private int _netTick;
    private JsonElement? _netServer;
    private static readonly HttpClient NetHttp = new() { Timeout = TimeSpan.FromSeconds(1.5) };

    private static string HudFlag(string name) => System.IO.Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "ProjectTilted", name);
    internal static bool NetHudEnabled
    {
        get { try { return !File.Exists(HudFlag("net-hud-off.txt")); } catch { return true; } }
        set { SetHudFlag("net-hud-off.txt", !value); }
    }
    internal static void SetArenaHudEnabled(bool on) => SetHudFlag("arena-hud-off.txt", !on);
    private static void SetHudFlag(string name, bool present)
    {
        try
        {
            var file = HudFlag(name);
            if (present) { Directory.CreateDirectory(System.IO.Path.GetDirectoryName(file)!); File.WriteAllText(file, "off"); }
            else if (File.Exists(file)) File.Delete(file);
        }
        catch { }
    }

    private void StartNetworkHud()
    {
        if (_netTimer != null) return;
        _netTimer = new DispatcherTimer { Interval = TimeSpan.FromSeconds(1) };
        _netTimer.Tick += async (_, __) => await TickNetworkHud();
        _netTimer.Start();
        Closed += (_, __) => { _netTimer?.Stop(); try { _netHud?.Close(); } catch { } _netHud = null; };
    }

    private async Task TickNetworkHud()
    {
        if (_netBusy) return;
        _netBusy = true;
        try
        {
            // Pre-season only (season 0), while it runs.
            if (!NetHudEnabled || !_sessionStarted || _runningSeason != 0) { _netHud?.Hide(); _netPings.Clear(); _netServer = null; return; }
            var game = ArenaHudWindow.FindGameWindow();
            if (game == IntPtr.Zero) { _netHud?.Hide(); return; }
            string host = RadminConnection.Remote ? RadminConnection.Host : "127.0.0.1";

            long rtt = -1;
            try
            {
                using var ping = new Ping();
                var reply = await ping.SendPingAsync(host, 1000);
                if (reply.Status == IPStatus.Success) rtt = reply.RoundtripTime;
            }
            catch { rtt = -1; }
            _netPings.Add(rtt);
            if (_netPings.Count > 20) _netPings.RemoveAt(0);

            if (_netTick++ % 2 == 0)
            {
                try
                {
                    using var request = new System.Net.Http.HttpRequestMessage(System.Net.Http.HttpMethod.Get, $"http://{host}:3551/tilted/net/live");
                    if (RadminConnection.DirectKey is { Length: > 0 } directKey) request.Headers.TryAddWithoutValidation("x-tilted-direct", directKey);
                    using var reply = await NetHttp.SendAsync(request);
                    var text = await reply.Content.ReadAsStringAsync();
                    using var doc = JsonDocument.Parse(text);
                    _netServer = doc.RootElement.Clone();
                }
                catch { _netServer = null; }
            }

            var ok = _netPings.Where(p => p >= 0).ToList();
            int lossPercent = _netPings.Count == 0 ? 0 : (int)Math.Round(100.0 * (_netPings.Count - ok.Count) / _netPings.Count);
            double jitter = ok.Count < 2 ? 0 : ok.Zip(ok.Skip(1), (a, b) => Math.Abs(a - b)).Average();
            double tick = -1, frame = -1; int gamePing = -1;
            if (_netServer is JsonElement s && s.ValueKind == JsonValueKind.Object && s.TryGetProperty("active", out var active) && active.ValueKind == JsonValueKind.True)
            {
                if (s.TryGetProperty("tick", out var t) && t.ValueKind == JsonValueKind.Number) tick = t.GetDouble();
                if (s.TryGetProperty("worstFrameMs", out var f) && f.ValueKind == JsonValueKind.Number) frame = f.GetDouble();
                var me = (_account.Username ?? "").Trim();
                if (me.Length > 0 && s.TryGetProperty("players", out var players) && players.ValueKind == JsonValueKind.Array)
                    foreach (var p in players.EnumerateArray())
                        if (p.ValueKind == JsonValueKind.Object && p.TryGetProperty("name", out var n) && n.ValueKind == JsonValueKind.String
                            && string.Equals(n.GetString(), me, StringComparison.OrdinalIgnoreCase) && p.TryGetProperty("ping", out var pp) && pp.ValueKind == JsonValueKind.Number)
                            gamePing = pp.GetInt32();
            }
            _netHud ??= new NetHudWindow();
            _netHud.Update(RadminConnection.Remote ? "Host " + host : "Host: this PC", rtt, lossPercent, jitter, tick, frame, gamePing);
            _netHud.FollowGame(ArenaHudWindow.FindGameWindow());
        }
        catch { _netHud?.Hide(); }
        finally { _netBusy = false; }
    }
}

internal sealed class NetHudWindow : Window
{
    private const double DesignWidth = 190, DesignHeight = 44;
    private readonly TextBlock _ping = Line(11, "#3BE48B", FontWeights.Black);
    private readonly TextBlock _server = Line(8.5, "#C8CDE6", FontWeights.SemiBold);
    private readonly TextBlock _host = Line(7.5, "#8890B0", FontWeights.SemiBold);

    public NetHudWindow()
    {
        WindowStyle = WindowStyle.None; ResizeMode = ResizeMode.NoResize; AllowsTransparency = true; Background = Brushes.Transparent;
        ShowInTaskbar = false; ShowActivated = false; Topmost = true; Focusable = false;
        Width = DesignWidth; Height = DesignHeight;
        WindowStartupLocation = WindowStartupLocation.Manual; Left = -32000; Top = -32000;
        Title = "Syndicate Network HUD";
        var stack = new StackPanel();
        stack.Children.Add(_ping); stack.Children.Add(_server); stack.Children.Add(_host);
        var panel = new Border
        {
            CornerRadius = new CornerRadius(5), Padding = new Thickness(6, 3, 6, 4),
            Background = new SolidColorBrush(Color.FromArgb(0xB8, 0x0D, 0x10, 0x20)),
            BorderBrush = new SolidColorBrush(Color.FromArgb(0x88, 0x22, 0x28, 0x4A)), BorderThickness = new Thickness(1),
            Child = stack
        };
        var root = new Grid { Width = DesignWidth, Height = DesignHeight };
        panel.VerticalAlignment = VerticalAlignment.Top; panel.HorizontalAlignment = HorizontalAlignment.Left;
        root.Children.Add(panel);
        Content = new Viewbox { Stretch = Stretch.Uniform, Child = root };
        SourceInitialized += (_, __) =>
        {
            var hwnd = new WindowInteropHelper(this).Handle;
            long style = GetWindowLongPtr(hwnd, GWL_EXSTYLE).ToInt64();
            style |= WS_EX_TRANSPARENT | WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE;
            SetWindowLongPtr(hwnd, GWL_EXSTYLE, new IntPtr(style));
        };
    }

    private static TextBlock Line(double size, string color, FontWeight weight) => new()
    {
        FontSize = size, FontWeight = weight, Foreground = new SolidColorBrush((Color)ColorConverter.ConvertFromString(color)),
        FontFamily = new FontFamily("Segoe UI"), TextTrimming = TextTrimming.CharacterEllipsis
    };

    public void Update(string host, long rtt, int loss, double jitter, double tick, double frame, int gamePing)
    {
        string pingColor = rtt < 0 ? "#FF5A6A" : rtt < 60 ? "#3BE48B" : rtt < 120 ? "#FFD23F" : "#FF5A6A";
        _ping.Foreground = new SolidColorBrush((Color)ColorConverter.ConvertFromString(loss >= 10 ? "#FF5A6A" : pingColor));
        _ping.Text = (rtt < 0 ? "PING —" : $"PING {rtt} ms") + $"   LOSS {loss}%   JITTER {jitter:0} ms";
        var parts = new List<string>();
        parts.Add(tick > 0 ? $"SERVER {tick:0} Hz" : "SERVER —");
        if (frame > 0) parts.Add($"FRAME {frame:0} ms");
        if (gamePing > 0) parts.Add($"GAME PING {gamePing} ms");
        _server.Text = string.Join("   ", parts);
        _host.Text = host;
    }

    // Top left of the game window, scaled to its height; only while the game is the active window.
    public void FollowGame(IntPtr game)
    {
        if (game == IntPtr.Zero || !GetClientRect(game, out RECT client)) { Hide(); return; }
        var origin = new POINT { X = 0, Y = 0 };
        if (!ClientToScreen(game, ref origin)) { Hide(); return; }
        int gameWidth = client.Right - client.Left, gameHeight = client.Bottom - client.Top;
        if (gameWidth < 200 || gameHeight < 200) { Hide(); return; }
        GetWindowThreadProcessId(GetForegroundWindow(), out uint foregroundPid);
        GetWindowThreadProcessId(game, out uint gamePid);
        if (foregroundPid != gamePid) { Hide(); return; }
        int width = (int)(gameHeight * 0.2), height = (int)(width * DesignHeight / DesignWidth);
        int x = origin.X + (int)(gameHeight * 0.015), y = origin.Y + (int)(gameHeight * 0.015);
        var hwnd = new WindowInteropHelper(this).EnsureHandle();
        SetWindowPos(hwnd, HWND_TOPMOST, x, y, width, height, SWP_NOACTIVATE);
        if (!IsVisible) Show();
        SetWindowPos(hwnd, HWND_TOPMOST, x, y, width, height, SWP_NOACTIVATE | SWP_SHOWWINDOW);
    }

    private const int GWL_EXSTYLE = -20;
    private const long WS_EX_TRANSPARENT = 0x20, WS_EX_LAYERED = 0x80000, WS_EX_TOOLWINDOW = 0x80, WS_EX_NOACTIVATE = 0x08000000;
    private static readonly IntPtr HWND_TOPMOST = new(-1);
    private const uint SWP_NOACTIVATE = 0x0010, SWP_SHOWWINDOW = 0x0040;
    [StructLayout(LayoutKind.Sequential)] private struct RECT { public int Left, Top, Right, Bottom; }
    [StructLayout(LayoutKind.Sequential)] private struct POINT { public int X, Y; }
    [DllImport("user32.dll", EntryPoint = "GetWindowLongPtrW")] private static extern IntPtr GetWindowLongPtr(IntPtr hWnd, int nIndex);
    [DllImport("user32.dll", EntryPoint = "SetWindowLongPtrW")] private static extern IntPtr SetWindowLongPtr(IntPtr hWnd, int nIndex, IntPtr dwNewLong);
    [DllImport("user32.dll")] private static extern bool SetWindowPos(IntPtr hWnd, IntPtr hWndInsertAfter, int X, int Y, int cx, int cy, uint uFlags);
    [DllImport("user32.dll")] private static extern bool GetClientRect(IntPtr hWnd, out RECT lpRect);
    [DllImport("user32.dll")] private static extern bool ClientToScreen(IntPtr hWnd, ref POINT lpPoint);
    [DllImport("user32.dll")] private static extern IntPtr GetForegroundWindow();
    [DllImport("user32.dll")] private static extern uint GetWindowThreadProcessId(IntPtr hWnd, out uint lpdwProcessId);
}