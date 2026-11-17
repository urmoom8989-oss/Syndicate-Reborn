using System.Collections.Generic;

namespace SyndicateLauncher.Models;

/// <summary>One group of changes on the Patch Notes page ("Bots", "Pre-season", ...).</summary>
public sealed record PatchNoteSection(string Title, IReadOnlyList<string> Items)
{
    public string Count => Items.Count == 1 ? "1 change" : Items.Count + " changes";
}

/// <summary>A headline card at the top of a release.</summary>
public sealed record PatchHighlight(string Title, string Text)
{
    /// <summary>"01", "02", ... set by <see cref="PatchNotes"/> so the cards can be numbered.</summary>
    public string Number { get; init; } = "";
}

/// <summary>One launcher release on the Patch Notes page.</summary>
public sealed record PatchRelease(
    string Version,
    string Date,
    string Title,
    string Summary,
    IReadOnlyList<PatchHighlight> Highlights,
    IReadOnlyList<PatchNoteSection> Sections,
    IReadOnlyList<string> KnownIssues)
{
    public bool HasKnownIssues => KnownIssues.Count > 0;
    public string VersionLabel => "v" + Version;
    public int ChangeCount
    {
        get { int n = 0; foreach (var s in Sections) n += s.Items.Count; return n; }
    }
    public string ChangeCountText => ChangeCount + " changes";
    /// <summary>Highlights with their card numbers filled in.</summary>
    public IReadOnlyList<PatchHighlight> NumberedHighlights
    {
        get
        {
            var list = new List<PatchHighlight>();
            for (int i = 0; i < Highlights.Count; i++) list.Add(Highlights[i] with { Number = (i + 1).ToString("00") });
            return list;
        }
    }
}

/// <summary>
/// The text players read on the Patch Notes tab. Newest release first. Add a new PatchRelease at the top of
/// <see cref="Releases"/> for every public release and bump &lt;Version&gt; in SyndicateLauncher.csproj to match:
/// the launcher opens this page once after it updates to a version the player hasn't seen.
/// </summary>
public static class PatchNotes
{
    public static IReadOnlyList<PatchRelease> Releases { get; } = new[]
    {
        new PatchRelease(
            Version: "1.3.1",
            Date: "November 17, 2026",
            Title: "EDIT + AUTO RUN FIXES",
            Summary: "Edit on release no longer leaves you stuck in edit mode, auto run works again, and Season X fast queue is back.",
            Highlights: new[]
            {
                new PatchHighlight("Edit on release",
                    "Quick taps confirm the edit every time, in every season."),
                new PatchHighlight("Auto run",
                    "The auto run key works again and keeps running until you stop."),
            },
            Sections: new[]
            {
                new PatchNoteSection("CONTROLS", new[]
                {
                    "Edit on release no longer gets stuck: a quick tap waits for the edit tool instead of giving up, and a confirm the game dropped is pressed again.",
                    "Season X: edits are no longer confirmed twice, which could reopen the edit.",
                    "Auto run works again and keeps working through the whole match, even after alt-tabbing.",
                }),
                new PatchNoteSection("MATCHES", new[]
                {
                    "Season X: fixed the background server crashing while it loads the island, so fast queue works again.",
                }),
            },
            KnownIssues: new[]
            {
                "Some players crash when opening settings in Season 2. We're looking into it. If it happens to you, send your logs in the Discord.",
            }),
        new PatchRelease(
            Version: "1.3.0",
            Date: "November 17, 2026",
            Title: "LATE GAME EVERYWHERE",
            Summary: "Late game comes to Season X with random loadouts, bots get sharper in fights and drops, the in-game "
                   + "console works on every season, wins pay more V-Bucks and Syndicate opens in seconds.",
            Highlights: new[]
            {
                new PatchHighlight("Late game on Season X",
                    "Turn it on in Settings > Bots. The bus flies over the final circles and everyone lands with a random loadout."),
                new PatchHighlight("Instant start",
                    "No more \"Preparing verified game files\" every time you open Syndicate. It only runs after an update."),
                new PatchHighlight("More V-Bucks",
                    "100 V-Bucks per kill and 1,500 per win, in every season."),
                new PatchHighlight("Console on every season",
                    "Press ~ in any Chapter 1 season to open the in-game console."),
            },
            Sections: new[]
            {
                new PatchNoteSection("LATE GAME", new[]
                {
                    "Late game now works on Season X: the bus passes over the final circles, the storm forms as the bus leaves and the first circles close almost instantly.",
                    "Everyone gets a random loadout: a rifle, a shotgun, an SMG or heals, a sniper or explosive, a heal and sometimes a trap, plus materials and full shield.",
                    "Season X: the bus doors stay shut until the final circle shows on the map, so you always see where to land before you jump.",
                    "Season 2: you stay in the bus during the pass and jump when you want, instead of being dropped over the circle.",
                    "Bots leave the bus in time, land at about the same time as you, and no longer land on water or steep slopes.",
                }),
                new PatchNoteSection("BOTS", new[]
                {
                    "The Bot and Easy difficulties are back, next to Medium, Hard and Variety. Your saved Bot or Easy presets are kept.",
                    "New ramp-wall rush: Medium and harder bots put a wall on their ramp as they rush you, like real players.",
                    "Fixed bots aiming at you with empty hands after building, editing or farming.",
                    "Bots stuck ramping into a wall, mountain, tree or roof now drop the ramp and go around.",
                    "More bots carry spike traps and launch pads, and they use launch pads to rotate, especially near the storm.",
                }),
                new PatchNoteSection("V-BUCKS", new[]
                {
                    "Matches now pay 100 V-Bucks per kill and 1,500 per win (was 25 and 450), in every season.",
                }),
                new PatchNoteSection("MATCHES", new[]
                {
                    "Fixed \"The Syndicate server exited while loading the island\" (HOST-ISLAND-01) on PCs low on free memory (seen on Season 3 and Season 8).",
                    "Faster requeue: your next match now loads in the background on 16 GB PCs too, and leaving a match right after the previous one no longer falls back to a full server start.",
                    "The in-game console (~) now opens on every Chapter 1 season.",
                }),
                new PatchNoteSection("LAUNCHER", new[]
                {
                    "Syndicate opens in seconds: game files stay installed between sessions instead of being re-checked and rewritten every time.",
                    "Files are still fully checked after every update, and anything antivirus removed is put back automatically.",
                    "New start screen in the launcher's style, with a progress bar while an update installs.",
                    "Old versions' files are cleaned up after an update to save disk space.",
                    "Patch Notes is easier to read: pick a version, then filter by category. No more broken icons.",
                    "Updates now also come from the new Syndicate download page, so you always get the newest version.",
                }),
            },
            KnownIssues: new[]
            {
                "Some players crash when opening settings in Season 2. We're looking into it. If it happens to you, send your logs in the Discord.",
            }),
        new PatchRelease(
            Version: "1.2.0",
            Date: "November 17, 2026",
            Title: "SMARTER BOTS + INSTANT PLAY",
            Summary: "Everything we shipped from Sunday afternoon to Monday night: bots that hear and push you, a much "
                   + "faster PLAY button, custom crosshairs, a daylight fix for Pre-season and a long list of crash fixes.",
            Highlights: new[]
            {
                new PatchHighlight("Instant Play",
                    "Fortnite now starts while your match loads, so the queue is a lot shorter."),
                new PatchHighlight("Smarter bots",
                    "Bots hear gunfire, turn on shooters they can't see, build more, push real players and dig out of roofs."),
                new PatchHighlight("Custom crosshair",
                    "Presets or build your own, drawn in the exact centre of the screen in every season."),
                new PatchHighlight("Season key art",
                    "Every season page now has its own key art behind it."),
            },
            Sections: new[]
            {
                new PatchNoteSection("BOTS", new[]
                {
                    "Bots hear gunfire and react to shooters they can't see, instead of standing still.",
                    "Bots build more in fights.",
                    "Some bots are now pushers: they take the fight to real players.",
                    "Bots dig their way out when they get stuck on a roof, and get off roofs to revive teammates instead of circling.",
                    "Realistic skydiving: every bot glides its own way on curved paths, and squad teammates jump before the drop window closes.",
                    "Knocked bots crawl away like real players.",
                    "Bots carry traps and launch pads again.",
                    "Bots hold their heal item and play its animation while healing.",
                    "Better bot movement around terrain and when fighting on different heights.",
                    "Season 7: bots land with weapons again, and glider redeploy is on.",
                    "Every match now has a fresh mix of random bot names. Add up to 99 of your own in Settings > Bots > Bot names.",
                    "The Bot and Easy difficulties are gone. If you used one, you're moved to Medium.",
                }),
                new PatchNoteSection("LATE GAME", new[]
                {
                    "Late game is available on Pre-season and Season 2.",
                    "The storm always forms, bots start with shields, and bots no longer die to the storm while still in the air.",
                    "Bots get a few seconds to settle after landing before the fight starts.",
                }),
                new PatchNoteSection("KILL FEED", new[]
                {
                    "Knocks now show in the kill feed.",
                    "Eliminations show the weapon that was really used.",
                }),
                new PatchNoteSection("PRE-SEASON", new[]
                {
                    "Fixed black players, bus and trees: the island now stays in daylight.",
                    "Fixed the screen blowing out to white or green glare.",
                    "You and the bots now wear real outfits instead of the orange default body. An empty locker slot rolls a random default each match.",
                    "Lean heals 75 health and 25 shield over 10 seconds. Swapping items while holding it no longer changes your health.",
                }),
                new PatchNoteSection("CONTROLS", new[]
                {
                    "Fixed mouse look sticking and the camera locking up after playing for a while.",
                    "High polling rate mice (2000 to 8000 Hz) no longer stutter.",
                    "Fixed Bluetooth PlayStation controllers going haywire.",
                    "Edit reset on release no longer leaves you stuck in edit mode, and now works on controller too.",
                    "New stretched resolution option in Settings > Display.",
                }),
                new PatchNoteSection("SYNDICATE MENU", new[]
                {
                    "Spin bot pauses while you heal and puts your aim back where it was.",
                    "New: time of day, always headshots, and magic bullet on Season 7.",
                    "New: give item, teleport to bot and heal while moving.",
                    "New Menu key option for keyboards without an Insert key.",
                    "ESP shows the bots' real names.",
                    "Fixed a crash on Season 9 while the match loads (chams are off on Chapter 1 seasons).",
                }),
                new PatchNoteSection("LAUNCHER", new[]
                {
                    "New Patch Notes tab (you're reading it). It opens by itself after each update.",
                    "Unlock All gives Battle Royale skins only. Save the World skins are an option under Choose Player, and the locker no longer jumps back every few seconds.",
                    "Launches that end in \"Profile Query Failed\" are retried automatically.",
                    "Clearer error messages with steps to fix them, and a fix for getting stuck waiting for players to ready up.",
                    "Files removed by antivirus are restored automatically.",
                    "Pressing Play without an account now tells you to create one first.",
                }),
                new PatchNoteSection("FIXES", new[]
                {
                    "Turning on Visualize Sound Effects no longer mutes the game audio.",
                    "Fixed placing a Mounted Turret crashing the match.",
                    "Fixed a Pre-season match crash when bots healed.",
                    "Fixed the crash when closing the game window (Season 3).",
                    "The match server now waits for the lobby to finish loading before it starts.",
                }),
            },
            KnownIssues: new[]
            {
                "Some players crash when opening settings in Season 2. We're looking into it. If it happens to you, send your logs in the Discord.",
            }),
    };

    public static PatchRelease Latest => Releases[0];
}
