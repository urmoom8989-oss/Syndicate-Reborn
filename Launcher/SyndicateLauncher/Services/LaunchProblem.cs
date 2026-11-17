using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;

namespace SyndicateLauncher.Services;

/// <summary>
/// A launch failure written for players: one short line saying what went wrong and a few numbered steps
/// that fix it. The technical text (exception, stack trace) stays in the session's startup.log.
/// Diagnostics shows the title in red with the steps under it, and clears both once a launch works again.
/// </summary>
public sealed record LaunchProblem(string Title, IReadOnlyList<string> Steps)
{
    /// <summary>The session log folder, shown as the last step so the full details are one click away.</summary>
    public string? LogFolder { get; init; }

    public LaunchProblem WithLogs(string? folder) => folder is null || LogFolder is not null ? this : this with { LogFolder = folder };

    public override string ToString()
    {
        var text = Title;
        for (int i = 0; i < Steps.Count; i++) text += Environment.NewLine + "  " + (i + 1) + ". " + Steps[i];
        if (LogFolder is not null) text += Environment.NewLine + "  Full details: " + LogFolder;
        return text;
    }

    // ------------------------------------------------------------------ modules (DLLs)

    public static LaunchProblem ModuleBlocked(string module) => new(
        "Windows Security blocked " + module + ".",
        new[]
        {
            "Open Windows Security > Virus & threat protection > Protection history.",
            "Find the entry for " + module + ", choose Actions > Allow on device (or Restore if it was quarantined).",
            "Self-built game DLLs are often flagged by mistake; to stop it happening after every rebuild, add your Documents\\Syndicate folder under Exclusions.",
            "Press Play again."
        });

    public static LaunchProblem ModuleMissing(string module) => new(
        module + " is missing.",
        new[]
        {
#if DEV_BUILD
            "Run UPDATE FROM GIT.cmd again (it builds or copies " + module + " into Documents\\Syndicate\\Runtime\\dlls).",
#else
            "Close Syndicate, then reinstall the launcher - a game file was removed (usually by antivirus). Add the Syndicate folder to your antivirus exclusions first so it is not removed again.",
#endif
            "If Windows Security removed it, restore it from Protection history first.",
            "Press Play again."
        });

    public static LaunchProblem ModuleDamaged(string module) => new(
        module + " is damaged or not a 64-bit Windows DLL.",
        new[]
        {
#if DEV_BUILD
            "Run UPDATE FROM GIT.cmd again to rebuild or re-copy it.",
#else
            "Reinstall the launcher so the file is replaced.",
#endif
            "Press Play again."
        });

    public static LaunchProblem ModuleNeedsDll(string module, string dependency) => new(
        module + " needs " + dependency + ", which isn't on this PC.",
        new[]
        {
            dependency.StartsWith("vcruntime", StringComparison.OrdinalIgnoreCase) || dependency.StartsWith("msvcp", StringComparison.OrdinalIgnoreCase)
                ? "Install the latest Microsoft Visual C++ Redistributable (x64) from Microsoft's website."
                : "Install or repair the Windows component that provides " + dependency + ", or rebuild " + module + " without it.",
            "Press Play again."
        });

    public static LaunchProblem ModuleStartFailed(string module, string? fortnite) => new(
        module + " stopped while starting inside Fortnite" + (fortnite is null ? "" : " " + fortnite) + ".",
        new[]
        {
            "Check Windows Security > Protection history for " + module + " and allow it if it is listed.",
#if DEV_BUILD
            "Run UPDATE FROM GIT.cmd so the newest " + module + " is built, then press Play again.",
#else
            "Fully close Fortnite and the launcher, then reopen the launcher and press Play again.",
#endif
            "If it still fails, this Fortnite version may not be supported by " + module + " yet. Send the log folder below to the team."
        });

    public static LaunchProblem ModuleTimedOut(string module) => new(
        "Loading " + module + " took more than two minutes.",
        new[]
        {
            "Check Windows Security > Protection history for a blocked or quarantined file.",
            "Make sure the Syndicate folder is on a local drive (not a network or cloud-only folder).",
            "Press Play again."
        });

    // ------------------------------------------------------------------ everything else

    internal static LaunchProblem IslandLoadFailed(string? exitCode, string? crashCode)
    {
        if (exitCode is null)
            return new("[HOST-ISLAND-02] The Syndicate server timed out while loading the island.",
                new[] { "Close other memory-heavy apps and press Play again.", "If it happens again, send the log folder below to the team." });

        string details = crashCode is null || string.Equals(exitCode, crashCode, StringComparison.OrdinalIgnoreCase)
            ? exitCode : exitCode + ", native exception " + crashCode;
        return new("[HOST-ISLAND-01] The Syndicate server exited while loading the island (" + details + ").",
            new[] { "Send the log folder below to the team. It includes any Erbium logs captured for this attempt." });
    }

    public static LaunchProblem LoginScreen() => new(
        "Fortnite could not finish signing in to the local server.",
        new[]
        {
            "Close Fortnite if it is still open, then press Play again.",
#if DEV_BUILD
            "If it keeps happening, run UPDATE FROM GIT.cmd and send the log folder below to the team."
#else
            "If it keeps happening, send the log folder below to the team."
#endif
        });

    /// <summary>Turns any other launch exception into a short title and steps.</summary>
    public static LaunchProblem From(Exception error)
    {
        if (error is LaunchProblemException known) return known.Problem;
        string message = error.Message;
        string m = message.ToLowerInvariant();
        string firstSentence = FirstSentence(message);

        if (m.Contains("already has a running game") || m.Contains("close the running fortnite"))
            return new("Fortnite is already running.", new[] { "Close every Fortnite window (or end FortniteClient-Win64-Shipping in Task Manager).", "Press Play again." });
        if (m.Contains("port 7777"))
            return new("Another game server is using port 7777.", new[] { "Close other Fortnite servers or private-server tools.", "Press Play again." });
        if (m.Contains("manifest") || m.Contains("bundled module is missing or changed"))
#if DEV_BUILD
            return new("The launcher's game files don't match.", new[] { "Run UPDATE FROM GIT.cmd (or extract the complete launcher ZIP again).", "Press Play again." });
#else
            return new("The launcher's game files don't match.", new[] { "Close Syndicate, add the Syndicate folder to your antivirus exclusions, then reinstall the launcher - a file was changed or removed (often by antivirus).", "Press Play again." });
#endif
        if (m.Contains("out of memory"))
            return new("Fortnite ran out of memory while loading.", new[] { "Close memory-heavy apps (browsers, editors).", "Make sure Windows manages the paging file and the drive has free space.", "Press Play again." });
        if (m.Contains("graphics device"))
            return new("Fortnite's graphics driver stopped responding.", new[] { "Update your graphics driver.", "Close other GPU-heavy apps and press Play again." });
        if (m.Contains("content/paks") || m.Contains("game asset"))
            return new("Fortnite's game files are incomplete.", new[] { "Re-extract the complete Fortnite build into its folder.", "Choose that folder again in the launcher and press Play." });
        if (m.Contains("did not bring up the frontend") || m.Contains("did not become joinable"))
            return new("The match server took too long to start.", new[] { "Close other heavy apps; slower PCs need a few minutes the first time.", "Press Play again.", "If it keeps happening, send the log folder below to the team." });
        if (m.Contains("exited") || m.Contains("crash"))
            return new(firstSentence, new[] { "Press Play again.", "If it happens again, send the log folder below to the team." });
        if (m.Contains("backend") || m.Contains("mongo") || m.Contains("node"))
#if DEV_BUILD
            return new("The local Syndicate backend didn't start.", new[] { "Run UPDATE FROM GIT.cmd (it repairs the backend and its Node/MongoDB files).", "Press Play again." });
#else
            return new("The local Syndicate backend didn't start.", new[] { "Fully close Syndicate, then end any leftover node.exe or mongod.exe in Task Manager.", "Reopen the launcher; if it keeps happening, reinstall it." });
#endif
        return new(firstSentence, new[] { "Press Play again.", "If it happens again, send the log folder below to the team." });
    }

    /// <summary>Pulls "Logs: C:\..." / "Log: C:\..." off the end of an old-style message.</summary>
    public static (string Message, string? Folder) SplitLogs(string message)
    {
        foreach (var marker in new[] { " Logs: ", " Log: " })
        {
            int at = message.LastIndexOf(marker, StringComparison.Ordinal);
            if (at < 0) continue;
            var path = message[(at + marker.Length)..].Trim();
            if (File.Exists(path)) path = Path.GetDirectoryName(path) ?? path;
            return (message[..at].Trim(), path);
        }
        return (message, null);
    }

    private static string FirstSentence(string message)
    {
        var (text, _) = SplitLogs(message);
        int dot = text.IndexOf(". ", StringComparison.Ordinal);
        var first = dot > 0 ? text[..(dot + 1)] : text;
        return first.Length > 160 ? first[..157].TrimEnd() + "..." : first;
    }
}

/// <summary>Thrown when the launcher already knows what went wrong and how to fix it.</summary>
public sealed class LaunchProblemException : InvalidOperationException
{
    public LaunchProblem Problem { get; }
    public LaunchProblemException(LaunchProblem problem, Exception? inner = null) : base(problem.Title, inner) => Problem = problem;
}
