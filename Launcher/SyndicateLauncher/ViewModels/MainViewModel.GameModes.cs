using System;
using System.Collections.Generic;
using System.Collections.ObjectModel;
using System.Linq;
using System.Windows.Input;
using SyndicateLauncher.Models;
using SyndicateLauncher.Services;

namespace SyndicateLauncher.ViewModels;

/// <summary>A season in the Game modes picker. Its build is the key the backend reads.</summary>
public sealed class GameModeSeasonChoice
{
    public GameModeSeasonChoice(SeasonInfo season) => Season = season;
    public SeasonInfo Season { get; }
    public string Build => Season.BuildVersion;
    /// <summary>Every build this season can run (the main one and any alternates), so they share one choice.</summary>
    public IEnumerable<string> Builds => new[] { Season.BuildVersion }.Concat(Season.AlternateBuildVersions);
    public override string ToString() =>
        (Season.ChapterNumber == 1 ? "" : "Chapter " + Season.ChapterNumber + " ") + Season.Name + "  (" + Season.BuildVersion + ")";
}

/// <summary>One mode's switch in the Game modes picker.</summary>
public sealed class GameModeToggle : ViewModelBase
{
    private readonly Action<GameModeToggle> _changed;
    private bool _isOn;

    public GameModeToggle(GameModeInfo info, bool isOn, Action<GameModeToggle> changed)
    {
        Info = info;
        _isOn = isOn;
        _changed = changed;
    }

    public GameModeInfo Info { get; }
    public string Name => Info.Name;
    public string Description => Info.Description;
    public bool Experimental => Info.Experimental;

    public bool IsOn
    {
        get => _isOn;
        set { if (Set(ref _isOn, value)) _changed(this); }
    }

    /// <summary>Changes the switch without saving (used to undo a change that isn't allowed).</summary>
    internal void SetQuietly(bool value)
    {
        if (_isOn == value) return;
        _isOn = value;
        OnPropertyChanged(nameof(IsOn));
    }
}

/// <summary>
/// Settings &gt; Match &amp; bots &gt; Game modes (Services/SeasonModes.cs): pick a season, switch its lobby modes on or off.
/// Saved straight away to settings.json and to the backend's season-modes.json; Fortnite reads its lobby list when it
/// starts, so an open game needs a restart to show the change.
/// </summary>
public sealed partial class MainViewModel
{
    private GameModeSeasonChoice? _gameModeSeason;
    private string _gameModesNote = "";
    private ICommand? _resetGameModesCommand;

    public ObservableCollection<GameModeSeasonChoice> GameModeSeasons { get; } = new();
    public ObservableCollection<GameModeToggle> GameModeToggles { get; } = new();

    public GameModeSeasonChoice? SelectedGameModeSeason
    {
        get => _gameModeSeason;
        set
        {
            if (!Set(ref _gameModeSeason, value)) return;
            LoadGameModeToggles();
        }
    }

    public string GameModesNote
    {
        get => _gameModesNote;
        private set => Set(ref _gameModesNote, value);
    }

    public ICommand ResetGameModesCommand => _resetGameModesCommand ??= new RelayCommand(ResetGameModes, () => _gameModeSeason is not null);

    /// <summary>Called once from the constructor, after the settings are loaded.</summary>
    private void InitializeGameModes()
    {
        _settings.SeasonModes ??= new();
        SeasonModes.Load(_settings.SeasonModes);
        foreach (var season in SeasonCatalog.GetSeasons(1).Concat(SeasonCatalog.GetSeasons(2)))
            if (season.IsReleased && SeasonModes.Available(season.BuildVersion).Count > 0)
                GameModeSeasons.Add(new GameModeSeasonChoice(season));
        _gameModeSeason = GameModeSeasons.FirstOrDefault(c => c.Season.Key == _settings.LastBuildSeasonKey)
                          ?? GameModeSeasons.LastOrDefault(c => c.Season.ChapterNumber == 1);
        LoadGameModeToggles();
        // Not written here: every launch writes it before Fortnite starts (GameLauncher), and a change writes it at once.
    }

    private void LoadGameModeToggles()
    {
        GameModeToggles.Clear();
        var choice = _gameModeSeason;
        if (choice is null) { GameModesNote = ""; return; }
        var enabled = SeasonModes.Enabled(choice.Build);
        foreach (string id in SeasonModes.Available(choice.Build))
        {
            var info = SeasonModes.All.FirstOrDefault(m => m.Id == id);
            if (info is not null) GameModeToggles.Add(new GameModeToggle(info, enabled.Contains(id), OnGameModeToggled));
        }
        GameModesNote = "Fortnite reads these when it starts: restart it if it's open to see a change.";
    }

    private void OnGameModeToggled(GameModeToggle toggle)
    {
        var choice = _gameModeSeason;
        if (choice is null) return;
        var on = GameModeToggles.Where(t => t.IsOn).Select(t => t.Info.Id).ToList();
        if (!on.Any(id => id is not ("creative" or "stw")))
        {
            // The lobby always needs a Battle Royale mode. Undo after the binding has finished writing the value.
            System.Windows.Application.Current?.Dispatcher.BeginInvoke(new Action(() => toggle.SetQuietly(true)));
            GameModesNote = "Keep at least one Battle Royale mode on (Solo, Duos, Squads, Arena, 50v50 or Playground).";
            return;
        }
        Dictionary<string, List<string>> map = _settings.SeasonModes;
        foreach (string build in choice.Builds) map = SeasonModes.Set(build, on);
        _settings.SeasonModes = map;
        Save();
        SeasonModes.WriteForBackend();
        GameModesNote = toggle.Experimental && toggle.IsOn
            ? toggle.Name + " is on for " + choice.Season.Name + " (experimental). Restart Fortnite if it's open."
            : "Saved for " + choice.Season.Name + ". Restart Fortnite if it's open to see the change.";
    }

    private void ResetGameModes()
    {
        var choice = _gameModeSeason;
        if (choice is null) return;
        Dictionary<string, List<string>> map = _settings.SeasonModes;
        foreach (string build in choice.Builds) map = SeasonModes.Reset(build);
        _settings.SeasonModes = map;
        Save();
        SeasonModes.WriteForBackend();
        LoadGameModeToggles();
        GameModesNote = choice.Season.Name + " is back to its usual modes.";
    }
}
