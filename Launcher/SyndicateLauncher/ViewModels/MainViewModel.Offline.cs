using System.Windows.Input;
using SyndicateLauncher.Services;

namespace SyndicateLauncher.ViewModels;

/// <summary>
/// Offline mode (Services/OfflineMode.cs): the ONLINE / OFFLINE button in the title bar and the switch in
/// Settings &gt; Launcher &amp; files. Switching restarts the local backend so it picks the mode up; while Fortnite is
/// running it waits for the next session instead (a restart would sign the game out).
/// </summary>
public sealed partial class MainViewModel
{
    private ICommand? _toggleOfflineCommand;

    public bool OfflineModeEnabled
    {
        get => _settings.OfflineMode;
        set
        {
            if (value == _settings.OfflineMode) return;
            _settings.OfflineMode = value;
            OfflineMode.Enabled = value;
            Save();
            OnPropertyChanged();
            OnPropertyChanged(nameof(OfflineButtonText));
            OnPropertyChanged(nameof(OfflineButtonTip));
            // Discord status shows your game to your Discord friends online, so it is off while offline.
            if (value) DiscordPresence.Stop();
            else StartDiscordPresence();
            if (_launching || GameLauncher.IsRunning)
            {
                StatusMessage = (value ? "Offline mode is on" : "Offline mode is off") +
                                ". It applies from your next session, once Fortnite is closed.";
                return;
            }
            StatusMessage = value
                ? "Offline mode is on: nothing goes online and no other PC can connect. Matches, bots, the locker and progression work as normal."
                : "Offline mode is off.";
            // The backend reads Offline mode when it starts: restart it now so Home and Play use the new mode.
            _ = EnsureBackendForHomeAsync();
        }
    }

    public ICommand ToggleOfflineCommand => _toggleOfflineCommand ??= new RelayCommand(() => OfflineModeEnabled = !OfflineModeEnabled);

    public string OfflineButtonText => _settings.OfflineMode ? "OFFLINE" : "ONLINE";

    public string OfflineButtonTip => _settings.OfflineMode
        ? "Offline mode is on: no update check, error reports, Discord status, friends or redeem codes, and other PCs can't connect. Click to go back online."
        : "Click for Offline mode: play with nothing going online (no update check, error reports, Discord status, friends or redeem codes).";
}
