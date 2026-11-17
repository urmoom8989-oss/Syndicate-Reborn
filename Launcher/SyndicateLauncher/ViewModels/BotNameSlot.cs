using System;
using System.Windows;
using System.Windows.Input;

namespace SyndicateLauncher.ViewModels;

/// <summary>One bot name in the Settings roster. Saving changes only this slot.</summary>
public sealed class BotNameSlot : ViewModelBase
{
    private readonly Action<BotNameSlot> _save;
    private string _name;
    private string _draftName;
    private string _error = "";
    private bool _isEditing;

    public BotNameSlot(int index, string defaultName, string name, Action<BotNameSlot> save)
    {
        Index = index;
        DefaultName = defaultName;
        _name = name;
        _draftName = name;
        _save = save;
        EditCommand = new RelayCommand(BeginEdit);
        SaveCommand = new RelayCommand(() => _save(this));
        CancelCommand = new RelayCommand(CancelEdit);
    }

    public int Index { get; }
    public string Number => (Index + 1).ToString("00");
    public string DefaultName { get; }
    public string Name
    {
        get => _name;
        private set
        {
            if (!Set(ref _name, value)) return;
            OnPropertyChanged(nameof(IsCustomized));
        }
    }
    public string DraftName
    {
        get => _draftName;
        set
        {
            if (!Set(ref _draftName, value ?? "")) return;
            if (_error.Length > 0) SetError("");
        }
    }
    public string Error => _error;
    public bool IsEditing
    {
        get => _isEditing;
        private set
        {
            if (!Set(ref _isEditing, value)) return;
            OnPropertyChanged(nameof(ViewVisibility));
            OnPropertyChanged(nameof(EditVisibility));
        }
    }
    public bool IsCustomized => !string.Equals(Name, DefaultName, StringComparison.Ordinal);
    public Visibility ViewVisibility => IsEditing ? Visibility.Collapsed : Visibility.Visible;
    public Visibility EditVisibility => IsEditing ? Visibility.Visible : Visibility.Collapsed;
    public Visibility ErrorVisibility => _error.Length > 0 ? Visibility.Visible : Visibility.Collapsed;
    public ICommand EditCommand { get; }
    public ICommand SaveCommand { get; }
    public ICommand CancelCommand { get; }

    public void BeginEdit()
    {
        DraftName = Name;
        SetError("");
        IsEditing = true;
    }

    public void SetName(string name)
    {
        Name = name;
        DraftName = name;
    }

    public void SetError(string error)
    {
        if (!Set(ref _error, error, nameof(Error))) return;
        OnPropertyChanged(nameof(ErrorVisibility));
    }

    public void FinishEdit()
    {
        SetError("");
        IsEditing = false;
    }

    public void CancelEdit()
    {
        if (!IsEditing) return;
        DraftName = Name;
        SetError("");
        IsEditing = false;
    }
}
