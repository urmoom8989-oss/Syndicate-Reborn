// Syndicate © 2026 Syndicate. All rights reserved. Proprietary — no copying, distribution, or derivative works. See LICENSE.
using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text.Json;
using System.Threading.Tasks;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Controls.Primitives;
using System.Windows.Input;
using System.Windows.Media;
using System.Windows.Media.Animation;
using System.Windows.Threading;

namespace SyndicateClassic;

// Secret item shop. Typing W W S S A D A D and then Escape anywhere in the launcher (not in a text box)
// shows "CODE ACTIVATED" and unlocks a hidden rail tab: every item-shop cosmetic from every season,
// searchable and filterable, bought with the player's V-Bucks (backend: launcher/shop/catalog|purchase).
// The unlock lasts until the launcher closes (hidden again on every start). Text only, no art.
public partial class MainWindow
{
    private static readonly Key[] SecretCode = { Key.W, Key.W, Key.S, Key.S, Key.A, Key.D, Key.A, Key.D };
    private readonly List<Key> _secretKeys = new();
    private Grid? _secretShopView;
    private Button? _secretShopRail;
    private Border? _codeToast;
    private TextBox? _secretSearch;
    private TextBlock? _secretBalance, _secretStatus;
    private WrapPanel? _secretGrid;
    private readonly List<Button> _secretChips = new();
    private string _secretType = "All";
    private JsonElement[] _secretItems = Array.Empty<JsonElement>();
    private bool _secretBusy;
    private string? _secretConfirm;
    private DispatcherTimer? _secretSearchDelay;

    // Unlocked only until the launcher closes: every new start has the tab hidden again.
    private static bool SecretShopUnlocked;
    private static string SecretFlag => Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "ProjectTilted", "secret-shop.txt");

    // Called once from the shell setup: watches keys (also ones other handlers already used).
    private void HookSecretCode()
    {
        try { if (File.Exists(SecretFlag)) File.Delete(SecretFlag); } catch { }   // an older build remembered the unlock on disk
        AddHandler(PreviewKeyDownEvent, new KeyEventHandler((_, e) =>
        {
            if (Keyboard.FocusedElement is TextBoxBase or PasswordBox) { _secretKeys.Clear(); return; }
            if (e.Key == Key.Escape)
            {
                bool match = _secretKeys.Count >= SecretCode.Length && _secretKeys.Skip(_secretKeys.Count - SecretCode.Length).SequenceEqual(SecretCode);
                _secretKeys.Clear();
                if (match) { ActivateSecretShop(); e.Handled = true; }
                return;
            }
            if (e.Key is Key.W or Key.A or Key.S or Key.D) { _secretKeys.Add(e.Key); if (_secretKeys.Count > 16) _secretKeys.RemoveAt(0); }
            else if (e.Key is not (Key.LeftShift or Key.RightShift)) _secretKeys.Clear();
        }), true);
    }

    private void ActivateSecretShop()
    {
        SecretShopUnlocked = true;
        if (_secretShopRail != null) _secretShopRail.Visibility = Visibility.Visible;
        ShowCodeToast();
        OpenSecretShop();
    }

    private Border BuildCodeToast()
    {
        var stack = new StackPanel { HorizontalAlignment = HorizontalAlignment.Center };
        stack.Children.Add(NightEyebrow("Secret").Also(t => t.HorizontalAlignment = HorizontalAlignment.Center));
        stack.Children.Add(NightTitle("CODE ACTIVATED", 44, Night.Gold).Also(t => t.HorizontalAlignment = HorizontalAlignment.Center));
        stack.Children.Add(NightText("The full item shop is unlocked. It has its own tab on the left.", 13.5, Night.Sub).Also(t => { t.HorizontalAlignment = HorizontalAlignment.Center; t.Margin = new Thickness(0, 6, 0, 0); }));
        _codeToast = new Border
        {
            Child = stack, Background = Hex("#F20D1020"), BorderBrush = Hex(Night.Gold), BorderThickness = new Thickness(1.5), CornerRadius = TiltCorners(18),
            Padding = new Thickness(40, 26, 40, 28), HorizontalAlignment = HorizontalAlignment.Center, VerticalAlignment = VerticalAlignment.Center,
            Visibility = Visibility.Collapsed, IsHitTestVisible = false,
            Effect = new System.Windows.Media.Effects.DropShadowEffect { Color = NightColor("#FFC21A"), BlurRadius = 40, ShadowDepth = 0, Opacity = .45 }
        };
        return _codeToast;
    }

    private void ShowCodeToast()
    {
        if (_codeToast == null) return;
        _codeToast.Visibility = Visibility.Visible;
        var fade = new DoubleAnimationUsingKeyFrames();
        fade.KeyFrames.Add(new LinearDoubleKeyFrame(0, KeyTime.FromTimeSpan(TimeSpan.Zero)));
        fade.KeyFrames.Add(new LinearDoubleKeyFrame(1, KeyTime.FromTimeSpan(TimeSpan.FromMilliseconds(220))));
        fade.KeyFrames.Add(new LinearDoubleKeyFrame(1, KeyTime.FromTimeSpan(TimeSpan.FromMilliseconds(2200))));
        fade.KeyFrames.Add(new LinearDoubleKeyFrame(0, KeyTime.FromTimeSpan(TimeSpan.FromMilliseconds(2700))));
        fade.Completed += (_, __) => _codeToast.Visibility = Visibility.Collapsed;
        _codeToast.BeginAnimation(OpacityProperty, fade);
    }

    private Grid BuildSecretShopPage()
    {
        var page = new Grid { Background = Brushes.Transparent, Visibility = Visibility.Collapsed };
        _secretShopView = page;
        var layout = new Grid { Margin = new Thickness(RailWidth + 22, 52 + 8, 40, 20) };
        RailTrack(layout);   // reflow with the menu (RailCollapse.cs)
        layout.RowDefinitions.Add(new RowDefinition { Height = GridLength.Auto });
        layout.RowDefinitions.Add(new RowDefinition { Height = GridLength.Auto });
        layout.RowDefinitions.Add(new RowDefinition());
        page.Children.Add(layout);

        var header = new DockPanel { Margin = new Thickness(0, 0, 0, 12) };
        _secretBalance = NightTitle("", 22, Night.Gold).Also(t => { t.VerticalAlignment = VerticalAlignment.Bottom; t.Margin = new Thickness(0, 0, 0, 6); });
        DockPanel.SetDock(_secretBalance, Dock.Right); header.Children.Add(_secretBalance);
        var titles = new StackPanel();
        titles.Children.Add(NightEyebrow("Every season"));
        titles.Children.Add(NightTitle("Item Shop", 40).Also(t => t.Margin = new Thickness(0, 2, 0, 2)));
        titles.Children.Add(NightText("Every cosmetic the item shop has sold, in one place. Outfits come with their matching items. Buy with V-Bucks; it goes straight to your locker.", 13, Night.Sub).Also(t => t.TextWrapping = TextWrapping.Wrap));
        header.Children.Add(titles);
        layout.Children.Add(header);

        var tools = new StackPanel { Margin = new Thickness(0, 0, 0, 10) };
        _secretSearch = new TextBox { Height = 40, MaxLength = 60, ToolTip = "Search by name, for example Raptor or Skull" };
        NightField(_secretSearch);
        var searchHint = NightText("Search skins, pickaxes, gliders…", 13, Night.Muted).Also(t => { t.IsHitTestVisible = false; t.Margin = new Thickness(12, 0, 0, 0); t.VerticalAlignment = VerticalAlignment.Center; });
        var searchBox = new Grid { MaxWidth = 520, HorizontalAlignment = HorizontalAlignment.Left, Margin = new Thickness(0, 0, 0, 10) };
        searchBox.Children.Add(_secretSearch); searchBox.Children.Add(searchHint);
        _secretSearch.TextChanged += (_, __) =>
        {
            searchHint.Visibility = string.IsNullOrEmpty(_secretSearch.Text) ? Visibility.Visible : Visibility.Collapsed;
            _secretSearchDelay ??= new DispatcherTimer { Interval = TimeSpan.FromMilliseconds(180) };
            _secretSearchDelay.Stop(); _secretSearchDelay.Tick -= SecretSearchTick; _secretSearchDelay.Tick += SecretSearchTick; _secretSearchDelay.Start();
        };
        tools.Children.Add(searchBox);
        var chips = new WrapPanel();
        foreach (var kind in new[] { "All", "Outfit", "Back bling", "Pickaxe", "Glider", "Emote", "Loading screen", "Owned" })
        {
            var chip = NightChip(kind == "All" || kind == "Owned" ? kind : kind + "s");
            chip.Tag = kind;
            chip.Click += (_, __) => { _secretType = kind; foreach (var c in _secretChips) SetChip(c, Equals(c.Tag, _secretType)); DrawSecretShop(); };
            SetChip(chip, kind == _secretType); _secretChips.Add(chip); chips.Children.Add(chip);
        }
        tools.Children.Add(chips);
        _secretStatus = NightText("", 13, Night.Sub).Also(t => { t.Margin = new Thickness(0, 2, 0, 0); t.TextWrapping = TextWrapping.Wrap; });
        tools.Children.Add(_secretStatus);
        Grid.SetRow(tools, 1); layout.Children.Add(tools);

        var scroll = new ScrollViewer { VerticalScrollBarVisibility = ScrollBarVisibility.Auto, HorizontalScrollBarVisibility = ScrollBarVisibility.Disabled };
        scroll.Resources.Add(typeof(ScrollBar), DarkScrollBar());
        _secretGrid = new WrapPanel { Margin = new Thickness(0, 6, 0, 10) };
        scroll.Content = _secretGrid;
        Grid.SetRow(scroll, 2); layout.Children.Add(scroll);
        return page;
    }

    private void SecretSearchTick(object? sender, EventArgs e) { _secretSearchDelay?.Stop(); DrawSecretShop(); }

    private void OpenSecretShop()
    {
        if (_secretShopView == null) return;
        FortniteTab_Click(this, new RoutedEventArgs());
        SettingsPanel.Visibility = Visibility.Collapsed;
        HighlightRail(5);
        _secretShopView.Visibility = Visibility.Visible;
        SlideIn(_secretShopView, 0, 110);
        _ = RefreshSecretShop();
    }

    private async Task RefreshSecretShop()
    {
        if (_secretGrid == null || _secretBusy) return;
        if (!_account.SignedIn) { _secretItems = Array.Empty<JsonElement>(); SecretMessage("Sign in to a player to use the shop.", Night.Sub); _secretGrid.Children.Clear(); return; }
        _secretBusy = true;
        try
        {
            if (_secretItems.Length == 0) SecretMessage("Loading the shop…", Night.Sub);
            var data = await _account.SecretShop();
            SecretMessage("", Night.Sub);
            ApplySecretShop(data);
        }
        catch (Exception error) { SecretMessage("Could not load the shop: " + error.Message, Night.Danger); }
        finally { _secretBusy = false; }
    }

    private void ApplySecretShop(JsonElement data)
    {
        if (data.TryGetProperty("items", out var items) && items.ValueKind == JsonValueKind.Array) _secretItems = items.EnumerateArray().Select(i => i.Clone()).ToArray();
        if (_secretBalance != null && data.TryGetProperty("balance", out var b) && b.ValueKind == JsonValueKind.Number) _secretBalance.Text = $"{b.GetInt64():N0} V-Bucks";
        DrawSecretShop();
    }

    private void SecretMessage(string text, string color) { if (_secretStatus == null) return; _secretStatus.Text = text; _secretStatus.Foreground = Hex(color); }

    private static string Str(JsonElement e, string name) => e.TryGetProperty(name, out var v) && v.ValueKind == JsonValueKind.String ? v.GetString() ?? "" : "";

    private void DrawSecretShop()
    {
        if (_secretGrid == null) return;
        string query = (_secretSearch?.Text ?? "").Trim();
        IEnumerable<JsonElement> list = _secretItems;
        if (_secretType == "Owned") list = list.Where(i => i.TryGetProperty("owned", out var o) && o.ValueKind == JsonValueKind.True);
        else if (_secretType != "All") list = list.Where(i => Str(i, "type") == _secretType);
        if (query.Length > 0) list = list.Where(i => Str(i, "name").Contains(query, StringComparison.OrdinalIgnoreCase) || Str(i, "id").Contains(query, StringComparison.OrdinalIgnoreCase));
        var matches = list.ToList();
        const int Shown = 150;
        _secretGrid.Children.Clear();
        foreach (var item in matches.Take(Shown)) _secretGrid.Children.Add(SecretCard(item));
        string current = _secretStatus?.Text ?? "";
        if (_secretItems.Length > 0 && (current.Length == 0 || current.StartsWith("Showing") || current.StartsWith("No cosmetics")))
            SecretMessage(matches.Count == 0 ? "No cosmetics match that." : matches.Count > Shown ? $"Showing {Shown} of {matches.Count:N0}. Search to narrow it down." : $"Showing {matches.Count:N0}.", Night.Sub);
    }

    private Border SecretCard(JsonElement item)
    {
        string id = Str(item, "id"), name = Str(item, "name"), type = Str(item, "type");
        long price = item.TryGetProperty("price", out var p) && p.ValueKind == JsonValueKind.Number ? p.GetInt64() : 0;
        bool owned = item.TryGetProperty("owned", out var o) && o.ValueKind == JsonValueKind.True;
        var body = new StackPanel();
        body.Children.Add(NightText(type.ToUpperInvariant(), 10.5, Night.Muted, bold: true));
        body.Children.Add(NightTitle(name, 20).Also(t => { t.TextWrapping = TextWrapping.Wrap; t.Margin = new Thickness(0, 2, 0, 2); t.MinHeight = 48; }));
        var extras = item.TryGetProperty("items", out var parts) && parts.ValueKind == JsonValueKind.Array ? parts.EnumerateArray().Skip(1).Select(x => Str(x, "type").ToLowerInvariant()).Where(x => x.Length > 0).ToList() : new List<string>();
        body.Children.Add(NightText(extras.Count > 0 ? "With its " + string.Join(", ", extras) : " ", 11.5, Night.Sub).Also(t => { t.TextWrapping = TextWrapping.Wrap; t.Margin = new Thickness(0, 0, 0, 10); }));
        var buy = new Button { Height = 36, HorizontalAlignment = HorizontalAlignment.Stretch };
        if (owned) { buy.Content = "OWNED"; NightButton(buy, "ghost"); buy.IsEnabled = false; }
        else
        {
            bool confirming = _secretConfirm == id;
            buy.Content = confirming ? $"CONFIRM {price:N0}" : $"{price:N0} V-BUCKS";
            NightButton(buy, confirming ? "gold" : "base");
            buy.Click += async (_, __) =>
            {
                if (_secretConfirm != id) { _secretConfirm = id; DrawSecretShop(); return; }   // first click asks, second click buys
                _secretConfirm = null; buy.IsEnabled = false; buy.Content = "BUYING…";
                try
                {
                    var result = await _account.BuySecretShop(id);
                    ApplySecretShop(result);
                    SecretMessage(Str(result, "message").Length > 0 ? Str(result, "message") : name + " bought.", Night.Good);
                    SetStatus(name + " bought. It shows in your locker next time the lobby loads.");
                }
                catch (Exception error) { SecretMessage(error.Message, Night.Danger); DrawSecretShop(); }
            };
        }
        body.Children.Add(buy);
        return new Border
        {
            Width = 212, Margin = new Thickness(0, 0, 12, 12), Padding = new Thickness(16, 14, 16, 14), CornerRadius = TiltCorners(14),
            Background = Hex(Night.Frost), BorderBrush = Hex(owned ? Night.Line : Night.FrostEdge), BorderThickness = new Thickness(1), Child = body
        };
    }
}