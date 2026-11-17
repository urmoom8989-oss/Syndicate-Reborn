using System;
using System.Collections.Generic;
using System.Collections.ObjectModel;
using System.Linq;
using System.Windows.Input;
using System.Windows.Media;
using System.Windows.Threading;
using SyndicateLauncher.Models;
using SyndicateLauncher.Services;

namespace SyndicateLauncher.ViewModels;

/// <summary>One theme card on the Themes tab.</summary>
public sealed class ThemeOption : ViewModelBase
{
    private bool _isSelected;
    private ThemePalette _palette;

    public ThemeOption(ThemePalette palette) => _palette = palette;

    public ThemePalette Palette
    {
        get => _palette;
        set
        {
            _palette = value;
            OnPropertyChanged(string.Empty);
        }
    }

    public string Key => Palette.Key;
    public string Name => Palette.Name;
    public string Description => Palette.Description;
    public bool IsCustom => Palette.IsCustom;
    public string CustomId => IsCustom ? Key["custom:".Length..] : "";

    public bool IsSelected
    {
        get => _isSelected;
        set => Set(ref _isSelected, value);
    }

    public Brush BackgroundBrush => Freeze(new SolidColorBrush(Palette.Background));
    public Brush SurfaceBrush => Freeze(new SolidColorBrush(Palette.Surface));
    public Brush AccentBrush => Freeze(new SolidColorBrush(Palette.Accent));
    public Brush Accent2Brush => Freeze(new SolidColorBrush(Palette.Accent2));
    public Brush TextBrush => Freeze(new SolidColorBrush(Palette.Text));
    public Brush MutedBrush => Freeze(new SolidColorBrush(ThemeColor.Mix(Palette.Text, Palette.Background, 0.45)));
    public Brush OnAccentBrush => Freeze(new SolidColorBrush(ThemeService.OnAccent(Palette)));
    public Color AccentColor => Palette.Accent;

    public Brush LaunchBrush => Freeze(new LinearGradientBrush(Palette.LaunchStart, Palette.LaunchEnd, 90));

    public Brush SidebarBrush => Freeze(new LinearGradientBrush(
        ThemeService.Tint(Palette, 0x15, 0xFF), ThemeService.Tint(Palette, 0x0C, 0xFF), 90));

    /// <summary>The card's background: the theme's base with its accent glowing in from a corner.</summary>
    public Brush PreviewBrush
    {
        get
        {
            var g = new RadialGradientBrush
            {
                Center = new System.Windows.Point(0.85, 0.1),
                GradientOrigin = new System.Windows.Point(0.85, 0.1),
                RadiusX = 0.95,
                RadiusY = 1.1
            };
            g.GradientStops.Add(new GradientStop(ThemeColor.Mix(Palette.Background, Palette.Accent, 0.28), 0));
            g.GradientStops.Add(new GradientStop(ThemeColor.Mix(Palette.Background, Palette.Accent2, 0.08), 0.55));
            g.GradientStops.Add(new GradientStop(Palette.Background, 1));
            return Freeze(g);
        }
    }

    public Brush StripeBrush => Freeze(new LinearGradientBrush(Palette.Accent, Palette.Accent2, 0));

    // Fortnite Lobby Themes: the card shows the lobby art (a small pre-made thumbnail, decoded once).
    public bool HasWallpaper => Palette.HasWallpaper;
    private ImageSource? _thumbnail;
    public ImageSource? Thumbnail => _thumbnail ??= LoadThumbnail(Palette.Wallpaper);

    /// <summary>Season label for the lobby card ("SEASON 8" from "Season 8: Pirate Cove"), empty when there is none.</summary>
    public string Badge
    {
        get
        {
            int colon = Name.IndexOf(':');
            return colon > 0 ? Name[..colon].ToUpperInvariant() : "LOBBY";
        }
    }

    /// <summary>The name without its season prefix, for the big card title.</summary>
    public string Title
    {
        get
        {
            int colon = Name.IndexOf(':');
            return (colon > 0 ? Name[(colon + 1)..].Trim() : Name).ToUpperInvariant();
        }
    }

    private static ImageSource? LoadThumbnail(string? wallpaper)
    {
        if (string.IsNullOrEmpty(wallpaper)) return null;
        try
        {
            var bmp = new System.Windows.Media.Imaging.BitmapImage();
            bmp.BeginInit();
            bmp.UriSource = new Uri($"pack://application:,,,/SyndicateLauncher;component/Assets/Lobby/{wallpaper}-thumb.jpg", UriKind.Absolute);
            bmp.CacheOption = System.Windows.Media.Imaging.BitmapCacheOption.OnLoad;
            bmp.EndInit();
            bmp.Freeze();
            return bmp;
        }
        catch
        {
            return null;
        }
    }

    private static Brush Freeze(Brush b)
    {
        b.Freeze();
        return b;
    }
}

/// <summary>A chip in a row of choices (particle kinds, RGB colourways).</summary>
public sealed class ThemeChoice : ViewModelBase
{
    private bool _isSelected;
    public ThemeChoice(string key, string name) { Key = key; Name = name; }
    public string Key { get; }
    public string Name { get; }
    public bool IsSelected { get => _isSelected; set => Set(ref _isSelected, value); }
}

/// <summary>A quick colour swatch in the theme editor.</summary>
public sealed record ThemeSwatch(string Slot, string Hex)
{
    public Brush Brush { get; } = MakeBrush(Hex);
    public string Parameter => Slot + "|" + Hex;
    private static Brush MakeBrush(string hex)
    {
        var b = new SolidColorBrush(ThemeColor.Parse(hex));
        b.Freeze();
        return b;
    }
}

/// <summary>
/// The Themes tab: preset and custom themes, the theme editor and the live effects. Every change is written to
/// AppSettings.Theme (settings.json) through the save callback, so it is all back the next time the launcher opens.
/// </summary>
public sealed class ThemesViewModel : ViewModelBase
{
    private readonly ThemeSettings _s;
    private readonly Action _save;
    private readonly DispatcherTimer _saveTimer;
    private bool _isEditorOpen;
    private string? _editingId;
    private string _editorName = "";
    private string _editorAccent = "", _editorAccent2 = "", _editorBackground = "", _editorSurface = "", _editorText = "";
    private string _editorError = "";
    private bool _syncingHue;

    public ThemesViewModel(ThemeSettings settings, Action save)
    {
        _s = settings;
        _save = save;
        _saveTimer = new DispatcherTimer(DispatcherPriority.Background) { Interval = TimeSpan.FromMilliseconds(350) };
        _saveTimer.Tick += (_, _) => FlushSave();

        foreach (var p in ThemeCatalog.Presets)
            (p.Group switch { "Signature" => SignatureThemes, "Lobby" => LobbyThemes, _ => ColourThemes }).Add(new ThemeOption(p));
        RebuildCustom();

        foreach (var k in ThemeCatalog.ParticleKinds) ParticleChoices.Add(new ThemeChoice(k.Key, k.Name));
        foreach (var r in ThemeCatalog.RgbStyles) RgbChoices.Add(new ThemeChoice(r.Key, r.Name));
        foreach (var hex in ThemeCatalog.AccentSwatches)
        {
            AccentSwatches.Add(new ThemeSwatch("accent", hex));
            Accent2Swatches.Add(new ThemeSwatch("accent2", hex));
        }
        foreach (var hex in ThemeCatalog.BackgroundSwatches) BackgroundSwatches.Add(new ThemeSwatch("background", hex));
        SyncChoices();
        SyncSelection();

        SelectThemeCommand = new RelayCommand(p => { if (p is ThemeOption o) Select(o.Key); });
        NewThemeCommand = new RelayCommand(() => OpenEditor(null));
        EditThemeCommand = new RelayCommand(p => { if (p is ThemeOption { IsCustom: true } o) OpenEditor(o.CustomId); });
        DuplicateThemeCommand = new RelayCommand(p => { if (p is ThemeOption o) OpenEditor(null, o.Palette); });
        DeleteThemeCommand = new RelayCommand(p => { if (p is ThemeOption { IsCustom: true } o) Delete(o.CustomId); });
        SaveEditorCommand = new RelayCommand(SaveEditor);
        CancelEditorCommand = new RelayCommand(CancelEditor);
        ShuffleEditorCommand = new RelayCommand(Shuffle);
        SwatchCommand = new RelayCommand(p => ApplySwatch(p as string));
        SelectParticlesCommand = new RelayCommand(p => { if (p is string k) Particles = k; });
        SelectRgbStyleCommand = new RelayCommand(p => { if (p is string k) RgbStyle = k; });
        ResetEffectsCommand = new RelayCommand(ResetEffects);
    }

    public ObservableCollection<ThemeOption> SignatureThemes { get; } = new();
    public ObservableCollection<ThemeOption> ColourThemes { get; } = new();
    public ObservableCollection<ThemeOption> LobbyThemes { get; } = new();
    public ObservableCollection<ThemeOption> CustomThemes { get; } = new();
    public ObservableCollection<ThemeChoice> ParticleChoices { get; } = new();
    public ObservableCollection<ThemeChoice> RgbChoices { get; } = new();
    public ObservableCollection<ThemeSwatch> AccentSwatches { get; } = new();
    public ObservableCollection<ThemeSwatch> Accent2Swatches { get; } = new();
    public ObservableCollection<ThemeSwatch> BackgroundSwatches { get; } = new();

    public ICommand SelectThemeCommand { get; }
    public ICommand NewThemeCommand { get; }
    public ICommand EditThemeCommand { get; }
    public ICommand DuplicateThemeCommand { get; }
    public ICommand DeleteThemeCommand { get; }
    public ICommand SaveEditorCommand { get; }
    public ICommand CancelEditorCommand { get; }
    public ICommand ShuffleEditorCommand { get; }
    public ICommand SwatchCommand { get; }
    public ICommand SelectParticlesCommand { get; }
    public ICommand SelectRgbStyleCommand { get; }
    public ICommand ResetEffectsCommand { get; }

    public bool HasCustomThemes => CustomThemes.Count > 0;
    public int ThemeCount => SignatureThemes.Count + ColourThemes.Count + LobbyThemes.Count + CustomThemes.Count;

    public string CurrentThemeName => ThemeCatalog.Current(_s).Name;
    public string CurrentThemeDescription => ThemeCatalog.Current(_s).Description;

    // ------------------------------------------------------------------ effects

    public string Particles
    {
        get => _s.Particles;
        set
        {
            if (value == _s.Particles) return;
            _s.Particles = value;
            OnPropertyChanged();
            OnPropertyChanged(nameof(HasParticles));
            SyncChoices();
            EffectsChanged();
        }
    }

    public bool HasParticles => _s.Particles != "None";

    public double ParticleAmount
    {
        get => _s.ParticleAmount;
        set { if (SetSetting(_s.ParticleAmount, Math.Clamp(value, 0.1, 1), v => _s.ParticleAmount = v)) OnPropertyChanged(nameof(ParticleAmountText)); }
    }

    public string ParticleAmountText => $"{Math.Round(_s.ParticleAmount * 100)}%";

    public bool RgbEdge
    {
        get => _s.RgbEdge;
        set => SetSetting(_s.RgbEdge, value, v => _s.RgbEdge = v);
    }

    public string RgbStyle
    {
        get => _s.RgbStyle;
        set
        {
            if (value == _s.RgbStyle) return;
            _s.RgbStyle = value;
            OnPropertyChanged();
            SyncChoices();
            EffectsChanged();
        }
    }

    public double RgbSpeed
    {
        get => _s.RgbSpeed;
        set { if (SetSetting(_s.RgbSpeed, Math.Clamp(value, 0.25, 3), v => _s.RgbSpeed = v)) OnPropertyChanged(nameof(RgbSpeedText)); }
    }

    public string RgbSpeedText => $"{_s.RgbSpeed:0.0#}x";

    public bool AmbientGlow
    {
        get => _s.AmbientGlow;
        set => SetSetting(_s.AmbientGlow, value, v => _s.AmbientGlow = v);
    }

    public double GlowStrength
    {
        get => _s.GlowStrength;
        set { if (SetSetting(_s.GlowStrength, Math.Clamp(value, 0, 1), v => _s.GlowStrength = v)) OnPropertyChanged(nameof(GlowStrengthText)); }
    }

    public string GlowStrengthText => $"{Math.Round(_s.GlowStrength * 100)}%";

    public bool SmoothTransitions
    {
        get => _s.SmoothTransitions;
        set => SetSetting(_s.SmoothTransitions, value, v => _s.SmoothTransitions = v);
    }

    public bool ReduceMotion
    {
        get => _s.ReduceMotion;
        set => SetSetting(_s.ReduceMotion, value, v => _s.ReduceMotion = v);
    }

    public bool PauseInGame
    {
        get => _s.PauseInGame;
        set => SetSetting(_s.PauseInGame, value, v => _s.PauseInGame = v);
    }

    // Fortnite Lobby Themes: camera drift and the theme's own weather, and how dark the wallpaper is under the pages.
    public bool LobbyMotion
    {
        get => _s.LobbyMotion;
        set => SetSetting(_s.LobbyMotion, value, v => _s.LobbyMotion = v);
    }

    public double WallpaperDim
    {
        get => _s.WallpaperDim;
        set { if (SetSetting(_s.WallpaperDim, Math.Clamp(value, 0.15, 0.85), v => _s.WallpaperDim = v)) OnPropertyChanged(nameof(WallpaperDimText)); }
    }

    public string WallpaperDimText => $"{Math.Round(_s.WallpaperDim * 100)}%";

    /// <summary>The lobby options only show while a Fortnite Lobby Theme is picked.</summary>
    public bool IsLobbyTheme => ThemeCatalog.Current(_s).HasWallpaper;

    // OG Fortnite launcher: MainWindow.xaml swaps the sidebar for a top tab bar and shows the classic home.
    public bool OgLauncher
    {
        get => _s.OgLauncher;
        set
        {
            if (_s.OgLauncher == value) return;
            _s.OgLauncher = value;
            OnPropertyChanged();
            QueueSave();
        }
    }

    private bool SetSetting<T>(T current, T value, Action<T> assign, [System.Runtime.CompilerServices.CallerMemberName] string? name = null)
    {
        if (EqualityComparer<T>.Default.Equals(current, value)) return false;
        assign(value);
        OnPropertyChanged(name);
        EffectsChanged();
        return true;
    }

    private void EffectsChanged()
    {
        ThemeService.NotifyEffectsChanged();
        QueueSave();
    }

    private void ResetEffects()
    {
        var d = new ThemeSettings();
        _s.Particles = d.Particles;
        _s.ParticleAmount = d.ParticleAmount;
        _s.RgbEdge = d.RgbEdge;
        _s.RgbStyle = d.RgbStyle;
        _s.RgbSpeed = d.RgbSpeed;
        _s.AmbientGlow = d.AmbientGlow;
        _s.GlowStrength = d.GlowStrength;
        _s.SmoothTransitions = d.SmoothTransitions;
        _s.ReduceMotion = d.ReduceMotion;
        _s.PauseInGame = d.PauseInGame;
        _s.LobbyMotion = d.LobbyMotion;
        _s.WallpaperDim = d.WallpaperDim;
        OnPropertyChanged(string.Empty);
        SyncChoices();
        EffectsChanged();
    }

    private void SyncChoices()
    {
        foreach (var c in ParticleChoices) c.IsSelected = c.Key == _s.Particles;
        foreach (var c in RgbChoices) c.IsSelected = c.Key == _s.RgbStyle;
    }

    // ------------------------------------------------------------------ picking themes

    private void Select(string key)
    {
        if (ThemeCatalog.Resolve(_s, key) is null) return;
        _s.Theme = key;
        SyncSelection();
        ThemeService.Apply();
        OnPropertyChanged(nameof(CurrentThemeName));
        OnPropertyChanged(nameof(CurrentThemeDescription));
        OnPropertyChanged(nameof(IsLobbyTheme));
        QueueSave();
    }

    private void SyncSelection()
    {
        foreach (var o in SignatureThemes.Concat(ColourThemes).Concat(LobbyThemes).Concat(CustomThemes)) o.IsSelected = o.Key == _s.Theme;
    }

    private void RebuildCustom()
    {
        CustomThemes.Clear();
        foreach (var t in _s.CustomThemes) CustomThemes.Add(new ThemeOption(t.ToPalette()));
        SyncSelection();
        OnPropertyChanged(nameof(HasCustomThemes));
        OnPropertyChanged(nameof(ThemeCount));
    }

    private void Delete(string id)
    {
        _s.CustomThemes.RemoveAll(t => t.Id == id);
        if (_s.Theme == "custom:" + id) _s.Theme = ThemeCatalog.DefaultKey;
        RebuildCustom();
        ThemeService.Apply();
        OnPropertyChanged(nameof(CurrentThemeName));
        OnPropertyChanged(nameof(CurrentThemeDescription));
        OnPropertyChanged(nameof(IsLobbyTheme));
        QueueSave();
    }

    // ------------------------------------------------------------------ editor

    public bool IsEditorOpen
    {
        get => _isEditorOpen;
        private set => Set(ref _isEditorOpen, value);
    }

    public string EditorTitle => _editingId is null ? "NEW THEME" : "EDIT THEME";

    public string EditorName
    {
        get => _editorName;
        set { if (Set(ref _editorName, value ?? "")) EditorError = ""; }
    }

    public string EditorAccent { get => _editorAccent; set => SetEditorColour(ref _editorAccent, value); }
    public string EditorAccent2 { get => _editorAccent2; set => SetEditorColour(ref _editorAccent2, value); }
    public string EditorBackground { get => _editorBackground; set => SetEditorColour(ref _editorBackground, value); }
    public string EditorSurface { get => _editorSurface; set => SetEditorColour(ref _editorSurface, value); }
    public string EditorText { get => _editorText; set => SetEditorColour(ref _editorText, value); }

    public string EditorError
    {
        get => _editorError;
        private set => Set(ref _editorError, value);
    }

    /// <summary>Hue wheel for the accent (0-360), keeps its brightness.</summary>
    public double EditorAccentHue
    {
        get => ThemeColor.ToHsv(ThemeColor.Parse(_editorAccent)).H;
        set => SetHue(value, accent2: false);
    }

    public double EditorAccent2Hue
    {
        get => ThemeColor.ToHsv(ThemeColor.Parse(_editorAccent2)).H;
        set => SetHue(value, accent2: true);
    }

    /// <summary>How much of the accent shows in the background (0 = neutral, 1 = strongly tinted).</summary>
    public double EditorTint
    {
        get
        {
            var (_, s, _) = ThemeColor.ToHsv(ThemeColor.Parse(_editorBackground));
            return Math.Clamp(s, 0, 1);
        }
        set
        {
            var accent = ThemeColor.Parse(_editorAccent);
            var (h, _, _) = ThemeColor.ToHsv(accent);
            double t = Math.Clamp(value, 0, 1);
            var bgV = ThemeColor.ToHsv(ThemeColor.Parse(_editorBackground)).V;
            var sfV = ThemeColor.ToHsv(ThemeColor.Parse(_editorSurface)).V;
            _syncingHue = true;
            EditorBackground = ThemeColor.ToHex(ThemeColor.FromHsv(h, t, Math.Clamp(bgV, 0, 0.16)));
            EditorSurface = ThemeColor.ToHex(ThemeColor.FromHsv(h, t * 0.9, Math.Clamp(Math.Max(sfV, bgV + 0.05), 0.05, 0.28)));
            _syncingHue = false;
            OnPropertyChanged();
            PreviewEditor();
        }
    }

    public Brush EditorAccentBrush => SwatchBrush(_editorAccent);
    public Brush EditorAccent2Brush => SwatchBrush(_editorAccent2);
    public Brush EditorBackgroundBrush => SwatchBrush(_editorBackground);
    public Brush EditorSurfaceBrush => SwatchBrush(_editorSurface);
    public Brush EditorTextBrush => SwatchBrush(_editorText);

    private static Brush SwatchBrush(string hex)
    {
        var b = new SolidColorBrush(ThemeColor.Parse(hex, Colors.Transparent));
        b.Freeze();
        return b;
    }

    private void SetHue(double hue, bool accent2)
    {
        var c = ThemeColor.Parse(accent2 ? _editorAccent2 : _editorAccent);
        var (_, s, v) = ThemeColor.ToHsv(c);
        if (s < 0.2) s = 0.72;
        if (v < 0.5) v = 0.95;
        string hex = ThemeColor.ToHex(ThemeColor.FromHsv(hue, s, v));
        if (accent2) EditorAccent2 = hex; else EditorAccent = hex;
    }

    private void SetEditorColour(ref string field, string? value, [System.Runtime.CompilerServices.CallerMemberName] string? name = null)
    {
        value = (value ?? "").Trim();
        if (value.Length > 0 && value[0] != '#') value = "#" + value;
        if (!Set(ref field, value, name)) return;
        OnPropertyChanged(name + "Brush");
        if (!_syncingHue)
        {
            OnPropertyChanged(nameof(EditorAccentHue));
            OnPropertyChanged(nameof(EditorAccent2Hue));
            OnPropertyChanged(nameof(EditorTint));
        }
        PreviewEditor();
    }

    private ThemePalette? EditorPalette()
    {
        bool ok = ThemeColor.TryParse(_editorAccent, out var a) & ThemeColor.TryParse(_editorAccent2, out var a2) &
                  ThemeColor.TryParse(_editorBackground, out var bg) & ThemeColor.TryParse(_editorSurface, out var sf) &
                  ThemeColor.TryParse(_editorText, out var tx);
        if (!ok) return null;
        string name = string.IsNullOrWhiteSpace(_editorName) ? "My theme" : _editorName.Trim();
        return new ThemePalette("custom:" + (_editingId ?? "preview"), name, "Your themes", "Made by you.",
            Opaque(a), Opaque(a2), Opaque(bg), Opaque(sf), Opaque(tx));
    }

    private static Color Opaque(Color c) => ThemeColor.WithAlpha(c, 0xFF);

    private void PreviewEditor()
    {
        if (!IsEditorOpen) return;
        var p = EditorPalette();
        if (p is null)
        {
            EditorError = "Colours are hex codes, like #3BD9FF.";
            return;
        }
        EditorError = "";
        ThemeService.Preview(p);
        OnPropertyChanged(nameof(EditorPreview));
    }

    /// <summary>The editor's live card.</summary>
    public ThemeOption? EditorPreview => EditorPalette() is { } p ? new ThemeOption(p) : null;

    private void OpenEditor(string? id, ThemePalette? from = null)
    {
        _editingId = id;
        var existing = id is null ? null : _s.CustomThemes.FirstOrDefault(t => t.Id == id);
        var source = existing?.ToPalette() ?? from ?? ThemeCatalog.Current(_s);
        _syncingHue = true;
        _editorName = existing?.Name ?? (from is not null ? from.Name + " remix" : "My theme " + (_s.CustomThemes.Count + 1));
        _editorAccent = ThemeColor.ToHex(source.Accent);
        _editorAccent2 = ThemeColor.ToHex(source.Accent2);
        _editorBackground = ThemeColor.ToHex(source.Background);
        _editorSurface = ThemeColor.ToHex(source.Surface);
        _editorText = ThemeColor.ToHex(source.Text);
        _syncingHue = false;
        EditorError = "";
        IsEditorOpen = true;
        OnPropertyChanged(string.Empty);
        PreviewEditor();
    }

    private void SaveEditor()
    {
        var p = EditorPalette();
        if (p is null) { EditorError = "Fix the colours first: each one is a hex code, like #3BD9FF."; return; }
        string name = string.IsNullOrWhiteSpace(_editorName) ? "My theme" : _editorName.Trim();
        var theme = _editingId is null ? null : _s.CustomThemes.FirstOrDefault(t => t.Id == _editingId);
        if (theme is null)
        {
            theme = new CustomTheme();
            _s.CustomThemes.Add(theme);
        }
        theme.Name = name;
        theme.Accent = ThemeColor.ToHex(p.Accent);
        theme.Accent2 = ThemeColor.ToHex(p.Accent2);
        theme.Background = ThemeColor.ToHex(p.Background);
        theme.Surface = ThemeColor.ToHex(p.Surface);
        theme.Text = ThemeColor.ToHex(p.Text);
        theme.Normalize();
        _s.Theme = "custom:" + theme.Id;
        IsEditorOpen = false;
        _editingId = null;
        RebuildCustom();
        ThemeService.Apply(animate: false);
        OnPropertyChanged(nameof(CurrentThemeName));
        OnPropertyChanged(nameof(CurrentThemeDescription));
        OnPropertyChanged(nameof(IsLobbyTheme));
        QueueSave();
        FlushSave();
    }

    private void CancelEditor()
    {
        IsEditorOpen = false;
        _editingId = null;
        ThemeService.Apply();
    }

    private void ApplySwatch(string? parameter)
    {
        if (parameter is null) return;
        var parts = parameter.Split('|');
        if (parts.Length != 2) return;
        string hex = parts[1];
        switch (parts[0])
        {
            case "accent": EditorAccent = hex; break;
            case "accent2": EditorAccent2 = hex; break;
            case "background":
                var bg = ThemeColor.Parse(hex);
                _syncingHue = true;
                EditorBackground = hex;
                // Keep the surface a step above the new background, in the same hue.
                EditorSurface = ThemeColor.ToHex(ThemeColor.Mix(bg, Colors.White, 0.07));
                _syncingHue = false;
                OnPropertyChanged(nameof(EditorTint));
                PreviewEditor();
                break;
        }
    }

    private readonly Random _rng = new();

    /// <summary>Rolls a random, good-looking theme: a bright accent, a neighbouring second accent and a dark tinted base.</summary>
    private void Shuffle()
    {
        double hue = _rng.NextDouble() * 360;
        double hue2 = hue + (_rng.Next(2) == 0 ? 1 : -1) * (30 + _rng.NextDouble() * 50);
        double tint = 0.35 + _rng.NextDouble() * 0.4;
        _syncingHue = true;
        EditorAccent = ThemeColor.ToHex(ThemeColor.FromHsv(hue, 0.55 + _rng.NextDouble() * 0.35, 0.95));
        EditorAccent2 = ThemeColor.ToHex(ThemeColor.FromHsv(hue2, 0.6 + _rng.NextDouble() * 0.3, 0.9));
        EditorBackground = ThemeColor.ToHex(ThemeColor.FromHsv(hue, tint, 0.06 + _rng.NextDouble() * 0.04));
        EditorSurface = ThemeColor.ToHex(ThemeColor.FromHsv(hue, tint * 0.85, 0.15 + _rng.NextDouble() * 0.05));
        EditorText = "#F4F5F6";
        _syncingHue = false;
        OnPropertyChanged(nameof(EditorAccentHue));
        OnPropertyChanged(nameof(EditorAccent2Hue));
        OnPropertyChanged(nameof(EditorTint));
        PreviewEditor();
    }

    // ------------------------------------------------------------------ saving

    private void QueueSave()
    {
        _saveTimer.Stop();
        _saveTimer.Start();
    }

    /// <summary>Writes settings.json now (also called when the launcher closes).</summary>
    public void FlushSave()
    {
        _saveTimer.Stop();
        _save();
    }
}
