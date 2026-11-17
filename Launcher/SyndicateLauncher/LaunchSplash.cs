using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Reflection;
using System.Runtime.InteropServices;
using System.Threading;
using System.Threading.Tasks;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Interop;
using System.Windows.Media;
using System.Windows.Media.Animation;
using System.Windows.Media.Effects;
using System.Windows.Media.Imaging;
using System.Windows.Shapes;
using System.Windows.Threading;

namespace SyndicateLauncher;

/// <summary>The theme colours and placement the splash is drawn with (taken on the main thread, see App.OnStartup).</summary>
public sealed record SplashLook(
    Color Accent, Color Accent2, Color Background, Color Surface, Color Text,
    bool Motion, Rect Bounds, string Version);

/// <summary>
/// Startup splash: the Syndicate mark and title animate in with a loading bar and status line while the main window
/// is built. It runs on its own UI thread, so it is on screen before the (large) main window is created and keeps
/// animating while the main thread is busy. It covers exactly the rectangle the main window opens in; when the
/// launcher has drawn its first frame, the splash fades down to the plain theme background and closes, and the main
/// window's matching cover (LaunchCover in MainWindow.xaml) fades to show the launcher. Every call is safe after
/// the splash is gone, and a splash that fails to start just means the launcher opens without one.
/// </summary>
public sealed class LaunchSplash
{
    private volatile Dispatcher? _dispatcher;
    private volatile SplashWindow? _window;

    private LaunchSplash() { }

    [DllImport("user32.dll")] private static extern IntPtr GetDC(IntPtr hwnd);
    [DllImport("user32.dll")] private static extern int ReleaseDC(IntPtr hwnd, IntPtr hdc);
    [DllImport("gdi32.dll")] private static extern int GetDeviceCaps(IntPtr hdc, int index);

    /// <summary>
    /// The main display's refresh rate (60 to 240). The hand-over fades run at this rate: WPF animations default to
    /// 60 frames a second, which on a 120 or 144 Hz screen shows each frame for an uneven number of refreshes.
    /// </summary>
    public static int DisplayRefreshRate { get; } = ReadRefreshRate();

    private static int ReadRefreshRate()
    {
        try
        {
            IntPtr dc = GetDC(IntPtr.Zero);
            if (dc == IntPtr.Zero) return 60;
            try
            {
                int hz = GetDeviceCaps(dc, 116); // VREFRESH
                return hz <= 1 ? 60 : Math.Clamp(hz, 60, 240);
            }
            finally { ReleaseDC(IntPtr.Zero, dc); }
        }
        catch { return 60; }
    }

    /// <summary>Shows the splash on its own thread. Returns null (no splash) if it could not be shown.</summary>
    public static LaunchSplash? TryShow(SplashLook look)
    {
        try
        {
            var splash = new LaunchSplash();
            using var shown = new ManualResetEventSlim();
            var thread = new Thread(() => splash.Run(look, shown))
            {
                IsBackground = true, // never keeps the launcher's process alive
                Name = "Syndicate splash"
            };
            thread.SetApartmentState(ApartmentState.STA);
            thread.Start();
            shown.Wait(TimeSpan.FromSeconds(3));
            return splash._window is null ? null : splash;
        }
        catch (Exception ex)
        {
            Debug.WriteLine("Splash not shown: " + ex);
            return null;
        }
    }

    private void Run(SplashLook look, ManualResetEventSlim shown)
    {
        try
        {
            var dispatcher = Dispatcher.CurrentDispatcher;
            SynchronizationContext.SetSynchronizationContext(new DispatcherSynchronizationContext(dispatcher));
            // A splash problem must never take the launcher down with it: drop the splash instead.
            dispatcher.UnhandledException += (_, e) =>
            {
                e.Handled = true;
                Debug.WriteLine("Splash error: " + e.Exception);
                try { _window?.Finish(null, instant: true); } catch { }
            };
            StartupTrace.Mark("splash thread up");
            var window = new SplashWindow(look);
            StartupTrace.Mark("splash built");
            window.Closed += (_, _) =>
            {
                _window = null;
                // Promptly: until the dispatcher stops, the splash's endless animations keep ticking.
                dispatcher.BeginInvokeShutdown(DispatcherPriority.Normal);
            };
            window.Show();
            StartupTrace.WatchFrames("splash");
            StartupTrace.WatchOperations("splash");
            _dispatcher = dispatcher;
            _window = window;
        }
        catch (Exception ex)
        {
            Debug.WriteLine("Splash not shown: " + ex);
            _window = null;
            return;
        }
        finally
        {
            try { shown.Set(); } catch (ObjectDisposedException) { }
        }

        try { Dispatcher.Run(); }
        catch (Exception ex) { Debug.WriteLine("Splash stopped: " + ex); }
        StartupTrace.Mark("splash thread exited");
    }

    /// <summary>Moves the loading bar to <paramref name="progress"/> (0 to 1) and shows the next status line.</summary>
    public void Report(double progress, string status) => Post(w => w.Report(progress, status));

    /// <summary>
    /// Finishes the bar, lets the intro play out, fades the splash down to the plain background and closes it.
    /// Completes once the splash is gone (or straight away if there is none).
    /// </summary>
    public Task FinishAsync()
    {
        var done = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        if (!Post(w => w.Finish(done, instant: false))) done.TrySetResult();
        return done.Task;
    }

    /// <summary>Closes the splash at once (startup failed or was cut short).</summary>
    public void Close() => Post(w => w.Finish(null, instant: true));

    private bool Post(Action<SplashWindow> action)
    {
        var dispatcher = _dispatcher;
        if (dispatcher is null || dispatcher.HasShutdownStarted || _window is null) return false;
        try
        {
            dispatcher.InvokeAsync(() =>
            {
                if (_window is { } window) action(window);
            });
            return true;
        }
        catch (Exception) { return false; }
    }
}

/// <summary>The splash window itself. Built in code so it needs nothing from the main thread's resources.</summary>
internal sealed class SplashWindow : Window
{
    // Design size of the centre block; it scales down (never up) on small screens.
    private const double BlockWidth = 640, BlockHeight = 520;
    private const int MinShownMs = 1500;   // the intro needs this long to play out
    private const int StatusDwellMs = 420; // each status line stays at least this long so it can be read
    private const int FinishDwellMs = 250; // "Almost done..." may replace a line that has been up this long
    private const int LastLineMs = 380;    // and stays this long before the fade
    internal const int OutroMs = 420;      // the splash fade; MainWindow's cover fade runs as long, mirrored

    private readonly SplashLook _look;
    private readonly Stopwatch _clock = Stopwatch.StartNew();
    private readonly Queue<string> _statusQueue = new();
    private readonly DispatcherTimer _statusTimer;
    private long _statusShownAt = -StatusDwellMs;
    private bool _finishing, _closed;

    private readonly Grid _scene = new();
    private readonly Grid _atmosphere = new();
    private readonly Canvas _particles = new() { IsHitTestVisible = false };
    private readonly Grid _mark = new() { Width = 300, Height = 300, RenderTransformOrigin = new Point(0.5, 0.5) };
    private readonly Ellipse _glow = new() { Width = 300, Height = 300, Opacity = 0 };
    private readonly Ellipse _ring = new() { Width = 176, Height = 176, Opacity = 0, StrokeThickness = 1.5, RenderTransformOrigin = new Point(0.5, 0.5) };
    private readonly Image _logo = new() { Width = 152, Height = 152, Opacity = 0, RenderTransformOrigin = new Point(0.5, 0.5) };
    private readonly Rectangle _shine = new() { Width = 152, Height = 152, IsHitTestVisible = false };
    private readonly StackPanel _titleBlock = new() { Opacity = 0, Margin = new Thickness(0, 6, 0, 0) };
    private readonly StackPanel _loader = new() { Opacity = 0, Margin = new Thickness(0, 34, 0, 0), HorizontalAlignment = HorizontalAlignment.Center };
    private readonly ScaleTransform _barFill = new(0.02, 1);
    private readonly Rectangle _shimmer = new() { Width = 70, HorizontalAlignment = HorizontalAlignment.Left };
    private int _barMove;
    private readonly TextBlock _status = new() { Text = "Initializing launcher...", Opacity = 0, TextAlignment = TextAlignment.Center };
    private readonly TranslateTransform _statusSlide = new();
    private readonly TextBlock _corner = new();

    public SplashWindow(SplashLook look)
    {
        _look = look;
        Title = "Syndicate Launcher";
        WindowStyle = WindowStyle.None;
        ResizeMode = ResizeMode.NoResize;
        WindowStartupLocation = WindowStartupLocation.Manual;
        Left = look.Bounds.Left;
        Top = look.Bounds.Top;
        Width = look.Bounds.Width;
        Height = look.Bounds.Height;
        Topmost = true; // the main window opens underneath it
        // Not in the taskbar: the main window opens underneath the splash a moment later and is the launcher's one
        // taskbar button. With both shown, Windows listed two "Syndicate Launcher" windows for the few seconds the
        // splash plays out (reported 2026-11-14: "2 tabs of the app open on my taskbar").
        ShowInTaskbar = false;
        UseLayoutRounding = true;
        SnapsToDevicePixels = true;
        // Same plain colour the main window's cover starts with, so the hand-over has no seam.
        Background = Frozen(new SolidColorBrush(look.Background));
        TextOptions.SetTextFormattingMode(this, TextFormattingMode.Display);
        // Like the main window: a normal (not layered) borderless window, so animating it stays cheap.
        // Windows draws the shadow and, on Windows 11, the rounded corners.
        System.Windows.Shell.WindowChrome.SetWindowChrome(this, new System.Windows.Shell.WindowChrome
        {
            CaptionHeight = 0, ResizeBorderThickness = new Thickness(0), GlassFrameThickness = new Thickness(0, 0, 0, 1),
            CornerRadius = new CornerRadius(0), UseAeroCaptionButtons = false
        });
        try { Icon = BitmapFrame.Create(Pack("Assets/syndicate.ico")); } catch { }

        BuildScene();
        Content = _scene;
        _scene.Opacity = 1;

        _statusTimer = new DispatcherTimer(TimeSpan.FromMilliseconds(60), DispatcherPriority.Normal, (_, _) => PumpStatus(), Dispatcher);
        SourceInitialized += (_, _) => RoundCorners();
        Closed += (_, _) => _closed = true;
        Loaded += (_, _) => PlayIntro();
    }

    // ------------------------------------------------------------------ scene

    private void BuildScene()
    {
        Color accent = _look.Accent, accent2 = _look.Accent2, bg = _look.Background, text = _look.Text;
        double w = _look.Bounds.Width, h = _look.Bounds.Height;

        // Atmosphere: a soft overhead light, two slow coloured light pools, drifting embers and a vignette.
        _atmosphere.Opacity = 0;
        _atmosphere.Children.Add(new Rectangle
        {
            Fill = Frozen(new LinearGradientBrush(Mix(bg, _look.Surface, 0.55), bg, 90))
        });
        _atmosphere.Children.Add(new Rectangle
        {
            Fill = Frozen(new RadialGradientBrush
            {
                Center = new Point(0.5, -0.05), GradientOrigin = new Point(0.5, -0.05), RadiusX = 0.75, RadiusY = 0.9,
                GradientStops = { new GradientStop(WithAlpha(Mix(text, accent, 0.5), 0x2A), 0), new GradientStop(WithAlpha(accent, 0), 1) }
            })
        });
        var pools = new Canvas { IsHitTestVisible = false, ClipToBounds = true };
        pools.Children.Add(LightPool(accent, 0x38, 820, w * 0.30 - 410, h * 0.42 - 410, 70, -40, 9));
        pools.Children.Add(LightPool(accent2, 0x2C, 760, w * 0.72 - 380, h * 0.62 - 380, -60, 36, 11));
        _atmosphere.Children.Add(pools);
        _particles.Width = w;
        _particles.Height = h;
        _particles.ClipToBounds = true;
        _atmosphere.Children.Add(_particles);
        _atmosphere.Children.Add(new Rectangle
        {
            IsHitTestVisible = false,
            Fill = Frozen(new RadialGradientBrush
            {
                RadiusX = 0.78, RadiusY = 0.9,
                GradientStops = { new GradientStop(Color.FromArgb(0, 0, 0, 0), 0.5), new GradientStop(Color.FromArgb(0xA0, 0, 0, 0), 1) }
            })
        });
        _scene.Children.Add(_atmosphere);

        // The mark: glow behind, a ring that ripples out when the logo lands, the logo and a light sweep across it.
        _glow.Fill = Frozen(new RadialGradientBrush
        {
            GradientStops = { new GradientStop(WithAlpha(accent, 0x70), 0), new GradientStop(WithAlpha(accent, 0x22), 0.45), new GradientStop(WithAlpha(accent, 0), 1) }
        });
        _ring.Stroke = Frozen(new SolidColorBrush(WithAlpha(Mix(accent, text, 0.35), 0xCC)));
        _ring.RenderTransform = new ScaleTransform(0.85, 0.85);

        var logoSource = new BitmapImage();
        logoSource.BeginInit();
        logoSource.UriSource = Pack("Assets/logo.png");
        logoSource.DecodePixelWidth = 304;
        logoSource.CacheOption = BitmapCacheOption.OnLoad;
        logoSource.EndInit();
        logoSource.Freeze();
        _logo.Source = logoSource;
        RenderOptions.SetBitmapScalingMode(_logo, BitmapScalingMode.HighQuality);
        _logo.RenderTransform = new TransformGroup { Children = { new ScaleTransform(0.82, 0.82), new TranslateTransform(0, 18) } };
        _logo.Effect = new BlurEffect { Radius = 16, KernelType = KernelType.Gaussian };

        // The sweep only lights the logo's own pixels (its alpha is the mask).
        _shine.OpacityMask = Frozen(new ImageBrush(logoSource) { Stretch = Stretch.Uniform });
        _shine.Fill = new LinearGradientBrush
        {
            StartPoint = new Point(0, 0), EndPoint = new Point(1, 1),
            GradientStops =
            {
                new GradientStop(Color.FromArgb(0, 255, 255, 255), 0.35),
                new GradientStop(Color.FromArgb(0x70, 255, 255, 255), 0.5),
                new GradientStop(Color.FromArgb(0, 255, 255, 255), 0.65)
            },
            RelativeTransform = new TranslateTransform(-1.2, -1.2)
        };
        _shine.RenderTransform = _logo.RenderTransform;

        _mark.Children.Add(_glow);
        _mark.Children.Add(_ring);
        _mark.Children.Add(_logo);
        _mark.Children.Add(_shine);

        // Title, with the word LAUNCHER spaced out underneath.
        var title = new TextBlock
        {
            Text = "SYNDICATE",
            FontFamily = TitleFont(),
            FontSize = 76,
            HorizontalAlignment = HorizontalAlignment.Center,
            Foreground = Frozen(new LinearGradientBrush(text, Mix(text, accent, 0.28), 90)),
            Effect = new DropShadowEffect { Color = accent, BlurRadius = 28, ShadowDepth = 0, Opacity = 0.45 }
        };
        var caption = new TextBlock
        {
            Text = "L  A  U  N  C  H  E  R",
            FontFamily = new FontFamily("Segoe UI Variable Display, Segoe UI"),
            FontWeight = FontWeights.SemiBold,
            FontSize = 12,
            Margin = new Thickness(0, 2, 0, 0),
            HorizontalAlignment = HorizontalAlignment.Center,
            Foreground = Frozen(new SolidColorBrush(WithAlpha(text, 0x8C)))
        };
        _titleBlock.Children.Add(title);
        _titleBlock.Children.Add(caption);
        _titleBlock.RenderTransform = new TranslateTransform(0, 16);

        // Loader: a thin bar with a light that runs along it, and the status line under it.
        var track = new Border
        {
            Width = 260, Height = 3, CornerRadius = new CornerRadius(1.5),
            Background = Frozen(new SolidColorBrush(WithAlpha(text, 0x1C))),
            ClipToBounds = true
        };
        _shimmer.Fill = Frozen(new LinearGradientBrush
        {
            StartPoint = new Point(0, 0.5), EndPoint = new Point(1, 0.5),
            GradientStops =
            {
                new GradientStop(Color.FromArgb(0, 255, 255, 255), 0),
                new GradientStop(Color.FromArgb(0x9A, 255, 255, 255), 0.5),
                new GradientStop(Color.FromArgb(0, 255, 255, 255), 1)
            }
        });
        _shimmer.RenderTransform = new TranslateTransform(-70, 0);
        var fill = new Grid
        {
            RenderTransformOrigin = new Point(0, 0.5),
            RenderTransform = _barFill,
            Background = Frozen(new LinearGradientBrush(accent, accent2, 0)),
            ClipToBounds = true,
            Children = { _shimmer }
        };
        track.Child = fill;
        var barGlow = new Border
        {
            Width = 260, Height = 3, CornerRadius = new CornerRadius(1.5), Child = track,
            Effect = new DropShadowEffect { Color = accent, BlurRadius = 10, ShadowDepth = 0, Opacity = 0.55 }
        };
        _status.FontFamily = new FontFamily("Segoe UI Variable Text, Segoe UI");
        _status.FontSize = 12.5;
        _status.Margin = new Thickness(0, 14, 0, 0);
        _status.Foreground = Frozen(new SolidColorBrush(WithAlpha(text, 0xA6)));
        _status.RenderTransform = _statusSlide;
        _loader.Children.Add(barGlow);
        _loader.Children.Add(_status);

        var block = new StackPanel { Width = BlockWidth, VerticalAlignment = VerticalAlignment.Center };
        block.Children.Add(_mark);
        block.Children.Add(_titleBlock);
        block.Children.Add(_loader);
        _scene.Children.Add(new Viewbox
        {
            Stretch = Stretch.Uniform,
            StretchDirection = StretchDirection.DownOnly,
            Margin = new Thickness(24),
            Child = new Grid { Width = BlockWidth, Height = BlockHeight, Children = { block } }
        });

        // Version (and DEV LAUNCHER on dev builds) in the corner.
        string corner = _look.Version;
#if DEV_BUILD
        corner = "DEV LAUNCHER   " + corner;
#endif
        _corner.Text = corner;
        _corner.FontFamily = new FontFamily("Segoe UI Variable Text, Segoe UI");
        _corner.FontSize = 11;
        _corner.Opacity = 0;
        _corner.Margin = new Thickness(0, 0, 22, 18);
        _corner.HorizontalAlignment = HorizontalAlignment.Right;
        _corner.VerticalAlignment = VerticalAlignment.Bottom;
        _corner.Foreground = Frozen(new SolidColorBrush(WithAlpha(text, 0x66)));
        _scene.Children.Add(_corner);
    }

    private UIElement LightPool(Color color, byte alpha, double size, double x, double y, double driftX, double driftY, double seconds)
    {
        var pool = new Ellipse
        {
            Width = size, Height = size,
            Fill = Frozen(new RadialGradientBrush
            {
                GradientStops = { new GradientStop(WithAlpha(color, alpha), 0), new GradientStop(WithAlpha(color, 0), 1) }
            })
        };
        Canvas.SetLeft(pool, x);
        Canvas.SetTop(pool, y);
        var drift = new TranslateTransform();
        pool.RenderTransform = drift;
        if (_look.Motion)
        {
            drift.BeginAnimation(TranslateTransform.XProperty, Loop(0, driftX, seconds));
            drift.BeginAnimation(TranslateTransform.YProperty, Loop(0, driftY, seconds * 1.3));
        }
        return pool;
    }

    private void SpawnParticles()
    {
        if (!_look.Motion) return;
        var random = new Random();
        double w = _look.Bounds.Width, h = _look.Bounds.Height;
        for (int i = 0; i < 34; i++)
        {
            double size = 1.4 + random.NextDouble() * 2.4;
            var color = random.NextDouble() < 0.55 ? Mix(_look.Accent, _look.Text, 0.4) : _look.Text;
            var dot = new Ellipse
            {
                Width = size, Height = size, Opacity = 0,
                Fill = Frozen(new SolidColorBrush(color)),
                RenderTransform = new TranslateTransform()
            };
            Canvas.SetLeft(dot, random.NextDouble() * w);
            _particles.Children.Add(dot);

            var life = TimeSpan.FromSeconds(7 + random.NextDouble() * 7);
            var start = TimeSpan.FromSeconds(-random.NextDouble() * life.TotalSeconds); // already in flight
            double peak = 0.25 + random.NextDouble() * 0.5;
            var rise = new DoubleAnimation(h + 10, h * (0.15 + random.NextDouble() * 0.35), life)
            {
                BeginTime = start, RepeatBehavior = RepeatBehavior.Forever
            };
            var fade = new DoubleAnimationUsingKeyFrames { BeginTime = start, Duration = life, RepeatBehavior = RepeatBehavior.Forever };
            fade.KeyFrames.Add(new LinearDoubleKeyFrame(0, KeyTime.FromPercent(0)));
            fade.KeyFrames.Add(new EasingDoubleKeyFrame(peak, KeyTime.FromPercent(0.3), new SineEase()));
            fade.KeyFrames.Add(new EasingDoubleKeyFrame(0, KeyTime.FromPercent(1), new SineEase { EasingMode = EasingMode.EaseIn }));
            dot.BeginAnimation(Canvas.TopProperty, rise);
            dot.BeginAnimation(OpacityProperty, fade);
            ((TranslateTransform)dot.RenderTransform).BeginAnimation(TranslateTransform.XProperty,
                Loop(-8 - random.NextDouble() * 10, 8 + random.NextDouble() * 10, 2.5 + random.NextDouble() * 3));
        }
    }

    // ------------------------------------------------------------------ animation

    private void PlayIntro()
    {
        _statusTimer.Start();
        if (!_look.Motion)
        {
            // Reduce motion: no movement, just a quick fade.
            _logo.Effect = null;
            ((ScaleTransform)((TransformGroup)_logo.RenderTransform).Children[0]).ScaleX = 1;
            ((ScaleTransform)((TransformGroup)_logo.RenderTransform).Children[0]).ScaleY = 1;
            ((TranslateTransform)((TransformGroup)_logo.RenderTransform).Children[1]).Y = 0;
            ((TranslateTransform)_titleBlock.RenderTransform).Y = 0;
            foreach (var e in new UIElement[] { _atmosphere, _logo, _titleBlock, _loader, _status, _corner })
                e.BeginAnimation(OpacityProperty, Anim(0, 1, 200));
            _glow.Opacity = 0.7;
            _statusShownAt = _clock.ElapsedMilliseconds;
            return;
        }

        var outQuint = new QuinticEase { EasingMode = EasingMode.EaseOut };
        var outCubic = new CubicEase { EasingMode = EasingMode.EaseOut };
        var outBack = new BackEase { EasingMode = EasingMode.EaseOut, Amplitude = 0.35 };

        SpawnParticles();
        _atmosphere.BeginAnimation(OpacityProperty, Anim(0, 1, 900, 0, outCubic));

        // Logo: fades in, scales up with a little overshoot, lifts and comes into focus.
        var logoParts = (TransformGroup)_logo.RenderTransform;
        var logoScale = (ScaleTransform)logoParts.Children[0];
        var logoLift = (TranslateTransform)logoParts.Children[1];
        _logo.BeginAnimation(OpacityProperty, Anim(0, 1, 520, 120, outCubic));
        logoScale.BeginAnimation(ScaleTransform.ScaleXProperty, Anim(0.82, 1, 900, 120, outBack));
        logoScale.BeginAnimation(ScaleTransform.ScaleYProperty, Anim(0.82, 1, 900, 120, outBack));
        logoLift.BeginAnimation(TranslateTransform.YProperty, Anim(18, 0, 900, 120, outQuint));
        if (_logo.Effect is BlurEffect blur)
        {
            var focus = Anim(16, 0, 700, 120, outCubic);
            focus.Completed += (_, _) => _logo.Effect = null; // no effect cost once it is sharp
            blur.BeginAnimation(BlurEffect.RadiusProperty, focus);
        }

        // Glow blooms behind it, then breathes slowly.
        var bloom = Anim(0, 1, 900, 250, outCubic);
        bloom.Completed += (_, _) =>
        {
            if (!_finishing) _glow.BeginAnimation(OpacityProperty, Loop(1, 0.62, 2.8));
        };
        _glow.BeginAnimation(OpacityProperty, bloom);

        // A ring ripples out as the logo lands.
        var ringScale = (ScaleTransform)_ring.RenderTransform;
        ringScale.BeginAnimation(ScaleTransform.ScaleXProperty, Anim(0.85, 1.55, 1100, 520, outQuint));
        ringScale.BeginAnimation(ScaleTransform.ScaleYProperty, Anim(0.85, 1.55, 1100, 520, outQuint));
        var ringFade = new DoubleAnimationUsingKeyFrames { BeginTime = Ms(520) };
        ringFade.KeyFrames.Add(new LinearDoubleKeyFrame(0, KeyTime.FromTimeSpan(Ms(0))));
        ringFade.KeyFrames.Add(new EasingDoubleKeyFrame(0.55, KeyTime.FromTimeSpan(Ms(160)), outCubic));
        ringFade.KeyFrames.Add(new EasingDoubleKeyFrame(0, KeyTime.FromTimeSpan(Ms(1100)), outCubic));
        _ring.BeginAnimation(OpacityProperty, Fps(ringFade));

        // A light sweep crosses the logo once it is in place.
        var sweep = (TranslateTransform)((LinearGradientBrush)_shine.Fill).RelativeTransform;
        var sweepEase = new SineEase { EasingMode = EasingMode.EaseInOut };
        sweep.BeginAnimation(TranslateTransform.XProperty, Anim(-1.2, 1.2, 950, 1000, sweepEase));
        sweep.BeginAnimation(TranslateTransform.YProperty, Anim(-1.2, 1.2, 950, 1000, sweepEase));

        // Title fades in and rises into place; the loader follows.
        _titleBlock.BeginAnimation(OpacityProperty, Anim(0, 1, 650, 520, outCubic));
        ((TranslateTransform)_titleBlock.RenderTransform).BeginAnimation(TranslateTransform.YProperty, Anim(16, 0, 800, 520, outQuint));
        _loader.BeginAnimation(OpacityProperty, Anim(0, 1, 500, 900, outCubic));
        _status.BeginAnimation(OpacityProperty, Anim(0, 1, 400, 950, outCubic));
        _corner.BeginAnimation(OpacityProperty, Anim(0, 1, 600, 1000, outCubic));
        _statusShownAt = _clock.ElapsedMilliseconds + 950;

        var run = new DoubleAnimation(-70, 330, TimeSpan.FromMilliseconds(1400))
        {
            BeginTime = Ms(1000), RepeatBehavior = RepeatBehavior.Forever,
            EasingFunction = new SineEase { EasingMode = EasingMode.EaseInOut }
        };
        _shimmer.RenderTransform.BeginAnimation(TranslateTransform.XProperty, Fps(run));
    }

    public void Report(double progress, string status)
    {
        if (_finishing) return;
        MoveBar(progress, 650);
        _statusQueue.Enqueue(status);
    }

    // Eases the bar to the target, then lets it creep a little further so it never looks stuck while a step runs.
    private void MoveBar(double target, int ms)
    {
        target = Math.Clamp(target, 0.02, 1);
        if (target < _barFill.ScaleX && !_finishing) return;
        int move = ++_barMove; // a newer move wins over this one's follow-up
        var to = new DoubleAnimation(target, TimeSpan.FromMilliseconds(ms)) { EasingFunction = new QuarticEase { EasingMode = EasingMode.EaseOut } };
        to.Completed += (_, _) =>
        {
            if (move != _barMove) return;
            if (_finishing || target >= 0.97) return;
            _barFill.BeginAnimation(ScaleTransform.ScaleXProperty,
                Fps(new DoubleAnimation(Math.Min(0.97, target + 0.07), TimeSpan.FromSeconds(6)) { EasingFunction = new CubicEase { EasingMode = EasingMode.EaseOut } }));
        };
        _barFill.BeginAnimation(ScaleTransform.ScaleXProperty, Fps(to));
    }

    // Shows queued status lines one at a time, each long enough to read, with a small cross-fade.
    private void PumpStatus(int dwellMs = StatusDwellMs)
    {
        if (_statusQueue.Count == 0 || _clock.ElapsedMilliseconds - _statusShownAt < dwellMs) return;
        string next = _statusQueue.Dequeue();
        if (next == _status.Text) return;
        _statusShownAt = _clock.ElapsedMilliseconds;
        if (!_look.Motion)
        {
            _status.Text = next;
            return;
        }
        var outEase = new CubicEase { EasingMode = EasingMode.EaseIn };
        var leave = Anim(_status.Opacity, 0, 130, 0, outEase);
        leave.Completed += (_, _) =>
        {
            _status.Text = next;
            var inEase = new CubicEase { EasingMode = EasingMode.EaseOut };
            _status.BeginAnimation(OpacityProperty, Anim(0, 1, 240, 0, inEase));
            _statusSlide.BeginAnimation(TranslateTransform.YProperty, Anim(6, 0, 280, 0, inEase));
        };
        _status.BeginAnimation(OpacityProperty, leave);
        _statusSlide.BeginAnimation(TranslateTransform.YProperty, Anim(0, -5, 130, 0, outEase));
    }

    /// <summary>
    /// Fills the bar, waits until the intro has played, then fades everything down to the plain background
    /// (the push-in on the mark leads into the launcher) and closes. <paramref name="instant"/> skips all that.
    /// </summary>
    public async void Finish(TaskCompletionSource? done, bool instant)
    {
        if (instant)
        {
            _finishing = true;
            _statusTimer.Stop();
            done?.TrySetResult();
            if (!_closed) Close();
            return;
        }
        if (_finishing) { done?.TrySetResult(); return; }
        _finishing = true;
        StartupTrace.Mark("splash finish (Almost done)");
        try
        {
            if (_look.Motion)
            {
                // "Almost done..." goes up straight away (as soon as the previous line has been read), while the
                // bar fills and the intro plays out, rather than after them.
                _statusQueue.Clear();
                if (_clock.ElapsedMilliseconds < _statusShownAt)
                    _status.Text = "Almost done..."; // the intro hasn't shown a status line yet: this is the one it fades in
                else
                {
                    _statusQueue.Enqueue("Almost done...");
                    PumpStatus(FinishDwellMs);
                }
                long barDone = _clock.ElapsedMilliseconds + Math.Max(380, MinShownMs - _clock.ElapsedMilliseconds);
                MoveBar(1, (int)(barDone - _clock.ElapsedMilliseconds));
                long giveUp = _clock.ElapsedMilliseconds + 3000;
                while (_clock.ElapsedMilliseconds < giveUp &&
                       (_statusQueue.Count > 0 || _clock.ElapsedMilliseconds < barDone ||
                        _clock.ElapsedMilliseconds - _statusShownAt < LastLineMs))
                {
                    PumpStatus(FinishDwellMs);
                    await Task.Delay(15);
                }
                _statusTimer.Stop();
                StartupTrace.Mark("splash fade");

                // The fade speeds up into the hand-over and the launcher's cover fade (MainWindow.OnLoaded) starts
                // at speed and slows down, so the two read as one movement with no pause on the plain background.
                // Both run at the display's refresh rate.
                var easeIn = new QuadraticEase { EasingMode = EasingMode.EaseIn };
                var fade = Outro(Anim(1, 0, OutroMs, 0, easeIn));
                var faded = new TaskCompletionSource();
                fade.Completed += (_, _) => faded.TrySetResult();
                _scene.BeginAnimation(OpacityProperty, fade);
                var push = new ScaleTransform(1, 1);
                _mark.RenderTransform = push;
                push.BeginAnimation(ScaleTransform.ScaleXProperty, Outro(Anim(1, 1.08, OutroMs, 0, easeIn)));
                push.BeginAnimation(ScaleTransform.ScaleYProperty, Outro(Anim(1, 1.08, OutroMs, 0, easeIn)));
                ((TranslateTransform)_titleBlock.RenderTransform).BeginAnimation(TranslateTransform.YProperty, Outro(Anim(0, 8, OutroMs, 0, easeIn)));
                // Close the moment the fade ends (not after a fixed wait), so the launcher takes over on the next frame.
                await Task.WhenAny(faded.Task, Task.Delay(OutroMs + 300));
            }
            else
            {
                MoveBar(1, 150);
                await Task.Delay(160);
                _statusTimer.Stop();
                _scene.BeginAnimation(OpacityProperty, Anim(1, 0, 160));
                await Task.Delay(170);
            }
        }
        catch (Exception ex) { Debug.WriteLine("Splash outro: " + ex); }
        finally
        {
            StartupTrace.Mark("splash close");
            done?.TrySetResult();
            if (!_closed) Close();
        }
    }

    // ------------------------------------------------------------------ helpers

    [DllImport("dwmapi.dll")]
    private static extern int DwmSetWindowAttribute(IntPtr hwnd, int attribute, ref int value, int size);

    private void RoundCorners()
    {
        try
        {
            var hwnd = new WindowInteropHelper(this).Handle;
            int round = 2; // DWMWCP_ROUND, no-op before Windows 11
            DwmSetWindowAttribute(hwnd, 33, ref round, sizeof(int));
            // No Windows open/close animation: when the splash closed, Windows kept animating it out on top of the
            // launcher for a few frames, so the screen held on the plain background and the launcher's fade then
            // appeared with a jump. The splash does its own fade.
            int off = 1;
            DwmSetWindowAttribute(hwnd, 3 /* DWMWA_TRANSITIONS_FORCEDISABLED */, ref off, sizeof(int));
        }
        catch (DllNotFoundException) { }
        catch (EntryPointNotFoundException) { }
    }

    private static Uri Pack(string path) => new("pack://application:,,,/SyndicateLauncher;component/" + path);

    private static FontFamily TitleFont()
    {
        try { return new FontFamily(Pack(""), "./Assets/BurbankBigCondensedBold.otf#Burbank Big Condensed"); }
        catch { return new FontFamily("Segoe UI Black"); }
    }

    private static TimeSpan Ms(int ms) => TimeSpan.FromMilliseconds(ms);

    // Smooth 60 FPS for the splash (the main window runs its always-on animations slower to save CPU).
    private static T Fps<T>(T timeline) where T : Timeline
    {
        Timeline.SetDesiredFrameRate(timeline, 60);
        return timeline;
    }

    private static T Outro<T>(T timeline) where T : Timeline
    {
        Timeline.SetDesiredFrameRate(timeline, LaunchSplash.DisplayRefreshRate);
        return timeline;
    }

    private static DoubleAnimation Anim(double from, double to, int ms, int delay = 0, IEasingFunction? ease = null) =>
        Fps(new DoubleAnimation(from, to, Ms(ms)) { BeginTime = Ms(delay), EasingFunction = ease });

    private static DoubleAnimation Loop(double from, double to, double seconds) =>
        Fps(new DoubleAnimation(from, to, TimeSpan.FromSeconds(seconds))
        {
            AutoReverse = true, RepeatBehavior = RepeatBehavior.Forever,
            EasingFunction = new SineEase { EasingMode = EasingMode.EaseInOut }
        });

    private static T Frozen<T>(T freezable) where T : Freezable
    {
        if (freezable.CanFreeze) freezable.Freeze();
        return freezable;
    }

    private static Color WithAlpha(Color c, byte a) => Color.FromArgb(a, c.R, c.G, c.B);

    private static Color Mix(Color a, Color b, double t)
    {
        byte Ch(byte x, byte y) => (byte)Math.Clamp(Math.Round(x + (y - x) * t), 0, 255);
        return Color.FromArgb(Ch(a.A, b.A), Ch(a.R, b.R), Ch(a.G, b.G), Ch(a.B, b.B));
    }
}
