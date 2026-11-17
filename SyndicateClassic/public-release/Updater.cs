// Syndicate public-release updater. It runs on a normal start (a quick, silent check so reopening
// the launcher installs a new version right away) and when the "Check for updates" button restarts this
// EXE with --check-updates (a longer check that shows every step).
// It asks GitHub Releases for the newest version. If it is newer, downloads the new EXE, checks
// it against the release's .sha256 file, swaps it in under the same file name and starts it.
// Any failure (offline, GitHub down, bad hash) just starts the current version.
using System;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Net.Http;
using System.Security.Cryptography;
using System.Text.Json;
using System.Threading;

static class Updater
{
    // Filled in by build-public-release.ps1 (owner/repo). Empty disables updating.
    const string Repository = "__TILTED_UPDATE_REPO__";
    const string UpdatedFlag = "--tilted-updated";
    const string CheckFlag = "--check-updates";

    static string LogPath => Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "SyndicateClassicStandalone", "updater.log");
    static void Log(string text)
    {
        try { Directory.CreateDirectory(Path.GetDirectoryName(LogPath)!); File.AppendAllText(LogPath, $"{DateTime.UtcNow:O} {text}\n"); } catch { }
    }

    // A freshly updated copy waits for the old one to close instead of saying "already running".
    public static int MutexWaitMs(string[] args) => args.Contains(CheckFlag) ? 120000 : args.Contains(UpdatedFlag) ? 20000 : 0;

    // Returns true when a newer version was started and this process should exit.
    // Two ways in: a normal start checks quickly and silently (so reopening the launcher picks up a new
    // version right away without slowing a fast start), and the "Check for updates" button (--check-updates)
    // waits longer and shows every step. Either way a newer version is downloaded, verified and swapped in.
    public static bool TryUpdate(string currentVersion, string[] args, Action<string> progress, Action releaseMutex)
    {
        CleanupSwapLeftovers();
        if (args.Contains(UpdatedFlag)) { Log("running updated version " + currentVersion); return false; }
        bool explicitCheck = args.Contains(CheckFlag);
        if (string.IsNullOrWhiteSpace(Repository) || Repository.StartsWith("__") || !Repository.Contains('/'))
        {
            Log("updates are off in this build (no repository)");
            if (explicitCheck) { progress("Updates aren't set up for this copy of Syndicate.\nStarting your current version…"); Thread.Sleep(2500); }
            return false;
        }
        try
        {
            if (explicitCheck) progress("Checking for updates…");
            // The button can afford to wait; a normal start keeps the check short so an offline or slow
            // GitHub never holds up opening the launcher.
            using var http = new HttpClient { Timeout = TimeSpan.FromSeconds(explicitCheck ? 8 : 4) };
            http.DefaultRequestHeaders.UserAgent.ParseAdd("SyndicateClassic-Updater/" + currentVersion);
            http.DefaultRequestHeaders.Accept.ParseAdd("application/vnd.github+json");
            string json = http.GetStringAsync($"https://api.github.com/repos/{Repository}/releases/latest").GetAwaiter().GetResult();
            using var doc = JsonDocument.Parse(json);
            var root = doc.RootElement;
            string tag = root.GetProperty("tag_name").GetString() ?? "";
            if (!Version.TryParse(tag.TrimStart('v', 'V'), out var latest) || !Version.TryParse(currentVersion, out var current) || latest <= current)
            {
                Log($"checked {Repository}: latest release is {tag}, running {currentVersion}; nothing to install");
                if (explicitCheck) { progress($"You have the latest version (v{currentVersion}).\nStarting Syndicate…"); Thread.Sleep(2000); }
                return false;
            }
            // A newer version exists: show what is happening even on a normal start, since the download takes a moment.
            progress($"A new version ({tag}) is available. Updating Syndicate…\nYour accounts and settings stay saved.");

            string? exeUrl = null, exeName = null, shaUrl = null; long exeSize = 0;
            foreach (var asset in root.GetProperty("assets").EnumerateArray())
            {
                string name = asset.GetProperty("name").GetString() ?? "";
                string url = asset.GetProperty("browser_download_url").GetString() ?? "";
                if (name.EndsWith(".exe", StringComparison.OrdinalIgnoreCase) && exeUrl == null) { exeUrl = url; exeName = name; exeSize = asset.GetProperty("size").GetInt64(); }
            }
            foreach (var asset in root.GetProperty("assets").EnumerateArray())
                if (exeName != null && string.Equals(asset.GetProperty("name").GetString(), exeName + ".sha256", StringComparison.OrdinalIgnoreCase))
                    shaUrl = asset.GetProperty("browser_download_url").GetString();
            if (exeUrl == null || shaUrl == null) { Log($"release {tag} has no .exe with a matching .sha256; not updating"); return false; }

            string expected = http.GetStringAsync(shaUrl).GetAwaiter().GetResult().Trim().Split(' ', '\t')[0].ToUpperInvariant();
            if (expected.Length != 64) { Log("bad .sha256 file in release " + tag); return false; }

            // Download next to the running EXE so it can be swapped in under the same name;
            // fall back to the user's AppData if that folder is read-only.
            string self = Environment.ProcessPath ?? throw new IOException("Unknown program path");
            string dir = Path.GetDirectoryName(self)!;
            string target = self;
            if (!CanWrite(dir))
            {
                dir = Path.Combine(Path.GetDirectoryName(LogPath)!, "updates");
                Directory.CreateDirectory(dir);
                target = Path.Combine(dir, Path.GetFileName(self));
            }
            string partial = target + ".download";
            using (var dl = new HttpClient { Timeout = TimeSpan.FromMinutes(30) })
            {
                dl.DefaultRequestHeaders.UserAgent.ParseAdd("SyndicateClassic-Updater/" + currentVersion);
                using var response = dl.GetAsync(exeUrl, HttpCompletionOption.ResponseHeadersRead).GetAwaiter().GetResult();
                response.EnsureSuccessStatusCode();
                long total = response.Content.Headers.ContentLength ?? exeSize;
                using var input = response.Content.ReadAsStream();
                using var output = new FileStream(partial, FileMode.Create, FileAccess.Write, FileShare.None);
                var buffer = new byte[1 << 20]; long done = 0; int read, lastPct = -1;
                while ((read = input.Read(buffer, 0, buffer.Length)) > 0)
                {
                    output.Write(buffer, 0, read); done += read;
                    int pct = total > 0 ? (int)(done * 100 / total) : 0;
                    if (pct != lastPct) { lastPct = pct; progress($"Updating Syndicate to {tag}… {pct}%\nYour accounts and settings stay saved."); }
                }
            }
            string actual;
            using (var f = File.OpenRead(partial)) actual = Convert.ToHexString(SHA256.HashData(f));
            if (actual != expected) { File.Delete(partial); Log($"download of {tag} failed verification; not updating"); return false; }

            // Windows allows renaming a running EXE: move this one aside, put the new one in its place.
            if (string.Equals(target, self, StringComparison.OrdinalIgnoreCase))
            {
                string old = self + ".old";
                if (File.Exists(old)) File.Delete(old);
                File.Move(self, old);
                try { File.Move(partial, self); }
                catch { File.Move(old, self); throw; }
            }
            else File.Move(partial, target, true);

            Log($"updated {currentVersion} -> {tag}");
            progress($"Starting Syndicate {tag}…");
            releaseMutex();
            var start = new ProcessStartInfo(target) { UseShellExecute = false, WorkingDirectory = Path.GetDirectoryName(target)! };
            start.ArgumentList.Add(UpdatedFlag);
            Process.Start(start);
            return true;
        }
        catch (Exception e)
        {
            Log("update skipped: " + e.Message);
            // On a normal start, stay quiet and just open: offline players should not see an error every launch.
            if (explicitCheck) { progress("Couldn't check for updates (" + e.Message + ").\nStarting your current version…"); Thread.Sleep(2500); }
            return false;
        }
    }

    // Unpacked files of older versions are ~300 MB each; remove them once a newer one runs.
    public static void CleanOldVersions(string home, string currentVersion)
    {
        try
        {
            if (!Version.TryParse(currentVersion, out var current)) return;
            var parent = Path.GetDirectoryName(home)!;
            foreach (var dir in Directory.EnumerateDirectories(parent))
            {
                var name = Path.GetFileName(dir);
                if (!Version.TryParse(name, out var v) || v >= current || name.Count(c => c == '.') != 2) continue;
                // Accounts live in %LOCALAPPDATA%\ProjectTilted\UserData; never touch a folder
                // that still holds an old local database, just in case.
                var db = Path.Combine(dir, "RuntimeFiles", "Runtime", "Database");
                if (Directory.Exists(db) && Directory.EnumerateFileSystemEntries(db).Any()) continue;
                    try { Directory.Delete(dir, true); Log("removed old version folder " + name); } catch { }
            }
        }
        catch { }
    }

    static void CleanupSwapLeftovers()
    {
        try
        {
            string? self = Environment.ProcessPath;
            if (self == null) return;
            string old = self + ".old";
            for (int i = 0; i < 20 && File.Exists(old); i++)
            {
                try { File.Delete(old); } catch { Thread.Sleep(250); }
            }
            if (File.Exists(self + ".download")) File.Delete(self + ".download");
        }
        catch { }
    }

    static bool CanWrite(string dir)
    {
        try { var probe = Path.Combine(dir, ".syndicate-write-test"); File.WriteAllText(probe, "1"); File.Delete(probe); return true; }
        catch { return false; }
    }
}
