using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Threading.Tasks;
using System.Collections.ObjectModel;
using System.Collections.Specialized;
using System.Diagnostics;
using System.Text.RegularExpressions;
using System.Windows;
using System.Windows.Input;
using System.Windows.Media;
using Microsoft.Win32;
using SyndicateLauncher.Models;
using SyndicateLauncher.Services;
namespace SyndicateLauncher.ViewModels;
public enum BuildState
{
    None,
    Detecting,
    Found,
    Problem
}
public enum ServerStatusKind
{
    Idle,
    Starting,
    Ready,
    Error
}
public enum AppScreen
{
    Home,
    Play,
    Chapter,
    Season,
    Credits,
    Studio,
    PatchNotes
}
public sealed partial class MainViewModel : ViewModelBase
{
    private const string NoFolderText = "Select your Fortnite build folder";
    private const string CommunityUrl = "https://discord.gg/synfn";
    private const string SupportUrl = "https://discord.gg/synsupport";
    private const string DonateUrl = "https://buymeacoffee.com/synfn";
    private static readonly Regex ProfileNameRule = new(@"^[A-Za-z0-9_\- ]{3,16}$", RegexOptions.Compiled);
    private readonly SettingsService _store;
    private readonly AppSettings _settings;
    private int _detectionId;
    private BuildState _state = BuildState.None;
    private BuildInfo? _build;
    private string _problemMessage = "";
    private string _gameDirectory = "";
    private bool _enableMenu;
    private bool _turboBuild = ClientMods.TurboBuild;
    private bool _resetOnRelease = ClientMods.ResetOnRelease;
    private bool _sprintByDefault = InputMods.SprintByDefaultEnabled;
    private bool _autoRun = InputMods.AutoRunEnabled;
    private KeyChoice? _sprintKeyChoice;
    private KeyChoice? _autoRunKeyChoice;
    private bool _classicArPump = ClientMods.ClassicArPump;
    private bool _botEmotesDisabled = ClientMods.BotEmotesDisabled;
    private bool _editSelectRelease = ClientMods.EditSelectRelease;
    private bool _editReleaseNeedsTile = ClientMods.EditReleaseNeedsTile;
    private string _editKey = ClientMods.EditKey;
    private KeyChoice? _selectedEditKey;
    private string _padEditButton = ClientMods.PadEditButton;
    private float _fov = ClientMods.Fov;
    private bool _isSettingsOpen;
    private Profile? _selectedProfile;
    private string _newProfileName = "";
    private bool _newProfileIsProgression = true;
    private string _profileError = "";
    private string _statusMessage = "";
    private string _serverStatusText = "Idle";
    private ServerStatusKind _serverStatusState = ServerStatusKind.Idle;
    private bool _isPlayerSelectOpen;
    private AvatarChoice? _selectedAvatar;
    private string _avatarError = "";
    private bool _launching;
    private bool _isAddingExclusions;
    private string _exclusionStatus = "";
    private AppScreen _currentScreen = AppScreen.Home;
    private ChapterInfo? _selectedChapter;
    private SeasonInfo? _selectedSeason;
    private SeasonInfo? _activeSeason;
    private int _browserChapter = 1;
    private SeasonInfo? _browserSeason;
    private bool _suppressBrowserSync = true;
    public MainViewModel():this(new SettingsService(),new PresetStore()){}
    public MainViewModel(SettingsService store,PresetStore presets)
    {
        _store=store;_presets=presets;
        _settings = _store.Load();
        _settings.BotNameOverrides ??= new();
        _settings.Theme ??= new ThemeSettings();
        ThemeService.Attach(_settings.Theme);
        Themes = new ThemesViewModel(_settings.Theme, Save);
        Home = new HomeViewModel(() => SelectedProfile?.Name, EnsureBackendForHomeAsync);
        _enableMenu = _settings.EnableMenu;
        ClientMods.EditOnRelease = _settings.EditOnRelease;
        _selectedEditKey = EditKeyChoices.All.FirstOrDefault(k => k.Value == ClientMods.EditKey)
                           ?? EditKeyChoices.All.First(k => k.Value == "key:71");
        _sprintKeyChoice = EditKeyChoices.All.FirstOrDefault(k => k.Value == InputMods.SprintKey)
                           ?? EditKeyChoices.All.First(k => k.Value == "key:160");
        _autoRunKeyChoice = EditKeyChoices.All.FirstOrDefault(k => k.Value == InputMods.AutoRunKey)
                           ?? EditKeyChoices.All.First(k => k.Value == "key:187");
        InitializeCrosshair();
        InitializeDifficulty();
        InitializeBotNames();
        InitializeCustomBotNames();
        foreach (var profile in _settings.Profiles)
        {
            ProfileAvatars.Ensure(profile);
            Profiles.Add(profile);
        }
        Save();
        foreach (string id in ProfileAvatars.Ids)
            AvatarChoices.Add(new AvatarChoice(id, ProfileAvatars.Load(id)));
        Profiles.CollectionChanged += OnProfilesChanged;
        TiltedImport.PlayersImported += OnTiltedPlayersImported;
        _selectedProfile = Profiles.FirstOrDefault(p =>
                               string.Equals(p.Name, _settings.ActiveProfile, StringComparison.OrdinalIgnoreCase))
                           ?? Profiles.FirstOrDefault();
        OpenPlayerSelectCommand = new RelayCommand(() =>
        {
            if (SelectedProfile is not null) ProfileAvatars.Ensure(SelectedProfile);
            AvatarError = "";
            SyncSelectedAvatar();
            if (!_actionBusy) EndPlayerAction(); // never reopen on a stale confirmation step
            IsPlayerSelectOpen = true;
        });
        ClosePlayerSelectCommand = new RelayCommand(() => IsPlayerSelectOpen = false);
        ChooseCustomAvatarCommand = new RelayCommand(ChooseCustomAvatar);
        BrowseCommand = new RelayCommand(Browse);
        OpenCommunityCommand = new RelayCommand(OpenCommunity);
        OpenSupportCommand = new RelayCommand(OpenSupport);
        OpenDonateCommand = new RelayCommand(OpenDonate);
        HomeCommand = new RelayCommand(ShowHome);
        OpenPlayCommand = new RelayCommand(ShowPlay);
        OpenCreditsCommand = new RelayCommand(ShowCredits);
        OpenPatchNotesCommand = new RelayCommand(ShowPatchNotes);
        OpenStudioScreenCommand = new RelayCommand(() => { CurrentScreen = AppScreen.Studio; IsSettingsOpen = false; });
        OpenChapterCommand = new RelayCommand(OpenChapter);
        OpenActiveChapterCommand = new RelayCommand(OpenActiveChapter);
        SelectBrowserChapterCommand = new RelayCommand(p =>
        {
            if (p is not null && int.TryParse(p.ToString(), out var chapter)) SelectBrowserChapter(chapter);
        });
        BrowserPreviousCommand = new RelayCommand(() => StepBrowser(-1), () => BrowserIndex > 0);
        BrowserNextCommand = new RelayCommand(() => StepBrowser(1), () => BrowserIndex >= 0 && BrowserIndex < BrowserSeasons.Count - 1);
        PlayBrowserCommand = new RelayCommand(PlayBrowserSeason, () =>
            BrowserSeason?.IsReleased == true &&
            DirectoryForSeason(BrowserSeason) is not null &&
            !_launching && !GameLauncher.IsRunning);
        BrowserSelectBuildCommand = new RelayCommand(async () =>
        {
            var season = BrowserSeason;
            if (season is null || !season.IsReleased) return;
            if (!await EnsureSeasonBuildAsync(season, chooseFolder: true)) return;
            StatusMessage = $"{season.Name} is ready: Fortnite {Build?.Version}.";
            CommandManager.InvalidateRequerySuggested();
        }, () => BrowserSeason?.IsReleased == true && !_launching);
        BackToChaptersCommand = new RelayCommand(ShowHome);
        BackToSeasonsCommand = new RelayCommand(BackToSeasons);
        ChooseSeasonBuildCommand = new RelayCommand(ChooseSeasonBuild, () => CanChooseSeasonBuild);
        OpenWheelSeasonCommand = new RelayCommand(OpenWheelSeason, () => CanChooseSeasonBuild && !_findingSeasonBuild);
        PlayCommand = new RelayCommand(Play, () => State == BuildState.Found && !_launching && !GameLauncher.IsRunning && IsPlayableBuild(Build));
        ToggleSettingsCommand = new RelayCommand(() => IsSettingsOpen = !IsSettingsOpen);
        // Esc first closes an open theme editor, then the settings sheet.
        CloseSettingsCommand = new RelayCommand(() =>
        {
            if (Themes.IsEditorOpen) Themes.CancelEditorCommand.Execute(null);
            else IsSettingsOpen = false;
        });
        CreateProfileCommand = new RelayCommand(CreateProfile);
        DeleteProfileCommand = new RelayCommand(p => DeleteProfile(p as Profile));
        SelectProgressionModeCommand = new RelayCommand(() => NewProfileIsProgression = true);
        SelectUnlockedAllModeCommand = new RelayCommand(() => NewProfileIsProgression = false);
        SetDifficultyCommand = new RelayCommand(p =>
        {
            if (p is not null && int.TryParse(p.ToString(), out var level)) DifficultyIndex = level;
        });
        AddExclusionsCommand = new RelayCommand(
            async () => await AddExclusionsAsync(),
            () => !_isAddingExclusions);
        RestoreActiveSeason();
        ShowPatchNotesAfterUpdate();
        SelectBrowserChapter(1);
        // Home must show the season the player picked, not whichever build folder was
        // imported last. Prefer the last season's linked folder; if it has none, show no build.
        string startupDirectory = _settings.GameDirectory;
        if (ActiveSeason is not null)
        {
            string? linked = DirectoryForSeason(ActiveSeason);
            if (linked is not null) startupDirectory = linked;
            else startupDirectory = "";
        }
        if (!string.Equals(_settings.GameDirectory, startupDirectory, StringComparison.OrdinalIgnoreCase))
        {
            _settings.GameDirectory = startupDirectory;
            Save();
        }
        if (!string.IsNullOrWhiteSpace(startupDirectory))
            SetGameDirectory(startupDirectory, persist: false);
        _suppressBrowserSync = false;
        // A Project Tilted account on this PC: start the backend now so the account is brought over
        // (Services/TiltedImport.cs) before a new player sees an empty "Who's playing?" and makes another one.
        if (TiltedImport.Pending) _ = GameLauncher.WarmBackendAsync(ActiveSeason?.SeasonNumber ?? 10);
    }
    // ---------------------------------------------------------------- commands
    public ICommand BrowseCommand { get; }
    public ICommand OpenPlayerSelectCommand { get; }
    public ICommand ClosePlayerSelectCommand { get; }
    public ICommand ChooseCustomAvatarCommand { get; }
    public ICommand OpenCommunityCommand { get; }
    public ICommand OpenSupportCommand { get; }
    public ICommand OpenDonateCommand { get; }
    public ICommand HomeCommand { get; }
    public ICommand OpenPlayCommand { get; }
    public ICommand OpenCreditsCommand { get; }
    public ICommand OpenPatchNotesCommand { get; }
    /// <summary>The Studio page (dev launcher): key art and Open Studio.</summary>
    public ICommand OpenStudioScreenCommand { get; }
    /// <summary>The Themes tab.</summary>
    public ThemesViewModel Themes { get; }
    public HomeViewModel Home { get; }

    /// <summary>Home's achievements and V-Bucks come from the local backend; start it if Play could start it now.</summary>
    private Task EnsureBackendForHomeAsync()
    {
        if (_launching || GameLauncher.IsRunning || ActiveSeason is null) return Task.CompletedTask;
        return GameLauncher.WarmBackendAsync(ActiveSeason.SeasonNumber);
    }
    public ICommand OpenChapterCommand { get; }
    public ICommand OpenActiveChapterCommand { get; }
    public ICommand SelectBrowserChapterCommand { get; }
    public ICommand BrowserPreviousCommand { get; }
    public ICommand BrowserNextCommand { get; }
    public ICommand PlayBrowserCommand { get; }
    public ICommand BrowserSelectBuildCommand { get; }
    public ICommand BackToChaptersCommand { get; }
    public ICommand BackToSeasonsCommand { get; }
    public ICommand ChooseSeasonBuildCommand { get; }
    /// <summary>The season wheel's button: uses the build already linked for that season (also from the
    /// Season Browser), or finds it next to your other builds, and only asks for a folder when there is none.</summary>
    public ICommand OpenWheelSeasonCommand { get; }
    public ICommand PlayCommand { get; }
    public ICommand ToggleSettingsCommand { get; }
    public ICommand CloseSettingsCommand { get; }
    public ICommand CreateProfileCommand { get; }
    public ICommand DeleteProfileCommand { get; }
    public ICommand SelectProgressionModeCommand { get; }
    public ICommand SelectUnlockedAllModeCommand { get; }
    public ICommand SetDifficultyCommand { get; }
    public ICommand AddExclusionsCommand { get; }
    // ---------------------------------------------------------------- game folder + build
    public string DisplayPath => HasDirectory ? _gameDirectory : NoFolderText;
    public bool HasDirectory => !string.IsNullOrWhiteSpace(_gameDirectory);        public BuildState State
        {
            get => _state;
            private set
            {
                if (!Set(ref _state, value)) return;
                OnPropertyChanged(nameof(PlayHint));
                OnPropertyChanged(nameof(BuildStateLabel));
                OnPropertyChanged(nameof(BuildStateDetail));
                OnPropertyChanged(nameof(BuildSourceText));
                CommandManager.InvalidateRequerySuggested();
            }
        }
    public BuildInfo? Build
    {
        get => _build;
        private set
        {
            if (!Set(ref _build, value)) return;
            OnPropertyChanged(nameof(BuildDetail));
            OnPropertyChanged(nameof(PlayHint));
            OnPropertyChanged(nameof(BuildWarning));
            OnPropertyChanged(nameof(HasBuildWarning));
            OnPropertyChanged(nameof(BuildVersionText));
            OnPropertyChanged(nameof(BuildEraText));
            OnPropertyChanged(nameof(BuildSourceText));
            OnPropertyChanged(nameof(BuildStateLabel));
            OnPropertyChanged(nameof(BuildStateDetail));
        }
    }        public string ProblemMessage
        {
            get => _problemMessage;
            private set
            {
                if (!Set(ref _problemMessage, value)) return;
                OnPropertyChanged(nameof(BuildStateDetail));
            }
        }
        public string BuildVersionText => Build?.Version ?? "—";
        public string BuildEraText => Build?.Era ?? "Chapter 1, Season X";
        public string BuildSourceText => State == BuildState.Found
            ? Build?.SourceLabel ?? ""
            : "Choose your Fortnite install folder";
        public string BuildStateLabel => State switch
        {
            BuildState.Found => HasBuildWarning ? "BUILD MISMATCH" : "READY TO LAUNCH",
            BuildState.Detecting => "READING BUILD",
            BuildState.Problem => "BUILD NOT FOUND",
            _ => "NO BUILD LINKED"
        };
        public string BuildStateDetail => State switch
        {
            BuildState.Found => HasBuildWarning ? BuildWarning : BuildDetail,
            BuildState.Detecting => "Reading the version from your game files…",
            BuildState.Problem => ProblemMessage,
            _ => "Select the folder that holds your Fortnite build."
        };
        public string FolderActionText => HasDirectory ? "Change folder" : "Choose folder";
        public string LauncherVersionText => ResolveLauncherVersion();
        /// <summary>The release build stamp ("Build 2026.11.17.0430"), or "" in a dev build.</summary>
        public string LauncherBuildText => ResolveReleaseTag() is { } tag ? "Build " + tag.TrimStart('v', 'V') : "";
        public string SettingsVersionLine => "SYNDICATE " + LauncherVersionText
            + (LauncherBuildText.Length > 0 ? " (" + LauncherBuildText + ")" : "") + "\nEsc closes settings";
        public string AboutVersionLine => "Syndicate Launcher " + LauncherVersionText
            + (LauncherBuildText.Length > 0 ? " (" + LauncherBuildText + ")" : "") + " - Chapter 1 (1.7.2 to 10.40)";

        // The version players see is the launcher's own version (<Version> in SyndicateLauncher.csproj, which
        // matches the newest entry in Models/PatchNotes.cs). The release build stamp that the updater compares
        // (Runtime\release-tag.txt, written by the release scripts) is shown next to it as "Build ...".
        private static string? _launcherVersion;
        private static string ResolveLauncherVersion()
        {
            if (_launcherVersion is not null) return _launcherVersion;
            try
            {
                var v = System.Reflection.Assembly.GetExecutingAssembly().GetName().Version;
                if (v is not null) return _launcherVersion = "v" + v.ToString(3);
            }
            catch { }
            return _launcherVersion = "v" + PatchNotes.Latest.Version;
        }

        private static string? _releaseTag;
        private static bool _releaseTagRead;
        private static string? ResolveReleaseTag()
        {
            if (_releaseTagRead) return _releaseTag;
            _releaseTagRead = true;
#if !DEV_BUILD
            try
            {
                foreach (var path in new[]
                {
                    System.IO.Path.Combine(System.AppContext.BaseDirectory, "Runtime", "release-tag.txt"),
                    System.IO.Path.Combine(System.AppContext.BaseDirectory, "release-tag.txt"),
                })
                {
                    if (System.IO.File.Exists(path))
                    {
                        var tag = System.IO.File.ReadAllText(path).Trim();
                        if (tag.Length > 0) return _releaseTag = tag;
                    }
                }
            }
            catch { }
#endif
            return _releaseTag;
        }
    /// <summary>For example "Changelist 3700114. Read from game files."</summary>
    public string BuildDetail
    {
        get
        {
            if (Build is null) return "";
            return Build.Changelist is int cl
                ? $"Changelist {cl}. {Build.SourceLabel}."
                : $"{Build.SourceLabel}.";
        }
    }
    // Every supported Chapter 1 build runs Syndicate's Erbium server.
    private static bool IsPlayableBuild(BuildInfo? build) =>
        build is not null && build.Source == DetectionSource.GameFiles && GameLauncher.IsSupportedBuild(build.Version);

    public string BuildWarning => Build is null ? "" :
        IsPlayableBuild(Build) ? "" :
        "Unsupported build. Chapter 1 needs 1.7.2, 1.8.2, 1.11, 2.5, 3.5.1, 4.5, 5.10, 6.20, 7.40, 8.50, 9.40 or 10.40 CL-9380822.";

    public bool HasBuildWarning => BuildWarning.Length > 0;
    private void OpenCommunity()
    {
        try
        {
            Process.Start(new ProcessStartInfo(CommunityUrl) { UseShellExecute = true });
        }
        catch (Exception ex)
        {
            StatusMessage = $"Couldn't open the Syndicate Discord: {ex.Message}";
        }
    }
    private void OpenSupport()
    {
        try
        {
            Process.Start(new ProcessStartInfo(SupportUrl) { UseShellExecute = true });
        }
        catch (Exception ex)
        {
            StatusMessage = $"Couldn't open Syndicate support: {ex.Message}";
        }
    }
    private void OpenDonate()
    {
        try
        {
            Process.Start(new ProcessStartInfo(DonateUrl) { UseShellExecute = true });
        }
        catch (Exception ex)
        {
            StatusMessage = $"Couldn't open the donation page: {ex.Message}";
        }
    }

    // ---------------------------------------------------------------- chapters + seasons
    public ObservableCollection<SeasonInfo> Seasons { get; } = new();
    public AppScreen CurrentScreen
    {
        get => _currentScreen;
        private set
        {
            if (!Set(ref _currentScreen, value)) return;
            OnPropertyChanged(nameof(IsHomeScreen));
            OnPropertyChanged(nameof(IsPlayScreen));
            OnPropertyChanged(nameof(IsChapterScreen));
            OnPropertyChanged(nameof(IsSeasonScreen));
            OnPropertyChanged(nameof(IsCreditsScreen));
            OnPropertyChanged(nameof(IsStudioScreen));
            OnPropertyChanged(nameof(IsPatchNotesScreen));
        }
    }
    public bool IsHomeScreen => CurrentScreen == AppScreen.Home;
    public bool IsPlayScreen => CurrentScreen == AppScreen.Play;
    public bool IsChapterScreen => CurrentScreen == AppScreen.Chapter;
    public bool IsSeasonScreen => CurrentScreen == AppScreen.Season;
    public bool IsCreditsScreen => CurrentScreen == AppScreen.Credits;
    public bool IsStudioScreen => CurrentScreen == AppScreen.Studio;
    public bool IsPatchNotesScreen => CurrentScreen == AppScreen.PatchNotes;
    public ChapterInfo? SelectedChapter
    {
        get => _selectedChapter;
        private set
        {
            if (!Set(ref _selectedChapter, value)) return;
            OnPropertyChanged(nameof(ChapterLabel));
        }
    }
    public SeasonInfo? SelectedSeason
    {
        get => _selectedSeason;
        set
        {
            if (!Set(ref _selectedSeason, value)) return;
            OnPropertyChanged(nameof(SelectedSeasonName));
            OnPropertyChanged(nameof(SelectedSeasonHeading));
            OnPropertyChanged(nameof(SelectedSeasonStatus));
            OnPropertyChanged(nameof(SeasonActionText));
            OnPropertyChanged(nameof(SeasonActionHint));
            OnPropertyChanged(nameof(CanChooseSeasonBuild));
            CommandManager.InvalidateRequerySuggested();
        }
    }
    public string ChapterLabel => SelectedChapter is null ? "" : $"CHAPTER {SelectedChapter.Number}";
    public string SelectedSeasonName => SelectedSeason?.Name ?? "Select a season";
    public string SelectedSeasonHeading => (SelectedSeason?.Name ?? "Select a season").ToUpperInvariant();
    public string SelectedSeasonStatus => SelectedSeason is null ? "" : !SelectedSeason.IsReleased ? "NOT RELEASED"
        : "FORTNITE " + SelectedSeason.BuildVersionsLabel + (DirectoryForSeason(SelectedSeason) is not null ? "  \u00B7  BUILD READY" : "");
    public string SeasonActionText => SelectedSeason?.IsReleased != true ? "NOT RELEASED"
        : _findingSeasonBuild ? "FINDING BUILD..."
        : DirectoryForSeason(SelectedSeason) is not null ? "CONTINUE" : "SELECT BUILD";
    public string SeasonActionHint => SelectedSeason?.IsReleased != true ? "This season isn't released yet."
        : DirectoryForSeason(SelectedSeason) is { } linked ? "Uses your build in " + linked + ". Change it on the next screen."
        : "Looks for this season's build next to your other builds first, then asks for the folder.";
    public bool CanChooseSeasonBuild => SelectedSeason?.IsReleased == true;
    public SeasonInfo? ActiveSeason
    {
        get => _activeSeason;
        private set
        {
            if (!Set(ref _activeSeason, value)) return;
            OnPropertyChanged(nameof(HasSeasonBackground));
            OnPropertyChanged(nameof(ActiveSeasonArt));
            OnPropertyChanged(nameof(ActiveSeasonAccentBrush));
            OnPropertyChanged(nameof(ActiveSeasonAccentGradient));
            OnPropertyChanged(nameof(ActiveSeasonOnAccentBrush));
            OnPropertyChanged(nameof(ActiveSeasonTitleBrush));
            OnPropertyChanged(nameof(ActiveSeasonHeroGradient));
            if (CurrentScreen == AppScreen.Play) WarmBackend();
        }
    }
    public bool HasSeasonBackground => ActiveSeasonArt is not null;
    /// <summary>The selected season's keyart, shown behind the home hero.</summary>
    public ImageSource? ActiveSeasonArt => ActiveSeason?.BackgroundImage ?? ActiveSeason?.DetailArt;
    public SolidColorBrush ActiveSeasonAccentBrush => ActiveSeason?.AccentBrush ?? FallbackAccentBrush;
    public LinearGradientBrush ActiveSeasonAccentGradient => ActiveSeason?.AccentGradient ?? FallbackAccentGradient;
    public SolidColorBrush ActiveSeasonOnAccentBrush => ActiveSeason?.OnAccentBrush ?? FallbackOnAccentBrush;
    public LinearGradientBrush ActiveSeasonTitleBrush => ActiveSeason?.TitleGradient ?? FallbackTitleGradient;
    public LinearGradientBrush ActiveSeasonHeroGradient => ActiveSeason?.HeroGradient ?? FallbackHeroGradient;

    private static SolidColorBrush Frozen(SolidColorBrush brush)
    {
        brush.Freeze();
        return brush;
    }

    private static LinearGradientBrush Frozen(LinearGradientBrush brush)
    {
        brush.Freeze();
        return brush;
    }

    private static readonly SolidColorBrush FallbackAccentBrush = Frozen(new SolidColorBrush(Color.FromRgb(0xF2, 0xF3, 0xF5)));
    private static readonly SolidColorBrush FallbackOnAccentBrush = Frozen(new SolidColorBrush(Color.FromRgb(0x14, 0x14, 0x14)));
    private static readonly LinearGradientBrush FallbackAccentGradient = Frozen(new LinearGradientBrush(
        Color.FromRgb(0xFF, 0xFF, 0xFF), Color.FromRgb(0xD2, 0xD4, 0xD8), 90));
    private static readonly LinearGradientBrush FallbackTitleGradient = Frozen(new LinearGradientBrush(
        Colors.White, Color.FromRgb(0xCD, 0xCD, 0xCD), 90));
    private static readonly LinearGradientBrush FallbackHeroGradient = Frozen(new LinearGradientBrush(
        Color.FromArgb(0x33, 0xF2, 0xF3, 0xF5), Color.FromArgb(0x00, 0xF2, 0xF3, 0xF5),
        new Point(0, 0), new Point(1, 1)));
    private static readonly LinearGradientBrush FallbackDetailGradient = Frozen(new LinearGradientBrush(
        Color.FromArgb(0xF0, 0x0B, 0x0B, 0x0B), Color.FromArgb(0x8C, 0x0B, 0x0B, 0x0B), 0));
    private void RestoreActiveSeason()
    {
        ActiveSeason = FindSeason(_settings.LastBuildSeasonKey);
        if (ActiveSeason is null && !string.IsNullOrWhiteSpace(_settings.GameDirectory))
        {
            ActiveSeason = SeasonCatalog.Chapters
                .SelectMany(chapter => SeasonCatalog.GetSeasons(chapter.Number))
                .FirstOrDefault(season => season.IsReleased);
        }
        if (ActiveSeason is not null && string.IsNullOrWhiteSpace(_settings.LastBuildSeasonKey))
        {
            _settings.LastBuildSeasonKey = ActiveSeason.Key;
            Save();
        }
    }
    private static SeasonInfo? FindSeason(string? key)
    {
        if (string.IsNullOrWhiteSpace(key)) return null;
        return SeasonCatalog.Chapters
            .SelectMany(chapter => SeasonCatalog.GetSeasons(chapter.Number))
            .FirstOrDefault(season => string.Equals(season.Key, key, StringComparison.OrdinalIgnoreCase));
    }
    private void ShowHome()
    {
        CurrentScreen = AppScreen.Home;
        IsSettingsOpen = false;
        _ = Home.RefreshAsync();
    }
    private void ShowCredits()
    {
        CurrentScreen = AppScreen.Credits;
        IsSettingsOpen = false;
    }

    private void BackToSeasons()
    {
        if (SelectedChapter is null)
        {
            ShowHome();
            return;
        }
        CurrentScreen = AppScreen.Chapter;
        IsSettingsOpen = false;
    }
    // ---------------------------------------------------------------- season browser (Play tab)
    public ObservableCollection<SeasonInfo> BrowserSeasons { get; } = new();

    public int BrowserChapter => _browserChapter;
    public string BrowserChapterLabel => $"CHAPTER {_browserChapter}";

    public SeasonInfo? BrowserSeason
    {
        get => _browserSeason;
        set
        {
            if (!Set(ref _browserSeason, value)) return;
            OnPropertyChanged(nameof(BrowserSeasonChapter));
            OnPropertyChanged(nameof(BrowserSeasonLabel));
            OnPropertyChanged(nameof(BrowserSeasonName));
            OnPropertyChanged(nameof(BrowserSeasonTagline));
            OnPropertyChanged(nameof(BrowserSeasonDates));
            OnPropertyChanged(nameof(BrowserSeasonDescription));
            OnPropertyChanged(nameof(BrowserFeatures));
            OnPropertyChanged(nameof(BrowserSeasonAvailable));
            OnPropertyChanged(nameof(BrowserPlayText));
            OnPropertyChanged(nameof(BrowserSeasonStatus));
            OnPropertyChanged(nameof(BrowserSeasonAccentBrush));
            OnPropertyChanged(nameof(BrowserSeasonAccentSoftBrush));
            OnPropertyChanged(nameof(BrowserSeasonTitleBrush));
            OnPropertyChanged(nameof(BrowserSeasonDetailGradient));
            OnPropertyChanged(nameof(BrowserSeasonPageGradient));
            OnPropertyChanged(nameof(BrowserIndex));
            OnPropertyChanged(nameof(BrowserSliderValue));
            CommandManager.InvalidateRequerySuggested();
            if (!_suppressBrowserSync && value is not null) _ = SyncHomeToBrowserSeasonAsync(value);
        }
    }

    public string BrowserSeasonChapter => BrowserSeason?.ChapterText ?? "";
    public string BrowserSeasonLabel => BrowserSeason?.Label ?? "";
    public string BrowserSeasonName => BrowserSeason?.Name ?? "";
    public string BrowserSeasonTagline => BrowserSeason?.Tagline ?? "";
    public string BrowserSeasonDates => BrowserSeason?.Dates ?? "";
    public string BrowserSeasonDescription => BrowserSeason?.Description ?? "";
    public IReadOnlyList<string> BrowserFeatures => BrowserSeason?.Features ?? Array.Empty<string>();
    public bool BrowserSeasonAvailable => BrowserSeason?.IsReleased == true;
    public string BrowserPlayText => BrowserSeasonAvailable ? "PLAY" : "NOT AVAILABLE";
    public string BrowserSeasonStatus => BrowserSeason is null ? "" :
        BrowserSeason.IsReleased ? "FORTNITE " + BrowserSeason.BuildVersionsLabel : "NOT AVAILABLE ON THIS BUILD";
    public SolidColorBrush BrowserSeasonAccentBrush => BrowserSeason?.AccentBrush ?? FallbackAccentBrush;
    public SolidColorBrush BrowserSeasonAccentSoftBrush => BrowserSeason?.AccentSoftBrush ?? FallbackAccentBrush;
    public LinearGradientBrush BrowserSeasonTitleBrush => BrowserSeason?.TitleGradient ?? FallbackTitleGradient;
    public LinearGradientBrush BrowserSeasonDetailGradient => BrowserSeason?.DetailGradient ?? FallbackDetailGradient;
    public LinearGradientBrush BrowserSeasonPageGradient => BrowserSeason?.PageGradient ?? FallbackDetailGradient;
    public double BrowserIndex => BrowserSeason is null ? 0 : Math.Max(0, BrowserSeasons.IndexOf(BrowserSeason));
    public int BrowserMaxIndex => Math.Max(0, BrowserSeasons.Count - 1);

    /// <summary>Two-way value for the timeline slider; snaps to the nearest season.</summary>
    public double BrowserSliderValue
    {
        get => BrowserIndex;
        set
        {
            int index = (int)Math.Round(value);
            if (index < 0 || index >= BrowserSeasons.Count) return;
            if (!ReferenceEquals(BrowserSeasons[index], BrowserSeason)) BrowserSeason = BrowserSeasons[index];
        }
    }

    private void ShowPlay()
    {
        EnsureBrowser();
        CurrentScreen = AppScreen.Play;
        IsSettingsOpen = false;
        WarmBackend();
    }

    /// <summary>Starts the local backend while the player is still choosing, so Play does not
    /// spend its first several seconds waiting for MongoDB and the Node services.</summary>
    private void WarmBackend()
    {
        if (_launching || GameLauncher.IsRunning || State != BuildState.Found || Build is null || ActiveSeason is null)
            return;
        _ = GameLauncher.WarmBackendAsync(ActiveSeason.SeasonNumber);
    }

    private void EnsureBrowser()
    {
        if (BrowserSeasons.Count == 0) SelectBrowserChapter(1);
    }

    private void SelectBrowserChapter(int chapter)
    {
        if (chapter is not (1 or 2)) return;
        _browserChapter = chapter;
        OnPropertyChanged(nameof(BrowserChapter));
        OnPropertyChanged(nameof(BrowserChapterLabel));
        BrowserSeasons.Clear();
        foreach (var season in SeasonCatalog.GetSeasons(chapter)) BrowserSeasons.Add(season);
        // Changing chapter is navigation, not a season choice: keep the home card on the
        // build the player actually selected until they click a season.
        bool previous = _suppressBrowserSync;
        _suppressBrowserSync = true;
        try { BrowserSeason = chapter == 1 ? BrowserSeasons.LastOrDefault() : BrowserSeasons.FirstOrDefault(); }
        finally { _suppressBrowserSync = previous; }
        OnPropertyChanged(nameof(BrowserMaxIndex));
        OnPropertyChanged(nameof(BrowserIndex));
    }

    private void StepBrowser(int delta)
    {
        if (BrowserSeasons.Count == 0) return;
        int index = Math.Clamp((int)BrowserIndex + delta, 0, BrowserSeasons.Count - 1);
        BrowserSeason = BrowserSeasons[index];
    }

    private async void PlayBrowserSeason()
    {
        if (BrowserSeason is null || !BrowserSeason.IsReleased) return;
        if (DirectoryForSeason(BrowserSeason) is null)
        {
            StatusMessage = $"Link a build for {BrowserSeason.Name} first (Select build).";
            return;
        }
        ActiveSeason = BrowserSeason;
        _settings.LastBuildSeasonKey = BrowserSeason.Key;
        Save();
        if (!await EnsureSeasonBuildAsync(BrowserSeason)) return;
        Play();
    }

    private void OpenChapter(object? parameter)
    {
        if (parameter is null || !int.TryParse(parameter.ToString(), out int number)) return;
        var chapter = SeasonCatalog.Chapters.FirstOrDefault(c => c.Number == number);
        if (chapter is null) return;
        SelectedChapter = chapter;
        Seasons.Clear();
        foreach (var season in SeasonCatalog.GetSeasons(number)) Seasons.Add(season);
        // Opening from Home should highlight the season the card is already showing.
        var active = ActiveSeason is not null && ActiveSeason.ChapterNumber == number
            ? Seasons.FirstOrDefault(s => string.Equals(s.Key, ActiveSeason.Key, StringComparison.OrdinalIgnoreCase))
            : null;
        SelectedSeason = active ?? Seasons.FirstOrDefault(s => s.IsReleased) ?? Seasons.FirstOrDefault();
        CurrentScreen = AppScreen.Chapter;
        IsSettingsOpen = false;
    }

    private void OpenActiveChapter()
    {
        OpenChapter((ActiveSeason?.ChapterNumber ?? 1).ToString());
    }
    private async void ChooseSeasonBuild()
    {
        if (SelectedSeason is null || !SelectedSeason.IsReleased) return;
        ActiveSeason = SelectedSeason;
        _settings.LastBuildSeasonKey = SelectedSeason.Key;
        Save();
        if (!await EnsureSeasonBuildAsync(SelectedSeason, chooseFolder: true)) return;
        CurrentScreen = AppScreen.Season;
        IsSettingsOpen = false;
    }
    private bool _findingSeasonBuild;

    private void RefreshSeasonAction()
    {
        OnPropertyChanged(nameof(SelectedSeasonStatus));
        OnPropertyChanged(nameof(SeasonActionText));
        OnPropertyChanged(nameof(SeasonActionHint));
        CommandManager.InvalidateRequerySuggested();
    }

    private async void OpenWheelSeason()
    {
        var season = SelectedSeason;
        if (season is null || !season.IsReleased || _findingSeasonBuild) return;
        string? directory = DirectoryForSeason(season);
        if (directory is null)
        {
            _findingSeasonBuild = true;
            RefreshSeasonAction();
            try { directory = await FindSeasonBuildAsync(season); }
            finally { _findingSeasonBuild = false; RefreshSeasonAction(); }
            if (directory is not null)
            {
                _settings.BuildDirectories[season.Key] = directory;
                Save();
                LogAdded?.Invoke($"Found the {season.Name} build (Fortnite {season.BuildVersion}) in {directory}.");
            }
        }
        if (!ReferenceEquals(SelectedSeason, season)) return;
        ActiveSeason = season;
        _settings.LastBuildSeasonKey = season.Key;
        Save();
        // A linked or found folder is used as is; only a season with no build asks for one.
        if (!await EnsureSeasonBuildAsync(season, chooseFolder: false)) { RefreshSeasonAction(); return; }
        RefreshSeasonAction();
        CurrentScreen = AppScreen.Season;
        IsSettingsOpen = false;
    }

    /// <summary>Looks for <paramref name="season"/>'s build among the folders the launcher already knows
    /// (every linked season, the current folder) and the folders next to them, e.g. Builds\1.8.2 beside
    /// Builds\3.5. Only folders with the game exe are read, and the version comes from the game files.</summary>
    private Task<string?> FindSeasonBuildAsync(SeasonInfo season)
    {
        var known = _settings.BuildDirectories.Values.Append(_settings.GameDirectory).Append(_gameDirectory)
            .Where(p => !string.IsNullOrWhiteSpace(p)).ToList();
        string wanted = season.BuildVersion;
        return Task.Run(() =>
        {
            var seen = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
            var candidates = new List<string>();
            void Add(string? dir)
            {
                if (string.IsNullOrWhiteSpace(dir)) return;
                try { dir = Path.GetFullPath(dir).TrimEnd(Path.DirectorySeparatorChar); } catch { return; }
                if (seen.Add(dir) && Directory.Exists(dir)) candidates.Add(dir);
            }
            foreach (var dir in known)
            {
                Add(dir);
                try
                {
                    string? parent = Path.GetDirectoryName(Path.GetFullPath(dir).TrimEnd(Path.DirectorySeparatorChar));
                    if (parent is null) continue;
                    foreach (var sibling in Directory.EnumerateDirectories(parent)) Add(sibling);
                }
                catch { }
            }
            // Folders named after the version are read first (a full exe scan takes a moment).
            foreach (var dir in candidates.OrderByDescending(d => Path.GetFileName(d).Contains(wanted, StringComparison.OrdinalIgnoreCase)))
            {
                try
                {
                    if (!BuildDetector.HasExecutable(dir)) continue;
                    var result = BuildDetector.Detect(dir);
                    if (result.Build is { Source: DetectionSource.GameFiles } build && season.AcceptsBuild(build.Version))
                        return dir;
                }
                catch { }
            }
            return (string?)null;
        });
    }

    private void Browse()
    {
        var dialog = new OpenFolderDialog
        {
            Title = "Select your Fortnite build folder",
            Multiselect = false
        };
        if (HasDirectory && Directory.Exists(_gameDirectory))
            dialog.InitialDirectory = _gameDirectory;
        if (dialog.ShowDialog() == true && !string.IsNullOrWhiteSpace(dialog.FolderName))
            SetGameDirectory(dialog.FolderName, persist: true);
    }
    private void SetGameDirectory(string path, bool persist)
    {
        _gameDirectory = path;
        OnPropertyChanged(nameof(DisplayPath));
        OnPropertyChanged(nameof(HasDirectory));
        OnPropertyChanged(nameof(FolderActionText));
        StatusMessage = "";
        if (persist)
        {
            _settings.GameDirectory = path;
            Save();
        }
        _ = DetectIntoStateAsync(path);
    }
    private async Task<DetectionResult> DetectIntoStateAsync(string path)
    {        int id = ++_detectionId;
        Build = null;
        ProblemMessage = "";
        State = BuildState.Detecting;
        var result = await Task.Run(() => BuildDetector.Detect(path));
        if (id != _detectionId) return result; // the player picked another folder meanwhile
        if (result.Build is not null)
        {
            Build = result.Build;
            State = BuildState.Found;
            AlignSeasonToBuild(result.Build);
            WarmBackend();
            _ = Home.RefreshAsync(force: true); // V-Bucks and achievements once the backend is up
        }
        else
        {
            ProblemMessage = result.Error ?? "This folder doesn't look like a Fortnite build.";
            State = BuildState.Problem;
        }
        return result;
    }
    /// <summary>Keeps the selected season and this folder's build in sync, and remembers the folder per season.</summary>
    private void AlignSeasonToBuild(BuildInfo build)
    {
        if (build.Source != DetectionSource.GameFiles) return;
        if (ActiveSeason is not null && ActiveSeason.AcceptsBuild(build.Version))
        {
            _settings.BuildDirectories[ActiveSeason.Key] = _gameDirectory;
            Save();
            CommandManager.InvalidateRequerySuggested();
            return;
        }
        var match = FindSeasonForBuild(build.Version);
        if (match is null) return;
        ActiveSeason = match;
        _settings.LastBuildSeasonKey = match.Key;
        _settings.BuildDirectories[match.Key] = _gameDirectory;
        Save();
        CommandManager.InvalidateRequerySuggested();
    }

    /// <summary>Choosing a season in the browser becomes the season shown on Home, with its keyart and build.</summary>
    private async Task SyncHomeToBrowserSeasonAsync(SeasonInfo season)
    {
        if (!season.IsReleased) return;
        ActiveSeason = season;
        if (!string.Equals(_settings.LastBuildSeasonKey, season.Key, StringComparison.OrdinalIgnoreCase))
        {
            _settings.LastBuildSeasonKey = season.Key;
            Save();
        }

        string? directory = DirectoryForSeason(season);
        if (directory is null)
        {
            // Show "no build linked" for this season instead of another season's build.
            _detectionId++;
            if (_gameDirectory.Length > 0 || Build is not null)
            {
                _gameDirectory = "";
                Build = null;
                ProblemMessage = "";
                State = BuildState.None;
                _settings.GameDirectory = "";
                Save();
                OnPropertyChanged(nameof(DisplayPath));
                OnPropertyChanged(nameof(HasDirectory));
                OnPropertyChanged(nameof(FolderActionText));
                CommandManager.InvalidateRequerySuggested();
            }
            return;
        }

        if (PathsEqual(_gameDirectory, directory) && Build is not null && season.AcceptsBuild(Build.Version))
            return;

        int id = ++_detectionId;
        await Task.Delay(150); // let a slider drag settle on one season
        if (id != _detectionId || !ReferenceEquals(BrowserSeason, season)) return;
        var result = await Task.Run(() => BuildDetector.Detect(directory));
        if (id != _detectionId || !ReferenceEquals(BrowserSeason, season)) return;

        if (result.Build is null || result.Build.Source != DetectionSource.GameFiles ||
            !season.AcceptsBuild(result.Build.Version))
        {
            Build = null;
            ProblemMessage = result.Build is null
                ? (result.Error ?? $"This folder no longer looks like a {season.BuildVersionsLabel} build.")
                : $"{season.Name} needs Fortnite {season.BuildVersionsLabel}, but this folder is {result.Build.Version}.";
            State = BuildState.Problem;
            return;
        }

        _gameDirectory = directory;
        Build = result.Build;
        ProblemMessage = "";
        State = BuildState.Found;
        _settings.GameDirectory = directory;
        Save();
        OnPropertyChanged(nameof(DisplayPath));
        OnPropertyChanged(nameof(HasDirectory));
        OnPropertyChanged(nameof(FolderActionText));
        CommandManager.InvalidateRequerySuggested();
    }
    private static SeasonInfo? FindSeasonForBuild(string version) =>
        SeasonCatalog.Chapters
            .Where(chapter => chapter.IsReleased)
            .SelectMany(chapter => SeasonCatalog.GetSeasons(chapter.Number))
            .FirstOrDefault(season => season.IsReleased && season.AcceptsBuild(version));
    private string? DirectoryForSeason(SeasonInfo season)
    {
        if (_settings.BuildDirectories.TryGetValue(season.Key, out var path) &&
            !string.IsNullOrWhiteSpace(path) && Directory.Exists(path))
            return path;
        return null;
    }
    private bool PathsEqual(string left, string right)
    {
        try
        {
            return string.Equals(Path.GetFullPath(left).TrimEnd(Path.DirectorySeparatorChar),
                Path.GetFullPath(right).TrimEnd(Path.DirectorySeparatorChar), StringComparison.OrdinalIgnoreCase);
        }
        catch { return false; }
    }
    private string? PromptForSeasonBuild(SeasonInfo season)
    {
        string wanted = "Fortnite " + season.BuildVersionsLabel;
        var dialog = new OpenFolderDialog
        {
            Title = $"Select your {season.Name} build folder ({wanted})",
            Multiselect = false
        };
        if (HasDirectory && Directory.Exists(_gameDirectory))
            dialog.InitialDirectory = _gameDirectory;
        if (dialog.ShowDialog() != true || string.IsNullOrWhiteSpace(dialog.FolderName)) return null;
        return dialog.FolderName;
    }
    private async Task<bool> EnsureSeasonBuildAsync(SeasonInfo season, bool chooseFolder = false)
    {
        string? directory = chooseFolder ? null : DirectoryForSeason(season);
        if (directory is null)
        {
            directory = PromptForSeasonBuild(season);
            if (directory is null) return false;
        }
        if (!PathsEqual(_gameDirectory, directory) || Build is null)
            SetGameDirectory(directory, persist: true);

        var result = await DetectIntoStateAsync(directory);
        if (result.Build is null) return false;
        if (result.Build.Source != DetectionSource.GameFiles || !season.AcceptsBuild(result.Build.Version))
        {
            ProblemMessage = result.Build.Source != DetectionSource.GameFiles
                ? $"Could not read the build version from the game files in this folder. {season.Name} needs Fortnite {season.BuildVersionsLabel}."
                : $"{season.Name} needs Fortnite {season.BuildVersionsLabel}, but this folder is {result.Build.Version}.";
            State = BuildState.Problem;
            StatusMessage = ProblemMessage;
            return false;
        }
        _settings.BuildDirectories[season.Key] = directory;
        Save();
        RefreshSeasonAction();
        return true;
    }
    // ---------------------------------------------------------------- play
    /// <summary>Every status message is also written to the diagnostics bar (Syndicate's Log()).</summary>
    public event Action<string>? LogAdded;
    /// <summary>A launch failed: Diagnostics shows the title in red with the numbered fix steps.</summary>
    public event Action<SyndicateLauncher.Services.LaunchProblem>? ProblemReported;
    /// <summary>A launch got the game ready: earlier problems are fixed and Diagnostics clears them.</summary>
    public event Action? ProblemsResolved;
    public string StatusMessage
    {
        get => _statusMessage;
        private set
        {
            if (!Set(ref _statusMessage, value)) return;
            OnPropertyChanged(nameof(HasStatus));
            if (value.Length > 0) LogAdded?.Invoke(value);
        }
    }
    public bool HasStatus => _statusMessage.Length > 0;
    /// <summary>Latest server/launch step for the Home status card; general launcher messages stay in StatusMessage.</summary>
    public string ServerStatusText
    {
        get => _serverStatusText;
        private set => Set(ref _serverStatusText, value);
    }
    public ServerStatusKind ServerStatusState
    {
        get => _serverStatusState;
        private set => Set(ref _serverStatusState, value);
    }
    public string PlayHint => State == BuildState.Found && Build is not null
        ? $"Launch Fortnite {Build.Version}"
        : "Choose a Fortnite build folder first";
    private async void Play() => await PlayAsync();

    // ---------------------------------------------------------------- Syndicate Studio
    /// <summary>The seasons Syndicate Studio can start: Chapter 1 seasons that run on the Erbium match server.</summary>
    public IReadOnlyList<SeasonInfo> StudioSeasons =>
        SeasonCatalog.GetSeasons(1).Where(s => s.IsReleased && GameLauncher.IsSyndicateBuild(s.BuildVersion)).ToList();

    /// <summary>True when the launcher already knows that season's build folder (else Start asks for it).</summary>
    public bool HasBuildFolder(SeasonInfo season) => DirectoryForSeason(season) is not null;

    /// <summary>The season the launcher has selected now, for the Studio's season picker.</summary>
    public string? ActiveSeasonKey => ActiveSeason?.Key;

    /// <summary>Syndicate Studio's Start: selects <paramref name="seasonKey"/> (asking for its build folder the first
    /// time), then the same launch as Play, without SyndicateMenu.dll. <paramref name="status"/> gets every status line;
    /// returns true once Fortnite is up (false: the reason is in <paramref name="status"/> or Diagnostics).</summary>
    public async Task<bool> StartStudioAsync(string seasonKey, Action<string> status)
    {
        if (_launching || GameLauncher.IsRunning) { status("A match is already starting or running."); return false; }
        var season = StudioSeasons.FirstOrDefault(s => string.Equals(s.Key, seasonKey, StringComparison.OrdinalIgnoreCase));
        if (season is null) { status("Pick a Chapter 1 season first."); return false; }
        if (!HasBuildFolder(season)) status("Select your " + season.Name + " build folder (Fortnite " + season.BuildVersion + ").");
        if (!await EnsureSeasonBuildAsync(season))
        {
            status(ProblemMessage.Length > 0 ? ProblemMessage : "No " + season.Name + " build folder was selected.");
            return false;
        }
        ActiveSeason = season;
        if (!string.Equals(_settings.LastBuildSeasonKey, season.Key, StringComparison.OrdinalIgnoreCase))
        {
            _settings.LastBuildSeasonKey = season.Key;
            Save();
        }
        if (State != BuildState.Found || Build is null) { status("The " + season.Name + " build isn't ready: " + ProblemMessage); return false; }
        return await PlayAsync(status, studio: true);
    }

    private async Task<bool> PlayAsync(Action<string>? studioStatus = null, bool studio = false)
    {
        StatusMessage = "";
        if (Build is null)
        {
            ServerStatusText = "No build selected";
            ServerStatusState = ServerStatusKind.Error;
            return false;
        }
        // The folder and the season must agree; pressing Play on Home should run the season
        // that belongs to the detected build instead of a stale selection.
        if (ActiveSeason is null || !ActiveSeason.AcceptsBuild(Build.Version))
        {
            var match = FindSeasonForBuild(Build.Version);
            if (match is not null)
            {
                ActiveSeason = match;
                _settings.LastBuildSeasonKey = match.Key;
                Save();
            }
        }
        if (SelectedProfile is null)
        {
            ProfileError = "Create a profile to play.";
            // Say it plainly instead of only opening Settings: a new player otherwise sees nothing happen.
            if (!studio)
                System.Windows.MessageBox.Show(System.Windows.Application.Current?.MainWindow!,
                    "You need an account to play.\n\nCreate one first: in Settings (opening now), type a name under New player, choose Progression or Unlock All, and press CREATE PLAYER. Then press Play again.",
                    "Create an account first", MessageBoxButton.OK, MessageBoxImage.Warning);
            IsSettingsOpen = true;
            ServerStatusText = "No profile selected";
            ServerStatusState = ServerStatusKind.Error;
            studioStatus?.Invoke("Create a profile in the launcher first (Settings).");
            return false;
        }
        SetServerStatus("Starting the Syndicate server...", ServerStatusKind.Starting);
        try
        {
            var botNames = RosterForMatch();
            var matchOptions=MatchOptions.Parse(_settings.BotCountRange,_settings.EditOnRelease) with {Difficulty=_currentPreset.Copy(), HighTickFullLobbies=_settings.HighTickFullLobbies, LateGame=_settings.LateGame && IsLateGameBuild(Build), FastQueue=_settings.FastQueue, BotNames=botNames, CustomBotNames=SavedCustomBotNames.ToList(), Playlist=SyndicateLauncher.Studio.StudioSession.ReadMode()};
            // Late game is always 60 players: 59 bots and you, whatever the bot count setting says.
            if(matchOptions.LateGame)matchOptions=matchOptions with {MinimumBots=59, MaximumBots=59};
            _launching = true;
            GameLauncher.SetStarting(true); // stops every launcher animation right away
            CommandManager.InvalidateRequerySuggested();
            await GameLauncher.LaunchAsync(Build, SelectedProfile, ActiveSeason?.SeasonNumber ?? 10, _settings.LaunchArguments, message =>
            {
                StatusMessage = message;
                SetServerStatus(message);
                studioStatus?.Invoke(message);
                CommandManager.InvalidateRequerySuggested();
            }, matchOptions, EnableMenu && !studio, message => LogAdded?.Invoke(message)); // the menu never loads into the Studio
            ServerStatusState = ServerStatusKind.Ready;
            ProblemsResolved?.Invoke();
            return true;
        }
        catch (Exception ex)
        {
            var problem = SyndicateLauncher.Services.LaunchProblem.From(ex);
            if (ex is not SyndicateLauncher.Services.LaunchProblemException)
            {
                var (_, folder) = SyndicateLauncher.Services.LaunchProblem.SplitLogs(ex.Message);
                problem = problem.WithLogs(folder);
            }
            ProblemReported?.Invoke(problem);
            SyndicateLauncher.Services.ErrorReporter.Report(problem.Title, problem.ToString(), "Season " + (ActiveSeason?.Name ?? "?") + "  Build " + (Build?.Version ?? "?"), problem.LogFolder);
            // The status line under Play stays short; the steps are in Diagnostics (it opens by itself).
            _statusMessage = "";
            StatusMessage = "Couldn't start: " + problem.Title + " See Diagnostics below for how to fix it.";
            ServerStatusText = "Couldn't start: " + problem.Title;
            ServerStatusState = ServerStatusKind.Error;
            studioStatus?.Invoke("Couldn't start: " + problem.Title);
            return false;
        }
        finally { _launching = false; GameLauncher.SetStarting(false); CommandManager.InvalidateRequerySuggested(); }
    }

    /// <summary>Keeps the Home status card's text and traffic-light state in sync with launch/background match steps.</summary>
    private void SetServerStatus(string message, ServerStatusKind? state = null)
    {
        ServerStatusText = message;
        if (state is { } explicitState)
        {
            ServerStatusState = explicitState;
            return;
        }
        if (message.StartsWith("Preparing your next match", StringComparison.OrdinalIgnoreCase) ||
            message.Contains("Retrying automatically", StringComparison.OrdinalIgnoreCase))
            ServerStatusState = ServerStatusKind.Starting;
        else if (message.StartsWith("Ready.", StringComparison.OrdinalIgnoreCase) ||
                 message.StartsWith("Your next match is ready", StringComparison.OrdinalIgnoreCase))
            ServerStatusState = ServerStatusKind.Ready;
        else if (message.Contains("could not", StringComparison.OrdinalIgnoreCase) ||
                 message.Contains("couldn't", StringComparison.OrdinalIgnoreCase) ||
                 message.Contains("failed", StringComparison.OrdinalIgnoreCase))
            ServerStatusState = ServerStatusKind.Error;
    }
    // ---------------------------------------------------------------- settings drawer
    public bool IsSettingsOpen
    {
        get => _isSettingsOpen;
        set
        {
            // Closing settings throws away an unsaved theme edit and bot-name draft.
            if (!value && Themes.IsEditorOpen) Themes.CancelEditorCommand.Execute(null);
            if (!value) CancelBotNameEdits();
            Set(ref _isSettingsOpen, value);
        }
    }
    public bool EnableMenu
    {
        get => _enableMenu;
        set { if (!Set(ref _enableMenu, value)) return; _settings.EnableMenu = value; Save(); OnPropertyChanged(nameof(CrosshairSummary)); }
    }
    // Bot difficulty -----------------------------------------------------
    public int DifficultyIndex
    {
        get=>_difficultyIndex;
        set {int level=Math.Clamp(value,0,5);ApplyPreset(BotPreset.Builtin(level));}
    }
    public string DifficultyName => DifficultyLevel >= 0 && DifficultyLevel < BotPreset.Levels.Length
        ? BotPreset.Levels[DifficultyLevel]
        : _currentPreset.Name;
    private int DifficultyLevel
    {
        get
        {
            if (_currentPreset.BuiltinIndex >= 0) return _currentPreset.BuiltinIndex;
            if (_selectedPreset?.BuiltinIndex >= 0) return _selectedPreset!.BuiltinIndex;
            if (_currentPreset.Name == "Trash") return 0;
            if (_currentPreset.Name == "Mixed") return 5;
            return Array.IndexOf(BotPreset.Levels, _currentPreset.Name);
        }
    }
    public string DifficultyDescription => DifficultyLevel switch
    {
        0 => "A relaxed match. Bots rarely shoot or build.",
        1 => "Forgiving aim with a little defensive building.",
        2 => "Balanced aim, movement and building.",
        3 => "Accurate shots and aggressive building.",
        5 => "A realistic lobby: 15% beginner, 30% average, 30% above-average, 20% advanced, 5% pro.",
        _ => "Higher sliders make that aspect more capable or more frequent. Settings apply to the next launched server."
    };
    public string BotChipText=>"AI: Syndicate";
    // Profiles -----------------------------------------------------------
    public ObservableCollection<Profile> Profiles { get; } = new();
    public bool HasProfiles => Profiles.Count > 0;
    public bool HasProfile => SelectedProfile is not null;
    public Profile? SelectedProfile
    {
        get => _selectedProfile;
        set
        {
            if (!Set(ref _selectedProfile, value)) return;
            _settings.ActiveProfile = value?.Name ?? "";
            Save();
            OnPropertyChanged(nameof(HasProfile));
            OnPropertyChanged(nameof(PlayerChipText));
            OnPropertyChanged(nameof(PlayerName));
            OnPropertyChanged(nameof(PlayerInitial));
            OnPropertyChanged(nameof(PlayerStatus));
            OnPropertyChanged(nameof(ProfileStateLabel));
            OnPropertyChanged(nameof(SelectedProfileModeLabel));
            OnPropertyChanged(nameof(SelectedProfileModeDescription));
            OnPropertyChanged(nameof(SwitchModeButtonText));
            OnPropertyChanged(nameof(ShowStwSkinsOption));
            OnPropertyChanged(nameof(SelectedStwSkinsOption));
            OnPropertyChanged(nameof(PlayerAvatarImage));
            OnPropertyChanged(nameof(PlayerInitialVisibility));
            AvatarError = "";
            SyncSelectedAvatar();
            _ = Home.RefreshAsync();
        }
    }
    public bool IsPlayerSelectOpen
    {
        get => _isPlayerSelectOpen;
        set => Set(ref _isPlayerSelectOpen, value);
    }
    public ObservableCollection<AvatarChoice> AvatarChoices { get; } = new();
    public AvatarChoice? SelectedAvatar
    {
        get => _selectedAvatar;
        set
        {
            if (!Set(ref _selectedAvatar, value) || value is null) return;
            if (SelectedProfile is null) return;
            string previousAvatar = SelectedProfile.Avatar;
            SelectedProfile.Avatar = value.Id;
            ReleaseAvatar(previousAvatar);
            _settings.ActiveProfile = SelectedProfile.Name;
            Save();
            OnPropertyChanged(nameof(PlayerAvatarImage));
            OnPropertyChanged(nameof(PlayerInitialVisibility));
        }
    }
    public ImageSource? PlayerAvatarImage => ProfileAvatars.Load(SelectedProfile?.Avatar);
    public Visibility PlayerInitialVisibility => PlayerAvatarImage is null ? Visibility.Visible : Visibility.Collapsed;
    private void SyncSelectedAvatar()
    {
        _selectedAvatar = AvatarChoices.FirstOrDefault(a =>
            string.Equals(a.Id, SelectedProfile?.Avatar, StringComparison.OrdinalIgnoreCase));
        OnPropertyChanged(nameof(SelectedAvatar));
    }
    private void ClearSelectedAvatar()
    {
        _selectedAvatar = null;
        OnPropertyChanged(nameof(SelectedAvatar));
    }

    private void ChooseCustomAvatar()
    {
        if (SelectedProfile is null)
        {
            AvatarError = "Choose or create a player first.";
            return;
        }

        var dialog = new OpenFileDialog
        {
            Title = "Choose a profile picture",
            Filter = "Image files (*.png;*.jpg;*.jpeg;*.bmp;*.gif)|*.png;*.jpg;*.jpeg;*.bmp;*.gif|All files (*.*)|*.*",
            CheckFileExists = true
        };
        if (dialog.ShowDialog() != true) return;

        string? id = ProfileAvatars.SaveCustom(dialog.FileName);
        if (id is null)
        {
            AvatarError = "That file could not be used. Try a PNG, JPG, BMP or GIF.";
            return;
        }

        string previousAvatar = SelectedProfile.Avatar;
        AvatarError = "";
        SelectedProfile.Avatar = id;
        ReleaseAvatar(previousAvatar);
        _settings.ActiveProfile = SelectedProfile.Name;
        Save();
        ClearSelectedAvatar();
        OnPropertyChanged(nameof(PlayerAvatarImage));
        OnPropertyChanged(nameof(PlayerInitialVisibility));
    }

    private void ReleaseAvatar(string? avatar)
    {
        if (!ProfileAvatars.IsCustom(avatar)) return;
        if (Profiles.Any(p => string.Equals(p.Avatar, avatar, StringComparison.OrdinalIgnoreCase))) return;
        ProfileAvatars.DeleteCustom(avatar);
    }

    public string AvatarError
    {
        get => _avatarError;
        private set
        {
            if (Set(ref _avatarError, value)) OnPropertyChanged(nameof(HasAvatarError));
        }
    }
    public bool HasAvatarError => _avatarError.Length > 0;

    public string PlayerChipText => SelectedProfile is null ? "No profile yet" : $"Playing as {SelectedProfile.Name}";
    public string PlayerName => SelectedProfile?.Name ?? "No profile";
    public string PlayerInitial => string.IsNullOrEmpty(SelectedProfile?.Name)
        ? "?"
        : char.ToUpperInvariant(SelectedProfile!.Name[0]).ToString();
    public string PlayerStatus => SelectedProfile is null ? "Create a profile to play" : $"Local profile · {SelectedProfile.ModeLabel}";
    public string ProfileStateLabel => SelectedProfile is null ? "CREATE PROFILE" : "PROFILE READY";
    public string SelectedProfileModeLabel => SelectedProfile?.ModeLabel ?? "";
    public string SelectedProfileModeDescription => SelectedProfile?.Mode == ProfileMode.Progression
        ? "Progression Mode is for users who want to feel that Nostalgic grinding feeling again. You start at level 0, tier 0. You can complete challenges and work your way through the battle pass"
        : "Unlock All is for users who want to enjoy the classic game but be able to use all cosmetics";
    public bool NewProfileIsProgression
    {
        get => _newProfileIsProgression;
        set
        {
            if (!Set(ref _newProfileIsProgression, value)) return;
            OnPropertyChanged(nameof(NewProfileIsUnlockedAll));
            OnPropertyChanged(nameof(NewProfileModeDescription));
        }
    }
    public bool NewProfileIsUnlockedAll
    {
        get => !_newProfileIsProgression;
        set { if (value) NewProfileIsProgression = false; }
    }
    public string NewProfileModeDescription => NewProfileIsProgression
        ? "Progression Mode is for users who want to feel that Nostalgic grinding feeling again. You start at level 0, tier 0. You can complete challenges and work your way through the battle pass"
        : "Unlock All is for users who want to enjoy the classic game but be able to use all cosmetics";
    public string DifficultyMeta => $"AI: Syndicate · {DifficultyName}";
    public string NewProfileName
    {
        get => _newProfileName;
        set
        {
            if (!Set(ref _newProfileName, value)) return;
            if (ProfileError.Length > 0) ProfileError = "";
        }
    }
    public string ProfileError
    {
        get => _profileError;
        private set
        {
            if (Set(ref _profileError, value)) OnPropertyChanged(nameof(HasProfileError));
        }
    }
    public bool HasProfileError => _profileError.Length > 0;
    private void CreateProfile()
    {
        var name = (NewProfileName ?? "").Trim();
        if (!ProfileNameRule.IsMatch(name))
        {
            ProfileError = "Use 3 to 16 letters, numbers, spaces, - or _.";
            return;
        }
        if (Profiles.Any(p => p.Name.Equals(name, StringComparison.OrdinalIgnoreCase)))
        {
            ProfileError = "You already have a profile with that name.";
            return;
        }
        var profile = new Profile { Name = name, Mode = NewProfileIsProgression ? ProfileMode.Progression : ProfileMode.UnlockedAll };
        profile.Avatar = ProfileAvatars.Ids[Random.Shared.Next(ProfileAvatars.Ids.Count)];
        _settings.Profiles.Add(profile);
        Profiles.Add(profile);
        NewProfileName = "";
        ProfileError = "";
        SelectedProfile = profile; // also saves
    }
    private void DeleteProfile(Profile? profile)
    {
        if (profile is null) return;
        bool wasSelected = ReferenceEquals(profile, SelectedProfile);
        string avatar = profile.Avatar;
        _settings.Profiles.Remove(profile);
        Profiles.Remove(profile);
        ReleaseAvatar(avatar);
        if (wasSelected) SelectedProfile = Profiles.FirstOrDefault();
        else Save();
    }
    private void OnProfilesChanged(object? sender, NotifyCollectionChangedEventArgs e)
        => OnPropertyChanged(nameof(HasProfiles));
    public string BotCountRange
    {
        get=>_settings.BotCountRange;
        set {value??="";if(value==_settings.BotCountRange)return;_settings.BotCountRange=value;Save();OnPropertyChanged();OnPropertyChanged(nameof(BotCountError));}
    }
    public string BotCountError
    {
        get {try{MatchOptions.Parse(_settings.BotCountRange,_settings.EditOnRelease);return "";}catch(InvalidOperationException e){return e.Message;}}
    }
    public ObservableCollection<BotNameSlot> BotNames { get; } = new();
    public string BotNamesSummary => _settings.BotNameOverrides.Count switch
    {
        0 => "Using the built-in names.",
        1 => "1 name customized.",
        var count => $"{count} names customized."
    };
    private void InitializeBotNames()
    {
        var used = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
        for (int index = 0; index < BotNameCatalog.DefaultNames.Count; index++)
        {
            string fallback = BotNameCatalog.DefaultNames[index];
            string name = fallback;
            if (_settings.BotNameOverrides.TryGetValue(index, out var custom))
            {
                custom = (custom ?? "").Trim();
                if (MatchOptions.ValidateBotName(custom).Length == 0 && used.Add(custom))
                    name = custom;
                else
                    used.Add(fallback);
            }
            else
            {
                used.Add(fallback);
            }
            BotNames.Add(new BotNameSlot(index, fallback, name, SaveBotName));
        }
    }
    private void SaveBotName(BotNameSlot slot)
    {
        string name = slot.DraftName.Trim();
        string error = MatchOptions.ValidateBotName(name);
        if (error.Length == 0 && BotNames.Any(other => !ReferenceEquals(other, slot) &&
                string.Equals(other.Name, name, StringComparison.OrdinalIgnoreCase)))
            error = "Another bot already uses that name.";
        if (error.Length > 0)
        {
            slot.SetError(error);
            return;
        }
        if (string.Equals(name, slot.DefaultName, StringComparison.Ordinal))
            _settings.BotNameOverrides.Remove(slot.Index);
        else
            _settings.BotNameOverrides[slot.Index] = name;
        slot.SetName(name);
        slot.FinishEdit();
        Save();
        OnPropertyChanged(nameof(BotNamesSummary));
    }
    private void CancelBotNameEdits()
    {
        foreach (var slot in BotNames) slot.CancelEdit();
    }
    public bool EditOnRelease
    {
        get=>_settings.EditOnRelease;
        set {if(value==_settings.EditOnRelease)return;_settings.EditOnRelease=value;ClientMods.EditOnRelease=value;Save();OnPropertyChanged();}
    }
    public bool HighTickFullLobbies
    {
        get => _settings.HighTickFullLobbies;
        set { if (value == _settings.HighTickFullLobbies) return; _settings.HighTickFullLobbies = value; Save(); OnPropertyChanged(); }
    }

    /// <summary>Late game runs on Pre-season (1.7.2), Season 2 (1.11, 2.5) and Season X (10.40). Season 1 (1.8, 1.8.2) was taken out on 2026-11-17; it and every other season play a normal match with it on.</summary>
    private static bool IsLateGameBuild(BuildInfo? build) => build?.Version is "1.7.2" or "1.11" or "2.5" or "10.40";
    public bool LateGame
    {
        get => _settings.LateGame;
        set { if (value == _settings.LateGame) return; _settings.LateGame = value; Save(); OnPropertyChanged(); }
    }
    public bool FastQueue
    {
        get => _settings.FastQueue;
        set { if (value == _settings.FastQueue) return; _settings.FastQueue = value; Save(); OnPropertyChanged(); }
    }
    public bool AutoAddExclusions
    {
        get => _settings.AutoAddExclusions;
        set { if (value == _settings.AutoAddExclusions) return; _settings.AutoAddExclusions = value; Save(); OnPropertyChanged(); }
    }
    /// <summary>Status text shown below the ADD TO EXCLUSIONS button in Settings &gt; Launcher &amp; files.</summary>
    public string ExclusionStatus
    {
        get => _exclusionStatus;
        private set
        {
            if (!Set(ref _exclusionStatus, value)) return;
            OnPropertyChanged(nameof(ExclusionStatusVisibility));
        }
    }
    public System.Windows.Visibility ExclusionStatusVisibility =>
        string.IsNullOrEmpty(_exclusionStatus)
            ? System.Windows.Visibility.Collapsed
            : System.Windows.Visibility.Visible;
    public bool TurboBuild
    {
        get => _turboBuild;
        set { if (!Set(ref _turboBuild, value)) return; ClientMods.TurboBuild = value; }
    }
    public bool ResetOnRelease
    {
        get => _resetOnRelease;
        set { if (!Set(ref _resetOnRelease, value)) return; ClientMods.ResetOnRelease = value; }
    }
    public bool SprintByDefault
    {
        get => _sprintByDefault;
        set { if (!Set(ref _sprintByDefault, value)) return; InputMods.SprintByDefaultEnabled = value; }
    }
    public KeyChoice? SprintKeyChoice
    {
        get => _sprintKeyChoice;
        set
        {
            if (!Set(ref _sprintKeyChoice, value) || value is null) return;
            InputMods.SprintKey = value.Value;
            OnPropertyChanged();
        }
    }
    public bool AutoRun
    {
        get => _autoRun;
        set { if (!Set(ref _autoRun, value)) return; InputMods.AutoRunEnabled = value; }
    }
    public KeyChoice? AutoRunKeyChoice
    {
        get => _autoRunKeyChoice;
        set
        {
            if (!Set(ref _autoRunKeyChoice, value) || value is null) return;
            InputMods.AutoRunKey = value.Value;
            OnPropertyChanged();
        }
    }
    public bool ClassicArPump
    {
        get => _classicArPump;
        set { if (!Set(ref _classicArPump, value)) return; ClientMods.ClassicArPump = value; }
    }
    public bool BotEmotesDisabled
    {
        get => _botEmotesDisabled;
        set { if (!Set(ref _botEmotesDisabled, value)) return; ClientMods.BotEmotesDisabled = value; }
    }
    public bool EditSelectRelease
    {
        get => _editSelectRelease;
        set { if (!Set(ref _editSelectRelease, value)) return; ClientMods.EditSelectRelease = value; }
    }
    public bool EditReleaseNeedsTile
    {
        get => _editReleaseNeedsTile;
        set { if (!Set(ref _editReleaseNeedsTile, value)) return; ClientMods.EditReleaseNeedsTile = value; }
    }
    public string EditKey
    {
        get => _editKey;
        set
        {
            value ??= "";
            if (value == _editKey) return;
            _editKey = value; OnPropertyChanged();
            ClientMods.EditKey = value;
        }
    }
    public IReadOnlyList<KeyChoice> EditKeys => EditKeyChoices.All;
    public KeyChoice? SelectedEditKey
    {
        get => _selectedEditKey;
        set
        {
            if (!Set(ref _selectedEditKey, value) || value is null) return;
            _editKey = value.Value;
            OnPropertyChanged(nameof(EditKey));
            ClientMods.EditKey = value.Value;
        }
    }
    public IReadOnlyList<string> PadEditButtons => ClientMods.PadButtons;
    public string PadEditButton
    {
        get => _padEditButton;
        set { if (string.IsNullOrEmpty(value) || value == _padEditButton) return; _padEditButton = value; OnPropertyChanged(); ClientMods.PadEditButton = value; }
    }
    public float Fov
    {
        get => _fov;
        set
        {
            value = Math.Clamp(MathF.Round(value), 0, 130);
            if (!Set(ref _fov, value)) return;
            ClientMods.Fov = value;
            OnPropertyChanged(nameof(FovText));
        }
    }
    public string FovText => _fov <= 0 ? "Off" : _fov.ToString("0");
    // Launch arguments ---------------------------------------------------
    public string LaunchArguments
    {
        get => _settings.LaunchArguments;
        set
        {
            value ??= "";
            if (value == _settings.LaunchArguments) return;
            _settings.LaunchArguments = value;
            Save();
            OnPropertyChanged();
        }
    }
    private void Save() => _store.Save(_settings);
    // ---------------------------------------------------------------- AV exclusions
    private async System.Threading.Tasks.Task AddExclusionsAsync()
    {
        if (_isAddingExclusions) return;
        _isAddingExclusions = true;
        ExclusionStatus = "Adding exclusions\u2026";
        System.Windows.Input.CommandManager.InvalidateRequerySuggested();

        string? gameDir = string.IsNullOrWhiteSpace(_settings.GameDirectory) ? null : _settings.GameDirectory;
        var result = await DefenderExclusions.AddAsync(gameDir);

        ExclusionStatus = result switch
        {
            ExclusionResult.Added        => "\u2713 Exclusions added. If files were already quarantined, restore them from Windows Security.",
            ExclusionResult.Cancelled    => "Cancelled \u2014 no changes were made.",
            ExclusionResult.NotSupported => "Windows Defender is not running. No changes were made.",
            ExclusionResult.Failed       => "Could not add exclusions. Try running the launcher as administrator.",
            _                            => ""
        };

        _isAddingExclusions = false;
        System.Windows.Input.CommandManager.InvalidateRequerySuggested();
    }
}
