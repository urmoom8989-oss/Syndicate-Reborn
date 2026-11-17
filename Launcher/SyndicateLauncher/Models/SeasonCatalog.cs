using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Windows;
using System.Windows.Media;
using System.Windows.Media.Imaging;

namespace SyndicateLauncher.Models;

/// <summary>A chapter entry used by the chapter cards and the season screen.</summary>
public sealed class ChapterInfo
{
    public required int Number { get; init; }
    public required string Title { get; init; }
    public required string Tagline { get; init; }
    public required bool IsReleased { get; init; }
}

/// <summary>A season entry. Availability is intentionally data-driven.</summary>
public sealed class SeasonInfo
{
    public required string Key { get; init; }
    public required int ChapterNumber { get; init; }
    public required string Code { get; init; }
    public int SeasonNumber => Code == "X" ? 10 : int.TryParse(Code, out var number) ? number : 0;
    public required string Name { get; init; }
    public required string Tagline { get; init; }
    public required string Description { get; init; }
    public required bool IsReleased { get; init; }
    public required Brush CoverBrush { get; init; }

    /// <summary>A vivid colour sampled from the season's keyart, used across the UI.</summary>
    public Color AccentColor { get; init; } = Color.FromRgb(0xF2, 0xF3, 0xF5);
    public SolidColorBrush AccentBrush { get; init; } = new(Color.FromRgb(0xF2, 0xF3, 0xF5));
    public SolidColorBrush AccentSoftBrush { get; init; } = new(Color.FromRgb(0xF2, 0xF3, 0xF5));
    public SolidColorBrush OnAccentBrush { get; init; } = new(Colors.White);
    public LinearGradientBrush AccentGradient { get; init; } = new();
    public LinearGradientBrush TitleGradient { get; init; } = new();
    public LinearGradientBrush HeroGradient { get; init; } = new();
    public LinearGradientBrush DetailGradient { get; init; } = new();
    public LinearGradientBrush PageGradient { get; init; } = new();

    /// <summary>The Fortnite build this season runs on (Syndicate's build slot).</summary>
    public string BuildVersion { get; init; } = "";

    public IReadOnlyList<string> AlternateBuildVersions { get; init; } = Array.Empty<string>();
    public string BuildVersionsLabel => string.Join(" / ", new[] { BuildVersion }.Concat(AlternateBuildVersions));

    /// <summary>True when this season can run the detected build or an explicitly supported alternate.</summary>
    public bool AcceptsBuild(string version) =>
        string.Equals(version, BuildVersion, StringComparison.OrdinalIgnoreCase) ||
        AlternateBuildVersions.Contains(version, StringComparer.OrdinalIgnoreCase);

    /// <summary>"PRE-SEASON", "SEASON 3", "SEASON X": the label used on browser cards.</summary>
    public string Label { get; init; } = "";
    public string Dates { get; init; } = "";
    public IReadOnlyList<string> Features { get; init; } = Array.Empty<string>();

    public ImageSource? WheelImage { get; init; }
    public ImageSource? BackgroundImage { get; init; }
    public ImageSource? WheelPageBackgroundImage { get; init; }
    public ImageSource? CardArt { get; init; }
    public ImageSource? DetailArt { get; init; }

    /// <summary>Key art behind the season page: Season X's own backdrop, otherwise the season's key art.</summary>
    public ImageSource? SeasonPageArt => BackgroundImage ?? DetailArt;
    public Point WheelImageFocus { get; init; } = new(0.5, 0.5);

    public string ChapterText => $"CHAPTER {ChapterNumber}";
    public string ReleaseLabel => IsReleased ? "AVAILABLE" : "NOT RELEASED";
    public string AvailabilityNote => IsReleased
        ? $"{Name} is available to play."
        : $"{Name} is not released yet.";
}

public static class SeasonCatalog
{
    private static readonly Brush LockedCover = Gradient("#263B4B", "#08131D");

    private static readonly IReadOnlyList<ChapterInfo> AllChapters = new[]
    {
        new ChapterInfo
        {
            Number = 1,
            Title = "Chapter 1",
            Tagline = "The Original Chapter",
            IsReleased = true
        },
        new ChapterInfo
        {
            Number = 2,
            Title = "Chapter 2",
            Tagline = "Return to the Island",
            IsReleased = false
        }
    };

    private static readonly IReadOnlyList<SeasonInfo> ChapterOneSeasons = new[]
    {
        Season(1, "0", "PRE-SEASON", "Season 0", "Before the Storm", "The island before the storm. A rare look at the first version of the map.", "Sep 26 - Oct 25, 2017", new[] { "Original map", "No Battle Pass" }, true),
        Season(1, "1", "SEASON 1", "Season 1", "The First Drop", "The island that started it all. Land, loot and the original Battle Royale.", "Oct 25 - Dec 13, 2017", new[] { "First tiers", "Original map" }, true),
        Season(1, "2", "SEASON 2", "Season 2", "Knights of the Island", "Medieval keeps, new weapons and the first Battle Pass.", "Dec 14, 2017 - Feb 21, 2018", new[] { "First Battle Pass", "Knights & castles" }, true),
        Season(1, "3", "SEASON 3", "Season 3", "A Sky on Fire", "A comet crosses the sky and the island begins to change.", "Feb 22 - Apr 30, 2018", new[] { "100-tier pass", "Comet" }, true),
        Season(1, "4", "SEASON 4", "Season 4", "Heroes and Villains", "Superheroes and supervillains collide as the comet's secret is revealed.", "May 1 - Jul 12, 2018", new[] { "Meteor", "Hop Rocks" }, true),
        Season(1, "5", "SEASON 5", "Season 5", "Worlds Collide", "Rifts open across the island, bringing new places and strange visitors.", "Jul 12 - Sep 27, 2018", new[] { "Rifts", "ATKs" }, true),
        Season(1, "6", "SEASON 6", "Season 6", "Darkness Rises", "Darkness falls and the Cube leaves its mark on the island.", "Sep 27 - Dec 6, 2018", new[] { "Pets", "Floating island" }, true),
        Season(1, "7", "SEASON 7", "Season 7", "A Frozen Frontier", "A frozen front moves in, turning the island into a winter battlefield.", "Dec 6, 2018 - Feb 28, 2019", new[] { "Planes", "Snow" }, true),
        Season(1, "8", "SEASON 8", "Season 8", "Treasure and Ash", "Volcanoes erupt, pirates arrive and the map breaks open.", "Feb 28 - May 9, 2019", new[] { "Pirates", "Volcano" }, true),
        Season(1, "9", "SEASON 9", "Season 9", "The Future Is Now", "The future lands in Neo Tilted, and the island's clock starts ticking.", "May 9 - Aug 1, 2019", new[] { "Neo Tilted", "Slipstreams" }, true),
        Season(1, "X", "SEASON X", "Season X", "The End of the Beginning", "The original island's final season. Rift zones rewrite familiar places as time collapses.", "Aug 1 - Oct 13, 2019", new[] { "B.R.U.T.E mechs", "Rift Zones" }, true)
    };

    private static readonly IReadOnlyList<SeasonInfo> ChapterTwoSeasons = new[]
    {
        Season(2, "1", "SEASON 1", "Season 1", "A New Island", "A brand-new island, a new cast and a fresh start for Chapter 2.", "Oct 15, 2019 - Feb 20, 2020", new[] { "New island", "Swimming & fishing" }, true),
        Season(2, "2", "SEASON 2", "Season 2", "Top Secret", "New agents, new locations and a mystery beneath the island.", "Feb 20 - Jun 17, 2020", new[] { "Bosses & henchmen", "The Agency" }, true),
        Season(2, "3", "SEASON 3", "Season 3", "Splash Down", "The island floods and the fight moves onto the water.", "Jun 17 - Aug 27, 2020", new[] { "Flooded island", "Sharks" }, false),
        Season(2, "4", "SEASON 4", "Season 4", "Nexus War", "Four factions clash as the Nexus comes apart.", "Aug 27 - Dec 2, 2020", new[] { "Marvel", "Galactus" }, false),
        Season(2, "5", "SEASON 5", "Season 5", "Zero Point", "The Zero Point awakens and reality begins to bend.", "Dec 2, 2020 - Mar 16, 2021", new[] { "Hunters", "Gold bars" }, false),
        Season(2, "6", "SEASON 6", "Season 6", "Primal", "The island turns wild as primal creatures take over.", "Mar 16 - Jun 8, 2021", new[] { "Crafting", "Wildlife" }, false),
        Season(2, "7", "SEASON 7", "Season 7", "Invasion", "An alien fleet arrives and the island becomes a war zone.", "Jun 8 - Sep 13, 2021", new[] { "UFOs", "Aliens" }, false),
        Season(2, "8", "SEASON 8", "Season 8", "The End", "The final season of Chapter 2. The island faces its last stand.", "Sep 13 - Dec 4, 2021", new[] { "The Sideways", "Cube Queen" }, false)
    };

    public static IReadOnlyList<ChapterInfo> Chapters => AllChapters;

    /// <summary>Syndicate's build slot for each Chapter 1 season.</summary>
        private static string BuildVersionFor(int chapter, string code) => chapter switch
    {
        1 => code switch
        {
            "0" => "1.7.2",
            "1" => "1.8.2",
            "2" => "1.11",
            "3" => "3.5.1",
            "4" => "4.5",
            "5" => "5.10",
            "6" => "6.20",
            "7" => "7.40",
            "8" => "8.50",
            "9" => "9.40",
            "X" => "10.40",
            _ => ""
        },
        2 => code switch
        {
            "1" => "11.00",
            "2" => "12.50",
            _ => ""
        },
        _ => ""
    };

    public static IReadOnlyList<SeasonInfo> GetSeasons(int chapterNumber) =>
        chapterNumber == 1 ? ChapterOneSeasons :
        chapterNumber == 2 ? ChapterTwoSeasons :
        System.Array.Empty<SeasonInfo>();

    private static SeasonInfo Season(int chapter, string code, string label, string name, string tagline,
        string description, string dates, string[] features, bool released)
    {
        var accent = AccentFor(chapter, code);
        var card = CardImageFor(chapter, code);
        return new SeasonInfo
        {
            Key = $"{chapter}-{code}",
            ChapterNumber = chapter,
            Code = code,
            Label = label,
            Name = name,
            Tagline = tagline,
            Description = description,
            Dates = dates,
            Features = features,
#if DEV_BUILD
            IsReleased = released,
#else
            // Public releases: Chapter 2 seasons are dev-only (not finished / crash), so they can't be played -
            // IsReleased=false disables Play and every launch path for them.
            IsReleased = chapter >= 2 ? false : released,
#endif
            BuildVersion = BuildVersionFor(chapter, code),
#if DEV_BUILD
            AlternateBuildVersions = chapter == 1 && code == "2" ? new[] { "2.5" } : Array.Empty<string>(),
#else
            // Public releases: the 2.5 build of Chapter 1 Season 2 is dev-only (it still crashes), so it is not
            // accepted - only the main 1.11 build of Season 2 can launch publicly.
            AlternateBuildVersions = Array.Empty<string>(),
#endif
            CoverBrush = CoverFor(chapter, code),
            AccentColor = accent,
            AccentBrush = Brush(accent),
            AccentSoftBrush = Brush(Lighten(accent, 0.45)),
            OnAccentBrush = Brush(Luminance(accent) > 165 ? Color.FromRgb(0x14, 0x14, 0x14) : Colors.White),
            AccentGradient = Vertical(Lighten(accent, 0.18), Darken(accent, 0.18)),
            TitleGradient = Vertical(Colors.White, Lighten(accent, 0.38)),
            HeroGradient = Diagonal(Alpha(accent, 0x66), Alpha(accent, 0x00)),
            DetailGradient = Detail(accent),
            PageGradient = Page(accent),
            WheelImage = WheelImageFor(chapter, code),
            BackgroundImage = BackgroundFor(chapter, code),
            WheelPageBackgroundImage = WheelPageBackgroundFor(chapter, code),
            CardArt = card,
            DetailArt = DetailImageFor(chapter, code) ?? card,
            WheelImageFocus = WheelImageFocusFor(chapter, code)
        };
    }

    private static ImageSource? CardImageFor(int chapter, string code)
    {
        if (chapter == 2)
        {
            if (code == "1" || code == "2") return LoadImage($"chapter2-season{code}.png");
            if (int.TryParse(code, out var number) && number is >= 3 and <= 8)
                return LoadImage($"chapter2-season{number}.jpg");
            return null;
        }

        // Chapter 1 uses Syndicate's key art. Seasons without key art fall back to the
        // season-number artwork the wheel already uses.
        return code switch
        {
            "0" => LoadImage("chapter1-season0.jpg"),
            "1" => LoadImage("chapter1-season1.jpg"),
            "2" => LoadImage("chapter1-season2.jpg"),
            "3" => LoadImage("chapter1-season3.jpg"),
            "4" => LoadImage("chapter1-season4.jpg"),
            "5" => LoadImage("chapter1-season5.jpg"),
            "6" => LoadImage("chapter1-season6.jpg"),
            "7" => LoadImage("chapter1-season7.jpg"),
            "8" => LoadImage("chapter1-season8.jpg"),
            "9" => LoadImage("chapter1-season9.jpg"),
            "X" => LoadImage("chapter1-season10.jpg"),
            _ => null
        };
    }

    private static ImageSource? DetailImageFor(int chapter, string code) =>
        CardImageFor(chapter, code);

    private static ImageSource? WheelImageFor(int chapter, string code)
    {
        if (chapter != 1 || code is not ("0" or "1" or "2" or "3" or "4" or "5" or "6" or "7" or "8" or "9" or "X"))
            return null;

        string assetCode = code == "X" ? "x" : code;
        return LoadImage($"season-{assetCode}.png");
    }

    private static ImageSource? BackgroundFor(int chapter, string code) =>
        chapter == 1 && code == "X" ? LoadImage("season-x-background-image.jpg") : null;

    private static ImageSource? WheelPageBackgroundFor(int chapter, string code)
    {
        if (chapter != 1) return null;

        string? fileName = code switch
        {
            "0" => "Season 0 Wheel page background.png",
            // Seasons 1-3 have no dedicated wheel-page background, so they use their key art.
            "1" => "chapter1-season1.jpg",
            "2" => "chapter1-season2.jpg",
            "3" => "chapter1-season3.jpg",
            "4" => "Season 4 wheel page background.jpg",
            "5" => "Season 5 wheel page background.jpg",
            "6" => "Season 6 wheel page background.jpg",
            "7" => "Season 7 wheel page background.jpg",
            "8" => "Season 8 wheel page background.jpg",
            "9" => "Season 9 Wheel page background.png",
            "X" => "Season x wheel page background.png",
            _ => null
        };

        // These full-page backgrounds are 1920x1080 and larger (some are 2+ MB PNGs). Decoding them at full
        // resolution made the bitmap that WPF composites on every season change huge, which stuttered the wheel.
        // 1280 wide is plenty for a darkened, overlaid background and cuts the composite cost a lot.
        // Only downscale: Season 2 and 3's key art is narrower than 1280, and decoding it wider would just upscale it.
        if (fileName is null) return null;
        return code is "2" or "3" ? LoadImage(fileName) : LoadImage(fileName, 1280);
    }

    private static ImageSource LoadImage(string fileName, int decodePixelWidth = 0)
    {
        string assemblyName = typeof(SeasonCatalog).Assembly.GetName().Name ?? "SyndicateLauncher";
        var image = new BitmapImage();
        image.BeginInit();
        image.UriSource = new Uri(
            $"pack://application:,,,/{assemblyName};component/Assets/{fileName}",
            UriKind.Absolute);
        image.CacheOption = BitmapCacheOption.OnLoad;
        image.CreateOptions = BitmapCreateOptions.IgnoreImageCache;
        // Decode at display size, not the source's full resolution, when a width is given. Only downscales
        // (every wheel-page background is at least ~1200 wide), so it saves memory and compositing with no visible loss.
        if (decodePixelWidth > 0) image.DecodePixelWidth = decodePixelWidth;
        image.EndInit();
        image.Freeze();
        return image;
    }

    // Normalized centers of the large white season numbers in each supplied logo.
    private static Point WheelImageFocusFor(int chapter, string code) =>
        chapter == 1
            ? code switch
            {
                "0" => new Point(0.502, 0.489),
                "1" => new Point(0.492, 0.492),
                "2" => new Point(0.500, 0.477),
                "3" => new Point(0.511, 0.481),
                "4" => new Point(0.502, 0.481),
                "5" => new Point(0.472, 0.470),
                "6" => new Point(0.509, 0.480),
                "7" => new Point(0.502, 0.489),
                "8" => new Point(0.516, 0.489),
                "9" => new Point(0.523, 0.489),
                "X" => new Point(0.520, 0.486),
                _ => new Point(0.5, 0.5)
            }
            : new Point(0.5, 0.5);

    private static Brush CoverFor(int chapter, string code)
    {
        if (chapter == 2)
        {
            return code switch
            {
                "1" => Gradient("#3A5774", "#081724"),
                "2" => Gradient("#8C4D28", "#160F1A"),
                "3" => Gradient("#1E6E8C", "#071724"),
                "4" => Gradient("#6D28D9", "#160F1A"),
                "5" => Gradient("#CA8A04", "#1A1408"),
                "6" => Gradient("#16A34A", "#07140C"),
                "7" => Gradient("#7C3AED", "#0B0B1A"),
                "8" => Gradient("#2563EB", "#080F1F"),
                _ => LockedCover
            };
        }

        return code switch
        {
            "0" => Gradient("#2D7D72", "#071724"),
            "1" => Gradient("#3A5774", "#081724"),
            "2" => Gradient("#8C4D28", "#160F1A"),
            "3" => Gradient("#385B8E", "#17162F"),
            "4" => Gradient("#9B642F", "#24120F"),
            "5" => Gradient("#654295", "#10172B"),
            "6" => Gradient("#4B2D6F", "#0E0B18"),
            "7" => Gradient("#5C99B8", "#0B2234"),
            "8" => Gradient("#A66532", "#0A2822"),
            "9" => Gradient("#2D8590", "#111A35"),
            "X" => Gradient("#246F96", "#51318A"),
            _ => LockedCover
        };
    }

    private static Brush Gradient(string from, string to)
    {
        var brush = new LinearGradientBrush(
            (Color)ColorConverter.ConvertFromString(from),
            (Color)ColorConverter.ConvertFromString(to),
            new Point(0, 0),
            new Point(1, 1));
        brush.Freeze();
        return brush;
    }

    // Chapter 1 accents sampled from each season's keyart, then brightened for UI use.
    private static Color AccentFor(int chapter, string code)
    {
        string hex = chapter switch
        {
            1 => code switch
            {
                "0" => "#9B6AD0",
                "1" => "#6FCF8B",
                "2" => "#E7B534",
                "3" => "#D07A4A",
                "4" => "#5E9BD8",
                "5" => "#E06075",
                "6" => "#A15CDE",
                "7" => "#7FC6EE",
                "8" => "#E0A54A",
                "9" => "#7DF0E0",
                "X" => "#5B9BE8",
                _ => "#F2F3F5"
            },
            2 => code switch
            {
                "1" => "#6FA8FF",
                "2" => "#F2B135",
                _ => "#F2F3F5"
            },
            _ => "#F2F3F5"
        };
        return (Color)ColorConverter.ConvertFromString(hex);
    }

    private static SolidColorBrush Brush(Color color)
    {
        var brush = new SolidColorBrush(color);
        brush.Freeze();
        return brush;
    }

    private static Color Lighten(Color color, double amount) => Color.FromRgb(
        (byte)Math.Min(255, color.R + (255 - color.R) * amount),
        (byte)Math.Min(255, color.G + (255 - color.G) * amount),
        (byte)Math.Min(255, color.B + (255 - color.B) * amount));

    private static Color Darken(Color color, double amount) => Color.FromRgb(
        (byte)(color.R * (1 - amount)),
        (byte)(color.G * (1 - amount)),
        (byte)(color.B * (1 - amount)));

    private static Color Alpha(Color color, byte alpha) => Color.FromArgb(alpha, color.R, color.G, color.B);

    private static double Luminance(Color color) => 0.299 * color.R + 0.587 * color.G + 0.114 * color.B;

    private static LinearGradientBrush Vertical(Color from, Color to)
    {
        var brush = new LinearGradientBrush(from, to, new Point(0, 0), new Point(0, 1));
        brush.Freeze();
        return brush;
    }

    private static LinearGradientBrush Diagonal(Color from, Color to)
    {
        var brush = new LinearGradientBrush(from, to, new Point(0, 0), new Point(1, 1));
        brush.Freeze();
        return brush;
    }

    private static LinearGradientBrush Detail(Color accent)
    {
        var brush = new LinearGradientBrush { StartPoint = new Point(0, 0), EndPoint = new Point(1, 0) };
        brush.GradientStops.Add(new GradientStop(Alpha(Darken(accent, 0.88), 0xF2), 0));
        brush.GradientStops.Add(new GradientStop(Alpha(Darken(accent, 0.78), 0xD6), 0.42));
        brush.GradientStops.Add(new GradientStop(Alpha(accent, 0x59), 0.72));
        brush.GradientStops.Add(new GradientStop(Alpha(Darken(accent, 0.50), 0x8C), 1));
        brush.Freeze();
        return brush;
    }

    private static LinearGradientBrush Page(Color accent)
    {
        var brush = new LinearGradientBrush { StartPoint = new Point(0, 0), EndPoint = new Point(0, 1) };
        brush.GradientStops.Add(new GradientStop(Alpha(Darken(accent, 0.76), 0xD9), 0));
        brush.GradientStops.Add(new GradientStop(Alpha(Darken(accent, 0.66), 0x99), 0.55));
        brush.GradientStops.Add(new GradientStop(Alpha(Darken(accent, 0.90), 0xE6), 1));
        brush.Freeze();
        return brush;
    }
}
