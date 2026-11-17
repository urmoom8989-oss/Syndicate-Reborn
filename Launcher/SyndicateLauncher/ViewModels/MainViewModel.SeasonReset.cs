using System;
using System.Collections.Generic;
using System.Collections.ObjectModel;
using System.Linq;
using System.Text.Json;
using System.Threading.Tasks;
using System.Windows.Input;
using SyndicateLauncher.Services;

namespace SyndicateLauncher.ViewModels;

/// <summary>A season in the Reset season list (2 to X, Chapter 2 Seasons 1 and 2; numbered like the backend).</summary>
public sealed record SeasonResetChoice(int Season, string Label)
{
    public override string ToString() => Label;
}

/// <summary>One Battle Pass item of the chosen season that is in the player's locker.</summary>
public sealed class SeasonResetItem : ViewModelBase
{
    private readonly Action _changed;
    private bool _isSelected;

    public SeasonResetItem(string templateId, string name, string type, int tier, bool premium, bool selected, Action changed)
    {
        TemplateId = templateId;
        Name = name;
        Type = type;
        Tier = tier;
        Premium = premium;
        _isSelected = selected;
        _changed = changed;
    }

    public string TemplateId { get; }
    public string Name { get; }
    public string Type { get; }
    public int Tier { get; }
    public bool Premium { get; }
    public string Detail => Type + "  ·  Tier " + Tier + (Premium ? "" : "  ·  Free");

    public bool IsSelected
    {
        get => _isSelected;
        set { if (Set(ref _isSelected, value)) _changed(); }
    }

    internal void SetQuietly(bool value)
    {
        if (_isSelected == value) return;
        _isSelected = value;
        OnPropertyChanged(nameof(IsSelected));
    }
}

/// <summary>
/// Choose Player &gt; RESET SEASON (backend Utils/syndicateSeasonReset.js, routes /launcher/seasonreset/*). Pick a season,
/// choose whether its level, Battle Pass tier and challenges start over, and tick which of its Battle Pass items leave
/// the locker (all, outfits only, or any you pick). Confirming takes two clicks. Removed items without a progress reset
/// stay out until PUT BACK; with a reset they are earned again tier by tier.
/// </summary>
public sealed partial class MainViewModel
{
    private bool _isSeasonResetOpen, _seasonResetBusy, _seasonResetProgress = true, _seasonResetArmed, _seasonResetUnlockAll;
    private bool _suppressSeasonResetLoad;
    private string _seasonResetError = "", _seasonResetDone = "", _seasonResetSummary = "", _seasonResetPassNote = "";
    private int _seasonResetRemoved, _seasonResetLoaded;
    private SeasonResetChoice? _seasonResetSeason;
    private Models.Profile? _seasonResetProfile;
    private ICommand? _startSeasonReset, _closeSeasonReset, _selectAllPass, _selectOutfits, _selectNone, _confirmSeasonReset, _restoreRemoved;

    public ObservableCollection<SeasonResetChoice> SeasonResetSeasons { get; } = new();
    public ObservableCollection<SeasonResetItem> SeasonResetItems { get; } = new();

    public ICommand StartSeasonResetCommand => _startSeasonReset ??= new RelayCommand(() => _ = OpenSeasonResetAsync());
    public ICommand CloseSeasonResetCommand => _closeSeasonReset ??= new RelayCommand(() => { if (!_seasonResetBusy) CloseSeasonReset(); });
    public ICommand SelectAllPassItemsCommand => _selectAllPass ??= new RelayCommand(() => SelectPassItems(_ => true), () => !_seasonResetBusy);
    public ICommand SelectOutfitPassItemsCommand => _selectOutfits ??= new RelayCommand(() => SelectPassItems(i => i.Type == "Outfit"), () => !_seasonResetBusy);
    public ICommand SelectNoPassItemsCommand => _selectNone ??= new RelayCommand(() => SelectPassItems(_ => false), () => !_seasonResetBusy);
    public ICommand ConfirmSeasonResetCommand => _confirmSeasonReset ??= new RelayCommand(() => _ = ConfirmSeasonResetAsync(), () => CanConfirmSeasonReset);
    public ICommand RestoreRemovedPassItemsCommand => _restoreRemoved ??= new RelayCommand(() => _ = RestoreRemovedAsync(), () => !_seasonResetBusy && _seasonResetRemoved > 0 && !_seasonResetUnlockAll);

    public bool IsSeasonResetOpen { get => _isSeasonResetOpen; private set => Set(ref _isSeasonResetOpen, value); }
    public bool SeasonResetBusy
    {
        get => _seasonResetBusy;
        private set { if (Set(ref _seasonResetBusy, value)) { RaiseSeasonReset(); CommandManager.InvalidateRequerySuggested(); } }
    }
    public bool SeasonResetIdle => !_seasonResetBusy;

    public SeasonResetChoice? SelectedSeasonResetSeason
    {
        get => _seasonResetSeason;
        set
        {
            if (!Set(ref _seasonResetSeason, value) || value is null || _suppressSeasonResetLoad) return;
            _ = LoadSeasonResetAsync(value.Season);
        }
    }

    /// <summary>Also reset level, Battle Pass tier and challenges (on by default).</summary>
    public bool SeasonResetProgress
    {
        get => _seasonResetProgress;
        set { if (Set(ref _seasonResetProgress, value)) { _seasonResetArmed = false; RaiseSeasonReset(); } }
    }

    public string SeasonResetTitle => "Reset a season for " + (_seasonResetProfile?.Name ?? "this player");
    public string SeasonResetSummary { get => _seasonResetSummary; private set => Set(ref _seasonResetSummary, value); }
    public string SeasonResetPassNote { get => _seasonResetPassNote; private set { if (Set(ref _seasonResetPassNote, value)) OnPropertyChanged(nameof(HasSeasonResetPassNote)); } }
    public bool HasSeasonResetPassNote => _seasonResetPassNote.Length > 0 && _seasonResetProgress;
    public string SeasonResetError { get => _seasonResetError; private set { if (Set(ref _seasonResetError, value)) OnPropertyChanged(nameof(HasSeasonResetError)); } }
    public bool HasSeasonResetError => _seasonResetError.Length > 0;
    public string SeasonResetDone { get => _seasonResetDone; private set { if (Set(ref _seasonResetDone, value)) OnPropertyChanged(nameof(HasSeasonResetDone)); } }
    public bool HasSeasonResetDone => _seasonResetDone.Length > 0;
    public bool HasSeasonResetItems => SeasonResetItems.Count > 0;
    public bool SeasonResetNoItems => !_seasonResetBusy && SeasonResetItems.Count == 0 && _seasonResetLoaded > 0;
    public bool SeasonResetShowLoading => _seasonResetBusy && SeasonResetItems.Count == 0;
    public bool HasRemovedPassItems => _seasonResetRemoved > 0;
    public string RestoreRemovedText => "PUT BACK " + _seasonResetRemoved + " REMOVED";

    private int SelectedPassItemCount => SeasonResetItems.Count(i => i.IsSelected);
    private string SeasonResetName => _seasonResetSeason?.Label ?? "This season";

    /// <summary>What confirming will do, in one or two sentences.</summary>
    public string SeasonResetPlan
    {
        get
        {
            if (_seasonResetUnlockAll)
                return (_seasonResetProfile?.Name ?? "This player") + " uses Unlock All, which puts every item back each time you play. " +
                       "Switch to Progression first (SWITCH TO PROGRESSION above), then reset.";
            int n = SelectedPassItemCount;
            string items = n + " Battle Pass item" + (n == 1 ? " leaves" : "s leave");
            if (_seasonResetProgress)
                return SeasonResetName + " starts over: level 1, no Battle Pass tiers and fresh challenges. " +
                       (n > 0 ? items + " the locker; you earn " + (n == 1 ? "it" : "them") + " again as you level up." : "Every item you own stays in the locker.");
            return n > 0
                ? items + " the locker and " + (n == 1 ? "stays" : "stay") + " out until you put " + (n == 1 ? "it" : "them") + " back here. Your level and tier don't change."
                : "Tick items to take out of the locker, or turn on the progress reset.";
        }
    }

    public string SeasonResetConfirmText
    {
        get
        {
            if (_seasonResetBusy) return "WORKING...";
            int n = SelectedPassItemCount;
            string action = _seasonResetProgress ? "RESET " + SeasonResetName.ToUpperInvariant() : "REMOVE " + n + " ITEM" + (n == 1 ? "" : "S");
            return _seasonResetArmed ? "CLICK AGAIN: " + action : action;
        }
    }

    private bool CanConfirmSeasonReset => !_seasonResetBusy && _seasonResetSeason is not null && !_seasonResetUnlockAll &&
                                          _seasonResetLoaded > 0 && (_seasonResetProgress || SelectedPassItemCount > 0);

    private void RaiseSeasonReset()
    {
        foreach (var name in new[] { nameof(SeasonResetIdle), nameof(SeasonResetPlan), nameof(SeasonResetConfirmText), nameof(SeasonResetTitle),
                     nameof(HasSeasonResetItems), nameof(SeasonResetNoItems), nameof(SeasonResetShowLoading), nameof(HasRemovedPassItems), nameof(RestoreRemovedText),
                     nameof(HasSeasonResetPassNote) })
            OnPropertyChanged(name);
        CommandManager.InvalidateRequerySuggested();
    }

    private void OnSeasonResetItemChanged()
    {
        _seasonResetArmed = false;
        SeasonResetDone = "";
        RaiseSeasonReset();
    }

    private void SelectPassItems(Func<SeasonResetItem, bool> pick)
    {
        foreach (var item in SeasonResetItems) item.SetQuietly(pick(item));
        OnSeasonResetItemChanged();
    }

    // The launcher's season as the backend numbers it: Chapter 1 Season X is 10, Chapter 2 Season 1 is 11.
    private int ActiveSeasonForReset()
    {
        var season = ActiveSeason;
        if (season is null) return 0;
        return season.ChapterNumber == 2 ? 10 + season.SeasonNumber : season.SeasonNumber;
    }

    private async Task OpenSeasonResetAsync()
    {
        if (SelectedProfile is null) { AvatarError = "Choose a player first."; return; }
        if (!_actionBusy) EndPlayerAction();
        _seasonResetProfile = SelectedProfile;
        _seasonResetArmed = false;
        _seasonResetLoaded = 0;
        SeasonResetError = "";
        SeasonResetDone = "";
        SeasonResetSummary = "";
        SeasonResetPassNote = "";
        SeasonResetItems.Clear();
        IsSeasonResetOpen = true;
        RaiseSeasonReset();
        int season = _seasonResetSeason?.Season ?? ActiveSeasonForReset();
        await LoadSeasonResetAsync(season);
    }

    private void CloseSeasonReset()
    {
        IsSeasonResetOpen = false;
        _seasonResetArmed = false;
        SeasonResetError = "";
        SeasonResetDone = "";
        SeasonResetItems.Clear();
        RaiseSeasonReset();
    }

    private string? SeasonResetBlocked() =>
        _launching || GameLauncher.IsRunning ? "Close Fortnite first: a season can't be reset while a match session is running." : null;

    private async Task LoadSeasonResetAsync(int season)
    {
        var profile = _seasonResetProfile;
        if (profile is null || _seasonResetBusy) return;
        if (SeasonResetBlocked() is { } blocked) { SeasonResetError = blocked; return; }
        SeasonResetBusy = true;
        SeasonResetError = "";
        try
        {
            await EnsureBackendForPlayersAsync();
            string json = await SyndicateBackend.PostAsPlayerAsync(profile.Name, "launcher/seasonreset/status", new { season });
            ApplySeasonResetView(json, keepSelection: null);
        }
        catch (Exception error)
        {
            SeasonResetError = "Couldn't load the season: " + error.Message;
        }
        finally
        {
            SeasonResetBusy = false;
        }
    }

    // Reads the backend's view (status, apply and restore all answer with it).
    private void ApplySeasonResetView(string json, IReadOnlyCollection<string>? keepSelection)
    {
        using var doc = JsonDocument.Parse(json);
        var root = doc.RootElement;
        _seasonResetUnlockAll = root.TryGetProperty("unlockAll", out var ua) && ua.ValueKind == JsonValueKind.True;

        if (SeasonResetSeasons.Count == 0 && root.TryGetProperty("seasons", out var seasons) && seasons.ValueKind == JsonValueKind.Array)
            foreach (var s in seasons.EnumerateArray())
                SeasonResetSeasons.Add(new SeasonResetChoice(s.GetProperty("season").GetInt32(), s.GetProperty("label").GetString() ?? ""));

        var selected = root.GetProperty("selected");
        int number = selected.GetProperty("season").GetInt32();
        _suppressSeasonResetLoad = true;
        try { SelectedSeasonResetSeason = SeasonResetSeasons.FirstOrDefault(c => c.Season == number); }
        finally { _suppressSeasonResetLoad = false; }

        bool played = selected.TryGetProperty("played", out var p) && p.ValueKind == JsonValueKind.True;
        int level = selected.GetProperty("level").GetInt32(), tier = selected.GetProperty("tier").GetInt32();
        int owned = selected.GetProperty("owned").GetInt32(), total = selected.GetProperty("total").GetInt32();
        _seasonResetRemoved = selected.GetProperty("removed").GetInt32();
        SeasonResetSummary = (played ? "Level " + Math.Max(1, level) + "  ·  Tier " + Math.Max(1, tier) : "Not played yet") +
                             "  ·  " + owned + " of " + total + " Battle Pass items in the locker";
        SeasonResetPassNote = selected.TryGetProperty("completePass", out var pass) && pass.ValueKind == JsonValueKind.Object
            ? "You bought this season's Complete Pass in the launcher. Resetting refunds the " +
              pass.GetProperty("price").GetInt32().ToString("N0") + " V-Bucks it cost, so the pass can be earned or bought again."
            : "";

        SeasonResetItems.Clear();
        foreach (var item in selected.GetProperty("items").EnumerateArray())
        {
            if (item.GetProperty("owned").ValueKind != JsonValueKind.True) continue;
            string id = item.GetProperty("templateId").GetString() ?? "";
            bool pick = keepSelection?.Contains(id, StringComparer.OrdinalIgnoreCase) ?? true; // everything ticked to start
            SeasonResetItems.Add(new SeasonResetItem(id, item.GetProperty("name").GetString() ?? id,
                item.GetProperty("type").GetString() ?? "", item.GetProperty("tier").GetInt32(),
                item.GetProperty("track").GetString() == "paid", pick, OnSeasonResetItemChanged));
        }
        _seasonResetLoaded++;
        _seasonResetArmed = false;
        RaiseSeasonReset();
    }

    private async Task ConfirmSeasonResetAsync()
    {
        var profile = _seasonResetProfile;
        var season = _seasonResetSeason;
        if (profile is null || season is null || !CanConfirmSeasonReset) return;
        if (SeasonResetBlocked() is { } blocked) { SeasonResetError = blocked; return; }
        if (!_seasonResetArmed)
        {
            // First click only arms the button: it now says exactly what the second click does.
            _seasonResetArmed = true;
            SeasonResetDone = "";
            RaiseSeasonReset();
            return;
        }
        _seasonResetArmed = false;
        var remove = SeasonResetItems.Where(i => i.IsSelected).Select(i => i.TemplateId).ToList();
        SeasonResetBusy = true;
        SeasonResetError = "";
        try
        {
            await EnsureBackendForPlayersAsync();
            string json = await SyndicateBackend.PostAsPlayerAsync(profile.Name, "launcher/seasonreset/apply",
                new { season = season.Season, resetProgress = _seasonResetProgress, remove });
            ApplySeasonResetView(json, keepSelection: Array.Empty<string>());
            SeasonResetDone = MessageOf(json);
            StatusMessage = profile.Name + ": " + SeasonResetDone;
            _ = Home.RefreshAsync(force: true);
        }
        catch (Exception error)
        {
            SeasonResetError = error.Message;
        }
        finally
        {
            SeasonResetBusy = false;
        }
    }

    private async Task RestoreRemovedAsync()
    {
        var profile = _seasonResetProfile;
        var season = _seasonResetSeason;
        if (profile is null || season is null || _seasonResetBusy) return;
        if (SeasonResetBlocked() is { } blocked) { SeasonResetError = blocked; return; }
        SeasonResetBusy = true;
        SeasonResetError = "";
        try
        {
            await EnsureBackendForPlayersAsync();
            string json = await SyndicateBackend.PostAsPlayerAsync(profile.Name, "launcher/seasonreset/restore", new { season = season.Season });
            ApplySeasonResetView(json, keepSelection: Array.Empty<string>());
            SeasonResetDone = MessageOf(json);
        }
        catch (Exception error)
        {
            SeasonResetError = error.Message;
        }
        finally
        {
            SeasonResetBusy = false;
        }
    }

    private static string MessageOf(string json)
    {
        try
        {
            using var doc = JsonDocument.Parse(json);
            return doc.RootElement.TryGetProperty("message", out var m) ? m.GetString() ?? "Done." : "Done.";
        }
        catch (JsonException) { return "Done."; }
    }
}
