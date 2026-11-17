using System;
using System.Collections.Concurrent;
using System.Diagnostics;
using System.IO;
using System.Windows.Media;
using System.Windows.Threading;

namespace SyndicateLauncher;

/// <summary>
/// Startup frame-timing trace, only when the environment variable SYNDICATE_SPLASH_TRACE=1. Logs startup milestones
/// and every frame gap over 20 ms on the splash and main UI threads (with the current phase) to
/// %LOCALAPPDATA%\PassionProject\dev\splash-frames.log. Does nothing otherwise.
/// </summary>
internal static class StartupTrace
{
    public static readonly bool Enabled = Environment.GetEnvironmentVariable("SYNDICATE_SPLASH_TRACE") == "1";
    private static readonly ConcurrentQueue<string> Lines = new();
    private static readonly DateTime ProcessStart = SafeProcessStart();
    private static volatile string _phase = "startup";
    private static int _flushed;
    private static double _idleAt;

    /// <summary>Milliseconds since the process started.</summary>
    public static double Now => (DateTime.Now - ProcessStart).TotalMilliseconds;

    public static void Mark(string phase)
    {
        if (!Enabled) return;
        _phase = phase;
        if (phase == "idle") _idleAt = Now;
        double qpc = Stopwatch.GetTimestamp() * 1000.0 / Stopwatch.Frequency; // same clock as screen-capture frame times
        Lines.Enqueue($"{Now,8:0.0} ms  [mark] {phase}  qpc={qpc:0.00}");
    }

    /// <summary>Watches frame pacing on the calling (UI) thread until <paramref name="stopAfterMs"/> after process start.</summary>
    public static void WatchFrames(string thread, double stopAfterMs = 12000)
    {
        if (!Enabled) return;
        double last = -1, worst = 0, lastTarget = -1;
        int frames = 0, slow = 0, skipped = 0;
        EventHandler? handler = null;
        handler = (_, e) =>
        {
            double now = Now;
            if (last < 0) Lines.Enqueue($"{now,8:0.0} ms  [{thread}] first frame tick");
            // RenderingTime is the frame time the render thread is aiming at; a jump of two or more refreshes
            // means the (process-wide) render thread missed a present, even when this UI thread was on time.
            if (e is RenderingEventArgs re)
            {
                double target = re.RenderingTime.TotalMilliseconds;
                if (target == lastTarget) return; // same frame ticked again
                if (lastTarget >= 0 && target - lastTarget > 25)
                {
                    skipped++;
                    Lines.Enqueue($"{now,8:0.0} ms  [{thread}] present gap {target - lastTarget,6:0.0} ms  phase={_phase}");
                }
                lastTarget = target;
            }
            if (last >= 0)
            {
                double gap = now - last;
                frames++;
                if (gap > worst) worst = gap;
                if (_phase == "cover fade" && gap <= 20)
                    Lines.Enqueue($"{now,8:0.0} ms  [{thread}] frame     {gap,6:0.0} ms  phase={_phase}");
                if (gap > 20)
                {
                    slow++;
                    Lines.Enqueue($"{now,8:0.0} ms  [{thread}] frame gap {gap,6:0.0} ms  phase={_phase}");
                }
            }
            last = now;
            if (now > stopAfterMs || (_idleAt > 0 && now > _idleAt + 1500))
            {
                CompositionTarget.Rendering -= handler;
                Lines.Enqueue($"{now,8:0.0} ms  [{thread}] done: {frames} frames, {slow} over 20 ms, worst {worst:0.0} ms, {skipped} missed presents");
            }
        };
        CompositionTarget.Rendering += handler;
        Dispatcher.CurrentDispatcher.ShutdownStarted += (_, _) =>
            Lines.Enqueue($"{Now,8:0.0} ms  [{thread}] stopped: {frames} frames, {slow} over 20 ms, worst {worst:0.0} ms, {skipped} missed presents");
    }

    /// <summary>Logs every operation on the calling thread's dispatcher that runs longer than 8 ms.</summary>
    public static void WatchOperations(string thread)
    {
        if (!Enabled) return;
        var started = new System.Collections.Generic.Dictionary<DispatcherOperation, double>();
        var hooks = Dispatcher.CurrentDispatcher.Hooks;
        hooks.OperationStarted += (_, e) => started[e.Operation] = Now;
        hooks.OperationCompleted += (_, e) =>
        {
            if (!started.Remove(e.Operation, out double t0)) return;
            double took = Now - t0;
            if (took < 8 || (_idleAt > 0 && Now > _idleAt + 1500)) return;
            Lines.Enqueue($"{Now,8:0.0} ms  [{thread}] op {took,6:0.0} ms  {e.Operation.Priority} {Describe(e.Operation)}  phase={_phase}");
        };
    }

    private static string Describe(DispatcherOperation op)
    {
        try
        {
            var field = typeof(DispatcherOperation).GetField("_method", System.Reflection.BindingFlags.NonPublic | System.Reflection.BindingFlags.Instance);
            if (field?.GetValue(op) is Delegate d)
                return (d.Method.DeclaringType?.FullName ?? "?") + "." + d.Method.Name;
        }
        catch { }
        return "?";
    }

    public static void Flush()
    {
        if (!Enabled || System.Threading.Interlocked.Exchange(ref _flushed, 1) == 1) return;
        try
        {
            string dir = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "PassionProject", "dev");
            Directory.CreateDirectory(dir);
            File.AppendAllText(Path.Combine(dir, "splash-frames.log"),
                $"==== run {DateTime.Now:yyyy-MM-dd HH:mm:ss}{Environment.NewLine}{string.Join(Environment.NewLine, Lines)}{Environment.NewLine}");
        }
        catch { }
    }

    private static DateTime SafeProcessStart()
    {
        try { return Process.GetCurrentProcess().StartTime; }
        catch { return DateTime.Now; }
    }
}
