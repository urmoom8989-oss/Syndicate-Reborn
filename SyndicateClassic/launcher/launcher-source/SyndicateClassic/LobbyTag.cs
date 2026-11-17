// Syndicate © 2026 Syndicate. All rights reserved. Proprietary — no copying, distribution, or derivative works. See LICENSE.
using System;
using System.IO;
using System.Runtime.InteropServices;
using System.Text.Json;
using System.Threading.Tasks;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Interop;
using System.Windows.Media;
using System.Windows.Media.Animation;
using System.Windows.Shapes;
using System.Windows.Threading;
using IOPath = System.IO.Path;
using ShapePath = System.Windows.Shapes.Path;

namespace SyndicateClassic;

// Lobby loading tag: a click-through overlay over the Fortnite lobby (like the Arena HUD), in the top header
// strip above the LOCKER tab. It follows a match from start to finish:
//   Loading…            services / match server starting (also right after you press Play)
//   Loading island…     Erbium is loading the island
//   Match ready         green: press Play and you join in seconds
//   Joining match…      Play pressed, connecting
//   In the match        green check, then the tag slides away
//   Error: <reason>     red, shakes, stays ~20 s; the reason also goes to the launcher's Diagnostics log
//   Lost connection     the local services stopped; the launcher restarts them (see GameExitWatch.cs)
// Only while Fortnite is the active window. Toggle: Settings > Modded (off = lobby-tag-off.txt).
public partial class MainWindow
{
    private string _lastStatus = "";
    private DateTime _lastStatusAt = DateTime.MinValue;
    private LobbyTagWindow? _lobbyTag;
    private DispatcherTimer? _lobbyTagTimer;
    private bool _lobbyTagBusy, _lobbyBackendSeen;
    private LobbyTagKind _lobbyLast = LobbyTagKind.Hidden;
    private DateTime _lobbyInMatchUntil = DateTime.MinValue;
    private string _lobbyLoggedError = "";

    internal enum LobbyTagKind { Hidden, Loading, Island, Ready, Joining, InMatch, Error, Lost }

    internal static bool LobbyTagEnabled
    {
        get { try { return !File.Exists(HudFlag("lobby-tag-off.txt")); } catch { return true; } }
        set { SetHudFlag("lobby-tag-off.txt", !value); }
    }

    private void StartLobbyTag()
    {
        if (_lobbyTagTimer != null) return;
        _lobbyTagTimer = new DispatcherTimer { Interval = TimeSpan.FromMilliseconds(400) };
        _lobbyTagTimer.Tick += async (_, __) => await TickLobbyTag();
        _lobbyTagTimer.Start();
        Closed += (_, __) => { _lobbyTagTimer?.Stop(); try { _lobbyTag?.Close(); } catch { } _lobbyTag = null; };
    }

    private sealed record LobbyFacts(bool BackendUp, bool HostRunning, bool Connected, bool HostReady, bool RequestRecent, bool RequestAfterReady);

    private async Task TickLobbyTag()
    {
        if (_lobbyTagBusy) return;
        _lobbyTagBusy = true;
        try
        {
            var paths = _paths;
            if (!LobbyTagEnabled || !_sessionStarted || paths == null || RadminConnection.Remote) { HideLobbyTag(); _lobbyBackendSeen = false; return; }
            var game = ArenaHudWindow.FindGameWindow();
            if (game == IntPtr.Zero) { HideLobbyTag(); return; }
            var facts = await Task.Run(() => ReadLobbyFacts(paths));
            var (kind, text) = DecideLobbyTag(facts);
            if (kind == LobbyTagKind.Hidden) { HideLobbyTag(); return; }
            _lobbyTag ??= new LobbyTagWindow();
            _lobbyTag.SetState(kind, text);
            _lobbyTag.FollowGame(game);
        }
        catch { HideLobbyTag(); }
        finally { _lobbyTagBusy = false; }
    }

    private void HideLobbyTag() { _lobbyLast = LobbyTagKind.Hidden; _lobbyTag?.AnimateOut(); }

    // What the files and processes say (background thread).
    private static LobbyFacts ReadLobbyFacts(LauncherPaths paths)
    {
        bool backendUp = Launcher.IsBackendUp();
        string runtime = IOPath.Combine(paths.Root, "Runtime", "Server");
        string request = IOPath.Combine(runtime, "playlist-request.json"), readyFile = IOPath.Combine(runtime, "ready.pid");
        DateTime requestAt = DateTime.MinValue, readyAt = DateTime.MinValue;
        try { if (File.Exists(request)) requestAt = File.GetLastWriteTimeUtc(request); } catch { }
        try { if (File.Exists(readyFile)) readyAt = File.GetLastWriteTimeUtc(readyFile); } catch { }
        bool requestRecent = DateTime.UtcNow - requestAt < TimeSpan.FromMinutes(2);
        bool hostRunning = SessionProcesses.OwnedRunning(paths, "host");
        if (!hostRunning) return new LobbyFacts(backendUp, false, false, false, requestRecent, false);
        int hostPid = 0;
        try { using var state = JsonDocument.Parse(ReadClientLog(IOPath.Combine(runtime, "server.json"))); hostPid = state.RootElement.GetProperty("Pid").GetInt32(); } catch { }
        bool connected = false;
        try
        {
            using var occupancy = JsonDocument.Parse(ReadClientLog(IOPath.Combine(runtime, "occupancy.json")));
            var o = occupancy.RootElement;
            connected = hostPid != 0 && o.GetProperty("Pid").GetInt32() == hostPid && o.GetProperty("Connected").GetInt32() > 0;
        }
        catch { }
        bool ready = false;
        try { ready = hostPid != 0 && ReadClientLog(readyFile).Trim() == hostPid.ToString(); } catch { }
        return new LobbyFacts(backendUp, true, connected, ready, requestRecent, requestRecent && requestAt > readyAt);
    }

    // The tag to show (UI thread: it reads the launcher's status line and writes to the Diagnostics log).
    private (LobbyTagKind Kind, string Text) DecideLobbyTag(LobbyFacts f)
    {
        var now = DateTime.UtcNow;
        if (f.BackendUp) _lobbyBackendSeen = true;

        // A match that could not start: the launcher status says why. Shown in red and logged once.
        string status = _lastStatus ?? "";
        bool recent = now - _lastStatusAt < TimeSpan.FromSeconds(20);
        bool failed = recent && (status.StartsWith("Playlist could not start", StringComparison.OrdinalIgnoreCase)
            || status.Contains("could not prepare a match", StringComparison.OrdinalIgnoreCase)
            || status.Contains("froze", StringComparison.OrdinalIgnoreCase)
            || status.Contains("did not become joinable", StringComparison.OrdinalIgnoreCase));
        if (failed)
        {
            string reason = status.Contains(':') ? status[(status.IndexOf(':') + 1)..].Trim() : status;
            if (reason.Length > 70) reason = reason[..67] + "…";
            if (_lobbyLoggedError != status) { _lobbyLoggedError = status; Log("ERROR (lobby tag): match could not start — " + status); }
            return Remember(LobbyTagKind.Error, "Error: " + reason);
        }
        // The local services stopped after having been up: the watchdog restarts them.
        if (_lobbyBackendSeen && !f.BackendUp)
        {
            if (_lobbyLast != LobbyTagKind.Lost) Log("ERROR (lobby tag): lost connection to the local services; restarting them.");
            return Remember(LobbyTagKind.Lost, "Lost connection — reconnecting");
        }
        if (f.Connected)
        {
            // Just made it in: a short green "In the match", then the tag slides away for the rest of the match.
            if (_lobbyLast is LobbyTagKind.Loading or LobbyTagKind.Island or LobbyTagKind.Ready or LobbyTagKind.Joining) _lobbyInMatchUntil = now.AddSeconds(2.5);
            return now < _lobbyInMatchUntil ? Remember(LobbyTagKind.InMatch, "In the match — good luck") : Remember(LobbyTagKind.Hidden, "");
        }
        if (!f.BackendUp) return Remember(LobbyTagKind.Loading, "Loading");
        if (!f.HostRunning) return f.RequestRecent ? Remember(LobbyTagKind.Loading, "Loading") : Remember(LobbyTagKind.Hidden, "");
        if (!f.HostReady)
        {
            bool island = status.Contains("island", StringComparison.OrdinalIgnoreCase) && now - _lastStatusAt < TimeSpan.FromMinutes(5);
            return island ? Remember(LobbyTagKind.Island, "Loading island") : Remember(LobbyTagKind.Loading, "Loading");
        }
        return f.RequestAfterReady ? Remember(LobbyTagKind.Joining, "Joining match") : Remember(LobbyTagKind.Ready, "Match ready — press PLAY");
    }

    private (LobbyTagKind, string) Remember(LobbyTagKind kind, string text) { _lobbyLast = kind; return (kind, text); }
}

// The tag itself: a dark pill with a status icon, the SYNDICATE mark and the status text.
//   Loading / island / joining: blue, the dot pulses with an expanding ring, a light sweep runs across the
//     pill and the text gets animated dots.
//   Ready / in the match: green; the check pops in.  Error: red, the pill shakes.  Lost: amber, pulsing.
// Every change cross-fades: the border and glow colours blend, the text slides up into place, and the
// tag slides down into view / up out of view.
internal sealed class LobbyTagWindow : Window
{
    private const double DesignWidth = 380, DesignHeight = 44;
    private readonly Border _pill;
    private readonly Grid _pillContent;
    private readonly TextBlock _text = new() { FontSize = 13, FontWeight = FontWeights.SemiBold, Foreground = Brushes.White, FontFamily = new FontFamily("Segoe UI"), VerticalAlignment = VerticalAlignment.Center, TextTrimming = TextTrimming.CharacterEllipsis };
    private readonly TranslateTransform _textMove = new();
    private readonly Ellipse _dot = new() { Width = 9, Height = 9 };
    private readonly Ellipse _ring = new() { Width = 9, Height = 9, StrokeThickness = 1.6, RenderTransformOrigin = new Point(0.5, 0.5) };
    private readonly ScaleTransform _ringScale = new(1, 1);
    private readonly ShapePath _check = new() { Data = Geometry.Parse("M 1,6 L 5,10 L 13,1"), StrokeThickness = 2.4, StrokeStartLineCap = PenLineCap.Round, StrokeEndLineCap = PenLineCap.Round, StrokeLineJoin = PenLineJoin.Round, Width = 14, Height = 12, Stretch = Stretch.None, RenderTransformOrigin = new Point(0.5, 0.5), Opacity = 0 };
    private readonly ScaleTransform _checkScale = new(1, 1);
    private readonly TextBlock _bang = new() { Text = "!", FontSize = 14, FontWeight = FontWeights.Black, Foreground = Brushes.White, HorizontalAlignment = HorizontalAlignment.Center, VerticalAlignment = VerticalAlignment.Center, Opacity = 0 };
    private readonly Rectangle _sweep = new() { Width = 90, IsHitTestVisible = false, HorizontalAlignment = HorizontalAlignment.Left, Opacity = 0 };
    private readonly TranslateTransform _sweepMove = new();
    private readonly TranslateTransform _slide = new();
    private readonly TranslateTransform _shake = new();
    private readonly SolidColorBrush _accent = new(Color.FromRgb(0x4C, 0xC2, 0xFF));
    private readonly SolidColorBrush _border = new(Color.FromArgb(0xAA, 0x4C, 0xC2, 0xFF));
    private readonly System.Windows.Media.Effects.DropShadowEffect _glow = new() { BlurRadius = 18, ShadowDepth = 0, Opacity = 0.55, Color = Color.FromRgb(0x4C, 0xC2, 0xFF) };
    private readonly DispatcherTimer _dots = new() { Interval = TimeSpan.FromMilliseconds(380) };
    private int _dotCount;
    private string _baseText = "";
    private MainWindow.LobbyTagKind _kind = MainWindow.LobbyTagKind.Hidden;
    private bool _leaving;

    public LobbyTagWindow()
    {
        WindowStyle = WindowStyle.None; ResizeMode = ResizeMode.NoResize; AllowsTransparency = true; Background = Brushes.Transparent;
        ShowInTaskbar = false; ShowActivated = false; Topmost = true; Focusable = false;
        Width = DesignWidth; Height = DesignHeight; WindowStartupLocation = WindowStartupLocation.Manual; Left = -32000; Top = -32000;
        Title = "Syndicate Lobby Tag";

        // Status icon: pulsing dot + expanding ring (loading), check (ready / in match), "!" (error / lost).
        _dot.Fill = _accent; _ring.Stroke = _accent; _ring.RenderTransform = _ringScale; _check.Stroke = Brushes.White; _check.RenderTransform = _checkScale;
        var icon = new Grid { Width = 20, Height = 20, Margin = new Thickness(0, 0, 9, 0), VerticalAlignment = VerticalAlignment.Center };
        var iconDisc = new Ellipse { Width = 20, Height = 20, Fill = _accent, Opacity = 0 };
        icon.Children.Add(iconDisc); icon.Children.Add(_ring); icon.Children.Add(_dot); icon.Children.Add(_check); icon.Children.Add(_bang);
        _iconDisc = iconDisc;

        var brand = new TextBlock { Text = "SYNDICATE", FontSize = 11.5, FontWeight = FontWeights.Black, Foreground = new SolidColorBrush(Color.FromRgb(0xFF, 0xD2, 0x3F)), FontFamily = new FontFamily("Segoe UI"), VerticalAlignment = VerticalAlignment.Center };
        var divider = new Rectangle { Width = 1, Height = 14, Fill = new SolidColorBrush(Color.FromArgb(0x55, 0xFF, 0xFF, 0xFF)), Margin = new Thickness(9, 0, 9, 0), VerticalAlignment = VerticalAlignment.Center };
        _text.RenderTransform = _textMove;
        var row = new StackPanel { Orientation = Orientation.Horizontal, VerticalAlignment = VerticalAlignment.Center, Margin = new Thickness(11, 0, 16, 0) };
        row.Children.Add(icon); row.Children.Add(brand); row.Children.Add(divider); row.Children.Add(_text);

        // Light sweep across the pill while something loads.
        _sweep.Fill = new LinearGradientBrush(new GradientStopCollection
        {
            new GradientStop(Color.FromArgb(0x00, 0xFF, 0xFF, 0xFF), 0), new GradientStop(Color.FromArgb(0x2E, 0xFF, 0xFF, 0xFF), 0.5), new GradientStop(Color.FromArgb(0x00, 0xFF, 0xFF, 0xFF), 1)
        }, new Point(0, 0.5), new Point(1, 0.5));
        _sweep.RenderTransform = _sweepMove;

        _pillContent = new Grid();
        _pillContent.Children.Add(_sweep); _pillContent.Children.Add(row);
        _pill = new Border
        {
            CornerRadius = new CornerRadius(DesignHeight / 2), Height = DesignHeight - 6, HorizontalAlignment = HorizontalAlignment.Center, VerticalAlignment = VerticalAlignment.Center,
            Background = new LinearGradientBrush(Color.FromArgb(0xEE, 0x14, 0x18, 0x31), Color.FromArgb(0xEE, 0x0A, 0x0C, 0x18), 90),
            BorderBrush = _border, BorderThickness = new Thickness(1.2), Child = _pillContent, Effect = _glow, RenderTransform = _shake, ClipToBounds = false
        };
        _pill.SizeChanged += (_, e) => _pillContent.Clip = new RectangleGeometry(new Rect(0, 0, e.NewSize.Width - 2.4, e.NewSize.Height - 2.4), (e.NewSize.Height - 2.4) / 2, (e.NewSize.Height - 2.4) / 2);

        var root = new Grid { Width = DesignWidth, Height = DesignHeight, RenderTransform = _slide, Opacity = 0 };
        root.Children.Add(_pill);
        _root = root;
        Content = new Viewbox { Stretch = Stretch.Uniform, Child = root };

        _dots.Tick += (_, __) => { _dotCount = (_dotCount + 1) % 4; _text.Text = _baseText + new string('.', _dotCount); };
        SourceInitialized += (_, __) =>
        {
            var hwnd = new WindowInteropHelper(this).Handle;
            long style = GetWindowLongPtr(hwnd, GWL_EXSTYLE).ToInt64();
            style |= WS_EX_TRANSPARENT | WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE;
            SetWindowLongPtr(hwnd, GWL_EXSTYLE, new IntPtr(style));
        };
    }

    private readonly Grid _root;
    private readonly Ellipse _iconDisc;

    private static readonly Color Blue = Color.FromRgb(0x4C, 0xC2, 0xFF), Green = Color.FromRgb(0x3B, 0xE4, 0x8B), Red = Color.FromRgb(0xFF, 0x5A, 0x6A), Amber = Color.FromRgb(0xFF, 0xB0, 0x3B);
    private static readonly IEasingFunction EaseOut = new CubicEase { EasingMode = EasingMode.EaseOut };

    public void SetState(MainWindow.LobbyTagKind kind, string text)
    {
        bool loading = kind is MainWindow.LobbyTagKind.Loading or MainWindow.LobbyTagKind.Island or MainWindow.LobbyTagKind.Joining;
        if (kind == _kind && text == _baseText && !_leaving) return;
        bool changed = kind != _kind;
        _kind = kind; _baseText = text; _dotCount = 0; _text.Text = text;
        if (loading) { if (!_dots.IsEnabled) _dots.Start(); } else _dots.Stop();

        // Colours blend to the new state.
        var color = kind switch { MainWindow.LobbyTagKind.Ready or MainWindow.LobbyTagKind.InMatch => Green, MainWindow.LobbyTagKind.Error => Red, MainWindow.LobbyTagKind.Lost => Amber, _ => Blue };
        var blend = new Duration(TimeSpan.FromMilliseconds(320));
        _accent.BeginAnimation(SolidColorBrush.ColorProperty, new ColorAnimation(color, blend) { EasingFunction = EaseOut });
        _border.BeginAnimation(SolidColorBrush.ColorProperty, new ColorAnimation(Color.FromArgb(0xAA, color.R, color.G, color.B), blend) { EasingFunction = EaseOut });
        _glow.BeginAnimation(System.Windows.Media.Effects.DropShadowEffect.ColorProperty, new ColorAnimation(color, blend));

        // Text slides up into place.
        _text.BeginAnimation(OpacityProperty, new DoubleAnimation(0.2, 1, TimeSpan.FromMilliseconds(240)));
        _textMove.BeginAnimation(TranslateTransform.YProperty, new DoubleAnimation(7, 0, TimeSpan.FromMilliseconds(260)) { EasingFunction = EaseOut });

        // Icon per state.
        bool check = kind is MainWindow.LobbyTagKind.Ready or MainWindow.LobbyTagKind.InMatch;
        bool bang = kind is MainWindow.LobbyTagKind.Error or MainWindow.LobbyTagKind.Lost;
        _dot.Opacity = loading ? 1 : 0;
        _ring.Opacity = 0;
        _iconDisc.BeginAnimation(OpacityProperty, new DoubleAnimation(check || bang ? 1 : 0, TimeSpan.FromMilliseconds(200)));
        _check.BeginAnimation(OpacityProperty, new DoubleAnimation(check ? 1 : 0, TimeSpan.FromMilliseconds(180)));
        _bang.BeginAnimation(OpacityProperty, new DoubleAnimation(bang ? 1 : 0, TimeSpan.FromMilliseconds(180)));
        if (loading)
        {
            var pulse = new DoubleAnimation(1, 0.35, TimeSpan.FromMilliseconds(650)) { AutoReverse = true, RepeatBehavior = RepeatBehavior.Forever, EasingFunction = new SineEase() };
            _dot.BeginAnimation(OpacityProperty, pulse);
            var grow = new DoubleAnimation(1, 2.6, TimeSpan.FromMilliseconds(1300)) { RepeatBehavior = RepeatBehavior.Forever, EasingFunction = EaseOut };
            _ringScale.BeginAnimation(ScaleTransform.ScaleXProperty, grow); _ringScale.BeginAnimation(ScaleTransform.ScaleYProperty, grow);
            _ring.BeginAnimation(OpacityProperty, new DoubleAnimation(0.8, 0, TimeSpan.FromMilliseconds(1300)) { RepeatBehavior = RepeatBehavior.Forever });
            _sweep.Opacity = 1;
            _sweepMove.BeginAnimation(TranslateTransform.XProperty, new DoubleAnimation(-100, DesignWidth + 20, TimeSpan.FromMilliseconds(1700)) { RepeatBehavior = RepeatBehavior.Forever, EasingFunction = new SineEase { EasingMode = EasingMode.EaseInOut } });
        }
        else
        {
            _dot.BeginAnimation(OpacityProperty, null); _dot.Opacity = 0;
            _ring.BeginAnimation(OpacityProperty, null); _ring.Opacity = 0;
            _sweepMove.BeginAnimation(TranslateTransform.XProperty, null); _sweep.Opacity = 0;
        }
        if (check && changed)
        {
            // The check pops in.
            var pop = new DoubleAnimationUsingKeyFrames { Duration = TimeSpan.FromMilliseconds(420) };
            pop.KeyFrames.Add(new EasingDoubleKeyFrame(0.3, KeyTime.FromPercent(0)));
            pop.KeyFrames.Add(new EasingDoubleKeyFrame(1.25, KeyTime.FromPercent(0.6), EaseOut));
            pop.KeyFrames.Add(new EasingDoubleKeyFrame(1, KeyTime.FromPercent(1), EaseOut));
            _checkScale.BeginAnimation(ScaleTransform.ScaleXProperty, pop); _checkScale.BeginAnimation(ScaleTransform.ScaleYProperty, pop);
        }
        if (kind == MainWindow.LobbyTagKind.Error && changed)
        {
            // Short shake for an error.
            var shake = new DoubleAnimationUsingKeyFrames { Duration = TimeSpan.FromMilliseconds(480) };
            foreach (var (x, t) in new[] { (0.0, 0.0), (-7.0, 0.1), (7.0, 0.25), (-5.0, 0.4), (4.0, 0.55), (-2.0, 0.7), (0.0, 1.0) })
                shake.KeyFrames.Add(new LinearDoubleKeyFrame(x, KeyTime.FromPercent(t)));
            _shake.BeginAnimation(TranslateTransform.XProperty, shake);
        }
        if (kind == MainWindow.LobbyTagKind.Lost)
            _glow.BeginAnimation(System.Windows.Media.Effects.DropShadowEffect.OpacityProperty, new DoubleAnimation(0.25, 0.8, TimeSpan.FromMilliseconds(700)) { AutoReverse = true, RepeatBehavior = RepeatBehavior.Forever });
        else _glow.BeginAnimation(System.Windows.Media.Effects.DropShadowEffect.OpacityProperty, null);
        AnimateIn();
    }

    // Slides down into view (only when it was hidden or leaving).
    private void AnimateIn()
    {
        if (_root.Opacity > 0.99 && !_leaving) return;
        _leaving = false;
        _root.BeginAnimation(OpacityProperty, new DoubleAnimation(1, TimeSpan.FromMilliseconds(260)));
        _slide.BeginAnimation(TranslateTransform.YProperty, new DoubleAnimation(-14, 0, TimeSpan.FromMilliseconds(340)) { EasingFunction = new BackEase { EasingMode = EasingMode.EaseOut, Amplitude = 0.35 } });
    }

    // Slides up and fades, then hides the window.
    public void AnimateOut()
    {
        if (!IsVisible || _leaving) return;
        _leaving = true; _kind = MainWindow.LobbyTagKind.Hidden; _dots.Stop();
        var fade = new DoubleAnimation(0, TimeSpan.FromMilliseconds(240));
        fade.Completed += (_, __) => { if (_leaving) Hide(); };
        _root.BeginAnimation(OpacityProperty, fade);
        _slide.BeginAnimation(TranslateTransform.YProperty, new DoubleAnimation(-10, TimeSpan.FromMilliseconds(240)) { EasingFunction = EaseOut });
    }

    // In the dark header strip at the top of the lobby (above the tab row), centred over the LOCKER tab: in the
    // lobby the tab sits about 52% across, and the header strip is the top 7.4% of the screen. Scaled to the
    // game's height. Only while the game is the active window.
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
        int height = Math.Max(26, (int)(gameHeight * 0.042)), width = (int)(height * DesignWidth / DesignHeight);
        int x = origin.X + (int)(gameWidth * 0.52) - width / 2, y = origin.Y + (int)(gameHeight * 0.037) - height / 2;
        var hwnd = new WindowInteropHelper(this).EnsureHandle();
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