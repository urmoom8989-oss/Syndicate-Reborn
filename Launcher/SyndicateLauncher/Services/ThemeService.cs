using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Globalization;
using System.Linq;
using System.Windows;
using System.Windows.Media;
using SyndicateLauncher.Models;

namespace SyndicateLauncher.Services;

/// <summary>
/// Recolours the whole launcher from a <see cref="ThemePalette"/>. Every themable colour in the XAML is a
/// {DynamicResource} (the palette brushes in Theme.xaml plus the slots in ThemeSlots.xaml); this replaces those
/// resources at application level, so the change shows everywhere at once and survives restarts through
/// AppSettings.Theme. Theme changes cross-fade over a short tween unless the player turned that off.
/// </summary>
public static class ThemeService
{
    private const double TweenSeconds = 0.42;

    private static ThemePalette _shown = ThemeCatalog.Presets[0];
    private static ThemePalette? _tweenFrom, _tweenTo;
    private static readonly Stopwatch TweenClock = new();
    private static bool _tweenHooked;
    private static List<string>? _slotKeys;

    public static ThemeSettings Settings { get; private set; } = new();

    /// <summary>The palette on screen right now (mid-tween this is the blended one).</summary>
    public static ThemePalette Palette => _shown;

    /// <summary>The palette the launcher is heading to (what effects should use).</summary>
    public static ThemePalette Target => _tweenTo ?? _shown;

    /// <summary>Raised when the theme or any effect setting changes.</summary>
    public static event Action? Changed;

    /// <summary>Called once at startup, before the main window exists, so the first frame is already themed.</summary>
    public static void Initialize(ThemeSettings settings)
    {
        Settings = (settings ?? new ThemeSettings()).Normalized();
        Show(ThemeCatalog.Current(Settings));
        Changed?.Invoke();
    }

    /// <summary>Points the service at the settings object the view model saves, without a visible change.</summary>
    public static void Attach(ThemeSettings settings)
    {
        Settings = (settings ?? new ThemeSettings()).Normalized();
        var target = ThemeCatalog.Current(Settings);
        if (target != _shown) Show(target);
        Changed?.Invoke();
    }

    /// <summary>Shows the saved theme (after the player picks one, or to undo an editor preview).</summary>
    public static void Apply(bool animate = true) => ShowAnimated(ThemeCatalog.Current(Settings), animate);

    /// <summary>Shows a palette without saving it (live preview while editing a custom theme).</summary>
    public static void Preview(ThemePalette palette, bool animate = false) => ShowAnimated(palette, animate);

    public static void NotifyEffectsChanged() => Changed?.Invoke();

    public static bool MotionAllowed => !Settings.ReduceMotion;

    private static void ShowAnimated(ThemePalette target, bool animate)
    {
        bool tween = animate && Settings.SmoothTransitions && !Settings.ReduceMotion && Application.Current is not null;
        if (!tween)
        {
            StopTween();
            Show(target);
            Changed?.Invoke();
            return;
        }
        _tweenFrom = _shown;
        _tweenTo = target;
        TweenClock.Restart();
        if (!_tweenHooked)
        {
            CompositionTarget.Rendering += OnTweenFrame;
            _tweenHooked = true;
        }
        Changed?.Invoke();
    }

    private static void OnTweenFrame(object? sender, EventArgs e)
    {
        if (_tweenFrom is null || _tweenTo is null) { StopTween(); return; }
        double t = Math.Min(1, TweenClock.Elapsed.TotalSeconds / TweenSeconds);
        double eased = 1 - Math.Pow(1 - t, 3);
        if (t >= 1)
        {
            var end = _tweenTo;
            StopTween();
            Show(end);
            return;
        }
        Show(ThemePalette.Lerp(_tweenFrom, _tweenTo, eased), final: false);
    }

    private static void StopTween()
    {
        if (_tweenHooked)
        {
            CompositionTarget.Rendering -= OnTweenFrame;
            _tweenHooked = false;
        }
        _tweenFrom = _tweenTo = null;
        TweenClock.Reset();
    }

    // Theme changes used to put ~110 new frozen brushes into Application.Resources on every tween frame. Each
    // replaced key makes WPF walk the whole visual tree to re-resolve {DynamicResource} references, so a theme
    // change cost 100+ tree walks per frame and the launcher stuttered for the whole cross-fade. Now each brush
    // key gets one live (unfrozen) brush the first time, and later frames only change that brush's colour, which
    // re-renders without any lookup. Plain Color resources (gradient stops) can't be changed in place; they are
    // collected and swapped in once, as one merged dictionary (a single tree walk), when the theme settles.
    private static readonly Dictionary<string, SolidColorBrush> LiveSolids = new();
    private static readonly Dictionary<string, LinearGradientBrush> LiveGradients = new();
    private static readonly Dictionary<string, Color> PendingColors = new();
    private static ResourceDictionary? _colorDictionary;

    private static void Show(ThemePalette p, bool final = true)
    {
        _shown = p;
        var app = Application.Current;
        if (app is null) return;
        var res = app.Resources;
        PendingColors.Clear();

        Color accent = p.Accent, accent2 = p.Accent2, text = p.Text, bg = p.Background;

        Set(res, "AccentBrush", Solid(accent));
        Set(res, "AccentSoftBrush", Solid(ThemeColor.Mix(accent, Colors.White, 0.35)));
        Set(res, "TextBrush", Solid(text));
        Set(res, "MutedTextBrush", Solid(ThemeColor.Mix(text, bg, 0.36)));
        Set(res, "DimTextBrush", Solid(ThemeColor.Mix(text, bg, 0.52)));
        Set(res, "HairlineBrush", Solid(ThemeColor.WithAlpha(text, 0x1A)));
        Set(res, "GlassStrokeBrush", Solid(ThemeColor.WithAlpha(text, 0x24)));
        Set(res, "PanelBrush", Solid(Tint(p, 0x18, 0xFF)));
        Set(res, "GlassBrush", Solid(Tint(p, 0x17, 0xB3)));
        Set(res, "GlassHoverBrush", Solid(Tint(p, 0x1F, 0xD9)));
        Set(res, "OnAccentBrush", Solid(OnAccent(p)));
        Set(res, "AccentGradient", Gradient(new Point(0, 0), new Point(1, 1),
            ThemeColor.Mix(accent, Colors.White, 0.18), ThemeColor.Mix(accent, accent2, 0.8)));
        Set(res, "LaunchGradient", Gradient(new Point(0, 0), new Point(0, 1), p.LaunchStart, p.LaunchEnd));
        Set(res, "PanelGradient", Gradient(new Point(0, 0), new Point(1, 1), Tint(p, 0x1B, 0xFF), Tint(p, 0x13, 0xFF)));
        // Extras the Themes page and effects use.
        Set(res, "Accent2Brush", Solid(accent2));
        Set(res, "ThemeBackgroundBrush", Solid(bg));
        Set(res, "ThemeSurfaceBrush", Solid(p.Surface));
        PendingColors["ThemeAccentColor"] = accent;
        PendingColors["ThemeAccent2Color"] = accent2;
        PendingColors["ThemeAccentClearColor"] = ThemeColor.WithAlpha(accent, 0);
        PendingColors["ThemeAccent2ClearColor"] = ThemeColor.WithAlpha(accent2, 0);

        foreach (string key in SlotKeys())
        {
            if (key.StartsWith("Accent_", StringComparison.Ordinal) && TryByte(key, 7, out byte aa))
                Set(res, key, Solid(ThemeColor.WithAlpha(accent, aa)));
            else if (key.StartsWith("TintColor_", StringComparison.Ordinal) && TryArgb(key, 10, out byte a1, out byte v1))
                PendingColors[key] = Tint(p, v1, a1);
            else if (key.StartsWith("Tint_", StringComparison.Ordinal) && TryArgb(key, 5, out byte a2, out byte v2))
                Set(res, key, Solid(Tint(p, v2, a2)));
        }
        if (final) ApplyColors(res);
    }

    private static void ApplyColors(ResourceDictionary res)
    {
        if (_colorDictionary is not null && PendingColors.All(c => _colorDictionary[c.Key] is Color old && old == c.Value))
            return; // nothing changed (an effects-only change), so skip the tree walk
        var next = new ResourceDictionary();
        foreach (var (key, color) in PendingColors) next[key] = color;
        // Last merged dictionary wins over ThemeSlots.xaml's defaults; replacing it is one resource change.
        int index = _colorDictionary is null ? -1 : res.MergedDictionaries.IndexOf(_colorDictionary);
        if (index >= 0) res.MergedDictionaries[index] = next;
        else res.MergedDictionaries.Add(next);
        _colorDictionary = next;
    }

    /// <summary>
    /// Maps one of Syndicate's neutral greys (0x0C = window background, 0x1B = raised surface) onto the
    /// theme's background to surface ramp, keeping its alpha. For the Syndicate theme this is the identity.
    /// </summary>
    public static Color Tint(ThemePalette p, byte grey, byte alpha)
    {
        double t = (grey - 12) / 15.0;
        byte Ch(byte a, byte b) => (byte)Math.Clamp(Math.Round(a + (b - a) * t), 0, 255);
        return Color.FromArgb(alpha, Ch(p.Background.R, p.Surface.R), Ch(p.Background.G, p.Surface.G), Ch(p.Background.B, p.Surface.B));
    }

    /// <summary>Text colour that reads on top of the accent (dark on bright accents, white on deep ones).</summary>
    public static Color OnAccent(ThemePalette p) =>
        ThemeColor.Luminance(p.Accent) > 150 ? ThemeColor.Mix(p.Background, Color.FromRgb(0x11, 0x11, 0x13), 0.5) : Colors.White;

    private static IEnumerable<string> SlotKeys()
    {
        if (_slotKeys is not null) return _slotKeys;
        var keys = new List<string>();
        void Walk(ResourceDictionary d)
        {
            if (d.Source is not null && d.Source.OriginalString.EndsWith("ThemeSlots.xaml", StringComparison.OrdinalIgnoreCase))
                keys.AddRange(d.Keys.OfType<string>());
            foreach (var m in d.MergedDictionaries) Walk(m);
        }
        if (Application.Current is not null) Walk(Application.Current.Resources);
        _slotKeys = keys.Distinct().ToList();
        return _slotKeys;
    }

    private static bool TryByte(string key, int at, out byte value)
    {
        value = 0;
        return key.Length >= at + 2 &&
               byte.TryParse(key.AsSpan(at, 2), NumberStyles.HexNumber, CultureInfo.InvariantCulture, out value);
    }

    private static bool TryArgb(string key, int at, out byte alpha, out byte grey)
    {
        grey = 0;
        return TryByte(key, at, out alpha) & TryByte(key, at + 2, out grey);
    }

    // Brush recipes: Set() turns them into the key's live brush (created once, then recoloured in place).
    private readonly record struct SolidSpec(Color Color);
    private readonly record struct GradientSpec(Point Start, Point End, Color A, Color B);

    private static void Set(ResourceDictionary res, string key, SolidSpec spec)
    {
        if (LiveSolids.TryGetValue(key, out var live) && !live.IsFrozen && ReferenceEquals(res[key], live))
        {
            if (live.Color != spec.Color) live.Color = spec.Color;
            return;
        }
        live = new SolidColorBrush(spec.Color);
        LiveSolids[key] = live;
        res[key] = live;
    }

    private static void Set(ResourceDictionary res, string key, GradientSpec spec)
    {
        if (LiveGradients.TryGetValue(key, out var live) && !live.IsFrozen && ReferenceEquals(res[key], live) && live.GradientStops.Count == 2)
        {
            if (live.GradientStops[0].Color != spec.A) live.GradientStops[0].Color = spec.A;
            if (live.GradientStops[1].Color != spec.B) live.GradientStops[1].Color = spec.B;
            return;
        }
        live = new LinearGradientBrush(spec.A, spec.B, spec.Start, spec.End);
        LiveGradients[key] = live;
        res[key] = live;
    }

    private static SolidSpec Solid(Color c) => new(c);

    private static GradientSpec Gradient(Point start, Point end, Color a, Color b) => new(start, end, a, b);
}
