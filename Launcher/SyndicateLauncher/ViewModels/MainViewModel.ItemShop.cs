using System;
using System.Collections.Generic;
using System.Collections.ObjectModel;
using System.Linq;
using System.Text.Json;
using System.Threading.Tasks;
using System.Windows.Input;
using SyndicateLauncher.Models;
using SyndicateLauncher.Services;

namespace SyndicateLauncher.ViewModels;

/// <summary>An item a season's shop can sell (backend /launcher/itemshop/items).</summary>
public sealed class ShopItemChoice
{
    public ShopItemChoice(string id, string name, string type, int price)
    {
        Id = id;
        Name = name;
        Type = type;
        Price = price;
    }

    public string Id { get; }
    public string Name { get; }
    public string Type { get; }
    public int Price { get; }
    public string Details => Type + "  ·  " + Price.ToString("N0") + " V-Bucks";
}

/// <summary>
/// Settings &gt; Item shop: how long each item shop lasts, and your own shop per season (featured and daily slots).
/// A season without one, and any slot you leave empty, stays automatic. Saved to settings.json and to the
/// backend's shop-settings.json (Services/ShopSettings.cs, backend Utils/syndicateShopSettings.js).
/// </summary>
public sealed partial class MainViewModel
{
    private static readonly int[] ShopHourValues = { 1, 2, 3, 6, 12, 24, 48, 72, 168 };
    public string[] ShopHourOptions { get; } =
        { "Every hour", "Every 2 hours", "Every 3 hours", "Every 6 hours (usual)", "Every 12 hours", "Every day", "Every 2 days", "Every 3 days", "Every week" };

    private GameModeSeasonChoice? _shopSeason;
    private List<ShopItemChoice> _shopAll = new();
    private string _shopSearch = "", _shopNote = "";
    private bool _shopBusy;
    private ICommand? _addShopFeaturedCommand, _addShopDailyCommand, _removeShopItemCommand, _clearCustomShopCommand, _loadShopItemsCommand;

    public ObservableCollection<ShopItemChoice> ShopResults { get; } = new();
    public ObservableCollection<ShopItemChoice> ShopFeatured { get; } = new();
    public ObservableCollection<ShopItemChoice> ShopDaily { get; } = new();

    public int ShopHoursIndex
    {
        get { int i = Array.IndexOf(ShopHourValues, _settings.ShopHours); return i >= 0 ? i : 3; }
        set
        {
            if (value < 0 || value >= ShopHourValues.Length || ShopHourValues[value] == _settings.ShopHours) return;
            _settings.ShopHours = ShopHourValues[value];
            SaveShop("The item shop now changes " + ShopHourOptions[value].ToLowerInvariant().Replace(" (usual)", "") + ".");
            OnPropertyChanged();
        }
    }

    public GameModeSeasonChoice? SelectedShopSeason
    {
        get => _shopSeason;
        set
        {
            if (!Set(ref _shopSeason, value)) return;
            _shopAll = new();
            ShopResults.Clear();
            LoadCustomShop();
            _ = LoadShopItemsAsync();
        }
    }

    public string ShopSearch
    {
        get => _shopSearch;
        set { if (Set(ref _shopSearch, value ?? "")) FilterShopResults(); }
    }

    public string ShopNote
    {
        get => _shopNote;
        private set => Set(ref _shopNote, value);
    }

    public bool ShopBusy
    {
        get => _shopBusy;
        private set => Set(ref _shopBusy, value);
    }

    public string ShopSlotsHint => _shopSeason?.Season.SeasonNumber switch
    {
        null => "",
        <= 3 => "This season's shop has 2 featured and 6 daily slots.",
        >= 8 => "This season's shop has 4 featured and 6 daily slots.",
        _ => "This season's shop has 3 featured and 6 daily slots."
    };

    public ICommand AddShopFeaturedCommand => _addShopFeaturedCommand ??= new RelayCommand(p => AddShopItem(p as ShopItemChoice, ShopFeatured));
    public ICommand AddShopDailyCommand => _addShopDailyCommand ??= new RelayCommand(p => AddShopItem(p as ShopItemChoice, ShopDaily));
    public ICommand RemoveShopItemCommand => _removeShopItemCommand ??= new RelayCommand(p =>
    {
        if (p is not ShopItemChoice item) return;
        ShopFeatured.Remove(item);
        ShopDaily.Remove(item);
        SaveCustomShop();
    });
    public ICommand ClearCustomShopCommand => _clearCustomShopCommand ??= new RelayCommand(() =>
    {
        ShopFeatured.Clear();
        ShopDaily.Clear();
        SaveCustomShop();
    }, () => _shopSeason is not null);
    public ICommand LoadShopItemsCommand => _loadShopItemsCommand ??= new RelayCommand(() => _ = LoadShopItemsAsync());

    private void InitializeItemShop()
    {
        _settings.CustomShops ??= new();
        if (_settings.ShopHours < 1 || _settings.ShopHours > 168) _settings.ShopHours = 6;
        ShopSettings.Load(_settings.ShopHours, _settings.CustomShops);
        _shopSeason = GameModeSeasons.FirstOrDefault(c => c.Season.Key == _settings.LastBuildSeasonKey)
                      ?? GameModeSeasons.LastOrDefault(c => c.Season.ChapterNumber == 1);
        LoadCustomShop();
    }

    private string ShopKey => _shopSeason?.Season.SeasonNumber.ToString() ?? "";

    private ShopItemChoice Choice(string id)
    {
        var known = _shopAll.FirstOrDefault(i => string.Equals(i.Id, id, StringComparison.OrdinalIgnoreCase));
        if (known is not null) return known;
        string name = id.Contains(':') ? id[(id.IndexOf(':') + 1)..] : id;
        return new ShopItemChoice(id, name, id.Split(':')[0].Replace("Athena", ""), 0);
    }

    private void LoadCustomShop()
    {
        ShopFeatured.Clear();
        ShopDaily.Clear();
        if (_settings.CustomShops.TryGetValue(ShopKey, out var shop))
        {
            foreach (string id in shop.Featured) ShopFeatured.Add(Choice(id));
            foreach (string id in shop.Daily) ShopDaily.Add(Choice(id));
        }
        OnPropertyChanged(nameof(ShopSlotsHint));
        ShopNote = ShopFeatured.Count + ShopDaily.Count == 0
            ? "No items picked: this season's shop is automatic."
            : "Your picks replace the automatic items, featured first, then daily. Empty slots stay automatic.";
    }

    private async Task LoadShopItemsAsync()
    {
        var choice = _shopSeason;
        var profile = SelectedProfile;
        if (choice is null || profile is null || ShopBusy) return;
        ShopBusy = true;
        try
        {
            await EnsureBackendForPlayersAsync();
            string json = await SyndicateBackend.PostAsPlayerAsync(profile.Name, "launcher/itemshop/items", new { season = choice.Season.SeasonNumber });
            using var doc = JsonDocument.Parse(json);
            var items = new List<ShopItemChoice>();
            foreach (var item in doc.RootElement.GetProperty("items").EnumerateArray())
                items.Add(new ShopItemChoice(item.GetProperty("id").GetString() ?? "", item.GetProperty("name").GetString() ?? "",
                    item.GetProperty("type").GetString() ?? "", item.TryGetProperty("price", out var price) && price.ValueKind == JsonValueKind.Number ? price.GetInt32() : 0));
            if (!ReferenceEquals(choice, _shopSeason)) return;
            _shopAll = items.Where(i => i.Id.Length > 0).OrderBy(i => i.Type).ThenBy(i => i.Name).ToList();
            LoadCustomShop(); // names for picks saved before the list was loaded
            FilterShopResults();
        }
        catch (Exception error)
        {
            ShopNote = "Couldn't load this season's items: " + error.Message;
        }
        finally
        {
            ShopBusy = false;
        }
    }

    private void FilterShopResults()
    {
        ShopResults.Clear();
        string search = _shopSearch.Trim();
        foreach (var item in _shopAll.Where(i => search.Length == 0 || i.Name.Contains(search, StringComparison.OrdinalIgnoreCase)
                                                 || i.Type.Contains(search, StringComparison.OrdinalIgnoreCase)).Take(80))
            ShopResults.Add(item);
    }

    private void AddShopItem(ShopItemChoice? item, ObservableCollection<ShopItemChoice> list)
    {
        if (item is null || _shopSeason is null) return;
        if (ShopFeatured.Concat(ShopDaily).Any(i => string.Equals(i.Id, item.Id, StringComparison.OrdinalIgnoreCase)))
        {
            ShopNote = item.Name + " is already in your shop.";
            return;
        }
        int limit = ReferenceEquals(list, ShopFeatured) ? (_shopSeason.Season.SeasonNumber <= 3 ? 2 : _shopSeason.Season.SeasonNumber >= 8 ? 4 : 3) : 6;
        if (list.Count >= limit)
        {
            ShopNote = "That part of the shop is full (" + limit + " slots). Remove an item first.";
            return;
        }
        list.Add(item);
        SaveCustomShop();
    }

    private void SaveCustomShop()
    {
        if (_shopSeason is null) return;
        if (ShopFeatured.Count + ShopDaily.Count == 0) _settings.CustomShops.Remove(ShopKey);
        else _settings.CustomShops[ShopKey] = new CustomShop { Featured = ShopFeatured.Select(i => i.Id).ToList(), Daily = ShopDaily.Select(i => i.Id).ToList() };
        SaveShop(ShopFeatured.Count + ShopDaily.Count == 0
            ? _shopSeason.Season.Name + "'s shop is automatic again."
            : "Saved " + _shopSeason.Season.Name + "'s shop. Fortnite shows it the next time it loads the shop.");
    }

    private void SaveShop(string note)
    {
        Save();
        ShopSettings.Load(_settings.ShopHours, _settings.CustomShops);
        ShopSettings.WriteForBackend();
        ShopNote = note;
    }
}
