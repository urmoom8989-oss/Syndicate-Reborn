using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Linq;
using System.Windows;
using System.Windows.Media;
using SyndicateLauncher.Models;
using SyndicateLauncher.Services;

namespace SyndicateLauncher.Controls;

public enum ThemeEffectsMode
{
    /// <summary>Behind the pages: the soft accent glow that drifts around.</summary>
    Backdrop,
    /// <summary>Over the pages (never takes clicks): particles and the RGB edge light.</summary>
    Overlay
}

/// <summary>
/// Draws the Themes tab's live effects with a single DrawingContext per frame, so even a few hundred particles
/// stay cheap. Never hit-testable. Stops drawing (and unhooks the frame callback) while the launcher is
/// minimised, while Fortnite runs (when "Pause in game" is on), or when there is nothing to animate.
/// </summary>
public sealed class ThemeEffectsLayer : FrameworkElement
{
    public static readonly DependencyProperty ModeProperty = DependencyProperty.Register(
        nameof(Mode), typeof(ThemeEffectsMode), typeof(ThemeEffectsLayer),
        new FrameworkPropertyMetadata(ThemeEffectsMode.Overlay, FrameworkPropertyMetadataOptions.AffectsRender,
            (d, _) => ((ThemeEffectsLayer)d).Refresh()));

    public ThemeEffectsMode Mode
    {
        get => (ThemeEffectsMode)GetValue(ModeProperty);
        set => SetValue(ModeProperty, value);
    }

    /// <summary>Corner radius of the RGB edge, to match the window.</summary>
    public double EdgeRadius { get; set; } = 15;

    private bool _suspended;

    /// <summary>Stops drawing while the opening titles cover the window.</summary>
    public bool Suspended
    {
        get => _suspended;
        set
        {
            if (_suspended == value) return;
            _suspended = value;
            Refresh();
        }
    }

    private struct Particle
    {
        public double X, Y, Vx, Vy, Size, Phase, Spin, Angle, Life;
        public Brush Brush;
    }

    private readonly List<Particle> _particles = new();
    private readonly Stopwatch _clock = Stopwatch.StartNew();
    private readonly Random _rng = new();
    private double _last;
    private bool _hooked;
    private Window? _window;
    private string _seededKind = "";
    private int _seededCount;
    private Size _seededSize;
    private Brush[] _orbs = Array.Empty<Brush>();
    private Pen? _rainPen;
    private readonly Dictionary<Color, Brush> _brushCache = new();

    public ThemeEffectsLayer()
    {
        IsHitTestVisible = false;
        Focusable = false;
        SnapsToDevicePixels = false;
        Loaded += (_, _) =>
        {
            ThemeService.Changed += Refresh;
            GameLauncher.StartingChanged += OnStartingChanged;
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
            GameLauncher.StartingChanged -= OnStartingChanged;
            if (_window is not null)
            {
                _window.StateChanged -= OnWindowStateChanged;
                _window.Activated -= OnWindowStateChanged;
                _window.Deactivated -= OnWindowStateChanged;
            }
            Hook(false);
        };
        SizeChanged += (_, _) => Refresh();
    }

    private void OnWindowStateChanged(object? sender, EventArgs e) => Refresh();

    // Play pressed (or the launch ended): re-check the game state now instead of at the next poll.
    private void OnStartingChanged()
    {
        _gameCheckedAt = -1;
        if (Paused)
        {
            Hook(false);
            InvalidateVisual();
            WaitForResume();
        }
        else Refresh();
    }

    private static ThemeSettings S => ThemeService.Settings;

    /// <summary>
    /// The particles on screen: the player's own choice, or else the Fortnite Lobby Theme's weather (snow on the
    /// winter lobbies, embers at Pirate Cove, leaves in the bamboo...) while lobby animations are on.
    /// </summary>
    private static string Kind => S.Particles != "None" ? S.Particles
        : S.LobbyMotion && ThemeService.Target.Ambience is { Length: > 0 } a ? a : "None";

    /// <summary>Storm light that flashes across the window now and then (Storm Forge, Fortnitemares, Season 5's rift).</summary>
    private static bool FlashOn => S.LobbyMotion && ThemeService.Target.Flash;

    // Checking the game's processes every frame costs a system call each time; refresh it twice a second.
    private bool _gameRunning;
    private double _gameCheckedAt = -1;

    private bool Paused
    {
        get
        {
            double now = _clock.Elapsed.TotalSeconds;
            if (now - _gameCheckedAt >= 0.5 || _gameCheckedAt < 0)
            {
                _gameCheckedAt = now;
                _gameRunning = S.PauseInGame && GameLauncher.IsRunning;
            }
            // Everything stops while the game is starting, whatever "Pause in game" says, and while another window
            // is in front (like the hero zoom and logo glow, WindowPerformance.cs), so the launcher costs nothing
            // in the background.
            return _suspended || _window?.WindowState == WindowState.Minimized || _window?.IsActive == false
                || _gameRunning || GameLauncher.Starting;
        }
    }

    // How often each layer redraws, on the shared AmbientClock so the layers draw on the same frames: the
    // backdrop glow drifts a few pixels a second (15 is plenty), particles and the RGB edge 30. Every redraw here
    // makes the window re-render under and over this layer, so it is never more often than this.
    private double FrameRate => Mode == ThemeEffectsMode.Backdrop ? AmbientClock.SlowFps : AmbientClock.FastFps;
    private long _lastSlot = -1;

    private bool HasWork => Mode == ThemeEffectsMode.Backdrop
        ? S.AmbientGlow && S.GlowStrength > 0.01
        : Kind != "None" || S.RgbEdge || FlashOn;

    private void Refresh()
    {
        if (!IsLoaded) return;
        _brushCache.Clear();
        _rainPen = null;
        BuildOrbs();
        Reseed(force: false);
        Hook(HasWork && !Paused && ThemeService.MotionAllowed);
        InvalidateVisual();
    }

    private void Hook(bool on)
    {
        if (on == _hooked) return;
        if (on)
        {
            _last = _clock.Elapsed.TotalSeconds;
            CompositionTarget.Rendering += OnFrame;
        }
        else CompositionTarget.Rendering -= OnFrame;
        _hooked = on;
    }

    private void OnFrame(object? sender, EventArgs e)
    {
        if (Paused)
        {
            // Fortnite started or the window was minimised: stop costing anything until it changes back.
            Hook(false);
            InvalidateVisual();
            WaitForResume();
            return;
        }
        long slot = AmbientClock.Slot(FrameRate);
        if (slot == _lastSlot) return;
        _lastSlot = slot;
        double now = _clock.Elapsed.TotalSeconds;
        double dt = Math.Clamp(now - _last, 0, 0.07);
        _last = now;
        if (Mode == ThemeEffectsMode.Overlay) Step(dt, now);
        InvalidateVisual();
    }

    private System.Windows.Threading.DispatcherTimer? _resumeTimer;

    private void WaitForResume()
    {
        _resumeTimer ??= new System.Windows.Threading.DispatcherTimer(TimeSpan.FromSeconds(2),
            System.Windows.Threading.DispatcherPriority.Background, (_, _) =>
            {
                if (Paused) return;
                _resumeTimer?.Stop();
                Refresh();
            }, Dispatcher);
        _resumeTimer.Start();
    }

    // ------------------------------------------------------------------ particles

    private int TargetCount()
    {
        int baseCount = Kind switch
        {
            "Snow" => 130, "Rain" => 170, "Stars" => 150, "Embers" => 80,
            "Fireflies" => 46, "Bubbles" => 50, "Confetti" => 110, "Leaves" => 36, "Motes" => 70, _ => 0
        };
        return (int)Math.Round(baseCount * (0.25 + S.ParticleAmount * 1.25));
    }

    private void Reseed(bool force)
    {
        if (Mode != ThemeEffectsMode.Overlay) return;
        int count = TargetCount();
        var size = RenderSize;
        bool sizeChanged = Math.Abs(size.Width - _seededSize.Width) > 40 || Math.Abs(size.Height - _seededSize.Height) > 40;
        if (!force && _seededKind == Kind && _seededCount == count && !sizeChanged)
        {
            // Same particles: only recolour them for the new theme.
            for (int i = 0; i < _particles.Count; i++)
            {
                var p = _particles[i];
                p.Brush = ColourFor(Kind, i);
                _particles[i] = p;
            }
            return;
        }
        _particles.Clear();
        _seededKind = Kind;
        _seededCount = count;
        _seededSize = size;
        double w = Math.Max(1, size.Width), h = Math.Max(1, size.Height);
        for (int i = 0; i < count; i++) _particles.Add(Spawn(i, w, h, scatter: true));
    }

    private double R(double min, double max) => min + _rng.NextDouble() * (max - min);

    private Particle Spawn(int index, double w, double h, bool scatter)
    {
        var p = new Particle { Phase = R(0, Math.PI * 2), Brush = ColourFor(Kind, index), Life = 1 };
        switch (Kind)
        {
            case "Snow":
                p.Size = R(0.8, 2.8); p.Vy = R(18, 55) * (0.6 + p.Size / 4); p.Vx = R(-8, 8);
                p.X = R(0, w); p.Y = scatter ? R(0, h) : R(-30, -4);
                break;
            case "Rain":
                p.Size = R(9, 22); p.Vy = R(520, 820); p.Vx = -p.Vy * 0.12;
                p.X = R(0, w + 80); p.Y = scatter ? R(0, h) : R(-60, -10);
                break;
            case "Stars":
                p.Size = R(0.5, 1.9); p.Vx = R(-3, 3); p.Vy = R(-2, 2);
                p.X = R(0, w); p.Y = R(0, h); p.Spin = R(0.6, 2.2);
                break;
            case "Embers":
                p.Size = R(0.9, 2.6); p.Vy = -R(22, 70); p.Vx = R(-10, 10);
                p.X = R(0, w); p.Y = scatter ? R(0, h) : h + R(4, 30); p.Life = scatter ? R(0.2, 1) : 1;
                break;
            case "Fireflies":
                p.Size = R(1.4, 2.8); p.Vx = R(-14, 14); p.Vy = R(-14, 14);
                p.X = R(0, w); p.Y = R(0, h); p.Spin = R(0.5, 1.6);
                break;
            case "Bubbles":
                p.Size = R(2.5, 9); p.Vy = -R(14, 42); p.Vx = 0;
                p.X = R(0, w); p.Y = scatter ? R(0, h) : h + R(10, 40);
                break;
            case "Confetti":
                p.Size = R(3, 6.5); p.Vy = R(45, 110); p.Vx = R(-20, 20);
                p.X = R(0, w); p.Y = scatter ? R(0, h) : R(-40, -6); p.Spin = R(-260, 260); p.Angle = R(0, 360);
                break;
            case "Leaves":
                // Drift down and sideways, tumbling, like leaves off the trees in the lobby art.
                p.Size = R(5, 10); p.Vy = R(28, 62); p.Vx = R(-28, 6);
                p.X = R(0, w + 60); p.Y = scatter ? R(0, h) : R(-40, -10); p.Spin = R(-110, 110); p.Angle = R(0, 360);
                break;
            case "Motes":
                // Dust caught in the light: rises slowly and breathes in and out.
                p.Size = R(0.8, 2.3); p.Vy = -R(5, 17); p.Vx = R(-6, 6); p.Spin = R(0.4, 1.3);
                p.X = R(0, w); p.Y = scatter ? R(0, h) : h + R(4, 30);
                break;
        }
        return p;
    }

    private void Step(double dt, double now)
    {
        double w = Math.Max(1, RenderSize.Width), h = Math.Max(1, RenderSize.Height);
        for (int i = 0; i < _particles.Count; i++)
        {
            var p = _particles[i];
            switch (Kind)
            {
                case "Snow":
                    p.X += (p.Vx + Math.Sin(now * 0.9 + p.Phase) * 12) * dt; p.Y += p.Vy * dt;
                    if (p.Y > h + 6) p = Spawn(i, w, h, false);
                    break;
                case "Rain":
                    p.X += p.Vx * dt; p.Y += p.Vy * dt;
                    if (p.Y > h + 20) p = Spawn(i, w, h, false);
                    break;
                case "Stars":
                    p.X += p.Vx * dt; p.Y += p.Vy * dt;
                    if (p.X < -4) p.X = w + 4; else if (p.X > w + 4) p.X = -4;
                    if (p.Y < -4) p.Y = h + 4; else if (p.Y > h + 4) p.Y = -4;
                    break;
                case "Embers":
                    p.X += (p.Vx + Math.Sin(now * 1.7 + p.Phase) * 16) * dt; p.Y += p.Vy * dt;
                    p.Life -= dt * 0.16;
                    if (p.Y < -10 || p.Life <= 0) p = Spawn(i, w, h, false);
                    break;
                case "Fireflies":
                    p.Vx += Math.Sin(now * 0.7 + p.Phase) * 10 * dt; p.Vy += Math.Cos(now * 0.6 + p.Phase * 1.3) * 10 * dt;
                    p.Vx = Math.Clamp(p.Vx, -18, 18); p.Vy = Math.Clamp(p.Vy, -18, 18);
                    p.X += p.Vx * dt; p.Y += p.Vy * dt;
                    if (p.X < -10) p.X = w + 10; else if (p.X > w + 10) p.X = -10;
                    if (p.Y < -10) p.Y = h + 10; else if (p.Y > h + 10) p.Y = -10;
                    break;
                case "Bubbles":
                    p.X += Math.Sin(now * 1.3 + p.Phase) * 14 * dt; p.Y += p.Vy * dt;
                    if (p.Y < -16) p = Spawn(i, w, h, false);
                    break;
                case "Confetti":
                    p.X += (p.Vx + Math.Sin(now * 2 + p.Phase) * 18) * dt; p.Y += p.Vy * dt; p.Angle += p.Spin * dt;
                    if (p.Y > h + 10) p = Spawn(i, w, h, false);
                    break;
                case "Leaves":
                    p.X += (p.Vx + Math.Sin(now * 1.1 + p.Phase) * 34) * dt;
                    p.Y += (p.Vy + Math.Cos(now * 1.7 + p.Phase) * 10) * dt;
                    p.Angle += p.Spin * dt;
                    if (p.Y > h + 14 || p.X < -30) p = Spawn(i, w, h, false);
                    break;
                case "Motes":
                    p.X += (p.Vx + Math.Sin(now * 0.6 + p.Phase) * 6) * dt; p.Y += p.Vy * dt;
                    if (p.Y < -10) p = Spawn(i, w, h, false);
                    break;
            }
            _particles[i] = p;
        }
    }

    private Brush ColourFor(string kind, int index)
    {
        var pal = ThemeService.Target;
        Color c = kind switch
        {
            "Snow" => ThemeColor.WithAlpha(ThemeColor.Mix(Colors.White, pal.Accent, 0.12), (byte)(150 + index * 37 % 90)),
            "Rain" => ThemeColor.WithAlpha(ThemeColor.Mix(pal.Text, pal.Accent, 0.35), 70),
            "Stars" => ThemeColor.WithAlpha(index % 5 == 0 ? pal.Accent : Colors.White, 230),
            "Embers" => ThemeColor.WithAlpha(index % 3 == 0 ? pal.Accent : ThemeColor.Mix(Color.FromRgb(0xFF, 0x7A, 0x2C), pal.Accent, 0.25), 235),
            "Fireflies" => ThemeColor.WithAlpha(ThemeColor.Mix(pal.Accent, Color.FromRgb(0xFF, 0xF2, 0x8A), 0.45), 240),
            "Bubbles" => ThemeColor.WithAlpha(ThemeColor.Mix(pal.Accent, Colors.White, 0.35), 120),
            "Confetti" => ConfettiColour(pal, index),
            "Leaves" => ThemeColor.WithAlpha(ThemeColor.Mix(index % 3 == 0 ? pal.Accent2 : pal.Accent, Colors.Black, (index % 4) * 0.08), 225),
            "Motes" => ThemeColor.WithAlpha(ThemeColor.Mix(pal.Accent, Colors.White, 0.45), 235),
            _ => Colors.Transparent
        };
        return Cached(c);
    }

    private static Color ConfettiColour(ThemePalette pal, int index) => (index % 6) switch
    {
        0 => pal.Accent,
        1 => pal.Accent2,
        2 => Colors.White,
        _ => ThemeColor.FromHsv(index * 47 % 360, 0.7, 1)
    };

    private Brush Cached(Color c)
    {
        if (_brushCache.TryGetValue(c, out var b)) return b;
        var nb = new SolidColorBrush(c);
        nb.Freeze();
        _brushCache[c] = nb;
        return nb;
    }

    // ------------------------------------------------------------------ backdrop glow

    private void BuildOrbs()
    {
        if (Mode != ThemeEffectsMode.Backdrop) { _orbs = Array.Empty<Brush>(); return; }
        var pal = ThemeService.Target;
        double s = S.GlowStrength;
        Brush Orb(Color c, double alpha)
        {
            var g = new RadialGradientBrush();
            g.GradientStops.Add(new GradientStop(ThemeColor.WithAlpha(c, (byte)Math.Clamp(alpha * 255, 0, 255)), 0));
            g.GradientStops.Add(new GradientStop(ThemeColor.WithAlpha(c, (byte)Math.Clamp(alpha * 90, 0, 255)), 0.45));
            g.GradientStops.Add(new GradientStop(ThemeColor.WithAlpha(c, 0), 1));
            g.Freeze();
            return g;
        }
        _orbs = new[]
        {
            Orb(pal.Accent, 0.30 * s),
            Orb(pal.Accent2, 0.24 * s),
            Orb(ThemeColor.Mix(pal.Accent, pal.Accent2, 0.5), 0.18 * s)
        };
    }

    // ------------------------------------------------------------------ drawing

    protected override void OnRender(DrawingContext dc)
    {
        double w = RenderSize.Width, h = RenderSize.Height;
        if (w < 2 || h < 2) return;
        double now = ThemeService.MotionAllowed ? _clock.Elapsed.TotalSeconds : 0;
        if (Mode == ThemeEffectsMode.Backdrop)
        {
            if (!S.AmbientGlow || _orbs.Length == 0) return;
            double big = Math.Max(w, h);
            for (int i = 0; i < _orbs.Length; i++)
            {
                double speed = 0.05 + i * 0.017;
                double cx = w * (0.5 + 0.42 * Math.Sin(now * speed + i * 2.1));
                double cy = h * (0.5 + 0.38 * Math.Cos(now * speed * 0.8 + i * 1.3));
                double r = big * (0.42 - i * 0.06);
                dc.DrawEllipse(_orbs[i], null, new Point(cx, cy), r, r * 0.8);
            }
            return;
        }

        if (Kind != "None" && ThemeService.MotionAllowed && !Paused) DrawParticles(dc, now);
        if (FlashOn && ThemeService.MotionAllowed && !Paused) DrawFlash(dc, w, h, now);
        if (S.RgbEdge) DrawEdge(dc, w, h, now);
    }

    private void DrawParticles(DrawingContext dc, double now)
    {
        switch (Kind)
        {
            case "Rain":
                _rainPen ??= MakePen(_particles.Count > 0 ? _particles[0].Brush : Brushes.White, 1.1);
                foreach (var p in _particles)
                    dc.DrawLine(_rainPen, new Point(p.X, p.Y), new Point(p.X - p.Size * 0.12, p.Y + p.Size));
                break;
            case "Stars":
                foreach (var p in _particles)
                {
                    double twinkle = 0.35 + 0.65 * (0.5 + 0.5 * Math.Sin(now * p.Spin + p.Phase));
                    dc.PushOpacity(twinkle);
                    dc.DrawEllipse(p.Brush, null, new Point(p.X, p.Y), p.Size, p.Size);
                    dc.Pop();
                }
                break;
            case "Embers":
                foreach (var p in _particles)
                {
                    dc.PushOpacity(Math.Clamp(p.Life, 0, 1));
                    dc.DrawEllipse(p.Brush, null, new Point(p.X, p.Y), p.Size, p.Size);
                    dc.Pop();
                }
                break;
            case "Fireflies":
                foreach (var p in _particles)
                {
                    double pulse = 0.25 + 0.75 * (0.5 + 0.5 * Math.Sin(now * p.Spin * 2 + p.Phase));
                    dc.PushOpacity(pulse * 0.22);
                    dc.DrawEllipse(p.Brush, null, new Point(p.X, p.Y), p.Size * 4.5, p.Size * 4.5);
                    dc.Pop();
                    dc.PushOpacity(pulse);
                    dc.DrawEllipse(p.Brush, null, new Point(p.X, p.Y), p.Size, p.Size);
                    dc.Pop();
                }
                break;
            case "Bubbles":
                foreach (var p in _particles)
                    dc.DrawEllipse(null, MakePen(p.Brush, 1), new Point(p.X, p.Y), p.Size, p.Size);
                break;
            case "Confetti":
                foreach (var p in _particles)
                {
                    dc.PushTransform(new RotateTransform(p.Angle, p.X, p.Y));
                    // Squash with the spin so the paper looks like it flips.
                    double flip = Math.Abs(Math.Cos((p.Angle + p.Phase * 57) * Math.PI / 180));
                    dc.DrawRectangle(p.Brush, null, new Rect(p.X - p.Size / 2, p.Y - p.Size * 0.3 * flip, p.Size, Math.Max(0.6, p.Size * 0.6 * flip)));
                    dc.Pop();
                }
                break;
            case "Leaves":
                foreach (var p in _particles)
                {
                    dc.PushTransform(new RotateTransform(p.Angle, p.X, p.Y));
                    // Squash with the tumble so the leaf turns edge-on and back.
                    double turn = 0.25 + 0.75 * Math.Abs(Math.Cos((p.Angle * 1.6 + p.Phase * 57) * Math.PI / 180));
                    dc.DrawEllipse(p.Brush, null, new Point(p.X, p.Y), p.Size * 0.55, p.Size * 0.24 * turn + 0.6);
                    dc.Pop();
                }
                break;
            case "Motes":
                foreach (var p in _particles)
                {
                    double breathe = 0.2 + 0.8 * (0.5 + 0.5 * Math.Sin(now * p.Spin + p.Phase));
                    dc.PushOpacity(breathe * 0.16);
                    dc.DrawEllipse(p.Brush, null, new Point(p.X, p.Y), p.Size * 4, p.Size * 4);
                    dc.Pop();
                    dc.PushOpacity(breathe);
                    dc.DrawEllipse(p.Brush, null, new Point(p.X, p.Y), p.Size, p.Size);
                    dc.Pop();
                }
                break;
            default:
                foreach (var p in _particles)
                    dc.DrawEllipse(p.Brush, null, new Point(p.X, p.Y), p.Size, p.Size);
                break;
        }
    }

    // ------------------------------------------------------------------ storm flash

    private double _nextFlash = -1, _flashStart = -10;

    /// <summary>
    /// Lightning: every 7 to 16 seconds a quick double flash in the theme's light (a strike, a dimmer echo, then it
    /// fades), kept faint so it never hides the page.
    /// </summary>
    private void DrawFlash(DrawingContext dc, double w, double h, double now)
    {
        if (_nextFlash < 0) _nextFlash = now + R(3, 7);
        if (now >= _nextFlash)
        {
            _flashStart = now;
            _nextFlash = now + R(7, 16);
        }
        double t = now - _flashStart;
        double f = t < 0.06 ? t / 0.06
            : t < 0.16 ? 1 - (t - 0.06) / 0.10 * 0.75
            : t < 0.22 ? 0.25 + (t - 0.16) / 0.06 * 0.55
            : t < 0.75 ? 0.8 * (1 - (t - 0.22) / 0.53)
            : 0;
        if (f <= 0.001) return;
        var pal = ThemeService.Target;
        var g = new RadialGradientBrush { Center = new Point(0.62, 0.0), GradientOrigin = new Point(0.62, 0.0), RadiusX = 1.1, RadiusY = 1.2 };
        g.GradientStops.Add(new GradientStop(ThemeColor.WithAlpha(ThemeColor.Mix(pal.Accent, Colors.White, 0.6), 255), 0));
        g.GradientStops.Add(new GradientStop(ThemeColor.WithAlpha(pal.Accent, 120), 0.5));
        g.GradientStops.Add(new GradientStop(ThemeColor.WithAlpha(pal.Accent, 0), 1));
        g.Freeze();
        dc.PushOpacity(f * 0.12);
        dc.DrawRectangle(g, null, new Rect(0, 0, w, h));
        dc.Pop();
    }

    private readonly Dictionary<Brush, Pen> _penCache = new();

    private Pen MakePen(Brush brush, double thickness)
    {
        if (_penCache.TryGetValue(brush, out var pen) && Math.Abs(pen.Thickness - thickness) < 0.01) return pen;
        pen = new Pen(brush, thickness) { StartLineCap = PenLineCap.Round, EndLineCap = PenLineCap.Round };
        pen.Freeze();
        _penCache[brush] = pen;
        return pen;
    }

    private void DrawEdge(DrawingContext dc, double w, double h, double now)
    {
        var colours = EdgeColours(now);
        double angle = now * 60 * S.RgbSpeed * Math.PI / 180;
        double cx = w / 2, cy = h / 2, rx = w / 2, ry = h / 2;
        var start = new Point((cx - Math.Cos(angle) * rx) / w, (cy - Math.Sin(angle) * ry) / h);
        var end = new Point((cx + Math.Cos(angle) * rx) / w, (cy + Math.Sin(angle) * ry) / h);
        var stops = new GradientStopCollection();
        for (int i = 0; i < colours.Length; i++)
            stops.Add(new GradientStop(colours[i], colours.Length == 1 ? 0 : (double)i / (colours.Length - 1)));
        var brush = new LinearGradientBrush(stops, start, end);
        brush.Freeze();
        var rect = new Rect(1, 1, w - 2, h - 2);
        var glow = new Pen(brush, 7);
        glow.Freeze();
        var line = new Pen(brush, 2);
        line.Freeze();
        dc.PushOpacity(0.18);
        dc.DrawRoundedRectangle(null, glow, rect, EdgeRadius, EdgeRadius);
        dc.Pop();
        dc.PushOpacity(0.95);
        dc.DrawRoundedRectangle(null, line, rect, EdgeRadius, EdgeRadius);
        dc.Pop();
    }

    private Color[] EdgeColours(double now)
    {
        var pal = ThemeService.Palette;
        switch (S.RgbStyle)
        {
            case "rainbow":
            {
                double hue = now * 40 * S.RgbSpeed;
                var list = new Color[7];
                for (int i = 0; i < list.Length; i++) list[i] = ThemeColor.FromHsv(hue + i * 60, 0.85, 1);
                return list;
            }
            case "theme":
                return new[] { pal.Accent, pal.Accent2, ThemeColor.Mix(pal.Accent, Colors.White, 0.45), pal.Accent2, pal.Accent };
            default:
            {
                var style = ThemeCatalog.RgbStyles.FirstOrDefault(s => s.Key == S.RgbStyle);
                if (style is null || style.Colors.Length == 0) return new[] { pal.Accent, pal.Accent2 };
                var list = new Color[style.Colors.Length + 1];
                for (int i = 0; i < style.Colors.Length; i++) list[i] = ThemeColor.Parse(style.Colors[i]);
                list[^1] = list[0];
                return list;
            }
        }
    }
}
