// Syndicate © 2026 Syndicate. All rights reserved. Proprietary — no copying, distribution, or derivative works. See LICENSE.
using System;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.Linq;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Controls.Primitives;
using System.Windows.Input;
using System.Windows.Media;
using System.Windows.Threading;

namespace SyndicateClassic;

// Launcher music: a small player card in the bottom-left corner (track name, timeline, back / play-pause / skip,
// mute and volume). Tracks are embedded as SyndicateClassic.Music.<file name> from launcher-source\music\*.tadp
// ("01 - Menu Theme.tadp" shows as "Menu Theme"; the number sets the order). They play in order and repeat;
// with one track it loops. Playback goes through winmm (MenuMusicPlayer), so it works on every Windows.
// Remembered: paused (music-off.txt), volume (music-volume.txt), track (music-track.txt), all in
// %LOCALAPPDATA%\ProjectTilted. The music pauses on its own while Fortnite is open and resumes after.
public partial class MainWindow
{
    private MenuMusicPlayer? _musicLoop;
    private DispatcherTimer? _musicGameWatch, _musicUiTimer;
    private bool _musicGameOpen;      // Fortnite is running: the music pauses while it is
    private readonly List<(string Title, string Resource)> _musicTracks = new();
    private int _musicIndex;
    private bool _musicLoading;
    private Action? _musicRefresh;    // redraws the player card
    private Action? _musicPlace;      // puts the player card back in its place (after "Reset position")
    private FrameworkElement? _browserSliderHost; // the season browser's scroll bar (SeasonBrowser.cs)

    private static bool FortniteIsOpen()
    {
        try { return System.Diagnostics.Process.GetProcessesByName("FortniteClient-Win64-Shipping").Length > 0; }
        catch { return false; }
    }
    // "On" = playing (the pause button turns it off). Kept in music-off.txt as before.
    internal static bool MusicEnabled
    {
        get { try { return !File.Exists(HudFlag("music-off.txt")); } catch { return true; } }
        set { SetHudFlag("music-off.txt", !value); }
    }
    internal static double MusicVolume
    {
        get { try { var f = HudFlag("music-volume.txt"); if (File.Exists(f) && double.TryParse(File.ReadAllText(f).Trim(), NumberStyles.Float, CultureInfo.InvariantCulture, out var v)) return Math.Clamp(v, 0, 1); } catch { } return 0.30; }
        set { try { var f = HudFlag("music-volume.txt"); Directory.CreateDirectory(Path.GetDirectoryName(f)!); File.WriteAllText(f, Math.Clamp(value, 0, 1).ToString("0.###", CultureInfo.InvariantCulture)); } catch { } }
    }
    private static bool MusicMuted
    {
        get { try { return File.Exists(HudFlag("music-muted.txt")); } catch { return false; } }
        set { SetHudFlag("music-muted.txt", value); }
    }
    private static string SavedTrack
    {
        get { try { var f = HudFlag("music-track.txt"); return File.Exists(f) ? File.ReadAllText(f).Trim() : ""; } catch { return ""; } }
        set { try { var f = HudFlag("music-track.txt"); Directory.CreateDirectory(Path.GetDirectoryName(f)!); File.WriteAllText(f, value); } catch { } }
    }
    private void MusicLog(string s) { try { Log("music: " + s); } catch { } }

    // Called once the shell is built (UI thread).
    private void StartLauncherMusic()
    {
        if (_musicTracks.Count > 0) return;
        var names = typeof(MainWindow).Assembly.GetManifestResourceNames();
        foreach (var name in names.Where(n => n.StartsWith("SyndicateClassic.Music.", StringComparison.Ordinal)).OrderBy(n => n, StringComparer.OrdinalIgnoreCase))
        {
            var title = Path.GetFileNameWithoutExtension(name.Substring("SyndicateClassic.Music.".Length));
            title = System.Text.RegularExpressions.Regex.Replace(title, @"^\d+\s*[-_.]\s*", "");
            _musicTracks.Add((title.Length > 0 ? title : "Track", name));
        }
        if (_musicTracks.Count == 0 && names.Contains("SyndicateClassic.MenuThemeAdpcm")) _musicTracks.Add(("Menu Theme", "SyndicateClassic.MenuThemeAdpcm"));
        if (_musicTracks.Count == 0) { MusicLog("no tracks in this build"); _musicRefresh?.Invoke(); return; }
        _musicIndex = Math.Max(0, _musicTracks.FindIndex(t => t.Title == SavedTrack));
        _musicGameOpen = FortniteIsOpen();
        LoadMusicTrack(_musicIndex);
        _musicGameWatch = new DispatcherTimer { Interval = TimeSpan.FromSeconds(1.5) };
        _musicGameWatch.Tick += (_, __) =>
        {
            bool open = FortniteIsOpen(); if (open == _musicGameOpen) return;
            _musicGameOpen = open; ApplyMusicState();
            MusicLog(open ? "Fortnite open, pausing" : MusicEnabled ? "Fortnite closed, resuming" : "Fortnite closed (paused)");
        };
        _musicGameWatch.Start();
        _musicUiTimer = new DispatcherTimer { Interval = TimeSpan.FromMilliseconds(250) };
        _musicUiTimer.Tick += (_, __) =>
        {
            if (_musicLoop != null && _musicLoop.Ended && !_musicLoading) LoadMusicTrack(_musicIndex + 1);
            _musicRefresh?.Invoke();
        };
        _musicUiTimer.Start();
        Closed += (_, __) => { try { _musicGameWatch?.Stop(); _musicUiTimer?.Stop(); _musicLoop?.Dispose(); _musicLoop = null; } catch { } };
    }

    // Decodes a track in the background and plays it (wraps around the list).
    private void LoadMusicTrack(int index)
    {
        if (_musicTracks.Count == 0 || _musicLoading) return;
        _musicIndex = ((index % _musicTracks.Count) + _musicTracks.Count) % _musicTracks.Count;
        var (title, resource) = _musicTracks[_musicIndex];
        SavedTrack = title; _musicLoading = true; _musicRefresh?.Invoke();
        System.Threading.Tasks.Task.Run(() =>
        {
            try
            {
                using var stream = typeof(MainWindow).Assembly.GetManifestResourceStream(resource) ?? throw new IOException("track missing");
                var (pcm, rate) = MenuMusicPlayer.Decode(stream);
                Dispatcher.Invoke(() =>
                {
                    try
                    {
                        if (_musicLoop == null) _musicLoop = new MenuMusicPlayer(pcm, rate);
                        else _musicLoop.Load(pcm, rate);
                        _musicLoop.Loop = _musicTracks.Count == 1;
                        MusicLog("playing \"" + title + "\" through Windows audio");
                    }
                    catch (Exception ex) { MusicLog("FAILED to play: " + ex.Message); }
                    finally { _musicLoading = false; ApplyMusicState(); }
                });
            }
            catch (Exception ex) { Dispatcher.Invoke(() => { _musicLoading = false; MusicLog("FAILED to load \"" + title + "\": " + ex.Message); }); }
        });
    }

    private void ApplyMusicState()
    {
        if (_musicLoop != null)
        {
            _musicLoop.Volume = MusicMuted ? 0f : (float)MusicVolume;
            _musicLoop.Playing = MusicEnabled && !_musicGameOpen;
        }
        _musicRefresh?.Invoke();
    }

    // A thin bar in the launcher's style (grey track, gold fill and a gold knob), clicked or dragged to set a 0..1 value.
    private static (Grid Bar, Action<double> Set) MusicBar(double height, Action<double> changed)
    {
        var fill = new Border { Height = height, CornerRadius = new CornerRadius(height / 2), Background = Hex(Night.Gold), HorizontalAlignment = HorizontalAlignment.Left, Width = 0 };
        var knob = new Border { Width = height + 6, Height = height + 6, CornerRadius = new CornerRadius((height + 6) / 2), Background = Hex(Night.Gold), HorizontalAlignment = HorizontalAlignment.Left,
            BorderBrush = Hex("#05060D"), BorderThickness = new Thickness(1.5), RenderTransform = new TranslateTransform(), Opacity = 0 };
        // Focusable so a controller can reach it (Menu / Options on the pad): left / right move it by 5%.
        var bar = new Grid { Height = height + 8, Background = Brushes.Transparent, Cursor = Cursors.Hand, Focusable = true, FocusVisualStyle = null };
        bar.Children.Add(new Border { Height = height, CornerRadius = new CornerRadius(height / 2), Background = Hex("#33FFFFFF") });
        bar.Children.Add(fill); bar.Children.Add(knob);
        double value = 0;
        void Draw() { var w = bar.ActualWidth; fill.Width = Math.Max(0, w * value); ((TranslateTransform)knob.RenderTransform).X = Math.Clamp(w * value - knob.Width / 2, 0, Math.Max(0, w - knob.Width)); }
        void From(MouseEventArgs e) { if (bar.ActualWidth <= 0) return; value = Math.Clamp(e.GetPosition(bar).X / bar.ActualWidth, 0, 1); Draw(); changed(value); }
        bar.MouseEnter += (_, __) => knob.Opacity = 1; bar.MouseLeave += (_, __) => { if (!bar.IsMouseCaptured) knob.Opacity = 0; };
        // Handled: otherwise the window's drag-to-move (on the page background) takes the mouse and the bar can't be dragged.
        bar.MouseLeftButtonDown += (_, e) => { e.Handled = true; bar.CaptureMouse(); knob.Opacity = 1; From(e); };
        bar.MouseMove += (_, e) => { if (bar.IsMouseCaptured) From(e); };
        bar.MouseLeftButtonUp += (_, e) => { e.Handled = true; bar.ReleaseMouseCapture(); if (!bar.IsMouseOver) knob.Opacity = 0; };
        bar.SizeChanged += (_, __) => Draw();
        bar.KeyDown += (_, e) =>
        {
            if (e.Key is not (Key.Left or Key.Right)) return;
            // At an end, the key moves on instead (to the mute button left of the volume bar).
            if ((e.Key == Key.Left && value <= 0) || (e.Key == Key.Right && value >= 1)) return;
            e.Handled = true;
            value = Math.Clamp(value + (e.Key == Key.Right ? .05 : -.05), 0, 1); Draw(); changed(value);
        };
        bar.GotKeyboardFocus += (_, __) => knob.Opacity = 1;
        bar.LostKeyboardFocus += (_, __) => { if (!bar.IsMouseOver) knob.Opacity = 0; };
        return (bar, v => { if (bar.IsMouseCaptured) return; value = Math.Clamp(v, 0, 1); Draw(); });
    }

    // Settings > Launcher & files: hide the player card (the music itself follows its own settings), and where the
    // card was dragged to (fractions of the window, "x,y"; missing = the automatic place below).
    internal static bool MusicPlayerHidden { get => File.Exists(HudFlag("music-player-hidden.txt")); set => SetHudFlag("music-player-hidden.txt", value); }
    private static string MusicPlayerPosFile => HudFlag("music-player-pos.txt");
    private static Point? MusicPlayerPos
    {
        get
        {
            try
            {
                var parts = File.ReadAllText(MusicPlayerPosFile).Split(',');
                if (parts.Length == 2 && double.TryParse(parts[0], System.Globalization.NumberStyles.Float, System.Globalization.CultureInfo.InvariantCulture, out var x)
                    && double.TryParse(parts[1], System.Globalization.NumberStyles.Float, System.Globalization.CultureInfo.InvariantCulture, out var y))
                    return new Point(Math.Clamp(x, 0, 1), Math.Clamp(y, 0, 1));
            }
            catch { }
            return null;
        }
        set
        {
            try
            {
                if (value is not Point p) { if (File.Exists(MusicPlayerPosFile)) File.Delete(MusicPlayerPosFile); return; }
                Directory.CreateDirectory(Path.GetDirectoryName(MusicPlayerPosFile)!);
                File.WriteAllText(MusicPlayerPosFile, p.X.ToString("0.####", System.Globalization.CultureInfo.InvariantCulture) + "," + p.Y.ToString("0.####", System.Globalization.CultureInfo.InvariantCulture));
            }
            catch { }
        }
    }

    // The player card: a compact box right under the season browser's scroll bar (right ends lined up); on other pages,
    // at the bottom right above the diagnostics bar. Drag it by the note or the title to put it anywhere instead.
    internal FrameworkElement BuildMusicPlayer()
    {
        Button Icon(string glyph, string tip, double size = 24, double font = 11, string color = Night.Sub)
        {
            var b = new Button { Width = size, Height = size, Padding = new Thickness(0), ToolTip = tip, Content = GlyphIcon(glyph, font, color), FocusVisualStyle = null, Background = Hex("#00000000"), BorderBrush = Hex("#00000000"), BorderThickness = new Thickness(0) };
            b.Template = TiltButtonTemplate(-12);
            b.MouseEnter += (_, __) => b.Opacity = .78; b.MouseLeave += (_, __) => b.Opacity = 1;
            return b;
        }
        var title = Display("Music", 16, Night.Text).Also(t => { t.TextTrimming = TextTrimming.CharacterEllipsis; t.Margin = new Thickness(0, -2, 0, 0); });
        var sub = NightText("", 9.5, Night.Muted).Also(t => { t.TextWrapping = TextWrapping.NoWrap; t.TextTrimming = TextTrimming.CharacterEllipsis; });
        var back = Icon("\uE892", "Back (restart / previous)"); var play = Icon("\uE769", "Pause", 28, 13, Night.OnGold); var skip = Icon("\uE893", "Skip");
        play.Background = GoldFill(); play.BorderBrush = Hex("#B07A00"); play.BorderThickness = new Thickness(0, 0, 0, 2);
        var mute = Icon("\uE767", "Mute", 22, 10, Night.Muted);

        // Row 1: note, title / time, back - play - skip
        var top = new Grid();
        foreach (var w in new[] { GridLength.Auto, new GridLength(1, GridUnitType.Star), GridLength.Auto }) top.ColumnDefinitions.Add(new ColumnDefinition { Width = w });
        var note = GlyphIcon("\uE8D6", 13, Night.Gold).Also(g => { g.HorizontalAlignment = HorizontalAlignment.Center; g.VerticalAlignment = VerticalAlignment.Center; });
        var noteTile = new Border { Width = 30, Height = 30, CornerRadius = new CornerRadius(9, 2, 9, 2), Background = Hex("#1FFFD23F"), BorderBrush = Hex("#40FFD23F"), BorderThickness = new Thickness(1), Child = note, Margin = new Thickness(0, 0, 9, 0), VerticalAlignment = VerticalAlignment.Center };
        var words = new StackPanel { VerticalAlignment = VerticalAlignment.Center }; words.Children.Add(title); words.Children.Add(sub);
        var buttons = new StackPanel { Orientation = Orientation.Horizontal, VerticalAlignment = VerticalAlignment.Center, Margin = new Thickness(6, 0, 0, 0) };
        buttons.Children.Add(back); buttons.Children.Add(play); buttons.Children.Add(skip);
        top.Children.Add(noteTile); Grid.SetColumn(words, 1); top.Children.Add(words); Grid.SetColumn(buttons, 2); top.Children.Add(buttons);

        // Row 2: timeline (click or drag to jump)
        var (timeline, setTimeline) = MusicBar(3, v => { if (_musicLoop != null) { _musicLoop.Seek(v * _musicLoop.Duration); _musicRefresh?.Invoke(); } });
        timeline.Margin = new Thickness(0, 6, 0, 0);

        // Row 3: mute and volume, in the same style
        var (volumeBar, setVolume) = MusicBar(3, v => { MusicVolume = v; if (MusicMuted && v > 0) MusicMuted = false; ApplyMusicState(); });
        var volumeRow = new Grid { Margin = new Thickness(0, 2, 0, 0) };
        volumeRow.ColumnDefinitions.Add(new ColumnDefinition { Width = GridLength.Auto }); volumeRow.ColumnDefinitions.Add(new ColumnDefinition());
        volumeBar.Margin = new Thickness(4, 0, 2, 0); volumeBar.VerticalAlignment = VerticalAlignment.Center; volumeBar.ToolTip = "Volume";
        volumeRow.Children.Add(mute); Grid.SetColumn(volumeBar, 1); volumeRow.Children.Add(volumeBar);

        var body = new StackPanel();
        body.Children.Add(NightEyebrow("Now playing").Also(t => t.Margin = new Thickness(0, 0, 0, 5)));
        body.Children.Add(top); body.Children.Add(timeline); body.Children.Add(volumeRow);
        var card = TiltTile(body, new Thickness(12, 9, 12, 9));
        card.Width = 244;
        card.HorizontalAlignment = HorizontalAlignment.Right; card.VerticalAlignment = VerticalAlignment.Bottom; card.Margin = new Thickness(0, 0, 40, 12);
        void Place()
        {
            if (card.Parent is not FrameworkElement parent || parent.ActualHeight <= 0) return;
            if (MusicPlayerPos is Point dragged)
            {
                // Where the player dragged it, kept inside the window when it is resized.
                double left = dragged.X * Math.Max(0, parent.ActualWidth - card.ActualWidth), topY = dragged.Y * Math.Max(0, parent.ActualHeight - card.ActualHeight);
                card.HorizontalAlignment = HorizontalAlignment.Left; card.VerticalAlignment = VerticalAlignment.Top;
                if (Math.Abs(card.Margin.Left - left) > .5 || Math.Abs(card.Margin.Top - topY) > .5) card.Margin = new Thickness(left, topY, 0, 0);
                return;
            }
            // Home (SyndicateHome.cs): bottom left, on the Chapter 1 card's empty corner, clear of the ENTER buttons.
            var side = _syndicateHomeView is { IsVisible: true } ? HorizontalAlignment.Left : HorizontalAlignment.Right;
            if (card.HorizontalAlignment != side) card.HorizontalAlignment = side;
            // Default (pages without the season browser): bottom right, above the diagnostics bar.
            var align = VerticalAlignment.Bottom; var margin = side == HorizontalAlignment.Left ? new Thickness(RailWidth + 40, 0, 0, 34) : new Thickness(0, 0, 40, 12);
            var bar = _browserSliderHost;
            if (bar != null && bar.IsVisible && bar.ActualWidth > 0)
                try
                {
                    // Right under the season browser's scroll bar, right ends lined up.
                    var at = bar.TranslatePoint(new Point(0, 0), parent);
                    align = VerticalAlignment.Top;
                    margin = new Thickness(0, at.Y + bar.ActualHeight + 4, Math.Max(8, parent.ActualWidth - at.X - bar.ActualWidth), 0);
                }
                catch { }
            if (card.VerticalAlignment != align) card.VerticalAlignment = align;
            if (Math.Abs(card.Margin.Left - margin.Left) > .5 || Math.Abs(card.Margin.Right - margin.Right) > .5 || Math.Abs(card.Margin.Top - margin.Top) > .5 || Math.Abs(card.Margin.Bottom - margin.Bottom) > .5) card.Margin = margin;
        }
        SizeChanged += (_, __) => Place();
        _musicPlace = Place;
        // Dragging: hold the note or the title and move. The spot is saved as a share of the window.
        Point? grab = null;
        foreach (var handle in new FrameworkElement[] { noteTile, words })
        {
            handle.Cursor = System.Windows.Input.Cursors.SizeAll; handle.ToolTip = "Drag to move the music player";
            handle.MouseLeftButtonDown += (_, e) => { if (card.Parent is FrameworkElement) { grab = e.GetPosition(card); handle.CaptureMouse(); e.Handled = true; } };
            handle.MouseMove += (_, e) =>
            {
                if (grab is not Point g || card.Parent is not FrameworkElement parent) return;
                var at = e.GetPosition(parent);
                double spanX = Math.Max(1, parent.ActualWidth - card.ActualWidth), spanY = Math.Max(1, parent.ActualHeight - card.ActualHeight);
                MusicPlayerPos = new Point(Math.Clamp((at.X - g.X) / spanX, 0, 1), Math.Clamp((at.Y - g.Y) / spanY, 0, 1));
                Place();
            };
            handle.MouseLeftButtonUp += (_, __) => { grab = null; handle.ReleaseMouseCapture(); };
        }

        play.Click += (_, __) => { MusicEnabled = !MusicEnabled; ApplyMusicState(); };
        skip.Click += (_, __) => LoadMusicTrack(_musicIndex + 1);
        back.Click += (_, __) => { if (_musicLoop != null && _musicLoop.Position > 3) { _musicLoop.Seek(0); _musicRefresh?.Invoke(); } else LoadMusicTrack(_musicIndex - 1); };
        mute.Click += (_, __) => { MusicMuted = !MusicMuted; ApplyMusicState(); };

        static string Clock(double s) { s = Math.Max(0, s); return ((int)s / 60) + ":" + ((int)s % 60).ToString("00"); }
        _musicRefresh = () =>
        {
            // Hidden while Settings is open (the card sits above the page, so it would cover it).
            if (_musicTracks.Count == 0 || MusicPlayerHidden || SettingsPanel.Visibility == Visibility.Visible) { card.Visibility = Visibility.Collapsed; return; }
            card.Visibility = Visibility.Visible; Place();
            title.Text = _musicTracks[Math.Clamp(_musicIndex, 0, _musicTracks.Count - 1)].Title;
            double d = _musicLoop?.Duration ?? 0, p = Math.Min(_musicLoop?.Position ?? 0, d);
            sub.Text = _musicLoading ? "Loading…" : _musicGameOpen && MusicEnabled ? "Paused while Fortnite is open" : Clock(p) + " / " + Clock(d) + (_musicTracks.Count > 1 ? $"  ·  {_musicIndex + 1} of {_musicTracks.Count}" : "");
            ((TextBlock)play.Content).Text = MusicEnabled ? "\uE769" : "\uE768"; play.ToolTip = MusicEnabled ? "Pause" : "Play";
            ((TextBlock)mute.Content).Text = MusicMuted || MusicVolume <= 0 ? "\uE74F" : "\uE767"; mute.ToolTip = MusicMuted ? "Unmute" : "Mute";
            skip.IsEnabled = back.IsEnabled = !_musicLoading;
            setTimeline(d > 0 ? p / d : 0);
            setVolume(MusicMuted ? 0 : MusicVolume);
        };
        card.Loaded += (_, __) => _musicRefresh?.Invoke();
        return card;
    }
}
