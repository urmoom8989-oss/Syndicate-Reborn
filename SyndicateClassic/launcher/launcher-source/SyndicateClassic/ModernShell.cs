// Syndicate © 2026 Syndicate. All rights reserved. Proprietary — no copying, distribution, or derivative works. See LICENSE.
using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.IO;
using System.Linq;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Controls.Primitives;
using System.Windows.Media;
using System.Windows.Media.Animation;
using System.Windows.Media.Effects;
using System.Windows.Media.Imaging;
namespace SyndicateClassic;

// Launcher home in the "Night Drop" style: the season's art fills the window (blurred, with a glow
// in the season's colour), a frosted icon rail on the left, one status pill in the title bar, and
// Syndicate's own season picker, big title and gold PLAY button.
// All existing buttons and their logic are reused; this file only rearranges and restyles them.
public partial class MainWindow
{
    private sealed record SeasonInfo(int Number, string Build, string Tagline, string Description);
    private static readonly SeasonInfo[] ModernSeasons =
    {
        new(0, "1.7.2", "WHERE IT STARTED", "Battle Royale's first weeks: the original island with Anarchy Acres, Moisty Mire, Tomato Town and Dusty Depot. No Battle Pass yet."),
        new(1, "1.8.2", "THE FIRST SEASON", "Season 1 on the original map, with Renegade Raider and Aerial Assault Trooper in the season shop."),
        new(2, "1.11", "KNIGHTS & CASTLES", "The medieval season and the very first Battle Pass: 70 tiers, the Black Knight and the Blue Squire."),
        new(3, "3.5.1", "OUT OF THIS WORLD", "A comet lights up the sky, jetpacks take off and the first 100-tier Battle Pass ends with The Reaper."),
        new(4, "4.5", "BRACE FOR IMPACT", "The meteor hits. Dusty Divot, Hop Rocks, superheroes, Carbide and Omega."),
        new(5, "5.10", "WORLDS COLLIDE", "Rifts tear open the island: Paradise Palms, Lazy Links, All Terrain Karts, Drift and Ragnarok."),
        new(6, "6.20", "DARKNESS RISES", "The floating island, Leaky Lake, Wailing Woods corruption, pets and Shadow Stones."),
        new(7, "7.40", "YOU BETTER WATCH OUT", "Snow covers the island: Frosty Flights, Polar Peak, planes, ziplines and the Ice King."),
        new(8, "8.50", "X MARKS THE SPOT", "Pirates and volcanoes: Lazy Lagoon, Sunny Steps, cannons, buried treasure and the Pirate Cannon."),
        new(9, "9.40", "FUTURE IS YOURS", "Neo Tilted, Mega Mall, Slipstreams, the Pressure Plant and the Combat Shotgun."),
        new(10, "10.31", "OUT OF TIME", "Season X: Rifts bring back old places, B.R.U.T.E mechs, Rift Zones and Dusty Depot."),
        // Chapter 2 Season 1. Launcher season 11 = Fortnite 11.00.
        new(11, "11.00", "A NEW WORLD", "Chapter 2 begins: a brand-new island with swimming, fishing, boats, upgrade benches and hiding spots."),
        // Chapter 2 Season 2 (TOP SECRET). Launcher season 12 = Fortnite 12.50. Needs the 12.50 folder and
        // season-12 backend to host a match; until those exist, launching falls back like any unlinked season.
        new(12, "12.50", "TOP SECRET", "Spies of Ghost and Shadow: Midas, Meowscles, Skye, henchmen and boss fights at The Agency, Grotto and Yacht."),
#if TILTED_DEVKIT
        // Chapter 3 Season 1 (19.00). DEV KIT ONLY, EXPERIMENTAL: 19.00 is Unreal Engine 5 and the Erbium match
        // server is UE4, so this boots for testing but will NOT host a working match until a UE5 server exists.
        new(19, "19.00", "A NEW CHAPTER (EXPERIMENTAL)", "Chapter 3 Season 1 — Dev Kit test build. 19.00 runs on Unreal Engine 5; the match server is UE4, so this is boot-test only and does not host a playable match yet."),
#endif
    };

    // Launcher season numbers 0-10 are Chapter 1; 11 and up are Chapter 2 (11 = Chapter 2 Season 1).
    private static int ChapterOf(int number) => number >= 19 ? 3 : number >= 11 ? 2 : 1;
    private static string SeasonName(int number) => number == 10 ? "X" : number >= 19 ? (number - 18).ToString() : number >= 11 ? (number - 10).ToString() : number.ToString();
    private static string SeasonTitle(int number) => number == 0 ? "PRE-SEASON" : "SEASON " + SeasonName(number);
    private static string PickerLabel(int number) => number == 0 ? "PRE" : number >= 19 ? "C3S" + SeasonName(number) : number >= 11 ? "C2S" + SeasonName(number) : "S" + SeasonName(number);

    private static readonly string HeroSeasonPath = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "ProjectTilted", "launcher-season.txt");
    private const string PlayYellow = "#FFD23F";
    private static readonly FontFamily DisplayFont = new("Bahnschrift SemiBold Condensed, Bahnschrift, Segoe UI Black, Segoe UI");
    private static readonly FontFamily IconFont = new("Segoe Fluent Icons, Segoe MDL2 Assets");

    private int _heroSeason = 1;
    private int? _storePreviewSeason;
    private Grid? _modernPage;
    private Border? _heroBackdrop;
    private Border? _heroGlow;
    private Border? _heroArt;
    private TextBlock? _heroTitle, _heroEyebrow, _heroDescription, _heroBuild, _heroArtBuild, _heroBadgeText;
    private System.Windows.Shapes.Ellipse? _heroBadgeDot;
    private ContentControl? _heroTagline;
    private Button? _heroPlay;
    private StackPanel? _seasonPicker;
    private readonly List<Button> _railButtons = new();
    private System.Windows.Shapes.Ellipse? _modernStatusDot, _hostStatusDot, _friendsStatusDot;
    private TextBlock? _modernStatusText, _hostStatusText, _friendsStatusText;
    private Button? _maximizeButton;
    private FrameworkElement? _modernFrame;
    private Rect _restoreBounds;

    private static SolidColorBrush Hex(string hex) => (SolidColorBrush)new BrushConverter().ConvertFromString(hex)!;

    private static TextBlock Display(string text, double size, string color = "#FFFFFF") => new()
    {
        Text = text, FontSize = size, Foreground = Hex(color), FontFamily = DisplayFont,
        FontWeight = FontWeights.SemiBold, FontStretch = FontStretches.Condensed, TextWrapping = TextWrapping.NoWrap,
        RenderTransform = new SkewTransform(-8, 0), RenderTransformOrigin = new Point(.5, .5)   // big type leans forward (the Syndicate look), around its middle
    };

    private static TextBlock Body(string text, double size, string color) => new()
    {
        Text = text, FontSize = size, Foreground = Hex(color), FontFamily = new FontFamily("Segoe UI Variable Text, Segoe UI"), TextWrapping = TextWrapping.Wrap
    };

    // Outlined display text (the second title line).
    private FrameworkElement OutlineText(string text, double size)
    {
        var typeface = new Typeface(DisplayFont, FontStyles.Normal, FontWeights.SemiBold, FontStretches.Condensed);
        var formatted = new FormattedText(text, System.Globalization.CultureInfo.CurrentUICulture, FlowDirection.LeftToRight,
            typeface, size, Brushes.White, VisualTreeHelper.GetDpi(this).PixelsPerDip);
        var geometry = formatted.BuildGeometry(new Point(0, 0));
        return new System.Windows.Shapes.Path
        {
            Data = geometry, Stroke = Brushes.White, StrokeThickness = 1.8, Fill = Brushes.Transparent,
            HorizontalAlignment = HorizontalAlignment.Left, SnapsToDevicePixels = true
        };
    }

    private static void RoundButton(Button button, string background, string foreground, double radius, string? border = null)
    {
        button.Background = Hex(background); button.Foreground = Hex(foreground);
        button.BorderBrush = Hex(border ?? "#00000000"); button.BorderThickness = new Thickness(border == null ? 0 : 1.5);
        button.Cursor = System.Windows.Input.Cursors.Hand; button.FocusVisualStyle = null;
        var chrome = new FrameworkElementFactory(typeof(Border));
        chrome.SetValue(Border.CornerRadiusProperty, TiltCorners(radius));   // leaning corners (NightDrop.cs)
        chrome.SetValue(Border.BackgroundProperty, new TemplateBindingExtension(Control.BackgroundProperty));
        chrome.SetValue(Border.BorderBrushProperty, new TemplateBindingExtension(Control.BorderBrushProperty));
        chrome.SetValue(Border.BorderThicknessProperty, new TemplateBindingExtension(Control.BorderThicknessProperty));
        chrome.SetValue(Border.PaddingProperty, new TemplateBindingExtension(Control.PaddingProperty));
        var content = new FrameworkElementFactory(typeof(ContentPresenter));
        content.SetValue(HorizontalAlignmentProperty, new TemplateBindingExtension(Control.HorizontalContentAlignmentProperty));
        content.SetValue(VerticalAlignmentProperty, VerticalAlignment.Center);
        chrome.AppendChild(content);
        var template = new ControlTemplate(typeof(Button)) { VisualTree = chrome };
        var hover = new Trigger { Property = IsMouseOverProperty, Value = true }; hover.Setters.Add(new Setter(OpacityProperty, 0.9)); template.Triggers.Add(hover);
        var disabled = new Trigger { Property = IsEnabledProperty, Value = false }; disabled.Setters.Add(new Setter(OpacityProperty, 0.45)); template.Triggers.Add(disabled);
        button.Template = template; button.HorizontalContentAlignment = HorizontalAlignment.Center;
        AddButtonMotion(button);
    }

    // Give a button a clean, fully-rounded template with no lean or parallelogram chrome, keeping its colours.
    private static void FlatTemplate(Button button, double radius)
    {
        var chrome = new FrameworkElementFactory(typeof(Border));
        chrome.SetValue(Border.CornerRadiusProperty, new CornerRadius(radius));
        chrome.SetValue(Border.BackgroundProperty, new TemplateBindingExtension(Control.BackgroundProperty));
        chrome.SetValue(Border.BorderBrushProperty, new TemplateBindingExtension(Control.BorderBrushProperty));
        chrome.SetValue(Border.BorderThicknessProperty, new TemplateBindingExtension(Control.BorderThicknessProperty));
        chrome.SetValue(Border.PaddingProperty, new TemplateBindingExtension(Control.PaddingProperty));
        var content = new FrameworkElementFactory(typeof(ContentPresenter));
        content.SetValue(HorizontalAlignmentProperty, new TemplateBindingExtension(Control.HorizontalContentAlignmentProperty));
        content.SetValue(VerticalAlignmentProperty, VerticalAlignment.Center);
        chrome.AppendChild(content);
        var template = new ControlTemplate(typeof(Button)) { VisualTree = chrome };
        var hover = new Trigger { Property = IsMouseOverProperty, Value = true }; hover.Setters.Add(new Setter(OpacityProperty, 0.92)); template.Triggers.Add(hover);
        var disabled = new Trigger { Property = IsEnabledProperty, Value = false }; disabled.Setters.Add(new Setter(OpacityProperty, 0.45)); template.Triggers.Add(disabled);
        button.Template = template; button.HorizontalContentAlignment = HorizontalAlignment.Center;
    }

    // A clean, flat button: fully rounded corners, no lean or metallic parallelogram chrome.
    private static void FlatButton(Button button, string background, string foreground, double radius, string? border = null)
    {
        button.Background = Hex(background); button.Foreground = Hex(foreground);
        button.BorderBrush = Hex(border ?? "#00000000"); button.BorderThickness = new Thickness(border == null ? 0 : 1);
        button.Cursor = System.Windows.Input.Cursors.Hand; button.FocusVisualStyle = null;
        FlatTemplate(button, radius);
        AddButtonMotion(button);
    }

    private static System.Windows.Controls.Image? SyndicateLogo(double height)
    {
        try
        {
            using var stream = System.Reflection.Assembly.GetExecutingAssembly().GetManifestResourceStream("SyndicateClassic.Logo");
            if (stream == null) return null;
            var bitmap = new System.Windows.Media.Imaging.BitmapImage();
            bitmap.BeginInit(); bitmap.CacheOption = System.Windows.Media.Imaging.BitmapCacheOption.OnLoad; bitmap.StreamSource = stream; bitmap.EndInit(); bitmap.Freeze();
            var image = new System.Windows.Controls.Image { Source = bitmap, Height = height, Stretch = Stretch.Uniform, VerticalAlignment = VerticalAlignment.Center, SnapsToDevicePixels = true };
            RenderOptions.SetBitmapScalingMode(image, BitmapScalingMode.HighQuality);
            return image;
        }
        catch { return null; }
    }

    private static TextBlock GlyphIcon(string glyph, double size, string color = "#EEF1FF") => new()
    {
        Text = glyph, FontFamily = IconFont, FontSize = size, Foreground = Hex(color),
        HorizontalAlignment = HorizontalAlignment.Center, VerticalAlignment = VerticalAlignment.Center
    };

    private static Brush GlassBrush(byte alpha = 120) => new SolidColorBrush(Color.FromArgb(alpha, 20, 24, 49));
    private static Brush GlassEdge() => new SolidColorBrush(Color.FromArgb(64, 255, 255, 255));

    // Season pictures are decoded once and reused: the home screen, browser cards, details card and
    // dropdown all show them, and decoding a 2-6 MB picture each time slowed opening and scrolling.
    private readonly System.Collections.Generic.Dictionary<int, ImageSource> _seasonImages = new();
    private ImageSource HeroSeasonImage(int season)
    {
        if (_seasonImages.TryGetValue(season, out var cached)) return cached;
        return _seasonImages[season] = LoadHeroSeasonImage(season);
    }

    private ImageSource LoadHeroSeasonImage(int season)
    {
        if (season >= 6 || season < 2) return SeasonArtImage(season);
        if (season == 3 || season == 4 || season == 5) return SeasonCardImage(season); // season-art\season4.png replaces the old 4.5 reference picture
        var image = new BitmapImage(new Uri(season == 2
            ? "pack://application:,,,/SyndicateLauncher;component/Assets/Fortnitebp.png"
            : "pack://application:,,,/SyndicateLauncher;component/Assets/Season4Reference.png"));
        image.Freeze(); return image;
    }

    private static Rect SeasonViewbox(int season, ImageSource image) => season == 2 ? new Rect(.18, 0, .74, .84) : SeasonArtViewbox(season, image); // Season 4 now uses a full wide picture: no crop

    private ImageBrush SeasonBrush(int season)
    {
        var image = HeroSeasonImage(season);
        return new(image) { Stretch = Stretch.UniformToFill, Viewbox = SeasonViewbox(season, image), ViewboxUnits = BrushMappingMode.RelativeToBoundingBox };
    }

    // True when a game folder has been chosen for this season.
    private bool SeasonLinked(SeasonInfo season)
    {
        // Ready only when this season's own files are there. (4.5 used to count as ready whenever any game
        // folder was known, so PLAY showed for it even without its files.)
        try { return Launcher.GameFolderFor(season.Build) != null; }
        catch { return false; }
    }

    // The existing per-season launch buttons hold the real Launch/Quit logic.
    private Button SeasonLaunchButton(int season) =>
        season == 4 ? LaunchButton : _seasonLaunchButtons.FirstOrDefault(b => b.Name == "LaunchSeason" + season) ?? LaunchButton;

    private Button? SeasonFolderButton(int season) =>
        _seasonFolderButtons.FirstOrDefault(b => b.Name == "ChooseSeason" + season + "Folder");

    // True for a panel/border that holds nothing (or only other empty boxes). Hidden overlays
    // with content are not empty, so they keep their space for when they are shown.
    private static bool IsEmptyBox(UIElement element)
    {
        if (element is Panel panel) return panel.Children.OfType<UIElement>().All(IsEmptyBox);
        if (element is Decorator decorator) return decorator.Child == null || IsEmptyBox(decorator.Child);
        return false;
    }

    private void ApplyModernShell(DockPanel frame, TextBlock versionLabel)
    {
        App.Mark("modern shell start");
        var library = SettingsTree(this).OfType<Grid>().FirstOrDefault(g => g.Name == "SeasonLibrary");
        if (library?.Parent is not Grid page) return; // keep the classic layout if the structure is unexpected
        _modernPage = page;

        // Window size suited to the hero layout.
        MinWidth = 1220; MinHeight = 720;   // room for the 232 px side menu (SyndicateHome.cs)
        if (double.IsNaN(Width) || Width < 1400) Width = 1400;
        if (double.IsNaN(Height) || Height < 820) Height = 820;
        UseLayoutRounding = true; SnapsToDevicePixels = true;
        frame.Background = Brushes.Transparent;
        WindowStyle = WindowStyle.None;
        System.Windows.Shell.WindowChrome.SetWindowChrome(this, new System.Windows.Shell.WindowChrome
        {
            CaptionHeight = 0, ResizeBorderThickness = new Thickness(6), GlassFrameThickness = new Thickness(0),
            CornerRadius = new CornerRadius(0), UseAeroCaptionButtons = false
        });
        StateChanged += (_, __) =>
        {
            // A borderless maximized window extends past the screen edge and under the taskbar; pad it
            // back in (the taskbar side by its size, so the diagnostics bar at the bottom stays visible).
            var work = SystemParameters.WorkArea;
            if (_modernFrame != null) _modernFrame.Margin = WindowState == WindowState.Maximized
                ? new Thickness(7 + Math.Max(0, work.Left), 7 + Math.Max(0, work.Top), 7 + Math.Max(0, SystemParameters.PrimaryScreenWidth - work.Right), 7 + Math.Max(0, SystemParameters.PrimaryScreenHeight - work.Bottom))
                : new Thickness(0);
            if (_maximizeButton?.Content is TextBlock glyph) glyph.Text = WindowState == WindowState.Maximized ? "" : "";
            if (_maximizeButton != null) _maximizeButton.ToolTip = WindowState == WindowState.Maximized ? "Restore" : "Maximize";
        };
        PreviewKeyDown += (_, e) =>
        {
            if (e.Key != System.Windows.Input.Key.Escape) return;
            if (_profileOverlay != null && _profileOverlay.Visibility == Visibility.Visible) { if (!_authBusy) _profileOverlay.Visibility = Visibility.Collapsed; }
            else if (AuthOverlay.Visibility == Visibility.Visible) { if (_account.SignedIn && !_authBusy) AuthOverlay.Visibility = Visibility.Collapsed; }
            else if (SettingsPanel.Visibility == Visibility.Visible) SettingsPanel.Visibility = Visibility.Collapsed;
            else if (PassShop.Visibility == Visibility.Visible) { FortniteTab_Click(this, new RoutedEventArgs()); HighlightRail(0); }
            else if (_railIndex == 3 || _railIndex == 4 || _railIndex == 5 || _railIndex == PlayTab || _railIndex == OnlineTab) HighlightRail(0);
            else return;
            e.Handled = true;
        };
        versionLabel.Visibility = Visibility.Collapsed;
        // Drag the window from any empty spot, not only the title bar. Buttons, text boxes, lists and
        // scrollbars keep their clicks (they handle the press first).
        MouseLeftButtonDown += (_, e) =>
        {
            if (e.Handled || e.OriginalSource is not DependencyObject source) return;
            if (FindAncestor<ButtonBase>(source) != null || FindAncestor<System.Windows.Controls.Primitives.TextBoxBase>(source) != null
                || FindAncestor<PasswordBox>(source) != null || FindAncestor<ScrollBar>(source) != null || FindAncestor<Slider>(source) != null
                || FindAncestor<ComboBox>(source) != null || FindAncestor<ListBoxItem>(source) != null || FindAncestor<Thumb>(source) != null) return;
            // No double-click maximize (it went full screen by accident); the maximize button still does it.
            if (e.ClickCount == 1 && e.ButtonState == System.Windows.Input.MouseButtonState.Pressed) { try { DragMove(); } catch (InvalidOperationException) { } }
        };

        // Hide the old top header (its row collapses to zero height).
        var tabsButton = SettingsTree(this).OfType<Button>().FirstOrDefault(b => b.Content?.ToString() == "Library");
        var header = tabsButton?.Parent is StackPanel tabs ? tabs.Parent as Grid : null;
        foreach (var keep in new FrameworkElement[] { AccountButton, BackendDot, BackendBadgeText }) ReleaseControl(keep);
        if (header != null)
        {
            header.Visibility = Visibility.Collapsed;
            if (header.Parent is Grid owner && owner.RowDefinitions.Count > Grid.GetRow(header))
                owner.RowDefinitions[Grid.GetRow(header)].Height = new GridLength(0);
        }

        // Reuse status/download controls from the old library, then remove it.
        foreach (var keep in new FrameworkElement[] { StatusText, DownloadPanel, LogToggleButton }) ReleaseControl(keep);
        page.Children.Remove(library);
        // The old page left a strip unused at the bottom. A star row absorbs it so the side menu,
        // full-page views and the home glow fill down to the diagnostics row.
        page.RowDefinitions.Add(new RowDefinition());
        if (page.Parent is Viewbox pageFit)
        {
            // Fill the whole area above the diagnostics bar. The page keeps its design size and the
            // Viewbox stretches it; opening the log or going fullscreen resizes the page and its
            // background together instead of reflowing the content and moving the gradient.
            if (double.IsNaN(page.Width) || page.Width <= 0) page.Width = 1280;
            if (double.IsNaN(page.Height) || page.Height <= 0) page.Height = 720;
            pageFit.HorizontalAlignment = HorizontalAlignment.Stretch;
            pageFit.VerticalAlignment = VerticalAlignment.Stretch;
            pageFit.Stretch = Stretch.Fill;
            pageFit.StretchDirection = StretchDirection.Both;
        }
        int rows = Math.Max(1, page.RowDefinitions.Count);

        // Backdrop: the season's art fills the window, blurred, under a dark wash, a left-to-right
        // gradient and a glow in the season's colour. Changing season changes the whole mood.
        var backdrop = new Grid { ClipToBounds = true };
        _heroBackdrop = new Border { Margin = new Thickness(-60), Effect = new BlurEffect { Radius = 26, RenderingBias = RenderingBias.Performance } };
        backdrop.Children.Add(_heroBackdrop);
        backdrop.Children.Add(new Border { Background = Hex("#7305060D") });
        var sweep = new LinearGradientBrush { StartPoint = new Point(0, 0), EndPoint = new Point(1, 0) };
        sweep.GradientStops.Add(new GradientStop(NightColor("#7305060D"), 0));
        sweep.GradientStops.Add(new GradientStop(NightColor("#4005060D"), .55));
        sweep.GradientStops.Add(new GradientStop(NightColor("#8C05060D"), 1));
        backdrop.Children.Add(new Border { Background = sweep });
        _heroGlow = new Border { Opacity = .28 };
        backdrop.Children.Add(_heroGlow);
        backdrop.Children.Add(new Border { Background = new LinearGradientBrush(Color.FromArgb(0, 5, 6, 13), Color.FromArgb(190, 5, 6, 13), 90) });
        AddThemeLayers(backdrop); // launcher themes and effects (LauncherThemes.cs)
        backdrop.Children.Add(TiltHatch());   // the faint diagonal hatch (NightDrop.cs)
        // The art fills the whole window. Before, it was drawn only inside the page area, so the
        // old header/footer rows around it showed up as black bars above and below.
        page.Background = Brushes.Transparent;
        FrameworkElement onPath = page;
        for (var ancestor = page.Parent as FrameworkElement; ancestor != null && ancestor != frame; onPath = ancestor, ancestor = ancestor.Parent as FrameworkElement)
        {
            if (ancestor is Panel ancestorPanel)
            {
                ancestorPanel.Background = Brushes.Transparent;
                // Old rows left empty by the redesign (header, status strip) still reserved space.
                foreach (var sibling in ancestorPanel.Children.OfType<FrameworkElement>().ToList())
                    if (sibling != onPath && IsEmptyBox(sibling)) sibling.Visibility = Visibility.Collapsed;
                if (ancestorPanel is Grid rowsGrid)
                    for (int r = 0; r < rowsGrid.RowDefinitions.Count; r++)
                        if (r < Grid.GetRow(onPath) || r >= Grid.GetRow(onPath) + Grid.GetRowSpan(onPath))
                            if (rowsGrid.Children.OfType<UIElement>().Where(c => Grid.GetRow(c) <= r && r < Grid.GetRow(c) + Grid.GetRowSpan(c)).All(IsEmptyBox))
                            rowsGrid.RowDefinitions[r].Height = new GridLength(0);
            }
            else if (ancestor is Border ancestorBorder) ancestorBorder.Background = Brushes.Transparent;
            else if (ancestor is Control ancestorControl) ancestorControl.Background = Brushes.Transparent;
        }
        if (Content == frame)
        {
            Content = null;
            var root = new Grid { Background = Hex(Night.Bg) };
            root.Children.Add(backdrop);
            root.Children.Add(frame);
            Content = root;
            _modernFrame = root;
        }
        else { Grid.SetRowSpan(backdrop, rows); page.Children.Insert(0, backdrop); page.Background = Hex(Night.Bg); _modernFrame = frame; }

        var home = BuildHome(); Grid.SetRowSpan(home, rows); _homeView = home;
        page.Children.Insert(page.Children.Contains(backdrop) ? 1 : 0, home);

        // Above the store: the multiplayer page, then the rail and the title bar.
        // Settings and sign-in overlays come later in the page, so they still cover everything.
        int above = page.Children.IndexOf(PassShop);
        int at = above >= 0 ? above + 1 : page.Children.Count;
        var multiplayer = BuildMultiplayerPage(); Grid.SetRowSpan(multiplayer, rows); multiplayer.Visibility = Visibility.Collapsed;
        page.Children.Insert(at++, multiplayer);
        var stats = BuildStatsPage(); Grid.SetRowSpan(stats, rows); stats.Visibility = Visibility.Collapsed;
        page.Children.Insert(at++, stats);
        var secretShop = BuildSecretShopPage(); Grid.SetRowSpan(secretShop, rows);   // SecretShop.cs (hidden until the key code)
        page.Children.Insert(at++, secretShop);
        var browser = BuildSeasonBrowser(); Grid.SetRowSpan(browser, rows); home.Visibility = Visibility.Collapsed;   // the season browser is the Play page
        page.Children.Insert(at++, browser);
        var syndicateHome = BuildSyndicateHome(); Grid.SetRowSpan(syndicateHome, rows);   // SyndicateHome.cs: the front page
        page.Children.Insert(at++, syndicateHome);
        var online = BuildOnlinePage(); Grid.SetRowSpan(online, rows);   // OnlinePage.cs: official servers, coming soon
        page.Children.Insert(at++, online);
        var rail = BuildRail(); Grid.SetRowSpan(rail, rows);
        page.Children.Insert(at++, rail);
        var titleBar = BuildTitleBar(); Grid.SetRowSpan(titleBar, rows);
        page.Children.Insert(at, titleBar);
        // Screens reflow with the menu so no dark strip is left when it's hidden (RailCollapse.cs).
        RailTrack(home); RailTrack(titleBar); RailTrack(browser); RailTrack(syndicateHome); RailTrack(online);
        BuildDiagnosticsDrawer(page, rows);
        // Music player card (LauncherMusic.cs): on the window's root grid, above the diagnostics bar, so the page's own
        // bounds can't clip it (under the season browser's scroll bar there is only a little page left).
        var musicPlayer = BuildMusicPlayer();
        _padMusicCard = musicPlayer;   // Menu / Options puts the controller here (ControllerNav.cs)
        if (_modernFrame is Grid musicRoot && musicRoot.RowDefinitions.Count >= 2) { Grid.SetRow(musicPlayer, 0); musicRoot.Children.Add(musicPlayer); }
        else { Grid.SetRowSpan(musicPlayer, rows); page.Children.Insert(at++, musicPlayer); }
        // Profile card sits above everything.
        var profile = BuildProfileOverlay(); Grid.SetRowSpan(profile, rows);
        page.Children.Add(profile);
        var codeToast = BuildCodeToast(); Grid.SetRowSpan(codeToast, rows);
        page.Children.Add(codeToast);
        HookSecretCode();
        StartControllerNavigation(page, rows);   // ControllerNav.cs: LB/RB tabs, A/B/X/Y, button hints
        try { StartLauncherMusic(); } catch { }   // LauncherMusic.cs: loop the menu theme at the saved volume

        // Store, settings and sign-in screens sit to the right of the rail and below the title bar.
        PassShop.Padding = new Thickness(RailWidth, 52, 0, 0);
        RailTrack(PassShop, true);   // reflow with the menu (RailCollapse.cs)
        PassShop.IsVisibleChanged += (_, __) =>
        {
            if (PassShop.Visibility == Visibility.Visible) HighlightRail(1);
            else if (_railIndex == 1 || _railIndex == 2) HighlightRail(0);
        };

        foreach (var button in _seasonLaunchButtons.Append(LaunchButton))
        {
            DependencyPropertyDescriptor.FromProperty(ContentControl.ContentProperty, typeof(Button)).AddValueChanged(button, (_, __) => SyncHeroPlay());
            button.IsEnabledChanged += (_, __) => SyncHeroPlay();
        }

        try { if (int.TryParse(File.ReadAllText(HeroSeasonPath).Trim(), out var saved) && ModernSeasons.Any(s => s.Number == saved)) _heroSeason = saved; } catch { }
        _hostSeason = _heroSeason;
        SelectHeroSeason(_heroSeason, animate: false);
        StartSeasonWatch();
        StartPresenceAndControllers();
        HighlightRail(0);
        BuildPlayerPicker();
        if (!_account.SignedIn) AccountButton.Content = "CHOOSE PLAYER";
        StartFriendFinder();

        // The RGB edge sits above every page, so full-bleed art, the title bar and panels can't clip it.
        if (_rgbEdge != null)
        {
            if (_rgbEdge.Parent is Panel edgeParent) edgeParent.Children.Remove(_rgbEdge);
            if (_modernFrame is Grid modernRoot)
            {
                Grid.SetRow(_rgbEdge, 0);
                Grid.SetRowSpan(_rgbEdge, Math.Max(1, modernRoot.RowDefinitions.Count));
                Panel.SetZIndex(_rgbEdge, 100);   // above the diagnostics bar and every page
                modernRoot.Children.Add(_rgbEdge);
            }
            else
            {
                Grid.SetRowSpan(_rgbEdge, rows);
                page.Children.Add(_rgbEdge);
            }
        }

        // The side menu is scaled by the page's Viewbox: keep the diagnostics bar and its footer
        // flush with the menu's real right edge so neither one overlaps the black rail.
        void FitDiagnosticsToRail()
        {
            if (_diagBar == null || _railFoot == null || _modernFrame == null) return;
            try
            {
                double edge = rail.TransformToAncestor(_modernFrame).Transform(new Point(rail.ActualWidth, 0)).X;
                if (double.IsNaN(edge) || edge <= 0) return;
                if (Math.Abs(_diagBar.Margin.Left - edge) > .5) _diagBar.Margin = new Thickness(edge, 0, 0, 0);
                if (double.IsNaN(_railFoot.Width) || Math.Abs(_railFoot.Width - edge) > .5) _railFoot.Width = edge;
            }
            catch { }
        }
        SizeChanged += (_, __) => FitDiagnosticsToRail();
        Dispatcher.BeginInvoke(new Action(FitDiagnosticsToRail), System.Windows.Threading.DispatcherPriority.Loaded);
    }

    // Title bar (52 px): one status pill, the profile chip and flat window buttons.
    private Grid BuildTitleBar()
    {
        var bar = new Grid { Height = 52, VerticalAlignment = VerticalAlignment.Top, Margin = new Thickness(RailWidth, 0, 0, 0), Background = Brushes.Transparent };
        bar.MouseLeftButtonDown += (_, e) =>
        {
            if (e.OriginalSource is DependencyObject source && FindAncestor<Button>(source) != null) return;
            e.Handled = true;   // the window-wide drag handler must not run a second time
            // No double-click maximize (it went full screen by accident); the maximize button still does it.
            if (e.ClickCount == 1 && e.ButtonState == System.Windows.Input.MouseButtonState.Pressed) { try { DragMove(); } catch (InvalidOperationException) { } }
        };
        var right = new StackPanel { Orientation = Orientation.Horizontal, HorizontalAlignment = HorizontalAlignment.Right, VerticalAlignment = VerticalAlignment.Center };
        bar.Children.Add(right);

        // Status pill: Backend · Host · Friends nearby.
        var pill = new StackPanel { Orientation = Orientation.Horizontal, VerticalAlignment = VerticalAlignment.Center };
        void Segment(string name, out System.Windows.Shapes.Ellipse dot, out TextBlock value, bool first = false)
        {
            var row = new StackPanel { Orientation = Orientation.Horizontal, Margin = new Thickness(first ? 0 : 16, 0, 0, 0), VerticalAlignment = VerticalAlignment.Center };
            dot = NightDot(Night.Muted); dot.Margin = new Thickness(0, 0, 7, 0); row.Children.Add(dot);
            var label = NightText(name, 12, Night.Text, bold: true); label.TextWrapping = TextWrapping.NoWrap; label.Margin = new Thickness(0, 0, 5, 0); row.Children.Add(label);
            value = NightText("", 12, Night.Sub); value.TextWrapping = TextWrapping.NoWrap; row.Children.Add(value);
            pill.Children.Add(row);
        }
        Segment("Backend", out var backendDot, out var backendText, first: true);
        Segment("Host", out var hostDot, out var hostText);
        Segment("Friends", out var friendsDot, out var friendsText);
        _modernStatusDot = backendDot; _modernStatusText = backendText; backendText.Text = "Starting…";
        _hostStatusDot = hostDot; _hostStatusText = hostText; hostText.Text = "Not hosting";
        _friendsStatusDot = friendsDot; _friendsStatusText = friendsText; friendsText.Text = "0 nearby";
        right.Children.Add(new Border
        {
            Child = pill, Background = Hex("#8C05060D"), BorderBrush = Hex("#26FFFFFF"), BorderThickness = new Thickness(1),
            CornerRadius = new CornerRadius(14), Padding = new Thickness(14, 0, 14, 0), Height = 34, Margin = new Thickness(0, 0, 10, 0)
        });

        // Profile chip (the existing account button: sign in, switch or sign out).
        AccountButton.Height = 34; AccountButton.MinWidth = 120; AccountButton.Padding = new Thickness(14, 0, 14, 0); AccountButton.Margin = new Thickness(0, 0, 10, 0);
        AccountButton.FontFamily = NightBody; AccountButton.FontWeight = FontWeights.SemiBold; AccountButton.FontSize = 12.5;
        AccountButton.ToolTip = "Sign in, register or switch player";
        RoundButton(AccountButton, "#8C0B0E1F", Night.Text, 17, "#26FFFFFF");
        AccountButton.BorderThickness = new Thickness(1);
        right.Children.Add(AccountButton);

        // Window buttons: flat, 46 × 34; close turns red on hover.
        Button WindowButton(string glyph, string tip, Action click, string hover)
        {
            var b = new Button { Width = 46, Height = 34, Content = GlyphIcon(glyph, 10, Night.Text), ToolTip = tip, Padding = new Thickness(0) };
            RoundButton(b, "#00000000", Night.Text, 0);
            b.MouseEnter += (_, __) => b.Background = Hex(hover); b.MouseLeave += (_, __) => b.Background = Hex("#00000000");
            b.Click += (_, __) => click(); return b;
        }
        right.Children.Add(WindowButton("", "Minimize", () => WindowState = WindowState.Minimized, "#1A2338"));
        _maximizeButton = WindowButton("", "Maximize", ToggleMaximize, "#1A2338");
        right.Children.Add(_maximizeButton);
        right.Children.Add(WindowButton("", "Exit Syndicate", Close, "#C42B1C"));
        return bar;
    }

    private static T? FindAncestor<T>(DependencyObject? item) where T : DependencyObject
    {
        while (item != null && item is not T) item = item is Visual || item is System.Windows.Media.Media3D.Visual3D ? VisualTreeHelper.GetParent(item) : LogicalTreeHelper.GetParent(item);
        return item as T;
    }

    private Grid BuildHome()
    {
        var home = new Grid { Name = "ModernHome", Margin = new Thickness(RailWidth + 22, 60, 40, 18) };
        home.RowDefinitions.Add(new RowDefinition());
        home.RowDefinitions.Add(new RowDefinition { Height = GridLength.Auto });

        // Hero: season picker | title, text and PLAY | art card.
        var hero = new Grid(); home.Children.Add(hero);
        hero.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(1.25, GridUnitType.Star) });
        hero.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(1, GridUnitType.Star) });

        var text = new StackPanel { VerticalAlignment = VerticalAlignment.Center, Margin = new Thickness(10, 0, 30, 0) }; hero.Children.Add(text);
        text.Children.Add(BuildSeasonDropdown());
        _heroEyebrow = NightEyebrow(""); _heroEyebrow.Margin = new Thickness(3, 0, 0, 6); text.Children.Add(_heroEyebrow);
        _heroTitle = Display("SEASON 4", 88, Night.Text); _heroTitle.Margin = new Thickness(0, -6, 0, -10);
        _heroTitle.Effect = new DropShadowEffect { Color = Colors.Black, BlurRadius = 30, ShadowDepth = 0, Opacity = .55 };
        text.Children.Add(new Viewbox { Child = _heroTitle, StretchDirection = StretchDirection.DownOnly, HorizontalAlignment = HorizontalAlignment.Left });
        _heroTagline = new ContentControl { Margin = new Thickness(2, 0, 0, 16), Focusable = false }; text.Children.Add(_heroTagline);
        _heroDescription = Body("", 15, "#C9CFE6"); _heroDescription.MaxWidth = 540; _heroDescription.HorizontalAlignment = HorizontalAlignment.Left; _heroDescription.LineHeight = 23; _heroDescription.Margin = new Thickness(2, 0, 0, 28); text.Children.Add(_heroDescription);

        var actions = new StackPanel { Orientation = Orientation.Horizontal };
        _heroPlay = new Button { Width = 230, Height = 64, Content = "PLAY", ToolTip = "Start Fortnite for this season" };
        NightButton(_heroPlay, "gold", 12);
        _heroPlay.FontSize = 30;
        // Sinks 3 px when pressed, showing the darker base.
        var sink = new TranslateTransform();
        _heroPlay.PreviewMouseLeftButtonDown += (_, __) => { if (_heroPlay.IsEnabled) { sink.Y = 3; _heroPlay.BorderThickness = new Thickness(0); } };
        _heroPlay.PreviewMouseLeftButtonUp += (_, __) => { sink.Y = 0; _heroPlay.BorderThickness = new Thickness(0, 0, 0, 3); };
        _heroPlay.MouseLeave += (_, __) => { sink.Y = 0; _heroPlay.BorderThickness = new Thickness(0, 0, 0, 3); };
        if (_heroPlay.RenderTransform is ScaleTransform scale) _heroPlay.RenderTransform = new TransformGroup { Children = { scale, sink } };
        else _heroPlay.RenderTransform = sink;
        _heroPlay.Click += (_, __) =>
        {
            if (!_heroPlay.IsEnabled) return;
            if (_heroPlay.Content as string == "LINK FOLDER" && SeasonFolderButton(_heroSeason) is Button choose) choose.RaiseEvent(new RoutedEventArgs(ButtonBase.ClickEvent));
            else SeasonLaunchButton(_heroSeason).RaiseEvent(new RoutedEventArgs(ButtonBase.ClickEvent));
        };
        actions.Children.Add(_heroPlay);
        var folder = NightRoundIcon("", "Choose the game folder for this season", 52, 18); folder.Margin = new Thickness(14, 0, 0, 0);
        folder.Click += (_, __) => SeasonFolderButton(_heroSeason)?.RaiseEvent(new RoutedEventArgs(ButtonBase.ClickEvent));
        actions.Children.Add(folder);
        var tune = NightRoundIcon("", "Match & bot settings", 52, 18); tune.Margin = new Thickness(10, 0, 0, 0);
        tune.Click += (_, __) => SettingsButton_Click(this, new RoutedEventArgs());
        actions.Children.Add(tune);
        _heroBuild = NightText("v4.5", 13, Night.Text, bold: true); _heroBuild.VerticalAlignment = VerticalAlignment.Center; _heroBuild.TextWrapping = TextWrapping.NoWrap;
        var buildPill = FrostBox(_heroBuild, 26); buildPill.Height = 52; buildPill.Padding = new Thickness(18, 0, 18, 0); buildPill.Margin = new Thickness(10, 0, 0, 0); buildPill.ToolTip = "Fortnite build for this season";
        actions.Children.Add(buildPill);
        text.Children.Add(actions);
        DownloadPanel.Margin = new Thickness(0, 16, 0, 0); DownloadPanel.MaxWidth = 470; DownloadPanel.HorizontalAlignment = HorizontalAlignment.Left;
        text.Children.Add(DownloadPanel);

        // Art card: radius 18, black gradient at the bottom, status badge, lifts on hover.
        var card = new Grid();
        card.Children.Add(new Border { CornerRadius = new CornerRadius(18), Background = new LinearGradientBrush(Color.FromArgb(0, 0, 0, 0), Color.FromArgb(200, 0, 0, 0), 90) { StartPoint = new Point(0, .45), EndPoint = new Point(0, 1) } });
        var badgeRow = new StackPanel { Orientation = Orientation.Horizontal };
        _heroBadgeDot = NightDot(Night.Good, 7); _heroBadgeDot.Margin = new Thickness(0, 0, 6, 0); badgeRow.Children.Add(_heroBadgeDot);
        _heroBadgeText = NightText("READY", 10.5, Night.Text, bold: true); _heroBadgeText.TextWrapping = TextWrapping.NoWrap; badgeRow.Children.Add(_heroBadgeText);
        card.Children.Add(new Border { Child = badgeRow, Background = Hex("#99000000"), CornerRadius = new CornerRadius(12), Padding = new Thickness(10, 4, 10, 4), Margin = new Thickness(14), HorizontalAlignment = HorizontalAlignment.Right, VerticalAlignment = VerticalAlignment.Top });
        _heroArtBuild = NightText("", 13, Night.Text, bold: true); _heroArtBuild.Margin = new Thickness(18, 0, 18, 16); _heroArtBuild.VerticalAlignment = VerticalAlignment.Bottom; _heroArtBuild.TextWrapping = TextWrapping.NoWrap;
        card.Children.Add(_heroArtBuild);
        _heroArt = new Border
        {
            Height = 290, MaxWidth = 500, CornerRadius = new CornerRadius(18), VerticalAlignment = VerticalAlignment.Center, Child = card,
            BorderBrush = Hex(Night.FrostEdge), BorderThickness = new Thickness(1),
            Effect = new DropShadowEffect { BlurRadius = 24, ShadowDepth = 8, Opacity = .5, Color = Colors.Black }
        };
        var lift = new TranslateTransform(); _heroArt.RenderTransform = lift;
        _heroArt.MouseEnter += (_, __) => lift.BeginAnimation(TranslateTransform.YProperty, new DoubleAnimation(-5, TimeSpan.FromMilliseconds(160)) { EasingFunction = new CubicEase { EasingMode = EasingMode.EaseOut } });
        _heroArt.MouseLeave += (_, __) => lift.BeginAnimation(TranslateTransform.YProperty, new DoubleAnimation(0, TimeSpan.FromMilliseconds(200)) { EasingFunction = new CubicEase { EasingMode = EasingMode.EaseOut } });
        Grid.SetColumn(_heroArt, 1); hero.Children.Add(_heroArt);

        // Bottom: status line in a frosted chip.
        var footer = new Border { Background = Hex("#8C05060D"), BorderBrush = Hex("#26FFFFFF"), BorderThickness = new Thickness(1), CornerRadius = new CornerRadius(14), Padding = new Thickness(16, 10, 16, 10), Margin = new Thickness(0, 14, 0, 0) };
        Grid.SetRow(footer, 1); home.Children.Add(footer);
        StatusText.FontSize = 12.5; StatusText.Margin = new Thickness(0); StatusText.TextWrapping = TextWrapping.Wrap; StatusText.FontFamily = NightBody;
        footer.Child = StatusText;
        return home;
    }

    // Season picker: one frosted "CHAPTER 1 · SEASON 4 ▾" button. It opens a list grouped by chapter:
    // Chapter 1 with every season, and Chapter 2 marked "coming soon". (Before, eleven PRE/S1…SX tabs
    // stood in a column next to the title.)
    private System.Windows.Controls.Primitives.Popup? _seasonPopup;
    private TextBlock? _seasonDropdownText, _seasonDropdownChapter;
    private FrameworkElement BuildSeasonDropdown()
    {
        var label = new StackPanel { Orientation = Orientation.Horizontal };
        var chapter = _seasonDropdownChapter = NightText("CHAPTER 1", 12, Night.Gold, bold: true); chapter.TextWrapping = TextWrapping.NoWrap; chapter.VerticalAlignment = VerticalAlignment.Center;
        label.Children.Add(chapter);
        label.Children.Add(NightText("  ·  ", 12, Night.Muted).Also(t => { t.TextWrapping = TextWrapping.NoWrap; t.VerticalAlignment = VerticalAlignment.Center; }));
        _seasonDropdownText = Display("SEASON 4", 17, Night.Text); _seasonDropdownText.VerticalAlignment = VerticalAlignment.Center;
        label.Children.Add(_seasonDropdownText);
        label.Children.Add(GlyphIcon("", 11, Night.Sub).Also(g => g.Margin = new Thickness(12, 1, 0, 0)));
        var button = new Button { Content = label, Height = 40, Padding = new Thickness(16, 0, 16, 0), HorizontalAlignment = HorizontalAlignment.Left, Margin = new Thickness(0, 0, 0, 16), ToolTip = "Choose a season" };
        RoundButton(button, "#8C141831", Night.Text, 20, "#40FFFFFF");
        button.BorderThickness = new Thickness(1);

        var list = new StackPanel { Margin = new Thickness(6) };
        list.Children.Add(NightText("CHAPTER 1", 11, Night.Gold, bold: true).Also(t => t.Margin = new Thickness(12, 6, 12, 6)));
        _seasonPicker = new StackPanel();
        foreach (var season in ModernSeasons)
        {
            // Chapter 2 seasons get their own heading inside the list.
            if (season.Number == 11)
                _seasonPicker.Children.Add(NightText("CHAPTER 2", 11, Night.Gold, bold: true).Also(t => t.Margin = new Thickness(12, 12, 12, 6)));
            else if (season.Number == 19)
                _seasonPicker.Children.Add(NightText("CHAPTER 3 · EXPERIMENTAL", 11, Night.Gold, bold: true).Also(t => t.Margin = new Thickness(12, 12, 12, 6)));
            var entry = new Button { Tag = season.Number, Height = 36, Margin = new Thickness(0, 1, 0, 1), HorizontalContentAlignment = HorizontalAlignment.Left, ToolTip = "Fortnite " + season.Build };
            RoundButton(entry, "#00000000", Night.Text, 10);
            entry.HorizontalContentAlignment = HorizontalAlignment.Left;
            entry.Click += (_, __) => { if (_seasonPopup != null) _seasonPopup.IsOpen = false; SelectHeroSeason(season.Number); };
            _seasonPicker.Children.Add(entry);
        }
        list.Children.Add(_seasonPicker);
        list.Children.Add(new Border { Height = 1, Background = Hex(Night.Line), Margin = new Thickness(8, 8, 8, 8) });
        var next = new StackPanel { Orientation = Orientation.Horizontal, Margin = new Thickness(12, 4, 12, 8), ToolTip = "Chapter 2 isn't playable yet" };
        next.Children.Add(NightText("CHAPTER 2", 11, Night.Muted, bold: true).Also(t => { t.TextWrapping = TextWrapping.NoWrap; t.VerticalAlignment = VerticalAlignment.Center; }));
        next.Children.Add(new Border { Background = Hex("#26FFD23F"), CornerRadius = new CornerRadius(8), Padding = new Thickness(8, 2, 8, 2), Margin = new Thickness(10, 0, 0, 0), Child = NightText("COMING SOON", 10, Night.Gold, bold: true).Also(t => t.TextWrapping = TextWrapping.NoWrap) });
        if (!ModernSeasons.Any(s => s.Number >= 11)) list.Children.Add(next);

        _seasonPopup = new System.Windows.Controls.Primitives.Popup
        {
            PlacementTarget = button, Placement = System.Windows.Controls.Primitives.PlacementMode.Bottom, VerticalOffset = 6,
            StaysOpen = false, AllowsTransparency = true, PopupAnimation = System.Windows.Controls.Primitives.PopupAnimation.Fade,
            Child = new Border
            {
                Child = new ScrollViewer { Content = list, MaxHeight = 460, VerticalScrollBarVisibility = ScrollBarVisibility.Auto }, MinWidth = 260,
                Background = Hex(Night.Surface2), BorderBrush = Hex(Night.Line), BorderThickness = new Thickness(1), CornerRadius = new CornerRadius(14),
                Effect = new DropShadowEffect { BlurRadius = 24, ShadowDepth = 6, Opacity = .5, Color = Colors.Black }
            }
        };
        button.Click += (_, __) => _seasonPopup.IsOpen = !_seasonPopup.IsOpen;
        var host = new Grid { HorizontalAlignment = HorizontalAlignment.Left };
        host.Children.Add(button); host.Children.Add(_seasonPopup);
        return host;
    }

    // The side menu and HighlightRail live in SyndicateHome.cs.

    // The backdrop can show a store battle-pass preview without changing the Home/Play season.
    private void ApplyBackdropSeason(int number)
    {
        if (_heroBackdrop != null) _heroBackdrop.Background = SeasonBrush(number);
        if (_heroGlow != null) _heroGlow.Background = new RadialGradientBrush(NightColor(SeasonAccent(number)), Color.FromArgb(0, 0, 0, 0)) { Center = new Point(.22, .55), GradientOrigin = new Point(.22, .55), RadiusX = .75, RadiusY = .9 };
    }

    private void ApplyBlankBackdrop()
    {
        if (_heroBackdrop != null) _heroBackdrop.Background = null;
        if (_heroGlow != null) _heroGlow.Background = null;
    }

    private void RefreshBackdrop()
    {
        // Store battle-pass cards preview their own season. Packs, Hosting, Stats and Online have no keyart.
        if (PassShop.Visibility == Visibility.Visible)
        {
            if (_showPacks) { ApplyBlankBackdrop(); return; }
            ApplyBackdropSeason(_storePreviewSeason ?? _heroSeason);
            return;
        }
        if (_railIndex is 3 or 4 or 7) { ApplyBlankBackdrop(); return; }
        ApplyBackdropSeason(_heroSeason);
    }

    internal void PreviewStoreSeason(int? season)
    {
        _storePreviewSeason = season;
        RefreshBackdrop();
    }

    private void SelectHeroSeason(int number, bool animate = true)
    {
        var season = ModernSeasons.FirstOrDefault(s => s.Number == number) ?? ModernSeasons[2];
        _heroSeason = season.Number;
        string accent = SeasonAccent(season.Number);
        bool linked = SeasonLinked(season);
        int chapter = ChapterOf(season.Number);
        string seasonText = SeasonTitle(season.Number);
        try { Directory.CreateDirectory(Path.GetDirectoryName(HeroSeasonPath)!); File.WriteAllText(HeroSeasonPath, season.Number.ToString()); } catch { }
        RefreshBackdrop();
        var art = SeasonBrush(season.Number);
        if (_heroArt != null) _heroArt.Background = art;
        // The side menu's season card is visible from every page: refresh it the moment the
        // season browser commits a selection, not only when Home is opened.
        if (_railSeasonArt != null) _railSeasonArt.Background = art;
        if (_railSeasonTitle != null) _railSeasonTitle.Text = (chapter > 1 ? "C" + chapter + " " : "") + seasonText;
        if (_railSeasonBuild != null) _railSeasonBuild.Text = "Fortnite v" + season.Build;
        if (_heroTitle != null) _heroTitle.Text = seasonText;
        if (_heroTagline != null) _heroTagline.Content = new Viewbox { Child = OutlineText(season.Tagline, 52), StretchDirection = StretchDirection.DownOnly, HorizontalAlignment = HorizontalAlignment.Left };
        if (_heroEyebrow != null) { _heroEyebrow.Text = ("Chapter " + ChapterOf(season.Number) + "  ·  v" + season.Build + "  ·  " + (linked ? "Ready" : "Folder not set")).ToUpperInvariant(); }
        if (_heroDescription != null) _heroDescription.Text = season.Description;
        if (_heroBuild != null) _heroBuild.Text = "v" + season.Build;
        if (_heroArtBuild != null)
        {
            _heroArtBuild.Inlines.Clear();
            _heroArtBuild.Inlines.Add(new System.Windows.Documents.Run("v" + season.Build) { Foreground = Hex(accent) });
            _heroArtBuild.Inlines.Add(new System.Windows.Documents.Run("  ·  " + System.Globalization.CultureInfo.CurrentCulture.TextInfo.ToTitleCase(season.Tagline.ToLowerInvariant())) { Foreground = Hex("#E6EAF8") });
        }
        if (_heroBadgeDot != null) _heroBadgeDot.Fill = Hex(linked ? Night.Good : Night.Muted);
        if (_heroBadgeText != null) _heroBadgeText.Text = linked ? "READY" : "NOT LINKED";
        if (_seasonPicker != null)
            foreach (var entry in _seasonPicker.Children.OfType<Button>())
            {
                int entrySeason = (int)entry.Tag;
                bool active = entrySeason == season.Number;
                var info = ModernSeasons.First(s => s.Number == entrySeason);
                var row = new Grid { Margin = new Thickness(10, 0, 10, 0) };
                row.ColumnDefinitions.Add(new ColumnDefinition { Width = GridLength.Auto });
                row.ColumnDefinitions.Add(new ColumnDefinition());
                row.ColumnDefinitions.Add(new ColumnDefinition { Width = GridLength.Auto });
                row.Children.Add(new Border { Width = 3, Height = active ? 18 : 10, CornerRadius = new CornerRadius(1.5), Background = Hex(active ? SeasonAccent(entrySeason) : "#3A4366"), VerticalAlignment = VerticalAlignment.Center, Margin = new Thickness(0, 0, 10, 0) });
                var label = Display(SeasonTitle(entrySeason), 16, active ? Night.Text : Night.Sub); label.VerticalAlignment = VerticalAlignment.Center; Grid.SetColumn(label, 1);
                row.Children.Add(label);
                var build = NightText(SeasonLinked(info) ? "v" + info.Build : "v" + info.Build + " · not linked", 11, SeasonLinked(info) ? Night.Sub : Night.Muted);
                build.TextWrapping = TextWrapping.NoWrap; build.VerticalAlignment = VerticalAlignment.Center; build.Margin = new Thickness(16, 0, 0, 0); Grid.SetColumn(build, 2);
                row.Children.Add(build);
                entry.Content = row;
                entry.Background = active ? Hex("#26FFFFFF") : Hex("#00000000");
            }
        if (_seasonDropdownText != null) _seasonDropdownText.Text = SeasonTitle(season.Number);
        if (_seasonDropdownChapter != null) _seasonDropdownChapter.Text = "CHAPTER " + ChapterOf(season.Number);
        SyncHeroPlay();
        SyncBrowserToHero();
        if (animate && MotionEnabled)
        {
            var fade = new DoubleAnimation(.15, 1, TimeSpan.FromMilliseconds(320)) { EasingFunction = new CubicEase { EasingMode = EasingMode.EaseOut } };
            _heroBackdrop?.BeginAnimation(OpacityProperty, fade);
            _heroArt?.BeginAnimation(OpacityProperty, fade);
            _heroTitle?.BeginAnimation(OpacityProperty, fade);
        }
    }

    private void SyncHeroPlay()
    {
        if (_heroPlay == null) return;
        var source = SeasonLaunchButton(_heroSeason);
        _heroPlay.Content = (source.Content?.ToString() ?? "Launch") switch
        {
            "Quit" => "QUIT", "Cancel" => "CANCEL", "Choose folder" => "LINK FOLDER", _ => "PLAY"
        };
        // No game folder chosen for this season yet: say so on the button, and let it open the folder picker.
        var heroInfo = ModernSeasons.FirstOrDefault(s => s.Number == _heroSeason);
        bool needsFolder = heroInfo != null && !SeasonLinked(heroInfo) && _heroPlay.Content as string == "PLAY";
        if (needsFolder) _heroPlay.Content = "LINK FOLDER";
        _heroPlay.ToolTip = _heroPlay.Content as string == "LINK FOLDER" ? "Choose where this season's Fortnite build is on your PC" : "Start Fortnite for this season";
        _heroPlay.IsEnabled = needsFolder || source.IsEnabled;
        bool quit = _heroPlay.Content as string == "QUIT";
        _heroPlay.Background = quit ? Hex(Night.Danger) : GoldFill();
        _heroPlay.Foreground = Hex(quit ? "#FFFFFF" : Night.OnGold);
        _heroPlay.BorderBrush = Hex(quit ? "#B23A47" : "#B07A00");
        if (_heroPlay.Effect is DropShadowEffect glow) glow.Color = NightColor(quit ? Night.Danger : "#FFC21A");
    }

    private void ToggleMaximize()
    {
        if (WindowState == WindowState.Maximized)
        {
            WindowState = WindowState.Normal;
            // WPF sometimes leaves the window at the maximized width when the custom chrome restores it.
            // Put the exact pre-maximize bounds back so exiting full screen always returns to the old size.
            if (_restoreBounds.Width >= 200 && _restoreBounds.Height >= 200)
            {
                Left = _restoreBounds.Left;
                Top = _restoreBounds.Top;
                Width = _restoreBounds.Width;
                Height = _restoreBounds.Height;
            }
        }
        else
        {
            _restoreBounds = new Rect(Left, Top, ActualWidth, ActualHeight);
            WindowState = WindowState.Maximized;
        }
    }

    private void SyncModernStatus(bool up)
    {
        if (_modernStatusDot != null) _modernStatusDot.Fill = Hex(up ? Night.Good : Night.Danger);
        if (_modernStatusText != null) _modernStatusText.Text = up ? "Online · local" : "Stopped";
    }
}

internal static class NightExtensions
{
    // Small helper so a control can be configured inline: FrostBox(...).Also(b => ...).
    public static T Also<T>(this T item, Action<T> setup) { setup(item); return item; }
}