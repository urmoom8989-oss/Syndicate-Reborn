using System;
using System.Collections.Generic;
using System.Linq;
using System.Runtime.CompilerServices;
using System.Windows.Input;
using SyndicateLauncher.Models;
using SyndicateLauncher.Services;

namespace SyndicateLauncher.ViewModels;

/// <summary>Settings &gt; Display &gt; Crosshair (Services/CrosshairService.cs, drawn by SyndicateMenu.dll).</summary>
public sealed partial class MainViewModel
{
    public sealed record CrosshairPreset(string Name, Action<CrosshairSettings> Shape)
    {
        public override string ToString() => Name;
    }

    public sealed record CrosshairSwatch(string Name, string Hex);

    private const string CustomPresetName = "Custom";

    // A preset only sets the shape; colour, opacity, outline colour and the hide options stay the player's.
    public IReadOnlyList<CrosshairPreset> CrosshairPresets { get; } = new[]
    {
        new CrosshairPreset("Classic", c => Shape(c, lines: true, length: 6, thickness: 2, gap: 3)),
        new CrosshairPreset("Classic + dot", c => Shape(c, lines: true, length: 6, thickness: 2, gap: 4, dot: true, dotSize: 2)),
        new CrosshairPreset("Plus", c => Shape(c, lines: true, length: 5, thickness: 2, gap: 0)),
        new CrosshairPreset("Dot", c => Shape(c, lines: false, dot: true, dotSize: 4, dotRound: true)),
        new CrosshairPreset("Tiny dot", c => Shape(c, lines: false, dot: true, dotSize: 2)),
        new CrosshairPreset("Circle + dot", c => Shape(c, lines: false, dot: true, dotSize: 2, dotRound: true, circle: true, circleRadius: 12)),
        new CrosshairPreset("T shape", c => Shape(c, lines: true, length: 7, thickness: 2, gap: 3, tStyle: true)),
        new CrosshairPreset("X", c => Shape(c, lines: true, length: 6, thickness: 2, gap: 3, diagonal: true)),
        new CrosshairPreset("Long lines", c => Shape(c, lines: true, length: 14, thickness: 1, gap: 5, dot: true, dotSize: 1)),
        new CrosshairPreset("Circle cross", c => Shape(c, lines: true, length: 5, thickness: 2, gap: 6, circle: true, circleRadius: 16)),
        new CrosshairPreset(CustomPresetName, _ => { }),
    };

    public IReadOnlyList<CrosshairSwatch> CrosshairSwatches { get; } = new[]
    {
        new CrosshairSwatch("Green", "#00FF78"), new CrosshairSwatch("Cyan", "#00E5FF"),
        new CrosshairSwatch("Yellow", "#FFE600"), new CrosshairSwatch("Red", "#FF2D2D"),
        new CrosshairSwatch("Pink", "#FF2DF5"), new CrosshairSwatch("Purple", "#9B5CFF"),
        new CrosshairSwatch("Orange", "#FF8A00"), new CrosshairSwatch("White", "#FFFFFF"),
    };

    private static void Shape(CrosshairSettings c, bool lines, int length = 6, int thickness = 2, int gap = 3,
        bool tStyle = false, bool diagonal = false, bool dot = false, int dotSize = 2, bool dotRound = false,
        bool circle = false, int circleRadius = 10, int circleThickness = 1)
    {
        c.Lines = lines; c.Length = length; c.Thickness = thickness; c.Gap = gap; c.TStyle = tStyle; c.Diagonal = diagonal;
        c.Dot = dot; c.DotSize = dotSize; c.DotRound = dotRound;
        c.Circle = circle; c.CircleRadius = circleRadius; c.CircleThickness = circleThickness;
        c.Outline = true; c.OutlineThickness = 1;
    }

    private CrosshairSettings Cross => _settings.Crosshair;
    private bool _applyingCrosshairPreset;

    private void InitializeCrosshair()
    {
        _settings.Crosshair ??= new CrosshairSettings();
        Cross.Color = "#" + CrosshairService.NormalizeRgb(Cross.Color, "00FF78");
        Cross.OutlineColor = "#" + CrosshairService.NormalizeRgb(Cross.OutlineColor, "000000");
        _crosshairHex = Cross.Color;
        CrosshairService.Apply(Cross); // the game reads the file, so it always matches what the launcher shows
    }

    /// <summary>A fresh copy on every change, so the preview redraws.</summary>
    public CrosshairSettings CrosshairPreview => Cross.Copy();

    public sealed record CrosshairPresetTile(CrosshairPreset Preset, CrosshairSettings Sample, bool Selected);

    /// <summary>The preset buttons, each drawn in the player's own colour.</summary>
    public IReadOnlyList<CrosshairPresetTile> CrosshairPresetTiles => CrosshairPresets
        .Where(p => p.Name != CustomPresetName)
        .Select(p => { var sample = Cross.Copy(); p.Shape(sample); return new CrosshairPresetTile(p, sample, p.Name == Cross.Preset); })
        .ToList();

    private void CrosshairChanged(bool shape, [CallerMemberName] string? name = null)
    {
        OnPropertyChanged(name);
        if (shape && !_applyingCrosshairPreset && Cross.Preset != CustomPresetName)
        {
            Cross.Preset = CustomPresetName;
            OnPropertyChanged(nameof(SelectedCrosshairPreset));
        }
        OnPropertyChanged(nameof(CrosshairPreview));
        OnPropertyChanged(nameof(CrosshairPresetTiles));
        OnPropertyChanged(nameof(CrosshairSummary));
        CrosshairService.Apply(Cross);
        Save();
    }

    public bool CrosshairEnabled
    {
        get => Cross.Enabled;
        set { if (Cross.Enabled == value) return; Cross.Enabled = value; CrosshairChanged(false); }
    }

    public CrosshairPreset? SelectedCrosshairPreset
    {
        get => CrosshairPresets.FirstOrDefault(p => p.Name == Cross.Preset) ?? CrosshairPresets[^1];
        set
        {
            if (value is null || value.Name == Cross.Preset) return;
            Cross.Preset = value.Name;
            _applyingCrosshairPreset = true;
            try
            {
                value.Shape(Cross);
                foreach (var p in new[] { nameof(CrosshairLines), nameof(CrosshairTStyle), nameof(CrosshairDiagonal), nameof(CrosshairLength),
                             nameof(CrosshairThickness), nameof(CrosshairGap), nameof(CrosshairDot), nameof(CrosshairDotSize), nameof(CrosshairDotRound),
                             nameof(CrosshairCircle), nameof(CrosshairCircleRadius), nameof(CrosshairCircleThickness), nameof(CrosshairOutline),
                             nameof(CrosshairOutlineThickness) })
                    OnPropertyChanged(p);
                CrosshairChanged(false);
            }
            finally { _applyingCrosshairPreset = false; }
        }
    }

    public ICommand CrosshairPresetCommand => _crosshairPresetCommand ??= new RelayCommand(p =>
    {
        if (p is CrosshairPreset preset) SelectedCrosshairPreset = preset;
    });
    private ICommand? _crosshairPresetCommand;

    public bool CrosshairLines { get => Cross.Lines; set { if (Cross.Lines == value) return; Cross.Lines = value; CrosshairChanged(true); } }
    public bool CrosshairTStyle { get => Cross.TStyle; set { if (Cross.TStyle == value) return; Cross.TStyle = value; if (value && Cross.Diagonal) { Cross.Diagonal = false; OnPropertyChanged(nameof(CrosshairDiagonal)); } CrosshairChanged(true); } }
    public bool CrosshairDiagonal { get => Cross.Diagonal; set { if (Cross.Diagonal == value) return; Cross.Diagonal = value; if (value && Cross.TStyle) { Cross.TStyle = false; OnPropertyChanged(nameof(CrosshairTStyle)); } CrosshairChanged(true); } }
    public double CrosshairLength { get => Cross.Length; set => SetCrossInt(v => Cross.Length = v, Cross.Length, value, 1, 40); }
    public double CrosshairThickness { get => Cross.Thickness; set => SetCrossInt(v => Cross.Thickness = v, Cross.Thickness, value, 1, 10); }
    public double CrosshairGap { get => Cross.Gap; set => SetCrossInt(v => Cross.Gap = v, Cross.Gap, value, 0, 30); }
    public bool CrosshairDot { get => Cross.Dot; set { if (Cross.Dot == value) return; Cross.Dot = value; CrosshairChanged(true); } }
    public double CrosshairDotSize { get => Cross.DotSize; set => SetCrossInt(v => Cross.DotSize = v, Cross.DotSize, value, 1, 10); }
    public bool CrosshairDotRound { get => Cross.DotRound; set { if (Cross.DotRound == value) return; Cross.DotRound = value; CrosshairChanged(true); } }
    public bool CrosshairCircle { get => Cross.Circle; set { if (Cross.Circle == value) return; Cross.Circle = value; CrosshairChanged(true); } }
    public double CrosshairCircleRadius { get => Cross.CircleRadius; set => SetCrossInt(v => Cross.CircleRadius = v, Cross.CircleRadius, value, 3, 60); }
    public double CrosshairCircleThickness { get => Cross.CircleThickness; set => SetCrossInt(v => Cross.CircleThickness = v, Cross.CircleThickness, value, 1, 6); }
    public bool CrosshairOutline { get => Cross.Outline; set { if (Cross.Outline == value) return; Cross.Outline = value; CrosshairChanged(true); } }
    public double CrosshairOutlineThickness { get => Cross.OutlineThickness; set => SetCrossInt(v => Cross.OutlineThickness = v, Cross.OutlineThickness, value, 1, 3); }
    public double CrosshairOpacity { get => Cross.Opacity; set => SetCrossInt(v => Cross.Opacity = v, Cross.Opacity, value, 10, 100, false); }
    public double CrosshairOutlineOpacity { get => Cross.OutlineOpacity; set => SetCrossInt(v => Cross.OutlineOpacity = v, Cross.OutlineOpacity, value, 0, 100, false); }
    public bool CrosshairHideInMenus { get => Cross.HideInMenus; set { if (Cross.HideInMenus == value) return; Cross.HideInMenus = value; CrosshairChanged(false); } }
    public bool CrosshairHideWhenAiming { get => Cross.HideWhenAiming; set { if (Cross.HideWhenAiming == value) return; Cross.HideWhenAiming = value; CrosshairChanged(false); } }

    private void SetCrossInt(Action<int> set, int current, double value, int min, int max, bool shape = true, [CallerMemberName] string? name = null)
    {
        int v = Math.Clamp((int)Math.Round(value), min, max);
        if (v == current) return;
        set(v);
        CrosshairChanged(shape, name);
    }

    private string _crosshairHex = "#00FF78";

    /// <summary>The colour box: typing a valid hex colour applies it.</summary>
    public string CrosshairHex
    {
        get => _crosshairHex;
        set
        {
            if (!Set(ref _crosshairHex, value ?? "")) return;
            string rgb = CrosshairService.NormalizeRgb(value, "");
            if (rgb.Length == 0 || "#" + rgb == Cross.Color) return;
            Cross.Color = "#" + rgb;
            CrosshairChanged(false, nameof(CrosshairColor));
        }
    }

    public string CrosshairColor => Cross.Color;

    public ICommand CrosshairColorCommand => _crosshairColorCommand ??= new RelayCommand(p =>
    {
        if (p is not string hex) return;
        Cross.Color = "#" + CrosshairService.NormalizeRgb(hex, "00FF78");
        _crosshairHex = Cross.Color;
        OnPropertyChanged(nameof(CrosshairHex));
        CrosshairChanged(false, nameof(CrosshairColor));
    });
    private ICommand? _crosshairColorCommand;

    public bool CrosshairBlackOutline
    {
        get => Cross.OutlineColor == "#000000";
        set
        {
            Cross.OutlineColor = value ? "#000000" : "#FFFFFF";
            CrosshairChanged(false);
            OnPropertyChanged(nameof(CrosshairWhiteOutline));
        }
    }
    public bool CrosshairWhiteOutline { get => !CrosshairBlackOutline; set => CrosshairBlackOutline = !value; }

    public ICommand ResetCrosshairCommand => _resetCrosshairCommand ??= new RelayCommand(() =>
    {
        bool on = Cross.Enabled;
        _settings.Crosshair = new CrosshairSettings { Enabled = on };
        _crosshairHex = Cross.Color;
        foreach (var p in typeof(MainViewModel).GetProperties().Where(p => p.Name.StartsWith("Crosshair", StringComparison.Ordinal)))
            OnPropertyChanged(p.Name);
        CrosshairChanged(false, nameof(CrosshairEnabled));
    });
    private ICommand? _resetCrosshairCommand;

    public string CrosshairSummary => !Cross.Enabled
        ? "Off. Turn it on to see it in your next match."
        : EnableMenu
            ? "On. Drawn by the Syndicate menu; changes show in game right away."
            : "On. Changes show in game right away, even mid-match. The Syndicate menu stays off; only the crosshair loads.";
}
