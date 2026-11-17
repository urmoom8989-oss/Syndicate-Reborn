using System;
using System.Diagnostics;
using System.IO;
using System.Globalization;
using System.Linq;
using System.Threading.Tasks;

namespace SyndicateLauncher.Services;

public static class StartupReadiness
{
    private static string StandardLogDirectory => Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "FortniteGame", "Saved", "Logs");

    public static bool IsCurrentStartup(string firstLine, DateTime startedUtc)
    {
        const string prefix = "Log file open, ";
        return firstLine.StartsWith(prefix, StringComparison.Ordinal) &&
            DateTime.TryParseExact(firstLine[prefix.Length..].Trim(), new[] { "MM/dd/yy HH:mm:ss", "M/d/yy HH:mm:ss" },
                CultureInfo.InvariantCulture, DateTimeStyles.AssumeLocal, out var opened) &&
            opened.ToUniversalTime() >= startedUtc.AddSeconds(-1);
    }

    private static string? FindLog(string requested, DateTime startedUtc)
    {
        // Concurrent Fortnite processes use FortniteGame.log, FortniteGame_2.log,
        // etc. A server writing its old log must never satisfy player readiness.
        var standard = Directory.Exists(StandardLogDirectory)
            ? Directory.EnumerateFiles(StandardLogDirectory, "FortniteGame*.log")
                .Where(p => !Path.GetFileName(p).Contains("-backup-", StringComparison.OrdinalIgnoreCase))
            : Enumerable.Empty<string>();
        foreach (var candidate in standard.Append(requested).Distinct(StringComparer.OrdinalIgnoreCase))
        {
            try
            {
                if (File.GetLastWriteTimeUtc(candidate) < startedUtc) continue;
                using var stream = new FileStream(candidate, FileMode.Open, FileAccess.Read, FileShare.ReadWrite | FileShare.Delete);
                using var reader = new StreamReader(stream);
                if (IsCurrentStartup(reader.ReadLine() ?? "", startedUtc)) return candidate;
            }
            catch (IOException) { }
        }
        return null;
    }

    public static async Task SaveLatestAsync(Process process, string destination)
    {
        try
        {
            var source = FindLog(destination, process.StartTime.ToUniversalTime());
            if (source is null || source.Equals(destination, StringComparison.OrdinalIgnoreCase)) return;
            using var stream = new FileStream(source, FileMode.Open, FileAccess.Read, FileShare.ReadWrite | FileShare.Delete);
            using var reader = new StreamReader(stream);
            await File.WriteAllTextAsync(destination, await reader.ReadToEndAsync());
        }
        catch (IOException) { }
        catch (InvalidOperationException) { }
        catch (UnauthorizedAccessException) { }
    }
    public static bool IsEngineReady(string log) => log.Contains("(Engine Initialization) Total time:", StringComparison.Ordinal);

    public static bool IsReady(string log) =>
        IsEngineReady(log) &&
        log.Split('\n').Any(line => line.Contains("[UOnlineAccountCommon::ContinueLoggingIn]", StringComparison.Ordinal) &&
            line.Contains("(Completed)", StringComparison.Ordinal));

    /// <summary>True when Fortnite gave up on the local login and showed the email/password screen.</summary>
    public static bool IsLoginAborted(string log) =>
        log.Contains("[UOnlineAccountCommon::AbortLoggingIn]", StringComparison.Ordinal);

    public static Task WaitAsync(Process process, string logPath, Action<string> status) => WaitAsync(process, logPath, status, false);

    /// <summary>
    /// Chapter 1: watches the player's log until the local login completes. When Fortnite aborts it (a request
    /// slipped past cobalt to Epic: "Profile Query Failed" or the sign-in screen), throws LoginAbortedException so
    /// GameLauncher retries with a fresh exchange code. Unlike WaitAsync it never fails the launch otherwise: a
    /// build that does not log the completed login, or a slow start, just stops being watched after the limit.
    /// </summary>
    public static async Task WatchLoginAsync(Process process, string logPath, TimeSpan limit)
    {
        var timer = Stopwatch.StartNew();
        var started = process.StartTime.ToUniversalTime();
        while (timer.Elapsed < limit && !process.HasExited)
        {
            try
            {
                var activeLog = FindLog(logPath, started);
                if (activeLog is not null)
                {
                    using var stream = new FileStream(activeLog, FileMode.Open, FileAccess.Read, FileShare.ReadWrite | FileShare.Delete);
                    using var reader = new StreamReader(stream);
                    var contents = await reader.ReadToEndAsync();
                    if (IsReady(contents)) return;
                    if (IsLoginAborted(contents))
                    {
                        await SaveLatestAsync(process, logPath);
                        throw new LoginAbortedException(logPath);
                    }
                }
            }
            catch (IOException) { }
            await Task.Delay(500);
        }
    }
    public static Task WaitForEngineAsync(Process process, string logPath, Action<string> status) => WaitAsync(process, logPath, status, true);

    private static async Task WaitAsync(Process process, string logPath, Action<string> status, bool engineOnly)
    {
        var timer = Stopwatch.StartNew();
        var started = process.StartTime.ToUniversalTime();
        while (timer.Elapsed < TimeSpan.FromSeconds(150))
        {
            if (process.HasExited)
            {
                await SaveLatestAsync(process, logPath);
                throw new InvalidOperationException(StartupFailure.ReadDiagnosis(logPath) ??
                    ("Fortnite exited during startup (0x" + process.ExitCode.ToString("X8") + "). Log: " + logPath));
            }
            try
            {
                // Some shipping builds ignore -abslog. Accept their standard log only
                // when it was written by a new startup, and retain a session snapshot.
                var activeLog = FindLog(logPath, started);
                if (activeLog is not null)
                {
                    using var stream = new FileStream(activeLog, FileMode.Open, FileAccess.Read, FileShare.ReadWrite | FileShare.Delete);
                    using var reader = new StreamReader(stream);
                    var contents = await reader.ReadToEndAsync();
                    var ready = engineOnly ? IsEngineReady(contents) : IsReady(contents);
                    if (ready)
                    {
                        if (activeLog != logPath) await File.WriteAllTextAsync(logPath, contents);
                        return;
                    }
                    // cobalt's page-guard hook can miss one of the concurrent login requests. Fortnite then
                    // tries Epic, rejects its certificate and drops to the sign-in screen instead of retrying.
                    // Report it now so GameLauncher can start a fresh session rather than waiting out the timeout.
                    if (!engineOnly && IsLoginAborted(contents))
                    {
                        await SaveLatestAsync(process, logPath);
                        throw new LoginAbortedException(logPath);
                    }
                }
            }
            catch (IOException) { /* The game has not created/flushed its log yet. */ }
            status((engineOnly ? "Initializing Fortnite's engine... " : "Waiting for local login... ") + (int)timer.Elapsed.TotalSeconds + "s");
            await Task.Delay(500);
        }
        throw new TimeoutException("Fortnite did not finish engine startup and local login. Log: " + logPath);
    }
}

/// <summary>Fortnite aborted the local login and is showing the email/password screen.</summary>
public sealed class LoginAbortedException : Exception
{
    public string LogPath { get; }

    public LoginAbortedException(string logPath)
        : base("Fortnite aborted the local login and showed the email/password screen. Log: " + logPath) => LogPath = logPath;
}
