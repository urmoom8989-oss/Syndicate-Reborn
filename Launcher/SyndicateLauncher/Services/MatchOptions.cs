using System;
using System.Collections.Generic;
using SyndicateLauncher.Models;
using System.Diagnostics;
using System.Text.RegularExpressions;
namespace SyndicateLauncher.Services;
public sealed record MatchOptions(int MinimumBots, int MaximumBots, bool EditOnRelease)
{
    public BotPreset Difficulty {get;init;}=BotPreset.Builtin(2);
    public bool HighTickFullLobbies { get; init; }
    public bool LateGame { get; init; }
    /// <summary>Keep the next match server loading in the background during a match (Settings &gt; Bots &gt; Fast queue).</summary>
    public bool FastQueue { get; init; } = true;
    public IReadOnlyList<string> BotNames { get; init; } = Array.Empty<string>();
    /// <summary>The player's own bot names (Settings &gt; Bots &gt; Bot names). Seasons 1-9: each goes to a random bot every match.</summary>
    public IReadOnlyList<string> CustomBotNames { get; init; } = Array.Empty<string>();
    /// <summary>The playlist the match host started with. It follows the playlist picked in Fortnite's lobby.</summary>
    public string Playlist { get; init; } = "solo";
    public static readonly string[] Playlists = { "solo", "duo", "squad", "50v50", "playground", "arenasolo", "creative", "intro" };
    public static string NormalizePlaylist(string? mode) => (mode ?? "").Trim().ToLowerInvariant() switch
    {
        "duos" => "duo",
        "squads" => "squad",
        var value when Array.IndexOf(Playlists, value) >= 0 => value,
        _ => "solo"
    };
    public static bool IsPlaylist(string? mode) => Array.IndexOf(Playlists, (mode ?? "").Trim().ToLowerInvariant()) >= 0;
    public static string PlaylistName(string mode) => NormalizePlaylist(mode) switch
    {
        "duo" => "Duos",
        "squad" => "Squads",
        "50v50" => "50v50",
        "playground" => "Playground",
        "arenasolo" => "Arena Solo",
        "creative" => "Creative",
        "intro" => "Chapter 2 entrance",
        _ => "Solo"
    };
    public static MatchOptions Parse(string? text, bool editOnRelease)
    {
        text=(text??"").Trim().Replace('\u2013','-').Replace('\u2014','-');
        if(text.Length==0)return new(99,99,editOnRelease);
        var m=Regex.Match(text,@"^([0-9]{1,2})(?:\s*-\s*([0-9]{1,2}))?$");
        if(!m.Success)throw new InvalidOperationException("Enter a bot count from 0 to 99, or a range such as 40-60. Leave blank for 99.");
        int low=int.Parse(m.Groups[1].Value),high=m.Groups[2].Success?int.Parse(m.Groups[2].Value):low;
        if(low>high)throw new InvalidOperationException("The minimum bot count must not exceed the maximum.");
        return new(low,high,editOnRelease);
    }

    /// <summary>Validates one editable bot name. An empty result means the name is valid.</summary>
    public static string ValidateBotName(string? text)
    {
        var name = (text ?? "").Trim();
        if (name.Length < 3 || name.Length > 16)
            return "Bot names must be 3 to 16 characters long.";
        foreach (var ch in name)
            if (ch == '|' || char.IsControl(ch))
                return "Bot names cannot contain | or control characters.";
        return "";
    }

    public void Apply(ProcessStartInfo info, bool host)
    {
        if(MinimumBots<0||MaximumBots>99||MinimumBots>MaximumBots)throw new InvalidOperationException("Invalid bot count range.");
        if(host)
        {
            info.ArgumentList.Add("-SyndicateDifficulty="+Difficulty.Argument());
            info.ArgumentList.Add("-SyndicateBotsMin="+MinimumBots);
            info.ArgumentList.Add("-SyndicateBotsMax="+MaximumBots);
            // Season X reads Late game from its command line (the Chapter 1 seasons use TILTED_LATE_GAME).
            if (LateGame) info.ArgumentList.Add("-SyndicateLateGame=1");
            if (BotNames.Count > 0) info.Environment["SYNDICATE_BOT_NAMES"] = string.Join("|", BotNames);
            else info.Environment.Remove("SYNDICATE_BOT_NAMES");
            if (CustomBotNames.Count > 0) info.Environment["SYNDICATE_CUSTOM_BOT_NAMES"] = string.Join("|", CustomBotNames);
            else info.Environment.Remove("SYNDICATE_CUSTOM_BOT_NAMES");
        }
        else info.ArgumentList.Add("-SyndicateEditOnRelease="+(EditOnRelease?"1":"0"));
    }
}
