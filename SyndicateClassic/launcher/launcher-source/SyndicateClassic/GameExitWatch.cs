// Syndicate © 2026 Syndicate. All rights reserved. Proprietary — no copying, distribution, or derivative works. See LICENSE.
using System.Linq;
using System;
using System.Threading.Tasks;
using System.Windows.Threading;

namespace SyndicateClassic;

// When Fortnite is closed outside the launcher (its own Quit, Alt+F4, Task Manager, a crash), the launcher
// used to keep showing QUIT until you pressed it. This watch notices the game is gone and ends the session
// the same way the QUIT button does (a match you host keeps running while players are still in it), so the
// button goes back to PLAY by itself. It also covers a launcher reopened while a game was running.
public partial class MainWindow
{
    private DispatcherTimer? _gameExitTimer;
    private int _gameGoneChecks;
    private bool _gameExitLeaving;

    private void StartGameExitWatch()
    {
        if (_gameExitTimer != null) return;
        _gameExitTimer = new DispatcherTimer { Interval = TimeSpan.FromSeconds(1.5) };
        _gameExitTimer.Tick += async (_, __) => { await CheckGameExited(); await KeepBackendAlive(); };
        _gameExitTimer.Start();
        Closed += (_, __) => _gameExitTimer?.Stop();
    }

    // Backend watchdog. If the local backend stops while Fortnite is open (a crash, or something closing it),
    // the game loses its services and drops back to the Epic login screen at the next request. Restart it
    // straight away: the game's login tokens are saved in tokens.json, so the open game keeps working.
    private int _backendDownChecks;
    private bool _backendRestarting;
    private async Task KeepBackendAlive()
    {
        if (_backendChecking) return; // the timer ticks every 1.5 s; one check at a time
        _backendChecking = true;
        try { await KeepBackendAliveCore(); } finally { _backendChecking = false; }
    }
    private bool _backendChecking;
    private async Task KeepBackendAliveCore()
    {
        if (_backendRestarting || _exitCleaning || _busy || _gameExitLeaving || !_sessionStarted || _paths == null || RadminConnection.Remote) { _backendDownChecks = 0; return; }
        bool up;
        var paths = _paths;
        bool running;
        try { running = await Task.Run(() => SessionProcesses.OwnedRunning(paths, "backend") && Launcher.IsBackendUp()); } catch { running = true; }
        // Also catch a backend that is running but stuck (accepts connections, never answers).
        up = running && await Launcher.IsBackendRespondingAsync();
        if (up) { _backendDownChecks = 0; return; }
        if (++_backendDownChecks < 2) return; // twice in a row (about 3 s), never on a single slow answer
        _backendDownChecks = 0; _backendRestarting = true;
        try
        {
            if (running)
            {
                Log("The local backend stopped answering while Fortnite was open: restarting it.");
                await Task.Run(() => SessionProcesses.StopOwned(paths, "backend"));
                Launcher.ForgetBackendCheck();
            }
            else Log("The local backend stopped while Fortnite was open: restarting it (see Runtime\\Server\\backend-startup.previous.log).");
            await EnsureAccountBackend();
            Log("Local backend restarted.");
        }
        catch (Exception error) { Log("Could not restart the local backend: " + error.Message); }
        finally { _backendRestarting = false; }
    }

    // When Fortnite closes (or crashes), its own log and the newest crash report (from the last 10 minutes) are
    // copied to Runtime\Server\fortnite-last.log and Runtime\Server\crash\, next to the other logs, so a
    // crash can be looked at without opening AppData. Which Dev Console switches were on goes in crash\dev-switches.txt.
    private static void SaveGameLogs(LauncherPaths? paths)
    {
        if (paths == null) return;
        try
        {
            var server = System.IO.Path.Combine(paths.Root, "Runtime", "Server");
            var local = System.IO.Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "FortniteGame", "Saved");
            var game = System.IO.Path.GetFullPath(System.IO.Path.Combine(System.IO.Path.GetDirectoryName(paths.GameExe) ?? "", "..", "..", "Saved"));
            foreach (var saved in new[] { local, game })
            {
                var log = System.IO.Path.Combine(saved, "Logs", "FortniteGame.log");
                if (System.IO.File.Exists(log) && DateTime.UtcNow - System.IO.File.GetLastWriteTimeUtc(log) < TimeSpan.FromMinutes(10))
                    CopyShared(log, System.IO.Path.Combine(server, "fortnite-last.log"));
                var crashes = System.IO.Path.Combine(saved, "Crashes");
                if (!System.IO.Directory.Exists(crashes)) continue;
                var newest = new System.IO.DirectoryInfo(crashes).GetDirectories().OrderByDescending(d => d.LastWriteTimeUtc).FirstOrDefault();
                if (newest == null || DateTime.UtcNow - newest.LastWriteTimeUtc > TimeSpan.FromMinutes(10)) continue;
                var target = System.IO.Path.Combine(server, "crash");
                System.IO.Directory.CreateDirectory(target);
                foreach (var file in newest.GetFiles())
                    if (file.Length < 20 * 1024 * 1024) CopyShared(file.FullName, System.IO.Path.Combine(target, file.Name));
                var switches = string.Join(", ", new[] { "dev-markers-on.txt", "dev-aim-assist.txt", "dev-fly-on.txt", "dev-no-recoil.txt", "dev-no-drop.txt", "dev-infinite-ammo.txt", "dev-console-on.txt" }
                    .Where(f => System.IO.File.Exists(HudFlag(f))));
                System.IO.File.WriteAllText(System.IO.Path.Combine(target, "dev-switches.txt"), $"{DateTime.Now:yyyy-MM-dd HH:mm:ss} crash {newest.Name}\nDev Console switches on: {(switches.Length == 0 ? "none" : switches)}\n");
            }
        }
        catch { }
    }

    private static void CopyShared(string from, string to)
    {
        try
        {
            using var input = new System.IO.FileStream(from, System.IO.FileMode.Open, System.IO.FileAccess.Read, System.IO.FileShare.ReadWrite | System.IO.FileShare.Delete);
            using var output = new System.IO.FileStream(to, System.IO.FileMode.Create, System.IO.FileAccess.Write);
            input.CopyTo(output);
        }
        catch { }
    }

    // Called from the game's Exited event too, so the button flips straight away.
    private async Task CheckGameExited(bool exitedNow = false)
    {
        if (_gameExitLeaving || _busy || _authBusy || !_sessionStarted || _paths == null) { _gameGoneChecks = 0; return; }
        bool running;
        try { running = (_game != null && !_game.HasExited) || await Task.Run(() => SessionProcesses.ClientRunning(_paths)); }
        catch { running = true; }
        if (running) { _gameGoneChecks = 0; return; }
        // Two checks in a row (about 3 s) unless the Exited event already told us, so a game that is still
        // starting or restarting is never mistaken for a closed one.
        if (!exitedNow && ++_gameGoneChecks < 2) return;
        _gameGoneChecks = 0;
        _gameExitLeaving = true; _busy = true; UpdateButtons();
        var crashPaths = _paths;
        _ = Task.Run(async () => { SaveGameLogs(crashPaths); await Task.Delay(10000); SaveGameLogs(crashPaths); });
        try
        {
            Log("Fortnite was closed outside the launcher: ending the session.");
            await LeaveSyndicateSession(_paths);
        }
        catch (Exception error) { Log("Could not end the session after Fortnite closed: " + error.Message); _sessionStarted = false; }
        finally { _busy = false; _gameExitLeaving = false; UpdateButtons(); }
    }
}