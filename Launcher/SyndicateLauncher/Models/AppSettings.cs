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
}
