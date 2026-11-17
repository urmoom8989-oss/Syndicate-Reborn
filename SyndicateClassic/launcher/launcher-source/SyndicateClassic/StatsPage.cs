// Syndicate © 2026 Syndicate. All rights reserved. Proprietary — no copying, distribution, or derivative works. See LICENSE.
using System;
using System.Text.Json;
using System.Threading.Tasks;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Controls.Primitives;
using System.Windows.Media;

namespace SyndicateClassic;

// Stats tab (side rail): the signed-in player's Syndicate career, read from the backend
// (POST launcher/stats, recorded after every match in every season): time played,
// matches, wins, kills and top placements for Solo, Duos and Squads, plus totals.
public partial class MainWindow
{
    private Grid? _statsView;
    private StackPanel? _statsBody;
    private bool _statsBusy;

    private Grid BuildStatsPage()
    {
        var page = new Grid { Background = Brushes.Transparent };
        _statsView = page;
        var scroll = new ScrollViewer { VerticalScrollBarVisibility = ScrollBarVisibility.Auto, HorizontalScrollBarVisibility = ScrollBarVisibility.Disabled, Margin = new Thickness(RailWidth, 52, 0, 0) };
        RailTrack(scroll);   // reflow with the menu (RailCollapse.cs)
        scroll.Resources.Add(typeof(ScrollBar), DarkScrollBar());
        page.Children.Add(scroll);
        var outer = new StackPanel { Margin = new Thickness(22, 8, 40, 28) };
        scroll.Content = outer;
        outer.Children.Add(NightEyebrow("Your career"));
        outer.Children.Add(NightTitle("Stats", 40).Also(t => t.Margin = new Thickness(0, 2, 0, 4)));
        outer.Children.Add(NightText("Every Syndicate match you finish, in every season, added up for the player you are signed in as.", 13.5, Night.Sub).Also(t => { t.Margin = new Thickness(0, 0, 0, 20); t.TextWrapping = TextWrapping.Wrap; }));
        _statsBody = new StackPanel();
        outer.Children.Add(_statsBody);
        return page;
    }

    private static string Hours(long seconds)
    {
        long minutes = seconds / 60;
        return minutes >= 60 ? $"{minutes / 60:N0}h {minutes % 60}m" : $"{minutes}m";
    }

    private async Task RefreshStats()
    {
        if (_statsBody == null || _statsBusy) return;
        _statsBusy = true;
        try
        {
            if (!_account.SignedIn)
            {
                _statsBody.Children.Clear();
                _statsBody.Children.Add(NightText("Sign in to a player to see their stats.", 14, Night.Sub));
                return;
            }
            if (_statsBody.Children.Count == 0) _statsBody.Children.Add(NightText("Loading…", 14, Night.Sub));
            JsonElement data;
            try { data = await _account.Stats(); }
            catch (Exception error)
            {
                _statsBody.Children.Clear();
                _statsBody.Children.Add(NightText("Could not load stats: " + error.Message, 14, Night.Danger).Also(t => t.TextWrapping = TextWrapping.Wrap));
                return;
            }
            DrawStats(data);
        }
        finally { _statsBusy = false; }
    }

    private void DrawStats(JsonElement data)
    {
        if (_statsBody == null) return;
        _statsBody.Children.Clear();
        long Num(JsonElement row, string name) => row.TryGetProperty(name, out var v) && v.ValueKind == JsonValueKind.Number ? v.GetInt64() : 0;
        var total = data.GetProperty("total");
        var modes = data.GetProperty("modes");

        // Leaning tiles (SyndicateHome.cs), each stat a big leaning number with a coloured bar under its label.
        Border Card(UIElement child, string accent) => TiltTile(child, new Thickness(22, 18, 22, 18)).Also(t => t.Margin = new Thickness(0, 0, 0, 14));
        FrameworkElement Tile(string label, string value, string color)
        {
            var s = new StackPanel { Margin = new Thickness(0, 0, 32, 8), MinWidth = 118 };
            var head = new StackPanel { Orientation = Orientation.Horizontal };
            head.Children.Add(TiltBar(3, 10, color).Also(b => b.Margin = new Thickness(0, 0, 8, 0)));
            head.Children.Add(NightText(label.ToUpperInvariant(), 11, Night.Muted, bold: true).Also(t => t.TextWrapping = TextWrapping.NoWrap));
            s.Children.Add(head);
            s.Children.Add(Display(value, 34).Also(t => { t.Foreground = Hex(color); t.Margin = new Thickness(2, 2, 0, 0); }));
            return s;
        }

        // Totals across all modes.
        long tMatches = Num(total, "matches"), tWins = Num(total, "wins");
        var head = new WrapPanel();
        head.Children.Add(Tile("Time played", Hours(Num(total, "seconds")), Night.Gold));
        head.Children.Add(Tile("Matches", $"{tMatches:N0}", Night.Text));
        head.Children.Add(Tile("Wins", $"{tWins:N0}", Night.Good));
        head.Children.Add(Tile("Kills", $"{Num(total, "kills"):N0}", Night.Text));
        head.Children.Add(Tile("Win rate", tMatches > 0 ? $"{100.0 * tWins / tMatches:0.#}%" : "–", Night.Sky));
        var totalBox = new StackPanel();
        totalBox.Children.Add(NightTitle("All modes", 22).Also(t => t.Margin = new Thickness(0, 0, 0, 12)));
        totalBox.Children.Add(head);
        _statsBody.Children.Add(Card(totalBox, Night.Gold));

        // One card per mode.
        foreach (var (key, title, topName, wideName) in new[] { ("solo", "Solo", "Top 10", "Top 25"), ("duo", "Duos", "Top 5", "Top 12"), ("squad", "Squads", "Top 3", "Top 6") })
        {
            var row = modes.TryGetProperty(key, out var r) ? r : default;
            long matches = row.ValueKind == JsonValueKind.Object ? Num(row, "matches") : 0, wins = row.ValueKind == JsonValueKind.Object ? Num(row, "wins") : 0;
            long kills = row.ValueKind == JsonValueKind.Object ? Num(row, "kills") : 0, deaths = Math.Max(1, matches - wins);
            var box = new StackPanel();
            var titleRow = new DockPanel { Margin = new Thickness(0, 0, 0, 12) };
            titleRow.Children.Add(NightTitle(title, 22));
            box.Children.Add(titleRow);
            var tiles = new WrapPanel();
            tiles.Children.Add(Tile("Time played", row.ValueKind == JsonValueKind.Object ? Hours(Num(row, "seconds")) : "0m", Night.Gold));
            tiles.Children.Add(Tile("Matches", $"{matches:N0}", Night.Text));
            tiles.Children.Add(Tile("Wins", $"{wins:N0}", Night.Good));
            tiles.Children.Add(Tile("Win rate", matches > 0 ? $"{100.0 * wins / matches:0.#}%" : "–", Night.Sky));
            tiles.Children.Add(Tile("Kills", $"{kills:N0}", Night.Text));
            tiles.Children.Add(Tile("K/D", matches > 0 ? $"{(double)kills / deaths:0.00}" : "–", Night.Text));
            tiles.Children.Add(Tile(topName, row.ValueKind == JsonValueKind.Object ? $"{Num(row, "top"):N0}" : "0", Night.Sub));
            tiles.Children.Add(Tile(wideName, row.ValueKind == JsonValueKind.Object ? $"{Num(row, "topWide"):N0}" : "0", Night.Sub));
            box.Children.Add(tiles);
            _statsBody.Children.Add(Card(box, Night.Violet));
        }
        if (tMatches == 0)
            _statsBody.Children.Add(NightText("No finished matches yet. Stats are added when a match ends (win, elimination or leaving after you are out).", 12.5, Night.Muted).Also(t => t.TextWrapping = TextWrapping.Wrap));
    }
}