using System;
using System.Collections.Generic;
using System.Collections.ObjectModel;
using System.Linq;
using System.Windows.Input;
using SyndicateLauncher.Models;

namespace SyndicateLauncher.ViewModels;

/// <summary>One chip on the Patch Notes page (a version, or a category such as BOTS).</summary>
public sealed class PatchTab : ViewModelBase
{
    private readonly Action<PatchTab> _select;
    private bool _isActive;

    public PatchTab(string key, string label, string? badge, Action<PatchTab> select)
    {
        Key = key;
        Label = label;
        Badge = badge ?? "";
        _select = select;
    }

    public string Key { get; }
    public string Label { get; }
    /// <summary>Small text after the label: a change count, or NEW on the newest version.</summary>
    public string Badge { get; }
    public bool HasBadge => Badge.Length > 0;

    /// <summary>Bound two-way to the chip; checking it selects this tab.</summary>
    public bool IsActive
    {
        get => _isActive;
        set
        {
            if (!Set(ref _isActive, value) || !value) return;
            _select(this);
        }
    }

    internal void SetActive(bool value) => Set(ref _isActive, value, nameof(IsActive));
}

public sealed partial class MainViewModel
{
    private const string AllCategories = "ALL";
    private PatchRelease _selectedPatch = PatchNotes.Latest;
    private string _patchCategory = AllCategories;
    private ObservableCollection<PatchTab>? _patchVersionTabs;

    /// <summary>One chip per release, newest first and selected.</summary>
    public ObservableCollection<PatchTab> PatchVersionTabs => _patchVersionTabs ??= new ObservableCollection<PatchTab>(
        PatchNotes.Releases.Select((r, i) =>
        {
            var tab = new PatchTab(r.Version, r.VersionLabel, i == 0 ? "NEW" : null, SelectPatchVersion);
            tab.SetActive(i == 0);
            return tab;
        }));

    /// <summary>ALL plus one chip per section of the selected release, each with its change count.</summary>
    public ObservableCollection<PatchTab> PatchCategoryTabs { get; } = new();

    public PatchRelease SelectedPatch
    {
        get => _selectedPatch;
        private set
        {
            if (!Set(ref _selectedPatch, value)) return;
            RebuildPatchCategories(AllCategories);
        }
    }

    /// <summary>The sections shown under the chips: all of them, or just the chosen category.</summary>
    public IReadOnlyList<PatchNoteSection> VisiblePatchSections => _patchCategory == AllCategories
        ? _selectedPatch.Sections
        : _selectedPatch.Sections.Where(s => s.Title == _patchCategory).ToList();

    public bool ShowPatchKnownIssues => _selectedPatch.HasKnownIssues && _patchCategory == AllCategories;

    /// <summary>True until the player has opened the notes for this version.</summary>
    public bool HasUnreadPatchNotes => !string.Equals(_settings.LastSeenPatchNotes, PatchNotes.Latest.Version, StringComparison.OrdinalIgnoreCase);

    private void SelectPatchVersion(PatchTab tab)
    {
        foreach (var t in PatchVersionTabs) if (!ReferenceEquals(t, tab)) t.SetActive(false);
        SelectedPatch = PatchNotes.Releases.FirstOrDefault(r => r.Version == tab.Key) ?? PatchNotes.Latest;
    }

    private void SelectPatchCategory(PatchTab tab)
    {
        foreach (var t in PatchCategoryTabs) if (!ReferenceEquals(t, tab)) t.SetActive(false);
        _patchCategory = tab.Key;
        OnPropertyChanged(nameof(VisiblePatchSections));
        OnPropertyChanged(nameof(ShowPatchKnownIssues));
    }

    private void RebuildPatchCategories(string active)
    {
        PatchCategoryTabs.Clear();
        _patchCategory = active;
        var all = new PatchTab(AllCategories, "ALL", _selectedPatch.ChangeCount.ToString(), SelectPatchCategory);
        all.SetActive(active == AllCategories);
        PatchCategoryTabs.Add(all);
        foreach (var section in _selectedPatch.Sections)
        {
            var tab = new PatchTab(section.Title, section.Title, section.Items.Count.ToString(), SelectPatchCategory);
            tab.SetActive(section.Title == active);
            PatchCategoryTabs.Add(tab);
        }
        OnPropertyChanged(nameof(VisiblePatchSections));
        OnPropertyChanged(nameof(ShowPatchKnownIssues));
    }

    private void ShowPatchNotes()
    {
        // Always open on the newest release with every category showing.
        foreach (var t in PatchVersionTabs) t.SetActive(t.Key == PatchNotes.Latest.Version);
        if (!ReferenceEquals(_selectedPatch, PatchNotes.Latest)) SelectedPatch = PatchNotes.Latest;
        else if (PatchCategoryTabs.Count == 0 || _patchCategory != AllCategories) RebuildPatchCategories(AllCategories);
        CurrentScreen = AppScreen.PatchNotes;
        IsSettingsOpen = false;
        MarkPatchNotesSeen();
    }

    private void MarkPatchNotesSeen()
    {
        if (!HasUnreadPatchNotes) return;
        _settings.LastSeenPatchNotes = PatchNotes.Latest.Version;
        Save();
        OnPropertyChanged(nameof(HasUnreadPatchNotes));
    }

    /// <summary>After an update, open the new notes once. A brand-new install (no account yet) just marks them
    /// seen, so first-time players land on Home instead of a changelog.</summary>
    private void ShowPatchNotesAfterUpdate()
    {
        if (!HasUnreadPatchNotes) return;
        if (_settings.Profiles.Count == 0) { MarkPatchNotesSeen(); return; }
        ShowPatchNotes();
    }
}
