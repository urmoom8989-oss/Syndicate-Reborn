using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Net.Http;
using System.Net.NetworkInformation;
using System.Security.Cryptography;
using System.Text.Json;
using System.Text.RegularExpressions;
using System.Threading.Tasks;
using SyndicateLauncher.Models;
namespace SyndicateLauncher.Services;

public static class GameLauncher
{
#if DEV_BUILD
    // stw-host.txt made every Pre-season/Season 1 host open the Save the World tutorial zone instead of the Battle
    // Royale map, so Battle Royale matchmaking never found a match. It is from the earlier hosted tutorial; the local
    // tutorial (stw-local.txt, the tutorial runs in the player's own game) replaced it. With the local tutorial on,
    // the host stays a Battle Royale host.
    private static bool StwHostWanted()
    {
        if (!File.Exists(Path.Combine(AppContext.BaseDirectory, "stw-host.txt"))) return false;
        bool stwLocal = File.Exists(Path.Combine(AppContext.BaseDirectory, "stw-local.txt"))
            || (SyndicateBackend.RuntimeDirectory is string runtime && File.Exists(Path.Combine(runtime, "stw-local.txt")));
        return !stwLocal;
    }
#endif

    public const string ConsoleModuleName = "console.dll";
    // EAC/launcher stubs are started frozen, exactly like Syndicate does for builds >= 6.
    private const string EpicCompanionArgs = " -nobe -fromfl=eac -fltoken=3db3ba5dcbd2e16703f3978d -caldera=eyJhbGciOiJFUzI1NiIsInR5cCI6IkpXVCJ9.eyJhY2NvdW50X2lkIjoiYmU5ZGE1YzJmYmVhNDQwN2IyZjQwZWJhYWQ4NTlhZDQiLCJnZW5lcmF0ZWQiOjE2Mzg3MTcyNzgsImNhbGRlcmFHdWlkIjoiMzgxMGI4NjMtMmE2NS00NDU3LTliNTgtNGRhYjNiNDgyYTg2IiwiYWNQcm92aWRlciI6IkVhc3lBbnRpQ2hlYXQiLCJub3RlcyI6IiIsImZhbGxiYWNrIjpmYWxzZX0.VAWQB67RTxhiWOxx7DBjnzDnXyyEnX7OljJm-j2d88G_WgwQ9wrE6lwMEHZHjBd1ISJdUO1UVUqkfLdU5nofBQ";
    private static readonly List<Process> Owned = new();
    private static readonly HttpClient Http = new() { Timeout = TimeSpan.FromSeconds(2) };
    private static readonly Lazy<string> BundledRuntime = new(RuntimeBundle.Resolve);
    private static string Runtime => BundledRuntime.Value;
    public static bool IsRunning => Owned.Any(p => !p.HasExited);

    /// <summary>Starts the local backend before Play is pressed, so the first launch does not
    /// spend its first several seconds waiting for MongoDB and the Node services.</summary>
    public static Task WarmBackendAsync(int season) => SyndicateBackend.WarmUpAsync(season);
    /// <summary>Runtime folder (dlls, backend data) the launcher runs from.</summary>
    public static string RuntimeFolder => Runtime;
    /// <summary>The player's Fortnite client in the current session (Syndicate Studio docks it), or null.</summary>
    private static Process? _client;
    private static DateTime _clientStartedUtc;
    public static Process? Client
    {
        get => _client;
        private set { _client = value; if (value is not null) _clientStartedUtc = DateTime.UtcNow; }
    }
    /// <summary>The profile, season and menu setting of the last launch (StatsService records the session).</summary>
    public static (string Profile, int Season, bool Menu)? LastLaunch { get; private set; }
    /// <summary>The current match server's engine log (-abslog), for Syndicate Studio's Output panel.</summary>
    public static string? HostLog { get; private set; }
    public static bool OwnsProcess(int pid) => Owned.Any(p => { try { return p.Id == pid && !p.HasExited; } catch (InvalidOperationException) { return false; } });

    /// <summary>True from the moment Play is pressed until the launch finishes or fails.
    /// The launcher's animations and effects stop at once while it is set, so starting the game
    /// never looks laggy.</summary>
    public static bool Starting { get; private set; }

    /// <summary>Raised on the UI thread when <see cref="Starting"/> changes.</summary>
    public static event Action? StartingChanged;

    public static void SetStarting(bool starting)
    {
        if (Starting == starting) return;
        Starting = starting;
        StartingChanged?.Invoke();
    }
    /// <summary>The Chapter 1 builds Syndicate's Erbium server supports.</summary>
    public static readonly string[] SyndicateBuilds =
    {
        "1.7.2", "1.8.2", "1.11", "2.5", "3.5.1", "4.5", "5.10", "6.20", "7.40", "8.50", "9.40"
    };

    public static bool IsSyndicateBuild(string version) => SyndicateBuilds.Contains(version, StringComparer.OrdinalIgnoreCase);

    /// <summary>Every build Syndicate can launch: the Chapter 1 slots plus the native 10.40.</summary>
    public static bool IsSupportedBuild(string version) =>
        IsSyndicateBuild(version) || string.Equals(version, "10.40", StringComparison.OrdinalIgnoreCase);

    public static void ValidateBuild(BuildInfo build)
    {
        bool native1040 = build.Version == "10.40" && build.Changelist == 9380822;
        bool chapter1 = IsSyndicateBuild(build.Version);
        if (build.Source != DetectionSource.GameFiles || (!native1040 && !chapter1))
            throw new InvalidOperationException(
                "Select a supported Fortnite build: 1.7.2, 1.8.2, 1.11, 2.5, 3.5.1, 4.5, 5.10, 6.20, 7.40, 8.50, 9.40 or 10.40 CL-9380822.");
    }

    /// <summary>All supported builds, including 10.40, use the bundled Erbium startup path.</summary>
    private static bool UsesSyndicateServer(BuildInfo build) => IsSupportedBuild(build.Version);

    /// <summary>Erbium reads TILTED_BOT_SKILL; the launcher's preset names map one-to-one.</summary>
    private static string SyndicateSkillName(BotPreset difficulty)
    {
        if (difficulty.BuiltinIndex >= 0)
            return difficulty.BuiltinIndex switch
            {
                0 => "bot",
                1 => "easy",
                3 => "hard",
                5 => "variety",
                _ => "medium"
            };
        return difficulty.Name.Trim().ToLowerInvariant() switch
        {
            "bot" => "bot",
            "easy" => "easy",
            "hard" => "hard",
            "variety" => "variety",
            _ => "medium"
        };
    }

    /// <summary>Syndicate only fakes the Epic launcher for Season 6 and later.</summary>
    private static bool NeedsEpicStub(BuildInfo build) => build.Major >= 6;

    /// <summary>Pre-season, Season 1 and Season 2 need longer before Erbium hooks in.</summary>
    private static bool IsEarlyBuild(BuildInfo build) => build.Version is "1.7.2" or "1.8" or "1.8.2" or "1.11" or "2.5";

    // 2.5 was verified with the legacy loading option and the core login module only.
    private static bool UseLegacy25ClientMode(BuildInfo build) => build.Version == "2.5";

    // Syndicate's prebuilt client modules. UPDATE FROM GIT installs them in Runtime\dlls under these names;
    // an older install (Documents\Project Tilted) still has them under their old file names.
    public const string ClientModsModule = "SyndicateClient.dll";
    public const string PadAssistModule = "SyndicatePadAssist.dll";

    private static string FindClientModule(string name, string oldName)
    {
        var bundled = Path.Combine(Runtime, "dlls", name);
        return File.Exists(bundled) ? bundled : FindSyndicateFile(@"Launcher\" + oldName, oldName);
    }

    private static string FindSyndicateFile(params string[] relative)
    {
        // The dev kit carries these next to the launcher so a developer does not need a
        // Syndicate install at a fixed path.
        var bundled = Path.Combine(Runtime, "dlls", relative[^1]);
        if (File.Exists(bundled)) return bundled;
        var roots = new[]
        {
            Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.MyDocuments), "Project Tilted"),
            Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.MyDocuments), "tiltedlegacy")
        };
        foreach (var root in roots)
            foreach (var part in relative)
            {
                var file = Path.Combine(root, part);
                if (File.Exists(file)) return file;
            }
        return "";
    }
    public static ProcessStartInfo CreateStartInfo(BuildInfo build, string name, bool host, string extra, string? exchangeCode = null)
    {
        bool syndicate = UsesSyndicateServer(build);
        string executableDirectory = Path.GetDirectoryName(build.ExecutablePath)!;
        // Syndicate starts its match host inside Runtime\Server, so the game's relative paths and
        // saved config resolve exactly as they do in Syndicate.
        string workingDirectory = syndicate && host ? (SyndicateBackend.RuntimeDirectory ?? executableDirectory) : executableDirectory;
        var start = new ProcessStartInfo(build.ExecutablePath) { WorkingDirectory = workingDirectory, UseShellExecute = false,
            CreateNoWindow = host, WindowStyle = host ? ProcessWindowStyle.Hidden : ProcessWindowStyle.Normal };
        // Match Reboot's compatibility setting for the game's older OpenSSL runtime.
        // Keep this scoped to our game processes rather than changing the machine.
        start.Environment["OPENSSL_ia32cap"] = ":~0x20000000";

        foreach (var arg in new[] { "-epicapp=Fortnite", "-epicenv=Prod", "-epiclocale=en-us", "-epicportal" }) start.ArgumentList.Add(arg);
        if (syndicate && !host) start.ArgumentList.Add("-game");
#if DEV_BUILD
        // Save the World (dev launcher), Pre-season/Season 1: the tutorial's reservation beacon sent an
        // encryption token, and the host (no online session of its own) failed it with "Unable to get user
        // or session". Turn net encryption off on both sides so the beacon connects in the clear.
        if (syndicate && build.Version is "1.7.2" or "1.8" or "1.8.2")
            start.ArgumentList.Add("-ini:Engine:[ConsoleVariables]:net.AllowEncryption=0");
#endif

        if (!host && exchangeCode is not null)
        {
            // Syndicate's backend signs in with a one-time exchange code (AccountClient.Exchange).
            start.ArgumentList.Add("-AUTH_TYPE=exchangecode");
            start.ArgumentList.Add("-AUTH_LOGIN=unused");
            start.ArgumentList.Add("-AUTH_PASSWORD=" + exchangeCode);
        }
        else if (!syndicate)
        {
            // Placeholder for the 10.40 host; LaunchAsync replaces it with a Syndicate exchange code once
            // the backend is running (Syndicate's backend rejects these LawinServer-style credentials).
            start.ArgumentList.Add("-AUTH_TYPE=epic");
            start.ArgumentList.Add("-AUTH_LOGIN=" + name + "@projectreboot.dev");
            start.ArgumentList.Add("-AUTH_PASSWORD=Rebooted");
        }

        if (syndicate)
        {
            if (host)
            {
                // Syndicate's match host command line, verbatim. -NoMovie, the hang-duration ini
                // override and the LogCmds filter change how the dedicated server initializes
                // and are kept identical so a crash cannot be blamed on a Syndicate-only flag.
                foreach (var arg in new[] { "-nullrhi", "-nosound", "-nosplash", "-NoMovie",
                    "-FORCELOGFLUSH", "-nothreadtimeout", "-NoHangDetection",
                    "-ini:Engine:[Core.System]:HangDuration=0",
                    "-LogCmds=LogAbilitySystem off, LogGameStats off, LogFortCustomization off, LogSkeletalMesh off, LogLevelActorContainer off" })
                    start.ArgumentList.Add(arg);
            }
            else
            {
                foreach (var arg in new[] { "-skippatchcheck", "-USEALLAVAILABLECORES", "-nosplash" }) start.ArgumentList.Add(arg);
                if (UseLegacy25ClientMode(build)) start.ArgumentList.Add("-NoAsyncLoadingThread");
            }
            // Season 6 and later pass the launcher-check arguments to the game itself too, not
            // only to the frozen companions; without them they force-log-out with
            // "Fortnite was not started correctly".
            if (NeedsEpicStub(build))
                foreach (var arg in SplitArguments(EpicCompanionArgs)) start.ArgumentList.Add(arg);
        }
        else
        {
            foreach (var arg in new[] { "-skippatchcheck", "-nobe", "-fromfl=eac", "-fltoken=3db3ba5dcbd2e16703f3978d",
                "-caldera=eyJhbGciOiJFUzI1NiIsInR5cCI6IkpXVCJ9.eyJhY2NvdW50X2lkIjoiYmU5ZGE1YzJmYmVhNDQwN2IyZjQwZWJhYWQ4NTlhZDQiLCJnZW5lcmF0ZWQiOjE2Mzg3MTcyNzgsImNhbGRlcmFHdWlkIjoiMzgxMGI4NjMtMmE2NS00NDU3LTliNTgtNGRhYjNiNDgyYTg2IiwiYWNQcm92aWRlciI6IkVhc3lBbnRpQ2hlYXQiLCJub3RlcyI6IiIsImZhbGxiYWNrIjpmYWxzZX0.VAWQB67RTxhiWOxx7DBjnzDnXyyEnX7OljJm-j2d88G_WgwQ9wrE6lwMEHZHjBd1ISJdUO1UVUqkfLdU5nofBQ" }) start.ArgumentList.Add(arg);
        }

        if (host && !syndicate)
        {
            foreach (var arg in new[] { "-windowed", "-ResX=640", "-ResY=360", "-nosound", "-nosplash", "-unattended", "-ExecCmds=t.IdleWhenNotForeground 0" }) start.ArgumentList.Add(arg);
        }
        else if (!host)
        {
            foreach (var arg in SplitArguments(extra))
            {
                if (arg.StartsWith("-AUTH_", StringComparison.OrdinalIgnoreCase) || arg.StartsWith("-epic", StringComparison.OrdinalIgnoreCase) || arg.StartsWith("-ExecCmds", StringComparison.OrdinalIgnoreCase) || arg.Equals("-nullrhi", StringComparison.OrdinalIgnoreCase))
                    throw new InvalidOperationException("Extra arguments cannot replace managed startup settings or hide the player window.");
                start.ArgumentList.Add(arg);
            }
        }
        return start;
    }
    public static IEnumerable<string> SplitArguments(string text)
    {
        var token = new System.Text.StringBuilder(); bool quoted = false;
        foreach (char ch in text ?? "")
        {
            if (ch == '"') { quoted = !quoted; continue; }
            if (char.IsWhiteSpace(ch) && !quoted) { if (token.Length > 0) { yield return token.ToString(); token.Clear(); } }
            else token.Append(ch);
        }
        if (quoted) throw new InvalidOperationException("An extra launch argument has an unclosed quote.");
        if (token.Length > 0) yield return token.ToString();
    }
    private static void VerifyModules()
    {
        Dictionary<string, string>? hashes;
        try { hashes = JsonSerializer.Deserialize<Dictionary<string, string>>(File.ReadAllText(Path.Combine(Runtime, "dlls", "manifest.json"))); }
        catch (JsonException ex) { throw new InvalidOperationException("Syndicate's file manifest is damaged. Extract the complete launcher archive again.", ex); }
        if (hashes is null) throw new InvalidOperationException("Syndicate's file manifest is empty. Extract the complete launcher archive again.");
        foreach (var name in ModuleStartup.RequiredModules)
        {
            var file = Path.Combine(Runtime, "dlls", name);
            if (!File.Exists(file) || !hashes.TryGetValue(name, out var expected) || !Convert.ToHexString(SHA256.HashData(File.ReadAllBytes(file))).Equals(expected, StringComparison.OrdinalIgnoreCase))
                throw new InvalidOperationException("Bundled module is missing or changed: " + name + ". Extract the complete launcher ZIP again.");
        }
    }
    public static string CheckRuntime() { VerifyModules(); return Runtime; }
    private static Process StartOwnedHidden(ProcessStartInfo info)
    {
        var process = HiddenDesktop.Start(info);
        Owned.Add(process); return process;
    }
    private static Process StartOwned(ProcessStartInfo info)
    {
        var process = Process.Start(info) ?? throw new InvalidOperationException("Windows could not start " + Path.GetFileName(info.FileName));
        Owned.Add(process); return process;
    }
    private static async Task<string> StartBackendAsync(Profile profile, int season, Action<string> status)
    {
        await SyndicateBackend.EnsureRunningAsync(status, season);
        return await SyndicateBackend.ExchangeCodeAsync(
            profile.Name,
            profile.Mode == ProfileMode.UnlockedAll,
            status,
            profile.StwSkins);
    }
    public static async Task LaunchAsync(BuildInfo build, Profile profile, int season, string extraArguments, Action<string> status, MatchOptions? options = null, bool enableMenu = false, Action<string>? log = null)
    {
        // One login request can occasionally slip past cobalt's page-guard hook and go to Epic,
        // which leaves Fortnite on the email/password screen. A fresh exchange code makes the
        // next session sign in normally; retry once instead of making the player press Play again.
        for (int attempt = 1; ; attempt++)
        {
            try
            {
                await LaunchOnceAsync(build, profile, season, extraArguments, status, options, enableMenu, log);
                return;
            }
            catch (LaunchProblemException error) when (attempt == 1 && error.InnerException is LoginAbortedException)
            {
                log?.Invoke("Fortnite aborted the local login; retrying with a fresh exchange code.");
                status("Fortnite showed the sign-in screen. Retrying automatically...");
                // StopOwned has already killed the failed host, client and backend. Give Windows
                // a moment to release the executable and port 7777 before the second attempt.
                for (int i = 0; i < 40 && Process.GetProcessesByName(Path.GetFileNameWithoutExtension(build.ExecutablePath)).Any(); i++)
                    await Task.Delay(250);
                for (int i = 0; i < 40 && PortInUse(7777); i++)
                    await Task.Delay(250);
            }
        }
    }

    private static async Task LaunchOnceAsync(BuildInfo build, Profile profile, int season, string extraArguments, Action<string> status, MatchOptions? options, bool enableMenu, Action<string>? log)
    {
        ValidateBuild(build);
        if (IsRunning) throw new InvalidOperationException("This launcher already has a running game. Close it before starting another session.");
        if (Process.GetProcessesByName(Path.GetFileNameWithoutExtension(build.ExecutablePath)).Any())
            throw new InvalidOperationException("Close the running Fortnite client/server before starting a new Syndicate session.");
        await Task.Run(VerifyModules);
        // Syndicate Studio open (or its Start): SyndicateMenu.dll never loads into a Studio session, so its hooks
        // can't crash the docked game or fight SyndicateStudio.dll.
        bool studioOpen = Studio.StudioSession.Current() is not null;
        bool useClientExtras = !UseLegacy25ClientMode(build);
        if (studioOpen || !useClientExtras) enableMenu = false;
        // Settings > Display > Crosshair: drawn by SyndicateMenu.dll. With the menu off the DLL is still loaded,
        // in crosshair-only mode (Services/CrosshairService.cs): no menu, keys or game-object hooks.
        bool crosshair = CrosshairService.Enabled && !studioOpen && useClientExtras;
        bool loadMenuDll = enableMenu || crosshair;
        if (loadMenuDll) CrosshairService.WriteLoadMode(enableMenu);
#if DEV_BUILD
        // Save the World, local tutorial (dev launcher, stw-local.txt next to the launcher or in Runtime\Server): your own game loads the
        // tutorial map itself (no host, no beacons). The backend writes stw-local-go.txt when the tutorial is
        // requested; SyndicateStudio.dll, loaded into the client below, then opens the map.
        bool stwLocal = File.Exists(Path.Combine(AppContext.BaseDirectory, "stw-local.txt"))
            || (SyndicateBackend.RuntimeDirectory is string stwRuntime && File.Exists(Path.Combine(stwRuntime, "stw-local.txt")));
        {
            string shared = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "ProjectTilted");
            Directory.CreateDirectory(shared);
            string flag = Path.Combine(shared, "stw-local.flag"), go = Path.Combine(shared, "stw-local-go.txt");
            if (File.Exists(go)) File.Delete(go);
            if (stwLocal) File.WriteAllText(flag, DateTime.UtcNow.ToString("o"));
            else if (File.Exists(flag)) File.Delete(flag);
        }
#else
        const bool stwLocal = false;
#endif
        bool syndicateServer = UsesSyndicateServer(build);
        string erbium = syndicateServer
            ? FindSyndicateFile(@"Erbium-Quality-of-life-changes-main\Erbium\x64\Release\Erbium.dll", "Erbium.dll")
            : "";
        string cobalt = syndicateServer ? FindSyndicateFile(@"Launcher\cobalt.dll", "cobalt.dll") : "";
        if (syndicateServer && erbium.Length == 0) throw new LaunchProblemException(LaunchProblem.ModuleMissing("Erbium.dll"));
        if (syndicateServer && cobalt.Length == 0) throw new LaunchProblemException(LaunchProblem.ModuleMissing("cobalt.dll"));
        // Check every DLL this launch injects before Fortnite starts, so a file Windows Security blocked
        // (common for self-built DLLs) is reported in seconds with the fix, not after the game has loaded.
        await Task.Run(() =>
        {
            var modules = syndicateServer
                ? new List<string> { erbium, cobalt }
                : ModuleStartup.RequiredModules.Where(name => name != ModuleStartup.MenuModule).Select(name => Path.Combine(Runtime, "dlls", name)).ToList();
            if (syndicateServer && useClientExtras)
            {
                modules.Add(FindClientModule(ClientModsModule, "TiltedClient.dll"));
                modules.Add(FindClientModule(PadAssistModule, "TiltedPadAssist.dll"));
            }
            if (loadMenuDll) modules.Add(Path.Combine(Runtime, "dlls", ModuleStartup.MenuModule));
            foreach (var module in modules.Where(m => m.Length > 0))
                if (ModuleCheck.Diagnose(module) is { } problem) throw new LaunchProblemException(problem);
        });
        if (!syndicateServer) CompanionProcesses.Validate(build);
        var hostInfo = CreateStartInfo(build, "SyndicateHost", true, "");
        options ??= MatchOptions.Parse("",true);
        options.Apply(hostInfo, true);
        if (IPGlobalProperties.GetIPGlobalProperties().GetActiveUdpListeners().Any(e => e.Port == 7777))
            throw new InvalidOperationException("Port 7777 is already in use by another game server. Close that server first.");
        Directory.CreateDirectory(SettingsService.DataDirectory);
        var sessionDirectory = Path.Combine(SettingsService.DataDirectory, "Logs", DateTime.UtcNow.ToString("yyyyMMdd-HHmmss") + "-" + Guid.NewGuid().ToString("N")[..8]);
        Directory.CreateDirectory(sessionDirectory);
        var hostLog = Path.Combine(sessionDirectory, "host.log");
        var clientLog = Path.Combine(sessionDirectory, "client.log");
        Process? activeGame = null;
        var activeLog = hostLog;
        hostInfo.ArgumentList.Add("-abslog=" + hostLog);
        string syndicateRuntime = syndicateServer ? (SyndicateBackend.RuntimeDirectory ?? "") : "";
        string syndicateReadyFile = Path.Combine(syndicateRuntime, "ready.pid");
        string syndicateRunId = "";
        string studioPlaylist = MatchOptions.NormalizePlaylist(options.Playlist);
        bool studioMatch = false;
        HostLog = hostLog;
        if (syndicateServer)
        {
            if (syndicateRuntime.Length == 0)
                throw new InvalidOperationException("The Syndicate Runtime\\Server folder could not be found next to Syndicate-backend.");
            Directory.CreateDirectory(syndicateRuntime);

            // The matchmaker checks the selected build against the client's build; a stale
            // active-build.json (for example 1.11) rejects 7.40 with errors.com.syndicate.game_version.
            File.WriteAllText(Path.Combine(syndicateRuntime, "active-build.json"),
                JsonSerializer.Serialize(new { Build = build.Version }));
            if (File.Exists(syndicateReadyFile)) File.Delete(syndicateReadyFile);

            syndicateRunId = Guid.NewGuid().ToString("N");
            hostInfo.ArgumentList.Add("-SyndicateServerRun=" + syndicateRunId);
            // Syndicate keeps one persistent UserData folder for its match host. Reusing it keeps
            // the same engine caches/config the working Syndicate install uses.
            var hostUserDir = Path.Combine(syndicateRuntime, "UserData");
            Directory.CreateDirectory(hostUserDir);
            hostInfo.ArgumentList.Add("-UserDir=" + hostUserDir);
            hostInfo.Environment["TILTED_PLAYLIST"] = studioPlaylist;
            hostInfo.Environment["TILTED_BOT_COUNT"] = Math.Clamp(options.MaximumBots, 0, 99).ToString();
            hostInfo.Environment["TILTED_BOT_SKILL"] = SyndicateSkillName(options.Difficulty);
            hostInfo.Environment["TILTED_SERVER_READY_FILE"] = syndicateReadyFile;
            hostInfo.Environment["TILTED_SERVER_LOG"] = Path.Combine(syndicateRuntime, "stdout.log");
            hostInfo.Environment["TILTED_TICK_RATE"] = options.MaximumBots <= 30 || options.HighTickFullLobbies ? "60" : "30";
            // Settings > Bots > Late game: Erbium starts the match in the third circle with a full loadout.
            if (options.LateGame) { hostInfo.Environment["TILTED_LATE_GAME"] = "1"; hostInfo.Environment["TILTED_LATE_GAME_ZONE"] = "4"; }
            else { hostInfo.Environment.Remove("TILTED_LATE_GAME"); hostInfo.Environment.Remove("TILTED_LATE_GAME_ZONE"); }
#if DEV_BUILD
            // Save the World (dev launcher): Erbium answers the STW reservation beacon on port 15009.
            hostInfo.Environment["SYNDICATE_STW_DEV"] = "1";
            // stw-host.txt next to the launcher: Pre-season/Season 1 hosts start on the Save the World tutorial zone.
            if (StwHostWanted()) hostInfo.Environment["SYNDICATE_STW_HOST"] = "1";
#endif
            // Syndicate Studio open: its playlist, dev live data, and for a Studio match no bots and no bus.
            var studio = Studio.StudioSession.Current();
            studioPlaylist = Studio.StudioSession.Apply(hostInfo, studio, message => Record(message));
            studioMatch = studio?.StudioMatch == true;
        }
        void Record(string message)
        {
            RecordFile(message);
            log?.Invoke(message);
        }
        void RecordFile(string message)
        {
            try { File.AppendAllText(Path.Combine(sessionDirectory, "startup.log"), DateTime.UtcNow.ToString("O") + " " + message + Environment.NewLine); }
            catch (IOException) { }
        }
        // 7.40 ignores -abslog when another instance (the player client) is already running,
        // so also read the game's usual Saved\Logs folder, exactly like Syndicate.
        static string ReadSharedText(string path)
        {
            try
            {
                using var stream = new FileStream(path, FileMode.Open, FileAccess.Read,
                    FileShare.ReadWrite | FileShare.Delete);
                const int limit = 2 * 1024 * 1024, head = 256 * 1024;
                long length = stream.Length;
                if (length <= limit)
                {
                    var all = new byte[(int)length];
                    int read = 0, n;
                    while (read < all.Length && (n = stream.Read(all, read, all.Length - read)) > 0) read += n;
                    return System.Text.Encoding.UTF8.GetString(all, 0, read);
                }
                var buffer = new byte[limit];
                int first = stream.Read(buffer, 0, head);
                stream.Seek(Math.Max(first, length - (limit - head)), SeekOrigin.Begin);
                int last = 0, m;
                while (last < limit - head && (m = stream.Read(buffer, head + last, limit - head - last)) > 0) last += m;
                return System.Text.Encoding.UTF8.GetString(buffer, 0, first) + "\n" +
                       System.Text.Encoding.UTF8.GetString(buffer, head, last);
            }
            catch (IOException) { return ""; }
        }

        IEnumerable<string> HostLogCandidates()
        {
            yield return hostLog;
            string local = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),
                "FortniteGame", "Saved", "Logs");
            string shipped = Path.GetFullPath(Path.Combine(Path.GetDirectoryName(build.ExecutablePath)!,
                "..", "..", "Saved", "Logs"));
            foreach (var directory in new[] { local, shipped })
            {
                if (!Directory.Exists(directory)) continue;
                foreach (var file in Directory.EnumerateFiles(directory, "FortniteGame*.log"))
                {
                    string name = Path.GetFileNameWithoutExtension(file);
                    if (name == "FortniteGame") yield return file;
                    else if (name.StartsWith("FortniteGame_", StringComparison.Ordinal) &&
                             int.TryParse(name["FortniteGame_".Length..], out _)) yield return file;
                }
            }
        }

        // Erbium is injected after the host has brought the frontend up (Syndicate does the same);
        // waiting for a player login is wrong for a dedicated host.
        async Task<string?> WaitForFrontendAsync(Process process, string logPath)
        {
            var timer = Stopwatch.StartNew();
            int shownSecond = -1;
            string marker = "-SyndicateServerRun=" + syndicateRunId;
            const string frontendLine = "Bringing World /Game/Maps/Frontend.Frontend up for play";
            string? foundLog = null;

            // 2 = frontend is up; 1 = our host log is readable but the frontend is not up yet; 0 = log not readable.
            int LogState()
            {
                int best = 0;
                foreach (var file in HostLogCandidates())
                {
                    string text = ReadSharedText(file);
                    if (text.Length == 0 || !text.Contains(marker, StringComparison.Ordinal)) continue;
                    best = 1;
                    if (text.Contains(frontendLine, StringComparison.Ordinal))
                    {
                        foundLog = file;
                        return 2;
                    }
                }
                return best;
            }

            while (timer.Elapsed < TimeSpan.FromMinutes(6))
            {
                if (process.HasExited)
                    throw new InvalidOperationException("The Syndicate server host exited before the frontend loaded. Log: " + logPath);
                int logState = LogState();
                if (logState == 2)
                {
                    Record("Frontend found in " + foundLog);
                    return foundLog;
                }

                // Only fall back to the blind timer when the host log is NOT readable (e.g. 7.40 ignores -abslog
                // when a client is already running, so the frontend line can never be seen). When the log IS
                // readable (logState == 1) the server is ours and still loading - keep waiting for the real
                // frontend line. Injecting Erbium before the frontend is up crashes a slow-loading server, which
                // is what made slower machines fail with "server exited while loading".
                if (logState == 0 && timer.Elapsed > TimeSpan.FromSeconds(IsEarlyBuild(build) ? 180 : 60))
                {
                    Record("Host log not readable after the fallback window; loading Erbium on the timer.");
                    return null;
                }

                int second = (int)timer.Elapsed.TotalSeconds;
                if (second != shownSecond)
                {
                    shownSecond = second;
                    status("Preparing the Syndicate server... " + second + "s");
                }
                await Task.Delay(500);
            }
            throw new TimeoutException("The Syndicate server did not bring up the frontend within six minutes. Log: " + logPath);
        }

        async Task Initialize(Process process, bool isHost, string log)
        {
            var role = isHost ? "server" : "player";
            Record(role + " PID " + process.Id);

            if (syndicateServer)
            {
                if (isHost)
                {
                    var frontendLog = await WaitForFrontendAsync(process, log);
                    await SettleBeforeErbiumAsync(process, frontendLog, IsEarlyBuild(build) ? 6000 : 2000, Record);
                    await NativeLoader.LoadAsync(process, erbium, message => { Record(role + " " + message); status(message); }, build.Version);
                    Record(role + " loaded Erbium");
                }
                else
                {
                    // cobalt.dll redirects Fortnite's Epic calls to Syndicate's backend.
                    await Task.Delay(800);
                    if (!process.HasExited)
                        await NativeLoader.LoadAsync(process, cobalt, message => { Record(role + " " + message); status(message); }, build.Version);
                    Record(role + " loaded cobalt");

                    // Syndicate's client modules read the gameplay/display switches (turbo build,
                    // edit on release, FOV, stretched, controller assists) from the same
                    // SyndicateClassic flag folder as Syndicate itself.
                    string clientMods = useClientExtras ? FindClientModule(ClientModsModule, "TiltedClient.dll") : "";
                    if (clientMods.Length > 0 && !process.HasExited)
                    {
                        Record(role + " loading " + Path.GetFileName(clientMods));
                        await NativeLoader.LoadAsync(process, clientMods, message => { Record(role + " " + message); status(message); }, build.Version);
                        Record(role + " loaded " + Path.GetFileName(clientMods));
                    }
                    string padAssist = useClientExtras ? FindClientModule(PadAssistModule, "TiltedPadAssist.dll") : "";
                    if (padAssist.Length > 0 && !process.HasExited)
                    {
                        Record(role + " loading " + Path.GetFileName(padAssist));
                        await NativeLoader.LoadAsync(process, padAssist, message => { Record(role + " " + message); status(message); }, build.Version);
                        Record(role + " loaded " + Path.GetFileName(padAssist));
                    }
                    // The Syndicate menu finds the game's layout at runtime, so the same
                    // SyndicateMenu.dll runs on every Chapter 1 season. Loaded last, after
                    // cobalt.dll (the menu checks for cobalt to skip the Season X-only hooks).
                    // It waits until Fortnite's engine has started: loaded one second into
                    // startup (before the engine made its window and graphics device) it
                    // closed 1.7.2 at once. ESP and aim only ever target bots.
                    if (build.Version == "10.40" && !process.HasExited)
                    {
                        // Keep the 10.40 console and optional menu on the Erbium path.
                        await ModuleStartup.InitializeClientAsync(loadMenuDll, async name =>
                        {
                            Record(role + " loading " + name);
                            await NativeLoader.LoadAsync(process, Path.Combine(Runtime, "dlls", name), message => { Record(role + " " + message); status(message); }, build.Version);
                            Record(role + " loaded " + name);
                        }, () => StartupReadiness.WaitForEngineAsync(process, log, status), () => StartupReadiness.WaitAsync(process, log, status));
                    }
                    else if (loadMenuDll && !process.HasExited)
                    {
                        if (!enableMenu) Record(role + " SyndicateMenu.dll loads for the custom crosshair only (menu off)");
                        await StartupReadiness.WaitForEngineAsync(process, log, status);
                        var menu = Path.Combine(Runtime, "dlls", ModuleStartup.MenuModule);
                        Record(role + " loading " + ModuleStartup.MenuModule);
                        await NativeLoader.LoadAsync(process, menu, message => { Record(role + " " + message); status(message); }, build.Version);
                        Record(role + " loaded " + ModuleStartup.MenuModule);
                    }
                    if (stwLocal && !studioOpen && build.Version is "1.7.2" or "1.8" or "1.8.2" && !process.HasExited)
                    {
                        await StartupReadiness.WaitForEngineAsync(process, log, status);
                        var studioDll = Path.Combine(Runtime, "dlls", "SyndicateStudio.dll");
                        Record(role + " loading SyndicateStudio.dll (Save the World local tutorial)");
                        await NativeLoader.LoadAsync(process, studioDll, message => { Record(role + " " + message); status(message); }, build.Version);
                        Record(role + " loaded SyndicateStudio.dll");
                    }
                }
                return;
            }

            throw new InvalidOperationException("No Erbium startup path is available for this build.");
        }
        File.WriteAllText(Path.Combine(SettingsService.DataDirectory, "launch.json"), JsonSerializer.Serialize(new
        { profile = profile.Name, profileMode = profile.Mode.ToString(), season, buildVersion = build.Version, changelist = build.Changelist, serverBuild = "ResourceMovementFix", launcherVersion = "1.1.18", menuEnabled = enableMenu }));
        try
        {
            string changelist = build.Changelist is int cl ? " CL-" + cl : " (changelist unavailable)";
            Record("Launcher " + typeof(GameLauncher).Assembly.GetName().Version?.ToString(3) + "; Fortnite " + build.Version + changelist);
            if (!useClientExtras) Record("Fortnite 2.5 compatibility: legacy loading option, optional client modules disabled.");
            if (studioOpen) Record("Syndicate Studio is open: SyndicateMenu.dll is off for this session.");
            if (syndicateServer) Record("Bot difficulty: " + SyndicateSkillName(options.Difficulty));
            status("Checking Fortnite's content files...");
            GameInstallation.Validate(build, Runtime, Record);
            status("Starting the local login backend...");
            var exchangeCode = await StartBackendAsync(profile, season, status);
            // Syndicate Achievements: matches played with the Syndicate menu on don't count. The backend has to know
            // before the first match event of this session arrives, so it is set here, at every launch.
            try { await SyndicateBackend.SetMenuAsync(profile.Name, enableMenu); }
            catch (Exception e) { Record("Achievements: could not tell the backend the menu state (" + e.Message + ")."); }
            LastLaunch = (profile.Name, season, enableMenu);
            if (!syndicateServer)
            {
                // The 10.40 host has to sign in to reach the frontend. Syndicate's backend has real accounts,
                // so the old LawinServer-style login (SyndicateHost@projectreboot.dev / Rebooted) is rejected
                // ("invalid_account_credentials") and the host never finished startup. Sign it in like the
                // player: a one-time exchange code for its own local "SyndicateHost" account.
                var hostCode = await SyndicateBackend.ExchangeCodeAsync("SyndicateHost", false, status);
                for (int i = hostInfo.ArgumentList.Count - 1; i >= 0; i--)
                    if (hostInfo.ArgumentList[i].StartsWith("-AUTH_", StringComparison.OrdinalIgnoreCase))
                        hostInfo.ArgumentList.RemoveAt(i);
                hostInfo.ArgumentList.Add("-AUTH_TYPE=exchangecode");
                hostInfo.ArgumentList.Add("-AUTH_LOGIN=unused");
                hostInfo.ArgumentList.Add("-AUTH_PASSWORD=" + hostCode);
                // Syndicate1040 reads the player's saved locker (outfit, pickaxe, glider) from the folder the
                // backend writes for match servers, and reports kills, placement and XP to the progression queue.
                var serverRuntime = SyndicateBackend.RuntimeDirectory;
                if (serverRuntime is not null)
                {
                    hostInfo.Environment["SYNDICATE_LOCKER_DIR"] = Path.Combine(serverRuntime, "locker");
                    hostInfo.Environment["SYNDICATE_SERVER_RUNTIME"] = serverRuntime;
                }
            }
            var clientInfo = CreateStartInfo(build, profile.Name, false, extraArguments, exchangeCode);
            clientInfo.ArgumentList.Add("-abslog=" + clientLog);
            options.Apply(clientInfo, false);
            status(syndicateServer ? "Starting the Syndicate server..." : "Starting the Syndicate 10.40 server...");
            if (!syndicateServer || NeedsEpicStub(build))
                CompanionProcesses.Start(build, syndicateServer ? EpicCompanionArgs : "", process => { Owned.Add(process); Record("server companion " + process.ProcessName + " PID " + process.Id); });
            // Every host starts on a hidden desktop like in Syndicate, so its window never flashes up.
            // The 10.40 host still renders there (it runs windowed with t.IdleWhenNotForeground 0, since a
            // window on a hidden desktop is never in the foreground); ServerWindowGuard stays as a backstop.
#if DEV_BUILD
            // Save the World tutorial host: the zone asks the backend for the player's profile through its own
            // signed-in account (local user 0); without one every request fails ("Invalid user auth ... Need to
            // login first") and the player is sent back to the menu. Erbium signs the host in with this code.
            if (syndicateServer && hostInfo.Environment.TryGetValue("SYNDICATE_STW_HOST", out var stwHost) && stwHost == "1")
            {
                var stwCode = await SyndicateBackend.ExchangeCodeAsync("SyndicateHost", false, status);
                for (int i = hostInfo.ArgumentList.Count - 1; i >= 0; i--)
                    if (hostInfo.ArgumentList[i].StartsWith("-AUTH_", StringComparison.OrdinalIgnoreCase))
                        hostInfo.ArgumentList.RemoveAt(i);
                // AutoLogin (unlike the client's login screen) may read the code from either field: give it both.
                hostInfo.ArgumentList.Add("-AUTH_TYPE=exchangecode");
                hostInfo.ArgumentList.Add("-AUTH_LOGIN=" + stwCode);
                hostInfo.ArgumentList.Add("-AUTH_PASSWORD=" + stwCode);
            }
#endif
            var host = StartOwnedHidden(hostInfo); activeGame = host;
            if (syndicateServer)
            {
                File.WriteAllText(Path.Combine(syndicateRuntime, "server.json"), JsonSerializer.Serialize(new
                {
                    Pid = host.Id,
                    Started = host.StartTime.ToUniversalTime().Ticks,
                    Mode = studioPlaylist,
                    Build = build.Version,
                    Skill = studioMatch ? "" : SyndicateSkillName(options.Difficulty),
                    Studio = studioMatch,
                    Port = HostSlot.A.Port,
                    Ready = HostSlot.A.ReadyName
                }));
            }
            _ = ServerWindowGuard.KeepHiddenAsync(host, System.Threading.CancellationToken.None);
            async Task WaitJoinableAsync()
            {
                await WaitForIslandAsync(host, syndicateReadyFile, syndicateServer ? syndicateRuntime : null, sessionDirectory);
                await StartupReadiness.SaveLatestAsync(host, hostLog);
            }
            Process client;
            Task clientInit;
            if (syndicateServer)
            {
                // Syndicate brings the match host up, injects Erbium and waits for the island to
                // be joinable before it launches the player. Starting both at once let the
                // client connect while the 99 bot pawns were still being created, which left
                // a pawn in a state the game's own landing handler crashed on.
                await Initialize(host, true, hostLog);
                // Instant play: start Fortnite as soon as Erbium is in the host, so the game boots to the lobby while
                // the island and its bots load, instead of after. The client can't join early: the matchmaker only
                // hands out a session once the host has written its ready file (Matchmaker/playlists.cjs ready()),
                // so Ready pressed during the load just stays "Queued" and joins the moment the match is ready.
                bool instant = InstantPlayAllowed(options, studioOpen, stwLocal, useClientExtras, Record);
                Task joinable = Task.CompletedTask;
                if (instant)
                {
                    Record("instant play: starting Fortnite while Erbium loads the island");
                    joinable = WaitJoinableAsync();
                    status("Starting Fortnite while the match loads...");
                }
                else
                {
                    status("Erbium is loading the island...");
                    await WaitJoinableAsync();
                    status("Starting Fortnite...");
                }
                if (NeedsEpicStub(build))
                    CompanionProcesses.Start(build, EpicCompanionArgs, process => { Owned.Add(process); Record("player companion " + process.ProcessName + " PID " + process.Id); });
                client = StartOwned(clientInfo); Client = client; activeGame = client; activeLog = clientLog;
                clientInit = Initialize(client, false, clientLog);
                InputMods.Attach(client.Id, season);
                async Task ClientUpAsync()
                {
                    await clientInit;
                    // cobalt's page-guard hook can let one of the concurrent login requests reach Epic. On Chapter 1
                    // that showed up as "Profile Query Failed" (QueryProfile 401 "Token is missing key ID value") and
                    // the player had to press Play again. Watch the login like 10.40 does, so the retry in LaunchAsync
                    // starts a fresh session by itself.
                    status("Waiting for local login...");
                    await StartupReadiness.WatchLoginAsync(client, clientLog, TimeSpan.FromSeconds(120));
                }
                var clientUp = ClientUpAsync();
                // Whichever fails first is reported at once (a host crash must not wait for the client's login).
                await await Task.WhenAny(clientUp, joinable);
                await clientUp;
                if (!joinable.IsCompleted)
                {
                    status("Fortnite is ready. The match is still loading: press Play now and you'll join as soon as it's done.");
                    var waited = Stopwatch.StartNew();
                    await joinable;
                    Record("instant play: island ready " + waited.Elapsed.TotalSeconds.ToString("0.0") + " s after the lobby");
                }
                else if (instant) Record("instant play: island was ready before the lobby");
            }
            else
            {
                await Initialize(host, true, hostLog);
                status("Starting Fortnite 10.40...");
                CompanionProcesses.Start(build, "", process => { Owned.Add(process); Record("player companion " + process.ProcessName + " PID " + process.Id); });
                client = StartOwned(clientInfo); Client = client; activeGame = client; activeLog = clientLog;
                clientInit = Initialize(client, false, clientLog);
                InputMods.Attach(client.Id, season);
                status("Syndicate is loading the map. Waiting for port 7777...");
                await WaitJoinableAsync();
                await clientInit;
                if (client.HasExited)
                {
                    await StartupReadiness.SaveLatestAsync(client, clientLog);
                    throw new InvalidOperationException(StartupFailure.ReadDiagnosis(clientLog) ??
                        ("Fortnite closed while starting (0x" + client.ExitCode.ToString("X8") + "). Log: " + clientLog));
                }
            }
            status("Ready. Press Play in the main lobby to join your local match.");
            if (syndicateServer)
                _ = WatchSessionAsync(client, host, build, options, sessionDirectory, syndicateRuntime, syndicateReadyFile, status, Record);
            else
                _ = ObserveSessionAsync(client, host, status, Record, clientLog);
        }
        catch (Exception error)
        {
            if (activeGame is not null) await StartupReadiness.SaveLatestAsync(activeGame, activeLog);
            // The full exception goes to startup.log only; Diagnostics gets a short title and the fix.
            RecordFile("Failed: " + error); StopOwned();
            var diagnosis = StartupFailure.ReadDiagnosis(activeLog);
            var problem = error is LaunchProblemException known ? known.Problem
                : error is LoginAbortedException ? LaunchProblem.LoginScreen()
                : LaunchProblem.From(diagnosis is null ? error : new InvalidOperationException(diagnosis));
            throw new LaunchProblemException(problem.WithLogs(sessionDirectory), error);
        }
    }
    private static string ReadSharedFile(string path)
    {
        try
        {
            using var stream = new FileStream(path, FileMode.Open, FileAccess.Read,
                FileShare.ReadWrite | FileShare.Delete);
            const int limit = 2 * 1024 * 1024, head = 256 * 1024;
            long length = stream.Length;
            if (length <= limit)
            {
                var all = new byte[(int)length];
                int read = 0, n;
                while (read < all.Length && (n = stream.Read(all, read, all.Length - read)) > 0) read += n;
                return System.Text.Encoding.UTF8.GetString(all, 0, read);
            }
            var buffer = new byte[limit];
            int first = stream.Read(buffer, 0, head);
            stream.Seek(Math.Max(first, length - (limit - head)), SeekOrigin.Begin);
            int last = 0, m;
            while (last < limit - head && (m = stream.Read(buffer, head + last, limit - head - last)) > 0) last += m;
            return System.Text.Encoding.UTF8.GetString(buffer, 0, first) + "\n" +
                   System.Text.Encoding.UTF8.GetString(buffer, head, last);
        }
        catch (IOException) { return ""; }
    }

    private static IEnumerable<string> HostLogCandidatesFor(string requestedLog, BuildInfo build)
    {
        yield return requestedLog;
        string local = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),
            "FortniteGame", "Saved", "Logs");
        string shipped = Path.GetFullPath(Path.Combine(Path.GetDirectoryName(build.ExecutablePath)!,
            "..", "..", "Saved", "Logs"));
        foreach (var directory in new[] { local, shipped })
        {
            if (!Directory.Exists(directory)) continue;
            foreach (var file in Directory.EnumerateFiles(directory, "FortniteGame*.log")
                         .OrderByDescending(File.GetLastWriteTimeUtc))
            {
                string name = Path.GetFileNameWithoutExtension(file);
                if (name == "FortniteGame") yield return file;
                else if (name.StartsWith("FortniteGame_", StringComparison.Ordinal) &&
                         int.TryParse(name["FortniteGame_".Length..], out _)) yield return file;
            }
        }
    }

    /// <summary>
    /// The frontend line only means the menu world exists: Fortnite then streams the lobby levels in for several
    /// more seconds (Frontend_Lobby_Layout, event menu maps, "Flushing async loaders"). Loading Erbium during that
    /// crashed the server while it hooked (0xC0000005 reading 0x461: [HOST-ISLAND-01] on 8.50, and a background
    /// server on 7.40 while a match was running). Wait for <paramref name="minimumMs"/>, then until the host log has
    /// had no level streaming for 3 s (at most 45 s). Without a readable log only the minimum applies.
    /// It also waits (at most 2 minutes) until the game has finished its first frame: on slow or low-memory PCs the
    /// frontend's first frame (login and splash UI) ran for 20 s and more after the lobby looked loaded, and Erbium
    /// hooking and opening the island inside it crashed the server ([HOST-ISLAND-01] 0xC0000005 on 3.5.1 and 8.50).
    /// </summary>
    private static async Task SettleBeforeErbiumAsync(Process process, string? log, int minimumMs, Action<string> record)
    {
        var timer = Stopwatch.StartNew();
        await Task.Delay(minimumMs);
        if (string.IsNullOrEmpty(log)) return;
        const string frontendLine = "Bringing World /Game/Maps/Frontend.Frontend up for play";
        static int Count(string text, string what, int from)
        {
            int n = 0;
            for (int at = text.IndexOf(what, from, StringComparison.Ordinal); at >= 0; at = text.IndexOf(what, at + what.Length, StringComparison.Ordinal)) n++;
            return n;
        }
        // Log lines start with "[time][frame]"; the frame number stays 0 until the first frame is over.
        var anyFrame = new Regex(@"^\[[^\]\r\n]*\]\[\s*\d+\]", RegexOptions.Multiline);
        var laterFrame = new Regex(@"^\[[^\]\r\n]*\]\[\s*[1-9]\d*\]", RegexOptions.Multiline);
        int last = -1;
        bool toldFirstFrame = false;
        var quiet = Stopwatch.StartNew();
        while (timer.Elapsed < TimeSpan.FromSeconds(120))
        {
            if (process.HasExited) return;
            string text = ReadSharedFile(log);
            int from = text.LastIndexOf(frontendLine, StringComparison.Ordinal);
            if (from < 0) from = 0;
            int streaming = Count(text, "LogStreaming", from) + Count(text, "ActivateLevel ", from) + Count(text, "LogLevel:", from);
            string after = text[from..];
            bool ticking = laterFrame.IsMatch(after) || !anyFrame.IsMatch(after);
            bool streamed = false;
            if (streaming != last) { last = streaming; quiet.Restart(); }
            else if (quiet.Elapsed >= TimeSpan.FromSeconds(3)) streamed = true;
            if ((streamed || timer.Elapsed >= TimeSpan.FromSeconds(45)) && ticking)
            {
                record((streamed ? "Lobby finished streaming in " : "Lobby still streaming after ")
                    + timer.Elapsed.TotalSeconds.ToString("0.0") + " s; loading Erbium.");
                return;
            }
            if (streamed && !ticking && !toldFirstFrame)
            {
                toldFirstFrame = true;
                record("Lobby streamed, but the server is still in its first frame; waiting before loading Erbium.");
            }
            await Task.Delay(250);
        }
        record("Server still busy after 120 s; loading Erbium anyway.");
    }

    private static async Task<string?> WaitForFrontendStaticAsync(Process process, string logPath, string runId,
        BuildInfo build, Action<string> status, Action<string> record)
    {
        var timer = Stopwatch.StartNew();
        string marker = "-SyndicateServerRun=" + runId;
        const string frontendLine = "Bringing World /Game/Maps/Frontend.Frontend up for play";
        string? foundLog = null;

        // 2 = frontend is up; 1 = our host log is readable but the frontend is not up yet; 0 = log not readable.
        int LogState()
        {
            int best = 0;
            foreach (var file in HostLogCandidatesFor(logPath, build))
            {
                string text = ReadSharedFile(file);
                if (text.Length == 0 || !text.Contains(marker, StringComparison.Ordinal)) continue;
                best = 1;
                if (text.Contains(frontendLine, StringComparison.Ordinal))
                {
                    foundLog = file;
                    return 2;
                }
            }
            return best;
        }

        while (timer.Elapsed < TimeSpan.FromMinutes(6))
        {
            if (process.HasExited)
                throw new InvalidOperationException("The Syndicate server host exited before the frontend loaded. Log: " + logPath);
            int logState = LogState();
            if (logState == 2)
            {
                record("Frontend found in " + foundLog);
                return foundLog;
            }
            // Only fall back to the blind timer when the host log is NOT readable. When it IS readable
            // (logState == 1) the server is ours and still loading - keep waiting for the real frontend line,
            // because injecting Erbium before the frontend is up crashes a slow-loading server.
            if (logState == 0 && timer.Elapsed > TimeSpan.FromSeconds(IsEarlyBuild(build) ? 180 : 60))
            {
                record("Host log not readable after the fallback window; loading Erbium on the timer.");
                return null;
            }
            await Task.Delay(500);
        }
        throw new TimeoutException("The Syndicate server did not bring up the frontend within six minutes. Log: " + logPath);
    }

    private static string? SaveErbiumStartupLogs(int pid, string runtime, string sessionDirectory)
    {
        string source = Path.Combine(runtime, "stdout.log." + pid + ".log");
        string destination = Path.Combine(sessionDirectory, "erbium-" + pid);
        string? crashCode = null;
        foreach (var (input, output) in new[] { (source, destination + ".log"), (source + ".stderr.log", destination + ".stderr.log") })
        {
            try
            {
                string log = ReadSharedFile(input);
                if (log.Length == 0) continue;
                var crash = Regex.Match(log, @"\[CrashReporter\] ORIGINAL code=(0x[0-9A-Fa-f]{8})\b");
                if (crash.Success) crashCode ??= crash.Groups[1].Value;
                File.WriteAllText(output, log);
            }
            catch (IOException) { }
            catch (UnauthorizedAccessException) { }
        }
        return crashCode;
    }

    private static async Task WaitForIslandAsync(Process host, string readyFile, string? syndicateRuntime, string sessionDirectory, int port = 7777)
    {
        bool syndicate = syndicateRuntime is not null;
        var waiting = Stopwatch.StartNew();
        while (true)
        {
            if (host.HasExited)
            {
                string exitCode = await ExitCodeTextAsync(host);
                if (syndicateRuntime is not null)
                {
                    string? crashCode = SaveErbiumStartupLogs(host.Id, syndicateRuntime, sessionDirectory);
                    throw new LaunchProblemException(LaunchProblem.IslandLoadFailed(exitCode, crashCode).WithLogs(sessionDirectory));
                }
                throw new InvalidOperationException("The 10.40 game server exited while loading (" + exitCode + "). Logs: " + sessionDirectory);
            }
            if (ServerPort.IsOwnedBy(host.Id, port)) return;
            if (syndicate)
            {
                try
                {
                    if (File.Exists(readyFile) && File.ReadAllText(readyFile).Trim() == host.Id.ToString()) return;
                }
                catch (IOException) { }
            }
            if (waiting.Elapsed > TimeSpan.FromSeconds(syndicate ? 300 : 180))
            {
                if (syndicateRuntime is not null)
                {
                    SaveErbiumStartupLogs(host.Id, syndicateRuntime, sessionDirectory);
                    throw new LaunchProblemException(LaunchProblem.IslandLoadFailed(null, null).WithLogs(sessionDirectory));
                }
                throw new TimeoutException("The 10.40 server did not become joinable in time. Logs: " + sessionDirectory);
            }
            await Task.Delay(500);
        }
    }

    private static bool PortInUse(int port)
    {
        var properties = IPGlobalProperties.GetIPGlobalProperties();
        return properties.GetActiveUdpListeners().Any(e => e.Port == port) ||
               properties.GetActiveTcpListeners().Any(e => e.Port == port);
    }

    internal static async Task<Process> StartHostOnlyAsync(BuildInfo build, MatchOptions options,
        string sessionDirectory, string syndicateRuntime, Action<string> status, Action<string> record, int hostIndex)
        => await StartHostOnlyAsync(build, options, sessionDirectory, syndicateRuntime, status, record, hostIndex, HostSlot.A, false, null);

    /// <param name="slot">Game port and ready file. The live server and the fast-queue background server use different ones.</param>
    /// <param name="standby">Fast queue: load the next match in the background. It does not touch server.json (the live
    /// match keeps it), runs at a lower priority and publishes its state to occupancy.&lt;pid&gt;.json until promoted.</param>
    /// <param name="started">Called with the new process before loading starts, so a background load can be cancelled.</param>
    private static async Task<Process> StartHostOnlyAsync(BuildInfo build, MatchOptions options,
        string sessionDirectory, string syndicateRuntime, Action<string> status, Action<string> record, int hostIndex,
        HostSlot slot, bool standby, Action<Process>? started)
    {
        string erbium = FindSyndicateFile(@"Erbium-Quality-of-life-changes-main\Erbium\x64\Release\Erbium.dll", "Erbium.dll");
        if (erbium.Length == 0)
            throw new InvalidOperationException("Erbium.dll is missing. Install Syndicate or copy Erbium.dll into Runtime\\dlls.");

        string hostLog = Path.Combine(sessionDirectory, hostIndex <= 0 ? "host.log" : "host-" + hostIndex + ".log");
        string readyFile = Path.Combine(syndicateRuntime, slot.ReadyName);
        var hostInfo = CreateStartInfo(build, "SyndicateHost", true, "");
        options.Apply(hostInfo, true);
        hostInfo.ArgumentList.Add("-abslog=" + hostLog);

        if (!standby)
            File.WriteAllText(Path.Combine(syndicateRuntime, "active-build.json"),
                JsonSerializer.Serialize(new { Build = build.Version }));
        if (File.Exists(readyFile)) File.Delete(readyFile);
        hostInfo.Environment["TILTED_SERVER_PORT"] = slot.Port.ToString();
        if (standby)
        {
            hostInfo.Environment["TILTED_SERVER_STANDBY"] = "1";
            // The player's own match comes first while this one waits; promotion raises it back to above normal.
            hostInfo.Environment["TILTED_SERVER_PRIORITY"] = "below";
        }
        else
        {
            hostInfo.Environment.Remove("TILTED_SERVER_STANDBY");
            hostInfo.Environment.Remove("TILTED_SERVER_PRIORITY");
        }

        string runId = Guid.NewGuid().ToString("N");
        hostInfo.ArgumentList.Add("-SyndicateServerRun=" + runId);
        string hostUserDir = Path.Combine(syndicateRuntime, "UserData");
        Directory.CreateDirectory(hostUserDir);
        hostInfo.ArgumentList.Add("-UserDir=" + hostUserDir);
        hostInfo.Environment["TILTED_PLAYLIST"] = MatchOptions.NormalizePlaylist(options.Playlist);
        hostInfo.Environment["TILTED_BOT_COUNT"] = Math.Clamp(options.MaximumBots, 0, 99).ToString();
        hostInfo.Environment["TILTED_BOT_SKILL"] = SyndicateSkillName(options.Difficulty);
        hostInfo.Environment["TILTED_SERVER_READY_FILE"] = readyFile;
        hostInfo.Environment["TILTED_SERVER_LOG"] = Path.Combine(syndicateRuntime, "stdout.log");
        hostInfo.Environment["TILTED_TICK_RATE"] =
            options.MaximumBots <= 30 || options.HighTickFullLobbies ? "60" : "30";
        if (options.LateGame) { hostInfo.Environment["TILTED_LATE_GAME"] = "1"; hostInfo.Environment["TILTED_LATE_GAME_ZONE"] = "4"; }
        else { hostInfo.Environment.Remove("TILTED_LATE_GAME"); hostInfo.Environment.Remove("TILTED_LATE_GAME_ZONE"); }
#if DEV_BUILD
        // Save the World (dev launcher): Erbium answers the STW reservation beacon on port 15009.
        hostInfo.Environment["SYNDICATE_STW_DEV"] = "1";
        if (StwHostWanted()) hostInfo.Environment["SYNDICATE_STW_HOST"] = "1";
#endif
        var studio = standby ? null : Studio.StudioSession.Current();
        string playlist = Studio.StudioSession.Apply(hostInfo, studio, record);
        if (!standby) HostLog = hostLog;

        var host = StartOwnedHidden(hostInfo);
        started?.Invoke(host);
        if (!standby)
            WriteHostState(syndicateRuntime, host, playlist, build, studio?.StudioMatch == true ? "" : SyndicateSkillName(options.Difficulty),
                studio?.StudioMatch == true, slot);
        _ = ServerWindowGuard.KeepHiddenAsync(host, System.Threading.CancellationToken.None);
        record((standby ? "background host PID " : "next host PID ") + host.Id + " (port " + slot.Port + ")");

        var frontendLog = await WaitForFrontendStaticAsync(host, hostLog, runId, build, status, record);
        // A background server loads while the player's match uses the CPU; give it a longer minimum on top.
        await SettleBeforeErbiumAsync(host, frontendLog, standby ? 8000 : IsEarlyBuild(build) ? 6000 : 2000, record);
        await NativeLoader.LoadAsync(host, erbium, message => { record("server " + message); status(message); }, build.Version);
        record("server loaded Erbium");
        try { await WaitForIslandAsync(host, readyFile, syndicateRuntime, sessionDirectory, slot.Port); }
        catch (LaunchProblemException)
        {
            try { if (!host.HasExited) host.Kill(entireProcessTree: true); }
            catch (InvalidOperationException) { } catch (System.ComponentModel.Win32Exception) { }
            await StartupReadiness.SaveLatestAsync(host, hostLog);
            throw;
        }
        return host;
    }

    /// <summary>Game port and ready file of a match server. Fast queue alternates between the two, so the next match
    /// can load in the background while the current one is still using its port.</summary>
    private sealed record HostSlot(int Port, string ReadyName)
    {
        public static readonly HostSlot A = new(7777, "ready.pid");
        public static readonly HostSlot B = new(7778, "ready-b.pid");
        public HostSlot Other => Port == A.Port ? B : A;
    }

    /// <summary>server.json names the live match server for the matchmaker, the backend (game port), Erbium (a
    /// background server is promoted when it finds its PID here) and Discord. Written whole, then swapped in.</summary>
    private static void WriteHostState(string runtime, Process host, string mode, BuildInfo build, string skill, bool studio, HostSlot slot)
    {
        string file = Path.Combine(runtime, "server.json"), temp = file + "." + Guid.NewGuid().ToString("N") + ".tmp";
        File.WriteAllText(temp, JsonSerializer.Serialize(new
        {
            Pid = host.Id,
            Started = host.StartTime.ToUniversalTime().Ticks,
            Mode = mode,
            Build = build.Version,
            Skill = skill,
            Studio = studio,
            Port = slot.Port,
            Ready = slot.ReadyName
        }));
        // The backend and Erbium read it all the time; retry the swap briefly if a reader has it open.
        for (int attempt = 0; ; attempt++)
        {
            try { File.Move(temp, file, true); return; }
            catch (Exception e) when ((e is IOException or UnauthorizedAccessException) && attempt < 20) { System.Threading.Thread.Sleep(25); }
        }
    }

    [System.Runtime.InteropServices.StructLayout(System.Runtime.InteropServices.LayoutKind.Sequential)]
    private struct MemoryStatus
    {
        public uint Length, MemoryLoad;
        public ulong TotalPhys, AvailPhys, TotalPageFile, AvailPageFile, TotalVirtual, AvailVirtual, AvailExtendedVirtual;
    }
    [System.Runtime.InteropServices.DllImport("kernel32.dll", SetLastError = true)]
    private static extern bool GlobalMemoryStatusEx(ref MemoryStatus status);

    /// <summary>Fast queue runs a second match server (about 2-3 GB on Chapter 1). Only on PCs with 16 GB or more.
    /// While the player is still alive in their match it needs 3 GB free, so their own game is not pushed into the
    /// page file mid-fight. Once they are eliminated (spectating or on the elimination screen) 2 GB is enough.
    /// It used to need 5 GB: a 16 GB PC running Fortnite and the match server has about 3 GB free, so fast queue
    /// never ran there and every requeue waited for a whole new server to boot after the player left.</summary>
    private static bool EnoughMemoryForBackgroundHost(bool playerEliminated, out string detail)
    {
        var memory = new MemoryStatus { Length = (uint)System.Runtime.InteropServices.Marshal.SizeOf<MemoryStatus>() };
        if (!GlobalMemoryStatusEx(ref memory)) { detail = "memory size unknown"; return false; }
        const ulong GB = 1024UL * 1024 * 1024;
        detail = (memory.TotalPhys / (double)GB).ToString("0.0") + " GB total, " + (memory.AvailPhys / (double)GB).ToString("0.0") + " GB free";
        return memory.TotalPhys >= 15 * GB && memory.AvailPhys >= (playerEliminated ? 2UL : 3UL) * GB;
    }

    /// <summary>Instant play (part of Settings &gt; Bots &gt; Fast queue): Fortnite starts while the match server loads
    /// the island. Both games end up running anyway; loading them together only needs enough memory and cores so the
    /// match server isn't slowed down. Off in Studio, the local Save the World tutorial and Fortnite 2.5's legacy mode.</summary>
    private static bool InstantPlayAllowed(MatchOptions options, bool studioOpen, bool stwLocal, bool useClientExtras, Action<string> record)
    {
        if (!options.FastQueue || studioOpen || stwLocal || !useClientExtras) return false;
        var memory = new MemoryStatus { Length = (uint)System.Runtime.InteropServices.Marshal.SizeOf<MemoryStatus>() };
        if (!GlobalMemoryStatusEx(ref memory)) { record("instant play: skipped, memory size unknown"); return false; }
        const ulong GB = 1024UL * 1024 * 1024;
        // 12 GB PCs report about 11.9 GB.
        if (memory.TotalPhys < 11 * GB || memory.AvailPhys < 5 * GB || Environment.ProcessorCount < 4)
        {
            record("instant play: skipped (" + (memory.TotalPhys / (double)GB).ToString("0.0") + " GB total, "
                + (memory.AvailPhys / (double)GB).ToString("0.0") + " GB free, " + Environment.ProcessorCount + " CPU threads)");
            return false;
        }
        return true;
    }

    private static Occupancy? ReadOccupancyFile(string file, int pid, TimeSpan fresh)
    {
        try
        {
            if (!File.Exists(file) || DateTime.UtcNow - File.GetLastWriteTimeUtc(file) > fresh) return null;
            var live = JsonSerializer.Deserialize<Occupancy>(ReadSharedFile(file));
            return live is not null && live.Pid == pid ? live : null;
        }
        catch (Exception e) when (e is IOException or JsonException or UnauthorizedAccessException) { return null; }
    }

    private sealed record PlaylistRequest(string Id, string Mode, long Expires, string? Build);
    private sealed record HostState(int Pid, long Started, string? Mode, string? Build);
    private sealed record Occupancy(int Pid, int Connected, bool Accepting, bool Ended = false, int HumansAlive = -1);

    private static HostState? ReadHostState(string path)
    {
        try { return JsonSerializer.Deserialize<HostState>(ReadSharedFile(path)); }
        catch (JsonException) { return null; }
    }

    private static bool HostCanTakeMatch(string runtime, int pid)
    {
        try
        {
            var live = JsonSerializer.Deserialize<Occupancy>(ReadSharedFile(Path.Combine(runtime, "occupancy.json")));
            return live is null || live.Pid != pid || (live.Accepting && !live.Ended);
        }
        catch (JsonException) { return true; }
    }

    private static bool HostHasPlayers(string runtime, int pid)
    {
        try
        {
            string file = Path.Combine(runtime, "occupancy.json");
            if (!File.Exists(file)) return false;
            var live = JsonSerializer.Deserialize<Occupancy>(ReadSharedFile(file));
            return live is null || (live.Pid == pid && live.Connected > 0);
        }
        catch (JsonException) { return true; } // unreadable: never stop a host that might have players
    }

    private static void WritePlaylistError(string path, string id, string message)
    {
        try
        {
            string temp = path + ".tmp";
            File.WriteAllText(temp, JsonSerializer.Serialize(new { Id = id, Message = message }));
            File.Move(temp, path, true);
        }
        catch (IOException) { }
    }

    private static async Task WatchSessionAsync(Process client, Process host, BuildInfo build,
        MatchOptions options, string sessionDirectory, string syndicateRuntime, string readyFile,
        Action<string> status, Action<string> record)
    {
        var currentHost = host;
        var currentSlot = HostSlot.A;
        string restartFile = Path.Combine(syndicateRuntime, "restart.request");
        string requestFile = Path.Combine(syndicateRuntime, "playlist-request.json");
        string stateFile = Path.Combine(syndicateRuntime, "server.json");
        string errorFile = Path.Combine(syndicateRuntime, "playlist-error.json");
        string occupancyFile = Path.Combine(syndicateRuntime, "occupancy.json");
        string handledRequest = "";
        int hostIndex = 1;
        int failures = 0;

        // Fast queue (Settings > Bots). Titanium's Season X server never closes: a finished match is reset inside the
        // same process, so Ready joins in about a second. Erbium has to start a fresh server for every match (20-40 s
        // and more on old seasons), and the player used to wait for that in the queue. Now, as soon as the player is in
        // a match, the next match server loads in the background on the other game port (7777/7778). When the match
        // ends (or the player picks that playlist again), server.json is pointed at it and the old server is closed:
        // Ready finds a loaded server and the player is on the warm-up island in seconds. Anything unexpected
        // (no memory, load failed, playlist changed, background server not joinable) falls back to the old restart.
        Process? standby = null;
        Task<Process>? standbyTask = null;
        var standbySlot = HostSlot.B;
        string standbyMode = "", standbyLog = "";
        int standbyTriedFor = 0, standbyTries = 0; // the live server the background server is for, and loads tried for it (2 at most)
        // Not enough memory does not use up a try: it is checked again every 10 s (memory frees up as the player's
        // game settles, and the bar is lower once they are eliminated), logged once per live server.
        var memoryCheck = Stopwatch.StartNew();
        int memoryLoggedFor = 0;
        foreach (var stale in SafeFiles(syndicateRuntime, "occupancy.*.json")) TryDelete(stale);
        TryDelete(Path.Combine(syndicateRuntime, HostSlot.B.ReadyName));

        void DropStandby(string why)
        {
            if (standby is null && standbyTask is null) return;
            record("fast queue: background server closed (" + why + ")");
            var process = standby; var task = standbyTask;
            standby = null; standbyTask = null;
            try { if (process is not null && !process.HasExited) { process.Kill(entireProcessTree: true); process.WaitForExit(10000); } }
            catch (InvalidOperationException) { } catch (System.ComponentModel.Win32Exception) { }
            // A load that is still running ends with "exited while loading"; nothing waits for it any more.
            task?.ContinueWith(t => _ = t.Exception, TaskScheduler.Default);
            if (process is not null) TryDelete(Path.Combine(syndicateRuntime, "occupancy." + process.Id + ".json"));
            TryDelete(Path.Combine(syndicateRuntime, standbySlot.ReadyName));
        }

        bool StandbyReady(string mode)
        {
            if (standbyTask is not { IsCompletedSuccessfully: true } || standby is null || standby.HasExited) return false;
            if (!string.Equals(standbyMode, mode, StringComparison.OrdinalIgnoreCase)) return false;
            try { if (File.ReadAllText(Path.Combine(syndicateRuntime, standbySlot.ReadyName)).Trim() != standby.Id.ToString()) return false; }
            catch (IOException) { return false; }
            var live = ReadOccupancyFile(Path.Combine(syndicateRuntime, "occupancy." + standby.Id + ".json"), standby.Id, TimeSpan.FromSeconds(4));
            return live is not null && live.Accepting && !live.Ended && live.Connected == 0;
        }

        // Point the matchmaker, backend and Erbium at the background server, then close the old one.
        bool PromoteStandby(string why)
        {
            var next = standby!; var old = currentHost; var oldSlot = currentSlot;
            try { WriteHostState(syndicateRuntime, next, standbyMode, build, SyndicateSkillName(options.Difficulty), false, standbySlot); }
            catch (Exception e) when (e is IOException or UnauthorizedAccessException)
            {
                DropStandby("server.json could not be updated: " + e.Message);
                return false;
            }
            try { next.PriorityClass = ProcessPriorityClass.AboveNormal; }
            catch (InvalidOperationException) { } catch (System.ComponentModel.Win32Exception) { }
            currentHost = next; currentSlot = standbySlot;
            standby = null; standbyTask = null;
            HostLog = standbyLog;
            try { if (!old.HasExited) { old.Kill(entireProcessTree: true); old.WaitForExit(10000); } }
            catch (InvalidOperationException) { } catch (System.ComponentModel.Win32Exception) { }
            TryDelete(restartFile);
            if (oldSlot.ReadyName != currentSlot.ReadyName) TryDelete(Path.Combine(syndicateRuntime, oldSlot.ReadyName));
            TryDelete(Path.Combine(syndicateRuntime, "occupancy." + next.Id + ".json"));
            TryDelete(errorFile);
            failures = 0;
            record("fast queue: " + why + "; switched to the background " + standbyMode + " server (PID " + next.Id + ", port " + currentSlot.Port + ")");
            return true;
        }

        bool StandbyLoadingFor(string mode) =>
            standbyTask is { IsCompleted: false } && string.Equals(standbyMode, mode, StringComparison.OrdinalIgnoreCase);

        // Ready pressed (or the match ended) while the background server is still loading the same playlist:
        // finishing that load is quicker than closing it and starting another server from scratch.
        async Task<bool> WaitForStandbyAsync()
        {
            status("Preparing your next match...");
            record("fast queue: waiting for the background server to finish loading");
            try { await standbyTask!; } catch (Exception) { }
            // The load counts as finished once the server owns its game port, which is a moment before it writes
            // its ready file and first occupancy file. Checking right away found it "not ready", closed it and
            // booted a whole new server (about 30 s) when the player left a match soon after the last switch.
            for (int i = 0; i < 60 && standbyTask is { IsCompletedSuccessfully: true } && standby is { HasExited: false }
                            && !StandbyReady(standbyMode); i++)
                await Task.Delay(250);
            return true;
        }

        while (true)
        {
            if (client.HasExited) break;

            // A background load that failed is closed; it is tried once more for this match.
            if (standbyTask is { IsCompleted: true, IsCompletedSuccessfully: false })
                DropStandby("load failed: " + (standbyTask.Exception?.GetBaseException().Message ?? "cancelled"));

            // The player picked a playlist in Fortnite's lobby. The matchmaker writes
            // playlist-request.json; Erbium can only run the playlist it was started with, so
            // stop the empty host and start a new one for the requested mode. This is what
            // makes Duos and Squads work outside Studio.
            PlaylistRequest? request = null;
            try { request = JsonSerializer.Deserialize<PlaylistRequest>(ReadSharedFile(requestFile)); }
            catch (JsonException) { }
            if (request is not null && request.Id is { Length: > 0 } && request.Id != handledRequest
                && (request.Build is null || request.Build == build.Version)
                && MatchOptions.IsPlaylist(request.Mode)
                && request.Expires > DateTimeOffset.UtcNow.ToUnixTimeMilliseconds())
            {
                handledRequest = request.Id;
                string requestedMode = MatchOptions.NormalizePlaylist(request.Mode);
                HostState? state = ReadHostState(stateFile);
                bool sameHost = !currentHost.HasExited && state is not null && state.Pid == currentHost.Id;
                bool sameMode = sameHost && string.Equals(state!.Mode, requestedMode, StringComparison.OrdinalIgnoreCase);
                if (!sameMode || !HostCanTakeMatch(syndicateRuntime, currentHost.Id))
                {
                    if (!currentHost.HasExited && HostHasPlayers(syndicateRuntime, currentHost.Id))
                    {
                        record("playlist " + requestedMode + " refused: the running match still has players");
                        WritePlaylistError(errorFile, request.Id,
                            "This host already has a match with players. Wait for it to finish before starting another playlist.");
                    }
                    else if ((!StandbyLoadingFor(requestedMode) || await WaitForStandbyAsync())
                             && StandbyReady(requestedMode) && PromoteStandby("Ready pressed for " + requestedMode))
                    {
                        // Ready pressed right after a match (or that playlist picked again): the next match is already loaded.
                        options = options with { Playlist = requestedMode };
                        if (requestedMode != "intro") Studio.StudioSession.SaveMode(requestedMode);
                        status("Your " + MatchOptions.PlaylistName(requestedMode) + " match is ready. Press Ready in Fortnite.");
                    }
                    else
                    {
                        DropStandby("playlist " + requestedMode + " picked");
                        status("Preparing your " + MatchOptions.PlaylistName(requestedMode) + " match while you're in the lobby...");
                        record("playlist picked in Fortnite: " + requestedMode);
                        try
                        {
                            try
                            {
                                if (!currentHost.HasExited)
                                {
                                    currentHost.Kill(entireProcessTree: true);
                                    currentHost.WaitForExit(10000);
                                }
                            }
                            catch (InvalidOperationException) { }
                            catch (System.ComponentModel.Win32Exception) { }
                            TryDelete(restartFile);
                            TryDelete(Path.Combine(syndicateRuntime, currentSlot.ReadyName));
                            currentSlot = HostSlot.A;
                            TryDelete(Path.Combine(syndicateRuntime, currentSlot.ReadyName));
                            // Poll the port every 250 ms (was 1 s): the old server frees it within a moment of exiting.
                            for (int i = 0; i < 120 && PortInUse(currentSlot.Port); i++) await Task.Delay(250);
                            options = options with { Playlist = requestedMode };
                            currentHost = await StartHostOnlyAsync(build, options, sessionDirectory, syndicateRuntime, status, record, hostIndex++, currentSlot, false, null);
                            failures = 0;
                            if (requestedMode != "intro") Studio.StudioSession.SaveMode(requestedMode);
                            TryDelete(errorFile);
                            record("playlist ready: " + requestedMode + " (host PID " + currentHost.Id + ")");
                            status("Your " + MatchOptions.PlaylistName(requestedMode) + " match is ready. Press Ready in Fortnite.");
                        }
                        catch (Exception error)
                        {
                            failures++;
                            record("playlist failed: " + error.Message);
                            status("Playlist could not start: " + error.Message);
                            WritePlaylistError(errorFile, request.Id, error.Message);
                        }
                    }
                }
            }

            // The player is in a match on the live server: load the next one in the background.
            if (standbyTask is null && options.FastQueue && (standbyTriedFor != currentHost.Id || standbyTries < 2) && !currentHost.HasExited)
            {
                var live = ReadOccupancyFile(occupancyFile, currentHost.Id, TimeSpan.FromSeconds(4));
                bool eliminated = live is { HumansAlive: 0 };
                string memory = "";
                bool memoryOk = live is not { Connected: > 0 }
                    || ((memoryCheck.Elapsed >= TimeSpan.FromSeconds(10) || memoryLoggedFor != currentHost.Id) && EnoughMemoryForBackgroundHost(eliminated, out memory));
                if (live is { Connected: > 0 } && !memoryOk)
                {
                    if (memoryLoggedFor != currentHost.Id && memory.Length > 0)
                    {
                        memoryLoggedFor = currentHost.Id;
                        record("fast queue: waiting for memory (" + memory + "); checking again every 10 s");
                    }
                    if (memoryCheck.Elapsed >= TimeSpan.FromSeconds(10)) memoryCheck.Restart();
                }
                else if (live is { Connected: > 0 })
                {
                    if (standbyTriedFor != currentHost.Id) { standbyTriedFor = currentHost.Id; standbyTries = 0; }
                    standbyTries++;
                    string mode = MatchOptions.NormalizePlaylist(options.Playlist);
                    if (mode is "intro" or "creative") { }
                    else if (Studio.StudioSession.Current() is not null) record("fast queue: off while Syndicate Studio is open");
                    else if (PortInUse(currentSlot.Other.Port)) record("fast queue: skipped, port " + currentSlot.Other.Port + " is in use");
                    else
                    {
                        standbySlot = currentSlot.Other; standbyMode = mode;
                        int index = hostIndex++;
                        standbyLog = Path.Combine(sessionDirectory, "host-" + index + ".log");
                        record("fast queue: loading the next " + mode + " match in the background (port " + standbySlot.Port + ")");
                        standbyTask = StartHostOnlyAsync(build, options with { Playlist = mode }, sessionDirectory, syndicateRuntime,
                            _ => { }, record, index, standbySlot, true, process => standby = process);
                    }
                }
            }

            bool requested = File.Exists(restartFile);
            bool hostExited = currentHost.HasExited;
            if (requested || hostExited)
            {
                string reason = "";
                if (requested)
                {
                    string text = "";
                    try { text = File.ReadAllText(restartFile).Trim(); } catch (IOException) { }
                    var parts = text.Split(' ', StringSplitOptions.RemoveEmptyEntries);
                    if (parts.Length == 2 && parts[0] == currentHost.Id.ToString())
                        reason = parts[1];
                    else if (parts.Length == 2)
                    {
                        // A stale request from an older host must not recycle the current one.
                        TryDelete(restartFile);
                        await Task.Delay(1000);
                        continue;
                    }
                }

                if (client.HasExited) break;
                string nextMode = MatchOptions.NormalizePlaylist(options.Playlist);
                string why = "match ended (" + (reason.Length > 0 ? reason : "host exited") + ")";
                // The player has already left the old server (Erbium only asks for a restart 3-8 s after they do), so
                // with the next match loaded there is nothing to wait for.
                if (!hostExited && StandbyReady(nextMode) && !HostHasPlayers(syndicateRuntime, currentHost.Id))
                {
                    await Task.Delay(500);
                    if (client.HasExited) break;
                    if (StandbyReady(nextMode) && PromoteStandby(why))
                    {
                        status("Your next match is ready. Press Ready in Fortnite.");
                        await Task.Delay(500);
                        continue;
                    }
                }

                int delay = reason == "left" ? 1000 : reason == "eliminated" ? 5000 : reason == "win" ? 10000 : 1500;
                status("Preparing your next match...");
                record(why + "; preparing the next host");
                await Task.Delay(delay);
                if (client.HasExited) break;

                try { if (!currentHost.HasExited) { currentHost.Kill(entireProcessTree: true); currentHost.WaitForExit(10000); } }
                catch (InvalidOperationException) { } catch (System.ComponentModel.Win32Exception) { }
                TryDelete(restartFile);
                TryDelete(Path.Combine(syndicateRuntime, currentSlot.ReadyName));

                // The background server is still loading this playlist: finishing it is quicker than starting over.
                if (StandbyLoadingFor(nextMode))
                {
                    await WaitForStandbyAsync();
                    if (client.HasExited) break;
                }
                if (StandbyReady(nextMode) && PromoteStandby(why))
                {
                    status("Your next match is ready. Press Ready in Fortnite.");
                    await Task.Delay(500);
                    continue;
                }
                DropStandby("not ready for the next match");

                currentSlot = HostSlot.A;
                TryDelete(Path.Combine(syndicateRuntime, currentSlot.ReadyName));
                for (int i = 0; i < 120 && PortInUse(currentSlot.Port); i++) await Task.Delay(250);
                try
                {
                    currentHost = await StartHostOnlyAsync(build, options, sessionDirectory, syndicateRuntime, status, record, hostIndex++, currentSlot, false, null);
                    failures = 0;
                    record("next match ready (host PID " + currentHost.Id + ")");
                    status("Your next match is ready. Press Ready in Fortnite.");
                }
                catch (Exception error)
                {
                    failures++;
                    record("next match failed: " + error.Message);
                    status("Could not prepare the next match: " + error.Message);
                    if (failures >= 3)
                    {
                        status("The next match could not start. Close Fortnite and press Play in the launcher.");
                        break;
                    }
                    await Task.Delay(5000);
                }
            }
            // Twice a second: a playlist picked in the lobby (backend prewarm) should start switching right away.
            await Task.Delay(500);
        }

        if (client.HasExited && UseLegacy25ClientMode(build))
        {
            var exitCode = await ExitCodeTextAsync(client);
            var clientLog = Path.Combine(sessionDirectory, "client.log");
            record("Fortnite 2.5 client exited: " + exitCode);
            await StartupReadiness.SaveLatestAsync(client, clientLog);
            if (exitCode != "0x00000000") status("Fortnite 2.5 exited (" + exitCode + "). Logs: " + sessionDirectory);
        }
        DropStandby("session ended");
        try { if (!currentHost.HasExited) currentHost.Kill(entireProcessTree: true); }
        catch (InvalidOperationException) { } catch (System.ComponentModel.Win32Exception) { }
        StopOwned();
    }

    private static void TryDelete(string file)
    {
        try { if (File.Exists(file)) File.Delete(file); }
        catch (IOException) { } catch (UnauthorizedAccessException) { }
    }

    private static IEnumerable<string> SafeFiles(string directory, string pattern)
    {
        try { return Directory.GetFiles(directory, pattern); }
        catch (Exception e) when (e is IOException or UnauthorizedAccessException) { return Array.Empty<string>(); }
    }

    private static async Task<string> ExitCodeTextAsync(Process process)
    {
        if (HiddenDesktop.ExitCodeAsync(process) is { } nativeExitCode && await nativeExitCode is uint code)
            return "0x" + code.ToString("X8");
        try { return "0x" + process.ExitCode.ToString("X8"); }
        catch (InvalidOperationException) { return "unknown"; }
        catch (System.ComponentModel.Win32Exception) { return "unknown"; }
    }

    private static async Task ObserveSessionAsync(Process client, Process host, Action<string> status, Action<string> record, string clientLog)
    {
        try
        {
            await Task.WhenAny(client.WaitForExitAsync(), host.WaitForExitAsync());
            var exited = client.HasExited ? client : host;
            var role = client.HasExited ? "Player" : "Server";
            string exitCode = await ExitCodeTextAsync(exited);
            record(role + " exited: " + exitCode);
            await StartupReadiness.SaveLatestAsync(client, clientLog);
            var message = exitCode == "0x00000000" ? role + " closed. Press Play to start a fresh session." : role + " exited (" + exitCode + "). Logs: " + Path.GetDirectoryName(clientLog);
            // Fortnite can send the last locker/profile mutation while the client is closing.
            // Keep the backend alive briefly so its synchronous profile write reaches disk.
            record("Waiting for the local backend to flush the player profile.");
            await Task.Delay(TimeSpan.FromSeconds(2));
            StopOwned(); status(message);
        }
        catch (Exception) { /* Launcher shutdown owns cleanup. */ }
    }
    public static void StopOwned()
    {
        InputMods.Detach();
        SyndicateBackend.StopStarted();
        foreach (var process in Owned.ToArray().Reverse())
        {
            try { if (!process.HasExited) process.Kill(entireProcessTree: true); }
            catch (InvalidOperationException) { } catch (System.ComponentModel.Win32Exception) { }
        }
        Owned.Clear();
        if (_client is not null && LastLaunch is { } launch)
            StatsService.Record(launch.Profile, _clientStartedUtc, DateTime.UtcNow, launch.Season, launch.Menu);
        Client = null;
        // Play's enabled state is a WPF command; WPF only re-checks it on input, so after Fortnite closed the button
        // stayed greyed until the mouse moved over the launcher. Ask for the re-check now.
        System.Windows.Application.Current?.Dispatcher.BeginInvoke(new Action(System.Windows.Input.CommandManager.InvalidateRequerySuggested));
    }
}
