// Syndicate © 2026 Syndicate. All rights reserved. Proprietary — no copying, distribution, or derivative works. See LICENSE.
using System;
using System.Collections.Generic;
using System.Linq;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Controls.Primitives;
using System.Windows.Input;
using System.Windows.Media;
using System.Windows.Media.Animation;
using System.Windows.Media.Effects;

namespace SyndicateClassic;

// Season browser, the Home tab (rail tab 0): one chapter dropdown, a details area for the selected
// season with the real PLAY / folder / settings controls (moved here from the old home hero), and a
// horizontal timeline of season cards with arrows and a draggable slider.
// The timeline moves like the "dynamic scrolling frame" reference: cards sit on a gentle arc, the one
// in the middle is full size and the others shrink, sink and fade with distance, and all of it follows
// one animated position, so arrows, slider, wheel and clicks move it the same way.
// Earliest season on the left, latest on the right: going to a later season moves the selection
// (and the slider) from left to right; going back moves it right to left.
public partial class MainWindow
{
    // One season in the browser. SyndicateSeason is the launcher's own season number when Syndicate can
    // run it (Chapter 1); Chapter 2 seasons are shown for browsing only.
    private sealed record BrowserSeason(int Chapter, int Number, string Label, string? Name, string Dates, string Description, string[] Features, int? SyndicateSeason);

    // Release dates are the seasons' real start and end dates. Chapter 1 seasons had no official names,
    // so Name is empty there (the launcher's own theme line is shown as the theme instead).
    private static readonly BrowserSeason[] BrowserChapter1 =
    {
        new(1, 0, "PRE-SEASON", null, "Sep 26 – Oct 25, 2017", "Battle Royale's first weeks on the original island: Anarchy Acres, Moisty Mire, Tomato Town and Dusty Depot. No Battle Pass yet.", new[] { "Original map", "No Battle Pass" }, 0),
        new(1, 1, "SEASON 1", null, "Oct 25 – Dec 13, 2017", "The first season with tiers and challenges. Renegade Raider and Aerial Assault Trooper were in the season shop.", new[] { "First tiers", "Original map" }, 1),
        new(1, 2, "SEASON 2", null, "Dec 14, 2017 – Feb 21, 2018", "The medieval season and the very first Battle Pass: 70 tiers, the Black Knight and the Blue Squire.", new[] { "First Battle Pass", "Knights & castles" }, 2),
        new(1, 3, "SEASON 3", null, "Feb 22 – Apr 30, 2018", "A comet lights up the sky, jetpacks take off and the first 100-tier Battle Pass ends with The Reaper.", new[] { "100-tier pass", "Comet" }, 3),
        new(1, 4, "SEASON 4", null, "May 1 – Jul 12, 2018", "The meteor hits: Dusty Divot, Hop Rocks, superheroes, Carbide and Omega.", new[] { "Meteor", "Hop Rocks" }, 4),
        new(1, 5, "SEASON 5", null, "Jul 12 – Sep 27, 2018", "Rifts tear open the island: Paradise Palms, Lazy Links, All Terrain Karts, Drift and Ragnarok.", new[] { "Rifts", "ATKs" }, 5),
        new(1, 6, "SEASON 6", null, "Sep 27 – Dec 6, 2018", "The floating island, Leaky Lake, corruption, pets and Shadow Stones.", new[] { "Pets", "Floating island" }, 6),
        new(1, 7, "SEASON 7", null, "Dec 6, 2018 – Feb 28, 2019", "Snow covers the island: Frosty Flights, Polar Peak, planes, ziplines and the Ice King.", new[] { "Planes", "Snow" }, 7),
        new(1, 8, "SEASON 8", null, "Feb 28 – May 9, 2019", "Pirates and volcanoes: Lazy Lagoon, Sunny Steps, cannons and buried treasure.", new[] { "Pirates", "Volcano" }, 8),
        new(1, 9, "SEASON 9", null, "May 9 – Aug 1, 2019", "Neo Tilted, Mega Mall, Slipstreams, the Pressure Plant and the Combat Shotgun.", new[] { "Neo Tilted", "Slipstreams" }, 9),
        new(1, 10, "SEASON X", null, "Aug 1 – Oct 13, 2019", "Rifts bring back old places, B.R.U.T.E mechs, Rift Zones and the return of Dusty Depot. Ended with \"The End\" event.", new[] { "B.R.U.T.E mechs", "Rift Zones" }, 10),
#if TILTED_DEVKIT
        // Dev-only slot after Season X for custom-season work. It runs on Season X's build and files (v10.31):
        // SyndicateSeason 10 means PLAY launches the Season X 10.31 client/folder. Link or own the Season X folder
        // to play it. (A fully separate XI identity that still runs 10.31 would need its own season number.)
        new(1, 11, "SEASON XI", "PARADOX", "Dev slot · runs on Season X", "The season that never was - Chapter 1 continues after \"The End.\" A dev slot for custom-season work: it launches Season X's build and files (v10.31), so link or own the Season X game folder to play it.", new[] { "Continues Chapter 1", "Dev only", "Season X files" }, 10),
#endif
    };

    private static readonly BrowserSeason[] BrowserChapter2 =
    {
        new(2, 1, "SEASON 1", null, "Oct 15, 2019 – Feb 20, 2020", "A brand-new island after \"The End\": swimming, fishing, boats, upgrade benches and hiding spots.", new[] { "New island", "Swimming & fishing" }, GameBuild.Chapter2 ? 11 : null),
        new(2, 2, "SEASON 2", "TOP SECRET", "Feb 20 – Jun 17, 2020", "Spies of Ghost and Shadow, Midas, henchmen and boss fights at The Agency.", new[] { "Bosses & henchmen", "The Agency" }, GameBuild.Chapter2 ? 12 : null),
        new(2, 3, "SEASON 3", "SPLASH DOWN", "Jun 17 – Aug 27, 2020", "The island floods: Aquaman, loot sharks, Coral Castle and water slowly receding.", new[] { "Flooded island", "Sharks" }, null),
        new(2, 4, "SEASON 4", "NEXUS WAR", "Aug 27 – Dec 2, 2020", "Marvel heroes defend the island from Galactus, with Mythic hero abilities.", new[] { "Marvel", "Galactus" }, null),
        new(2, 5, "SEASON 5", "ZERO POINT", "Dec 2, 2020 – Mar 16, 2021", "Hunters from other worlds, like the Mandalorian and Kratos, gold bars and the Zero Point.", new[] { "Hunters", "Gold bars" }, null),
        new(2, 6, "SEASON 6", "PRIMAL", "Mar 16 – Jun 8, 2021", "A wild island: crafting, primal weapons, wildlife to hunt and tame, and Lara Croft.", new[] { "Crafting", "Wildlife" }, null),
        new(2, 7, "SEASON 7", "INVASION", "Jun 8 – Sep 13, 2021", "Aliens invade: UFOs, abductors, alien weapons and the IO, with Superman.", new[] { "UFOs", "Aliens" }, null),
        new(2, 8, "SEASON 8", "CUBED", "Sep 13 – Dec 4, 2021", "Cubes spread over the island, the Sideways appears and the Cube Queen arrives.", new[] { "The Sideways", "Cube Queen" }, null),
    };

    // Chapter 3 is Dev Kit only and experimental. Season 1 (19.00) runs on Unreal Engine 5; the Erbium match
    // server is UE4, so it is boot-test only and does not host a working match yet (SyndicateSeason only when Chapter3).
    private static readonly BrowserSeason[] BrowserChapter3 =
    {
        new(3, 1, "SEASON 1", "FLIPPED", "Dec 5, 2021 – Mar 19, 2022", "Chapter 3 begins on a flipped island: sliding, tents, mantling and a brand-new map. 19.00 runs on Unreal Engine 5 — experimental, boot-test only in Syndicate.", new[] { "New island", "Unreal Engine 5" }, GameBuild.Chapter3 ? 19 : null),
    };

    private BrowserSeason[] BrowserSeasons(int chapter) => chapter == 3 ? BrowserChapter3 : chapter == 2 ? BrowserChapter2 : BrowserChapter1;

    // Chapter 2 season accent, taken from each season's key art. Used for the CHAPTER 2
    // eyebrow and its leaning bar on the season browser.
    private static string Chapter2Accent(int number) => number switch
    {
        3 => "#22D3EE", // Splashdown - bright ocean cyan
        4 => "#C084FC", // Nexus War - Galactus magenta
        5 => "#F59E0B", // Zero Point - Mando amber
        6 => "#4ADE80", // Primal - forest green
        7 => "#A855F7", // Invasion - purple rift
        8 => "#A78BFA", // Cubed - violet Sideways
        _ => Night.Gold,
    };

    // Chapter 1 accents picked from the card/key art itself (same idea as Chapter 2 above).
    private static string Chapter1Accent(int number) => number switch
    {
        0 => "#B388FF", // Pre-season - the purple storm over the first island
        3 => "#FF8A4C", // Season 3 - the orange astronaut suits and comet lights
        _ => Night.Gold,
    };

    private Grid? _seasonBrowserView;
    private Canvas? _timeline;
    private TimelinePosition? _timelinePosition;
    private readonly List<Border> _timelineCards = new();
    private int _browserChapter = 1, _browserIndex = 4;
    private Border? _browserDetails, _browserDetailArt, _browserDetailCard, _sliderThumb, _sliderTrack;
    private StackPanel? _browserDetailText, _browserPlayArea;
    private TextBlock? _browserSeasonHint;
    private Button? _chapterDropdown;
    private TextBlock? _chapterDropdownText;
    private Popup? _chapterPopup;
    private bool _sliderDragging;
    private const double TimelineCardWidth = 168, TimelineCardHeight = 212, TimelineSpacing = 150;

    // Animated position of the timeline (in cards: 0 = first season in the middle). Every visual
    // follows this one value, so arrows, slider, wheel and clicks animate identically.
    private sealed class TimelinePosition : FrameworkElement
    {
        public static readonly DependencyProperty ValueProperty = DependencyProperty.Register(nameof(Value), typeof(double), typeof(TimelinePosition),
            new PropertyMetadata(0.0, (d, e) => ((TimelinePosition)d).Changed?.Invoke((double)e.NewValue)));
        public double Value { get => (double)GetValue(ValueProperty); set => SetValue(ValueProperty, value); }
        public Action<double>? Changed;
    }

    private Grid BuildSeasonBrowser()
    {
        var view = new Grid { Margin = new Thickness(RailWidth + 22, 60, 40, 18), ClipToBounds = true };
        _seasonBrowserView = view;
        view.RowDefinitions.Add(new RowDefinition { Height = GridLength.Auto });   // header
        view.RowDefinitions.Add(new RowDefinition());                              // details
        view.RowDefinitions.Add(new RowDefinition { Height = GridLength.Auto });   // timeline
        view.RowDefinitions.Add(new RowDefinition { Height = GridLength.Auto });   // slider

        // ---- Header: title on the left, the chapter dropdown on the right.
        var header = new Grid { Margin = new Thickness(0, 0, 0, 14) };
        header.ColumnDefinitions.Add(new ColumnDefinition());
        header.ColumnDefinitions.Add(new ColumnDefinition { Width = GridLength.Auto });
        var titles = new StackPanel();
        // Back to the front page (SyndicateHome.cs).
        var backRow = new StackPanel { Orientation = Orientation.Horizontal };
        backRow.Children.Add(GlyphIcon("\uE72B", 11, Night.Text).Also(g => g.Margin = new Thickness(0, 0, 8, 0)));
        backRow.Children.Add(NightText("BACK TO HOME", 11, Night.Text, bold: true).Also(t => { t.TextWrapping = TextWrapping.NoWrap; t.VerticalAlignment = VerticalAlignment.Center; }));
        var back = new Button { Content = backRow, Height = 32, Padding = new Thickness(14, 0, 16, 0), HorizontalAlignment = HorizontalAlignment.Left, Margin = new Thickness(0, 0, 0, 12), ToolTip = "Back to the home page (Esc)" };
        RoundButton(back, "#8C05060D", Night.Text, 16, "#40FFFFFF"); back.BorderThickness = new Thickness(1);
        back.Click += (_, __) => HighlightRail(0);
        titles.Children.Add(back);
        titles.Children.Add(NightEyebrow("Season browser"));
        titles.Children.Add(NightTitle("EVERY SEASON", 40));
        header.Children.Add(titles);
        var controls = new StackPanel { Orientation = Orientation.Horizontal, VerticalAlignment = VerticalAlignment.Bottom };
        Grid.SetColumn(controls, 1); header.Children.Add(controls);

        // The one chapter picker: "CHAPTER 1 ▾", opening a list with art, season range and availability.
        var dropLabel = new StackPanel { Orientation = Orientation.Horizontal };
        _chapterDropdownText = Display("CHAPTER 1", 20, Night.Text).Also(t => t.VerticalAlignment = VerticalAlignment.Center);
        dropLabel.Children.Add(_chapterDropdownText);
        dropLabel.Children.Add(GlyphIcon("", 11, Night.Gold).Also(g => { g.Margin = new Thickness(12, 2, 0, 0); g.VerticalAlignment = VerticalAlignment.Center; }));
        _chapterDropdown = new Button { Content = dropLabel, Height = 46, Padding = new Thickness(20, 0, 18, 0), ToolTip = "Choose a chapter" };
        RoundButton(_chapterDropdown, "#8C141831", Night.Text, 23, "#40FFFFFF"); _chapterDropdown.BorderThickness = new Thickness(1);
        var options = new StackPanel { Margin = new Thickness(6) };
        options.Children.Add(ChapterOption(1, "Seasons 1 – X  ·  Sep 2017 – Oct 2019", "Playable in Syndicate"));
        options.Children.Add(ChapterOption(2, "Seasons 1 – 8  ·  Oct 2019 – Dec 2021", GameBuild.Chapter2 ? "Season 1 is playable" : "Coming soon to Syndicate"));
#if TILTED_DEVKIT
        options.Children.Add(ChapterOption(3, "Season 1  ·  Dec 2021 – Mar 2022", "Experimental (Dev Kit, boot-test)"));
#endif
        _chapterPopup = new Popup
        {
            PlacementTarget = _chapterDropdown, Placement = PlacementMode.Bottom, VerticalOffset = 6, HorizontalOffset = -200, StaysOpen = false, AllowsTransparency = true, PopupAnimation = PopupAnimation.Fade,
            Child = new Border { Child = options, Width = 340, Background = Hex(Night.Surface2), BorderBrush = Hex(Night.Line), BorderThickness = new Thickness(1), CornerRadius = TiltCorners(14), Effect = new DropShadowEffect { BlurRadius = 24, ShadowDepth = 6, Opacity = .5, Color = Colors.Black } }
        };
        _chapterDropdown.Click += (_, __) => _chapterPopup.IsOpen = !_chapterPopup.IsOpen;
        var dropHost = new Grid(); dropHost.Children.Add(_chapterDropdown); dropHost.Children.Add(_chapterPopup);
        controls.Children.Add(dropHost);
        view.Children.Add(header);

        // ---- Details: a faint wash of the season art behind; text and PLAY on the left, and on the
        // right the art in a card cropped the same way the old home hero card was, so it fits.
        _browserDetailArt = new Border { CornerRadius = new CornerRadius(18), Opacity = .22 };
        var detailsGrid = new Grid();
        detailsGrid.ColumnDefinitions.Add(new ColumnDefinition());
        detailsGrid.ColumnDefinitions.Add(new ColumnDefinition { Width = GridLength.Auto });
        Grid.SetColumnSpan(_browserDetailArt, 2); detailsGrid.Children.Add(_browserDetailArt);
        var shade = new LinearGradientBrush { StartPoint = new Point(0, 0), EndPoint = new Point(1, 0) };
        shade.GradientStops.Add(new GradientStop(NightColor("#E605060D"), 0)); shade.GradientStops.Add(new GradientStop(NightColor("#8005060D"), 1));
        detailsGrid.Children.Add(new Border { CornerRadius = new CornerRadius(18), Background = shade }.Also(b => Grid.SetColumnSpan(b, 2)));
        _browserDetailText = new StackPanel { MaxWidth = 640, RenderTransform = new TranslateTransform() };
        // Shrinks (never grows) to fit a small window or an open diagnostics log, instead of being cut off.
        detailsGrid.Children.Add(new Viewbox { Child = _browserDetailText, StretchDirection = StretchDirection.DownOnly, Margin = new Thickness(30, 20, 20, 20), VerticalAlignment = VerticalAlignment.Center, HorizontalAlignment = HorizontalAlignment.Left });
        _browserDetailCard = new Border
        {
            CornerRadius = new CornerRadius(16), BorderBrush = Hex(Night.FrostEdge), BorderThickness = new Thickness(1), Margin = new Thickness(0, 18, 18, 18),
            Effect = new DropShadowEffect { BlurRadius = 22, ShadowDepth = 6, Opacity = .5, Color = Colors.Black }
        };
        // Same shape as the old home art card (500 x 290), sized by the panel's height.
        _browserDetailCard.SizeChanged += (_, __) => { if (_browserDetailCard.ActualHeight > 0) _browserDetailCard.Width = Math.Min(500, _browserDetailCard.ActualHeight * 500 / 290); };
        Grid.SetColumn(_browserDetailCard, 1); detailsGrid.Children.Add(_browserDetailCard);

        // PLAY, folder, match settings and the build pill are the home screen's own controls (with their
        // launch / quit / link-folder logic), moved here; the download progress comes with them.
        _browserPlayArea = new StackPanel();
        _browserPlayArea.PreviewMouseLeftButtonDown += (_, __) => CommitHeroSeason();   // PLAY / folder act on the season shown
        if (_heroPlay?.Parent is Panel heroActions)
        {
            if (heroActions.Parent is Panel heroText) heroText.Children.Remove(heroActions);
            _browserPlayArea.Children.Add(heroActions);
        }
        if (DownloadPanel.Parent is Panel downloadParent) { downloadParent.Children.Remove(DownloadPanel); _browserPlayArea.Children.Add(DownloadPanel); }
        // Why the shown season can't start yet (missing files, still copying, not linked), refreshed live.
        _browserSeasonHint = new TextBlock { FontSize = 12.5, Foreground = Hex("#FFD580"), TextWrapping = TextWrapping.Wrap, MaxWidth = 560, HorizontalAlignment = HorizontalAlignment.Left, Margin = new Thickness(2, 12, 0, 0), Visibility = Visibility.Collapsed };
        _browserPlayArea.Children.Add(_browserSeasonHint);
        _browserDetails = new Border { Child = detailsGrid, CornerRadius = new CornerRadius(18), BorderBrush = Hex(Night.FrostEdge), BorderThickness = new Thickness(1), ClipToBounds = true, Margin = new Thickness(0, 0, 0, 14) };
        _browserDetails.SizeChanged += (_, __) => detailsGrid.Clip = new RectangleGeometry(new Rect(0, 0, _browserDetails.ActualWidth, _browserDetails.ActualHeight), 17, 17);
        Grid.SetRow(_browserDetails, 1); view.Children.Add(_browserDetails);

        // ---- Timeline: arrow | cards on an arc | arrow.
        var timelineRow = new Grid { Height = TimelineCardHeight + 46 };
        timelineRow.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(56) });
        timelineRow.ColumnDefinitions.Add(new ColumnDefinition());
        timelineRow.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(56) });
        var left = NightRoundIcon("", "Earlier season", 46, 16); left.VerticalAlignment = VerticalAlignment.Center; left.HorizontalAlignment = HorizontalAlignment.Left;
        left.Click += (_, __) => SelectBrowserSeason(_browserIndex - 1);
        var right = NightRoundIcon("", "Later season", 46, 16); right.VerticalAlignment = VerticalAlignment.Center; right.HorizontalAlignment = HorizontalAlignment.Right;
        right.Click += (_, __) => SelectBrowserSeason(_browserIndex + 1);
        Grid.SetColumn(right, 2);
        _timeline = new Canvas { ClipToBounds = true, Background = Brushes.Transparent };
        Grid.SetColumn(_timeline, 1);
        timelineRow.Children.Add(_timeline); timelineRow.Children.Add(left); timelineRow.Children.Add(right);
        _timeline.SizeChanged += (_, __) => LayoutTimeline(_timelinePosition?.Value ?? 0);
        // One season per wheel notch (120). Touchpads send many small deltas, which are added up
        // instead of each jumping a whole season.
        int wheel = 0;
        _timeline.MouseWheel += (_, e) =>
        {
            e.Handled = true;
            if (Math.Sign(e.Delta) != Math.Sign(wheel)) wheel = 0;
            wheel += e.Delta;
            if (Math.Abs(wheel) < 120) return;
            int steps = wheel / 120; wheel -= steps * 120;
            SelectBrowserSeason(_browserIndex - steps);
        };
        Grid.SetRow(timelineRow, 2); view.Children.Add(timelineRow);

        // ---- Custom slider: a track with a draggable gold thumb, synced with the timeline.
        _sliderTrack = new Border { Height = 6, CornerRadius = new CornerRadius(3), Background = Hex("#26FFFFFF"), Margin = new Thickness(56, 10, 56, 4), Cursor = Cursors.Hand };
        var sliderHost = new Grid { Margin = _sliderTrack.Margin, Height = 26 };
        _sliderTrack.Margin = new Thickness(0); _sliderTrack.VerticalAlignment = VerticalAlignment.Center;
        _sliderThumb = new Border { Height = 14, Width = 64, CornerRadius = new CornerRadius(7), Background = GoldFill(), HorizontalAlignment = HorizontalAlignment.Left, VerticalAlignment = VerticalAlignment.Center, RenderTransform = new TranslateTransform(), Cursor = Cursors.SizeWE };
        sliderHost.Children.Add(_sliderTrack); sliderHost.Children.Add(_sliderThumb);
        sliderHost.Background = Brushes.Transparent;
        sliderHost.MouseLeftButtonDown += (_, e) => { _sliderDragging = true; sliderHost.CaptureMouse(); SliderTo(e.GetPosition(sliderHost).X, sliderHost.ActualWidth, live: true); e.Handled = true; };
        sliderHost.MouseMove += (_, e) => { if (_sliderDragging) SliderTo(e.GetPosition(sliderHost).X, sliderHost.ActualWidth, live: true); };
        sliderHost.MouseLeftButtonUp += (_, e) => { if (!_sliderDragging) return; _sliderDragging = false; sliderHost.ReleaseMouseCapture(); SliderTo(e.GetPosition(sliderHost).X, sliderHost.ActualWidth, live: false); e.Handled = true; };
        sliderHost.SizeChanged += (_, __) => SyncSlider(_timelinePosition?.Value ?? 0);
        Grid.SetRow(sliderHost, 3); view.Children.Add(sliderHost);
        _browserSliderHost = sliderHost; // the music player card sits just above it (LauncherMusic.cs)

        _timelinePosition = new TimelinePosition { Visibility = Visibility.Collapsed };
        _timelinePosition.Changed = value => { LayoutTimeline(value); SyncSlider(value); };
        view.Children.Add(_timelinePosition);

        view.IsVisibleChanged += (_, __) => { if (view.IsVisible) view.Focus(); };
        view.Focusable = true; view.FocusVisualStyle = null;
        view.KeyDown += (_, e) =>
        {
            if (e.Key == Key.Left) { SelectBrowserSeason(_browserIndex - 1); e.Handled = true; }
            else if (e.Key == Key.Right) { SelectBrowserSeason(_browserIndex + 1); e.Handled = true; }
        };

        // Start where the home screen is.
        _browserChapter = ChapterOf(_heroSeason) == 2 ? 1 : 2; // so the call below counts as a change
        SelectBrowserChapter(ChapterOf(_heroSeason), animate: false);
        return view;
    }

    // One row of the chapter dropdown: art preview, name, season range and availability.
    private Button ChapterOption(int chapter, string range, string availability)
    {
        var row = new Grid { Margin = new Thickness(4) };
        row.ColumnDefinitions.Add(new ColumnDefinition { Width = GridLength.Auto });
        row.ColumnDefinitions.Add(new ColumnDefinition());
        var preview = new Border { Width = 84, Height = 52, CornerRadius = TiltCorners(10), Background = chapter == 1 ? SeasonBrush(1) : chapter == 3 ? BrowserArt(BrowserChapter3[0]) : BrowserArt(BrowserChapter2[0]), BorderBrush = Hex(Night.FrostEdge), BorderThickness = new Thickness(1) };
        row.Children.Add(preview);
        var text = new StackPanel { Margin = new Thickness(12, 0, 0, 0), VerticalAlignment = VerticalAlignment.Center };
        text.Children.Add(Display("CHAPTER " + chapter, 18, Night.Text));
        text.Children.Add(NightText(range, 11.5, Night.Sub).Also(t => t.TextWrapping = TextWrapping.NoWrap));
        text.Children.Add(NightText(availability, 11, chapter == 1 ? Night.Good : Night.Gold, bold: true).Also(t => { t.TextWrapping = TextWrapping.NoWrap; t.Margin = new Thickness(0, 2, 0, 0); }));
        Grid.SetColumn(text, 1); row.Children.Add(text);
        var option = new Button { Content = row, Padding = new Thickness(6), HorizontalContentAlignment = HorizontalAlignment.Stretch, Margin = new Thickness(0, 2, 0, 2) };
        RoundButton(option, "#00000000", Night.Text, 12);
        option.HorizontalContentAlignment = HorizontalAlignment.Stretch;
        option.MouseEnter += (_, __) => option.Background = Hex(Night.Hover); option.MouseLeave += (_, __) => option.Background = Hex("#00000000");
        option.Click += (_, __) => { if (_chapterPopup != null) _chapterPopup.IsOpen = false; SelectBrowserChapter(chapter); };
        return option;
    }

    // Art for seasons Syndicate has no picture of: the chapter's colours with a big season number.
    private static Brush FallbackSeasonArt(BrowserSeason season)
    {
        // Chapter 2 Seasons 3-8 use the real key art embedded by the launcher build.
        if (season.Chapter == 2 && season.Number >= 3 && season.Number <= 8)
        {
            try
            {
                using var s = typeof(MainWindow).Assembly.GetManifestResourceStream("SyndicateClassic.Chapter2Season" + season.Number);
                if (s != null)
                {
                    var image = new System.Windows.Media.Imaging.BitmapImage();
                    image.BeginInit();
                    image.CacheOption = System.Windows.Media.Imaging.BitmapCacheOption.OnLoad;
                    image.StreamSource = s;
                    image.EndInit();
                    image.Freeze();
                    return new ImageBrush(image) { Stretch = Stretch.UniformToFill, AlignmentX = AlignmentX.Center, AlignmentY = AlignmentY.Top };
                }
            }
            catch { }
        }
        string[] colors = season.Chapter == 3
            ? new[] { "#22D3EE" }
            : season.Chapter == 2
            ? new[] { "#3B82F6", "#6D28D9", "#0EA5E9", "#DC2626", "#CA8A04", "#16A34A", "#7C3AED", "#2563EB" }
            : new[] { "#38BDF8" };
        var accent = NightColor(colors[Math.Abs(season.Number - 1) % colors.Length]);
        var group = new DrawingGroup();
        group.Children.Add(new GeometryDrawing(new LinearGradientBrush(accent, NightColor("#0B0E1F"), 60), null, new RectangleGeometry(new Rect(0, 0, 160, 200))));
        var text = new FormattedText((season.Chapter == 3 ? "C3" : season.Chapter == 2 ? "C2" : "C1") + "S" + (season.Number == 10 ? "X" : season.Number.ToString()), System.Globalization.CultureInfo.InvariantCulture, FlowDirection.LeftToRight,
            new Typeface(DisplayFont, FontStyles.Normal, FontWeights.Bold, FontStretches.Condensed), 64, new SolidColorBrush(Color.FromArgb(70, 255, 255, 255)), 1.0);
        group.Children.Add(new GeometryDrawing(new SolidColorBrush(Color.FromArgb(70, 255, 255, 255)), null, text.BuildGeometry(new Point(14, 112))));
        return new DrawingBrush(group) { Stretch = Stretch.UniformToFill };
    }

    // Season XI (PARADOX) is a dev slot that PLAYS on Season X's build but has its OWN art (season-art\
    // season90.png -> SyndicateClassic.SeasonCard90). Load it directly so the Season-X splash/art path can't
    // override it with Season X's picture. (season12.png is now Chapter 2 Season 2's key art.)
    private static ImageSource? _paradoxArt;
    private static bool _paradoxTried;
    private static ImageSource? ParadoxArt()
    {
        if (_paradoxTried) return _paradoxArt;
        _paradoxTried = true;
        try
        {
            using var s = typeof(MainWindow).Assembly.GetManifestResourceStream("SyndicateClassic.SeasonCard90");
            if (s != null) { var i = new System.Windows.Media.Imaging.BitmapImage(); i.BeginInit(); i.CacheOption = System.Windows.Media.Imaging.BitmapCacheOption.OnLoad; i.StreamSource = s; i.EndInit(); i.Freeze(); _paradoxArt = i; }
        }
        catch { }
        return _paradoxArt;
    }
    private static bool IsParadox(BrowserSeason s) => s.Chapter == 1 && s.Number == 11;
    // True while PARADOX (not Season X) is the chosen Season-X-build season. Default false, so a stale
    // stw-loot.on from an earlier PARADOX session never makes a plain Season X launch show STW loot - the
    // launch path re-affirms the flag from this.
    private bool _paradoxLoot;
    // PARADOX (Season XI) runs on the Season X 10.31 build but uses Save the World loot; Season X uses normal
    // loot. The match server reads Runtime\Server\stw-loot.on to decide (FortLootPackage SyndicateStwLootOn, gated
    // to the 10.x build). Write it when PARADOX is the chosen season, delete it otherwise, so the loot follows
    // which season was launched rather than just the build version.
    private void SetParadoxStwLoot(bool on)
    {
        if (_paths == null) return;
        try
        {
            var dir = System.IO.Path.Combine(_paths.Root, "Runtime", "Server");
            System.IO.Directory.CreateDirectory(dir);
            var flag = System.IO.Path.Combine(dir, "stw-loot.on");
            if (on) System.IO.File.WriteAllText(flag, "paradox");
            else if (System.IO.File.Exists(flag)) System.IO.File.Delete(flag);
        }
        catch { }
    }
    private Brush BrowserArt(BrowserSeason season)
    {
        if (IsParadox(season) && ParadoxArt() is { } art) return new ImageBrush(art) { Stretch = Stretch.UniformToFill, AlignmentX = AlignmentX.Left };
        return season.SyndicateSeason is int syndicate ? SeasonBrush(syndicate) : FallbackSeasonArt(season);
    }

    // Card thumbnails: tall art is shown whole (it already has the card's shape); wide art keeps the
    // hand-tuned crop the home screen uses, so the characters stay in frame.
    private Brush CardArt(BrowserSeason season)
    {
        if (IsParadox(season) && ParadoxArt() is { } art) return new ImageBrush(art) { Stretch = Stretch.UniformToFill, AlignmentX = AlignmentX.Left };
        if (season.SyndicateSeason is not int syndicate) return FallbackSeasonArt(season);
        var image = HeroSeasonImage(syndicate);
        if (image is System.Windows.Media.Imaging.BitmapSource bitmap && bitmap.PixelHeight > bitmap.PixelWidth * 0.9)
            return new ImageBrush(image) { Stretch = Stretch.UniformToFill, AlignmentY = AlignmentY.Top };
        return SeasonBrush(syndicate);
    }

    private void SelectBrowserChapter(int chapter, bool animate = true)
    {
        bool changed = chapter != _browserChapter;
        if (!changed && animate) return;
        int direction = chapter > _browserChapter ? 1 : -1;
        _browserChapter = chapter;
        if (changed) _browserIndex = Math.Max(0, Array.FindIndex(BrowserSeasons(chapter), s => s.SyndicateSeason == _heroSeason));
        if (_chapterDropdownText != null) _chapterDropdownText.Text = "CHAPTER " + chapter;
        BuildTimelineCards();
        // Chapters slide in from the side they sit on (Chapter 2 from the right).
        if (_timeline != null && animate && MotionEnabled)
        {
            var slide = new TranslateTransform(); _timeline.RenderTransform = slide;
            slide.BeginAnimation(TranslateTransform.XProperty, new DoubleAnimation(90 * direction, 0, TimeSpan.FromMilliseconds(420)) { EasingFunction = new CubicEase { EasingMode = EasingMode.EaseOut } });
            _timeline.BeginAnimation(OpacityProperty, new DoubleAnimation(0, 1, TimeSpan.FromMilliseconds(360)));
        }
        _timelinePosition?.BeginAnimation(TimelinePosition.ValueProperty, null);
        if (_timelinePosition != null) _timelinePosition.Value = _browserIndex;
        LayoutTimeline(_browserIndex); SyncSlider(_browserIndex);
        ShowBrowserDetails(direction, animate);
    }

    // Making a season the launcher's season saves it to disk and refreshes the home controls, which is
    // too heavy to do mid-scroll. It runs once the timeline settles (or at once when PLAY etc. is pressed).
    private System.Windows.Threading.DispatcherTimer? _heroSeasonTimer;
    private int _pendingHeroSeason = -1;

    private void QueueHeroSeason(int season)
    {
        if (season == _heroSeason) { _pendingHeroSeason = -1; _heroSeasonTimer?.Stop(); return; }
        _pendingHeroSeason = season;
        if (_heroSeasonTimer == null)
        {
            _heroSeasonTimer = new System.Windows.Threading.DispatcherTimer { Interval = TimeSpan.FromMilliseconds(700) };
            _heroSeasonTimer.Tick += (_, __) => CommitHeroSeason();
        }
        _heroSeasonTimer.Stop(); _heroSeasonTimer.Start();
    }

    private void CommitHeroSeason()
    {
        _heroSeasonTimer?.Stop();
        int season = _pendingHeroSeason; _pendingHeroSeason = -1;
        if (season >= 0 && season != _heroSeason) SelectHeroSeason(season, animate: false);
    }

    // Keeps the browser on the season the rest of the launcher has selected (multiplayer, saved choice).
    private void SyncBrowserToHero()
    {
        if (_seasonBrowserView == null || _browserChapter != ChapterOf(_heroSeason) || _pendingHeroSeason >= 0) return;
        int index = Array.FindIndex(BrowserSeasons(_browserChapter), s => s.SyndicateSeason == _heroSeason);
        if (index >= 0 && index != _browserIndex) SelectBrowserSeason(index);
    }

    // Brushes reused every frame (parsing a colour per card per frame shows up as stutter).
    private static readonly Brush CardEdge = Frozen("#33FFFFFF"), CardEdgeHover = Frozen("#80FFFFFF"), CardEdgeSelected = Frozen(Night.Gold);
    private static Brush Frozen(string hex) { var brush = Hex(hex); brush.Freeze(); return brush; }

    private void BuildTimelineCards()
    {
        if (_timeline == null) return;
        _timeline.Children.Clear(); _timelineCards.Clear();
        var seasons = BrowserSeasons(_browserChapter);
        for (int i = 0; i < seasons.Length; i++)
        {
            var season = seasons[i]; int index = i;
            var art = new Border { Background = CardArt(season) };
            var shade = new Border { Background = new LinearGradientBrush(Color.FromArgb(0, 0, 0, 0), Color.FromArgb(230, 0, 0, 0), 90) { StartPoint = new Point(0, .4), EndPoint = new Point(0, 1) } };
            var label = new StackPanel { VerticalAlignment = VerticalAlignment.Bottom, Margin = new Thickness(12, 0, 12, 12) };
            label.Children.Add(NightText("CHAPTER " + season.Chapter, 10, SeasonAccentFor(season), bold: true).Also(t => t.TextWrapping = TextWrapping.NoWrap));
            label.Children.Add(new Viewbox { Child = Display(season.Label, 22, Night.Text), StretchDirection = StretchDirection.DownOnly, HorizontalAlignment = HorizontalAlignment.Left });
            if (season.Name != null) label.Children.Add(NightText(season.Name, 11, "#D7DCEE", bold: true).Also(t => t.TextWrapping = TextWrapping.NoWrap));
            var inner = new Grid(); inner.Children.Add(art); inner.Children.Add(shade); inner.Children.Add(label);
            inner.Clip = new RectangleGeometry(new Rect(0, 0, TimelineCardWidth - 4, TimelineCardHeight - 4), 14, 14);
            // No drop shadow (it is re-drawn every frame while cards move); a dark outline gives the depth.
            // The card is drawn once into a cached bitmap; moving, scaling and fading it then costs almost nothing.
            var card = new Border
            {
                Width = TimelineCardWidth, Height = TimelineCardHeight, CornerRadius = new CornerRadius(16), Child = inner, BorderThickness = new Thickness(2), BorderBrush = CardEdge,
                Cursor = Cursors.Hand, RenderTransformOrigin = new Point(.5, .5), ToolTip = season.Label + (season.Name != null ? " · " + season.Name : "") + "  (" + season.Dates + ")",
                RenderTransform = new TransformGroup { Children = { new ScaleTransform(1, 1), new TranslateTransform() } },
                CacheMode = new BitmapCache { RenderAtScale = 1.25, SnapsToDevicePixels = false }
            };
            // Hover: outline and a slight lift; nothing else is re-laid out.
            card.MouseEnter += (_, __) => { card.Tag = "hover"; card.BorderBrush = index == _browserIndex ? CardEdgeSelected : CardEdgeHover; LayoutTimeline(_timelinePosition?.Value ?? _browserIndex); };
            card.MouseLeave += (_, __) => { card.Tag = null; card.BorderBrush = index == _browserIndex ? CardEdgeSelected : CardEdge; LayoutTimeline(_timelinePosition?.Value ?? _browserIndex); };
            // Handled on press: the window drags from any unhandled press, which would swallow the release.
            card.MouseLeftButtonDown += (_, e) => { SelectBrowserSeason(index); e.Handled = true; };
            Canvas.SetLeft(card, 0); Canvas.SetTop(card, 0);
            _timelineCards.Add(card); _timeline.Children.Add(card);
        }
    }

    private static string SeasonAccentFor(BrowserSeason season) =>
        season.Chapter == 2 && season.Number >= 3 ? Chapter2Accent(season.Number)
        : season.Chapter == 1 && (season.Number == 0 || season.Number == 3) ? Chapter1Accent(season.Number)
        : season.SyndicateSeason is int syndicate ? SeasonAccent(syndicate)
        : Night.Sky;

    // The arc: cards are placed around the middle of the timeline by their distance d from the
    // animated position. Further cards are smaller, lower and dimmer; the nearest is on top. Only
    // render transforms and opacity change per frame (no layout pass), and the stacking order and
    // outlines are only touched when they actually change.
    private void LayoutTimeline(double position)
    {
        if (_timeline == null || _timelineCards.Count == 0) return;
        double width = _timeline.ActualWidth, height = _timeline.ActualHeight;
        if (width <= 0) return;
        double centerX = width / 2, top = Math.Max(4, (height - TimelineCardHeight) / 2 - 8), fade = Math.Min(90, width * .08);
        for (int i = 0; i < _timelineCards.Count; i++)
        {
            var card = _timelineCards[i];
            double d = i - position, distance = Math.Abs(d);
            bool hover = Equals(card.Tag, "hover");
            double scale = Math.Max(.62, 1 - .13 * distance) * (hover ? 1.04 : 1);
            double x = centerX + d * TimelineSpacing * (1 - .06 * Math.Min(distance, 3)) - TimelineCardWidth / 2;
            double y = top + Math.Min(distance * distance * 7, 60);
            if (card.RenderTransform is TransformGroup group && group.Children.Count == 2 && group.Children[0] is ScaleTransform s && group.Children[1] is TranslateTransform t)
            {
                s.ScaleX = scale; s.ScaleY = scale; t.X = x; t.Y = y;
            }
            // Fade by distance, and towards the ends of the strip so nothing looks cut off.
            double middle = x + TimelineCardWidth / 2, edge = Math.Min(middle, width - middle);
            double opacity = Math.Max(0, Math.Min(1, 1.15 - .23 * distance)) * Math.Clamp(edge / fade, 0, 1);
            card.Opacity = opacity;
            var visibility = opacity <= .01 ? Visibility.Hidden : Visibility.Visible;
            if (card.Visibility != visibility) card.Visibility = visibility;
            int z = 1000 - (int)Math.Round(distance * 4);
            if (Panel.GetZIndex(card) != z) Panel.SetZIndex(card, z);
            var outline = i == _browserIndex ? CardEdgeSelected : hover ? CardEdgeHover : CardEdge;
            if (!ReferenceEquals(card.BorderBrush, outline)) card.BorderBrush = outline;
        }
    }

    private void SyncSlider(double position)
    {
        if (_sliderThumb == null || _sliderTrack == null || _sliderThumb.Parent is not FrameworkElement host) return;
        int count = _timelineCards.Count;
        double width = host.ActualWidth;
        if (width <= 0 || count == 0) return;
        double thumb = Math.Max(44, width / Math.Max(1, count) * 1.4);
        if (Math.Abs(_sliderThumb.Width - thumb) > .5) _sliderThumb.Width = thumb;   // set only on resize: a size change means a layout pass
        double travel = Math.Max(0, width - _sliderThumb.Width);
        double fraction = count <= 1 ? 0 : Math.Clamp(position / (count - 1), 0, 1);
        ((TranslateTransform)_sliderThumb.RenderTransform).X = travel * fraction;
    }

    // Slider drag / click: live while dragging (follows the pointer), snaps to a season on release.
    private void SliderTo(double x, double width, bool live)
    {
        int count = _timelineCards.Count;
        if (count == 0 || width <= 0 || _timelinePosition == null || _sliderThumb == null) return;
        double travel = Math.Max(1, width - _sliderThumb.Width);
        double fraction = Math.Clamp((x - _sliderThumb.Width / 2) / travel, 0, 1);
        double position = fraction * (count - 1);
        if (live)
        {
            _timelinePosition.BeginAnimation(TimelinePosition.ValueProperty, null);
            _timelinePosition.Value = position;
            int nearest = (int)Math.Round(position);
            if (nearest != _browserIndex) { int direction = nearest > _browserIndex ? 1 : -1; _browserIndex = nearest; ShowBrowserDetails(direction, animate: false); }
        }
        else SelectBrowserSeason((int)Math.Round(position), force: true);
    }

    private void SelectBrowserSeason(int index, bool force = false)
    {
        var seasons = BrowserSeasons(_browserChapter);
        index = Math.Clamp(index, 0, seasons.Length - 1);
        if (index == _browserIndex && !force && Math.Abs((_timelinePosition?.Value ?? index) - index) < .001) return;
        int direction = index > _browserIndex ? 1 : index < _browserIndex ? -1 : 0;
        bool newSeason = index != _browserIndex;
        _browserIndex = index;
        if (_timelinePosition != null)
        {
            double from = _timelinePosition.Value;
            var time = TimeSpan.FromMilliseconds(MotionEnabled ? 380 + 70 * Math.Min(4, Math.Abs(index - from)) : 0);
            _timelinePosition.BeginAnimation(TimelinePosition.ValueProperty, new DoubleAnimation(from, index, time) { EasingFunction = new QuinticEase { EasingMode = EasingMode.EaseOut } });
        }
        if (newSeason || force) ShowBrowserDetails(direction, animate: true);
    }

    // Details for the selected season, sliding in from the direction of travel.
    private void ShowBrowserDetails(int direction, bool animate)
    {
        if (_browserDetailText == null || _browserDetailArt == null) return;
        var season = BrowserSeasons(_browserChapter)[Math.Clamp(_browserIndex, 0, BrowserSeasons(_browserChapter).Length - 1)];
        _browserDetailArt.Background = BrowserArt(season);
        var text = _browserDetailText; text.Children.Clear();
        string eyebrowColor = season.Chapter == 2 ? Chapter2Accent(season.Number)
            : season.Chapter == 1 && (season.Number == 0 || season.Number == 3) ? Chapter1Accent(season.Number)
            : Night.Gold;
        text.Children.Add(NightEyebrow(("Chapter " + season.Chapter + "  ·  " + season.Label).ToUpperInvariant(), eyebrowColor));
        var title = Display(season.Name ?? season.Label, 64, Night.Text);
        text.Children.Add(new Viewbox { Child = title, StretchDirection = StretchDirection.DownOnly, HorizontalAlignment = HorizontalAlignment.Left, Margin = new Thickness(0, 2, 0, 2) });
        var dates = new StackPanel { Orientation = Orientation.Horizontal, Margin = new Thickness(0, 0, 0, 10) };
        dates.Children.Add(GlyphIcon("", 13, Night.Gold).Also(g => g.Margin = new Thickness(0, 0, 8, 0)));
        dates.Children.Add(NightText(season.Dates, 13.5, Night.Text, bold: true).Also(t => t.TextWrapping = TextWrapping.NoWrap));
        if (season.SyndicateSeason is int syndicate && ModernSeasons.FirstOrDefault(s => s.Number == syndicate) is { } info)
            dates.Children.Add(NightText("   ·   Fortnite v" + info.Build + "   ·   " + System.Globalization.CultureInfo.CurrentCulture.TextInfo.ToTitleCase(info.Tagline.ToLowerInvariant()), 13, Night.Sub).Also(t => t.TextWrapping = TextWrapping.NoWrap));
        text.Children.Add(dates);
        text.Children.Add(Body(season.Description, 14.5, "#C9CFE6").Also(t => { t.LineHeight = 22; t.Margin = new Thickness(0, 0, 0, 12); }));
        var features = new WrapPanel { Margin = new Thickness(0, 0, 0, 16) };
        foreach (var feature in season.Features)
            features.Children.Add(new Border { Background = Hex("#26FFFFFF"), BorderBrush = Hex("#33FFFFFF"), BorderThickness = new Thickness(1), CornerRadius = TiltCorners(12), Padding = new Thickness(10, 3, 10, 3), Margin = new Thickness(0, 0, 8, 6), Child = NightText(feature, 11.5, Night.Text, bold: true).Also(t => t.TextWrapping = TextWrapping.NoWrap) });
        text.Children.Add(features);

        // Only seasons Syndicate can run get PLAY: selecting one makes it the launcher's season, so the
        // home controls (PLAY / LINK FOLDER / QUIT, folder, settings, build) act on it.
        if (_browserDetailCard != null) _browserDetailCard.Background = season.SyndicateSeason is int art ? SeasonBrush(art) : FallbackSeasonArt(season);
        if (season.SyndicateSeason is int playable && _browserPlayArea != null)
        {
            QueueHeroSeason(playable);
            // PARADOX and Season X share the same 10.31 build, so the match server tells them apart only by the
            // stw-loot.on flag: write it for PARADOX (Save the World loot), remove it for Season X and every other
            // season (normal loot). Keeps STW loot from bleeding into Season X.
            _paradoxLoot = IsParadox(season);
            SetParadoxStwLoot(_paradoxLoot);
            if (_browserSeasonHint != null && ModernSeasons.FirstOrDefault(s => s.Number == playable) is { } shown)
            {
                var hint = SeasonHint(shown);
                _browserSeasonHint.Text = hint ?? "";
                _browserSeasonHint.Visibility = hint == null ? Visibility.Collapsed : Visibility.Visible;
            }
            text.Children.Add(_browserPlayArea);
        }
        else
            text.Children.Add(new Border { Background = Hex("#26FFD23F"), BorderBrush = Hex("#40FFD23F"), BorderThickness = new Thickness(1), CornerRadius = TiltCorners(14), Padding = new Thickness(14, 6, 14, 6), HorizontalAlignment = HorizontalAlignment.Left,
                Child = NightText(season.Chapter == 1 ? "Dev slot - no build assigned yet. Browse only." : "Chapter 2 isn't playable in Syndicate yet. Browse only.", 12.5, Night.Gold, bold: true).Also(t => t.TextWrapping = TextWrapping.NoWrap) });

        if (animate && MotionEnabled && direction != 0)
        {
            var ease = new CubicEase { EasingMode = EasingMode.EaseOut };
            ((TranslateTransform)text.RenderTransform).BeginAnimation(TranslateTransform.XProperty, new DoubleAnimation(-60 * direction, 0, TimeSpan.FromMilliseconds(380)) { EasingFunction = ease });
            text.BeginAnimation(OpacityProperty, new DoubleAnimation(0, 1, TimeSpan.FromMilliseconds(320)));
            _browserDetailArt.BeginAnimation(OpacityProperty, new DoubleAnimation(.05, .22, TimeSpan.FromMilliseconds(420)) { EasingFunction = ease });
        }
        LayoutTimeline(_timelinePosition?.Value ?? _browserIndex);
    }
}