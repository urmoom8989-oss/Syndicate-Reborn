using System;
using System.Linq;
using System.Threading.Tasks;
using System.Windows.Input;
using SyndicateLauncher.Models;
using SyndicateLauncher.Services;

namespace SyndicateLauncher.ViewModels;

/// <summary>What the Choose Player confirmation step is asking about.</summary>
public enum PlayerAction
{
    None,
    Rename,
    SwitchMode,
    Delete
}

/// <summary>
/// Choose Player: rename a player, switch between Progression and Unlock All, or delete a player. Every action
/// opens a confirmation step that says exactly what it will do; deleting also asks for the player's name.
/// The backend account is changed first, so the launcher profile and the game save never disagree.
/// </summary>
public sealed partial class MainViewModel
{
    private PlayerAction _playerAction;
    private Profile? _actionProfile;
    private string _actionInput = "";
    private string _actionError = "";
    private bool _actionBusy;
    private ICommand? _startRename, _startSwitchMode, _startDelete, _cancelAction, _confirmAction;

    public ICommand StartRenamePlayerCommand => _startRename ??= new RelayCommand(() => BeginPlayerAction(PlayerAction.Rename));
    public ICommand StartSwitchModeCommand => _startSwitchMode ??= new RelayCommand(() => BeginPlayerAction(PlayerAction.SwitchMode));
    public ICommand StartDeletePlayerCommand => _startDelete ??= new RelayCommand(() => BeginPlayerAction(PlayerAction.Delete));
    public ICommand CancelPlayerActionCommand => _cancelAction ??= new RelayCommand(() => { if (!_actionBusy) EndPlayerAction(); });
    public ICommand ConfirmPlayerActionCommand => _confirmAction ??= new RelayCommand(() => _ = ConfirmPlayerActionAsync(), () => CanConfirmPlayerAction);

    /// <summary>Right-click > Delete player in the list (MainWindow.xaml.cs).</summary>
    public void BeginDeletePlayer(Profile profile)
    {
        SelectedProfile = profile;
        BeginPlayerAction(PlayerAction.Delete);
    }

    public bool IsPlayerActionOpen => _playerAction != PlayerAction.None;
    public bool PlayerActionNeedsInput => _playerAction is PlayerAction.Rename or PlayerAction.Delete;
    public bool PlayerActionIsDanger => _playerAction == PlayerAction.Delete;
    public bool PlayerActionBusy { get => _actionBusy; private set { if (Set(ref _actionBusy, value)) OnPropertyChanged(nameof(PlayerActionConfirmText)); } }

    public string PlayerActionInput
    {
        get => _actionInput;
        set { if (Set(ref _actionInput, value ?? "")) { PlayerActionError = ""; CommandManager.InvalidateRequerySuggested(); } }
    }

    public string PlayerActionError
    {
        get => _actionError;
        private set { if (Set(ref _actionError, value)) OnPropertyChanged(nameof(HasPlayerActionError)); }
    }
    public bool HasPlayerActionError => _actionError.Length > 0;

    /// <summary>Unlock All players: Battle Royale cosmetics only, or with the Save the World heroes too.</summary>
    public string[] StwSkinsOptions { get; } = { "Battle Royale skins only", "Add Save the World skins" };
    public bool ShowStwSkinsOption => SelectedProfile?.Mode == ProfileMode.UnlockedAll;
    public int SelectedStwSkinsOption
    {
        get => SelectedProfile?.StwSkins == true ? 1 : 0;
        set
        {
            var profile = SelectedProfile;
            if (profile is null || (value == 1) == profile.StwSkins) return;
            profile.StwSkins = value == 1;
            Save();
            OnPropertyChanged();
            _ = ApplyStwSkinsAsync(profile);
        }
    }
    private async Task ApplyStwSkinsAsync(Profile profile)
    {
        try
        {
            await EnsureBackendForPlayersAsync();
            await SyndicateBackend.PostAsPlayerAsync(profile.Name, "launcher/stwskins/set", new { enabled = profile.StwSkins && profile.Mode == ProfileMode.UnlockedAll });
            StatusMessage = profile.Name + (profile.StwSkins ? ": Save the World skins added to the locker." : ": locker has Battle Royale skins only.");
        }
        catch (Exception error)
        {
            StatusMessage = "Saved. The locker updates the next time you press Play (" + error.Message + ")";
        }
    }

    /// <summary>Text on the Switch mode button for the selected player.</summary>
    public string SwitchModeButtonText => SelectedProfile?.Mode == ProfileMode.Progression ? "SWITCH TO UNLOCK ALL" : "SWITCH TO PROGRESSION";

    private bool SwitchingToUnlockAll => _actionProfile?.Mode == ProfileMode.Progression;

    public string PlayerActionTitle => _playerAction switch
    {
        PlayerAction.Rename => "Rename " + _actionProfile?.Name + "?",
        PlayerAction.SwitchMode => SwitchingToUnlockAll ? "Switch to Unlock All?" : "Switch to Progression?",
        PlayerAction.Delete => "Delete " + _actionProfile?.Name + "?",
        _ => ""
    };

    public string PlayerActionBody => _playerAction switch
    {
        PlayerAction.Rename =>
            "Only the name changes. " + _actionProfile?.Name + " keeps their level, Battle Pass, locker, V-Bucks, " +
            "achievements and stats, and friends see the new name in game. Use 3 to 16 letters, numbers, spaces, - or _.",
        PlayerAction.SwitchMode when SwitchingToUnlockAll =>
            "Every cosmetic from your Fortnite builds is added to " + _actionProfile?.Name + "'s locker, and the season " +
            "is maxed (level, Battle Pass tier and challenges). Your real progress is kept and comes back if you " +
            "switch to Progression again.",
        PlayerAction.SwitchMode =>
            "The cosmetics Unlock All added are removed from " + _actionProfile?.Name + "'s locker, and your real level, " +
            "Battle Pass tier and challenges come back. Anything you earned, bought or redeemed stays, and so do your " +
            "V-Bucks.",
        PlayerAction.Delete =>
            "This permanently deletes " + _actionProfile?.Name + " from this PC: level, Battle Pass, locker, V-Bucks, " +
            "achievements, stats and friends list. It can't be undone. Type the player's name to confirm.",
        _ => ""
    };

    public string PlayerActionInputLabel => _playerAction == PlayerAction.Delete ? "TYPE " + _actionProfile?.Name?.ToUpperInvariant() + " TO CONFIRM" : "NEW NAME";

    public string PlayerActionConfirmText => _actionBusy ? "WORKING..." : _playerAction switch
    {
        PlayerAction.Rename => "RENAME PLAYER",
        PlayerAction.SwitchMode => SwitchingToUnlockAll ? "SWITCH TO UNLOCK ALL" : "SWITCH TO PROGRESSION",
        PlayerAction.Delete => "DELETE FOREVER",
        _ => ""
    };

    private bool CanConfirmPlayerAction => !_actionBusy && _actionProfile is not null && _playerAction switch
    {
        PlayerAction.Rename => ProfileNameRule.IsMatch(_actionInput.Trim()) &&
                               !string.Equals(_actionInput.Trim(), _actionProfile.Name, StringComparison.Ordinal),
        PlayerAction.Delete => string.Equals(_actionInput.Trim(), _actionProfile.Name, StringComparison.OrdinalIgnoreCase),
        PlayerAction.SwitchMode => true,
        _ => false
    };

    private void BeginPlayerAction(PlayerAction action)
    {
        if (SelectedProfile is null) { AvatarError = "Choose a player first."; return; }
        _actionProfile = SelectedProfile;
        _playerAction = action;
        PlayerActionInput = action == PlayerAction.Rename ? SelectedProfile.Name : "";
        PlayerActionError = _launching || GameLauncher.IsRunning
            ? "Close Fortnite first: players can't be changed while a match session is running."
            : "";
        RaisePlayerAction();
    }

    private void EndPlayerAction()
    {
        _playerAction = PlayerAction.None;
        _actionProfile = null;
        _actionInput = "";
        PlayerActionError = "";
        RaisePlayerAction();
    }

    private void RaisePlayerAction()
    {
        foreach (var name in new[] { nameof(IsPlayerActionOpen), nameof(PlayerActionNeedsInput), nameof(PlayerActionIsDanger),
                     nameof(PlayerActionTitle), nameof(PlayerActionBody), nameof(PlayerActionInputLabel),
                     nameof(PlayerActionConfirmText), nameof(PlayerActionInput), nameof(SwitchModeButtonText) })
            OnPropertyChanged(name);
        CommandManager.InvalidateRequerySuggested();
    }

    private async Task ConfirmPlayerActionAsync()
    {
        var profile = _actionProfile;
        if (profile is null || !CanConfirmPlayerAction) return;
        if (_launching || GameLauncher.IsRunning)
        {
            PlayerActionError = "Close Fortnite first: players can't be changed while a match session is running.";
            return;
        }
        PlayerActionBusy = true;
        PlayerActionError = "";
        try
        {
            switch (_playerAction)
            {
                case PlayerAction.Rename: await RenamePlayerAsync(profile, _actionInput.Trim()); break;
                case PlayerAction.SwitchMode: await SwitchPlayerModeAsync(profile); break;
                case PlayerAction.Delete: await DeletePlayerAsync(profile); break;
            }
            EndPlayerAction();
        }
        catch (Exception e)
        {
            PlayerActionError = e.Message;
        }
        finally
        {
            PlayerActionBusy = false;
            CommandManager.InvalidateRequerySuggested();
        }
    }

    /// <summary>
    /// Players brought over from Project Tilted (Services/TiltedImport.cs) get a player here too, in the mode their
    /// Tilted locker was in. Names that already have a player here are left alone.
    /// </summary>
    private void OnTiltedPlayersImported(System.Collections.Generic.IReadOnlyList<TiltedImport.ImportedPlayer> players)
    {
        var dispatcher = System.Windows.Application.Current?.Dispatcher;
        if (dispatcher is null) return;
        dispatcher.InvokeAsync(() =>
        {
            var added = new System.Collections.Generic.List<string>();
            foreach (var player in players)
            {
                if (Profiles.Any(p => p.Name.Equals(player.Name, StringComparison.OrdinalIgnoreCase))) continue;
                var profile = new Profile
                {
                    Name = player.Name,
                    Mode = player.UnlockAll ? ProfileMode.UnlockedAll : ProfileMode.Progression,
                    Avatar = ProfileAvatars.Ids[Random.Shared.Next(ProfileAvatars.Ids.Count)]
                };
                _settings.Profiles.Add(profile);
                Profiles.Add(profile);
                added.Add(player.Name);
            }
            if (added.Count == 0) return;
            if (SelectedProfile is null) SelectedProfile = Profiles.FirstOrDefault(p => p.Name == added[0]); // also saves
            else Save();
            StatusMessage = "Brought over from Project Tilted: " + string.Join(", ", added) + ".";
        });
    }

    /// <summary>The backend has to be up to change a player's game save; starts it the same way Play does.</summary>
    private async Task EnsureBackendForPlayersAsync()
    {
        if (await SyndicateBackend.IsAvailableAsync()) return;
        await SyndicateBackend.EnsureRunningAsync(_ => { }, ActiveSeason?.SeasonNumber ?? 7);
    }

    private async Task RenamePlayerAsync(Profile profile, string newName)
    {
        if (Profiles.Any(p => !ReferenceEquals(p, profile) && p.Name.Equals(newName, StringComparison.OrdinalIgnoreCase)))
            throw new InvalidOperationException("You already have a player with that name.");
        string oldName = profile.Name;
        await EnsureBackendForPlayersAsync();
        // Backend first: if it refuses (name taken), nothing changes here either.
        await SyndicateBackend.RenamePlayerAsync(oldName, newName);
        profile.Name = newName;
        StatsService.Rename(oldName, newName);
        if (ReferenceEquals(profile, SelectedProfile)) _settings.ActiveProfile = newName;
        Save();
        foreach (var name in new[] { nameof(PlayerName), nameof(PlayerInitial), nameof(PlayerChipText), nameof(PlayerStatus) })
            OnPropertyChanged(name);
        _ = Home.RefreshAsync(force: true);
        StatusMessage = "Renamed " + oldName + " to " + newName + ".";
    }

    private async Task SwitchPlayerModeAsync(Profile profile)
    {
        bool unlockAll = profile.Mode == ProfileMode.Progression;
        await EnsureBackendForPlayersAsync();
        // Applied now (Play applies it again every launch), so the locker is right the moment the game opens.
        await SyndicateBackend.PostAsPlayerAsync(profile.Name, "launcher/unlockall/set", new { enabled = unlockAll });
        profile.Mode = unlockAll ? ProfileMode.UnlockedAll : ProfileMode.Progression;
        Save();
        foreach (var name in new[] { nameof(PlayerStatus), nameof(SelectedProfileModeLabel), nameof(SelectedProfileModeDescription), nameof(SwitchModeButtonText), nameof(ShowStwSkinsOption), nameof(SelectedStwSkinsOption) })
            OnPropertyChanged(name);
        StatusMessage = profile.Name + " is now " + profile.ModeLabel + ".";
    }

    private async Task DeletePlayerAsync(Profile profile)
    {
        await EnsureBackendForPlayersAsync();
        await SyndicateBackend.DeletePlayerAsync(profile.Name);
        StatsService.Delete(profile.Name);
        string name = profile.Name;
        DeleteProfile(profile);
        StatusMessage = "Deleted " + name + ".";
    }
}
