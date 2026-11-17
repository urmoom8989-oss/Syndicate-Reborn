// Syndicate © 2026 Syndicate. All rights reserved. Proprietary — no copying, distribution, or derivative works. See LICENSE.
using System;
using System.Collections.Generic;
using System.Linq;
using System.Runtime.InteropServices;
using System.Threading;
using System.Windows;
using System.Windows.Automation.Peers;
using System.Windows.Automation.Provider;
using System.Windows.Controls;
using System.Windows.Controls.Primitives;
using System.Windows.Documents;
using System.Windows.Input;
using System.Windows.Media;
using System.Windows.Media.Imaging;
using System.Windows.Threading;
using HidSharp;

namespace SyndicateClassic;

// Use the launcher with a controller (Xbox, or PS4/PS5 read straight from the pad):
//   LB / RB        previous / next tab (Home, Store, Multiplayer); in Settings, previous / next page
//   D-pad / L stick move between buttons (on Home, left / right picks the season)
//   A              press the highlighted button or flip the highlighted switch
//   B              back (closes Settings, the profile card, the store...)
//   Y              PLAY the season shown on Home
//   X              open / close Settings
//   View / Share   side menu (profile, item shop, log, updates); B or right leaves it
//   Menu / Options music player (play, skip, seek and volume bars); B leaves it
//   R stick        scroll
// Sliders: left / right change the value, up / down move on. Text boxes: A opens an on-screen keyboard
// (X backspace, Y space, LB shift, B done).
// Only while the launcher is the active window. A gold ring shows what A will press, and button
// hints (controller-icons\*.png, embedded as SyndicateClassic.Pad.<name>) appear once controller input is seen.
// Toggle: Settings > Modded (off = %LOCALAPPDATA%\ProjectTilted\launcher-pad-off.txt).
public partial class MainWindow
{
    [Flags]
    private enum Pad : uint
    {
        None = 0, Up = 1, Down = 2, Left = 4, Right = 8, A = 16, B = 32, X = 64, Y = 128,
        LB = 256, RB = 512, Start = 1024, Back = 2048
    }

    private DispatcherTimer? _padTimer;
    private Pad _padHeld;
    private Pad _padRepeatDir;
    private long _padRepeatAt;
    private bool _padMode;               // last input came from a controller: show the focus ring
    private bool _padConnected;
    private Point _padMouseAt = new(double.NaN, double.NaN);
    private Border? _padHints;
    private StackPanel? _padHintRow;
    private string _padHintKey = "";
    private readonly List<FrameworkElement> _padRailIcons = new();
    private PadFocusRing? _padRing;
    private UIElement? _padRingOn;
    private Action<int>? _settingsStep;   // SettingsDesign: previous / next settings page
    private static SonyNavReader? _sonyNav;
    private FrameworkElement? _padRail;       // ModernShell.BuildRail: the left column
    private FrameworkElement? _padMusicCard;  // LauncherMusic: the player card
    private bool _padInRail, _padInMusic;     // View / Menu put the controller on the rail or the music card

    internal static bool LauncherPadEnabled
    {
        get { try { return !System.IO.File.Exists(HudFlag("launcher-pad-off.txt")); } catch { return true; } }
        set { SetHudFlag("launcher-pad-off.txt", !value); }
    }

    // Called once the modern shell is built (page = the main grid, rows = its row count).
    private void StartControllerNavigation(Grid page, int rows)
    {
        if (_padTimer != null) return;
        try { BuildPadHints(page, rows); } catch { }
        try { AddRailPadIcons(); } catch { }
        _padTimer = new DispatcherTimer(DispatcherPriority.Input) { Interval = TimeSpan.FromMilliseconds(16) };
        _padTimer.Tick += (_, __) => { try { TickPad(); } catch { } };
        _padTimer.Start();
        // Mouse or keyboard use hides the focus ring again.
        PreviewMouseMove += (_, e) =>
        {
            var p = e.GetPosition(this);
            if (!double.IsNaN(_padMouseAt.X) && (Math.Abs(p.X - _padMouseAt.X) > 4 || Math.Abs(p.Y - _padMouseAt.Y) > 4)) SetPadMode(false);
            _padMouseAt = p;
        };
        PreviewMouseDown += (_, __) => SetPadMode(false);
        PreviewKeyDown += (_, e) => { if (!_padSynthetic) SetPadMode(false); };
        Closed += (_, __) => { _padTimer?.Stop(); _sonyNav?.Dispose(); _sonyNav = null; };
    }

    // ---------------------------------------------------------------- input

    private void TickPad()
    {
        bool enabled = LauncherPadEnabled;
        bool gameRunning = _game is { HasExited: false };
        // PS pads: read them here while the game is closed. While it runs, ControllerService turns them
        // into an Xbox pad, which XInput below sees.
        bool wantSony = enabled && !gameRunning && LauncherPrefs.PlayStationControllers;
        if (wantSony && _sonyNav == null) _sonyNav = new SonyNavReader();
        else if (!wantSony && _sonyNav != null) { _sonyNav.Dispose(); _sonyNav = null; }

        var (state, connected, lx, ly, ry) = ReadPads();
        if (connected != _padConnected) { _padConnected = connected; if (!connected) _padMode = false; UpdatePadHints(force: true); UpdatePadRing(); }
        if (!enabled || !IsActive || WindowState == WindowState.Minimized) { _padHeld = state; return; }

        // Sticks act like the d-pad (with a dead zone) for moving around.
        if (lx < -0.55) state |= Pad.Left; else if (lx > 0.55) state |= Pad.Right;
        if (ly > 0.55) state |= Pad.Up; else if (ly < -0.55) state |= Pad.Down;

        var pressed = state & ~_padHeld;
        _padHeld = state;
        if (pressed != Pad.None || Math.Abs(ry) > 0.25) SetPadMode(true);

        // Directions repeat while held: first after 380 ms, then every 110 ms.
        const Pad dirs = Pad.Up | Pad.Down | Pad.Left | Pad.Right;
        var dir = state & dirs;
        long now = Environment.TickCount64;
        Pad fire = pressed & dirs;
        if (dir == Pad.None) _padRepeatDir = Pad.None;
        else if (fire != Pad.None) { _padRepeatDir = dir; _padRepeatAt = now + 380; }
        else if (dir == _padRepeatDir && now >= _padRepeatAt) { fire = dir; _padRepeatAt = now + 110; }
        if ((fire & Pad.Up) != 0) PadMove(Key.Up, FocusNavigationDirection.Up);
        else if ((fire & Pad.Down) != 0) PadMove(Key.Down, FocusNavigationDirection.Down);
        if ((fire & Pad.Left) != 0) PadMove(Key.Left, FocusNavigationDirection.Left);
        else if ((fire & Pad.Right) != 0) PadMove(Key.Right, FocusNavigationDirection.Right);

        if (PadKeyboardOpen)
        {
            // On-screen keyboard: A presses the key, X backspace, Y space, LB / RB shift, B or Menu done.
            if ((pressed & Pad.A) != 0) PadActivate();
            if ((pressed & Pad.X) != 0) PadKeyBackspace();
            if ((pressed & Pad.Y) != 0) PadKeyType(" ");
            if ((pressed & (Pad.LB | Pad.RB)) != 0) PadKeyShift();
            if ((pressed & (Pad.B | Pad.Start)) != 0) ClosePadKeyboard();
        }
        else
        {
            if ((pressed & Pad.A) != 0) PadActivate();
            if ((pressed & Pad.B) != 0) PadBack();
            if ((pressed & Pad.Y) != 0) { if (_padInMusic) { MusicMuted = !MusicMuted; ApplyMusicState(); } else PadPlay(); }
            if ((pressed & Pad.X) != 0) { _padInRail = _padInMusic = false; SettingsButton_Click(this, new RoutedEventArgs()); FocusPadRoot(); }
            if ((pressed & Pad.Back) != 0) TogglePadRail();
            if ((pressed & Pad.Start) != 0) TogglePadMusic();
            if ((pressed & Pad.LB) != 0) PadTab(-1);
            if ((pressed & Pad.RB) != 0) PadTab(+1);
        }
        if (Math.Abs(ry) > 0.25) PadScroll(ry);

        UpdatePadRing();
        UpdatePadHints(force: false);
    }

    private (Pad State, bool Connected, double LX, double LY, double RY) ReadPads()
    {
        Pad state = Pad.None; bool connected = false; double lx = 0, ly = 0, ry = 0;
        for (uint i = 0; i < 4; i++)
        {
            if (XInput.Get(i, out var s) != 0) continue;
            connected = true;
            var g = s.Pad;
            if ((g.Buttons & 0x0001) != 0) state |= Pad.Up;
            if ((g.Buttons & 0x0002) != 0) state |= Pad.Down;
            if ((g.Buttons & 0x0004) != 0) state |= Pad.Left;
            if ((g.Buttons & 0x0008) != 0) state |= Pad.Right;
            if ((g.Buttons & 0x0010) != 0) state |= Pad.Start;
            if ((g.Buttons & 0x0020) != 0) state |= Pad.Back;
            if ((g.Buttons & 0x0100) != 0) state |= Pad.LB;
            if ((g.Buttons & 0x0200) != 0) state |= Pad.RB;
            if ((g.Buttons & 0x1000) != 0) state |= Pad.A;
            if ((g.Buttons & 0x2000) != 0) state |= Pad.B;
            if ((g.Buttons & 0x4000) != 0) state |= Pad.X;
            if ((g.Buttons & 0x8000) != 0) state |= Pad.Y;
            Bigger(ref lx, g.LX / 32768.0); Bigger(ref ly, g.LY / 32768.0); Bigger(ref ry, g.RY / 32768.0);
        }
        if (_sonyNav?.Latest is ControllerService.PadState p)
        {
            connected = true;
            int hat = p.Face & 0x0F;
            if (hat is 0 or 1 or 7) state |= Pad.Up;
            if (hat is 1 or 2 or 3) state |= Pad.Right;
            if (hat is 3 or 4 or 5) state |= Pad.Down;
            if (hat is 5 or 6 or 7) state |= Pad.Left;
            if ((p.Face & 0x20) != 0) state |= Pad.A;   // Cross
            if ((p.Face & 0x40) != 0) state |= Pad.B;   // Circle
            if ((p.Face & 0x10) != 0) state |= Pad.X;   // Square
            if ((p.Face & 0x80) != 0) state |= Pad.Y;   // Triangle
            if ((p.Shoulders & 0x01) != 0) state |= Pad.LB;
            if ((p.Shoulders & 0x02) != 0) state |= Pad.RB;
            if ((p.Shoulders & 0x10) != 0) state |= Pad.Back;
            if ((p.Shoulders & 0x20) != 0) state |= Pad.Start;
            // HID sticks: 0..255, 128 = centre, Y down is positive.
            Bigger(ref lx, (p.LX - 128) / 128.0); Bigger(ref ly, (128 - p.LY) / 128.0); Bigger(ref ry, (128 - p.RY) / 128.0);
        }
        return (state, connected, lx, ly, ry);
        static void Bigger(ref double v, double n) { if (Math.Abs(n) > Math.Abs(v)) v = n; }
    }

    private void SetPadMode(bool on)
    {
        if (_padMode == on) return;
        _padMode = on;
        if (on && Keyboard.FocusedElement is not UIElement { IsVisible: true }) FocusPadRoot();
        UpdatePadRing();
        UpdatePadHints(force: true);
    }

    // ---------------------------------------------------------------- actions

    // The screen that has the controller now: the topmost open overlay, else the current tab.
    private FrameworkElement? PadRoot()
    {
        if (PadKeyboardOpen) return _padKeyboard;
        if (OpenPadPopup() is { } popup) return popup.Child as FrameworkElement;
        if (_padInRail && _padRail is { IsVisible: true }) return _padRail;
        if (_padInMusic && _padMusicCard is { IsVisible: true }) return _padMusicCard;
        _padInRail = _padInMusic = false;
        if (_profileOverlay is { Visibility: Visibility.Visible }) return _profileOverlay;
        if (AuthOverlay is { Visibility: Visibility.Visible })
            return _pickerDialog is { Visibility: Visibility.Visible, IsVisible: true } ? _pickerDialog : AuthOverlay;   // rename / delete player
        if (SettingsPanel is { Visibility: Visibility.Visible }) return SettingsPanel;
        if (_secretShopView is { Visibility: Visibility.Visible }) return _secretShopView;
        if (PassShop is { Visibility: Visibility.Visible }) return PassShop;
        if (_multiplayerView is { Visibility: Visibility.Visible }) return _multiplayerView;
        if (_statsView is { Visibility: Visibility.Visible }) return _statsView;
        if (_syndicateHomeView is { IsVisible: true }) return _syndicateHomeView;
        return _seasonBrowserView is { IsVisible: true } ? _seasonBrowserView : _homeView;
    }

    // An open menu popup (season list, chapter list, player menu): it takes the controller until it closes.
    private Popup? OpenPadPopup()
    {
        foreach (var p in new[] { _seasonPopup, _chapterPopup, _pickerMenu })
            if (p is { IsOpen: true, Child: FrameworkElement { IsVisible: true } }) return p;
        return null;
    }

    // View / Share: the left rail (tabs, item shop, log, profile, settings, updates).
    private void TogglePadRail()
    {
        if (_padRail == null) return;
        _padInMusic = false;
        _padInRail = !_padInRail && _padRail.IsVisible && OpenPadPopup() == null;
        Keyboard.ClearFocus();
        FocusPadRoot();
    }

    // Menu / Options: the music player card, while it shows.
    private void TogglePadMusic()
    {
        if (_padMusicCard == null) return;
        _padInRail = false;
        _padInMusic = !_padInMusic && _padMusicCard.IsVisible && OpenPadPopup() == null;
        Keyboard.ClearFocus();
        FocusPadRoot();
    }

    private static bool Inside(DependencyObject? child, DependencyObject? root)
    {
        for (var d = child; d != null; d = PadParent(d))
            if (d == root) return true;
        return false;
    }

    // Visual parent, or the logical one (popups, text elements, which have no visual parent).
    private static DependencyObject? PadParent(DependencyObject d) =>
        (d is Visual or System.Windows.Media.Media3D.Visual3D ? VisualTreeHelper.GetParent(d) : null) ?? LogicalTreeHelper.GetParent(d);

    private void FocusPadRoot()
    {
        // After a tab or overlay change, wait for layout so the new screen's buttons are focusable.
        Dispatcher.BeginInvoke(DispatcherPriority.Loaded, () =>
        {
            var root = PadRoot();
            if (root == null) return;
            if (Keyboard.FocusedElement is DependencyObject f && f is UIElement { IsVisible: true } && Inside(f, root)) return;
            if (root == _seasonBrowserView) { root.Focus(); return; }
            if (!root.MoveFocus(new TraversalRequest(FocusNavigationDirection.First))) root.Focus();
            UpdatePadRing();
        });
    }

    private bool _padSynthetic;
    // Gives the element the same key press the keyboard would (season browser left/right, lists...).
    private bool RaisePadKey(IInputElement target, Key key)
    {
        var source = PresentationSource.FromVisual(this);
        if (source == null) return false;
        _padSynthetic = true;
        try
        {
            var preview = new KeyEventArgs(Keyboard.PrimaryDevice, source, Environment.TickCount, key) { RoutedEvent = Keyboard.PreviewKeyDownEvent };
            target.RaiseEvent(preview);
            if (preview.Handled) return true;
            var down = new KeyEventArgs(Keyboard.PrimaryDevice, source, Environment.TickCount, key) { RoutedEvent = Keyboard.KeyDownEvent };
            target.RaiseEvent(down);
            return down.Handled;
        }
        finally { _padSynthetic = false; }
    }

    private void PadMove(Key key, FocusNavigationDirection direction)
    {
        var root = PadRoot();
        if (Keyboard.FocusedElement is not UIElement current || !current.IsVisible || (root != null && !Inside(current, root)))
        {
            FocusPadRoot();
            return;
        }
        // Right leaves the side menu (back to the page).
        if (_padInRail && key == Key.Right) { _padInRail = false; Keyboard.ClearFocus(); FocusPadRoot(); return; }
        bool horizontal = key is Key.Left or Key.Right;
        int sign = key is Key.Right or Key.Up ? 1 : -1;
        if (FindPadAncestor<Slider>(current) is { IsEnabled: true } slider)
        {
            // Sliders: along the slider changes the value; across it moves on (WPF would change the value on up / down too).
            bool along = slider.Orientation == Orientation.Horizontal ? horizontal : !horizontal;
            if (along)
            {
                double step = slider.SmallChange > 0 ? slider.SmallChange : (slider.Maximum - slider.Minimum) / 20;
                if (slider.IsSnapToTickEnabled && slider.TickFrequency > 0) step = Math.Max(step, slider.TickFrequency);
                if (step <= 0) step = 1;
                slider.Value = Math.Clamp(slider.Value + sign * step, slider.Minimum, slider.Maximum);
                return;
            }
        }
        else if (current is TextBox text)
        {
            // Text boxes: left / right move the caret until it reaches an end; up / down always move on.
            if (horizontal)
            {
                if (text.SelectionLength > 0) { text.CaretIndex = key == Key.Right ? text.SelectionStart + text.SelectionLength : text.SelectionStart; return; }
                int at = text.CaretIndex + (key == Key.Right ? 1 : -1);
                if (at >= 0 && at <= text.Text.Length) { text.CaretIndex = at; return; }
            }
        }
        else if (current is PasswordBox) { }
        // An open dropdown (and the season browser, lists, the music bars...) moves through its own items.
        else if (RaisePadKey(current, key)) return;
        bool moved = current.MoveFocus(new TraversalRequest(direction));
        // Never wander under an overlay or onto the window buttons: stay on this screen.
        if (moved && root != null && Keyboard.FocusedElement is DependencyObject now && !Inside(now, root)) { current.Focus(); moved = false; }
        if (moved && Keyboard.FocusedElement == current) moved = false;
        // WPF found nothing that way (controls laid out in loose grids, cards, the music card): pick the nearest
        // control on screen in that direction instead.
        if (!moved && root != null) PadSpatialMove(root, current, direction);
        if (Keyboard.FocusedElement is FrameworkElement shown) shown.BringIntoView();
    }

    private static T? FindPadAncestor<T>(DependencyObject? d) where T : DependencyObject
    {
        for (int hops = 0; d != null && hops < 6; d = PadParent(d), hops++) if (d is T match) return match;
        return null;
    }

    // Every control A can use on this screen: buttons, switches, sliders, text boxes, lists, and panels made
    // focusable on purpose (the music bars).
    private static IEnumerable<UIElement> PadTargets(DependencyObject root)
    {
        int n = VisualTreeHelper.GetChildrenCount(root);
        for (int i = 0; i < n; i++)
        {
            if (VisualTreeHelper.GetChild(root, i) is not UIElement child || !child.IsVisible) continue;
            if (child.Focusable && child.IsEnabled && child is ButtonBase or Slider or TextBox or PasswordBox or ComboBox or ListBoxItem or TabItem or Panel or Decorator)
            {
                yield return child;
                if (child is not Panel and not Decorator) continue; // a button's own insides are not separate stops
            }
            foreach (var deeper in PadTargets(child)) yield return deeper;
        }
    }

    private static bool PadBounds(UIElement e, Visual root, out Rect box)
    {
        box = Rect.Empty;
        try
        {
            if (e.RenderSize.Width < 2 || e.RenderSize.Height < 2 || !e.IsDescendantOf(root)) return false;
            box = e.TransformToAncestor(root).TransformBounds(new Rect(e.RenderSize));
            return true;
        }
        catch { return false; }
    }

    private bool PadSpatialMove(FrameworkElement root, UIElement current, FocusNavigationDirection direction)
    {
        if (!PadBounds(current, root, out var from)) return false;
        var c = new Point(from.X + from.Width / 2, from.Y + from.Height / 2);
        UIElement? best = null; double bestScore = double.MaxValue;
        foreach (var e in PadTargets(root))
        {
            if (e == current || Inside(current, e) || !PadBounds(e, root, out var r)) continue;
            if (r.Width > ActualWidth * .7 && r.Height > ActualHeight * .7) continue;
            var p = new Point(r.X + r.Width / 2, r.Y + r.Height / 2);
            double dx = p.X - c.X, dy = p.Y - c.Y, primary, side;
            switch (direction)
            {
                case FocusNavigationDirection.Up: primary = -dy; side = Math.Abs(dx); break;
                case FocusNavigationDirection.Down: primary = dy; side = Math.Abs(dx); break;
                case FocusNavigationDirection.Left: primary = -dx; side = Math.Abs(dy); break;
                case FocusNavigationDirection.Right: primary = dx; side = Math.Abs(dy); break;
                default: return false;
            }
            if (primary <= 2) continue;
            double score = primary + 2 * side;
            if (score < bestScore) { bestScore = score; best = e; }
        }
        if (best == null || !best.Focus()) return false;
        return true;
    }

    private void PadActivate()
    {
        if (Keyboard.FocusedElement is not DependencyObject focused) { FocusPadRoot(); return; }
        if (_browserPlayArea != null && Inside(focused, _browserPlayArea)) CommitHeroSeason();
        // A rail button opens its page (or the profile, the item shop...): the controller goes with it.
        if (_padInRail && focused is ButtonBase) { _padInRail = false; }
        switch (focused)
        {
            case TextBox text when text.IsEnabled && !text.IsReadOnly:
                OpenPadKeyboard(text);
                break;
            case PasswordBox password when password.IsEnabled:
                OpenPadKeyboard(password);
                break;
            case ToggleButton toggle when toggle.IsEnabled:
                if (new ToggleButtonAutomationPeer(toggle) is IToggleProvider t) t.Toggle();
                break;
            case Button button when button.IsEnabled:
                if (new ButtonAutomationPeer(button) is IInvokeProvider i) i.Invoke();
                break;
            case ButtonBase other when other.IsEnabled:
                other.RaiseEvent(new RoutedEventArgs(ButtonBase.ClickEvent));
                break;
            case ComboBox combo:
                combo.IsDropDownOpen = !combo.IsDropDownOpen;
                break;
            case ComboBoxItem item:
                item.IsSelected = true;
                if (ItemsControl.ItemsControlFromItemContainer(item) is ComboBox owner) { owner.IsDropDownOpen = false; owner.Focus(); }
                break;
            case ListBoxItem listItem:
                listItem.IsSelected = true;
                break;
            default:
                if (focused == _seasonBrowserView) PadPlay();
                else if (focused is IInputElement element) RaisePadKey(element, Key.Enter);
                break;
        }
        FocusPadRoot();
    }

    private void PadBack()
    {
        if (OpenPadPopup() is { } popup) { popup.IsOpen = false; Keyboard.ClearFocus(); FocusPadRoot(); return; }
        if (_padInRail || _padInMusic) { _padInRail = _padInMusic = false; Keyboard.ClearFocus(); FocusPadRoot(); return; }
        if (Keyboard.FocusedElement is DependencyObject f)
        {
            for (var d = f; d != null; d = PadParent(d))
                if (d is ComboBox { IsDropDownOpen: true } combo) { combo.IsDropDownOpen = false; combo.Focus(); return; }
        }
        // Escape does the rest: it closes the profile card, sign-in, Settings, the store and Multiplayer.
        RaisePadKey(Keyboard.FocusedElement ?? this, Key.Escape);
        FocusPadRoot();
    }

    private void PadPlay()
    {
        if (PadRoot() != _seasonBrowserView || _heroPlay is not { IsEnabled: true, IsVisible: true } play) return;
        CommitHeroSeason();
        if (new ButtonAutomationPeer(play) is IInvokeProvider i) i.Invoke();
    }

    private void PadTab(int step)
    {
        if (_profileOverlay is { Visibility: Visibility.Visible } || AuthOverlay is { Visibility: Visibility.Visible }) return;
        if (OpenPadPopup() is { } popup) popup.IsOpen = false;
        _padInRail = _padInMusic = false;
        if (SettingsPanel is { Visibility: Visibility.Visible }) { _settingsStep?.Invoke(step); FocusPadRoot(); return; }
        int tabs = _railButtons.Count;
        if (tabs < 3) return;
        int slot = PassShop is { Visibility: Visibility.Visible } ? 1 : _multiplayerView is { Visibility: Visibility.Visible } ? 2 : _statsView is { Visibility: Visibility.Visible } ? 3 : 0;
        int next = (slot + step + tabs) % tabs;
        if (new ButtonAutomationPeer(_railButtons[next]) is IInvokeProvider i) i.Invoke();
        // The rail button runs on the dispatcher; move the focus once the new tab is showing.
        Dispatcher.BeginInvoke(DispatcherPriority.Background, () =>
        {
            if (Keyboard.FocusedElement is DependencyObject f && PadRoot() is { } root && !Inside(f, root)) Keyboard.ClearFocus();
            FocusPadRoot();
        });
    }

    private void PadScroll(double amount)
    {
        ScrollViewer? viewer = null;
        for (var d = Keyboard.FocusedElement as DependencyObject; d != null && viewer == null; d = PadParent(d))
            if (d is ScrollViewer { ScrollableHeight: > 0 } sv) viewer = sv;
        viewer ??= FindVisible<ScrollViewer>(PadRoot()).FirstOrDefault(s => s.ScrollableHeight > 0);
        viewer?.ScrollToVerticalOffset(viewer.VerticalOffset - amount * 22);
    }

    private static IEnumerable<T> FindVisible<T>(DependencyObject? root) where T : FrameworkElement
    {
        if (root == null) yield break;
        int n = VisualTreeHelper.GetChildrenCount(root);
        for (int i = 0; i < n; i++)
        {
            var child = VisualTreeHelper.GetChild(root, i);
            if (child is UIElement { IsVisible: false }) continue;
            if (child is T match) yield return match;
            foreach (var deeper in FindVisible<T>(child)) yield return deeper;
        }
    }

    // ---------------------------------------------------------------- focus ring

    private void UpdatePadRing()
    {
        UIElement? target = _padMode && LauncherPadEnabled ? Keyboard.FocusedElement as UIElement : null;
        // The season browser itself has focus on Home: its selected card is already highlighted.
        if (target == _seasonBrowserView || target is Window || target is { IsVisible: false }) target = null;
        if (target is FrameworkElement big && (big.ActualWidth > ActualWidth * .7 || big.ActualHeight > ActualHeight * .7)) target = null;
        if (target == _padRingOn) return;
        if (_padRing != null && _padRingOn != null) AdornerLayer.GetAdornerLayer(_padRingOn)?.Remove(_padRing);
        _padRing = null; _padRingOn = null;
        if (target == null) return;
        var layer = AdornerLayer.GetAdornerLayer(target);
        if (layer == null) return;
        _padRing = new PadFocusRing(target);
        layer.Add(_padRing);
        _padRingOn = target;
    }

    private sealed class PadFocusRing : Adorner
    {
        private static readonly Pen Ring = Frozen(new Pen(new SolidColorBrush(Color.FromRgb(0xFF, 0xD2, 0x3F)), 2.5));
        private static readonly Pen Glow = Frozen(new Pen(new SolidColorBrush(Color.FromArgb(0x55, 0xFF, 0xD2, 0x3F)), 7));
        public PadFocusRing(UIElement adorned) : base(adorned) { IsHitTestVisible = false; }
        private static Pen Frozen(Pen p) { p.Freeze(); return p; }
        protected override void OnRender(DrawingContext dc)
        {
            var size = AdornedElement.RenderSize;
            var box = new Rect(-4, -4, size.Width + 8, size.Height + 8);
            double r = Math.Min(14, Math.Min(box.Width, box.Height) / 2);
            dc.DrawRoundedRectangle(null, Glow, box, r, r);
            dc.DrawRoundedRectangle(null, Ring, box, r, r);
        }
    }

    // ---------------------------------------------------------------- button hints

    private static readonly Dictionary<string, ImageSource?> PadIconCache = new();
    private static ImageSource? PadIcon(string name)
    {
        if (PadIconCache.TryGetValue(name, out var cached)) return cached;
        ImageSource? image = null;
        try
        {
            using var stream = typeof(MainWindow).Assembly.GetManifestResourceStream("SyndicateClassic.Pad." + name);
            if (stream != null)
            {
                var bitmap = new BitmapImage();
                bitmap.BeginInit(); bitmap.CacheOption = BitmapCacheOption.OnLoad; bitmap.DecodePixelHeight = 64; bitmap.StreamSource = stream; bitmap.EndInit();
                bitmap.Freeze(); image = bitmap;
            }
        }
        catch { image = null; }
        PadIconCache[name] = image;
        return image;
    }

    // The icon, or the button's name in a small pill if the picture is missing (older build).
    private static FrameworkElement PadGlyph(string name, double height)
    {
        var icon = PadIcon(name);
        if (icon != null) return new Image { Source = icon, Height = height, Stretch = Stretch.Uniform, VerticalAlignment = VerticalAlignment.Center };
        return new Border
        {
            Height = height, MinWidth = height, CornerRadius = new CornerRadius(height / 2), Background = Hex("#33FFFFFF"), Padding = new Thickness(5, 0, 5, 0), VerticalAlignment = VerticalAlignment.Center,
            Child = new TextBlock { Text = name.ToUpperInvariant().Replace("DPAD-", ""), FontSize = height * .45, FontWeight = FontWeights.Bold, Foreground = Brushes.White, VerticalAlignment = VerticalAlignment.Center, HorizontalAlignment = HorizontalAlignment.Center }
        };
    }

    // Button hints: a footer bar along the bottom of the window (right of the rail), shown only while the launcher is
    // used with a controller. It used to float over the bottom-right corner, on top of the Play button, the store and
    // settings text. Now every page is lifted by the bar's height while it shows, so nothing sits under it.
    private const double PadFooterHeight = 50;
    private readonly Dictionary<FrameworkElement, Thickness> _padFooterSaved = new();
    private bool _padFooterShown;
    private TextBlock? _padFooterName;

    private void BuildPadHints(Grid page, int rows)
    {
        _padHintRow = new StackPanel { Orientation = Orientation.Horizontal, VerticalAlignment = VerticalAlignment.Center };
        var bar = new Grid { Margin = new Thickness(20, 0, 20, 0) };
        bar.ColumnDefinitions.Add(new ColumnDefinition { Width = GridLength.Auto });
        bar.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(1, GridUnitType.Star) });

        // Left: which input is in use.
        var who = new StackPanel { Orientation = Orientation.Horizontal, VerticalAlignment = VerticalAlignment.Center, Margin = new Thickness(0, 0, 24, 0) };
        who.Children.Add(new Border
        {
            Width = 30, Height = 30, CornerRadius = new CornerRadius(15), Background = Hex("#268B6CFF"), BorderBrush = Hex("#668B6CFF"), BorderThickness = new Thickness(1),
            Child = GlyphIcon("\uE7FC", 15, Night.Text).Also(g => { g.HorizontalAlignment = HorizontalAlignment.Center; g.VerticalAlignment = VerticalAlignment.Center; })
        });
        _padFooterName = NightText("Controller", 12.5, Night.Text, bold: true).Also(t => { t.VerticalAlignment = VerticalAlignment.Center; t.Margin = new Thickness(10, 0, 0, 0); t.TextWrapping = TextWrapping.NoWrap; });
        who.Children.Add(_padFooterName);
        bar.Children.Add(who);

        // Right: the hints. Shrinks (never grows) when the window is narrow, so the labels never overlap or clip.
        var fit = new Viewbox { Child = _padHintRow, Stretch = Stretch.Uniform, StretchDirection = StretchDirection.DownOnly, HorizontalAlignment = HorizontalAlignment.Right, VerticalAlignment = VerticalAlignment.Center, MaxHeight = 34 };
        Grid.SetColumn(fit, 1);
        bar.Children.Add(fit);

        _padHints = new Border
        {
            Child = bar, Height = PadFooterHeight,
            Background = new LinearGradientBrush(Hex("#F00B0E1F").Color, Hex("#F0070914").Color, 90),
            BorderBrush = Hex(Night.Line), BorderThickness = new Thickness(0, 1, 0, 0),
            HorizontalAlignment = HorizontalAlignment.Stretch, VerticalAlignment = VerticalAlignment.Bottom, Margin = new Thickness(RailWidth, 0, 0, 0),
            IsHitTestVisible = false, Visibility = Visibility.Collapsed
        };
        Grid.SetRowSpan(_padHints, rows);
        page.Children.Add(_padHints);
    }

    // The rail keeps its own size: LB / RB are shown in the footer instead of being squeezed into the rail.
    private void AddRailPadIcons() { }

    // Lifts every page by the footer's height while it shows (and puts the margins back after).
    private void SetPadFooterSpace(bool show)
    {
        if (show == _padFooterShown) return;
        _padFooterShown = show;
        var views = new List<FrameworkElement?> { _seasonBrowserView, _homeView, _multiplayerView, _statsView, _secretShopView, PassShop, SettingsPanel, AuthOverlay };
        foreach (var view in views)
        {
            if (view == null) continue;
            if (show)
            {
                if (!_padFooterSaved.ContainsKey(view)) _padFooterSaved[view] = view.Margin;
                var m = _padFooterSaved[view];
                view.Margin = new Thickness(m.Left, m.Top, m.Right, m.Bottom + PadFooterHeight);
            }
            else if (_padFooterSaved.TryGetValue(view, out var saved)) view.Margin = saved;
        }
        if (!show) _padFooterSaved.Clear();
    }

    // One hint: the button picture(s) in a key cap, then what it does.
    private FrameworkElement PadHintChip(string[] icons, string text, bool first)
    {
        var chip = new StackPanel { Orientation = Orientation.Horizontal, VerticalAlignment = VerticalAlignment.Center, Margin = new Thickness(first ? 0 : 22, 0, 0, 0) };
        var keys = new StackPanel { Orientation = Orientation.Horizontal, VerticalAlignment = VerticalAlignment.Center };
        for (int i = 0; i < icons.Length; i++)
        {
            if (i > 0) keys.Children.Add(NightText("/", 12, Night.Muted).Also(t => { t.VerticalAlignment = VerticalAlignment.Center; t.Margin = new Thickness(3, 0, 3, 0); }));
            keys.Children.Add(PadGlyph(icons[i], 24));
        }
        chip.Children.Add(new Border { Child = keys, Background = Hex("#14FFFFFF"), BorderBrush = Hex("#1FFFFFFF"), BorderThickness = new Thickness(1), CornerRadius = new CornerRadius(9), Padding = new Thickness(5, 3, 5, 3), VerticalAlignment = VerticalAlignment.Center });
        chip.Children.Add(NightText(text, 13, Night.Text, bold: true).Also(t => { t.VerticalAlignment = VerticalAlignment.Center; t.Margin = new Thickness(9, 0, 0, 0); t.TextWrapping = TextWrapping.NoWrap; }));
        return chip;
    }

    private void UpdatePadHints(bool force)
    {
        if (_padHints == null || _padHintRow == null) return;
        // Only once the launcher has seen controller input; mouse or keyboard use hides them again.
        bool show = _padMode && _padConnected && LauncherPadEnabled;
        var root = PadRoot();
        var hints = new List<(string[] Icons, string Text)>();
        if (root != null && root == _padKeyboard) { hints.Add((new[] { "a" }, "Type")); hints.Add((new[] { "x" }, "Delete")); hints.Add((new[] { "y" }, "Space")); hints.Add((new[] { "lb", "rb" }, "Shift")); hints.Add((new[] { "b" }, "Done")); }
        else if (_padInMusic) { hints.Add((new[] { "dpad-left", "dpad-right" }, "Seek / volume")); hints.Add((new[] { "a" }, "Select")); hints.Add((new[] { "y" }, "Mute")); hints.Add((new[] { "b" }, "Back")); }
        else if (_padInRail || (root != null && OpenPadPopup()?.Child == root)) { hints.Add((new[] { "a" }, "Select")); hints.Add((new[] { "b" }, "Back")); }
        else if (root == SettingsPanel) { hints.Add((new[] { "lb", "rb" }, "Page")); hints.Add((new[] { "rs" }, "Scroll")); hints.Add((new[] { "a" }, "Select")); hints.Add((new[] { "b" }, "Close")); }
        else if (root == _profileOverlay || root == AuthOverlay) { hints.Add((new[] { "a" }, "Select")); hints.Add((new[] { "b" }, "Back")); }
        else
        {
            hints.Add((new[] { "lb", "rb" }, "Tabs"));
            if (root == _seasonBrowserView) { hints.Add((new[] { "dpad-left", "dpad-right" }, "Season")); hints.Add((new[] { "y" }, "Play")); }
            hints.Add((new[] { "a" }, "Select"));
            if (root != _seasonBrowserView) hints.Add((new[] { "b" }, "Back"));
            hints.Add((new[] { "x" }, "Settings"));
            hints.Add((new[] { "view" }, "Menu"));
            if (_padMusicCard is { IsVisible: true }) hints.Add((new[] { "menu" }, "Music"));
        }
        string key = show + "|" + string.Join(";", hints.Select(h => string.Join(",", h.Icons) + h.Text));
        if (!force && key == _padHintKey) return;
        _padHintKey = key;
        foreach (var icon in _padRailIcons) icon.Visibility = Visibility.Collapsed;
        _padHints.Visibility = show ? Visibility.Visible : Visibility.Collapsed;
        SetPadFooterSpace(show);
        if (_padFooterName != null) _padFooterName.Text = root == SettingsPanel ? "Controller · Settings" : root == _profileOverlay ? "Controller · Profile" : root == AuthOverlay ? "Controller · Sign in"
            : root != null && root == _padKeyboard ? "Controller · Keyboard" : _padInRail ? "Controller · Menu" : _padInMusic ? "Controller · Music" : "Controller";
        _padHintRow.Children.Clear();
        foreach (var (icons, text) in hints) _padHintRow.Children.Add(PadHintChip(icons, text, _padHintRow.Children.Count == 0));
    }

    // ---------------------------------------------------------------- devices

    private static class XInput
    {
        [StructLayout(LayoutKind.Sequential)] public struct Gamepad { public ushort Buttons; public byte LeftTrigger, RightTrigger; public short LX, LY, RX, RY; }
        [StructLayout(LayoutKind.Sequential)] public struct State { public uint PacketNumber; public Gamepad Pad; }
        [DllImport("xinput1_4.dll", EntryPoint = "XInputGetState")] private static extern uint Get14(uint user, out State state);
        [DllImport("xinput9_1_0.dll", EntryPoint = "XInputGetState")] private static extern uint Get910(uint user, out State state);
        private static int _which; // 0 = try 1_4, 1 = use 9_1_0, 2 = none
        private static long _retryAt;
        private static readonly long[] _absentUntil = new long[4];

        public static uint Get(uint user, out State state)
        {
            state = default;
            // Checking an empty slot is slow on some PCs; look again only once a second.
            long now = Environment.TickCount64;
            if (now < _absentUntil[user]) return 1167;
            uint result = 1167; // ERROR_DEVICE_NOT_CONNECTED
            try
            {
                if (_which == 0) result = Get14(user, out state);
                else if (_which == 1) result = Get910(user, out state);
                else if (now >= _retryAt) { _which = 0; _retryAt = now + 30000; }
            }
            catch (DllNotFoundException) { _which++; }
            catch (EntryPointNotFoundException) { _which++; }
            if (result != 0) _absentUntil[user] = now + 1000;
            return result;
        }
    }

    // Reads the first PS4/PS5 pad straight from HID (USB or Bluetooth) while the game is closed.
    private sealed class SonyNavReader : IDisposable
    {
        private static readonly Dictionary<int, bool> Models = new() { [0x05C4] = false, [0x09CC] = false, [0x0BA0] = false, [0x0CE6] = true, [0x0DF2] = true };
        private volatile bool _stop;
        private ControllerService.PadState? _latest;
        private long _latestAt;
        private readonly Thread _thread;

        public ControllerService.PadState? Latest => Environment.TickCount64 - Interlocked.Read(ref _latestAt) < 1500 ? _latest : null;

        public SonyNavReader()
        {
            _thread = new Thread(Run) { IsBackground = true, Name = "Syndicate launcher PS pad" };
            _thread.Start();
        }

        private void Run()
        {
            while (!_stop)
            {
                HidDevice? device = null; bool dualSense = false;
                try
                {
                    foreach (var d in DeviceList.Local.GetHidDevices(0x054C))
                        if (Models.TryGetValue(d.ProductID, out dualSense)) { device = d; break; }
                }
                catch { device = null; }
                if (device == null) { Sleep(2000); continue; }
                try
                {
                    if (!device.TryOpen(out HidStream stream)) { Sleep(2000); continue; }
                    using (stream)
                    {
                        stream.ReadTimeout = 500;
                        var buf = new byte[Math.Max(128, SafeLength(device))];
                        while (!_stop)
                        {
                            int n;
                            try { n = stream.Read(buf, 0, buf.Length); }
                            catch (TimeoutException) { continue; }
                            if (n > 0 && ControllerService.PadState.TryParse(buf, n, dualSense, out var s))
                            {
                                _latest = s;
                                Interlocked.Exchange(ref _latestAt, Environment.TickCount64);
                            }
                        }
                    }
                }
                catch { Sleep(1000); } // unplugged or Bluetooth dropped: look again
            }
        }

        private static int SafeLength(HidDevice d) { try { return d.GetMaxInputReportLength(); } catch { return 128; } }
        private void Sleep(int ms) { for (int t = 0; t < ms && !_stop; t += 100) Thread.Sleep(100); }
        public void Dispose() => _stop = true;
    }
}