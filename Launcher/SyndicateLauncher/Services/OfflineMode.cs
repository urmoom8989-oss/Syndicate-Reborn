namespace SyndicateLauncher.Services;

/// <summary>
/// Offline mode (the OFFLINE button in the title bar, and Settings &gt; Launcher &amp; files). While it is on, nothing the
/// launcher starts reaches the internet and nothing outside this PC can connect in:
///   * the launcher skips the GitHub update check, the error reports (Discord webhook) and Discord status;
///   * the backend and matchmaker start with SYNDICATE_OFFLINE=1 (backend Utils/syndicateOffline.js): they refuse
///     connections from other PCs, do not start the friends bridge, ignore "Play with friends", and turn off the
///     Discord bot, match uploads, redeem codes and party voice chat.
/// Matches with bots, the locker, the shops, progression and everything else on this PC work as normal.
/// Set once from the saved settings at startup (App.xaml.cs) and by MainViewModel when the player switches it.
/// </summary>
public static class OfflineMode
{
    private static volatile bool _enabled;

    public static bool Enabled
    {
        get => _enabled;
        set => _enabled = value;
    }
}
