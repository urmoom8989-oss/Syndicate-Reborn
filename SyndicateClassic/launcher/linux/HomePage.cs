// Syndicate © 2026 Syndicate. All rights reserved. Proprietary — see LICENSE.
// The home page content (right of the rail), ported from SyndicateHome.cs (BuildSyndicateHome).
// Buttons navigate through the shell; data is placeholder until live season/account data
// is wired. LAUNCH is left inert for now — it needs the game + backend, wired later.
using System;
using System.IO;
using System.Threading.Tasks;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Input;
using Avalonia.Layout;
using Avalonia.Media;
using Avalonia.Platform.Storage;
using static SyndicateLauncher.Linux.Ui;

namespace SyndicateLauncher.Linux;

public class HomePage : UserControl
{
    private readonly Action<string> _nav;
    private TextBlock _launchText = null!;
    private Border _launchBorder = null!;
    private TextBlock _statusText = null!;

    private const int Level = 42;
    private const int Xp = 18750;
    private const int XpToNext = 45000;
    private const string PlayerName = "UNKN0WN";
    private const string Tagline = "BRACE FOR IMPACT";
    private const string Description =
        "A meteor hangs over the island. Tilted Towers, dusty divots, and the first-ever Battle Pass map — " +
        "Season 4 in full, on your own private server.";

    public HomePage(Action<string> navigate)
    {
        _nav = navigate;
        Content = BuildContent();
    }

    private Control BuildContent()
    {
        var view = new Grid { Margin = new Thickness(Shell.RailWidth, 56, 32, 0), ClipToBounds = true };
        view.ColumnDefinitions.Add(new ColumnDefinition(GridLength.Star));
        view.ColumnDefinitions.Add(new ColumnDefinition(372, GridUnitType.Pixel));
        view.RowDefinitions.Add(new RowDefinition(GridLength.Star));
        view.RowDefinitions.Add(new RowDefinition(GridLength.Auto));

        var watermark = Display(Shell.SeasonNumber.ToString(), 420, Night.Text).Also(t =>
        {
            t.Opacity = 0.06;
            t.IsHitTestVisible = false;
            t.HorizontalAlignment = HorizontalAlignment.Right;
            t.VerticalAlignment = VerticalAlignment.Center;
            t.Margin = new Thickness(0, -60, 10, 0);
            t.RenderTransform = Lean(-14);
        });
        Grid.SetRowSpan(watermark, 2);
        Grid.SetColumnSpan(watermark, 2);
        view.Children.Add(watermark);

        var words = new StackPanel { VerticalAlignment = VerticalAlignment.Center, Margin = new Thickness(60, 0, 24, 0) };
        words.Children.Add(new Border
        {
            Background = Hex(Shell.Accent),
            Padding = new Thickness(12, 4, 12, 4),
            HorizontalAlignment = HorizontalAlignment.Left,
            RenderTransform = Lean(-14),
            RenderTransformOrigin = RelativePoint.Center,
            Margin = new Thickness(4, 0, 0, 14),
            Child = NightText($"CHAPTER 1  ·  {Shell.SeasonLabel}", 11, Night.OnGold, bold: true).Also(t => t.TextWrapping = TextWrapping.NoWrap)
        });

        var name = new StackPanel();
        name.Children.Add(Display("PROJECT", 46, Night.Text).Also(t => { t.Margin = new Thickness(6, 0, 0, -24); t.Opacity = 0.92; }));
        name.Children.Add(Display("SYNDICATE", 150, Night.Gold).Also(t => t.RenderTransform = Lean(-12)));
        words.Children.Add(new Viewbox { Child = name, StretchDirection = StretchDirection.DownOnly, HorizontalAlignment = HorizontalAlignment.Left, MaxHeight = 210 });

        words.Children.Add(Display(Tagline, 26, Night.Text).Also(t => t.Margin = new Thickness(8, -4, 0, 6)));
        words.Children.Add(Body(Description, 14, "#B9C0DA").Also(t => { t.MaxWidth = 520; t.HorizontalAlignment = HorizontalAlignment.Left; t.LineHeight = 21; t.Margin = new Thickness(8, 0, 0, 18); }));
        view.Children.Add(words);

        var actions = new StackPanel { Orientation = Orientation.Horizontal, HorizontalAlignment = HorizontalAlignment.Left };

        var launchRow = new StackPanel { Orientation = Orientation.Horizontal, HorizontalAlignment = HorizontalAlignment.Center };
        launchRow.Children.Add(Icon("play", 15, Night.OnGold).Also(g => g.Margin = new Thickness(0, 0, 12, 0)));
        _launchText = Display("LAUNCH", 28, Night.OnGold).Also(t => t.VerticalAlignment = VerticalAlignment.Center);
        launchRow.Children.Add(_launchText);
        _launchBorder = new Border
        {
            Child = launchRow,
            Height = 62,
            MinWidth = 240,
            Padding = new Thickness(30, 0, 30, 0),
            CornerRadius = new CornerRadius(14),
            Background = GoldFill(),
            BorderBrush = Hex("#B07A00"),
            BorderThickness = new Thickness(0, 0, 0, 3),
            Cursor = new Cursor(StandardCursorType.Hand)
        };
        _launchBorder.Effect = new DropShadowEffect { Color = Color.Parse("#FFC21A"), BlurRadius = 22, OffsetX = 0, OffsetY = 0, Opacity = 0.35 };
        _launchBorder.PointerPressed += OnLaunchClick;
        actions.Children.Add(_launchBorder);

        actions.Children.Add(LeanButton("play", "SEASONS", () => _nav("play")));
        actions.Children.Add(LeanButton("settings", "SETTINGS", () => _nav("settings")));

        var status = new StackPanel { Orientation = Orientation.Horizontal, Margin = new Thickness(12, 16, 0, 0) };
        status.Children.Add(new Border { Width = 8, Height = 8, CornerRadius = new CornerRadius(4), Background = Hex(Night.Good), VerticalAlignment = VerticalAlignment.Center, Margin = new Thickness(0, 0, 9, 0) });
        _statusText = NightText("", 11.5, Night.Sub, bold: true).Also(t => t.TextWrapping = TextWrapping.NoWrap);
        status.Children.Add(_statusText);

        var bottomActions = new StackPanel { HorizontalAlignment = HorizontalAlignment.Left, VerticalAlignment = VerticalAlignment.Bottom, Margin = new Thickness(60, 12, 24, 28) };
        bottomActions.Children.Add(new Viewbox { Child = actions, StretchDirection = StretchDirection.DownOnly, HorizontalAlignment = HorizontalAlignment.Left });
        bottomActions.Children.Add(status);
        Grid.SetRow(bottomActions, 1);
        view.Children.Add(bottomActions);

        var side = new StackPanel { Width = 348 };
        side.Children.Add(TiltTile(PlayerTile(), new Thickness(18, 16, 18, 16)));
        side.Children.Add(TiltTile(ShortcutsTile(), new Thickness(12, 14, 12, 10)).Also(t => t.Margin = new Thickness(0, 12, 0, 0)));
        side.Children.Add(TiltTile(FriendsTile(), new Thickness(16, 14, 16, 14)).Also(t => t.Margin = new Thickness(0, 12, 0, 0)));
        var sideFit = new Viewbox { Child = side, StretchDirection = StretchDirection.DownOnly, VerticalAlignment = VerticalAlignment.Top, HorizontalAlignment = HorizontalAlignment.Right, Margin = new Thickness(0, 8, 0, 10) };
        Grid.SetColumn(sideFit, 1);
        view.Children.Add(sideFit);

        RefreshLaunchState();
        return view;
    }

    // The launch button doubles as "link your Fortnite build" until one is linked,
    // mirroring the Windows launcher (PLAY becomes LINK FOLDER when no game is set).
    private void RefreshLaunchState()
    {
        bool linked = Config.LooksLikeBuild(Config.GameFolder);
        _launchText.Text = linked ? "LAUNCH" : "LINK FORTNITE FOLDER";
        _statusText.Text = linked
            ? $"LINKED  ·  {Path.GetFileName(Config.GameFolder!.TrimEnd('/'))}"
            : "NO BUILD LINKED  ·  CLICK TO CHOOSE YOUR FORTNITE FOLDER";
    }

    private async void OnLaunchClick(object? sender, PointerPressedEventArgs e)
    {
        if (!e.GetCurrentPoint(_launchBorder).Properties.IsLeftButtonPressed) return;

        if (!Config.LooksLikeBuild(Config.GameFolder))
        {
            await PickBuild();
            return;
        }

        // A build is linked. The launch engine (backend + redirect + injection) isn't wired
        // into the launcher yet — that's the next step — so for now we're honest about it
        // instead of flashing and doing nothing.
        _statusText.Text = "BUILD LINKED · LAUNCH PIPELINE NOT WIRED INTO THE LAUNCHER YET";
    }

    private async Task PickBuild()
    {
        var top = TopLevel.GetTopLevel(this);
        if (top is null) return;
        var picked = await top.StorageProvider.OpenFolderPickerAsync(new FolderPickerOpenOptions
        {
            Title = "Select your Fortnite build folder (the one with FortniteGame and Engine inside)",
            AllowMultiple = false
        });
        if (picked.Count == 0) return;

        string? path = picked[0].TryGetLocalPath();
        if (string.IsNullOrWhiteSpace(path)) { _statusText.Text = "COULDN'T READ THAT FOLDER — TRY ANOTHER"; return; }

        if (!Config.LooksLikeBuild(path))
        {
            _statusText.Text = "THAT FOLDER ISN'T A FORTNITE BUILD (NO FortniteClient-Win64-Shipping.exe)";
            return;
        }

        Config.SetGameFolder(path);
        RefreshLaunchState();
    }

    private Control LeanButton(string icon, string label, Action onClick)
    {
        var row = new StackPanel { Orientation = Orientation.Horizontal, HorizontalAlignment = HorizontalAlignment.Center };
        row.Children.Add(Icon(icon, 13, Night.Text).Also(g => g.Margin = new Thickness(0, 0, 10, 0)));
        row.Children.Add(Display(label, 17, Night.Text).Also(t => t.VerticalAlignment = VerticalAlignment.Center));
        var b = new Border
        {
            Child = row,
            Height = 62,
            Padding = new Thickness(24, 0, 24, 0),
            Margin = new Thickness(14, 0, 0, 0),
            CornerRadius = new CornerRadius(12),
            Background = Hex("#161B2B"),
            BorderBrush = Hex("#2BFFFFFF"),
            BorderThickness = new Thickness(1),
            Cursor = new Cursor(StandardCursorType.Hand)
        };
        b.PointerEntered += (_, _) => b.BorderBrush = Hex(Night.Gold);
        b.PointerExited += (_, _) => b.BorderBrush = Hex("#2BFFFFFF");
        b.PointerPressed += (_, e) => { if (e.GetCurrentPoint(b).Properties.IsLeftButtonPressed) onClick(); };
        return b;
    }

    private Control PlayerTile()
    {
        var player = new StackPanel();
        player.Children.Add(NightEyebrow("Player"));

        var who = new Grid { Margin = new Thickness(0, 12, 0, 12) };
        who.ColumnDefinitions.Add(new ColumnDefinition(GridLength.Auto));
        who.ColumnDefinitions.Add(new ColumnDefinition(GridLength.Star));
        who.Children.Add(new Border { Width = 48, Height = 48, CornerRadius = new CornerRadius(24), Background = BrandFill() });
        var whoText = new StackPanel { VerticalAlignment = VerticalAlignment.Center, Margin = new Thickness(12, 0, 0, 0) };
        whoText.Children.Add(Display(PlayerName, 22, Night.Text));
        whoText.Children.Add(NightText("Local profile  ·  Active", 11.5, Night.Sub).Also(t => t.TextWrapping = TextWrapping.NoWrap));
        Grid.SetColumn(whoText, 1);
        who.Children.Add(whoText);
        player.Children.Add(who);

        var levelHead = new Grid { Margin = new Thickness(0, 2, 0, 7) };
        levelHead.Children.Add(NightText($"LEVEL {Level}", 12, Night.Gold, bold: true).Also(t => t.VerticalAlignment = VerticalAlignment.Center));
        levelHead.Children.Add(NightText($"{Xp:N0} / {XpToNext:N0} XP", 11, Night.Muted).Also(t => { t.HorizontalAlignment = HorizontalAlignment.Right; t.VerticalAlignment = VerticalAlignment.Center; t.TextWrapping = TextWrapping.NoWrap; }));
        player.Children.Add(levelHead);

        double frac = Math.Min(1.0, Math.Max(0.0, Xp / (double)XpToNext));
        var xpBar = new Grid { Height = 7, Margin = new Thickness(0, 0, 0, 14) };
        xpBar.ColumnDefinitions.Add(new ColumnDefinition(frac, GridUnitType.Star));
        xpBar.ColumnDefinitions.Add(new ColumnDefinition(Math.Max(0.0001, 1 - frac), GridUnitType.Star));
        var track = new Border { Background = Hex("#33000000"), CornerRadius = new CornerRadius(4) };
        Grid.SetColumnSpan(track, 2);
        xpBar.Children.Add(track);
        var fill = new Border { CornerRadius = new CornerRadius(4), Background = Linear(Night.GoldDeep, Night.Gold, 0) };
        Grid.SetColumn(fill, 0);
        xpBar.Children.Add(fill);
        player.Children.Add(xpBar);

        var edit = NightText("EDIT PROFILE", 11.5, Night.Text, bold: true).Also(t =>
        {
            t.HorizontalAlignment = HorizontalAlignment.Center;
            t.VerticalAlignment = VerticalAlignment.Center;
            t.TextWrapping = TextWrapping.NoWrap;
        });
        var editBtn = new Border
        {
            Child = edit,
            Height = 32,
            Background = Hex(Night.Surface2),
            BorderBrush = Hex(Night.Line),
            BorderThickness = new Thickness(1),
            CornerRadius = new CornerRadius(8, 2, 8, 2),
            HorizontalAlignment = HorizontalAlignment.Stretch,
            Cursor = new Cursor(StandardCursorType.Hand)
        };
        editBtn.PointerPressed += (_, e) => { if (e.GetCurrentPoint(editBtn).Properties.IsLeftButtonPressed) _nav("settings"); };
        player.Children.Add(editBtn);
        return player;
    }

    private Control ShortcutsTile()
    {
        var shortcuts = new StackPanel();
        shortcuts.Children.Add(NightEyebrow("Jump to").Also(t => t.Margin = new Thickness(0, 0, 0, 6)));
        shortcuts.Children.Add(Shortcut("hosting", "Host a match for friends", () => _nav("hosting")));
        shortcuts.Children.Add(Shortcut("store", "Battle passes & packs", () => _nav("store")));
        shortcuts.Children.Add(Shortcut("stats", "Your stats", () => _nav("stats")));
        return shortcuts;
    }

    private Control Shortcut(string icon, string text, Action onClick)
    {
        var row = new Grid { Height = 34 };
        row.ColumnDefinitions.Add(new ColumnDefinition(26, GridUnitType.Pixel));
        row.ColumnDefinitions.Add(new ColumnDefinition(GridLength.Star));
        row.ColumnDefinitions.Add(new ColumnDefinition(GridLength.Auto));
        row.Children.Add(Icon(icon, 13, Night.Gold).Also(g => g.HorizontalAlignment = HorizontalAlignment.Left));
        var label = NightText(text, 12.5, Night.Text, bold: true).Also(t => { t.TextWrapping = TextWrapping.NoWrap; t.VerticalAlignment = VerticalAlignment.Center; });
        Grid.SetColumn(label, 1);
        row.Children.Add(label);
        var arrow = Icon("arrow", 11, Night.Muted);
        Grid.SetColumn(arrow, 2);
        row.Children.Add(arrow);
        var b = new Border { Child = row, Padding = new Thickness(8, 0, 8, 0), CornerRadius = new CornerRadius(8, 2, 8, 2), Cursor = new Cursor(StandardCursorType.Hand) };
        Hover(b, "#00000000", "#12FFFFFF");
        b.PointerPressed += (_, e) => { if (e.GetCurrentPoint(b).Properties.IsLeftButtonPressed) onClick(); };
        return b;
    }

    private Control FriendsTile()
    {
        var friends = new StackPanel();
        friends.Children.Add(NightEyebrow("Friends").Also(t => t.Margin = new Thickness(0, 0, 0, 10)));
        var wrap = new StackPanel { HorizontalAlignment = HorizontalAlignment.Center };
        wrap.Children.Add(new Border { Width = 42, Height = 42, CornerRadius = new CornerRadius(21), Background = Hex("#0DFFFFFF"), HorizontalAlignment = HorizontalAlignment.Center, Margin = new Thickness(0, 2, 0, 10), Child = Icon("friends", 18, Night.Muted) });
        wrap.Children.Add(NightText("No friends online", 12.5, Night.Text, bold: true).Also(t => { t.HorizontalAlignment = HorizontalAlignment.Center; t.TextWrapping = TextWrapping.NoWrap; }));
        wrap.Children.Add(NightText("Invite your friends to play!", 11, Night.Muted).Also(t => { t.HorizontalAlignment = HorizontalAlignment.Center; t.Margin = new Thickness(0, 3, 0, 12); }));
        var invite = NightText("INVITE FRIENDS", 11.5, Night.OnGold, bold: true).Also(t => { t.HorizontalAlignment = HorizontalAlignment.Center; t.VerticalAlignment = VerticalAlignment.Center; t.TextWrapping = TextWrapping.NoWrap; });
        var inviteBtn = new Border { Child = invite, Height = 34, Background = GoldFill(), CornerRadius = new CornerRadius(8, 2, 8, 2), HorizontalAlignment = HorizontalAlignment.Stretch, Cursor = new Cursor(StandardCursorType.Hand) };
        inviteBtn.PointerPressed += (_, e) => { if (e.GetCurrentPoint(inviteBtn).Properties.IsLeftButtonPressed) _nav("hosting"); };
        wrap.Children.Add(inviteBtn);
        friends.Children.Add(wrap);
        return friends;
    }
}
