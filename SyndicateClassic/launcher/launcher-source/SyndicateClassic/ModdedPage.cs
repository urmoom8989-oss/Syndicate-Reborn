// Syndicate © 2026 Syndicate. All rights reserved. Proprietary — no copying, distribution, or derivative works. See LICENSE.
using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Windows;
using System.Windows.Automation;
using System.Windows.Controls;
using System.Windows.Media;
using WpfPath = System.Windows.Shapes.Path;

namespace SyndicateClassic;

// Settings > Modded: every extra as a card in two columns. Each card has an on/off switch, the seasons it
// works in, whether it changes while you play, and its options behind an Options button. A search box narrows the list.
//
// The new client mods below are read by TiltedClient.dll from %LOCALAPPDATA%\ProjectTilted once a second,
// so they can be changed while Fortnite runs:
//   edit-release-on.txt     Edit on release on
//   reset-release-on.txt    Reset on release on (let go of the edit key without picking a tile -> piece resets to a full wall)
//   edit-key.txt            your edit key ("key:<virtual key>" or "mouse:middle|x1|x2"; default G, Fortnite's own default)
//   edit-release-any.txt    confirm even when no tile was picked (default: only with a tile)
//   edit-release-select-off.txt  letting go of select (left mouse / right trigger) does not confirm (default: it does)
//   pad-edit-button.txt     your controller's edit button (A, B, X, Y, LB, RB, LS, RS, UP, DOWN, LEFT, RIGHT; default B),
//                           read by TiltedPadAssist.dll
public partial class MainWindow
{
    private static bool ModFlag(string name) { try { return File.Exists(HudFlag(name)); } catch { return false; } }

    internal static bool EditOnReleaseEnabled { get => ModFlag("edit-release-on.txt"); set => SetHudFlag("edit-release-on.txt", value); }
    // Reset on release: shares edit-key.txt as its trigger. Let go of the edit key without picking a tile and Syndicate
    // fires Fortnite's Reset Building Edit (right mouse) then confirms, snapping the piece back to a full wall.
    internal static bool ResetOnReleaseEnabled { get => ModFlag("reset-release-on.txt"); set => SetHudFlag("reset-release-on.txt", value); }
    internal static bool EditReleaseNeedsTile { get => !ModFlag("edit-release-any.txt"); set => SetHudFlag("edit-release-any.txt", !value); }
    // classic-ar-pump.txt: Chapter 2 rolls the Chapter 1 Assault Rifle and Pump Shotgun instead of the Chapter 2 ones
    // (read by the match server on this PC when loot is rolled; the host's choice applies to everyone in the match).
    internal static bool ClassicArPumpEnabled { get => ModFlag("classic-ar-pump.txt"); set => SetHudFlag("classic-ar-pump.txt", value); }
    // no-bot-emotes.txt: present = bots never play an emote after eliminating you (and skip their warm-up dances).
    // Read by the match server on this PC (SpectraBots BotEmotesDisabled, cached), so the host's choice applies to everyone.
    internal static bool BotEmotesDisabled { get => ModFlag("no-bot-emotes.txt"); set => SetHudFlag("no-bot-emotes.txt", value); }
#if TILTED_DEVKIT
    // dev-aim-assist.txt: present = on, its text the strength 1 to 3 (read by TiltedClient.dll once a second).
    // dev-aim-assist-level.txt keeps the chosen strength while it is off.
    internal static int DevAimAssistLevel
    {
        get { try { var f = HudFlag("dev-aim-assist.txt"); if (!File.Exists(f)) return 0; return int.TryParse(File.ReadAllText(f).Trim(), out var v) ? Math.Clamp(v, 1, 3) : 2; } catch { return 0; } }
        set
        {
            try
            {
                var f = HudFlag("dev-aim-assist.txt");
                if (value <= 0) { if (File.Exists(f)) File.Delete(f); return; }
                Directory.CreateDirectory(System.IO.Path.GetDirectoryName(f)!); File.WriteAllText(f, Math.Clamp(value, 1, 3).ToString());
            }
            catch { }
        }
    }
    internal static int DevAimAssistLastLevel
    {
        get { try { var f = HudFlag("dev-aim-assist-level.txt"); return File.Exists(f) && int.TryParse(File.ReadAllText(f).Trim(), out var v) ? Math.Clamp(v, 1, 3) : 2; } catch { return 2; } }
        set { try { var f = HudFlag("dev-aim-assist-level.txt"); Directory.CreateDirectory(System.IO.Path.GetDirectoryName(f)!); File.WriteAllText(f, Math.Clamp(value, 1, 3).ToString()); } catch { } }
    }
#endif
    internal static bool EditSelectRelease { get => !ModFlag("edit-release-select-off.txt"); set => SetHudFlag("edit-release-select-off.txt", !value); }
    internal static readonly string[] PadEditButtons = { "B", "Y", "X", "A", "RB", "LB", "RS", "LS", "UP", "DOWN", "LEFT", "RIGHT" };
    internal static string PadEditButton
    {
        get { try { var f = HudFlag("pad-edit-button.txt"); if (File.Exists(f)) { var v = File.ReadAllText(f).Trim().ToUpperInvariant(); if (Array.IndexOf(PadEditButtons, v) >= 0) return v; } } catch { } return "B"; }
        set { try { var f = HudFlag("pad-edit-button.txt"); Directory.CreateDirectory(System.IO.Path.GetDirectoryName(f)!); File.WriteAllText(f, value); } catch { } }
    }
    private FrameworkElement PadEditButtonPicker()
    {
        var names = new Dictionary<string, string> { ["B"] = "B (Circle)", ["Y"] = "Y (Triangle)", ["X"] = "X (Square)", ["A"] = "A (Cross)", ["RB"] = "RB (R1)", ["LB"] = "LB (L1)", ["RS"] = "Right stick click (R3)", ["LS"] = "Left stick click (L3)", ["UP"] = "D-pad up", ["DOWN"] = "D-pad down", ["LEFT"] = "D-pad left", ["RIGHT"] = "D-pad right" };
        var combo = new ComboBox { MinWidth = 190, Height = 32, Margin = new Thickness(12, 0, 0, 0) };
        foreach (var b in PadEditButtons) combo.Items.Add(new ComboBoxItem { Content = names[b], Tag = b });
        combo.SelectedIndex = Math.Max(0, Array.IndexOf(PadEditButtons, PadEditButton));
        combo.SelectionChanged += (_, __) => { if (combo.SelectedItem is ComboBoxItem { Tag: string b }) PadEditButton = b; };
        var label = NightText("Your controller edit button", 12.5, Night.Sub).Also(t => t.VerticalAlignment = VerticalAlignment.Center);
        var row = new DockPanel { Margin = new Thickness(0, -6, 0, 18) };
        DockPanel.SetDock(combo, Dock.Right);
        row.Children.Add(combo); row.Children.Add(label);
        return row;
    }
    internal static string EditKey
    {
        get
        {
            try { var f = HudFlag("edit-key.txt"); if (File.Exists(f)) { var v = File.ReadAllText(f).Trim(); if (ValidSprintKey(v)) return v; } }
            catch { }
            return "key:71"; // G, Fortnite's default edit key (was F, which is not an edit key: nothing happened)
        }
        set
        {
            if (!ValidSprintKey(value)) return;
            try { var f = HudFlag("edit-key.txt"); Directory.CreateDirectory(System.IO.Path.GetDirectoryName(f)!); File.WriteAllText(f, value); } catch { }
        }
    }

    private sealed class ModCard
    {
        public string Category = "", Title = "", Detail = "", Seasons = "", When = "", Glyph = "", GlyphColor = Night.Sky;
        public string? CustomIcon;            // vector icon for cards without a Segoe glyph
        public bool IsNew;
        public Func<bool>? Get;               // null: a card without its own switch (set up inside Options)
        public Action<bool>? Set;
        public Action<StackPanel>? Options;   // builds the options the first time they are opened
        public string OptionsLabel = "Options";
        public Border? Card;
    }

    private void BuildModdedPage(StackPanel page)
    {
        // The page's own card frame is replaced by the feature cards.
        if (page.Parent is Border frame) { frame.Background = Brushes.Transparent; frame.BorderThickness = new Thickness(0); frame.Padding = new Thickness(0); }

        var cards = new List<ModCard>
        {
            new() { Category = "Controller", Title = "PS4 / PS5 controllers", Glyph = "", GlyphColor = Night.Sky, Seasons = "All seasons", When = "Works mid-match",
                Detail = "DualShock 4 and DualSense, over USB or Bluetooth, play as an Xbox controller in every season. Xbox controllers need nothing.",
                OptionsLabel = "Status and setup", Options = panel => AddControllerSettings(panel) },
            new() { Category = "Controller", Title = "Controller button layout", Glyph = "\uE7FC", GlyphColor = Night.Sky, Seasons = "Pre-season to Season 5", When = "Works mid-match", IsNew = true,
                Detail = "Custom controller binds for the seasons that never had them (Pre-season to Season 5): move any button or trigger to another one, turn buttons off, or swap the sticks. Xbox and PS controllers. Season 6 and later use Fortnite's own controller binds, so this stays off there.",
                OptionsLabel = "Change buttons", Options = panel => AddControllerBinds(panel) },
            new() { Category = "Controller", Title = "Use the launcher with a controller", Glyph = "", GlyphColor = Night.Sky, Seasons = "Launcher", When = "Instant",
                Detail = "LB / RB switch tabs and pages, A presses, B goes back, Y plays the season on Home, X opens Settings. Hints appear when you pick up a pad.",
                Get = () => LauncherPadEnabled, Set = on => { LauncherPadEnabled = on; UpdatePadHints(force: true); } },
            new() { Category = "Movement", Title = "Sprint by default", Glyph = "", GlyphColor = Night.Good, Seasons = "All seasons", When = "Works mid-match",
                Detail = "Moving forward sprints for you: W, W+A or W+D on keyboard, the left stick pushed forward on a controller. Aiming still slows you down.",
                Get = () => SprintByDefaultEnabled, Set = on => SprintByDefaultEnabled = on,
                Options = panel => { panel.Children.Add(Tidy(SprintKeyPicker())); panel.Children.Add(OptionNote("Season 5 and later: this stays off. Use Fortnite's own Sprint by default setting there.")); } },
            new() { Category = "Movement", Title = "Auto run", Glyph = "", GlyphColor = Night.Good, Seasons = "All seasons", When = "Works mid-match",
                Detail = "Press your auto run key and keep running forward on your own. Press it again, W or S to stop. With sprint by default on, you sprint too.",
                Get = () => AutoRunEnabled, Set = on => AutoRunEnabled = on,
                Options = panel => { panel.Children.Add(Tidy(KeyPicker("Your auto run key", "auto run key", () => AutoRunKey, v => AutoRunKey = v))); panel.Children.Add(OptionNote("Season 3 and later also have Fortnite's own auto run. If both use the same key, Syndicate's takes it; unbind the game's or pick a different key here.")); } },
            new() { Category = "Building", Title = "Turbo build", Glyph = "", GlyphColor = Night.Gold, Seasons = "All seasons", When = "Works mid-match",
                Detail = "Hold the left mouse button (or the right trigger) in build mode to keep placing walls, floors, stairs and roofs. Guns and the edit tool are never affected.",
                Get = () => TurboBuildEnabled, Set = on => TurboBuildEnabled = on },
            new() { Category = "Building", Title = "Edit on release", Glyph = "", GlyphColor = Night.Gold, Seasons = "All seasons", When = "Works mid-match", IsNew = true,
                Detail = "Pick your tiles with select (left mouse or right trigger) and let go: the edit is confirmed for you. Letting go of the edit key or button confirms too. Keyboard, mouse and controller.",
                Get = () => EditOnReleaseEnabled, Set = on => EditOnReleaseEnabled = on,
                Options = panel =>
                {
                    panel.Children.Add(OptionNote("Set this to the Edit key from Fortnite's Settings > Input (G unless you changed it). If they differ, letting go does nothing."));
                    panel.Children.Add(Tidy(KeyPicker("Your edit key", "edit key", () => EditKey, v => EditKey = v)));
                    panel.Children.Add(Tidy(PadEditButtonPicker()));
                    panel.Children.Add(SettingsToggle("Confirm when you let go of select", "In edit mode, hold the left mouse button (or right trigger) over the tiles and let go: Syndicate presses your edit key (or button) for you. Turn this off to only confirm by letting go of the edit key.", EditSelectRelease, v => EditSelectRelease = v));
                    panel.Children.Add(SettingsToggle("Only confirm when you picked a tile", "Let go without picking a tile and you stay in edit mode. Turn this off to confirm (and leave edit mode) every time you let go.", EditReleaseNeedsTile, v => EditReleaseNeedsTile = v));
                    panel.Children.Add(OptionNote("If your season has Fortnite's own \"Confirm edit on release\" setting, keep that one off while this is on."));
                } },
            new() { Category = "Building", Title = "Reset on release", Glyph = "", GlyphColor = Night.Gold, Seasons = "All seasons", When = "Works mid-match", IsNew = true,
                Detail = "An edit peek that always resets. Open your build with the edit key, look through it, and let go WITHOUT picking any tiles: Syndicate snaps the piece straight back to a full wall for you. Pick tiles and let go as usual to keep the edit.",
                Get = () => ResetOnReleaseEnabled, Set = on => ResetOnReleaseEnabled = on,
                Options = panel =>
                {
                    panel.Children.Add(OptionNote("Uses the same edit key as Edit on release — set it to Fortnite's Edit key (G unless you changed it). The piece is reset with Fortnite's Reset Building Edit, the right mouse button, so leave that one on its default."));
                    panel.Children.Add(Tidy(KeyPicker("Your edit key", "edit key", () => EditKey, v => EditKey = v)));
                    panel.Children.Add(OptionNote("You can run this with Edit on release at the same time: pick tiles and let go to confirm your edit, or let go without a tile to reset to a full wall."));
                } },
            new() { Category = "HUDs", Title = "Network HUD", Glyph = "", GlyphColor = "#B9A6FF", Seasons = "Pre-season", When = "Works mid-match",
                Detail = "Your ping to the host with packet loss and jitter, the host's tick rate and worst frame. Top left of the game.",
                Get = () => NetHudEnabled, Set = on => NetHudEnabled = on },
            new() { Category = "HUDs", Title = "Lobby loading tag", Glyph = "\uE7F4", GlyphColor = "#B9A6FF", Seasons = "All seasons", When = "Works mid-match", IsNew = true,
                Detail = "An animated tag above the Locker tab in the Fortnite lobby: Loading, Loading island, Match ready, then it slides away once you are in. If a match cannot start it turns red with the reason (also in the Diagnostics log), and shows Lost connection if the local services stop.",
                Get = () => LobbyTagEnabled, Set = on => LobbyTagEnabled = on },
        };
        // Chapter 2 Season 1 loot switch.
        cards.Add(new() { Category = "Weapons", Title = "Chapter 1 AR and pump", Glyph = "\uE7FC", GlyphColor = "#FF8A3D", Seasons = "Chapter 2 Season 1", When = "Next match", IsNew = true,
            Detail = "Chapter 2 drops the classic Chapter 1 Assault Rifle (SCAR at purple and gold) and Pump Shotgun instead of the Chapter 2 ones, at the same rarity. Bots carry them too. Off: the Chapter 2 AR and pump.",
            Get = () => ClassicArPumpEnabled, Set = on => ClassicArPumpEnabled = on });
        // Stop the AI emoting on you (bug: bots dancing over your body after a kill).
        cards.Add(new() { Category = "Bots", Title = "Stop bots emoting on you", Glyph = "", CustomIcon = "bots", GlyphColor = "#FF8A3D", Seasons = "All seasons", When = "Next match", IsNew = true,
            Detail = "Bots no longer play an emote after they eliminate you, and skip their warm-up dances. Your own emotes and real players are never affected.",
            Get = () => BotEmotesDisabled, Set = on => BotEmotesDisabled = on });
#if TILTED_DEVKIT
        // Dev Kit builds only: never in a public release (the whole Dev Console is compiled out there).
        cards.Add(new() { Category = "Developer", Title = "Dev Console", Glyph = "\uE756", GlyphColor = "#8B6CFF", Seasons = "Dev Kit only", When = "Your own matches", IsNew = true,
            Detail = "Press Insert in game to open the Dev Console: every player and bot, teleport to or kill bots, Thanos snap, instant win, cheats (aim assist, fly, no recoil, no bullet drop, infinite ammo) and live logs. Bots only.",
            // Only the on/off switch here: bot markers and every other option live in the Dev Console itself (Insert).
            Get = () => DevConsoleEnabled, Set = on => DevConsoleEnabled = on });
#endif
        // Cosmetics moved here from its own Settings tab: "Unlock all" for the selected player, as a card.
        cards.Add(new() { Category = "Cosmetics", Title = "Unlock all cosmetics", Glyph = "", GlyphColor = "#B1E9C9", Seasons = "All seasons", When = "Next lobby load",
            Detail = "Unlock every skin, back bling, pickaxe, glider, emote and wrap in the game for the selected player. Turning it off removes only what it unlocked; everything you already owned stays. Its options also have the Save the World skins switch.",
            OptionsLabel = "Unlock all", Options = panel => AddCosmeticsSettings(panel) });
        cards.Add(new() { Category = "Cosmetics", Title = "Remove default outfits from locker", Glyph = "", CustomIcon = "outfits", GlyphColor = "#B1E9C9", Seasons = "All seasons", When = "Next lobby load", IsNew = true,
            Detail = "Hide the free default outfits, pickaxe and glider from the locker for the selected player. An equipped default is swapped to one of your own cosmetics first. Turning it off puts the defaults back.",
            OptionsLabel = "Remove defaults", Options = panel => AddHideDefaultsSettings(panel) });
        cards.Add(new() { Category = "Cosmetics", Title = "Remove unreleased win umbrellas", Glyph = "", CustomIcon = "umbrellas", GlyphColor = "#B1E9C9", Seasons = "All seasons", When = "Next lobby load", IsNew = true,
            Detail = "Hide the unreleased Bronze (25 wins) and Gold (50 wins) milestone umbrellas from the locker. If one is equipped it swaps to another glider first. Turning it off brings back the ones you've earned.",
            OptionsLabel = "Remove umbrellas", Options = panel => AddHideWinUmbrellasSettings(panel) });

        // Search box
        var search = new TextBox { Height = 42, MinWidth = 240 };
        NightField(search); search.Padding = new Thickness(34, 2, 10, 2);
        AutomationProperties.SetName(search, "Search mods");
        var searchHint = NightText("Search: sprint, build, HUD…", 13.5, Night.Muted).Also(t => { t.IsHitTestVisible = false; t.VerticalAlignment = VerticalAlignment.Center; t.Margin = new Thickness(36, 0, 0, 0); });
        var searchIcon = GlyphIcon("", 14, Night.Muted).Also(g => { g.HorizontalAlignment = HorizontalAlignment.Left; g.Margin = new Thickness(13, 0, 0, 0); g.IsHitTestVisible = false; });
        var searchBox = new Grid { Width = 320, Margin = new Thickness(0, 0, 0, 10), HorizontalAlignment = HorizontalAlignment.Left };
        searchBox.Children.Add(search); searchBox.Children.Add(searchHint); searchBox.Children.Add(searchIcon);
        var toolbar = new WrapPanel { Margin = new Thickness(0, 0, 0, 8) };
        toolbar.Children.Add(searchBox);
        page.Children.Add(toolbar);

        // Two columns of cards and an empty state
        var columns = new Grid();
        columns.ColumnDefinitions.Add(new ColumnDefinition()); columns.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(12) }); columns.ColumnDefinitions.Add(new ColumnDefinition());
        var left = new StackPanel(); var right = new StackPanel(); Grid.SetColumn(right, 2);
        columns.Children.Add(left); columns.Children.Add(right);
        var empty = new Border { CornerRadius = TiltCorners(14), BorderBrush = Hex(Night.Line), BorderThickness = new Thickness(1), Padding = new Thickness(24), Visibility = Visibility.Collapsed,
            Child = NightText("Nothing matches that. Try another word.", 13.5, Night.Sub).Also(t => t.HorizontalAlignment = HorizontalAlignment.Center) };
        page.Children.Add(empty); page.Children.Add(columns);
        page.Children.Add(NightText("Field of view and stretched resolution are under Display.", 12, Night.Muted).Also(t => t.Margin = new Thickness(0, 6, 0, 0)));

        string query = "";
        bool Matches(ModCard c) => query.Length == 0 || $"{c.Title} {c.Detail} {c.Category} {c.Seasons}".Contains(query, StringComparison.OrdinalIgnoreCase);
        void Relayout()
        {
            left.Children.Clear(); right.Children.Clear();
            int shown = 0;
            foreach (var c in cards.Where(Matches))
                (shown++ % 2 == 0 ? left : right).Children.Add(c.Card ??= BuildModCard(c));
            empty.Visibility = shown == 0 ? Visibility.Visible : Visibility.Collapsed;
        }
        search.TextChanged += (_, __) => { query = search.Text.Trim(); searchHint.Visibility = search.Text.Length == 0 ? Visibility.Visible : Visibility.Collapsed; Relayout(); };
        Relayout();
    }

    private Border BuildModCard(ModCard c)
    {
        var body = new StackPanel();
        var card = new Border { CornerRadius = new CornerRadius(14, 3, 14, 3), Background = GlassFill(), BorderBrush = Hex(Night.Line), BorderThickness = new Thickness(1), Padding = new Thickness(12, 12, 10, 12), Margin = new Thickness(0, 0, 0, 8), Child = body };

        var head = new Grid();
        head.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(34) });
        head.ColumnDefinitions.Add(new ColumnDefinition());
        head.ColumnDefinitions.Add(new ColumnDefinition { Width = GridLength.Auto });
        head.ColumnDefinitions.Add(new ColumnDefinition { Width = GridLength.Auto });
        head.Children.Add(new Border { Width = 34, Height = 34, CornerRadius = new CornerRadius(9, 3, 9, 3), Background = Hex("#14FFFFFF"), VerticalAlignment = VerticalAlignment.Top, Child = c.CustomIcon is { } customIcon ? ModIcon(customIcon, c.GlyphColor) : GlyphIcon(c.Glyph, 16, c.GlyphColor) });
        var words = new StackPanel { Margin = new Thickness(10, 0, 6, 0) };
        var titleRow = new WrapPanel();
        titleRow.Children.Add(NightText(c.Title, 14, Night.Text, bold: true).Also(t => { t.VerticalAlignment = VerticalAlignment.Center; t.Margin = new Thickness(0, 0, 8, 0); }));
        if (c.IsNew) titleRow.Children.Add(Badge("NEW", Night.OnGold, Night.Gold, Night.Gold).Also(b => b.VerticalAlignment = VerticalAlignment.Center));
        words.Children.Add(titleRow);
        // Descriptions stay folded away so the list stays compact; the arrow beside the title opens one.
        var detail = NightText(c.Detail, 12.5, Night.Sub).Also(t => { t.TextWrapping = TextWrapping.Wrap; t.Margin = new Thickness(0, 4, 0, 0); t.LineHeight = 18; });
        detail.Visibility = Visibility.Collapsed;
        words.Children.Add(detail);
        Grid.SetColumn(words, 1); head.Children.Add(words);
        if (!string.IsNullOrWhiteSpace(c.Detail))
        {
            var arrow = new Button { Width = 30, Height = 30, Content = GlyphIcon("\uE70D", 10, Night.Sub), ToolTip = "Show description", VerticalAlignment = VerticalAlignment.Top, Margin = new Thickness(8, 4, 0, 0), Padding = new Thickness(0) };
            RoundButton(arrow, Night.Frost, Night.Sub, 15);
            arrow.BorderBrush = Hex(Night.FrostEdge); arrow.BorderThickness = new Thickness(1);
            arrow.MouseEnter += (_, __) => { arrow.Background = Hex(Night.FrostHover); ((TextBlock)arrow.Content).Foreground = Hex(Night.Text); };
            arrow.MouseLeave += (_, __) => { arrow.Background = Hex(Night.Frost); ((TextBlock)arrow.Content).Foreground = Hex(Night.Sub); };
            arrow.Click += (_, __) =>
            {
                bool show = detail.Visibility != Visibility.Visible;
                detail.Visibility = show ? Visibility.Visible : Visibility.Collapsed;
                ((TextBlock)arrow.Content).Text = show ? "\uE70E" : "\uE70D";
                arrow.ToolTip = show ? "Hide description" : "Show description";
                AutomationProperties.SetName(arrow, (show ? "Hide" : "Show") + " description for " + c.Title);
            };
            AutomationProperties.SetName(arrow, "Show description for " + c.Title);
            Grid.SetColumn(arrow, 2); head.Children.Add(arrow);
        }
        void Mark(bool on) { card.BorderBrush = on ? GlassEdgeFill() : Hex(Night.Line); }
        if (c.Get is { } isOn && c.Set is { } turn)
        {
            var toggle = new CheckBox { IsChecked = isOn(), Template = SwitchTemplate.Value, Cursor = System.Windows.Input.Cursors.Hand, VerticalAlignment = VerticalAlignment.Top, Margin = new Thickness(0, 8, 0, 0), ToolTip = "Turn " + c.Title + " on or off" };
            AutomationProperties.SetName(toggle, c.Title);
            toggle.Checked += (_, __) => { turn(true); Mark(true); };
            toggle.Unchecked += (_, __) => { turn(false); Mark(false); };
            Grid.SetColumn(toggle, 3); head.Children.Add(toggle);
            Mark(isOn());
        }
        body.Children.Add(head);

        // Badges, then the options button on its own line (beside the badges it ran off the card's right edge).
        var meta = new StackPanel { Margin = new Thickness(44, 8, 0, 0) };
        var badges = new WrapPanel();
        badges.Children.Add(Badge(c.Seasons, "#CFE9FF", "#10233A", "#1D3C5E").Also(b => b.Margin = new Thickness(0, 0, 6, 4)));
        badges.Children.Add(Badge(c.When, Night.Sub, "#12162B", Night.Line).Also(b => b.Margin = new Thickness(0, 0, 6, 4)));
        meta.Children.Add(badges);
        body.Children.Add(meta);

        if (c.Options is { } buildOptions)
        {
            var options = new StackPanel();
            var drawer = new Border { Margin = new Thickness(44, 8, 0, 0), Padding = new Thickness(12, 12, 12, 2), CornerRadius = TiltCorners(10), Background = Hex("#090C1A"), BorderBrush = Hex("#1E2444"), BorderThickness = new Thickness(1), Child = options, Visibility = Visibility.Collapsed };
            var open = new Button { Content = c.OptionsLabel, Height = 28, Padding = new Thickness(10, 0, 10, 0) };
            NightButton(open, "base"); open.FontSize = 12;
            bool built = false;
            open.Click += (_, __) =>
            {
                if (!built) { built = true; buildOptions(options); }
                bool show = drawer.Visibility != Visibility.Visible;
                drawer.Visibility = show ? Visibility.Visible : Visibility.Collapsed;
                open.Content = show ? "Hide" : c.OptionsLabel;
            };
            open.HorizontalAlignment = HorizontalAlignment.Left; open.Margin = new Thickness(0, 4, 0, 0); meta.Children.Add(open);
            body.Children.Add(drawer);
        }
        return card;
    }

    private static FrameworkElement ModIcon(string kind, string color)
    {
        string data = kind switch
        {
            "bots" => "M6,7 H18 V17 H6 Z M12,7 V4 M9,10 V11.5 M15,10 V11.5 M9,14 H15 M4,4 L20,20",
            "outfits" => "M8,4 L5,6 L2,10 L5,13 L7,11 V20 H17 V11 L19,13 L22,10 L19,6 L16,4 L13,7 H11 Z M16,17 H21",
            "umbrellas" => "M3,12 A9,9 0 0 1 21,12 Z M12,3 V12 M6.5,6.5 L9,12 M17.5,6.5 L15,12 M12,12 V18 A2,2 0 0 1 8,18 M16,20 H21",
            _ => "M4,4 H20 V20 H4 Z"
        };
        var path = new WpfPath
        {
            Data = Geometry.Parse(data), Stroke = Hex(color), StrokeThickness = 1.5, Fill = Brushes.Transparent,
            StrokeLineJoin = PenLineJoin.Round, StrokeStartLineCap = PenLineCap.Round, StrokeEndLineCap = PenLineCap.Round
        };
        return new Viewbox { Width = 19, Height = 19, Child = path, Stretch = Stretch.Uniform };
    }

    private static Border Badge(string text, string foreground, string background, string border) => new()
    {
        CornerRadius = new CornerRadius(6), Background = Hex(background), BorderBrush = Hex(border), BorderThickness = new Thickness(1), Padding = new Thickness(8, 2, 8, 3),
        Child = NightText(text, 11.5, foreground, bold: true).Also(t => t.TextWrapping = TextWrapping.NoWrap)
    };

    private static TextBlock OptionNote(string text) => NightText(text, 12, Night.Muted).Also(t => { t.TextWrapping = TextWrapping.Wrap; t.Margin = new Thickness(0, 0, 0, 12); t.LineHeight = 17; });

    // Key pickers and toggles carry page margins; inside a card's options they sit flush.
    private static FrameworkElement Tidy(FrameworkElement element) { element.Margin = new Thickness(0, 0, 0, 12); return element; }
}