using System.Diagnostics;

namespace SyndicateLauncher.Controls;

/// <summary>
/// One clock for every always-on effect (lobby wallpaper drift, backdrop glow, particles / lobby weather).
/// Each of them redraws when its frame slot changes: Slot(15) for the slow ones, Slot(30) for particles. 30 is a
/// multiple of 15, so every slow frame lands on a particle frame, and the window presents at most 30 times a second.
/// Before this, the layers each kept their own timer (20, 30 and 60 frames a second, out of step). Almost every
/// screen refresh redrew the whole window then, which made the launcher stutter.
/// </summary>
internal static class AmbientClock
{
    private static readonly Stopwatch Clock = Stopwatch.StartNew();

    /// <summary>The slow effects (glow, wallpaper drift): they move under a pixel per frame at this rate.</summary>
    public const double SlowFps = 15;
    /// <summary>Moving particles and the RGB edge.</summary>
    public const double FastFps = 30;

    public static double Seconds => Clock.Elapsed.TotalSeconds;

    /// <summary>The frame slot at <paramref name="fps"/>: redraw only when it changes.</summary>
    public static long Slot(double fps) => (long)(Clock.Elapsed.TotalSeconds * fps);
}
