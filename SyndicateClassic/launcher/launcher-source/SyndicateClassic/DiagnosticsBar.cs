// Syndicate © 2026 Syndicate. All rights reserved. Proprietary — no copying, distribution, or derivative works. See LICENSE.
using System;
using System.Linq;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Controls.Primitives;
using System.Windows.Media;
using System.Windows.Media.Animation;

namespace SyndicateClassic;

// Diagnostics in the Project Springs style: a slim bar docked along the bottom of the window
// ("^ DIAGNOSTICS  [2 errors]  last status ...  Copy log  Clear"). Clicking it opens the log above
// the bar. The bar has its own row in the window, so opening it pushes the page up instead of
// covering it. Each line is coloured by level (INFO / OK / ERROR) with a muted timestamp.
public partial class MainWindow
{
    private Border? _diagBar;
    private Border? _railFoot;
    private Border? _diagBody;
    private StackPanel? _diagLines;
    private ScrollViewer? _diagScroll;
    private TextBlock? _diagChevron, _diagStatus, _diagErrorText;
    private Border? _diagErrorChip;
    private int _diagErrors;
    private bool _diagOpen;
    private string? _diagLast, _diagLastTime;
    private const double DiagnosticsBodyHeight = 230;

    private void BuildDiagnosticsDrawer(Grid page, int rows)
    {
        // The bar needs the window's root grid (backdrop + launcher frame) to get its own row.
        if (_modernFrame is not Grid root || root.Children.Count < 2) return;
        try
        {
            // The old log box stays as the place Log() writes to, but out of sight.
            if (LogPanel.Parent is Panel oldParent) oldParent.Children.Remove(LogPanel);
            else if (LogPanel.Parent is Decorator oldDecorator) oldDecorator.Child = null;
            LogPanel.Visibility = Visibility.Collapsed;

            if (root.RowDefinitions.Count == 0)
            {
                root.RowDefinitions.Add(new RowDefinition());
                root.RowDefinitions.Add(new RowDefinition { Height = GridLength.Auto });
            }
            // Keep the backdrop/gradient in the page row only: it ends exactly at the diagnostics bar and
            // resizes with it, so opening the log or going fullscreen can't move the fade out of place.
            Grid.SetRow(root.Children[0], 0);
            foreach (UIElement child in root.Children.OfType<UIElement>().Skip(1)) Grid.SetRow(child, 0);

            var layout = new Grid();
            layout.RowDefinitions.Add(new RowDefinition { Height = GridLength.Auto });
            layout.RowDefinitions.Add(new RowDefinition { Height = GridLength.Auto });

            // Body below the header: the header (with the close toggle) is always the bar's top row,
            // so it stays on screen whatever happens at the bottom edge.
            _diagLines = new StackPanel { Margin = new Thickness(18, 6, 18, 8) };
            _diagScroll = new ScrollViewer { Content = _diagLines, VerticalScrollBarVisibility = ScrollBarVisibility.Auto, HorizontalScrollBarVisibility = ScrollBarVisibility.Disabled };
            _diagScroll.Resources[typeof(ScrollBar)] = DarkScrollBar();
            _diagBody = new Border { Child = _diagScroll, Height = 0, ClipToBounds = true, BorderBrush = Hex("#14FFFFFF"), BorderThickness = new Thickness(0, 1, 0, 0) };
            Grid.SetRow(_diagBody, 1); layout.Children.Add(_diagBody);

            // Header: toggle (chevron + title + error chip), last status, then Copy log / Clear.
            var header = new Grid { Height = 44, Margin = new Thickness(14, 0, 14, 0) };
            header.ColumnDefinitions.Add(new ColumnDefinition { Width = GridLength.Auto });
            header.ColumnDefinitions.Add(new ColumnDefinition());
            header.ColumnDefinitions.Add(new ColumnDefinition { Width = GridLength.Auto });
            layout.Children.Add(header);

            var toggleRow = new StackPanel { Orientation = Orientation.Horizontal, VerticalAlignment = VerticalAlignment.Center };
            _diagChevron = GlyphIcon("", 10, Night.Sub); _diagChevron.Margin = new Thickness(0, 0, 10, 0);
            toggleRow.Children.Add(_diagChevron);
            toggleRow.Children.Add(NightText("DIAGNOSTICS", 12, Night.Text, bold: true).Also(t => { t.TextWrapping = TextWrapping.NoWrap; t.VerticalAlignment = VerticalAlignment.Center; }));
            _diagErrorText = NightText("", 11, "#FF8A95", bold: true).Also(t => t.TextWrapping = TextWrapping.NoWrap);
            _diagErrorChip = new Border { Child = _diagErrorText, Background = Hex("#40FF5A6A"), CornerRadius = new CornerRadius(8), Padding = new Thickness(7, 1, 7, 1), Margin = new Thickness(10, 0, 0, 0), Visibility = Visibility.Collapsed, VerticalAlignment = VerticalAlignment.Center };
            toggleRow.Children.Add(_diagErrorChip);
            var toggle = new Button { Content = toggleRow, Height = 34, Padding = new Thickness(12, 0, 14, 0), ToolTip = "Show or hide the log", VerticalAlignment = VerticalAlignment.Center };
            RoundButton(toggle, "#00000000", Night.Text, 10, "#00000000");
            toggle.MouseEnter += (_, __) => toggle.BorderBrush = Hex(Night.Sky);
            toggle.MouseLeave += (_, __) => toggle.BorderBrush = Hex("#00000000");
            toggle.BorderThickness = new Thickness(1);
            toggle.Click += (_, __) => ShowLog(!_diagOpen);
            header.Children.Add(toggle);

            _diagStatus = NightText("Ready", 12, Night.Muted); _diagStatus.TextWrapping = TextWrapping.NoWrap; _diagStatus.TextTrimming = TextTrimming.CharacterEllipsis;
            _diagStatus.VerticalAlignment = VerticalAlignment.Center; _diagStatus.Margin = new Thickness(14, 0, 14, 0);
            Grid.SetColumn(_diagStatus, 1); header.Children.Add(_diagStatus);

            var actions = new StackPanel { Orientation = Orientation.Horizontal, VerticalAlignment = VerticalAlignment.Center };
            Button Link(string glyph, string text, string tip, Action click)
            {
                var content = new StackPanel { Orientation = Orientation.Horizontal };
                if (glyph.Length > 0) content.Children.Add(GlyphIcon(glyph, 12, Night.Gold).Also(g => g.Margin = new Thickness(0, 0, 6, 0)));
                content.Children.Add(NightText(text, 12, Night.Sub, bold: true).Also(t => t.TextWrapping = TextWrapping.NoWrap));
                var b = new Button { Content = content, Height = 30, Padding = new Thickness(10, 0, 10, 0), Margin = new Thickness(4, 0, 0, 0), ToolTip = tip };
                RoundButton(b, "#00000000", Night.Text, 8);
                b.MouseEnter += (_, __) => b.Background = Hex(Night.FrostHover); b.MouseLeave += (_, __) => b.Background = Hex("#00000000");
                b.Click += (_, __) => click(); return b;
            }
            actions.Children.Add(Link("", "Copy log", "Copy the whole log, to paste it when asking for help", () =>
            {
                try { Clipboard.SetText(LogBox.Text); SetStatus("Log copied."); } catch { }
            }));
            actions.Children.Add(Link("", "Clear", "Empty the log", ClearDiagnostics));
            Grid.SetColumn(actions, 2); header.Children.Add(actions);

            _diagBar = new Border
            {
                Child = layout, Background = Hex("#E6080A14"), BorderBrush = Hex("#1FFFFFFF"), BorderThickness = new Thickness(0, 1, 0, 0),
                Margin = new Thickness(RailWidth, 0, 0, 0)   // starts right of the side menu, like the rest of the page
            };
            Grid.SetRow(_diagBar, 1);
            root.Children.Add(_diagBar);

            // Continue the side menu's black shade behind the frame and under the diagnostics bar, all the way down.
            _railFoot = new Border
            {
                Width = RailWidth, HorizontalAlignment = HorizontalAlignment.Left, IsHitTestVisible = false,
                Background = new LinearGradientBrush(NightColor("#F506070C"), NightColor("#E006070C"), 0)
            };
            Grid.SetRow(_railFoot, 0);
            Grid.SetRowSpan(_railFoot, 2);
            root.Children.Insert(1, _railFoot);

            // Lines logged before the bar existed.
            foreach (var line in LogBox.Text.Split(new[] { "\r\n", "\n" }, StringSplitOptions.RemoveEmptyEntries)) AddDiagnosticsLine(line, stamped: true);
        }
        catch { _diagBar = null; /* the old log box keeps working */ }
    }

    private static string DiagnosticsLevel(string message)
    {
        string m = message.ToLowerInvariant();
        if (m.Contains("error") || m.Contains("fail") || m.Contains("could not") || m.Contains("couldn't") || m.Contains("can't") || m.Contains("crash") || m.Contains("exception")) return "ERROR";
        if (m.Contains("ready") || m.Contains(" ok") || m.StartsWith("ok") || m.Contains("loaded") || m.Contains("connected") || m.Contains("started") || m.Contains("online")) return "OK";
        return "INFO";
    }

    // One coloured line: time (muted), level (blue / green / red), message.
    private void AddDiagnosticsLine(string text, bool stamped = false)
    {
        if (_diagLines == null) return;
        string time = DateTime.Now.ToString("HH:mm:ss"), message = text;
        if (stamped && text.Length > 10 && text[0] == '[' && text[9] == ']') { time = text.Substring(1, 8); message = text.Substring(10).TrimStart(); }
        // Errors often arrive twice (logged, then shown with SetStatus): count them once.
        if (message == _diagLast && time == _diagLastTime) return;
        _diagLast = message; _diagLastTime = time;
        string level = DiagnosticsLevel(message);
        var line = new TextBlock { FontFamily = NightMono, FontSize = 12, TextWrapping = TextWrapping.Wrap, Margin = new Thickness(0, 1, 0, 1) };
        line.Inlines.Add(new System.Windows.Documents.Run(time + "  ") { Foreground = Hex(Night.Muted) });
        line.Inlines.Add(new System.Windows.Documents.Run(level.PadRight(6)) { Foreground = Hex(level == "ERROR" ? Night.Danger : level == "OK" ? Night.Good : Night.Sky), FontWeight = FontWeights.Bold });
        line.Inlines.Add(new System.Windows.Documents.Run(message) { Foreground = Hex(level == "ERROR" ? "#FFB3BA" : "#D7DCEE") });
        _diagLines.Children.Add(line);
        while (_diagLines.Children.Count > 600) _diagLines.Children.RemoveAt(0);
        if (level == "ERROR") { _diagErrors++; SyncDiagnosticsChip(); }
        _diagScroll?.ScrollToEnd();
    }

    private void ClearDiagnostics()
    {
        LogBox.Clear();
        _diagLines?.Children.Clear();
        _diagLast = _diagLastTime = null;
        _diagErrors = 0; SyncDiagnosticsChip();
    }

    private void SyncDiagnosticsChip()
    {
        if (_diagErrorChip == null || _diagErrorText == null) return;
        _diagErrorChip.Visibility = _diagErrors > 0 ? Visibility.Visible : Visibility.Collapsed;
        _diagErrorText.Text = _diagErrors == 1 ? "1 error" : _diagErrors + " errors";
    }

    // Opens or closes the log above the bar (the page above shrinks; nothing is covered).
    private bool SetDiagnosticsOpen(bool open)
    {
        if (_diagBar == null || _diagBody == null) return false;
        _diagOpen = open;
        if (_diagChevron != null) _diagChevron.Text = open ? "" : "";
        var animation = new DoubleAnimation(open ? DiagnosticsBodyHeight : 0, TimeSpan.FromMilliseconds(MotionEnabled ? 220 : 0)) { EasingFunction = new CubicEase { EasingMode = EasingMode.EaseOut } };
        _diagBody.BeginAnimation(HeightProperty, animation);
        if (open) _diagScroll?.ScrollToEnd();
        return true;
    }

    private void SetDiagnosticsStatus(string message)
    {
        if (_diagStatus != null) _diagStatus.Text = message;
    }
}