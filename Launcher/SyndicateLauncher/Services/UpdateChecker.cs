using System;
using System.Diagnostics;
using System.IO;
using System.Net.Http;
using System.Text.Json;
using System.Threading.Tasks;
using System.Windows;

namespace SyndicateLauncher.Services;

/// <summary>
/// On startup, asks GitHub Releases for the newest release of the public launcher. If its tag is newer than
/// the tag this copy shipped as, it tells the player and (on Yes) opens the download page so they reinstall.
/// The tag this build shipped as is written by publish-public-release.ps1 to Runtime\release-tag.txt; if that
/// file is missing (a dev build, or a copy with no tag) the check does nothing, so developers are never nagged.
/// Everything is guarded: any failure (offline, GitHub down, bad JSON) is swallowed and the launcher runs normally.
/// </summary>
public static class UpdateChecker
{
    // The public download repo. Releases are tagged by date, e.g. v2026-11-15, so a later tag sorts greater.
    // Checked in order; the newest release across them wins (same list as dev/standalone/Bootstrap/Updater.cs).
    private static readonly string[] Repos = { "Sacttt/Syndicate-Launcher", "d4shyb0i/Syndicate-Launcher" };
    private static readonly HttpClient Http = new() { Timeout = TimeSpan.FromSeconds(10) };
    private static bool _done;

    private static string? OwnTag()
    {
        foreach (var path in new[]
        {
            Path.Combine(AppContext.BaseDirectory, "Runtime", "release-tag.txt"),
            Path.Combine(AppContext.BaseDirectory, "release-tag.txt")
        })
        {
            try { if (File.Exists(path)) { var t = File.ReadAllText(path).Trim(); if (t.Length > 0) return t; } }
            catch { }
        }
        return null;
    }

    /// <summary>"v2026.11.17.0430" / "2026-11-15" -> [2026, 11, 17, 430] / [2026, 11, 15]; null if not numeric.</summary>
    internal static int[]? ParseTag(string? tag)
    {
        if (string.IsNullOrWhiteSpace(tag)) return null;
        var parts = tag.Trim().TrimStart('v', 'V').Split(new[] { '.', '-' }, StringSplitOptions.RemoveEmptyEntries);
        var numbers = new int[parts.Length];
        for (int i = 0; i < parts.Length; i++)
            if (!int.TryParse(parts[i], out numbers[i])) return null;
        return numbers.Length > 0 ? numbers : null;
    }

    /// <summary>True when <paramref name="latest"/> is a later release than <paramref name="own"/>.</summary>
    internal static bool IsNewer(string latest, string own)
    {
        var a = ParseTag(latest);
        var b = ParseTag(own);
        if (a is null || b is null)
            return string.Compare(latest.TrimStart('v', 'V'), own.TrimStart('v', 'V'), StringComparison.OrdinalIgnoreCase) > 0;
        for (int i = 0; i < Math.Max(a.Length, b.Length); i++)
        {
            int x = i < a.Length ? a[i] : 0, y = i < b.Length ? b[i] : 0;
            if (x != y) return x > y;
        }
        return false;
    }

    /// <summary>Fire-and-forget: call once at startup. Never throws.</summary>
    public static async Task CheckAsync()
    {
        try
        {
            if (_done) return;
            _done = true;
#if DEV_BUILD
            // The dev launcher (UPDATE FROM GIT) is built from source and is never replaced by a public release.
            // Runtime\release-tag.txt is checked into Git, so the tag file alone can't tell a dev copy apart.
            return;
#endif

            var own = OwnTag();
            if (string.IsNullOrWhiteSpace(own)) return; // dev build / untagged copy: don't nag

            // The single-file download (Syndicate.exe) runs this launcher from %LOCALAPPDATA%\SyndicateStandalone
            // and updates itself before it starts us, so asking here would only send the player to a download page.
            if (AppContext.BaseDirectory.Contains(@"\SyndicateStandalone\", StringComparison.OrdinalIgnoreCase)) return;

            string? latest = null, url = null;
            foreach (var repo in Repos)
            {
                try
                {
                    using var req = new HttpRequestMessage(HttpMethod.Get, $"https://api.github.com/repos/{repo}/releases/latest");
                    req.Headers.UserAgent.ParseAdd("SyndicateLauncher-UpdateCheck");
                    using var resp = await Http.SendAsync(req).ConfigureAwait(false);
                    if (!resp.IsSuccessStatusCode) continue;
                    using var doc = JsonDocument.Parse(await resp.Content.ReadAsStringAsync().ConfigureAwait(false));
                    var root = doc.RootElement;
                    if (!root.TryGetProperty("tag_name", out var tagEl) || tagEl.GetString() is not { Length: > 0 } tag) continue;
                    if (latest is not null && !IsNewer(tag, latest)) continue;
                    latest = tag;
                    url = root.TryGetProperty("html_url", out var u) && u.GetString() is { Length: > 0 } h
                        ? h
                        : $"https://github.com/{repo}/releases/latest";
                }
                catch { }
            }
            if (latest is null || url is null) return;

            // Only prompt when the latest tag is actually newer than ours. Tags come in two shapes: the zip
            // package's date tags (v2026-11-15) and the standalone exe's versions (v2026.11.17.0430, stamped into
            // Runtime\release-tag.txt without the "v"). Compare them as numbers so neither the "v" nor "-" vs "."
            // makes an up-to-date copy look old.
            if (!IsNewer(latest, own)) return;

            var app = Application.Current;
            if (app?.Dispatcher is null) return;
            app.Dispatcher.Invoke(() =>
            {
                var choice = MessageBox.Show(
                    $"A new version of Syndicate is available ({latest}).\n\n" +
                    "Download it now? Your accounts and settings are kept.",
                    "Syndicate Update Available",
                    MessageBoxButton.YesNo, MessageBoxImage.Information);
                if (choice == MessageBoxResult.Yes)
                {
                    try { Process.Start(new ProcessStartInfo(url) { UseShellExecute = true }); }
                    catch { }
                }
            });
        }
        catch { }
    }
}
