using System;
using System.Collections.Generic;
using System.IO;
using System.Windows.Input;
using SyndicateLauncher.Services;

namespace SyndicateLauncher.ViewModels;

/// <summary>Settings &gt; Display &gt; Stretched resolution (Services/StretchedResolution.cs).</summary>
public sealed partial class MainViewModel
{
    public sealed record StretchPreset(string Label, int Width, int Height)
    {
        public override string ToString() => Label;
    }

    public IReadOnlyList<StretchPreset> StretchPresets { get; } = new[]
    {
        new StretchPreset("4:3  -  1440 x 1080", 1440, 1080),
        new StretchPreset("4:3  -  1280 x 960", 1280, 960),
        new StretchPreset("4:3  -  1920 x 1440", 1920, 1440),
        new StretchPreset("5:4  -  1280 x 1024", 1280, 1024),
        new StretchPreset("16:10  -  1680 x 1050", 1680, 1050),
        new StretchPreset("16:10  -  1728 x 1080", 1728, 1080),
        new StretchPreset("Custom", 0, 0),
    };

    private StretchPreset? _stretchPreset;
    private string _stretchWidth = "", _stretchHeight = "", _stretchStatus = "";
    private bool _stretchLoaded;

    private void EnsureStretchLoaded()
    {
        if (_stretchLoaded) return;
        _stretchLoaded = true;
        var (w, h) = StretchedResolution.Current();
        var (sw, sh) = StretchedResolution.NativeScreenSize();
        if (!StretchedResolution.IsOn || w <= 0 || h <= 0)
        {
            // Suggest the classic 4:3 stretched size for this monitor's height (1440 x 1080 on 1080p).
            h = sh >= 480 ? sh : 1080;
            w = h * 4 / 3;
        }
        _stretchWidth = w.ToString();
        _stretchHeight = h.ToString();
        _stretchPreset = Array.Find(((StretchPreset[])StretchPresets), p => p.Width == w && p.Height == h) ?? StretchPresets[^1];
        _stretchStatus = StretchedResolution.IsOn ? $"Stretched is on: {w} x {h}." : "Stretched is off.";
    }

    public StretchPreset? SelectedStretchPreset
    {
        get { EnsureStretchLoaded(); return _stretchPreset; }
        set
        {
            if (!Set(ref _stretchPreset, value) || value is null || value.Width == 0) return;
            _stretchWidth = value.Width.ToString();
            _stretchHeight = value.Height.ToString();
            OnPropertyChanged(nameof(StretchWidth));
            OnPropertyChanged(nameof(StretchHeight));
        }
    }

    public string StretchWidth
    {
        get { EnsureStretchLoaded(); return _stretchWidth; }
        set { if (Set(ref _stretchWidth, value)) MarkStretchCustom(); }
    }

    public string StretchHeight
    {
        get { EnsureStretchLoaded(); return _stretchHeight; }
        set { if (Set(ref _stretchHeight, value)) MarkStretchCustom(); }
    }

    public string StretchStatus
    {
        get { EnsureStretchLoaded(); return _stretchStatus; }
        private set => Set(ref _stretchStatus, value);
    }

    private void MarkStretchCustom()
    {
        if (!int.TryParse(_stretchWidth, out int w) || !int.TryParse(_stretchHeight, out int h)) return;
        var match = Array.Find(((StretchPreset[])StretchPresets), p => p.Width == w && p.Height == h) ?? StretchPresets[^1];
        if (!ReferenceEquals(match, _stretchPreset)) { _stretchPreset = match; OnPropertyChanged(nameof(SelectedStretchPreset)); }
    }

    public ICommand ApplyStretchCommand => _applyStretchCommand ??= new RelayCommand(ApplyStretch);
    public ICommand ResetStretchCommand => _resetStretchCommand ??= new RelayCommand(ResetStretch);
    private ICommand? _applyStretchCommand, _resetStretchCommand;

    private void ApplyStretch()
    {
        if (!int.TryParse(StretchWidth.Trim(), out int w) || !int.TryParse(StretchHeight.Trim(), out int h))
        {
            StretchStatus = "Enter the width and height as numbers.";
            return;
        }
        string error = StretchedResolution.Validate(w, h);
        if (error.Length > 0) { StretchStatus = error; return; }
        try
        {
            StretchedResolution.Apply(w, h);
            StretchStatus = $"Saved {w} x {h}. Start (or restart) Fortnite to play stretched. Black bars? NVIDIA Control Panel > Adjust desktop size and position > Full-screen, scaling on GPU (AMD: GPU scaling, Full panel).";
        }
        catch (Exception e) when (e is IOException or UnauthorizedAccessException)
        {
            StretchStatus = "Could not save: " + e.Message;
        }
    }

    private void ResetStretch()
    {
        try
        {
            StretchedResolution.Reset();
            var (w, h) = StretchedResolution.NativeScreenSize();
            StretchStatus = w > 0 ? $"Back to normal: windowed fullscreen at {w} x {h}." : "Back to normal.";
        }
        catch (Exception e) when (e is IOException or UnauthorizedAccessException)
        {
            StretchStatus = "Could not reset: " + e.Message;
        }
    }
}
