using System;
using System.Collections.Generic;
using System.Globalization;
using System.Linq;
using System.Windows.Media;

namespace SyndicateLauncher.Models;

/// <summary>
/// The look the player picked on the Themes tab. Saved inside settings.json (AppSettings.Theme), so it comes back
/// exactly the same the next time the launcher opens.
/// </summary>
public sealed class ThemeSettings
{
    /// <summary>A preset key from <see cref="ThemeCatalog"/>, or "custom:&lt;id&gt;" for one of the player's own themes.</summary>
    public string Theme { get; set; } = ThemeCatalog.DefaultKey;
    public List<CustomTheme> CustomThemes { get; set; } = new();

    /// <summary>None, Snow, Rain, Stars, Embers, Fireflies, Bubbles or Confetti.</summary>
    public string Particles { get; set; } = "None";
    /// <summary>0.1 to 1: how many particles.</summary>
    public double ParticleAmount { get; set; } = 0.55;

    public bool RgbEdge { get; set; }
    /// <summary>A key from <see cref="ThemeCatalog.RgbStyles"/>.</summary>
    public string RgbStyle { get; set; } = "theme";
    /// <summary>0.25 to 3: how fast the edge light travels.</summary>
    public double RgbSpeed { get; set; } = 1.0;

    /// <summary>Soft accent-coloured light that drifts behind the launcher.</summary>
    public bool AmbientGlow { get; set; } = true;
    /// <summary>0 to 1.</summary>
    public double GlowStrength { get; set; } = 0.55;

    /// <summary>Fade between colours when the theme changes.</summary>
    public bool SmoothTransitions { get; set; } = true;
    /// <summary>Turns off moving effects and page animations.</summary>
    public bool ReduceMotion { get; set; }
    /// <summary>Stops every effect while Fortnite is running so it never costs frames in game.</summary>
    public bool PauseInGame { get; set; } = true;
    /// <summary>OG Fortnite launcher look: top tab bar instead of the sidebar, classic home (MainWindow.xaml).</summary>
    public bool OgLauncher { get; set; }

    /// <summary>
    /// Fortnite Lobby Themes: the slow camera drift over the wallpaper and the theme's own animated weather
    /// (snow, embers, leaves, storm flashes...). The player's own Particles choice wins over the theme's.
    /// </summary>
    public bool LobbyMotion { get; set; } = true;
    /// <summary>0.15 to 0.85: how much the lobby wallpaper is darkened behind the launcher's text.</summary>
    public double WallpaperDim { get; set; } = 0.5;

    public ThemeSettings Normalized()
    {
        CustomThemes ??= new List<CustomTheme>();
        CustomThemes.RemoveAll(t => t is null || string.IsNullOrWhiteSpace(t.Id));
        foreach (var t in CustomThemes) t.Normalize();
        if (string.IsNullOrWhiteSpace(Theme) || ThemeCatalog.Resolve(this, Theme) is null) Theme = ThemeCatalog.DefaultKey;
        if (!ThemeCatalog.ParticleKinds.Any(p => p.Key == Particles)) Particles = "None";
        if (!ThemeCatalog.RgbStyles.Any(s => s.Key == RgbStyle)) RgbStyle = "theme";
        ParticleAmount = Clamp(ParticleAmount, 0.1, 1, 0.55);
        RgbSpeed = Clamp(RgbSpeed, 0.25, 3, 1);
        GlowStrength = Clamp(GlowStrength, 0, 1, 0.55);
        WallpaperDim = Clamp(WallpaperDim, 0.15, 0.85, 0.5);
        return this;
    }

    private static double Clamp(double v, double min, double max, double fallback) =>
        double.IsNaN(v) || double.IsInfinity(v) ? fallback : Math.Clamp(v, min, max);
}

/// <summary>A theme the player made in the theme editor.</summary>
public sealed class CustomTheme
{
    public string Id { get; set; } = Guid.NewGuid().ToString("N")[..10];
    public string Name { get; set; } = "My theme";
    public string Accent { get; set; } = "#F2F3F5";
    public string Accent2 { get; set; } = "#C9CBCF";
    public string Background { get; set; } = "#0C0C0C";
    public string Surface { get; set; } = "#1B1B1B";
    public string Text { get; set; } = "#F4F5F6";

    public void Normalize()
    {
        Name = string.IsNullOrWhiteSpace(Name) ? "My theme" : Name.Trim();
        if (Name.Length > 24) Name = Name[..24];
        Accent = ThemeColor.NormalizeHex(Accent, "#F2F3F5");
        Accent2 = ThemeColor.NormalizeHex(Accent2, Accent);
        Background = ThemeColor.NormalizeHex(Background, "#0C0C0C");
        Surface = ThemeColor.NormalizeHex(Surface, "#1B1B1B");
        Text = ThemeColor.NormalizeHex(Text, "#F4F5F6");
    }

    public ThemePalette ToPalette() => new(
        "custom:" + Id, Name, "Your themes", "Made by you.",
        ThemeColor.Parse(Accent), ThemeColor.Parse(Accent2), ThemeColor.Parse(Background),
        ThemeColor.Parse(Surface), ThemeColor.Parse(Text));

    public CustomTheme Copy() => (CustomTheme)MemberwiseClone();
}

/// <summary>
/// The five colours a theme is built from. Everything else in the launcher is derived from these.
/// Fortnite Lobby Themes also carry a wallpaper (Assets/Lobby/&lt;name&gt;.jpg, shown behind the pages by
/// Controls/LobbyWallpaper.cs), the animated weather that plays over it (a <see cref="ThemeCatalog.ParticleKinds"/> key)
/// and whether storm light flashes now and then.
/// </summary>
public sealed record ThemePalette(
    string Key, string Name, string Group, string Description,
    Color Accent, Color Accent2, Color Background, Color Surface, Color Text,
    Color? Launch = null, Color? Launch2 = null,
    string? Wallpaper = null, string? Ambience = null, bool Flash = false)
{
    public bool HasWallpaper => !string.IsNullOrEmpty(Wallpaper);

    public bool IsCustom => Key.StartsWith("custom:", StringComparison.Ordinal);

    public static ThemePalette Lerp(ThemePalette a, ThemePalette b, double t) => b with
    {
        Accent = ThemeColor.Mix(a.Accent, b.Accent, t),
        Accent2 = ThemeColor.Mix(a.Accent2, b.Accent2, t),
        Background = ThemeColor.Mix(a.Background, b.Background, t),
        Surface = ThemeColor.Mix(a.Surface, b.Surface, t),
        Text = ThemeColor.Mix(a.Text, b.Text, t),
        Launch = ThemeColor.Mix(a.LaunchStart, b.LaunchStart, t),
        Launch2 = ThemeColor.Mix(a.LaunchEnd, b.LaunchEnd, t)
    };

    public Color LaunchStart => Launch ?? ThemeColor.Mix(Accent, Colors.White, 0.12);
    public Color LaunchEnd => Launch2 ?? ThemeColor.Mix(Accent, Accent2, 0.65);
}

public sealed record ParticleKind(string Key, string Name);
public sealed record RgbStyle(string Key, string Name, string[] Colors);

public static class ThemeCatalog
{
    public const string DefaultKey = "syndicate";

    private static ThemePalette P(string key, string name, string group, string description,
        string accent, string accent2, string background, string surface, string text = "#F4F5F6",
        string? launch = null, string? launch2 = null) =>
        new(key, name, group, description,
            ThemeColor.Parse(accent), ThemeColor.Parse(accent2), ThemeColor.Parse(background),
            ThemeColor.Parse(surface), ThemeColor.Parse(text),
            launch is null ? null : ThemeColor.Parse(launch), launch2 is null ? null : ThemeColor.Parse(launch2));

    public static readonly IReadOnlyList<ThemePalette> Presets = new[]
    {
        // Signature
        P("syndicate", "Syndicate", "Signature", "The SYNDICATE look: graphite and clean white.",
          "#F2F3F5", "#C9CBCF", "#0C0C0C", "#1B1B1B"),
        P("classic-ice", "Syndicate Ice", "Signature", "The original Syndicate launcher look: deep navy, ice cyan and a gold play button.",
          "#3BD9FF", "#1E8FD0", "#06121D", "#0E2333", "#EAF6FF", "#F7D154", "#D9A514"),
        P("classic-gold", "Syndicate Gold", "Signature", "The classic gold launcher look: charcoal olive and bright yellow.",
          "#FFD23F", "#F5A900", "#11120D", "#22241A", "#FFFFFF", "#FFE066", "#F5B400"),

        // Colour themes
        P("midnight", "Midnight", "Colour", "Cold blue light over a midnight sky.", "#4C8DFF", "#7C5CFF", "#060A16", "#111A33"),
        P("arctic", "Arctic", "Colour", "Frozen frontier blues.", "#7DD3FC", "#38BDF8", "#07121A", "#12232F"),
        P("ocean", "Deep Ocean", "Colour", "Teal water and deep blue.", "#2DD4BF", "#0EA5E9", "#04121A", "#0C2230"),
        P("emerald", "Emerald", "Colour", "Rich jungle green.", "#34D399", "#10B981", "#05120C", "#0F2219"),
        P("toxic", "Toxic", "Colour", "Slurp-green glow, straight from the barrel.", "#A3E635", "#4ADE80", "#0A1006", "#172010"),
        P("royalgold", "Royal Gold", "Colour", "Black and legendary gold.", "#FFC83D", "#F59E0B", "#0F0C06", "#211B0E"),
        P("sunset", "Sunset", "Colour", "Orange to pink, like the last circle at dusk.", "#FF8A3D", "#F43F7A", "#140A0C", "#26141A"),
        P("inferno", "Inferno", "Colour", "Lava orange and ember red.", "#FF6B2C", "#FF2E4D", "#130806", "#27120C"),
        P("crimson", "Crimson", "Colour", "Blood red on black.", "#F43F5E", "#BE123C", "#120608", "#241014"),
        P("rose", "Rose Quartz", "Colour", "Soft pinks.", "#FDA4AF", "#F472B6", "#140B0F", "#26161D"),
        P("vaporwave", "Vaporwave", "Colour", "Neon pink and cyan.", "#F472B6", "#22D3EE", "#0F0A1A", "#1E1433"),
        P("amethyst", "Amethyst", "Colour", "Deep purple crystal.", "#A78BFA", "#7C3AED", "#0D0A17", "#1B1530"),
        P("storm", "The Storm", "Colour", "Storm purple with an electric edge.", "#C084FC", "#6366F1", "#0C0716", "#1C1130"),
        P("slate", "Slate", "Colour", "Calm blue-grey, easy on the eyes.", "#CBD5E1", "#94A3B8", "#0B0E13", "#181D26"),
        P("oled", "OLED Black", "Colour", "True black with white text for OLED screens.", "#FFFFFF", "#A1A1AA", "#000000", "#0E0E0E", "#FFFFFF"),

        // Fortnite Lobby Themes: the lobby art behind the launcher with its own animated weather. Colours are picked
        // from each picture. Every one has Fortnite's yellow Play button.
        L("lobby-classic", "Classic Lobby", "The original blue lobby grid, from the very first season.",
          "classic", "Motes", "#4FC3FF", "#2E7BFF", "#06142E", "#0E2552"),
        L("lobby-season2", "Season 2: Winter", "Frozen lake, candy canes and the four lobby pads.",
          "season2", "Snow", "#9CD8FF", "#FF5A6E", "#0A1020", "#18223A"),
        L("lobby-season4", "Season 4: Meteor Crater", "The crater where the comet hit, under drone lights.",
          "season4", "Stars", "#5FB4FF", "#9AE6FF", "#050A16", "#0F1B30"),
        L("lobby-season5", "Season 5: Worlds Collide", "Station 99 in the desert, with the rift crackling overhead.",
          "season5", "Motes", "#FF9E5E", "#5FD0FF", "#160C0A", "#2C1A14", flash: true),
        L("lobby-season6", "Season 6: Darkness Rises", "The floating island and its cube over Loot Lake at night.",
          "season6", "Fireflies", "#B76BFF", "#4FA8FF", "#08081A", "#16173A"),
        L("lobby-season8", "Season 8: Pirate Cove", "Sunset over the volcano, the shipwreck and the cannons.",
          "season8", "Embers", "#FF9A3C", "#FF4E2E", "#140907", "#2A160F"),
        L("lobby-season9", "Season 9: Neo Tilted", "Neo Tilted and the Mega Mall glowing at night.",
          "season9", "Stars", "#C66BFF", "#5BE3FF", "#0B0716", "#1B1430"),
        L("lobby-fortnitemares", "Fortnitemares", "The Halloween graveyard: candles, pumpkins and a full moon.",
          "fortnitemares", "Embers", "#FF8A2B", "#A97BFF", "#0D0912", "#1F1626", flash: true),
        L("lobby-haunted-gate", "Haunted Gate", "Fortnitemares night at the cemetery gates.",
          "haunted-gate", "Fireflies", "#FFA040", "#8E9BFF", "#0A0B16", "#181A2C"),
        L("lobby-winterfest", "Winterfest", "Llama-topped trees on a snowy night.",
          "winterfest", "Snow", "#9FD8FF", "#FFD84A", "#050B18", "#101C33"),
        L("lobby-mega", "MEGA: Bamboo Crossing", "Chapter 4 Season 2: the bamboo forest, a dojo house and a frozen llama.",
          "mega", "Leaves", "#5FE3A1", "#8FD3FF", "#06120E", "#10251D"),
        L("lobby-olympus", "Mount Olympus", "Chapter 5 Season 2, Myths & Mortals: the temples under the winged statue.",
          "olympus", "Motes", "#FFD27A", "#E8A54B", "#120D07", "#2A2013"),
        L("lobby-treasure", "Pirate Treasure", "Chapter 7: a pirate ship and a chained treasure chest on the sand.",
          "treasure", "Motes", "#FFC94A", "#9B6BFF", "#071428", "#10264A"),
        L("lobby-storm-forge", "Storm Forge", "Red autumn trees under a storm machine tearing the sky open.",
          "storm-forge", "Leaves", "#C77DFF", "#FF5A4E", "#0E0717", "#20112E", flash: true),
        L("lobby-neon-district", "Neon District", "Rain on a neon-lit street after dark.",
          "neon-district", "Rain", "#FF4FB8", "#38E1FF", "#0A0814", "#1A1530"),
        L("lobby-splashdown", "Splashdown", "Sunlit pool water rippling across the whole launcher.",
          "splashdown", "Bubbles", "#3FE0FF", "#1E90FF", "#031726", "#0A2A40"),
    };

    /// <summary>A Fortnite Lobby Theme: <paramref name="wallpaper"/> names Assets/Lobby/&lt;name&gt;.jpg (and its -thumb.jpg).</summary>
    private static ThemePalette L(string key, string name, string description, string wallpaper, string ambience,
        string accent, string accent2, string background, string surface, bool flash = false) =>
        P(key, name, "Lobby", description, accent, accent2, background, surface, "#F4F5F6", "#FFE15A", "#F5B800") with
        {
            Wallpaper = wallpaper, Ambience = ambience, Flash = flash
        };

    public static readonly IReadOnlyList<ParticleKind> ParticleKinds = new[]
    {
        new ParticleKind("None", "None"),
        new ParticleKind("Snow", "Snow"),
        new ParticleKind("Rain", "Rain"),
        new ParticleKind("Stars", "Starfield"),
        new ParticleKind("Embers", "Embers"),
        new ParticleKind("Fireflies", "Fireflies"),
        new ParticleKind("Bubbles", "Bubbles"),
        new ParticleKind("Confetti", "Confetti"),
        new ParticleKind("Leaves", "Falling leaves"),
        new ParticleKind("Motes", "Glowing dust"),
    };

    public static readonly IReadOnlyList<RgbStyle> RgbStyles = new[]
    {
        new RgbStyle("theme", "Theme colours", Array.Empty<string>()),
        new RgbStyle("rainbow", "Rainbow", Array.Empty<string>()),
        new RgbStyle("sunset", "Sunset", new[] { "#FF7A18", "#FF3D81", "#B14EFF", "#5B2A86" }),
        new RgbStyle("midnight", "Midnight", new[] { "#1E3A8A", "#4C1D95", "#0EA5E9", "#312E81" }),
        new RgbStyle("snow", "Snow", new[] { "#FFFFFF", "#D6F0FF", "#8FD8FF", "#E8F7FF" }),
        new RgbStyle("peppermint", "Peppermint", new[] { "#FF4D5E", "#FFFFFF", "#22C55E", "#FF8A95" }),
        new RgbStyle("halloween", "Halloween", new[] { "#FF8A3D", "#8B5CF6", "#22C55E", "#FFC21A" }),
        new RgbStyle("christmas", "Christmas", new[] { "#E11D48", "#FFFFFF", "#22C55E", "#FFD23F" }),
        new RgbStyle("vaporwave", "Vaporwave", new[] { "#FF71CE", "#01CDFE", "#05FFA1", "#B967FF" }),
        new RgbStyle("gold", "Gold", new[] { "#FFE46B", "#FFC21A", "#FF8A3D", "#FFD23F" }),
    };

    /// <summary>Quick colour swatches for the theme editor.</summary>
    public static readonly IReadOnlyList<string> AccentSwatches = new[]
    {
        "#F2F3F5", "#3BD9FF", "#4C8DFF", "#7C5CFF", "#A78BFA", "#C084FC", "#F472B6", "#F43F5E",
        "#FF6B2C", "#FF8A3D", "#FFC83D", "#FFD23F", "#A3E635", "#34D399", "#2DD4BF", "#7DD3FC"
    };

    public static readonly IReadOnlyList<string> BackgroundSwatches = new[]
    {
        "#000000", "#0C0C0C", "#06121D", "#11120D", "#060A16", "#05120C", "#0F0C06", "#140A0C",
        "#120608", "#0F0A1A", "#0D0A17", "#0B0E13"
    };

    public static ThemePalette? Resolve(ThemeSettings settings, string key)
    {
        if (key.StartsWith("custom:", StringComparison.Ordinal))
        {
            string id = key["custom:".Length..];
            return settings.CustomThemes?.FirstOrDefault(t => t.Id == id)?.ToPalette();
        }
        return Presets.FirstOrDefault(p => p.Key == key);
    }

    public static ThemePalette Current(ThemeSettings settings) =>
        Resolve(settings, settings.Theme) ?? Presets[0];
}

public static class ThemeColor
{
    public static Color Parse(string? hex, Color? fallback = null)
    {
        if (TryParse(hex, out var c)) return c;
        return fallback ?? Colors.White;
    }

    public static bool TryParse(string? hex, out Color color)
    {
        color = Colors.Transparent;
        if (string.IsNullOrWhiteSpace(hex)) return false;
        string h = hex.Trim().TrimStart('#');
        if (h.Length == 3) h = string.Concat(h.Select(ch => new string(ch, 2)));
        if (h.Length != 6 && h.Length != 8) return false;
        if (!uint.TryParse(h, NumberStyles.HexNumber, CultureInfo.InvariantCulture, out uint v)) return false;
        if (h.Length == 6) v |= 0xFF000000;
        color = Color.FromArgb((byte)(v >> 24), (byte)(v >> 16), (byte)(v >> 8), (byte)v);
        return true;
    }

    public static string NormalizeHex(string? hex, string fallback) =>
        TryParse(hex, out var c) ? ToHex(c) : fallback;

    public static string ToHex(Color c) => $"#{c.R:X2}{c.G:X2}{c.B:X2}";

    public static Color WithAlpha(Color c, byte a) => Color.FromArgb(a, c.R, c.G, c.B);

    public static Color Mix(Color a, Color b, double t)
    {
        t = Math.Clamp(t, 0, 1);
        return Color.FromArgb(
            (byte)Math.Round(a.A + (b.A - a.A) * t),
            (byte)Math.Round(a.R + (b.R - a.R) * t),
            (byte)Math.Round(a.G + (b.G - a.G) * t),
            (byte)Math.Round(a.B + (b.B - a.B) * t));
    }

    public static double Luminance(Color c) => 0.2126 * c.R + 0.7152 * c.G + 0.0722 * c.B;

    public static Color FromHsv(double h, double s, double v)
    {
        h = ((h % 360) + 360) % 360;
        double c = v * s, x = c * (1 - Math.Abs(h / 60 % 2 - 1)), m = v - c;
        (double r, double g, double b) = h switch
        {
            < 60 => (c, x, 0d),
            < 120 => (x, c, 0d),
            < 180 => (0d, c, x),
            < 240 => (0d, x, c),
            < 300 => (x, 0d, c),
            _ => (c, 0d, x)
        };
        return Color.FromRgb((byte)Math.Round((r + m) * 255), (byte)Math.Round((g + m) * 255), (byte)Math.Round((b + m) * 255));
    }

    public static (double H, double S, double V) ToHsv(Color c)
    {
        double r = c.R / 255.0, g = c.G / 255.0, b = c.B / 255.0;
        double max = Math.Max(r, Math.Max(g, b)), min = Math.Min(r, Math.Min(g, b)), d = max - min;
        double h = d == 0 ? 0 : max == r ? 60 * (((g - b) / d) % 6) : max == g ? 60 * ((b - r) / d + 2) : 60 * ((r - g) / d + 4);
        return ((h + 360) % 360, max == 0 ? 0 : d / max, max);
    }
}
