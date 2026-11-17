// Syndicate © 2026 Syndicate. All rights reserved. Proprietary — no copying, distribution, or derivative works. See LICENSE.
using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Net.Http;
using System.Text;
using System.Text.Json;
using System.Threading.Tasks;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Controls.Primitives;
using System.Windows.Media;

namespace SyndicateClassic;

// Match data: the player-facing explanation and switches (Settings > Match data), and the developer
// diagnostics window that reads the backend's /tilted/telemetry API (reports, bot weaknesses,
// landing intelligence, playstyles, bot version gate, datasets).
//
// The game server writes the match files; the backend analyses them. Nothing here reads game memory
// or the game client: every number comes from events our own server observed.
internal static class MatchTelemetry
{
    internal static string? Folder(LauncherPaths? paths)
    {
        if (paths == null && !Launcher.TryAutoLocate(out paths, out _)) return null;
        return paths == null ? null : Path.Combine(paths.Root, "Runtime", "Server", "telemetry");
    }

    internal sealed class Settings
    {
        public bool collect { get; set; } = true;
        public bool upload { get; set; } = true;
        public bool errors { get; set; } = true;   // send error reports when a match server fails to start (ErrorReporter.cs)
        public int retentionDays { get; set; } = 30;
        public int maxMegabytes { get; set; } = 500;
    }

    internal static Settings Load(string? folder)
    {
        try { if (folder != null) return JsonSerializer.Deserialize<Settings>(File.ReadAllText(Path.Combine(folder, "settings.json"))) ?? new Settings(); } catch { }
        return new Settings();
    }

    internal static void Save(string folder, Settings settings)
    {
        Directory.CreateDirectory(folder);
        File.WriteAllText(Path.Combine(folder, "settings.json"), JsonSerializer.Serialize(settings, new JsonSerializerOptions { WriteIndented = true }));
    }

    // Read by GameServer when it starts the match server (TILTED_TELEMETRY=0 turns recording off).
    internal static bool Collect(string serverRuntime) => Load(Path.Combine(serverRuntime, "telemetry")).collect;

    // Developer Tools exist only in Dev Kit builds. build-launcher.ps1 adds the TILTED_DEVKIT compile
    // symbol unless it runs with -Public (build-public-release.ps1 does that), so public EXEs do not
    // contain the developer window, the Discord test or the diagnostics API switch at all. Nothing a
    // user can edit (settings, files, environment variables) turns them on.
#if TILTED_DEVKIT
    internal const bool DevKit = true;
    internal const string BuildKind = "TILTED_DEVKIT_BUILD"; // build-public-release.ps1 refuses to pack a DLL containing this text
#else
    internal const bool DevKit = false;
    internal const string BuildKind = "public";
#endif
}

public partial class MainWindow
{
    // Settings > Match data: what is recorded, why, where it goes, and the switches.
    private void AddMatchDataSettings(Panel group)
    {
        var folder = MatchTelemetry.Folder(_paths);
        var settings = MatchTelemetry.Load(folder);
        group.Children.Add(Body(
            "What is recorded: during matches on your PC, Syndicate's match server writes what it sees: where players and bots land, eliminations and knocks (who, how far, which weapon), " +
            "what each bot was doing when it switched activity or got stuck, storm phases, and how the match ended. Players are stored as random codes, never names, accounts or IP addresses. " +
            "Nothing is read from the Fortnite game itself.\n\n" +
            "Why: to find where bots make mistakes and to make them smarter, and to learn which landing spots are hot.\n\n" +
            "Where it goes: the full match files stay in Syndicate's Runtime\\Server\\telemetry folder on this PC. A short summary of each finished match " +
            "(counts only: no player codes, positions or names) is sent to the Syndicate developers' Discord through Syndicate's relay to help improve the bots. You can turn that off below. Files older than the retention period are deleted automatically.",
            12.5, Night.Sub).Also(t => { t.TextWrapping = TextWrapping.Wrap; t.Margin = new Thickness(0, 0, 0, 14); }));

        CheckBox Toggle(string text, bool value, Action<bool> changed)
        {
            var box = new CheckBox { Content = NightText(text, 13, Night.Text), IsChecked = value, Margin = new Thickness(0, 0, 0, 10), Foreground = Hex(Night.Text) };
            box.Checked += (_, __) => changed(true); box.Unchecked += (_, __) => changed(false);
            return box;
        }
        void Persist() { if (folder == null) { SetStatus("Syndicate's files weren't found, so match data settings can't be saved yet.", okay: false); return; } try { MatchTelemetry.Save(folder, settings); } catch (Exception error) { SetStatus("Couldn't save match data settings: " + error.Message, okay: false); } }
        group.Children.Add(Toggle("Record matches on this PC (applies to the next match)", settings.collect, v => { settings.collect = v; Persist(); }));
        group.Children.Add(Toggle("Share finished-match summaries with the Syndicate developers", settings.upload, v => { settings.upload = v; Persist(); }));
        group.Children.Add(Toggle("Send error reports when a season's match server fails to start", settings.errors, v => { settings.errors = v; Persist(); }));
        group.Children.Add(Body("Error reports go to the Syndicate developers' Discord so a broken season can be fixed: the season, game mode, launcher and Windows version, the error, and the last lines of the launcher and match server logs. " +
            "Your Windows user name, PC name, folder paths, e-mail and IP addresses are removed first. At most one report every few minutes.", 12, Night.Muted).Also(t => { t.TextWrapping = TextWrapping.Wrap; t.Margin = new Thickness(26, -4, 0, 12); }));

        var retentionRow = new StackPanel { Orientation = Orientation.Horizontal, Margin = new Thickness(0, 4, 0, 12) };
        retentionRow.Children.Add(NightText("Keep match files for", 13, Night.Text).Also(t => t.VerticalAlignment = VerticalAlignment.Center));
        var days = new TextBox { Text = settings.retentionDays.ToString(), Width = 70, Height = 34, Margin = new Thickness(10, 0, 10, 0), MaxLength = 3 };
        NightField(days);
        days.LostKeyboardFocus += (_, __) => { if (int.TryParse(days.Text, out var d) && d >= 1 && d <= 365) { settings.retentionDays = d; Persist(); } else days.Text = settings.retentionDays.ToString(); };
        retentionRow.Children.Add(days);
        retentionRow.Children.Add(NightText("days", 13, Night.Text).Also(t => t.VerticalAlignment = VerticalAlignment.Center));
        group.Children.Add(retentionRow);

        var buttons = new WrapPanel();
        var openFolder = new Button { Content = "Open match data folder", Height = 36, Padding = new Thickness(14, 0, 14, 0), Margin = new Thickness(0, 0, 8, 8) };
        NightButton(openFolder, "base");
        openFolder.Click += (_, __) => { if (folder == null) return; Directory.CreateDirectory(folder); try { System.Diagnostics.Process.Start(new System.Diagnostics.ProcessStartInfo("explorer.exe", "\"" + folder + "\"") { UseShellExecute = true }); } catch { } };
        buttons.Children.Add(openFolder);
        group.Children.Add(buttons);
#if TILTED_DEVKIT
        // Developer Tools: always here in Dev Kit builds (no switch, no restart).
        group.Children.Add(NightTitle("Developer Tools", 18).Also(t => t.Margin = new Thickness(0, 18, 0, 4)));
        group.Children.Add(NightText("Dev Kit build: match reports, bot weaknesses, landing stats and the Discord relay test.", 12, Night.Sub).Also(t => { t.TextWrapping = TextWrapping.Wrap; t.Margin = new Thickness(0, 0, 0, 10); }));
        var devButtons = new WrapPanel();
        var open = new Button { Content = "Open developer diagnostics", Height = 36, Padding = new Thickness(14, 0, 14, 0), Margin = new Thickness(0, 0, 8, 8) };
        NightButton(open, "gold");
        open.Click += (_, __) => new DiagnosticsWindow(folder) { Owner = this }.Show();
        devButtons.Children.Add(open);
        var studio = new Button { Content = "Syndicate Studio", Height = 36, Padding = new Thickness(14, 0, 14, 0), Margin = new Thickness(0, 0, 8, 8), ToolTip = "Run the local match server, see the island and edit the map live" };
        NightButton(studio, "gold");
        studio.Click += (_, __) => OpenStudio();
        devButtons.Children.Add(studio);
        var test = new Button { Content = "Send test message", Height = 36, Padding = new Thickness(14, 0, 14, 0), Margin = new Thickness(0, 0, 8, 8), ToolTip = "Sends a clearly labelled test message to the match tracking Discord channel" };
        NightButton(test, "base");
        var result = NightText("", 12.5, Night.Sub).Also(t => { t.TextWrapping = TextWrapping.Wrap; t.Margin = new Thickness(0, 2, 0, 0); });
        test.Click += async (_, __) =>
        {
            test.IsEnabled = false; result.Foreground = Hex(Night.Sub); result.Text = "Sending a test message to Discord…";
            var (ok, message) = await DevApi.DiscordTest(folder);
            result.Foreground = Hex(ok ? Night.Good : Night.Danger); result.Text = message; test.IsEnabled = true;
            Log((ok ? "Discord relay test: " : "ERROR: Discord relay test failed: ") + message);
        };
        devButtons.Children.Add(test);
        group.Children.Add(devButtons);
        group.Children.Add(result);
#endif
    }

#if TILTED_DEVKIT
    // Settings > Developer tools (Dev Kit builds only). The player-facing Match data page was retired, so the
    // developer window, the custom-map editor and the Discord relay test now live on their own sidebar page.
    private void AddDeveloperToolsSettings(Panel group)
    {
        var folder = MatchTelemetry.Folder(_paths);
        group.Children.Add(NightText("Dev Kit build: match reports, bot weaknesses, landing stats, Syndicate Studio (match server + live map editing) and the Discord relay test.", 12.5, Night.Sub).Also(t => { t.TextWrapping = TextWrapping.Wrap; t.Margin = new Thickness(0, 0, 0, 12); }));
        var devButtons = new WrapPanel();
        var open = new Button { Content = "Open developer diagnostics", Height = 36, Padding = new Thickness(14, 0, 14, 0), Margin = new Thickness(0, 0, 8, 8) };
        NightButton(open, "gold");
        open.Click += (_, __) => new DiagnosticsWindow(folder) { Owner = this }.Show();
        devButtons.Children.Add(open);
        var studio = new Button { Content = "Syndicate Studio", Height = 36, Padding = new Thickness(14, 0, 14, 0), Margin = new Thickness(0, 0, 8, 8), ToolTip = "Run the local match server, see the island and edit the map live" };
        NightButton(studio, "gold");
        studio.Click += (_, __) => OpenStudio();
        devButtons.Children.Add(studio);
        var second = new Button { Content = "Launch 2nd player", Height = 36, Padding = new Thickness(14, 0, 14, 0), Margin = new Thickness(0, 0, 8, 8), ToolTip = "Open a second Fortnite client as a different saved player, into the same match (duo self-testing). Press Play on your own game first." };
        NightButton(second, "base");
        second.Click += (_, __) => LaunchSecondClient();
        devButtons.Children.Add(second);
        var swap = new Button { Content = "Swap 2nd player controls", Height = 36, Padding = new Thickness(14, 0, 14, 0), Margin = new Thickness(0, 0, 8, 8), ToolTip = "By default your game uses keyboard + mouse and the 2nd player uses the controller. This flips it." };
        NightButton(swap, "base");
        swap.Click += (_, __) => SwapInputRoles();
        devButtons.Children.Add(swap);
        var result = NightText("", 12.5, Night.Sub).Also(t => { t.TextWrapping = TextWrapping.Wrap; t.Margin = new Thickness(0, 2, 0, 0); });
        var test = new Button { Content = "Send test message", Height = 36, Padding = new Thickness(14, 0, 14, 0), Margin = new Thickness(0, 0, 8, 8), ToolTip = "Sends a clearly labelled test message to the match tracking Discord channel" };
        NightButton(test, "base");
        test.Click += async (_, __) =>
        {
            test.IsEnabled = false; result.Foreground = Hex(Night.Sub); result.Text = "Sending a test message to Discord…";
            var (ok, message) = await DevApi.DiscordTest(folder);
            result.Foreground = Hex(ok ? Night.Good : Night.Danger); result.Text = message; test.IsEnabled = true;
            Log((ok ? "Discord relay test: " : "ERROR: Discord relay test failed: ") + message);
        };
        devButtons.Children.Add(test);
        group.Children.Add(devButtons);
        group.Children.Add(result);
    }

    // Syndicate Studio (Dev Kit): one window at a time; its Play is the launcher's Play for the current season.
    private StudioWindow? _studio;
    private void OpenStudio()
    {
        if (_studio != null) { if (_studio.WindowState == WindowState.Minimized) _studio.WindowState = WindowState.Normal; _studio.Activate(); return; }
        _studio = new StudioWindow(() => _paths, MyDevName, Log, () =>
        {
            var play = SeasonLaunchButton(_heroSeason);
            if (_sessionStarted || (_paths != null && SessionProcesses.ClientRunning(_paths))) Log("Syndicate Studio: Fortnite is already running.");
            else if (play != null && play.IsEnabled) LaunchButton_Click(play, new RoutedEventArgs());
            else Log("Syndicate Studio: Play isn't available right now — check the launcher's home screen.");
        });
        _studio.Closed += (_, __) => _studio = null;
        _studio.Show();
    }
#endif
}

#if TILTED_DEVKIT
// Calls to the backend's developer API (this PC only, developer token from Runtime\Server\telemetry\dev-token).
internal static class DevApi
{
    private static readonly HttpClient Http = new() { BaseAddress = new Uri("http://127.0.0.1:3551/"), Timeout = TimeSpan.FromSeconds(30) };

    // The backend answers {ok, message}; ok is true only when Discord accepted the message.
    internal static async Task<(bool Ok, string Message)> DiscordTest(string? folder)
    {
        try
        {
            var tokenFile = folder == null ? null : Path.Combine(folder, "dev-token");
            if (tokenFile == null || !File.Exists(tokenFile)) return (false, "The backend has not created its developer token yet. Start Syndicate's local services, then try again.");
            using var request = new HttpRequestMessage(HttpMethod.Post, "syndicate/telemetry/discord-test");
            request.Headers.Add("X-Syndicate-Dev", File.ReadAllText(tokenFile).Trim());
            request.Content = new StringContent("{}", Encoding.UTF8, "application/json");
            using var response = await Http.SendAsync(request);
            var text = await response.Content.ReadAsStringAsync();
            if ((int)response.StatusCode == 403 && RadminConnection.Remote) return (false, DiagnosticsWindow.RemoteNotice);
            try
            {
                using var doc = JsonDocument.Parse(text);
                var root = doc.RootElement;
                bool ok = root.TryGetProperty("ok", out var flag) && flag.ValueKind == JsonValueKind.True;
                return (ok, root.TryGetProperty("message", out var m) ? m.GetString() ?? "" : $"Backend answered {(int)response.StatusCode}.");
            }
            catch (JsonException) { return (false, $"Backend answered {(int)response.StatusCode}. Restart Syndicate's local services so the backend has the relay test."); }
        }
        catch (HttpRequestException) { return (false, "The backend is not running. Start Syndicate's local services, then try again."); }
        catch (TaskCanceledException) { return (false, "The backend did not answer within 30 seconds."); }
    }
}

// Developer diagnostics: a separate window so the player-facing launcher stays simple.
internal sealed class DiagnosticsWindow : Window
{
    // As a Radmin guest, this launcher talks to the host's backend (through the local relay), and the
    // match files are written on the host's PC: developer data is only available there.
    internal static string RemoteNotice => "You are connected to " + RadminConnection.Host + " as a guest, so this launcher is talking to the host's backend. Match data is recorded on the hosting PC: open Developer Tools there, or host the match yourself to record it here. (Disconnect in Multiplayer to use your own backend again.)";
    private readonly string? _folder;
    private readonly HttpClient _http = new() { BaseAddress = new Uri("http://127.0.0.1:3551/"), Timeout = TimeSpan.FromSeconds(30) };
    private readonly StackPanel _content = new() { Margin = new Thickness(18) };
    private readonly TextBlock _status = new() { Foreground = Ink("#9AA3C7"), FontSize = 12, TextWrapping = TextWrapping.Wrap, Margin = new Thickness(18, 0, 18, 8) };
    private readonly TextBox _version = Field("Fortnite version, e.g. 4.50"), _botVersion = Field("Bot version"), _outcome = Field("Outcome: winner / endgame / terminated_unexpectedly");
    private string _section = "matches";

    private static SolidColorBrush Ink(string hex) => (SolidColorBrush)new BrushConverter().ConvertFromString(hex)!;
    private static TextBox Field(string tip) => new() { Width = 170, Height = 30, Margin = new Thickness(0, 0, 8, 0), ToolTip = tip, Background = Ink("#141831"), Foreground = Brushes.White, BorderBrush = Ink("#22284A"), Padding = new Thickness(6, 4, 6, 4) };

    internal DiagnosticsWindow(string? folder)
    {
        _folder = folder;
        Title = "Syndicate match diagnostics (developer)"; Width = 1180; Height = 820; MinWidth = 800; MinHeight = 500;
        Background = Ink("#05060D"); Foreground = Brushes.White; FontFamily = new FontFamily("Segoe UI");
        var root = new DockPanel();
        var top = new StackPanel { Margin = new Thickness(18, 14, 18, 6) };
        top.Children.Add(new TextBlock { Text = "MATCH DIAGNOSTICS", FontSize = 26, FontWeight = FontWeights.Bold, Foreground = Ink("#FFD23F") });
        var tabs = new WrapPanel { Margin = new Thickness(0, 10, 0, 8) };
        foreach (var (key, label) in new[] { ("matches", "Matches"), ("weaknesses", "Bot weaknesses"), ("landing", "Landing / hot drops"), ("players", "Playstyles"), ("versions", "Bot versions"), ("datasets", "Datasets"), ("status", "Status") })
        {
            var b = Act(label, async () => { _section = key; await Load(); });
            tabs.Children.Add(b);
        }
        top.Children.Add(tabs);
        var filters = new WrapPanel();
        filters.Children.Add(_version); filters.Children.Add(_botVersion); filters.Children.Add(_outcome);
        filters.Children.Add(Act("Apply filters", Load));
        filters.Children.Add(Act("Export filtered matches", () => Export(false)));
        filters.Children.Add(Act("Export with raw events", () => Export(true)));
        top.Children.Add(filters);
        DockPanel.SetDock(top, Dock.Top); root.Children.Add(top);
        DockPanel.SetDock(_status, Dock.Top); root.Children.Add(_status);
        root.Children.Add(new ScrollViewer { Content = _content, VerticalScrollBarVisibility = ScrollBarVisibility.Auto });
        Content = root;
        Loaded += async (_, __) => await Load();
    }

    private Button Act(string text, Func<Task> click)
    {
        var b = new Button { Content = text, Height = 30, Padding = new Thickness(12, 0, 12, 0), Margin = new Thickness(0, 0, 8, 6), Background = Ink("#141831"), Foreground = Brushes.White, BorderBrush = Ink("#2C3466") };
        b.Click += async (_, __) => { try { await click(); } catch (Exception error) { _status.Text = "Error: " + error.Message; } };
        return b;
    }

    private string Query(string extra = "")
    {
        var parts = new List<string>();
        void Add(string key, TextBox box) { if (!string.IsNullOrWhiteSpace(box.Text)) parts.Add(key + "=" + Uri.EscapeDataString(box.Text.Trim())); }
        Add("version", _version); Add("botVersion", _botVersion); Add("outcome", _outcome);
        if (extra.Length > 0) parts.Add(extra);
        return parts.Count > 0 ? "?" + string.Join("&", parts) : "";
    }

    private async Task<JsonElement> Get(string path)
    {
        using var request = new HttpRequestMessage(HttpMethod.Get, path);
        return await Send(request);
    }

    private async Task<JsonElement> Send(HttpRequestMessage request)
    {
        var token = _folder != null && File.Exists(Path.Combine(_folder, "dev-token")) ? File.ReadAllText(Path.Combine(_folder, "dev-token")).Trim() : "";
        if (token.Length == 0) throw new InvalidOperationException("No developer token yet. Start the backend once (it creates Runtime\\Server\\telemetry\\dev-token).");
        request.Headers.Add("X-Syndicate-Dev", token);
        using var response = await _http.SendAsync(request);
        var text = await response.Content.ReadAsStringAsync();
        if ((int)response.StatusCode == 403 && RadminConnection.Remote) throw new InvalidOperationException(RemoteNotice);
        if (!response.IsSuccessStatusCode) throw new InvalidOperationException($"Backend answered {(int)response.StatusCode}: {text}");
        using var doc = JsonDocument.Parse(text);
        return doc.RootElement.Clone();
    }

    private async Task Load()
    {
        _content.Children.Clear(); _status.Text = "Loading…";
        try
        {
            switch (_section)
            {
                case "matches": ShowMatches(await Get("syndicate/telemetry/matches" + Query())); break;
                case "weaknesses": ShowWeaknesses(await Get("syndicate/telemetry/weaknesses" + Query())); break;
                case "landing": ShowLanding(await Get("syndicate/telemetry/landing" + Query())); break;
                case "players": ShowPlayers(await Get("syndicate/telemetry/players" + Query())); break;
                case "versions": await ShowVersions(); break;
                case "datasets": ShowDatasets(await Get("syndicate/telemetry/datasets")); break;
                default: ShowJson("Telemetry status", await Get("syndicate/telemetry/status")); break;
            }
            _status.Text = "Every number links back to event ids (match:seq) in the match files. Findings marked \"hypothesis\" are not proven until a benchmark shows a fix works.";
        }
        catch (HttpRequestException) { _status.Text = "The backend isn't running. Start Syndicate's local services, then press a tab again."; }
        catch (Exception error) { _status.Text = error.Message; }
    }

    private TextBlock Heading(string text) => new() { Text = text, FontSize = 18, FontWeight = FontWeights.Bold, Margin = new Thickness(0, 14, 0, 6), Foreground = Brushes.White, TextWrapping = TextWrapping.Wrap };
    private TextBlock Line(string text, string color = "#D7DCEE", bool mono = false) => new() { Text = text, FontSize = 12.5, Foreground = Ink(color), TextWrapping = TextWrapping.Wrap, Margin = new Thickness(0, 1, 0, 1), FontFamily = mono ? new FontFamily("Cascadia Mono, Consolas") : FontFamily };
    private Border Card(params UIElement[] children) { var s = new StackPanel(); foreach (var c in children) s.Children.Add(c); return new Border { Child = s, Background = Ink("#0D1020"), BorderBrush = Ink("#22284A"), BorderThickness = new Thickness(1), CornerRadius = new CornerRadius(10), Padding = new Thickness(14), Margin = new Thickness(0, 0, 0, 10) }; }

    private static string S(JsonElement e, string name) => e.ValueKind == JsonValueKind.Object && e.TryGetProperty(name, out var v) ? v.ValueKind switch { JsonValueKind.String => v.GetString() ?? "", JsonValueKind.Null => "—", JsonValueKind.Undefined => "", _ => v.ToString() } : "";
    private static string Pct(JsonElement e, string name)
    {
        if (e.ValueKind != JsonValueKind.Object || !e.TryGetProperty(name, out var r) || r.ValueKind != JsonValueKind.Object || !r.TryGetProperty("rate", out var rate) || rate.ValueKind != JsonValueKind.Number) return "—";
        string lo = r.TryGetProperty("low", out var l) && l.ValueKind == JsonValueKind.Number ? Math.Round(l.GetDouble() * 100).ToString() : "?";
        string hi = r.TryGetProperty("high", out var h) && h.ValueKind == JsonValueKind.Number ? Math.Round(h.GetDouble() * 100).ToString() : "?";
        return $"{Math.Round(rate.GetDouble() * 100)}% ({lo}-{hi}, n={S(r, "n")})";
    }

    private void ShowMatches(JsonElement list)
    {
        _content.Children.Add(Heading($"Recent matches ({list.GetArrayLength()})"));
        if (list.GetArrayLength() == 0) _content.Children.Add(Line("No matches recorded yet. Play a match with bots; the server writes Runtime\\Server\\telemetry\\<match>.jsonl.", "#FFD580"));
        foreach (var m in list.EnumerateArray())
        {
            var id = S(m, "match");
            var open = Act("Open report", async () => ShowReport(await Get("syndicate/telemetry/matches/" + Uri.EscapeDataString(id))));
            _content.Children.Add(Card(Line(S(m, "summary"), "#FFFFFF"), Line($"{S(m, "startedAt")} · {S(m, "events")} events · {S(m, "warnings")} warnings{(S(m, "complete") == "True" ? "" : " · INCOMPLETE (recovered)")}", "#9AA3C7"), open));
        }
    }

    private void ShowReport(JsonElement r)
    {
        _content.Children.Clear();
        var o = r.GetProperty("overview");
        _content.Children.Add(Heading($"Match {S(o, "match")}"));
        _content.Children.Add(Line($"{S(o, "fortniteVersion")} {S(o, "mode")} · {S(o, "outcome")} · winner: {S(o, "winner")} · {S(o, "durationSeconds")} s · bot {S(o, "botVersion")} · server build {S(o, "serverBuild")}"));
        _content.Children.Add(Line($"Integrity: {o.GetProperty("integrity")}", "#9AA3C7", true));
        Section(r, "humans", "Players", h => $"{S(h, "player")}: landed {S(h, "landedAt")}, {S(h, "kills")} kills, survived {S(h, "survivedSeconds")} s, place {S(h, "place")}, eliminated by {S(h, "eliminatedBy")}");
        _content.Children.Add(Heading("Bots")); _content.Children.Add(Line(r.GetProperty("bots").ToString(), "#D7DCEE", true));
        _content.Children.Add(Heading("Bot decisions")); _content.Children.Add(Line(r.GetProperty("decisions").ToString(), "#D7DCEE", true));
        Section(r, "suspectedMistakes", "Suspected bot mistakes (hypotheses)", m => $"{S(m, "title")}: {S(m, "observed")}. Why it may happen: {S(m, "hypothesis")}. Next: {S(m, "nextStep")} [evidence {S(m, "evidence")}]");
        Section(r, "recommendedSteps", "Recommended investigation steps", s => s.GetString() ?? "");
        Section(r, "encounters", "Combat encounters with players", e => $"[{S(e, "id")}] t={S(e, "t")} {S(e, "victim")} by {S(e, "killer")} {S(e, "weapon")} {S(e, "distanceM")} m (bot state {S(e, "botState")}) at {S(e, "place")}");
        Section(r, "timeline", "Timeline", e => $"[{S(e, "id")}] t={S(e, "t")} {S(e, "summary")}");
        Section(r, "issues", "Errors and warnings", i => $"{S(i, "severity")}: {S(i, "kind")} {S(i, "detail")}");
    }

    private void Section(JsonElement parent, string name, string title, Func<JsonElement, string> line)
    {
        if (!parent.TryGetProperty(name, out var list) || list.ValueKind != JsonValueKind.Array) return;
        _content.Children.Add(Heading($"{title} ({list.GetArrayLength()})"));
        foreach (var item in list.EnumerateArray().Take(400)) _content.Children.Add(Line(line(item), "#D7DCEE", name == "timeline" || name == "encounters"));
    }

    private void ShowWeaknesses(JsonElement list)
    {
        _content.Children.Add(Heading($"Recurring bot weaknesses ({list.GetArrayLength()})"));
        if (list.GetArrayLength() == 0) _content.Children.Add(Line("Nothing recurring yet (each pattern needs at least 5 occurrences).", "#9AA3C7"));
        foreach (var f in list.EnumerateArray())
        {
            var h = f.GetProperty("hypothesis");
            var alternatives = string.Join("; ", h.GetProperty("alternatives").EnumerateArray().Select(a => a.GetString()));
            var timeline = string.Join("\n", f.GetProperty("timeline").EnumerateArray().Select(t => $"  [{S(t, "id")}] t={S(t, "t")} {S(t, "summary")}"));
            _content.Children.Add(Card(
                Line($"{S(f, "title")}  ·  severity {S(f, "severity")}  ·  confidence {S(f, "confidence")}", "#FFD23F"),
                Line($"Observed: {S(f, "observed")} · {S(f, "encounters")} encounters · bot versions {S(f, "botVersions")}"),
                Line($"Hypothesis (not yet verified): {S(h, "cause")}", "#FFB3BA"),
                Line($"Other explanations: {alternatives}", "#9AA3C7"),
                Line($"Experiment: {S(f, "experiment")}"), Line($"Test case: {S(f, "testCase")}"), Line($"Success when: {S(f, "successCriterion")}", "#3BE48B"),
                Line(timeline.Length > 0 ? "Lead-up to an example:\n" + timeline : "", "#9AA3C7", true),
                Line($"Evidence: {S(f, "evidence")}", "#7F89AA", true)));
        }
    }

    private void ShowLanding(JsonElement data)
    {
        _content.Children.Add(Line("Definitions: " + data.GetProperty("definitions").ToString(), "#7F89AA", true));
        foreach (var version in data.GetProperty("versions").EnumerateObject())
        {
            var v = version.Value;
            _content.Children.Add(Heading($"Fortnite {version.Name}: {S(v, "matches")} matches, {S(v, "landings")} landings"));
            foreach (var ranking in v.GetProperty("rankings").EnumerateObject())
                _content.Children.Add(Line($"{ranking.Name}: {string.Join(", ", ranking.Value.EnumerateArray().Select(x => x.GetString()))}"));
            var table = new StringBuilder();
            table.AppendLine($"{"Place",-28}{"Land",6}{"Avg/m",7}{"Contested",22}{"Early elim",22}{"Survive 180s",22}{"Conf",14}");
            foreach (var p in v.GetProperty("places").EnumerateArray().Take(60))
                table.AppendLine($"{S(p, "place"),-28}{S(p, "landings"),6}{S(p, "avgPerMatch"),7}{Pct(p, "contestedRate"),22}{Pct(p, "earlyEliminationRate"),22}{Pct(p.GetProperty("survival"), "s180"),22}{S(p, "confidence"),14}");
            _content.Children.Add(Line(table.ToString(), "#D7DCEE", true));
            _content.Children.Add(Line("Explanations:", "#FFD23F"));
            foreach (var p in v.GetProperty("places").EnumerateArray().Take(20)) _content.Children.Add(Line($"{S(p, "place")}: {S(p, "explanation")}", "#9AA3C7"));
            _content.Children.Add(Heading("Bot landing strategies"));
            foreach (var s in v.GetProperty("strategies").EnumerateArray()) _content.Children.Add(Line($"{S(s, "strategy")}: {S(s, "landings")} landings, survive 180 s {Pct(s, "survival180")}, early elimination {Pct(s, "earlyElimination")}, kills/landing {S(s, "killsPerLanding")}, avg place {S(s, "averagePlace")}"));
            _content.Children.Add(Heading("Landing heatmap (schematic: 100 m cells, not drawn on a map image)"));
            _content.Children.Add(Heatmap(v.GetProperty("heatmap")));
            _content.Children.Add(Heading("Trends (recent vs baseline)"));
            _content.Children.Add(Line(v.GetProperty("trends").ToString(), "#D7DCEE", true));
            _content.Children.Add(Heading("Prediction check (holdout)"));
            _content.Children.Add(Line(v.GetProperty("prediction").ToString(), "#D7DCEE", true));
        }
    }

    // Cells "x,y" (100 m) coloured by landing count. Only real cells from the data are drawn.
    private UIElement Heatmap(JsonElement cells)
    {
        var points = new List<(int X, int Y, int N, int Dead)>();
        foreach (var c in cells.EnumerateArray())
        {
            var parts = S(c, "cell").Split(',');
            if (parts.Length == 2 && int.TryParse(parts[0], out var x) && int.TryParse(parts[1], out var y)) points.Add((x, y, int.Parse(S(c, "landings")), int.Parse(S(c, "earlyDeaths"))));
        }
        if (points.Count == 0) return Line("No positioned landings yet.", "#9AA3C7");
        int minX = points.Min(p => p.X), maxX = points.Max(p => p.X), minY = points.Min(p => p.Y), maxY = points.Max(p => p.Y), max = points.Max(p => p.N);
        double size = Math.Max(4, Math.Min(14, 900.0 / Math.Max(1, Math.Max(maxX - minX + 1, maxY - minY + 1))));
        var canvas = new Canvas { Width = (maxX - minX + 1) * size, Height = (maxY - minY + 1) * size, Background = Ink("#0B0E1F"), HorizontalAlignment = HorizontalAlignment.Left };
        foreach (var p in points)
        {
            double heat = (double)p.N / max;
            var rect = new Border { Width = size - 1, Height = size - 1, Background = new SolidColorBrush(Color.FromArgb((byte)(60 + 195 * heat), 255, (byte)(210 - 150 * heat), 63)), ToolTip = $"cell {p.X},{p.Y}: {p.N} landings, {p.Dead} early eliminations" };
            Canvas.SetLeft(rect, (p.X - minX) * size); Canvas.SetTop(rect, (maxY - p.Y) * size); canvas.Children.Add(rect);
        }
        return canvas;
    }

    private void ShowPlayers(JsonElement data)
    {
        _content.Children.Add(Heading("How players beat bots (bot deaths to humans)"));
        _content.Children.Add(Line(data.GetProperty("howHumansBeatBots").ToString(), "#D7DCEE", true));
        _content.Children.Add(Heading($"Playstyle profiles (labels need {S(data, "minMatches")}+ matches; players are pseudonyms)"));
        foreach (var p in data.GetProperty("profiles").EnumerateArray())
        {
            var labels = p.GetProperty("labels").EnumerateArray().Concat(p.GetProperty("tentativeLabels").EnumerateArray()).Select(l => $"{S(l, "label")} ({S(l, "status")}, {S(l, "confidence")}: {S(l, "basis")})");
            _content.Children.Add(Card(Line($"{S(p, "player")} · {S(p, "matches")} matches", "#FFD23F"), Line("Observed: " + p.GetProperty("observed").ToString(), "#D7DCEE", true), Line("Labels: " + string.Join("; ", labels))));
        }
    }

    private async Task ShowVersions()
    {
        var versions = await Get("syndicate/telemetry/versions" + Query());
        _content.Children.Add(Heading("Bot versions (from recorded matches)"));
        foreach (var v in versions.EnumerateArray()) _content.Children.Add(Card(Line(S(v, "botVersion"), "#FFD23F"), Line(v.ToString(), "#D7DCEE", true)));
        var baseline = Field("Baseline bot version"); var candidate = Field("Candidate bot version");
        var row = new WrapPanel(); row.Children.Add(baseline); row.Children.Add(candidate);
        var result = new StackPanel();
        row.Children.Add(Act("Compare (release gate)", async () =>
        {
            var r = await Get("syndicate/telemetry/compare" + Query("baseline=" + Uri.EscapeDataString(baseline.Text.Trim()) + "&candidate=" + Uri.EscapeDataString(candidate.Text.Trim())));
            result.Children.Clear();
            result.Children.Add(Line(S(r, "decision"), S(r, "passed") == "True" ? "#3BE48B" : "#FFB3BA"));
            foreach (var c in r.GetProperty("checks").EnumerateArray()) result.Children.Add(Line($"{S(c, "metric")}: {S(c, "verdict")} (baseline {Pct(c, "baseline")}, candidate {Pct(c, "candidate")}, p={S(c, "p")})"));
            result.Children.Add(Line("Limitation: compares real recorded matches; a fixed-seed simulator does not exist yet.", "#9AA3C7"));
        }));
        _content.Children.Add(Heading("Compare two bot versions")); _content.Children.Add(row); _content.Children.Add(result);
    }

    private void ShowDatasets(JsonElement list)
    {
        _content.Children.Add(Heading($"Training datasets ({list.GetArrayLength()})"));
        _content.Children.Add(Act("Build dataset from the current filters", async () =>
        {
            var body = new Dictionary<string, string>();
            if (!string.IsNullOrWhiteSpace(_version.Text)) body["version"] = _version.Text.Trim();
            if (!string.IsNullOrWhiteSpace(_botVersion.Text)) body["botVersion"] = _botVersion.Text.Trim();
            using var request = new HttpRequestMessage(HttpMethod.Post, "syndicate/telemetry/datasets") { Content = new StringContent(JsonSerializer.Serialize(body), Encoding.UTF8, "application/json") };
            var manifest = await Send(request);
            _status.Text = $"Dataset {S(manifest, "id")} written to {S(manifest, "folder")}.";
            await Load();
        }));
        foreach (var d in list.EnumerateArray()) _content.Children.Add(Card(Line(S(d, "id"), "#FFD23F"), Line(d.ToString(), "#D7DCEE", true)));
    }

    private void ShowJson(string title, JsonElement data) { _content.Children.Add(Heading(title)); _content.Children.Add(Line(JsonSerializer.Serialize(data, new JsonSerializerOptions { WriteIndented = true }), "#D7DCEE", true)); }

    private async Task Export(bool raw)
    {
        var dialog = new Microsoft.Win32.SaveFileDialog { FileName = $"syndicate-matches-{DateTime.Now:yyyyMMdd-HHmm}.json", Filter = "JSON|*.json" };
        if (dialog.ShowDialog(this) != true) return;
        var data = await Get("syndicate/telemetry/export" + Query(raw ? "raw=1" : ""));
        await File.WriteAllTextAsync(dialog.FileName, JsonSerializer.Serialize(data, new JsonSerializerOptions { WriteIndented = true }));
        _status.Text = "Exported to " + dialog.FileName;
    }
}
#endif