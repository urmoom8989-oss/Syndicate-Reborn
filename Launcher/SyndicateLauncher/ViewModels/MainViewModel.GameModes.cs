using System;
using System.Collections.Generic;
using System.Collections.ObjectModel;
using System.Globalization;
using System.Linq;
using System.Windows.Input;
using System.Windows.Media;
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
/// One mode in the lobby editor: on / shown locked / off, its badge, the selector row it sits in, and whether it is the
/// tile Fortnite opens on. Save the World is a lobby button, not a tile, so it only has on and off.
/// </summary>
public sealed class GameModeToggle : ViewModelBase
{
    public const int On = 0, Locked = 1, Off = 2;
    private static readonly string[] TileStates = { "On", "Shown locked", "Off" };
    private static readonly string[] ButtonStates = { "On", "Off" };
    private static readonly string[] Badges = { "No badge", "NEW", "LIMITED TIME" };

    // Tile art in the editor: a colour per mode, a glyph (Segoe Fluent Icons / MDL2 Assets) and a sub-heading.
    private static readonly Dictionary<string, (string From, string To, string Glyph, string Sub)> Art = new()
    {
        ["solo"] = ("2F86FF", "0E2F78", "", "1 PLAYER"),
        ["duo"] = ("33B5FF", "0D447F", "", "2 PLAYERS"),
        ["squad"] = ("5C6BFF", "1A2275", "", "4 PLAYERS"),
        ["arenasolo"] = ("B25CFF", "3D1677", "", "HYPE AND DIVISIONS"),
        ["50v50"] = ("FF8A3D", "83260E", "", "TWO TEAMS OF 50"),
        ["playground"] = ("2FD3A0", "0B5C4D", "", "NO STORM DAMAGE"),
        ["creative"] = ("19C3E6", "0A4F7E", "", "BUILD AND PLAY"),
        ["stw"] = ("F5B92E", "7A4A08", "", "PVE"),
    };
    private static readonly Dictionary<string, Brush> ArtBrushes = new();

    private readonly Action<GameModeToggle, int> _stateChanged;
    private readonly Action _layoutChanged;
    private int _state, _badge, _row;
    private bool _isDefault, _isSelected;

    public GameModeToggle(GameModeInfo info, int state, int badge, int row, Action<GameModeToggle, int> stateChanged, Action layoutChanged)
    {
        Info = info;
        _state = state;
        _badge = badge;
        _row = row;
        _stateChanged = stateChanged;
        _layoutChanged = layoutChanged;
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
    public int State => _state;
    public bool IsOn => _state == On;
    public bool IsLocked => _state == Locked;
    public bool IsOff => _state == Off;

    public string TileName => Info.Id == "50v50" ? "50 V 50" : Info.Name.ToUpperInvariant();
    public string SubName => Art.TryGetValue(Id, out var art) ? art.Sub : "";
    public string Glyph => Art.TryGetValue(Id, out var art) ? art.Glyph : "";

    public Brush ArtBrush
    {
        get
        {
            if (ArtBrushes.TryGetValue(Id, out var brush)) return brush;
            var (from, to, _, _) = Art.TryGetValue(Id, out var art) ? art : ("4A5568", "1A202C", "", "");
            var gradient = new LinearGradientBrush(Hex(from), Hex(to), new System.Windows.Point(0, 0), new System.Windows.Point(1, 1));
            gradient.Freeze();
            ArtBrushes[Id] = gradient;
            return gradient;
        }
    }

    private static Color Hex(string rgb) => Color.FromRgb(
        byte.Parse(rgb.Substring(0, 2), NumberStyles.HexNumber, CultureInfo.InvariantCulture),
        byte.Parse(rgb.Substring(2, 2), NumberStyles.HexNumber, CultureInfo.InvariantCulture),
        byte.Parse(rgb.Substring(4, 2), NumberStyles.HexNumber, CultureInfo.InvariantCulture));

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
            RaiseState();
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
            OnPropertyChanged(nameof(BadgeText));
            OnPropertyChanged(nameof(HasBadge));
            OnPropertyChanged(nameof(IsLimitedBadge));
            _layoutChanged();
        }
    }

    public string BadgeText => _badge == 1 ? "NEW!" : _badge == 2 ? "LIMITED TIME" : "";
    public bool HasBadge => _badge > 0;
    public bool IsLimitedBadge => _badge == 2;

    /// <summary>The selector row the tile sits in (0 is the top row).</summary>
    public int Row
    {
        get => _row;
        internal set => Set(ref _row, value);
    }

    public bool IsDefault
    {
        get => _isDefault;
        internal set => Set(ref _isDefault, value);
    }

    public bool IsSelected
    {
        get => _isSelected;
        internal set => Set(ref _isSelected, value);
    }

    /// <summary>Changes the state without the change callback (turning a tile on by dropping it in a row, or undoing).</summary>
    internal void SetStateQuietly(int state)
    {
        if (_state == state) return;
        _state = state;
        RaiseState();
    }

    private void RaiseState()
    {
        OnPropertyChanged(nameof(StateIndex));
        OnPropertyChanged(nameof(State));
        OnPropertyChanged(nameof(IsOn));
        OnPropertyChanged(nameof(IsLocked));
        OnPropertyChanged(nameof(IsOff));
    }
}

/// <summary>One row of Fortnite's mode selector in the lobby editor.</summary>
public sealed class LobbyRow
{
    public LobbyRow(int index, string title, string hint)
    {
        Index = index;
        Title = title;
        Hint = hint;
    }

    public int Index { get; }
    public string Title { get; }
    public string Hint { get; }
    public bool IsTop => Index == 0;
    // The top row has the big tiles, like the game.
    public double TileWidth => IsTop ? 196 : 158;
    public double TileHeight => IsTop ? 118 : 94;
    public ObservableCollection<GameModeToggle> Tiles { get; } = new();
}

/// <summary>
/// Settings &gt; Match &amp; bots &gt; Game modes and the lobby editor (Views/LobbyEditorView.xaml): pick a season, then lay
/// out its mode selector like Fortnite's own screen. Tiles sit in the selector's rows (standard modes on top, limited-time
/// rows below), can be dragged between rows, reordered, shown locked or taken out of the lobby, carry a NEW or LIMITED
/// TIME badge, and one is the tile the lobby opens on. Saved straight away to settings.json and to the backend's
/// season-modes.json (Services/SeasonModes.cs); Fortnite reads its lobby when it starts, so an open game needs a restart.
/// </summary>
public sealed partial class MainViewModel
{
    private GameModeSeasonChoice? _gameModeSeason;
    private string _gameModesNote = "";
    private string _chosenDefault = "";
    private readonly List<GameModeToggle> _lobbyTiles = new();
    private GameModeToggle? _lobbyStw;
    private GameModeToggle? _selectedLobbyTile;
    private bool _isLobbyEditorOpen, _lobbyRefreshQueued;
    private ICommand? _resetGameModesCommand, _openLobbyEditorCommand, _closeLobbyEditorCommand, _selectLobbyTileCommand,
        _moveLobbyTileLeftCommand, _moveLobbyTileRightCommand, _makeLobbyDefaultCommand;

    public ObservableCollection<GameModeSeasonChoice> GameModeSeasons { get; } = new();
    public ObservableCollection<LobbyRow> LobbyRows { get; } = new();
    /// <summary>Modes the season offers that are off: not in the lobby at all.</summary>
    public ObservableCollection<GameModeToggle> LobbyOffTiles { get; } = new();

    public GameModeSeasonChoice? SelectedGameModeSeason
    {
        get => _gameModeSeason;
        set
        {
            if (!Set(ref _gameModeSeason, value)) return;
            LoadGameModeToggles();
            OnPropertyChanged(nameof(LobbyRowsNote));
        }
    }

    public string GameModesNote
    {
        get => _gameModesNote;
        private set => Set(ref _gameModesNote, value);
    }

    public bool IsLobbyEditorOpen
    {
        get => _isLobbyEditorOpen;
        private set => Set(ref _isLobbyEditorOpen, value);
    }

    public GameModeToggle? SelectedLobbyTile
    {
        get => _selectedLobbyTile;
        private set
        {
            if (ReferenceEquals(_selectedLobbyTile, value)) return;
            if (_selectedLobbyTile is not null) _selectedLobbyTile.IsSelected = false;
            _selectedLobbyTile = value;
            if (value is not null) value.IsSelected = true;
            OnPropertyChanged();
            OnPropertyChanged(nameof(HasSelectedLobbyTile));
            OnPropertyChanged(nameof(SelectedLobbyTileRow));
            CommandManager.InvalidateRequerySuggested();
        }
    }

    public bool HasSelectedLobbyTile => _selectedLobbyTile is not null;

    /// <summary>Row names for the selected tile's row picker.</summary>
    public IReadOnlyList<string> LobbyRowNames => LobbyRows.Select(r => r.Title).ToList();
    public bool HasLobbyRowChoice => LobbyRows.Count > 1;

    /// <summary>The selected tile's row (the side panel's row picker).</summary>
    public int SelectedLobbyTileRow
    {
        get => _selectedLobbyTile is null ? -1 : Math.Min(_selectedLobbyTile.Row, Math.Max(0, LobbyRows.Count - 1));
        set
        {
            var tile = _selectedLobbyTile;
            if (tile is null || value < 0 || value == SelectedLobbyTileRow) return;
            if (tile.IsOff)
            {
                // Not in the lobby: only remember the row it goes back to when it is turned on.
                tile.Row = Math.Clamp(value, 0, LobbyRowCount - 1);
                SaveGameModes();
                return;
            }
            PlaceLobbyTile(tile, value, -1);
        }
    }

    public bool HasLobbyStw => _lobbyStw is not null;

    /// <summary>Save the World's lobby button (it is not a tile in the mode selector).</summary>
    public bool LobbyStwOn
    {
        get => _lobbyStw?.IsOn ?? false;
        set
        {
            if (_lobbyStw is null || value == _lobbyStw.IsOn) return;
            _lobbyStw.StateIndex = value ? 0 : 1;
            OnPropertyChanged();
        }
    }

    /// <summary>One line for the Settings page: what the lobby shows, row by row.</summary>
    public string LobbySummary
    {
        get
        {
            if (_gameModeSeason is null) return "";
            var parts = LobbyRows.Where(r => r.Tiles.Count > 0)
                .Select(r => r.Title + ": " + string.Join(", ", r.Tiles.Select(t => t.Name + (t.IsLocked ? " (locked)" : ""))))
                .ToList();
            if (_lobbyStw is not null) parts.Add("Save the World: " + (_lobbyStw.IsOn ? "on" : "off"));
            return string.Join("   ·   ", parts);
        }
    }

    public string LobbyRowsNote => _gameModeSeason?.Build switch
    {
        null => "",
        "4.5" => "Season 4's lobby keeps its own order, rows and badges: only On and Off apply there (Shown locked hides the mode).",
        "10.31" or "10.40" => "Season X's mode selector has three rows: standard modes on top, then two limited-time rows.",
        _ => "This season's mode selector has two rows: standard modes and limited time. Very old seasons may show every tile in one row."
    };

    public ICommand ResetGameModesCommand => _resetGameModesCommand ??= new RelayCommand(ResetGameModes, () => _gameModeSeason is not null);
    public ICommand OpenLobbyEditorCommand => _openLobbyEditorCommand ??= new RelayCommand(OpenLobbyEditor, () => _gameModeSeason is not null);
    public ICommand CloseLobbyEditorCommand => _closeLobbyEditorCommand ??= new RelayCommand(() => IsLobbyEditorOpen = false);
    public ICommand SelectLobbyTileCommand => _selectLobbyTileCommand ??= new RelayCommand(p => { if (p is GameModeToggle tile) SelectedLobbyTile = tile; });
    public ICommand MoveLobbyTileLeftCommand => _moveLobbyTileLeftCommand ??= new RelayCommand(() => NudgeLobbyTile(-1), () => _selectedLobbyTile is { IsOff: false });
    public ICommand MoveLobbyTileRightCommand => _moveLobbyTileRightCommand ??= new RelayCommand(() => NudgeLobbyTile(+1), () => _selectedLobbyTile is { IsOff: false });
    public ICommand MakeLobbyDefaultCommand => _makeLobbyDefaultCommand ??= new RelayCommand(MakeLobbyDefault, () => _selectedLobbyTile is { IsOn: true, IsDefault: false });

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

    private void OpenLobbyEditor()
    {
        if (_gameModeSeason is null) return;
        SelectedLobbyTile ??= LobbyRows.SelectMany(r => r.Tiles).FirstOrDefault() ?? LobbyOffTiles.FirstOrDefault();
        IsLobbyEditorOpen = true;
    }

    private int LobbyRowCount => Math.Max(1, SeasonModes.RowCount(_gameModeSeason?.Build ?? ""));

    private void LoadGameModeToggles()
    {
        string? keepSelected = _selectedLobbyTile?.Id;
        SelectedLobbyTile = null;
        _lobbyTiles.Clear();
        _lobbyStw = null;
        var choice = _gameModeSeason;
        if (choice is null)
        {
            RefreshLobby();
            GameModesNote = "";
            return;
        }
        var available = SeasonModes.Available(choice.Build);
        var enabled = SeasonModes.Enabled(choice.Build);
        var layout = SeasonModes.Layout(choice.Build);
        int rows = Math.Max(1, SeasonModes.RowCount(choice.Build));
        // Tiles in the saved order, then any the layout does not mention in their usual order.
        var ordered = layout.Order.Where(m => available.Contains(m) && m != "stw")
            .Concat(available.Where(m => m != "stw"))
            .Distinct(StringComparer.OrdinalIgnoreCase)
            .ToList();
        foreach (string id in ordered)
        {
            var info = SeasonModes.All.FirstOrDefault(m => m.Id == id);
            if (info is null) continue;
            int state = enabled.Contains(id) ? GameModeToggle.On
                : layout.Locked.Contains(id, StringComparer.OrdinalIgnoreCase) ? GameModeToggle.Locked : GameModeToggle.Off;
            int badge = layout.Badges.TryGetValue(id, out var b) ? (b == "new" ? 1 : b == "limited" ? 2 : 0) : 0;
            int row = layout.Rows.TryGetValue(id, out var r) ? Math.Clamp(r, 0, rows - 1) : 0;
            _lobbyTiles.Add(new GameModeToggle(info, state, badge, row, OnGameModeStateChanged, SaveGameModes));
        }
        if (available.Contains("stw") && SeasonModes.All.FirstOrDefault(m => m.Id == "stw") is { } stw)
            _lobbyStw = new GameModeToggle(stw, enabled.Contains("stw") ? GameModeToggle.On : GameModeToggle.Off, 0, 0,
                OnGameModeStateChanged, SaveGameModes);
        _chosenDefault = layout.Default;
        RefreshLobby();
        SelectedLobbyTile = _lobbyTiles.FirstOrDefault(t => t.Id == keepSelected)
                            ?? LobbyRows.SelectMany(r => r.Tiles).FirstOrDefault() ?? LobbyOffTiles.FirstOrDefault();
        OnPropertyChanged(nameof(HasLobbyStw));
        OnPropertyChanged(nameof(LobbyStwOn));
        GameModesNote = "Fortnite reads these when it starts: restart it if it's open to see a change.";
    }

    // Tiles in the order the lobby lists them: by row, then by their place in the saved order (OrderBy is stable).
    private List<GameModeToggle> LobbyDisplayOrder()
    {
        int last = LobbyRowCount - 1;
        return _lobbyTiles.OrderBy(t => Math.Min(t.Row, last)).ToList();
    }

    private List<GameModeToggle> TilesInRow(int row)
    {
        int last = LobbyRowCount - 1;
        return LobbyDisplayOrder().Where(t => !t.IsOff && Math.Min(t.Row, last) == row).ToList();
    }

    /// <summary>Rebuilds the rows and the "not in the lobby" tray from the tiles, and the default mark.</summary>
    private void RefreshLobby()
    {
        _lobbyRefreshQueued = false;
        int rows = _gameModeSeason is null ? 0 : LobbyRowCount;
        if (LobbyRows.Count != rows)
        {
            LobbyRows.Clear();
            for (int i = 0; i < rows; i++)
                LobbyRows.Add(rows == 1
                    ? new LobbyRow(0, "Modes", "Season 4 lists its modes in one row")
                    : i switch
                    {
                        0 => new LobbyRow(0, "Standard modes", "Top row"),
                        1 => new LobbyRow(1, "Limited time", "Second row"),
                        _ => new LobbyRow(i, "Limited time 2", "Third row")
                    });
            OnPropertyChanged(nameof(LobbyRowNames));
            OnPropertyChanged(nameof(HasLobbyRowChoice));
        }
        foreach (var row in LobbyRows) row.Tiles.Clear();
        LobbyOffTiles.Clear();
        foreach (var tile in LobbyDisplayOrder())
        {
            if (tile.IsOff) LobbyOffTiles.Add(tile);
            else if (LobbyRows.Count > 0) LobbyRows[Math.Min(tile.Row, LobbyRows.Count - 1)].Tiles.Add(tile);
        }
        RefreshDefaultMarks();
        OnPropertyChanged(nameof(LobbySummary));
        OnPropertyChanged(nameof(SelectedLobbyTileRow));
        OnPropertyChanged(nameof(LobbyStwOn));
        CommandManager.InvalidateRequerySuggested();
    }

    // Rebuilt after the current input event, so a drop or a list change never pulls a tile out from under the mouse.
    private void QueueLobbyRefresh()
    {
        if (_lobbyRefreshQueued) return;
        _lobbyRefreshQueued = true;
        var dispatcher = System.Windows.Application.Current?.Dispatcher;
        if (dispatcher is null) RefreshLobby();
        else dispatcher.BeginInvoke(new Action(RefreshLobby));
    }

    // The tile Fortnite opens on: the chosen one while it is on, otherwise the first tile that is on.
    private void RefreshDefaultMarks()
    {
        var shown = LobbyDisplayOrder();
        var mark = shown.FirstOrDefault(t => t.IsOn && string.Equals(t.Id, _chosenDefault, StringComparison.OrdinalIgnoreCase))
                   ?? shown.FirstOrDefault(t => t.IsOn);
        foreach (var tile in shown) tile.IsDefault = ReferenceEquals(tile, mark);
    }

    private void OnGameModeStateChanged(GameModeToggle toggle, int oldState)
    {
        if (toggle.IsTile && !_lobbyTiles.Any(t => t.IsOn && t.Id != "creative"))
        {
            // The lobby always needs a Battle Royale mode. Undo after the binding has finished writing the value.
            System.Windows.Application.Current?.Dispatcher.BeginInvoke(new Action(() =>
            {
                toggle.SetStateQuietly(oldState);
                QueueLobbyRefresh();
            }));
            GameModesNote = "Keep at least one Battle Royale mode on (Solo, Duos, Squads, Arena, 50v50 or Playground).";
            return;
        }
        SaveGameModes();
        if (!toggle.IsTile) OnPropertyChanged(nameof(LobbyStwOn));
        if (toggle.Experimental && toggle.IsOn)
            GameModesNote = toggle.Name + " is on for " + _gameModeSeason?.Season.Name + " (experimental). Restart Fortnite if it's open.";
    }

    /// <summary>
    /// Puts a tile in a row at a position among that row's tiles, counted with the tile still where it is (-1: at the
    /// end). A tile that was out of the lobby is turned on. Called by the lobby editor's drag and drop, its arrows and
    /// its row picker.
    /// </summary>
    public void PlaceLobbyTile(GameModeToggle tile, int row, int index)
    {
        if (!_lobbyTiles.Contains(tile) || _gameModeSeason is null) return;
        row = Math.Clamp(row, 0, LobbyRowCount - 1);
        var before = TilesInRow(row);
        int current = before.IndexOf(tile);                      // -1 when it comes from another row or is off
        if (current >= 0 && index > current) index--;            // its own place closes up when it is lifted out
        if (current >= 0 && (index == current || (index < 0 && current == before.Count - 1))) return;

        if (tile.IsOff) tile.SetStateQuietly(GameModeToggle.On);
        _lobbyTiles.Remove(tile);
        tile.Row = row;
        var rest = before.Where(t => !ReferenceEquals(t, tile)).ToList();
        int at = index >= 0 && index < rest.Count ? _lobbyTiles.IndexOf(rest[index])
            : rest.Count > 0 ? _lobbyTiles.IndexOf(rest[^1]) + 1
            : _lobbyTiles.Count;
        _lobbyTiles.Insert(at, tile);
        SelectedLobbyTile = tile;
        SaveGameModes();
    }

    /// <summary>Takes a tile out of the lobby (dropped on "Not in the lobby").</summary>
    public void TakeLobbyTileOut(GameModeToggle tile)
    {
        if (!_lobbyTiles.Contains(tile) || tile.IsOff) return;
        SelectedLobbyTile = tile;
        tile.StateIndex = GameModeToggle.Off; // goes through OnGameModeStateChanged (keeps one Battle Royale mode on)
    }

    public void SelectLobbyTile(GameModeToggle tile) => SelectedLobbyTile = tile;

    private void NudgeLobbyTile(int delta)
    {
        var tile = _selectedLobbyTile;
        if (tile is null || tile.IsOff) return;
        int row = Math.Min(tile.Row, LobbyRowCount - 1);
        var inRow = TilesInRow(row);
        int from = inRow.IndexOf(tile), to = from + delta;
        if (from < 0 || to < 0 || to >= inRow.Count) return;
        // PlaceLobbyTile counts positions with the tile still in the row: right of the next tile is to + 1.
        PlaceLobbyTile(tile, row, delta > 0 ? to + 1 : to);
    }

    private void MakeLobbyDefault()
    {
        var tile = _selectedLobbyTile;
        if (tile is null || !tile.IsTile) return;
        if (!tile.IsOn) { GameModesNote = "Turn " + tile.Name + " on first: Fortnite can only open on a mode that is on."; return; }
        _chosenDefault = tile.Id;
        SaveGameModes();
    }

    private void SaveGameModes()
    {
        var choice = _gameModeSeason;
        if (choice is null) return;
        RefreshDefaultMarks();
        var shown = LobbyDisplayOrder();
        int last = LobbyRowCount - 1;
        var on = shown.Where(t => t.IsOn).Select(t => t.Id).ToList();
        if (_lobbyStw is { IsOn: true }) on.Add(_lobbyStw.Id);
        var layout = new SeasonLayout
        {
            Order = shown.Select(t => t.Id).ToList(),
            Default = shown.FirstOrDefault(t => t.IsOn && string.Equals(t.Id, _chosenDefault, StringComparison.OrdinalIgnoreCase))?.Id ?? "",
            Badges = shown.Where(t => t.BadgeIndex > 0).ToDictionary(t => t.Id, t => t.BadgeIndex == 1 ? "new" : "limited"),
            Locked = shown.Where(t => t.IsLocked).Select(t => t.Id).ToList(),
            Rows = shown.Where(t => Math.Min(t.Row, last) > 0).ToDictionary(t => t.Id, t => Math.Min(t.Row, last))
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
        QueueLobbyRefresh();
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
