// Syndicate © 2026 Syndicate. All rights reserved. Proprietary — see LICENSE.
// Small persisted settings for the Linux launcher (which Fortnite folder is linked, etc.).
// Stored under ~/.local/share/SyndicateClassic/linux-config.json. All reads/writes are
// wrapped so a missing or unreadable file never crashes the launcher.
using System;
using System.IO;
using System.Text.Json;

namespace SyndicateLauncher.Linux;

public static class Config
{
    private static string Dir =>
        Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "ProjectTilted");
    private static string FilePath => Path.Combine(Dir, "linux-config.json");

    public static string? GameFolder { get; private set; }

    static Config() => Load();

    private static void Load()
    {
        try
        {
            if (!File.Exists(FilePath)) return;
            using var doc = JsonDocument.Parse(File.ReadAllText(FilePath));
            if (doc.RootElement.TryGetProperty("gameFolder", out var g) && g.ValueKind == JsonValueKind.String)
                GameFolder = g.GetString();
        }
        catch { /* ignore a missing/corrupt config */ }
    }

    public static void SetGameFolder(string? path)
    {
        GameFolder = path;
        try
        {
            Directory.CreateDirectory(Dir);
            File.WriteAllText(FilePath, JsonSerializer.Serialize(new { gameFolder = GameFolder }));
        }
        catch { /* best effort */ }
    }

    // A folder is a Fortnite build if it has the shipping client where the launcher expects it.
    public static bool LooksLikeBuild(string? folder)
    {
        if (string.IsNullOrWhiteSpace(folder)) return false;
        try { return File.Exists(Path.Combine(folder, "FortniteGame", "Binaries", "Win64", "FortniteClient-Win64-Shipping.exe")); }
        catch { return false; }
    }
}
