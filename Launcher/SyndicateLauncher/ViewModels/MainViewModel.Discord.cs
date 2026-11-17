using SyndicateLauncher.Models;
using SyndicateLauncher.Services;

namespace SyndicateLauncher.ViewModels;

/// <summary>Settings > Launcher: Discord status (Services/DiscordPresence.cs).</summary>
public sealed partial class MainViewModel
{
    public bool DiscordPresenceEnabled
    {
        get => _settings.DiscordPresence;
        set
        {
            if (value == _settings.DiscordPresence) return;
            _settings.DiscordPresence = value;
            Save();
            OnPropertyChanged();
            if (value) StartDiscordPresence(); else DiscordPresence.Stop();
        }
    }

    /// <summary>Called once the main window is up. Reads only plain strings from here, from its own task.</summary>
    public void StartDiscordPresence()
    {
        if (!_settings.DiscordPresence) return;
        DiscordPresence.Start(() => (State == BuildState.Found ? BuildEraText : "", SelectedProfile?.Name ?? ""));
    }
}
