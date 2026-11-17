// Syndicate © 2026 Syndicate. All rights reserved. Proprietary — no copying, distribution, or derivative works. See LICENSE.
using System;
using System.Collections.Concurrent;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Net;
using System.Net.NetworkInformation;
using System.Net.Sockets;
using System.Text;
using System.Text.Json;
using System.Text.Json.Serialization;
using System.Threading;
using System.Threading.Tasks;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Controls.Primitives;
using System.Windows.Media;
using System.Windows.Threading;
namespace SyndicateClassic;

// Multiplayer tab ("Host your island"): host on this PC, or join a friend. Friends running Syndicate
// on the same Radmin VPN / Hamachi / ZeroTier / home network show up by themselves, so joining is
// one click instead of typing an IP. Joining still goes through the existing Radmin connection
// (RadminConnection + SaveRadmin), so the match itself works exactly as before.
public partial class MainWindow
{
    private Grid? _multiplayerView;
    private SyndicateFriendFinder? _friendFinder;
    private DispatcherTimer? _multiplayerTimer;
    private volatile FriendAdvert? _friendAdvert;
    private Button? _hostButton;
    private System.Windows.Shapes.Ellipse? _hostStateDot;
    private TextBlock? _hostStateText, _hostHelp, _friendsEmpty, _firewallNote;
    private StackPanel? _friendsList, _addressList;
    private WrapPanel? _hostSeasonChips;
    private int _hostSeason = 1;
    private Button? _firewallButton;
    private string _friendsSignature = "", _addressSignature = "";
    private DateTime _addressesRead = DateTime.MinValue;
    private bool _joinBusy;
    // Direct play (share code, no Radmin): see DirectPlay.cs.
    private Button? _directButton, _directCopy, _directShow;
    private TextBox? _directCodeBox, _joinCodeBox;
    private TextBlock? _directStatus, _joinCodeStatus;
    private string _directCode = "";
    private bool _directBusy, _directShown;
    // While direct play is open: who asked to join (asked once each), the players let in, the 6-hour code limit and
    // closing after 30 minutes with no friend connected.
    private System.Windows.Threading.DispatcherTimer? _directWatch;
    private readonly HashSet<string> _directAsked = new();
    private StackPanel? _directPeople;
    private DateTime _directOpenedAt;
    private bool _directPrompting;
    private const string DirectWarnColor = "#FF9B5E";

    private static readonly string FirewallMarker = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "ProjectTilted", "firewall-rules.txt");

    private Grid BuildMultiplayerPage()
    {
        var page = new Grid { Background = Brushes.Transparent };
        _multiplayerView = page;
        var scroll = new ScrollViewer { VerticalScrollBarVisibility = ScrollBarVisibility.Auto, HorizontalScrollBarVisibility = ScrollBarVisibility.Disabled, Margin = new Thickness(RailWidth, 52, 0, 0) };
        RailTrack(scroll);   // reflow with the menu (RailCollapse.cs)
        scroll.Resources.Add(typeof(ScrollBar), DarkScrollBar());
        page.Children.Add(scroll);
        var body = new StackPanel { Margin = new Thickness(22, 8, 40, 28) };
        scroll.Content = body;

        body.Children.Add(NightEyebrow("Play with friends"));
        body.Children.Add(NightTitle("Host your island", 40).Also(t => t.Margin = new Thickness(0, 2, 0, 4)));
        body.Children.Add(NightText("Host matches on this PC, or join a friend over Radmin VPN, Hamachi, ZeroTier or LAN.", 13.5, Night.Sub).Also(t => t.Margin = new Thickness(0, 0, 0, 20)));

        var columns = new Grid();
        columns.ColumnDefinitions.Add(new ColumnDefinition());
        columns.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(20) });
        columns.ColumnDefinitions.Add(new ColumnDefinition());
        body.Children.Add(columns);

        StackPanel PanelHeader(StackPanel into, string glyph, string title)
        {
            var row = new StackPanel { Orientation = Orientation.Horizontal, Margin = new Thickness(0, 0, 0, 20) };
            var icon = new Border { Width = 38, Height = 38, CornerRadius = TiltCorners(10), Background = Hex(Night.Surface2), BorderBrush = Hex(Night.Line), BorderThickness = new Thickness(1), Child = GlyphIcon(glyph, 16, Night.Gold) };
            row.Children.Add(icon);
            row.Children.Add(NightTitle(title, 22).Also(t => { t.VerticalAlignment = VerticalAlignment.Center; t.Margin = new Thickness(12, 0, 0, 0); }));
            into.Children.Add(row);
            return row;
        }
        TextBlock FieldLabel(string text) => NightText(text, 12, Night.Sub, bold: true).Also(t => t.Margin = new Thickness(0, 0, 0, 8));

        // ---- Host a match ----
        var host = new StackPanel();
        PanelHeader(host, "", "Host a match");
        host.Children.Add(FieldLabel("Season"));
        _hostSeasonChips = new WrapPanel { Margin = new Thickness(0, 0, 0, 10) };
        foreach (var season in ModernSeasons)
        {
            var chip = NightChip(PickerLabel(season.Number)); chip.Tag = season.Number;
            chip.ToolTip = SeasonWords(season.Number) + " (Fortnite " + season.Build + ")";
            chip.Click += (_, __) => { _hostSeason = season.Number; RefreshMultiplayer(); };
            _hostSeasonChips.Children.Add(chip);
        }
        host.Children.Add(_hostSeasonChips);
        host.Children.Add(FieldLabel("Mode"));
        host.Children.Add(NightText("Pick Solo, Duos or Squads in Fortnite's lobby. Your bot settings decide the match.", 12.5, Night.Sub).Also(t => t.Margin = new Thickness(0, 0, 0, 18)));
        var state = new StackPanel { Orientation = Orientation.Horizontal };
        _hostStateDot = NightDot(Night.Muted); _hostStateDot.Margin = new Thickness(0, 0, 10, 0); state.Children.Add(_hostStateDot);
        _hostStateText = NightText("Not hosting", 13, Night.Text); _hostStateText.VerticalAlignment = VerticalAlignment.Center; state.Children.Add(_hostStateText);
        host.Children.Add(new Border { Child = state, Background = Hex(Night.Surface), BorderBrush = Hex(Night.Line), BorderThickness = new Thickness(1), CornerRadius = new CornerRadius(9), Padding = new Thickness(16, 14, 16, 14), Margin = new Thickness(0, 0, 0, 16) });
        _hostButton = new Button { Content = "START HOSTING", Height = 54, HorizontalAlignment = HorizontalAlignment.Stretch };
        NightButton(_hostButton, "gold", 12); _hostButton.FontSize = 22;
        _hostButton.Click += async (_, __) => await StartOrStopHosting();
        host.Children.Add(_hostButton);
        _hostHelp = NightText("Friends press Join on their Multiplayer tab, then launch the same season. Keep Syndicate open while they play.", 11.5, Night.Sub);
        _hostHelp.Margin = new Thickness(0, 10, 0, 0);
        host.Children.Add(_hostHelp);
        host.Children.Add(BuildDirectPlayHost());
        var hostPanel = NightPanel(host); hostPanel.VerticalAlignment = VerticalAlignment.Top;
        columns.Children.Add(hostPanel);

        // ---- Join a friend ----
        var join = new StackPanel();
        PanelHeader(join, "", "Join a friend");
        join.Children.Add(NightText("Friends on your Radmin / Hamachi network", 13, Night.Text, bold: true));
        _friendsEmpty = NightText("Looking for friends… They show up here when their Syndicate launcher is open on the same Radmin VPN, Hamachi or home network.", 12, Night.Muted);
        _friendsEmpty.Margin = new Thickness(0, 6, 0, 0);
        join.Children.Add(_friendsEmpty);
        _friendsList = new StackPanel { Margin = new Thickness(0, 8, 0, 0) };
        join.Children.Add(_friendsList);

        join.Children.Add(FieldLabel("Or enter the host's Radmin / Hamachi / LAN IPv4").Also(t => t.Margin = new Thickness(0, 20, 0, 8)));
        // The existing Radmin controls (moved here from Settings) keep their own logic.
        foreach (var control in new FrameworkElement[] { RadminHostBox, SaveRadminButton, RadminStatus }) ReleaseControl(control);
        var manual = new Grid();
        manual.ColumnDefinitions.Add(new ColumnDefinition());
        manual.ColumnDefinitions.Add(new ColumnDefinition { Width = GridLength.Auto });
        manual.ColumnDefinitions.Add(new ColumnDefinition { Width = GridLength.Auto });
        NightField(RadminHostBox); RadminHostBox.Height = 44; RadminHostBox.FontFamily = NightMono;
        RadminHostBox.ToolTip = "The host's Radmin IPv4, for example 26.40.50.60. Leave empty to host on this PC.";
        manual.Children.Add(RadminHostBox);
        SaveRadminButton.Content = "Save"; SaveRadminButton.Height = 44; SaveRadminButton.MinWidth = 76; SaveRadminButton.Margin = new Thickness(10, 0, 0, 0);
        NightButton(SaveRadminButton, "base");
        Grid.SetColumn(SaveRadminButton, 1); manual.Children.Add(SaveRadminButton);
        var clear = new Button { Content = "Clear", Height = 44, MinWidth = 60, Margin = new Thickness(4, 0, 0, 0), ToolTip = "Stop joining a friend and host on this PC again" };
        NightButton(clear, "ghost");
        clear.Click += async (_, __) => { await SetJoinHost(""); RefreshMultiplayer(); };
        Grid.SetColumn(clear, 2); manual.Children.Add(clear);
        join.Children.Add(manual);
        RadminStatus.Foreground = Hex(Night.Sub); RadminStatus.FontSize = 12; RadminStatus.FontFamily = NightBody; RadminStatus.TextWrapping = TextWrapping.Wrap;
        RadminStatus.Margin = new Thickness(0, 10, 0, 0);
        join.Children.Add(RadminStatus);
        join.Children.Add(BuildDirectPlayJoin(FieldLabel("Or paste a friend's share code (Direct play, no Radmin)")));
        join.Children.Add(BuildNetworkTestControls());

        join.Children.Add(FieldLabel("Your addresses (share with friends)").Also(t => t.Margin = new Thickness(0, 20, 0, 2)));
        join.Children.Add(NightText("These are your Radmin / home-network addresses, not your internet IP. Hidden until you press the eye; Copy works either way.", 11.5, Night.Muted).Also(t => { t.TextWrapping = TextWrapping.Wrap; t.Margin = new Thickness(0, 0, 0, 8); }));
        _addressList = new StackPanel();
        join.Children.Add(_addressList);

        var tipText = new TextBlock { FontSize = 12, Foreground = Hex(Night.Sub), FontFamily = NightBody, TextWrapping = TextWrapping.Wrap, LineHeight = 18 };
        tipText.Inlines.Add(new System.Windows.Documents.Run("Playing with friends: ") { FontWeight = FontWeights.SemiBold, Foreground = Hex(Night.Text) });
        tipText.Inlines.Add(new System.Windows.Documents.Run("everyone joins the same Radmin VPN or Hamachi network and opens Syndicate. Friends appear in the list above: press Join, then launch the same season as the host. Windows asks once for permission the first time you host."));
        var tip = new StackPanel();
        tip.Children.Add(tipText);
        _firewallButton = new Button { Content = "Allow friends to join (one time)", Height = 36, HorizontalAlignment = HorizontalAlignment.Left, Margin = new Thickness(0, 12, 0, 0), ToolTip = "Adds Windows Firewall rules for Syndicate's multiplayer ports. Windows asks for administrator permission once." };
        NightButton(_firewallButton, "base");
        _firewallButton.Click += async (_, __) => await AllowFriendsThroughFirewall();
        tip.Children.Add(_firewallButton);
        _firewallNote = NightText("", 11.5, Night.Sub); _firewallNote.Margin = new Thickness(0, 8, 0, 0); _firewallNote.Visibility = Visibility.Collapsed;
        tip.Children.Add(_firewallNote);
        join.Children.Add(new Border { Child = tip, Background = Hex("#0F1328"), BorderBrush = Hex(Night.Line), BorderThickness = new Thickness(1), CornerRadius = TiltCorners(12), Padding = new Thickness(16), Margin = new Thickness(0, 18, 0, 0) });
        var joinPanel = NightPanel(join); joinPanel.VerticalAlignment = VerticalAlignment.Top;
        Grid.SetColumn(joinPanel, 2); columns.Children.Add(joinPanel);

        if (File.Exists(FirewallMarker)) _firewallButton.Content = "Friends allowed ✓  (run again)";
        return page;
    }

    // ---- Direct play: host side ----
    private FrameworkElement BuildDirectPlayHost()
    {
        var box = new StackPanel();
        box.Children.Add(NightText("Direct play (no Radmin)", 14, Night.Text, bold: true));
        box.Children.Add(NightText("Friends join with a share code over the internet. Best ping, nothing to install.", 12, Night.Sub).Also(t => { t.TextWrapping = TextWrapping.Wrap; t.Margin = new Thickness(0, 4, 0, 8); }));
        var warn = new TextBlock { FontSize = 12, FontFamily = NightBody, TextWrapping = TextWrapping.Wrap, LineHeight = 17, Foreground = Hex(DirectWarnColor), Margin = new Thickness(0, 0, 0, 10) };
        warn.Inlines.Add(new System.Windows.Documents.Run("Real friends only. ") { FontWeight = FontWeights.SemiBold });
        warn.Inlines.Add(new System.Windows.Documents.Run("Your share code leads to your internet address (IP). Anyone you give it to can see your IP and join your matches. Never post it in a public Discord, a stream or to people you don't know."));
        box.Children.Add(warn);
        _directButton = new Button { Content = "OPEN DIRECT PLAY", Height = 40, HorizontalAlignment = HorizontalAlignment.Stretch };
        NightButton(_directButton, "base");
        _directButton.Click += async (_, __) => await ToggleDirectPlay();
        box.Children.Add(_directButton);
        // Mini guide: getting UPnP to work (the launcher opens the ports itself once the router allows it).
        var guide = new StackPanel { Margin = new Thickness(2, 6, 0, 0) };
        foreach (var line in new[] {
            "1. Open your router page in a browser (usually 192.168.0.1 or 192.168.1.1; the address and password are often on a sticker on the router).",
            "2. Find UPnP (under Advanced, NAT, Network or Gaming) and turn it ON, then save. Some routers also need \"Allow devices to change port forwarding\" on, or \"UPnP secure mode\" off.",
            "3. Two routers (your internet provider's box plus your own, or a mesh system)? UPnP must be on in the box that connects to the internet, or put that box in bridge mode.",
            "4. Restart the router, then press OPEN DIRECT PLAY again. If it still fails, the message under the button names what the router answered.",
            "5. Last resort: forward TCP 70, 3551, 3552, 6969 and UDP 7777 to this PC by hand, or play over Radmin." })
            guide.Children.Add(NightText(line, 11.5, Night.Sub).Also(t => { t.TextWrapping = TextWrapping.Wrap; t.Margin = new Thickness(0, 0, 0, 4); }));
        box.Children.Add(new Expander { Header = NightText("UPnP not working? Quick guide", 12, Night.Gold, bold: true), Content = guide, Margin = new Thickness(0, 8, 0, 0), Foreground = Hex(Night.Sub) });
        var row = new Grid { Margin = new Thickness(0, 10, 0, 0), Visibility = Visibility.Collapsed };
        row.ColumnDefinitions.Add(new ColumnDefinition());
        row.ColumnDefinitions.Add(new ColumnDefinition { Width = GridLength.Auto });
        row.ColumnDefinitions.Add(new ColumnDefinition { Width = GridLength.Auto });
        _directCodeBox = new TextBox { IsReadOnly = true, Height = 40 };
        NightField(_directCodeBox); _directCodeBox.FontFamily = NightMono;
        row.Children.Add(_directCodeBox);
        _directShow = new Button { Content = "Show", Height = 40, MinWidth = 64, Margin = new Thickness(8, 0, 0, 0) };
        NightButton(_directShow, "ghost");
        _directShow.Click += (_, __) => { _directShown = !_directShown; ShowDirectCode(); };
        Grid.SetColumn(_directShow, 1); row.Children.Add(_directShow);
        _directCopy = new Button { Content = "Copy code", Height = 40, MinWidth = 96, Margin = new Thickness(6, 0, 0, 0) };
        NightButton(_directCopy, "gold", 8);
        _directCopy.Click += (_, __) => { try { Clipboard.SetText(_directCode); if (_directStatus != null) _directStatus.Text = "Copied. Send it privately to your friend (DM), not in a public channel."; } catch { } };
        Grid.SetColumn(_directCopy, 2); row.Children.Add(_directCopy);
        box.Children.Add(row);
        _directStatus = NightText("", 11.5, Night.Sub).Also(t => { t.TextWrapping = TextWrapping.Wrap; t.Margin = new Thickness(0, 8, 0, 0); });
        box.Children.Add(_directStatus);
        _directPeople = new StackPanel { Margin = new Thickness(0, 10, 0, 0), Visibility = Visibility.Collapsed };
        box.Children.Add(_directPeople);
        _directButton.Tag = row;
        // Ports left open by a launcher that crashed or was killed: close them now (the join key is gone anyway).
        if (Launcher.TryAutoLocate(out LauncherPaths startPaths, out string startError) && DirectPlay.IsOn(startPaths)) _ = Task.Run(() => DirectPlay.Close(startPaths));
        Closed += (_, __) => { if (Launcher.TryAutoLocate(out LauncherPaths closePaths, out string closeError) && DirectPlay.IsOn(closePaths)) DirectPlay.Close(closePaths); };
        return new Border { Child = box, Background = Hex("#0F1328"), BorderBrush = Hex(Night.Line), BorderThickness = new Thickness(1), CornerRadius = TiltCorners(12), Padding = new Thickness(16), Margin = new Thickness(0, 18, 0, 0) };
    }

    private void ShowDirectCode()
    {
        if (_directCodeBox == null || _directShow == null) return;
        _directCodeBox.Text = _directShown ? _directCode : new string('•', Math.Min(_directCode.Length, 32));
        _directShow.Content = _directShown ? "Hide" : "Show";
    }

    private async Task ToggleDirectPlay()
    {
        if (_directBusy || _directButton == null || _directStatus == null) return;
        if (!Launcher.TryAutoLocate(out LauncherPaths paths, out string locateError)) { _directStatus.Text = "Choose your game folder first."; return; }
        var row = _directButton.Tag as FrameworkElement;
        if (DirectPlay.IsOn(paths))
        {
            _directBusy = true; _directButton.IsEnabled = false;
            await CloseDirectPlay(paths, "Direct play closed. The ports on your router are closed and the old code no longer works.");
            return;
        }
        if (RadminConnection.Remote) { _directStatus.Text = "You're set to join someone. Press HOST ON THIS PC first."; return; }
        var answer = MessageBox.Show(this,
            "Direct play is for REAL FRIENDS ONLY.\n\n" +
            "• Your share code leads to your internet address (IP). Anyone who has it can see your IP and join your matches.\n" +
            "• Only send it privately to people you know and trust. Never post it in a public Discord, chat or stream.\n" +
            "• Syndicate opens a few ports on your router while Direct play is on, and closes them when you turn it off or close Syndicate. Strangers without your code are refused.\n" +
            "• Even with the code, nobody gets in until you press Allow. The code stops working after " + DirectPlay.CodeHours + " hours, and Direct play closes itself after 30 minutes with no friend connected.\n\n" +
            "Open Direct play now?",
            "Direct play: friends only", MessageBoxButton.YesNo, MessageBoxImage.Warning, MessageBoxResult.No);
        if (answer != MessageBoxResult.Yes) return;
        if (!File.Exists(FirewallMarker)) await AllowFriendsThroughFirewall();
        _directBusy = true; _directButton.IsEnabled = false;
        _directStatus.Text = "Asking your router to open Syndicate's ports…";
        var result = await DirectPlay.Open(paths);
        _directBusy = false; _directButton.IsEnabled = true;
        if (!result.Ok) { _directStatus.Text = result.Message; Log("Direct play: " + result.Message); return; }
        _directCode = result.Code; _directShown = false; ShowDirectCode();
        if (row != null) row.Visibility = Visibility.Visible;
        _directButton.Content = "CLOSE DIRECT PLAY";
        _directStatus.Text = result.Message + " Friends paste the code in Join a friend; you'll be asked to Allow each one. The code stops working at " + DateTime.Now.AddHours(DirectPlay.CodeHours).ToString("h:mm tt") + ".";
        _directOpenedAt = DateTime.UtcNow; _directAsked.Clear();
        _directWatch ??= new System.Windows.Threading.DispatcherTimer { Interval = TimeSpan.FromSeconds(2) };
        _directWatch.Tick -= DirectWatchTick; _directWatch.Tick += DirectWatchTick;
        _directWatch.Start();
        RefreshDirectPeople(paths);
        Log("Direct play open (UPnP).");
    }

    private async Task CloseDirectPlay(LauncherPaths paths, string message)
    {
        if (_directButton == null || _directStatus == null) return;
        _directWatch?.Stop();
        _directBusy = true; _directButton.IsEnabled = false;
        await Task.Run(() => DirectPlay.Close(paths));
        _directCode = ""; if (_directButton.Tag is FrameworkElement row) row.Visibility = Visibility.Collapsed;
        if (_directPeople != null) { _directPeople.Children.Clear(); _directPeople.Visibility = Visibility.Collapsed; }
        _directButton.Content = "OPEN DIRECT PLAY"; _directStatus.Text = message;
        _directBusy = false; _directButton.IsEnabled = true;
        Log("Direct play closed: " + message);
    }

    private async void DirectWatchTick(object? sender, EventArgs e)
    {
        if (_directPrompting || _directBusy) return;
        if (!Launcher.TryAutoLocate(out LauncherPaths paths, out string _) || !DirectPlay.IsOn(paths)) { _directWatch?.Stop(); return; }
        // The code's time is up.
        if (DirectPlay.ExpiresAt(paths) is DateTime end && DateTime.Now >= end)
        { await CloseDirectPlay(paths, $"Direct play closed: your code is {DirectPlay.CodeHours} hours old. Open it again for a new code."); return; }
        // Nobody let in has connected for 30 minutes (or ever, 30 minutes after opening): close the router ports.
        var last = DirectPlay.LastActivity(paths) ?? _directOpenedAt;
        if (last < _directOpenedAt) last = _directOpenedAt;
        if (DateTime.UtcNow - last > DirectPlay.IdleClose && (_game == null || _game.HasExited))
        { await CloseDirectPlay(paths, "Direct play closed after 30 minutes with no friend connected. Your router ports are closed again."); return; }
        // Someone with the code wants in: ask once.
        foreach (var who in DirectPlay.Waiting(paths))
        {
            if (!_directAsked.Add(who.Ip)) continue;
            _directPrompting = true;
            try
            {
                Activate();
                var name = who.Name.Length > 0 ? who.Name : "Someone";
                var answer = MessageBox.Show(this,
                    $"{name} wants to join your match with your Direct play code.\n\n" +
                    "Only press Yes if you know who this is. Once you let someone in, they can see your IP address.\n\n" +
                    "Let them in?",
                    "Direct play: someone wants to join", MessageBoxButton.YesNo, MessageBoxImage.Question, MessageBoxResult.No);
                DirectPlay.Answer(paths, who, answer == MessageBoxResult.Yes);
                if (_directStatus != null) _directStatus.Text = answer == MessageBoxResult.Yes ? $"{name} can join now (they press Join again)." : $"{name} was turned away. They can't join with this code.";
                Log($"Direct play: {name} {(answer == MessageBoxResult.Yes ? "allowed" : "turned away")}.");
            }
            finally { _directPrompting = false; }
        }
        RefreshDirectPeople(paths);
    }

    // The players let in with this code, each with a Remove button (removing also blocks them for this code).
    private void RefreshDirectPeople(LauncherPaths paths)
    {
        if (_directPeople == null) return;
        var people = DirectPlay.Allowed(paths);
        string key = string.Join("|", people.Select(p => p.Ip + p.Name));
        if (Equals(_directPeople.Tag, key)) return;
        _directPeople.Tag = key;
        _directPeople.Children.Clear();
        _directPeople.Visibility = people.Count > 0 ? Visibility.Visible : Visibility.Collapsed;
        if (people.Count == 0) return;
        _directPeople.Children.Add(NightText("Let in with this code", 12, Night.Sub, bold: true).Also(t => t.Margin = new Thickness(0, 0, 0, 6)));
        foreach (var who in people)
        {
            var line = new Grid { Margin = new Thickness(0, 0, 0, 6) };
            line.ColumnDefinitions.Add(new ColumnDefinition());
            line.ColumnDefinitions.Add(new ColumnDefinition { Width = GridLength.Auto });
            line.Children.Add(NightText(who.Name, 12.5, Night.Text, bold: true).Also(t => { t.VerticalAlignment = VerticalAlignment.Center; t.TextTrimming = TextTrimming.CharacterEllipsis; }));
            var remove = new Button { Content = "Remove", Height = 28, MinWidth = 76, ToolTip = "Turns this player away: they can't join again with this code." };
            NightButton(remove, "ghost");
            remove.Click += (_, __) => { DirectPlay.Answer(paths, who, false); if (_directStatus != null) _directStatus.Text = who.Name + " was removed and can't join again with this code. Close and reopen Direct play for a new code if needed."; RefreshDirectPeople(paths); };
            Grid.SetColumn(remove, 1); line.Children.Add(remove);
            _directPeople.Children.Add(line);
        }
    }

    // ---- Direct play: join side ----
    private FrameworkElement BuildDirectPlayJoin(TextBlock label)
    {
        var box = new StackPanel { Margin = new Thickness(0, 20, 0, 0) };
        label.Margin = new Thickness(0, 0, 0, 8);
        box.Children.Add(label);
        var row = new Grid();
        row.ColumnDefinitions.Add(new ColumnDefinition());
        row.ColumnDefinitions.Add(new ColumnDefinition { Width = GridLength.Auto });
        _joinCodeBox = new TextBox { Height = 44, ToolTip = "The code your friend copied from Direct play (starts with TILT-)." };
        NightField(_joinCodeBox); _joinCodeBox.FontFamily = NightMono;
        row.Children.Add(_joinCodeBox);
        var join = new Button { Content = "Join", Height = 44, MinWidth = 76, Margin = new Thickness(10, 0, 0, 0) };
        NightButton(join, "base");
        join.Click += async (_, __) => await JoinWithCode();
        Grid.SetColumn(join, 1); row.Children.Add(join);
        box.Children.Add(row);
        _joinCodeStatus = NightText("Only use codes from friends you know: joining shows them your IP too.", 11.5, Night.Sub).Also(t => { t.TextWrapping = TextWrapping.Wrap; t.Margin = new Thickness(0, 8, 0, 0); });
        box.Children.Add(_joinCodeStatus);
        return box;
    }

    private async Task JoinWithCode()
    {
        if (_joinCodeBox == null || _joinCodeStatus == null) return;
        if (!DirectPlay.TryReadCode(_joinCodeBox.Text, out var host, out var key)) { _joinCodeStatus.Text = "That isn't a Direct play code. Ask your friend to press Copy code and send it again."; return; }
        RadminConnection.SaveDirectKey(host, key);
        if (!await SetJoinHost(host)) { _joinCodeStatus.Text = RadminStatus.Text; return; }
        if (_paths != null) RadminConnection.Configure(_paths);
        _joinCodeBox.Text = "";
        _joinCodeStatus.Text = "Set to join your friend's code. Launch the same season as them.";
        RadminStatus.Text = "Joining by share code. Launch the same season as your friend.";
        RefreshMultiplayer();
    }

    // Starts friend discovery and a 1-second refresh of the status pill and this page.
    private void StartFriendFinder()
    {
        _friendFinder = new SyndicateFriendFinder { Advert = () => _friendAdvert };
        _friendFinder.Start();
        _multiplayerTimer = new DispatcherTimer { Interval = TimeSpan.FromSeconds(1) };
        _multiplayerTimer.Tick += (_, __) => RefreshMultiplayer();
        _multiplayerTimer.Start();
        Closed += (_, __) => { _multiplayerTimer?.Stop(); _friendFinder?.Dispose(); };
        RefreshMultiplayer();
        StartArenaHud();
    }

    private int? _runningSeason;
    private SeasonInfo HostSeasonInfo() => _sessionStarted && _runningSeason is int running ? ModernSeasons.FirstOrDefault(s => s.Number == running) ?? HostSeason() : HostSeason();
    private SeasonInfo HostSeason() => ModernSeasons.FirstOrDefault(s => s.Number == _hostSeason) ?? ModernSeasons[4];

    private static string SeasonWords(int number) => number == 0 ? "Pre-season" : (number >= 11 ? "Chapter 2 Season " : "Season ") + SeasonName(number);

    private void RefreshMultiplayer()
    {
        SyncRailAvatar();
        var season = HostSeasonInfo();
        bool joining = RadminConnection.Remote;
        bool playing = _sessionStarted;

        // What this launcher tells friends on the network. Players who are joining someone stay quiet.
        _friendAdvert = joining ? null : new FriendAdvert
        {
            Name = (_account.SignedIn ? _account.Username : null) ?? "Player",
            Season = season.Number, Build = season.Build, InGame = playing, Version = LauncherVersion(), Icon = _railAvatarId
        };

        var friends = _friendFinder?.Current() ?? new List<(FriendAdvert Ad, string Address)>();

        // Title bar pill.
        if (_hostStatusDot != null && _hostStatusText != null)
        {
            _hostStatusDot.Fill = Hex(joining ? Night.Sky : playing ? Night.Good : Night.Muted);
            _hostStatusText.Text = joining ? "Joining " + RadminConnection.Host : playing ? "Hosting" : "Not hosting";
        }
        if (_friendsStatusDot != null && _friendsStatusText != null)
        {
            _friendsStatusDot.Fill = Hex(friends.Count > 0 ? Night.Good : Night.Muted);
            _friendsStatusText.Text = friends.Count + " nearby";
        }
        if (_multiplayerView == null || _multiplayerView.Visibility != Visibility.Visible) return;

        // Host panel.
        if (_hostSeasonChips != null)
            foreach (var chip in _hostSeasonChips.Children.OfType<Button>()) { SetChip(chip, (int)chip.Tag == season.Number); chip.IsEnabled = !playing || (int)chip.Tag == season.Number; } // the running season is fixed until you quit
        if (_hostStateDot != null && _hostStateText != null && _hostButton != null && _hostHelp != null)
        {
            if (joining)
            {
                _hostStateDot.Fill = Hex(Night.Sky);
                _hostStateText.Text = "Set to join " + RadminConnection.Host + (playing ? " · in a match" : "");
                _hostButton.Content = "HOST ON THIS PC";
                _hostButton.IsEnabled = !playing && !_busy;
                _hostHelp.Text = playing ? "Quit Fortnite first, then you can host on this PC." : "Stops joining your friend. Friends can then join you instead.";
            }
            else if (playing)
            {
                _hostStateDot.Fill = Hex(Night.Good);
                _hostStateText.Text = "Hosting " + SeasonWords(season.Number) + " · friends can join now";
                _hostButton.Content = "STOP HOSTING";
                _hostButton.IsEnabled = SeasonLaunchButton(season.Number).IsEnabled;
                _hostHelp.Text = "Friends press Join on their Multiplayer tab, then launch " + SeasonWords(season.Number) + ". The host keeps running until everyone leaves.";
            }
            else
            {
                _hostStateDot.Fill = Hex(Night.Muted);
                _hostStateText.Text = "Not hosting";
                _hostButton.Content = "START HOSTING";
                _hostButton.IsEnabled = !_busy && SeasonLaunchButton(_hostSeason).IsEnabled;
                _hostHelp.Text = "Starts " + SeasonWords(season.Number) + " on this PC as the host. Friends press Join on their Multiplayer tab, then launch the same season. Keep Syndicate open while they play.";
            }
        }

        // Friends list (rebuilt only when it changes).
        string signature = string.Join("|", friends.Select(f => f.Address + "/" + f.Ad.Name + "/" + f.Ad.Season + "/" + f.Ad.InGame + "/" + f.Ad.Icon)) + "#" + RadminConnection.Host + "#" + _joinBusy + "#" + playing;
        if (signature != _friendsSignature && _friendsList != null && _friendsEmpty != null)
        {
            _friendsSignature = signature;
            _friendsList.Children.Clear();
            _friendsEmpty.Visibility = friends.Count == 0 ? Visibility.Visible : Visibility.Collapsed;
            foreach (var (ad, address) in friends) _friendsList.Children.Add(FriendRow(ad, address, playing));
        }

        // This PC's addresses (re-read every 10 seconds; adapters rarely change).
        if (_addressList != null && DateTime.UtcNow - _addressesRead > TimeSpan.FromSeconds(10))
        {
            _addressesRead = DateTime.UtcNow;
            var addresses = LocalAddresses();
            string addressSignature = string.Join("|", addresses.Select(a => a.Address + a.Label));
            if (addressSignature != _addressSignature)
            {
                _addressSignature = addressSignature;
                _addressList.Children.Clear();
                if (addresses.Count == 0) _addressList.Children.Add(NightText("No network found. Connect to Radmin VPN or your home network.", 12, Night.Muted));
                foreach (var (address, label) in addresses) _addressList.Children.Add(AddressRow(address, label));
            }
        }
    }

    private Border FriendRow(FriendAdvert ad, string address, bool playing)
    {
        bool joined = RadminConnection.Host == address;
        var row = new Grid();
        row.ColumnDefinitions.Add(new ColumnDefinition { Width = GridLength.Auto });
        row.ColumnDefinitions.Add(new ColumnDefinition());
        row.ColumnDefinitions.Add(new ColumnDefinition { Width = GridLength.Auto });
        var avatar = new Border { Width = 40, Height = 40, CornerRadius = new CornerRadius(20), Background = BrandFill(), Padding = new Thickness(2) };
        avatar.Child = AvatarFace(AvatarIds.Contains(ad.Icon) ? ad.Icon : "", ad.Name, 36);
        row.Children.Add(avatar);
        var words = new StackPanel { Margin = new Thickness(12, 0, 12, 0), VerticalAlignment = VerticalAlignment.Center };
        words.Children.Add(NightText(ad.Name, 13.5, Night.Text, bold: true).Also(t => t.TextWrapping = TextWrapping.NoWrap));
        var detail = new TextBlock { FontSize = 11.5, FontFamily = NightBody, TextWrapping = TextWrapping.NoWrap };
        detail.Inlines.Add(new System.Windows.Documents.Run(SeasonWords(ad.Season)) { Foreground = Hex(SeasonAccent(ad.Season)) });
        detail.Inlines.Add(new System.Windows.Documents.Run("  ·  " + (ad.InGame ? "In a match" : "In the lobby") + "  ·  " + address) { Foreground = Hex(Night.Sub) });
        words.Children.Add(detail);
        Grid.SetColumn(words, 1); row.Children.Add(words);
        var action = new Button { Content = joined ? "Leave" : "Join", Height = 34, MinWidth = 76, Padding = new Thickness(14, 0, 14, 0), VerticalAlignment = VerticalAlignment.Center };
        NightButton(action, joined ? "base" : "gold", 9);
        if (!joined) { action.FontSize = 16; action.Effect = null; }
        action.IsEnabled = !playing && !_joinBusy;
        action.ToolTip = playing ? "Quit Fortnite first" : joined ? "Stop joining " + ad.Name + " and host on this PC" : "Join " + ad.Name + "'s matches";
        action.Click += async (_, __) =>
        {
            if (_joinBusy) return;
            _joinBusy = true; RefreshMultiplayer();
            try
            {
                if (joined) { await SetJoinHost(""); return; }
                if (await SetJoinHost(address))
                {
                    if (ModernSeasons.Any(s => s.Number == ad.Season)) SelectHeroSeason(ad.Season);
                    RadminStatus.Text = "Joined " + ad.Name + ". Press PLAY on " + SeasonWords(ad.Season) + " to join their match.";
                    SetStatus("Joined " + ad.Name + " (" + address + "). Press PLAY on " + SeasonWords(ad.Season) + " to join their match.");
                }
            }
            finally { _joinBusy = false; _friendsSignature = ""; RefreshMultiplayer(); }
        };
        Grid.SetColumn(action, 2); row.Children.Add(action);
        return new Border
        {
            Child = row, Background = Hex(joined ? "#1A2340" : Night.Surface), BorderBrush = Hex(joined ? Night.Sky : Night.Line), BorderThickness = new Thickness(1),
            CornerRadius = TiltCorners(12), Padding = new Thickness(12, 10, 12, 10), Margin = new Thickness(0, 0, 0, 8)
        };
    }

    private Border AddressRow(string address, string label)
    {
        var row = new Grid();
        row.ColumnDefinitions.Add(new ColumnDefinition());
        row.ColumnDefinitions.Add(new ColumnDefinition { Width = GridLength.Auto });
        var words = new StackPanel { VerticalAlignment = VerticalAlignment.Center };
        // Blurred until you press the eye: people seeing a number on screen thought it was their real internet IP.
        var addressText = new TextBlock { Text = address, FontFamily = NightMono, FontSize = 15, Foreground = Hex(Night.Sky), Effect = new System.Windows.Media.Effects.BlurEffect { Radius = 9 } };
        words.Children.Add(addressText);
        words.Children.Add(NightText(label, 11.5, Night.Sub));
        row.Children.Add(words);
        row.ColumnDefinitions.Add(new ColumnDefinition { Width = GridLength.Auto }); // column 2: Copy
        var eye = NightRoundIcon("\uE890", "Show address", 34, 13);
        eye.Margin = new Thickness(0, 0, 8, 0);
        eye.Click += (_, __) =>
        {
            bool show = addressText.Effect != null;
            addressText.Effect = show ? null : new System.Windows.Media.Effects.BlurEffect { Radius = 9 };
            eye.Content = GlyphIcon(show ? "\uED1A" : "\uE890", 13, Night.Text);
            eye.ToolTip = show ? "Hide address" : "Show address";
        };
        Grid.SetColumn(eye, 1); row.Children.Add(eye);
        var copy = NightRoundIcon("", "Copy " + address, 34, 13);
        copy.Click += (_, __) =>
        {
            try { Clipboard.SetText(address); SetStatus("Copied your " + label + " address. Send it to your friends."); }
            catch (Exception error) { SetStatus("Could not copy: " + error.Message, okay: false); }
        };
        Grid.SetColumn(copy, 2); row.Children.Add(copy);
        return new Border { Child = row, Background = Hex(Night.Surface), BorderBrush = Hex(Night.Line), BorderThickness = new Thickness(1), CornerRadius = TiltCorners(10), Padding = new Thickness(14, 8, 10, 8), Margin = new Thickness(0, 0, 0, 8) };
    }

    // IPv4 addresses friends can use, Radmin first.
    private static List<(string Address, string Label)> LocalAddresses()
    {
        var found = new List<(string Address, string Label, int Order)>();
        try
        {
            foreach (var nic in NetworkInterface.GetAllNetworkInterfaces())
            {
                if (nic.OperationalStatus != OperationalStatus.Up || nic.NetworkInterfaceType == NetworkInterfaceType.Loopback) continue;
                string description = (nic.Description + " " + nic.Name).ToLowerInvariant();
                string label; int order;
                if (description.Contains("radmin")) { label = "Radmin VPN"; order = 0; }
                else if (description.Contains("hamachi")) { label = "Hamachi"; order = 1; }
                else if (description.Contains("zerotier")) { label = "ZeroTier"; order = 2; }
                else if (nic.NetworkInterfaceType == NetworkInterfaceType.Wireless80211) { label = "Wi-Fi"; order = 3; }
                else if (nic.NetworkInterfaceType == NetworkInterfaceType.Ethernet) { label = "Ethernet"; order = 4; }
                else { label = nic.Name; order = 5; }
                if (description.Contains("virtualbox") || description.Contains("vmware") || description.Contains("hyper-v") || description.Contains("vethernet")) continue;
                foreach (var unicast in nic.GetIPProperties().UnicastAddresses)
                {
                    if (unicast.Address.AddressFamily != AddressFamily.InterNetwork) continue;
                    var bytes = unicast.Address.GetAddressBytes();
                    if (bytes[0] == 169 && bytes[1] == 254) continue;
                    found.Add((unicast.Address.ToString(), label, order));
                }
            }
        }
        catch { }
        return found.OrderBy(f => f.Order).Select(f => (f.Address, f.Label)).Distinct().ToList();
    }

    // Same rules as Settings' old "Save connection": quit any session, then save who to join ("" = host here).
    private async Task<bool> SetJoinHost(string value)
    {
        if (_busy || _sessionStarted || _authBusy)
        {
            RadminStatus.Text = _sessionStarted ? "Quit Fortnite first, then change who you play with." : "Syndicate is busy. Try again in a moment.";
            return false;
        }
        bool marked = false;
        try
        {
            string host = RadminConnection.Validate(value);
            RadminHostBox.Text = host;
            if (host == RadminConnection.Host)
            {
                RadminStatus.Text = host.Length == 0 ? "Hosting on this PC." : "Already set to join " + host + ".";
                return true;
            }
            _busy = true; marked = true; UpdateButtons();
            if (_paths != null) await LeaveSyndicateSession(_paths);
            RadminConnection.Save(host);
            if (_paths != null) RadminConnection.Configure(_paths);
            RadminHostBox.Text = host;
            RadminStatus.Text = host.Length == 0 ? "Hosting on this PC. Friends can join you now." : "Saved. Launch the same season as the host to join. Your account stays local.";
            SetBackendBadge(Launcher.IsBackendUp());
            return true;
        }
        catch (Exception error)
        {
            RadminStatus.Text = error.Message;
            return false;
        }
        finally
        {
            if (marked) { _busy = false; UpdateButtons(); }
        }
    }

    private async Task StartOrStopHosting()
    {
        if (_hostButton == null || !_hostButton.IsEnabled) return;
        if (_sessionStarted)
        {
            // Quit uses the season's own Launch/Quit button, which also keeps the host up for friends still playing.
            if (!RadminConnection.Remote) SeasonLaunchButton(HostSeasonInfo().Number).RaiseEvent(new RoutedEventArgs(ButtonBase.ClickEvent));
            RefreshMultiplayer();
            return;
        }
        if (RadminConnection.Remote)
        {
            if (!await SetJoinHost("")) { RefreshMultiplayer(); return; }
            RefreshMultiplayer();
            return; // now hosting locally; press START HOSTING to launch
        }
        var launch = SeasonLaunchButton(_hostSeason);
        if (!launch.IsEnabled) return;
        // First time hosting on this PC: open Syndicate's ports in Windows Firewall so friends aren't blocked
        // (one UAC prompt, remembered after). Hosting still goes ahead even if the prompt is declined.
        if (!File.Exists(FirewallMarker)) { try { await AllowFriendsThroughFirewall(); } catch { } }
        HighlightRail(0);
        launch.RaiseEvent(new RoutedEventArgs(ButtonBase.ClickEvent));
    }

    // One-time Windows Firewall rules so friends can reach this PC (needs one administrator approval).
    private async Task AllowFriendsThroughFirewall()
    {
        if (_firewallButton == null || _firewallNote == null) return;
        var programs = new List<string>();
        foreach (var season in ModernSeasons)
        {
            try
            {
                var folder = Launcher.GameFolderFor(season.Build);
                if (folder != null) programs.Add(Path.Combine(folder, "FortniteGame", "Binaries", "Win64", "FortniteClient-Win64-Shipping.exe"));
            }
            catch { }
        }
        if (_paths != null) programs.Add(_paths.GameExe);
        _firewallButton.IsEnabled = false;
        _firewallNote.Visibility = Visibility.Visible;
        _firewallNote.Text = "Windows will ask for permission…";
        string error = "";
        bool ok = await Task.Run(() => SyndicateFirewall.Run(programs, out error));
        _firewallButton.IsEnabled = true;
        if (ok)
        {
            try { Directory.CreateDirectory(Path.GetDirectoryName(FirewallMarker)!); File.WriteAllText(FirewallMarker, DateTime.Now.ToString("u")); } catch { }
            bool verified = await Task.Run(() => SyndicateFirewall.Verify());
            _firewallButton.Content = "Friends allowed ✓  (run again)";
            _firewallNote.Text = verified
                ? "Done and verified: Syndicate's ports (TCP 70, 3551, 3552, 6969, 7071 · UDP 3554, 7777) are open. Friends on your Radmin, Hamachi or home network can now reach your matches."
                : "Rules added. Friends on your Radmin, Hamachi or home network can now reach your matches.";
            SetStatus("Firewall rules added. Friends can join your matches.");
        }
        else _firewallNote.Text = error.Length > 0 ? error : "The firewall setup did not finish. Try again.";
    }
}

// What a Syndicate launcher announces on the local / VPN network (no account data: name, season, state).
public sealed class FriendAdvert
{
    [JsonPropertyName("syndicate")] public int Syndicate { get; set; } = 1;
    [JsonPropertyName("id")] public string Id { get; set; } = "";
    [JsonPropertyName("name")] public string Name { get; set; } = "";
    [JsonPropertyName("season")] public int Season { get; set; }
    [JsonPropertyName("build")] public string Build { get; set; } = "";
    [JsonPropertyName("inGame")] public bool InGame { get; set; }
    [JsonPropertyName("version")] public string Version { get; set; } = "";
    [JsonPropertyName("icon")] public string Icon { get; set; } = "";
}

// Finds friends running Syndicate on the same Radmin VPN, Hamachi, ZeroTier or home network.
// Every launcher that isn't joining someone announces itself every 2 s with a small UDP broadcast
// on port 3554 to each adapter's own subnet. Nothing is sent outside the local/VPN networks.
internal sealed class SyndicateFriendFinder : IDisposable
{
    public const int Port = 3554;
    private static readonly TimeSpan Expiry = TimeSpan.FromSeconds(7);
    private readonly string _id = Guid.NewGuid().ToString("N");
    private readonly ConcurrentDictionary<string, (FriendAdvert Ad, string Address, DateTime Seen)> _seen = new();
    private readonly CancellationTokenSource _stop = new();
    private UdpClient? _listener;

    public Func<FriendAdvert?>? Advert { get; set; }

    public void Start()
    {
        try
        {
            _listener = new UdpClient(AddressFamily.InterNetwork);
            _listener.Client.SetSocketOption(SocketOptionLevel.Socket, SocketOptionName.ReuseAddress, true);
            _listener.Client.Bind(new IPEndPoint(IPAddress.Any, Port));
            _ = ListenLoop(_listener);
        }
        catch { /* port busy: friends can still be joined by IP */ }
        _ = AnnounceLoop();
    }

    public List<(FriendAdvert Ad, string Address)> Current()
    {
        var now = DateTime.UtcNow;
        foreach (var entry in _seen.Where(e => now - e.Value.Seen > Expiry).ToList()) _seen.TryRemove(entry.Key, out _);
        return _seen.Values.OrderBy(v => v.Ad.Name, StringComparer.OrdinalIgnoreCase).Select(v => (v.Ad, v.Address)).ToList();
    }

    private async Task ListenLoop(UdpClient listener)
    {
        while (!_stop.IsCancellationRequested)
        {
            UdpReceiveResult received;
            try { received = await listener.ReceiveAsync(_stop.Token).ConfigureAwait(false); }
            catch (OperationCanceledException) { return; }
            catch (ObjectDisposedException) { return; }
            catch (SocketException) { try { await Task.Delay(250, _stop.Token).ConfigureAwait(false); } catch (OperationCanceledException) { return; } continue; }
            try
            {
                if (received.Buffer.Length > 2048) continue;
                var ad = JsonSerializer.Deserialize<FriendAdvert>(received.Buffer);
                if (ad == null || ad.Syndicate != 1 || string.IsNullOrEmpty(ad.Id) || ad.Id == _id || ad.Id.Length > 64) continue;
                ad.Name = Clean(ad.Name, 24); ad.Build = Clean(ad.Build, 12); ad.Version = Clean(ad.Version, 16); ad.Icon = Clean(ad.Icon, 8);
                if (ad.Name.Length == 0) ad.Name = "Player";
                if (ad.Season < 0 || ad.Season > 10) ad.Season = 4;
                _seen[ad.Id] = (ad, received.RemoteEndPoint.Address.ToString(), DateTime.UtcNow);
            }
            catch (Exception) { }
        }
    }

    private async Task AnnounceLoop()
    {
        while (!_stop.IsCancellationRequested)
        {
            try
            {
                var ad = Advert?.Invoke();
                if (ad != null)
                {
                    var copy = new FriendAdvert { Id = _id, Name = ad.Name, Season = ad.Season, Build = ad.Build, InGame = ad.InGame, Version = ad.Version, Icon = ad.Icon };
                    byte[] data = JsonSerializer.SerializeToUtf8Bytes(copy);
                    foreach (var (local, broadcast) in Interfaces())
                    {
                        try
                        {
                            using var sender = new UdpClient(new IPEndPoint(local, 0)) { EnableBroadcast = true };
                            await sender.SendAsync(data, data.Length, new IPEndPoint(broadcast, Port)).ConfigureAwait(false);
                        }
                        catch (SocketException) { }
                    }
                }
            }
            catch { }
            try { await Task.Delay(2000, _stop.Token).ConfigureAwait(false); } catch (OperationCanceledException) { return; }
        }
    }

    // IPv4 address and subnet broadcast address of every connected adapter (LAN, Radmin, Hamachi, ZeroTier).
    private static IEnumerable<(IPAddress Local, IPAddress Broadcast)> Interfaces()
    {
        NetworkInterface[] nics;
        try { nics = NetworkInterface.GetAllNetworkInterfaces(); } catch { yield break; }
        foreach (var nic in nics)
        {
            if (nic.OperationalStatus != OperationalStatus.Up || nic.NetworkInterfaceType == NetworkInterfaceType.Loopback) continue;
            IPInterfaceProperties properties;
            try { properties = nic.GetIPProperties(); } catch { continue; }
            foreach (var unicast in properties.UnicastAddresses)
            {
                if (unicast.Address.AddressFamily != AddressFamily.InterNetwork || unicast.IPv4Mask == null) continue;
                byte[] ip = unicast.Address.GetAddressBytes(), mask = unicast.IPv4Mask.GetAddressBytes();
                if (ip[0] == 169 && ip[1] == 254) continue;
                var broadcast = new byte[4];
                for (int i = 0; i < 4; i++) broadcast[i] = (byte)(ip[i] | ~mask[i]);
                yield return (unicast.Address, new IPAddress(broadcast));
            }
        }
    }

    private static string Clean(string? text, int max)
    {
        var builder = new StringBuilder();
        foreach (char c in text ?? "") if (!char.IsControl(c)) builder.Append(c);
        string clean = builder.ToString().Trim();
        return clean.Length > max ? clean.Substring(0, max) : clean;
    }

    public void Dispose()
    {
        _stop.Cancel();
        try { _listener?.Dispose(); } catch { }
    }
}

// Windows Firewall rules for Syndicate multiplayer. VPN adapters usually count as Public networks and
// the match server runs hidden (so Windows never asks), which silently blocks friends. This allows
// Syndicate's ports on every profile and replaces old per-program rules (a dismissed Windows prompt
// leaves a BLOCK rule, which beats any allow rule). Needs one administrator (UAC) approval.
internal static class SyndicateFirewall
{
    private const string Prefix = "Project Tilted";

    public static bool Run(IEnumerable<string> programs, out string error)
    {
        error = "";
        var lines = new List<string>
        {
            $"netsh advfirewall firewall delete rule name=\"{Prefix} - multiplayer TCP\"",
            $"netsh advfirewall firewall delete rule name=\"{Prefix} - match UDP\"",
            $"netsh advfirewall firewall add rule name=\"{Prefix} - multiplayer TCP\" dir=in action=allow protocol=TCP localport=70,3551,3552,6969,7071 profile=any",
            $"netsh advfirewall firewall add rule name=\"{Prefix} - match UDP\" dir=in action=allow protocol=UDP localport=3554,7777 profile=any",
        };
        int index = 0;
        foreach (string program in programs.Where(File.Exists).Distinct(StringComparer.OrdinalIgnoreCase))
        {
            if (program.IndexOf('"') >= 0) continue;
            index++;
            lines.Add($"netsh advfirewall firewall delete rule name=all program=\"{program}\"");
            lines.Add($"netsh advfirewall firewall add rule name=\"{Prefix} - program {index}\" dir=in action=allow program=\"{program}\" profile=any");
        }
        string script = Path.Combine(Path.GetTempPath(), "SyndicateClassic-firewall.cmd");
        try
        {
            File.WriteAllText(script, "@echo off\r\n" + string.Join("\r\n", lines.Select(l => l + " >nul")) + "\r\n", new UTF8Encoding(false));
            var start = new ProcessStartInfo("cmd.exe", $"/c \"\"{script}\"\"") { UseShellExecute = true, Verb = "runas", WindowStyle = ProcessWindowStyle.Hidden };
            using var process = Process.Start(start);
            if (process == null) { error = "Windows didn't start the firewall setup."; return false; }
            process.WaitForExit(30000);
            return process.HasExited && process.ExitCode == 0;
        }
        catch (System.ComponentModel.Win32Exception denied) when (denied.NativeErrorCode == 1223)
        {
            error = "You said No to the Windows prompt, so nothing was changed.";
            return false;
        }
        catch (Exception other)
        {
            error = other.Message;
            return false;
        }
        finally
        {
            try { File.Delete(script); } catch { }
        }
    }

    // Confirms the port rule is present and enabled (no admin needed). Used after Run() to tell the user it really took.
    public static bool Verify()
    {
        try
        {
            var start = new ProcessStartInfo("netsh", $"advfirewall firewall show rule name=\"{Prefix} - multiplayer TCP\"")
            { UseShellExecute = false, CreateNoWindow = true, RedirectStandardOutput = true };
            using var process = Process.Start(start);
            if (process == null) return false;
            string output = process.StandardOutput.ReadToEnd();
            process.WaitForExit(8000);
            // "show rule" prints the rule when it exists, or "No rules match the specified criteria." when it doesn't.
            return output.IndexOf("3551", StringComparison.Ordinal) >= 0;
        }
        catch { return false; }
    }
}