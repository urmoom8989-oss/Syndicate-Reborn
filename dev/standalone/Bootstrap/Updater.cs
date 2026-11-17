// Syndicate standalone updater. On a normal start it does a quick, silent check so reopening the exe installs
// a newer version right away; with --check-updates it waits longer and shows every step. It asks GitHub
// Releases for the newest version, and if the release tag is a higher version than this exe, downloads the new
// .exe, verifies it against the release's matching .sha256 file, renames the running exe aside, drops the new
// one in under the same name, and starts it. Any failure (offline, GitHub down, bad hash) just starts the
// current version. Saved accounts live in %APPDATA%\SyndicateLauncher and are never touched.
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
    // The repos the standalone exe is published to. Every one is checked and the highest version wins, so a
    // release in either repo reaches every installed copy. The first one is Sact's release repo; the second is
    // the original channel, kept so copies keep updating from wherever a release goes.
    static readonly string[] Repositories = { "Sacttt/Syndicate-Launcher", "d4shyb0i/Syndicate-Launcher" };
    static string Repository => string.Join(", ", Repositories);
    const string UpdatedFlag = "--syndicate-updated";
    const string CheckFlag = "--check-updates";

    static string LogPath => Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "SyndicateStandalone", "updater.log");
    static void Log(string text)
    {
        try { Directory.CreateDirectory(Path.GetDirectoryName(LogPath)!); File.AppendAllText(LogPath, $"{DateTime.UtcNow:O} {text}\n"); } catch { }
    }

    // A freshly updated copy waits for the old one to close instead of saying "already running".
    public static int MutexWaitMs(string[] args) => args.Contains(CheckFlag) ? 120000 : args.Contains(UpdatedFlag) ? 20000 : 0;

    public static bool WasJustUpdated(string[] args) => args.Contains(UpdatedFlag);

    // Returns true when a newer version was started and this process should exit.
    public static bool TryUpdate(string currentVersion, string[] args, Action<string> progress, Action releaseMutex)
    {
        CleanupSwapLeftovers();
        if (args.Contains(UpdatedFlag)) { Log("running updated version " + currentVersion); return false; }
        bool explicitCheck = args.Contains(CheckFlag);
        if (Repositories.Length == 0)
        {
            Log("updates are off in this build (no repository)");
            if (explicitCheck) { progress("Updates aren't set up for this copy of Syndicate.\nStarting your current version…"); Thread.Sleep(2500); }
            return false;
        }
        try
        {
            if (explicitCheck) progress("Checking for updates…");
            using var http = new HttpClient { Timeout = TimeSpan.FromSeconds(explicitCheck ? 8 : 4) };
            http.DefaultRequestHeaders.UserAgent.ParseAdd("Syndicate-Updater/" + currentVersion);
            http.DefaultRequestHeaders.Accept.ParseAdd("application/vnd.github+json");
            // Newest release across all repos that has an installable exe (+ .sha256).
            JsonDocument doc = null; string tag = ""; Version latest = null;
            foreach (var repo in Repositories)
            {
                try
                {
                    var candidate = JsonDocument.Parse(http.GetStringAsync($"https://api.github.com/repos/{repo}/releases/latest").GetAwaiter().GetResult());
                    string candidateTag = candidate.RootElement.GetProperty("tag_name").GetString() ?? "";
                    bool hasExe = candidate.RootElement.GetProperty("assets").EnumerateArray()
                        .Any(a => (a.GetProperty("name").GetString() ?? "").EndsWith(".exe", StringComparison.OrdinalIgnoreCase));
                    if (hasExe && Version.TryParse(candidateTag.TrimStart('v', 'V'), out var v) && (latest == null || v > latest))
                    { doc?.Dispose(); doc = candidate; tag = candidateTag; latest = v; }
                    else candidate.Dispose();
                }
                catch (Exception e) { Log($"could not check {repo}: {e.Message}"); }
            }
            using var held = doc;
            if (doc == null) throw new IOException("no release with an exe found");
            var root = doc.RootElement;
            if (!Version.TryParse(currentVersion, out var current) || latest <= current)
            {
                Log($"checked {Repository}: latest release is {tag}, running {currentVersion}; nothing to install");
                if (explicitCheck) { progress($"You have the latest version (v{currentVersion}).\nStarting Syndicate…"); Thread.Sleep(2000); }
                return false;
            }
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
                dl.DefaultRequestHeaders.UserAgent.ParseAdd("Syndicate-Updater/" + currentVersion);
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
            if (explicitCheck) { progress("Couldn't check for updates (" + e.Message + ").\nStarting your current version…"); Thread.Sleep(2500); }
            return false;
        }
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
