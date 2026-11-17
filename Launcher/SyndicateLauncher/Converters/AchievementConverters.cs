using System;
using System.Collections.Generic;
using System.Globalization;
using System.Windows;
using System.Windows.Data;
using System.Windows.Media;

namespace SyndicateLauncher.Converters;

/// <summary>
/// Syndicate Achievement badges: Season 1 style hexagons coloured by rarity tier, with an original vector glyph.
/// All brushes and geometries are frozen and shared, so a page of badges costs nothing to redraw.
/// </summary>
public static class AchievementArt
{
    private static readonly Dictionary<string, (Color Light, Color Dark)> Tiers = new(StringComparer.OrdinalIgnoreCase)
    {
        ["common"] = (Color.FromRgb(0xC4, 0xC6, 0xCC), Color.FromRgb(0x5E, 0x63, 0x6E)),
        ["uncommon"] = (Color.FromRgb(0x8B, 0xE0, 0x3A), Color.FromRgb(0x2E, 0x7D, 0x14)),
        ["rare"] = (Color.FromRgb(0x5C, 0xC8, 0xFF), Color.FromRgb(0x17, 0x5C, 0xC9)),
        ["epic"] = (Color.FromRgb(0xD5, 0x78, 0xFF), Color.FromRgb(0x6B, 0x22, 0xB8)),
        ["legendary"] = (Color.FromRgb(0xFF, 0xB2, 0x45), Color.FromRgb(0xB8, 0x4A, 0x12)),
        ["mythic"] = (Color.FromRgb(0xFF, 0xE8, 0x7A), Color.FromRgb(0xC2, 0x8A, 0x0E)),
    };

    private static readonly Dictionary<string, Brush> FillCache = new();
    private static readonly Dictionary<string, Brush> SolidCache = new();
    private static readonly Dictionary<string, Geometry> GlyphCache = new();

    // 24x24 glyphs (F0 = even-odd, so rings and eyes are cut out).
    private static readonly Dictionary<string, string> Glyphs = new(StringComparer.OrdinalIgnoreCase)
    {
        ["crosshair"] = "F0 M12,3 A9,9 0 1 1 11.99,3 Z M12,5.5 A6.5,6.5 0 1 0 12.01,5.5 Z M12,10 A2,2 0 1 1 11.99,10 Z M11,0 H13 V3.4 H11 Z M11,20.6 H13 V24 H11 Z M0,11 H3.4 V13 H0 Z M20.6,11 H24 V13 H20.6 Z",
        ["crown"] = "M2,18 L3.6,7 L8.6,11.6 L12,3.5 L15.4,11.6 L20.4,7 L22,18 Z M3,19.6 H21 V22 H3 Z",
        ["bus"] = "F0 M4.5,4 H19.5 A2.5,2.5 0 0 1 22,6.5 V17 H2 V6.5 A2.5,2.5 0 0 1 4.5,4 Z M4.2,7 H8.6 V11.4 H4.2 Z M10.4,7 H14.8 V11.4 H10.4 Z M16.6,7 H19.8 V11.4 H16.6 Z M4.5,17.2 A2.3,2.3 0 1 0 9.1,17.2 Z M14.9,17.2 A2.3,2.3 0 1 0 19.5,17.2 Z",
        ["storm"] = "F0 M12,2 A10,10 0 1 1 11.99,2 Z M12,4.6 A7.4,7.4 0 1 0 12.01,4.6 Z M12,7.4 A4.6,4.6 0 1 1 11.99,7.4 Z M12,9.8 A2.2,2.2 0 1 0 12.01,9.8 Z",
        ["scope"] = "F0 M12,2 A10,10 0 1 1 11.99,2 Z M12,4.2 A7.8,7.8 0 1 0 12.01,4.2 Z M11.2,4.2 H12.8 V10.4 H11.2 Z M11.2,13.6 H12.8 V19.8 H11.2 Z M4.2,11.2 H10.4 V12.8 H4.2 Z M13.6,11.2 H19.8 V12.8 H13.6 Z",
        ["skull"] = "F0 M12,2 C6.5,2 3,5.8 3,10.5 C3,13.4 4.4,15.6 6.5,16.8 V20 C6.5,20.6 6.9,21 7.5,21 H16.5 C17.1,21 17.5,20.6 17.5,20 V16.8 C19.6,15.6 21,13.4 21,10.5 C21,5.8 17.5,2 12,2 Z M8.4,8.6 A2.3,2.3 0 1 1 8.39,8.6 Z M15.6,8.6 A2.3,2.3 0 1 1 15.59,8.6 Z M12,13.6 L13.4,16.4 H10.6 Z",
        ["trophy"] = "F0 M6.5,2.5 H17.5 V4.5 H21.5 V8 C21.5,10.6 19.6,12.4 16.9,12.7 C16,14.6 14.5,15.8 13.1,16.1 V18.2 H16.4 V21.5 H7.6 V18.2 H10.9 V16.1 C9.5,15.8 8,14.6 7.1,12.7 C4.4,12.4 2.5,10.6 2.5,8 V4.5 H6.5 Z M4.4,6.3 V8 C4.4,9.4 5.2,10.4 6.6,10.7 C6.5,10 6.5,9.2 6.5,8.4 V6.3 Z M19.6,6.3 H17.5 V8.4 C17.5,9.2 17.5,10 17.4,10.7 C18.8,10.4 19.6,9.4 19.6,8 Z",
        ["rift"] = "M12,0.5 L14.4,9.6 L23.5,12 L14.4,14.4 L12,23.5 L9.6,14.4 L0.5,12 L9.6,9.6 Z M19,1.5 L19.9,4.1 L22.5,5 L19.9,5.9 L19,8.5 L18.1,5.9 L15.5,5 L18.1,4.1 Z",
        ["clock"] = "F0 M12,2 A10,10 0 1 1 11.99,2 Z M12,4.4 A7.6,7.6 0 1 0 12.01,4.4 Z M11,6.8 H13 V11.6 L16.6,13.7 L15.6,15.4 L11,12.7 Z",
        ["flame"] = "M12.5,1 C13.6,5.2 18.5,7.7 18.5,13.6 C18.5,18.4 15.6,22.5 12,22.5 C8.4,22.5 5.5,18.6 5.5,14.4 C5.5,10.8 7.6,9.2 8.2,6.6 C9.8,8.1 10.4,9.8 10.4,11.4 C12,9.3 13,5.3 12.5,1 Z",
        ["lightning"] = "M14.5,0.5 L3.5,13.8 H10.8 L8.6,23.5 L20.5,9 H13.2 Z",
        ["target"] = "F0 M12,2 A10,10 0 1 1 11.99,2 Z M12,5 A7,7 0 1 0 12.01,5 Z M12,8 A4,4 0 1 1 11.99,8 Z M12,10.4 A1.6,1.6 0 1 0 12.01,10.4 Z M12,11.2 A0.8,0.8 0 1 1 11.99,11.2 Z",
        ["llama"] = "F0 M7.4,1.5 L9.2,5.6 L9.9,1.8 L11.7,6.2 C14.2,6.4 15.3,8 15.3,10.2 V13 C17.6,13.2 19.8,14.1 20.6,15.9 V22.5 H17.6 V18.9 H11.4 V22.5 H8.4 V12.4 C6.5,12.3 4.6,11.2 4.6,9.4 C4.6,7.7 6.1,6.9 7.6,6.8 Z M10.2,8.4 A1,1 0 1 1 10.19,8.4 Z",
        ["star"] = "M12,1 L14.9,8.4 L22.8,8.9 L16.7,14 L18.7,21.7 L12,17.4 L5.3,21.7 L7.3,14 L1.2,8.9 L9.1,8.4 Z",
    };

    public static Brush Fill(string? tier)
    {
        tier ??= "common";
        if (FillCache.TryGetValue(tier, out var cached)) return cached;
        var (light, dark) = Tiers.TryGetValue(tier, out var t) ? t : Tiers["common"];
        var brush = new LinearGradientBrush(new GradientStopCollection
        {
            new(light, 0), new(dark, 1)
        }, new Point(0.2, 0), new Point(0.8, 1));
        brush.Freeze();
        return FillCache[tier] = brush;
    }

    public static Brush Solid(string? tier)
    {
        tier ??= "common";
        if (SolidCache.TryGetValue(tier, out var cached)) return cached;
        var brush = new SolidColorBrush(Tiers.TryGetValue(tier, out var t) ? t.Light : Tiers["common"].Light);
        brush.Freeze();
        return SolidCache[tier] = brush;
    }

    public static Geometry Glyph(string? badge)
    {
        badge ??= "star";
        if (GlyphCache.TryGetValue(badge, out var cached)) return cached;
        var geometry = Geometry.Parse(Glyphs.TryGetValue(badge, out var data) ? data : Glyphs["star"]);
        geometry.Freeze();
        return GlyphCache[badge] = geometry;
    }
}

/// <summary>Tier → badge gradient (ConverterParameter "solid" gives the tier's flat colour).</summary>
public sealed class TierBrushConverter : IValueConverter
{
    public object Convert(object? value, Type targetType, object? parameter, CultureInfo culture) =>
        string.Equals(parameter as string, "solid", StringComparison.OrdinalIgnoreCase)
            ? AchievementArt.Solid(value as string)
            : AchievementArt.Fill(value as string);
    public object ConvertBack(object? value, Type targetType, object? parameter, CultureInfo culture) => Binding.DoNothing;
}

/// <summary>Badge key → glyph geometry.</summary>
public sealed class BadgeGlyphConverter : IValueConverter
{
    public object Convert(object? value, Type targetType, object? parameter, CultureInfo culture) => AchievementArt.Glyph(value as string);
    public object ConvertBack(object? value, Type targetType, object? parameter, CultureInfo culture) => Binding.DoNothing;
}

/// <summary>0..1 → a length (ConverterParameter = full length), for bars and progress fills.</summary>
public sealed class FractionToLengthConverter : IValueConverter
{
    public object Convert(object? value, Type targetType, object? parameter, CultureInfo culture)
    {
        double f = value is double d ? d : 0;
        double full = double.TryParse(parameter?.ToString(), NumberStyles.Float, CultureInfo.InvariantCulture, out var p) ? p : 100;
        return Math.Max(0, Math.Min(1, f)) * full;
    }
    public object ConvertBack(object? value, Type targetType, object? parameter, CultureInfo culture) => Binding.DoNothing;
}
