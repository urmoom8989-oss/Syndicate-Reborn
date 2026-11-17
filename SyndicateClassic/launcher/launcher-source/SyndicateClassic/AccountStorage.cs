// Syndicate © 2026 Syndicate. All rights reserved. Proprietary — no copying, distribution, or derivative works. See LICENSE.
using System;
using System.IO;
using System.Linq;
using System.Collections.Generic;
using System.Security.Cryptography;
using System.Text.Json;

namespace SyndicateClassic;

// Account/profile database belongs to the Windows user, never to an installation.
internal static class AccountStorage
{
    internal static string Root => Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "ProjectTilted", "UserData");
    internal static string Database => Path.Combine(Root, "Database");
    private sealed record SnapshotFile(string Path, string Sha256);
    private static bool HasDatabase(string path) => Directory.Exists(path) && File.Exists(Path.Combine(path, "WiredTiger"));

    // snapshot: whether to take the daily account copy now. Start-up passes false unless the newest copy is
    // more than 3 days old (SnapshotOverdue): the copy is taken when Syndicate closes instead (SnapshotAfterStop),
    // right after the database has stopped, so nobody waits for it when opening the launcher.
    internal static string PrepareDatabase(string legacy, string? userRoot = null, bool snapshot = true)
    {
        string root = userRoot ?? Root;
        Directory.CreateDirectory(root);
        using var lease = new FileStream(Path.Combine(root, "account-storage.lock"), FileMode.OpenOrCreate, FileAccess.ReadWrite, FileShare.None);
        string database = Path.Combine(root, "Database"), backups = Path.Combine(root, "AccountBackups");
        Directory.CreateDirectory(backups);
        if (!HasDatabase(database))
        {
            // Never replace an incomplete/nonempty database with a new empty account store.
            if (Directory.Exists(database) && Directory.EnumerateFileSystemEntries(database).Any())
                throw new IOException("The saved account database needs recovery. Your files have been preserved at " + database);
            string? source = Directory.EnumerateDirectories(backups, "snapshot-*").OrderByDescending(p => p, StringComparer.Ordinal)
                .FirstOrDefault(p => ValidSnapshot(p));
            source ??= HasDatabase(legacy) ? legacy : null;
            if (source != null)
            {
                string staging = Path.Combine(root, "migration-" + Guid.NewGuid().ToString("N"));
                CopyCold(source, staging);
                if (Directory.Exists(database)) Directory.Delete(database); // verified empty above
                Directory.Move(staging, database); // atomic publication; original is retained
                File.WriteAllText(Path.Combine(root, "migration.txt"), DateTime.UtcNow.ToString("O") + "\nCopied from " + source);
            }
            else if (Directory.EnumerateDirectories(backups, "snapshot-*").Any())
                throw new IOException("Account backups exist but failed verification. No empty database was created. Backups: " + backups);
            else if (Directory.Exists(legacy) && Directory.EnumerateFiles(legacy, "*", SearchOption.AllDirectories)
                .Any(p => Path.GetFileName(p) is not ("mongodb.log" or "mongod.lock" or "WiredTiger.lock")))
                throw new IOException("The previous account database needs recovery. No files were replaced: " + legacy);
            else Directory.CreateDirectory(database);
        }
        if (snapshot && HasDatabase(database)) TakeSnapshotIfDue(database, backups);
        return database;
    }

    // When Syndicate closes (database stopped): today's account copy, if one is due. Never throws, so closing the
    // launcher can not fail because of it; a copy that could not be made is simply taken next time.
    internal static void SnapshotAfterStop()
    {
        try
        {
            Directory.CreateDirectory(Root);
            using var lease = new FileStream(Path.Combine(Root, "account-storage.lock"), FileMode.OpenOrCreate, FileAccess.ReadWrite, FileShare.None);
            string backups = Path.Combine(Root, "AccountBackups");
            Directory.CreateDirectory(backups);
            // The database Syndicate actually uses: its own folder when the shared one belongs to another launcher.
            string database = File.Exists(Path.Combine(Root, "use-tilted-database.txt")) ? Path.Combine(Root, "Database-Tilted") : Database;
            // CopyCold locks mongod.lock / WiredTiger.lock exclusively, so a database that is somehow still
            // running is never copied (the lock fails, and the copy is simply taken another time).
            if (HasDatabase(database)) TakeSnapshotIfDue(database, backups);
        }
        catch { }
    }

    // True when the newest account copy is more than 3 days old (and the database changed since): then start-up
    // still takes one, in case Syndicate has not been closed normally for a while.
    internal static bool SnapshotOverdue()
    {
        try
        {
            string backups = Path.Combine(Root, "AccountBackups");
            if (!Directory.Exists(backups) || !HasDatabase(Database)) return false;
            string? newest = Directory.EnumerateDirectories(backups, "snapshot-*").OrderByDescending(p => p, StringComparer.Ordinal).FirstOrDefault();
            if (newest == null) return true;
            if (!DateTime.TryParseExact(Path.GetFileName(newest).Substring("snapshot-".Length), "yyyyMMdd-HHmmss-fffffff", System.Globalization.CultureInfo.InvariantCulture,
                    System.Globalization.DateTimeStyles.AssumeUniversal | System.Globalization.DateTimeStyles.AdjustToUniversal, out var taken)) return true;
            return DateTime.UtcNow - taken > TimeSpan.FromDays(3) && !SnapshotIsFresh(Database, backups);
        }
        catch { return false; }
    }

    private static void TakeSnapshotIfDue(string database, string backups)
    {
        {
            // A cold snapshot includes the journal; never copy a running WiredTiger database.
            string day = "snapshot-" + DateTime.UtcNow.ToString("yyyyMMdd-HHmmss-fffffff");
            // Copying and checksumming the whole database (often 200+ MB) on every start made the
            // launcher take ages to open. One snapshot a day is plenty, and none when nothing changed.
            if (!Directory.Exists(Path.Combine(backups, day)) && !SnapshotIsFresh(database, backups))
            {
                string pending = Path.Combine(backups, ".pending-" + Guid.NewGuid().ToString("N"));
                CopyCold(database, pending);
                Directory.Move(pending, Path.Combine(backups, day));
                foreach (string old in Directory.EnumerateDirectories(backups, "snapshot-*").OrderByDescending(p => p, StringComparer.Ordinal).Skip(3))
                    Directory.Delete(old, true);
            }
        }
    }

    // The newest snapshot is under a day old, or the database hasn't been written since it was taken.
    private static bool SnapshotIsFresh(string database, string backups)
    {
        try
        {
            string? newest = Directory.EnumerateDirectories(backups, "snapshot-*").OrderByDescending(p => p, StringComparer.Ordinal).FirstOrDefault();
            if (newest == null || !File.Exists(Path.Combine(newest, "account-snapshot.json"))) return false;
            string stamp = Path.GetFileName(newest).Substring("snapshot-".Length);
            if (!DateTime.TryParseExact(stamp, "yyyyMMdd-HHmmss-fffffff", System.Globalization.CultureInfo.InvariantCulture,
                    System.Globalization.DateTimeStyles.AssumeUniversal | System.Globalization.DateTimeStyles.AdjustToUniversal, out var taken)) return false;
            if (DateTime.UtcNow - taken < TimeSpan.FromHours(24)) return true;
            var changed = Directory.EnumerateFiles(database, "*", SearchOption.AllDirectories)
                .Where(p => Path.GetFileName(p) is not ("mongodb.log" or "mongod.lock" or "WiredTiger.lock"))
                .Select(File.GetLastWriteTimeUtc).DefaultIfEmpty(DateTime.MaxValue).Max();
            return changed <= taken;
        }
        catch (IOException) { return false; }
        catch (UnauthorizedAccessException) { return false; }
    }

    private static void CopyCold(string source, string target)
    {
        var locks = new List<FileStream>();
        try
        {
            foreach (string name in new[] { "mongod.lock", "WiredTiger.lock" })
                if (File.Exists(Path.Combine(source, name)))
                    locks.Add(new FileStream(Path.Combine(source, name), FileMode.Open, FileAccess.ReadWrite, FileShare.None));
            Directory.CreateDirectory(target);
            var files = new List<SnapshotFile>();
            CopyDirectory(source, target, "", files);
            File.WriteAllText(Path.Combine(target, "account-snapshot.json"), JsonSerializer.Serialize(files));
            if (!ValidSnapshot(target)) throw new IOException("Account backup verification failed. Original data was preserved.");
        }
        catch (IOException ex)
        {
            throw new IOException("Could not safely copy account data. Close other Syndicate/database processes and retry. Original files remain at " + source, ex);
        }
        finally { foreach (var file in locks) file.Dispose(); }
    }

    private static void CopyDirectory(string source, string target, string relative, List<SnapshotFile> files)
    {
        if ((File.GetAttributes(source) & FileAttributes.ReparsePoint) != 0) throw new IOException("Account storage cannot contain directory links.");
        foreach (string path in Directory.EnumerateFiles(source))
        {
            string name = Path.GetFileName(path);
            if (name is "mongod.lock" or "WiredTiger.lock" or "account-snapshot.json" or "mongodb.log") continue;
            if ((File.GetAttributes(path) & FileAttributes.ReparsePoint) != 0) throw new IOException("Account storage cannot contain file links.");
            string destination = Path.Combine(target, name);
            using var input = new FileStream(path, FileMode.Open, FileAccess.Read, FileShare.Read);
            using (var output = new FileStream(destination, FileMode.CreateNew, FileAccess.Write, FileShare.None)) input.CopyTo(output);
            input.Position = 0;
            string expected = Convert.ToHexString(SHA256.HashData(input));
            using var copied = File.OpenRead(destination);
            if (expected != Convert.ToHexString(SHA256.HashData(copied))) throw new IOException("Account copy checksum mismatch.");
            files.Add(new SnapshotFile(Path.Combine(relative, name), expected));
        }
        foreach (string dir in Directory.EnumerateDirectories(source))
        {
            string name = Path.GetFileName(dir), destination = Path.Combine(target, name);
            Directory.CreateDirectory(destination);
            CopyDirectory(dir, destination, Path.Combine(relative, name), files);
        }
    }

    private static bool ValidSnapshot(string path)
    {
        try
        {
            if (!HasDatabase(path)) return false;
            var files = JsonSerializer.Deserialize<List<SnapshotFile>>(File.ReadAllText(Path.Combine(path, "account-snapshot.json")));
            if (files == null || files.Count == 0) return false;
            string root = Path.GetFullPath(path) + Path.DirectorySeparatorChar;
            foreach (var file in files)
            {
                string full = Path.GetFullPath(Path.Combine(path, file.Path));
                if (!full.StartsWith(root, StringComparison.OrdinalIgnoreCase)) return false;
                using var stream = File.OpenRead(full);
                if (Convert.ToHexString(SHA256.HashData(stream)) != file.Sha256) return false;
            }
            return true;
        }
        catch (Exception ex) when (ex is IOException or JsonException or UnauthorizedAccessException or ArgumentException) { return false; }
    }
}