// Syndicate © 2026 Syndicate. All rights reserved. Proprietary — no copying, distribution, or derivative works. See LICENSE.
using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text.Json;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Media;
using System.Windows.Shapes;

namespace SyndicateClassic;

// Launcher themes and live effects (Settings > Launcher & files > Look and effects).
// A theme is a colour wash over the season art plus an accent for the glow; an effect is a moving layer drawn
// behind every page (rain, snow, stars, embers, fireflies) and/or an RGB light running around the window edge.
// Both live in the backdrop grid (ModernShell), so nothing covers buttons or catches clicks. Effects pause while
// Fortnite runs or the launcher is minimised, so they never cost frames in game.
// Saved in %LOCALAPPDATA%\ProjectTilted\launcher-theme.json: {"Theme":"...","Effect":"...","Rgb":true}.
public partial class MainWindow
{
    private sealed record ThemeChoice(string Theme = "Season", string Effect = "None", bool Rgb = false)
    {
        // Colourway for the RGB edge. Kept outside the positional parameters so older save files still load.
        public string RgbStyle { get; init; } = "rainbow";
    }
    private sealed record ThemeInfo(string Key, string Name, string Top, string Bottom, string? Glow);
    private sealed record RgbStyleInfo(string Key, string Name, string[] Colors, double Speed);
    private static readonly ThemeInfo[] Themes =
    {
        new("Season", "Season colours", "#00000000", "#00000000", null),
        new("Midnight", "Midnight", "#5A0B1A4A", "#8C02040F", "#3B82F6"),
        new("Crimson", "Crimson", "#5A4A0B14", "#8C0F0204", "#EF4444"),
        new("Emerald", "Emerald", "#4D0B4A2A", "#8C020F08", "#22C55E"),
        new("Sunset", "Sunset", "#59F97316", "#8C3B0A40", "#FB923C"),
        new("Vaporwave", "Vaporwave", "#59D946EF", "#8C0E7490", "#F472B6"),
        new("Gold", "Royal gold", "#4DFFD23F", "#99140F02", "#FFD23F"),
        new("Mono", "Monochrome", "#66101010", "#B3000000", "#D4D4D8"),
    };
    private static readonly (string Key, string Name)[] Effects =
        { ("None", "None"), ("Rain", "Rain"), ("Snow", "Snow"), ("Stars", "Starfield"), ("Embers", "Embers"), ("Fireflies", "Fireflies") };
    private static readonly RgbStyleInfo[] RgbStyles =
    {
        new("rainbow", "Rainbow", Array.Empty<string>(), 60),
        new("sunset", "Sunset", new[] { "#FF7A18", "#FF3D81", "#B14EFF", "#5B2A86" }, 34),
        new("midnight", "Midnight", new[] { "#1E3A8A", "#4C1D95", "#0EA5E9", "#312E81" }, 22),
        new("snow", "Snow", new[] { "#FFFFFF", "#D6F0FF", "#8FD8FF", "#E8F7FF" }, 18),
        new("peppermint", "Peppermint", new[] { "#FF4D5E", "#FFFFFF", "#22C55E", "#FF8A95" }, 30),
        new("halloween", "Halloween", new[] { "#FF8A3D", "#8B5CF6", "#22C55E", "#FFC21A" }, 32),
        new("christmas", "Christmas", new[] { "#E11D48", "#FFFFFF", "#22C55E", "#FFD23F" }, 26),
        new("vaporwave", "Vaporwave", new[] { "#FF71CE", "#01CDFE", "#05FFA1", "#B967FF" }, 40),
        new("gold", "Gold", new[] { "#FFE46B", "#FFC21A", "#FF8A3D", "#FFD23F" }, 24),
    };

    private static string ThemeFile => System.IO.Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "ProjectTilted", "launcher-theme.json");
    private static ThemeChoice LoadTheme()
    {
        try
        {
            var c = JsonSerializer.Deserialize<ThemeChoice>(File.ReadAllText(ThemeFile));
            if (c != null)
                return RgbStyles.Any(s => s.Key == c.RgbStyle) ? c : c with { RgbStyle = "rainbow" };
        }
        catch { }
        return new ThemeChoice();
    }
    private static void SaveTheme(ThemeChoice c)
    {
        try { Directory.CreateDirectory(System.IO.Path.GetDirectoryName(ThemeFile)!); File.WriteAllText(ThemeFile, JsonSerializer.Serialize(c)); } catch { }
    }

    private ThemeChoice _theme = new();
    private Border? _themeWash, _themeGlow, _rgbEdge;
    private Canvas? _effectCanvas;
    private readonly List<(FrameworkElement Shape, double X, double Y, double Speed, double Drift, double Phase)> _particles = new();
    private bool _effectsHooked;
    private TimeSpan _lastFrame;
    private double _rgbHue;

    // Called once when the backdrop is built (ModernShell): adds the theme layers on top of the season art.
    private void AddThemeLayers(Grid backdrop)
    {
        _theme = LoadTheme();
        _themeWash = new Border { IsHitTestVisible = false };
        _themeGlow = new Border { IsHitTestVisible = false, Opacity = .32 };
        _effectCanvas = new Canvas { IsHitTestVisible = false, ClipToBounds = true };
        _rgbEdge = new Border { IsHitTestVisible = false, BorderThickness = new Thickness(3), CornerRadius = new CornerRadius(10), Opacity = .9 };
        foreach (var layer in new FrameworkElement[] { _themeWash, _themeGlow, _effectCanvas, _rgbEdge }) backdrop.Children.Add(layer);
        _effectCanvas.SizeChanged += (_, __) => SeedParticles();
        ApplyTheme();
    }

    private void ApplyTheme()
    {
        var info = Themes.FirstOrDefault(t => t.Key == _theme.Theme) ?? Themes[0];
        if (_themeWash != null) _themeWash.Background = new LinearGradientBrush(NightColor(info.Top), NightColor(info.Bottom), 90);
        if (_themeGlow != null) _themeGlow.Background = info.Glow == null ? null
            : new RadialGradientBrush(NightColor(info.Glow), Color.FromArgb(0, 0, 0, 0)) { Center = new Point(.8, .2), GradientOrigin = new Point(.8, .2), RadiusX = .7, RadiusY = .8 };
        if (_rgbEdge != null)
        {
            _rgbEdge.Visibility = _theme.Rgb ? Visibility.Visible : Visibility.Collapsed;
            if (_theme.Rgb) _rgbEdge.BorderBrush = RgbBrush(_rgbHue);
        }
        SeedParticles();
        bool animate = _theme.Rgb || _theme.Effect != "None";
        if (animate && !_effectsHooked) { CompositionTarget.Rendering += OnEffectFrame; _effectsHooked = true; }
        else if (!animate && _effectsHooked) { CompositionTarget.Rendering -= OnEffectFrame; _effectsHooked = false; }
    }

    private void SeedParticles()
    {
        if (_effectCanvas == null) return;
        _effectCanvas.Children.Clear(); _particles.Clear();
        double w = Math.Max(1, _effectCanvas.ActualWidth), h = Math.Max(1, _effectCanvas.ActualHeight);
        var rng = new Random();
        int count = _theme.Effect switch { "Rain" => 160, "Snow" => 110, "Stars" => 140, "Embers" => 70, "Fireflies" => 45, _ => 0 };
        for (int i = 0; i < count; i++)
        {
            FrameworkElement shape;
            double speed, drift = 0, size;
            switch (_theme.Effect)
            {
                case "Rain":
                    size = 10 + rng.NextDouble() * 18; speed = 700 + rng.NextDouble() * 500; drift = -120;
                    shape = new Rectangle { Width = 1.4, Height = size, Fill = new SolidColorBrush(Color.FromArgb((byte)rng.Next(60, 140), 170, 200, 255)), RenderTransform = new RotateTransform(9) };
                    break;
                case "Snow":
                    size = 2 + rng.NextDouble() * 4; speed = 30 + rng.NextDouble() * 60; drift = 20;
                    shape = new Ellipse { Width = size, Height = size, Fill = new SolidColorBrush(Color.FromArgb((byte)rng.Next(120, 230), 255, 255, 255)) };
                    break;
                case "Stars":
                    size = 1 + rng.NextDouble() * 2.4; speed = 4 + rng.NextDouble() * 10; drift = 0;
                    shape = new Ellipse { Width = size, Height = size, Fill = Brushes.White };
                    break;
                case "Embers":
                    size = 2 + rng.NextDouble() * 3; speed = -(40 + rng.NextDouble() * 70); drift = 25;
                    shape = new Ellipse { Width = size, Height = size, Fill = new SolidColorBrush(Color.FromArgb(220, 255, (byte)rng.Next(90, 180), 40)) };
                    break;
                default: // Fireflies
                    size = 3 + rng.NextDouble() * 3; speed = -(6 + rng.NextDouble() * 14); drift = 30;
                    shape = new Ellipse { Width = size, Height = size, Fill = new RadialGradientBrush(Color.FromArgb(255, 220, 255, 120), Color.FromArgb(0, 220, 255, 120)) };
                    break;
            }
            double x = rng.NextDouble() * w, y = rng.NextDouble() * h;
            Canvas.SetLeft(shape, x); Canvas.SetTop(shape, y);
            _effectCanvas.Children.Add(shape);
            _particles.Add((shape, x, y, speed, drift, rng.NextDouble() * Math.PI * 2));
        }
    }

    private void OnEffectFrame(object? sender, EventArgs e)
    {
        if (e is not RenderingEventArgs r || r.RenderingTime == _lastFrame) return;
        double dt = _lastFrame == TimeSpan.Zero ? 0 : Math.Min(.1, (r.RenderingTime - _lastFrame).TotalSeconds);
        _lastFrame = r.RenderingTime;
        // Never cost frames in game or while minimised.
        bool paused = WindowState == WindowState.Minimized || (_game is { HasExited: false });
        if (_effectCanvas != null) _effectCanvas.Visibility = paused ? Visibility.Hidden : Visibility.Visible;
        if (paused || dt <= 0) return;
        if (_theme.Rgb && _rgbEdge != null)
        {
            var style = RgbStyles.FirstOrDefault(s => s.Key == _theme.RgbStyle) ?? RgbStyles[0];
            _rgbHue = (_rgbHue + dt * style.Speed) % 360;
            _rgbEdge.BorderBrush = RgbBrush(_rgbHue);
        }
        if (_effectCanvas == null || _particles.Count == 0) return;
        double w = Math.Max(1, _effectCanvas.ActualWidth), h = Math.Max(1, _effectCanvas.ActualHeight), t = r.RenderingTime.TotalSeconds;
        for (int i = 0; i < _particles.Count; i++)
        {
            var p = _particles[i];
            double x = p.X + p.Drift * dt * (_theme.Effect is "Snow" or "Embers" or "Fireflies" ? Math.Sin(t * .8 + p.Phase) : 1);
            double y = p.Y + p.Speed * dt;
            if (y > h + 30) { y = -30; x = (x * 7919 + 13) % w; }
            else if (y < -30) { y = h + 30; x = (x * 7919 + 13) % w; }
            if (x < -30) x = w + 20; else if (x > w + 30) x = -20;
            if (_theme.Effect is "Stars" or "Fireflies") p.Shape.Opacity = .35 + .65 * Math.Abs(Math.Sin(t * (_theme.Effect == "Stars" ? 1.3 : 2.2) + p.Phase));
            Canvas.SetLeft(p.Shape, x); Canvas.SetTop(p.Shape, y);
            _particles[i] = (p.Shape, x, y, p.Speed, p.Drift, p.Phase);
        }
    }

    private static Color Hue(double h)
    {
        double c = 1, x = 1 - Math.Abs(h / 60 % 2 - 1);
        (double r, double g, double b) = h < 60 ? (c, x, 0d) : h < 120 ? (x, c, 0d) : h < 180 ? (0d, c, x) : h < 240 ? (0d, x, c) : h < 300 ? (x, 0d, c) : (c, 0d, x);
        return Color.FromRgb((byte)(r * 255), (byte)(g * 255), (byte)(b * 255));
    }

    private LinearGradientBrush RgbBrush(double hue)
    {
        var style = RgbStyles.FirstOrDefault(s => s.Key == _theme.RgbStyle) ?? RgbStyles[0];
        var brush = new LinearGradientBrush { StartPoint = new Point(0, 0), EndPoint = new Point(1, 1) };
        if (style.Key == "rainbow")
        {
            for (int i = 0; i <= 5; i++) brush.GradientStops.Add(new GradientStop(Hue((hue + i * 60) % 360), i / 5.0));
        }
        else
        {
            double phase = hue / 360.0;
            for (int i = 0; i <= 5; i++) brush.GradientStops.Add(new GradientStop(PaletteColor(style.Colors, (phase + i / 6.0) % 1.0), i / 5.0));
        }
        return brush;
    }

    private static Color PaletteColor(string[] colors, double t)
    {
        if (colors.Length == 0) return Colors.White;
        double pos = (t % 1.0) * colors.Length;
        int i = (int)pos, j = (i + 1) % colors.Length;
        double f = pos - i;
        var a = NightColor(colors[i]); var b = NightColor(colors[j]);
        return Color.FromRgb((byte)(a.R + (b.R - a.R) * f), (byte)(a.G + (b.G - a.G) * f), (byte)(a.B + (b.B - a.B) * f));
    }

    // Settings > Launcher & files: theme chips, effect chips and the RGB edge switch. Changes show at once.
    private void AddThemeSettings(StackPanel page)
    {
        // Settings are built before the backdrop loads the saved theme; load it here too, or the
        // chips and the RGB switch always show their defaults after a restart.
        _theme = LoadTheme();
        page.Children.Add(NightText("Look and effects", 13.5, Night.Text, bold: true).Also(t => t.Margin = new Thickness(0, 0, 0, 4)));
        page.Children.Add(NightText("Pick a colour theme and a moving effect for the launcher. Effects pause while Fortnite is running, so they never cost frames in game.", 12, Night.Sub).Also(t => { t.TextWrapping = TextWrapping.Wrap; t.Margin = new Thickness(0, 0, 0, 12); }));
        page.Children.Add(NightText("Theme", 12, Night.Muted).Also(t => t.Margin = new Thickness(0, 0, 0, 6)));
        var themes = new WrapPanel { Margin = new Thickness(0, 0, 0, 10) }; page.Children.Add(themes);
        var themeChips = new List<(string Key, Button Chip)>();
        foreach (var info in Themes)
        {
            var chip = NightChip(info.Name); string key = info.Key;
            chip.Click += (_, __) => { _theme = _theme with { Theme = key }; SaveTheme(_theme); ApplyTheme(); foreach (var c in themeChips) SetChip(c.Chip, c.Key == key); };
            themeChips.Add((key, chip)); themes.Children.Add(chip); SetChip(chip, key == _theme.Theme);
        }
        page.Children.Add(NightText("Effect", 12, Night.Muted).Also(t => t.Margin = new Thickness(0, 0, 0, 6)));
        var effects = new WrapPanel { Margin = new Thickness(0, 0, 0, 10) }; page.Children.Add(effects);
        var effectChips = new List<(string Key, Button Chip)>();
        foreach (var (key, name) in Effects)
        {
            var chip = NightChip(name); string k = key;
            chip.Click += (_, __) => { _theme = _theme with { Effect = k }; SaveTheme(_theme); ApplyTheme(); foreach (var c in effectChips) SetChip(c.Chip, c.Key == k); };
            effectChips.Add((key, chip)); effects.Children.Add(chip); SetChip(chip, key == _theme.Effect);
        }
        var rgbOptions = new StackPanel { Visibility = _theme.Rgb ? Visibility.Visible : Visibility.Collapsed, Margin = new Thickness(0, -4, 0, 10) };
        page.Children.Add(SettingsToggle("RGB edge glow", "A coloured light that slowly runs around the edge of the window. Pick a colourway below.", _theme.Rgb,
            on => { _theme = _theme with { Rgb = on }; SaveTheme(_theme); ApplyTheme(); rgbOptions.Visibility = on ? Visibility.Visible : Visibility.Collapsed; }));
        rgbOptions.Children.Add(NightText("RGB colour", 12, Night.Muted).Also(t => t.Margin = new Thickness(0, 0, 0, 6)));
        var rgbRow = new WrapPanel { Margin = new Thickness(0, 0, 0, 8) };
        var rgbChips = new List<(string Key, Button Chip)>();
        foreach (var style in RgbStyles)
        {
            var chip = NightChip(style.Name); string key = style.Key;
            chip.Click += (_, __) =>
            {
                _theme = _theme with { RgbStyle = key };
                SaveTheme(_theme); ApplyTheme();
                foreach (var c in rgbChips) SetChip(c.Chip, c.Key == key);
            };
            rgbChips.Add((key, chip)); rgbRow.Children.Add(chip); SetChip(chip, key == _theme.RgbStyle);
        }
        rgbOptions.Children.Add(rgbRow);
        rgbOptions.Children.Add(NightText("Rainbow cycles the full spectrum; the others cycle a fixed palette.", 11.5, Night.Muted).Also(t => t.Margin = new Thickness(0, 0, 0, 4)));
        page.Children.Add(rgbOptions);
    }
}
