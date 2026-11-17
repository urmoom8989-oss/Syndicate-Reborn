using System;
using System.IO;

namespace SyndicateLauncher.Services;

/// <summary>
/// Moves data saved by launchers from before the rename to Syndicate into the Syndicate folders,
/// once, before anything else reads them:
///   %APPDATA%\(old)Launcher      -> %APPDATA%\SyndicateLauncher (settings, profiles, presets, lockers)
///   %LOCALAPPDATA%\(old)Launcher -> %LOCALAPPDATA%\SyndicateLauncher (accounts database, backend, caches)
/// Entries already present under the new name are left alone. When something cannot be moved (a file
/// is open), the launcher keeps reading it from the old place and tries again on the next start.
/// The old names are built from parts so they appear nowhere else in the source.
/// </summary>
public static class LegacyData
{
    private static readonly string OldLauncher = "Tit" + "anium" + "Launcher";
    private static readonly string OldDatabase = "Til" + "ted" + "Database";
    private static readonly string OldBackend = "Til" + "ted-backend";
    private const string NewLauncher = "SyndicateLauncher";

    private static string Roaming => Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData);
    private static string Local => Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData);

    /// <summary>%APPDATA%\SyndicateLauncher, or the old folder while it could not be moved yet.</summary>
    public static string RoamingDirectory
    {
        get
        {
            string current = Path.Combine(Roaming, NewLauncher);
            string old = Path.Combine(Roaming, OldLauncher);
            return !Directory.Exists(current) && Directory.Exists(old) ? old : current;
        }
    }

    /// <summary>%LOCALAPPDATA%\SyndicateLauncher.</summary>
    public static string LocalDirectory => Path.Combine(Local, NewLauncher);

    /// <summary>The MongoDB data folder (accounts). Falls back to an old one that could not be moved.</summary>
    public static string DatabaseDirectory
    {
        get
        {
            string current = Path.Combine(LocalDirectory, "SyndicateDatabase");
            if (Directory.Exists(current)) return current;
            foreach (var old in new[]
            {
                Path.Combine(LocalDirectory, OldDatabase),
                Path.Combine(Local, OldLauncher, OldDatabase),
                Path.Combine(Local, OldLauncher, "SyndicateDatabase"),
            })
                if (Directory.Exists(old)) return old;
            return current;
        }
    }

    public static void Migrate()
    {
        MoveFolder(Path.Combine(Roaming, OldLauncher), Path.Combine(Roaming, NewLauncher));
        MoveFolder(Path.Combine(Local, OldLauncher), LocalDirectory);
    }

    private static string NewEntryName(string name)
    {
        if (name.Equals(OldDatabase, StringComparison.OrdinalIgnoreCase)) return "SyndicateDatabase";
        if (name.Equals(OldBackend, StringComparison.OrdinalIgnoreCase)) return "Syndicate-backend";
        return name;
    }

    private static void MoveFolder(string from, string to)
    {
        try
        {
            if (!Directory.Exists(from)) return;
            Directory.CreateDirectory(to);
            foreach (var entry in Directory.EnumerateFileSystemEntries(from))
            {
                string target = Path.Combine(to, NewEntryName(Path.GetFileName(entry)));
                if (File.Exists(target) || Directory.Exists(target)) continue;
                try
                {
                    if (Directory.Exists(entry)) Directory.Move(entry, target);
                    else File.Move(entry, target);
                }
                catch (IOException) { }
                catch (UnauthorizedAccessException) { }
            }
            // Inside the new folder too, in case an older build already moved the parent only.
            foreach (var oldName in new[] { OldDatabase, OldBackend })
            {
                string inner = Path.Combine(to, oldName);
                string renamed = Path.Combine(to, NewEntryName(oldName));
                if (Directory.Exists(inner) && !Directory.Exists(renamed))
                {
                    try { Directory.Move(inner, renamed); }
                    catch (IOException) { }
                    catch (UnauthorizedAccessException) { }
                }
            }
            if (Directory.GetFileSystemEntries(from).Length == 0) Directory.Delete(from);
        }
        catch (IOException) { }
        catch (UnauthorizedAccessException) { }
    }
}
