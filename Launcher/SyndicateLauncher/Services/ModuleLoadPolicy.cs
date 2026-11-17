using System;

namespace SyndicateLauncher.Services;

internal static class ModuleLoadPolicy
{
    // Slow storage and first-run file scanning can exceed the former 15 seconds.
    public static readonly TimeSpan Timeout = TimeSpan.FromSeconds(120);
    public static bool Confirmed(uint result, bool mapped) => mapped;
    public static string Rejection(string name, uint result) =>
        "Windows could not initialize " + name + " (loader result 0x" + result.ToString("X8") + "). " +
        "Extract the complete launcher ZIP into a local folder and check Windows Security's Protection history for a blocked file. " +
        "The launcher will not retry a failed module in the same game process.";
}
