using System;
using System.Globalization;
using System.IO;

namespace SyndicateLauncher.Services;

/// <summary>
/// The on/off files read live by SyndicateClient.dll, SyndicatePadAssist.dll and Erbium.
/// They live in the same folder Syndicate uses, so the modules (which are
/// injected into Fortnite) see the same settings while Syndicate runs.
/// </summary>
public static class ClientMods
{
    public static string Folder
    {
        get
        {
            string local = Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData);
            string shared = Path.Combine(local, "ProjectTilted");
            if (Directory.Exists(shared)) return shared;
            // Syndicate keeps its own copy when Syndicate is not installed.
            string own = Path.Combine(local, "SyndicateLauncher", "ProjectTilted");
            Directory.CreateDirectory(own);
            return own;
        }
    }

    public static bool Flag(string name)
    {
        try { return File.Exists(Path.Combine(Folder, name)); }
        catch { return false; }
    }

    public static void SetFlag(string name, bool on)
    {
        try
        {
            Directory.CreateDirectory(Folder);
            string file = Path.Combine(Folder, name);
            if (on) File.WriteAllText(file, "");
            else if (File.Exists(file)) File.Delete(file);
        }
        catch { }
    }

    public static string Text(string name, string fallback = "")
    {
        try
        {
            string file = Path.Combine(Folder, name);
            return File.Exists(file) ? File.ReadAllText(file).Trim() : fallback;
        }
        catch { return fallback; }
    }

    public static void SetText(string name, string value)
    {
        try
        {
            Directory.CreateDirectory(Folder);
            File.WriteAllText(Path.Combine(Folder, name), value);
        }
        catch { }
    }

    // Gameplay mods ------------------------------------------------------
    public static bool TurboBuild { get => Flag("turbo-build-on.txt"); set => SetFlag("turbo-build-on.txt", value); }
    public static bool EditOnRelease { get => Flag("edit-release-on.txt"); set => SetFlag("edit-release-on.txt", value); }
    public static bool ResetOnRelease { get => Flag("reset-release-on.txt"); set => SetFlag("reset-release-on.txt", value); }
    public static bool EditSelectRelease { get => !Flag("edit-release-select-off.txt"); set => SetFlag("edit-release-select-off.txt", !value); }
    public static bool EditReleaseNeedsTile { get => !Flag("edit-release-any.txt"); set => SetFlag("edit-release-any.txt", !value); }

    public static string EditKey
    {
        get { string v = Text("edit-key.txt"); return ValidKey(v) ? v : "key:71"; }
        set { if (ValidKey(value)) SetText("edit-key.txt", value); }
    }

    public static string PadEditButton
    {
        get { string v = Text("pad-edit-button.txt").ToUpperInvariant(); return Array.IndexOf(PadButtons, v) >= 0 ? v : "B"; }
        set { SetText("pad-edit-button.txt", value); }
    }

    public static readonly string[] PadButtons = { "B", "Y", "X", "A", "RB", "LB", "RS", "LS", "UP", "DOWN", "LEFT", "RIGHT" };

    // Server-side flags read by Erbium ------------------------------------
    public static bool ClassicArPump { get => Flag("classic-ar-pump.txt"); set => SetFlag("classic-ar-pump.txt", value); }
    public static bool BotEmotesDisabled { get => Flag("no-bot-emotes.txt"); set => SetFlag("no-bot-emotes.txt", value); }

    // Display ------------------------------------------------------------
    public static float Fov
    {
        get
        {
            string text = Text("fov.txt");
            return float.TryParse(text, NumberStyles.Float, CultureInfo.InvariantCulture, out float value) &&
                   value >= 60 && value <= 130 ? value : 0;
        }
        set
        {
            try
            {
                string file = Path.Combine(Folder, "fov.txt");
                if (value <= 0) { if (File.Exists(file)) File.Delete(file); }
                else SetText("fov.txt", value.ToString("0.#", CultureInfo.InvariantCulture));
            }
            catch { }
        }
    }

    public static bool Stretched { get => Flag("stretched-on.txt"); set => SetFlag("stretched-on.txt", value); }

    private static bool ValidKey(string value) =>
        value.StartsWith("key:", StringComparison.Ordinal) && int.TryParse(value.AsSpan(4), out int vk) && vk is > 7 and < 255 ||
        value.StartsWith("mouse:", StringComparison.Ordinal);
}
