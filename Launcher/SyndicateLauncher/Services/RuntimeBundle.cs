using System;
using System.IO;
using System.IO.Compression;
using System.Reflection;
using System.Security.Cryptography;
using System.Threading;
using System.Text.Json;
using System.Collections.Generic;

namespace SyndicateLauncher.Services;

public static class RuntimeBundle
{
    private const long ExpandedLimit = 512L * 1024 * 1024;
    private static readonly string[] Required = {
        "dlls/manifest.json", "dlls/Erbium.dll", "dlls/cobalt.dll", "dlls/memory.dll", "dlls/console.dll",
        "dlls/Syndicate1040.dll", "dlls/SyndicateConsoleRecovery.dll", "dlls/SyndicateMenu.dll", "game-10.40.json", "backend/lawinserver.exe",
        "backend/titanium-profiles.cjs", "backend/titanium-modes.cjs", "backend/CloudStorage/DefaultEngine.ini", "backend/CloudStorage/DefaultInput.ini", "backend/CloudStorage/DefaultGame.ini"
    };
    private static bool Complete(string root)
    {
        foreach (var file in Required) if (!File.Exists(Path.Combine(root, file))) return false;
        return true;
    }
    private static bool VerifiedCache(string root, string identity, string manifestHash)
    {
        try
        {
            if (!Complete(root) || File.ReadAllText(Path.Combine(root, ".ready")) != identity) return false;
            using (var file = File.OpenRead(Path.Combine(root, "dlls", "manifest.json")))
                if (Convert.ToHexString(SHA256.HashData(file)) != manifestHash) return false;
            var hashes = JsonSerializer.Deserialize<Dictionary<string, string>>(File.ReadAllText(Path.Combine(root, "dlls", "manifest.json")));
            if (hashes is null) return false;
            foreach (var name in ModuleStartup.RequiredModules)
            {
                using var input = File.OpenRead(Path.Combine(root, "dlls", name));
                if (!hashes.TryGetValue(name, out var digest) || !Convert.ToHexString(SHA256.HashData(input)).Equals(digest, StringComparison.OrdinalIgnoreCase)) return false;
            }
            return true;
        }
        catch (Exception ex) when (ex is IOException or UnauthorizedAccessException or JsonException) { return false; }
    }
    public static string Resolve() => Resolve(AppContext.BaseDirectory,
        Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "SyndicateLauncher", "Runtime"),
        () => Assembly.GetExecutingAssembly().GetManifestResourceStream("SyndicateLauncher.Runtime.zip"));

    public static string Resolve(string baseDirectory, string cacheDirectory, Func<Stream?> openBundle)
    {
        var external = Path.GetFullPath(Path.Combine(baseDirectory, "Runtime"));
        if (Complete(external)) return external;
        using var stream = openBundle();
        if (stream is null)
            throw new InvalidOperationException("Syndicate's required files are missing. Extract the entire launcher archive into a normal folder, then open SyndicateLauncher.exe there. Do not run it inside the ZIP/RAR preview.");
        if (!stream.CanSeek) throw new InvalidDataException("The bundled launcher files could not be read. Download the complete launcher again.");
        var identity = Convert.ToHexString(SHA256.HashData(stream)).ToLowerInvariant();
        stream.Position = 0;
        string manifestHash;
        using (var archive = new ZipArchive(stream, ZipArchiveMode.Read, leaveOpen: true))
        {
            var manifest = archive.GetEntry("dlls/manifest.json") ?? throw new InvalidDataException("The launcher bundle has no file manifest.");
            using var input = manifest.Open();
            manifestHash = Convert.ToHexString(SHA256.HashData(input));
        }
        stream.Position = 0;
        var cacheRoot = Path.GetFullPath(cacheDirectory);
        var destination = Path.Combine(cacheRoot, identity);
        using var mutex = new Mutex(false, "Local\\SyndicateRuntime-" + identity);
        bool locked;
        try { locked = mutex.WaitOne(TimeSpan.FromSeconds(20)); }
        catch (AbandonedMutexException) { locked = true; }
        if (!locked) throw new IOException("Another Syndicate launcher is preparing its files. Wait a moment, then press Play again.");
        try
        {
            if (VerifiedCache(destination, identity, manifestHash)) return destination;
            Directory.CreateDirectory(cacheRoot);
            var staging = Path.Combine(cacheRoot, identity + ".prepare-" + Guid.NewGuid().ToString("N"));
            Directory.CreateDirectory(staging);
            try
            {
                using var zip = new ZipArchive(stream, ZipArchiveMode.Read, leaveOpen: true);
                if (zip.Entries.Count > 4096) throw new InvalidDataException("Invalid launcher file bundle.");
                long expanded = 0;
                var prefix = Path.GetFullPath(staging) + Path.DirectorySeparatorChar;
                foreach (var entry in zip.Entries)
                {
                    var name = entry.FullName.Replace('\\', '/');
                    if (name.IndexOf(':') >= 0 || name.StartsWith('/') || name.IndexOf('\0') >= 0)
                        throw new InvalidDataException("Invalid launcher file path.");
                    var target = Path.GetFullPath(Path.Combine(staging, name));
                    if (!target.StartsWith(prefix, StringComparison.OrdinalIgnoreCase))
                        throw new InvalidDataException("Invalid launcher file path.");
                    expanded = checked(expanded + entry.Length);
                    if (expanded > ExpandedLimit) throw new InvalidDataException("Invalid launcher file bundle size.");
                    if (name.EndsWith('/')) { Directory.CreateDirectory(target); continue; }
                    Directory.CreateDirectory(Path.GetDirectoryName(target)!);
                    using var input = entry.Open();
                    using var output = new FileStream(target, FileMode.CreateNew, FileAccess.Write, FileShare.None);
                    input.CopyTo(output);
                    if (output.Length != entry.Length) throw new InvalidDataException("Incomplete bundled launcher file.");
                }
                if (!Complete(staging)) throw new InvalidDataException("The launcher file bundle is incomplete. Download the complete launcher again.");
                File.WriteAllText(Path.Combine(staging, ".ready"), identity);
                if (!VerifiedCache(staging, identity, manifestHash)) throw new InvalidDataException("The bundled launcher modules could not be verified. Download the complete launcher again.");
                // Only a failed, uncommitted cache for this exact embedded
                // bundle may be replaced; saved profiles live elsewhere.
                if (Directory.Exists(destination))
                {
                    var abandoned = destination + ".incomplete-" + Guid.NewGuid().ToString("N");
                    Directory.Move(destination, abandoned);
                }
                Directory.Move(staging, destination);
                return destination;
            }
            finally
            {
                if (Directory.Exists(staging) && Path.GetFullPath(staging).StartsWith(cacheRoot + Path.DirectorySeparatorChar, StringComparison.OrdinalIgnoreCase))
                    Directory.Delete(staging, recursive: true);
            }
        }
        catch (UnauthorizedAccessException ex)
        { throw new IOException("Syndicate could not prepare its files in your local app folder. Check that the folder is writable and try again.", ex); }
        finally { mutex.ReleaseMutex(); }
    }
}
