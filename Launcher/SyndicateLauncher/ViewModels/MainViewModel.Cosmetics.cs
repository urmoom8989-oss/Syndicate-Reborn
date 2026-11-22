using System;
using System.Collections.Generic;
using System.Collections.ObjectModel;
using System.Linq;
using System.Text.Json;
using System.Threading.Tasks;
using System.Windows.Input;
using SyndicateLauncher.Services;

namespace SyndicateLauncher.ViewModels;

/// <summary>One cosmetic tile in the cosmetics editor.</summary>
public sealed class CosmeticTile : ViewModelBase
{
    private bool _isOwned;
    private string _pick = "";

    public CosmeticTile(string id, string name, string type, int price)
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
    public string PriceText => Price > 0 ? Price.ToString("N0") : "";

    /// <summary>The item's picture (fortnite-api.com), only while the launcher is online.</summary>
    public string? IconUrl => OfflineMode.Enabled || !Id.Contains(':')
        ? null
        : "https://fortnite-api.com/images/cosmetics/br/" + Id[(Id.IndexOf(':') + 1)..].ToLowerInvariant() + "/smallicon.png";

    public bool IsOwned
    {
        get => _isOwned;
        set => Set(ref _isOwned, value);
    }

    /// <summary>Item shop mode: "FEATURED" or "DAILY" when the item is one of your picks.</summary>
    public string Pick
    {
        get => _pick;
        set { if (Set(ref _pick, value ?? "")) OnPropertyChanged(nameof(IsPicked)); }
    }

    public bool IsPicked => _pick.Length > 0;
}

/// <summary>
/// The cosmetics editor (Views/CosmeticsView.xaml): every cosmetic a season's game files have, as picture tiles.
/// Locker mode adds or removes items on the selected player (backend /launcher/locker/*); Item shop mode picks the
/// season's featured and daily items (MainViewModel.ItemShop.cs). Pictures come from fortnite-api.com and are not
/// loaded in Offline mode (the tiles show names then).
/// </summary>
public sealed partial class MainViewModel
{
    private const int CosmeticsPage = 150;
    private bool _isCosmeticsOpen, _cosmeticsLockerMode, _cosmeticsBusy;
    private string _cosmeticsSearch = "", _cosmeticsType = "All", _cosmeticsNote = "";
    private int _cosmeticsShown = CosmeticsPage;
    private List<CosmeticTile> _cosmeticsAll = new();
    private ICommand? _openLockerEditorCommand, _openShopEditorCommand, _closeCosmeticsCommand, _cosmeticTileCommand,
        _cosmeticsMoreCommand, _cosmeticsTypeCommand, _lockerAddShownCommand, _lockerRemoveShownCommand;

    public ObservableCollection<CosmeticTile> CosmeticTiles { get; } = new();
    public ObservableCollection<string> CosmeticTypes { get; } = new();

    public bool IsCosmeticsOpen { get => _isCosmeticsOpen; private set => Set(ref _isCosmeticsOpen, value); }
    public bool CosmeticsLockerMode
    {
        get => _cosmeticsLockerMode;
        private set { if (Set(ref _cosmeticsLockerMode, value)) { OnPropertyChanged(nameof(CosmeticsShopMode)); OnPropertyChanged(nameof(CosmeticsTitle)); } }
    }
    public bool CosmeticsShopMode => !_cosmeticsLockerMode;
    public string CosmeticsTitle => _cosmeticsLockerMode ? "LOCKER" : "ITEM SHOP";
    public bool CosmeticsBusy { get => _cosmeticsBusy; private set => Set(ref _cosmeticsBusy, value); }
    public string CosmeticsNote { get => _cosmeticsNote; private set => Set(ref _cosmeticsNote, value); }
    public string CosmeticsType { get => _cosmeticsType; private set => Set(ref _cosmeticsType, value); }
    public string CosmeticsSearch
    {
        get => _cosmeticsSearch;
        set { if (Set(ref _cosmeticsSearch, value ?? "")) { _cosmeticsShown = CosmeticsPage; FilterCosmetics(); } }
    }
    public bool CosmeticsHasMore => Filtered().Count() > CosmeticTiles.Count;
    public string CosmeticsCount => CosmeticTiles.Count + " of " + Filtered().Count() + " shown"
                                    + (_cosmeticsLockerMode ? "  ·  " + _cosmeticsAll.Count(t => t.IsOwned) + " owned" : "");

    public ICommand OpenLockerEditorCommand => _openLockerEditorCommand ??= new RelayCommand(() => _ = OpenCosmeticsAsync(locker: true));
    public ICommand OpenShopEditorCommand => _openShopEditorCommand ??= new RelayCommand(() => _ = OpenCosmeticsAsync(locker: false));
    public ICommand CloseCosmeticsCommand => _closeCosmeticsCommand ??= new RelayCommand(() => IsCosmeticsOpen = false);
    public ICommand CosmeticTileCommand => _cosmeticTileCommand ??= new RelayCommand(p => { if (p is CosmeticTile t) _ = ClickCosmeticAsync(t); });
    public ICommand CosmeticsMoreCommand => _cosmeticsMoreCommand ??= new RelayCommand(() => { _cosmeticsShown += CosmeticsPage; FilterCosmetics(); });
    public ICommand CosmeticsTypeCommand => _cosmeticsTypeCommand ??= new RelayCommand(p =>
    {
        CosmeticsType = p as string ?? "All";
        _cosmeticsShown = CosmeticsPage;
        FilterCosmetics();
    });
    public ICommand LockerAddShownCommand => _lockerAddShownCommand ??= new RelayCommand(() =>
        _ = EditLockerAsync(Filtered().Where(t => !t.IsOwned).Select(t => t.Id).ToList(), new List<string>()));
    public ICommand LockerRemoveShownCommand => _lockerRemoveShownCommand ??= new RelayCommand(() =>
        _ = EditLockerAsync(new List<string>(), Filtered().Where(t => t.IsOwned).Select(t => t.Id).ToList()));

    private IEnumerable<CosmeticTile> Filtered()
    {
        string search = _cosmeticsSearch.Trim();
        return _cosmeticsAll.Where(t => (_cosmeticsType == "All" || t.Type == _cosmeticsType)
                                        && (search.Length == 0 || t.Name.Contains(search, StringComparison.OrdinalIgnoreCase)));
    }

    private void FilterCosmetics()
    {
        CosmeticTiles.Clear();
        foreach (var tile in Filtered().Take(_cosmeticsShown)) CosmeticTiles.Add(tile);
        OnPropertyChanged(nameof(CosmeticsHasMore));
        OnPropertyChanged(nameof(CosmeticsCount));
    }

    /// <summary>The season the editor shows (the item shop's season picker drives both modes).</summary>
    private GameModeSeasonChoice? CosmeticsSeason => SelectedShopSeason;

    private async Task OpenCosmeticsAsync(bool locker)
    {
        CosmeticsLockerMode = locker;
        IsCosmeticsOpen = true;
        await ReloadCosmeticsAsync();
    }

    /// <summary>Reloads the tiles when the season changes while the editor is open.</summary>
    private void OnCosmeticsSeasonChanged()
    {
        if (IsCosmeticsOpen) _ = ReloadCosmeticsAsync();
    }

    private async Task ReloadCosmeticsAsync()
    {
        var season = CosmeticsSeason;
        var profile = SelectedProfile;
        if (season is null || profile is null) { CosmeticsNote = "Pick a player and a season first."; return; }
        CosmeticsBusy = true;
        CosmeticsNote = "Loading " + season.Season.Name + "'s items...";
        try
        {
            await EnsureBackendForPlayersAsync();
            string json = await SyndicateBackend.PostAsPlayerAsync(profile.Name, "launcher/itemshop/items", new { season = season.Season.SeasonNumber });
            var tiles = new List<CosmeticTile>();
            using (var doc = JsonDocument.Parse(json))
                foreach (var item in doc.RootElement.GetProperty("items").EnumerateArray())
                {
                    string id = item.GetProperty("id").GetString() ?? "";
                    if (id.Length == 0) continue;
                    tiles.Add(new CosmeticTile(id, item.GetProperty("name").GetString() ?? id, item.GetProperty("type").GetString() ?? "",
                        item.TryGetProperty("price", out var p) && p.ValueKind == JsonValueKind.Number ? p.GetInt32() : 0));
                }
            if (_cosmeticsLockerMode)
            {
                string state = await SyndicateBackend.PostAsPlayerAsync(profile.Name, "launcher/locker/state", new { });
                using var doc = JsonDocument.Parse(state);
                var owned = doc.RootElement.GetProperty("owned").EnumerateArray().Select(e => e.GetString() ?? "").ToHashSet(StringComparer.OrdinalIgnoreCase);
                foreach (var t in tiles) t.IsOwned = owned.Contains(t.Id);
                CosmeticsNote = doc.RootElement.TryGetProperty("unlockAll", out var all) && all.GetBoolean()
                    ? "This player has Unlock all on: switch the player to Progression to choose items."
                    : "Click an item to add it to " + profile.Name + "'s locker or take it out. Outfits come with all their styles.";
            }
            else CosmeticsNote = "Click an item to put it in your shop: outfits go to Featured, everything else to Daily. Click it again to take it out.";
            string[] order = { "Outfit", "Back bling", "Pickaxe", "Glider", "Emote", "Contrail", "Wrap", "Loading screen", "Music", "Spray", "Toy", "Pet", "Emoticon" };
            _cosmeticsAll = tiles.OrderBy(t => Array.IndexOf(order, t.Type) is var i && i >= 0 ? i : 99).ThenBy(t => t.Name).ToList();
            CosmeticTypes.Clear();
            CosmeticTypes.Add("All");
            foreach (string type in _cosmeticsAll.Select(t => t.Type).Distinct()) CosmeticTypes.Add(type);
            if (!CosmeticTypes.Contains(_cosmeticsType)) CosmeticsType = "All";
            RefreshShopPicks();
            _cosmeticsShown = CosmeticsPage;
            FilterCosmetics();
        }
        catch (Exception error)
        {
            CosmeticsNote = "Couldn't load the items: " + error.Message;
        }
        finally
        {
            CosmeticsBusy = false;
        }
    }

    private void RefreshShopPicks()
    {
        var featured = ShopFeatured.Select(i => i.Id).ToHashSet(StringComparer.OrdinalIgnoreCase);
        var daily = ShopDaily.Select(i => i.Id).ToHashSet(StringComparer.OrdinalIgnoreCase);
        foreach (var t in _cosmeticsAll) t.Pick = featured.Contains(t.Id) ? "FEATURED" : daily.Contains(t.Id) ? "DAILY" : "";
    }

    private async Task ClickCosmeticAsync(CosmeticTile tile)
    {
        if (_cosmeticsLockerMode)
        {
            if (tile.IsOwned) await EditLockerAsync(new List<string>(), new List<string> { tile.Id });
            else await EditLockerAsync(new List<string> { tile.Id }, new List<string>());
            return;
        }
        var existing = ShopFeatured.Concat(ShopDaily).FirstOrDefault(i => string.Equals(i.Id, tile.Id, StringComparison.OrdinalIgnoreCase));
        if (existing is not null) RemoveShopItemCommand.Execute(existing);
        else AddShopItem(new ShopItemChoice(tile.Id, tile.Name, tile.Type, tile.Price), tile.Type == "Outfit" ? ShopFeatured : ShopDaily);
        RefreshShopPicks();
        CosmeticsNote = ShopNote;
    }

    private async Task EditLockerAsync(List<string> add, List<string> remove)
    {
        var season = CosmeticsSeason;
        var profile = SelectedProfile;
        if (season is null || profile is null || CosmeticsBusy || add.Count + remove.Count == 0) return;
        CosmeticsBusy = true;
        try
        {
            await EnsureBackendForPlayersAsync();
            string json = await SyndicateBackend.PostAsPlayerAsync(profile.Name, "launcher/locker/set", new { season = season.Season.SeasonNumber, add, remove });
            using var doc = JsonDocument.Parse(json);
            var owned = doc.RootElement.GetProperty("owned").EnumerateArray().Select(e => e.GetString() ?? "").ToHashSet(StringComparer.OrdinalIgnoreCase);
            foreach (var t in _cosmeticsAll) t.IsOwned = owned.Contains(t.Id);
            int added = doc.RootElement.GetProperty("added").GetInt32(), removed = doc.RootElement.GetProperty("removed").GetInt32();
            CosmeticsNote = (added > 0 ? added + " added" : "") + (added > 0 && removed > 0 ? ", " : "") + (removed > 0 ? removed + " removed" : "")
                            + (added + removed == 0 ? "Nothing changed (default items always stay)." : ". Restart Fortnite if it's open to see your locker.");
            OnPropertyChanged(nameof(CosmeticsCount));
        }
        catch (Exception error)
        {
            CosmeticsNote = error.Message;
        }
        finally
        {
            CosmeticsBusy = false;
        }
    }
}
