using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text.Json;
using SyndicateLauncher.Models;

namespace SyndicateLauncher.Services;

/// <summary>One game mode a season can offer in Fortnite's lobby.</summary>
public sealed record GameModeInfo(string Id, string Name, string Description, bool Experimental);

/// <summary>
/// Settings &gt; Match &amp; bots &gt; Game modes: which modes each season shows in Fortnite's lobby. The choice is saved
/// in settings.json (AppSettings.SeasonModes, keyed by build) and written for the backend to
/// Runtime\Server\season-modes.json, which drives the lobby playlist list, the matchmaker and Save the World access
/// (backend Utils/syndicateSeasonModes.js; the mode lists here must match it). A build with no saved choice keeps the
/// lobby it always had. Creative (Seasons 7 and up) and Save the World are experimental.
/// </summary>
public static class SeasonModes
{
    public static readonly IReadOnlyList<GameModeInfo> All = new[]
    {
        new GameModeInfo("solo", "Solo", "Battle Royale on your own against bots.", false),
        new GameModeInfo("duo", "Duos", "Battle Royale in teams of two.", false),
        new GameModeInfo("squad", "Squads", "Battle Royale in teams of four.", false),
        new GameModeInfo("arenasolo", "Arena", "Solo Arena with Hype and divisions (Season 8 and up).", false),
        new GameModeInfo("50v50", "50v50", "Two teams of 50 with a storm line down the map (Seasons 2-6).", false),
        new GameModeInfo("gauntlet", "Infinity Gauntlet", "Season 4 crossover: find the Infinity Gauntlet and become Thanos. Experimental: the match server plays it once Erbium is rebuilt (needs Visual Studio).", true),
        new GameModeInfo("solidgold", "Solid Gold", "Every weapon on the map is legendary (Seasons 3-6). Experimental: the match server plays it once Erbium is rebuilt (needs Visual Studio).", true),
        new GameModeInfo("blitz", "Blitz", "Short matches with a fast storm and more loot (Seasons 3-6). Experimental: the match server plays it once Erbium is rebuilt (needs Visual Studio).", true),
        new GameModeInfo("snipers", "Sniper Shootout", "Sniper rifles and pistols only (Seasons 3-9). Experimental: the match server plays it once Erbium is rebuilt (needs Visual Studio).", true),
        new GameModeInfo("close", "Close Encounters", "Shotguns and jetpacks only (Seasons 4-9). Experimental: the match server plays it once Erbium is rebuilt (needs Visual Studio).", true),
        new GameModeInfo("highexplosives", "High Explosives", "Squads with explosive weapons only (Seasons 5-9). Experimental: the match server plays it once Erbium is rebuilt (needs Visual Studio).", true),
        new GameModeInfo("disco", "Disco Domination", "Two teams dance on dance floors to win (Seasons 6-7). Experimental: the match server plays it once Erbium is rebuilt (needs Visual Studio).", true),
        new GameModeInfo("teamrumble", "Team Rumble", "Two big teams with respawns race to the elimination target (Season 7 and up). Experimental: the match server plays it once Erbium is rebuilt (needs Visual Studio).", true),
        new GameModeInfo("foodfight", "Food Fight", "Durrr Burger against Pizza Pit, split by a barrier (Seasons 7-9). Experimental: the match server plays it once Erbium is rebuilt (needs Visual Studio).", true),
        new GameModeInfo("floorislava", "Floor Is Lava", "The lava rises through the match: build up to stay alive (Seasons 8-9). Experimental: the match server plays it once Erbium is rebuilt (needs Visual Studio).", true),
        new GameModeInfo("arenaduos", "Arena Duos", "Duos Arena with Hype and divisions (Season 8 and up). Experimental: the match server plays it once Erbium is rebuilt (needs Visual Studio).", true),
        new GameModeInfo("playground", "Playground", "Season 4's Playground: respawns, extra materials and no storm damage.", false),
        new GameModeInfo("creative", "Creative",
            "Experimental, Season 7 and up. The lobby button and playlist open and you go to the Creative hub, but player islands don't load yet (you may land in empty sea), and Season X's match server still starts a normal match.", true),
        new GameModeInfo("stw", "Save the World",
            "Experimental. Opens the Save the World button and gives your players Save the World access. Only Pre-season and Season 1 reach a mission (the tutorial) so far; on other seasons you get the Save the World lobby but missions don't start yet.", true),
    };

    private static readonly string[] BattleRoyale = { "solo", "duo", "squad" };
    /// <summary>Limited-time modes: their playlist and the builds that have them (backend LTM list in Utils/syndicateSeasonModes.js).</summary>
    public static readonly IReadOnlyDictionary<string, (string Playlist, string[] Builds)> LimitedTime =
        new Dictionary<string, (string, string[])>(StringComparer.OrdinalIgnoreCase)
        {
            ["50v50"] = ("Playlist_50v50", new[] { "1.11", "2.5", "3.5.1", "4.5", "5.10", "6.20" }),
            ["gauntlet"] = ("Playlist_Ashton_Sm", new[] { "4.5" }),
            ["solidgold"] = ("Playlist_SolidGold_Solo", new[] { "3.5.1", "4.5", "5.10", "6.20" }),
            ["blitz"] = ("Playlist_Blitz_Solo", new[] { "3.5.1", "4.5", "5.10", "6.20" }),
            ["snipers"] = ("Playlist_Snipers_Solo", new[] { "3.5.1", "4.5", "5.10", "6.20", "7.40", "8.50", "9.40" }),
            ["close"] = ("Playlist_Close_Solo", new[] { "4.5", "5.10", "6.20", "7.40", "8.50", "9.40" }),
            ["highexplosives"] = ("Playlist_HighExplosives_Squads", new[] { "5.10", "6.20", "7.40", "8.50", "9.40" }),
            ["disco"] = ("Playlist_Disco_32", new[] { "6.20", "7.40" }),
            ["teamrumble"] = ("Playlist_Respawn_24", new[] { "7.40", "8.50", "9.40", "10.31", "10.40", "11.00", "12.50" }),
            ["foodfight"] = ("Playlist_Barrier_16", new[] { "7.40", "8.50", "9.40" }),
            ["floorislava"] = ("Playlist_Respawn_20_Lava", new[] { "8.50", "9.40" }),
            ["arenaduos"] = ("Playlist_ShowdownAlt_Duos", new[] { "8.50", "9.40", "10.31", "10.40", "11.00", "12.50" }),
        };
    private static readonly HashSet<string> ArenaBuilds = new(StringComparer.OrdinalIgnoreCase) { "8.50", "9.40", "10.31", "10.40", "11.00", "12.50" };
    private static readonly HashSet<string> CreativeBuilds = new(StringComparer.OrdinalIgnoreCase) { "7.40", "8.50", "9.40", "10.31", "10.40", "11.00", "12.50" };
    private static readonly HashSet<string> KnownBuilds = new(StringComparer.OrdinalIgnoreCase)
        { "1.7.2", "1.8.2", "1.11", "2.5", "3.5.1", "4.5", "5.10", "6.20", "7.40", "8.50", "9.40", "10.31", "10.40", "11.00", "12.50" };

    private static readonly object Gate = new();
    private static Dictionary<string, List<string>> _saved = new(StringComparer.OrdinalIgnoreCase);
    private static Dictionary<string, SeasonLayout> _layouts = new(StringComparer.OrdinalIgnoreCase);
    private static readonly JsonSerializerOptions BackendJson = new() { PropertyNamingPolicy = JsonNamingPolicy.CamelCase };

    /// <summary>The modes a build can offer at all, in lobby order.</summary>
    public static IReadOnlyList<string> Available(string build)
    {
        if (!KnownBuilds.Contains(build ?? "")) return Array.Empty<string>();
        var list = new List<string>(BattleRoyale);
        if (ArenaBuilds.Contains(build!)) list.Add("arenasolo");
        foreach (var (mode, ltm) in LimitedTime)
            if (Array.IndexOf(ltm.Builds, build) >= 0) list.Add(mode);
        if (build == "4.5") list.Add("playground");
        if (CreativeBuilds.Contains(build!)) list.Add("creative");
        list.Add("stw");
        return list;
    }

    /// <summary>
    /// Rows in the build's mode selector (FrontEndPlaylistData CategoryIndex). Season X's screen has three: standard
    /// modes and two limited-time rows. Other seasons get two; Season 4 lists PlaylistOverrides, which have no rows.
    /// Must match lobbyRowCount in the backend's Utils/syndicateSeasonModes.js.
    /// </summary>
    public static int RowCount(string build)
    {
        if (!KnownBuilds.Contains(build ?? "")) return 0;
        if (build == "4.5") return 1;
        return build is "10.31" or "10.40" ? 3 : 2;
    }

    /// <summary>The lobby each build had before modes could be chosen.</summary>
    public static IReadOnlyList<string> Defaults(string build)
    {
        if (!KnownBuilds.Contains(build ?? "")) return Array.Empty<string>();
        var list = new List<string>(BattleRoyale);
        if (ArenaBuilds.Contains(build!)) list.Add("arenasolo");
        if (build == "4.5") { list.Add("50v50"); list.Add("playground"); }
#if DEV_BUILD
        if (build == "11.00") list.Add("creative"); // the Dev Kit backend has always shown Creative on 11.00
#endif
        return list;
    }

    /// <summary>Called with the saved settings at startup.</summary>
    public static void Load(Dictionary<string, List<string>>? saved, Dictionary<string, SeasonLayout>? layouts = null)
    {
        lock (Gate)
        {
            _saved = new Dictionary<string, List<string>>(StringComparer.OrdinalIgnoreCase);
            _layouts = new Dictionary<string, SeasonLayout>(StringComparer.OrdinalIgnoreCase);
            if (saved is not null)
                foreach (var (build, modes) in saved)
                    if (!string.IsNullOrWhiteSpace(build) && modes is not null) _saved[build] = Clean(build, modes);
            if (layouts is not null)
                foreach (var (build, layout) in layouts)
                    if (!string.IsNullOrWhiteSpace(build) && layout is not null) _layouts[build] = Copy(layout);
        }
    }

    /// <summary>The saved lobby layout for a build (a copy), or an empty one: usual order, first mode default.</summary>
    public static SeasonLayout Layout(string build)
    {
        lock (Gate) return _layouts.TryGetValue(build ?? "", out var layout) ? Copy(layout) : new SeasonLayout();
    }

    /// <summary>Saves a build's lobby layout and returns the full map to store in settings.json.</summary>
    public static Dictionary<string, SeasonLayout> SetLayout(string build, SeasonLayout layout)
    {
        lock (Gate)
        {
            _layouts[build] = Copy(layout);
            return LayoutSnapshot();
        }
    }

    private static SeasonLayout Copy(SeasonLayout layout) => new()
    {
        Order = (layout.Order ?? new()).Where(m => !string.IsNullOrEmpty(m)).ToList(),
        Default = layout.Default ?? "",
        Badges = (layout.Badges ?? new()).Where(pair => pair.Value is "new" or "limited")
            .ToDictionary(pair => pair.Key, pair => pair.Value, StringComparer.OrdinalIgnoreCase),
        Locked = (layout.Locked ?? new()).Where(m => !string.IsNullOrEmpty(m)).ToList(),
        Rows = (layout.Rows ?? new()).Where(pair => !string.IsNullOrEmpty(pair.Key) && pair.Value > 0)
            .ToDictionary(pair => pair.Key, pair => Math.Min(pair.Value, 2), StringComparer.OrdinalIgnoreCase)
    };

    private static Dictionary<string, SeasonLayout> LayoutSnapshot() =>
        _layouts.ToDictionary(pair => pair.Key, pair => Copy(pair.Value), StringComparer.OrdinalIgnoreCase);

    /// <summary>The modes turned on for a build, in lobby order.</summary>
    public static IReadOnlyList<string> Enabled(string build)
    {
        lock (Gate)
            return _saved.TryGetValue(build ?? "", out var list) ? list.ToList() : Defaults(build ?? "").ToList();
    }

    public static bool IsEnabled(string build, string mode) => Enabled(build).Contains(mode, StringComparer.OrdinalIgnoreCase);

    /// <summary>Saves a new choice for a build and returns the full map to store in settings.json.</summary>
    public static Dictionary<string, List<string>> Set(string build, IEnumerable<string> modes)
    {
        lock (Gate)
        {
            _saved[build] = Clean(build, modes);
            return Snapshot();
        }
    }

    /// <summary>Back to the lobby the build had before (no saved modes and no saved layout).</summary>
    public static (Dictionary<string, List<string>> Modes, Dictionary<string, SeasonLayout> Layouts) Reset(string build)
    {
        lock (Gate)
        {
            _saved.Remove(build);
            _layouts.Remove(build);
            return (Snapshot(), LayoutSnapshot());
        }
    }

    private static Dictionary<string, List<string>> Snapshot() =>
        _saved.ToDictionary(pair => pair.Key, pair => pair.Value.ToList(), StringComparer.OrdinalIgnoreCase);

    // Only modes the build offers, in lobby order, and at least one Battle Royale mode so the lobby is never empty.
    private static List<string> Clean(string build, IEnumerable<string> modes)
    {
        var picked = new HashSet<string>(modes.Where(m => m is not null), StringComparer.OrdinalIgnoreCase);
        var list = Available(build).Where(picked.Contains).ToList();
        if (!list.Any(m => m is not ("creative" or "stw"))) list.Insert(0, "solo");
        return list;
    }

    /// <summary>
    /// Writes Runtime\Server\season-modes.json for the backend (next to Syndicate-backend). Best-effort: when the
    /// backend can't be found yet, the next launch writes it again before Fortnite starts.
    /// </summary>
    public static void WriteForBackend()
    {
        try
        {
            string? runtime = SyndicateBackend.RuntimeDirectory;
            if (string.IsNullOrEmpty(runtime)) return;
            Directory.CreateDirectory(runtime);
            Dictionary<string, List<string>> builds;
            Dictionary<string, SeasonLayout> layout;
            lock (Gate) { builds = Snapshot(); layout = LayoutSnapshot(); }
            string file = Path.Combine(runtime, "season-modes.json");
            // Property names in camelCase (order, default, badges, locked); build and mode keys stay as they are.
            string json = JsonSerializer.Serialize(new { version = 2, builds, layout }, BackendJson);
            if (File.Exists(file) && File.ReadAllText(file) == json) return;
            File.WriteAllText(file + ".tmp", json);
            File.Move(file + ".tmp", file, overwrite: true);
        }
        catch
        {
            // The backend keeps its last copy (or its defaults); the next launch tries again.
        }
    }
}
