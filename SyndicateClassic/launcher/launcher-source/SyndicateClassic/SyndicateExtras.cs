// Syndicate © 2026 Syndicate. All rights reserved. Proprietary — no copying, distribution, or derivative works. See LICENSE.
using System;
using System.IO;
using System.Linq;
using System.Text.RegularExpressions;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Input;
using System.Windows.Media;
using System.Windows.Media.Animation;

namespace SyndicateClassic;
public partial class MainWindow
{
    // ---- Tab transitions -------------------------------------------------
    private FrameworkElement? _homeView;
    private int _railIndex = -1;

    // Slides an element in from the side (or from below) with a soft ease and fade.
    private static void SlideIn(FrameworkElement element, double fromX, double fromY = 0, int milliseconds = 340)
    {
        if (!SystemParameters.ClientAreaAnimation) return;
        TranslateTransform? move = element.RenderTransform as TranslateTransform;
        if (move == null && element.RenderTransform is TransformGroup groupTransform) move = groupTransform.Children.OfType<TranslateTransform>().FirstOrDefault();
        if (move == null && (element.RenderTransform == null || element.RenderTransform == Transform.Identity)) { move = new TranslateTransform(); element.RenderTransform = move; }
        var ease = new CubicEase { EasingMode = EasingMode.EaseOut };
        var time = TimeSpan.FromMilliseconds(milliseconds);
        if (move != null && !move.IsFrozen)
        {
            move.BeginAnimation(TranslateTransform.XProperty, new DoubleAnimation(fromX, 0, time) { EasingFunction = ease });
            move.BeginAnimation(TranslateTransform.YProperty, new DoubleAnimation(fromY, 0, time) { EasingFunction = ease });
        }
        element.BeginAnimation(OpacityProperty, new DoubleAnimation(0, 1, TimeSpan.FromMilliseconds(milliseconds * 0.8)));
    }

    // Called whenever the side rail changes tab: Home (0), Battle passes (1), Packs (2), Multiplayer (3).
    private void AnimateRailChange(int index)
    {
        if (index == _railIndex) return;
        int previous = _railIndex; _railIndex = index;
        if (previous < 0) return; // first draw: no animation
        double direction = index > previous ? 1 : -1;
        if (index == 0) { if (_syndicateHomeView != null) SlideIn(_syndicateHomeView, 0, -110); else if (_seasonBrowserView != null) SlideIn(_seasonBrowserView, 0, -110); }
        else if (index == PlayTab) { if (_seasonBrowserView != null) SlideIn(_seasonBrowserView, 110, 0); }
        else if (index == OnlineTab) { if (_onlineView != null) SlideIn(_onlineView, 110, 0); }
        else if (index == 3) { if (_multiplayerView != null) SlideIn(_multiplayerView, 0, 110 * direction); }
        else if (index == 4) { if (_statsView != null) SlideIn(_statsView, 0, 110 * direction); }
        else if (_storeBody != null) SlideIn(_storeBody, 0, 110 * direction);
    }

    private void HookSettingsAnimation()
    {
        SettingsPanel.IsVisibleChanged += (_, __) => { if (SettingsPanel.IsVisible) SlideIn(SettingsPanel, 0, 46, 300); };
    }

    // ---- Cosmetics: unlock all ------------------------------------------------
    private void AddCosmeticsSettings(Panel group)
    {
        var state = Label("Sign in to a player to use this.", 13, "#D9E8DF"); state.Margin = new Thickness(0, 4, 0, 8); state.TextWrapping = TextWrapping.Wrap;
        // An on/off switch, like Save the World skins below it.
        var unlockWords = new StackPanel();
        unlockWords.Children.Add(Label("Unlock all cosmetics", 13, "#D9E8DF"));
        unlockWords.Children.Add(new TextBlock { FontSize = 11, Foreground = Ink("#9DB6A6"), TextWrapping = TextWrapping.Wrap, Margin = new Thickness(0, 3, 0, 0), Text = "Turning it off removes only what it unlocked; everything you owned stays." });
        var toggle = new CheckBox { Content = unlockWords, IsChecked = false, IsEnabled = false, Margin = new Thickness(0, 2, 0, 0),
            Foreground = Ink("#D9E8DF"), Template = SwitchTemplate.Value, Cursor = System.Windows.Input.Cursors.Hand, ToolTip = "Turn Unlock all on or off" };
        var hint = new TextBlock { FontSize = 11, Foreground = Ink("#9DB6A6"), TextWrapping = TextWrapping.Wrap, Margin = new Thickness(0, 10, 0, 0),
            Text = "Cosmetics are read from each Fortnite season you play. If a season shows nothing new, play one match in it, return to the lobby and it fills in. Takes effect next time the lobby loads." };
        bool enabled = false, busy = false, unlockFilling = false;
        Func<System.Threading.Tasks.Task>? reloadHeroes = null;
        void Show(int available)
        {
            state.Text = enabled ? $"Unlock all is ON for {_account.Username}." + (available > 0 ? $" {available} cosmetics available." : "") : $"Unlock all is OFF for {_account.Username}.";
            unlockFilling = true; toggle.IsChecked = enabled; unlockFilling = false;
        }
        async System.Threading.Tasks.Task Refresh()
        {
            if (!_account.SignedIn) { state.Text = "Choose a player first, then come back here."; toggle.IsEnabled = false; return; }
            toggle.IsEnabled = true;
            try { var r = await _account.UnlockAll(null); enabled = r.Enabled; Show(r.Available); }
            catch (Exception error) { state.Text = "Couldn't read the setting: " + error.GetBaseException().Message; }
            if (reloadHeroes != null) await reloadHeroes();
        }
        async System.Threading.Tasks.Task ChangeUnlockAll(bool on)
        {
            if (unlockFilling || busy || !_account.SignedIn) return; busy = true; toggle.IsEnabled = false;
            state.Text = on ? "Unlocking cosmetics…" : "Removing unlocked cosmetics…";
            try
            {
                var r = await _account.UnlockAll(on); enabled = r.Enabled; Show(r.Available);
                SetStatus(enabled ? $"Unlocked {r.Changed} cosmetics. Restart Fortnite's lobby to see them." : $"Removed {r.Changed} unlocked cosmetics. Your own cosmetics were kept.");
            }
            catch (Exception error) { state.Text = "Couldn't change it: " + error.GetBaseException().Message; unlockFilling = true; toggle.IsChecked = enabled; unlockFilling = false; }
            finally { busy = false; toggle.IsEnabled = _account.SignedIn; }
            if (reloadHeroes != null) await reloadHeroes();
        }
        toggle.Checked += async (_, __) => await ChangeUnlockAll(true);
        toggle.Unchecked += async (_, __) => await ChangeUnlockAll(false);
        group.IsVisibleChanged += async (_, __) => { if (group.IsVisible) await Refresh(); };
        group.Children.Add(state); group.Children.Add(toggle); group.Children.Add(hint);

        // "Save the World skins", right under Unlock all and only usable while it is on: every Save the World hero
        // becomes an outfit in Fortnite's locker (Pre-season to Chapter 2 Season 1), and can be picked below.
        bool stwOn = false, stwBusy = false, stwFilling = false;
        Func<System.Threading.Tasks.Task>? heroPicker = null;
        var stwNote = new TextBlock { FontSize = 11, Foreground = Ink("#9DB6A6"), TextWrapping = TextWrapping.Wrap, Margin = new Thickness(0, 3, 0, 0),
            Text = "Turn on Unlock all first, then switch this on." };
        var stwWords = new StackPanel();
        stwWords.Children.Add(Label("Save the World skins", 13, "#D9E8DF")); stwWords.Children.Add(stwNote);
        var stwSwitch = new CheckBox { Content = stwWords, IsChecked = false, IsEnabled = false, Opacity = 0.5, Margin = new Thickness(0, 18, 0, 0),
            Foreground = Ink("#D9E8DF"), Template = SwitchTemplate.Value, Cursor = System.Windows.Input.Cursors.Hand, ToolTip = "Needs Unlock all on" };
        void ShowStw()
        {
            stwFilling = true; stwSwitch.IsChecked = stwOn; stwFilling = false;
            bool usable = _account.SignedIn && enabled && !stwBusy;
            stwSwitch.IsEnabled = usable; stwSwitch.Opacity = _account.SignedIn && enabled ? 1 : 0.5;
            stwNote.Text = !enabled ? "Turn on Unlock all first, then switch this on."
                : stwOn ? "On: Save the World heroes are outfits in your locker in every season (Pre-season to Chapter 2 Season 1). Restart Fortnite's lobby to see them."
                : "Off. Switch on to add every Save the World hero to your locker as an outfit.";
        }
        async System.Threading.Tasks.Task RefreshStw()
        {
            if (!_account.SignedIn || !enabled) { stwOn = false; ShowStw(); return; }
            try { stwOn = await _account.StwSkins(null); }
            catch (Exception error) { stwNote.Text = "Couldn't read the setting: " + error.GetBaseException().Message; return; }
            ShowStw();
        }
        async System.Threading.Tasks.Task ChangeStw(bool on)
        {
            if (stwFilling || stwBusy || !_account.SignedIn) return;
            stwBusy = true; stwSwitch.IsEnabled = false;
            try
            {
                stwOn = await _account.StwSkins(on);
                SetStatus(stwOn ? "Save the World skins are on. Restart Fortnite's lobby to see the heroes in your locker." : "Save the World skins are off. The heroes leave your locker next time the lobby loads.");
            }
            catch (Exception error) { SetStatus("Couldn't change Save the World skins: " + error.GetBaseException().Message, okay: false); }
            finally { stwBusy = false; ShowStw(); }
            if (heroPicker != null) await heroPicker();
        }
        stwSwitch.Checked += async (_, __) => await ChangeStw(true);
        stwSwitch.Unchecked += async (_, __) => await ChangeStw(false);
        group.Children.Add(stwSwitch);

        heroPicker = AddStwHeroPicker(group, () => enabled && stwOn);
        reloadHeroes = async () => { await RefreshStw(); if (heroPicker != null) await heroPicker(); };
    }

    // ---- Cosmetics: remove default outfits from the locker -------------------
    private void AddHideDefaultsSettings(Panel group)
    {
        var state = Label("Sign in to a player to use this.", 13, "#D9E8DF"); state.Margin = new Thickness(0, 4, 0, 8); state.TextWrapping = TextWrapping.Wrap;
        var toggle = new Button { Content = "Remove default outfits", Height = 36, Padding = new Thickness(14, 6, 14, 6), HorizontalAlignment = HorizontalAlignment.Left };
        PolishButton(toggle, "#B1E9C9", "#10251A");
        var hint = new TextBlock { FontSize = 11, Foreground = Ink("#9DB6A6"), TextWrapping = TextWrapping.Wrap, Margin = new Thickness(0, 10, 0, 0),
            Text = "Hides the free default outfits, pickaxe and glider from the locker. A default you have equipped is swapped to one of your own cosmetics first; if a slot has nothing else, its default is kept. Turn it off to put the defaults back. Takes effect next time the lobby loads." };
        bool enabled = false, busy = false;
        void Show()
        {
            state.Text = enabled ? $"Defaults are HIDDEN for {_account.Username}." : $"Defaults are SHOWN for {_account.Username}.";
            toggle.Content = enabled ? "Put defaults back" : "Remove default outfits";
            PolishButton(toggle, enabled ? "#43372A" : "#B1E9C9", enabled ? "#E8D4B4" : "#10251A");
        }
        async System.Threading.Tasks.Task Refresh()
        {
            if (!_account.SignedIn) { state.Text = "Choose a player first, then come back here."; toggle.IsEnabled = false; return; }
            toggle.IsEnabled = true;
            try { var r = await _account.HideDefaults(null); enabled = r.Enabled; Show(); }
            catch (Exception error) { state.Text = "Couldn't read the setting: " + error.GetBaseException().Message; }
        }
        toggle.Click += async (_, __) =>
        {
            if (busy || !_account.SignedIn) return; busy = true; toggle.IsEnabled = false;
            state.Text = enabled ? "Putting defaults back…" : "Removing defaults…";
            try
            {
                var r = await _account.HideDefaults(!enabled); enabled = r.Enabled; Show();
                SetStatus(enabled ? $"Removed the default outfits. Restart Fortnite's lobby to see the change." : $"Put the default outfits back. Restart Fortnite's lobby to see them.");
            }
            catch (Exception error) { state.Text = "Couldn't change it: " + error.GetBaseException().Message; }
            finally { busy = false; toggle.IsEnabled = _account.SignedIn; }
        };
        group.IsVisibleChanged += async (_, __) => { if (group.IsVisible) await Refresh(); };
        group.Children.Add(state); group.Children.Add(toggle); group.Children.Add(hint);
    }

    // ---- Cosmetics: remove unreleased win umbrellas --------------------------
    private void AddHideWinUmbrellasSettings(Panel group)
    {
        var state = Label("Sign in to a player to use this.", 13, "#D9E8DF"); state.Margin = new Thickness(0, 4, 0, 8); state.TextWrapping = TextWrapping.Wrap;
        var toggle = new Button { Content = "Remove win umbrellas", Height = 36, Padding = new Thickness(14, 6, 14, 6), HorizontalAlignment = HorizontalAlignment.Left };
        PolishButton(toggle, "#B1E9C9", "#10251A");
        var hint = new TextBlock { FontSize = 11, Foreground = Ink("#9DB6A6"), TextWrapping = TextWrapping.Wrap, Margin = new Thickness(0, 10, 0, 0),
            Text = "Hides the unreleased Bronze (25 wins) and Gold (50 wins) milestone umbrellas from the locker. If you have one equipped it swaps to another of your gliders first. Turn it off to bring back the ones you've earned. Takes effect next time the lobby loads." };
        bool enabled = false, busy = false;
        void Show()
        {
            state.Text = enabled ? $"Win umbrellas are HIDDEN for {_account.Username}." : $"Win umbrellas are SHOWN for {_account.Username}.";
            toggle.Content = enabled ? "Put win umbrellas back" : "Remove win umbrellas";
            PolishButton(toggle, enabled ? "#43372A" : "#B1E9C9", enabled ? "#E8D4B4" : "#10251A");
        }
        async System.Threading.Tasks.Task Refresh()
        {
            if (!_account.SignedIn) { state.Text = "Choose a player first, then come back here."; toggle.IsEnabled = false; return; }
            toggle.IsEnabled = true;
            try { var r = await _account.HideWinUmbrellas(null); enabled = r.Enabled; Show(); }
            catch (Exception error) { state.Text = "Couldn't read the setting: " + error.GetBaseException().Message; }
        }
        toggle.Click += async (_, __) =>
        {
            if (busy || !_account.SignedIn) return; busy = true; toggle.IsEnabled = false;
            state.Text = enabled ? "Putting win umbrellas back…" : "Removing win umbrellas…";
            try
            {
                var r = await _account.HideWinUmbrellas(!enabled); enabled = r.Enabled; Show();
                SetStatus(enabled ? "Removed the unreleased win umbrellas. Restart Fortnite's lobby to see the change." : "Put your earned win umbrellas back. Restart Fortnite's lobby to see them.");
            }
            catch (Exception error) { state.Text = "Couldn't change it: " + error.GetBaseException().Message; }
            finally { busy = false; toggle.IsEnabled = _account.SignedIn; }
        };
        group.IsVisibleChanged += async (_, __) => { if (group.IsVisible) await Refresh(); };
        group.Children.Add(state); group.Children.Add(toggle); group.Children.Add(hint);
    }

    // Save the World hero in Battle Royale (only while Unlock all is on). Fortnite's lobby can't list heroes, so
    // the pick is made here; the match server puts the hero on the player in matches. The lobby keeps the outfit.
    private Func<System.Threading.Tasks.Task> AddStwHeroPicker(Panel group, Func<bool> unlockAllOn)
    {
        var title = Label("Save the World hero in matches", 13, "#D9E8DF"); title.Margin = new Thickness(0, 18, 0, 6);
        var combo = new ComboBox { MinWidth = 320, Height = 34, HorizontalAlignment = HorizontalAlignment.Left, IsEnabled = false };
        var note = new TextBlock { FontSize = 11, Foreground = Ink("#9DB6A6"), TextWrapping = TextWrapping.Wrap, Margin = new Thickness(0, 8, 0, 0),
            Text = "Turn on Unlock all and Save the World skins to pick one, or equip a hero in Fortnite's locker. You wear the hero in matches. A season that doesn't have the hero keeps your outfit." };
        bool filling = false;
        async System.Threading.Tasks.Task Load()
        {
            if (!_account.SignedIn || !unlockAllOn()) { combo.IsEnabled = false; return; }
            try
            {
                var r = await _account.StwHero(null);
                var current = r.TryGetProperty("hero", out var h) ? h.GetString() ?? "" : "";
                filling = true; combo.Items.Clear();
                combo.Items.Add(new ComboBoxItem { Content = "None (wear my outfit)", Tag = "" });
                if (r.TryGetProperty("heroes", out var list) && list.ValueKind == System.Text.Json.JsonValueKind.Array)
                    foreach (var hero in list.EnumerateArray())
                    {
                        var id = hero.TryGetProperty("id", out var i) ? i.GetString() : null; if (string.IsNullOrEmpty(id)) continue;
                        combo.Items.Add(new ComboBoxItem { Content = hero.TryGetProperty("name", out var n) ? n.GetString() : id, Tag = id, ToolTip = id });
                    }
                combo.SelectedItem = combo.Items.OfType<ComboBoxItem>().FirstOrDefault(i => (string)i.Tag == current) ?? combo.Items[0];
                combo.IsEnabled = true;
            }
            catch (Exception error) { note.Text = "Couldn't read the heroes: " + error.GetBaseException().Message; }
            finally { filling = false; }
        }
        combo.SelectionChanged += async (_, __) =>
        {
            if (filling || combo.SelectedItem is not ComboBoxItem { Tag: string id }) return;
            combo.IsEnabled = false;
            try { await _account.StwHero(id); SetStatus(id.Length == 0 ? "No Save the World hero: you wear your outfit in matches." : "You'll wear " + ((ComboBoxItem)combo.SelectedItem).Content + " in your next match."); }
            catch (Exception error) { SetStatus("Couldn't save the hero: " + error.GetBaseException().Message, okay: false); await Load(); }
            finally { combo.IsEnabled = _account.SignedIn && unlockAllOn(); }
        };
        group.Children.Add(title); group.Children.Add(combo); group.Children.Add(note);
        return Load; // called by the Unlock all card whenever it has read or changed the setting
    }

    // ---- Redeem code ---------------------------------------------------------
    // One-time codes for items that are not in Unlock all or the shops (Psycho Bandit). Checked by the local
    // backend; the item goes to the signed-in player's locker.
    private void AddRedeemSettings(Panel group)
    {
        var state = Label("Sign in to a player to redeem a code.", 13, "#D9E8DF"); state.Margin = new Thickness(0, 4, 0, 10); state.TextWrapping = TextWrapping.Wrap;
        var box = new TextBox { Height = 44, MaxLength = 40, CharacterCasing = CharacterCasing.Upper, MinWidth = 320, HorizontalAlignment = HorizontalAlignment.Left, VerticalAlignment = VerticalAlignment.Center, ToolTip = "TILT-ABCD-EFGH-JKLM-NPQR" };
        NightField(box); box.FontFamily = NightMono;
        var redeem = new Button { Content = "REDEEM", Height = 46, Padding = new Thickness(22, 0, 22, 0), Margin = new Thickness(10, 0, 0, 0), VerticalAlignment = VerticalAlignment.Center };
        NightButton(redeem, "gold", 10); redeem.FontSize = 18;
        var row = new StackPanel { Orientation = Orientation.Horizontal };
        row.Children.Add(box); row.Children.Add(redeem);
        var hint = new TextBlock { FontSize = 11, Foreground = Ink("#9DB6A6"), TextWrapping = TextWrapping.Wrap, Margin = new Thickness(0, 10, 0, 0),
            Text = "Each code works once. Codes look like TILT-ABCD-EFGH-JKLM-NPQR. The item shows up the next time Fortnite's lobby loads." };

        // The controls sit under a transparent confetti layer that only comes alive on a successful redeem.
        var content = new StackPanel();
        content.Children.Add(state); content.Children.Add(row); content.Children.Add(hint);
        var confetti = new Canvas { IsHitTestVisible = false, ClipToBounds = false };
        var overlay = new Grid();
        overlay.Children.Add(content); overlay.Children.Add(confetti);
        group.Children.Add(overlay);

        bool busy = false;
        void Neutral() => state.Foreground = Ink("#D9E8DF");
        void Refresh()
        {
            bool signedIn = _account.SignedIn;
            box.IsEnabled = redeem.IsEnabled = signedIn && !busy;
            if (!busy) { Neutral(); state.Text = signedIn ? $"Redeeming for {_account.Username}." : "Choose a player first, then come back here."; }
        }
        async System.Threading.Tasks.Task Go()
        {
            if (busy || !_account.SignedIn) return;
            var code = box.Text.Trim();
            if (code.Length == 0) { Neutral(); state.Text = "Type or paste your code first."; return; }
            busy = true; Refresh(); Neutral(); state.Text = "Checking the code…";
            try
            {
                var result = await _account.Redeem(code);
                var title = result.TryGetProperty("title", out var t) ? t.GetString() : null;
                var message = result.TryGetProperty("message", out var m) ? m.GetString() ?? "Redeemed." : "Redeemed.";
                state.Foreground = Ink("#6FE39A"); // success green
                state.Text = (string.IsNullOrWhiteSpace(title) ? "Successfully redeemed! " : "Successfully redeemed " + title + "! ") + message;
                box.Text = ""; SetStatus(message);
                BurstConfetti(confetti);
                try { System.Media.SystemSounds.Asterisk.Play(); } catch { }
            }
            catch (Exception error)
            {
                state.Foreground = Ink("#FF6B6B"); // error red
                state.Text = error.GetBaseException().Message;
                try { System.Media.SystemSounds.Hand.Play(); } catch { }
            }
            finally { busy = false; box.IsEnabled = redeem.IsEnabled = _account.SignedIn; }
        }
        redeem.Click += async (_, __) => await Go();
        box.KeyDown += async (_, e) => { if (e.Key == Key.Enter) { e.Handled = true; await Go(); } };
        group.IsVisibleChanged += (_, __) => { if (group.IsVisible) Refresh(); };
    }

    // A short confetti burst over the redeem card when a code is accepted. Pure code (no asset); each piece
    // falls, sways, spins and fades, then removes itself. Wrapped in try/catch so it can never break a redeem.
    private static void BurstConfetti(Canvas canvas)
    {
        try
        {
            double w = canvas.ActualWidth > 20 ? canvas.ActualWidth : 520;
            var rnd = new Random();
            string[] colors = { "#FFD34D", "#54E08A", "#5AA9FF", "#FF6FB5", "#B98BFF", "#FF8A3D", "#4DE1C4" };
            for (int i = 0; i < 48; i++)
            {
                var piece = new System.Windows.Shapes.Rectangle
                {
                    Width = rnd.Next(6, 11), Height = rnd.Next(9, 16), RadiusX = 1.5, RadiusY = 1.5, Opacity = 0.96,
                    Fill = new SolidColorBrush((Color)ColorConverter.ConvertFromString(colors[rnd.Next(colors.Length)]))
                };
                double startX = rnd.NextDouble() * w;
                Canvas.SetLeft(piece, startX); Canvas.SetTop(piece, -14);
                var rot = new RotateTransform(rnd.Next(360));
                piece.RenderTransformOrigin = new Point(0.5, 0.5); piece.RenderTransform = rot;
                canvas.Children.Add(piece);
                double dur = 1.1 + rnd.NextDouble() * 0.9;
                double endY = 150 + rnd.NextDouble() * 170;
                double drift = (rnd.NextDouble() - 0.5) * 140;
                var fall = new DoubleAnimation(-14, endY, TimeSpan.FromSeconds(dur)) { EasingFunction = new QuadraticEase { EasingMode = EasingMode.EaseIn } };
                var sway = new DoubleAnimation(startX, startX + drift, TimeSpan.FromSeconds(dur)) { EasingFunction = new SineEase { EasingMode = EasingMode.EaseInOut } };
                var spin = new DoubleAnimation(rot.Angle, rot.Angle + rnd.Next(240, 720) * (rnd.Next(2) == 0 ? 1 : -1), TimeSpan.FromSeconds(dur));
                var fade = new DoubleAnimation(0.96, 0, TimeSpan.FromSeconds(dur * 0.45)) { BeginTime = TimeSpan.FromSeconds(dur * 0.55) };
                var me = piece;
                fall.Completed += (_, __) => canvas.Children.Remove(me);
                piece.BeginAnimation(Canvas.TopProperty, fall);
                piece.BeginAnimation(Canvas.LeftProperty, sway);
                rot.BeginAnimation(RotateTransform.AngleProperty, spin);
                piece.BeginAnimation(UIElement.OpacityProperty, fade);
            }
        }
        catch { }
    }

    // ---- Display settings: stretched resolution ----------------------------
    private static string GameSettingsFile => Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),
        "FortniteGame", "Saved", "Config", "WindowsClient", "GameUserSettings.ini");

    private static string SetIniValue(string text, string key, string value)
    {
        var pattern = new Regex("^" + Regex.Escape(key) + "=.*$", RegexOptions.Multiline);
        if (pattern.IsMatch(text)) return pattern.Replace(text, key + "=" + value);
        const string section = "[/Script/FortniteGame.FortGameUserSettings]";
        int at = text.IndexOf(section, StringComparison.OrdinalIgnoreCase);
        if (at < 0) return text.TrimEnd() + "\r\n\r\n" + section + "\r\n" + key + "=" + value + "\r\n";
        int line = text.IndexOf('\n', at); if (line < 0) { text += "\r\n"; line = text.Length - 1; }
        return text.Insert(line + 1, key + "=" + value + "\r\n");
    }

    // The monitor's real desktop size in physical pixels (DPI-independent), for "Back to normal".
    [System.Runtime.InteropServices.DllImport("user32.dll")] private static extern IntPtr GetDC(IntPtr hWnd);
    [System.Runtime.InteropServices.DllImport("user32.dll")] private static extern int ReleaseDC(IntPtr hWnd, IntPtr hDC);
    [System.Runtime.InteropServices.DllImport("gdi32.dll")] private static extern int GetDeviceCaps(IntPtr hdc, int index);
    private static (int Width, int Height) DesktopSize()
    {
        var dc = GetDC(IntPtr.Zero);
        try { return (GetDeviceCaps(dc, 118 /*DESKTOPHORZRES*/), GetDeviceCaps(dc, 117 /*DESKTOPVERTRES*/)); }
        finally { ReleaseDC(IntPtr.Zero, dc); }
    }
    // The monitor's native size (its largest mode), even while a custom stretched desktop resolution is in use.
    [System.Runtime.InteropServices.StructLayout(System.Runtime.InteropServices.LayoutKind.Sequential, CharSet = System.Runtime.InteropServices.CharSet.Unicode)]
    private struct DEVMODE
    {
        [System.Runtime.InteropServices.MarshalAs(System.Runtime.InteropServices.UnmanagedType.ByValTStr, SizeConst = 32)] public string dmDeviceName;
        public short dmSpecVersion, dmDriverVersion, dmSize, dmDriverExtra; public int dmFields;
        public int dmPositionX, dmPositionY, dmDisplayOrientation, dmDisplayFixedOutput;
        public short dmColor, dmDuplex, dmYResolution, dmTTOption, dmCollate;
        [System.Runtime.InteropServices.MarshalAs(System.Runtime.InteropServices.UnmanagedType.ByValTStr, SizeConst = 32)] public string dmFormName;
        public short dmLogPixels; public int dmBitsPerPel, dmPelsWidth, dmPelsHeight, dmDisplayFlags, dmDisplayFrequency;
        public int dmICMMethod, dmICMIntent, dmMediaType, dmDitherType, dmReserved1, dmReserved2, dmPanningWidth, dmPanningHeight;
    }
    [System.Runtime.InteropServices.DllImport("user32.dll", CharSet = System.Runtime.InteropServices.CharSet.Unicode)] private static extern bool EnumDisplaySettings(string? device, int mode, ref DEVMODE devMode);
    private static (int Width, int Height) NativeScreenSize()
    {
        int w = 0, h = 0;
        try
        {
            var mode = new DEVMODE { dmSize = (short)System.Runtime.InteropServices.Marshal.SizeOf<DEVMODE>() };
            for (int i = 0; EnumDisplaySettings(null, i, ref mode); i++)
                if ((long)mode.dmPelsWidth * mode.dmPelsHeight > (long)w * h) { w = mode.dmPelsWidth; h = mode.dmPelsHeight; }
        }
        catch { }
        return w > 0 && h > 0 ? (w, h) : DesktopSize();
    }

    // The field of view that keeps a widescreen-wide view in a narrower stretched picture (same formula as the
    // client mod, SyndicateClient StretchFov): 2 * atan(tan(base / 2) * screen aspect / picture aspect). 0 = none needed.
    internal static double StretchedFov(int w, int h, int sw, int sh, float custom)
    {
        if (w <= 0 || h <= 0 || sw <= 0 || sh <= 0) return 0;
        double picture = (double)w / h, screen = (double)sw / sh;
        if (picture >= screen - .01) return 0;
        double baseFov = custom > 20 && custom < 170 ? custom : 80, rad = Math.PI / 180;
        return Math.Min(150, 2 * Math.Atan(Math.Tan(baseFov * rad / 2) * screen / picture) / rad);
    }

    private void AddDisplaySettings(Panel group)
    {
        var heading = Label("Resolution (width × height)", 13, "#D9E8DF");
        var row = new StackPanel { Orientation = Orientation.Horizontal };
        var width = new TextBox { MinWidth = 64, MaxLength = 4, ToolTip = "Width in pixels, for example 1440" };
        var times = new TextBlock { Text = " × ", Foreground = Brushes.White, VerticalAlignment = VerticalAlignment.Center };
        var height = new TextBox { MinWidth = 64, MaxLength = 4, ToolTip = "Height in pixels, for example 1080" };
        row.Children.Add(width); row.Children.Add(times); row.Children.Add(height);
        var buttons = new StackPanel { Orientation = Orientation.Horizontal, Margin = new Thickness(0, 6, 0, 0) };
        var apply = new Button { Content = "Apply stretched", Padding = new Thickness(10, 6, 10, 6), Margin = new Thickness(0, 0, 8, 0) };
        var reset = new Button { Content = "Back to normal", Padding = new Thickness(10, 6, 10, 6) };
        buttons.Children.Add(apply); buttons.Children.Add(reset);
        var windowed = new CheckBox { Content = NightText("Windowed fullscreen (the launcher switches your desktop to this size while Fortnite is open)", 13, Night.Text, bold: true), Margin = new Thickness(0, 12, 0, 0), IsChecked = DesktopStretchSize().W > 0, Template = SwitchTemplate.Value, Foreground = Hex(Night.Text), Cursor = System.Windows.Input.Cursors.Hand };
        windowed.Foreground = Hex(Night.Text);
        var hint = new TextBlock { FontSize = 11, Foreground = Ink("#9DB6A6"), Text = "Type a resolution (for example 1440 × 1080), press Apply, then start Fortnite. Stretched needs Fullscreen: in Windowed Fullscreen the game always draws at your desktop size, so nothing stretches. Black bars: NVIDIA Control Panel > Adjust desktop size and position > Full-screen, perform scaling on GPU, override the scaling mode set by games. Applies to every season.", TextWrapping = TextWrapping.Wrap, Margin = new Thickness(0, 10, 0, 0) };
        try
        {
            if (File.Exists(GameSettingsFile))
            {
                var text = File.ReadAllText(GameSettingsFile);
                var x = Regex.Match(text, "^ResolutionSizeX=(\\d+)", RegexOptions.Multiline); var y = Regex.Match(text, "^ResolutionSizeY=(\\d+)", RegexOptions.Multiline);
                if (x.Success) width.Text = x.Groups[1].Value; if (y.Success) height.Text = y.Groups[1].Value;
            }
            // Detected screen: when the game is at the screen's own size (not stretched yet), suggest the classic
            // 4:3 stretched size for this screen's height (1440 x 1080 on 1080p, 1920 x 1440 on 1440p).
            var (sw, sh) = NativeScreenSize();
            if (sw >= 640 && sh >= 480 && (width.Text == sw.ToString() || width.Text.Length == 0) && (height.Text == sh.ToString() || height.Text.Length == 0))
            { width.Text = (sh * 4 / 3).ToString(); height.Text = sh.ToString(); }
        }
        catch { }
        apply.Click += (_, __) =>
        {
            if (!int.TryParse(width.Text.Trim(), out int w) || !int.TryParse(height.Text.Trim(), out int h) || w < 640 || h < 480 || w > 7680 || h > 4320)
            { hint.Text = "Enter a width from 640 to 7680 and a height from 480 to 4320."; return; }
            try
            {
                var file = GameSettingsFile; Directory.CreateDirectory(Path.GetDirectoryName(file)!);
                if (File.Exists(file)) File.SetAttributes(file, File.GetAttributes(file) & ~FileAttributes.ReadOnly);
                var text = File.Exists(file) ? File.ReadAllText(file) : "[/Script/FortniteGame.FortGameUserSettings]\r\n";
                foreach (var key in new[] { "ResolutionSizeX", "LastUserConfirmedResolutionSizeX", "DesiredScreenWidth", "LastUserConfirmedDesiredScreenWidth" }) text = SetIniValue(text, key, w.ToString());
                foreach (var key in new[] { "ResolutionSizeY", "LastUserConfirmedResolutionSizeY", "DesiredScreenHeight", "LastUserConfirmedDesiredScreenHeight" }) text = SetIniValue(text, key, h.ToString());
                // Fullscreen (0) draws at this size and the screen stretches it. Windowed fullscreen (1) always draws at the
                // desktop's size, so for it the launcher switches the desktop to this size while Fortnite is open.
                bool desktop = windowed.IsChecked == true;
                if (desktop && !DisplayModeExists(w, h)) { hint.Text = $"Your graphics driver has no {w} × {h} mode, so windowed fullscreen can't use it. NVIDIA Control Panel > Change resolution > Customize > Create Custom Resolution {w} × {h}, then press Apply again (or untick windowed fullscreen)."; return; }
                foreach (var key in new[] { "FullscreenMode", "LastConfirmedFullscreenMode", "PreferredFullscreenMode" }) text = SetIniValue(text, key, desktop ? "1" : "0");
                DesktopStretchSize((desktop ? w : 0, desktop ? h : 0));
                File.WriteAllText(file, text);
                // Read-only stops the game resetting the custom size when it starts.
                File.SetAttributes(file, File.GetAttributes(file) | FileAttributes.ReadOnly);
                SetHudFlag("stretched-on.txt", true); // Season 8-X: the client mod restores the Season 7 stretched view
                var (sw, sh) = NativeScreenSize();
                double fov = StretchedFov(w, h, sw, sh, CustomFov);
                hint.Text = (fov > 0 ? $"Saved {w} × {h} · FOV {fov:0.#}° (applied in game automatically)." : $"Saved {w} × {h}.") + (desktop ? " Your desktop switches to it when Fortnite starts and back when it closes. Black bars: NVIDIA scaling Full-screen, on GPU." : "");
                SetStatus($"Stretched resolution set to {w} × {h}.");
            }
            catch (Exception error) { hint.Text = "Could not save: " + error.Message; }
        };
        reset.Click += (_, __) =>
        {
            try
            {
                var file = GameSettingsFile;
                if (File.Exists(file))
                {
                    File.SetAttributes(file, File.GetAttributes(file) & ~FileAttributes.ReadOnly);
                    var text = File.ReadAllText(file);
                    foreach (var key in new[] { "FullscreenMode", "LastConfirmedFullscreenMode", "PreferredFullscreenMode" }) text = SetIniValue(text, key, "1");
                    // Back to the monitor's own resolution too: leaving the stretched size in the file made Fortnite start
                    // at it every time (and alt-tab switch the monitor's mode), as if stretched were still on.
                    var (dw, dh) = NativeScreenSize();
                    if (dw >= 640 && dh >= 480)
                    {
                        foreach (var key in new[] { "ResolutionSizeX", "LastUserConfirmedResolutionSizeX", "DesiredScreenWidth", "LastUserConfirmedDesiredScreenWidth" }) text = SetIniValue(text, key, dw.ToString());
                        foreach (var key in new[] { "ResolutionSizeY", "LastUserConfirmedResolutionSizeY", "DesiredScreenHeight", "LastUserConfirmedDesiredScreenHeight" }) text = SetIniValue(text, key, dh.ToString());
                        width.Text = dw.ToString(); height.Text = dh.ToString();
                    }
                    File.WriteAllText(file, text);
                }
                SetHudFlag("stretched-on.txt", false); DesktopStretchSize((0, 0)); RestoreDesktop();
                hint.Text = "Back to normal: windowed fullscreen at your monitor's resolution. You can change resolution inside the game again.";
                SetStatus("Stretched resolution turned off.");
            }
            catch (Exception error) { hint.Text = "Could not reset: " + error.Message; }
        };
        foreach (var box in new[] { width, height }) { NightField(box); box.Height = 40; box.Width = 90; }
        times.Foreground = Hex(Night.Sub); times.FontSize = 16; row.Margin = new Thickness(0, 8, 0, 4);
        heading.FontWeight = FontWeights.SemiBold; heading.Foreground = Hex(Night.Text); hint.Foreground = Hex(Night.Sub);
        apply.Height = 38; reset.Height = 38; apply.Margin = new Thickness(0, 6, 8, 0); reset.Margin = new Thickness(0, 6, 0, 0);
        NightButton(apply, "base"); NightButton(reset, "ghost");
        group.Children.Add(heading); group.Children.Add(row); group.Children.Add(windowed); group.Children.Add(buttons); group.Children.Add(hint);
    }

    // ---- Stretched in windowed fullscreen ------------------------------------------------------------------
    // Windowed fullscreen draws at the desktop's size. With "Windowed fullscreen" ticked, the launcher sets the
    // desktop to the stretched size (a temporary display mode, not saved in Windows' settings) right before
    // Fortnite starts, and puts it back when Fortnite closes or the launcher exits. The graphics card's scaling
    // stretches the desktop over the screen; the client mod raises the FOV where the season needs it.
    private static (int W, int H) DesktopStretchSize()
    {
        try { var f = HudFlag("stretched-desktop.txt"); if (!File.Exists(f)) return (0, 0); var p = File.ReadAllText(f).Trim().Split('x'); return p.Length == 2 && int.TryParse(p[0], out var w) && int.TryParse(p[1], out var h) && w >= 640 && h >= 480 ? (w, h) : (0, 0); }
        catch { return (0, 0); }
    }
    private static void DesktopStretchSize((int W, int H) size)
    {
        try { var f = HudFlag("stretched-desktop.txt"); if (size.W <= 0) { if (File.Exists(f)) File.Delete(f); return; } Directory.CreateDirectory(Path.GetDirectoryName(f)!); File.WriteAllText(f, size.W + "x" + size.H); }
        catch { }
    }
    [System.Runtime.InteropServices.DllImport("user32.dll", CharSet = System.Runtime.InteropServices.CharSet.Unicode)] private static extern int ChangeDisplaySettingsEx(string? device, ref DEVMODE mode, IntPtr hwnd, int flags, IntPtr param);
    [System.Runtime.InteropServices.DllImport("user32.dll", CharSet = System.Runtime.InteropServices.CharSet.Unicode)] private static extern int ChangeDisplaySettingsEx(string? device, IntPtr mode, IntPtr hwnd, int flags, IntPtr param);
    private static bool DisplayModeExists(int w, int h) => FindDisplayMode(w, h, out _);
    // The w x h mode with the highest refresh rate this display offers.
    private static bool FindDisplayMode(int w, int h, out DEVMODE best)
    {
        best = default; bool found = false;
        try
        {
            var mode = new DEVMODE { dmSize = (short)System.Runtime.InteropServices.Marshal.SizeOf<DEVMODE>() };
            for (int i = 0; EnumDisplaySettings(null, i, ref mode); i++)
                if (mode.dmPelsWidth == w && mode.dmPelsHeight == h && (!found || mode.dmDisplayFrequency > best.dmDisplayFrequency) && mode.dmBitsPerPel >= 24) { best = mode; found = true; }
        }
        catch { }
        return found;
    }
    private static bool _desktopStretched;
    // Right before Fortnite starts.
    internal void StretchDesktopForGame()
    {
        var (w, h) = DesktopStretchSize(); if (w <= 0) return;
        if (!FindDisplayMode(w, h, out var mode)) { Log($"Stretched (windowed fullscreen): the graphics driver has no {w}x{h} mode; desktop left as it is."); return; }
        mode.dmFields = 0x80000 | 0x100000 | 0x400000; // DM_PELSWIDTH | DM_PELSHEIGHT | DM_DISPLAYFREQUENCY
        int result = ChangeDisplaySettingsEx(null, ref mode, IntPtr.Zero, 0x4 /*CDS_FULLSCREEN: temporary*/, IntPtr.Zero);
        _desktopStretched = result == 0;
        Log(_desktopStretched ? $"Stretched (windowed fullscreen): desktop set to {w}x{h} @ {mode.dmDisplayFrequency} Hz while Fortnite runs." : $"Stretched (windowed fullscreen): Windows refused {w}x{h} (code {result}).");
    }
    // When Fortnite closes, the stretch is turned off, or the launcher exits.
    internal static void RestoreDesktop()
    {
        if (!_desktopStretched) return;
        _desktopStretched = false;
        try { ChangeDisplaySettingsEx(null, IntPtr.Zero, IntPtr.Zero, 0, IntPtr.Zero); } catch { } // back to the saved (normal) mode
    }

    // Custom field of view (every season). The launcher saves it to %LOCALAPPDATA%\ProjectTilted\fov.txt and,
    // when a value is set, loads SyndicateFov.dll into Fortnite, which applies it (and picks up changes live).
    internal static float CustomFov
    {
        get { try { var f = HudFlag("fov.txt"); if (File.Exists(f) && float.TryParse(File.ReadAllText(f).Trim(), System.Globalization.NumberStyles.Float, System.Globalization.CultureInfo.InvariantCulture, out var v) && v >= 60 && v <= 130) return v; } catch { } return 0; }
        set
        {
            try
            {
                var f = HudFlag("fov.txt");
                if (value <= 0) { if (File.Exists(f)) File.Delete(f); return; }
                Directory.CreateDirectory(Path.GetDirectoryName(f)!);
                File.WriteAllText(f, value.ToString("0.#", System.Globalization.CultureInfo.InvariantCulture));
            }
            catch { }
        }
    }

    // Shared with TiltedClient.dll: one log for what the launcher and the module did.
    internal static void ClientModsLog(string line)
    {
        try { File.AppendAllText(HudFlag("tilted-client.log"), DateTime.Now.ToString("HH:mm:ss") + " " + line + Environment.NewLine); } catch { }
    }

    private void AddFovSettings(Panel group)
    {
        // A slider from 60 to 130 (whole degrees); the value is saved as you drag (a moment after you stop),
        // so with Fortnite open you see it change in the game.
        var heading = Label("Field of view (FOV)", 13, "#D9E8DF");
        var row = new Grid { Margin = new Thickness(0, 10, 0, 4) };
        row.ColumnDefinitions.Add(new ColumnDefinition());
        row.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(70) });
        row.ColumnDefinitions.Add(new ColumnDefinition { Width = GridLength.Auto });
        var current = CustomFov;
        var slider = new Slider { Minimum = 60, Maximum = 130, Value = current > 0 ? current : 80, IsSnapToTickEnabled = true, TickFrequency = 1, SmallChange = 1, LargeChange = 5,
            VerticalAlignment = VerticalAlignment.Center, Template = FovSliderTemplate.Value, Focusable = true, Cursor = Cursors.Hand,
            ToolTip = "Drag, or use the arrow keys. Fortnite's normal view is about 80." };
        var value = NightText("", 20, Night.Text, bold: true).Also(t => { t.HorizontalAlignment = HorizontalAlignment.Center; t.VerticalAlignment = VerticalAlignment.Center; });
        var reset = new Button { Content = "Game default", Padding = new Thickness(10, 6, 10, 6), Height = 38 };
        Grid.SetColumn(value, 1); Grid.SetColumn(reset, 2);
        row.Children.Add(slider); row.Children.Add(value); row.Children.Add(reset);
        var hint = new TextBlock { FontSize = 11, TextWrapping = TextWrapping.Wrap, Margin = new Thickness(0, 10, 0, 0) };
        bool on = current > 0;
        void Show()
        {
            value.Text = on ? $"{slider.Value:0}" : "Off";
            value.Foreground = Hex(on ? Night.Gold : Night.Sub);
            hint.Text = on
                ? $"FOV {slider.Value:0}. Works in every season. You can change it at any time, also in the middle of a match: it updates within a second. Aiming down sights and scopes still zoom normally."
                : "Game default. Drag the slider to choose your FOV (Fortnite's normal view is about 80).";
        }
        var save = new System.Windows.Threading.DispatcherTimer { Interval = TimeSpan.FromMilliseconds(250) };
        save.Tick += (_, __) => { save.Stop(); CustomFov = (float)slider.Value; };
        slider.ValueChanged += (_, __) => { on = true; Show(); save.Stop(); save.Start(); };
        reset.Click += (_, __) => { save.Stop(); CustomFov = 0; on = false; Show(); SetStatus("FOV back to the game default."); };
        NightButton(reset, "ghost"); reset.Margin = new Thickness(10, 0, 0, 0);
        heading.FontWeight = FontWeights.SemiBold; heading.Foreground = Hex(Night.Text); hint.Foreground = Hex(Night.Sub);
        heading.Margin = new Thickness(0, 24, 0, 0);
        Show();
        group.Children.Add(heading); group.Children.Add(row); group.Children.Add(hint);
    }

    // Night-themed slider: a thin track, the filled part in gold, a round white knob that grows on hover.
    private static readonly Lazy<ControlTemplate> FovSliderTemplate = new(() => (ControlTemplate)System.Windows.Markup.XamlReader.Parse($$"""
        <ControlTemplate TargetType="Slider" xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation" xmlns:x="http://schemas.microsoft.com/winfx/2006/xaml">
          <Grid Height="30" Background="Transparent">
            <Border Height="6" CornerRadius="3" Background="{{Night.Hover}}" BorderBrush="{{Night.Line}}" BorderThickness="1" VerticalAlignment="Center"/>
            <Track x:Name="PART_Track" VerticalAlignment="Center">
              <Track.DecreaseRepeatButton>
                <RepeatButton Command="Slider.DecreaseLarge" Focusable="False">
                  <RepeatButton.Template><ControlTemplate TargetType="RepeatButton"><Border Height="6" CornerRadius="3,0,0,3" Background="{{Night.Gold}}"/></ControlTemplate></RepeatButton.Template>
                </RepeatButton>
              </Track.DecreaseRepeatButton>
              <Track.IncreaseRepeatButton>
                <RepeatButton Command="Slider.IncreaseLarge" Focusable="False">
                  <RepeatButton.Template><ControlTemplate TargetType="RepeatButton"><Border Background="Transparent"/></ControlTemplate></RepeatButton.Template>
                </RepeatButton>
              </Track.IncreaseRepeatButton>
              <Track.Thumb>
                <Thumb x:Name="Knob" Width="20" Height="20">
                  <Thumb.Template>
                    <ControlTemplate TargetType="Thumb">
                      <Ellipse x:Name="Dot" Fill="{{Night.Text}}" Stroke="{{Night.GoldDeep}}" StrokeThickness="2">
                        <Ellipse.Effect><DropShadowEffect BlurRadius="6" ShadowDepth="1" Opacity="0.5" Color="#000000"/></Ellipse.Effect>
                      </Ellipse>
                      <ControlTemplate.Triggers>
                        <Trigger Property="IsMouseOver" Value="True"><Setter TargetName="Dot" Property="Fill" Value="{{Night.Gold}}"/></Trigger>
                        <Trigger Property="IsDragging" Value="True"><Setter TargetName="Dot" Property="Fill" Value="{{Night.Gold}}"/></Trigger>
                      </ControlTemplate.Triggers>
                    </ControlTemplate>
                  </Thumb.Template>
                </Thumb>
              </Track.Thumb>
            </Track>
          </Grid>
        </ControlTemplate>
        """));
}