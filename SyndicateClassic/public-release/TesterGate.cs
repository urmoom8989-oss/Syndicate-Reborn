// Syndicate Tester launcher gate (only compiled into the Tester EXE: build-public-release.ps1 -Tester).
//
// The Tester EXE only opens for people with a Tester role in the Syndicate Discord, so sharing the EXE is
// useless. It runs in the bootstrapper before the encrypted launcher is unpacked:
//   * First start on a PC: "Sign in with Discord" opens the relay (/tester/start) in the browser. The relay checks
//     the Tester role with the bot and hands this PC a token (/tester/poll). The token only works on this PC.
//   * Every start, and every 20 minutes while Syndicate is open: /tester/check asks Discord again. A new tester only
//     needs the role; removing the role locks them out the next time it checks.
//   * No role (or not in the server): a message, then the Tester launcher closes and removes its program files
//     (the EXE, the encrypted bundle and the unpacked launcher). Accounts and settings are never touched.
//   * Updates: /tester/check also returns the newest tester build (published by MAKE TESTER RELEASE.cmd). A newer
//     one is downloaded, checked against its SHA-256 and swapped in, like the public updater does.
//   * The relay can't be reached: it keeps working for 24 hours after the last successful check, then waits.
using System;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Net.Http;
using System.Net.Http.Json;
using System.Security.Cryptography;
using System.Text;
using System.Text.Json;
using System.Threading;
using System.Windows;

static class TesterGate
{
    // Filled in by build-public-release.ps1 -Tester.
    const string Relay = "__TILTED_RELAY__";
    const int Build = __TILTED_TESTER_BUILD__;
    const string UpdatedFlag = "--tilted-updated";
    const string CheckFlag = "--check-updates";
    static readonly TimeSpan OfflineGrace = TimeSpan.FromHours(24);

    static string Folder => Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "SyndicateClassicTester");
    static string TokenFile => Path.Combine(Folder, "tester.json");
    static void Log(string text) { try { Directory.CreateDirectory(Folder); File.AppendAllText(Path.Combine(Folder, "tester.log"), $"{DateTime.UtcNow:O} {text}\n"); } catch { } }

    sealed class Saved { public string? Token { get; set; } public string? Name { get; set; } public DateTime LastOk { get; set; } }
    static Saved? Load() { try { return File.Exists(TokenFile) ? JsonSerializer.Deserialize<Saved>(File.ReadAllText(TokenFile)) : null; } catch { return null; } }
    static void Save(Saved s) { try { Directory.CreateDirectory(Folder); File.WriteAllText(TokenFile, JsonSerializer.Serialize(s)); } catch { } }

    // This PC: a hash of Windows' machine ID and the Windows user, so a copied token does not work elsewhere.
    static string Install()
    {
        string machine = "";
        try
        {
            using var key = Microsoft.Win32.RegistryKey.OpenBaseKey(Microsoft.Win32.RegistryHive.LocalMachine, Microsoft.Win32.RegistryView.Registry64).OpenSubKey(@"SOFTWARE\Microsoft\Cryptography");
            machine = key?.GetValue("MachineGuid") as string ?? "";
        }
        catch { }
        if (machine.Length == 0) machine = Environment.MachineName;
        return Convert.ToHexString(SHA256.HashData(Encoding.UTF8.GetBytes("tilted-install-v1:" + machine + "|" + Environment.UserName))).ToLowerInvariant()[..32];
    }

    static HttpClient Http(int seconds) { var h = new HttpClient { Timeout = TimeSpan.FromSeconds(seconds) }; h.DefaultRequestHeaders.UserAgent.ParseAdd("SyndicateClassic-Tester/" + Build); return h; }

    // Keeps the progress window responsive while waiting.
    static void Wait(int ms, Action<string> progress, string text) { for (int waited = 0; waited < ms; waited += 200) { progress(text); Thread.Sleep(200); } }

    // 0: carry on and open Syndicate. 1: stop (no access, cancelled, or a newer build was started).
    public static int Run(string[] args, Action<string> progress, Action releaseMutex)
    {
        bool explicitCheck = args.Contains(CheckFlag);
        string install = Install();
        var saved = Load();
        for (int attempt = 0; attempt < 2; attempt++)
        {
            if (saved?.Token == null)
            {
                var signedIn = SignIn(install, progress);
                if (signedIn == null) return 1;
                saved = signedIn;
            }
            progress("Checking your Tester access…");
            var (status, body) = Check(saved.Token!, install);
            if (status == 200 && body is JsonElement ok)
            {
                saved.LastOk = DateTime.UtcNow;
                if (ok.TryGetProperty("name", out var n) && n.ValueKind == JsonValueKind.String) saved.Name = n.GetString();
                Save(saved);
                Log("access ok for " + saved.Name);
                if (ok.TryGetProperty("latest", out var latest) && latest.ValueKind == JsonValueKind.Object && TryUpdate(latest, args, progress, releaseMutex)) return 1;
                if (explicitCheck) Wait(1800, progress, $"You have the newest Tester build (b{Build}).\nStarting Syndicate…");
                return 0;
            }
            if (status == 403) { SelfDestruct(Reason(body, "Your Discord account does not have the Tester role.")); return 1; }
            if (status == 401) { saved = null; TryDelete(TokenFile); continue; }   // not activated on this PC (or the token was removed): sign in again
            // The relay or Discord is unreachable.
            if (saved.LastOk > DateTime.UtcNow - OfflineGrace) { Log("relay unreachable; within the offline grace"); return 0; }
            HideAndTell("Syndicate Tester couldn't confirm your Tester role (the Syndicate server or Discord can't be reached).\n\nCheck your internet connection and open it again.");
            return 1;
        }
        return 1;
    }

    static string Reason(JsonElement? body, string fallback) =>
        body is JsonElement b && b.ValueKind == JsonValueKind.Object && b.TryGetProperty("reason", out var r) && r.ValueKind == JsonValueKind.String ? r.GetString()! : fallback;

    static (int Status, JsonElement? Body) Check(string token, string install)
    {
        try
        {
            using var http = Http(10);
            using var response = http.PostAsJsonAsync(Relay + "/tester/check", new { token, install, build = Build }).GetAwaiter().GetResult();
            JsonElement? body = null;
            try { body = response.Content.ReadFromJsonAsync<JsonElement>().GetAwaiter().GetResult(); } catch { }
            return ((int)response.StatusCode, body);
        }
        catch (Exception e) { Log("check failed: " + e.Message); return (0, null); }
    }

    // Discord sign-in in the browser; null when cancelled, timed out, or no Tester role (then it removes itself).
    static Saved? SignIn(string install, Action<string> progress)
    {
        var answer = MessageBox.Show("This is the Syndicate Tester build. It is only for members with the Tester role in the Syndicate Discord.\n\nPress OK to sign in with Discord in your browser. Syndicate only reads your Discord name and roles.",
            "Syndicate Tester", MessageBoxButton.OKCancel, MessageBoxImage.Information);
        if (answer != MessageBoxResult.OK) return null;
        string session = Convert.ToHexString(RandomNumberGenerator.GetBytes(16)).ToLowerInvariant();
        try { Process.Start(new ProcessStartInfo(Relay + "/tester/start?session=" + session + "&install=" + install) { UseShellExecute = true }); }
        catch (Exception e) { HideAndTell("Couldn't open your browser: " + e.Message); return null; }
        using var http = Http(10);
        var until = DateTime.UtcNow.AddMinutes(9);
        while (DateTime.UtcNow < until)
        {
            Wait(2000, progress, "Sign in with Discord in your browser…\nThis window continues by itself once you're confirmed as a Tester.");
            JsonElement result;
            try
            {
                using var response = http.PostAsJsonAsync(Relay + "/tester/poll", new { session, install }).GetAwaiter().GetResult();
                result = response.Content.ReadFromJsonAsync<JsonElement>().GetAwaiter().GetResult();
            }
            catch { continue; }
            string status = result.TryGetProperty("status", out var s) ? s.GetString() ?? "" : "";
            if (status == "pending") continue;
            if (status == "ok")
            {
                var saved = new Saved { Token = result.GetProperty("token").GetString(), Name = result.TryGetProperty("name", out var n) ? n.GetString() : null, LastOk = DateTime.UtcNow };
                Save(saved);
                Log("activated for " + saved.Name);
                progress("Welcome, " + saved.Name + "! Tester access confirmed.");
                return saved;
            }
            if (status == "denied") { SelfDestruct(Reason(result, "Your Discord account does not have the Tester role.")); return null; }
            break;   // expired
        }
        HideAndTell("The Discord sign-in timed out. Open Syndicate Tester again to retry.");
        return null;
    }

    // Newer tester build: download, verify, swap in under the same name and start it.
    static bool TryUpdate(JsonElement latest, string[] args, Action<string> progress, Action releaseMutex)
    {
        if (args.Contains(UpdatedFlag)) return false;
        try
        {
            int build = latest.GetProperty("build").GetInt32();
            if (build <= Build) return false;
            string url = latest.GetProperty("url").GetString() ?? "", expected = (latest.GetProperty("sha256").GetString() ?? "").ToUpperInvariant();
            if (!url.StartsWith("https://github.com/") || expected.Length != 64) return false;
            string self = Environment.ProcessPath ?? throw new IOException("Unknown program path");
            string partial = self + ".download";
            using (var dl = Http(1800))
            {
                using var response = dl.GetAsync(url, HttpCompletionOption.ResponseHeadersRead).GetAwaiter().GetResult();
                response.EnsureSuccessStatusCode();
                long total = response.Content.Headers.ContentLength ?? 0;
                using var input = response.Content.ReadAsStream();
                using var output = new FileStream(partial, FileMode.Create, FileAccess.Write, FileShare.None);
                var buffer = new byte[1 << 20]; long done = 0; int read, lastPct = -1;
                while ((read = input.Read(buffer, 0, buffer.Length)) > 0)
                {
                    output.Write(buffer, 0, read); done += read;
                    int pct = total > 0 ? (int)(done * 100 / total) : 0;
                    if (pct != lastPct) { lastPct = pct; progress($"New Tester build b{build}. Updating… {pct}%\nYour accounts and settings stay saved."); }
                }
            }
            string actual; using (var f = File.OpenRead(partial)) actual = Convert.ToHexString(SHA256.HashData(f));
            if (actual != expected) { TryDelete(partial); Log($"tester build b{build} failed verification"); return false; }
            string old = self + ".old";
            TryDelete(old);
            File.Move(self, old);
            try { File.Move(partial, self); } catch { File.Move(old, self); throw; }
            Log($"updated b{Build} -> b{build}");
            progress($"Starting Tester build b{build}…");
            releaseMutex();
            var start = new ProcessStartInfo(self) { UseShellExecute = false, WorkingDirectory = Path.GetDirectoryName(self)! };
            start.ArgumentList.Add(UpdatedFlag);
            Process.Start(start);
            return true;
        }
        catch (Exception e) { Log("tester update skipped: " + e.Message); return false; }
    }

    // While Syndicate is open: check again every 20 minutes. Role removed: close Syndicate and remove the Tester files.
    public static void Watch(Process child)
    {
        var thread = new Thread(() =>
        {
            while (!child.HasExited)
            {
                if (child.WaitForExit(TimeSpan.FromMinutes(20))) return;
                var saved = Load();
                if (saved?.Token == null) continue;
                var (status, body) = Check(saved.Token, Install());
                if (status == 200) { saved.LastOk = DateTime.UtcNow; Save(saved); continue; }
                if (status != 403 && status != 401) continue;   // offline: keep playing
                Log("access removed while open");
                try { child.Kill(entireProcessTree: true); } catch { }
                SelfDestruct(Reason(body, "Your Tester access was removed."));
                return;
            }
        }) { IsBackground = true, Name = "Syndicate tester check" };
        thread.SetApartmentState(ApartmentState.STA);   // for the message box
        thread.Start();
    }

    static void HideAndTell(string text) => MessageBox.Show(text, "Syndicate Tester", MessageBoxButton.OK, MessageBoxImage.Warning);

    // No Tester role: say why, then remove the Tester program once this process has exited.
    // Removes: this EXE, the encrypted bundles and unpacked launchers under SyndicateClassicTester, and the token.
    // Never removes accounts or settings (%LOCALAPPDATA%\ProjectTilted) or the local database.
    static void SelfDestruct(string reason)
    {
        Log("no access: " + reason);
        MessageBox.Show(reason + "\n\nThe Syndicate Tester launcher is only for members with the Tester role in the Syndicate Discord. It will now close and remove itself.\n\nYour accounts and settings are not deleted. The normal Syndicate download still works.",
            "Syndicate Tester", MessageBoxButton.OK, MessageBoxImage.Stop);
        TryDelete(TokenFile);
        var commands = new StringBuilder("ping 127.0.0.1 -n 5 >nul");
        string? self = Environment.ProcessPath;
        if (self != null) commands.Append(" & del /f /q \"" + self + "\" & del /f /q \"" + self + ".old\"");
        try
        {
            foreach (var version in Directory.Exists(Folder) ? Directory.GetDirectories(Folder) : Array.Empty<string>())
            {
                commands.Append(" & del /f /q \"" + Path.Combine(version, "SyndicateClassic.encrypted") + "\" \"" + Path.Combine(version, "SyndicateClassic.encrypted.sha256") + "\"");
                commands.Append(" & rmdir /s /q \"" + Path.Combine(version, "RuntimeFiles", "Launcher") + "\"");
            }
        }
        catch { }
        try
        {
            // Arguments (not ArgumentList): cmd does not understand the \" escaping ArgumentList would add.
            var start = new ProcessStartInfo("cmd.exe", "/c " + commands) { UseShellExecute = false, CreateNoWindow = true, WindowStyle = ProcessWindowStyle.Hidden };
            Process.Start(start);
        }
        catch (Exception e) { Log("could not remove the tester files: " + e.Message); }
    }

    static void TryDelete(string path) { try { if (File.Exists(path)) File.Delete(path); } catch { } }
}
