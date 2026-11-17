// Syndicate © 2026 Syndicate. All rights reserved. Proprietary — no copying, distribution, or derivative works. See LICENSE.
using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Net.Http;
using System.Text;
using System.Text.Json;
using System.Text.RegularExpressions;
using System.Threading.Tasks;

namespace SyndicateClassic;

// Error reports to the Syndicate developers' Discord.
//
// When a season's match server fails to start ("Playlist could not start: Erbium exited before
// the island was ready", a freeze, a timeout), the launcher sends what is needed to see why:
// the season and build, game mode, launcher version, Windows version, the error, the last
// launcher log lines, and the end of the match server's own logs (stdout.log.<pid>.log,
// server-game.log, matchmaker errors). The summary is the Discord message; the logs are
// attached as error-report.txt.
//
// Privacy: Windows user name, PC name, user folder paths, e-mail addresses and IP addresses
// (except 127.x) are replaced before anything is sent. Each PC gets a random report id
// (UserData\report-id.txt) so repeated reports from one PC can be grouped; it is not tied to
// an account. Settings > Match data > "Send error reports" turns it off.
//
// Where it goes: Syndicate's relay (POST /error); the Discord webhook stays a secret of the relay,
// so it is never in the launcher, the public release or the repo. A developer PC can post
// straight to a channel instead by putting a webhook URL in
// %LOCALAPPDATA%\ProjectTilted\UserData\discord-errors-webhook.txt (never committed).
// Limits: one report per 3 minutes, and the same error at most once per 30 minutes.
internal static class ErrorReporter
{
    private const string RelayUrl = "https://your-relay.example.workers.dev/error";
    private static readonly HttpClient Http = new() { Timeout = TimeSpan.FromSeconds(30) };
    private static readonly object Gate = new();
    private static readonly LinkedList<string> Recent = new();
    private static DateTime _lastSent = DateTime.MinValue;
    private static readonly Dictionary<string, DateTime> SentErrors = new();

    private static string UserData => Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "ProjectTilted", "UserData");

    // Every launcher log line (MainWindow.Log) also lands here: the last 80 go with a report.
    internal static void Remember(string line)
    {
        lock (Gate)
        {
            Recent.AddLast($"[{DateTime.Now:HH:mm:ss}] {line}");
            while (Recent.Count > 80) Recent.RemoveFirst();
        }
    }

    internal static bool Enabled(LauncherPaths? paths)
    {
        try { return MatchTelemetry.Load(MatchTelemetry.Folder(paths)).errors; } catch { return true; }
    }

    // Fire and forget: never throws, never blocks the caller. Result goes to the launcher error log.
    internal static void ServerStartFailed(LauncherPaths paths, string build, string mode, Exception error)
    {
        if (!Enabled(paths)) return;
        string message = error.GetBaseException().Message;
        lock (Gate)
        {
            var now = DateTime.UtcNow;
            if (now - _lastSent < TimeSpan.FromMinutes(3)) return;
            string key = build + "|" + Regex.Replace(message, @"\d+", "#");
            if (SentErrors.TryGetValue(key, out var at) && now - at < TimeSpan.FromMinutes(30)) return;
            SentErrors[key] = now; _lastSent = now;
        }
        string[] launcherLines;
        lock (Gate) launcherLines = Recent.ToArray();
        _ = Task.Run(async () =>
        {
            try
            {
                var (summary, log) = Build(paths, build, mode, message, launcherLines);
                string result = await Send(summary, log);
                Note("Error report: " + result);
            }
            catch (Exception failure) { Note("Error report not sent: " + failure.Message); }
        });
    }

    // A match server that died during a match (its log has the crash reporter's lines). Only start failures were
    // reported before, so "5-9 bots, then the server crashed" never reached the developers. Same throttling.
    internal static void ServerCrashed(LauncherPaths paths, string build, string mode, string stdoutLog)
    {
        if (!Enabled(paths)) return;
        int bots = 0;
        try { foreach (var l in Tail(stdoutLog, 400)) { var m = Regex.Match(l, @"Roaming with (\d+) bots"); if (m.Success) bots = int.Parse(m.Groups[1].Value); } } catch { }
        ServerStartFailed(paths, build, mode, new Exception($"The match server crashed during the match ({bots} bots were in). See {Path.GetFileName(stdoutLog)}."));
    }

    private static (string Summary, string Log) Build(LauncherPaths paths, string build, string mode, string message, string[] launcherLines)
    {
        string runtime = Path.Combine(paths.Root, "Runtime", "Server");
        string version = System.Reflection.Assembly.GetExecutingAssembly().GetName().Version?.ToString(3) ?? "?";
        long ramGb = 0;
        try { ramGb = (long)Math.Round(GC.GetGCMemoryInfo().TotalAvailableMemoryBytes / 1073741824.0); } catch { }
        string season = SeasonOf(build);
        var summary = new StringBuilder();
        summary.AppendLine($"Match server failed: {season} ({build}), {mode}");
        summary.AppendLine($"Syndicate v{version} | Windows {Environment.OSVersion.Version} | {Environment.ProcessorCount} threads | {ramGb} GB RAM | report id {ReportId()}");
        summary.AppendLine("Error: " + message);
        string? stdout = Newest(runtime, "stdout.log.*.log", f => !f.EndsWith(".stderr.log", StringComparison.OrdinalIgnoreCase));
        if (stdout != null)
        {
            var last = Tail(stdout, 6).Where(l => l.Trim().Length > 0).ToArray();
            if (last.Length > 0) summary.AppendLine("Server log ends with:").AppendLine(string.Join("\n", last.Select(l => "  " + Cut(l, 160))));
        }

        var log = new StringBuilder();
        log.AppendLine(summary.ToString());
        void Section(string title, IEnumerable<string> lines)
        {
            log.AppendLine("===== " + title + " =====");
            foreach (var l in lines) log.AppendLine(l);
            log.AppendLine();
        }
        Section("Launcher log (last lines)", launcherLines);
        if (stdout != null) Section("Erbium " + Path.GetFileName(stdout) + " (last 250 lines)", Tail(stdout, 250));
        var stderr = stdout != null && File.Exists(stdout + ".stderr.log") ? stdout + ".stderr.log" : null;
        if (stderr != null && new FileInfo(stderr).Length > 0) Section(Path.GetFileName(stderr), Tail(stderr, 60));
        string gameLog = Path.Combine(runtime, "server-game.log");
        if (File.Exists(gameLog)) Section("server-game.log (last 150 lines)", Tail(gameLog, 150));
        foreach (var name in new[] { "matchmaker-startup-error.log", "playlist-error.json" })
        {
            var f = Path.Combine(runtime, name);
            if (File.Exists(f) && File.GetLastWriteTimeUtc(f) > DateTime.UtcNow.AddMinutes(-15)) Section(name, Tail(f, 60));
        }
        string botLog = Path.Combine(runtime, "ready.pid.bot.log");
        if (File.Exists(botLog) && File.GetLastWriteTimeUtc(botLog) > DateTime.UtcNow.AddMinutes(-15)) Section("ready.pid.bot.log (last 40 lines)", Tail(botLog, 40));
        string errors = Path.Combine(CrashGuard.LogFolder, "launcher-errors.log");
        if (File.Exists(errors) && File.GetLastWriteTimeUtc(errors) > DateTime.UtcNow.AddMinutes(-15)) Section("launcher-errors.log (last 40 lines)", Tail(errors, 40));
        foreach (var name in new[] { "challenge-data", "cosmetics" })
        {
            var dir = Path.Combine(runtime, name);
            var files = Directory.Exists(dir) ? Directory.GetFiles(dir).Select(f => Path.GetFileName(f)).OrderBy(n => n).ToArray() : Array.Empty<string>();
            Section(name + " folder", new[] { files.Length == 0 ? "(empty or missing)" : string.Join(", ", files) });
        }
        return (Redact(summary.ToString().TrimEnd()), Redact(log.ToString()));
    }

    private static string SeasonOf(string build) => build switch
    {
        "1.7.2" => "Pre-season",
        "1.8.2" or "1.8" => "Season 1",
        "1.11" => "Season 2",
        "11.00" => "Chapter 2 Season 1",
        "12.50" => "Chapter 2 Season 2",
        "19.00" => "Chapter 3 Season 1",
        _ => int.TryParse(build.Split('.')[0], out int major) ? (major == 10 ? "Season X" : "Season " + major) : "Season ?",
    };

    private static async Task<string> Send(string summary, string log)
    {
        string? direct = null;
        try
        {
            var file = Path.Combine(UserData, "discord-errors-webhook.txt");
            if (File.Exists(file))
            {
                var url = File.ReadAllText(file).Trim();
                if (url.StartsWith("https://discord.com/api/webhooks/", StringComparison.OrdinalIgnoreCase) || url.StartsWith("https://discordapp.com/api/webhooks/", StringComparison.OrdinalIgnoreCase)) direct = url;
            }
        }
        catch { }
        HttpResponseMessage response;
        if (direct != null)
        {
            using var form = new MultipartFormDataContent();
            form.Add(new StringContent(JsonSerializer.Serialize(new
            {
                username = "Syndicate error reports",
                content = "```\n" + Cut(summary.Replace("`", "'").Replace("@", "'"), 1800) + "\n```",
                allowed_mentions = new { parse = Array.Empty<string>() },
            }), Encoding.UTF8, "application/json"), "payload_json");
            var bytes = Encoding.UTF8.GetBytes(Cut(log, 250000));
            var attachment = new ByteArrayContent(bytes);
            attachment.Headers.ContentType = new System.Net.Http.Headers.MediaTypeHeaderValue("text/plain");
            form.Add(attachment, "files[0]", "error-report.txt");
            response = await Http.PostAsync(direct, form);
            return response.IsSuccessStatusCode ? "sent to the Discord channel" : "Discord answered " + (int)response.StatusCode;
        }
        var body = JsonSerializer.Serialize(new { source = "syndicate", summary, log = Cut(log, 250000) });
        response = await Http.PostAsync(RelayUrl, new StringContent(body, Encoding.UTF8, "application/json"));
        return response.IsSuccessStatusCode ? "sent through the relay" : "relay answered " + (int)response.StatusCode + " " + await response.Content.ReadAsStringAsync();
    }

    // ---- helpers ----
    private static string ReportId()
    {
        try
        {
            Directory.CreateDirectory(UserData);
            var file = Path.Combine(UserData, "report-id.txt");
            if (File.Exists(file)) { var id = File.ReadAllText(file).Trim(); if (Regex.IsMatch(id, "^[a-f0-9]{8}$")) return id; }
            var created = Guid.NewGuid().ToString("N")[..8];
            File.WriteAllText(file, created);
            return created;
        }
        catch { return "unknown"; }
    }

    private static string? Newest(string folder, string pattern, Func<string, bool>? keep = null)
    {
        try
        {
            return Directory.EnumerateFiles(folder, pattern).Where(f => keep == null || keep(f))
                .OrderByDescending(f => File.GetLastWriteTimeUtc(f)).FirstOrDefault();
        }
        catch { return null; }
    }

    // Last lines of a file that may still be open for writing (only the last 256 KB is read).
    internal static string[] Tail(string file, int lines)
    {
        try
        {
            using var stream = new FileStream(file, FileMode.Open, FileAccess.Read, FileShare.ReadWrite | FileShare.Delete);
            long start = Math.Max(0, stream.Length - 262144);
            stream.Seek(start, SeekOrigin.Begin);
            using var reader = new StreamReader(stream, Encoding.UTF8);
            var all = reader.ReadToEnd().Replace("\r", "").Split('\n');
            if (start > 0 && all.Length > 1) all = all[1..];
            return all.Skip(Math.Max(0, all.Length - lines)).Select(l => Cut(StripAnsi(l), 400)).ToArray();
        }
        catch (Exception error) { return new[] { "(could not read: " + error.Message + ")" }; }
    }

    private static string StripAnsi(string s) => Regex.Replace(s, @"\x1B\[[0-9;]*[A-Za-z]", "");
    private static string Cut(string s, int max) => s.Length <= max ? s : s[..max] + "…";

    internal static string Redact(string text)
    {
        try
        {
            string profile = Environment.GetFolderPath(Environment.SpecialFolder.UserProfile);
            if (profile.Length > 3)
            {
                text = text.Replace(profile, "%USERPROFILE%", StringComparison.OrdinalIgnoreCase);
                text = text.Replace(profile.Replace('\\', '/'), "%USERPROFILE%", StringComparison.OrdinalIgnoreCase);
            }
            // Other user folders (C:\Users\Name\...) and the plain user / PC names.
            text = Regex.Replace(text, @"([A-Za-z]:[\\/]+Users[\\/]+)[^\\/""'\r\n]+", "$1<user>", RegexOptions.IgnoreCase);
            foreach (var name in new[] { Environment.UserName, Environment.MachineName })
                if (!string.IsNullOrWhiteSpace(name) && name.Length >= 3)
                    text = Regex.Replace(text, @"(?<![A-Za-z0-9])" + Regex.Escape(name) + @"(?![A-Za-z0-9])", "<name>", RegexOptions.IgnoreCase);
            text = Regex.Replace(text, @"[A-Za-z0-9._%+-]+@[A-Za-z0-9.-]+\.[A-Za-z]{2,}", "<email>");
            text = Regex.Replace(text, @"\b(?!127\.)(?!0\.0\.0\.0\b)(\d{1,3})\.(\d{1,3})\.(\d{1,3})\.(\d{1,3})\b", m =>
                m.Groups.Cast<Group>().Skip(1).All(g => int.Parse(g.Value) <= 255) ? "<ip>" : m.Value);
            // Bearer / exchange tokens and webhook URLs never belong in a report.
            text = Regex.Replace(text, @"(eg1~|bearer\s+)[A-Za-z0-9._~\-]+", "$1<token>", RegexOptions.IgnoreCase);
            text = Regex.Replace(text, @"https://(discord|discordapp)\.com/api/webhooks/\S+", "<webhook>", RegexOptions.IgnoreCase);
        }
        catch { }
        return text;
    }

    private static void Note(string line)
    {
        try
        {
            Directory.CreateDirectory(CrashGuard.LogFolder);
            File.AppendAllText(Path.Combine(CrashGuard.LogFolder, "error-reports.log"), $"[{DateTime.Now:yyyy-MM-dd HH:mm:ss}] {line}{Environment.NewLine}");
        }
        catch { }
    }
}