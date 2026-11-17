using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Net.Http;
using System.Reflection;
using System.Text;
using System.Text.Json;
using System.Threading.Tasks;

namespace SyndicateLauncher.Services;

/// <summary>
/// Fire-and-forget error reporting to a Discord webhook, so the team sees the errors players actually hit -
/// and the real logs behind them - without having to ask anyone. The webhook URL is read once (in order) from:
///   1. the SYNDICATE_ERROR_WEBHOOK environment variable,
///   2. "error-webhook.txt" next to the launcher exe, or in its Runtime folder,
///   3. %LOCALAPPDATA%\SyndicateLauncher\error-webhook.txt.
/// If none is set, nothing is sent. Every call is fully guarded, so a reporting failure can never affect the
/// launcher. The same error title is sent at most once per launcher run, so a retry loop can't spam the channel.
/// No account names are sent - only the error, the season, the build version, the launcher version, and the
/// launch/game log files (the session log folder plus Fortnite's own FortniteGame.log), which is what a game
/// crash like "Erbium stopped while starting" actually explains itself in.
/// </summary>
public static class ErrorReporter
{
    private static readonly HttpClient Http = new() { Timeout = TimeSpan.FromSeconds(20) };
    private static readonly object Gate = new();
    private static readonly HashSet<string> Sent = new(StringComparer.OrdinalIgnoreCase);
    private static string? _cached;
    private static bool _looked;

    private const int MaxFileBytes = 60 * 1024;   // tail of each log file
    private const int MaxFiles = 8;

    private static string? Webhook()
    {
        lock (Gate)
        {
            if (_looked) return _cached;
            _looked = true;
            try
            {
                var env = Environment.GetEnvironmentVariable("SYNDICATE_ERROR_WEBHOOK");
                if (IsWebhook(env)) return _cached = env!.Trim();
                foreach (var path in CandidatePaths())
                {
                    try
                    {
                        if (!File.Exists(path)) continue;
                        var text = File.ReadAllText(path).Trim();
                        if (IsWebhook(text)) return _cached = text;
                    }
                    catch { }
                }
            }
            catch { }
            return _cached;
        }
    }

    private static bool IsWebhook(string? s) =>
        !string.IsNullOrWhiteSpace(s) && s!.Trim().StartsWith("https://", StringComparison.OrdinalIgnoreCase)
        && s.Contains("/api/webhooks/", StringComparison.OrdinalIgnoreCase);

    private static IEnumerable<string> CandidatePaths()
    {
        string? exeDir = null;
        try { exeDir = AppContext.BaseDirectory; } catch { }
        if (!string.IsNullOrEmpty(exeDir))
        {
            yield return Path.Combine(exeDir, "error-webhook.txt");
            yield return Path.Combine(exeDir, "Runtime", "error-webhook.txt");
        }
        string? local = null;
        try { local = Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData); } catch { }
        if (!string.IsNullOrEmpty(local))
            yield return Path.Combine(local, "SyndicateLauncher", "error-webhook.txt");
    }

    private static string Version()
    {
        try { return Assembly.GetExecutingAssembly().GetName().Version?.ToString() ?? "?"; }
        catch { return "?"; }
    }

    /// <summary>Report one error to the webhook, attaching the session logs and Fortnite's own log. Never throws.</summary>
    public static void Report(string title, string body, string context, string? logFolder = null)
    {
        try
        {
            var hook = Webhook();
            if (string.IsNullOrWhiteSpace(hook)) return;
            lock (Gate) { if (!Sent.Add(title ?? "")) return; }

            string content =
                "**" + Clip(title ?? "(no title)", 240) + "**\n"
                + Clip(context, 350) + "  |  launcher " + Version() + "\n"
                + "```\n" + Clip(body, 1200) + "\n```";

            // Collect the logs that actually explain a game crash.
            var attachments = CollectLogs(logFolder);

            _ = Task.Run(async () =>
            {
                try
                {
                    using var form = new MultipartFormDataContent();
                    var payload = JsonSerializer.Serialize(new
                    {
                        content,
                        allowed_mentions = new { parse = Array.Empty<string>() }
                    });
                    form.Add(new StringContent(payload, Encoding.UTF8, "application/json"), "payload_json");

                    int i = 0;
                    foreach (var (name, bytes) in attachments)
                    {
                        form.Add(new ByteArrayContent(bytes), "files[" + i + "]", name);
                        i++;
                    }

                    using var _ = await Http.PostAsync(hook, form).ConfigureAwait(false);
                }
                catch { }
            });
        }
        catch { }
    }

    // Returns (fileName, tailBytes) for the session log folder's files plus Fortnite's newest FortniteGame*.log.
    private static List<(string Name, byte[] Bytes)> CollectLogs(string? logFolder)
    {
        var result = new List<(string, byte[])>();
        try
        {
            var paths = new List<string>();

            if (!string.IsNullOrWhiteSpace(logFolder) && Directory.Exists(logFolder))
            {
                foreach (var f in Directory.EnumerateFiles(logFolder, "*.log"))
                    paths.Add(f);
            }

            // Fortnite's own log - where an engine crash / Erbium injection failure is recorded.
            try
            {
                var saved = Path.Combine(
                    Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),
                    "FortniteGame", "Saved", "Logs");
                var newestGame = NewestIn(saved, "FortniteGame*.log");
                if (newestGame is not null) paths.Add(newestGame);
            }
            catch { }

            // Backend logs (matchmaker + Node API) live in %APPDATA%\SyndicateLauncher\Logs, not the session
            // folder - this is where "Matchmaking exited during startup" / "backend exited" record why.
            try
            {
                var backendLogs = Path.Combine(
                    Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData),
                    "SyndicateLauncher", "Logs");
                foreach (var name in new[] { "matchmaker.log", "syndicate-backend.log" })
                {
                    var p = Path.Combine(backendLogs, name);
                    if (File.Exists(p)) paths.Add(p);
                }
            }
            catch { }

            // Erbium's own server log (stdout.log.<pid>.log), written in the Runtime folder - where
            // "Erbium stopped while starting" records WHY it stopped. Runtime sits next to the exe for a
            // folder install, or under %LOCALAPPDATA%\SyndicateLauncher\Runtime for an embedded one.
            try
            {
                string? newestErbium = null;
                DateTime newestWhen = DateTime.MinValue;
                foreach (var root in RuntimeRoots())
                {
                    if (!Directory.Exists(root)) continue;
                    foreach (var f in Directory.EnumerateFiles(root, "stdout.log*.log", SearchOption.TopDirectoryOnly))
                    {
                        var when = File.GetLastWriteTimeUtc(f);
                        if (when > newestWhen) { newestWhen = when; newestErbium = f; }
                    }
                }
                if (newestErbium is not null) paths.Add(newestErbium);
            }
            catch { }

            foreach (var path in paths.Distinct(StringComparer.OrdinalIgnoreCase).Take(MaxFiles))
            {
                var tail = TailBytes(path, MaxFileBytes);
                if (tail.Length > 0) result.Add((Path.GetFileName(path), tail));
            }
        }
        catch { }
        return result;
    }

    private static string? NewestIn(string dir, string pattern)
    {
        try
        {
            if (!Directory.Exists(dir)) return null;
            return Directory.EnumerateFiles(dir, pattern, SearchOption.TopDirectoryOnly)
                .OrderByDescending(File.GetLastWriteTimeUtc)
                .FirstOrDefault();
        }
        catch { return null; }
    }

    // The folders Erbium's stdout.log sits in: Runtime next to the exe (folder install), and the extracted
    // Runtime under %LOCALAPPDATA%\SyndicateLauncher\Runtime\<hash> (embedded install) plus that parent.
    private static IEnumerable<string> RuntimeRoots()
    {
        string? exeDir = null;
        try { exeDir = AppContext.BaseDirectory; } catch { }
        if (!string.IsNullOrEmpty(exeDir))
            yield return Path.Combine(exeDir, "Runtime");

        string? local = null;
        try { local = Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData); } catch { }
        if (!string.IsNullOrEmpty(local))
        {
            var runtime = Path.Combine(local, "SyndicateLauncher", "Runtime");
            yield return runtime;
            IEnumerable<string> subs = Array.Empty<string>();
            try { if (Directory.Exists(runtime)) subs = Directory.EnumerateDirectories(runtime); }
            catch { subs = Array.Empty<string>(); }
            foreach (var s in subs) yield return s;
        }
    }

    private static byte[] TailBytes(string path, int max)
    {
        try
        {
            using var stream = new FileStream(path, FileMode.Open, FileAccess.Read,
                FileShare.ReadWrite | FileShare.Delete);
            long length = stream.Length;
            if (length <= max)
            {
                var all = new byte[(int)length];
                int read = 0, n;
                while (read < all.Length && (n = stream.Read(all, read, all.Length - read)) > 0) read += n;
                return read == all.Length ? all : all[..read];
            }
            stream.Seek(length - max, SeekOrigin.Begin);
            var buf = new byte[max];
            int got = 0, m;
            while (got < buf.Length && (m = stream.Read(buf, got, buf.Length - got)) > 0) got += m;
            return got == buf.Length ? buf : buf[..got];
        }
        catch { return Array.Empty<byte>(); }
    }

    private static string Clip(string? s, int max) =>
        string.IsNullOrEmpty(s) ? "" : (s!.Length <= max ? s : s.Substring(0, max) + " ...");
}
