using System;
using System.IO;
using System.Text.RegularExpressions;

namespace SyndicateLauncher.Services;

public static class StartupFailure
{
    public static string? Diagnose(string log)
    {
        var missing = Regex.Match(log, @"Could not find SuperStruct ([A-Za-z0-9_]+) to create ([A-Za-z0-9_]+)");
        if (missing.Success)
            return "Fortnite could not load a required game asset (" + missing.Groups[1].Value + "). " +
                "Its Content/Paks files are incomplete or mixed. Re-extract the complete 10.40 CL-9380822 game download; replacing the AI DLL cannot repair missing game assets.";
        if (log.Contains("Out of memory", StringComparison.OrdinalIgnoreCase) || log.Contains("Ran out of memory allocating", StringComparison.OrdinalIgnoreCase))
            return "Fortnite ran out of memory while loading. Close other memory-heavy apps and make sure Windows has a system-managed paging file and free disk space.";
        if (log.Contains("DXGI_ERROR_DEVICE_REMOVED", StringComparison.Ordinal) || log.Contains("DXGI_ERROR_DEVICE_HUNG", StringComparison.Ordinal))
            return "Fortnite's graphics device stopped responding. Update the graphics driver and retry with other GPU-heavy apps closed.";
        return null;
    }

    public static string? ReadDiagnosis(string path)
    {
        try { return File.Exists(path) ? Diagnose(File.ReadAllText(path)) : null; }
        catch (IOException) { return null; }
        catch (UnauthorizedAccessException) { return null; }
    }
}
