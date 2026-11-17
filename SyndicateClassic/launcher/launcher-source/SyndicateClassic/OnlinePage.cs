// Syndicate © 2026 Syndicate. All rights reserved. Proprietary — no copying, distribution, or derivative works. See LICENSE.
using System;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Controls.Primitives;
using System.Windows.Media;
using System.Windows.Media.Effects;
namespace SyndicateClassic;

// "Online": official Syndicate servers. A work in progress (the servers are being bought), so for now this
// page only says what is coming and points to what works today (Multiplayer with friends, the Discord for news).
// When the servers exist, their list / queue goes here, in the Syndicate look like the rest (SyndicateHome.cs).
public partial class MainWindow
{
    private const int OnlineTab = 7;   // HighlightRail index (SyndicateHome.cs)
    private Grid? _onlineView;

    private Grid BuildOnlinePage()
    {
        var view = new Grid { Margin = new Thickness(RailWidth + 26, 56, 32, 18), ClipToBounds = true, Visibility = Visibility.Collapsed };
        _onlineView = view;
        view.Children.Add(new Border { Opacity = .28, IsHitTestVisible = false,
            Background = new RadialGradientBrush(NightColor("#4CC2FF"), Color.FromArgb(0, 0, 0, 0)) { Center = new Point(.55, .5), GradientOrigin = new Point(.55, .5), RadiusX = .45, RadiusY = .5 } });
        view.Children.Add(Display("ONLINE", 300, Night.Text).Also(t => { t.Opacity = .05; t.IsHitTestVisible = false; t.HorizontalAlignment = HorizontalAlignment.Right; t.VerticalAlignment = VerticalAlignment.Bottom; t.Margin = new Thickness(0, 0, -10, -60); t.RenderTransform = new SkewTransform(-14, 0); }));

        var page = new StackPanel { Margin = new Thickness(34, 10, 24, 0), VerticalAlignment = VerticalAlignment.Center, MaxWidth = 980, HorizontalAlignment = HorizontalAlignment.Left };
        var tagRow = new StackPanel { Orientation = Orientation.Horizontal, Margin = new Thickness(4, 0, 0, 12) };
        tagRow.Children.Add(new Border { Background = Hex(Night.Gold), Padding = new Thickness(12, 4, 12, 4), RenderTransform = new SkewTransform(-14, 0),
            Child = NightText("COMING SOON", 11, Night.OnGold, bold: true).Also(t => t.TextWrapping = TextWrapping.NoWrap) });
        tagRow.Children.Add(new Border { Background = Hex("#14FFFFFF"), BorderBrush = Hex("#33FFFFFF"), BorderThickness = new Thickness(1), Padding = new Thickness(12, 3, 12, 3), Margin = new Thickness(14, 0, 0, 0), RenderTransform = new SkewTransform(-14, 0),
            Child = NightText("WORK IN PROGRESS", 11, Night.Sub, bold: true).Also(t => t.TextWrapping = TextWrapping.NoWrap) });
        page.Children.Add(tagRow);
        var title = Display("PLAY ONLINE", 110, Night.Text);
        title.Effect = new DropShadowEffect { Color = NightColor("#4CC2FF"), BlurRadius = 40, ShadowDepth = 0, Opacity = .3 };
        page.Children.Add(new Viewbox { Child = title, StretchDirection = StretchDirection.DownOnly, HorizontalAlignment = HorizontalAlignment.Left, MaxHeight = 140 });
        page.Children.Add(Body("Official Syndicate servers are on the way. We're setting up our own dedicated servers so you can queue into matches with everyone, not only friends on your network. When they're ready they'll show up right here.", 15, "#C3CAE3")
            .Also(t => { t.MaxWidth = 640; t.LineHeight = 23; t.HorizontalAlignment = HorizontalAlignment.Left; t.Margin = new Thickness(8, 4, 0, 26); }));

        // What it will be, and where it stands.
        var tiles = new UniformGrid { Columns = 3, Margin = new Thickness(8, 0, 0, 26) };
        void Tile(string glyph, string name, string text, string state, string stateColor, int index)
        {
            var body = new StackPanel();
            var head = new Grid();
            head.Children.Add(GlyphIcon(glyph, 20, Night.Gold).Also(g => g.HorizontalAlignment = HorizontalAlignment.Left));
            head.Children.Add(new Border { Background = Hex("#1AFFFFFF"), CornerRadius = new CornerRadius(8, 2, 8, 2), Padding = new Thickness(8, 2, 8, 2), HorizontalAlignment = HorizontalAlignment.Right,
                Child = NightText(state, 10, stateColor, bold: true).Also(t => t.TextWrapping = TextWrapping.NoWrap) });
            body.Children.Add(head);
            body.Children.Add(Display(name, 24, Night.Text).Also(t => t.Margin = new Thickness(0, 12, 0, 2)));
            body.Children.Add(Body(text, 12.5, Night.Sub).Also(t => t.LineHeight = 18));
            tiles.Children.Add(TiltTile(body, new Thickness(18, 18, 18, 18)).Also(t => t.Margin = new Thickness(index == 0 ? 0 : 7, 0, index == 2 ? 0 : 7, 0)));
        }
        Tile("", "Official servers", "Dedicated Syndicate servers, always on, so a match never depends on someone's PC.", "IN PROGRESS", Night.Gold, 0);
        Tile("", "Public matchmaking", "Pick a season and playlist, press Play, and get put in a lobby with other players.", "PLANNED", Night.Sky, 1);
        Tile("", "Your stats online", "Wins, kills and progress from online matches, next to the stats you already have.", "PLANNED", Night.Sky, 2);
        page.Children.Add(tiles);

        // What works today.
        var actions = new StackPanel { Orientation = Orientation.Horizontal, Margin = new Thickness(10, 0, 0, 0) };
        var friends = new Button { Content = Display("PLAY WITH FRIENDS NOW", 20, Night.OnGold), Height = 54, Padding = new Thickness(28, 0, 28, 0), ToolTip = "Host or join a friend's match (Multiplayer)" };
        NightButton(friends, "gold");
        friends.Padding = new Thickness(28, 0, 28, 0);
        friends.Click += (_, __) => { HighlightRail(3); FortniteTab_Click(this, new RoutedEventArgs()); };
        actions.Children.Add(friends);
        var news = new Button { Content = Display("NEWS ON DISCORD", 17, Night.Text), Height = 54, Padding = new Thickness(24, 0, 24, 0), Margin = new Thickness(14, 0, 0, 0), ToolTip = "Announcements come on the Syndicate Discord" };
        RoundButton(news, "#B30C0F1A", Night.Text, 4, "#40FFFFFF");
        news.BorderThickness = new Thickness(1);
        news.Template = TiltButtonTemplate();
        news.MouseEnter += (_, __) => news.BorderBrush = Hex(Night.Gold); news.MouseLeave += (_, __) => news.BorderBrush = Hex("#40FFFFFF");
        news.Click += (_, __) => OpenLink(DiscordPresence.DiscordInvite);
        actions.Children.Add(news);
        page.Children.Add(new Viewbox { Child = actions, StretchDirection = StretchDirection.DownOnly, HorizontalAlignment = HorizontalAlignment.Left });
        view.Children.Add(new ScrollViewer { Content = page, VerticalScrollBarVisibility = ScrollBarVisibility.Auto, HorizontalScrollBarVisibility = ScrollBarVisibility.Disabled });
        return view;
    }
}
