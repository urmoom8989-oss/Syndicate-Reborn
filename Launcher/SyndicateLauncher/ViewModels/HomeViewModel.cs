using System;
using System.Collections.ObjectModel;
using System.IO;
using System.Linq;
using System.Text.Json;
using System.Threading.Tasks;
using System.Windows;
using System.Windows.Input;
using SyndicateLauncher.Models;
using SyndicateLauncher.Services;

namespace SyndicateLauncher.ViewModels;

/// <summary>
/// One Syndicate Achievement as the backend reports it (Utils/syndicateAchievements.js). Counted by the backend from
/// match events in any season; paid in real V-Bucks when claimed; matches with the Syndicate menu on don't count.
/// </summary>
public sealed class AchievementItem : ViewModelBase
{
    private bool _claiming;
    public string Id { get; init; } = "";
    public string Name { get; init; } = "";
    public string Description { get; init; } = "";
    public string Tier { get; init; } = "common";
    public string Badge { get; init; } = "star";
    public long Reward { get; init; }
    public double Progress { get; init; }
    public double Target { get; init; } = 1;
    public bool Unlocked { get; init; }
    public bool Claimed { get; init; }

    public bool Claimable => Unlocked && !Claimed;
    public bool Locked => !Unlocked;
    public double Fraction => Target <= 0 ? 1 : Math.Clamp(Progress / Target, 0, 1);
    public string ProgressText => Claimed ? "Claimed" : Unlocked ? "Unlocked" : Short(Math.Min(Progress, Target)) + " / " + Short(Target);
    public string RewardText => Reward.ToString("N0");
    public string TierText => Tier.ToUpperInvariant();
    public bool Claiming { get => _claiming; set => Set(ref _claiming, value); }

    private static string Short(double v) => v >= 10000 ? (v / 1000).ToString("0.#") + "K" : Math.Floor(v).ToString("N0");
}

/// <summary>Home tab: V-Bucks, Total Playtime, 7-Day Activity and Syndicate Achievements.</summary>
public sealed class HomeViewModel : ViewModelBase
{
    private readonly Func<string?> _profile;
    private readonly Func<Task> _ensureBackend;
    private string? _loadedFor;
    private bool _refreshing;
    private long? _vbucks;
    private string _totalPlaytime = "0m";
    private string _weekPlaytime = "0m";
    private int _weekGames;
    private int _totalGames;
    private int _unlockedCount;
    private int _achievementCount;
    private string _rewardsText = "";
    private bool _menuActive;
    private bool _backendOffline;
    private string _message = "";
    private bool _isAchievementsOpen;
    private string _kills = "0", _wins = "0", _matches = "0";
    private double _localMinutes, _matchMinutes;

    public HomeViewModel(Func<string?> profile, Func<Task> ensureBackend)
    {
        _profile = profile;
        _ensureBackend = ensureBackend;
        ClaimCommand = new RelayCommand(p => { if (p is AchievementItem a) _ = ClaimAsync(a); }, p => p is AchievementItem { Claimable: true, Claiming: false });
        OpenAchievementsCommand = new RelayCommand(() => IsAchievementsOpen = true);
        CloseAchievementsCommand = new RelayCommand(() => IsAchievementsOpen = false);
        RefreshCommand = new RelayCommand(() => _ = RefreshAsync(force: true));
        StatsService.Changed += name =>
        {
            if (string.Equals(name, _profile(), StringComparison.OrdinalIgnoreCase))
                Application.Current?.Dispatcher.BeginInvoke(() => _ = RefreshAsync(force: true));
        };
    }

    public ObservableCollection<ActivityDay> Activity { get; } = new();
    public ObservableCollection<AchievementItem> Achievements { get; } = new();
    /// <summary>The Home strip: claimable first, then the closest to unlocking.</summary>
    public ObservableCollection<AchievementItem> Spotlight { get; } = new();

    public ICommand ClaimCommand { get; }
    public ICommand OpenAchievementsCommand { get; }
    public ICommand CloseAchievementsCommand { get; }
    public ICommand RefreshCommand { get; }

    public bool HasVBucks => _vbucks is not null;
    public string VBucksText => _vbucks is long v ? v.ToString("N0") : "—";
    public string TotalPlaytime { get => _totalPlaytime; private set => Set(ref _totalPlaytime, value); }
    public string WeekPlaytime { get => _weekPlaytime; private set => Set(ref _weekPlaytime, value); }
    public int WeekGames { get => _weekGames; private set => Set(ref _weekGames, value); }
    public int TotalGames { get => _totalGames; private set => Set(ref _totalGames, value); }
    public int UnlockedCount { get => _unlockedCount; private set { if (Set(ref _unlockedCount, value)) OnPropertyChanged(nameof(AchievementsProgress)); } }
    public int AchievementCount { get => _achievementCount; private set { if (Set(ref _achievementCount, value)) OnPropertyChanged(nameof(AchievementsProgress)); } }
    public double AchievementsProgress => AchievementCount == 0 ? 0 : (double)UnlockedCount / AchievementCount;
    public string RewardsText { get => _rewardsText; private set => Set(ref _rewardsText, value); }
    public bool MenuActive { get => _menuActive; private set => Set(ref _menuActive, value); }
    public bool BackendOffline { get => _backendOffline; private set => Set(ref _backendOffline, value); }
    public string Message { get => _message; private set => Set(ref _message, value); }
    public string Kills { get => _kills; private set => Set(ref _kills, value); }
    public string Wins { get => _wins; private set => Set(ref _wins, value); }
    public string Matches { get => _matches; private set => Set(ref _matches, value); }
    public bool ShowAchievementsPlaceholder => Achievements.Count == 0;
    public bool IsAchievementsOpen { get => _isAchievementsOpen; set => Set(ref _isAchievementsOpen, value); }

    private static string CacheFile(string profile) => CacheFileFor(profile);

    /// <summary>The cached backend reply for a profile (also moved/removed by StatsService on rename/delete).</summary>
    internal static string CacheFileFor(string profile) =>
        Path.Combine(SettingsService.DataDirectory, "stats", string.Concat(profile.Select(c => char.IsLetterOrDigit(c) ? char.ToLowerInvariant(c) : '_')) + ".home.json");

    /// <summary>Called when Home is shown and when the profile changes. Cheap when nothing changed.</summary>
    public async Task RefreshAsync(bool force = false)
    {
        string? profile = _profile();
        if (profile is null) return;
        bool switched = !string.Equals(profile, _loadedFor, StringComparison.OrdinalIgnoreCase);
        if (!force && !switched && !BackendOffline && Achievements.Count > 0) { LoadLocal(profile); return; }
        _loadedFor = profile;
        LoadLocal(profile);
        if (switched) ApplyCached(profile);
        if (_refreshing) return;
        _refreshing = true;
        try
        {
            if (!await SyndicateBackend.IsAvailableAsync())
            {
                if (GameLauncher.IsRunning) { BackendOffline = true; return; }
                await _ensureBackend();
            }
            string json = await SyndicateBackend.PostAsPlayerAsync(profile, "launcher/achievements", new { });
            if (!string.Equals(profile, _profile(), StringComparison.OrdinalIgnoreCase)) return;
            Apply(json);
            BackendOffline = false;
            try { File.WriteAllText(CacheFile(profile), json); } catch (Exception) { }
        }
        catch (Exception)
        {
            BackendOffline = true;
        }
        finally { _refreshing = false; }
    }

    private void LoadLocal(string profile)
    {
        var stats = StatsService.Load(profile);
        _localMinutes = stats.TotalMinutes;
        ShowTotalPlaytime();
        TotalGames = stats.Sessions.Count;
        var days = stats.SevenDayActivity();
        double max = Math.Max(1, days.Max(d => d.Minutes));
        WeekGames = days.Sum(d => d.Games);
        WeekPlaytime = PlaytimeFormat.Duration(days.Sum(d => d.Minutes));
        Activity.Clear();
        foreach (var d in days) Activity.Add(d with { Height = d.Minutes <= 0 ? 0 : Math.Max(0.06, d.Minutes / max) });
    }

    private void ShowTotalPlaytime() => TotalPlaytime = PlaytimeFormat.Duration(Math.Max(_localMinutes, _matchMinutes));

    private void ApplyCached(string profile)
    {
        Achievements.Clear(); Spotlight.Clear(); _vbucks = null; _matchMinutes = 0;
        OnPropertyChanged(nameof(ShowAchievementsPlaceholder));
        OnPropertyChanged(nameof(VBucksText)); OnPropertyChanged(nameof(HasVBucks));
        try { if (File.Exists(CacheFile(profile))) Apply(File.ReadAllText(CacheFile(profile))); } catch (Exception) { }
    }

    private void Apply(string json)
    {
        using var doc = JsonDocument.Parse(json);
        var root = doc.RootElement;
        if (root.TryGetProperty("vbucks", out var vb) && vb.TryGetInt64(out long v)) { _vbucks = v; OnPropertyChanged(nameof(VBucksText)); OnPropertyChanged(nameof(HasVBucks)); }
        MenuActive = root.TryGetProperty("menuActive", out var m) && m.ValueKind == JsonValueKind.True;
        if (root.TryGetProperty("totals", out var t))
        {
            Kills = Num(t, "kills"); Wins = Num(t, "wins"); Matches = Num(t, "matches");
            // Time in matches as the backend counts it (any season, also matches the launcher didn't time, such as
            // a friend's server), so Total Playtime shows whichever total is larger.
            _matchMinutes = t.TryGetProperty("hours", out var h) && h.TryGetDouble(out double hours) ? hours * 60 : 0;
            ShowTotalPlaytime();
        }
        long claimed = root.TryGetProperty("rewardClaimed", out var rc) && rc.TryGetInt64(out long c1) ? c1 : 0;
        long total = root.TryGetProperty("rewardTotal", out var rt) && rt.TryGetInt64(out long c2) ? c2 : 0;
        RewardsText = claimed.ToString("N0") + " / " + total.ToString("N0");

        Achievements.Clear();
        if (root.TryGetProperty("achievements", out var list))
            foreach (var a in list.EnumerateArray())
                Achievements.Add(new AchievementItem
                {
                    Id = Str(a, "id"), Name = Str(a, "name"), Description = Str(a, "desc"),
                    Tier = Str(a, "tier"), Badge = Str(a, "badge"),
                    Reward = a.TryGetProperty("reward", out var r) && r.TryGetInt64(out long rv) ? rv : 0,
                    Progress = a.TryGetProperty("progress", out var p) && p.TryGetDouble(out double pv) ? pv : 0,
                    Target = a.TryGetProperty("target", out var tg) && tg.TryGetDouble(out double tv) ? tv : 1,
                    Unlocked = a.TryGetProperty("unlocked", out var u) && u.ValueKind == JsonValueKind.True,
                    Claimed = a.TryGetProperty("claimed", out var cl) && cl.ValueKind == JsonValueKind.True,
                });
        OnPropertyChanged(nameof(ShowAchievementsPlaceholder));
        UnlockedCount = Achievements.Count(a => a.Unlocked);
        AchievementCount = Achievements.Count;
        Spotlight.Clear();
        foreach (var a in Achievements.OrderByDescending(a => a.Claimable).ThenBy(a => a.Claimed).ThenByDescending(a => a.Fraction).Take(4))
            Spotlight.Add(a);
    }

    private async Task ClaimAsync(AchievementItem item)
    {
        string? profile = _profile();
        if (profile is null) return;
        item.Claiming = true;
        try
        {
            string json = await SyndicateBackend.PostAsPlayerAsync(profile, "launcher/achievements/claim", new { id = item.Id });
            Apply(json);
            Message = "+" + item.RewardText + " V-Bucks added for " + item.Name + ".";
            try { File.WriteAllText(CacheFile(profile), json); } catch (Exception) { }
        }
        catch (Exception e) { Message = e.Message; item.Claiming = false; }
    }

    private static string Str(JsonElement e, string name) =>
        e.TryGetProperty(name, out var v) && v.ValueKind == JsonValueKind.String ? v.GetString() ?? "" : "";
    private static string Num(JsonElement e, string name) =>
        e.TryGetProperty(name, out var v) && v.TryGetDouble(out double d) ? Math.Floor(d).ToString("N0") : "0";
}
