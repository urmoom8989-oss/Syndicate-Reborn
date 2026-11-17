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

/// <summary>
/// One mode's row in the Game modes picker: on / shown locked / off, its lobby badge, whether it is the tile Fortnite
/// opens on, and its place in the lobby (the up and down arrows). Save the World is a lobby button, not a tile, so it
/// only has on and off.
/// </summary>
public sealed class GameModeToggle : ViewModelBase
{
    public const int On = 0, Locked = 1, Off = 2;
    private static readonly string[] TileStates = { "On", "Shown locked", "Off" };
    private static readonly string[] ButtonStates = { "On", "Off" };
    private static readonly string[] Badges = { "No badge", "NEW", "LIMITED TIME" };

    private readonly Action<GameModeToggle, int> _stateChanged;
    private readonly Action _layoutChanged;
    private int _state, _badge;
    private bool _isDefault;
    private ICommand? _up, _down, _makeDefault;

    public GameModeToggle(GameModeInfo info, int state, int badge, Action<GameModeToggle, int> stateChanged,
        Action layoutChanged, Action<GameModeToggle, int> move, Action<GameModeToggle> makeDefault)
    {
        Info = info;
        _state = state;
        _badge = badge;
        _stateChanged = stateChanged;
        _layoutChanged = layoutChanged;
        _up = new RelayCommand(() => move(this, -1));
        _down = new RelayCommand(() => move(this, +1));
        _makeDefault = new RelayCommand(() => makeDefault(this));
    }

    public GameModeInfo Info { get; }
    public string Id => Info.Id;
    public string Name => Info.Name;
    public string Description => Info.Description;
    public bool Experimental => Info.Experimental;
    /// <summary>A tile in Fortnite's mode selector (every mode but Save the World).</summary>
    public bool IsTile => Info.Id != "stw";
    public string[] StateOptions => IsTile ? TileStates : ButtonStates;
    public string[] BadgeOptions => Badges;
    public bool IsOn => _state == On;
    public int State => _state;

    /// <summary>Index into StateOptions (Save the World: 0 on, 1 off).</summary>
    public int StateIndex
    {
        get => IsTile ? _state : (_state == On ? 0 : 1);
        set
        {
            if (value < 0) return;
            int state = IsTile ? value : (value == 0 ? On : Off);
            if (state == _state) return;
            int old = _state;
            _state = state;
            OnPropertyChanged();
            OnPropertyChanged(nameof(IsOn));
            _stateChanged(this, old);
        }
    }

    /// <summary>0 no badge, 1 NEW, 2 LIMITED TIME.</summary>
    public int BadgeIndex
    {
        get => _badge;
        set
        {
            if (value < 0 || value == _badge) return;
            _badge = value;
            OnPropertyChanged();
            _layoutChanged();
        }
    }

    public bool IsDefault
    {
        get => _isDefault;
        // Always raised: a click on the DEFAULT chip checks it before the view model decides, so the view must be told.
        internal set { _isDefault = value; OnPropertyChanged(); }
    }

    public ICommand MoveUpCommand => _up!;
    public ICommand MoveDownCommand => _down!;
    public ICommand MakeDefaultCommand => _makeDefault!;

    /// <summary>Changes the state without saving (used to undo a change that isn't allowed).</summary>
    internal void SetStateQuietly(int state)
    {
        if (_state == state) return;
        _state = state;
        OnPropertyChanged(nameof(StateIndex));
        OnPropertyChanged(nameof(IsOn));
    }
}

/// <summary>
/// Settings &gt; Match &amp; bots &gt; Game modes (Services/SeasonModes.cs): pick a season, then for each mode choose on,
/// shown locked or off, its badge, which tile Fortnite opens on, and the order of the tiles in the lobby's mode
/// selector. Saved straight away to settings.json and to the backend's season-modes.json; Fortnite reads its lobby when
/// it starts, so an open game needs a restart to show a change.
/// </summary>
public sealed partial class MainViewModel
{
    private GameModeSeasonChoice? _gameModeSeason;
    private string _gameModesNote = "";
    private string _chosenDefault = "";
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
            OnPropertyChanged(nameof(GameModesLayoutNote));
            OnPropertyChanged(nameof(HasGameModesLayoutNote));
        }
    }

    public string GameModesNote
    {
        get => _gameModesNote;
        private set => Set(ref _gameModesNote, value);
    }

    /// <summary>Season 4's lobby (4.5) only takes on and off; it keeps its own order and badges.</summary>
    public string GameModesLayoutNote => _gameModeSeason?.Build == "4.5"
        ? "Season 4's lobby keeps its own order and badges: only On and Off apply there (Shown locked hides the mode)."
        : "";
    public bool HasGameModesLayoutNote => GameModesLayoutNote.Length > 0;

    public ICommand ResetGameModesCommand => _resetGameModesCommand ??= new RelayCommand(ResetGameModes, () => _gameModeSeason is not null);

    /// <summary>Called once from the constructor, after the settings are loaded.</summary>
    private void InitializeGameModes()
    {
        _settings.SeasonModes ??= new();
        _settings.SeasonLayouts ??= new();
        SeasonModes.Load(_settings.SeasonModes, _settings.SeasonLayouts);
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
        var available = SeasonModes.Available(choice.Build);
        var enabled = SeasonModes.Enabled(choice.Build);
        var layout = SeasonModes.Layout(choice.Build);
        // Tiles in the saved order, then any the layout does not mention in their usual order; Save the World last.
        var ordered = layout.Order.Where(m => available.Contains(m) && m != "stw")
            .Concat(available.Where(m => m != "stw"))
            .Distinct(StringComparer.OrdinalIgnoreCase)
            .Concat(available.Where(m => m == "stw"))
            .ToList();
        foreach (string id in ordered)
        {
            var info = SeasonModes.All.FirstOrDefault(m => m.Id == id);
            if (info is null) continue;
            int state = enabled.Contains(id) ? GameModeToggle.On
                : layout.Locked.Contains(id, StringComparer.OrdinalIgnoreCase) ? GameModeToggle.Locked : GameModeToggle.Off;
            int badge = layout.Badges.TryGetValue(id, out var b) ? (b == "new" ? 1 : b == "limited" ? 2 : 0) : 0;
            GameModeToggles.Add(new GameModeToggle(info, state, badge, OnGameModeStateChanged, SaveGameModes, MoveGameMode, MakeGameModeDefault));
        }
        _chosenDefault = layout.Default;
        RefreshDefaultMarks();
        GameModesNote = "Fortnite reads these when it starts: restart it if it's open to see a change.";
    }

    // The tile Fortnite opens on: the chosen one while it is on, otherwise the first tile that is on.
    private void RefreshDefaultMarks()
    {
        var tiles = GameModeToggles.Where(t => t.IsTile).ToList();
        var mark = tiles.FirstOrDefault(t => t.IsOn && string.Equals(t.Id, _chosenDefault, StringComparison.OrdinalIgnoreCase))
                   ?? tiles.FirstOrDefault(t => t.IsOn);
        foreach (var tile in tiles) tile.IsDefault = ReferenceEquals(tile, mark);
    }

    private void OnGameModeStateChanged(GameModeToggle toggle, int oldState)
    {
        if (!GameModeToggles.Any(t => t.IsTile && t.IsOn && t.Id != "creative"))
        {
            // The lobby always needs a Battle Royale mode. Undo after the binding has finished writing the value.
            System.Windows.Application.Current?.Dispatcher.BeginInvoke(new Action(() => toggle.SetStateQuietly(oldState)));
            GameModesNote = "Keep at least one Battle Royale mode on (Solo, Duos, Squads, Arena, 50v50 or Playground).";
            return;
        }
        SaveGameModes();
        if (toggle.Experimental && toggle.IsOn)
            GameModesNote = toggle.Name + " is on for " + _gameModeSeason?.Season.Name + " (experimental). Restart Fortnite if it's open.";
    }

    private void MoveGameMode(GameModeToggle toggle, int delta)
    {
        if (!toggle.IsTile) return;
        int from = GameModeToggles.IndexOf(toggle), to = from + delta;
        if (from < 0 || to < 0 || to >= GameModeToggles.Count || !GameModeToggles[to].IsTile) return;
        GameModeToggles.Move(from, to);
        SaveGameModes();
    }

    private void MakeGameModeDefault(GameModeToggle toggle)
    {
        if (!toggle.IsTile) return;
        if (!toggle.IsOn) { RefreshDefaultMarks(); GameModesNote = "Turn " + toggle.Name + " on first: Fortnite can only open on a mode that is on."; return; }
        _chosenDefault = toggle.Id;
        SaveGameModes();
    }

    private void SaveGameModes()
    {
        var choice = _gameModeSeason;
        if (choice is null) return;
        RefreshDefaultMarks();
        var on = GameModeToggles.Where(t => t.IsOn).Select(t => t.Id).ToList();
        var layout = new SeasonLayout
        {
            Order = GameModeToggles.Where(t => t.IsTile).Select(t => t.Id).ToList(),
            Default = GameModeToggles.FirstOrDefault(t => t.IsTile && t.IsOn && string.Equals(t.Id, _chosenDefault, StringComparison.OrdinalIgnoreCase))?.Id ?? "",
            Badges = GameModeToggles.Where(t => t.IsTile && t.BadgeIndex > 0)
                .ToDictionary(t => t.Id, t => t.BadgeIndex == 1 ? "new" : "limited"),
            Locked = GameModeToggles.Where(t => t.IsTile && t.State == GameModeToggle.Locked).Select(t => t.Id).ToList()
        };
        Dictionary<string, List<string>> modes = _settings.SeasonModes;
        Dictionary<string, SeasonLayout> layouts = _settings.SeasonLayouts;
        foreach (string build in choice.Builds)
        {
            modes = SeasonModes.Set(build, on);
            layouts = SeasonModes.SetLayout(build, layout);
        }
        _settings.SeasonModes = modes;
        _settings.SeasonLayouts = layouts;
        Save();
        SeasonModes.WriteForBackend();
        GameModesNote = "Saved for " + choice.Season.Name + ". Restart Fortnite if it's open to see the change.";
    }

    private void ResetGameModes()
    {
        var choice = _gameModeSeason;
        if (choice is null) return;
        foreach (string build in choice.Builds)
        {
            var (modes, layouts) = SeasonModes.Reset(build);
            _settings.SeasonModes = modes;
            _settings.SeasonLayouts = layouts;
        }
        Save();
        SeasonModes.WriteForBackend();
        LoadGameModeToggles();
        GameModesNote = choice.Season.Name + " is back to its usual modes and layout.";
    }
}
