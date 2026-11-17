using System;
using System.Collections.Generic;
using System.Linq;
using System.Text.Json.Serialization;

namespace SyndicateLauncher.Models;

/// <summary>One play session: the launcher owns the game process, so it knows when a game opened and closed.</summary>
public sealed class GameSession
{
    public DateTime StartedUtc { get; set; }
    public double Minutes { get; set; }
    public int Season { get; set; }
    /// <summary>The Syndicate menu was on. Kept for the record; it only matters for achievements (backend).</summary>
    public bool MenuOn { get; set; }
}

/// <summary>
/// The launcher's play history for one profile (Home: Total Playtime and 7-Day Activity), saved to
/// %AppData%\SyndicateLauncher\stats\&lt;profile&gt;.json. Achievements and V-Bucks live in the backend, not here.
/// </summary>
public sealed class PlayerStats
{
    public int Version { get; set; } = 1;
    /// <summary>Completed sessions, oldest first (StatsService keeps the last 4000).</summary>
    public List<GameSession> Sessions { get; set; } = new();

    [JsonIgnore] public double TotalMinutes => Sessions.Sum(s => s.Minutes);

    /// <summary>Sessions and minutes on each of the last 7 local days, oldest first.</summary>
    public IReadOnlyList<ActivityDay> SevenDayActivity()
    {
        var today = DateTime.Now.Date;
        var counts = new int[7];
        var minutes = new double[7];
        foreach (var s in Sessions)
        {
            int ago = (today - s.StartedUtc.ToLocalTime().Date).Days;
            if (ago is < 0 or > 6) continue;
            counts[6 - ago]++;
            minutes[6 - ago] += s.Minutes;
        }
        var result = new List<ActivityDay>(7);
        for (int i = 0; i < 7; i++)
        {
            var day = today.AddDays(i - 6);
            result.Add(new ActivityDay(day.ToString("ddd").ToUpperInvariant(), counts[i], minutes[i], day == today));
        }
        return result;
    }
}

/// <summary>One bar of the 7-Day Activity card. <see cref="Height"/> is set by the Home view model (0..1).</summary>
public sealed record ActivityDay(string Label, int Games, double Minutes, bool IsToday)
{
    public double Height { get; init; }
    public string Tooltip => Games == 0 ? "No games" :
        Games + (Games == 1 ? " game" : " games") + " · " + PlaytimeFormat.Duration(Minutes);
}

public static class PlaytimeFormat
{
    public static string Duration(double minutes)
    {
        if (minutes < 1) return "0m";
        var t = TimeSpan.FromMinutes(minutes);
        if (t.TotalHours < 1) return (int)t.TotalMinutes + "m";
        return (int)t.TotalHours + "h " + t.Minutes + "m";
    }
}
