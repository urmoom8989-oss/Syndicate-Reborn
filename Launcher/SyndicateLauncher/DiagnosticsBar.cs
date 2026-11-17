using System;
using System.Collections.Generic;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Documents;
using System.Windows.Media;
using System.Windows.Media.Animation;

namespace SyndicateLauncher;

// Syndicate's diagnostics bar, adapted to Syndicate's visual language: a slim bar docked at the
// bottom of the window ("^ DIAGNOSTICS  [2 errors]  last status ...  Copy log  Clear"). Clicking
// it opens the log above the bar, so opening it pushes the page up instead of covering it.
// Each line is coloured by level (INFO / OK / ERROR) with a muted timestamp.
public partial class MainWindow
{
    private const double DiagnosticsBodyHeight = 230;
    private const int DiagnosticsMaxLines = 600;
    private readonly List<string> _diagHistory = new();
    private bool _diagOpen;
    private int _diagErrors;
    private string? _diagLast, _diagLastTime;
    private DateTime _diagLastAt;

    private void OnDiagnosticsLog(string message)
    {
        if (!Dispatcher.CheckAccess())
        {
            Dispatcher.BeginInvoke(new Action(() => OnDiagnosticsLog(message)));
            return;
        }

        DiagnosticsStatus.Text = message;
        // "Preparing the Syndicate server... 38s" changes every second; keep it in the status
        // line but out of the log so real events stay readable.
        if (System.Text.RegularExpressions.Regex.IsMatch(message, @"\.\.\. \d+s$")) return;
        // The short "Couldn't start: ..." status repeats the problem block that ShowProblem already wrote.
        if (message.StartsWith("Couldn't start: ", StringComparison.Ordinal)) return;
        AddDiagnosticsLine(message);
    }

    // Lines that belong to an unresolved problem (title + fix steps). A launch that works removes them.
    private readonly List<UIElement> _problemLines = new();
    private readonly List<string> _problemTitles = new();

    /// <summary>One red line saying what went wrong, then the numbered steps that fix it.</summary>
    private void ShowProblem(SyndicateLauncher.Services.LaunchProblem problem)
    {
        if (!Dispatcher.CheckAccess())
        {
            Dispatcher.BeginInvoke(new Action(() => ShowProblem(problem)));
            return;
        }
        if (DiagnosticsLines is null) return;
        string time = DateTime.Now.ToString("HH:mm:ss");
        var block = new StackPanel { Margin = new Thickness(0, 4, 0, 6) };
        var title = NewLogLine();
        title.Inlines.Add(new Run(time + "  ") { Foreground = (Brush)FindResource("DimTextBrush") });
        title.Inlines.Add(new Run("ERROR".PadRight(7)) { Foreground = (Brush)FindResource("DangerBrush"), FontWeight = FontWeights.Bold });
        title.Inlines.Add(new Run(problem.Title) { Foreground = new SolidColorBrush(Color.FromRgb(0xFF, 0xB3, 0xBA)), FontWeight = FontWeights.SemiBold });
        block.Children.Add(title);
        _diagHistory.Add($"{time}  {"ERROR".PadRight(7)}{problem.Title}");

        var steps = new List<string>(problem.Steps);
        for (int i = 0; i < steps.Count; i++)
        {
            var step = NewLogLine();
            step.Margin = new Thickness(78, 1, 0, 1);
            step.Inlines.Add(new Run((i + 1) + ". ") { Foreground = (Brush)FindResource("SuccessBrush"), FontWeight = FontWeights.Bold });
            step.Inlines.Add(new Run(steps[i]) { Foreground = new SolidColorBrush(Color.FromRgb(0xD7, 0xDC, 0xEE)) });
            block.Children.Add(step);
            _diagHistory.Add($"          FIX    {i + 1}. {steps[i]}");
        }
        if (problem.LogFolder is { Length: > 0 } folder)
        {
            var logs = NewLogLine();
            logs.Margin = new Thickness(78, 1, 0, 1);
            var link = new Hyperlink(new Run("Open the log folder")) { Foreground = (Brush)FindResource("MutedTextBrush") };
            link.Click += (_, _) =>
            {
                try { System.Diagnostics.Process.Start(new System.Diagnostics.ProcessStartInfo(folder) { UseShellExecute = true }); }
                catch (Exception) { }
            };
            logs.Inlines.Add(link);
            block.Children.Add(logs);
            _diagHistory.Add($"          LOGS   {folder}");
        }

        DiagnosticsLines.Children.Add(block);
        _problemLines.Add(block);
        _problemTitles.Add(problem.Title);
        _diagLast = _diagLastTime = null;
        TrimDiagnostics();
        _diagErrors++;
        SyncDiagnosticsChip();
        DiagnosticsStatus.Text = problem.Title;
        if (!_diagOpen) SetDiagnosticsOpen(true);
        else DiagnosticsScroll.ScrollToEnd();
    }

    /// <summary>
    /// The game started fine: the earlier errors are fixed, so take them (and every red line) off the
    /// panel and say so once. COPY LOG still has them.
    /// </summary>
    private void ResolveProblems()
    {
        if (!Dispatcher.CheckAccess())
        {
            Dispatcher.BeginInvoke(new Action(ResolveProblems));
            return;
        }
        if (_diagErrors == 0 && _problemLines.Count == 0) return;
        foreach (var line in _problemLines) DiagnosticsLines.Children.Remove(line);
        foreach (var line in _errorLines) DiagnosticsLines.Children.Remove(line);
        _buildProblemLines.Clear();
        int fixedCount = _problemTitles.Count;
        _problemLines.Clear();
        _problemTitles.Clear();
        _errorLines.Clear();
        _diagErrors = 0;
        SyncDiagnosticsChip();
        AddDiagnosticsLine(fixedCount == 1 ? "Fixed: the earlier problem is gone and the game started." : "Fixed: the earlier problems are gone and the game started.");
    }

    private static TextBlock NewLogLine() => new()
    {
        FontFamily = new FontFamily("Cascadia Mono, Consolas"),
        FontSize = 11.5,
        TextWrapping = TextWrapping.Wrap,
        Margin = new Thickness(0, 1, 0, 1)
    };

    private readonly List<UIElement> _errorLines = new();

    private void TrimDiagnostics()
    {
        while (DiagnosticsLines.Children.Count > DiagnosticsMaxLines)
        {
            var first = DiagnosticsLines.Children[0];
            DiagnosticsLines.Children.RemoveAt(0);
            _problemLines.Remove(first);
            _buildProblemLines.Remove(first);
            _errorLines.Remove(first);
        }
        while (_diagHistory.Count > DiagnosticsMaxLines * 2) _diagHistory.RemoveAt(0);
    }

    private static string DiagnosticsLevel(string message)
    {
        string m = message.ToLowerInvariant();
        if (m.Contains("error") || m.Contains("fail") || m.Contains("could not") || m.Contains("couldn't")
            || m.Contains("can't") || m.Contains("can\u2019t") || m.Contains("crash") || m.Contains("exception")
            || m.Contains("exited") || m.Contains("timed out") || m.Contains("timeout") || m.Contains("missing"))
            return "ERROR";
        if (m.Contains("ready") || m.Contains(" ok") || m.StartsWith("ok") || m.Contains("loaded")
            || m.Contains("connected") || m.Contains("started") || m.Contains("online") || m.Contains("joined"))
            return "OK";
        return "INFO";
    }

    // One coloured line: time (muted), level (grey / green / red), message.
    private TextBlock? AddDiagnosticsLine(string text, bool stamped = false)
    {
        if (DiagnosticsLines is null) return null;
        string time = DateTime.Now.ToString("HH:mm:ss"), message = text.Trim();
        if (message.Length == 0) return null;
        if (stamped && message.Length > 10 && message[0] == '[' && message[9] == ']')
        {
            time = message.Substring(1, 8);
            message = message.Substring(10).TrimStart();
        }

        // Status often repeats (for example "Detected Fortnite 1.7.2" three times while a folder is
        // read); keep the log readable by dropping the exact same line within a few seconds.
        var now = DateTime.Now;
        if (message == _diagLast && (now - _diagLastAt).TotalSeconds < 5) return null;
        _diagLast = message;
        _diagLastTime = time;
        _diagLastAt = now;

        string level = DiagnosticsLevel(message);
        var line = NewLogLine();
        line.Inlines.Add(new Run(time + "  ") { Foreground = (Brush)FindResource("DimTextBrush") });
        line.Inlines.Add(new Run(level.PadRight(7))
        {
            Foreground = (Brush)FindResource(level == "ERROR" ? "DangerBrush" : level == "OK" ? "SuccessBrush" : "MutedTextBrush"),
            FontWeight = FontWeights.Bold
        });
        line.Inlines.Add(new Run(message)
        {
            Foreground = new SolidColorBrush(level == "ERROR"
                ? Color.FromRgb(0xFF, 0xB3, 0xBA)
                : Color.FromRgb(0xD7, 0xDC, 0xEE))
        });

        DiagnosticsLines.Children.Add(line);
        _diagHistory.Add($"{time}  {level.PadRight(7)}{message}");
        TrimDiagnostics();

        if (level == "ERROR")
        {
            _errorLines.Add(line);
            _diagErrors++;
            SyncDiagnosticsChip();
        }
        if (_diagOpen) DiagnosticsScroll.ScrollToEnd();
        return line;
    }

    // Red lines about the chosen Fortnite folder (wrong folder, unsupported build, ...). They go away
    // as soon as a working build is picked.
    private readonly List<UIElement> _buildProblemLines = new();

    private void ShowBuildProblem(string message)
    {
        DiagnosticsStatus.Text = message;
        if (AddDiagnosticsLine(message) is { } line && _errorLines.Contains(line)) _buildProblemLines.Add(line);
    }

    private void ResolveBuildProblems()
    {
        if (_buildProblemLines.Count == 0) return;
        foreach (var line in _buildProblemLines)
        {
            DiagnosticsLines.Children.Remove(line);
            if (_errorLines.Remove(line)) _diagErrors = Math.Max(0, _diagErrors - 1);
        }
        _buildProblemLines.Clear();
        SyncDiagnosticsChip();
    }

    private void DiagnosticsToggle_Click(object sender, RoutedEventArgs e) => SetDiagnosticsOpen(!_diagOpen);

    // Opens or closes the log above the bar (the page above shrinks; nothing is covered).
    private void SetDiagnosticsOpen(bool open)
    {
        _diagOpen = open;
        DiagnosticsChevron.Text = open ? "\uE70D" : "\uE70E";
        var animation = new DoubleAnimation(open ? DiagnosticsBodyHeight : 0, TimeSpan.FromMilliseconds(220))
        {
            EasingFunction = new CubicEase { EasingMode = EasingMode.EaseOut }
        };
        DiagnosticsBody.BeginAnimation(FrameworkElement.HeightProperty, animation);
        if (open) Dispatcher.BeginInvoke(new Action(() => DiagnosticsScroll.ScrollToEnd()));
    }

    private void DiagnosticsCopy_Click(object sender, RoutedEventArgs e)
    {
        try
        {
            Clipboard.SetText(string.Join(Environment.NewLine, _diagHistory));
            DiagnosticsStatus.Text = "Log copied.";
        }
        catch { }
    }

    private void DiagnosticsClear_Click(object sender, RoutedEventArgs e) => ClearDiagnostics();

    private void ClearDiagnostics()
    {
        DiagnosticsLines.Children.Clear();
        _problemLines.Clear();
        _problemTitles.Clear();
        _errorLines.Clear();
        _buildProblemLines.Clear();
        _diagHistory.Clear();
        _diagLast = _diagLastTime = null;
        _diagErrors = 0;
        SyncDiagnosticsChip();
        DiagnosticsStatus.Text = "Ready";
    }

    private void SyncDiagnosticsChip()
    {
        DiagnosticsErrorChip.Visibility = _diagErrors > 0 ? Visibility.Visible : Visibility.Collapsed;
        DiagnosticsErrorText.Text = _diagErrors == 1 ? "1 error" : _diagErrors + " errors";
    }
}
