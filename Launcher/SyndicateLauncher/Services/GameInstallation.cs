using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Security.Cryptography;
using System.Text.Json;
using SyndicateLauncher.Models;

namespace SyndicateLauncher.Services;

public sealed record ExpectedGameFile(string Name, long Length, string TailSha256);

public static class GameInstallation
{
    public static IReadOnlyList<string> Inspect(string pakDirectory, IEnumerable<ExpectedGameFile> expected)
    {
        var problems = new List<string>();
        foreach (var entry in expected)
        {
            if (Path.GetFileName(entry.Name) != entry.Name || !entry.Name.EndsWith(".pak", StringComparison.OrdinalIgnoreCase))
                throw new InvalidDataException("Invalid bundled game-file checklist.");
            var file = Path.Combine(pakDirectory, entry.Name);
            if (!File.Exists(file)) { problems.Add("Missing: " + entry.Name); continue; }
            try
            {
                using var stream = new FileStream(file, FileMode.Open, FileAccess.Read, FileShare.Read, 65536);
                if (stream.Length != entry.Length)
                { problems.Add("Wrong size / incomplete: " + entry.Name); continue; }
                var tail = new byte[(int)Math.Min(65536, stream.Length)];
                stream.Seek(-tail.Length, SeekOrigin.End);
                stream.ReadExactly(tail);
                if (!Convert.ToHexString(SHA256.HashData(tail)).Equals(entry.TailSha256, StringComparison.OrdinalIgnoreCase))
                    problems.Add("Different or damaged content index: " + entry.Name);
            }
            catch (IOException) { problems.Add("Cannot read: " + entry.Name); }
            catch (UnauthorizedAccessException) { problems.Add("Access denied: " + entry.Name); }
        }
        return problems;
    }

    public static void Validate(BuildInfo build, string runtime, Action<string> record)
    {
        if (build.Version != "10.40")
        {
            record("Skipping the 10.40 content checklist for Fortnite " + build.Version + ".");
            return;
        }

        var manifest = Path.Combine(runtime, "game-10.40.json");
        if (!File.Exists(manifest)) throw new FileNotFoundException("The launcher game-file checklist is missing. Extract the complete launcher ZIP again.", manifest);
        var expected = JsonSerializer.Deserialize<ExpectedGameFile[]>(File.ReadAllText(manifest));
        if (expected is null || expected.Length == 0) throw new InvalidDataException("The launcher game-file checklist is empty.");
        var paks = Path.GetFullPath(Path.Combine(Path.GetDirectoryName(build.ExecutablePath)!, "..", "..", "Content", "Paks"));
        var problems = Inspect(paks, expected);
        foreach (var problem in problems) record(problem);
        if (problems.Count > 0)
            throw new InvalidDataException("Fortnite's 10.40 content files are missing, incomplete or mixed. " +
                string.Join("; ", problems.Take(3)) + (problems.Count > 3 ? "; plus " + (problems.Count - 3) + " more" : "") +
                ". Re-extract the complete Fortnite 10.40 CL-9380822 game download, including every Content/Paks file. The launcher cannot recreate missing game assets.");
        record("Game content check passed: " + expected.Length + " core PAK sizes and index tails. This is not a full-file checksum.");
    }
}
