using System;
using System.Globalization;
using System.IO;
using System.Text;
using SyndicateLauncher.Models;

namespace SyndicateLauncher.Services;

/// <summary>
/// Hands the custom crosshair (Settings &gt; Display &gt; Crosshair) to SyndicateMenu.dll. The DLL reads
/// crosshair.ini from the shared ProjectTilted folder (<see cref="ClientMods.Folder"/>) twice a second, so a
/// change here shows up in game at once, even mid-match. With the Syndicate menu setting off, GameLauncher
/// still loads SyndicateMenu.dll when the crosshair is on, after writing menu-load.txt = "crosshair": the DLL
/// then runs in crosshair-only mode (no menu, keys, game-object scan or other hooks), so nothing else of the
/// menu is in the match and achievements still count.
/// </summary>
public static class CrosshairService
{
    public const string ConfigFile = "crosshair.ini";
    public const string LoadModeFile = "menu-load.txt";

    /// <summary>The settings in use; set by the view model at start and on every change.</summary>
    public static CrosshairSettings Current { get; private set; } = new();

    public static bool Enabled => Current.Enabled;

    public static void Apply(CrosshairSettings settings)
    {
        Current = settings.Copy();
        Write(Current);
    }

    /// <summary>Written right before SyndicateMenu.dll is loaded: the DLL reads it once at start.</summary>
    public static void WriteLoadMode(bool fullMenu) => ClientMods.SetText(LoadModeFile, fullMenu ? "menu" : "crosshair");

    public static string Serialize(CrosshairSettings c)
    {
        static string B(bool v) => v ? "1" : "0";
        static string I(int v) => v.ToString(CultureInfo.InvariantCulture);
        var sb = new StringBuilder();
        sb.Append("; Syndicate crosshair. Written by the launcher (Settings > Display > Crosshair); edits here are overwritten.\r\n");
        sb.Append("enabled=").Append(B(c.Enabled)).Append("\r\n");
        sb.Append("lines=").Append(B(c.Lines)).Append("\r\n");
        sb.Append("t=").Append(B(c.TStyle)).Append("\r\n");
        sb.Append("diagonal=").Append(B(c.Diagonal)).Append("\r\n");
        sb.Append("length=").Append(I(c.Length)).Append("\r\n");
        sb.Append("thickness=").Append(I(c.Thickness)).Append("\r\n");
        sb.Append("gap=").Append(I(c.Gap)).Append("\r\n");
        sb.Append("dot=").Append(B(c.Dot)).Append("\r\n");
        sb.Append("dotsize=").Append(I(c.DotSize)).Append("\r\n");
        sb.Append("dotround=").Append(B(c.DotRound)).Append("\r\n");
        sb.Append("circle=").Append(B(c.Circle)).Append("\r\n");
        sb.Append("circleradius=").Append(I(c.CircleRadius)).Append("\r\n");
        sb.Append("circlethickness=").Append(I(c.CircleThickness)).Append("\r\n");
        sb.Append("outline=").Append(B(c.Outline)).Append("\r\n");
        sb.Append("outlinethickness=").Append(I(c.OutlineThickness)).Append("\r\n");
        sb.Append("color=").Append(Argb(c.Color, c.Opacity)).Append("\r\n");
        sb.Append("outlinecolor=").Append(Argb(c.OutlineColor, c.OutlineOpacity)).Append("\r\n");
        sb.Append("hidewithcursor=").Append(B(c.HideInMenus)).Append("\r\n");
        sb.Append("hidewhenaiming=").Append(B(c.HideWhenAiming)).Append("\r\n");
        return sb.ToString();
    }

    /// <summary>"#RRGGBB" + opacity (0-100) as "AARRGGBB".</summary>
    public static string Argb(string rgb, int opacity)
    {
        int alpha = (int)Math.Round(Math.Clamp(opacity, 0, 100) * 2.55);
        return alpha.ToString("X2", CultureInfo.InvariantCulture) + NormalizeRgb(rgb, "FFFFFF");
    }

    /// <summary>"#abc", "aabbcc" or "#AABBCC" as "AABBCC"; anything else gives <paramref name="fallback"/>.</summary>
    public static string NormalizeRgb(string? text, string fallback)
    {
        string t = (text ?? "").Trim().TrimStart('#');
        if (t.Length == 3) t = string.Concat(t[0], t[0], t[1], t[1], t[2], t[2]);
        if (t.Length == 8) t = t[2..]; // AARRGGBB: keep the colour, opacity has its own slider
        return t.Length == 6 && int.TryParse(t, NumberStyles.HexNumber, CultureInfo.InvariantCulture, out _)
            ? t.ToUpperInvariant() : fallback;
    }

    private static void Write(CrosshairSettings c)
    {
        try
        {
            string folder = ClientMods.Folder;
            Directory.CreateDirectory(folder);
            string file = Path.Combine(folder, ConfigFile);
            string temp = file + ".tmp";
            string text = Serialize(c);
            File.WriteAllText(temp, text);
            try { File.Move(temp, file, true); } // the game never reads a half-written file
            catch (IOException) { File.WriteAllText(file, text); File.Delete(temp); } // the game had it open that instant
        }
        catch { }
    }
}
