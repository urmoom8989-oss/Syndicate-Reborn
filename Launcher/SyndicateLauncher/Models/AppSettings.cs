using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.IO;
using System.Linq;
using System.Runtime.CompilerServices;
using System.Threading.Tasks;
using System.Text.Json.Serialization;
using System.Windows.Media;
using SyndicateLauncher.Services;
namespace SyndicateLauncher.Models;
[JsonConverter(typeof(JsonStringEnumConverter))]
public enum BotDifficulty
{
    Easy = 0,
    Medium = 1,
    Hard = 2
}
[JsonConverter(typeof(JsonStringEnumConverter))]
public enum ProfileMode
{
    UnlockedAll = 0,
    Progression = 1
}
public sealed class Profile : INotifyPropertyChanged
{
    private string _name = "";
    private string _avatar = "";

    public string Name
    {
        get => _name;
        set
        {
            if (_name == value) return;
            _name = value;
            OnPropertyChanged();
        }
    }

    public DateTime CreatedUtc { get; set; } = DateTime.UtcNow;
    private ProfileMode _mode = ProfileMode.UnlockedAll;
    public ProfileMode Mode
    {
        get => _mode;
        set
        {
            if (_mode == value) return;
            _mode = value;
            OnPropertyChanged();
            OnPropertyChanged(nameof(ModeLabel));
        }
    }

    /// <summary>Unlock All only: also put the Save the World heroes (and STW backpacks, loading screens and music) in the
    /// locker. Off: Battle Royale cosmetics only.</summary>
    public bool StwSkins { get; set; }

    public string Avatar
    {
        get => _avatar;
        set
        {
            if (_avatar == value) return;
            _avatar = value;
            OnPropertyChanged();
            OnPropertyChanged(nameof(AvatarImage));
        }
    }

    [JsonIgnore]
    public ImageSource? AvatarImage => ProfileAvatars.Load(Avatar);

    [JsonIgnore]
    public string ModeLabel => Mode == ProfileMode.Progression ? "Progression Mode" : "Unlocked All";

    public event PropertyChangedEventHandler? PropertyChanged;

    private void OnPropertyChanged([CallerMemberName] string? name = null) =>
        PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(name));
}
/// <summary>Everything the launcher remembers between runs (saved to %AppData%\SyndicateLauncher\settings.json).</summary>
public sealed class AppSettings
{
    public string GameDirectory { get; set; } = "";
    /// <summary>Install folder per season key ("1-7"), so every Chapter 1 build can be swapped in place.</summary>
    public Dictionary<string, string> BuildDirectories { get; set; } = new();
    public string LastBuildSeasonKey { get; set; } = "";
    public BotDifficulty Difficulty { get; set; } = BotDifficulty.Medium;
    public List<Profile> Profiles { get; set; } = new();
    public string ActiveProfile { get; set; } = "";
    public string BotCountRange { get; set; } = "";
    /// <summary>Saved per-bot name overrides, keyed by roster index; unnamed entries use the built-in name.</summary>
    public Dictionary<int, string> BotNameOverrides { get; set; } = new();
    /// <summary>Settings &gt; Bots &gt; Bot names: names the player added. Every match each one goes to a random bot.</summary>
    public List<string>? CustomBotNames { get; set; }
    public bool EditOnRelease { get; set; } = true;
    public bool EnableMenu { get; set; }
    public bool HighTickFullLobbies { get; set; }
    /// <summary>Settings &gt; Bots: late game. Matches start in the third storm circle with a full loadout (Chapter 1 seasons).</summary>
    public bool LateGame { get; set; }
    /// <summary>Settings &gt; Bots: fast queue. While a match runs, the next match server loads in the background
    /// so Ready joins in seconds (like Season X). Uses about 2-4 GB more memory; skipped on PCs without enough.
    /// It also turns on instant play: PLAY starts Fortnite while the first match server loads the island.</summary>
    public bool FastQueue { get; set; } = true;
    public BotPreset? BotPreset { get; set; }
    public string LaunchArguments { get; set; } = "";
    /// <summary>Settings &gt; Mods &gt; Controller: move around the launcher with a controller.</summary>
    public bool ControllerNavigation { get; set; } = true;
    /// <summary>Settings &gt; Mods &gt; Controller: PS4 / PS5 pads play as an Xbox controller in game (needs ViGEmBus).</summary>
    public bool PlayStationControllers { get; set; } = true;
    /// <summary>Themes tab: the chosen theme, the player's own themes and the live effects.</summary>
    public ThemeSettings Theme { get; set; } = new();
    /// <summary>Settings &gt; Launcher &amp; files: silently try to add Syndicate's folders to Windows Defender's
    /// exclusion list each time the launcher opens. The button in that section always uses a UAC-elevated prompt.</summary>
    public bool AutoAddExclusions { get; set; } = true;
    /// <summary>Settings &gt; Launcher: show "Playing SYNDICATE" (season, mode, players left) in Discord.</summary>
    public bool DiscordPresence { get; set; } = true;
    /// <summary>Settings &gt; Display &gt; Crosshair: the custom crosshair drawn in game (Services/CrosshairService.cs).</summary>
    public CrosshairSettings Crosshair { get; set; } = new();
    /// <summary>The newest Patch Notes version the player has opened. A newer one opens the Patch Notes tab once.</summary>
    public string LastSeenPatchNotes { get; set; } = "";
    /// <summary>Title bar OFFLINE button (Services/OfflineMode.cs): nothing goes online and no other PC can connect in.</summary>
    public bool OfflineMode { get; set; }
    /// <summary>Settings &gt; Match &amp; bots &gt; Game modes (Services/SeasonModes.cs): the modes each build shows in the
    /// lobby, keyed by build ("7.40"). A build that is not listed keeps its usual lobby.</summary>
    public Dictionary<string, List<string>> SeasonModes { get; set; } = new();
    /// <summary>Settings &gt; Match &amp; bots &gt; Game modes: how Fortnite's mode selector lays out each build's modes
    /// (order, default, badges, locked tiles), keyed by build. A build that is not listed keeps its usual layout.</summary>
    public Dictionary<string, SeasonLayout> SeasonLayouts { get; set; } = new();
    /// <summary>Settings &gt; Item shop: how many hours each item shop lasts (1 to 168).</summary>
    public int ShopHours { get; set; } = 6;
    /// <summary>Settings &gt; Item shop: your own shop per season number; seasons without one stay automatic.</summary>
    public Dictionary<string, CustomShop> CustomShops { get; set; } = new();
    /// <summary>Settings &gt; News &amp; tournaments: posts for the game's News tab (Services/NewsSettings.cs).</summary>
    public List<NewsPost> News { get; set; } = new();
    /// <summary>Settings &gt; News &amp; tournaments: tournaments for the Compete tab (8.50 to 12.50) and the News tab.</summary>
    public List<CustomTournament> Tournaments { get; set; } = new();
}

/// <summary>
/// The lobby mode selector for one build (Services/SeasonModes.cs, backend Utils/syndicateSeasonModes.js lobbyTiles):
/// rows, tile order, the tile picked when Fortnite opens, NEW / LIMITED TIME badges, and modes shown greyed out.
/// </summary>
public sealed class SeasonLayout
{
    public List<string> Order { get; set; } = new();
    public string Default { get; set; } = "";
    /// <summary>Mode id to "new" or "limited".</summary>
    public Dictionary<string, string> Badges { get; set; } = new();
    /// <summary>Modes that are off but still shown in the selector, locked.</summary>
    public List<string> Locked { get; set; } = new();
    /// <summary>Mode id to the selector row it sits in (0 top row, 1 and 2 the limited-time rows). Missing: top row.</summary>
    public Dictionary<string, int> Rows { get; set; } = new();
    /// <summary>Mode id to a picture the player chose for its tile (a copy in %APPDATA%\SyndicateLauncher\ModeImages).</summary>
    public Dictionary<string, string> Images { get; set; } = new();
}

/// <summary>A season's own item shop (Settings &gt; Item shop): template ids for its featured and daily slots.</summary>
/// <summary>A post in the game's News tab. Builds empty = every season.</summary>
public sealed class NewsPost
{
    public string Id { get; set; } = "";
    public string Title { get; set; } = "";
    public string Body { get; set; } = "";
    /// <summary>File name inside %APPDATA%\SyndicateLauncher\NewsImages, or empty.</summary>
    public string Image { get; set; } = "";
    public List<string> Builds { get; set; } = new();
}

/// <summary>A tournament in the Compete tab (8.50 to 12.50), also posted in the News tab when PostInNews is on.</summary>
public sealed class CustomTournament
{
    public string Id { get; set; } = "";
    public string Title { get; set; } = "";
    public string Subtitle { get; set; } = "";
    public string Description { get; set; } = "";
    public string Schedule { get; set; } = "";
    public string Mode { get; set; } = "solo";
    public DateTime Begin { get; set; } = DateTime.Now;
    public DateTime End { get; set; } = DateTime.Now.AddHours(3);
    public string Image { get; set; } = "";
    public string Color { get; set; } = "F81B2D";
    public bool PostInNews { get; set; } = true;
    public List<string> Builds { get; set; } = new();
}

public sealed class CustomShop
{
    public List<string> Featured { get; set; } = new();
    public List<string> Daily { get; set; } = new();
}
