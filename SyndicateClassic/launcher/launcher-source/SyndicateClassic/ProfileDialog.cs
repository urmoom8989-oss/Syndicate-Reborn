// Syndicate © 2026 Syndicate. All rights reserved. Proprietary — no copying, distribution, or derivative works. See LICENSE.
using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text.Json;
using System.Threading.Tasks;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Input;
using System.Windows.Media;
using System.Windows.Media.Imaging;
namespace SyndicateClassic;

// Profile card (from Project Springs): change your display name and pick an OG character icon.
// The icon shows on the rail avatar and next to your name in friends' Multiplayer lists.
// Icons are saved per player on this PC (%LOCALAPPDATA%\ProjectTilted\profile-icons.json).
public partial class MainWindow
{
    // Every picture embedded from launcher-source\avatars (SyndicateClassic.Avatar.<name>), so adding a PNG
    // there is all it takes to offer a new icon. Falls back to og01-og23 if the list cannot be read.
    private static readonly string[] AvatarIds = LoadAvatarIds();
    private static string[] LoadAvatarIds()
    {
        const string prefix = "SyndicateClassic.Avatar.";
        try
        {
            var ids = typeof(MainWindow).Assembly.GetManifestResourceNames().Where(n => n.StartsWith(prefix, StringComparison.Ordinal))
                .Select(n => n.Substring(prefix.Length)).OrderBy(n => n, StringComparer.OrdinalIgnoreCase).ToArray();
            if (ids.Length > 0) return ids;
        }
        catch { }
        return Enumerable.Range(1, 23).Select(i => "og" + i.ToString("D2")).ToArray();
    }
    private static readonly string AvatarStore = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "ProjectTilted", "profile-icons.json");
    private static readonly Dictionary<string, ImageSource?> AvatarCache = new();
    private Grid? _profileOverlay;
    private Border? _railAvatarFace;
    private string? _railAvatarFor;
    private string _railAvatarId = "";

    private static ImageSource? AvatarImage(string id)
    {
        if (AvatarCache.TryGetValue(id, out var cached)) return cached;
        ImageSource? image = null;
        try
        {
            using var stream = typeof(MainWindow).Assembly.GetManifestResourceStream("SyndicateClassic.Avatar." + id);
            if (stream != null)
            {
                var bitmap = new BitmapImage();
                bitmap.BeginInit(); bitmap.CacheOption = BitmapCacheOption.OnLoad; bitmap.DecodePixelWidth = 112; bitmap.StreamSource = stream; bitmap.EndInit(); bitmap.Freeze();
                image = bitmap;
            }
        }
        catch { }
        AvatarCache[id] = image;
        return image;
    }

    private static Dictionary<string, string> ReadAvatars()
    {
        try { return JsonSerializer.Deserialize<Dictionary<string, string>>(File.ReadAllText(AvatarStore)) ?? new(); }
        catch { return new(); }
    }

    private static void WriteAvatars(Dictionary<string, string> map)
    {
        try
        {
            Directory.CreateDirectory(Path.GetDirectoryName(AvatarStore)!);
            File.WriteAllText(AvatarStore + ".tmp", JsonSerializer.Serialize(map));
            File.Move(AvatarStore + ".tmp", AvatarStore, overwrite: true);
        }
        catch { }
    }

    // A player's icon. The first time, a random one is picked and then sticks.
    private static string AvatarFor(string? player)
    {
        if (string.IsNullOrWhiteSpace(player)) return "";
        string key = player.Trim().ToLowerInvariant();
        var map = ReadAvatars();
        if (map.TryGetValue(key, out var id) && AvatarIds.Contains(id)) return id;
        id = AvatarIds[Random.Shared.Next(AvatarIds.Length)];
        map[key] = id; WriteAvatars(map);
        return id;
    }

    private static void SaveAvatar(string player, string id)
    {
        var map = ReadAvatars(); map[player.Trim().ToLowerInvariant()] = id; WriteAvatars(map);
    }

    // Round icon: the picture if it is there, otherwise the first letter of the name.
    private static Border AvatarFace(string id, string name, double size)
    {
        var image = id.Length > 0 ? AvatarImage(id) : null;
        var face = new Border { Width = size, Height = size, CornerRadius = new CornerRadius(size / 2), Background = Hex(Night.Surface2) };
        if (image != null) face.Background = new ImageBrush(image) { Stretch = Stretch.UniformToFill };
        else
        {
            string initial = string.IsNullOrWhiteSpace(name) ? "?" : name.Trim().Substring(0, 1).ToUpperInvariant();
            face.Child = NightText(initial, size * .4, Night.Text, bold: true).Also(t => { t.HorizontalAlignment = HorizontalAlignment.Center; t.VerticalAlignment = VerticalAlignment.Center; t.TextWrapping = TextWrapping.NoWrap; });
        }
        return face;
    }

    // Keeps the rail avatar in step with the signed-in player (called every second).
    private void SyncRailAvatar()
    {
        if (_railAvatarFace == null) return;
        string? player = _account.SignedIn ? _account.Username : null;
        if (string.IsNullOrWhiteSpace(player)) player = null;
        if (player == _railAvatarFor) return;
        _railAvatarFor = player;
        _railAvatarId = player == null ? "" : AvatarFor(player);
        if (player == null) { _railAvatarFace.Background = Hex(Night.Surface2); _railAvatarFace.Child = GlyphIcon("", 16, Night.Text); return; }
        var image = AvatarImage(_railAvatarId);
        if (image != null) { _railAvatarFace.Child = null; _railAvatarFace.Background = new ImageBrush(image) { Stretch = Stretch.UniformToFill }; }
        else { _railAvatarFace.Background = Hex(Night.Surface2); _railAvatarFace.Child = NightText(player.Substring(0, 1).ToUpperInvariant(), 16, Night.Text, bold: true).Also(t => { t.HorizontalAlignment = HorizontalAlignment.Center; t.VerticalAlignment = VerticalAlignment.Center; }); }
    }

    private Grid BuildProfileOverlay()
    {
        var overlay = new Grid { Background = Hex("#B305060D"), Visibility = Visibility.Collapsed };
        overlay.MouseLeftButtonDown += (_, e) => { if (e.OriginalSource == overlay && !_authBusy) overlay.Visibility = Visibility.Collapsed; };
        overlay.PreviewKeyDown += (_, e) => { if (e.Key == Key.Escape && !_authBusy) { overlay.Visibility = Visibility.Collapsed; e.Handled = true; } };
        _profileOverlay = overlay;
        return overlay;
    }

    // Opens the profile card for the signed-in player (or the player picker when nobody is signed in).
    private void OpenProfile()
    {
        if (_profileOverlay == null || !_account.SignedIn || string.IsNullOrEmpty(_account.Username))
        {
            AccountButton_Click(AccountButton, new RoutedEventArgs());
            return;
        }
        string player = _account.Username;
        string chosen = AvatarFor(player);

        var body = new StackPanel();
        body.Children.Add(NightEyebrow("Profile"));
        body.Children.Add(Display("Your display name", 26, Night.Text).Also(t => t.Margin = new Thickness(0, 2, 0, 14)));
        var name = new TextBox { Text = player, MaxLength = 24, Height = 46 };
        NightField(name); name.BorderBrush = Hex(Night.Gold);
        body.Children.Add(name);
        var hint = NightText("3–24 characters: letters, numbers, spaces, _ or -.", 11.5, Night.Muted); hint.Margin = new Thickness(0, 8, 0, 16);
        body.Children.Add(hint);
        // Picking an icon saves it straight away (no SAVE needed for that) and updates the big preview, the
        // rail and what friends see. Each icon is a real button, so the keyboard and a controller work too.
        var preview = new Border { Width = 64, Height = 64 };
        void ShowPreview() { preview.Child = AvatarFace(chosen, player, 64); }
        var previewRow = new DockPanel { Margin = new Thickness(0, 0, 0, 10) };
        DockPanel.SetDock(preview, Dock.Left); previewRow.Children.Add(preview);
        var previewWords = new StackPanel { VerticalAlignment = VerticalAlignment.Center, Margin = new Thickness(14, 0, 0, 0) };
        previewWords.Children.Add(NightText("Your icon", 13.5, Night.Text, bold: true));
        var savedNote = NightText("Click one below. It's saved as soon as you pick it.", 12, Night.Sub).Also(t => t.TextWrapping = TextWrapping.Wrap);
        previewWords.Children.Add(savedNote);
        previewRow.Children.Add(previewWords);
        body.Children.Add(previewRow);
        ShowPreview();

        var grid = new WrapPanel { Margin = new Thickness(-4, 0, -4, 0) };
        var tiles = new Dictionary<string, Button>();
        void Mark()
        {
            foreach (var (id, tile) in tiles)
            {
                bool on = id == chosen;
                tile.BorderBrush = on ? (Brush)Hex(Night.Gold) : Hex("#33FFFFFF");
                tile.BorderThickness = new Thickness(on ? 3 : 1.5);
            }
        }
        foreach (var id in AvatarIds)
        {
            var tile = new Button { Width = 58, Height = 58, Margin = new Thickness(4), Padding = new Thickness(2), ToolTip = "Use this icon", Content = AvatarFace(id, player, 50) };
            RoundButton(tile, "#00000000", Night.Text, 29, "#33FFFFFF");
            System.Windows.Automation.AutomationProperties.SetName(tile, "Profile icon " + id);
            string pick = id;
            tile.Click += (_, __) =>
            {
                chosen = pick; Mark(); ShowPreview();
                SaveAvatar(player, pick);
                _railAvatarFor = null; SyncRailAvatar();
                savedNote.Text = "Saved. Friends see it the next time your status updates.";
            };
            tiles[id] = tile; grid.Children.Add(tile);
        }
        Mark();
        body.Children.Add(grid);

        var error = NightText("", 12, Night.Danger); error.Margin = new Thickness(0, 12, 0, 0); error.Visibility = Visibility.Collapsed;
        body.Children.Add(error);

        var buttons = new DockPanel { Margin = new Thickness(0, 22, 0, 0), LastChildFill = false };
        var switchPlayer = new Button { Content = "Switch player", Height = 40 };
        NightButton(switchPlayer, "ghost");
        switchPlayer.Click += (_, __) => { if (_authBusy) return; _profileOverlay.Visibility = Visibility.Collapsed; AccountButton_Click(AccountButton, new RoutedEventArgs()); };
        DockPanel.SetDock(switchPlayer, Dock.Left); buttons.Children.Add(switchPlayer);
        var save = new Button { Content = "SAVE", Width = 110, Height = 42, Margin = new Thickness(8, 0, 0, 0) };
        NightButton(save, "gold", 10); save.FontSize = 18;
        var cancel = new Button { Content = "Cancel", Height = 42 };
        NightButton(cancel, "ghost");
        cancel.Click += (_, __) => { if (!_authBusy) _profileOverlay.Visibility = Visibility.Collapsed; };
        DockPanel.SetDock(save, Dock.Right); DockPanel.SetDock(cancel, Dock.Right);
        buttons.Children.Add(save); buttons.Children.Add(cancel);
        body.Children.Add(buttons);

        async Task Submit()
        {
            if (_authBusy) return;
            string next = (name.Text ?? "").Trim();
            if (next.Length < 3 || next.Length > 24 || next.Any(c => !(char.IsLetterOrDigit(c) || c == ' ' || c == '_' || c == '-')))
            {
                error.Text = "Use 3–24 letters, numbers, spaces, _ or -."; error.Visibility = Visibility.Visible; return;
            }
            string saved = player;
            if (!string.Equals(next, player, StringComparison.Ordinal))
            {
                _authBusy = true; save.IsEnabled = cancel.IsEnabled = name.IsEnabled = false;
                error.Foreground = Hex(Night.Sub); error.Text = "Saving…"; error.Visibility = Visibility.Visible;
                try
                {
                    await WarmBackend();
                    saved = await _account.RenamePlayer(player, next);
                    AccountButton.Content = saved + "  ·  SWITCH";
                }
                catch (Exception failure)
                {
                    error.Foreground = Hex(Night.Danger); error.Text = failure.GetBaseException().Message; return;
                }
                finally { _authBusy = false; save.IsEnabled = cancel.IsEnabled = name.IsEnabled = true; UpdateButtons(); }
            }
            SaveAvatar(saved, chosen);
            _railAvatarFor = null; SyncRailAvatar();
            _profileOverlay.Visibility = Visibility.Collapsed;
            SetStatus("Profile saved: " + saved + ".");
        }
        save.Click += async (_, __) => await Submit();
        name.KeyDown += async (_, e) => { if (e.Key == Key.Enter) { e.Handled = true; await Submit(); } };

        var card = NightPanel(body);
        card.Width = 452; card.Background = Hex(Night.Surface2);
        card.HorizontalAlignment = HorizontalAlignment.Center; card.VerticalAlignment = VerticalAlignment.Center;
        card.Effect = new System.Windows.Media.Effects.DropShadowEffect { Color = Colors.Black, BlurRadius = 40, ShadowDepth = 10, Opacity = .55 };
        _profileOverlay.Children.Clear();
        _profileOverlay.Children.Add(card);
        _profileOverlay.Visibility = Visibility.Visible;
        name.Focus(); name.SelectAll();
    }
}