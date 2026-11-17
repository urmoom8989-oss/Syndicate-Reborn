using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Net.Http;
using System.Text;
using System.Text.Json;
using System.Threading;
using System.Threading.Tasks;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Controls.Primitives;
using System.Windows.Input;
using System.Windows.Media;
using System.Windows.Media.Imaging;
using System.Windows.Threading;
using Ellipse = System.Windows.Shapes.Ellipse;

namespace SyndicateLauncher.Studio
{
    // Syndicate Studio (Dev launcher only, the Studio button under Play): an editor-style control room for the local
    // match server, laid out like an engine editor. Ported from Tilted Studio (SyndicateClassic). In Syndicate the
    // match server and your game always start together, so Start here is the launcher's Play for the selected
    // Chapter 1 season; while this window is open that Play starts a Studio match (StudioSession.cs). Top: Settings | Server. Bottom: Content | Output. Right: Outliner over Details.
    // - Settings / Server: playlist and Studio match options; Start / Stop / Restart through the launcher's Play,
    //   live state from server.json, ready.pid, occupancy.json, net-live.json.
    // - Outliner / Details: everyone in the match (dev-live.json) and every piece the editor placed
    //   (mapeditor-live.json); teleport to / kill players and bots, move / delete placed pieces.
    // - Content: the placeable classes the match server exported (map-assets.json); double-click drops one in front
    //   of you through the backend's map editor API.
    // - Output: the match server's log, live.
    // Panels move between the five docks, float or close; layout, theme and accent are remembered.
    internal sealed partial class StudioWindow : Window
    {
        readonly Func<LauncherPaths?> _getPaths;
        readonly Func<string> _me;
        readonly Action<string> _log;
        readonly Func<string, Action<string>, Task<bool>> _launch; // season key -> the launcher's Play; true once Fortnite is in the lobby
        // The Chapter 1 seasons the Studio can start (key, label, Fortnite build), picked next to Start.
        readonly IReadOnlyList<(string Key, string Label, string Build)> _seasons;
        string _season = "";
        readonly Action _play;
        static readonly HttpClient Http = new() { Timeout = TimeSpan.FromSeconds(6) };

        // ---- panels / layout ---------------------------------------------------------------------------
        sealed record PanelDef(string Id, string Title, string Glyph, Func<UIElement> Build);
        sealed class Layout
        {
            public Dictionary<string, string> Groups { get; set; } = new(); // panel id -> group id | float | closed
            public Dictionary<string, string> Active { get; set; } = new(); // group id -> active panel id
            public double MainRatio { get; set; } = 0.8;
            public double TopRatio { get; set; } = 0.66;
            public double TopLeftRatio { get; set; } = 0.22;
            public double RightRatio { get; set; } = 0.48;
            public string Theme { get; set; } = "Syndicate";
            public string Accent { get; set; } = "Launcher";
            public bool AutoEmbed { get; set; } = true;   // dock your game in the Server viewport as soon as it renders
            public bool AutoJoin { get; set; } = true;    // kept for saved layouts; Start always launches your game in Syndicate
            public int FlySpeed { get; set; } = 8000;     // cm/s, dev-fly-speed.txt (match server and SyndicateClient read it)
            public bool StudioMatch { get; set; } = true; // no bots, never leaves the warm-up island (no bus, no storm)
            public bool Fly { get; set; } = true;         // F8 fly while docked: through walls and the ground
            public string Season { get; set; } = "";      // the season key picked next to Start
            public List<string> Favorites { get; set; } = new(); // Content Browser favourites (placeable class names), starred by the user
            public int Grid { get; set; } = 512;          // editing grid (toolbar): 0 off, 64-512; 512 = one build tile
        }
        // Docks: tl / tr (top row), bottom, rt / rb (right column).
        static readonly string[] GroupIds = { "tl", "tr", "bottom", "rt", "rb" };
        static Dictionary<string, string> Preset(string name) => name switch
        {
            "Play" => new() { ["settings"] = "closed", ["server"] = "tl", ["sandbox"] = "tl", ["viewport"] = "tr", ["content"] = "closed", ["output"] = "bottom", ["outliner"] = "rt", ["details"] = "rb" },
            "Diagnostics" => new() { ["settings"] = "tl", ["server"] = "tl", ["viewport"] = "tr", ["content"] = "closed", ["output"] = "bottom", ["outliner"] = "rt", ["details"] = "rb" },
            _ /* Editor */ => new() { ["settings"] = "tl", ["server"] = "tl", ["controls"] = "tl", ["sandbox"] = "tl", ["viewport"] = "tr", ["content"] = "bottom", ["output"] = "bottom", ["outliner"] = "rt", ["details"] = "rb" },
        };
        static string LayoutFile => Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "ProjectTilted", "syndicate-studio-layout.json");

        readonly List<PanelDef> _panels;
        readonly Dictionary<string, UIElement> _built = new();
        readonly Dictionary<string, Window> _floating = new();
        readonly List<Border> _hosts = new();
        Layout _layout;
        Grid _root = null!;
        bool _closing;

        // ---- server state ------------------------------------------------------------------------------
        sealed record HostState(int Pid, long Started, string Mode, string Build, string Skill);
        CancellationTokenSource? _startCancel;
        bool _starting, _stopping;
        string _phase = "";
        string? _build;
        readonly DispatcherTimer _timer;
        HostState? _host;

        // ---- Studio pipe (SyndicateStudio.dll) ----------------------------------------------------------
        StudioPipeClient? _pipe;
        bool _pipeInjecting;

        // ---- Gizmo overlay (translation arrows over the viewport) --------------------------------------
        StudioGizmoOverlay? _gizmo;
        bool _gizmoOn = true;
        Button? _gizmoBtn;

        // ---- Map save/load (Phase 2) ------------------------------------------------------------------
        StudioMapSession _mapSession;
        TextBlock? _mapLabel;

        // ---- live match data ---------------------------------------------------------------------------
        sealed record Item(string Key, string Kind, string Name, string Type, Dictionary<string, string> Props);
        readonly Dictionary<string, Item> _items = new();
        string? _selected;
        string _outlinerSig = "", _outlinerFilter = "";

        // ---- output log --------------------------------------------------------------------------------
        const int MaxLines = 4000;
        readonly List<string> _lines = new();
        string _filter = "", _logSource = "server";
        bool _errorsOnly, _follow = true;
        string? _tailFile; long _tailPos; string? _tailRest = ""; // null: started mid-file, drop the partial first line

        // ---- widgets -----------------------------------------------------------------------------------
        TextBox? _outBox;
        TextBlock? _stateText, _detailText, _buildText, _statusLeft, _statusRight, _contentInfo;
        Ellipse? _stateDot, _statusDot;
        Button? _startBtn, _stopBtn, _restartBtn;
        ListBox? _outliner;
        StackPanel? _details;
        WrapPanel? _tiles;
        ListBox? _categories;
        string _contentFilter = "", _contentCategory = "";
        string? _contentPick;
        readonly List<string> _recent = new();                 // recently placed classes, most recent first (this session)
        const string FavCat = "★ Favorites", RecentCat = "Recent"; // virtual Content Browser categories
        void ToggleFavorite(string cls)
        {
            int i = _layout.Favorites.FindIndex(x => x.Equals(cls, StringComparison.OrdinalIgnoreCase));
            if (i >= 0) _layout.Favorites.RemoveAt(i); else _layout.Favorites.Insert(0, cls);
            SaveLayout(); RefreshContent(categoriesToo: true);
        }
        void RecordRecent(string cls)
        {
            _recent.RemoveAll(x => x.Equals(cls, StringComparison.OrdinalIgnoreCase));
            _recent.Insert(0, cls);
            if (_recent.Count > 40) _recent.RemoveRange(40, _recent.Count - 40);
        }
        Dictionary<string, List<string>> _assets = new();
        string _assetsSig = "";

        public StudioWindow(Func<LauncherPaths?> getPaths, Func<string> me, Action<string> log,
            IReadOnlyList<(string Key, string Label, string Build)> seasons, string? currentSeason, Func<string, Action<string>, Task<bool>> launch)
        {
            _getPaths = getPaths; _me = me; _log = log; _launch = launch; _seasons = seasons;
            // Play: in Syndicate the server and your game start together, so Play is Start unless the game already runs.
            _play = () => { if (StudioSession.ClientRunning()) Out("Fortnite is already running: it docks in the Server viewport once it renders."); else StartServer(); };
            Title = "Syndicate Studio"; Width = 1600; Height = 940; MinWidth = 1000; MinHeight = 600;
            WindowStartupLocation = WindowStartupLocation.CenterScreen;
            FontFamily = new FontFamily("Segoe UI Variable Text, Segoe UI"); FontSize = 12.5; // the launcher's body font
            try { Icon = BitmapFrame.Create(new Uri("pack://application:,,,/Assets/syndicate.ico")); }
            catch { }
            _panels = new()
            {
                new PanelDef("settings", "Session Settings", "", BuildSettingsPanel),
                new PanelDef("server", "Server Info", "", BuildServerPanel),
                new PanelDef("controls", "Controls", "\uE765", BuildControlsPanel),
                new PanelDef("sandbox", "Sandbox", "", BuildSandboxPanel),
                new PanelDef("viewport", "Server", "", BuildViewportPanel),
                new PanelDef("content", "Content Browser", "", BuildContentPanel),
                new PanelDef("output", "Output Log", "", BuildOutputPanel),
                new PanelDef("outliner", "Outliner", "", BuildOutlinerPanel),
                new PanelDef("details", "Details", "", BuildDetailsPanel),
            };
            _layout = LoadLayout();
            // Season: the one picked here last time, else the launcher's selected season if it is a Chapter 1 one.
            _season = seasons.Any(s => s.Key == _layout.Season) ? _layout.Season
                : seasons.Any(s => s.Key == currentSeason) ? currentSeason! : seasons.Count > 0 ? seasons[^1].Key : "";
            _build = SeasonBuild();
            Rebuild();
            _timer = new DispatcherTimer { Interval = TimeSpan.FromMilliseconds(700) };
            _timer.Tick += (_, __) => { Poll(); TailOutput(); ViewTick(); GizmoTick(); KeepPlacingAlive(); StudioSessionTick(); };
            _timer.Start();
            InitEditing();
            SourceInitialized += (_, __) => _self = new System.Windows.Interop.WindowInteropHelper(this).Handle;
            LocationChanged += (_, __) => PlaceView();
            SizeChanged += (_, __) => PlaceView();
            StateChanged += (_, __) => Dispatcher.BeginInvoke(new Action(PlaceView));
            Activated += (_, __) => PlaceView();
            Closed += (_, __) => { _closing = true; _timer.Stop(); _gizmo?.Hide(); _gizmo = null; try { _pipe?.Dispose(); _pipe = null; } catch { } StudioSession.Clear();
                // Closing the Studio ends the dev session it started: stop the server it launched so the match
                // doesn't orphan. Skipped when other players are connected, so hosting for friends isn't cut off.
                try { var paths = _getPaths(); if (paths != null) { var host = RunningHost(paths); if (host != null) { var occ = Occupancy(paths.ServerDir, host.Pid); if (!(occ != null && occ.Value.Connected > 0 && !occ.Value.Ended)) StudioSession.EndMatchNow(); } } } catch { }
                foreach (var w in _floating.Values.ToList()) w.Close(); ReleaseView(); if (_placingOn) PushPlacing(false); SaveLayout(); };
            _mapSession = new StudioMapSession(s => UI(() => Out(s)));
            _mapSession.BaseSeason = _build;
            try { File.Delete(DevFlag("free-cam-on.txt")); } catch { }
            WriteFlySpeed();
            Out("Syndicate Studio open. Pick a season next to Start and press Start. Settings apply the next time the server starts. The Syndicate menu (SyndicateMenu.dll) stays off while the Studio is open.");
            PopulateLocations();
            Poll();
        }

        // ---- theme -------------------------------------------------------------------------------------
        static SolidColorBrush B(string hex) { var b = new SolidColorBrush((Color)ColorConverter.ConvertFromString(hex)); b.Freeze(); return b; }
        // Themes: "Syndicate" (default) uses the launcher's near-black greys, "Graphite" the lighter editor greys, "Slate" blue.
        bool Slate => _layout.Theme == "Slate";
        bool Graphite => _layout.Theme == "Graphite";
        string T(string syndicate, string graphite, string slate) => Slate ? slate : Graphite ? graphite : syndicate;
        SolidColorBrush Back => B(T("#080808", "#0F0F0F", "#0B1019"));     // gaps between docks
        SolidColorBrush Panel => B(T("#141414", "#242424", "#161E2B"));    // panel body
        SolidColorBrush TabBar => B(T("#0C0C0C", "#151515", "#0F1520"));   // tab strip
        SolidColorBrush TabOn => B(T("#1C1C1C", "#2F2F2F", "#1E2838"));    // active tab
        SolidColorBrush Field => B(T("#0E0E0E", "#171717", "#0E141E"));    // inputs, lists
        SolidColorBrush Raised => B(T("#222222", "#383838", "#243045"));   // buttons
        SolidColorBrush Hover => B(T("#2C2C2C", "#454545", "#2C3A52"));
        SolidColorBrush Line => B(T("#262626", "#3A3A3A", "#2A3650"));
        // Text on an accent fill: dark on light accents (the launcher's default white), white on dark ones.
        SolidColorBrush OnAccent { get { var c = (Color)ColorConverter.ConvertFromString(AccentHex); return B(0.299 * c.R + 0.587 * c.G + 0.114 * c.B > 150 ? "#111111" : "#FFFFFF"); } }
        static readonly FontFamily Display = new("Bahnschrift SemiCondensed, Bahnschrift, Segoe UI");
        static SolidColorBrush Fg => B("#D9D9D9");
        static SolidColorBrush Bright => B("#FFFFFF");
        static SolidColorBrush Muted => B("#8C8C8C");
        static SolidColorBrush Good => B("#3BE48B");
        static SolidColorBrush Bad => B("#FF5A6A");
        static SolidColorBrush Warn => B("#FFB020");
        // "Launcher" follows the accent of the player's launcher theme (Settings > Themes).
        static readonly (string Name, string Hex)[] Accents = { ("Syndicate", "#FFD23F"), ("Launcher", ""), ("Blue", "#2F8FFF"), ("Amber", "#FF9A1F"), ("Teal", "#2DD4BF"), ("Violet", "#8B6CFF") };
        static string LauncherAccentHex()
        {
            var c = Application.Current?.TryFindResource("AccentBrush") is SolidColorBrush b ? b.Color : Color.FromRgb(0xFF, 0xD2, 0x3F);
            return $"#{c.R:X2}{c.G:X2}{c.B:X2}";
        }
        string AccentHex => _layout.Accent == "Launcher" ? LauncherAccentHex() : Accents.FirstOrDefault(a => a.Name == _layout.Accent).Hex ?? "#FFD23F";
        SolidColorBrush Accent => B(AccentHex);
        SolidColorBrush AccentDim => B("#55" + AccentHex[1..]);
        static readonly FontFamily Mono = new("Cascadia Mono, Consolas");
        static readonly FontFamily Icons = new("Segoe Fluent Icons, Segoe MDL2 Assets");

        // Flat control template: WPF's default button/list item chrome is light grey, which glares on dark panels.
        ControlTemplate FlatTemplate(Type type, bool selectable = false)
        {
            var border = new FrameworkElementFactory(typeof(Border)) { Name = "bd" };
            border.SetValue(Border.CornerRadiusProperty, new CornerRadius(selectable ? 0 : 3));
            border.SetBinding(Border.BackgroundProperty, new System.Windows.Data.Binding("Background") { RelativeSource = System.Windows.Data.RelativeSource.TemplatedParent });
            border.SetBinding(Border.BorderBrushProperty, new System.Windows.Data.Binding("BorderBrush") { RelativeSource = System.Windows.Data.RelativeSource.TemplatedParent });
            border.SetBinding(Border.BorderThicknessProperty, new System.Windows.Data.Binding("BorderThickness") { RelativeSource = System.Windows.Data.RelativeSource.TemplatedParent });
            border.SetBinding(Border.PaddingProperty, new System.Windows.Data.Binding("Padding") { RelativeSource = System.Windows.Data.RelativeSource.TemplatedParent });
            var content = new FrameworkElementFactory(typeof(ContentPresenter));
            content.SetValue(FrameworkElement.HorizontalAlignmentProperty, selectable ? HorizontalAlignment.Stretch : HorizontalAlignment.Center);
            content.SetValue(FrameworkElement.VerticalAlignmentProperty, VerticalAlignment.Center);
            border.AppendChild(content);
            var t = new ControlTemplate(type) { VisualTree = border };
            var hover = new Trigger { Property = UIElement.IsMouseOverProperty, Value = true };
            hover.Setters.Add(new Setter(Border.BackgroundProperty, Hover, "bd"));
            t.Triggers.Add(hover);
            if (selectable)
            {
                var sel = new Trigger { Property = ListBoxItem.IsSelectedProperty, Value = true };
                sel.Setters.Add(new Setter(Border.BackgroundProperty, AccentDim, "bd"));
                t.Triggers.Add(sel);
            }
            var off = new Trigger { Property = UIElement.IsEnabledProperty, Value = false };
            off.Setters.Add(new Setter(UIElement.OpacityProperty, 0.4));
            t.Triggers.Add(off);
            return t;
        }
        Style ItemStyle()
        {
            var s = new Style(typeof(ListBoxItem));
            s.Setters.Add(new Setter(Control.TemplateProperty, FlatTemplate(typeof(ListBoxItem), selectable: true)));
            s.Setters.Add(new Setter(Control.BackgroundProperty, Brushes.Transparent));
            s.Setters.Add(new Setter(Control.ForegroundProperty, Fg));
            s.Setters.Add(new Setter(Control.PaddingProperty, new Thickness(6, 2, 6, 2)));
            s.Setters.Add(new Setter(Control.HorizontalContentAlignmentProperty, HorizontalAlignment.Stretch));
            return s;
        }

        Button MakeButton(string text, Action click, bool primary = false, string? tip = null)
        {
            var b = new Button
            {
                Content = text, Height = 28, Margin = new Thickness(0, 0, 6, 6), Padding = new Thickness(12, 0, 12, 0),
                Background = primary ? Accent : Raised, Foreground = primary ? OnAccent : Fg, BorderBrush = Line, BorderThickness = new Thickness(1),
                FontWeight = primary ? FontWeights.SemiBold : FontWeights.Normal, Template = FlatTemplate(typeof(Button)), ToolTip = tip, Cursor = Cursors.Hand
            };
            b.Click += (_, __) => click(); return b;
        }
        // Editor-style toolbar button: icon above a short label.
        Button ToolButton(string glyph, string label, Action click, string tip, Brush? iconBrush = null)
        {
            var stack = new StackPanel();
            stack.Children.Add(new TextBlock { Text = glyph, FontFamily = Icons, FontSize = 18, Foreground = iconBrush ?? Fg, HorizontalAlignment = HorizontalAlignment.Center });
            stack.Children.Add(new TextBlock { Text = label, FontSize = 11, Foreground = Fg, HorizontalAlignment = HorizontalAlignment.Center, Margin = new Thickness(0, 3, 0, 0) });
            var b = new Button
            {
                Content = stack, Width = 70, Height = 52, Margin = new Thickness(0, 0, 4, 0), Background = Brushes.Transparent, BorderBrush = Brushes.Transparent,
                BorderThickness = new Thickness(0), Template = FlatTemplate(typeof(Button)), ToolTip = tip, Cursor = Cursors.Hand
            };
            b.Click += (_, __) => click(); return b;
        }
        TextBlock Section(string t) => new() { Text = t.ToUpperInvariant(), Foreground = Muted, FontWeight = FontWeights.SemiBold, FontSize = 10.5, Margin = new Thickness(0, 14, 0, 6) };
        TextBlock Note(string t) => new() { Text = t, Foreground = Muted, FontSize = 11, TextWrapping = TextWrapping.Wrap, Margin = new Thickness(0, 4, 0, 0) };
        TextBox Input(string text = "", double width = double.NaN) => new()
        {
            Text = text, Width = width, Height = 26, VerticalContentAlignment = VerticalAlignment.Center, Background = Field, Foreground = Fg, BorderBrush = Line,
            CaretBrush = Fg, Padding = new Thickness(4, 0, 4, 0), SelectionBrush = Accent
        };
        ScrollViewer Scroll(UIElement content) => new() { Content = content, VerticalScrollBarVisibility = ScrollBarVisibility.Auto, HorizontalScrollBarVisibility = ScrollBarVisibility.Disabled };

        // A dark drop-down: a flat button showing the value, opening a menu of choices.
        Button Picker(IReadOnlyList<(string Value, string Label)> choices, string current, Action<string> picked)
        {
            var label = new TextBlock { Text = choices.FirstOrDefault(c => c.Value == current).Label ?? current, Foreground = Fg, VerticalAlignment = VerticalAlignment.Center, TextTrimming = TextTrimming.CharacterEllipsis };
            var row = new DockPanel();
            var arrow = new TextBlock { Text = "", FontFamily = Icons, FontSize = 10, Foreground = Muted, VerticalAlignment = VerticalAlignment.Center, Margin = new Thickness(8, 0, 0, 0) };
            DockPanel.SetDock(arrow, Dock.Right); row.Children.Add(arrow); row.Children.Add(label);
            var b = new Button
            {
                Content = row, Height = 28, Background = Field, BorderBrush = Line, BorderThickness = new Thickness(1), Padding = new Thickness(8, 0, 8, 0),
                HorizontalContentAlignment = HorizontalAlignment.Stretch, Template = FlatTemplate(typeof(Button), selectable: true), Cursor = Cursors.Hand
            };
            b.Click += (_, __) =>
            {
                var menu = new ContextMenu { PlacementTarget = b, Placement = PlacementMode.Bottom, MinWidth = b.ActualWidth };
                foreach (var (value, text) in choices)
                {
                    var item = new MenuItem { Header = text, IsCheckable = true, IsChecked = value == current };
                    item.Click += (_, __) => { current = value; label.Text = text; picked(value); };
                    menu.Items.Add(item);
                }
                menu.IsOpen = true;
            };
            return b;
        }

        // ---- chrome ------------------------------------------------------------------------------------
        void Rebuild()
        {
            foreach (var h in _hosts) h.Child = null;
            foreach (var w in _floating.Values) w.Content = null;
            _built.Clear();
            Background = Back;
            var dock = new DockPanel();
            var menu = BuildMenu(); DockPanel.SetDock(menu, Dock.Top); dock.Children.Add(menu);
            var bar = BuildToolbar(); DockPanel.SetDock(bar, Dock.Top); dock.Children.Add(bar);
            var status = BuildStatusBar(); DockPanel.SetDock(status, Dock.Bottom); dock.Children.Add(status);
            _root = new Grid { Margin = new Thickness(2) };
            dock.Children.Add(_root);
            Content = dock;
            foreach (var w in _floating) w.Value.Content = Built(w.Key);
            Arrange();
        }

        Menu BuildMenu()
        {
            var menu = new Menu { Background = TabBar, Foreground = Fg, Padding = new Thickness(6, 3, 6, 3) };
            MenuItem Item(string header, Action click, bool check = false, bool isChecked = false)
            {
                var m = new MenuItem { Header = header, IsCheckable = check, IsChecked = isChecked, Foreground = B("#111111") };
                m.Click += (_, __) => click(); return m;
            }
            MenuItem Top(string header) => new() { Header = header, Foreground = Fg };
            var server = Top("_Server");
            server.Items.Add(Item("Start", StartServer));
            server.Items.Add(Item("Stop", StopServer));
            server.Items.Add(Item("Restart", RestartServer));
            server.Items.Add(new Separator());
            server.Items.Add(Item("Play (launch Fortnite into it)", _play));
            server.Items.Add(Item("Open server folder", () => OpenFolder(RuntimeDir())));
            menu.Items.Add(server);

            var window = Top("_Window");
            foreach (var p in _panels)
            {
                var id = p.Id;
                window.Items.Add(Item(p.Title, () => { _layout.Groups[id] = Group(id) == "closed" ? DefaultGroup(id) : "closed"; Arrange(); }, true, Group(id) != "closed"));
            }
            window.Items.Add(new Separator());
            var presets = new MenuItem { Header = "Workspace presets", Foreground = B("#111111") };
            foreach (var name in new[] { "Editor", "Play", "Diagnostics" })
                presets.Items.Add(Item(name, () => ApplyPreset(name)));
            window.Items.Add(presets);
            window.Items.Add(Item("Reset layout", () => { var keep = (_layout.Theme, _layout.Accent); _layout = new Layout { Theme = keep.Theme, Accent = keep.Accent }; ApplyPreset("Editor"); }));
            menu.Items.Add(window);

            var view = Top("_Appearance");
            foreach (var theme in new[] { "Syndicate", "Graphite", "Slate" })
                view.Items.Add(Item("Theme: " + theme, () => { _layout.Theme = theme; SaveLayout(); Rebuild(); }, true, _layout.Theme == theme));
            view.Items.Add(new Separator());
            foreach (var (name, _) in Accents)
                view.Items.Add(Item("Accent: " + name, () => { _layout.Accent = name; SaveLayout(); Rebuild(); }, true, _layout.Accent == name));
            menu.Items.Add(view);

            return menu;
        }

        void ApplyPreset(string name)
        {
            foreach (var w in _floating.Values.ToList()) w.Close();
            _layout.Groups = Preset(name);
            Arrange();
        }

        UIElement BuildToolbar()
        {
            var bar = new Border { Background = Panel, BorderBrush = Back, BorderThickness = new Thickness(0, 0, 0, 2), Padding = new Thickness(10, 4, 10, 4) };
            var row = new DockPanel { LastChildFill = false };
            var brand = new StackPanel { Orientation = Orientation.Horizontal, VerticalAlignment = VerticalAlignment.Center, Margin = new Thickness(0, 0, 18, 0) };
            var logo = LoadLogo();
            if (logo != null) brand.Children.Add(new Image { Source = logo, Height = 38, Margin = new Thickness(0, 0, 10, 0), VerticalAlignment = VerticalAlignment.Center });
            var names = new StackPanel { VerticalAlignment = VerticalAlignment.Center };
            names.Children.Add(new TextBlock { Text = "SYNDICATE STUDIO", Foreground = Bright, FontFamily = Display, FontWeight = FontWeights.Bold, FontStyle = FontStyles.Italic, FontSize = 19 });
            _buildText = new TextBlock { Text = BuildLine(), Foreground = Muted, FontSize = 11 };
            names.Children.Add(_buildText);
            brand.Children.Add(names);
            row.Children.Add(brand);
            row.Children.Add(new Border { Width = 1, Background = Line, Margin = new Thickness(0, 6, 12, 6) });
            // Season, then Start: Start selects this season in the launcher (asking for its build folder the first time).
            var seasonBox = new StackPanel { VerticalAlignment = VerticalAlignment.Center, Margin = new Thickness(0, 0, 12, 0) };
            seasonBox.Children.Add(new TextBlock { Text = "SEASON", Foreground = Muted, FontSize = 10, FontWeight = FontWeights.SemiBold, Margin = new Thickness(1, 0, 0, 3) });
            var seasonPick = Picker(_seasons.Select(s => (s.Key, s.Label)).ToList(), _season, key =>
            {
                _season = key; _layout.Season = key; SaveLayout();
                _build = SeasonBuild(); if (_buildText != null) _buildText.Text = BuildLine(); PopulateLocations();
                Out("Season: " + SeasonLabel() + ". Press Start to load it" + (_host != null ? " (Stop the running match first)." : "."));
            });
            seasonPick.Width = 210; seasonPick.ToolTip = "The season the Studio match runs on (Chapter 1: Erbium match server)";
            seasonBox.Children.Add(seasonPick);
            row.Children.Add(seasonBox);
            _startBtn = ToolButton("", "Start", StartServer, "Start the match server with these settings; your game joins it and docks here", Good); row.Children.Add(_startBtn);
            _stopBtn = ToolButton("", "Stop", StopServer, "Stop the match server and your game (asks first if other players are connected)", Bad); row.Children.Add(_stopBtn);
            _restartBtn = ToolButton("", "Restart", RestartServer, "Stop, then start with the current settings"); row.Children.Add(_restartBtn);
            row.Children.Add(new Border { Width = 1, Background = Line, Margin = new Thickness(8, 6, 12, 6) });
            row.Children.Add(ToolButton("", "Play", _play, "Start the match with your game (the launcher's Play button)", Accent));
            row.Children.Add(ToolButton("", "Folder", () => OpenFolder(RuntimeDir()), "Open Runtime\\Server"));
            AddEditTools(row);
            var state = new Border { Background = Field, BorderBrush = Line, BorderThickness = new Thickness(1), CornerRadius = new CornerRadius(3), Padding = new Thickness(12, 6, 14, 6), VerticalAlignment = VerticalAlignment.Center };
            var stateRow = new StackPanel { Orientation = Orientation.Horizontal };
            _stateDot = new Ellipse { Width = 10, Height = 10, Fill = Muted, Margin = new Thickness(0, 0, 8, 0), VerticalAlignment = VerticalAlignment.Center };
            stateRow.Children.Add(_stateDot);
            _stateText = new TextBlock { Foreground = Bright, VerticalAlignment = VerticalAlignment.Center, FontWeight = FontWeights.SemiBold };
            stateRow.Children.Add(_stateText);
            state.Child = stateRow;
            DockPanel.SetDock(state, Dock.Right);
            row.Children.Add(state);
            bar.Child = row;
            return bar;
        }

        UIElement BuildStatusBar()
        {
            var bar = new Border { Background = TabBar, BorderBrush = Back, BorderThickness = new Thickness(0, 1, 0, 0), Padding = new Thickness(10, 3, 10, 3) };
            var row = new DockPanel();
            _statusRight = new TextBlock { Foreground = Muted, FontSize = 11.5 };
            DockPanel.SetDock(_statusRight, Dock.Right); row.Children.Add(_statusRight);
            _statusDot = new Ellipse { Width = 8, Height = 8, Fill = Muted, Margin = new Thickness(0, 0, 7, 0), VerticalAlignment = VerticalAlignment.Center };
            row.Children.Add(_statusDot);
            _statusLeft = new TextBlock { Foreground = Fg, FontSize = 11.5 };
            row.Children.Add(_statusLeft);
            bar.Child = row;
            return bar;
        }

        static BitmapImage? LoadLogo()
        {
            try
            {
                // The Syndicate logo (the same Assets/logo.png the launcher's sidebar shows).
                var image = new BitmapImage();
                image.BeginInit(); image.CacheOption = BitmapCacheOption.OnLoad; image.UriSource = new Uri("pack://application:,,,/Assets/logo.png"); image.EndInit(); image.Freeze();
                return image;
            }
            catch { return null; }
        }

        // ---- docking -----------------------------------------------------------------------------------
        string DefaultGroup(string id) => Preset("Editor").TryGetValue(id, out var g) ? g : "bottom";
        string Group(string id) => _layout.Groups.TryGetValue(id, out var g) && (GroupIds.Contains(g) || g is "float" or "closed") ? g : DefaultGroup(id);
        UIElement Built(string id) { if (!_built.TryGetValue(id, out var e)) _built[id] = e = _panels.First(p => p.Id == id).Build(); return e; }

        // Rebuild the dock tree: main | right, main = top / bottom, top = tl | tr, right = rt / rb. A dock with no
        // panels disappears and its neighbour takes the space.
        void Arrange()
        {
            if (_closing) return;
            foreach (var h in _hosts) h.Child = null;
            _hosts.Clear();
            _root.Children.Clear();
            UIElement? G(string id) => DockGroup(id);
            var top = Split(false, _layout.TopLeftRatio, G("tl"), G("tr"), r => _layout.TopLeftRatio = r);
            var main = Split(true, _layout.TopRatio, top, G("bottom"), r => _layout.TopRatio = r);
            var right = Split(true, _layout.RightRatio, G("rt"), G("rb"), r => _layout.RightRatio = r);
            var all = Split(false, _layout.MainRatio, main, right, r => _layout.MainRatio = r);
            if (all != null) _root.Children.Add(all);
            foreach (var p in _panels.Where(p => Group(p.Id) == "float" && !_floating.ContainsKey(p.Id))) Float(p);
            foreach (var id in _floating.Keys.Where(id => Group(id) != "float").ToList()) { var w = _floating[id]; _floating.Remove(id); w.Content = null; w.Close(); }
            SaveLayout();
            UpdateButtons();
        }

        UIElement? Split(bool vertical, double ratio, UIElement? a, UIElement? b, Action<double> save)
        {
            if (a == null) return b;
            if (b == null) return a;
            var g = new Grid();
            ratio = Math.Clamp(ratio, 0.1, 0.9);
            if (vertical)
            {
                g.RowDefinitions.Add(new RowDefinition { Height = new GridLength(ratio, GridUnitType.Star), MinHeight = 80 });
                g.RowDefinitions.Add(new RowDefinition { Height = new GridLength(3) });
                g.RowDefinitions.Add(new RowDefinition { Height = new GridLength(1 - ratio, GridUnitType.Star), MinHeight = 80 });
                Grid.SetRow(b, 2);
            }
            else
            {
                g.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(ratio, GridUnitType.Star), MinWidth = 160 });
                g.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(3) });
                g.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(1 - ratio, GridUnitType.Star), MinWidth = 160 });
                Grid.SetColumn(b, 2);
            }
            g.Children.Add(a); g.Children.Add(b);
            var splitter = new GridSplitter { Background = Back, HorizontalAlignment = HorizontalAlignment.Stretch, VerticalAlignment = VerticalAlignment.Stretch, ResizeBehavior = GridResizeBehavior.PreviousAndNext, ResizeDirection = vertical ? GridResizeDirection.Rows : GridResizeDirection.Columns };
            if (vertical) Grid.SetRow(splitter, 1); else Grid.SetColumn(splitter, 1);
            splitter.DragCompleted += (_, __) =>
            {
                double first = vertical ? g.RowDefinitions[0].ActualHeight : g.ColumnDefinitions[0].ActualWidth;
                double second = vertical ? g.RowDefinitions[2].ActualHeight : g.ColumnDefinitions[2].ActualWidth;
                if (first + second > 0) save(first / (first + second));
                SaveLayout();
            };
            g.Children.Add(splitter);
            return g;
        }

        // One dock: a tab strip (editor style: dark strip, lighter active tab with an accent line) over the active panel.
        UIElement? DockGroup(string group)
        {
            var panels = _panels.Where(p => Group(p.Id) == group).ToList();
            if (panels.Count == 0) return null;
            if (!_layout.Active.TryGetValue(group, out var active) || panels.All(p => p.Id != active)) active = panels[0].Id;
            var outer = new DockPanel { Background = Panel };
            var strip = new WrapPanel { Background = TabBar }; // wraps to a second row when the dock is too narrow for its tabs
            var host = new Border { Background = Panel };
            _hosts.Add(host);
            foreach (var p in panels)
            {
                bool on = p.Id == active;
                var tab = new Border
                {
                    Background = on ? TabOn : Brushes.Transparent, BorderBrush = on ? Accent : Brushes.Transparent, BorderThickness = new Thickness(0, 2, 0, 0),
                    Padding = new Thickness(10, 0, 6, 0), Margin = new Thickness(0, 0, 1, 0), Height = 28, Cursor = Cursors.Hand
                };
                var row = new StackPanel { Orientation = Orientation.Horizontal, VerticalAlignment = VerticalAlignment.Center };
                row.Children.Add(new TextBlock { Text = p.Glyph, FontFamily = Icons, FontSize = 12, Foreground = on ? Accent : Muted, VerticalAlignment = VerticalAlignment.Center, Margin = new Thickness(0, 0, 7, 0) });
                row.Children.Add(new TextBlock { Text = p.Title, Foreground = on ? Bright : Muted, VerticalAlignment = VerticalAlignment.Center, FontWeight = on ? FontWeights.SemiBold : FontWeights.Normal });
                var more = new TextBlock { Text = "", FontFamily = Icons, FontSize = 11, Foreground = Muted, VerticalAlignment = VerticalAlignment.Center, Margin = new Thickness(8, 1, 0, 0), ToolTip = "Move, float or close" };
                row.Children.Add(more);
                tab.Child = row;
                var menu = TabMenu(p);
                tab.ContextMenu = menu;
                var id = p.Id;
                tab.MouseLeftButtonUp += (_, e) =>
                {
                    if (e.OriginalSource == more) { menu.PlacementTarget = more; menu.IsOpen = true; }
                    else { _layout.Active[group] = id; Arrange(); }
                    e.Handled = true;
                };
                DockPanel.SetDock(tab, Dock.Left);
                strip.Children.Add(tab);
            }
            DockPanel.SetDock(strip, Dock.Top);
            outer.Children.Add(strip);
            host.Child = Built(active);
            outer.Children.Add(host);
            return outer;
        }

        ContextMenu TabMenu(PanelDef p)
        {
            var menu = new ContextMenu();
            foreach (var (group, label) in new[] { ("tl", "Dock: top left"), ("tr", "Dock: top right"), ("bottom", "Dock: bottom"), ("rt", "Dock: right, top"), ("rb", "Dock: right, bottom") })
            {
                var item = new MenuItem { Header = label, IsEnabled = Group(p.Id) != group };
                item.Click += (_, __) => { _layout.Groups[p.Id] = group; _layout.Active[group] = p.Id; Arrange(); };
                menu.Items.Add(item);
            }
            menu.Items.Add(new Separator());
            var floatItem = new MenuItem { Header = "Float in its own window" }; floatItem.Click += (_, __) => { _layout.Groups[p.Id] = "float"; Arrange(); }; menu.Items.Add(floatItem);
            var closeItem = new MenuItem { Header = "Close (Window menu brings it back)" }; closeItem.Click += (_, __) => { _layout.Groups[p.Id] = "closed"; Arrange(); }; menu.Items.Add(closeItem);
            return menu;
        }

        void Float(PanelDef p)
        {
            var w = new Window
            {
                Title = "Syndicate Studio — " + p.Title, Owner = this, Width = 560, Height = 600, Background = Panel, FontFamily = FontFamily, FontSize = FontSize,
                Content = Built(p.Id), ShowInTaskbar = false, Icon = Icon, WindowStartupLocation = WindowStartupLocation.CenterOwner
            };
            _floating[p.Id] = w;
            // Closing the floating window docks the panel back where it belongs.
            w.Closed += (_, __) => { w.Content = null; if (_floating.Remove(p.Id) && !_closing) { _layout.Groups[p.Id] = DefaultGroup(p.Id); Arrange(); } };
            w.Show();
        }

        Layout LoadLayout()
        {
            try
            {
                var l = JsonSerializer.Deserialize<Layout>(File.ReadAllText(LayoutFile));
                if (l != null)
                {
                    l.Groups ??= Preset("Editor"); l.Active ??= new();
                    foreach (var k in l.Groups.Where(g => g.Value == "float").Select(g => g.Key).ToList()) l.Groups[k] = DefaultGroup(k);
                    return l;
                }
            }
            catch { }
            return new Layout { Groups = Preset("Editor") };
        }
        void SaveLayout()
        {
            try { Directory.CreateDirectory(Path.GetDirectoryName(LayoutFile)!); File.WriteAllText(LayoutFile, JsonSerializer.Serialize(_layout, new JsonSerializerOptions { WriteIndented = true })); }
            catch { }
        }

        // ---- Settings panel ----------------------------------------------------------------------------
        static readonly (string Value, string Label)[] Playlists =
        {
            ("solo", "Solo"), ("duo", "Duos"), ("squad", "Squads"), ("50v50", "50 v 50  (Season 4)"), ("playground", "Playground  (Season 4, no bots)"),
            ("arenasolo", "Arena Solo  (Season 8+, hard bots)"), ("creative", "Creative  (Season 7+, no bots)"),
        };

        UIElement BuildSettingsPanel()
        {
            var col = new StackPanel { Margin = new Thickness(12, 2, 12, 12) };
            var paths = _getPaths();
            col.Children.Add(Section("Playlist"));
            col.Children.Add(Picker(Playlists, StudioSession.ReadMode(), id =>
            {
                try { StudioSession.SaveMode(id); Out("Playlist set to " + id + " (next start)."); } catch (Exception ex) { Out("ERROR: " + ex.Message); }
            }));
            col.Children.Add(Note("Used for matches started while the Studio is open. Pick the season next to Start in the toolbar."));

            col.Children.Add(Section("Bots"));
            col.Children.Add(Note("A Studio match has no bots. With Studio match off, the bot count, bot difficulty and tick rate come from the launcher's own match settings."));

            col.Children.Add(Section("Startup"));
            CheckBox Option(string text, bool value, Action<bool> set)
            {
                var box = new CheckBox { Content = new TextBlock { Text = text, TextWrapping = TextWrapping.Wrap }, IsChecked = value, Foreground = Fg, Margin = new Thickness(0, 0, 0, 6) };
                box.Checked += (_, __) => { set(true); SaveLayout(); };
                box.Unchecked += (_, __) => { set(false); SaveLayout(); };
                return box;
            }
            col.Children.Add(Option("Viewport always embedded", _layout.AutoEmbed, v => { _layout.AutoEmbed = v; if (!v) ReleaseView(); }));
            col.Children.Add(Option("Studio match: no bots, no bus or storm (you stay on the island)", _layout.StudioMatch, v => { _layout.StudioMatch = v; StudioSessionTick(); Out(v ? "Studio match on: Restart the server to get an empty island with no bus." : "Studio match off: Restart the server for a normal match with bots."); }));
            col.Children.Add(Option("Fly when the game docks (F8 switches it in the game)", _layout.Fly, v => { _layout.Fly = v; if (NativeEmbed.Alive(_game)) SetFly(v); }));
            col.Children.Add(Note("The viewport shows your own game, docked: set Fortnite to Windowed in its video settings. Keys are listed in the Controls tab."));
            return Scroll(col);
        }

        string? SeasonBuild() => _seasons.FirstOrDefault(s => s.Key == _season).Build;
        string SeasonLabel() => _seasons.FirstOrDefault(s => s.Key == _season).Label ?? "none";
        string BuildLine() => _build == null ? "Pick a season, then Start" : "Fortnite " + _build + " · Dev launcher";

        // ---- Server panel ------------------------------------------------------------------------------
        UIElement BuildServerPanel()
        {
            var col = new StackPanel { Margin = new Thickness(12, 2, 12, 12) };
            col.Children.Add(Section("Match server"));
            _detailText = new TextBlock { Foreground = Fg, FontFamily = Mono, FontSize = 12, TextWrapping = TextWrapping.Wrap, LineHeight = 19 };
            col.Children.Add(_detailText);
            var buttons = new WrapPanel { Margin = new Thickness(0, 12, 0, 0) };
            buttons.Children.Add(MakeButton("Open server folder", () => OpenFolder(RuntimeDir())));
            buttons.Children.Add(MakeButton("Open full log", () => { if (_tailFile != null && File.Exists(_tailFile)) Open(_tailFile); else Out("No server log yet."); }));
            col.Children.Add(buttons);
            col.Children.Add(Note("Start launches the match server and your game together, like the launcher's Play. Stop ends both (it asks first when other players are connected)."));
            return Scroll(col);
        }

        string? RuntimeDir() => _getPaths()?.ServerDir;

        static string ReadShared(string path)
        {
            using var stream = new FileStream(path, FileMode.Open, FileAccess.Read, FileShare.ReadWrite | FileShare.Delete);
            using var reader = new StreamReader(stream);
            return reader.ReadToEnd();
        }
        // A live JSON file the match server rewrites, or null when missing, stale or written by another process.
        static JsonDocument? LiveJson(string file, int pid, double maxAgeSeconds)
        {
            try
            {
                if (!File.Exists(file) || (DateTime.UtcNow - File.GetLastWriteTimeUtc(file)).TotalSeconds > maxAgeSeconds) return null;
                var doc = JsonDocument.Parse(ReadShared(file));
                if (doc.RootElement.TryGetProperty("pid", out var p) && p.ValueKind == JsonValueKind.Number && p.GetInt32() != pid) { doc.Dispose(); return null; }
                return doc;
            }
            catch { return null; }
        }

        // The running host the launcher owns (server.json), or null when none is alive.
        HostState? RunningHost(LauncherPaths paths)
        {
            try
            {
                var state = JsonSerializer.Deserialize<HostState>(ReadShared(Path.Combine(paths.ServerDir, "server.json")));
                if (state == null) return null;
                using var process = Process.GetProcessById(state.Pid);
                if (process.HasExited || process.StartTime.ToUniversalTime().Ticks != state.Started) return null;
                return StudioSession.OwnsProcess(state.Pid) ? state : null;
            }
            catch { return null; }
        }

        (int Connected, bool Accepting, bool Ended, int HumansAlive, double AgeSeconds)? Occupancy(string runtime, int pid)
        {
            try
            {
                var file = Path.Combine(runtime, "occupancy.json");
                using var doc = JsonDocument.Parse(ReadShared(file));
                var r = doc.RootElement;
                if (!r.TryGetProperty("Pid", out var p) || p.GetInt32() != pid) return null;
                int Int(string name, int fallback) => r.TryGetProperty(name, out var v) && v.ValueKind == JsonValueKind.Number ? v.GetInt32() : fallback;
                bool Bool(string name) => r.TryGetProperty(name, out var v) && v.ValueKind == JsonValueKind.True;
                return (Int("Connected", 0), Bool("Accepting"), Bool("Ended"), Int("HumansAlive", -1), (DateTime.UtcNow - File.GetLastWriteTimeUtc(file)).TotalSeconds);
            }
            catch { return null; }
        }

        void Poll()
        {
            var paths = _getPaths();
            string state; Brush dot; var detail = new StringBuilder(); string stats = "";
            HostState? host = paths == null ? null : RunningHost(paths);
            _host = host;
            if (paths == null) { state = "Syndicate's files not found"; dot = Bad; }
            else if (host == null) { state = _starting ? "Starting…" : "Stopped"; dot = _starting ? Warn : Muted; }
            else
            {
                var runtime = paths.ServerDir;
                bool ready = false;
                try { ready = ReadShared(Path.Combine(runtime, "ready.pid")).Trim() == host.Pid.ToString(); } catch { }
                var occ = ready ? Occupancy(runtime, host.Pid) : null;
                if (!ready) { state = "Loading the island…"; dot = Warn; }
                else if (occ == null) { state = "Ready"; dot = Good; }
                else if (occ.Value.Ended) { state = "Match over"; dot = Muted; }
                else if (occ.Value.Accepting) { state = occ.Value.Connected == 0 ? "Ready — waiting for players" : "Warm-up — " + occ.Value.Connected + " connected"; dot = Good; }
                else { state = "Match in progress"; dot = Accent; }
                var uptime = DateTime.UtcNow - new DateTime(host.Started, DateTimeKind.Utc);
                detail.AppendLine("Season     " + host.Build);
                detail.AppendLine("Playlist   " + host.Mode);
                detail.AppendLine("Bot skill  " + (host.Skill.Length == 0 ? "-" : host.Skill));
                detail.AppendLine("PID        " + host.Pid);
                detail.AppendLine("Uptime     " + (uptime.TotalHours >= 1 ? ((int)uptime.TotalHours) + "h " : "") + uptime.Minutes + "m " + uptime.Seconds + "s");
                if (occ != null)
                {
                    detail.AppendLine("Connected  " + occ.Value.Connected + (occ.Value.HumansAlive >= 0 ? "  (" + occ.Value.HumansAlive + " alive)" : ""));
                    if (occ.Value.AgeSeconds > 5) detail.AppendLine("Last tick  " + (int)occ.Value.AgeSeconds + " s ago (slow or frozen)");
                }
                using (var net = LiveJson(Path.Combine(runtime, "net-live.json"), host.Pid, 5))
                    if (net != null)
                    {
                        var r = net.RootElement;
                        string N(string k, string f = "0.#") => r.TryGetProperty(k, out var v) && v.ValueKind == JsonValueKind.Number ? v.GetDouble().ToString(f) : "?";
                        detail.AppendLine("Tick rate  " + N("tick") + " Hz  (worst frame " + N("worstFrameMs", "0") + " ms)");
                        stats = N("tick") + " Hz · " + N("humans", "0") + " humans · " + N("bots", "0") + " bots";
                    }
                if (ready) RefreshLive(runtime, host.Pid);
                PollDevResult(runtime);
                var gameLog = StudioSession.HostEngineLog();
                SetTail(_logSource == "game" && gameLog != null ? gameLog : Path.Combine(runtime, "stdout.log." + host.Pid + ".log"));
            }
            if (host == null) { RefreshLive(null, 0); if (paths != null) LoadAssets(paths.ServerDir); } // last match's class list
            if (_starting && _phase.Length > 0) detail.AppendLine().Append(_phase);
            if (host == null && !_starting) detail.Append("No match server is running. Press Start.");
            _stateNow = state;
            if (_stateText != null) _stateText.Text = state;
            if (_stateDot != null) _stateDot.Fill = dot;
            if (_statusDot != null) _statusDot.Fill = dot;
            if (_statusLeft != null) _statusLeft.Text = state + (host != null ? "   ·   Fortnite " + host.Build + " · " + host.Mode : "");
            if (_statusRight != null) _statusRight.Text = stats;
            if (_detailText != null) _detailText.Text = detail.ToString().TrimEnd();
            UpdateButtons(host != null);
        }

        void UpdateButtons(bool? running = null)
        {
            bool up = running ?? _host != null;
            if (_startBtn != null) _startBtn.IsEnabled = !_starting && !_stopping;
            if (_stopBtn != null) { _stopBtn.IsEnabled = !_stopping && (up || _starting); _stopBtn.ToolTip = _starting ? "Cancel the start" : "Stop the match server and your game (asks first if other players are connected)"; }
            if (_restartBtn != null) _restartBtn.IsEnabled = !_starting && !_stopping && up;
        }

        // ---- server control ----------------------------------------------------------------------------
        async void StartServer()
        {
            if (_starting || _stopping) return;
            var paths = _getPaths();
            if (paths == null) { Out("ERROR: Syndicate's files weren't found (Runtime\\Server next to Syndicate-backend). Run UPDATE FROM GIT once."); return; }
            if (!StudioSession.SupportsBuild(SeasonBuild()))
            {
                Out("ERROR: pick a season next to Start first (Pre-season to Season 9: they run on the Erbium match server).");
                return;
            }
            if (StudioSession.ClientRunning() || _host != null) { Out("A match is already running. Stop it first (or use Restart)."); return; }
            _starting = true; _phase = ""; _startCancel = new CancellationTokenSource(); UpdateButtons();
            StudioSessionTick(); // the launcher reads the Studio options from this when it starts the server
            Out("Starting " + SeasonLabel() + " (" + StudioSession.ReadMode() + (_layout.StudioMatch ? ", Studio match: no bots, endless warm-up" : ", normal match") + ") and your game…");
            try
            {
                // Stop (while starting) ends the launch: the launcher's session is torn down and Play reports it.
                using var cancel = _startCancel.Token.Register(StudioSession.EndMatchNow);
                bool ok = await _launch(_season, s => UI(() => { if (s.Length == 0 || s == _phase) return; _phase = s; Out(s); }));
                if (_startCancel.IsCancellationRequested) Out("Start cancelled.");
                else if (ok)
                {
                    _log("Syndicate Studio: match ready.");
                    Out("Match ready. Your game docks in the Server viewport once it renders: press Play in Fortnite's lobby to drop onto the island.");
                }
                else Out("ERROR: the match didn't start. The launcher's Diagnostics panel has the reason and how to fix it.");
            }
            catch (Exception ex) { Out("ERROR: " + ex.Message); _log("Syndicate Studio: start failed — " + ex.Message); }
            finally { _starting = false; _phase = ""; _startCancel?.Dispose(); _startCancel = null; Poll(); }
        }

        async Task<bool> StopCore()
        {
            if (_starting) { _startCancel?.Cancel(); Out("Cancelling the start…"); return true; }
            var paths = _getPaths();
            if (paths == null) return false;
            var host = RunningHost(paths);
            if (host == null) { Out("No match server is running."); return true; }
            var occ = Occupancy(paths.ServerDir, host.Pid);
            if (occ != null && occ.Value.Connected > 0 && !occ.Value.Ended &&
                MessageBox.Show(this, occ.Value.Connected + " player(s) are connected to this server. Stop it anyway? They will be disconnected.", "Syndicate Studio", MessageBoxButton.YesNo, MessageBoxImage.Warning) != MessageBoxResult.Yes)
                return false;
            _stopping = true; UpdateButtons();
            try
            {
                await Task.Run(StudioSession.StopMatch);
                Out("Match stopped (server and game).");
                return true;
            }
            catch (Exception ex) { Out("ERROR: couldn't stop the server — " + ex.Message); return false; }
            finally { _stopping = false; Poll(); }
        }

        async void StopServer() => await StopCore();

        async void RestartServer()
        {
            if (await StopCore()) StartServer();
        }

        // ---- live match data (Outliner / Details) ------------------------------------------------------
        void RefreshLive(string? runtime, int pid)
        {
            // If the pipe is connected, use it for actors (faster, direct UE4 access). One actor request at a time,
            // at most every 1.5 s: each one walks every actor in the world on the game thread.
            if (_pipe != null)
            {
                if (_liveInFlight || (DateTime.UtcNow - _liveLast).TotalMilliseconds < 1500) return;
                _liveInFlight = true; _liveLast = DateTime.UtcNow;
                _ = RefreshLiveViaPipe(runtime, pid);
                return;
            }
            _items.Clear();
            RefreshLiveViaFiles(runtime, pid);
        }

        // The match server answers dev commands (tppos, actormove, ...) in dev-command-result.txt: show each new answer.
        DateTime _devResultSeen = DateTime.MinValue;
        void PollDevResult(string runtime)
        {
            try
            {
                var file = Path.Combine(runtime, "dev-command-result.txt");
                if (!File.Exists(file)) return;
                var stamp = File.GetLastWriteTimeUtc(file);
                if (_devResultSeen == DateTime.MinValue) { _devResultSeen = stamp; return; } // not last session's answer
                if (stamp <= _devResultSeen) return;
                _devResultSeen = stamp;
                var text = File.ReadAllText(file).Trim();
                if (text.Length > 0) Out("Server: " + (text.StartsWith("!") ? "ERROR — " + text[1..] : text));
            }
            catch { }
        }

        bool _liveInFlight;
        DateTime _liveLast = DateTime.MinValue, _liveFailLogged = DateTime.MinValue;
        string PipeWhy() => _pipe != null && _pipe.LastFailure.Length > 0 ? " (" + _pipe.LastFailure + ")" : "";

        async Task RefreshLiveViaPipe(string? runtime, int pid)
        {
            var fresh = new Dictionary<string, Item>();
            try
            {
                // Still read dev-live.json for players/bots — the pipe only handles scene actors
                if (runtime != null)
                {
                    using (var dev = LiveJson(Path.Combine(runtime, "dev-live.json"), pid, 5))
                        if (dev != null && dev.RootElement.TryGetProperty("players", out var players) && players.ValueKind == JsonValueKind.Array)
                        {
                            foreach (var p in players.EnumerateArray())
                            {
                                var props = Props(p);
                                var name = props.GetValueOrDefault("n", "?");
                                bool bot = props.GetValueOrDefault("b") == "1";
                                var key = (bot ? "bot:" : "player:") + name;
                                props.Remove("n"); props.Remove("b");
                                fresh[key] = new Item(key, bot ? "bot" : "player", name, bot ? "Bot" : "Player", props);
                            }
                            if (dev.RootElement.TryGetProperty("zone", out var zone))
                                fresh["storm"] = new Item("storm", "storm", "Next safe zone", "Storm", Props(zone));
                        }
                }

                // Get scene actors from the pipe
                var actors = await _pipe!.GetActorsAsync();
                if (actors != null)
                {
                    foreach (var a in actors)
                    {
                        var key = "actor:" + a.Index;
                        var props = new Dictionary<string, string>
                        {
                            ["id"] = a.Index.ToString(),
                            ["c"] = a.ClassName ?? "?",
                            ["x"] = a.X.ToString("0.##", Inv),
                            ["y"] = a.Y.ToString("0.##", Inv),
                            ["z"] = a.Z.ToString("0.##", Inv),
                            ["yaw"] = a.Yaw.ToString("0.##", Inv),
                            ["pitch"] = a.Pitch.ToString("0.##", Inv),
                            ["roll"] = a.Roll.ToString("0.##", Inv),
                        };
                        fresh[key] = new Item(key, "actor", a.Name ?? a.ClassName ?? "?", "Scene actor", props);
                    }
                }
                else
                {
                    if ((DateTime.UtcNow - _liveFailLogged).TotalSeconds > 15)
                    { _liveFailLogged = DateTime.UtcNow; Out("Outliner: couldn't read the actors this time" + PipeWhy() + " — keeping the last list."); }
                    // Keep the last actor list on a failed read instead of blanking the Outliner; the pipe reconnects itself
                    foreach (var kv in _items) if (kv.Value.Kind == "actor") fresh[kv.Key] = kv.Value;
                }

                if (runtime != null) LoadAssets(runtime);
            }
            catch (Exception ex)
            {
                // Keep the pipe: it reconnects by itself on the next command. Dropping it here used to turn the
                // Studio features off until the game was restarted (the DLL is only injected when the game docks).
                Out("Outliner refresh failed — " + ex.Message);
                _liveInFlight = false;
                return;
            }
            finally { _liveInFlight = false; }
            UI(() =>
            {
                KeepSelectedItems(fresh);
                _items.Clear();
                foreach (var kv in fresh) _items[kv.Key] = kv.Value;
                RefreshOutliner(); RefreshDetails();
            });
        }

        void RefreshLiveViaFiles(string? runtime, int pid)
        {
            if (runtime != null)
            {
                using (var dev = LiveJson(Path.Combine(runtime, "dev-live.json"), pid, 5))
                    if (dev != null && dev.RootElement.TryGetProperty("players", out var players) && players.ValueKind == JsonValueKind.Array)
                    {
                        foreach (var p in players.EnumerateArray())
                        {
                            var props = Props(p);
                            var name = props.GetValueOrDefault("n", "?");
                            bool bot = props.GetValueOrDefault("b") == "1";
                            var key = (bot ? "bot:" : "player:") + name;
                            props.Remove("n"); props.Remove("b");
                            _items[key] = new Item(key, bot ? "bot" : "player", name, bot ? "Bot" : "Player", props);
                        }
                        if (dev.RootElement.TryGetProperty("zone", out var zone))
                            _items["storm"] = new Item("storm", "storm", "Next safe zone", "Storm", Props(zone));
                    }
                using (var map = LiveJson(Path.Combine(runtime, "mapeditor-live.json"), pid, 5))
                    if (map != null && map.RootElement.TryGetProperty("actors", out var actors) && actors.ValueKind == JsonValueKind.Array)
                        foreach (var a in actors.EnumerateArray())
                        {
                            var props = Props(a);
                            var key = "actor:" + props.GetValueOrDefault("id", "?");
                            var cls = props.GetValueOrDefault("c", "?");
                            _items[key] = new Item(key, "actor", cls, props.GetValueOrDefault("map") == "1" ? "Map piece" : "Placed piece", props);
                        }
                LoadAssets(runtime);
            }
            RefreshOutliner();
            RefreshDetails();
        }

        static Dictionary<string, string> Props(JsonElement e)
        {
            var d = new Dictionary<string, string>();
            if (e.ValueKind != JsonValueKind.Object) return d;
            foreach (var p in e.EnumerateObject())
                d[p.Name] = p.Value.ValueKind switch
                {
                    JsonValueKind.String => p.Value.GetString() ?? "",
                    JsonValueKind.Number => p.Value.GetDouble().ToString("0.##"),
                    JsonValueKind.True => "1",
                    JsonValueKind.False => "0",
                    _ => p.Value.GetRawText()
                };
            return d;
        }

        UIElement BuildOutlinerPanel()
        {
            var dock = new DockPanel { Margin = new Thickness(6) };
            var search = Input(_outlinerFilter); search.ToolTip = "Search the outliner"; search.Margin = new Thickness(0, 0, 0, 6);
            search.TextChanged += (_, __) => { _outlinerFilter = search.Text; _outlinerSig = ""; RefreshOutliner(); };
            DockPanel.SetDock(search, Dock.Top); dock.Children.Add(search);
            var header = new Grid { Margin = new Thickness(6, 0, 6, 2) };
            header.ColumnDefinitions.Add(new ColumnDefinition()); header.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(96) });
            header.Children.Add(new TextBlock { Text = "Item Label", Foreground = Muted, FontSize = 11 });
            var typeHead = new TextBlock { Text = "Type", Foreground = Muted, FontSize = 11 }; Grid.SetColumn(typeHead, 1); header.Children.Add(typeHead);
            DockPanel.SetDock(header, Dock.Top); dock.Children.Add(header);
            _outliner = new ListBox { Background = Field, BorderBrush = Line, Foreground = Fg, ItemContainerStyle = ItemStyle() };
            ScrollViewer.SetHorizontalScrollBarVisibility(_outliner, ScrollBarVisibility.Disabled);
            // A click on a row selects that one item (a rebuild re-selecting the current row keeps a multi-selection)
            _outliner.SelectionChanged += (_, __) => { if (_outliner.SelectedItem is ListBoxItem { Tag: string key } && key != _selected) { _selected = key; _sel.Clear(); _sel.Add(key); RefreshDetails(force: true); GizmoTick(); } };
            dock.Children.Add(_outliner);
            _outlinerSig = "\0";
            RefreshOutliner();
            return dock;
        }

        // Rows are rebuilt only when the set of items changes (positions change every tick; the Details panel shows those).
        void RefreshOutliner()
        {
            if (_outliner == null) return;
            var shown = _items.Values.Where(i => _outlinerFilter.Length == 0 || i.Name.Contains(_outlinerFilter, StringComparison.OrdinalIgnoreCase) || i.Type.Contains(_outlinerFilter, StringComparison.OrdinalIgnoreCase)).ToList();
            string Alive(Item i) => i.Props.GetValueOrDefault("a", "1");
            // Order-independent signature: the pipe hands actors over nearest-first, so their order shifts whenever you
            // or the camera move. Rebuilding on every shift re-created ~1500 rows every refresh and ate Outliner clicks.
            var sig = string.Join("|", shown.Select(i => i.Key + Alive(i)).OrderBy(k => k, StringComparer.Ordinal)) + (_host == null ? "#off" : "#on");
            if (sig == _outlinerSig) return;
            // Don't rebuild under the mouse: a rebuild mid-click drops the click (the next tick catches up)
            if (_outliner.IsMouseOver && _outliner.Items.Count > 0 && _host != null) return;
            _outlinerSig = sig;
            _outliner.Items.Clear();
            void GroupRows(string title, IEnumerable<Item> items)
            {
                var list = items.ToList();
                if (list.Count == 0) return;
                _outliner.Items.Add(new ListBoxItem { Content = Row("", title + "  (" + list.Count + ")", "Folder", Fg, true), Focusable = false, IsHitTestVisible = false });
                foreach (var i in list)
                {
                    bool dead = Alive(i) == "0";
                    var glyphFor = i.Kind switch { "bot" => "", "player" => "", "storm" => "", _ => "" };
                    var item = new ListBoxItem { Content = Row(glyphFor, i.Name + (dead ? "  (eliminated)" : ""), i.Type, dead ? Muted : Fg, false), Tag = i.Key };
                    _outliner.Items.Add(item);
                    if (i.Key == _selected) item.IsSelected = true;
                }
            }
            GroupRows("Players", shown.Where(i => i.Kind == "player"));
            GroupRows("Bots", shown.Where(i => i.Kind == "bot").OrderBy(i => int.TryParse(i.Props.GetValueOrDefault("t"), out var t) ? t : 0).ThenBy(i => i.Name));
            GroupRows("Placed pieces", shown.Where(i => i.Kind == "actor").OrderBy(i => i.Name, StringComparer.OrdinalIgnoreCase));
            GroupRows("World", shown.Where(i => i.Kind == "storm"));
            if (_outliner.Items.Count == 0)
                _outliner.Items.Add(new ListBoxItem { Content = new TextBlock { Text = _host == null ? "Start the server to see the match here." : "Nobody in the match yet. Players, bots and placed pieces appear once the island is up.", Foreground = Muted, TextWrapping = TextWrapping.Wrap, Margin = new Thickness(2, 6, 2, 6) }, Focusable = false, IsHitTestVisible = false });
        }

        UIElement Row(string glyph, string name, string type, Brush color, bool folder)
        {
            var g = new Grid { Margin = new Thickness(folder ? 0 : 16, 1, 0, 1) };
            g.ColumnDefinitions.Add(new ColumnDefinition { Width = GridLength.Auto }); g.ColumnDefinitions.Add(new ColumnDefinition()); g.ColumnDefinitions.Add(new ColumnDefinition { Width = type.Length == 0 ? GridLength.Auto : new GridLength(90) });
            g.Children.Add(new TextBlock { Text = glyph, FontFamily = Icons, FontSize = 12, Foreground = folder ? Accent : Muted, Margin = new Thickness(0, 1, 7, 0), VerticalAlignment = VerticalAlignment.Center });
            var n = new TextBlock { Text = name, Foreground = color, TextTrimming = TextTrimming.CharacterEllipsis, FontWeight = folder ? FontWeights.SemiBold : FontWeights.Normal, VerticalAlignment = VerticalAlignment.Center }; Grid.SetColumn(n, 1); g.Children.Add(n);
            var t = new TextBlock { Text = type, Foreground = Muted, FontSize = 11, VerticalAlignment = VerticalAlignment.Center }; Grid.SetColumn(t, 2); g.Children.Add(t);
            return g;
        }

        UIElement BuildDetailsPanel()
        {
            _details = new StackPanel { Margin = new Thickness(12, 2, 12, 12) };
            RefreshDetails(force: true);
            return Scroll(_details);
        }

        string? _detailsKey;
        readonly Dictionary<string, TextBlock> _detailValues = new();
        static readonly Dictionary<string, string> PropNames = new()
        {
            ["t"] = "Team", ["s"] = "State", ["a"] = "Alive", ["k"] = "Knocked", ["hp"] = "Health", ["sh"] = "Shield", ["x"] = "X", ["y"] = "Y", ["z"] = "Z", ["yaw"] = "Yaw",
            ["r"] = "Radius", ["id"] = "Id", ["c"] = "Class", ["ok"] = "Spawned", ["map"] = "From saved map", ["e"] = "Error",
        };
        static readonly string[] PropOrder = { "c", "id", "t", "s", "a", "k", "hp", "sh", "x", "y", "z", "yaw", "r" };

        // Details for the selected outliner item. The layout is rebuilt when the selection changes; the values update
        // in place every tick so edit boxes keep what you're typing.
        void RefreshDetails(bool force = false)
        {
            if (_details == null) return;
            _items.TryGetValue(_selected ?? "", out var item);
            if (force || item?.Key != _detailsKey)
            {
                _details.Children.Clear(); _detailValues.Clear();
                _detailsKey = item?.Key;
                AddSelectionTools(_details);
                if (item == null)
                {
                    _details.Children.Add(Note(_selected == null ? "Select something in the Outliner to see its details." : "That item has left the match."));
                    return;
                }
                _details.Children.Add(new TextBlock { Text = item.Name, Foreground = Bright, FontSize = 15, FontWeight = FontWeights.SemiBold, TextTrimming = TextTrimming.CharacterEllipsis, Margin = new Thickness(0, 10, 0, 2) });
                _details.Children.Add(new TextBlock { Text = item.Type, Foreground = Accent, FontSize = 11.5 });
                if (item.Kind is "player" or "bot")
                {
                    _details.Children.Add(Section("Actions"));
                    var row = new WrapPanel();
                    var name = item.Name;
                    row.Children.Add(MakeButton("Teleport me here", () => DevCommand("tp", name)));
                    if (item.Kind == "bot") row.Children.Add(MakeButton("Kill", () => DevCommand("kill", name)));
                    if (item.Kind == "bot") row.Children.Add(MakeButton("Bot brain", () => DevCommand("botinfo", name), false, "What this bot is doing and why, what it heard, and whether it is stuck"));
                    _details.Children.Add(row);
                }
                if (item.Kind == "actor") AddActorEditor(_details, item);
                _details.Children.Add(Section("Properties"));
                var grid = new Grid();
                grid.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(110) }); grid.ColumnDefinitions.Add(new ColumnDefinition());
                int r = 0;
                foreach (var key in item.Props.Keys.OrderBy(k => Array.IndexOf(PropOrder, k) is var i && i >= 0 ? i : 99))
                {
                    grid.RowDefinitions.Add(new RowDefinition { Height = GridLength.Auto });
                    var label = new TextBlock { Text = PropNames.GetValueOrDefault(key, key), Foreground = Muted, Margin = new Thickness(0, 3, 8, 3) };
                    Grid.SetRow(label, r); grid.Children.Add(label);
                    var value = new TextBlock { Text = Pretty(key, item.Props[key]), Foreground = Fg, FontFamily = Mono, FontSize = 12, Margin = new Thickness(0, 3, 0, 3), TextWrapping = TextWrapping.Wrap };
                    Grid.SetRow(value, r); Grid.SetColumn(value, 1); grid.Children.Add(value);
                    _detailValues[key] = value;
                    r++;
                }
                _details.Children.Add(grid);
                return;
            }
            if (item == null) return;
            foreach (var (key, block) in _detailValues) block.Text = item.Props.TryGetValue(key, out var v) ? Pretty(key, v) : "-";
        }

        static string Pretty(string key, string value) => key is "a" or "k" or "ok" or "map" ? (value == "1" ? "Yes" : "No") : value;

        void AddActorEditor(StackPanel panel, Item item)
        {
            panel.Children.Add(Section("Transform"));
            var grid = new Grid();
            grid.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(40) }); grid.ColumnDefinitions.Add(new ColumnDefinition());
            var boxes = new Dictionary<string, TextBox>();
            int r = 0;
            foreach (var (key, label, color) in new[] { ("x", "X", "#FF6B6B"), ("y", "Y", "#6BE06B"), ("z", "Z", "#6BA8FF"), ("yaw", "Yaw", "#8C8C8C") })
            {
                grid.RowDefinitions.Add(new RowDefinition { Height = GridLength.Auto });
                var l = new TextBlock { Text = label, Foreground = B(color), VerticalAlignment = VerticalAlignment.Center, FontWeight = FontWeights.SemiBold };
                Grid.SetRow(l, r); grid.Children.Add(l);
                var box = Input(item.Props.GetValueOrDefault(key, "0")); box.Margin = new Thickness(0, 2, 0, 2); box.FontFamily = Mono;
                Grid.SetRow(box, r); Grid.SetColumn(box, 1); grid.Children.Add(box);
                boxes[key] = box; r++;
            }
            panel.Children.Add(grid);
            var row = new WrapPanel { Margin = new Thickness(0, 8, 0, 0) };
            var id = item.Props.GetValueOrDefault("id", "0");
            row.Children.Add(MakeButton("Apply", () =>
            {
                string V(string k) => boxes[k].Text.Trim();
                if (_pipe != null && uint.TryParse(id, out var idx))
                    ApplyActorViaPipe(idx, item.Name, V("x"), V("y"), V("z"), V("yaw"));
                else
                    MapEditorOps("{\"ops\":[{\"op\":\"move\",\"id\":" + id + ",\"x\":" + Json(V("x")) + ",\"y\":" + Json(V("y")) + ",\"z\":" + Json(V("z")) + ",\"yaw\":" + Json(V("yaw")) + "}]}", "Moved piece " + id + ".");
            }, primary: true));
            row.Children.Add(MakeButton("Delete", () =>
            {
                if (_pipe != null && uint.TryParse(id, out var idx))
                    DeleteActorViaPipe(idx, item.Name);
                else
                    MapEditorOps("{\"ops\":[{\"op\":\"delete\",\"id\":" + id + "}]}", "Deleted piece " + id + ".");
            }));
            panel.Children.Add(row);
        }

        async void ApplyActorViaPipe(uint index, string name, string xs, string ys, string zs, string yaws)
        {
            try
            {
                if (!float.TryParse(xs, System.Globalization.NumberStyles.Float, System.Globalization.CultureInfo.InvariantCulture, out var x) ||
                    !float.TryParse(ys, System.Globalization.NumberStyles.Float, System.Globalization.CultureInfo.InvariantCulture, out var y) ||
                    !float.TryParse(zs, System.Globalization.NumberStyles.Float, System.Globalization.CultureInfo.InvariantCulture, out var z))
                { Out("ERROR: invalid position values."); return; }
                float.TryParse(yaws, System.Globalization.NumberStyles.Float, System.Globalization.CultureInfo.InvariantCulture, out var yaw);
                // The server owns map pieces (collision, every player): move it there too. The pipe move below is
                // what you see right away in your own game.
                DevCommand("actormove", name + "@" + FormattableString.Invariant($"{x:0.##},{y:0.##},{z:0.##},{yaw:0.##}"));

                var posOk = await _pipe!.SetActorPositionAsync(index, x, y, z);
                var rotOk = await _pipe.SetActorRotationAsync(index, 0, yaw, 0);
                if (posOk && rotOk)
                {
                    _mapSession.RecordMoveYaw(index, x, y, z, yaw);
                    UpdateMapLabel();
                    Out($"Moved actor {index} to ({x:0.#}, {y:0.#}, {z:0.#}) yaw {yaw:0.#}.");
                }
                else Out("ERROR: your game didn't move that piece (details in %LOCALAPPDATA%\\ProjectTilted\\syndicate-studio.log)." + PipeWhy());
            }
            catch (Exception ex) { Out("ERROR: pipe command failed — " + ex.Message); }
        }

        async void DeleteActorViaPipe(uint index, string name)
        {
            try
            {
                DevCommand("actordel", name); // the server owns map pieces: remove it there too
                var ok = await _pipe!.DeleteActorAsync(index);
                if (ok)
                {
                    _mapSession.RecordDelete(index);
                    UpdateMapLabel();
                    Out($"Deleted actor {index}.");
                }
                else Out("ERROR: the game couldn't delete that actor." + PipeWhy());
            }
            catch (Exception ex) { Out("ERROR: pipe command failed — " + ex.Message); }
        }

        // Dev Console commands for the match server (Runtime\Server\dev-command.txt, "command|target|you").
        void DevCommand(string command, string target, bool quiet = false)
        {
            var dir = RuntimeDir();
            if (dir == null || _host == null) { Out("No match server is running."); return; }
            var me = _me();
            if (me.Length == 0) { Out("Sign in to the launcher first: commands act on your player."); return; }
            try { File.AppendAllText(Path.Combine(dir, "dev-command.txt"), $"{command}|{target.Replace("|", "")}|{me.Replace("|", "")}\n"); if (!quiet) Out($"{command} {target}"); }
            catch (Exception ex) { Out("ERROR: " + ex.Message); }
        }

        string? DevToken()
        {
            try { var dir = RuntimeDir(); var f = dir == null ? null : Path.Combine(dir, "telemetry", "dev-token"); return f != null && File.Exists(f) ? File.ReadAllText(f).Trim() : null; }
            catch { return null; }
        }

        async void MapEditorOps(string json, string done)
        {
            var token = DevToken();
            if (string.IsNullOrEmpty(token)) { Out("ERROR: the backend hasn't created its developer token yet — start Syndicate's local services first."); return; }
            try
            {
                using var content = new StringContent(json, Encoding.UTF8, "application/json");
                using var resp = await Http.PostAsync("http://127.0.0.1:3551/tilted/mapeditor/live?token=" + Uri.EscapeDataString(token), content);
                var body = await resp.Content.ReadAsStringAsync();
                if (resp.IsSuccessStatusCode && body.Contains("\"ok\":true")) Out(done);
                else Out("ERROR: the map editor refused it — " + (body.Length > 200 ? body[..200] : body));
            }
            catch (Exception ex) { Out("ERROR: couldn't reach the backend — " + ex.Message); }
        }

        static string Json(string s)
        {
            var sb = new StringBuilder("\"");
            foreach (var c in s) sb.Append(c == '"' || c == '\\' ? "\\" + c : c < ' ' ? "" : c.ToString());
            return sb.Append('"').ToString();
        }

        // ---- Content browser ---------------------------------------------------------------------------
        void LoadAssets(string runtime)
        {
            var file = Path.Combine(runtime, "map-assets.json");
            string sig;
            try { if (!File.Exists(file)) return; sig = File.GetLastWriteTimeUtc(file).Ticks.ToString(); } catch { return; }
            if (sig == _assetsSig) return;
            _assetsSig = sig;
            var assets = new Dictionary<string, List<string>>();
            try
            {
                using var doc = JsonDocument.Parse(ReadShared(file));
                if (doc.RootElement.TryGetProperty("classes", out var cls) && cls.ValueKind == JsonValueKind.Object)
                    foreach (var c in cls.EnumerateObject())
                        if (c.Value.ValueKind == JsonValueKind.Array && c.Name.Length > 0)
                            assets[char.ToUpperInvariant(c.Name[0]) + c.Name[1..]] = c.Value.EnumerateArray().Where(v => v.ValueKind == JsonValueKind.String).Select(v => v.GetString()!).ToList();
            }
            catch { return; }
            _assets = assets;
            RefreshContent(categoriesToo: true);
        }

        UIElement BuildContentPanel()
        {
            var grid = new Grid { Margin = new Thickness(6) };
            grid.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(170) });
            grid.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(6) });
            grid.ColumnDefinitions.Add(new ColumnDefinition());
            _categories = new ListBox { Background = Field, BorderBrush = Line, Foreground = Fg, ItemContainerStyle = ItemStyle() };
            ScrollViewer.SetHorizontalScrollBarVisibility(_categories, ScrollBarVisibility.Disabled);
            _categories.SelectionChanged +=(_, __) => { if (_categories.SelectedItem is ListBoxItem { Tag: string c } && c != _contentCategory) { _contentCategory = c; RefreshContent(); } };
            grid.Children.Add(_categories);
            var right = new DockPanel(); Grid.SetColumn(right, 2); grid.Children.Add(right);
            var top = new DockPanel { Margin = new Thickness(0, 0, 0, 6) };
            var place = MakeButton("Place in front of me", PlaceContent, primary: true, tip: "Drops the selected class 4 m in front of you in the running match (or double-click a tile)");
            place.Margin = new Thickness(6, 0, 0, 0);
            DockPanel.SetDock(place, Dock.Right); top.Children.Add(place);
            var snap = new CheckBox { Content = "Snap", IsChecked = _snap, Foreground = Fg, VerticalAlignment = VerticalAlignment.Center, Margin = new Thickness(10, 0, 4, 0), ToolTip = "Build pieces snap to the 512 grid and turn in 90° steps" };
            snap.Checked += (_, __) => { _snap = true; if (_placingOn) PushPlacing(true); };
            snap.Unchecked += (_, __) => { _snap = false; if (_placingOn) PushPlacing(true); };
            DockPanel.SetDock(snap, Dock.Right); top.Children.Add(snap);
            _placingBtn = MakeButton(_placingOn ? "In-game placing: ON" : "In-game placing: off", () => { if (!_placingOn && (_contentPick == null || _contentPick.StartsWith(PlotPrefix))) { Out("Pick a piece tile first: that's the piece F9 places (House Plots are dragged onto the game)."); return; } PushPlacing(!_placingOn); }, tip: "In the game: F9 places the selected piece where you aim, F10 turns it, Delete removes the piece you aim at");
            _placingBtn.Margin = new Thickness(6, 0, 0, 0);
            DockPanel.SetDock(_placingBtn, Dock.Right); top.Children.Add(_placingBtn);
            var search = Input(_contentFilter); search.ToolTip = "Search classes";
            search.TextChanged += (_, __) => { _contentFilter = search.Text; RefreshContent(); };
            top.Children.Add(search);
            DockPanel.SetDock(top, Dock.Top); right.Children.Add(top);
            _contentInfo = new TextBlock { Foreground = Muted, FontSize = 11, Margin = new Thickness(2, 6, 0, 0) };
            DockPanel.SetDock(_contentInfo, Dock.Bottom); right.Children.Add(_contentInfo);
            _tiles = new WrapPanel();
            right.Children.Add(new ScrollViewer { Content = _tiles, VerticalScrollBarVisibility = ScrollBarVisibility.Auto, Background = Field });
            RefreshContent(categoriesToo: true);
            return grid;
        }

        void RefreshContent(bool categoriesToo = false)
        {
            if (_tiles == null || _categories == null || _contentInfo == null) return;
            if (categoriesToo)
            {
                _categories.Items.Clear();
                if (_contentCategory != FavCat && _contentCategory != RecentCat && _contentCategory != PlotsCat && !_assets.ContainsKey(_contentCategory)) _contentCategory = _assets.Keys.FirstOrDefault() ?? PlotsCat;
                _categories.Items.Add(new ListBoxItem { Content = Row("", FavCat + "  (" + _layout.Favorites.Count + ")", "", Fg, true), Tag = FavCat, IsSelected = _contentCategory == FavCat });
                _categories.Items.Add(new ListBoxItem { Content = Row("", RecentCat + "  (" + _recent.Count + ")", "", Fg, true), Tag = RecentCat, IsSelected = _contentCategory == RecentCat });
                _categories.Items.Add(new ListBoxItem { Content = Row("\uE80F", PlotsCat + "  (" + PlotTiles().Count() + ")", "", Fg, true), Tag = PlotsCat, IsSelected = _contentCategory == PlotsCat });
                foreach (var (cat, list) in _assets)
                    _categories.Items.Add(new ListBoxItem { Content = Row("", cat + "  (" + list.Count + ")", "", Fg, true), Tag = cat, IsSelected = cat == _contentCategory });
            }
            _tiles.Children.Clear();
            if (_contentCategory == PlotsCat) { RenderPlotTiles(); return; }
            bool virtualCat = _contentCategory == FavCat || _contentCategory == RecentCat;
            if (_assets.Count == 0 && !virtualCat)
            {
                _contentInfo.Text = "The match server lists its placeable classes a few seconds into a match started from the Studio. Start the server to fill this.";
                return;
            }
            var list2 = _contentCategory == FavCat ? _layout.Favorites.ToList()
                      : _contentCategory == RecentCat ? _recent.ToList()
                      : _assets.TryGetValue(_contentCategory, out var l) ? l : new List<string>();
            var shown = list2.Where(n => _contentFilter.Length == 0 || n.Contains(_contentFilter, StringComparison.OrdinalIgnoreCase)).ToList();
            var glyph = _contentCategory.ToLowerInvariant() switch { "build" => "", "containers" => "", "props" => "", _ => "" };
            foreach (var name in shown.Take(400))
            {
                bool picked = name == _contentPick;
                var tile = new Border { Width = 104, Height = 104, Margin = new Thickness(4), Background = picked ? AccentDim : Raised, BorderBrush = picked ? Accent : Line, BorderThickness = new Thickness(1), CornerRadius = new CornerRadius(3), Cursor = Cursors.Hand, ToolTip = name };
                var stack = new DockPanel();
                var label = new TextBlock { Text = Short(name), Foreground = Fg, FontSize = 10.5, TextWrapping = TextWrapping.Wrap, TextAlignment = TextAlignment.Center, Margin = new Thickness(4, 0, 4, 6), MaxHeight = 30, TextTrimming = TextTrimming.CharacterEllipsis };
                DockPanel.SetDock(label, Dock.Bottom); stack.Children.Add(label);
                stack.Children.Add(new TextBlock { Text = glyph, FontFamily = Icons, FontSize = 30, Foreground = Muted, HorizontalAlignment = HorizontalAlignment.Center, VerticalAlignment = VerticalAlignment.Center });
                var n = name;
                bool fav = _layout.Favorites.Contains(n, StringComparer.OrdinalIgnoreCase);
                var star = new TextBlock { Text = "★", FontSize = 12.5, Foreground = fav ? Accent : Muted, HorizontalAlignment = HorizontalAlignment.Right, Margin = new Thickness(0, 3, 6, 0), Cursor = Cursors.Hand, ToolTip = fav ? "Unfavourite" : "Favourite (right-click the tile too)" };
                star.MouseLeftButtonDown += (_, e) => { e.Handled = true; ToggleFavorite(n); };
                DockPanel.SetDock(star, Dock.Top); stack.Children.Insert(0, star);
                tile.Child = stack;
                WireTileDrag(tile, n); // click picks it, double-click places it, drag it onto the game to place it there
                tile.MouseRightButtonUp += (_, e) => { e.Handled = true; ToggleFavorite(n); };
                _tiles.Children.Add(tile);
            }
            _contentInfo.Text = shown.Count + " item(s)" + (shown.Count > 400 ? " — showing the first 400, search to narrow" : "") + (_contentPick != null ? "   ·   selected: " + _contentPick : "") + "   ·   drag a tile onto the game to place it there";
        }

        static string Short(string cls)
        {
            var s = cls.EndsWith("_C") ? cls[..^2] : cls;
            foreach (var prefix in new[] { "PBWA_", "PBW_", "Tiered_", "BP_", "Prop_", "Athena_" }) if (s.StartsWith(prefix)) { s = s[prefix.Length..]; break; }
            return s.Replace('_', ' ');
        }

        bool _placingOn, _snap = true;
        Button? _placingBtn;
        DateTime _placingAlive;

        // The match server reads Runtime\Server\mapeditor-ingame.txt (written by the backend) and ignores it once it is
        // 15 s old, so placing turns itself off when the Studio closes; reading /live keeps it fresh.
        async void PushPlacing(bool on)
        {
            var token = DevToken();
            if (string.IsNullOrEmpty(token)) { Out("ERROR: the backend hasn't created its developer token yet — start Syndicate's local services first."); return; }
            try
            {
                var body = "{\"on\":" + (on ? "true" : "false") + ",\"class\":" + Json(_contentPick ?? "") + ",\"snap\":" + (_snap ? "true" : "false") + ",\"keys\":{\"place\":120,\"turn\":121,\"remove\":46}}";
                using var content = new StringContent(body, Encoding.UTF8, "application/json");
                using var resp = await Http.PostAsync("http://127.0.0.1:3551/tilted/mapeditor/ingame?token=" + Uri.EscapeDataString(token), content);
                if (!resp.IsSuccessStatusCode) { Out("ERROR: in-game placing — " + await resp.Content.ReadAsStringAsync()); return; }
                _placingOn = on;
                if (_placingBtn != null) _placingBtn.Content = on ? "In-game placing: ON" : "In-game placing: off";
                Out(on ? "In-game placing on: in Fortnite, F9 places " + _contentPick + " where you aim, F10 turns it, Delete removes what you aim at." : "In-game placing off.");
            }
            catch (Exception ex) { Out("ERROR: couldn't reach the backend — " + ex.Message); }
        }

        async void KeepPlacingAlive()
        {
            if (!_placingOn || DateTime.UtcNow - _placingAlive < TimeSpan.FromSeconds(5)) return;
            _placingAlive = DateTime.UtcNow;
            var token = DevToken();
            if (string.IsNullOrEmpty(token)) return;
            try { using var resp = await Http.GetAsync("http://127.0.0.1:3551/tilted/mapeditor/live?token=" + Uri.EscapeDataString(token)); } catch { }
        }

        int _placeId = 800000000 + new Random().Next(1, 50000000);
        async void PlaceContent()
        {
            if (_contentPick == null) { Out("Pick a tile in the Content Browser first."); return; }
            if (_host == null) { Out("Start the server and join the match first: pieces drop in front of you."); return; }
            // Free cam: in the middle of the view, through the map editor (undoable)
            var pick = _contentPick;
            if (await PlaceAtViewCenter(pick)) return;
            if (pick.StartsWith(PlotPrefix)) { Out(_freeCam ? "Aim the middle of the view at the ground, or drag the plot onto the game." : "Turn the free cam on and drag the plot onto the game where you want it."); return; }
            if (_pipe != null)
                PlaceContentViaPipe(pick);
            else
                MapEditorOps("{\"ops\":[{\"op\":\"here\",\"id\":" + (_placeId++) + ",\"class\":" + Json(pick) + ",\"distance\":400,\"yaw\":0}]}", "Placed " + pick + " in front of you.");
            RecordRecent(pick); RefreshContent(categoriesToo: true); // keep the Recent category current
        }

        async void PlaceContentViaPipe(string classPath)
        {
            try
            {
                // Spawn 4 m (400 UU) in front of the camera
                var cam = await _pipe!.GetCameraAsync();
                if (cam == null) { Out("ERROR: couldn't read the camera to place the actor." + PipeWhy()); return; }
                double rad = cam.Yaw * Math.PI / 180.0;
                float x = cam.X + (float)(Math.Cos(rad) * 400);
                float y = cam.Y + (float)(Math.Sin(rad) * 400);
                var idx = await _pipe.SpawnActorAsync(classPath, x, y, cam.Z);
                if (idx != null)
                {
                    _mapSession.RecordSpawn(classPath, x, y, cam.Z, 0, idx.Value);
                    UpdateMapLabel();
                    Out($"Placed {classPath} (actor {idx.Value}).");
                }
                else Out("ERROR: the game couldn't spawn that actor — the class may not exist in this build." + PipeWhy());
            }
            catch (Exception ex) { Out("ERROR: pipe spawn failed — " + ex.Message); }
        }

        // ---- Map save / load ---------------------------------------------------------------------------

        void UpdateMapLabel()
        {
            if (_mapLabel == null || _mapSession == null) return;
            if (_mapSession.ActorCount == 0)
                _mapLabel.Text = _mapSession.MapName != null ? $"({_mapSession.MapName} — empty)" : "";
            else
                _mapLabel.Text = (_mapSession.MapName ?? "Unsaved") + $" · {_mapSession.ActorCount} actor{(_mapSession.ActorCount == 1 ? "" : "s")}" + (_mapSession.IsDirty ? " *" : "");
        }

        void SaveMap()
        {
            if (_mapSession.ActorCount == 0) { Out("Nothing to save — place some actors first."); return; }
            if (_mapSession.MapName == null || _mapSession.MapName == "Untitled")
            {
                // Show a simple input dialog
                var dlg = new Window
                {
                    Title = "Save Map", Width = 360, Height = 150, WindowStartupLocation = WindowStartupLocation.CenterOwner,
                    Owner = this, ResizeMode = ResizeMode.NoResize, Background = Panel, WindowStyle = WindowStyle.ToolWindow
                };
                var col = new StackPanel { Margin = new Thickness(16) };
                col.Children.Add(new TextBlock { Text = "Map name:", Foreground = Fg, Margin = new Thickness(0, 0, 0, 6) });
                var nameBox = new TextBox { Text = _mapSession.MapName ?? "My Map", Background = Field, Foreground = Fg, BorderBrush = Line, Padding = new Thickness(6, 4, 6, 4) };
                col.Children.Add(nameBox);
                var row = new StackPanel { Orientation = Orientation.Horizontal, HorizontalAlignment = HorizontalAlignment.Right, Margin = new Thickness(0, 12, 0, 0) };
                row.Children.Add(MakeButton("Save", () => { _mapSession.MapName = nameBox.Text.Trim(); dlg.DialogResult = true; dlg.Close(); }, primary: true));
                row.Children.Add(MakeButton("Cancel", () => dlg.Close()));
                col.Children.Add(row);
                dlg.Content = col;
                nameBox.SelectAll(); nameBox.Focus();
                if (dlg.ShowDialog() != true) return;
            }
            _mapSession.BaseSeason = _build;
            try
            {
                _mapSession.Save();
                UpdateMapLabel();
            }
            catch (Exception ex) { Out("ERROR saving map: " + ex.Message); }
        }

        void ShowLoadMap()
        {
            var maps = StudioMapSession.ListMaps();
            if (maps.Count == 0) { Out("No saved maps yet. Place some actors and press Save first."); return; }

            var dlg = new Window
            {
                Title = "Load Map", Width = 440, Height = 350, WindowStartupLocation = WindowStartupLocation.CenterOwner,
                Owner = this, ResizeMode = ResizeMode.CanResize, Background = Panel, WindowStyle = WindowStyle.ToolWindow
            };
            var dock = new DockPanel { Margin = new Thickness(12) };
            var info = new TextBlock { Text = "Pick a saved map to load. Its actors will be spawned when the pipe is connected.", Foreground = Muted, TextWrapping = TextWrapping.Wrap, Margin = new Thickness(0, 0, 0, 8) };
            DockPanel.SetDock(info, Dock.Top); dock.Children.Add(info);
            var buttons = new StackPanel { Orientation = Orientation.Horizontal, HorizontalAlignment = HorizontalAlignment.Right, Margin = new Thickness(0, 8, 0, 0) };
            DockPanel.SetDock(buttons, Dock.Bottom); dock.Children.Add(buttons);

            var list = new ListBox { Background = Field, BorderBrush = Line, Foreground = Fg, ItemContainerStyle = ItemStyle() };
            foreach (var (name, path, season, actors, modified) in maps)
            {
                var item = new ListBoxItem
                {
                    Tag = path,
                    Content = new StackPanel
                    {
                        Children =
                        {
                            new TextBlock { Text = name, Foreground = Bright, FontSize = 13 },
                            new TextBlock { Text = $"{actors} actors · {(season != null ? "Season " + season + " · " : "")}{modified:MMM d, yyyy h:mm tt}", Foreground = Muted, FontSize = 11 }
                        }
                    },
                    Padding = new Thickness(8, 6, 8, 6)
                };
                list.Items.Add(item);
            }
            dock.Children.Add(list);

            var loadBtn = MakeButton("Load", () =>
            {
                if (list.SelectedItem is ListBoxItem { Tag: string p })
                {
                    try
                    {
                        _mapSession.Load(p);
                        UpdateMapLabel();
                        // If the pipe is live, replay immediately
                        if (_pipe != null) ReplayMap();
                        dlg.Close();
                    }
                    catch (Exception ex) { Out("ERROR loading map: " + ex.Message); }
                }
            }, primary: true);
            var deleteBtn = MakeButton("Delete", () =>
            {
                if (list.SelectedItem is ListBoxItem { Tag: string p })
                {
                    try { File.Delete(p); list.Items.Remove(list.SelectedItem); Out("Deleted " + Path.GetFileName(p)); }
                    catch (Exception ex) { Out("ERROR: " + ex.Message); }
                }
            });
            deleteBtn.Margin = new Thickness(8, 0, 0, 0);
            var cancelBtn = MakeButton("Cancel", () => dlg.Close());
            cancelBtn.Margin = new Thickness(8, 0, 0, 0);
            buttons.Children.Add(loadBtn); buttons.Children.Add(deleteBtn); buttons.Children.Add(cancelBtn);

            if (list.Items.Count > 0) list.SelectedIndex = 0;
            dlg.Content = dock;
            dlg.ShowDialog();
        }

        async void ReplayMap()
        {
            if (_pipe == null || _mapSession.ActorCount == 0) return;
            Out($"Replaying map \"{_mapSession.MapName}\" ({_mapSession.ActorCount} actors)…");
            try
            {
                var ok = await _mapSession.ReplayAsync(_pipe);
                UpdateMapLabel();
            }
            catch (Exception ex) { Out("ERROR replaying map: " + ex.Message); }
        }

        // ---- Controls ----------------------------------------------------------------------------------
        UIElement BuildControlsPanel()
        {
            var col = new StackPanel { Margin = new Thickness(12, 2, 12, 12) };
            void Keys(string title, params (string Key, string What)[] rows)
            {
                col.Children.Add(Section(title));
                var grid = new Grid();
                grid.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(96) }); grid.ColumnDefinitions.Add(new ColumnDefinition());
                for (int r = 0; r < rows.Length; r++)
                {
                    grid.RowDefinitions.Add(new RowDefinition { Height = GridLength.Auto });
                    var key = new Border { Background = Raised, BorderBrush = Line, BorderThickness = new Thickness(1), CornerRadius = new CornerRadius(3), Padding = new Thickness(6, 1, 6, 2), Margin = new Thickness(0, 3, 8, 3), HorizontalAlignment = HorizontalAlignment.Left, VerticalAlignment = VerticalAlignment.Top, Child = new TextBlock { Text = rows[r].Key, Foreground = Bright, FontFamily = Mono, FontSize = 11.5 } };
                    Grid.SetRow(key, r); grid.Children.Add(key);
                    var what = new TextBlock { Text = rows[r].What, Foreground = Fg, TextWrapping = TextWrapping.Wrap, Margin = new Thickness(0, 4, 0, 3) };
                    Grid.SetRow(what, r); Grid.SetColumn(what, 1); grid.Children.Add(what);
                }
                col.Children.Add(grid);
            }
            col.Children.Add(Note("Studio match (Session Settings > Startup) gives an empty island with no bots. The storm is paused by default (Sandbox > Pause storm)."));
            Keys("Studio", ("Start", "Starts the match server and joins with your game; it docks in the Server viewport"),
                ("Click game", "Keyboard and mouse go to Fortnite (or press Click into game)"), ("Alt+Tab", "Back to the Studio"),
                ("▶ Play here", "Drop from fly mode into gameplay at the camera position"), ("■ Stop", "Return to fly mode"));
            Keys("Moving around (in the game)", ("F8", "Fly on / off (on by default in the Studio)"), ("W A S D", "Move"), ("Space", "Fly up"), ("Ctrl / C", "Fly down"),
                ("Mouse", "Look"), ("Speed", "The slider above the viewport (up to 300 m/s)"));
            Keys("Moving the map's own objects (Alt+E)", ("Aim", "At a wall, house piece, tree, rock, car or chest"), ("Insert", "Grab it: it follows your aim"),
                ("F9 / Insert", "Drop it"), ("F10", "Turn it 90° while holding"), ("PgUp/PgDn", "Raise / lower while holding"), ("Delete", "Remove it"),
                ("Note", "Changes to the map's own objects last for this match; pieces you place are saved"));
            Keys("Placing (Content Browser)", ("Tile", "Pick a piece, then press In-game placing"), ("F9", "Place it where you aim"), ("F10", "Turn it 90°"),
                ("Delete", "Remove the piece you aim at"), ("Dbl-click", "Drop a tile 4 m in front of you"));
            Keys("In-game editor (Alt+E)", ("Alt+E", "Editor on / off"), ("Alt+1..9", "Pick a piece from the hotbar"), ("F9", "Place (a preview shows where)"),
                ("Insert", "Grab the piece you aim at; Insert or F9 drops it"), ("F10", "Turn 90°"), ("PgUp/PgDn", "Raise / lower"), ("Delete", "Remove"),
                ("Alt+C", "Copy the piece you aim at"), ("Alt+Z", "Undo"), ("Alt+G", "Grid snap on / off"), ("Alt+S", "Save now (it also saves by itself)"));
            Keys("Free cam (Free cam: on above the viewport)", ("Right mouse", "Hold over the game to look around"), ("W A S D", "Move (while holding the right mouse button or with the cursor over the game)"),
                ("Space / E", "Up"), ("C / Q", "Down"), ("Shift", "Faster"), ("Wheel", "Slower / faster"),
                ("Left click", "Select the piece under the cursor (Shift+click adds or removes one)"), ("Drag", "Selection box: selects every piece inside (Shift adds, Alt includes everything)"),
                ("Arrows", "Drag the arrows to move the selection; it snaps to the Grid in the toolbar"), ("R", "Turn the selection 90° (Shift+R the other way)"),
                ("← ↑ → ↓", "Nudge the selection one grid step"), ("PgUp/PgDn", "Raise / lower one step"), ("Delete", "Delete the selection"),
                ("Ctrl+Z / Y", "Undo / redo (also the toolbar buttons)"), ("Esc", "Select nothing"), ("Note", "The game's HUD is hidden and it ignores your keys and clicks while the free cam is on"));
            Keys("House Plots (Content Browser)", ("Copy", "Free cam: left click a piece of a real house, Details > Select whole house, then Save as House Plot"),
                ("Drag", "A plot onto the game: the same house goes there, facing you"), ("Dbl-click", "Place it in the middle of the view (free cam)"), ("Right-click", "Delete one of your own plots"));
            Keys("Outliner and Details", ("Select", "A player, bot or placed piece in the Outliner"), ("Apply", "Move a placed piece to the X / Y / Z / Yaw you typed"),
                ("Teleport", "Puts you next to a player or bot"));
            return Scroll(col);
        }

        // ---- Sandbox panel -----------------------------------------------------------------------------
        // Dev cheats (flag files polled by the match server every 250 ms) and dev commands (written to
        // Runtime\Server\dev-command.txt).
        CheckBox? _chkStormPaused, _chkInfAmmo, _chkNoDrop, _chkMagicBullet;

        UIElement BuildSandboxPanel()
        {
            var col = new StackPanel { Margin = new Thickness(12, 2, 12, 12) };

            // ---- Storm ----
            col.Children.Add(Section("Storm"));
            // Default storm paused in Studio — set the flag file now if it doesn't exist yet
            if (!File.Exists(DevFlag("dev-storm-paused.txt"))) SetDevCheat("dev-storm-paused.txt", true);
            _chkStormPaused = new CheckBox { Content = "Pause storm", IsChecked = File.Exists(DevFlag("dev-storm-paused.txt")), Foreground = Fg, Margin = new Thickness(0, 0, 0, 4) };
            _chkStormPaused.Checked += (_, __) => SetDevCheat("dev-storm-paused.txt", true);
            _chkStormPaused.Unchecked += (_, __) => SetDevCheat("dev-storm-paused.txt", false);
            col.Children.Add(_chkStormPaused);
            col.Children.Add(Note("Freezes the safe zone so the storm never closes. On by default in Studio."));

            // ---- Dev cheats (flag files) ----
            col.Children.Add(Section("Dev cheats"));
            col.Children.Add(Note("Toggles take effect within 250 ms. The match server reads these while TILTED_DEV_LIVE=1 (Studio sets it)."));

            _chkInfAmmo = new CheckBox { Content = "Infinite ammo", IsChecked = File.Exists(DevFlag("dev-infinite-ammo.txt")), Foreground = Fg, Margin = new Thickness(0, 0, 0, 4) };
            _chkInfAmmo.Checked += (_, __) => SetDevCheat("dev-infinite-ammo.txt", true);
            _chkInfAmmo.Unchecked += (_, __) => SetDevCheat("dev-infinite-ammo.txt", false);
            col.Children.Add(_chkInfAmmo);

            _chkNoDrop = new CheckBox { Content = "No bullet drop", IsChecked = File.Exists(DevFlag("dev-no-drop.txt")), Foreground = Fg, Margin = new Thickness(0, 0, 0, 4) };
            _chkNoDrop.Checked += (_, __) => SetDevCheat("dev-no-drop.txt", true);
            _chkNoDrop.Unchecked += (_, __) => SetDevCheat("dev-no-drop.txt", false);
            col.Children.Add(_chkNoDrop);

            _chkMagicBullet = new CheckBox { Content = "Magic bullet (bot aimbot)", IsChecked = File.Exists(DevFlag("dev-magic-bullet.txt")), Foreground = Fg, Margin = new Thickness(0, 0, 0, 4) };
            _chkMagicBullet.Checked += (_, __) => SetDevCheat("dev-magic-bullet.txt", true);
            _chkMagicBullet.Unchecked += (_, __) => SetDevCheat("dev-magic-bullet.txt", false);
            col.Children.Add(_chkMagicBullet);

            // ---- Quick actions ----
            col.Children.Add(Section("Quick actions"));
            var actRow = new WrapPanel { Margin = new Thickness(0, 0, 0, 2) };
            actRow.Children.Add(MakeButton("Thanos", () => DevCommand("thanos", ""), false, "Kill half the bots"));
            actRow.Children.Add(MakeButton("Win (kill all)", () => DevCommand("win", ""), false, "Eliminate every bot — you win"));
            actRow.Children.Add(MakeButton("Bot report", () => DevCommand("botreport", ""), false, "Summary of every bot's brain (stuck bots, gunfire heard, reaction times); full lines go to the bot log"));
            col.Children.Add(actRow);

            // ---- Teleport / kill target ----
            col.Children.Add(Section("Target command"));
            var cmdRow = new DockPanel { Margin = new Thickness(0, 0, 0, 2) };
            var targetBox = Input("", 120); targetBox.ToolTip = "Bot name or * for all";
            DockPanel.SetDock(targetBox, Dock.Left); cmdRow.Children.Add(targetBox);
            var cmdBtns = new WrapPanel { Margin = new Thickness(8, 0, 0, 0) };
            cmdBtns.Children.Add(MakeButton("TP to", () => { if (targetBox.Text.Trim().Length > 0) DevCommand("tp", targetBox.Text.Trim()); }, false, "Teleport the target to your position"));
            cmdBtns.Children.Add(MakeButton("Kill", () => { if (targetBox.Text.Trim().Length > 0) DevCommand("kill", targetBox.Text.Trim()); }, false, "Eliminate the target"));
            cmdBtns.Children.Add(MakeButton("Brain", () => DevCommand("botinfo", targetBox.Text.Trim()), false, "Show the target bot's brain (empty = the bot nearest you)"));
            cmdRow.Children.Add(cmdBtns);
            col.Children.Add(cmdRow);
            col.Children.Add(Note("Enter a bot name from the Outliner, or * for all bots."));

            return Scroll(col);
        }

        void SetDevCheat(string flagFile, bool on)
        {
            try
            {
                var path = DevFlag(flagFile);
                if (on)
                {
                    Directory.CreateDirectory(Path.GetDirectoryName(path)!);
                    File.WriteAllText(path, "1");
                    // Dev cheats need the dev console flag
                    if (!File.Exists(DevFlag("dev-console-on.txt"))) File.WriteAllText(DevFlag("dev-console-on.txt"), "1");
                }
                else if (File.Exists(path)) File.Delete(path);
                Out($"{flagFile}: {(on ? "on" : "off")}");
            }
            catch (Exception ex) { Out("ERROR: " + ex.Message); }
        }

        // ---- Server viewport ---------------------------------------------------------------------------
        // Your rendering game (the client that joined this server) docked over the panel: a frameless window owned
        // by the Studio window and kept on the panel's screen rectangle. The match server itself runs headless, so
        // the client is what can be shown; with the free camera on it flies over the island with no character.
        IntPtr _self, _game;
        Grid? _viewArea;
        TextBlock? _viewHeadline, _viewSub, _viewState;
        Button? _viewDockBtn, _viewMuteBtn, _viewFlyBtn, _playtestBtn, _freeCamBtn;
        bool _freeCam;
        System.Windows.Controls.ComboBox? _locationBox;
        bool _popupOpen;
        bool _muted;
        bool _viewAttaching;

        // ---- Named map locations per build ----
        // Chapter 1 kept one coordinate system (X = north, Y = east). Exact town centers come from the bots' hot-drop
        // table (SpectraBotsIntegration.h SeasonHotTowns), which was measured from each town's chests. Names marked
        // "~" are approximate (no measured center yet). Every teleport lands on the ground: the server traces down
        // from high above (tppos x,y,z,1), so an approximate point still puts you on the island, not in the air.
        // MinVer/MaxVer: the Fortnite versions the town exists in (MaxVer exclusive).
        static readonly (string Name, float X, float Y, float MinVer, float MaxVer)[] AllLocations = {
            ("Island center",       5000,      0,  0f, 11f),
            ("Pleasant Park",      85000, -83500,  0f, 11f),
            ("Salty Springs",       4400,  -9200,  0f, 11f),
            ("Fatal Fields",      -34000,   1500,  0f, 11f),
            ("Loot Lake",          65300, -43300,  0f, 11f),
            ("Retail Row",         25000,  38000,  0f,  9f),
            ("Retail Row",         25000,  38000, 10f, 11f),
            ("Mega Mall",          25000,  38000,  9f, 10f),
            ("Greasy Grove",        1200, -95000,  0f,  7f),
            ("Greasy Grove",        1200, -95000,  9f, 11f),
            ("Dusty Depot",        16000,   7600,  0f,  4f),
            ("Dusty Divot",        16000,   7600,  4f, 11f),
            ("Tilted Towers",      34300, -58500,  3f,  9f),
            ("Neo Tilted",         34300, -58500,  9f, 10f),
            ("Tilted Town",        34300, -58500, 10f, 11f),
            ("Lazy Links",         97000,      0,  5f,  8f),
            ("Lazy Lagoon",        97000,      0,  8f,  9f),
            ("Paradise Palms",    -27400,  52400,  5f, 11f),
            ("Happy Hamlet",      -59500, -71800,  7f, 11f),
            ("Polar Peak",        -21200, -90100,  7f, 11f),
            ("~Anarchy Acres",    105000,  15000,  0f,  5f),
            ("~Tomato Town",       85000,  40000,  0f,  9f),
            ("~Wailing Woods",     80000,  95000,  0f, 11f),
            ("~Lonely Lodge",      40000, 105000,  0f, 11f),
            ("~Moisty Mire",      -60000,  85000,  0f,  6f),
            ("~Lucky Landing",   -105000,  10000,  0f, 11f),
            ("~Flush Factory",    -85000, -40000,  0f,  7f),
            ("~Snobby Shores",     35000,-118000,  0f, 11f),
            ("~Haunted Hills",     95000,-115000,  2f, 11f),
            ("~Junk Junction",    115000,-100000,  2f, 11f),
            ("~Shifty Shafts",     -5000, -55000,  2f, 11f),
        };

        static (string Name, float X, float Y)[] GetLocationsForVersion(float ver)
        {
            var list = new List<(string Name, float X, float Y)>();
            foreach (var l in AllLocations)
                if (ver >= l.MinVer && ver < l.MaxVer && !list.Exists(e => e.Name == l.Name)) list.Add((l.Name, l.X, l.Y));
            // Island center first, then measured towns A-Z, then approximate ones A-Z
            var first = list[0];
            var rest = list.Skip(1).OrderBy(e => e.Name.StartsWith("~") ? 1 : 0).ThenBy(e => e.Name.TrimStart('~'), StringComparer.Ordinal).ToList();
            rest.Insert(0, first);
            return rest.ToArray();
        }
        string _stateNow = "Stopped";

        UIElement BuildViewportPanel()
        {
            var dock = new DockPanel();
            var strip = new DockPanel { Background = TabBar, Height = 30, LastChildFill = false };
            _viewState = new TextBlock { Foreground = Accent, FontSize = 11.5, VerticalAlignment = VerticalAlignment.Center, Margin = new Thickness(0, 0, 10, 0) };
            DockPanel.SetDock(_viewState, Dock.Right); strip.Children.Add(_viewState);
            Button Small(string text, Action click, string tip)
            {
                var b = MakeButton(text, click, tip: tip); b.Height = 22; b.Padding = new Thickness(8, 0, 8, 0); b.Margin = new Thickness(4, 0, 0, 0); b.FontSize = 11.5;
                return b;
            }
            strip.Children.Add(Small("Click into game", () => { var w = ViewOwner(); if (NativeEmbed.Alive(_game) && w != IntPtr.Zero) NativeEmbed.FocusGame(_game, w); }, "Give the game your keyboard and mouse (Alt+Tab or click Studio to get them back)"));
            _viewFlyBtn = Small(_layout.Fly ? "Fly: on" : "Fly: off", () => { _layout.Fly = !_layout.Fly; SaveLayout(); if (NativeEmbed.Alive(_game)) SetFly(_layout.Fly); _viewFlyBtn!.Content = _layout.Fly ? "Fly: on" : "Fly: off"; }, "Fly through walls and the ground: WASD, Space up, Ctrl or C down (F8 in the game)");
            strip.Children.Add(_viewFlyBtn);
            _freeCamBtn = Small("Free cam: off", ToggleFreeCam, "Detach the camera from your character, like an editor viewport: hold the right mouse button in the game to look, WASD to move, Space/E up, C/Q down, Shift faster, mouse wheel slower/faster. Left click selects a piece, drag on an empty spot to box-select. The HUD is hidden and the game ignores your keys. Play here drops you where the camera is.");
            strip.Children.Add(_freeCamBtn);
            _gizmoBtn = Small(_gizmoOn ? "Gizmo: on" : "Gizmo: off", () => { _gizmoOn = !_gizmoOn; _gizmoBtn!.Content = _gizmoOn ? "Gizmo: on" : "Gizmo: off"; if (!_gizmoOn) _gizmo?.Hide(); else GizmoTick(); }, "Show translation gizmo arrows over the selected actor — drag an arrow to move it");
            strip.Children.Add(_gizmoBtn);
            // Playtest: drop from free-cam into gameplay at the camera position
            strip.Children.Add(new Border { Width = 1, Background = Line, Margin = new Thickness(6, 4, 2, 4) }); // separator
            _playtestBtn = Small("▶ Play here", StartPlaytest, "Drop into gameplay at the camera position — press Stop to fly again");
            strip.Children.Add(_playtestBtn);
            // Teleport to named locations on the Athena island
            strip.Children.Add(new Border { Width = 1, Background = Line, Margin = new Thickness(6, 4, 2, 4) }); // separator
            var goLabel = new TextBlock { Text = "Go to:", Foreground = Muted, FontSize = 11.5, VerticalAlignment = VerticalAlignment.Center, Margin = new Thickness(4, 0, 2, 0) };
            strip.Children.Add(goLabel);
            _locationBox = new System.Windows.Controls.ComboBox { Width = 130, Height = 22, FontSize = 11, VerticalAlignment = VerticalAlignment.Center, Margin = new Thickness(2, 0, 0, 0), ToolTip = "Pick a named location on the Athena island" };
            PopulateLocations();
            // The docked game is a window owned by the Studio, so it sits above the Studio's own popups: the list
            // would open behind it. Hide the game while the list is open.
            _locationBox.DropDownOpened += (_, __) => { _popupOpen = true; PlaceView(); };
            _locationBox.DropDownClosed += (_, __) => { _popupOpen = false; PlaceView(); };
            strip.Children.Add(_locationBox);
            strip.Children.Add(Small("Go", TeleportToLocation, "Teleport your player and camera to the selected location"));
            // Map save/load buttons
            strip.Children.Add(new Border { Width = 1, Background = Line, Margin = new Thickness(6, 4, 2, 4) }); // separator
            strip.Children.Add(Small("Save map", SaveMap, "Save all placed actors to a .syndicate-map file"));
            strip.Children.Add(Small("Load map", ShowLoadMap, "Load a saved map and replay its actors"));
            strip.Children.Add(Small("Clear map", () => { _mapSession.Clear(); Out("Map cleared — all tracked placements removed."); UpdateMapLabel(); }, "Remove all tracked placements from this session"));
            _mapLabel = new TextBlock { Foreground = Muted, FontSize = 10.5, VerticalAlignment = VerticalAlignment.Center, Margin = new Thickness(6, 0, 0, 0) };
            strip.Children.Add(_mapLabel);
            UpdateMapLabel();
            strip.Children.Add(new Border { Width = 1, Background = Line, Margin = new Thickness(6, 4, 2, 4) }); // separator
            var speedLabel = new TextBlock { Text = "Speed", Foreground = Muted, FontSize = 11.5, VerticalAlignment = VerticalAlignment.Center, Margin = new Thickness(10, 0, 4, 0) };
            strip.Children.Add(speedLabel);
            var speed = new Slider { Minimum = 1000, Maximum = 120000, Value = _layout.FlySpeed, Width = 120, VerticalAlignment = VerticalAlignment.Center, ToolTip = "Fly speed (applies while you fly)", IsSnapToTickEnabled = true, TickFrequency = 1000 };
            speed.ValueChanged += (_, e) => { _layout.FlySpeed = (int)e.NewValue; speedLabel.Text = "Speed " + (_layout.FlySpeed / 100) + " m/s"; };
            speed.PreviewMouseUp += (_, __) => { WriteFlySpeed(); SaveLayout(); };
            speed.KeyUp += (_, __) => { WriteFlySpeed(); SaveLayout(); };
            speedLabel.Text = "Speed " + (_layout.FlySpeed / 100) + " m/s";
            strip.Children.Add(speed);
            _viewDockBtn = Small(_layout.AutoEmbed ? "Undock" : "Dock", () => { _layout.AutoEmbed = !_layout.AutoEmbed; SaveLayout(); if (!_layout.AutoEmbed) ReleaseView(); _viewDockBtn!.Content = _layout.AutoEmbed ? "Undock" : "Dock"; }, "Dock your game in this panel, or give it back its own window");
            strip.Children.Add(_viewDockBtn);
            _viewMuteBtn = Small(_muted ? "Unmute game" : "Mute game", ToggleMute, "Mute or unmute Fortnite's sound");
            strip.Children.Add(_viewMuteBtn);
            DockPanel.SetDock(strip, Dock.Top); dock.Children.Add(strip);

            _viewArea = new Grid { Background = new LinearGradientBrush((Color)ColorConverter.ConvertFromString(Slate ? "#243049" : "#2B313C"), (Color)ColorConverter.ConvertFromString(Slate ? "#0E141E" : "#161A20"), 90), ClipToBounds = true };
            var grid = new Canvas { Width = 1000, Height = 560 };
            var line = B(Slate ? "#33405A" : "#3A4250");
            for (int i = -14; i <= 14; i++) grid.Children.Add(new System.Windows.Shapes.Line { X1 = 500 + i * 22, Y1 = 290, X2 = 500 + i * 150, Y2 = 560, Stroke = line, StrokeThickness = 0.8 });
            for (int k = 0; k < 12; k++) { double y = 290 + 270 * Math.Pow(k / 11.0, 1.8); grid.Children.Add(new System.Windows.Shapes.Line { X1 = 0, Y1 = y, X2 = 1000, Y2 = y, Stroke = line, StrokeThickness = 0.8 }); }
            _viewArea.Children.Add(new Viewbox { Child = grid, Stretch = Stretch.Fill });
            var message = new StackPanel { VerticalAlignment = VerticalAlignment.Center, HorizontalAlignment = HorizontalAlignment.Center };
            _viewHeadline = new TextBlock { Foreground = B("#C8CDD6"), FontSize = 17, HorizontalAlignment = HorizontalAlignment.Center };
            _viewSub = new TextBlock { Foreground = B("#A9B0BC"), FontSize = 14, HorizontalAlignment = HorizontalAlignment.Center, Margin = new Thickness(0, 14, 0, 0), TextAlignment = TextAlignment.Center, TextWrapping = TextWrapping.Wrap, MaxWidth = 560 };
            message.Children.Add(_viewHeadline); message.Children.Add(_viewSub);
            _viewArea.Children.Add(message);
            _viewArea.SizeChanged += (_, __) => PlaceView();
            _viewArea.IsVisibleChanged += (_, __) => Dispatcher.BeginInvoke(new Action(PlaceView));
            dock.Children.Add(_viewArea);
            ViewTick();
            return dock;
        }

        IntPtr ViewOwner()
        {
            var w = _viewArea == null ? null : Window.GetWindow(_viewArea);
            return w == null ? IntPtr.Zero : new System.Windows.Interop.WindowInteropHelper(w).Handle;
        }

        void ViewTick()
        {
            if (_viewState != null) _viewState.Text = "Server • " + _stateNow;
            if (NativeEmbed.Alive(_game) && Process.GetProcessesByName("FortniteClient-Win64-Shipping").All(p => p.Id != NativeEmbed.PidOf(_game)))
            {
                _game = IntPtr.Zero;
                // Game process is gone — dispose the old pipe so a new injection can happen when the next game starts
                try { _pipe?.Dispose(); } catch { } _pipe = null;
                _freeCam = false; if (_freeCamBtn != null) _freeCamBtn.Content = "Free cam: off";
                _gizmo?.Hide();
            }
            bool docked = NativeEmbed.Alive(_game);
            var paths = _getPaths();
            bool client = StudioSession.ClientRunning();
            if (_viewHeadline != null && _viewSub != null)
            {
                (string head, string sub) = docked ? ("", "")
                    : _starting ? ("STARTING ENGINE", "The game viewport will appear here.")
                    : _host == null ? ("NO WORLD LOADED", "Press Start to launch the server and open the viewport.")
                    : !_layout.AutoEmbed ? ("VIEWPORT UNDOCKED", "Press Dock to show your game here.")
                    : client ? ("WAITING FOR YOUR GAME", "It docks here as soon as it renders. Fortnite must be set to Windowed.")
                    : ("SERVER READY", "Your game docks here once it renders.");
                _viewHeadline.Text = head; _viewSub.Text = sub;
            }
            if (!docked && _layout.AutoEmbed && client && !_viewAttaching && _viewArea != null && _viewArea.IsVisible) AttachView();
            PlaceView();
        }

        void AttachView()
        {
            _viewAttaching = true;
            Task.Run(() =>
            {
                // The rendering client only: the hidden -nullrhi match server has no window worth showing.
                foreach (var p in Process.GetProcessesByName("FortniteClient-Win64-Shipping")) { var h = NativeEmbed.FindRenderingWindow(p.Id); if (h != IntPtr.Zero) return (h, p.Id); }
                return (IntPtr.Zero, 0);
            }).ContinueWith(t => UI(() =>
            {
                _viewAttaching = false;
                var (h, gamePid) = t.IsCompletedSuccessfully ? t.Result : (IntPtr.Zero, 0);
                if (h == IntPtr.Zero || _closing || !_layout.AutoEmbed) return;
                try
                {
                    NativeEmbed.AttachOverlay(h);
                    NativeEmbed.SetOwner(h, ViewOwner());
                    _game = h;
                    if (_layout.Fly) SetFly(true);
                    Out("Your game is docked in the Server viewport. Click into game (or click it) to fly; Alt+Tab to come back.");
                    PlaceView();
                    // Load SyndicateStudio.dll for pipe-based scene access
                    InjectStudioDll(gamePid);
                }
                catch (Exception ex) { Out("ERROR: couldn't dock the game — " + ex.Message); }
            }));
        }

        async void InjectStudioDll(int gamePid)
        {
            if (_pipeInjecting || _pipe != null || gamePid == 0) return;
            _pipeInjecting = true;
            try
            {
                var paths = _getPaths();
                if (paths == null) return;
                // SyndicateStudio.dll: UPDATE FROM GIT builds it from SyndicateClassic\\game-server\\SyndicateStudio into Runtime\\dlls.
                string dllSrc = StudioSession.StudioModule;
                if (!File.Exists(dllSrc)) { Out("SyndicateStudio.dll not found in Runtime\\dlls — pipe features unavailable (run UPDATE FROM GIT with Visual Studio's C++ tools installed). Outliner and Details still work from the match server's files."); return; }
                if (SyndicateLauncher.Services.ModuleCheck.Diagnose(dllSrc) is { } blocked) { Out("SyndicateStudio.dll can't be used: " + blocked.Title + " " + string.Join(" ", blocked.Steps)); return; }
                // Load a copy named by its hash, so UPDATE FROM GIT can replace Runtime\\dlls\\SyndicateStudio.dll while a game has it loaded.
                var modsDir = StudioSession.ModuleCopyFolder;
                Directory.CreateDirectory(modsDir);
                var hash = Convert.ToHexString(System.Security.Cryptography.SHA256.HashData(File.ReadAllBytes(dllSrc)))[..16];
                var dllDest = Path.Combine(modsDir, "SyndicateStudio-" + hash + ".dll");
                if (!File.Exists(dllDest)) File.Copy(dllSrc, dllDest);

                Out("Loading SyndicateStudio.dll into your game…");
                var pipe = await StudioPipeClient.InjectAndConnectAsync(gamePid, dllDest, s => UI(() => Out(s)));
                if (pipe != null)
                {
                    _pipe = pipe;
                    var ver = await pipe.GetVersionAsync();
                    Out($"SyndicateStudio pipe connected (v{ver:0.0#}).");
                    // Create the gizmo overlay now that we have a pipe
                    if (_gizmo != null) _gizmo.Pipe = pipe; // a restarted game has a new pipe
                    if (_gizmo == null)
                    {
                        _gizmo = new StudioGizmoOverlay(pipe, s => UI(() => Out(s)));
                        // Arrow drags: snapped to the grid, the rest of the selection follows, and Ctrl+Z undoes it
                        // (the server owns map pieces, so it is moved there too, like Apply)
                        _gizmo.DragStarted += (idx, x, y, z) => UI(() => OnGizmoDragStarted(idx, x, y, z));
                        _gizmo.DragCompleted += (idx, x, y, z) => UI(() => OnGizmoDragCompleted(idx, x, y, z));
                        PlaceView(); // position the gizmo over the viewport
                    }
                    // If a map is loaded, replay it into the new match
                    if (_mapSession.ActorCount > 0) ReplayMap();
                }
                else
                {
                    Out("SyndicateStudio.dll loaded but the pipe didn't connect — falling back to file-based mode (details in %LOCALAPPDATA%\\ProjectTilted\\syndicate-studio.log).");
                }
            }
            catch (Exception ex) { Out("SyndicateStudio inject failed: " + ex.Message + " — falling back to file-based mode."); }
            finally { _pipeInjecting = false; }
        }

        void PlaceView()
        {
            if (!NativeEmbed.Alive(_game)) return;
            var win = _viewArea == null ? null : Window.GetWindow(_viewArea);
            bool show = !_popupOpen && win != null && _viewArea!.IsVisible && win.WindowState != WindowState.Minimized && _viewArea.ActualWidth > 20 && _viewArea.ActualHeight > 20;
            if (!show) { NativeEmbed.SetShown(_game, false); _gizmo?.Hide(); return; }
            NativeEmbed.SetOwner(_game, new System.Windows.Interop.WindowInteropHelper(win!).Handle);
            var p = _viewArea!.PointToScreen(new Point(0, 0));
            var dpi = VisualTreeHelper.GetDpi(_viewArea);
            int sx = (int)Math.Round(p.X), sy = (int)Math.Round(p.Y);
            int sw = (int)Math.Round(_viewArea.ActualWidth * dpi.DpiScaleX), sh = (int)Math.Round(_viewArea.ActualHeight * dpi.DpiScaleY);
            NativeEmbed.PlaceOwned(_game, sx, sy, sw, sh);
            NativeEmbed.SetShown(_game, true);
            _gizmo?.AttachToGame(_game);
            // Keep the gizmo overlay positioned and sized to match the game viewport
            _gizmo?.Reposition(sx, sy, sw, sh);
        }

        void ReleaseView()
        {
            if (NativeEmbed.Alive(_game)) { NativeEmbed.SetOwner(_game, IntPtr.Zero); NativeEmbed.SetShown(_game, true); NativeEmbed.Release(_game); Out("Your game has its own window again."); }
            _game = IntPtr.Zero;
            _gizmo?.Hide();
            try { _pipe?.Dispose(); } catch { } _pipe = null; // allow re-injection on next attach
            _freeCam = false; if (_freeCamBtn != null) _freeCamBtn.Content = "Free cam: off";
            if (_flySet) SetFly(false);
        }

        // Project the selected actor's position to screen coordinates and update the gizmo overlay.
        async void GizmoTick()
        {
            if (_gizmo == null || !_gizmoOn || _pipe == null || _gizmo.IsDragging) return;
            if (_selected == null || !_selected.StartsWith("actor:") || !_items.TryGetValue(_selected, out var item))
            { _gizmo.Hide(); return; }
            if (!uint.TryParse(item.Props.GetValueOrDefault("id", ""), out var idx))
            { _gizmo.Hide(); return; }
            if (!float.TryParse(item.Props.GetValueOrDefault("x", ""), System.Globalization.NumberStyles.Float, System.Globalization.CultureInfo.InvariantCulture, out var ax)
             || !float.TryParse(item.Props.GetValueOrDefault("y", ""), System.Globalization.NumberStyles.Float, System.Globalization.CultureInfo.InvariantCulture, out var ay)
             || !float.TryParse(item.Props.GetValueOrDefault("z", ""), System.Globalization.NumberStyles.Float, System.Globalization.CultureInfo.InvariantCulture, out var az))
            { _gizmo.Hide(); return; }
            // The overlay projects the actor itself at ~30 Hz (position + real axis directions); just say which one
            _gizmo.Pipe = _pipe;
            _gizmo.Track(idx, ax, ay, az);
            // The other selected pieces get a marker
            var others = new List<(float X, float Y, float Z)>();
            foreach (var other in SelectedActors())
                if (other.Key != _selected && ActorPos(other, out var ox, out var oy, out var oz, out _)) others.Add((ox, oy, oz));
            _gizmo.SetMarkers(NativeEmbed.Alive(_game) && !_popupOpen ? others : new List<(float X, float Y, float Z)>());
            await Task.CompletedTask;
        }

        void ToggleMute()
        {
            int pid = NativeEmbed.PidOf(_game);
            if (pid == 0) foreach (var p in Process.GetProcessesByName("FortniteClient-Win64-Shipping")) if (NativeEmbed.FindRenderingWindow(p.Id) != IntPtr.Zero) { pid = p.Id; break; }
            if (pid == 0) { Out("Fortnite isn't running yet."); return; }
            bool want = !_muted;
            Task.Run(() => ProcessAudio.SetMuted(pid, want)).ContinueWith(t => UI(() =>
            {
                if (t.IsCompletedSuccessfully && t.Result) { _muted = want; if (_viewMuteBtn != null) _viewMuteBtn.Content = _muted ? "Unmute game" : "Mute game"; }
                else Out("Couldn't reach Fortnite's sound yet.");
            }));
        }

        // Fly (dev-fly-on.txt): the match server lets you fly through walls and the ground, SyndicateClient adds Space up /
        // Ctrl or C down; the client reads it only while the Dev Console switch (dev-console-on.txt) is on.
        bool _flySet;
        static string DevFlag(string name) => Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "ProjectTilted", name);
        void SetFly(bool on)
        {
            try
            {
                if (on) { Directory.CreateDirectory(Path.GetDirectoryName(DevFlag("dev-fly-on.txt"))!); File.WriteAllText(DevFlag("dev-fly-on.txt"), "1"); if (!File.Exists(DevFlag("dev-console-on.txt"))) File.WriteAllText(DevFlag("dev-console-on.txt"), "1"); }
                else if (File.Exists(DevFlag("dev-fly-on.txt"))) File.Delete(DevFlag("dev-fly-on.txt"));
                _flySet = on;
            }
            catch (Exception ex) { Out("ERROR: fly switch — " + ex.Message); }
        }

        void WriteFlySpeed()
        {
            try { Directory.CreateDirectory(Path.GetDirectoryName(DevFlag("dev-fly-speed.txt"))!); File.WriteAllText(DevFlag("dev-fly-speed.txt"), Math.Clamp(_layout.FlySpeed, 1000, 120000).ToString(System.Globalization.CultureInfo.InvariantCulture)); }
            catch (Exception ex) { Out("ERROR: fly speed — " + ex.Message); }
        }

        // ---- Playtest (Phase 4) -----------------------------------------------------------------------
        // Drop from the free-cam into gameplay at the camera position, then return to flying.
        bool _playtesting;

        // Studio fly moves your real character (server-side noclip), so "Play here" is simply: stop flying right
        // where you are and focus the game. No camera read is needed, so it works even while the pipe reconnects.
        // (It used to teleport the character to the camera, which for a third-person view is behind and above you.)
        // Play here: with the free cam on, your character is dropped on the ground under the camera and you play
        // from there; otherwise Studio fly moves your real character, so it simply stops flying where you are.
        async void StartPlaytest()
        {
            if (_playtesting) { StopPlaytest(); return; }
            if (_host == null) { Out("Start the match and join it first."); return; }
            if (_freeCam && _pipe != null)
            {
                var cam = await _pipe.GetCameraAsync();
                if (cam != null)
                    DevCommand("tppos", FormattableString.Invariant($"{cam.X:F0},{cam.Y:F0},{cam.Z:F0},1"));
                else Out("Couldn't read the free cam position" + PipeWhy() + " — playing from where your character is.");
                await SetFreeCam(false);
            }
            _layout.Fly = false; SaveLayout(); SetFly(false);
            if (_viewFlyBtn != null) _viewFlyBtn.Content = "Fly: off";
            _playtesting = true;
            if (_playtestBtn != null) _playtestBtn.Content = "■ Stop";
            Out("Playtest started. Press Stop to fly again.");
            var w = ViewOwner();
            if (NativeEmbed.Alive(_game) && w != IntPtr.Zero) NativeEmbed.FocusGame(_game, w);
        }

        async void ToggleFreeCam() => await SetFreeCam(!_freeCam);

        async Task SetFreeCam(bool on)
        {
            if (_pipe == null) { Out("The free cam needs the Studio pipe: start the match and let your game dock first."); return; }
            var ok = await _pipe.SetFreeCamAsync(on);
            if (!ok) { Out("ERROR: the free cam didn't " + (on ? "start" : "stop") + PipeWhy() + " (details in %LOCALAPPDATA%\\ProjectTilted\\syndicate-studio.log)."); return; }
            _freeCam = on;
            if (on) _freeCamOnAt = DateTime.UtcNow;
            if (_freeCamBtn != null) _freeCamBtn.Content = on ? "Free cam: on" : "Free cam: off";
            if (on)
            {
                Out("Free cam on — hold the RIGHT mouse button over the game to look, WASD to move (Space/E up, C/Q down, Shift faster, wheel slower/faster). Left click selects a piece, drag on an empty spot to box-select, R turns, Delete removes, Ctrl+Z undoes.");
                var w = ViewOwner();
                if (NativeEmbed.Alive(_game) && w != IntPtr.Zero) NativeEmbed.FocusGame(_game, w);
            }
            else Out("Free cam off — back to your character.");
        }

        void StopPlaytest()
        {
            // Return to fly mode
            _layout.Fly = true; SaveLayout(); SetFly(true);
            if (_viewFlyBtn != null) _viewFlyBtn.Content = "Fly: on";
            _playtesting = false;
            if (_playtestBtn != null) _playtestBtn.Content = "▶ Play here";
            Out("Playtest stopped — back to fly mode.");
        }

        void PopulateLocations()
        {
            if (_locationBox == null) return;
            _locationBox.Items.Clear();
            float ver = ParseBuildVersion(_build);
            var locs = GetLocationsForVersion(ver);
            foreach (var loc in locs) _locationBox.Items.Add(loc.Name);
            if (_locationBox.Items.Count > 0) _locationBox.SelectedIndex = 0;
        }

        static float ParseBuildVersion(string? build)
        {
            // "1.7.2" → 1.72, "3.5.1" → 3.51, "10.31" → 10.31
            if (string.IsNullOrEmpty(build)) return 1.0f;
            var parts = build.Split('.');
            if (parts.Length >= 2 && float.TryParse(parts[0], out float major) && float.TryParse(parts[1], out float minor))
                return major + minor / (minor >= 10 ? 100f : 10f);
            return 1.0f;
        }

        void TeleportToLocation()
        {
            if (_locationBox == null || _locationBox.SelectedItem == null) return;
            var name = _locationBox.SelectedItem.ToString()!;
            var loc = Array.Find(GetLocationsForVersion(ParseBuildVersion(_build)), l => l.Name == name);
            if (loc.Name == null) return;

            var dir = RuntimeDir();
            if (dir == null || _host == null) { Out("Start the match and join it first."); return; }
            var me = _me();
            if (string.IsNullOrEmpty(me)) { Out("No player name available."); return; }

            // x,y,z,1: the server snaps you to the ground under x,y (z is only the fallback height if nothing is hit)
            var coords = FormattableString.Invariant($"{loc.X:F0},{loc.Y:F0},3000,1");
            try { File.AppendAllText(Path.Combine(dir, "dev-command.txt"), $"tppos|{coords}|{me.Replace("|", "")}\n"); }
            catch (Exception ex) { Out("Teleport failed: " + ex.Message); return; }

            Out($"Teleporting to {name.TrimStart('~')} — the Outliner fills with the pieces around you.");
            if (_freeCam && _pipe != null) _ = _pipe.SetCameraAsync(loc.X, loc.Y, 7000, -35, 0, 0); // look down over the town
            _liveLast = DateTime.MinValue; // refresh the nearby actors right away (the camera follows your character)
        }

        // While the Studio is open the launcher starts its match server with the Studio's options (StudioSession.Current).
        void StudioSessionTick()
        {
            try
            {
                if (_closing) StudioSession.Clear();
                else StudioSession.Write(_layout.StudioMatch);
            }
            catch { }
        }


        // ---- Output panel ------------------------------------------------------------------------------
        UIElement BuildOutputPanel()
        {
            var dock = new DockPanel();
            var tools = new DockPanel { Margin = new Thickness(6), LastChildFill = true };
            var right = new StackPanel { Orientation = Orientation.Horizontal };
            DockPanel.SetDock(right, Dock.Right);
            var source = Picker(new[] { ("server", "Match server log"), ("game", "Game engine log") }, _logSource, v => { _logSource = v; _tailFile = null; Out(v == "game" ? "Showing the match server's engine log (host.log)." : "Showing the match server log."); Poll(); });
            source.Width = 170; source.Margin = new Thickness(6, 0, 0, 0);
            right.Children.Add(source);
            var errors = new CheckBox { Content = "Errors only", IsChecked = _errorsOnly, Foreground = Fg, VerticalAlignment = VerticalAlignment.Center, Margin = new Thickness(12, 0, 10, 0) };
            errors.Checked += (_, __) => { _errorsOnly = true; RenderOutput(); };
            errors.Unchecked += (_, __) => { _errorsOnly = false; RenderOutput(); };
            right.Children.Add(errors);
            var follow = new CheckBox { Content = "Follow", IsChecked = _follow, Foreground = Fg, VerticalAlignment = VerticalAlignment.Center, Margin = new Thickness(0, 0, 10, 0) };
            follow.Checked += (_, __) => { _follow = true; _outBox?.ScrollToEnd(); };
            follow.Unchecked += (_, __) => _follow = false;
            right.Children.Add(follow);
            var clear = MakeButton("Clear", () => { _lines.Clear(); RenderOutput(); }); clear.Margin = new Thickness(0);
            right.Children.Add(clear);
            tools.Children.Add(right);
            var filter = Input(_filter); filter.ToolTip = "Filter: show only lines containing this text";
            filter.TextChanged += (_, __) => { _filter = filter.Text; RenderOutput(); };
            tools.Children.Add(filter);
            DockPanel.SetDock(tools, Dock.Top);
            dock.Children.Add(tools);
            _outBox = new TextBox
            {
                IsReadOnly = true, FontFamily = Mono, FontSize = 12, Background = Field, Foreground = Fg, BorderThickness = new Thickness(0), SelectionBrush = Accent,
                VerticalScrollBarVisibility = ScrollBarVisibility.Auto, HorizontalScrollBarVisibility = ScrollBarVisibility.Auto, TextWrapping = TextWrapping.NoWrap, Padding = new Thickness(8, 4, 8, 4)
            };
            dock.Children.Add(_outBox);
            RenderOutput();
            return dock;
        }

        static bool IsError(string line) =>
            line.Contains("ERROR", StringComparison.OrdinalIgnoreCase) || line.Contains("Fatal", StringComparison.OrdinalIgnoreCase) ||
            line.Contains("[SyndicateHang]", StringComparison.Ordinal) || line.Contains("exception", StringComparison.OrdinalIgnoreCase) || line.Contains("crash", StringComparison.OrdinalIgnoreCase);
        bool Shows(string line) => (!_errorsOnly || IsError(line)) && (_filter.Length == 0 || line.Contains(_filter, StringComparison.OrdinalIgnoreCase));

        void RenderOutput()
        {
            if (_outBox == null) return;
            var sb = new StringBuilder();
            foreach (var l in _lines) if (Shows(l)) sb.AppendLine(l);
            _outBox.Text = sb.ToString();
            if (_follow) _outBox.ScrollToEnd();
        }

        void AddLines(IEnumerable<string> lines)
        {
            var shown = new StringBuilder();
            foreach (var l in lines) { _lines.Add(l); if (Shows(l)) shown.AppendLine(l); }
            if (_lines.Count > MaxLines + 500) { _lines.RemoveRange(0, _lines.Count - MaxLines); RenderOutput(); return; } // trim in chunks, not every line
            if (_outBox != null && shown.Length > 0) { _outBox.AppendText(shown.ToString()); if (_follow) _outBox.ScrollToEnd(); }
        }

        // Studio's own messages go in the Output panel too, marked [Studio].
        void Out(string message) => AddLines(new[] { $"[{DateTime.Now:HH:mm:ss}] [Studio] {message}" });

        void SetTail(string file)
        {
            if (string.Equals(file, _tailFile, StringComparison.OrdinalIgnoreCase)) return;
            _tailFile = file; _tailPos = 0; _tailRest = "";
            // A server that has been up a while: start near the end instead of replaying a huge log.
            try { var len = new FileInfo(file).Length; if (len > 256 * 1024) { _tailPos = len - 256 * 1024; _tailRest = null; } } catch { }
        }

        void TailOutput()
        {
            if (_tailFile == null) return;
            try
            {
                using var stream = new FileStream(_tailFile, FileMode.Open, FileAccess.Read, FileShare.ReadWrite | FileShare.Delete);
                if (stream.Length < _tailPos) { _tailPos = 0; _tailRest = ""; }
                if (stream.Length == _tailPos) return;
                stream.Seek(_tailPos, SeekOrigin.Begin);
                var buffer = new byte[Math.Min(stream.Length - _tailPos, 1024 * 1024)];
                int read = stream.Read(buffer, 0, buffer.Length);
                _tailPos += read;
                var text = Encoding.UTF8.GetString(buffer, 0, read);
                bool skipFirst = _tailRest == null;
                text = (_tailRest ?? "") + text;
                var parts = text.Replace("\r\n", "\n").Split('\n');
                _tailRest = parts[^1];
                AddLines(parts.Take(parts.Length - 1).Skip(skipFirst ? 1 : 0));
            }
            catch (IOException) { }
            catch (UnauthorizedAccessException) { }
        }

        // ---- misc --------------------------------------------------------------------------------------
        void OpenFolder(string? folder)
        {
            if (folder == null) { Out("Syndicate's files weren't found."); return; }
            try { Directory.CreateDirectory(folder); Process.Start(new ProcessStartInfo("explorer.exe", "\"" + folder + "\"") { UseShellExecute = true }); }
            catch (Exception ex) { Out("ERROR: " + ex.Message); }
        }
        void Open(string file)
        {
            try { Process.Start(new ProcessStartInfo(file) { UseShellExecute = true }); } catch (Exception ex) { Out("ERROR: " + ex.Message); }
        }
        void UI(Action a) { if (Dispatcher.CheckAccess()) a(); else Dispatcher.BeginInvoke(a); }
    }
}
