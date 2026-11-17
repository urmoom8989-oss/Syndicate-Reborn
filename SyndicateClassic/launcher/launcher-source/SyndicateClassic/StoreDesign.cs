// Syndicate © 2026 Syndicate. All rights reserved. Proprietary — no copying, distribution, or derivative works. See LICENSE.
using System;
using System.Collections.Generic;
using System.Linq;
using System.Text.Json;
using System.Text.RegularExpressions;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Controls.Primitives;
using System.Windows.Input;
using System.Windows.Media;
using System.Windows.Media.Animation;
using System.Windows.Media.Effects;
using System.Windows.Media.Imaging;

namespace SyndicateClassic;

// The store, built like the season browser: two tabs (BATTLE PASS, EXCLUSIVE PACKS), each one row of
// cards on a gentle arc (earliest first), with left/right arrows, a draggable slider, mouse wheel and
// arrow keys. The selected item fills the details panel above the row. No chapter dropdown: battle
// passes of every chapter in the data sit in one chronological row.
// OWNED comes only from the backend's per-account "owned" flag (/launcher store data); nothing is
// marked owned by default, and the badge updates whenever the store data refreshes (every 20 s and
// after a purchase).
public partial class MainWindow
{
    private Grid? _storeRoot;
    private Grid? _storeBody;
    private JsonElement _storeData;
    private string? _storeSelection;           // id shown in the details panel
    private Action? _storeTabSync;
    private Border? _storeTabIndicator;
    private readonly List<Button> _storeTabButtons = new();
    private readonly Dictionary<bool, string> _storePicked = new();   // last selected id per tab
    private Canvas? _storeTimeline;
    private TimelinePosition? _storePosition;
    private readonly List<(Border Card, string Id)> _storeCards = new();
    private string _storeCardKey = "";
    private Border? _storeThumb;
    private bool _storeDragging;
    private Grid? _storeDetails;
    private const double StoreCardWidth = 172, StoreCardHeight = 236, StoreSpacing = 156;

    private static SolidColorBrush StoreInk(string hex) => (SolidColorBrush)new BrushConverter().ConvertFromString(hex)!;
    private static TextBlock StoreText(string text, double size = 14, string color = "#EAF0FF") => new TextBlock { Text = text, FontSize = size, Foreground = StoreInk(color), TextWrapping = TextWrapping.Wrap, Margin = new Thickness(0, 0, 0, 10) };
    private static string PackRelease(string id) => id switch { "rogue-agent" => "March 27, 2018", "wingman" => "June 12, 2018", "blue-team-leader" => "February 14, 2018", "blue-striker" => "June 12, 2018", "trailblazer" => "2018", "sun-strider" => "2018", "reflex" => "2018", "instinct" => "2018", "frostbite" => "2019", "red-strike" => "2019", _ => "" };
    private static bool StoreExclusive(JsonElement item) => item.TryGetProperty("exclusive", out var x) && x.ValueKind == JsonValueKind.True;
    private static bool StorePack(JsonElement item) => item.TryGetProperty("kind", out var k) && k.GetString() == "pack";
    private static bool StoreOwned(JsonElement item) => item.TryGetProperty("owned", out var o) && o.ValueKind == JsonValueKind.True;
    private static string StoreTitle(JsonElement item) => item.GetProperty("title").GetString()!.Split(new[] { '•', '—' })[0].Trim();
    // The store's chapter label/detail color follows the same per-season accent as the
    // play menu's season cards.
    private static string StorePassAccent((int Chapter, int Season) pass)
    {
        if (pass.Chapter == 2)
            return pass.Season >= 3 ? Chapter2Accent(pass.Season) : SeasonAccent(10 + pass.Season);
        int launcherSeason = pass.Chapter == 1 ? pass.Season : pass.Chapter == 3 ? 18 + pass.Season : -1;
        return launcherSeason >= 0 ? SeasonAccent(launcherSeason) : "#9ABEFF";
    }
    private static string StoreAccent(string id)
    {
        if (PassSeason(id) is { } pass) return StorePassAccent(pass);
        return id switch { "rogue-agent" => "#B19BFF", "wingman" => "#FFAC86", "blue-team-leader" => "#75CFFF", "blue-striker" => "#91A9FF", "red-strike" => "#FF6B7A", _ => "#9ABEFF" };
    }
    private static readonly string[][] StoreRarity = { new[] { "#5FD1FF", "#1F7FD8" }, new[] { "#C77DFF", "#6A1FC4" }, new[] { "#FFB547", "#E0581B" }, new[] { "#FF7AC8", "#B4237B" }, new[] { "#68E3A0", "#178A5A" } };

    // "season4" = Chapter 1 Season 4; "c2s3" (or "chapter2-season3") = Chapter 2 Season 3. Null for packs.
    private static (int Chapter, int Season)? PassSeason(string id)
    {
        var m = Regex.Match(id, @"^(?:c|chapter)(\d+)-?s(?:eason)?(\d+)$", RegexOptions.IgnoreCase);
        if (m.Success) return (int.Parse(m.Groups[1].Value), int.Parse(m.Groups[2].Value));
        m = Regex.Match(id, @"^season(\d+)$", RegexOptions.IgnoreCase);
        return m.Success ? (1, int.Parse(m.Groups[1].Value)) : null;
    }
    private static int? StoreLauncherSeason((int Chapter, int Season) pass)
    {
        int number = pass.Chapter == 1 ? pass.Season : pass.Chapter == 2 ? 10 + pass.Season : pass.Chapter == 3 ? 18 + pass.Season : 0;
        return ModernSeasons.Any(s => s.Number == number) ? number : null;
    }
    private static string SeasonWord(int season) => season == 10 ? "X" : season.ToString();
    // "BATTLE PASS — CHAPTER 1, SEASON 4" or "EXCLUSIVE PACK — RED STRIKE PACK"
    private static string StoreHeadline(JsonElement item)
    {
        var id = item.GetProperty("id").GetString()!;
        if (!StorePack(item)) { var s = PassSeason(id); return s is { } p ? $"BATTLE PASS — CHAPTER {p.Chapter}, SEASON {SeasonWord(p.Season)}" : "BATTLE PASS"; }
        return (StoreExclusive(item) ? "EXCLUSIVE PACK" : id.StartsWith("blue") ? "PLAYSTATION PLUS PACK" : "STARTER PACK") + " — " + StoreTitle(item).ToUpperInvariant();
    }
    private static string StoreKind(JsonElement item) => !StorePack(item) ? "Battle pass" : StoreExclusive(item) ? "Exclusive" : item.GetProperty("id").GetString()!.StartsWith("blue") ? "PlayStation Plus" : "Starter pack";

    // Pictures embedded from pack-art (SyndicateClassic.Pack.<name>), decoded once.
    private static readonly Dictionary<string, ImageSource?> StoreImages = new();
    private static ImageSource? StoreImage(string name)
    {
        if (StoreImages.TryGetValue(name, out var cached)) return cached;
        ImageSource? image = null;
        try
        {
            using var stream = typeof(MainWindow).Assembly.GetManifestResourceStream("SyndicateClassic.Pack." + name);
            if (stream != null) { var b = new BitmapImage(); b.BeginInit(); b.CacheOption = BitmapCacheOption.OnLoad; b.DecodePixelWidth = name == "vbucks" ? 96 : 900; b.StreamSource = stream; b.EndInit(); b.Freeze(); image = b; }
        }
        catch { }
        return StoreImages[name] = image;
    }

    // "[coin] 1,250" (pack-art\vbucks.png; without it the word V-BUCKS is shown instead).
    private static FrameworkElement VBucks(string amount, double size, string color)
    {
        var row = new StackPanel { Orientation = Orientation.Horizontal, HorizontalAlignment = HorizontalAlignment.Center, VerticalAlignment = VerticalAlignment.Center };
        var coin = StoreImage("vbucks");
        if (coin != null) row.Children.Add(new Image { Source = coin, Width = size * 1.05, Height = size * 1.05, Margin = new Thickness(0, 0, size * .3, 0), VerticalAlignment = VerticalAlignment.Center });
        row.Children.Add(Display(coin != null ? amount : amount + " V-BUCKS", size, color).Also(t => t.VerticalAlignment = VerticalAlignment.Center));
        return row;
    }

    // OWNED badge: a green pill with a check mark, used on cards and in the details panel.
    private static Border OwnedBadge(double size) => new()
    {
        Background = StoreInk("#E62FB872"), CornerRadius = new CornerRadius(size), Padding = new Thickness(size * .7, size * .25, size * .9, size * .25),
        BorderBrush = StoreInk("#8CFFFFFF"), BorderThickness = new Thickness(1),
        Effect = new DropShadowEffect { Color = Colors.Black, BlurRadius = 10, ShadowDepth = 2, Opacity = .45 },
        Child = new StackPanel { Orientation = Orientation.Horizontal, Children = {
            GlyphIcon("", size * .9, "#FFFFFF").Also(g => { g.Margin = new Thickness(0, 0, size * .4, 0); g.VerticalAlignment = VerticalAlignment.Center; }),
            Display("OWNED", size * 1.25, "#FFFFFF").Also(t => t.VerticalAlignment = VerticalAlignment.Center) } },
    };

    private Border StoreArt(JsonElement item, int index, bool details = false)
    {
        var id = item.GetProperty("id").GetString()!;
        var colors = StoreRarity[index % StoreRarity.Length];
        var art = new Grid { Background = new LinearGradientBrush((Color)ColorConverter.ConvertFromString(colors[0]), (Color)ColorConverter.ConvertFromString(colors[1]), 90) };
        if (StorePack(item))
        {
            var image = StoreImage(id);
            if (image != null) art.Children.Add(new Image { Source = image, Stretch = Stretch.Uniform, VerticalAlignment = VerticalAlignment.Bottom, Margin = new Thickness(10, 14, 10, 0) });
            else art.Children.Add(Display(StoreExclusive(item) ? "EXCLUSIVE" : "STARTER PACK", details ? 30 : 18).Also(t => { t.HorizontalAlignment = HorizontalAlignment.Center; t.VerticalAlignment = VerticalAlignment.Center; t.Opacity = .85; }));
        }
        else
        {
            string asset = id switch { "season1" => "Season1Pass.png", "season2" => "Fortnitebp.png", "season4" => "season4reference.png", "season5" => "season5.jpg", _ => "Season3Pass.jpg" };
            art.Children.Add(new Border { Background = new ImageBrush(PassArt(id, asset)) { Stretch = Stretch.UniformToFill, Viewbox = new Rect(0, 0, 1, 1), ViewboxUnits = BrushMappingMode.RelativeToBoundingBox } });
        }
        return new Border { Child = art };
    }

    private void EnsureStoreDesign()
    {
        if (_storeRoot != null) return;
        _storeRoot = new Grid { Margin = new Thickness(28, 18, 28, 16) };
        _storeRoot.RowDefinitions.Add(new RowDefinition { Height = GridLength.Auto });
        _storeRoot.RowDefinitions.Add(new RowDefinition());
        _storeRoot.RowDefinitions.Add(new RowDefinition { Height = GridLength.Auto });

        var header = new DockPanel { Margin = new Thickness(0, 0, 0, 12) };
        PassBalance = new TextBlock { FontFamily = DisplayFont, FontStretch = FontStretches.Condensed, FontWeight = FontWeights.Bold, FontSize = 22, Foreground = StoreInk("#15120A"), VerticalAlignment = VerticalAlignment.Center };
        var balanceRow = new StackPanel { Orientation = Orientation.Horizontal };
        if (StoreImage("vbucks") is ImageSource coin) balanceRow.Children.Add(new Image { Source = coin, Width = 26, Height = 26, Margin = new Thickness(0, 0, 8, 0), VerticalAlignment = VerticalAlignment.Center });
        balanceRow.Children.Add(PassBalance);
        var balanceChip = new Border { Child = balanceRow, Background = GoldFill(), CornerRadius = TiltCorners(14), Padding = new Thickness(14, 8, 18, 8), VerticalAlignment = VerticalAlignment.Center, ToolTip = "Your V-Bucks (in-game balance)" };
        DockPanel.SetDock(balanceChip, Dock.Right); header.Children.Add(balanceChip);

        var titles = new StackPanel();
        titles.Children.Add(NightEyebrow("Store"));
        var tabHost = new Grid { Margin = new Thickness(0, 6, 0, 0), HorizontalAlignment = HorizontalAlignment.Left };
        var tabRow = new StackPanel { Orientation = Orientation.Horizontal };
        foreach (var (label, packs) in new[] { ("BATTLE PASS", false), ("EXCLUSIVE PACKS", true) })
        {
            var tab = new Button { Content = Display(label, 30, Night.Sub), Height = 50, Padding = new Thickness(12, 0, 12, 0), Tag = packs };
            RoundButton(tab, "#00000000", Night.Text, 10);
            tab.MouseEnter += (_, __) => { if (_showPacks != packs && tab.Content is TextBlock t) t.Foreground = Hex(Night.Text); };
            tab.MouseLeave += (_, __) => { if (_showPacks != packs && tab.Content is TextBlock t) t.Foreground = Hex(Night.Sub); };
            tab.Click += (_, __) => SelectStoreTab(packs, animate: true);
            tab.SizeChanged += (_, __) => { if (_showPacks == packs) MoveStoreIndicator(false); };
            _storeTabButtons.Add(tab); tabRow.Children.Add(tab);
            if (!packs) tabRow.Children.Add(new Border { Width = 1, Height = 24, Background = Hex(Night.Line), Margin = new Thickness(6, 0, 6, 0), VerticalAlignment = VerticalAlignment.Center });
        }
        tabHost.Children.Add(tabRow);
        _storeTabIndicator = new Border { Width = 0, Height = 3, CornerRadius = new CornerRadius(1.5), Background = GoldFill(), VerticalAlignment = VerticalAlignment.Bottom, HorizontalAlignment = HorizontalAlignment.Left, RenderTransform = new TranslateTransform(), IsHitTestVisible = false,
            Effect = new DropShadowEffect { Color = NightColor("#FFC21A"), BlurRadius = 12, ShadowDepth = 0, Opacity = .7 } };
        tabHost.Children.Add(_storeTabIndicator);
        titles.Children.Add(tabHost);
        header.Children.Add(titles);
        _storeRoot.Children.Add(header);

        _storeBody = new Grid { ClipToBounds = true, Focusable = true, FocusVisualStyle = null }; Grid.SetRow(_storeBody, 1); _storeRoot.Children.Add(_storeBody);
        _storeBody.KeyDown += (_, e) => { if (e.Key == Key.Left) { StoreStep(-1); e.Handled = true; } else if (e.Key == Key.Right) { StoreStep(1); e.Handled = true; } };
        PassMessage = StoreText("", 12, "#B5C3DD"); PassMessage.Margin = new Thickness(0, 10, 0, 0); Grid.SetRow(PassMessage, 2); _storeRoot.Children.Add(PassMessage);
        PassShop.Child = _storeRoot; PassShop.Background = Brushes.Transparent;
        _storeTabSync = SyncStoreTabs;
        PassShop.IsVisibleChanged += (_, __) =>
        {
            SyncStoreTabs(); MoveStoreIndicator(false);
            if (!PassShop.IsVisible) PreviewStoreSeason(null);
            else if (_showPacks) PreviewStoreSeason(null);
            if (PassShop.IsVisible) _storeBody.Focus();
        };
    }

    private void SyncStoreTabs()
    {
        foreach (var tab in _storeTabButtons)
            if (tab.Content is TextBlock label) label.Foreground = Hex((bool)tab.Tag == _showPacks ? Night.Text : Night.Sub);
    }

    private void MoveStoreIndicator(bool animate)
    {
        var tab = _storeTabButtons.FirstOrDefault(t => (bool)t.Tag == _showPacks);
        if (tab == null || _storeTabIndicator?.Parent is not FrameworkElement host || tab.ActualWidth <= 0) return;
        var at = tab.TransformToAncestor(host).Transform(new Point(0, 0));
        var time = TimeSpan.FromMilliseconds(animate && MotionEnabled ? 320 : 0);
        var ease = new CubicEase { EasingMode = EasingMode.EaseInOut };
        ((TranslateTransform)_storeTabIndicator.RenderTransform).BeginAnimation(TranslateTransform.XProperty, new DoubleAnimation(at.X + 10, time) { EasingFunction = ease });
        _storeTabIndicator.BeginAnimation(WidthProperty, new DoubleAnimation(Math.Max(0, tab.ActualWidth - 20), time) { EasingFunction = ease });
    }

    private List<JsonElement> StoreItems(bool packs)
    {
        if (_storeData.ValueKind != JsonValueKind.Object) return new();
        var items = _storeData.GetProperty("passes").EnumerateArray().Where(x => StorePack(x) == packs).ToList();
        // Battle passes: chronological across chapters. Packs: starter packs, then PlayStation, then exclusives (data order inside).
        return packs
            ? items.Select((x, i) => (x, i)).OrderBy(p => StoreExclusive(p.x) ? 2 : p.x.GetProperty("id").GetString()!.StartsWith("blue") ? 1 : 0).ThenBy(p => p.i).Select(p => p.x).ToList()
            : items.Select((x, i) => (x, i)).OrderBy(p => PassSeason(p.x.GetProperty("id").GetString()!)?.Chapter ?? 99).ThenBy(p => PassSeason(p.x.GetProperty("id").GetString()!)?.Season ?? 99).ThenBy(p => p.i).Select(p => p.x).ToList();
    }

    // Tab switch: the whole page slides in from the side the tab sits on.
    private void SelectStoreTab(bool packs, bool animate)
    {
        int direction = packs == _showPacks ? 0 : packs ? 1 : -1;
        _showPacks = packs;
        if (packs) PreviewStoreSeason(null);
        SyncStoreTabs(); MoveStoreIndicator(animate);
        if (_storeData.ValueKind != JsonValueKind.Object) return;
        _storeCardKey = ""; DrawStoreBrowser(selectId: _storePicked.TryGetValue(packs, out var id) ? id : null);
        if (animate && direction != 0 && _storeBody != null && MotionEnabled)
        {
            var move = new TranslateTransform(); _storeBody.RenderTransform = move;
            move.BeginAnimation(TranslateTransform.XProperty, new DoubleAnimation(80 * direction, 0, TimeSpan.FromMilliseconds(360)) { EasingFunction = new CubicEase { EasingMode = EasingMode.EaseOut } });
            _storeBody.BeginAnimation(OpacityProperty, new DoubleAnimation(0, 1, TimeSpan.FromMilliseconds(300)));
        }
        _storeBody?.Focus();
    }

    // Kept for callers elsewhere (rail buttons, packs shortcut).
    private void JumpToStoreSection(string tag, bool animate = true) => SelectStoreTab(tag == "packs", animate);

    private void DrawStore(JsonElement data)
    {
        EnsureStoreDesign(); _storeData = data.Clone();
        PassBalance.Text = $"{data.GetProperty("balance").GetInt32():N0}";
        SyncStoreTabs(); MoveStoreIndicator(false);
        PassMessage.Text = data.TryGetProperty("message", out var message) ? message.GetString() : "Purchases use your in-game V-Bucks. New items appear in your locker on your next Fortnite login.";
        DrawStoreBrowser(selectId: _storePicked.TryGetValue(_showPacks, out var id) ? id : null);
    }

    // Builds (or refreshes) the details panel + row for the current tab. Cards are rebuilt only when the
    // set of items changes; a refresh just updates badges and the details, so nothing jumps.
    private void DrawStoreBrowser(string? selectId)
    {
        if (_storeBody == null) return;
        var items = StoreItems(_showPacks);
        string key = (_showPacks ? "packs|" : "passes|") + string.Join(",", items.Select(i => i.GetProperty("id").GetString() + (StoreOwned(i) ? "+" : "")));
        int selected = Math.Max(0, items.FindIndex(i => i.GetProperty("id").GetString() == selectId));
        if (key != _storeCardKey || _storeTimeline == null || _storePosition == null)
        {
            _storeCardKey = key;
            BuildStoreLayout(items, selected);
        }
        if (items.Count == 0) return;
        ShowStoreItem(items[selected], selected, direction: 0, animate: false);
        _storePosition!.BeginAnimation(TimelinePosition.ValueProperty, null);
        _storePosition.Value = selected; LayoutStoreRow(selected); SyncStoreSlider(selected);
    }

    private void BuildStoreLayout(List<JsonElement> items, int selected)
    {
        _storeBody!.Children.Clear(); _storeCards.Clear();
        if (items.Count == 0) { _storeBody.Children.Add(NightText(_showPacks ? "No packs are available right now." : "No battle passes are available right now.", 14, Night.Sub)); return; }
        var layout = new Grid();
        layout.RowDefinitions.Add(new RowDefinition());                                  // details
        layout.RowDefinitions.Add(new RowDefinition { Height = GridLength.Auto });       // row
        layout.RowDefinitions.Add(new RowDefinition { Height = GridLength.Auto });       // slider
        _storeDetails = new Grid { Margin = new Thickness(0, 0, 0, 10) };
        layout.Children.Add(_storeDetails);

        var row = new Grid { Height = StoreCardHeight + 44 };
        row.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(56) });
        row.ColumnDefinitions.Add(new ColumnDefinition());
        row.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(56) });
        var left = NightRoundIcon("", "Earlier", 46, 16); left.VerticalAlignment = VerticalAlignment.Center; left.HorizontalAlignment = HorizontalAlignment.Left; left.Click += (_, __) => StoreStep(-1);
        var right = NightRoundIcon("", "Later", 46, 16); right.VerticalAlignment = VerticalAlignment.Center; right.HorizontalAlignment = HorizontalAlignment.Right; right.Click += (_, __) => StoreStep(1); Grid.SetColumn(right, 2);
        _storeTimeline = new Canvas { ClipToBounds = true, Background = Brushes.Transparent }; Grid.SetColumn(_storeTimeline, 1);
        row.Children.Add(_storeTimeline); row.Children.Add(left); row.Children.Add(right);
        int wheel = 0;
        _storeTimeline.MouseWheel += (_, e) => { e.Handled = true; if (Math.Sign(e.Delta) != Math.Sign(wheel)) wheel = 0; wheel += e.Delta; if (Math.Abs(wheel) < 120) return; int steps = wheel / 120; wheel -= steps * 120; StoreStep(-steps); };
        _storeTimeline.SizeChanged += (_, __) => LayoutStoreRow(_storePosition?.Value ?? 0);
        Grid.SetRow(row, 1); layout.Children.Add(row);

        // Slider: a track and a draggable gold thumb, following the row.
        var slider = new Grid { Height = 26, Margin = new Thickness(56, 6, 56, 0), Background = Brushes.Transparent, Cursor = Cursors.Hand };
        slider.Children.Add(new Border { Height = 6, CornerRadius = new CornerRadius(3), Background = Hex("#26FFFFFF"), VerticalAlignment = VerticalAlignment.Center });
        _storeThumb = new Border { Height = 14, Width = 60, CornerRadius = new CornerRadius(7), Background = GoldFill(), HorizontalAlignment = HorizontalAlignment.Left, VerticalAlignment = VerticalAlignment.Center, RenderTransform = new TranslateTransform(), Cursor = Cursors.SizeWE };
        slider.Children.Add(_storeThumb);
        slider.MouseLeftButtonDown += (_, e) => { _storeDragging = true; slider.CaptureMouse(); StoreSliderTo(e.GetPosition(slider).X, slider.ActualWidth, live: true); e.Handled = true; };
        slider.MouseMove += (_, e) => { if (_storeDragging) StoreSliderTo(e.GetPosition(slider).X, slider.ActualWidth, live: true); };
        slider.MouseLeftButtonUp += (_, e) => { if (!_storeDragging) return; _storeDragging = false; slider.ReleaseMouseCapture(); StoreSliderTo(e.GetPosition(slider).X, slider.ActualWidth, live: false); e.Handled = true; };
        slider.SizeChanged += (_, __) => SyncStoreSlider(_storePosition?.Value ?? 0);
        Grid.SetRow(slider, 2); layout.Children.Add(slider);

        _storePosition = new TimelinePosition { Visibility = Visibility.Collapsed };
        _storePosition.Changed = value => { LayoutStoreRow(value); SyncStoreSlider(value); };
        layout.Children.Add(_storePosition);

        for (int i = 0; i < items.Count; i++) { var card = StoreCard(items[i], i); _storeCards.Add((card, items[i].GetProperty("id").GetString()!)); _storeTimeline.Children.Add(card); }
        _storeBody.Children.Add(layout);
    }

    private Border StoreCard(JsonElement item, int index)
    {
        var id = item.GetProperty("id").GetString()!;
        var colors = StoreRarity[index % StoreRarity.Length];
        var tile = new Grid();
        tile.Children.Add(StoreArt(item, index));
        var label = new StackPanel { VerticalAlignment = VerticalAlignment.Bottom, Margin = new Thickness(12, 0, 12, 12) };
        var season = PassSeason(id);
        string chapterColor = season is { } pass ? StorePassAccent(pass) : colors[0];
        label.Children.Add(NightText(season is { } s ? $"CHAPTER {s.Chapter}" : StoreKind(item).ToUpperInvariant(), 10, chapterColor, bold: true).Also(t => t.TextWrapping = TextWrapping.NoWrap));
        label.Children.Add(new Viewbox { Child = Display(season is { } p ? $"SEASON {SeasonWord(p.Season)}" : StoreTitle(item).ToUpperInvariant(), 22), StretchDirection = StretchDirection.DownOnly, HorizontalAlignment = HorizontalAlignment.Left });
        var shade = new Border { Background = new LinearGradientBrush(Color.FromArgb(0, 0, 0, 0), Color.FromArgb(225, 4, 6, 14), 90) { StartPoint = new Point(0, .45), EndPoint = new Point(0, 1) } };
        tile.Children.Add(shade); tile.Children.Add(label);
        if (StoreOwned(item)) tile.Children.Add(OwnedBadge(10).Also(b => { b.HorizontalAlignment = HorizontalAlignment.Right; b.VerticalAlignment = VerticalAlignment.Top; b.Margin = new Thickness(8); }));
        else tile.Children.Add(new Border { Background = new SolidColorBrush(Color.FromArgb(200, 8, 10, 22)), CornerRadius = TiltCorners(10), Padding = new Thickness(8, 3, 9, 3), HorizontalAlignment = HorizontalAlignment.Right, VerticalAlignment = VerticalAlignment.Top, Margin = new Thickness(8), Child = VBucks($"{item.GetProperty("price").GetInt32():N0}", 13, "#FFFFFF") });
        tile.Clip = new RectangleGeometry(new Rect(0, 0, StoreCardWidth - 4, StoreCardHeight - 4), 14, 14);
        var card = new Border
        {
            Width = StoreCardWidth, Height = StoreCardHeight, CornerRadius = new CornerRadius(16), Child = tile, BorderThickness = new Thickness(2), BorderBrush = Hex("#33FFFFFF"), Cursor = Cursors.Hand,
            RenderTransformOrigin = new Point(.5, .5), RenderTransform = new TransformGroup { Children = { new ScaleTransform(1, 1), new TranslateTransform() } },
            CacheMode = new BitmapCache { RenderAtScale = 1.25 }, ToolTip = StoreHeadline(item) + (StoreOwned(item) ? "  ·  OWNED" : ""), Tag = index,
        };
        card.MouseEnter += (_, __) => { card.Uid = "hover"; LayoutStoreRow(_storePosition?.Value ?? 0); };
        card.MouseLeave += (_, __) => { card.Uid = ""; LayoutStoreRow(_storePosition?.Value ?? 0); };
        card.MouseLeftButtonDown += (_, e) => { StoreSelect(index); e.Handled = true; }; // handled on press: the window drags on unhandled presses
        return card;
    }

    private int StoreSelectedIndex() => Math.Max(0, _storeCards.FindIndex(c => c.Id == _storeSelection));

    private void StoreStep(int delta) => StoreSelect(StoreSelectedIndex() + delta);

    private void StoreSelect(int index, bool force = false)
    {
        var items = StoreItems(_showPacks);
        if (items.Count == 0 || _storePosition == null) return;
        index = Math.Clamp(index, 0, items.Count - 1);
        int current = StoreSelectedIndex();
        if (index == current && !force && Math.Abs(_storePosition.Value - index) < .001) return;
        int direction = index > current ? 1 : index < current ? -1 : 0;
        double from = _storePosition.Value;
        var time = TimeSpan.FromMilliseconds(MotionEnabled ? 380 + 70 * Math.Min(4, Math.Abs(index - from)) : 0);
        _storePosition.BeginAnimation(TimelinePosition.ValueProperty, new DoubleAnimation(from, index, time) { EasingFunction = new QuinticEase { EasingMode = EasingMode.EaseOut } });
        if (index != current || force) ShowStoreItem(items[index], index, direction, animate: true);
    }

    private void LayoutStoreRow(double position)
    {
        if (_storeTimeline == null || _storeCards.Count == 0) return;
        double width = _storeTimeline.ActualWidth, height = _storeTimeline.ActualHeight;
        if (width <= 0) return;
        double centerX = width / 2, top = Math.Max(4, (height - StoreCardHeight) / 2 - 6), fade = Math.Min(90, width * .08);
        int selected = StoreSelectedIndex();
        for (int i = 0; i < _storeCards.Count; i++)
        {
            var card = _storeCards[i].Card;
            double d = i - position, distance = Math.Abs(d);
            bool hover = card.Uid == "hover";
            double scale = Math.Max(.62, 1 - .12 * distance) * (hover ? 1.04 : 1);
            double x = centerX + d * StoreSpacing * (1 - .06 * Math.Min(distance, 3)) - StoreCardWidth / 2, y = top + Math.Min(distance * distance * 7, 56);
            if (card.RenderTransform is TransformGroup g && g.Children[0] is ScaleTransform s && g.Children[1] is TranslateTransform t) { s.ScaleX = s.ScaleY = scale; t.X = x; t.Y = y; }
            double middle = x + StoreCardWidth / 2, edge = Math.Min(middle, width - middle);
            double opacity = Math.Max(0, Math.Min(1, 1.15 - .22 * distance)) * Math.Clamp(edge / fade, 0, 1);
            card.Opacity = opacity;
            var visibility = opacity <= .01 ? Visibility.Hidden : Visibility.Visible; if (card.Visibility != visibility) card.Visibility = visibility;
            int z = 1000 - (int)Math.Round(distance * 4); if (Panel.GetZIndex(card) != z) Panel.SetZIndex(card, z);
            var edgeBrush = i == selected ? Night.Gold : hover ? "#80FFFFFF" : "#33FFFFFF";
            if (!(card.BorderBrush is SolidColorBrush b && b.Color == NightColor(edgeBrush))) card.BorderBrush = Hex(edgeBrush);
        }
    }

    private void SyncStoreSlider(double position)
    {
        if (_storeThumb?.Parent is not FrameworkElement host || _storeCards.Count == 0 || host.ActualWidth <= 0) return;
        double thumb = Math.Max(44, host.ActualWidth / _storeCards.Count * 1.4);
        if (Math.Abs(_storeThumb.Width - thumb) > .5) _storeThumb.Width = thumb;
        double fraction = _storeCards.Count <= 1 ? 0 : Math.Clamp(position / (_storeCards.Count - 1), 0, 1);
        ((TranslateTransform)_storeThumb.RenderTransform).X = Math.Max(0, host.ActualWidth - _storeThumb.Width) * fraction;
    }

    private void StoreSliderTo(double x, double width, bool live)
    {
        if (_storeCards.Count == 0 || width <= 0 || _storePosition == null || _storeThumb == null) return;
        double fraction = Math.Clamp((x - _storeThumb.Width / 2) / Math.Max(1, width - _storeThumb.Width), 0, 1);
        double position = fraction * (_storeCards.Count - 1);
        if (live)
        {
            _storePosition.BeginAnimation(TimelinePosition.ValueProperty, null); _storePosition.Value = position;
            int nearest = (int)Math.Round(position);
            if (nearest != StoreSelectedIndex()) { var items = StoreItems(_showPacks); int direction = nearest > StoreSelectedIndex() ? 1 : -1; ShowStoreItem(items[nearest], nearest, direction, animate: false); }
        }
        else StoreSelect((int)Math.Round(position), force: true);
    }

    private static string RewardGlyph(string category) => category.ToLowerInvariant() switch
    {
        var c when c.Contains("outfit") => "", var c when c.Contains("glider") => "", var c when c.Contains("pickaxe") || c.Contains("harvest") => "",
        var c when c.Contains("back") => "", var c when c.Contains("emote") || c.Contains("dance") => "", var c when c.Contains("v-buck") || c.Contains("currency") => "",
        var c when c.Contains("wrap") || c.Contains("spray") => "", _ => "",
    };

    // Details panel for the selected item: art card, headline, OWNED or price + buy, contents.
    private void ShowStoreItem(JsonElement item, int index, int direction, bool animate)
    {
        if (_storeDetails == null) return;
        var id = item.GetProperty("id").GetString()!;
        _storeSelection = id; _storePicked[_showPacks] = id;
        int price = item.GetProperty("price").GetInt32(), balance = _storeData.GetProperty("balance").GetInt32();
        bool owned = StoreOwned(item);
        // Season Shop bundles unlock at a season level, like the shop in game (backend "minLevel"/"locked").
        int minLevel = item.TryGetProperty("minLevel", out var ml) && ml.ValueKind == JsonValueKind.Number ? ml.GetInt32() : 0;
        int playerLevel = _storeData.TryGetProperty("level", out var pl) && pl.ValueKind == JsonValueKind.Number ? pl.GetInt32() : 0;
        bool locked = item.TryGetProperty("locked", out var lk) && lk.ValueKind == JsonValueKind.True;
        string accent = StoreAccent(id);
        _storeDetails.Children.Clear(); _storeDetails.ColumnDefinitions.Clear();
        _storeDetails.ColumnDefinitions.Add(new ColumnDefinition { Width = GridLength.Auto });
        _storeDetails.ColumnDefinitions.Add(new ColumnDefinition());
        _storeDetails.ColumnDefinitions.Add(new ColumnDefinition { Width = GridLength.Auto }); // price + buy, on the right

        // Art card (sized by the panel's height).
        var art = new Border { CornerRadius = TiltCorners(18), BorderBrush = Hex(owned ? "#5C2FB872" : Night.FrostEdge), BorderThickness = new Thickness(2), Margin = new Thickness(0, 0, 26, 0), VerticalAlignment = VerticalAlignment.Stretch };
        var artGrid = new Grid(); artGrid.Children.Add(StoreArt(item, index, details: true));
        if (owned) artGrid.Children.Add(OwnedBadge(13).Also(b => { b.HorizontalAlignment = HorizontalAlignment.Left; b.VerticalAlignment = VerticalAlignment.Top; b.Margin = new Thickness(12); }));
        art.Child = artGrid;
        art.SizeChanged += (_, __) => { if (art.ActualHeight > 0) { art.Width = Math.Clamp(art.ActualHeight * .78, 180, 380); artGrid.Clip = new RectangleGeometry(new Rect(0, 0, art.Width - 4, art.ActualHeight - 4), 16, 16); } };
        _storeDetails.Children.Add(art);

        var info = new StackPanel { RenderTransform = new TranslateTransform() };
        info.Children.Add(NightText(StoreHeadline(item), 12, accent, bold: true).Also(t => t.TextWrapping = TextWrapping.Wrap));
        var titleRow = new WrapPanel { Margin = new Thickness(0, 4, 0, 6) };
        var season = PassSeason(id);
        // Only the Battle Pass tab previews its season behind the store. Packs keep the Home/Play backdrop.
        PreviewStoreSeason(!_showPacks && season is { } selectedPass ? StoreLauncherSeason(selectedPass) : null);
        titleRow.Children.Add(new Viewbox { Child = Display(season is { } s ? $"SEASON {SeasonWord(s.Season)}" : StoreTitle(item).ToUpperInvariant(), 54), StretchDirection = StretchDirection.DownOnly, MaxWidth = 620, Margin = new Thickness(0, 0, 14, 0) });
        if (owned) titleRow.Children.Add(OwnedBadge(14).Also(b => b.VerticalAlignment = VerticalAlignment.Center));
        info.Children.Add(titleRow);
        if (StorePack(item) && PackRelease(id).Length > 0) info.Children.Add(NightText("Originally released " + PackRelease(id), 12.5, Night.Sub).Also(t => t.Margin = new Thickness(0, 0, 0, 6)));
        info.Children.Add(Body(item.GetProperty("description").GetString()!, 14, "#C9CFE6").Also(t => { t.LineHeight = 21; t.Margin = new Thickness(0, 0, 0, 14); t.MaxWidth = 680; t.HorizontalAlignment = HorizontalAlignment.Left; }));

        // Price + buy (or OWNED) in their own column on the right, the price right above the button.
        var buyRow = new StackPanel { Width = 240, Margin = new Thickness(24, 0, 4, 0), VerticalAlignment = VerticalAlignment.Center, HorizontalAlignment = HorizontalAlignment.Right };
        var status = NightText("", 12, Night.Sub).Also(t => { t.TextWrapping = TextWrapping.Wrap; t.Margin = new Thickness(0, 8, 0, 0); t.TextAlignment = TextAlignment.Center; });
        if (owned)
            buyRow.Children.Add(NightText("In your locker. It loads on your next Fortnite login.", 13.5, Night.Good, bold: true).Also(t => { t.TextWrapping = TextWrapping.Wrap; t.TextAlignment = TextAlignment.Center; }));
        else
        {
            var priceTag = VBucks($"{price:N0}", 26, Night.Text); priceTag.HorizontalAlignment = HorizontalAlignment.Center;
            buyRow.Children.Add(new Border { Background = StoreInk("#141A33"), BorderBrush = Hex(Night.Line), BorderThickness = new Thickness(1), CornerRadius = TiltCorners(12), Padding = new Thickness(16, 8, 18, 8), Margin = new Thickness(0, 0, 0, 10), Child = priceTag });
            var buy = new Button { Content = locked ? $"SEASON LEVEL {minLevel} NEEDED" : balance < price ? "NOT ENOUGH V-BUCKS" : "BUY NOW", Height = 50, Padding = new Thickness(18, 0, 18, 0), FontSize = locked ? 16 : 19, IsEnabled = !locked && balance >= price, HorizontalAlignment = HorizontalAlignment.Stretch };
            NightButton(buy, "gold", 12);
            buy.Click += async (_, __) =>
            {
                if (_shopBusy) return; _shopBusy = true; _storeRoot!.IsEnabled = false; buy.Content = "BUYING…";
                try { var result = await _account.BuyPass(id); DrawStore(result); }
                catch (Exception ex) { status.Text = ex.Message; status.Foreground = Hex(Night.Danger); buy.Content = "TRY AGAIN"; }
                finally { _shopBusy = false; _storeRoot.IsEnabled = true; }
            };
            buyRow.Children.Add(buy);
            if (minLevel > 0) buyRow.Children.Add(NightText(locked ? $"Requires Season Level {minLevel}. You are level {playerLevel}." : $"Requires Season Level {minLevel}. You are level {playerLevel}, unlocked.", 12.5, locked ? "#FFC06A" : Night.Good, bold: true).Also(t => { t.TextWrapping = TextWrapping.Wrap; t.TextAlignment = TextAlignment.Center; t.Margin = new Thickness(0, 8, 0, 0); }));
            buyRow.Children.Add(NightText(balance >= price ? $"{balance - price:N0} V-Bucks left after" : $"You need {price - balance:N0} more", 12.5, balance >= price ? Night.Sub : "#FF9B9B").Also(t => { t.HorizontalAlignment = HorizontalAlignment.Center; t.Margin = new Thickness(0, 8, 0, 0); }));
        }
        buyRow.Children.Add(status);
        Grid.SetColumn(buyRow, 2); _storeDetails.Children.Add(buyRow);

        var rewards = item.GetProperty("rewards").EnumerateArray().ToList();
        // A pass holds 100+ rewards: listing every one made the text tiny. Show a summary instead: one chip per
        // kind ("12 Outfits", "1,300 V-Bucks"; hover lists the names), then the outfits by name.
        info.Children.Add(NightText($"WHAT'S INSIDE  ·  {rewards.Count} ITEMS", 12, Night.Muted, bold: true).Also(t => t.Margin = new Thickness(0, 2, 0, 10)));
        string[] order = { "Outfits", "Back blings", "Pickaxes", "Gliders", "Emotes", "Contrails", "Wraps", "Loading screens", "Banners", "Boosts", "V-Bucks", "Cosmetics" };
        var groups = rewards.GroupBy(r => r.GetProperty("category").GetString() ?? "Cosmetics")
            .OrderBy(g => { int i = Array.IndexOf(order, g.Key); return i < 0 ? order.Length : i; }).ToList();
        var tiles = new WrapPanel { MaxWidth = 700, HorizontalAlignment = HorizontalAlignment.Left };
        foreach (var g in groups)
        {
            int count = g.Count();
            string label = g.Key == "V-Bucks"
                ? $"{g.Sum(r => Math.Max(1, r.GetProperty("quantity").GetInt32())):N0} V-Bucks"
                : $"{count} {(count == 1 && g.Key.EndsWith("s") ? g.Key[..^1] : g.Key)}";
            var names = string.Join("\n", g.Select(r => r.GetProperty("name").GetString()).Where(n => !string.IsNullOrWhiteSpace(n)).Distinct().Take(40));
            var chip = new StackPanel { Orientation = Orientation.Horizontal };
            chip.Children.Add(GlyphIcon(RewardGlyph(g.Key), 15, accent).Also(x => { x.Margin = new Thickness(0, 0, 9, 0); x.VerticalAlignment = VerticalAlignment.Center; }));
            chip.Children.Add(NightText(label, 14.5, Night.Text, bold: true).Also(t => { t.TextWrapping = TextWrapping.NoWrap; t.VerticalAlignment = VerticalAlignment.Center; }));
            tiles.Children.Add(new Border { Child = chip, Background = StoreInk("#10152B"), BorderBrush = Hex(Night.Line), BorderThickness = new Thickness(1), CornerRadius = TiltCorners(12), Padding = new Thickness(12, 8, 14, 8), Margin = new Thickness(0, 0, 10, 10), ToolTip = names.Length > 0 ? names : g.Key });
        }
        info.Children.Add(tiles);
        var leveled = rewards.Where(r => r.TryGetProperty("level", out var lv) && lv.ValueKind == JsonValueKind.Number).OrderBy(r => r.GetProperty("level").GetInt32()).ToList();
        if (leveled.Count > 0)
        {
            info.Children.Add(NightText("SEASON SHOP LEVELS", 11, Night.Muted, bold: true).Also(t => t.Margin = new Thickness(0, 6, 0, 6)));
            var levelRow = new WrapPanel { MaxWidth = 700, HorizontalAlignment = HorizontalAlignment.Left };
            foreach (var r in leveled)
            {
                int need = r.GetProperty("level").GetInt32();
                var cell = new StackPanel();
                cell.Children.Add(NightText(r.GetProperty("name").GetString() ?? "", 13.5, Night.Text, bold: true).Also(t => t.TextWrapping = TextWrapping.NoWrap));
                cell.Children.Add(NightText($"Season Level {need}", 12, playerLevel >= need ? Night.Good : "#FFC06A"));
                levelRow.Children.Add(new Border { Child = cell, Background = StoreInk("#10152B"), BorderBrush = Hex(Night.Line), BorderThickness = new Thickness(1), CornerRadius = TiltCorners(12), Padding = new Thickness(12, 7, 14, 8), Margin = new Thickness(0, 0, 8, 8) });
            }
            info.Children.Add(levelRow);
        }
        var outfits = rewards.Where(r => r.GetProperty("category").GetString() == "Outfits").Select(r => r.GetProperty("name").GetString()).Where(n => !string.IsNullOrWhiteSpace(n)).Distinct().ToList();
        if (outfits.Count > 0)
        {
            const int Shown = 8;
            var line = string.Join("  ·  ", outfits.Take(Shown)) + (outfits.Count > Shown ? $"  ·  and {outfits.Count - Shown} more" : "");
            info.Children.Add(NightText("OUTFITS", 11, Night.Muted, bold: true).Also(t => t.Margin = new Thickness(0, 6, 0, 4)));
            info.Children.Add(Body(line, 13.5, "#C9CFE6").Also(t => { t.LineHeight = 20; t.MaxWidth = 680; t.HorizontalAlignment = HorizontalAlignment.Left; }));
        }
        // No scrolling: the text column takes the panel's width and, when a long reward list would
        // not fit the height, the whole column scales down a little instead.
        var infoHost = new Grid { ClipToBounds = true };
        var fit = new Viewbox { Child = info, Stretch = Stretch.Uniform, StretchDirection = StretchDirection.DownOnly, VerticalAlignment = VerticalAlignment.Center, HorizontalAlignment = HorizontalAlignment.Left };
        infoHost.Children.Add(fit);
        infoHost.SizeChanged += (_, __) => { if (infoHost.ActualWidth > 0) info.Width = infoHost.ActualWidth; };
        Grid.SetColumn(infoHost, 1); _storeDetails.Children.Add(infoHost);

        if (animate && direction != 0 && MotionEnabled)
        {
            var ease = new CubicEase { EasingMode = EasingMode.EaseOut };
            ((TranslateTransform)info.RenderTransform).BeginAnimation(TranslateTransform.XProperty, new DoubleAnimation(-50 * direction, 0, TimeSpan.FromMilliseconds(360)) { EasingFunction = ease });
            info.BeginAnimation(OpacityProperty, new DoubleAnimation(0, 1, TimeSpan.FromMilliseconds(300)));
            art.BeginAnimation(OpacityProperty, new DoubleAnimation(.3, 1, TimeSpan.FromMilliseconds(380)));
        }
        LayoutStoreRow(_storePosition?.Value ?? index);
    }
}