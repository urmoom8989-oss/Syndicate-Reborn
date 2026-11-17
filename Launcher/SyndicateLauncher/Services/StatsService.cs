using System;
using System.IO;
using System.Linq;
using System.Text.Json;
using SyndicateLauncher.Models;

namespace SyndicateLauncher.Services;

/// <summary>
/// Saves play sessions per profile (Total Playtime and 7-Day Activity on Home). GameLauncher calls
/// <see cref="Record"/> when the game client it started goes away.
/// </summary>
public static class StatsService
{
    private static readonly object Gate = new();
    private static readonly JsonSerializerOptions Json = new() { WriteIndented = true };

    /// <summary>Raised (on any thread) after a session was saved, with the profile name.</summary>
    public static event Action<string>? Changed;

    private static string Folder => Path.Combine(SettingsService.DataDirectory, "stats");

    private static string FileFor(string profile)
    {
        var safe = new string(profile.Select(c => Path.GetInvalidFileNameChars().Contains(c) ? '_' : char.ToLowerInvariant(c)).ToArray());
        return Path.Combine(Folder, (safe.Length == 0 ? "player" : safe) + ".json");
    }

    public static PlayerStats Load(string? profile)
    {
        if (string.IsNullOrWhiteSpace(profile)) return new PlayerStats();
        lock (Gate)
        {
            try
            {
                var path = FileFor(profile);
                if (File.Exists(path)) return JsonSerializer.Deserialize<PlayerStats>(File.ReadAllText(path)) ?? new PlayerStats();
            }
            catch (Exception) { /* a damaged file starts a fresh history rather than breaking Home */ }
            return new PlayerStats();
        }
    }

    /// <summary>Moves a profile's play history and Home cache to its new name (Choose Player > Rename).</summary>
    public static void Rename(string oldName, string newName)
    {
        lock (Gate)
        {
            Move(FileFor(oldName), FileFor(newName));
            Move(ViewModels.HomeViewModel.CacheFileFor(oldName), ViewModels.HomeViewModel.CacheFileFor(newName));
        }
        static void Move(string from, string to)
        {
            try { if (File.Exists(from) && !string.Equals(from, to, StringComparison.OrdinalIgnoreCase)) File.Move(from, to, overwrite: true); }
            catch (Exception) { /* history is a nice-to-have; the rename itself already happened */ }
        }
    }

    /// <summary>Removes a profile's play history and Home cache (Choose Player > Delete).</summary>
    public static void Delete(string profile)
    {
        lock (Gate)
        {
            foreach (var path in new[] { FileFor(profile), ViewModels.HomeViewModel.CacheFileFor(profile) })
                try { if (File.Exists(path)) File.Delete(path); } catch (Exception) { }
        }
    }

    public static void Record(string profile, DateTime startedUtc, DateTime endedUtc, int season, bool menuOn)
    {
        double minutes = (endedUtc - startedUtc).TotalMinutes;
        if (string.IsNullOrWhiteSpace(profile) || minutes < 0.5 || minutes > 24 * 60) return; // crashes at boot and clock jumps
        lock (Gate)
        {
            var stats = Load(profile);
            stats.Sessions.Add(new GameSession { StartedUtc = startedUtc, Minutes = Math.Round(minutes, 2), Season = season, MenuOn = menuOn });
            if (stats.Sessions.Count > 4000) stats.Sessions.RemoveRange(0, stats.Sessions.Count - 4000);
            try
            {
                Directory.CreateDirectory(Folder);
                var path = FileFor(profile);
                File.WriteAllText(path + ".tmp", JsonSerializer.Serialize(stats, Json));
                File.Move(path + ".tmp", path, overwrite: true);
            }
            catch (Exception) { return; }
        }
        Changed?.Invoke(profile);
    }
}
