// Syndicate © 2026 Syndicate. All rights reserved. Proprietary — no copying, distribution, or derivative works. See LICENSE.
using System;
using System.Linq;
using System.Threading.Tasks;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Controls.Primitives;
using System.Windows.Input;
using System.Windows.Media;
using System.Windows.Media.Animation;
namespace SyndicateClassic;

// "Who's playing?" screen: no email or password. Pick a player saved on this PC
// (from %LOCALAPPDATA%\ProjectTilted\UserData) or type a new name and go.
// Right-click a player for Rename / Delete (launcher-styled menu and dialogs).
public partial class MainWindow
{
    private WrapPanel? _pickerPlayers;
    private TextBox? _pickerName;
    private TextBlock? _pickerMessage;
    private Button? _pickerGo;
    private Task? _backendWarmup;
    private Popup? _pickerMenu;
    private Border? _pickerDialog;

    private const string DangerRed = "#FF4D5E";

    // One shared start-up of the local account service for the picker.
    private Task WarmBackend()
    {
        if (_backendWarmup == null || _backendWarmup.IsCompleted) _backendWarmup = EnsureAccountBackend();
        return _backendWarmup;
    }

    private TextBox PickerTextBox()
    {
        var box = new TextBox { Height = 54, MaxLength = 24, ToolTip = PickerTip("3-24 letters, numbers, spaces, _ or -") };
        NightField(box); box.FontSize = 18; box.Padding = new Thickness(14, 2, 14, 2);
        return box;
    }

    // Dark, rounded tooltip that matches the launcher instead of the default white Windows one.
    private static ToolTip PickerTip(string text)
    {
        var tip = new ToolTip
        {
            Background = Brushes.Transparent, BorderThickness = new Thickness(0), HasDropShadow = false, Padding = new Thickness(0),
            Content = new Border
            {
                Background = GlassFill(), BorderBrush = GlassEdgeFill(), BorderThickness = new Thickness(1), CornerRadius = new CornerRadius(10, 2, 10, 2),
                Padding = new Thickness(10, 6, 10, 6), Margin = new Thickness(8), Child = Body(text, 12, "#E6EAFF"),
                Effect = new System.Windows.Media.Effects.DropShadowEffect { BlurRadius = 16, ShadowDepth = 0, Opacity = .55, Color = Colors.Black }
            }
        };
        tip.Template = new ControlTemplate(typeof(ToolTip)) { VisualTree = new FrameworkElementFactory(typeof(ContentPresenter)) };
        return tip;
    }

    private void BuildPlayerPicker()
    {
        var body = new StackPanel();
        var header = new Grid();
        header.ColumnDefinitions.Add(new ColumnDefinition());
        header.ColumnDefinitions.Add(new ColumnDefinition { Width = GridLength.Auto });
        var titles = new StackPanel();
        titles.Children.Add(NightEyebrow("Player select"));
        titles.Children.Add(Display("WHO'S PLAYING?", 42).Also(t => t.Margin = new Thickness(0, 2, 0, 0)));
        header.Children.Add(titles);
        var close = new Button { Width = 40, Height = 40, Content = GlyphIcon("", 14), ToolTip = PickerTip("Close"), VerticalAlignment = VerticalAlignment.Top };
        RoundButton(close, Night.Frost, Night.Text, 20);
        close.BorderBrush = Hex(Night.FrostEdge); close.BorderThickness = new Thickness(1);
        close.MouseEnter += (_, __) => close.Background = Hex(Night.FrostHover);
        close.MouseLeave += (_, __) => close.Background = Hex(Night.Frost);
        close.Click += (_, __) => { if (!_authBusy) AuthOverlay.Visibility = Visibility.Collapsed; };
        Grid.SetColumn(close, 1); close.Margin = new Thickness(12, 0, 0, 0); header.Children.Add(close);
        body.Children.Add(header);
        body.Children.Add(Body("Pick your player or create a new one. Right-click a player to rename or delete it.", 14, "#AEB8DA").Also(t => t.Margin = new Thickness(0, 10, 0, 0)));

        _pickerPlayers = new WrapPanel { Margin = new Thickness(-6, 18, -6, 8) };
        body.Children.Add(new ScrollViewer { Content = _pickerPlayers, MaxHeight = 290, VerticalScrollBarVisibility = ScrollBarVisibility.Auto, HorizontalScrollBarVisibility = ScrollBarVisibility.Disabled });

        var label = NightEyebrow("New player"); label.Margin = new Thickness(0, 16, 0, 8); body.Children.Add(label);
        var row = new Grid();
        row.ColumnDefinitions.Add(new ColumnDefinition());
        row.ColumnDefinitions.Add(new ColumnDefinition { Width = GridLength.Auto });
        _pickerName = PickerTextBox();
        _pickerName.KeyDown += async (_, e) => { if (e.Key == Key.Enter) { e.Handled = true; await PlayAs(_pickerName.Text); } };
        row.Children.Add(_pickerName);
        _pickerGo = new Button { Width = 170, Height = 54, Margin = new Thickness(12, 0, 0, 0), Content = "LET'S GO" };
        NightButton(_pickerGo, "gold", 10);
        _pickerGo.FontSize = 24;
        _pickerGo.Click += async (_, __) => await PlayAs(_pickerName.Text);
        Grid.SetColumn(_pickerGo, 1); row.Children.Add(_pickerGo);
        body.Children.Add(row);

        _pickerMessage = Body("", 13, "#FFD580"); _pickerMessage.Margin = new Thickness(0, 12, 0, 0); _pickerMessage.MinHeight = 20;
        body.Children.Add(_pickerMessage);
        body.Children.Add(Body("Players are saved on this PC. Your progress, V-Bucks and locker stay with your player.", 12, "#7F89AA"));

        // Card content plus a dialog layer (rename / delete) that sits on top of it.
        _pickerDialog = new Border { Visibility = Visibility.Collapsed, CornerRadius = new CornerRadius(20), Background = new SolidColorBrush(Color.FromArgb(215, 6, 7, 16)) };
        var layers = new Grid();
        layers.Children.Add(new Border { Padding = new Thickness(34), Child = body });
        layers.Children.Add(_pickerDialog);

        var card = new Border
        {
            Width = 660, CornerRadius = new CornerRadius(20, 3, 20, 3),
            Background = GlassFill(), BorderBrush = GlassEdgeFill(), BorderThickness = new Thickness(1),
            HorizontalAlignment = HorizontalAlignment.Center, VerticalAlignment = VerticalAlignment.Center,
            Effect = new System.Windows.Media.Effects.DropShadowEffect { BlurRadius = 50, ShadowDepth = 0, Opacity = .6, Color = Colors.Black }
        };
        var shell = new Grid();
        shell.Children.Add(layers);
        shell.Children.Add(TiltBar(36, 4).Also(b => { b.HorizontalAlignment = HorizontalAlignment.Left; b.VerticalAlignment = VerticalAlignment.Top; b.Margin = new Thickness(20, 0, 0, 0); }));
        card.Child = shell;
        AuthOverlay.Child = card;
        AuthOverlay.Background = new SolidColorBrush(Color.FromArgb(205, 4, 5, 12));
        AuthOverlay.IsVisibleChanged += async (_, __) =>
        {
            if (!AuthOverlay.IsVisible) { CloseMenu(); return; }
            _pickerMessage!.Text = ""; _pickerName!.Text = "";
            HideDialog();
            _pickerName.Focus();
            await LoadPlayers();
        };
        AuthOverlay.PreviewKeyDown += (_, e) => { if (e.Key == Key.Escape && _pickerDialog.Visibility == Visibility.Visible && !_authBusy) { e.Handled = true; HideDialog(); } };
    }

    private async Task LoadPlayers()
    {
        if (_pickerPlayers == null) return;
        _pickerPlayers.Children.Clear();
        _pickerPlayers.Children.Add(Body("Loading players…", 13, "#AEB8DA"));
        try
        {
            await WarmBackend();
            var players = await _account.Players();
            _pickerPlayers.Children.Clear();
            if (players.Length == 0)
            {
                _pickerPlayers.Children.Add(Body("No players on this PC yet. Type a name below to create one.", 13, "#AEB8DA"));
                return;
            }
            foreach (var name in players) _pickerPlayers.Children.Add(PlayerTile(name));
        }
        catch (Exception error)
        {
            _pickerPlayers.Children.Clear();
            _pickerPlayers.Children.Add(Body("Couldn't load players: " + error.GetBaseException().Message, 13, "#FF9B9B"));
        }
    }

    private Button PlayerTile(string name)
    {
        var current = string.Equals(_account.Username, name, StringComparison.OrdinalIgnoreCase);
        var stack = new StackPanel { HorizontalAlignment = HorizontalAlignment.Center, VerticalAlignment = VerticalAlignment.Center };
        // The player's profile icon, in a ring: gold for the current player.
        var ring = new Border
        {
            Width = 70, Height = 70, CornerRadius = new CornerRadius(35), Padding = new Thickness(3), Margin = new Thickness(0, 0, 0, 9),
            Background = current ? GoldFill() : BrandFill(), Child = AvatarFace(AvatarFor(name), name, 64)
        };
        if (current) ring.Effect = new System.Windows.Media.Effects.DropShadowEffect { Color = NightColor(Night.Gold), BlurRadius = 18, ShadowDepth = 0, Opacity = .3 };
        stack.Children.Add(ring);
        var label = Display(name, 17, Night.Text); label.TextWrapping = TextWrapping.NoWrap; label.TextTrimming = TextTrimming.CharacterEllipsis;
        label.MaxWidth = 120; label.HorizontalAlignment = HorizontalAlignment.Center;
        stack.Children.Add(label);
        if (current)
        {
            var tag = new Border
            {
                Background = GoldFill(), CornerRadius = new CornerRadius(8, 2, 8, 2), Padding = new Thickness(7, 2, 9, 3),
                Margin = new Thickness(0, 5, 0, 0), HorizontalAlignment = HorizontalAlignment.Center,
                Child = NightText("PLAYING", 9, Night.OnGold, bold: true).Also(t => t.TextWrapping = TextWrapping.NoWrap)
            };
            stack.Children.Add(tag);
        }
        var inner = new Grid { Width = 148, Height = 154 };
        inner.Children.Add(stack);
        inner.Children.Add(TiltBar(20, 3).Also(b => { b.HorizontalAlignment = HorizontalAlignment.Left; b.VerticalAlignment = VerticalAlignment.Top; b.Margin = new Thickness(9, 7, 0, 0); }));
        var tile = new Button { Width = 148, Height = 154, Margin = new Thickness(6), Content = inner, ToolTip = PickerTip(name + "  ·  click to play, right-click for options"), Padding = new Thickness(6) };
        var edge = current ? (Brush)Hex("#99FFD23F") : GlassEdgeFill();
        RoundButton(tile, "#00000000", Night.Text, 14, current ? "#99FFD23F" : "#33FFFFFF");
        tile.Background = current ? Hex("#1FFFD23F") : GlassFill();
        tile.BorderBrush = edge; tile.BorderThickness = new Thickness(1);
        tile.MouseEnter += (_, __) => tile.BorderBrush = Hex(current ? Night.Gold : "#66FFFFFF");
        tile.MouseLeave += (_, __) => tile.BorderBrush = edge;
        tile.Click += async (_, __) => await PlayAs(name);
        tile.MouseRightButtonUp += (_, e) => { e.Handled = true; ShowPlayerMenu(tile, name); };
        return tile;
    }

    // ---- right-click menu ----------------------------------------------------------------
    private void CloseMenu() { if (_pickerMenu != null) _pickerMenu.IsOpen = false; }

    private Button MenuRow(string glyph, string text, string color, Action onClick)
    {
        var row = new StackPanel { Orientation = Orientation.Horizontal };
        var icon = GlyphIcon(glyph, 14, color); icon.Width = 22; icon.Margin = new Thickness(0, 0, 10, 0);
        row.Children.Add(icon);
        var label = Body(text, 14, color); label.FontWeight = FontWeights.SemiBold; label.VerticalAlignment = VerticalAlignment.Center;
        row.Children.Add(label);
        var button = new Button { Height = 40, Padding = new Thickness(12, 0, 16, 0), Content = row };
        RoundButton(button, "#00FFFFFF", color, 8);
        button.HorizontalContentAlignment = HorizontalAlignment.Left;
        button.MouseEnter += (_, __) => button.Background = Hex("#1FFFFFFF");
        button.MouseLeave += (_, __) => button.Background = Hex("#00FFFFFF");
        button.Click += (_, __) => { CloseMenu(); onClick(); };
        return button;
    }

    private void ShowPlayerMenu(UIElement anchor, string name)
    {
        if (_authBusy) return;
        CloseMenu();
        var list = new StackPanel { MinWidth = 190 };
        var header = Display(name.ToUpperInvariant(), 15, "#AEB8DA"); header.Margin = new Thickness(12, 4, 12, 6);
        header.TextTrimming = TextTrimming.CharacterEllipsis; header.MaxWidth = 220;
        list.Children.Add(header);
        list.Children.Add(new Border { Height = 1, Margin = new Thickness(8, 0, 8, 6), Background = GlassEdge() });
        list.Children.Add(MenuRow("", "Rename", "#EEF1FF", () => ShowRenameDialog(name)));
        list.Children.Add(MenuRow("", "Delete", "#FF8A95", () => ShowDeleteDialog(name)));
        var panel = new Border
        {
            Background = GlassFill(), BorderBrush = GlassEdgeFill(), BorderThickness = new Thickness(1), CornerRadius = new CornerRadius(12, 3, 12, 3),
            Padding = new Thickness(6), Child = list, Margin = new Thickness(12),
            Effect = new System.Windows.Media.Effects.DropShadowEffect { BlurRadius = 24, ShadowDepth = 0, Opacity = .6, Color = Colors.Black }
        };
        _pickerMenu = new Popup
        {
            Child = panel, PlacementTarget = anchor, Placement = PlacementMode.MousePoint, StaysOpen = false,
            AllowsTransparency = true, PopupAnimation = PopupAnimation.Fade
        };
        _pickerMenu.IsOpen = true;
    }

    // ---- dialogs (shown over the picker card) ----------------------------------------------
    private void HideDialog()
    {
        if (_pickerDialog == null) return;
        _pickerDialog.Visibility = Visibility.Collapsed; _pickerDialog.Child = null;
        _pickerName?.Focus();
    }

    private void ShowPickerDialog(FrameworkElement content)
    {
        if (_pickerDialog == null) return;
        var scale = new ScaleTransform(.94, .94);
        var box = new Border
        {
            Width = 470, Padding = new Thickness(28), CornerRadius = new CornerRadius(16, 3, 16, 3),
            Background = GlassFill(), BorderBrush = GlassEdgeFill(), BorderThickness = new Thickness(1),
            HorizontalAlignment = HorizontalAlignment.Center, VerticalAlignment = VerticalAlignment.Center, Child = content,
            Effect = new System.Windows.Media.Effects.DropShadowEffect { BlurRadius = 40, ShadowDepth = 0, Opacity = .7, Color = Colors.Black },
            RenderTransformOrigin = new Point(.5, .5), RenderTransform = scale
        };
        _pickerDialog.Child = box;
        _pickerDialog.Visibility = Visibility.Visible;
        var grow = new DoubleAnimation(.94, 1, TimeSpan.FromMilliseconds(180)) { EasingFunction = new CubicEase { EasingMode = EasingMode.EaseOut } };
        scale.BeginAnimation(ScaleTransform.ScaleXProperty, grow);
        scale.BeginAnimation(ScaleTransform.ScaleYProperty, grow);
        _pickerDialog.BeginAnimation(OpacityProperty, new DoubleAnimation(0, 1, TimeSpan.FromMilliseconds(160)));
    }

    private (StackPanel buttons, Button primary, Button cancel) DialogButtons(string primaryText, string primaryBack, string primaryFore)
    {
        var buttons = new StackPanel { Orientation = Orientation.Horizontal, HorizontalAlignment = HorizontalAlignment.Right, Margin = new Thickness(0, 22, 0, 0) };
        var cancel = new Button { Width = 130, Height = 46, Content = "CANCEL" };
        RoundButton(cancel, "#22FFFFFF", "#FFFFFF", 10, "#33FFFFFF");
        var primary = new Button { Width = 150, Height = 46, Margin = new Thickness(12, 0, 0, 0), Content = primaryText };
        RoundButton(primary, primaryBack, primaryFore, 10);
        foreach (var b in new[] { cancel, primary }) { b.FontFamily = DisplayFont; b.FontStretch = FontStretches.Condensed; b.FontWeight = FontWeights.Bold; b.FontSize = 20; }
        primary.Template = TiltButtonTemplate(); primary.BorderThickness = new Thickness(0, 0, 0, 3);
        cancel.Click += (_, __) => { if (!_authBusy) HideDialog(); };
        buttons.Children.Add(cancel); buttons.Children.Add(primary);
        return (buttons, primary, cancel);
    }

    private void ShowRenameDialog(string name)
    {
        var content = new StackPanel();
        content.Children.Add(Display("RENAME PLAYER", 30));
        var hint = Body("Choose a new name for " + name + ". Progress, V-Bucks and locker stay the same.", 13, "#AEB8DA"); hint.Margin = new Thickness(0, 6, 0, 16);
        content.Children.Add(hint);
        var box = PickerTextBox(); box.Text = name; content.Children.Add(box);
        var error = Body("", 13, "#FF9B9B"); error.Margin = new Thickness(0, 10, 0, 0); error.MinHeight = 18; content.Children.Add(error);
        var (buttons, save, cancel) = DialogButtons("SAVE", PlayYellow, "#15120A");
        content.Children.Add(buttons);
        async Task Submit()
        {
            var next = (box.Text ?? "").Trim();
            if (string.Equals(next, name, StringComparison.Ordinal)) { HideDialog(); return; }
            if (next.Length < 3) { error.Text = "Use at least 3 characters."; return; }
            if (_authBusy) return;
            _authBusy = true; save.IsEnabled = cancel.IsEnabled = box.IsEnabled = false; error.Foreground = Hex("#AEB8DA"); error.Text = "Renaming…";
            try
            {
                await WarmBackend();
                var renamed = await _account.RenamePlayer(name, next);
                SaveAvatar(renamed, AvatarFor(name)); // the icon follows the player to the new name
                if (string.Equals(_account.Username, renamed, StringComparison.Ordinal)) AccountButton.Content = renamed + "  ·  SWITCH";
                HideDialog();
                _pickerMessage!.Foreground = Hex("#FFD580"); _pickerMessage.Text = name + " is now " + renamed + ".";
                await LoadPlayers();
            }
            catch (Exception failure) { error.Foreground = Hex("#FF9B9B"); error.Text = failure.GetBaseException().Message; }
            finally { _authBusy = false; save.IsEnabled = cancel.IsEnabled = box.IsEnabled = true; UpdateButtons(); }
        }
        save.Click += async (_, __) => await Submit();
        box.KeyDown += async (_, e) => { if (e.Key == Key.Enter) { e.Handled = true; await Submit(); } };
        ShowPickerDialog(content);
        box.Focus(); box.SelectAll();
    }

    private void ShowDeleteDialog(string name)
    {
        var content = new StackPanel();
        var title = new StackPanel { Orientation = Orientation.Horizontal };
        var icon = GlyphIcon("", 26, DangerRed); icon.Margin = new Thickness(0, 0, 12, 0); title.Children.Add(icon);
        title.Children.Add(Display("DELETE PLAYER?", 30));
        content.Children.Add(title);
        var who = Display(name, 22, PlayYellow); who.Margin = new Thickness(0, 12, 0, 6); who.TextTrimming = TextTrimming.CharacterEllipsis; content.Children.Add(who);
        content.Children.Add(Body("This removes the player's locker, V-Bucks, Battle Pass and stats from this PC. It can't be undone.", 13, "#AEB8DA"));
        var error = Body("", 13, "#FF9B9B"); error.Margin = new Thickness(0, 10, 0, 0); error.MinHeight = 18; content.Children.Add(error);
        var (buttons, delete, cancel) = DialogButtons("DELETE", DangerRed, "#FFFFFF");
        content.Children.Add(buttons);
        delete.Click += async (_, __) =>
        {
            if (_authBusy) return;
            _authBusy = true; delete.IsEnabled = cancel.IsEnabled = false; error.Foreground = Hex("#AEB8DA"); error.Text = "Deleting…";
            try
            {
                await WarmBackend();
                var wasCurrent = string.Equals(_account.Username, name, StringComparison.OrdinalIgnoreCase);
                await _account.DeletePlayer(name);
                if (wasCurrent) { AccountButton.Content = "CHOOSE PLAYER"; SetStatus("Pick a player to continue."); }
                HideDialog();
                _pickerMessage!.Foreground = Hex("#FFD580"); _pickerMessage.Text = name + " was deleted.";
                await LoadPlayers();
            }
            catch (Exception failure) { error.Foreground = Hex("#FF9B9B"); error.Text = failure.GetBaseException().Message; }
            finally { _authBusy = false; delete.IsEnabled = cancel.IsEnabled = true; UpdateButtons(); }
        };
        ShowPickerDialog(content);
        cancel.Focus();
    }

    private async Task PlayAs(string name)
    {
        name = (name ?? "").Trim();
        if (_authBusy) return;
        _pickerMessage!.Foreground = Hex("#FFD580");
        if (name.Length < 3) { _pickerMessage.Text = "Type a name with at least 3 characters."; return; }
        _authBusy = true; if (_pickerGo != null) _pickerGo.IsEnabled = false;
        _pickerMessage.Text = "Getting " + name + " ready…";
        try
        {
            await WarmBackend();
            await _account.QuickPlay(name);
            AccountButton.Content = _account.Username + "  ·  SWITCH";
            AuthOverlay.Visibility = Visibility.Collapsed;
            SetStatus("Welcome, " + _account.Username + "! Pick a season and press PLAY.");
            if (PassShop.Visibility == Visibility.Visible) await RefreshPasses();
        }
        catch (Exception error) { _pickerMessage.Text = error.GetBaseException().Message; }
        finally { _authBusy = false; if (_pickerGo != null) _pickerGo.IsEnabled = true; UpdateButtons(); }
    }
}