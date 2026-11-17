// Syndicate © 2026 Syndicate. All rights reserved. Proprietary — no copying, distribution, or derivative works. See LICENSE.
using System;
using System.IO;
using System.Threading;
using System.Threading.Tasks;
using System.Windows;
using System.Windows.Threading;

namespace SyndicateClassic;

// Crash and freeze diagnostics.
// Before: no handler existed, so any exception in a button, timer or background task closed Syndicate
// with no message and nothing in the log. Now:
//  - errors on the UI thread are logged to %LOCALAPPDATA%\ProjectTilted\logs\launcher-errors.log and
//    shown in the diagnostics bar; Syndicate keeps running when it safely can;
//  - fatal errors write a crash report (crash-*.txt) that the next start points to;
//  - failed background tasks are logged instead of tearing the process down;
//  - a watchdog notices when the window stops responding for 3+ seconds (a freeze) and logs how long.
internal static class CrashGuard
{
    internal static readonly string LogFolder = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "ProjectTilted", "logs");
    private static string ErrorLog => Path.Combine(LogFolder, "launcher-errors.log");
    private static string CrashMarker => Path.Combine(LogFolder, "last-crash.txt");
    internal static Action<string>? Report; // set by MainWindow: writes to the diagnostics log
    private static int _errorsShown;

    internal static void Install(Application app)
    {
        app.DispatcherUnhandledException += (_, e) =>
        {
            Write("UI error", e.Exception);
            // Keep running for ordinary errors; a handful in a row means something is broken, so stop then.
            if (Interlocked.Increment(ref _errorsShown) <= 25)
            {
                e.Handled = true;
                Report?.Invoke("ERROR: something went wrong (" + e.Exception.GetBaseException().Message + "). Syndicate kept running; details are in " + ErrorLog);
            }
        };
        AppDomain.CurrentDomain.UnhandledException += (_, e) =>
        {
            if (e.ExceptionObject is Exception error) Write(e.IsTerminating ? "CRASH" : "Background error", error, crash: e.IsTerminating);
        };
        TaskScheduler.UnobservedTaskException += (_, e) =>
        {
            e.SetObserved();
            // A port probe that timed out (backend, MongoDB, a Radmin host) leaves its connect task behind; when the
            // socket closes it ends with "aborted" (995) or "refused" (10061). That is expected, not an error.
            foreach (var inner in e.Exception.InnerExceptions)
                if (inner.GetBaseException() is not System.Net.Sockets.SocketException) { Write("Background task error", e.Exception); return; }
        };
    }

    internal static void Write(string kind, Exception error, bool crash = false)
    {
        try
        {
            Directory.CreateDirectory(LogFolder);
            string text = $"[{DateTime.Now:yyyy-MM-dd HH:mm:ss}] {kind} (Syndicate {System.Reflection.Assembly.GetExecutingAssembly().GetName().Version?.ToString(3)})\n{error}\n\n";
            File.AppendAllText(ErrorLog, text);
            if (crash)
            {
                string file = Path.Combine(LogFolder, $"crash-{DateTime.Now:yyyyMMdd-HHmmss}.txt");
                File.WriteAllText(file, text);
                File.WriteAllText(CrashMarker, file);
            }
        }
        catch { }
    }

    // Shown once after a crash: where the report is, so it can be pasted when asking for help.
    internal static string? TakeCrashNotice()
    {
        try
        {
            if (!File.Exists(CrashMarker)) return null;
            var file = File.ReadAllText(CrashMarker).Trim();
            File.Delete(CrashMarker);
            return "Syndicate closed unexpectedly last time. The crash report is " + file + " (paste it when asking for help).";
        }
        catch { return null; }
    }

    // Freeze watchdog: posts a tiny job to the UI thread every second and measures how long it waits.
    internal static void WatchForFreezes(Dispatcher dispatcher)
    {
        var thread = new Thread(() =>
        {
            while (!dispatcher.HasShutdownStarted)
            {
                var started = DateTime.UtcNow;
                var done = new ManualResetEventSlim();
                try { dispatcher.BeginInvoke(DispatcherPriority.Send, new Action(done.Set)); } catch { return; }
                if (!done.Wait(3000))
                {
                    done.Wait();
                    var seconds = (DateTime.UtcNow - started).TotalSeconds;
                    var message = $"The launcher window froze for {seconds:0.0} s.";
                    Write("Freeze", new TimeoutException(message));
                    try { dispatcher.BeginInvoke(new Action(() => Report?.Invoke("WARNING: " + message + " (logged in " + ErrorLog + ")"))); } catch { }
                }
                Thread.Sleep(1000);
            }
        }) { IsBackground = true, Name = "Syndicate freeze watchdog" };
        thread.Start();
    }
}