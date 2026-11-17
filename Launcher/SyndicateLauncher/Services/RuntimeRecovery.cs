using System;
using System.Collections.Generic;
using System.IO;
using System.IO.Compression;
using System.Reflection;
using System.Runtime.InteropServices;
using System.Text;
using System.Threading.Tasks;

namespace SyndicateLauncher.Services;

/// <summary>
/// Checks the resolved Runtime folder for missing or AV-quarantined DLLs and
/// restores them from the embedded Runtime.zip bundle.  After a successful
/// restore the affected folders are re-added to Defender's exclusion list so
/// the same files are not flagged again.
/// </summary>
public static class RuntimeRecovery
{
    // ------------------------------------------------------------------ public types

    public enum RecoveryStatus
    {
        /// <summary>Everything present — nothing to do.</summary>
        Clean,
        /// <summary>One or more files were restored successfully.</summary>
        Restored,
        /// <summary>Files are missing but the bundle could not restore them.</summary>
        Failed,
    }

    public sealed class RecoveryResult
    {
        public RecoveryStatus Status     { get; init; }
        public IReadOnlyList<string> Restored { get; init; } = Array.Empty<string>();
        public IReadOnlyList<string> Failed   { get; init; } = Array.Empty<string>();
        public string? ErrorMessage           { get; init; }
    }

    // ------------------------------------------------------------------ public API

    /// <summary>
    /// Scans <paramref name="runtimeRoot"/>\dlls for every file listed in
    /// <see cref="ModuleStartup.RequiredModules"/> plus the extra runtime files
    /// below, and restores any that are absent by extracting them from the
    /// embedded Runtime.zip.  Defender exclusions are refreshed after a restore.
    /// </summary>
    public static async Task<RecoveryResult> CheckAndRestoreAsync(
        string runtimeRoot, string? gameDirectory = null)
    {
        var dllsDir = Path.Combine(runtimeRoot, "dlls");
        var missing = FindMissing(dllsDir);

        if (missing.Count == 0)
            return new RecoveryResult { Status = RecoveryStatus.Clean };

        // Restore from the embedded bundle.
        var (restored, failed, error) = await Task.Run(() => RestoreFiles(dllsDir, missing));

        // Re-add Defender exclusions for the restored folder so AV does not
        // immediately re-quarantine the files we just put back.
        if (restored.Count > 0)
            await DefenderExclusions.TryAddSilentAsync(gameDirectory);

        return new RecoveryResult
        {
            Status      = failed.Count == 0 ? RecoveryStatus.Restored : RecoveryStatus.Failed,
            Restored    = restored,
            Failed      = failed,
            ErrorMessage = error,
        };
    }

    // ------------------------------------------------------------------ scanning

    /// <summary>
    /// All DLL filenames that must be present in <paramref name="dllsDir"/>.
    /// Covers every entry in <see cref="ModuleStartup.RequiredModules"/> plus
    /// the two optional client-side modules that players may still have under
    /// their old names.
    /// </summary>
    private static readonly string[] WatchedFiles =
    {
        // Required game-server / injection modules
        "memory.dll",
        "Erbium.dll",
        "cobalt.dll",
        "console.dll",
        "Syndicate1040.dll",
        "SyndicateConsoleRecovery.dll",
        "SyndicateMenu.dll",
        // Client-side modules (may be absent on first install; restored if in bundle)
        "SyndicateClient.dll",
        "SyndicatePadAssist.dll",
        // manifest is required for the launcher's integrity check
        "manifest.json",
    };

    private static List<string> FindMissing(string dllsDir)
    {
        var missing = new List<string>();
        foreach (var name in WatchedFiles)
        {
            var fullPath = Path.Combine(dllsDir, name);
            if (!File.Exists(fullPath))
            {
                missing.Add(name);
                continue;
            }

            // Even if the file exists, check whether AV has blocked/quarantined it
            // (Windows returns ERROR_VIRUS_INFECTED / ERROR_VIRUS_DELETED on open).
            if (IsBlocked(fullPath))
                missing.Add(name);
        }
        return missing;
    }

    // ------------------------------------------------------------------ restoration

    private static (List<string> Restored, List<string> Failed, string? Error)
        RestoreFiles(string dllsDir, List<string> targets)
    {
        var restored = new List<string>();
        var failed   = new List<string>();
        string? error = null;

        try
        {
            using var stream = Assembly.GetExecutingAssembly()
                .GetManifestResourceStream("SyndicateLauncher.Runtime.zip");

            if (stream is null)
            {
                error = "The embedded Runtime bundle is missing. Re-download the launcher.";
                failed.AddRange(targets);
                return (restored, failed, error);
            }

            using var zip = new ZipArchive(stream, ZipArchiveMode.Read, leaveOpen: false);

            Directory.CreateDirectory(dllsDir);

            foreach (var name in targets)
            {
                // Entries are stored as  dlls/<name>  inside the bundle.
                var entryPath = "dlls/" + name;
                var entry     = zip.GetEntry(entryPath);

                if (entry is null)
                {
                    // Some client-side files (SyndicateClient.dll, SyndicatePadAssist.dll)
                    // may legitimately not be in the bundle — skip silently.
                    if (IsOptional(name))
                        continue;

                    failed.Add(name);
                    continue;
                }

                try
                {
                    var dest = Path.Combine(dllsDir, name);
                    // Write to a temp file first so we never leave a half-written DLL.
                    var tmp = dest + ".restore-" + Guid.NewGuid().ToString("N");
                    try
                    {
                        using (var src = entry.Open())
                        using (var dst = new FileStream(tmp, FileMode.Create, FileAccess.Write, FileShare.None))
                            src.CopyTo(dst);

                        // Replace atomically (Move overwrites on .NET).
                        File.Move(tmp, dest, overwrite: true);
                        restored.Add(name);
                    }
                    finally
                    {
                        if (File.Exists(tmp)) try { File.Delete(tmp); } catch { }
                    }
                }
                catch (Exception ex) when (ex is IOException or UnauthorizedAccessException)
                {
                    failed.Add(name);
                }
            }
        }
        catch (Exception ex)
        {
            error = ex.Message;
            // Any file not yet handled goes into failed.
            foreach (var name in targets)
                if (!restored.Contains(name) && !failed.Contains(name))
                    failed.Add(name);
        }

        return (restored, failed, error);
    }

    // ------------------------------------------------------------------ helpers

    private static readonly HashSet<string> OptionalFiles = new(StringComparer.OrdinalIgnoreCase)
    {
        "SyndicateClient.dll",
        "SyndicatePadAssist.dll",
    };

    private static bool IsOptional(string name) => OptionalFiles.Contains(name);

    [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    private static extern IntPtr CreateFileW(
        string lpFileName, uint dwDesiredAccess, uint dwShareMode,
        IntPtr lpSecurityAttributes, uint dwCreationDisposition,
        uint dwFlagsAndAttributes, IntPtr hTemplateFile);

    [DllImport("kernel32.dll", SetLastError = false)]
    private static extern bool CloseHandle(IntPtr hObject);

    private const uint GenericRead          = 0x80000000;
    private const uint FileShareReadWrite   = 0x00000003;
    private const uint OpenExisting         = 3;
    private const int  ErrorVirusInfected   = 225;
    private const int  ErrorVirusDeleted    = 226;
    private const int  ErrorAccessDenied    = 5;
    private const int  ErrorInvalidImageHash = 577;
    private static readonly IntPtr InvalidHandle = new(-1);

    /// <summary>
    /// Returns true when Windows Defender (or another AV) has the file quarantined
    /// or blocked — it exists on disk but cannot be opened for reading.
    /// </summary>
    private static bool IsBlocked(string path)
    {
        var h = CreateFileW(path, GenericRead, FileShareReadWrite,
                            IntPtr.Zero, OpenExisting, 0, IntPtr.Zero);
        if (h != InvalidHandle)
        {
            CloseHandle(h);
            return false; // opened fine
        }
        int err = Marshal.GetLastWin32Error();
        // Only treat genuine AV block/quarantine codes as "blocked". A file that is merely locked or in use
        // (ERROR_ACCESS_DENIED / ERROR_SHARING_VIOLATION - e.g. the game already has the DLL loaded) is NOT
        // quarantined: it is present and working, so do not try to restore it (overwriting a loaded DLL throws
        // "being used by another process"). This is why cobalt.dll errored when a second copy was running.
        return err is ErrorVirusInfected or ErrorVirusDeleted or ErrorInvalidImageHash;
    }
}
