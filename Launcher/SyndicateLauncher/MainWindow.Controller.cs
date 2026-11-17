using System;
using System.Collections.Generic;
using System.Linq;
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
using SyndicateLauncher.Services;
using SyndicateLauncher.Services.Controllers;

namespace SyndicateLauncher;

// Use the launcher with a controller (Xbox, or PS4 / PS5 read straight from the pad), ported from Syndicate Classic:
//   LB / RB          previous / next screen (Home, Play, Studio, Credits); in Settings, previous / next page
//   D-pad / L stick  move between buttons; on Play, left / right (or LT / RT) pick the season
//   A                press the highlighted button, flip the highlighted switch, open a list or a text box
//   B                back (closes the player picker, Settings, the chapter list, a screen...)
//   Y                PLAY (Home and Play)
//   X                open / close Settings
//   View / Share     the side menu; B or right leaves it
//   Menu / Options   player picker
//   R stick          scroll
// Sliders: left / right change the value, up / down move on. Text boxes: A opens an on-screen keyboard
// (X backspace, Y space, LB / RB shift, B done).
// Only while the launcher is the active window. A ring in the theme's accent shows what A will press, and a button
// hint bar appears once controller input is seen; moving the mouse or typing hides both again.
// Switch: Settings > Mods > Controller > Move around the launcher with a controller.
public partial class MainWindow
{
    private const PadButtons Directions = PadButtons.Up | PadButtons.Down | PadButtons.Left | PadButtons.Right;

    private readonly PadReader _pad = new();
    private DispatcherTimer? _padTimer;
    private PadButtons _padHeld;
    private PadButtons _padRepeatDir;
    private long _padRepeatAt;
    private bool _padTriggersHeld;
    private bool _padMode;               // last input came from a controller: show the ring and hints
    private bool _padConnected;
    private bool _padPlayStation;
    private bool _padInRail;             // View / Share put the controller on the side menu
    private bool _padSynthetic;
    private Point _padMouseAt = new(double.NaN, double.NaN);
    private PadFocusRing? _padRing;
    private UIElement? _padRingOn;

    private void StartControllerNavigation()
    {
        if (_padTimer is not null) return;
        try { BuildPadHints(); } catch { }
        _padTimer = new DispatcherTimer(DispatcherPriority.Input) { Interval = TimeSpan.FromMilliseconds(16) };
        _padTimer.Tick += (_, _) => { try { TickPad(); } catch { } };
        _padTimer.Start();
        // Mouse or keyboard use hides the ring and the hints again.
        PreviewMouseMove += (_, e) =>
        {
            var p = e.GetPosition(this);
            if (!double.IsNaN(_padMouseAt.X) && (Math.Abs(p.X - _padMouseAt.X) > 4 || Math.Abs(p.Y - _padMouseAt.Y) > 4)) SetPadMode(false);
            _padMouseAt = p;
        };
        PreviewMouseDown += (_, _) => SetPadMode(false);
        PreviewKeyDown += (_, _) => { if (!_padSynthetic) SetPadMode(false); };
        Closed += (_, _) => { _padTimer?.Stop(); _pad.Dispose(); };
    }

    // ---------------------------------------------------------------- input

    private void TickPad()
    {
        bool enabled = _model.ControllerNavigation;
        // PS pads: read here while the game is closed. While it runs, ControllerService turns them into an Xbox
        // pad (which XInput sees), and two readers on one pad would fight.
        var snap = _pad.Read(enabled && !GameLauncher.IsRunning && _model.PlayStationControllers);
        if (snap.Connected != _padConnected || snap.PlayStation != _padPlayStation)
        {
            _padConnected = snap.Connected;
            _padPlayStation = snap.PlayStation;
            if (!_padConnected) _padMode = false;
            UpdatePadHints(force: true);
            UpdatePadRing();
        }
        if (!enabled)
        {
            if (_padMode) SetPadMode(false);
            _padHeld = snap.Buttons;
            return;
        }
        if (!IsActive || WindowState == WindowState.Minimized) { _padHeld = snap.Buttons; return; }

        var state = snap.Buttons;
        // Sticks act like the d-pad (with a dead zone) for moving around.
        if (snap.LX < -0.55) state |= PadButtons.Left; else if (snap.LX > 0.55) state |= PadButtons.Right;
        if (snap.LY > 0.55) state |= PadButtons.Up; else if (snap.LY < -0.55) state |= PadButtons.Down;

        var pressed = state & ~_padHeld;
        _padHeld = state;
        if (pressed != PadButtons.None || Math.Abs(snap.RY) > 0.25) SetPadMode(true);

        // Directions repeat while held: first after 380 ms, then every 110 ms.
        var dir = state & Directions;
        long now = Environment.TickCount64;
        var fire = pressed & Directions;
        if (dir == PadButtons.None) _padRepeatDir = PadButtons.None;
        else if (fire != PadButtons.None) { _padRepeatDir = dir; _padRepeatAt = now + 380; }
        else if (dir == _padRepeatDir && now >= _padRepeatAt) { fire = dir; _padRepeatAt = now + 110; }
        if ((fire & PadButtons.Up) != 0) PadMove(Key.Up, FocusNavigationDirection.Up);
        else if ((fire & PadButtons.Down) != 0) PadMove(Key.Down, FocusNavigationDirection.Down);
        if ((fire & PadButtons.Left) != 0) PadMove(Key.Left, FocusNavigationDirection.Left);
        else if ((fire & PadButtons.Right) != 0) PadMove(Key.Right, FocusNavigationDirection.Right);

        if (PadKeyboardOpen)
        {
            if ((pressed & PadButtons.A) != 0) PadActivate();
            if ((pressed & PadButtons.X) != 0) PadKeyBackspace();
            if ((pressed & PadButtons.Y) != 0) PadKeyType(" ");
            if ((pressed & (PadButtons.LB | PadButtons.RB)) != 0) PadKeyShift();
            if ((pressed & (PadButtons.B | PadButtons.Start)) != 0) ClosePadKeyboard();
        }
        else
        {
            if ((pressed & PadButtons.A) != 0) PadActivate();
            if ((pressed & PadButtons.B) != 0) PadBack();
            if ((pressed & PadButtons.Y) != 0) PadPlay();
            if ((pressed & PadButtons.X) != 0) PadSettings();
            if ((pressed & PadButtons.Back) != 0) TogglePadRail();
            if ((pressed & PadButtons.Start) != 0) PadPlayerSelect();
            if ((pressed & PadButtons.LB) != 0) PadTab(-1);
            if ((pressed & PadButtons.RB) != 0) PadTab(+1);
        }
        if (Math.Abs(snap.RY) > 0.25) PadScroll(snap.RY);

        UpdatePadRing();
        UpdatePadHints(force: false);
    }

    private void SetPadMode(bool on)
    {
        if (_padMode == on) return;
        _padMode = on;
        if (on && Keyboard.FocusedElement is not UIElement { IsVisible: true }) FocusPadRoot();
        UpdatePadRing();
        UpdatePadHints(force: true);
    }

    // ---------------------------------------------------------------- where the controller is

    // The screen that has the controller now: the topmost open overlay, else the current screen.
    private FrameworkElement? PadRoot()
    {
        if (PadKeyboardOpen) return _padKeyboard;
        if (PlayerSelectRoot.IsVisible) return PlayerSelectRoot;
        if (ChapterPopup.IsOpen && ChapterPopup.Child is FrameworkElement { IsVisible: true } chapters) return chapters;
        if (ThemeEditorRoot.IsVisible) return ThemeEditorRoot;
        if (SettingsRoot.IsVisible) return SettingsRoot;
        if (_padInRail && SidebarNav.IsVisible) return SidebarNav;
        _padInRail = false;
        return ContentHost;
    }

    private static bool Inside(DependencyObject? child, DependencyObject? root)
    {
        for (var d = child; d is not null; d = PadParent(d))
            if (d == root) return true;
        return false;
    }

    // Visual parent, or the logical one (popups and text elements have no visual parent).
    private static DependencyObject? PadParent(DependencyObject d) =>
        (d is Visual or System.Windows.Media.Media3D.Visual3D ? VisualTreeHelper.GetParent(d) : null) ?? LogicalTreeHelper.GetParent(d);

    private static T? FindPadAncestor<T>(DependencyObject? d) where T : DependencyObject
    {
        for (int hops = 0; d is not null && hops < 8; d = PadParent(d), hops++) if (d is T match) return match;
        return null;
    }

    private void FocusPadRoot()
    {
        // After a screen or overlay change, wait for layout so the new screen's buttons can take focus.
        Dispatcher.BeginInvoke(DispatcherPriority.Loaded, () =>
        {
            var root = PadRoot();
            if (root is null) return;
            if (Keyboard.FocusedElement is DependencyObject f && f is UIElement { IsVisible: true } && Inside(f, root)) return;
            // Play: start on the season slider, so left / right pick the season straight away.
            if (root == ContentHost && _model.IsPlayScreen && BrowserSlider.IsVisible && BrowserSlider.Focus()) { UpdatePadRing(); return; }
            if (!root.MoveFocus(new TraversalRequest(FocusNavigationDirection.First))) root.Focus();
            UpdatePadRing();
        });
    }

    // Gives the element the same key press the keyboard would (lists, dropdowns...).
    private bool RaisePadKey(IInputElement target, Key key)
    {
        var source = PresentationSource.FromVisual(this);
        if (source is null) return false;
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

    // ---------------------------------------------------------------- moving

    // Play screen: left / right on the season slider, the timeline or its arrows pick the season.
    private bool OnSeasonPicker(DependencyObject? focused) =>
        focused is null || focused == ContentHost || Inside(focused, BrowserSlider) || Inside(focused, BrowserTimeline)
        || focused is ButtonBase { Command: var c } && (c == _model.BrowserPreviousCommand || c == _model.BrowserNextCommand);

    private void StepSeason(int step)
    {
        var command = step < 0 ? _model.BrowserPreviousCommand : _model.BrowserNextCommand;
        if (command.CanExecute(null)) command.Execute(null);
    }

    private void PadMove(Key key, FocusNavigationDirection direction)
    {
        var root = PadRoot();
        bool horizontal = key is Key.Left or Key.Right;
        int sign = key is Key.Right or Key.Up ? 1 : -1;
        if (horizontal && root == ContentHost && _model.IsPlayScreen && OnSeasonPicker(Keyboard.FocusedElement as DependencyObject))
        {
            StepSeason(sign);
            return;
        }
        if (Keyboard.FocusedElement is not UIElement current || !current.IsVisible || (root is not null && !Inside(current, root)))
        {
            FocusPadRoot();
            return;
        }
        // Right leaves the side menu (back to the page).
        if (_padInRail && key == Key.Right) { _padInRail = false; Keyboard.ClearFocus(); FocusPadRoot(); return; }
        if (FindPadAncestor<Slider>(current) is { IsEnabled: true } slider)
        {
            // Sliders: along the slider changes the value; across it moves on.
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
        // An open dropdown or a list moves through its own items.
        else if ((current is ComboBoxItem or ListBoxItem || FindPadAncestor<ComboBox>(current) is { IsDropDownOpen: true }) && RaisePadKey(current, key)) return;

        bool moved = current.MoveFocus(new TraversalRequest(direction));
        // Never wander under an overlay or onto the window buttons: stay on this screen.
        if (moved && root is not null && Keyboard.FocusedElement is DependencyObject now && !Inside(now, root)) { current.Focus(); moved = false; }
        if (moved && Keyboard.FocusedElement == current) moved = false;
        // WPF found nothing that way (controls in loose grids and cards): take the nearest control on screen instead.
        if (!moved && root is not null) PadSpatialMove(root, current, direction);
        if (Keyboard.FocusedElement is FrameworkElement shown) shown.BringIntoView();
    }

    // Every control A can use on this screen.
    private static IEnumerable<UIElement> PadTargets(DependencyObject root)
    {
        int n = VisualTreeHelper.GetChildrenCount(root);
        for (int i = 0; i < n; i++)
        {
            if (VisualTreeHelper.GetChild(root, i) is not UIElement child || !child.IsVisible) continue;
            if (child.Focusable && child.IsEnabled && child is ButtonBase or Slider or TextBox or PasswordBox or ComboBox or ListBoxItem)
            {
                yield return child;
                continue; // a button's own insides are not separate stops
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
        return best is not null && best.Focus();
    }

    // ---------------------------------------------------------------- actions

    private void PadActivate()
    {
        if (Keyboard.FocusedElement is not DependencyObject focused || (PadRoot() is { } root && !Inside(focused, root))) { FocusPadRoot(); return; }
        // A side-menu button opens its screen: the controller goes with it.
        if (_padInRail && focused is ButtonBase) _padInRail = false;
        switch (focused)
        {
            case TextBox text when text.IsEnabled && !text.IsReadOnly:
                OpenPadKeyboard(text);
                return;
            case PasswordBox password when password.IsEnabled:
                OpenPadKeyboard(password);
                return;
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
                if (focused == BrowserSlider) PadPlay();
                else if (focused is IInputElement element) RaisePadKey(element, Key.Enter);
                break;
        }
        FocusPadRoot();
    }

    private void PadBack()
    {
        if (ChapterPopup.IsOpen) { ChapterDrop.IsChecked = false; ChapterDrop.Focus(); return; }
        if (Keyboard.FocusedElement is DependencyObject f && FindPadAncestor<ComboBox>(f) is { IsDropDownOpen: true } combo)
        {
            combo.IsDropDownOpen = false; combo.Focus(); return;
        }
        if (PlayerSelectRoot.IsVisible) Run(_model.ClosePlayerSelectCommand);
        else if (SettingsRoot.IsVisible) Run(_model.CloseSettingsCommand);   // closes the theme editor first
        else if (_padInRail) _padInRail = false;
        else if (_model.IsSeasonScreen) Run(_model.BackToSeasonsCommand);
        else if (_model.IsChapterScreen) Run(_model.BackToChaptersCommand);
        else if (!_model.IsHomeScreen) Run(_model.HomeCommand);
        Keyboard.ClearFocus();
        FocusPadRoot();
    }

    private static void Run(ICommand command)
    {
        if (command.CanExecute(null)) command.Execute(null);
    }

    private void PadPlay()
    {
        if (PadRoot() != ContentHost) return;
        if (_model.IsPlayScreen) Run(_model.PlayBrowserCommand);
        else if (_model.IsHomeScreen || _model.IsSeasonScreen) Run(_model.PlayCommand);
    }

    private void PadSettings()
    {
        if (PlayerSelectRoot.IsVisible) return;
        _padInRail = false;
        if (ChapterPopup.IsOpen) ChapterDrop.IsChecked = false;
        if (SettingsRoot.IsVisible) Run(_model.CloseSettingsCommand);
        else Run(_model.ToggleSettingsCommand);
        Keyboard.ClearFocus();
        FocusPadRoot();
    }

    private void PadPlayerSelect()
    {
        _padInRail = false;
        if (PlayerSelectRoot.IsVisible) Run(_model.ClosePlayerSelectCommand);
        else Run(_model.OpenPlayerSelectCommand);
        Keyboard.ClearFocus();
        FocusPadRoot();
    }

    private void TogglePadRail()
    {
        if (PlayerSelectRoot.IsVisible || SettingsRoot.IsVisible) return;
        if (ChapterPopup.IsOpen) ChapterDrop.IsChecked = false;
        _padInRail = !_padInRail;
        Keyboard.ClearFocus();
        FocusPadRoot();
    }

    private void PadTab(int step)
    {
        if (PlayerSelectRoot.IsVisible || ThemeEditorRoot.IsVisible) return;
        if (ChapterPopup.IsOpen) ChapterDrop.IsChecked = false;
        _padInRail = false;
        if (SettingsRoot.IsVisible)
        {
            var pages = new[] { NavMatch, NavMods, NavDisplay, NavFiles, NavThemes };
            int at = Array.FindIndex(pages, p => p.IsChecked == true);
            pages[(Math.Max(at, 0) + step + pages.Length) % pages.Length].IsChecked = true;
        }
        else
        {
            var screens = new List<(Func<bool> Showing, ICommand Open)>
            {
                (() => _model.IsHomeScreen || _model.IsChapterScreen || _model.IsSeasonScreen, _model.HomeCommand),
                (() => _model.IsPlayScreen, _model.OpenPlayCommand),
            };
            if (StudioNavButton.Visibility == Visibility.Visible) screens.Add((() => _model.IsStudioScreen, _model.OpenStudioScreenCommand));
            screens.Add((() => _model.IsCreditsScreen, _model.OpenCreditsCommand));
            int at = Math.Max(0, screens.FindIndex(s => s.Showing()));
            Run(screens[(at + step + screens.Count) % screens.Count].Open);
        }
        Keyboard.ClearFocus();
        // The new screen shows on the next layout pass; take the controller there once it has.
        Dispatcher.BeginInvoke(DispatcherPriority.Background, FocusPadRoot);
    }

    private void PadScroll(double amount)
    {
        ScrollViewer? viewer = null;
        for (var d = Keyboard.FocusedElement as DependencyObject; d is not null && viewer is null; d = PadParent(d))
            if (d is ScrollViewer { ScrollableHeight: > 0 } sv) viewer = sv;
        viewer ??= FindVisible<ScrollViewer>(PadRoot()).FirstOrDefault(s => s.ScrollableHeight > 0);
        viewer?.ScrollToVerticalOffset(viewer.VerticalOffset - amount * 22);
    }

    private static IEnumerable<T> FindVisible<T>(DependencyObject? root) where T : FrameworkElement
    {
        if (root is null) yield break;
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
        UIElement? target = _padMode && _model.ControllerNavigation ? Keyboard.FocusedElement as UIElement : null;
        if (target is Window || target is { IsVisible: false }) target = null;
        if (target is FrameworkElement big && (big.ActualWidth > ActualWidth * .7 || big.ActualHeight > ActualHeight * .7)) target = null;
        if (target == _padRingOn) return;
        if (_padRing is not null && _padRingOn is not null) AdornerLayer.GetAdornerLayer(_padRingOn)?.Remove(_padRing);
        _padRing = null; _padRingOn = null;
        if (target is null) return;
        var layer = AdornerLayer.GetAdornerLayer(target);
        if (layer is null) return;
        var accent = (TryFindResource("AccentBrush") as SolidColorBrush)?.Color ?? Color.FromRgb(0xFF, 0xD2, 0x3F);
        _padRing = new PadFocusRing(target, accent);
        layer.Add(_padRing);
        _padRingOn = target;
    }

    // Drawn in the theme's accent colour (taken when the ring moves, so a theme change shows on the next move).
    private sealed class PadFocusRing : Adorner
    {
        private readonly Pen _ring, _glow;
        public PadFocusRing(UIElement adorned, Color accent) : base(adorned)
        {
            IsHitTestVisible = false;
            _ring = new Pen(new SolidColorBrush(accent), 2.5); _ring.Freeze();
            _glow = new Pen(new SolidColorBrush(Color.FromArgb(0x55, accent.R, accent.G, accent.B)), 7); _glow.Freeze();
        }
        protected override void OnRender(DrawingContext dc)
        {
            var size = AdornedElement.RenderSize;
            var box = new Rect(-4, -4, size.Width + 8, size.Height + 8);
            double r = Math.Min(14, Math.Min(box.Width, box.Height) / 2);
            dc.DrawRoundedRectangle(null, _glow, box, r, r);
            dc.DrawRoundedRectangle(null, _ring, box, r, r);
        }
    }

    // ---------------------------------------------------------------- button hints

    private const double PadFooterHeight = 46;
    private Border? _padHints;
    private StackPanel? _padHintRow;
    private TextBlock? _padFooterName;
    private string _padHintKey = "";
    private Thickness _contentMarginBeforeHints;
    private bool _padFooterShown;
    private static readonly Dictionary<string, ImageSource?> PadIconCache = new();

    private static ImageSource? PadIcon(string name)
    {
        if (PadIconCache.TryGetValue(name, out var cached)) return cached;
        ImageSource? image = null;
        try
        {
            var info = Application.GetResourceStream(new Uri("pack://application:,,,/Assets/Controller/" + name + ".png"));
            if (info is not null)
            {
                using var stream = info.Stream;
                var bitmap = new BitmapImage();
                bitmap.BeginInit(); bitmap.CacheOption = BitmapCacheOption.OnLoad; bitmap.DecodePixelHeight = 64; bitmap.StreamSource = stream; bitmap.EndInit();
                bitmap.Freeze(); image = bitmap;
            }
        }
        catch { image = null; }
        PadIconCache[name] = image;
        return image;
    }

    // The button picture, or the button's name in a small pill when there is no picture (View, Menu, LT, RT).
    private FrameworkElement PadGlyph(string name, double height)
    {
        if (PadIcon(name) is { } icon) return new Image { Source = icon, Height = height, Stretch = Stretch.Uniform, VerticalAlignment = VerticalAlignment.Center };
        var label = new TextBlock
        {
            Text = PadButtonName(name), FontSize = height * .42, FontWeight = FontWeights.Bold,
            VerticalAlignment = VerticalAlignment.Center, HorizontalAlignment = HorizontalAlignment.Center
        };
        label.SetResourceReference(TextBlock.ForegroundProperty, "TextBrush");
        return new Border
        {
            Height = height, MinWidth = height, CornerRadius = new CornerRadius(height / 2), Background = new SolidColorBrush(Color.FromArgb(0x33, 0xFF, 0xFF, 0xFF)),
            Padding = new Thickness(6, 0, 6, 0), VerticalAlignment = VerticalAlignment.Center, Child = label
        };
    }

    private string PadButtonName(string name) => (name, _padPlayStation) switch
    {
        ("view", true) => "SHARE", ("view", false) => "VIEW",
        ("menu", true) => "OPTIONS", ("menu", false) => "MENU",
        ("lt", true) => "L2", ("rt", true) => "R2",
        _ => name.ToUpperInvariant().Replace("DPAD-", "")
    };

    private void BuildPadHints()
    {
        if (ContentHost.Parent is not Grid main) return;
        _padHintRow = new StackPanel { Orientation = Orientation.Horizontal, VerticalAlignment = VerticalAlignment.Center };
        var bar = new Grid { Margin = new Thickness(24, 0, 24, 0) };
        bar.ColumnDefinitions.Add(new ColumnDefinition { Width = GridLength.Auto });
        bar.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(1, GridUnitType.Star) });

        // Left: which input is in use.
        var who = new StackPanel { Orientation = Orientation.Horizontal, VerticalAlignment = VerticalAlignment.Center, Margin = new Thickness(0, 0, 24, 0) };
        var glyph = new TextBlock { Text = "", FontSize = 15, VerticalAlignment = VerticalAlignment.Center };
        glyph.SetResourceReference(TextBlock.FontFamilyProperty, "IconFont");
        glyph.SetResourceReference(TextBlock.ForegroundProperty, "AccentBrush");
        who.Children.Add(glyph);
        _padFooterName = new TextBlock { Text = "Controller", FontSize = 12.5, FontWeight = FontWeights.Bold, VerticalAlignment = VerticalAlignment.Center, Margin = new Thickness(10, 0, 0, 0) };
        _padFooterName.SetResourceReference(TextBlock.ForegroundProperty, "TextBrush");
        who.Children.Add(_padFooterName);
        bar.Children.Add(who);

        // Right: the hints. Shrinks (never grows) when the window is narrow, so the labels never overlap.
        var fit = new Viewbox { Child = _padHintRow, Stretch = Stretch.Uniform, StretchDirection = StretchDirection.DownOnly, HorizontalAlignment = HorizontalAlignment.Right, VerticalAlignment = VerticalAlignment.Center, MaxHeight = 30 };
        Grid.SetColumn(fit, 1);
        bar.Children.Add(fit);

        _padHints = new Border
        {
            Child = bar, Height = PadFooterHeight, BorderThickness = new Thickness(0, 1, 0, 0),
            BorderBrush = new SolidColorBrush(Color.FromArgb(0x1F, 0xFF, 0xFF, 0xFF)),
            HorizontalAlignment = HorizontalAlignment.Stretch, VerticalAlignment = VerticalAlignment.Bottom,
            IsHitTestVisible = false, Visibility = Visibility.Collapsed
        };
        _padHints.SetResourceReference(Border.BackgroundProperty, "Tint_F0141414");
        Grid.SetRow(_padHints, Grid.GetRow(ContentHost));
        Panel.SetZIndex(_padHints, 40);
        main.Children.Add(_padHints);
    }

    // One hint: the button picture(s) in a key cap, then what it does.
    private FrameworkElement PadHintChip(string[] icons, string text, bool first)
    {
        var chip = new StackPanel { Orientation = Orientation.Horizontal, VerticalAlignment = VerticalAlignment.Center, Margin = new Thickness(first ? 0 : 20, 0, 0, 0) };
        var keys = new StackPanel { Orientation = Orientation.Horizontal, VerticalAlignment = VerticalAlignment.Center };
        for (int i = 0; i < icons.Length; i++)
        {
            if (i > 0) keys.Children.Add(new TextBlock { Text = "/", FontSize = 12, Foreground = Brushes.Gray, VerticalAlignment = VerticalAlignment.Center, Margin = new Thickness(3, 0, 3, 0) });
            keys.Children.Add(PadGlyph(icons[i], 22));
        }
        chip.Children.Add(new Border
        {
            Child = keys, Background = new SolidColorBrush(Color.FromArgb(0x14, 0xFF, 0xFF, 0xFF)), BorderBrush = new SolidColorBrush(Color.FromArgb(0x1F, 0xFF, 0xFF, 0xFF)),
            BorderThickness = new Thickness(1), CornerRadius = new CornerRadius(9), Padding = new Thickness(5, 3, 5, 3), VerticalAlignment = VerticalAlignment.Center
        });
        var label = new TextBlock { Text = text, FontSize = 13, FontWeight = FontWeights.Bold, VerticalAlignment = VerticalAlignment.Center, Margin = new Thickness(8, 0, 0, 0) };
        label.SetResourceReference(TextBlock.ForegroundProperty, "TextBrush");
        chip.Children.Add(label);
        return chip;
    }

    private void UpdatePadHints(bool force)
    {
        if (_padHints is null || _padHintRow is null) return;
        // Only once the launcher has seen controller input; mouse or keyboard use hides them again.
        bool show = _padMode && _padConnected && _model.ControllerNavigation;
        var root = PadRoot();
        var hints = new List<(string[] Icons, string Text)>();
        string where;
        if (root is not null && root == _padKeyboard)
        {
            where = "Keyboard";
            hints.Add((new[] { "a" }, "Type")); hints.Add((new[] { "x" }, "Delete")); hints.Add((new[] { "y" }, "Space"));
            hints.Add((new[] { "lb", "rb" }, "Shift")); hints.Add((new[] { "b" }, "Done"));
        }
        else if (root == PlayerSelectRoot)
        {
            where = "Players";
            hints.Add((new[] { "a" }, "Select")); hints.Add((new[] { "b" }, "Done"));
        }
        else if (root == SettingsRoot || root == ThemeEditorRoot)
        {
            where = "Settings";
            if (root == SettingsRoot) hints.Add((new[] { "lb", "rb" }, "Page"));
            hints.Add((new[] { "rs" }, "Scroll")); hints.Add((new[] { "a" }, "Select")); hints.Add((new[] { "b" }, "Close"));
        }
        else if (_padInRail || root != ContentHost)
        {
            where = _padInRail ? "Menu" : "Chapters";
            hints.Add((new[] { "a" }, "Select")); hints.Add((new[] { "b" }, "Back"));
        }
        else
        {
            where = "";
            hints.Add((new[] { "lb", "rb" }, "Screens"));
            if (_model.IsPlayScreen) { hints.Add((new[] { "dpad-left", "dpad-right" }, "Season")); hints.Add((new[] { "y" }, "Play")); }
            else if (_model.IsHomeScreen || _model.IsSeasonScreen) hints.Add((new[] { "y" }, "Play"));
            hints.Add((new[] { "a" }, "Select"));
            if (!_model.IsHomeScreen) hints.Add((new[] { "b" }, "Back"));
            hints.Add((new[] { "x" }, "Settings"));
            hints.Add((new[] { "menu" }, "Players"));
            hints.Add((new[] { "view" }, "Menu"));
        }
        string key = show + "|" + _padPlayStation + "|" + where + "|" + string.Join(";", hints.Select(h => string.Join(",", h.Icons) + h.Text));
        if (!force && key == _padHintKey) return;
        _padHintKey = key;
        _padHints.Visibility = show ? Visibility.Visible : Visibility.Collapsed;
        // Lift the pages by the bar's height while it shows, so it never covers PLAY or the settings.
        if (show != _padFooterShown)
        {
            _padFooterShown = show;
            if (show)
            {
                _contentMarginBeforeHints = ContentHost.Margin;
                var m = _contentMarginBeforeHints;
                ContentHost.Margin = new Thickness(m.Left, m.Top, m.Right, m.Bottom + PadFooterHeight);
            }
            // Clear rather than restore: ContentHost's margin comes from its style (the OG launcher theme changes it).
            else ContentHost.ClearValue(FrameworkElement.MarginProperty);
        }
        if (_padFooterName is not null)
            _padFooterName.Text = (_padPlayStation ? "PlayStation controller" : "Controller") + (where.Length > 0 ? "  ·  " + where : "");
        _padHintRow.Children.Clear();
        foreach (var (icons, text) in hints) _padHintRow.Children.Add(PadHintChip(icons, text, _padHintRow.Children.Count == 0));
    }

    // ---------------------------------------------------------------- on-screen keyboard
    // A on a text box opens it. The d-pad / stick moves over the keys, A types, X deletes, Y adds a space, LB / RB
    // switch case, B (or Menu / Done) closes it and puts the controller back on the box.

    private Border? _padKeyboard;
    private Control? _padKeyTarget;
    private TextBlock? _padKeyPreview;
    private bool _padKeyUpper;
    private readonly List<(Button Key, string Lower)> _padLetterKeys = new();

    private bool PadKeyboardOpen => _padKeyboard is { Visibility: Visibility.Visible } && _padKeyTarget is not null;

    private void OpenPadKeyboard(Control target)
    {
        if (_padKeyboard is null) BuildPadKeyboard();
        if (_padKeyboard is null) return;
        _padKeyTarget = target;
        _padInRail = false;
        UpdatePadKeyPreview();
        _padKeyboard.Visibility = Visibility.Visible;
        Keyboard.ClearFocus();
        FocusPadRoot();
        UpdatePadHints(force: true);
    }

    private void ClosePadKeyboard()
    {
        if (_padKeyboard is null) return;
        _padKeyboard.Visibility = Visibility.Collapsed;
        var target = _padKeyTarget;
        _padKeyTarget = null;
        if (target is { IsVisible: true }) target.Focus();
        else FocusPadRoot();
        UpdatePadHints(force: true);
    }

    private void BuildPadKeyboard()
    {
        if (PlayerSelectRoot.Parent is not Grid host) return;
        var rows = new[] { "1234567890", "qwertyuiop", "asdfghjkl@", "zxcvbnm-_.", ":/!?#&+=()" };
        var keys = new StackPanel { HorizontalAlignment = HorizontalAlignment.Center };
        Button Key(string label, double width, Action press)
        {
            var b = new Button { Width = width, Height = 40, Margin = new Thickness(3), Content = label, FontSize = 15, FontWeight = FontWeights.SemiBold, FocusVisualStyle = null };
            b.SetResourceReference(StyleProperty, "GhostButton");
            b.Click += (_, _) => press();
            return b;
        }
        _padLetterKeys.Clear();
        foreach (var row in rows)
        {
            var line = new StackPanel { Orientation = Orientation.Horizontal, HorizontalAlignment = HorizontalAlignment.Center };
            foreach (char ch in row)
            {
                string lower = ch.ToString();
                Button? b = null;
                b = Key(lower, 46, () => PadKeyType(_padKeyUpper ? lower.ToUpperInvariant() : lower));
                if (char.IsLetter(ch)) _padLetterKeys.Add((b, lower));
                line.Children.Add(b);
            }
            keys.Children.Add(line);
        }
        var bottom = new StackPanel { Orientation = Orientation.Horizontal, HorizontalAlignment = HorizontalAlignment.Center };
        bottom.Children.Add(Key("Shift", 92, PadKeyShift));
        bottom.Children.Add(Key("Space", 200, () => PadKeyType(" ")));
        bottom.Children.Add(Key("Delete", 92, PadKeyBackspace));
        bottom.Children.Add(Key("Done", 92, ClosePadKeyboard));
        keys.Children.Add(bottom);

        _padKeyPreview = new TextBlock { FontSize = 16, FontWeight = FontWeights.SemiBold, Margin = new Thickness(6, 0, 6, 12), TextTrimming = TextTrimming.CharacterEllipsis };
        _padKeyPreview.SetResourceReference(TextBlock.ForegroundProperty, "TextBrush");
        var panel = new StackPanel();
        panel.Children.Add(_padKeyPreview);
        panel.Children.Add(keys);
        var sheet = new Border
        {
            Child = panel, Padding = new Thickness(18), CornerRadius = new CornerRadius(14), BorderThickness = new Thickness(1),
            BorderBrush = new SolidColorBrush(Color.FromArgb(0x33, 0xFF, 0xFF, 0xFF)),
            HorizontalAlignment = HorizontalAlignment.Center, VerticalAlignment = VerticalAlignment.Bottom, Margin = new Thickness(0, 0, 0, 70)
        };
        sheet.SetResourceReference(Border.BackgroundProperty, "Tint_F0141414");
        _padKeyboard = new Border { Child = sheet, Background = new SolidColorBrush(Color.FromArgb(0x88, 0, 0, 0)), Visibility = Visibility.Collapsed };
        Panel.SetZIndex(_padKeyboard, 200);
        host.Children.Add(_padKeyboard);
    }

    private void UpdatePadKeyPreview()
    {
        if (_padKeyPreview is null) return;
        string text = _padKeyTarget switch { TextBox t => t.Text, PasswordBox p => new string('•', p.Password.Length), _ => "" };
        _padKeyPreview.Text = (text.Length == 0 ? " " : text) + "_";
        foreach (var (key, lower) in _padLetterKeys) key.Content = _padKeyUpper ? lower.ToUpperInvariant() : lower;
    }

    private void PadKeyType(string s)
    {
        switch (_padKeyTarget)
        {
            case TextBox t:
                int at = t.SelectionStart, len = t.SelectionLength;
                string next = t.Text.Remove(at, len).Insert(at, s);
                if (t.MaxLength > 0 && next.Length > t.MaxLength) return;
                t.Text = next;
                t.CaretIndex = at + s.Length;
                t.GetBindingExpression(TextBox.TextProperty)?.UpdateSource();
                break;
            case PasswordBox p:
                p.Password += s;
                break;
        }
        UpdatePadKeyPreview();
    }

    private void PadKeyBackspace()
    {
        switch (_padKeyTarget)
        {
            case TextBox t when t.SelectionLength > 0:
                int start = t.SelectionStart;
                t.Text = t.Text.Remove(start, t.SelectionLength);
                t.CaretIndex = start;
                t.GetBindingExpression(TextBox.TextProperty)?.UpdateSource();
                break;
            case TextBox t when t.CaretIndex > 0:
                int at = t.CaretIndex;
                t.Text = t.Text.Remove(at - 1, 1);
                t.CaretIndex = at - 1;
                t.GetBindingExpression(TextBox.TextProperty)?.UpdateSource();
                break;
            case PasswordBox p when p.Password.Length > 0:
                p.Password = p.Password[..^1];
                break;
        }
        UpdatePadKeyPreview();
    }

    private void PadKeyShift()
    {
        _padKeyUpper = !_padKeyUpper;
        UpdatePadKeyPreview();
    }
}
