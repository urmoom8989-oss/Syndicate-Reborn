// Syndicate © 2026 Syndicate. All rights reserved. Proprietary — see LICENSE.
// The Night palette, ported from the Windows launcher so the Linux build already
// looks like Syndicate. Same hex values; Avalonia's Color.Parse handles #RRGGBB and #AARRGGBB.
using Avalonia.Media;

namespace SyndicateLauncher.Linux;

public static class Night
{
    public const string Bg       = "#06070C";
    public const string Surface  = "#0C0F1A";
    public const string Surface2 = "#131726";
    public const string Line     = "#232A40";
    public const string Text     = "#F5F7FF";
    public const string Sub      = "#A3ABCB";
    public const string Muted    = "#636B92";
    public const string Gold      = "#FFD23F";
    public const string GoldDeep  = "#F2B705";
    public const string Sky       = "#4CC2FF";
    public const string Violet    = "#8B6CFF";
    public const string Good      = "#3BE48B";
    public const string Danger    = "#FF5A6A";
    public const string Hover     = "#1B2033";
    public const string Frost     = "#990C0F1A";
    public const string FrostHover= "#C21B2033";
    public const string FrostEdge = "#33FFFFFF";
    public const string OnGold    = "#1A1405";

    // Each season has its own accent (window glow, picker bar, build text).
    public static string SeasonAccent(int season) => season switch
    {
        0 => "#B388FF", 1 => "#22C55E", 2 => "#F59E0B", 3 => "#FF8A4C", 4 => "#38BDF8", 5 => "#2DD4BF",
        6 => "#8B5CF6", 7 => "#7DD3FC", 8 => "#F97316", 9 => "#22D3EE", 11 => "#3B82F6", 12 => "#F2B135", _ => "#E11D48"
    };

    // WPF's `Hex(string)` equivalent. Fully qualified so the `Color` method name below
    // doesn't shadow the Color type (the classic C# "Color Color" clash).
    public static SolidColorBrush Brush(string hex) => new(Avalonia.Media.Color.Parse(hex));

    public static Avalonia.Media.Color Color(string hex) => Avalonia.Media.Color.Parse(hex);
}
