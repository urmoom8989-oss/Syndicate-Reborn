using System;
using System.Threading.Tasks;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Media;
using System.Windows.Media.Animation;
using System.Windows.Media.Imaging;
using System.Windows.Shapes;
using System.Windows.Threading;
using SyndicateLauncher.Models;
using SyndicateLauncher.Services;

namespace SyndicateLauncher.Controls;

/// <summary>
/// Fortnite Lobby Themes: the lobby art (Assets/Lobby/&lt;name&gt;.jpg) behind every page, like the Fortnite lobby.
/// <list type="bullet">
/// <item>Switching themes cross-fades from one picture to the next.</item>
/// <item>A slow camera drift (zoom and pan, about 40 seconds each way) keeps the scene alive, like the lobby's
/// moving camera. The theme's weather (snow, embers, leaves, storm flashes) is drawn by ThemeEffectsLayer.</item>
/// <item>A wash in the theme's background colour, heavier on the sidebar side and at the bottom, keeps text
/// readable. Themes > Wallpaper dim sets how strong it is.</item>
/// </list>
/// The picture is decoded off the UI thread. The drift stops (and nothing redraws) while the window is minimised,
/// while Fortnite runs (with "Pause effects in game" on), while a launch is starting, with Reduce motion on or with
/// lobby animations off. Never hit-testable. Collapsed when the theme has no wallpaper.
/// </summary>
public sealed class LobbyWallpaper : Grid
{
    private const double FadeMs = 750;

    private readonly Grid _stage = new();
    private readonly Image _a = MakeImage(), _b = MakeImage();
    private readonly Rectangle _wash = new(), _floor = new();
    // Start where the drift starts (OnDriftFrame at time 0), so its first frame doesn't jump.
    private readonly ScaleTransform _zoom = new(1.105, 1.105);
    private readonly TranslateTransform _pan = new(0, 1 + 11 * 0.96355818541719296);
    // Camera drift, stepped by hand on the shared AmbientClock (15 frames a second, the same frames as the
    // backdrop glow and the particles) instead of its own animation clock, so it never adds frames of its own.
    private bool _driftHooked;
    private long _driftSlot = -1;
    private double _driftTime, _driftLast;

    private Image _front;
    private string? _shown;   // wallpaper on screen (or fading in)
    private int _loadVersion;
    private Window? _window;
    private DispatcherTimer? _gameTimer;
    private bool _suspended;

    public LobbyWallpaper()
    {
        IsHitTestVisible = false;
        Focusable = false;
        ClipToBounds = true;
        Visibility = Visibility.Collapsed;
        _front = _a;

        _stage.RenderTransformOrigin = new Point(0.5, 0.5);
        // The pictures are cached at their laid-out size, after the high-quality downscale, and the camera drift
        // (the transform below) only moves and zooms that cached copy on the GPU. Without the cache WPF re-sampled
        // the 1920 px picture in software on every drift frame (HighQuality + an animated scale): 30 to 40 ms per
        // frame on the render thread, the stutter measured with the frame monitor (2026-11-14). The cache redraws
        // only while a cross-fade runs.
        _stage.CacheMode = new BitmapCache { RenderAtScale = 1.0, EnableClearType = false, SnapsToDevicePixels = false };
        var transforms = new TransformGroup();
        transforms.Children.Add(_zoom);
        transforms.Children.Add(_pan);
        _stage.RenderTransform = transforms;
        _stage.Children.Add(_a);
        _stage.Children.Add(_b);
        Children.Add(_stage);
        Children.Add(_wash);
        Children.Add(_floor);

        Loaded += (_, _) =>
        {
            ThemeService.Changed += Refresh;
            GameLauncher.StartingChanged += UpdateMotion;
            _window = Window.GetWindow(this);
            if (_window is not null)
            {
                _window.StateChanged += OnWindowStateChanged;
                _window.Activated += OnWindowStateChanged;
                _window.Deactivated += OnWindowStateChanged;
            }
            Refresh();
        };
        Unloaded += (_, _) =>
        {
            ThemeService.Changed -= Refresh;
            GameLauncher.StartingChanged -= UpdateMotion;
            if (_window is not null)
            {
                _window.StateChanged -= OnWindowStateChanged;
                _window.Activated -= OnWindowStateChanged;
                _window.Deactivated -= OnWindowStateChanged;
            }
            _gameTimer?.Stop();
            HookDrift(false);
        };
    }

    /// <summary>Holds the drift still while the opening titles cover the window (like ThemeEffectsLayer.Suspended).</summary>
    public bool Suspended
    {
        get => _suspended;
        set
        {
            if (_suspended == value) return;
            _suspended = value;
            UpdateMotion();
        }
    }

    private static Image MakeImage()
    {
        var img = new Image { Stretch = Stretch.UniformToFill, Opacity = 0, IsHitTestVisible = false };
        RenderOptions.SetBitmapScalingMode(img, BitmapScalingMode.HighQuality);
        return img;
    }

    private static ThemeSettings S => ThemeService.Settings;

    private void OnWindowStateChanged(object? sender, EventArgs e) => UpdateMotion();

    private bool MotionPaused =>
        _suspended || !ThemeService.MotionAllowed || !S.LobbyMotion
        || _window?.WindowState == WindowState.Minimized || _window?.IsActive == false
        || GameLauncher.Starting || (S.PauseInGame && GameLauncher.IsRunning);

    // ------------------------------------------------------------------ theme changes

    private void Refresh()
    {
        if (!IsLoaded) return;
        var palette = ThemeService.Target;
        UpdateWash(palette);
        string? wallpaper = palette.HasWallpaper ? palette.Wallpaper : null;
        if (wallpaper != _shown)
        {
            _shown = wallpaper;
            if (wallpaper is null) FadeOut();
            else _ = LoadAsync(wallpaper);
        }
        UpdateMotion();
    }

    private async Task LoadAsync(string wallpaper)
    {
        int version = ++_loadVersion;
        BitmapImage? bmp = await Task.Run(() =>
        {
            try
            {
                var b = new BitmapImage();
                b.BeginInit();
                b.UriSource = new Uri($"pack://application:,,,/SyndicateLauncher;component/Assets/Lobby/{wallpaper}.jpg", UriKind.Absolute);
                b.CacheOption = BitmapCacheOption.OnLoad;
                b.EndInit();
                b.Freeze();
                return b;
            }
            catch
            {
                return null;
            }
        });
        // A newer pick (or a switch to a theme without art) while this one was decoding wins.
        if (version != _loadVersion || bmp is null || _shown != wallpaper) return;
        ShowImage(bmp);
    }

    private void ShowImage(ImageSource source)
    {
        Visibility = Visibility.Visible;
        var incoming = _front.Source is null && _front.Opacity == 0 ? _front : (_front == _a ? _b : _a);
        var outgoing = incoming == _a ? _b : _a;
        incoming.Source = source;
        Panel.SetZIndex(incoming, 1);
        Panel.SetZIndex(outgoing, 0);
        _front = incoming;
        bool animate = ThemeService.MotionAllowed && S.SmoothTransitions;
        if (!animate)
        {
            incoming.BeginAnimation(OpacityProperty, null);
            incoming.Opacity = 1;
            Clear(outgoing);
            return;
        }
        var fade = new DoubleAnimation(1, TimeSpan.FromMilliseconds(FadeMs))
        {
            EasingFunction = new CubicEase { EasingMode = EasingMode.EaseInOut }
        };
        fade.Completed += (_, _) =>
        {
            if (_front == incoming) Clear(outgoing);
        };
        incoming.BeginAnimation(OpacityProperty, fade);
    }

    private static void Clear(Image img)
    {
        img.BeginAnimation(OpacityProperty, null);
        img.Opacity = 0;
        img.Source = null;
    }

    private void FadeOut()
    {
        ++_loadVersion;
        if (Visibility != Visibility.Visible) return;
        if (!ThemeService.MotionAllowed || !S.SmoothTransitions)
        {
            Clear(_a);
            Clear(_b);
            Visibility = Visibility.Collapsed;
            return;
        }
        var front = _front;
        var fade = new DoubleAnimation(0, TimeSpan.FromMilliseconds(FadeMs * 0.8))
        {
            EasingFunction = new CubicEase { EasingMode = EasingMode.EaseInOut }
        };
        fade.Completed += (_, _) =>
        {
            if (_shown is not null) return; // a lobby theme was picked again meanwhile
            Clear(_a);
            Clear(_b);
            Visibility = Visibility.Collapsed;
            UpdateMotion();
        };
        front.BeginAnimation(OpacityProperty, fade);
        // The wash fades with the picture so the plain theme background shows through evenly.
        _wash.Fill = null;
        _floor.Fill = null;
    }

    /// <summary>The wash in the theme's background colour: strong under the sidebar, lighter over the pages, and a floor at the bottom.</summary>
    private void UpdateWash(ThemePalette p)
    {
        if (!p.HasWallpaper)
        {
            _wash.Fill = null;
            _floor.Fill = null;
            return;
        }
        double d = S.WallpaperDim;
        byte A(double v) => (byte)Math.Clamp(Math.Round(v * 255), 0, 255);
        Color bg = p.Background;
        var across = new LinearGradientBrush { StartPoint = new Point(0, 0), EndPoint = new Point(1, 0) };
        across.GradientStops.Add(new GradientStop(ThemeColor.WithAlpha(bg, A(d + 0.32)), 0));
        across.GradientStops.Add(new GradientStop(ThemeColor.WithAlpha(bg, A(d + 0.08)), 0.22));
        across.GradientStops.Add(new GradientStop(ThemeColor.WithAlpha(bg, A(d * 0.82)), 0.6));
        across.GradientStops.Add(new GradientStop(ThemeColor.WithAlpha(bg, A(d * 0.7)), 1));
        across.Freeze();
        var down = new LinearGradientBrush { StartPoint = new Point(0, 0), EndPoint = new Point(0, 1) };
        down.GradientStops.Add(new GradientStop(ThemeColor.WithAlpha(bg, A(d * 0.35)), 0));
        down.GradientStops.Add(new GradientStop(ThemeColor.WithAlpha(bg, 0), 0.3));
        down.GradientStops.Add(new GradientStop(ThemeColor.WithAlpha(bg, 0), 0.62));
        down.GradientStops.Add(new GradientStop(ThemeColor.WithAlpha(bg, A(Math.Min(1, d + 0.3))), 1));
        down.Freeze();
        _wash.Fill = across;
        _floor.Fill = down;
    }

    // ------------------------------------------------------------------ camera drift

    private void UpdateMotion()
    {
        if (!IsLoaded) return;
        bool showing = _shown is not null;
        // Poll the game state only while a wallpaper is up (GameLauncher has no "exited" event).
        if (showing && S.PauseInGame)
        {
            _gameTimer ??= new DispatcherTimer(TimeSpan.FromSeconds(2), DispatcherPriority.Background, (_, _) => UpdateMotion(), Dispatcher);
            _gameTimer.Start();
        }
        else _gameTimer?.Stop();

        HookDrift(showing && !MotionPaused);
    }

    private void HookDrift(bool on)
    {
        if (on == _driftHooked) return;
        _driftHooked = on;
        if (on)
        {
            _driftLast = AmbientClock.Seconds;
            CompositionTarget.Rendering += OnDriftFrame;
        }
        else CompositionTarget.Rendering -= OnDriftFrame;
    }

    private void OnDriftFrame(object? sender, EventArgs e)
    {
        long slot = AmbientClock.Slot(AmbientClock.SlowFps);
        if (slot == _driftSlot) return;
        _driftSlot = slot;
        double now = AmbientClock.Seconds;
        // Only the time the drift actually ran counts, so it carries on from where it stopped after a pause.
        _driftTime += Math.Clamp(now - _driftLast, 0, 0.2);
        _driftLast = now;
        // Zoom 1.06 to 1.15 and back every 76 s, pan across every 94 s and up and down every 58 s: different
        // periods so the path never repeats. At 15 frames a second each step is well under a pixel.
        const double Tau = Math.PI * 2;
        double zoom = 1.105 + 0.045 * Math.Sin(_driftTime * Tau / 76);
        _zoom.ScaleX = zoom;
        _zoom.ScaleY = zoom;
        _pan.X = 24 * Math.Sin(_driftTime * Tau / 94);
        _pan.Y = 1 + 11 * Math.Sin(_driftTime * Tau / 58 + 1.3);
    }
}
