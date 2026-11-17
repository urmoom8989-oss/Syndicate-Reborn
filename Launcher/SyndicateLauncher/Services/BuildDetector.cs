using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Threading.Tasks;
using System.Diagnostics;
using System.Text;
using System.Text.RegularExpressions;
using SyndicateLauncher.Models;
namespace SyndicateLauncher.Services;
public sealed record DetectionResult(BuildInfo? Build, string? Error);
/// <summary>
/// Works out which Fortnite build lives in a folder.
///
/// 1. Finds FortniteClient-Win64-Shipping.exe (the folder can be the build root,
///    the FortniteGame folder, or Binaries\Win64).
/// 2. Reads the build tag "++Fortnite+Release-1.7.2-CL-3700114" from the exe's
///    version resource, or by scanning the exe's bytes for it.
/// 3. Falls back to a version number in the folder name (e.g. "Fortnite 1.7.2").
/// </summary>
public static class BuildDetector
{
    public const string ClientExe = "FortniteClient-Win64-Shipping.exe";
    private static readonly string[] ExeLocations =
    {
        Path.Combine("FortniteGame", "Binaries", "Win64", ClientExe),
        Path.Combine("Binaries", "Win64", ClientExe),
        Path.Combine("Win64", ClientExe),
        ClientExe
    };
    private static readonly Regex ReleaseTag = new(
        @"\+\+Fortnite\+Release-(\d{1,2}(?:\.\d{1,2}){1,2})-CL-(\d{5,9})",
        RegexOptions.Compiled);
    private static readonly Regex ChangelistTag = new(@"CL-(\d{5,9})", RegexOptions.Compiled);
    /// <summary>Syndicate's exact build markers (some old builds carry no version in their tag).</summary>
    private static string? DetectMarker(string text)
    {
        if (Regex.IsMatch(text, @"\+\+Fortnite\+Release-3\.5-CL-4000805(?![0-9])")) return "3.5.1";
        if (Regex.IsMatch(text, @"\+\+Fortnite\+Release-(?:2\.5|Cert)-CL-3889387(?![0-9])")) return "2.5";
        if (text.Contains("++Fortnite+Release-Cert-CL-3807424", StringComparison.Ordinal)) return "1.11";
        if (text.Contains("++Fortnite+Release-4.5-CL-4166199", StringComparison.Ordinal)) return "4.5";
        if (text.Contains("++Fortnite+Release-Live-CL-3741772", StringComparison.Ordinal) ||
            text.Contains("++Fortnite+Release-Cert-CL-3741772", StringComparison.Ordinal)) return "1.8.2";
        if (text.Contains("++Fortnite+Release-Live-CL-3724489", StringComparison.Ordinal)) return "1.8";
        if (text.Contains("++Fortnite+Release-Live-CL-3700114", StringComparison.Ordinal) ||
            text.Contains("++Fortnite+Release-Cert-CL-3700114", StringComparison.Ordinal)) return "1.7.2";
        var later = Regex.Match(text, @"\+\+Fortnite\+Release-(6\.20|7\.40|8\.50|9\.40|10\.31|10\.40|11\.00)-CL-[0-9]+", RegexOptions.CultureInvariant);
        return later.Success ? later.Groups[1].Value : null;
    }
    private static readonly Regex FolderVersion = new(
        @"(?<![\d.])(\d{1,2}\.\d{1,2}(?:\.\d{1,2})?)(?![\d.])",
        RegexOptions.Compiled);
    private static readonly byte[] AsciiNeedle = Encoding.ASCII.GetBytes("++Fortnite+Release-");
    private static readonly byte[] Utf16Needle = Encoding.Unicode.GetBytes("++Fortnite+Release-");
    private const int ChunkSize = 4 * 1024 * 1024;
    private const int TailLength = 256; // keeps a tag that straddles two chunks intact
    public static DetectionResult Detect(string directory)
    {
        if (string.IsNullOrWhiteSpace(directory) || !Directory.Exists(directory))
            return new DetectionResult(null, "That folder doesn't exist any more. Pick it again.");
        var exe = FindExecutable(directory);
        if (exe is null)
        {
            return new DetectionResult(null,
                $"{ClientExe} wasn't found. Pick the folder that contains FortniteGame and Engine.");
        }
        var (version, changelist) = ReadFromExecutable(exe);
        var source = DetectionSource.GameFiles;
        if (version is null)
        {
            version = ReadFromFolderNames(directory);
            source = DetectionSource.FolderName;
        }
        if (version is null)
            return new DetectionResult(new BuildInfo("Unknown", null, 0, DetectionSource.GameFiles, exe), null);
        int major = int.TryParse(version.Split('.')[0], out var m) ? m : 0;
        return new DetectionResult(new BuildInfo(version, changelist, major, source, exe), null);
    }
    /// <summary>True when <paramref name="directory"/> holds the Fortnite client exe (cheap: no version read).</summary>
    public static bool HasExecutable(string directory) => FindExecutable(directory) is not null;

    private static string? FindExecutable(string directory)
    {
        foreach (var relative in ExeLocations)
        {
            var candidate = Path.Combine(directory, relative);
            if (File.Exists(candidate)) return candidate;
        }
        return null;
    }
    private static (string? Version, int? Changelist) ReadFromExecutable(string exePath)
    {
        try
        {
            var info = FileVersionInfo.GetVersionInfo(exePath);
            foreach (var text in new[] { info.ProductVersion, info.FileVersion, info.ProductName, info.Comments })
            {
                if (string.IsNullOrEmpty(text)) continue;
                var marker = DetectMarker(text);
                if (marker is not null) return (marker, ChangelistFrom(text));
                var match = ReleaseTag.Match(text);
                if (match.Success) return (match.Groups[1].Value, ParseChangelist(match.Groups[2].Value));
            }
        }
        catch
        {
            // fall through to the binary scan
        }
        try
        {
            return ScanBinary(exePath);
        }
        catch
        {
            return (null, null);
        }
    }
    private static (string? Version, int? Changelist) ScanBinary(string path)
    {
        using var stream = new FileStream(path, FileMode.Open, FileAccess.Read, FileShare.ReadWrite,
            1 << 16, FileOptions.SequentialScan);
        var buffer = new byte[ChunkSize + TailLength];
        int carry = 0;
        while (true)
        {
            int read = stream.Read(buffer, carry, ChunkSize);
            if (read <= 0) break;
            int length = carry + read;
            var window = new ReadOnlySpan<byte>(buffer, 0, length);
            var hit = Probe(window, AsciiNeedle, 1);
            if (hit.Version is null) hit = Probe(window, Utf16Needle, 2);
            if (hit.Version is not null) return hit;
            carry = Math.Min(TailLength, length);
            Buffer.BlockCopy(buffer, length - carry, buffer, 0, carry);
        }
        return (null, null);
    }
    private static (string? Version, int? Changelist) Probe(ReadOnlySpan<byte> data, byte[] needle, int bytesPerChar)
    {
        var encoding = bytesPerChar == 2 ? Encoding.Unicode : Encoding.ASCII;
        int offset = 0;
        while (offset < data.Length)
        {
            int index = data.Slice(offset).IndexOf(needle);
            if (index < 0) break;
            int start = offset + index;
            int take = Math.Min(data.Length - start, (needle.Length / bytesPerChar + 48) * bytesPerChar);
            var text = encoding.GetString(data.Slice(start, take));
            // The tag must be followed by at least one more character (the string's terminator).
            // Otherwise it may have been cut off at the end of this chunk, and the next
            // pass - which re-reads the tail - will see it in full.
            var marker = DetectMarker(text);
            if (marker is not null && text.Length > 10)
                return (marker, ChangelistFrom(text));
            var match = ReleaseTag.Match(text);
            if (match.Success && match.Index == 0 && match.Length < text.Length)
                return (match.Groups[1].Value, ParseChangelist(match.Groups[2].Value));
            offset = start + 1;
        }
        return (null, null);
    }
    private static string? ReadFromFolderNames(string directory)
    {
        DirectoryInfo? dir = new DirectoryInfo(directory);
        for (int i = 0; i < 3 && dir is not null; i++)
        {
            var match = FolderVersion.Match(dir.Name);
            if (match.Success) return match.Groups[1].Value;
            dir = dir.Parent;
        }
        return null;
    }
    private static int? ParseChangelist(string text) => int.TryParse(text, out var cl) ? cl : null;

    private static int? ChangelistFrom(string text)
    {
        var match = ChangelistTag.Match(text);
        return match.Success ? ParseChangelist(match.Groups[1].Value) : null;
    }
}
