// Syndicate © 2026 Syndicate. All rights reserved. Proprietary — no copying, distribution, or derivative works. See LICENSE.
using System;
using System.Diagnostics;
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

namespace SyndicateClassic;

// Syndicate Arena HUD. The Season 8-X clients never show their own in-match Hype widget on Syndicate,
// so the launcher draws one: a small click-through strip at the bottom left of the Fortnite
// window (just above the health bar) with the player's Hype, division, progress to the next division and
// what this match has earned. Data: backend /tilted/arena/live, fed by the game server's
// Runtime\Server\arena-live.json. Turn it off in Settings > Modded (creates
// %LOCALAPPDATA%\ProjectTilted\arena-hud-off.txt).
public partial class MainWindow
{
    private ArenaHudWindow? _arenaHud;
    private DispatcherTimer? _arenaTimer;
    private bool _arenaBusy;

    private static bool ArenaHudEnabled
    {
        get
        {
            try { return !File.Exists(System.IO.Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "ProjectTilted", "arena-hud-off.txt")); }
            catch { return true; }
        }
    }

    // Why the Arena HUD is or is not showing, written to Launcher\arena-hud.log whenever it changes.
    private string _arenaHudState = "";
    private void ArenaHudLog(string state)
    {
        if (state == _arenaHudState) return;
        _arenaHudState = state;
        try
        {
            var path = System.IO.Path.Combine(AppContext.BaseDirectory, "arena-hud.log");
            if (File.Exists(path) && new FileInfo(path).Length > 200_000) File.Delete(path);
            File.AppendAllText(path, DateTime.Now.ToString("yyyy-MM-dd HH:mm:ss") + "  " + state + Environment.NewLine);
        }
        catch { }
    }

    private void StartArenaHud()
    {
        StartNetworkHud(); // Pre-season network HUD (NetworkHud.cs) starts with it
        StartSprintByDefault(); // SprintByDefault.cs
        // The launcher's own Arena overlay is retired: the match server now puts the game's own Arena points
        // counter on screen (ClientNotifyMatchEntered, NotifyArenaMatchEntered in SpectraBots).
        return;
#pragma warning disable CS0162
        if (_arenaTimer != null) return;
        _arenaTimer = new DispatcherTimer { Interval = TimeSpan.FromSeconds(1) };
        _arenaTimer.Tick += async (_, __) => await TickArenaHud();
        _arenaTimer.Start();
        Closed += (_, __) => { _arenaTimer?.Stop(); try { _arenaHud?.Close(); } catch { } _arenaHud = null; };
    }

    private async Task TickArenaHud()
    {
        if (_arenaBusy) return;
        _arenaBusy = true;
        try
        {
            if (!ArenaHudEnabled || !_account.SignedIn) { ArenaHudLog(!ArenaHudEnabled ? "off in Modded tab" : "launcher not signed in"); _arenaHud?.Hide(); return; }
            var game = ArenaHudWindow.FindGameWindow();
            if (game == IntPtr.Zero) { ArenaHudLog("no Fortnite window"); _arenaHud?.Hide(); return; }
            JsonElement? live = null;
            string why = "";
            try { live = await _account.ArenaLive(); } catch (Exception error) { live = null; why = error.GetType().Name; }
            if (live is not JsonElement data || data.ValueKind != JsonValueKind.Object
                || !data.TryGetProperty("active", out var active) || active.ValueKind != JsonValueKind.True)
            { ArenaHudLog(live == null ? "backend gave no answer " + why : "not an Arena match for this account: " + live.Value.ToString()); _arenaHud?.Hide(); return; }
            _arenaHud ??= new ArenaHudWindow();
            _arenaHud.Update(data);
            ArenaHudLog(_arenaHud.FollowGame(ArenaHudWindow.FindGameWindow()));
        }
        catch (Exception error) { ArenaHudLog("error " + error.GetType().Name + ": " + error.Message); _arenaHud?.Hide(); }
        finally { _arenaBusy = false; }
    }
}

internal sealed class ArenaHudWindow : Window
{
    // Layout size in WPF units; the window is scaled to the game's height through the Viewbox.
    // Compact strip: the top ~18 units hold the "+20 ELIMINATION" pop, the rest is the panel.
    private const double DesignWidth = 200, DesignHeight = 62;

    private readonly TextBlock _league = Text(9, "#1A1405", FontWeights.Black);
    private readonly TextBlock _hype = Text(16, "#F5F7FF", FontWeights.Black);
    private readonly TextBlock _hypeLabel = Text(8, "#A3ABCB", FontWeights.Bold);
    private readonly TextBlock _match = Text(12, "#3BE48B", FontWeights.Black);
    private readonly TextBlock _next = Text(8, "#A3ABCB", FontWeights.SemiBold);
    private readonly TextBlock _popup = Text(11, "#FFD23F", FontWeights.Black);
    private readonly Rectangle _fill = new Rectangle { RadiusX = 1.5, RadiusY = 1.5 };
    private readonly ScaleTransform _fillScale = new ScaleTransform(0, 1);
    private readonly TranslateTransform _popupMove = new TranslateTransform();
    private int _lastEarned = int.MinValue, _lastKills = -1, _lastDivision = -1;
    private string _lastMatch = "";
    // Animation parts: the whole HUD slides in, the Hype number counts up, the bar fills smoothly with a light
    // sweep, the match total pops, the panel border flashes gold on a gain, and the popup springs up and floats.
    private readonly TranslateTransform _enter = new();
    private readonly ScaleTransform _matchPop = new(1, 1);
    private readonly ScaleTransform _popupScale = new(1, 1);
    private readonly ScaleTransform _chipPop = new(1, 1);
    private readonly SolidColorBrush _panelBorder = new(Color.FromArgb(0x88, 0x22, 0x28, 0x4A));
    private readonly System.Windows.Media.Effects.DropShadowEffect _panelGlow = new() { BlurRadius = 14, ShadowDepth = 0, Opacity = 0, Color = Color.FromRgb(0xFF, 0xD2, 0x3F) };
    private readonly Rectangle _barSweep = new() { Width = 26, HorizontalAlignment = HorizontalAlignment.Left, Opacity = 0, IsHitTestVisible = false };
    private readonly TranslateTransform _barSweepMove = new();
    private Grid? _root;
    private double _hypeShown = double.NaN, _hypeFrom, _hypeTo;
    private DateTime _hypeStart;
    private bool _counting;
    private static readonly IEasingFunction Out = new CubicEase { EasingMode = EasingMode.EaseOut };

    public ArenaHudWindow()
    {
        WindowStyle = WindowStyle.None;
        ResizeMode = ResizeMode.NoResize;
        AllowsTransparency = true;
        Background = Brushes.Transparent;
        ShowInTaskbar = false;
        ShowActivated = false;
        Topmost = true;
        Focusable = false;
        Width = DesignWidth; Height = DesignHeight;
        // Off screen until FollowGame moves it, so it never flashes in the wrong place.
        WindowStartupLocation = WindowStartupLocation.Manual; Left = -32000; Top = -32000;
        Title = "Syndicate Arena HUD";

        var panel = new Border
        {
            CornerRadius = new CornerRadius(6),
            Background = new LinearGradientBrush(Color.FromArgb(0xDD, 0x14, 0x18, 0x31), Color.FromArgb(0xDD, 0x0A, 0x0C, 0x18), 90),
            BorderBrush = _panelBorder,
            BorderThickness = new Thickness(1),
            Padding = new Thickness(6, 4, 8, 5),
            Effect = _panelGlow
        };
        var grid = new Grid();
        grid.RowDefinitions.Add(new RowDefinition { Height = GridLength.Auto });
        grid.RowDefinitions.Add(new RowDefinition { Height = GridLength.Auto });

        // Row 1: [DIV 6] 2,640 HYPE ............ +40
        var top = new Grid();
        top.ColumnDefinitions.Add(new ColumnDefinition { Width = GridLength.Auto });
        top.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(1, GridUnitType.Star) });
        top.ColumnDefinitions.Add(new ColumnDefinition { Width = GridLength.Auto });
        var chip = new Border { CornerRadius = new CornerRadius(3), Padding = new Thickness(4, 0, 4, 1), Margin = new Thickness(0, 0, 6, 0), VerticalAlignment = VerticalAlignment.Center,
            Background = new LinearGradientBrush(Color.FromRgb(0xFF, 0xD2, 0x3F), Color.FromRgb(0xF2, 0xB7, 0x05), 90), Child = _league,
            RenderTransformOrigin = new Point(0.5, 0.5), RenderTransform = _chipPop };
        var hypeLine = new StackPanel { Orientation = Orientation.Horizontal, VerticalAlignment = VerticalAlignment.Center };
        _hypeLabel.Text = "HYPE"; _hypeLabel.Margin = new Thickness(3, 0, 0, 2); _hypeLabel.VerticalAlignment = VerticalAlignment.Bottom;
        hypeLine.Children.Add(_hype); hypeLine.Children.Add(_hypeLabel);
        _match.VerticalAlignment = VerticalAlignment.Center; _match.Margin = new Thickness(6, 0, 0, 0);
        _match.RenderTransformOrigin = new Point(1, 0.5); _match.RenderTransform = _matchPop;
        Grid.SetColumn(hypeLine, 1); Grid.SetColumn(_match, 2);
        top.Children.Add(chip); top.Children.Add(hypeLine); top.Children.Add(_match);
        grid.Children.Add(top);

        // Row 2: thin progress bar to the next division + "360 to Div 7"
        var bottom = new Grid { Margin = new Thickness(0, 2, 0, 0) };
        bottom.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(1, GridUnitType.Star) });
        bottom.ColumnDefinitions.Add(new ColumnDefinition { Width = GridLength.Auto });
        var track = new Grid { Height = 3, VerticalAlignment = VerticalAlignment.Center };
        track.Children.Add(new Rectangle { RadiusX = 1.5, RadiusY = 1.5, Fill = new SolidColorBrush(Color.FromArgb(0xCC, 0x22, 0x28, 0x4A)) });
        _fill.Fill = new LinearGradientBrush(Color.FromRgb(0xFF, 0xD2, 0x3F), Color.FromRgb(0x4C, 0xC2, 0xFF), 0);
        _fill.RenderTransformOrigin = new Point(0, 0.5); _fill.RenderTransform = _fillScale;
        track.Children.Add(_fill);
        _barSweep.Fill = new LinearGradientBrush(new GradientStopCollection
        {
            new GradientStop(Color.FromArgb(0x00, 0xFF, 0xFF, 0xFF), 0), new GradientStop(Color.FromArgb(0xCC, 0xFF, 0xFF, 0xFF), 0.5), new GradientStop(Color.FromArgb(0x00, 0xFF, 0xFF, 0xFF), 1)
        }, new Point(0, 0.5), new Point(1, 0.5));
        _barSweep.RenderTransform = _barSweepMove; track.ClipToBounds = true;
        track.Children.Add(_barSweep);
        _next.Margin = new Thickness(6, 0, 0, 0); _next.VerticalAlignment = VerticalAlignment.Center;
        Grid.SetColumn(_next, 1);
        bottom.Children.Add(track); bottom.Children.Add(_next);
        Grid.SetRow(bottom, 1);
        grid.Children.Add(bottom);
        panel.Child = grid;

        _popup.HorizontalAlignment = HorizontalAlignment.Left; _popup.VerticalAlignment = VerticalAlignment.Top;
        _popup.Margin = new Thickness(4, 0, 0, 0); _popup.Opacity = 0;
        _popup.RenderTransformOrigin = new Point(0, 0.5);
        var popupTransform = new TransformGroup(); popupTransform.Children.Add(_popupScale); popupTransform.Children.Add(_popupMove);
        _popup.RenderTransform = popupTransform;
        _popup.Effect = new System.Windows.Media.Effects.DropShadowEffect { BlurRadius = 10, ShadowDepth = 0, Opacity = 0.95, Color = Color.FromRgb(0xB0, 0x7A, 0x00) };

        var root = new Grid { Width = DesignWidth, Height = DesignHeight, RenderTransform = _enter, Opacity = 0 };
        _root = root;
        panel.VerticalAlignment = VerticalAlignment.Bottom;
        root.Children.Add(panel); root.Children.Add(_popup);
        Content = new Viewbox { Stretch = Stretch.Uniform, Child = root };

        SourceInitialized += (_, __) =>
        {
            // Click-through, never takes focus from the game, not in Alt+Tab.
            var hwnd = new WindowInteropHelper(this).Handle;
            long style = GetWindowLongPtr(hwnd, GWL_EXSTYLE).ToInt64();
            style |= WS_EX_TRANSPARENT | WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE;
            SetWindowLongPtr(hwnd, GWL_EXSTYLE, new IntPtr(style));
        };
    }

    private static TextBlock Text(double size, string color, FontWeight weight) => new TextBlock
    {
        FontSize = size, FontWeight = weight, Foreground = new SolidColorBrush((Color)ColorConverter.ConvertFromString(color)),
        FontFamily = new FontFamily("Segoe UI"), TextTrimming = TextTrimming.CharacterEllipsis
    };

    private static int Int(JsonElement data, string name)
    {
        if (data.TryGetProperty(name, out var v) && v.ValueKind == JsonValueKind.Number && v.TryGetInt32(out int n)) return n;
        return 0;
    }

    private static string Signed(int value) => value > 0 ? "+" + value.ToString("N0") : value < 0 ? "−" + (-value).ToString("N0") : "0";

    public void Update(JsonElement data)
    {
        int hype = Int(data, "hype"), earned = Int(data, "earned"), division = Math.Max(1, Int(data, "division"));
        int kills = Int(data, "kills"), floor = Int(data, "divisionFloor");
        int? next = data.TryGetProperty("nextThreshold", out var n) && n.ValueKind == JsonValueKind.Number ? n.GetInt32() : null;
        string match = data.TryGetProperty("baseline", out var b) ? b.ToString() + "|" + Int(data, "started") : "";

        _league.Text = "DIV " + division;
        CountHypeTo(hype);
        string matchText = Signed(earned);
        if (_match.Text != matchText && _match.Text.Length > 0) Pop(_matchPop, 1.35);
        _match.Text = matchText;
        _match.Foreground = new SolidColorBrush(earned >= 0 ? Color.FromRgb(0x3B, 0xE4, 0x8B) : Color.FromRgb(0xFF, 0x5A, 0x6A));
        double fill = next is int top && top > floor ? Math.Clamp((hype - floor) / (double)(top - floor), 0, 1) : 1;
        if (Math.Abs(fill - _fillScale.ScaleX) > 0.001)
        {
            bool grew = fill > _fillScale.ScaleX;
            _fillScale.BeginAnimation(ScaleTransform.ScaleXProperty, new DoubleAnimation(fill, TimeSpan.FromMilliseconds(700)) { EasingFunction = Out });
            if (grew) Sweep();
        }
        _next.Text = next is int target ? $"{Math.Max(0, target - hype):N0} to Div {division + 1}" : "Top division";

        // "+20 ELIMINATION" style pop when this match's total goes up; "DIVISION UP" when the division rises.
        if (match != _lastMatch) { _lastMatch = match; _lastEarned = earned; _lastKills = kills; _lastDivision = division; return; }
        if (division > _lastDivision && _lastDivision > 0) { Pop(_chipPop, 1.4); ShowPopup("DIVISION UP"); }
        else if (earned > _lastEarned) ShowPopup($"{Signed(earned - _lastEarned)} {(kills > _lastKills ? "ELIMINATION" : "PLACEMENT")}");
        _lastEarned = earned; _lastKills = kills; _lastDivision = division;
    }

    private void ShowPopup(string text)
    {
        _popup.Text = text;
        var fade = new DoubleAnimationUsingKeyFrames { Duration = TimeSpan.FromMilliseconds(2300) };
        fade.KeyFrames.Add(new LinearDoubleKeyFrame(1, KeyTime.FromTimeSpan(TimeSpan.FromMilliseconds(120))));
        fade.KeyFrames.Add(new LinearDoubleKeyFrame(1, KeyTime.FromTimeSpan(TimeSpan.FromMilliseconds(1600))));
        fade.KeyFrames.Add(new LinearDoubleKeyFrame(0, KeyTime.FromTimeSpan(TimeSpan.FromMilliseconds(2300))));
        _popup.BeginAnimation(OpacityProperty, fade);
        // Springs in (overshoot), then floats up while it fades.
        var spring = new DoubleAnimationUsingKeyFrames { Duration = TimeSpan.FromMilliseconds(420) };
        spring.KeyFrames.Add(new EasingDoubleKeyFrame(0.5, KeyTime.FromPercent(0)));
        spring.KeyFrames.Add(new EasingDoubleKeyFrame(1.18, KeyTime.FromPercent(0.55), Out));
        spring.KeyFrames.Add(new EasingDoubleKeyFrame(1, KeyTime.FromPercent(1), Out));
        _popupScale.BeginAnimation(ScaleTransform.ScaleXProperty, spring); _popupScale.BeginAnimation(ScaleTransform.ScaleYProperty, spring);
        _popupMove.BeginAnimation(TranslateTransform.YProperty, new DoubleAnimation(10, -6, TimeSpan.FromMilliseconds(2300)) { EasingFunction = Out });
        // The panel border flashes gold with a soft glow.
        _panelBorder.BeginAnimation(SolidColorBrush.ColorProperty, new ColorAnimation(Color.FromArgb(0xEE, 0xFF, 0xD2, 0x3F), Color.FromArgb(0x88, 0x22, 0x28, 0x4A), TimeSpan.FromMilliseconds(1200)) { EasingFunction = Out });
        _panelGlow.BeginAnimation(System.Windows.Media.Effects.DropShadowEffect.OpacityProperty, new DoubleAnimation(0.85, 0, TimeSpan.FromMilliseconds(1200)) { EasingFunction = Out });
    }

    private static void Pop(ScaleTransform scale, double peak)
    {
        var pop = new DoubleAnimationUsingKeyFrames { Duration = TimeSpan.FromMilliseconds(380) };
        pop.KeyFrames.Add(new EasingDoubleKeyFrame(peak, KeyTime.FromPercent(0.35), Out));
        pop.KeyFrames.Add(new EasingDoubleKeyFrame(1, KeyTime.FromPercent(1), Out));
        scale.BeginAnimation(ScaleTransform.ScaleXProperty, pop); scale.BeginAnimation(ScaleTransform.ScaleYProperty, pop);
    }

    // A light sweep runs along the progress bar when it grows.
    private void Sweep()
    {
        var run = new DoubleAnimation(-30, 160, TimeSpan.FromMilliseconds(800)) { EasingFunction = new SineEase { EasingMode = EasingMode.EaseInOut } };
        _barSweepMove.BeginAnimation(TranslateTransform.XProperty, run);
        var show = new DoubleAnimationUsingKeyFrames { Duration = TimeSpan.FromMilliseconds(800) };
        show.KeyFrames.Add(new LinearDoubleKeyFrame(1, KeyTime.FromPercent(0.15)));
        show.KeyFrames.Add(new LinearDoubleKeyFrame(1, KeyTime.FromPercent(0.8)));
        show.KeyFrames.Add(new LinearDoubleKeyFrame(0, KeyTime.FromPercent(1)));
        _barSweep.BeginAnimation(OpacityProperty, show);
    }

    // The Hype number counts up (or down) to its new value over 0.8 s instead of jumping.
    private void CountHypeTo(int hype)
    {
        if (double.IsNaN(_hypeShown)) { _hypeShown = _hypeTo = hype; _hype.Text = hype.ToString("N0"); return; }
        if (hype == (int)Math.Round(_hypeTo)) return;
        _hypeFrom = _hypeShown; _hypeTo = hype; _hypeStart = DateTime.UtcNow;
        if (!_counting) { _counting = true; CompositionTarget.Rendering += CountFrame; }
    }

    private void CountFrame(object? sender, EventArgs e)
    {
        double t = Math.Min(1, (DateTime.UtcNow - _hypeStart).TotalMilliseconds / 800.0);
        double eased = 1 - Math.Pow(1 - t, 3);
        _hypeShown = _hypeFrom + (_hypeTo - _hypeFrom) * eased;
        _hype.Text = Math.Round(_hypeShown).ToString("N0");
        if (t >= 1) { _counting = false; CompositionTarget.Rendering -= CountFrame; }
    }

    // Places the HUD above the health bar, scaled to the game window, and only while the game is
    // the active window (so it never floats over Discord or the desktop).
    public string FollowGame(IntPtr game)
    {
        if (game == IntPtr.Zero || !GetClientRect(game, out RECT client)) { Hide(); return "game window gone"; }
        var origin = new POINT { X = 0, Y = 0 };
        if (!ClientToScreen(game, ref origin)) { Hide(); return "game window position unknown"; }
        int gameWidth = client.Right - client.Left, gameHeight = client.Bottom - client.Top;
        if (gameWidth < 200 || gameHeight < 200) { Hide(); return $"game window too small ({gameWidth}x{gameHeight})"; }
        IntPtr foreground = GetForegroundWindow();
        GetWindowThreadProcessId(foreground, out uint foregroundPid);
        GetWindowThreadProcessId(game, out uint gamePid);
        if (foregroundPid != gamePid) { Hide(); return $"game not in front (front pid {foregroundPid}, game pid {gamePid})"; }

        // Bottom left, sitting just above the health and shield bars.
        int width = (int)(gameHeight * 0.21), height = (int)(width * DesignHeight / DesignWidth);
        int x = origin.X + (int)(gameHeight * 0.03);
        int y = origin.Y + (int)(gameHeight * 0.845) - height;
        var hwnd = new WindowInteropHelper(this).EnsureHandle();
        SetWindowPos(hwnd, HWND_TOPMOST, x, y, width, height, SWP_NOACTIVATE);
        if (!IsVisible)
        {
            Show();
            // Slides in from the left and fades in each time it appears.
            _root?.BeginAnimation(OpacityProperty, new DoubleAnimation(0, 1, TimeSpan.FromMilliseconds(320)));
            _enter.BeginAnimation(TranslateTransform.XProperty, new DoubleAnimation(-28, 0, TimeSpan.FromMilliseconds(420)) { EasingFunction = new BackEase { EasingMode = EasingMode.EaseOut, Amplitude = 0.3 } });
        }
        SetWindowPos(hwnd, HWND_TOPMOST, x, y, width, height, SWP_NOACTIVATE | SWP_SHOWWINDOW);
        return $"showing at {x},{y} ({width}x{height}) over a {gameWidth}x{gameHeight} game";
    }

    // The visible Fortnite game window (the hidden game server process has no visible window).
    public static IntPtr FindGameWindow()
    {
        // The local match server is a FortniteClient-Win64-Shipping process too, and on some builds (11.00) it
        // has a visible window of its own. Prefer the Fortnite process that owns the foreground window, so the
        // HUD follows the game the player is looking at; otherwise the first visible one.
        GetWindowThreadProcessId(GetForegroundWindow(), out uint foregroundPid);
        IntPtr first = IntPtr.Zero;
        foreach (var process in Process.GetProcessesByName("FortniteClient-Win64-Shipping"))
        {
            try
            {
                var handle = process.MainWindowHandle;
                if (handle == IntPtr.Zero || !IsWindowVisible(handle) || IsIconic(handle)) continue;
                if ((uint)process.Id == foregroundPid) return handle;
                if (first == IntPtr.Zero) first = handle;
            }
            catch { }
            finally { process.Dispose(); }
        }
        return first;
    }

    private const int GWL_EXSTYLE = -20;
    private const long WS_EX_TRANSPARENT = 0x20, WS_EX_LAYERED = 0x80000, WS_EX_TOOLWINDOW = 0x80, WS_EX_NOACTIVATE = 0x08000000;
    private static readonly IntPtr HWND_TOPMOST = new IntPtr(-1);
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
    [DllImport("user32.dll")] private static extern bool IsWindowVisible(IntPtr hWnd);
    [DllImport("user32.dll")] private static extern bool IsIconic(IntPtr hWnd);
}