using System;
using System.IO;
using System.Runtime.InteropServices;
using System.Text.RegularExpressions;

namespace SyndicateLauncher.Services;

/// <summary>
/// Settings &gt; Display &gt; Stretched resolution (ported from Project Tilted). Writes the size into Fortnite's
/// GameUserSettings.ini in Fullscreen mode and makes the file read-only, so the game starts at that size and the
/// monitor stretches it; turns on the client mod's stretched view (stretched-on.txt). "Back to normal" puts the
/// monitor's own size and windowed fullscreen back. Every season reads the same file.
/// </summary>
public static class StretchedResolution
{
    public static string SettingsFile => Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),
        "FortniteGame", "Saved", "Config", "WindowsClient", "GameUserSettings.ini");

    private static readonly string[] WidthKeys = { "ResolutionSizeX", "LastUserConfirmedResolutionSizeX", "DesiredScreenWidth", "LastUserConfirmedDesiredScreenWidth" };
    private static readonly string[] HeightKeys = { "ResolutionSizeY", "LastUserConfirmedResolutionSizeY", "DesiredScreenHeight", "LastUserConfirmedDesiredScreenHeight" };
    private static readonly string[] ModeKeys = { "FullscreenMode", "LastConfirmedFullscreenMode", "PreferredFullscreenMode" };

    public static bool IsOn => ClientMods.Stretched;

    /// <summary>The size in the settings file now (0, 0 when there is none).</summary>
    public static (int Width, int Height) Current()
    {
        try
        {
            if (!File.Exists(SettingsFile)) return (0, 0);
            string text = File.ReadAllText(SettingsFile);
            var x = Regex.Match(text, "^ResolutionSizeX=(\\d+)", RegexOptions.Multiline);
            var y = Regex.Match(text, "^ResolutionSizeY=(\\d+)", RegexOptions.Multiline);
            return (x.Success ? int.Parse(x.Groups[1].Value) : 0, y.Success ? int.Parse(y.Groups[1].Value) : 0);
        }
        catch (Exception e) when (e is IOException or UnauthorizedAccessException or FormatException) { return (0, 0); }
    }

    public static string Validate(int width, int height) =>
        width < 640 || height < 480 || width > 7680 || height > 4320
            ? "Enter a width from 640 to 7680 and a height from 480 to 4320." : "";

    public static void Apply(int width, int height)
    {
        string file = SettingsFile;
        Directory.CreateDirectory(Path.GetDirectoryName(file)!);
        if (File.Exists(file)) File.SetAttributes(file, File.GetAttributes(file) & ~FileAttributes.ReadOnly);
        string text = File.Exists(file) ? File.ReadAllText(file) : "[/Script/FortniteGame.FortGameUserSettings]\r\n";
        foreach (var key in WidthKeys) text = SetIniValue(text, key, width.ToString());
        foreach (var key in HeightKeys) text = SetIniValue(text, key, height.ToString());
        // Fullscreen (0) draws at this size and the monitor stretches it; windowed fullscreen always draws at the
        // desktop's size, so nothing would stretch.
        foreach (var key in ModeKeys) text = SetIniValue(text, key, "0");
        File.WriteAllText(file, text);
        // Read-only stops the game putting its own size back when it starts.
        File.SetAttributes(file, File.GetAttributes(file) | FileAttributes.ReadOnly);
        ClientMods.Stretched = true;
    }

    public static void Reset()
    {
        string file = SettingsFile;
        if (File.Exists(file))
        {
            File.SetAttributes(file, File.GetAttributes(file) & ~FileAttributes.ReadOnly);
            string text = File.ReadAllText(file);
            foreach (var key in ModeKeys) text = SetIniValue(text, key, "1");
            var (w, h) = NativeScreenSize();
            if (w >= 640 && h >= 480)
            {
                foreach (var key in WidthKeys) text = SetIniValue(text, key, w.ToString());
                foreach (var key in HeightKeys) text = SetIniValue(text, key, h.ToString());
            }
            File.WriteAllText(file, text);
        }
        ClientMods.Stretched = false;
    }

    private static string SetIniValue(string text, string key, string value)
    {
        var pattern = new Regex("^" + Regex.Escape(key) + "=.*$", RegexOptions.Multiline);
        if (pattern.IsMatch(text)) return pattern.Replace(text, key + "=" + value);
        const string section = "[/Script/FortniteGame.FortGameUserSettings]";
        int at = text.IndexOf(section, StringComparison.OrdinalIgnoreCase);
        if (at < 0) return text.TrimEnd() + "\r\n\r\n" + section + "\r\n" + key + "=" + value + "\r\n";
        int line = text.IndexOf('\n', at);
        if (line < 0) { text += "\r\n"; line = text.Length - 1; }
        return text.Insert(line + 1, key + "=" + value + "\r\n");
    }

    [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
    private struct DEVMODE
    {
        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 32)] public string dmDeviceName;
        public short dmSpecVersion, dmDriverVersion, dmSize, dmDriverExtra; public int dmFields;
        public int dmPositionX, dmPositionY, dmDisplayOrientation, dmDisplayFixedOutput;
        public short dmColor, dmDuplex, dmYResolution, dmTTOption, dmCollate;
        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 32)] public string dmFormName;
        public short dmLogPixels; public int dmBitsPerPel, dmPelsWidth, dmPelsHeight, dmDisplayFlags, dmDisplayFrequency;
        public int dmICMMethod, dmICMIntent, dmMediaType, dmDitherType, dmReserved1, dmReserved2, dmPanningWidth, dmPanningHeight;
    }
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] private static extern bool EnumDisplaySettings(string? device, int mode, ref DEVMODE devMode);

    /// <summary>The monitor's native size (its largest mode).</summary>
    public static (int Width, int Height) NativeScreenSize()
    {
        int w = 0, h = 0;
        try
        {
            var mode = new DEVMODE { dmSize = (short)Marshal.SizeOf<DEVMODE>() };
            for (int i = 0; EnumDisplaySettings(null, i, ref mode); i++)
                if ((long)mode.dmPelsWidth * mode.dmPelsHeight > (long)w * h) { w = mode.dmPelsWidth; h = mode.dmPelsHeight; }
        }
        catch (Exception e) when (e is DllNotFoundException or EntryPointNotFoundException) { }
        return (w, h);
    }
}
