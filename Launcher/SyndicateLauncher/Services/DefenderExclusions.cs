using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Text;
using System.Threading.Tasks;

namespace SyndicateLauncher.Services;

public enum ExclusionResult { Added, Cancelled, Failed, NotSupported }

/// <summary>
/// Adds the Syndicate runtime folders to the Windows Defender exclusion list so the game DLLs
/// are not flagged or deleted. Only operates when Windows Defender is the active AV; silently
/// skips on third-party products that manage their own exclusion lists.
/// </summary>
public static class DefenderExclusions
{
    /// <summary>
    /// Returns the folders that should be added to Defender's exclusion list.
    /// <paramref name="gameDirectory"/> is the Fortnite install path from settings (may be null).
    /// </summary>
    public static IReadOnlyList<string> GetPaths(string? gameDirectory = null)
    {
        var seen  = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
        var paths = new List<string>();

        void Add(string path)
        {
            var trimmed = path.TrimEnd(Path.DirectorySeparatorChar, Path.AltDirectorySeparatorChar);
            if (!string.IsNullOrWhiteSpace(trimmed) && seen.Add(trimmed))
                paths.Add(trimmed);
        }

        // The folder the launcher exe sits in — covers the exe, Runtime\, and every DLL next to it.
        Add(AppContext.BaseDirectory);

        // %LOCALAPPDATA%\SyndicateLauncher — the extracted Runtime cache for release installs.
        Add(LegacyData.LocalDirectory);

        // %LOCALAPPDATA%\ProjectTilted — frozen name, read by the prebuilt client modules.
        Add(Path.Combine(
            Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),
            "ProjectTilted"));

        // Fortnite install directory, if one is configured.
        if (!string.IsNullOrWhiteSpace(gameDirectory))
            Add(gameDirectory);

        return paths.AsReadOnly();
    }

    /// <summary>
    /// Silently tries to add exclusions without showing a UAC prompt.
    /// Succeeds only when the launcher is already running as administrator.
    /// Safe to fire-and-forget on startup — never blocks or throws to the caller.
    /// </summary>
    public static Task<bool> TryAddSilentAsync(string? gameDirectory = null)
        => Task.Run(() =>
        {
            if (!IsDefenderServiceRunning()) return false;
            try
            {
                RunPowerShellScript(BuildScript(GetPaths(gameDirectory)), elevated: false);
                return true;
            }
            catch { return false; }
        });

    /// <summary>
    /// Adds exclusions with a UAC prompt if the launcher is not already elevated.
    /// Call this from the settings "Add to exclusions" button.
    /// </summary>
    public static async Task<ExclusionResult> AddAsync(string? gameDirectory = null)
    {
        if (!IsDefenderServiceRunning()) return ExclusionResult.NotSupported;

        var script = BuildScript(GetPaths(gameDirectory));

        // Try without elevation first — succeeds when the launcher is already running as admin.
        try
        {
            await Task.Run(() => RunPowerShellScript(script, elevated: false));
            return ExclusionResult.Added;
        }
        catch (UnauthorizedAccessException) { }
        catch (InvalidOperationException)   { }

        // Needs admin — request elevation via the UAC prompt.
        try
        {
            await Task.Run(() => RunPowerShellScript(script, elevated: true));
            return ExclusionResult.Added;
        }
        catch (OperationCanceledException) { return ExclusionResult.Cancelled; }
        catch                              { return ExclusionResult.Failed; }
    }

    // ------------------------------------------------------------------ internals

    /// <summary>
    /// Returns true when the Windows Defender service (WinDefend) is running.
    /// Uses sc.exe so no extra NuGet package is required.
    /// </summary>
    private static bool IsDefenderServiceRunning()
    {
        try
        {
            var psi = new ProcessStartInfo
            {
                FileName               = "sc.exe",
                Arguments              = "query WinDefend",
                UseShellExecute        = false,
                RedirectStandardOutput = true,
                CreateNoWindow         = true,
            };
            using var proc = Process.Start(psi);
            if (proc is null) return false;
            var output = proc.StandardOutput.ReadToEnd();
            proc.WaitForExit(5_000);
            return output.Contains("RUNNING", StringComparison.OrdinalIgnoreCase);
        }
        catch { return false; }
    }

    /// <summary>Builds a PowerShell script that adds each path as a Defender exclusion.</summary>
    private static string BuildScript(IReadOnlyList<string> paths)
    {
        var sb = new StringBuilder();
        foreach (var p in paths)
        {
            // Single-quote the path in PS; escape any embedded single quotes.
            var escaped = p.Replace("'", "''");
            sb.AppendLine($"Add-MpPreference -ExclusionPath '{escaped}' -Force -ErrorAction SilentlyContinue");
        }
        return sb.ToString();
    }

    /// <summary>
    /// Writes <paramref name="script"/> to a temp .ps1 file and runs it with PowerShell.
    /// When <paramref name="elevated"/> is true, the shell is started with <c>runas</c> (UAC).
    /// </summary>
    private static void RunPowerShellScript(string script, bool elevated)
    {
        var tempFile = Path.Combine(Path.GetTempPath(), $"syndicate-av-{Guid.NewGuid():N}.ps1");
        try
        {
            File.WriteAllText(tempFile, script, Encoding.UTF8);

            var psi = new ProcessStartInfo
            {
                FileName  = "powershell.exe",
                Arguments = $"-NoProfile -NonInteractive -ExecutionPolicy Bypass -File \"{tempFile}\"",
                // UseShellExecute must be true for Verb = "runas"; false lets us redirect streams.
                UseShellExecute        = elevated,
                CreateNoWindow         = !elevated,
                RedirectStandardOutput = !elevated,
                RedirectStandardError  = !elevated,
            };
            if (elevated) psi.Verb = "runas";

            using var proc = Process.Start(psi)
                             ?? throw new InvalidOperationException("Failed to start PowerShell.");

            bool exited = proc.WaitForExit(60_000);
            if (!exited)
            {
                try { proc.Kill(); } catch { }
                throw new OperationCanceledException("PowerShell timed out.");
            }

            // Non-elevated path: a non-zero exit usually means insufficient privileges.
            if (!elevated && proc.ExitCode != 0)
                throw new UnauthorizedAccessException($"PowerShell exited {proc.ExitCode}.");
        }
        catch (System.ComponentModel.Win32Exception ex) when (ex.NativeErrorCode == 1223)
        {
            // ERROR_CANCELLED — the user dismissed the UAC prompt.
            throw new OperationCanceledException("UAC cancelled.", ex);
        }
        finally
        {
            try { File.Delete(tempFile); } catch { }
        }
    }
}
