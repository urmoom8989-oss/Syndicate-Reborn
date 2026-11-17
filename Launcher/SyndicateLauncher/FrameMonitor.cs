using System;
using System.Diagnostics;
using System.IO;
using System.Text;
using System.Windows.Media;

namespace SyndicateLauncher;

/// <summary>
/// Frame-pacing monitor for the whole session, only when the environment variable SYNDICATE_FRAME_TRACE=1 or a
/// file named frame-trace.on sits next to the exe
/// (StartupTrace covers the first seconds of startup only). Every 10 seconds it appends one line to
/// launcher-frames.log, next to the exe when frame-trace.on turned it on, else in %LOCALAPPDATA%\PassionProject\dev:
/// <list type="bullet">
/// <item>frames per second;</item>
/// <item>how many frames took over 20 and over 50 ms;</item>
/// <item>the worst frame;</item>
/// <item>how many presents the render thread missed;</item>
/// <item>the theme and the launcher page that were open.</item>
/// </list>
/// So "the launcher stutters" can be read as numbers, page by page. Costs nothing when the variable isn't set.
/// </summary>
internal static class FrameMonitor
{
    // On with the environment variable, or with an empty file named frame-trace.on next to SyndicateLauncher.exe
    // (handy when the launcher is opened by double-click).
    public static readonly bool Enabled = Environment.GetEnvironmentVariable("SYNDICATE_FRAME_TRACE") == "1"
        || File.Exists(Path.Combine(AppContext.BaseDirectory, "frame-trace.on"));

    /// <summary>Set by the main window when the open page changes (shown in each line).</summary>
    public static string Page { get; set; } = "?";

    private static readonly Stopwatch Clock = Stopwatch.StartNew();
    private static string? _path;

    private static string[]? _switches;

    /// <summary>
    /// Measuring aid: true when perf-switches.txt next to the exe has a line with <paramref name="name"/> (for example
    /// no-hero-drift, no-logo-pulse). Lets one effect at a time be held back while reading launcher-frames.log.
    /// </summary>
    public static bool Off(string name)
    {
        if (_switches is null)
        {
            try
            {
                string file = Path.Combine(AppContext.BaseDirectory, "perf-switches.txt");
                _switches = File.Exists(file) ? File.ReadAllLines(file) : Array.Empty<string>();
            }
            catch { _switches = Array.Empty<string>(); }
        }
        foreach (var line in _switches)
            if (string.Equals(line.Trim(), name, StringComparison.OrdinalIgnoreCase)) return true;
        return false;
    }

    private static string Describe(System.Windows.Threading.DispatcherOperation op)
    {
        try
        {
            var field = typeof(System.Windows.Threading.DispatcherOperation).GetField("_method",
                System.Reflection.BindingFlags.NonPublic | System.Reflection.BindingFlags.Instance);
            if (field?.GetValue(op) is Delegate d)
                return (d.Method.DeclaringType?.Name ?? "?") + "." + d.Method.Name + "/" + op.Priority;
        }
        catch { }
        return "?/" + op.Priority;
    }

    public static void Start()
    {
        if (!Enabled) return;
        try
        {
            // Turned on by the frame-trace.on file: the log goes next to it, beside the exe. Otherwise the dev folder.
            string dir = File.Exists(Path.Combine(AppContext.BaseDirectory, "frame-trace.on"))
                ? AppContext.BaseDirectory
                : Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "PassionProject", "dev");
            Directory.CreateDirectory(dir);
            _path = Path.Combine(dir, "launcher-frames.log");
            File.AppendAllText(_path, $"==== run {DateTime.Now:yyyy-MM-dd HH:mm:ss}, monitor {RenderCapability.Tier >> 16} (render tier){Environment.NewLine}");
        }
        catch { return; }

        double windowStart = Clock.Elapsed.TotalMilliseconds, last = -1, lastTarget = -1, worst = 0;
        int frames = 0, over20 = 0, over50 = 0, missed = 0;

        // UI-thread work: every dispatcher operation's run time, so a slow frame can be told apart from the render
        // thread being busy (then the UI thread was idle) and the culprit named.
        double busy = 0, slowestOp = 0;
        int opsOver8 = 0;
        string slowestName = "-";
        var started = new System.Collections.Generic.Dictionary<System.Windows.Threading.DispatcherOperation, double>();
        var hooks = System.Windows.Threading.Dispatcher.CurrentDispatcher.Hooks;
        hooks.OperationStarted += (_, e) => started[e.Operation] = Clock.Elapsed.TotalMilliseconds;
        hooks.OperationCompleted += (_, e) =>
        {
            if (!started.Remove(e.Operation, out double t0)) return;
            double took = Clock.Elapsed.TotalMilliseconds - t0;
            busy += took;
            if (took > 8) opsOver8++;
            if (took > slowestOp) { slowestOp = took; slowestName = Describe(e.Operation); }
        };
        hooks.OperationAborted += (_, e) => started.Remove(e.Operation);
        CompositionTarget.Rendering += (_, e) =>
        {
            double now = Clock.Elapsed.TotalMilliseconds;
            if (e is RenderingEventArgs re)
            {
                double target = re.RenderingTime.TotalMilliseconds;
                if (target == lastTarget) return; // the same frame ticked twice
                if (lastTarget >= 0 && target - lastTarget > 25) missed++;
                lastTarget = target;
            }
            if (last >= 0)
            {
                double gap = now - last;
                frames++;
                if (gap > 20) over20++;
                if (gap > 50) over50++;
                if (gap > worst) worst = gap;
            }
            last = now;
            if (now - windowStart < 10000) return;
            double seconds = (now - windowStart) / 1000;
            var line = new StringBuilder()
                .Append($"{DateTime.Now:HH:mm:ss}  {frames / seconds,5:0.0} ticks/s  over20ms {over20,4}  over50ms {over50,3}  worst {worst,6:0.0} ms  ")
                .Append($"missed presents {missed,4}  ui busy {busy / seconds / 10,4:0.0}%  ui ops>8ms {opsOver8,3}  slowest ui op {slowestOp,5:0.0} ms {slowestName}  ")
                .Append($"active={(System.Windows.Application.Current?.MainWindow?.IsActive == true ? "yes" : "no")}  theme={Services.ThemeService.Settings.Theme}  page={Page}");
            try { File.AppendAllText(_path, line + Environment.NewLine); } catch { }
            windowStart = now;
            frames = over20 = over50 = missed = opsOver8 = 0;
            worst = busy = slowestOp = 0;
            slowestName = "-";
        };
    }
}
