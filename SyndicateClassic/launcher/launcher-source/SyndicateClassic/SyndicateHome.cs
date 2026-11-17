// Syndicate © 2026 Syndicate. All rights reserved. Proprietary — no copying, distribution, or derivative works. See LICENSE.
using System;
using System.Collections.Generic;
using System.Linq;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Controls.Primitives;
using System.Windows.Media;
using System.Windows.Media.Animation;
using System.Text.Json;
using System.Windows.Media.Effects;
using System.Windows.Threading;
namespace SyndicateClassic;

// The launcher's front page and side menu, in the "Syndicate" look (NightDrop.cs): everything leans.
//   * Side menu (232 px): logo, numbered pages (01 Home … 06 Settings) with a leaning gold bar on the one you are on,
//     Discord and the log, then the selected season, your avatar, updates and version.
//   * Home: no box around it. The name leans across the season's art ("PROJECT" over a huge gold "SYNDICATE"), the
//     season's own number sits huge and faint behind it, and LAUNCH is a gold parallelogram. On the right a stack of
//     leaning tiles: player, build, and shortcuts. Below, the chapters as parallelograms; one opens the season
//     browser (SeasonBrowser.cs) at that chapter.
// Every button reuses the existing launch / folder / settings / profile logic; this file only lays it out.
public partial class MainWindow
{
    private const double RailWidth = 232;
    private const int PlayTab = 6;   // HighlightRail index of the season browser (Home is 0)

    private Grid? _syndicateHomeView;
    private Border? _homeGlow;
    private TextBlock? _homeWatermark, _homeTag, _homeTagline, _homeDescription, _homeStatusText, _homeHint;
    private TextBlock? _homePlayerName, _homePlayerSub, _homeBuildNumber, _homeBuildSeason, _homeBuildState;
    private TextBlock? _homeLevelText, _homeXpText, _homeFriendsText;
    private Grid? _homeXpBar;
    private Border? _homeStatusDot, _homeTagBox, _homePlayerFace;
    private Button? _homeLaunch, _homePlayerEdit;
    private TextBlock? _homeLaunchText;
    private TextBlock? _railSeasonTitle, _railSeasonBuild;
    private Border? _railSeasonArt;
    private TextBlock? _railPlayerName;
    private readonly Dictionary<int, Button> _navByIndex = new();
    private int _homeShownSeason = -1;
    private string? _homeShownPlayer = "\0";
    private DispatcherTimer? _homeTimer;

    // A leaning tile: rounded top-left and bottom-right, lit edge, a gold notch on its top edge.
    private static Border TiltTile(UIElement child, Thickness padding)
    {
        var inner = new Grid();
        inner.Children.Add(child);
        inner.Children.Add(TiltBar(26, 3).Also(b => { b.HorizontalAlignment = HorizontalAlignment.Left; b.VerticalAlignment = VerticalAlignment.Top; b.Margin = new Thickness(0, -padding.Top, 0, 0); }));
        return new Border
        {
            Child = inner, Background = GlassFill(), BorderBrush = GlassEdgeFill(), BorderThickness = new Thickness(1),
            CornerRadius = new CornerRadius(16, 3, 16, 3), Padding = padding
        };
    }

    // ---- Side menu ------------------------------------------------------------------------------------
    private Grid BuildRail()
    {
        var column = new Grid { Width = RailWidth, HorizontalAlignment = HorizontalAlignment.Left };
        _padRail = column;   // View / Share puts the controller here (ControllerNav.cs)
        var shade = new LinearGradientBrush { StartPoint = new Point(0, 0), EndPoint = new Point(1, 0) };
        shade.GradientStops.Add(new GradientStop(NightColor("#F506070C"), 0));
        shade.GradientStops.Add(new GradientStop(NightColor("#E006070C"), 1));
        column.Background = shade;
        // The right edge leans: a thin gold-to-clear line drawn at an angle.
        var edge = new LinearGradientBrush { StartPoint = new Point(0, 0), EndPoint = new Point(0, 1) };
        edge.GradientStops.Add(new GradientStop(NightColor("#80FFD23F"), 0));
        edge.GradientStops.Add(new GradientStop(NightColor("#14FFFFFF"), .35));
        edge.GradientStops.Add(new GradientStop(NightColor("#14FFFFFF"), 1));
        column.Children.Add(new Border { Width = 1, HorizontalAlignment = HorizontalAlignment.Right, Background = edge });
        column.RowDefinitions.Add(new RowDefinition { Height = GridLength.Auto });
        column.RowDefinitions.Add(new RowDefinition());
        column.RowDefinitions.Add(new RowDefinition { Height = GridLength.Auto });

        // Logo and name.
        // The logo already says SYNDICATE, so no name next to it (it read twice).
        var brand = new StackPanel { Orientation = Orientation.Horizontal, Margin = new Thickness(22, 20, 16, 4) };
        FrameworkElement logo = (FrameworkElement?)SyndicateLogo(54) ?? Display("SYNDICATE", 30, Night.Gold);
        if (logo is System.Windows.Controls.Image picture) { picture.Height = 54; picture.MaxWidth = 130; picture.Effect = new DropShadowEffect { Color = NightColor(Night.Gold), BlurRadius = 22, ShadowDepth = 0, Opacity = .25 }; }
        brand.Children.Add(logo);
        var names = new StackPanel { VerticalAlignment = VerticalAlignment.Center, Margin = new Thickness(14, 0, 0, 0) };
        names.Children.Add(NightText("EVERY SEASON.", 10, Night.Sub, bold: true).Also(t => t.TextWrapping = TextWrapping.NoWrap));
        names.Children.Add(NightText("YOUR WAY.", 10, Night.Gold, bold: true).Also(t => t.TextWrapping = TextWrapping.NoWrap));
        brand.Children.Add(names);
        column.Children.Add(brand);

        // Pages, numbered.
        var nav = new StackPanel { Margin = new Thickness(12, 22, 12, 0) };
        int number = 0;
        Button Item(string glyph, string text, string tip, Action click, bool numbered = true)
        {
            var row = new Grid();
            row.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(12) });   // leaning bar (on the active page)
            row.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(30) });   // number
            row.ColumnDefinitions.Add(new ColumnDefinition());                               // label
            row.ColumnDefinitions.Add(new ColumnDefinition { Width = GridLength.Auto });     // icon
            var bar = TiltBar(4, 20); bar.Visibility = Visibility.Hidden; bar.HorizontalAlignment = HorizontalAlignment.Left; bar.Tag = "bar";
            row.Children.Add(bar);
            var index = NightText(numbered ? (++number).ToString("00") : "", 11, Night.Muted, bold: true); index.TextWrapping = TextWrapping.NoWrap; index.VerticalAlignment = VerticalAlignment.Center; index.Tag = "index";
            Grid.SetColumn(index, 1); row.Children.Add(index);
            var label = Display(text.ToUpperInvariant(), 17, Night.Sub); label.VerticalAlignment = VerticalAlignment.Center; label.Tag = "label";
            Grid.SetColumn(label, 2); row.Children.Add(label);
            var icon = GlyphIcon(glyph, 13, Night.Muted); icon.Tag = "icon";
            Grid.SetColumn(icon, 3); row.Children.Add(icon);
            var b = new Button { Content = row, Height = 42, Margin = new Thickness(0, 1, 0, 1), Padding = new Thickness(6, 0, 14, 0), ToolTip = tip, HorizontalAlignment = HorizontalAlignment.Stretch };
            RoundButton(b, "#00000000", Night.Text, 10, "#00000000");
            b.HorizontalContentAlignment = HorizontalAlignment.Stretch;
            b.MouseEnter += (_, __) => { if (!Equals(b.Tag, "on")) b.Background = Hex("#0FFFFFFF"); };
            b.MouseLeave += (_, __) => { if (!Equals(b.Tag, "on")) b.Background = Hex("#00000000"); };
            b.Click += (_, __) => click();
            nav.Children.Add(b);
            return b;
        }
        var home = Item("", "Home", "Home", () => { FortniteTab_Click(this, new RoutedEventArgs()); HighlightRail(0); });
        var play = Item("", "Play", "Pick a chapter and season", () => { FortniteTab_Click(this, new RoutedEventArgs()); HighlightRail(PlayTab); });
        var store = Item("", "Store", "Battle passes, packs & exclusives", () => { PassesTab_Click(new Button { Tag = "passes" }, new RoutedEventArgs()); HighlightRail(1); });
        var multiplayer = Item("", "Hosting", "Host or join friends", () => { HighlightRail(3); FortniteTab_Click(this, new RoutedEventArgs()); });
        var online = Item("\uE774", "Online", "Official Syndicate servers: coming soon", () => { FortniteTab_Click(this, new RoutedEventArgs()); HighlightRail(OnlineTab); });
        if (online.Content is Grid onlineRow)   // a small SOON tag next to the name (OnlinePage.cs)
            onlineRow.Children.Add(new Border { Background = Hex("#26FFD23F"), BorderBrush = Hex("#66FFD23F"), BorderThickness = new Thickness(1), CornerRadius = new CornerRadius(6, 2, 6, 2), Padding = new Thickness(6, 1, 6, 1),
                HorizontalAlignment = HorizontalAlignment.Right, VerticalAlignment = VerticalAlignment.Center, Margin = new Thickness(0, 0, 6, 0), IsHitTestVisible = false,
                Child = NightText("SOON", 9, Night.Gold, bold: true).Also(t => t.TextWrapping = TextWrapping.NoWrap) }.Also(b => Grid.SetColumn(b, 2)));
        var stats = Item("", "Stats", "Your time played, wins and kills", () => { FortniteTab_Click(this, new RoutedEventArgs()); HighlightRail(4); _ = RefreshStats(); });
        Item("", "Settings", "Launcher, match and bot settings", () => SettingsButton_Click(this, new RoutedEventArgs()));
        _secretShopRail = Item("", "Item shop", "Every cosmetic", () => OpenSecretShop(), numbered: false);
        _secretShopRail.Visibility = SecretShopUnlocked ? Visibility.Visible : Visibility.Collapsed;
        // Controller LB/RB walk these four in this order (ControllerNav.cs).
        _railButtons.Add(home); _railButtons.Add(store); _railButtons.Add(multiplayer); _railButtons.Add(stats);
        _navByIndex[0] = home; _navByIndex[PlayTab] = play; _navByIndex[1] = store; _navByIndex[3] = multiplayer; _navByIndex[4] = stats; _navByIndex[5] = _secretShopRail; _navByIndex[OnlineTab] = online;
        nav.Children.Add(new Border { Height = 1, Background = Hex("#14FFFFFF"), Margin = new Thickness(18, 12, 18, 10) });
        Button Small(string glyph, string text, string tip, Action click)
        {
            var row = new StackPanel { Orientation = Orientation.Horizontal };
            row.Children.Add(GlyphIcon(glyph, 12, Night.Muted).Also(g => g.Margin = new Thickness(0, 0, 10, 0)));
            row.Children.Add(NightText(text, 12, Night.Sub, bold: true).Also(t => { t.TextWrapping = TextWrapping.NoWrap; t.VerticalAlignment = VerticalAlignment.Center; }));
            var b = new Button { Content = row, Height = 32, Padding = new Thickness(18, 0, 10, 0), ToolTip = tip, HorizontalAlignment = HorizontalAlignment.Stretch };
            RoundButton(b, "#00000000", Night.Text, 8);
            b.HorizontalContentAlignment = HorizontalAlignment.Left;
            b.MouseEnter += (_, __) => b.Background = Hex("#0FFFFFFF"); b.MouseLeave += (_, __) => b.Background = Hex("#00000000");
            b.Click += (_, __) => click();
            nav.Children.Add(b);
            return b;
        }
        Small("", "Discord", "Join the Syndicate Discord", () => OpenLink(DiscordPresence.DiscordInvite));
        Small("", "Log", "Show or hide the log", () => LogToggleButton_Click(LogToggleButton, new RoutedEventArgs()));
        Grid.SetRow(nav, 1); column.Children.Add(nav);

        // Bottom: the selected season (opens Play), then the player, updates and version.
        var bottom = new StackPanel { Margin = new Thickness(14, 0, 14, 14) };
        var seasonCard = new Grid { Height = 96 };
        _railSeasonArt = new Border { CornerRadius = new CornerRadius(14, 3, 14, 3) };
        seasonCard.Children.Add(_railSeasonArt);
        seasonCard.Children.Add(new Border { CornerRadius = new CornerRadius(14, 3, 14, 3), Background = new LinearGradientBrush(Color.FromArgb(30, 6, 7, 12), Color.FromArgb(240, 6, 7, 12), 90) });
        var seasonWords = new StackPanel { VerticalAlignment = VerticalAlignment.Bottom, Margin = new Thickness(14, 0, 14, 10) };
        seasonWords.Children.Add(NightEyebrow("Now selected").Also(t => t.FontSize = 9.5));
        _railSeasonTitle = Display("SEASON 4", 22, Night.Text);
        seasonWords.Children.Add(_railSeasonTitle);
        _railSeasonBuild = NightText("Fortnite v4.5", 10.5, Night.Sub); _railSeasonBuild.TextWrapping = TextWrapping.NoWrap;
        seasonWords.Children.Add(_railSeasonBuild);
        seasonCard.Children.Add(seasonWords);
        var seasonButton = new Button { Content = seasonCard, Padding = new Thickness(0), ToolTip = "Change season", Margin = new Thickness(0, 0, 0, 12) };
        RoundButton(seasonButton, "#00000000", Night.Text, 14, "#26FFFFFF");
        seasonButton.BorderThickness = new Thickness(1);
        seasonButton.HorizontalContentAlignment = HorizontalAlignment.Stretch;
        seasonButton.MouseEnter += (_, __) => seasonButton.BorderBrush = Hex(Night.Gold);
        seasonButton.MouseLeave += (_, __) => seasonButton.BorderBrush = Hex("#26FFFFFF");
        seasonButton.Click += (_, __) => { FortniteTab_Click(this, new RoutedEventArgs()); HighlightRail(PlayTab); };
        bottom.Children.Add(seasonButton);

        var me = new Grid();
        me.ColumnDefinitions.Add(new ColumnDefinition { Width = GridLength.Auto });
        me.ColumnDefinitions.Add(new ColumnDefinition());
        me.ColumnDefinitions.Add(new ColumnDefinition { Width = GridLength.Auto });
        var avatar = new Button { Width = 40, Height = 40, Padding = new Thickness(2), ToolTip = "Profile: name and icon" };
        RoundButton(avatar, "#00000000", Night.Text, 20);
        avatar.Background = BrandFill();
        _railAvatarFace = new Border { Width = 36, Height = 36, CornerRadius = new CornerRadius(18), Background = Hex(Night.Surface2), Child = GlyphIcon("", 14, Night.Text) };
        avatar.Content = _railAvatarFace;
        avatar.Click += (_, __) => OpenProfile();
        me.Children.Add(avatar);
        var meText = new StackPanel { VerticalAlignment = VerticalAlignment.Center, Margin = new Thickness(10, 0, 6, 0) };
        _railPlayerName = NightText("Choose player", 12.5, Night.Text, bold: true); _railPlayerName.TextWrapping = TextWrapping.NoWrap; _railPlayerName.TextTrimming = TextTrimming.CharacterEllipsis;
        meText.Children.Add(_railPlayerName);
        meText.Children.Add(NightText("v" + LauncherVersion() + (Environment.GetEnvironmentVariable("TILTED_TESTER") is string testerBuild ? "  ·  TESTER " + testerBuild : ""), 10.5, Night.Muted).Also(t => t.TextWrapping = TextWrapping.NoWrap));
        if (BuildProvenance.State == BuildProvenance.Result.Tampered)
            meText.Children.Add(NightText("Unverified build", 10, "#FF6B6B").Also(t => { t.TextWrapping = TextWrapping.NoWrap; t.ToolTip = "This copy's files do not match a genuine Syndicate release. It has been modified or rebranded."; }));
        Grid.SetColumn(meText, 1); me.Children.Add(meText);
        var updates = new Button { Width = 34, Height = 34, Content = GlyphIcon("", 13, Night.Sub), ToolTip = "Check for updates", Padding = new Thickness(0), VerticalAlignment = VerticalAlignment.Center };
        RoundButton(updates, "#00000000", Night.Text, 17);
        updates.MouseEnter += (_, __) => updates.Background = Hex(Night.FrostHover); updates.MouseLeave += (_, __) => updates.Background = Hex("#00000000");
        updates.Click += (_, __) => CheckForUpdates(message => SetStatus(message));
        _railUpdateButton = updates;
        Grid.SetColumn(updates, 2); me.Children.Add(updates);
        bottom.Children.Add(me);
        Grid.SetRow(bottom, 2); column.Children.Add(bottom);
        return column;
    }

    private void StyleNav(Button b, bool active)
    {
        b.Tag = active ? "on" : null;
        b.Background = Hex(active ? "#14FFD23F" : "#00000000");
        if (b.Content is not Grid row) return;
        foreach (var child in row.Children.OfType<FrameworkElement>())
            switch (child.Tag as string)
            {
                case "bar": child.Visibility = active ? Visibility.Visible : Visibility.Hidden; break;
                case "index": ((TextBlock)child).Foreground = Hex(active ? Night.Gold : Night.Muted); break;
                case "label": ((TextBlock)child).Foreground = Hex(active ? Night.Text : Night.Sub); break;
                case "icon": ((TextBlock)child).Foreground = Hex(active ? Night.Gold : Night.Muted); break;
            }
    }

    // Tabs: Home (0), Store (1; 2 = the same store opened at packs), Multiplayer (3), Stats (4), Item shop (5), Play (6), Online (7).
    private void HighlightRail(int index)
    {
        AnimateRailChange(index);
        if (_multiplayerView != null) _multiplayerView.Visibility = index == 3 ? Visibility.Visible : Visibility.Collapsed;
        if (_statsView != null) _statsView.Visibility = index == 4 ? Visibility.Visible : Visibility.Collapsed;
        if (_secretShopView != null) _secretShopView.Visibility = index == 5 ? Visibility.Visible : Visibility.Collapsed;
        if (_syndicateHomeView != null) _syndicateHomeView.Visibility = index == 0 ? Visibility.Visible : Visibility.Collapsed;
        if (_onlineView != null) _onlineView.Visibility = index == OnlineTab ? Visibility.Visible : Visibility.Collapsed;
        if (_seasonBrowserView != null) _seasonBrowserView.Visibility = index == PlayTab || (index == 0 && _syndicateHomeView == null) ? Visibility.Visible : Visibility.Collapsed;
        if (_homeView != null) _homeView.Visibility = Visibility.Collapsed;
        if (index == 3) RefreshMultiplayer();
        if (index == 0) RefreshSyndicateHome(force: true);
        RefreshBackdrop();
        int active = index == 2 ? 1 : index;
        foreach (var pair in _navByIndex) StyleNav(pair.Value, pair.Key == active);
        Dispatcher.BeginInvoke(DispatcherPriority.Loaded, new Action(() => _musicPlace?.Invoke()));   // the music card moves with the page (LauncherMusic.cs)
    }

    // ---- Home ----------------------------------------------------------------------------------------
    private Grid BuildSyndicateHome()
    {
        var view = new Grid { Margin = new Thickness(RailWidth, 56, 32, 0), ClipToBounds = true };
        _syndicateHomeView = view;
        view.ColumnDefinitions.Add(new ColumnDefinition());
        view.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(372) });
        view.RowDefinitions.Add(new RowDefinition());
        view.RowDefinitions.Add(new RowDefinition { Height = GridLength.Auto });

        // The season's colour glows up from the bottom left, and its number sits huge and faint behind the name.
        _homeGlow = new Border { Opacity = .32, IsHitTestVisible = false, Margin = new Thickness(-40, 0, 0, -40) };
        Grid.SetRowSpan(_homeGlow, 2); Grid.SetColumnSpan(_homeGlow, 2); view.Children.Add(_homeGlow);
        _homeWatermark = Display("4", 420, Night.Text);
        _homeWatermark.Opacity = .06; _homeWatermark.IsHitTestVisible = false;
        _homeWatermark.HorizontalAlignment = HorizontalAlignment.Right; _homeWatermark.VerticalAlignment = VerticalAlignment.Center; _homeWatermark.Margin = new Thickness(0, -60, 10, 0);
        _homeWatermark.RenderTransform = new SkewTransform(-14, 0);
        view.Children.Add(_homeWatermark);

        // ---- The name, the season and LAUNCH.
        var words = new StackPanel { VerticalAlignment = VerticalAlignment.Center, Margin = new Thickness(60, 0, 24, 0), RenderTransform = new TranslateTransform() };   // room on the left: the big name leans out
        _homeTag = NightText("CHAPTER 1  ·  SEASON 4", 11, Night.OnGold, bold: true); _homeTag.TextWrapping = TextWrapping.NoWrap;
        _homeTagBox = new Border { Child = _homeTag, Background = Hex(Night.Gold), Padding = new Thickness(12, 4, 12, 4), HorizontalAlignment = HorizontalAlignment.Left, RenderTransform = new SkewTransform(-14, 0), Margin = new Thickness(4, 0, 0, 14) };
        words.Children.Add(_homeTagBox);
        var name = new StackPanel();
        name.Children.Add(Display("PROJECT", 46, Night.Text).Also(t => { t.Margin = new Thickness(6, 0, 0, -24); t.Opacity = .92; }));
        var syndicate = Display("SYNDICATE", 150, Night.Gold);
        syndicate.RenderTransform = new SkewTransform(-12, 0);
        syndicate.Effect = new DropShadowEffect { Color = NightColor("#FFB800"), BlurRadius = 40, ShadowDepth = 0, Opacity = .35 };
        name.Children.Add(syndicate);
        words.Children.Add(new Viewbox { Child = name, StretchDirection = StretchDirection.DownOnly, HorizontalAlignment = HorizontalAlignment.Left, MaxHeight = 210 });
        _homeTagline = Display("BRACE FOR IMPACT", 26, Night.Text); _homeTagline.Margin = new Thickness(8, -4, 0, 6);
        words.Children.Add(_homeTagline);
        _homeDescription = Body("", 14, "#B9C0DA"); _homeDescription.MaxWidth = 520; _homeDescription.HorizontalAlignment = HorizontalAlignment.Left; _homeDescription.LineHeight = 21; _homeDescription.Margin = new Thickness(8, 0, 0, 18);
        words.Children.Add(_homeDescription);

        var actions = new StackPanel { Orientation = Orientation.Horizontal, HorizontalAlignment = HorizontalAlignment.Left };
        var launchRow = new StackPanel { Orientation = Orientation.Horizontal };
        launchRow.Children.Add(GlyphIcon("", 15, Night.OnGold).Also(g => { g.Margin = new Thickness(0, 0, 12, 0); g.Foreground = Hex(Night.OnGold); }));
        _homeLaunchText = Display("LAUNCH", 28, Night.OnGold); _homeLaunchText.VerticalAlignment = VerticalAlignment.Center;
        launchRow.Children.Add(_homeLaunchText);
        _homeLaunch = new Button { Content = launchRow, Height = 62, MinWidth = 240, Padding = new Thickness(30, 0, 30, 0), ToolTip = "Start Fortnite for this season" };
        NightButton(_homeLaunch, "gold");
        _homeLaunch.Padding = new Thickness(30, 0, 30, 0);
        _homeLaunch.HorizontalAlignment = HorizontalAlignment.Left;
        FlatTemplate(_homeLaunch, 14);   // flat, rounded (no parallelogram), keeps the gold fill and glow
        _homeLaunch.Click += (_, __) => LaunchFromHome();
        actions.Children.Add(_homeLaunch);
        Button Lean(string glyph, string text, string tip, Action click)
        {
            var row = new StackPanel { Orientation = Orientation.Horizontal };
            row.Children.Add(GlyphIcon(glyph, 13, Night.Text).Also(g => g.Margin = new Thickness(0, 0, 10, 0)));
            row.Children.Add(Display(text, 17, Night.Text).Also(t => t.VerticalAlignment = VerticalAlignment.Center));
            var b = new Button { Content = row, Height = 62, Padding = new Thickness(24, 0, 24, 0), Margin = new Thickness(14, 0, 0, 0), ToolTip = tip };
            FlatButton(b, "#161B2B", Night.Text, 12, "#2BFFFFFF");
            b.MouseEnter += (_, __) => b.BorderBrush = Hex(Night.Gold); b.MouseLeave += (_, __) => b.BorderBrush = Hex("#2BFFFFFF");
            b.Click += (_, __) => click();
            return b;
        }
        actions.Children.Add(Lean("", "SEASONS", "Pick a chapter and season", () => HighlightRail(PlayTab)));
        actions.Children.Add(Lean("", "SETTINGS", "Match & bot settings", () => SettingsButton_Click(this, new RoutedEventArgs())));
        var status = new StackPanel { Orientation = Orientation.Horizontal, Margin = new Thickness(12, 16, 0, 0) };
        _homeStatusDot = new Border { Width = 8, Height = 8, CornerRadius = new CornerRadius(4), Background = Hex(Night.Good), VerticalAlignment = VerticalAlignment.Center, Margin = new Thickness(0, 0, 9, 0) };
        status.Children.Add(_homeStatusDot);
        _homeStatusText = NightText("READY TO LAUNCH", 11.5, Night.Sub, bold: true); _homeStatusText.TextWrapping = TextWrapping.NoWrap;
        status.Children.Add(_homeStatusText);
        _homeHint = new TextBlock { FontSize = 12.5, Foreground = Hex("#FFD580"), TextWrapping = TextWrapping.Wrap, MaxWidth = 540, HorizontalAlignment = HorizontalAlignment.Left, Margin = new Thickness(12, 8, 0, 0), Visibility = Visibility.Collapsed };
        view.Children.Add(words);
        // LAUNCH / SEASONS / SETTINGS and the status line, straight down at the bottom of the page.
        var bottomActions = new StackPanel { HorizontalAlignment = HorizontalAlignment.Left, VerticalAlignment = VerticalAlignment.Bottom, Margin = new Thickness(60, 12, 24, 28) };
        bottomActions.Children.Add(new Viewbox { Child = actions, StretchDirection = StretchDirection.DownOnly, HorizontalAlignment = HorizontalAlignment.Left });   // shrink to fit so the row never clips
        bottomActions.Children.Add(status);
        bottomActions.Children.Add(_homeHint);
        Grid.SetRow(bottomActions, 1); view.Children.Add(bottomActions);

        // ---- Right: player, build, shortcuts.
        // The tiles shrink to fit a small window instead of running under the title bar or off the bottom.
        var side = new StackPanel { Width = 348 };
        var sideFit = new Viewbox { Child = side, StretchDirection = StretchDirection.DownOnly, VerticalAlignment = VerticalAlignment.Top, HorizontalAlignment = HorizontalAlignment.Right, Margin = new Thickness(0, 8, 0, 10) };
        Grid.SetColumn(sideFit, 1); view.Children.Add(sideFit);

        var player = new StackPanel();
        player.Children.Add(NightEyebrow("Player"));
        var who = new Grid { Margin = new Thickness(0, 12, 0, 12) };
        who.ColumnDefinitions.Add(new ColumnDefinition { Width = GridLength.Auto });
        who.ColumnDefinitions.Add(new ColumnDefinition());
        _homePlayerFace = new Border { Width = 48, Height = 48, CornerRadius = new CornerRadius(24), Background = BrandFill(), Padding = new Thickness(2) };
        who.Children.Add(_homePlayerFace);
        var whoText = new StackPanel { VerticalAlignment = VerticalAlignment.Center, Margin = new Thickness(12, 0, 0, 0) };
        _homePlayerName = Display("NO PLAYER", 22, Night.Text); _homePlayerName.TextTrimming = TextTrimming.CharacterEllipsis;
        whoText.Children.Add(_homePlayerName);
        _homePlayerSub = NightText("Choose a player to play", 11.5, Night.Sub); _homePlayerSub.TextWrapping = TextWrapping.NoWrap;
        whoText.Children.Add(_homePlayerSub);
        Grid.SetColumn(whoText, 1); who.Children.Add(whoText);
        player.Children.Add(who);
        // Season level and the XP toward the next level (filled from /launcher/level).
        var levelHead = new Grid { Margin = new Thickness(0, 2, 0, 7) };
        _homeLevelText = NightText("LEVEL \u2014", 12, Night.Gold, bold: true); _homeLevelText.VerticalAlignment = VerticalAlignment.Center;
        levelHead.Children.Add(_homeLevelText);
        _homeXpText = NightText("", 11, Night.Muted); _homeXpText.HorizontalAlignment = HorizontalAlignment.Right; _homeXpText.VerticalAlignment = VerticalAlignment.Center; _homeXpText.TextWrapping = TextWrapping.NoWrap;
        levelHead.Children.Add(_homeXpText);
        player.Children.Add(levelHead);
        _homeXpBar = new Grid { Height = 7, Margin = new Thickness(0, 0, 0, 14) };
        _homeXpBar.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(0, GridUnitType.Star) });
        _homeXpBar.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(1, GridUnitType.Star) });
        var xpTrack = new Border { Background = Hex("#33000000"), CornerRadius = new CornerRadius(4) }; Grid.SetColumnSpan(xpTrack, 2); _homeXpBar.Children.Add(xpTrack);
        var xpFill = new Border { CornerRadius = new CornerRadius(4), Background = new LinearGradientBrush(NightColor(Night.GoldDeep), NightColor(Night.Gold), 0) }; Grid.SetColumn(xpFill, 0); _homeXpBar.Children.Add(xpFill);
        player.Children.Add(_homeXpBar);
        _homePlayerEdit = new Button { Content = "EDIT PROFILE", Height = 32, FontSize = 11.5, FontWeight = FontWeights.SemiBold, Padding = new Thickness(14, 0, 14, 0), HorizontalAlignment = HorizontalAlignment.Stretch };
        NightButton(_homePlayerEdit, "base", 8);
        _homePlayerEdit.Padding = new Thickness(14, 0, 14, 0);
        _homePlayerEdit.Click += (_, __) =>
        {
            if (_account.SignedIn) OpenProfile();
            else AccountButton.RaiseEvent(new RoutedEventArgs(ButtonBase.ClickEvent));
        };
        player.Children.Add(_homePlayerEdit);
        side.Children.Add(TiltTile(player, new Thickness(18, 16, 18, 16)));

        // Fortnite build tile removed; the build number and "Change folder" live on the season browser.

        var shortcuts = new StackPanel();
        shortcuts.Children.Add(NightEyebrow("Jump to").Also(t => t.Margin = new Thickness(0, 0, 0, 6)));
        void Shortcut(string glyph, string text, Action click)
        {
            var row = new Grid();
            row.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(26) });
            row.ColumnDefinitions.Add(new ColumnDefinition());
            row.ColumnDefinitions.Add(new ColumnDefinition { Width = GridLength.Auto });
            row.Children.Add(GlyphIcon(glyph, 13, Night.Gold).Also(g => g.HorizontalAlignment = HorizontalAlignment.Left));
            var label = NightText(text, 12.5, Night.Text, bold: true); label.TextWrapping = TextWrapping.NoWrap; label.VerticalAlignment = VerticalAlignment.Center; Grid.SetColumn(label, 1); row.Children.Add(label);
            var arrow = GlyphIcon("", 9, Night.Muted); Grid.SetColumn(arrow, 2); row.Children.Add(arrow);
            var b = new Button { Content = row, Height = 34, Padding = new Thickness(8, 0, 8, 0), HorizontalAlignment = HorizontalAlignment.Stretch };
            RoundButton(b, "#00000000", Night.Text, 8);
            b.HorizontalContentAlignment = HorizontalAlignment.Stretch;
            b.MouseEnter += (_, __) => b.Background = Hex("#12FFFFFF"); b.MouseLeave += (_, __) => b.Background = Hex("#00000000");
            b.Click += (_, __) => click();
            shortcuts.Children.Add(b);
        }
        Shortcut("", "Host a match for friends", () => { HighlightRail(3); FortniteTab_Click(this, new RoutedEventArgs()); });
        Shortcut("", "Battle passes & packs", () => { PassesTab_Click(new Button { Tag = "passes" }, new RoutedEventArgs()); HighlightRail(1); });
        Shortcut("", "Your stats", () => { FortniteTab_Click(this, new RoutedEventArgs()); HighlightRail(4); _ = RefreshStats(); });
        side.Children.Add(TiltTile(shortcuts, new Thickness(12, 14, 12, 10)).Also(t => t.Margin = new Thickness(0, 12, 0, 0)));

        // ---- Friends (from the mock-up): an invite card in the right rail.
        var friends = new StackPanel();
        friends.Children.Add(NightEyebrow("Friends").Also(t => t.Margin = new Thickness(0, 0, 0, 10)));
        var fWrap = new StackPanel { HorizontalAlignment = HorizontalAlignment.Center };
        fWrap.Children.Add(new Border { Width = 42, Height = 42, CornerRadius = new CornerRadius(21), Background = Hex("#0DFFFFFF"), HorizontalAlignment = HorizontalAlignment.Center, Margin = new Thickness(0, 2, 0, 10), Child = GlyphIcon("\uE716", 18, Night.Muted).Also(g => { g.HorizontalAlignment = HorizontalAlignment.Center; g.VerticalAlignment = VerticalAlignment.Center; }) });
        _homeFriendsText = NightText("No friends online", 12.5, Night.Text, bold: true); _homeFriendsText.HorizontalAlignment = HorizontalAlignment.Center; _homeFriendsText.TextWrapping = TextWrapping.NoWrap;
        fWrap.Children.Add(_homeFriendsText);
        fWrap.Children.Add(NightText("Invite your friends to play!", 11, Night.Muted).Also(t => { t.HorizontalAlignment = HorizontalAlignment.Center; t.Margin = new Thickness(0, 3, 0, 12); }));
        var invite = new Button { Content = "INVITE FRIENDS", Height = 34, FontSize = 11.5, FontWeight = FontWeights.SemiBold, HorizontalAlignment = HorizontalAlignment.Stretch };
        NightButton(invite, "gold", 8);
        invite.Click += (_, __) => { HighlightRail(3); FortniteTab_Click(this, new RoutedEventArgs()); };
        fWrap.Children.Add(invite);
        friends.Children.Add(fWrap);
        side.Children.Add(TiltTile(friends, new Thickness(16, 14, 16, 14)).Also(t => t.Margin = new Thickness(0, 12, 0, 0)));


        view.IsVisibleChanged += (_, __) =>
        {
            if (!view.IsVisible) return;
            RefreshSyndicateHome(force: true);
            if (MotionEnabled && words.RenderTransform is TranslateTransform slide)
            {
                slide.BeginAnimation(TranslateTransform.XProperty, new DoubleAnimation(-40, 0, TimeSpan.FromMilliseconds(460)) { EasingFunction = new QuinticEase { EasingMode = EasingMode.EaseOut } });
                words.BeginAnimation(OpacityProperty, new DoubleAnimation(0, 1, TimeSpan.FromMilliseconds(380)));
                _homeWatermark?.BeginAnimation(OpacityProperty, new DoubleAnimation(0, .06, TimeSpan.FromMilliseconds(900)));
            }
        };
        _homeTimer = new DispatcherTimer { Interval = TimeSpan.FromSeconds(1) };
        _homeTimer.Tick += (_, __) => { if (view.IsVisible) RefreshSyndicateHome(force: false); };
        _homeTimer.Start();
        return view;
    }

    // A parallelogram: the frame leans, the art and text inside stand upright (clipped by the leaning frame).
    private Button LeaningCard(UIElement inside, int column, string tip, Action click, out Border frame)
    {
        var upright = new Grid { Margin = new Thickness(-28, 0, -28, 0), RenderTransform = new SkewTransform(12, 0), RenderTransformOrigin = new Point(.5, .5) };
        upright.Children.Add(inside);
        frame = new Border { Child = upright, ClipToBounds = true, CornerRadius = new CornerRadius(6), BorderBrush = Hex("#33FFFFFF"), BorderThickness = new Thickness(1), Background = Hex(Night.Surface) };
        var card = new Button { Content = frame, Padding = new Thickness(0), Margin = new Thickness(column == 0 ? 0 : 14, 0, 0, 0), ToolTip = tip, RenderTransform = new SkewTransform(-12, 0), RenderTransformOrigin = new Point(.5, .5) };
        // Bare template: the content fills the whole tile (RoundButton's chrome centres it vertically).
        card.Template = new ControlTemplate(typeof(Button)) { VisualTree = new FrameworkElementFactory(typeof(ContentPresenter)) };
        card.Background = Brushes.Transparent; card.Cursor = System.Windows.Input.Cursors.Hand; card.FocusVisualStyle = null;
        card.Click += (_, __) => click();
        Grid.SetColumn(card, column);
        return card;
    }

    // One chapter: its art, name and status. Click opens the season browser at that chapter.
    private Button ChapterCard(int chapter, int artSeason, string subtitle, string badge, string badgeColor, int column)
    {
        var grid = new Grid();
        var art = new Border();
        try { art.Background = SeasonBrush(artSeason); } catch { art.Background = Hex(Night.Surface2); }
        var zoom = new ScaleTransform(1, 1); art.RenderTransform = zoom; art.RenderTransformOrigin = new Point(.5, .5);
        grid.Children.Add(art);
        var shade = new LinearGradientBrush { StartPoint = new Point(0, 0), EndPoint = new Point(0, 1) };
        shade.GradientStops.Add(new GradientStop(NightColor("#1A06070C"), 0));
        shade.GradientStops.Add(new GradientStop(NightColor("#F006070C"), 1));
        grid.Children.Add(new Border { Background = shade });
        var words = new StackPanel { Margin = new Thickness(44, 0, 44, 14), VerticalAlignment = VerticalAlignment.Bottom };
        var badgeRow = new StackPanel { Orientation = Orientation.Horizontal, Margin = new Thickness(2, 0, 0, 4) };
        badgeRow.Children.Add(new Border { Width = 7, Height = 7, CornerRadius = new CornerRadius(3.5), Background = Hex(badgeColor), Margin = new Thickness(0, 0, 6, 0), VerticalAlignment = VerticalAlignment.Center });
        badgeRow.Children.Add(NightText(badge, 10, Night.Text, bold: true).Also(t => t.TextWrapping = TextWrapping.NoWrap));
        words.Children.Add(badgeRow);
        var titleRow = new Grid();
        var big = Display("CHAPTER " + chapter, 40, Night.Text);
        big.Effect = new DropShadowEffect { Color = Colors.Black, BlurRadius = 16, ShadowDepth = 0, Opacity = .8 };
        titleRow.Children.Add(big);
        var arrow = GlyphIcon("", 18, Night.Gold); arrow.HorizontalAlignment = HorizontalAlignment.Right; var nudge = new TranslateTransform(); arrow.RenderTransform = nudge;
        titleRow.Children.Add(arrow);
        words.Children.Add(titleRow);
        words.Children.Add(NightText(subtitle, 12, Night.Sub).Also(t => { t.TextWrapping = TextWrapping.NoWrap; t.Margin = new Thickness(2, -2, 0, 0); }));
        grid.Children.Add(words);
        var card = LeaningCard(grid, column, "Browse Chapter " + chapter + " seasons", () => OpenChapter(chapter), out var frame);
        var ease = new CubicEase { EasingMode = EasingMode.EaseOut };
        card.MouseEnter += (_, __) =>
        {
            frame.BorderBrush = Hex(Night.Gold);
            if (!MotionEnabled) return;
            zoom.BeginAnimation(ScaleTransform.ScaleXProperty, new DoubleAnimation(1.07, TimeSpan.FromMilliseconds(380)) { EasingFunction = ease });
            zoom.BeginAnimation(ScaleTransform.ScaleYProperty, new DoubleAnimation(1.07, TimeSpan.FromMilliseconds(380)) { EasingFunction = ease });
            nudge.BeginAnimation(TranslateTransform.XProperty, new DoubleAnimation(6, TimeSpan.FromMilliseconds(220)) { EasingFunction = ease });
        };
        card.MouseLeave += (_, __) =>
        {
            frame.BorderBrush = Hex("#33FFFFFF");
            zoom.BeginAnimation(ScaleTransform.ScaleXProperty, new DoubleAnimation(1, TimeSpan.FromMilliseconds(300)) { EasingFunction = ease });
            zoom.BeginAnimation(ScaleTransform.ScaleYProperty, new DoubleAnimation(1, TimeSpan.FromMilliseconds(300)) { EasingFunction = ease });
            nudge.BeginAnimation(TranslateTransform.XProperty, new DoubleAnimation(0, TimeSpan.FromMilliseconds(220)) { EasingFunction = ease });
        };
        return card;
    }

    // The last tile: every season on one timeline (the season browser at the selected season's chapter).
    private Button AllSeasonsCard(int column)
    {
        var grid = new Grid { Background = GoldFill() };
        var words = new StackPanel { Margin = new Thickness(44, 0, 44, 14), VerticalAlignment = VerticalAlignment.Bottom };
        words.Children.Add(GlyphIcon("", 20, Night.OnGold).Also(g => { g.HorizontalAlignment = HorizontalAlignment.Left; g.Margin = new Thickness(2, 0, 0, 8); g.Foreground = Hex(Night.OnGold); }));
        words.Children.Add(Display("ALL SEASONS", 30, Night.OnGold));
        words.Children.Add(NightText("Browse the timeline", 12, Night.OnGold, bold: true).Also(t => { t.TextWrapping = TextWrapping.NoWrap; t.Opacity = .75; t.Margin = new Thickness(2, -2, 0, 0); }));
        grid.Children.Add(words);
        var card = LeaningCard(grid, column, "Every season, on one timeline", () => OpenChapter(ChapterOf(_heroSeason)), out var frame);
        frame.BorderBrush = Hex("#00000000");
        card.MouseEnter += (_, __) => frame.BorderBrush = Hex("#FFFFFF");
        card.MouseLeave += (_, __) => frame.BorderBrush = Hex("#00000000");
        return card;
    }

    private void OpenChapter(int chapter)
    {
        FortniteTab_Click(this, new RoutedEventArgs());
        SelectBrowserChapter(chapter);
        HighlightRail(PlayTab);
    }

    // LAUNCH: the same as PLAY in the season browser (launch / quit / cancel / link folder).
    private void LaunchFromHome()
    {
        CommitHeroSeason();
        SyncHeroPlay();
        if (_heroPlay == null) { SeasonLaunchButton(_heroSeason).RaiseEvent(new RoutedEventArgs(ButtonBase.ClickEvent)); return; }
        if (!_heroPlay.IsEnabled) return;
        _heroPlay.RaiseEvent(new RoutedEventArgs(ButtonBase.ClickEvent));
        RefreshSyndicateHome(force: true);
    }

    // Keeps the home page and the side menu's season card in step (once a second, and on every change of page).
    // Pull the signed-in player's season level and XP and fill the bar in the Player tile.
    private async void RefreshHomeLevel()
    {
        if (_homeLevelText == null || _homeXpBar == null) return;
        if (!_account.SignedIn) { _homeLevelText.Text = "LEVEL —"; if (_homeXpText != null) _homeXpText.Text = ""; SetXpFraction(0); return; }
        try
        {
            var d = await _account.Level(_heroSeason);
            int level = JInt(d, "level", 1), xp = JInt(d, "xp", 0), next = JInt(d, "xpToNext", 0);
            _homeLevelText.Text = "LEVEL " + level;
            double frac = next > 0 ? Math.Min(1.0, Math.Max(0.0, xp / (double)next)) : (level >= 100 ? 1.0 : 0.0);
            SetXpFraction(frac);
            if (_homeXpText != null) _homeXpText.Text = level >= 100 ? "MAX" : next > 0 ? xp.ToString("N0") + " / " + next.ToString("N0") + " XP" : xp.ToString("N0") + " XP";
        }
        catch { }
    }

    private void SetXpFraction(double frac)
    {
        if (_homeXpBar == null || _homeXpBar.ColumnDefinitions.Count < 2) return;
        _homeXpBar.ColumnDefinitions[0].Width = new GridLength(frac, GridUnitType.Star);
        _homeXpBar.ColumnDefinitions[1].Width = new GridLength(Math.Max(0.0001, 1 - frac), GridUnitType.Star);
    }

    private static int JInt(JsonElement d, string name, int fallback) =>
        d.TryGetProperty(name, out var v) && v.ValueKind == JsonValueKind.Number ? v.GetInt32() : fallback;

    private void RefreshSyndicateHome(bool force)
    {
        var season = ModernSeasons.FirstOrDefault(s => s.Number == _heroSeason);
        if (season == null) return;
        bool linked = SeasonLinked(season);
        string accent = SeasonAccent(season.Number);
        int chapter = ChapterOf(season.Number);
        string seasonText = SeasonTitle(season.Number);
        if (force || _homeShownSeason != season.Number)
        {
            _homeShownSeason = season.Number;
            Brush art; try { art = SeasonBrush(season.Number); } catch { art = Hex(Night.Surface2); }
            if (_railSeasonArt != null) _railSeasonArt.Background = art;
            if (_homeGlow != null) _homeGlow.Background = new RadialGradientBrush(NightColor(accent), Color.FromArgb(0, 0, 0, 0)) { Center = new Point(.1, .95), GradientOrigin = new Point(.1, .95), RadiusX = .65, RadiusY = .8 };
            if (_homeWatermark != null) _homeWatermark.Text = season.Number == 0 ? "0" : SeasonName(season.Number);
            if (_homeTag != null) _homeTag.Text = ("Chapter " + chapter + "  ·  " + (season.Number == 0 ? "Pre-season" : "Season " + SeasonName(season.Number))).ToUpperInvariant();
            if (_homeTagBox != null) _homeTagBox.Background = Hex(accent);
            if (_homeTagline != null) _homeTagline.Text = season.Tagline;
            if (_homeDescription != null) _homeDescription.Text = season.Description;
            if (_railSeasonTitle != null) _railSeasonTitle.Text = (chapter > 1 ? "C" + chapter + " " : "") + seasonText;
            if (_railSeasonBuild != null) _railSeasonBuild.Text = "Fortnite v" + season.Build;
            if (_homeBuildNumber != null) _homeBuildNumber.Text = season.Build;
            if (_homeBuildSeason != null) _homeBuildSeason.Text = "Chapter " + chapter + ", " + (season.Number == 0 ? "Pre-season" : "Season " + SeasonName(season.Number));
            if (force && MotionEnabled && _homeWatermark != null)
                _homeWatermark.BeginAnimation(OpacityProperty, new DoubleAnimation(0, .06, TimeSpan.FromMilliseconds(700)));
            RefreshHomeLevel();   // the selected season changed: show its level and XP
        }

        // LAUNCH mirrors the season browser's PLAY.
        string state = (_heroPlay?.Content as string) ?? "PLAY";
        if (!linked && state == "PLAY") state = "LINK FOLDER";
        if (_homeLaunchText != null) _homeLaunchText.Text = state switch { "QUIT" => "QUIT GAME", "CANCEL" => "CANCEL", "LINK FOLDER" => "LINK FOLDER", _ => "LAUNCH" };
        if (_homeLaunch != null)
        {
            bool quit = state == "QUIT";
            _homeLaunch.IsEnabled = state == "LINK FOLDER" || (_heroPlay?.IsEnabled ?? true);
            _homeLaunch.Background = quit ? Hex(Night.Danger) : GoldFill();
            _homeLaunch.BorderBrush = Hex(quit ? "#B23A47" : "#B07A00");
            if (_homeLaunchText != null) _homeLaunchText.Foreground = Hex(quit ? "#FFFFFF" : Night.OnGold);
            if (_homeLaunch.Effect is DropShadowEffect glow) glow.Color = NightColor(quit ? Night.Danger : "#FFC21A");
        }
        string? hint = null;
        try { hint = SeasonHint(season); } catch { }
        string statusText = state == "QUIT" ? "IN GAME" : state == "CANCEL" ? "STARTING…" : !linked ? "GAME FOLDER NOT SET" : hint != null ? "NEEDS ATTENTION" : "READY TO LAUNCH";
        string statusColor = state == "QUIT" || state == "CANCEL" ? Night.Sky : !linked || hint != null ? Night.Gold : Night.Good;
        if (_homeStatusText != null) _homeStatusText.Text = statusText + "   ·   FORTNITE v" + season.Build;
        if (_homeStatusDot != null) _homeStatusDot.Background = Hex(statusColor);
        if (_homeBuildState != null) { _homeBuildState.Text = statusText; _homeBuildState.Foreground = Hex(statusColor); }
        if (_homeBuildNumber?.Parent is Grid buildRow)
            foreach (var dot in buildRow.Children.OfType<Border>().Where(b => Equals(b.Tag, "dot"))) dot.Background = Hex(statusColor);
        if (_homeHint != null) { _homeHint.Text = hint ?? ""; _homeHint.Visibility = hint == null ? Visibility.Collapsed : Visibility.Visible; }

        // Player.
        string? playerName = _account.SignedIn ? _account.Username : null;
        if (string.IsNullOrWhiteSpace(playerName)) playerName = null;
        if (force || playerName != _homeShownPlayer)
        {
            _homeShownPlayer = playerName;
            if (_homePlayerName != null) _homePlayerName.Text = (playerName ?? "No player").ToUpperInvariant();
            if (_homePlayerSub != null) _homePlayerSub.Text = playerName != null ? "Local profile  ·  Active" : "Choose a player to play";
            if (_homePlayerEdit != null) _homePlayerEdit.Content = playerName != null ? "EDIT PROFILE" : "CHOOSE PLAYER";
            if (_homePlayerFace != null) _homePlayerFace.Child = AvatarFace(playerName == null ? "" : AvatarFor(playerName), playerName ?? "?", 44);
            RefreshHomeLevel();
            if (_railPlayerName != null) _railPlayerName.Text = playerName ?? "Choose player";
        }
    }
}
