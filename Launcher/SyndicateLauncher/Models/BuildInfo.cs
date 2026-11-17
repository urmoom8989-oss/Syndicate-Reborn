using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Threading.Tasks;
namespace SyndicateLauncher.Models;
public enum DetectionSource
{
    GameFiles,
    FolderName
}
/// <summary>A Fortnite build found in the folder the player selected.</summary>
public sealed record BuildInfo(
    string Version,
    int? Changelist,
    int Major,
    DetectionSource Source,
    string ExecutablePath)
{
    public bool IsKnown => Major > 0;
    public bool IsChapterOne => Major is >= 1 and <= 10;
    /// <summary>
    /// Chapter 1 ran from 1.x (Season 1) to 10.x (Season X),
    /// Chapter 2 from 11.x to 18.x.
    /// </summary>
    public string Era
    {
        get
        {
            if (!IsKnown) return "Version not recognised";
            return Version switch
            {
                "1.7.2" => "Chapter 1, Pre-Season",
                "1.8" or "1.8.2" => "Chapter 1, Season 1",
                "1.11" or "2.5" => "Chapter 1, Season 2",
                "3.5" or "3.5.1" => "Chapter 1, Season 3",
                "4.5" => "Chapter 1, Season 4",
                "5.10" => "Chapter 1, Season 5",
                "6.20" => "Chapter 1, Season 6",
                "7.40" => "Chapter 1, Season 7",
                "8.50" => "Chapter 1, Season 8",
                "9.40" => "Chapter 1, Season 9",
                "10.31" or "10.40" => "Chapter 1, Season X",
                _ when Major <= 10 => $"Chapter 1, Season {(Major == 10 ? "X" : Major.ToString())}",
                _ when Major <= 18 => $"Chapter 2, Season {Major - 10}",
                _ => "Chapter 3 or later"
            };
        }
    }
    public string SourceLabel => !IsKnown
        ? "Could not read a version"
        : Source == DetectionSource.GameFiles ? "Read from game files" : "Taken from folder name";
}
