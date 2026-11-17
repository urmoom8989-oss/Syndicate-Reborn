// Syndicate © 2026 Syndicate. All rights reserved. Proprietary — see LICENSE.
// The launcher shell: a persistent left nav rail + a content area that swaps pages when
// you click, ported from the WPF HighlightRail/BuildRail navigation. Home is fully laid
// out (HomePage); the other pages are titled placeholders until each is ported in turn.
using System;
using System.Collections.Generic;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Input;
using Avalonia.Layout;
using Avalonia.Media;
using Avalonia.Media.Imaging;
using Avalonia.Platform;
using static SyndicateLauncher.Linux.Ui;

namespace SyndicateLauncher.Linux;

public class Shell : UserControl
{
    public const double RailWidth = 232;

    // Placeholder selected-season data (shared with HomePage until live data is wired).
    public const int SeasonNumber = 4;
    public const string SeasonLabel = "SEASON 4";
    public const string Build = "4.5";
    public static string Accent => Night.SeasonAccent(SeasonNumber);

    private readonly ContentControl _host = new();
    private readonly Dictionary<string, Action<bool>> _navSetters = new();
    private string _active = "";

    public Shell()
    {
        _host.HorizontalContentAlignment = HorizontalAlignment.Stretch;
        _host.VerticalContentAlignment = VerticalAlignment.Stretch;
        var root = new Grid();
        root.Children.Add(Backdrop());
        root.Children.Add(_host);                 // pages inset themselves past the rail
        root.Children.Add(BuildRail());
        Content = root;
        Background = Hex(Night.Bg);
        Navigate("home");
    }

    public void Navigate(string key)
    {
        _active = key;
        _host.Content = PageFor(key);
        foreach (var kv in _navSetters) kv.Value(kv.Key == key);
    }

    private Control PageFor(string key) => key switch
    {
        "home" => new HomePage(Navigate),
        "play" => Placeholder("Pick a season", "PLAY", "Chapter and season browser — porting next. This is where you choose which build to launch."),
        "store" => Placeholder("Shop", "STORE", "Battle passes, packs and exclusives. Porting from the Windows store screen."),
        "hosting" => Placeholder("Play together", "HOSTING", "Host a match for friends or join theirs (Radmin / direct connect)."),
        "online" => Placeholder("Official servers", "ONLINE", "Official Syndicate servers — coming soon."),
        "stats" => Placeholder("Your career", "STATS", "Time played, matches, wins and kills across every season."),
        "settings" => Placeholder("Launcher", "SETTINGS", "Launcher, match and bot settings. Porting from the Windows settings screen."),
        _ => new HomePage(Navigate)
    };

    private Control Placeholder(string eyebrow, string title, string body)
    {
        var s = new StackPanel { Margin = new Thickness(RailWidth + 44, 84, 44, 0) };
        s.Children.Add(NightEyebrow(eyebrow));
        s.Children.Add(NightTitle(title, 46).Also(t => t.Margin = new Thickness(0, 6, 0, 12)));
        s.Children.Add(NightText(body, 14, Night.Sub).Also(t => { t.MaxWidth = 560; t.LineHeight = 22; }));
        return s;
    }

    // ---- backdrop -------------------------------------------------------------------
    private Control Backdrop()
    {
        var g = new Grid();
        g.Children.Add(new Border
        {
            Background = new LinearGradientBrush
            {
                StartPoint = new RelativePoint(0, 0, RelativeUnit.Relative),
                EndPoint = new RelativePoint(0, 1, RelativeUnit.Relative),
                GradientStops =
                {
                    new GradientStop(Color.Parse("#0D1120"), 0),
                    new GradientStop(Color.Parse(Night.Bg), 0.7)
                }
            }
        });
        g.Children.Add(new Border
        {
            IsHitTestVisible = false,
            Opacity = 0.30,
            HorizontalAlignment = HorizontalAlignment.Left,
            VerticalAlignment = VerticalAlignment.Bottom,
            Width = 760,
            Height = 520,
            Margin = new Thickness(-160, 0, 0, -160),
            Background = new RadialGradientBrush
            {
                GradientOrigin = new RelativePoint(0.5, 0.5, RelativeUnit.Relative),
                Center = new RelativePoint(0.5, 0.5, RelativeUnit.Relative),
                GradientStops =
                {
                    new GradientStop(Color.Parse(Accent), 0),
                    new GradientStop(Color.Parse("#00000000"), 1)
                }
            }
        });
        return g;
    }

    // ---- rail -----------------------------------------------------------------------
    private Control BuildRail()
    {
        var column = new Grid { Width = RailWidth, HorizontalAlignment = HorizontalAlignment.Left };
        column.Background = Linear("#F506070C", "#E006070C", 0);
        column.RowDefinitions.Add(new RowDefinition(GridLength.Auto));
        column.RowDefinitions.Add(new RowDefinition(GridLength.Star));
        column.RowDefinitions.Add(new RowDefinition(GridLength.Auto));
        var edge = new Border { Width = 1, HorizontalAlignment = HorizontalAlignment.Right, Background = Linear("#80FFD23F", "#14FFFFFF", 90) };
        Grid.SetRowSpan(edge, 3);
        column.Children.Add(edge);

        var brand = new StackPanel { Orientation = Orientation.Horizontal, Margin = new Thickness(22, 20, 16, 4) };
        brand.Children.Add(LogoOrWordmark(54));
        var names = new StackPanel { VerticalAlignment = VerticalAlignment.Center, Margin = new Thickness(14, 0, 0, 0) };
        names.Children.Add(NightText("EVERY SEASON.", 10, Night.Sub, bold: true).Also(t => t.TextWrapping = TextWrapping.NoWrap));
        names.Children.Add(NightText("YOUR WAY.", 10, Night.Gold, bold: true).Also(t => t.TextWrapping = TextWrapping.NoWrap));
        brand.Children.Add(names);
        column.Children.Add(brand);

        var nav = new StackPanel { Margin = new Thickness(12, 22, 12, 0) };
        int n = 0;
        nav.Children.Add(NavRow(ref n, "home", "HOME"));
        nav.Children.Add(NavRow(ref n, "play", "PLAY"));
        nav.Children.Add(NavRow(ref n, "store", "STORE"));
        nav.Children.Add(NavRow(ref n, "hosting", "HOSTING"));
        nav.Children.Add(NavRow(ref n, "online", "ONLINE", soon: true));
        nav.Children.Add(NavRow(ref n, "stats", "STATS"));
        nav.Children.Add(NavRow(ref n, "settings", "SETTINGS"));
        nav.Children.Add(new Border { Height = 1, Background = Hex("#14FFFFFF"), Margin = new Thickness(18, 12, 18, 10) });
        nav.Children.Add(SmallRow("discord", "Discord"));
        nav.Children.Add(SmallRow("log", "Log"));
        Grid.SetRow(nav, 1);
        column.Children.Add(nav);

        var bottom = new StackPanel { Margin = new Thickness(14, 0, 14, 14) };
        bottom.Children.Add(SeasonCard());
        bottom.Children.Add(PlayerFooter());
        Grid.SetRow(bottom, 2);
        column.Children.Add(bottom);

        return column;
    }

    private Control LogoOrWordmark(double height)
    {
        try
        {
            using var stream = AssetLoader.Open(new Uri("avares://SyndicateLauncher/assets/logo.png"));
            var bmp = new Bitmap(stream);
            return new Image { Source = bmp, Height = height, MaxWidth = 130, Stretch = Stretch.Uniform, VerticalAlignment = VerticalAlignment.Center };
        }
        catch
        {
            return Display("SYNDICATE", 30, Night.Gold);
        }
    }

    private Control NavRow(ref int number, string key, string label, bool soon = false)
    {
        number++;
        var row = new Grid { Height = 42 };
        row.ColumnDefinitions.Add(new ColumnDefinition(12, GridUnitType.Pixel));
        row.ColumnDefinitions.Add(new ColumnDefinition(30, GridUnitType.Pixel));
        row.ColumnDefinitions.Add(new ColumnDefinition(GridLength.Star));
        row.ColumnDefinitions.Add(new ColumnDefinition(GridLength.Auto));

        var bar = TiltBar(4, 20).Also(b => { b.HorizontalAlignment = HorizontalAlignment.Left; b.IsVisible = false; });
        row.Children.Add(bar);
        var index = NightText(number.ToString("00"), 11, Night.Muted, bold: true).Also(t => { t.VerticalAlignment = VerticalAlignment.Center; t.TextWrapping = TextWrapping.NoWrap; });
        Grid.SetColumn(index, 1);
        row.Children.Add(index);
        var text = Display(label, 17, Night.Sub).Also(t => t.VerticalAlignment = VerticalAlignment.Center);
        Grid.SetColumn(text, 2);
        row.Children.Add(text);

        TextBlock? icon = null;
        if (soon)
        {
            var tag = new Border
            {
                Background = Hex("#26FFD23F"),
                BorderBrush = Hex("#66FFD23F"),
                BorderThickness = new Thickness(1),
                CornerRadius = new CornerRadius(6, 2, 6, 2),
                Padding = new Thickness(6, 1, 6, 1),
                VerticalAlignment = VerticalAlignment.Center,
                Margin = new Thickness(0, 0, 6, 0),
                Child = NightText("SOON", 9, Night.Gold, bold: true).Also(t => t.TextWrapping = TextWrapping.NoWrap)
            };
            Grid.SetColumn(tag, 3);
            row.Children.Add(tag);
        }
        else
        {
            icon = Icon(key, 13, Night.Muted);
            Grid.SetColumn(icon, 3);
            row.Children.Add(icon);
        }

        var border = new Border
        {
            Child = row,
            Margin = new Thickness(0, 1, 0, 1),
            Padding = new Thickness(6, 0, 14, 0),
            CornerRadius = new CornerRadius(10, 3, 10, 3),
            Background = Hex("#00000000"),
            Cursor = new Cursor(StandardCursorType.Hand)
        };

        void SetActive(bool on)
        {
            bar.IsVisible = on;
            index.Foreground = Hex(on ? Night.Gold : Night.Muted);
            text.Foreground = Hex(on ? Night.Text : Night.Sub);
            if (icon != null) icon.Foreground = Hex(on ? Night.Gold : Night.Muted);
            border.Background = Hex(on ? "#14FFD23F" : "#00000000");
        }
        _navSetters[key] = SetActive;

        border.PointerEntered += (_, _) => { if (_active != key) border.Background = Hex("#0FFFFFFF"); };
        border.PointerExited += (_, _) => { if (_active != key) border.Background = Hex("#00000000"); };
        border.PointerPressed += (_, e) => { if (e.GetCurrentPoint(border).Properties.IsLeftButtonPressed) Navigate(key); };
        return border;
    }

    private Control SmallRow(string icon, string label)
    {
        var row = new StackPanel { Orientation = Orientation.Horizontal };
        row.Children.Add(Icon(icon, 12, Night.Muted).Also(g => g.Margin = new Thickness(0, 0, 10, 0)));
        row.Children.Add(NightText(label, 12, Night.Sub, bold: true).Also(t => { t.TextWrapping = TextWrapping.NoWrap; t.VerticalAlignment = VerticalAlignment.Center; }));
        var b = new Border { Child = row, Height = 32, Padding = new Thickness(18, 0, 10, 0), CornerRadius = new CornerRadius(8, 2, 8, 2), Cursor = new Cursor(StandardCursorType.Hand) };
        Hover(b, "#00000000", "#0FFFFFFF");
        return b;
    }

    private Control SeasonCard()
    {
        var card = new Grid { Height = 96, Margin = new Thickness(0, 0, 0, 12) };
        card.Children.Add(new Border { CornerRadius = new CornerRadius(14, 3, 14, 3), Background = Linear(Accent, "#0A0C14", 60) });
        card.Children.Add(new Border { CornerRadius = new CornerRadius(14, 3, 14, 3), Background = Linear("#1E06070C", "#F006070C", 90) });
        var words = new StackPanel { VerticalAlignment = VerticalAlignment.Bottom, Margin = new Thickness(14, 0, 14, 10) };
        words.Children.Add(NightEyebrow("Now selected"));
        words.Children.Add(Display(SeasonLabel, 22, Night.Text).Also(t => t.Margin = new Thickness(0, 2, 0, 0)));
        words.Children.Add(NightText("Fortnite v" + Build, 10.5, Night.Sub).Also(t => t.TextWrapping = TextWrapping.NoWrap));
        card.Children.Add(words);
        var outer = new Border
        {
            Child = card,
            CornerRadius = new CornerRadius(14, 3, 14, 3),
            BorderBrush = Hex("#26FFFFFF"),
            BorderThickness = new Thickness(1),
            Cursor = new Cursor(StandardCursorType.Hand),
            ClipToBounds = true
        };
        outer.PointerEntered += (_, _) => outer.BorderBrush = Hex(Night.Gold);
        outer.PointerExited += (_, _) => outer.BorderBrush = Hex("#26FFFFFF");
        outer.PointerPressed += (_, e) => { if (e.GetCurrentPoint(outer).Properties.IsLeftButtonPressed) Navigate("play"); };
        return outer;
    }

    private Control PlayerFooter()
    {
        var me = new Grid();
        me.ColumnDefinitions.Add(new ColumnDefinition(GridLength.Auto));
        me.ColumnDefinitions.Add(new ColumnDefinition(GridLength.Star));
        me.ColumnDefinitions.Add(new ColumnDefinition(GridLength.Auto));

        var avatar = new Border { Width = 40, Height = 40, CornerRadius = new CornerRadius(20), Background = BrandFill(), Padding = new Thickness(2) };
        avatar.Child = new Border { CornerRadius = new CornerRadius(18), Background = Hex(Night.Surface2), Child = Icon("friends", 14, Night.Text) };
        me.Children.Add(avatar);

        var meText = new StackPanel { VerticalAlignment = VerticalAlignment.Center, Margin = new Thickness(10, 0, 6, 0) };
        meText.Children.Add(NightText("UNKN0WN", 12.5, Night.Text, bold: true).Also(t => t.TextWrapping = TextWrapping.NoWrap));
        meText.Children.Add(NightText("v1.0.0", 10.5, Night.Muted).Also(t => t.TextWrapping = TextWrapping.NoWrap));
        Grid.SetColumn(meText, 1);
        me.Children.Add(meText);

        var update = new Border { Width = 34, Height = 34, CornerRadius = new CornerRadius(17), Child = Icon("update", 13, Night.Sub), VerticalAlignment = VerticalAlignment.Center, Cursor = new Cursor(StandardCursorType.Hand) };
        Hover(update, "#00000000", "#C21B2033");
        Grid.SetColumn(update, 2);
        me.Children.Add(update);
        return me;
    }
}
