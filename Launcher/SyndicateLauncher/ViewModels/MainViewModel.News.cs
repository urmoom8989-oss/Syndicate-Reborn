using System;
using System.Collections.Generic;
using System.Collections.ObjectModel;
using System.Globalization;
using System.IO;
using System.Linq;
using System.Windows.Input;
using System.Windows.Media;
using System.Windows.Media.Imaging;
using SyndicateLauncher.Models;
using SyndicateLauncher.Services;

namespace SyndicateLauncher.ViewModels;

/// <summary>A season choice in the news editor: every season, or one season's builds.</summary>
public sealed class NewsSeasonOption
{
    public NewsSeasonOption(string label, IEnumerable<string> builds)
    {
        Label = label;
        Builds = builds.ToList();
    }

    public string Label { get; }
    public List<string> Builds { get; }
    public override string ToString() => Label;
}

/// <summary>A mode a tournament can be played in.</summary>
public sealed record TournamentModeOption(string Id, string Name);

/// <summary>A colour a tournament's poster can use.</summary>
public sealed record TournamentColorOption(string Name, string Hex)
{
    public Brush Swatch { get; } = new SolidColorBrush((Color)ColorConverter.ConvertFromString("#" + Hex));
}

/// <summary>One news post or tournament in the editor (Views/NewsEditorView.xaml).</summary>
public sealed class NewsEntryViewModel : ViewModelBase
{
    private const string TimeFormat = "yyyy-MM-dd HH:mm";
    private string _title = "", _subtitle = "", _body = "", _schedule = "", _mode = "solo", _image = "", _color = "F81B2D";
    private string _beginText = "", _endText = "";
    private bool _postInNews = true;
    private List<string> _builds = new();
    private ImageSource? _picture;

    public NewsEntryViewModel(bool tournament, string id)
    {
        IsTournament = tournament;
        Id = id;
        var start = DateTime.Now.Date.AddHours(DateTime.Now.Hour + 1);
        _beginText = start.ToString(TimeFormat, CultureInfo.InvariantCulture);
        _endText = start.AddHours(3).ToString(TimeFormat, CultureInfo.InvariantCulture);
    }

    public bool IsTournament { get; }
    public bool IsNews => !IsTournament;
    public string Id { get; }
    public string Kind => IsTournament ? "TOURNAMENT" : "NEWS";
    public string Heading => string.IsNullOrWhiteSpace(_title) ? (IsTournament ? "New tournament" : "New post") : _title;

    public string Title
    {
        get => _title;
        set { if (Set(ref _title, value ?? "")) OnPropertyChanged(nameof(Heading)); }
    }
    public string Subtitle { get => _subtitle; set => Set(ref _subtitle, value ?? ""); }
    /// <summary>The post's text, or the tournament's description.</summary>
    public string Body { get => _body; set => Set(ref _body, value ?? ""); }
    public string Schedule { get => _schedule; set => Set(ref _schedule, value ?? ""); }
    public string Mode { get => _mode; set => Set(ref _mode, string.IsNullOrEmpty(value) ? "solo" : value); }
    public bool PostInNews { get => _postInNews; set => Set(ref _postInNews, value); }
    public string Color { get => _color; set => Set(ref _color, value ?? "F81B2D"); }
    public List<string> Builds { get => _builds; set => Set(ref _builds, value ?? new()); }

    public string BeginText
    {
        get => _beginText;
        set { if (Set(ref _beginText, value ?? "")) OnPropertyChanged(nameof(TimeNote)); }
    }
    public string EndText
    {
        get => _endText;
        set { if (Set(ref _endText, value ?? "")) OnPropertyChanged(nameof(TimeNote)); }
    }

    public static bool TryTime(string text, out DateTime time) =>
        DateTime.TryParseExact(text.Trim(), TimeFormat, CultureInfo.InvariantCulture, DateTimeStyles.AssumeLocal, out time)
        || DateTime.TryParse(text.Trim(), CultureInfo.CurrentCulture, DateTimeStyles.AssumeLocal, out time);

    public string TimeNote
    {
        get
        {
            if (!TryTime(_beginText, out var begin)) return "Start time: write it like " + DateTime.Now.ToString(TimeFormat, CultureInfo.InvariantCulture);
            if (!TryTime(_endText, out var end)) return "End time: write it like " + DateTime.Now.AddHours(3).ToString(TimeFormat, CultureInfo.InvariantCulture);
            if (end <= begin) return "The end has to be after the start.";
            if (end < DateTime.Now) return "This tournament is over, so the Compete tab no longer shows it.";
            return begin > DateTime.Now ? "Opens " + begin.ToString("ddd d MMM, HH:mm", CultureInfo.CurrentCulture) + " (your time)." : "Open now.";
        }
    }

    /// <summary>File name inside the NewsImages folder, or empty.</summary>
    public string Image
    {
        get => _image;
        set
        {
            if (!Set(ref _image, value ?? "")) return;
            Picture = LoadPicture(_image);
            OnPropertyChanged(nameof(HasPicture));
        }
    }
    public bool HasPicture => !string.IsNullOrEmpty(_image);
    public ImageSource? Picture { get => _picture; private set => Set(ref _picture, value); }

    private static ImageSource? LoadPicture(string name)
    {
        if (string.IsNullOrEmpty(name)) return null;
        try
        {
            string file = Path.Combine(NewsSettings.ImageFolder, name);
            if (!File.Exists(file)) return null;
            var image = new BitmapImage();
            image.BeginInit();
            image.CacheOption = BitmapCacheOption.OnLoad;
            image.DecodePixelWidth = 480;
            image.UriSource = new Uri(file);
            image.EndInit();
            image.Freeze();
            return image;
        }
        catch { return null; }
    }

    public static NewsEntryViewModel From(NewsPost post) => new(false, post.Id)
    {
        Title = post.Title, Body = post.Body, Image = post.Image, Builds = post.Builds?.ToList() ?? new(),
    };

    public static NewsEntryViewModel From(CustomTournament t) => new(true, t.Id)
    {
        Title = t.Title, Subtitle = t.Subtitle, Body = t.Description, Schedule = t.Schedule, Mode = t.Mode, Image = t.Image,
        Color = t.Color, PostInNews = t.PostInNews, Builds = t.Builds?.ToList() ?? new(),
        BeginText = t.Begin.ToString(TimeFormat, CultureInfo.InvariantCulture), EndText = t.End.ToString(TimeFormat, CultureInfo.InvariantCulture),
    };

    public NewsPost ToPost() => new() { Id = Id, Title = Title.Trim(), Body = Body.Trim(), Image = Image, Builds = Builds.ToList() };

    public CustomTournament ToTournament()
    {
        var begin = TryTime(_beginText, out var b) ? b : DateTime.Now;
        var end = TryTime(_endText, out var e) && e > begin ? e : begin.AddHours(3);
        return new CustomTournament
        {
            Id = Id, Title = Title.Trim(), Subtitle = Subtitle.Trim(), Description = Body.Trim(), Schedule = Schedule.Trim(), Mode = Mode,
            Begin = begin, End = end, Image = Image, Color = Color, PostInNews = PostInNews, Builds = Builds.ToList(),
        };
    }
}

/// <summary>
/// Settings &gt; News &amp; tournaments: posts for the game's News tab and tournaments for the Compete tab (8.50 to
/// 12.50; with "Also post in News" they show in every season's News tab too). Saved to settings.json and to the
/// backend's news-settings.json (Services/NewsSettings.cs, backend Utils/syndicateNewsSettings.js).
/// </summary>
public sealed partial class MainViewModel
{
    private bool _isNewsEditorOpen;
    private NewsEntryViewModel? _selectedNewsEntry;
    private string _newsNote = "";
    private ICommand? _openNewsEditorCommand, _closeNewsEditorCommand, _addNewsPostCommand, _addTournamentCommand, _deleteNewsEntryCommand,
        _chooseNewsImageCommand, _clearNewsImageCommand, _saveNewsCommand, _newsColorCommand, _selectNewsEntryCommand;

    public ObservableCollection<NewsEntryViewModel> NewsPosts { get; } = new();
    public ObservableCollection<NewsEntryViewModel> NewsTournaments { get; } = new();
    public ObservableCollection<NewsSeasonOption> NewsSeasonOptions { get; } = new();
    public IReadOnlyList<TournamentModeOption> TournamentModes { get; } = SeasonModes.All
        .Where(m => m.Id is not ("creative" or "stw" or "playground"))
        .Select(m => new TournamentModeOption(m.Id, m.Name)).ToList();
    public IReadOnlyList<TournamentColorOption> TournamentColors { get; } = new[]
    {
        new TournamentColorOption("Red", "F81B2D"), new TournamentColorOption("Blue", "1E6FE8"), new TournamentColorOption("Purple", "8B2FE0"),
        new TournamentColorOption("Gold", "E8A317"), new TournamentColorOption("Green", "21A85A"), new TournamentColorOption("Pink", "E83E8C"),
        new TournamentColorOption("Teal", "12A3A3"), new TournamentColorOption("Black", "1B1B1B"),
    };

    public bool IsNewsEditorOpen { get => _isNewsEditorOpen; private set => Set(ref _isNewsEditorOpen, value); }
    public string NewsNote { get => _newsNote; private set => Set(ref _newsNote, value); }
    public string NewsSummary =>
        NewsPosts.Count + (NewsPosts.Count == 1 ? " news post, " : " news posts, ") +
        NewsTournaments.Count + (NewsTournaments.Count == 1 ? " tournament." : " tournaments.");

    public NewsEntryViewModel? SelectedNewsEntry
    {
        get => _selectedNewsEntry;
        set
        {
            if (!Set(ref _selectedNewsEntry, value)) return;
            OnPropertyChanged(nameof(HasSelectedNewsEntry));
            OnPropertyChanged(nameof(SelectedNewsSeason));
        }
    }
    public bool HasSelectedNewsEntry => _selectedNewsEntry is not null;

    public NewsSeasonOption? SelectedNewsSeason
    {
        get
        {
            if (_selectedNewsEntry is null) return null;
            var builds = _selectedNewsEntry.Builds;
            return NewsSeasonOptions.FirstOrDefault(o => builds.Count == 0 ? o.Builds.Count == 0 : o.Builds.Count > 0 && o.Builds.Intersect(builds).Any())
                   ?? NewsSeasonOptions.FirstOrDefault();
        }
        set
        {
            if (_selectedNewsEntry is null || value is null) return;
            _selectedNewsEntry.Builds = value.Builds.ToList();
            OnPropertyChanged();
        }
    }

    public ICommand OpenNewsEditorCommand => _openNewsEditorCommand ??= new RelayCommand(() =>
    {
        SelectedNewsEntry ??= NewsPosts.FirstOrDefault() ?? NewsTournaments.FirstOrDefault();
        NewsNote = "";
        IsNewsEditorOpen = true;
    });
    public ICommand CloseNewsEditorCommand => _closeNewsEditorCommand ??= new RelayCommand(() =>
    {
        SaveNews(quiet: true);
        IsNewsEditorOpen = false;
    });
    public ICommand AddNewsPostCommand => _addNewsPostCommand ??= new RelayCommand(() =>
    {
        var entry = new NewsEntryViewModel(false, NewEntryId("News"));
        NewsPosts.Add(entry);
        SelectedNewsEntry = entry;
        OnPropertyChanged(nameof(NewsSummary));
    });
    public ICommand AddTournamentCommand => _addTournamentCommand ??= new RelayCommand(() =>
    {
        var entry = new NewsEntryViewModel(true, NewEntryId("Cup")) { Title = "Syndicate Cup", Schedule = "Today only" };
        NewsTournaments.Add(entry);
        SelectedNewsEntry = entry;
        OnPropertyChanged(nameof(NewsSummary));
    });
    public ICommand DeleteNewsEntryCommand => _deleteNewsEntryCommand ??= new RelayCommand(() =>
    {
        if (_selectedNewsEntry is not { } entry) return;
        var list = entry.IsTournament ? NewsTournaments : NewsPosts;
        int index = list.IndexOf(entry);
        list.Remove(entry);
        SelectedNewsEntry = list.Count > 0 ? list[Math.Clamp(index, 0, list.Count - 1)] : NewsPosts.FirstOrDefault() ?? NewsTournaments.FirstOrDefault();
        SaveNews(quiet: false, note: "Deleted.");
    }, () => _selectedNewsEntry is not null);
    public ICommand ChooseNewsImageCommand => _chooseNewsImageCommand ??= new RelayCommand(ChooseNewsImage, () => _selectedNewsEntry is not null);
    public ICommand ClearNewsImageCommand => _clearNewsImageCommand ??= new RelayCommand(() =>
    {
        if (_selectedNewsEntry is { } entry) entry.Image = "";
    }, () => _selectedNewsEntry is not null);
    public ICommand SelectNewsEntryCommand => _selectNewsEntryCommand ??= new RelayCommand(parameter =>
    {
        if (parameter is NewsEntryViewModel entry) SelectedNewsEntry = entry;
    });
    public ICommand SaveNewsCommand => _saveNewsCommand ??= new RelayCommand(() => SaveNews(quiet: false));
    public ICommand NewsColorCommand => _newsColorCommand ??= new RelayCommand(parameter =>
    {
        if (_selectedNewsEntry is { } entry && parameter is TournamentColorOption color) entry.Color = color.Hex;
    });

    private static string NewEntryId(string prefix) => prefix + DateTime.Now.ToString("yyMMddHHmmss", CultureInfo.InvariantCulture);

    private void InitializeNews()
    {
        _settings.News ??= new();
        _settings.Tournaments ??= new();
        NewsSeasonOptions.Clear();
        NewsSeasonOptions.Add(new NewsSeasonOption("Every season", Array.Empty<string>()));
        foreach (var season in GameModeSeasons) NewsSeasonOptions.Add(new NewsSeasonOption(season.ToString(), season.Builds));
        NewsPosts.Clear();
        foreach (var post in _settings.News.Where(p => p is not null && !string.IsNullOrEmpty(p.Id))) NewsPosts.Add(NewsEntryViewModel.From(post));
        NewsTournaments.Clear();
        foreach (var cup in _settings.Tournaments.Where(t => t is not null && !string.IsNullOrEmpty(t.Id))) NewsTournaments.Add(NewsEntryViewModel.From(cup));
        NewsSettings.Load(_settings.News, _settings.Tournaments);
        OnPropertyChanged(nameof(NewsSummary));
    }

    private void ChooseNewsImage()
    {
        if (_selectedNewsEntry is not { } entry) return;
        var dialog = new Microsoft.Win32.OpenFileDialog
        {
            Title = "Picture for " + entry.Heading,
            Filter = "Pictures|*.png;*.jpg;*.jpeg;*.bmp;*.gif",
        };
        if (dialog.ShowDialog() != true) return;
        try
        {
            Directory.CreateDirectory(NewsSettings.ImageFolder);
            string name = entry.Id + "_" + DateTime.Now.Ticks + Path.GetExtension(dialog.FileName).ToLowerInvariant();
            File.Copy(dialog.FileName, Path.Combine(NewsSettings.ImageFolder, name), true);
            entry.Image = name;
            NewsNote = "Picture added. Press Save to put it in the game.";
        }
        catch (Exception ex)
        {
            NewsNote = "Couldn't copy that picture: " + ex.Message;
        }
    }

    private void SaveNews(bool quiet, string? note = null)
    {
        _settings.News = NewsPosts.Select(p => p.ToPost()).Where(p => p.Title.Length + p.Body.Length > 0).ToList();
        _settings.Tournaments = NewsTournaments.Select(t => t.ToTournament()).ToList();
        Save();
        NewsSettings.Load(_settings.News, _settings.Tournaments);
        NewsSettings.WriteForBackend();
        OnPropertyChanged(nameof(NewsSummary));
        if (!quiet || note is not null)
            NewsNote = note ?? "Saved. Fortnite shows it the next time it loads the News tab (restart the game if it is already open).";
    }
}
