// Syndicate © 2026 Syndicate. All rights reserved. Proprietary — no copying, distribution, or derivative works. See LICENSE.
using System.Linq;
using System;
using System.Diagnostics;
using System.IO;
using System.Net.Sockets;
using System.Text.Json;
using System.Threading;
using System.Threading.Tasks;

namespace SyndicateClassic;

public static class GameServer
{
	private sealed record ServerState(int Pid, long Started, string Mode = "solo", string Build = "4.5", string Skill = "", bool Studio = false);
    // Syndicate Studio (Dev Kit only) keeps studio-session.txt fresh while its "Studio match" option is on. The next match
    // server then starts with no bots and stays on the warm-up island (TILTED_STUDIO=1: no bus, no storm) so the map
    // can be edited. A file older than 20 s means the Studio closed.
    internal static string StudioSessionFlag => Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),"ProjectTilted","studio-session.txt");
    private static bool StudioSession(){
        if(!MatchTelemetry.DevKit)return false;
        try{return File.Exists(StudioSessionFlag) && DateTime.UtcNow-File.GetLastWriteTimeUtc(StudioSessionFlag)<TimeSpan.FromSeconds(20);}catch{return false;}
    }

	public static void SelectBuild(LauncherPaths paths,string build)
    {
        if(build is not ("1.7.2" or "1.8.2" or "1.8" or "1.11" or "3.5.1" or "4.5" or "5.10" or "6.20" or "7.40" or "8.50" or "9.40" or "10.31") && !((build=="11.00" || build=="12.50") && GameBuild.Chapter2) && !(build=="19.00" && GameBuild.Chapter3))throw new InvalidOperationException("Unsupported game build.");
        if(GameBuild.Detect(paths.GameExe)!=build)throw new InvalidOperationException("The selected game changed; select its folder again.");
        if(SessionProcesses.ClientRunning(paths))throw new InvalidOperationException("Quit the existing Fortnite client before changing seasons.");
        var runtime=Path.Combine(paths.Root,"Runtime","Server");Directory.CreateDirectory(runtime);
        var stateFile=Path.Combine(runtime,"server.json");
        if(File.Exists(stateFile)){
            ServerState? state=null;try{state=JsonSerializer.Deserialize<ServerState>(ReadShared(stateFile));}catch(JsonException){}
            if(state!=null&&state.Build!=build&&SessionProcesses.OwnedMatches(paths,"host",state.Pid,state.Started)&&!CanReplaceEmptyHost(runtime,state.Pid,new DateTime(state.Started,DateTimeKind.Utc)))
                throw new InvalidOperationException("The other season still has players or its occupancy cannot be verified. Finish that match before switching.");
        }
        var file=Path.Combine(runtime,"active-build.json");
        File.WriteAllText(file+".tmp",JsonSerializer.Serialize(new {Build=build}));File.Move(file+".tmp",file,true);
    }

    public static string ReadMode(LauncherPaths paths)
	{
		try
		{
			string text = File.ReadAllText(Path.Combine(paths.Root, "Runtime", "Server", "selected-mode.txt")).Trim();
			bool flag = ((text == "duo" || text == "squad" || text == "50v50" || text == "playground" || text == "arenasolo" || text == "creative") ? true : false);
			return flag ? text : "solo";
		}
		catch
		{
			return "solo";
		}
	}

	public static void SaveMode(LauncherPaths paths, string mode)
	{
		bool flag;
		switch (mode)
		{
		case "solo":
		case "duo":
		case "squad":
        case "50v50":
        case "playground":
        case "arenasolo":
        case "creative":
			flag = true;
			break;
		default:
			flag = false;
			break;
		}
		if (!flag)
		{
			throw new ArgumentException("Unknown game mode.");
		}
		string text = Path.Combine(paths.Root, "Runtime", "Server");
		Directory.CreateDirectory(text);
		string text2 = Path.Combine(text, "selected-mode.txt");
		File.WriteAllText(text2 + ".tmp", mode);
		File.Move(text2 + ".tmp", text2, overwrite: true);
	}

    public static async Task EnsureReady(LauncherPaths paths,Action<string> status,CancellationToken cancellationToken=default,Action? onHostLoading=null,string? requestedMode=null)
    {
        for(int attempt=0;;attempt++)try {
            await EnsureReadyCore(paths,status,cancellationToken,onHostLoading,requestedMode);return;
        } catch(Exception error) when(attempt==0 && !cancellationToken.IsCancellationRequested &&
            (error is TimeoutException || error.Message.Contains("exited")) && !SessionProcesses.OwnedRunning(paths,"host")) {
            status("Server startup stopped ("+error.Message+") Retrying once automatically...");
            await Task.Delay(1500,cancellationToken);
        }
    }

    private static bool GamePortBusy() => System.Net.NetworkInformation.IPGlobalProperties.GetIPGlobalProperties().GetActiveUdpListeners().Any(p=>p.Port==7777);

    private static bool HostSettingsMatch(ServerState? state,string mode,string build,string skill,bool studio) => state!=null && state.Mode==mode && state.Build==build && state.Skill==skill && state.Studio==studio;

    private static void CheckStartupFiles(LauncherPaths paths)
    {
        if(!File.Exists(paths.GameExe))throw new FileNotFoundException("Fortnite is missing. Select the extracted game folder again.");
        _=PortableServices.Node(paths);
        foreach(var name in new[]{"Matchmaker/index.js","Matchmaker/playlists.cjs","Matchmaker/readiness.cjs","node_modules/ws/package.json"})
            if(!File.Exists(Path.Combine(paths.BackendDir,name)))throw new FileNotFoundException("Incomplete Syndicate installation: "+name+" is missing. Reinstall the latest standalone launcher; keep your account data.");
        var folder=Path.Combine(paths.Root,"Runtime","Server");Directory.CreateDirectory(folder);
        var probe=Path.Combine(folder,"startup-write-"+Guid.NewGuid().ToString("N")+".tmp");
        try{using(var f=new FileStream(probe,FileMode.CreateNew,FileAccess.Write,FileShare.None))f.WriteByte(0);}
        catch(UnauthorizedAccessException){throw new UnauthorizedAccessException("Syndicate cannot write to its folder. Move the launcher to a writable folder, then reopen it.");}
        finally{if(File.Exists(probe))File.Delete(probe);}
    }

	// Runtime\Server\startup-timing.log: how long each stage of a match server start took (kept short), so a
	// slow queue shows exactly where the time went: process start -> frontend, settle, Erbium + island, matchmaking.
	private static void TimingLog(string runtime, string line)
	{
		try
		{
			var file = Path.Combine(runtime, "startup-timing.log");
			var lines = File.Exists(file) ? File.ReadAllLines(file).TakeLast(199).ToList() : new System.Collections.Generic.List<string>();
			lines.Add(DateTime.Now.ToString("yyyy-MM-dd HH:mm:ss") + "  " + line);
			File.WriteAllLines(file, lines);
		}
		catch { }
	}

	private static async Task EnsureReadyCore(LauncherPaths paths, Action<string> status, CancellationToken cancellationToken = default(CancellationToken), Action? onHostLoading = null, string? requestedMode = null)
	{
		CheckStartupFiles(paths);
        string build=GameBuild.Detect(paths.GameExe);
        string skill=BotSkillSettings.Load();
        bool studio=StudioSession();
        string runtime = Path.Combine(paths.Root, "Runtime", "Server");
		Directory.CreateDirectory(runtime);
		string statePath = Path.Combine(runtime, "server.json");
		string readyPath = Path.Combine(runtime, "ready.pid");
		string logPath = Path.Combine(runtime, "server-game.log");
		string runId = Guid.NewGuid().ToString("N");
		string legacyLog = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "FortniteGame", "Saved", "Logs", "FortniteGame.log");
		string dll = Path.Combine(paths.Root, "Erbium-Quality-of-life-changes-main", "Erbium", "x64", "Release", "Erbium.dll");
		if (!File.Exists(dll))
		{
			throw new InvalidOperationException("Erbium.dll is missing. Reinstall the latest standalone launcher and check Windows Security Protection History for a blocked file.");
		}
		using (await AcquireLock(Path.Combine(runtime, "startup.lock"), cancellationToken))
		{
			Process server = null;
			bool frozen = false;
			try
			{
				if (File.Exists(statePath))
				{
					ServerState serverState = JsonSerializer.Deserialize<ServerState>(File.ReadAllText(statePath));
					if (serverState != null)
					{
						Process processById = Process.GetProcessById(serverState.Pid);
						if (!processById.HasExited && processById.StartTime.ToUniversalTime().Ticks == serverState.Started && SessionProcesses.OwnedMatches(paths,"host",serverState.Pid,serverState.Started))
						{
							server = processById;
						}
					}
				}
			}
			catch
			{
			}
			string text = requestedMode ?? ReadMode(paths);
            if(text=="50v50" && build!="4.5"){if(requestedMode!=null)throw new ArgumentException("50v50 is available only in Season 4.");text="solo";}
            if(text=="playground" && build!="4.5"){if(requestedMode!=null)throw new ArgumentException("Playground is available only in Season 4.");text="solo";}
            if(text is "arenasolo" && build is not ("8.50" or "9.40" or "10.31" or "11.00" or "12.50")){if(requestedMode!=null)throw new ArgumentException("Arena is available in Season 8 and later.");text="solo";}
            bool creativeBuild = build is ("7.40" or "8.50" or "9.40" or "10.31");
#if TILTED_DEVKIT
            creativeBuild = creativeBuild || build=="11.00"; // Dev Kit: experiment with Creative on 11.00 (island streaming still broken - expect empty sea)
#endif
            if(text=="creative" && !creativeBuild){if(requestedMode!=null)throw new ArgumentException("Creative is available in Season 7 and later.");text="solo";}
            // Chapter 2 entrance (an account's first 11.00 match, chosen by the backend): Fortnite's own intro playlist,
            // whose warmup movie mutator plays the Chapter 2 cinematic and drops everyone from the bus.
            if(text=="intro" && !(build=="11.00" && GameBuild.Chapter2)){if(requestedMode!=null)throw new ArgumentException("The Chapter 2 entrance needs Fortnite 11.00.");text="solo";}
            if(text is not ("solo" or "duo" or "squad" or "50v50" or "playground" or "arenasolo" or "creative" or "intro"))throw new ArgumentException("Unsupported playlist");
            // Arena always uses hard bots, whatever the launcher's bot skill setting says.
            if(text is "arenasolo")skill="hard";
			if (server != null && (!HostSettingsMatch(JsonSerializer.Deserialize<ServerState>(File.ReadAllText(statePath)),text,build,skill,studio) || !string.Equals(server.MainModule?.FileName,paths.GameExe,StringComparison.OrdinalIgnoreCase) || File.Exists(dll + ".pending")))
			{
				if (!CanReplaceEmptyHost(runtime,server.Id,server.StartTime.ToUniversalTime()))
                {
                    throw new InvalidOperationException("The current match has players or its occupancy cannot be verified. Wait for it to finish.");
                }
				SessionProcesses.StopHost(paths, server.Id, server.StartTime.ToUniversalTime().Ticks);
				if (!server.WaitForExit(10000))
				{
					throw new InvalidOperationException("The previous host did not stop. Quit and relaunch before changing modes.");
				}
				server.Dispose();
				server = null;
			}
			// Same settings, but the match on it already started or finished: a new Ready must not be sent there.
			// A player who just closed or crashed out of a match still counts as connected until the server's
			// connection timeout drops them (up to ~20 s). Wait for that before saying the match is still going.
			if (server != null && requestedMode != null && !HostAccepting(runtime, server.Id) && !FinishedHost(runtime, server.Id, server.StartTime.ToUniversalTime()))
			{
				for (int wait = 0; wait < 25 && !server.HasExited && !FinishedHost(runtime, server.Id, server.StartTime.ToUniversalTime()); wait++)
				{
					if (wait == 0) status("Waiting for the last match to notice you left…");
					await Task.Delay(1000, cancellationToken);
				}
			}
			if (server != null && FinishedHost(runtime, server.Id, server.StartTime.ToUniversalTime()))
			{
				status("Starting a fresh match (the previous one is over)…");
				SessionProcesses.StopHost(paths, server.Id, server.StartTime.ToUniversalTime().Ticks);
				if (!server.WaitForExit(10000)) throw new InvalidOperationException("The previous match did not close. Press Ready again in a few seconds.");
				server.Dispose();
				server = null;
			}
			else if (server != null && requestedMode != null && !HostAccepting(runtime, server.Id))
			{
				throw new InvalidOperationException("A match is still being played on this host. Wait for it to end (or for the players in it to be eliminated), then press Ready again.");
			}
			bool created = server == null;
			if (created)
			{
                // A host that was just stopped (slow start, mode change) can hold the port for a few seconds.
                for(int wait=0;wait<30 && GamePortBusy();wait++){
                    if(wait==0)status("Waiting for the previous game server to close…");
                    await Task.Delay(1000,cancellationToken);
                }
                if(GamePortBusy())
                    throw new InvalidOperationException("Game port 7777 is occupied. Close the other game server or Syndicate copy, then press Ready again.");
				if (File.Exists(dll + ".pending"))
				{
					File.Copy(dll + ".pending", dll, overwrite: true);
					File.Delete(dll + ".pending");
				}
				if (File.Exists(readyPath))
				{
					File.Delete(readyPath);
				}
				string path = Path.Combine(runtime, "restart.request");
				if (File.Exists(path))
				{
					File.Delete(path);
				}
				cancellationToken.ThrowIfCancellationRequested();
				if (File.Exists(logPath))
				{
					File.Move(logPath, Path.Combine(runtime, $"server-game-{DateTime.UtcNow:yyyyMMdd-HHmmssfff}.log"));
				}
				ProcessStartInfo processStartInfo = new ProcessStartInfo(paths.GameExe)
				{
					WorkingDirectory = runtime,
					UseShellExecute = false,
					CreateNoWindow = true,
					WindowStyle = ProcessWindowStyle.Hidden,
					Arguments = $"-epicapp=Fortnite -epicenv=Prod -epiclocale=en-us -epicportal -nullrhi -nosound -nosplash -NoMovie -TiltedServerRun={runId} -FORCELOGFLUSH -nothreadtimeout -NoHangDetection -ini:Engine:[Core.System]:HangDuration=0 -LogCmds=\"LogAbilitySystem off, LogGameStats off, LogFortCustomization off, LogSkeletalMesh off, LogLevelActorContainer off\" -abslog=\"{logPath}\" -UserDir=\"{Path.Combine(runtime, "UserData")}\"" + EpicLaunchStub.ArgumentsFor(build)
				};
				processStartInfo.Environment["TILTED_PLAYLIST"] = text;
				int syndicateBots = MatchPerformanceSettings.Load();
				if(text is "creative" or "playground")syndicateBots=0; // Creative and Playground have no bots.
                if(studio)syndicateBots=0; // Syndicate Studio: an empty island to edit
                if(studio)processStartInfo.Environment["TILTED_STUDIO"]="1"; else processStartInfo.Environment.Remove("TILTED_STUDIO");
				processStartInfo.Environment["TILTED_BOT_COUNT"] = syndicateBots.ToString(System.Globalization.CultureInfo.InvariantCulture);
				// Light matches can afford a 60 Hz server: inputs and hits are processed twice as often.
				// Full lobbies stay at 30 unless "60 ticks in full lobbies" is on (Settings > Match & bots).
				processStartInfo.Environment["TILTED_TICK_RATE"] = syndicateBots <= 30 || HighTickFullLobbies ? "60" : "30";
                processStartInfo.Environment["TILTED_BOT_SKILL"] = skill;
				processStartInfo.Environment["TILTED_SERVER_READY_FILE"] = readyPath;
                processStartInfo.Environment["TILTED_TELEMETRY"] = MatchTelemetry.Collect(runtime) ? "1" : "0"; // match data recording (Settings > Match data)
                // Dev Kit builds only: the server writes Runtime\Server\dev-live.json for the Dev Console (DevConsole.cs).
                if (MatchTelemetry.DevKit) processStartInfo.Environment["TILTED_DEV_LIVE"] = "1"; else processStartInfo.Environment.Remove("TILTED_DEV_LIVE");
                // Dev Kit, for crash hunts: %LOCALAPPDATA%\ProjectTilted\dev-hook-trace.txt makes Erbium log every hook and patch
                // it puts into the game ([HookTrace] lines in stdout.log.<pid>.log). Public builds never pass it.
                if (MatchTelemetry.DevKit && File.Exists(Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "ProjectTilted", "dev-hook-trace.txt"))) processStartInfo.Environment["TILTED_HOOK_TRACE"] = "1"; else processStartInfo.Environment.Remove("TILTED_HOOK_TRACE");
				processStartInfo.Environment["TILTED_SERVER_LOG"] = Path.Combine(runtime, "stdout.log");
				var hostStubs = EpicLaunchStub.StartFrozen(paths.GameExe, build);
				server = NativeMethods.StartHiddenHost(processStartInfo);
				EpicLaunchStub.Attach(server, hostStubs);
				File.WriteAllText(statePath, JsonSerializer.Serialize(new ServerState(server.Id, server.StartTime.ToUniversalTime().Ticks, text, build, skill, studio)));
			}
			SessionProcesses.Track(paths, "host", server);
			try
			{
				var clock = System.Diagnostics.Stopwatch.StartNew(); long frontendMs = 0, settleMs = 0;
				if (created)
				{
					status("Starting Erbium — preparing Fortnite…");
					bool initialized = false;
					// First launches can be slow while antivirus scans Fortnite; allow up to 4 minutes.
					for (int i = 0; i < 960; i++) // every 250 ms (was every second), still up to 4 minutes
					{
						if (server.HasExited)
						{
							throw new InvalidOperationException("The server host exited during startup. See Runtime/Server/server-game.log.");
						}
						string text2 = ReadHostStartupLog(logPath, legacyLog, runId);
						if (text2.Contains(runId) && text2.Contains("Bringing World /Game/Maps/Frontend.Frontend up for play"))
						{
							initialized = true;
							File.WriteAllText(logPath, text2);
							break;
						}
						await Task.Delay(250, cancellationToken);
					}
					if (!initialized)
					{
						throw new TimeoutException("The server did not finish loading the frontend. See Runtime/Server/server-game.log.");
					}
					// Pre-season / Season 1 / 1.11 sometimes crashed in the game's own code (null read) right after
					// Erbium hooked in, when it was injected 2 s after the frontend: the automatic retry then cost
					// about 40 s. Give those builds a few more seconds to settle first.
					frontendMs = clock.ElapsedMilliseconds;
					await Task.Delay(build is "1.7.2" or "1.8.2" or "1.8" or "1.11" ? 6000 : 2000, cancellationToken);
					settleMs = clock.ElapsedMilliseconds - frontendMs;
					await Task.Run(delegate
					{
						NativeMethods.InjectDll((uint)server.Id, dll);
					});
				}
				onHostLoading?.Invoke();
				status("Erbium is loading the island…");
				bool ready = false;
				long islandFrom = clock.ElapsedMilliseconds;
				for (int i = 0; i < 1200; i++) // every 250 ms (was every second), still up to 5 minutes
				{
					if (server.HasExited)
					{
						throw new InvalidOperationException("Erbium exited before the island was ready. See Runtime/Server/stdout.log.<host PID>.log.");
					}
					// The game server's hang watchdog (SyndicateHangWatch.h) writes [SyndicateHang] when the island load froze:
					// stop waiting (was 5 minutes of "loading the island") and report it.
					if (i % 20 == 19 && ErrorReporter.Tail(Path.Combine(runtime, $"stdout.log.{server.Id}.log"), 40).Any(l => l.StartsWith("[SyndicateHang]")))
					{
						frozen = true;
						throw new TimeoutException($"Erbium froze while loading the island. See Runtime/Server/stdout.log.{server.Id}.log ([SyndicateHang] lines).");
					}
					if (ReadShared(readyPath).Trim() == server.Id.ToString() && File.GetLastWriteTimeUtc(readyPath) >= server.StartTime.ToUniversalTime())
					{
						ready = true;
						break;
					}
					await Task.Delay(250, cancellationToken);
				}
				if (!ready)
				{
					throw new TimeoutException("Erbium did not become joinable. See Runtime/Server logs.");
				}
				long islandMs = clock.ElapsedMilliseconds - islandFrom;
				await EnsureMatchmaker(paths, status);
				TimingLog(runtime, created
					? $"{build} {text}: new server ready in {clock.ElapsedMilliseconds / 1000.0:0.0} s (frontend {frontendMs / 1000.0:0.0} s, settle {settleMs / 1000.0:0.0} s, Erbium + island {islandMs / 1000.0:0.0} s, matchmaking {(clock.ElapsedMilliseconds - frontendMs - settleMs - islandMs) / 1000.0:0.0} s)"
					: $"{build} {text}: joined the waiting server in {clock.ElapsedMilliseconds / 1000.0:0.0} s");
				status("Erbium is ready — matchmaking available.");
			}
			catch
			{
				// A frozen host is useless even when it was reused from an earlier request: close it too.
				if ((created || frozen) && server != null && !server.HasExited)
				{
					server.Kill(entireProcessTree: true);
					// Let it release port 7777 before the automatic retry starts a new host.
					try { server.WaitForExit(15000); } catch { }
				}
				throw;
			}
		}
	}

    private sealed record PlaylistRequest(string Id,string Mode,long Expires,string Build="4.5");
    private sealed record Occupancy(int Pid,int Connected,bool Accepting,bool Ended=false,int HumansAlive=-1);
    // The running host can no longer take a new match: it is past the warm-up island and either over,
    // empty, or every real player in it is already eliminated (spectating). Replacing it is then safe.
    private static bool FinishedHost(string runtime,int pid,DateTime started)
    {
        try{
            string file=Path.Combine(runtime,"occupancy.json");
            var live=JsonSerializer.Deserialize<Occupancy>(ReadShared(file));
            var written=File.GetLastWriteTimeUtc(file);
            if(live==null || live.Pid!=pid || written<started || DateTime.UtcNow-written>TimeSpan.FromSeconds(4) || live.Accepting)return false;
            return live.Connected==0 || live.Ended || live.HumansAlive==0;
        }catch{return false;}
    }
    internal static bool HostMayHavePlayers(LauncherPaths paths)
    {
        if(!SessionProcesses.OwnedRunning(paths,"host"))return false;
        try {
            var runtime=Path.Combine(paths.Root,"Runtime","Server");
            var state=JsonSerializer.Deserialize<ServerState>(ReadShared(Path.Combine(runtime,"server.json")));
            return state==null || !CanReplaceEmptyHost(runtime,state.Pid,new DateTime(state.Started,DateTimeKind.Utc));
        } catch { return true; } // Unknown occupancy must not disconnect other players.
    }
    private static bool CanReplaceEmptyHost(string runtime,int pid,DateTime started)
    {
        // Still loading the island (not ready yet): nobody can have joined it, so switching seasons is safe.
        // (It used to refuse here, "occupancy cannot be verified", whenever you picked another season while
        // the warm match server was still loading.)
        try{
            var ready=Path.Combine(runtime,"ready.pid");
            if(!File.Exists(ready) || ReadShared(ready).Trim()!=pid.ToString())return true;
        }catch{}
        try{
            string file=Path.Combine(runtime,"occupancy.json");
            var live=JsonSerializer.Deserialize<Occupancy>(ReadShared(file));
            var written=File.GetLastWriteTimeUtc(file);
            if(live==null || live.Pid!=pid || written<started)return false;
            if(live.Connected==0 && DateTime.UtcNow-written<TimeSpan.FromSeconds(4))return true;
            // Frozen host (a slow PC: ticks of several seconds, occupancy stops updating). Its last word was "nobody
            // here", and nobody can join a host that does not tick, so it is safe to replace. Same for a host frozen for
            // over two minutes with players in it: that match is already lost for them. This used to refuse forever.
            var silent=DateTime.UtcNow-written;
            return (live.Connected==0 && silent>TimeSpan.FromSeconds(20)) || silent>TimeSpan.FromMinutes(2);
        }catch{return false;}
    }
    private static bool HostAccepting(string runtime,int pid)
    {
        try{
            var live=JsonSerializer.Deserialize<Occupancy>(ReadShared(Path.Combine(runtime,"occupancy.json")));
            return live==null || live.Pid!=pid || live.Accepting; // unknown: keep the old behaviour
        }catch{return true;}
    }
    // True when the host has not written occupancy for longer than 'age' (frozen or stuck).
    private static bool OccupancyStale(string runtime,int pid,TimeSpan age)
    {
        try{
            string file=Path.Combine(runtime,"occupancy.json");
            if(!File.Exists(file))return true;
            var live=JsonSerializer.Deserialize<Occupancy>(ReadShared(file));
            return live==null || live.Pid!=pid || DateTime.UtcNow-File.GetLastWriteTimeUtc(file)>age;
        }catch{return false;}
    }
    public static Task PrepareLobby(LauncherPaths paths,Action<string> status){CheckStartupFiles(paths);return EnsureMatchmaker(paths,status);}

    // Quick queue: while you are in the Fortnite lobby, the match server for your last playlist starts and
    // loads the island in the background, so Ready only has to join it (was: Ready started a fresh server,
    // 1-2 minutes). Choosing a different playlist cancels it and starts that one (same wait as before).
    // A warm server nobody joined is closed a minute after Fortnite closes. Settings > Match & bots.
    private static string QuickQueueFlag => Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),"ProjectTilted","quick-queue-off.txt");
    internal static bool QuickQueueEnabled
    {
        get { try { return !File.Exists(QuickQueueFlag); } catch { return true; } }
        set { try { if(value){ if(File.Exists(QuickQueueFlag))File.Delete(QuickQueueFlag); } else { Directory.CreateDirectory(Path.GetDirectoryName(QuickQueueFlag)!); File.WriteAllText(QuickQueueFlag,"off"); } } catch { } }
    }
    // 60 ticks in full lobbies (on = tick-60-full-lobbies.txt). Off: 60 only with 30 bots or fewer, 30 above that.
    private static string HighTickFlag => Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),"ProjectTilted","tick-60-full-lobbies.txt");
    internal static bool HighTickFullLobbies
    {
        get { try { return File.Exists(HighTickFlag); } catch { return false; } }
        set { try { if(!value){ if(File.Exists(HighTickFlag))File.Delete(HighTickFlag); } else { Directory.CreateDirectory(Path.GetDirectoryName(HighTickFlag)!); File.WriteAllText(HighTickFlag,"on"); } } catch { } }
    }
    // The playlist EnsureReady would really start for this mode on this build.
    private static string EffectiveMode(string mode,string build)
    {
        if(mode=="50v50" && build!="4.5")return "solo";
        if(mode is "arenasolo" && build is not ("8.50" or "9.40" or "10.31" or "11.00" or "12.50"))return "solo";
        if(mode=="creative")return "solo"; // never warm Creative (turned off; no bots)
        if(mode=="playground")return build=="4.5" ? "playground" : "solo";
        return mode is "solo" or "duo" or "squad" or "50v50" or "arenasolo" ? mode : "solo";
    }
    private static string ModeName(string mode) => mode switch { "solo"=>"Solo", "duo"=>"Duos", "squad"=>"Squads", "50v50"=>"50 v 50", "playground"=>"Playground", "arenasolo"=>"Arena Solo", "intro"=>"Chapter 2 entrance", _=>mode };
    public static async Task WatchMatches(LauncherPaths paths,Action<string> status,CancellationToken cancel)
    {
        string runtime=Path.Combine(paths.Root,"Runtime","Server");Directory.CreateDirectory(runtime);
        string restart=Path.Combine(runtime,"restart.request"),stateFile=Path.Combine(runtime,"server.json");
        string requestFile=Path.Combine(runtime,"playlist-request.json"),handled="";
        int crashChecked=0; // the last match server checked for a crash after it exited
        DateTime nextMatchmakerCheck=DateTime.MinValue;
        // Quick queue state (see QuickQueueEnabled).
        Task? warm=null;CancellationTokenSource? warmCancel=null;string warmMode="",warmBuild="";int warmPid=0;
        DateTime nextWarm=DateTime.UtcNow.AddSeconds(2),idleSince=DateTime.MinValue;
        async Task Warm(string mode,CancellationToken token){
            // The match server sometimes dies while it starts (a crash inside Fortnite while it loads). That used to
            // show an ERROR and give up for 3 minutes; now it quietly tries again (3 tries), the same way Ready does.
            for(int attempt=1;attempt<=3;attempt++){
                try{
                    await EnsureReady(paths,_=>{},token,null,mode);
                    try{var st=JsonSerializer.Deserialize<ServerState>(ReadShared(stateFile));warmPid=st?.Pid??0;}catch{}
                    status("Your "+ModeName(mode)+" match is ready. Press Ready in Fortnite to join right away.");
                    return;
                }
                catch(OperationCanceledException){return;}
                catch(Exception error){
                    if(attempt<3){status("Preparing your "+ModeName(mode)+" match again (the match server stopped while starting).");try{await Task.Delay(2000,token);}catch(OperationCanceledException){return;}continue;}
                    status("Your "+ModeName(mode)+" match will start when you press Ready ("+error.GetBaseException().Message+").");nextWarm=DateTime.UtcNow.AddMinutes(1);
                }
            }
        }
        using(await AcquireLock(Path.Combine(runtime,"watch.lock"),cancel)){
            while(!cancel.IsCancellationRequested){
                await Task.Delay(500,cancel);
                if(DateTime.UtcNow>=nextMatchmakerCheck){
                    try{await EnsureMatchmaker(paths,status);}
                    catch(Exception error){status("Matchmaking recovery: "+error.Message);}
                    nextMatchmakerCheck=DateTime.UtcNow.AddSeconds(10);
                }
                if(File.Exists(restart)){
                    var parts=ReadShared(restart).Trim().Split(' ');
                    ServerState? state=null;try{state=JsonSerializer.Deserialize<ServerState>(ReadShared(stateFile));}catch(JsonException){}
                    if(parts.Length==2 && int.TryParse(parts[0],out int pid) && state!=null && state.Pid==pid && File.GetLastWriteTimeUtc(restart).Ticks>=state.Started && parts[1] is "win" or "eliminated" or "left"){
                        // Short wait so the match can finish sending XP and challenge progress (was 10 s after an elimination too).
                        await Task.Delay(parts[1]=="left"?1000:parts[1]=="eliminated"?5000:10000,cancel);
                        cancel.ThrowIfCancellationRequested();
                        var requested=File.GetLastWriteTimeUtc(restart);
                        var waited=DateTime.UtcNow-requested;
                        // A stopped host cannot refresh occupancy. Never let its stale restart starve new requests.
                        if(!SessionProcesses.OwnedRunning(paths,"host")){
                            File.Delete(restart);
                        }else if(CanReplaceEmptyHost(runtime,pid,new DateTime(state.Started,DateTimeKind.Utc))
                            // Before: a host that froze after the match (no occupancy updates) or a player
                            // idling on the victory screen blocked the restart forever.
                            || (OccupancyStale(runtime,pid,TimeSpan.FromSeconds(30)) && waited>TimeSpan.FromSeconds(30))
                            || (parts[1]=="win" && waited>TimeSpan.FromSeconds(90))
                            // Every real player is out (eliminated, or the match is over): recycle now
                            // instead of leaving them on a finished match.
                            || ((parts[1]=="eliminated" || parts[1]=="win") && FinishedHost(runtime,pid,new DateTime(state.Started,DateTimeKind.Utc)))){
                            SessionProcesses.StopHost(paths,pid,state.Started);File.Delete(restart);
                            status("Choose a playlist and press Ready in Fortnite for your next match.");
                            // Warm the next match now (was up to 30 s later), so Ready after a match is quick too.
                            nextWarm=DateTime.UtcNow.AddSeconds(2);
                        }
                    }else if(state==null || !SessionProcesses.OwnedRunning(paths,"host")){
                        try{File.Delete(restart);}catch(IOException){}
                    }
                }
                // Quick queue: warm a server for the last playlist while the player is in the lobby.
                if(warm!=null && warm.IsCompleted){warm=null;warmCancel?.Dispose();warmCancel=null;}
                bool clientUp=SessionProcesses.ClientRunning(paths),hostUp=SessionProcesses.OwnedRunning(paths,"host");
                if(warm==null && clientUp && !hostUp && QuickQueueEnabled && !RadminConnection.Remote && !File.Exists(restart) && DateTime.UtcNow>=nextWarm){
                    nextWarm=DateTime.UtcNow.AddSeconds(30);
                    try{
                        warmBuild=GameBuild.Detect(paths.GameExe);warmMode=EffectiveMode(ReadMode(paths),warmBuild);warmPid=0;
                        status("Getting a "+ModeName(warmMode)+" match ready in the background…");
                        warmCancel=CancellationTokenSource.CreateLinkedTokenSource(cancel);
                        warm=Warm(warmMode,warmCancel.Token);
                    }catch(Exception error){status("Quick queue skipped: "+error.Message);nextWarm=DateTime.UtcNow.AddMinutes(3);}
                }
                // Fortnite closed and nobody joined the warm server: close it after a minute.
                if(warm==null && warmPid!=0 && !clientUp && hostUp){
                    ServerState? live=null;try{live=JsonSerializer.Deserialize<ServerState>(ReadShared(stateFile));}catch{}
                    if(live!=null && live.Pid==warmPid && CanReplaceEmptyHost(runtime,live.Pid,new DateTime(live.Started,DateTimeKind.Utc))){
                        if(idleSince==DateTime.MinValue)idleSince=DateTime.UtcNow;
                        else if(DateTime.UtcNow-idleSince>TimeSpan.FromSeconds(60)){SessionProcesses.StopHost(paths,live.Pid,live.Started);warmPid=0;idleSince=DateTime.MinValue;status("Closed the waiting match server (Fortnite is closed).");}
                    }else idleSince=DateTime.MinValue;
                }else idleSince=DateTime.MinValue;
                // A match server that exited with the crash reporter's lines in its log crashed mid-match: report it.
                try{
                    var last=JsonSerializer.Deserialize<ServerState>(ReadShared(stateFile));
                    if(last!=null && last.Pid!=0 && last.Pid!=crashChecked){
                        bool alive;try{using var p=Process.GetProcessById(last.Pid);alive=!p.HasExited;}catch{alive=false;}
                        if(!alive){
                            crashChecked=last.Pid;
                            var crashLog=Path.Combine(runtime,$"stdout.log.{last.Pid}.log");
                            if(File.Exists(crashLog) && File.GetLastWriteTimeUtc(crashLog)>DateTime.UtcNow.AddMinutes(-15) && ErrorReporter.Tail(crashLog,400).Any(l=>l.Contains("[CrashReporter]")) && ErrorReporter.Tail(crashLog,400).Any(l=>l.Contains("Roaming with"))){
                                status("The match server crashed during the match. An error report was sent (Settings > Match data).");
                                ErrorReporter.ServerCrashed(paths,last.Build,last.Mode,crashLog);
                            }
                        }
                    }
                }catch{}
                PlaylistRequest? request=null;
                try{request=JsonSerializer.Deserialize<PlaylistRequest>(ReadShared(requestFile));}catch(JsonException){}
                if(request==null || request.Id==handled || request.Expires<=DateTimeOffset.UtcNow.ToUnixTimeMilliseconds() || request.Mode is not ("solo" or "duo" or "squad" or "50v50" or "playground" or "arenasolo" or "creative" or "intro"))continue;
                handled=request.Id;
                // A queue request left over from the season that was just closed (it lives 10 minutes). The player
                // is on another build now, so it is stale, not an error: skip it quietly and let the new queue win.
                string runningBuild;try{runningBuild=GameBuild.Detect(paths.GameExe);}catch{runningBuild="";}
                if(runningBuild!="" && request.Build!=runningBuild){TimingLog(runtime,$"skipped a stale {request.Build} {request.Mode} request (Fortnite {runningBuild} is selected)");continue;}
                if(warm!=null && !warm.IsCompleted){
                    if(request.Mode==warmMode && request.Build==warmBuild){status("Your "+ModeName(warmMode)+" match is almost ready…");}
                    else {status("Switching the waiting match to "+ModeName(request.Mode)+"…");warmCancel?.Cancel();}
                    try{await warm;}catch{}
                    warm=null;warmCancel?.Dispose();warmCancel=null;
                }
                warmPid=0; // a real match request owns the host from here
                try{
                    if(request.Build!=GameBuild.Detect(paths.GameExe))throw new InvalidOperationException("The requested match is for a different Fortnite version. Relaunch the correct season.");
                    status("Preparing "+request.Mode+" selected in Fortnite…");
                    try{await EnsureReady(paths,status,cancel,null,request.Mode);}
                    catch(Exception first) when (first is TimeoutException || (first is InvalidOperationException && first.Message.Contains("exited"))){
                        TimingLog(runtime,$"{request.Build} {request.Mode}: start failed ({first.Message}); retrying");
                        // One automatic retry: a slow first start (antivirus scan) usually succeeds the second time.
                        status("The game server was slow to start ("+first.Message+") Retrying once…");
                        await EnsureReady(paths,status,cancel,null,request.Mode);
                    }
                    if(request.Mode!="intro")SaveMode(paths,request.Mode); // the Chapter 2 entrance is a one-off, never the remembered playlist
                }catch(OperationCanceledException){throw;}
                catch(Exception error){
                    var result=Path.Combine(runtime,"playlist-error.json");
                    File.WriteAllText(result+".tmp",JsonSerializer.Serialize(new {Id=request.Id,Message=error.Message}));File.Move(result+".tmp",result,true);
                    status("Playlist could not start: "+error.Message);
                    ErrorReporter.ServerStartFailed(paths,request.Build,request.Mode,error); // Discord error report (Settings > Match data)
                }
            }
        }
    }

	private static async Task<FileStream> AcquireLock(string path, CancellationToken cancellationToken)
	{
		for (int i = 0; i < 240; i++)
		{
			try
			{
				return new FileStream(path, FileMode.OpenOrCreate, FileAccess.ReadWrite, FileShare.None);
			}
			catch (IOException)
			{
				await Task.Delay(1000, cancellationToken);
			}
		}
		throw new TimeoutException("Another launcher is still starting Erbium.");
	}


    private static string ReadHostStartupLog(string requestedLog, string legacyLog, string runId)
    {
        string marker = "-TiltedServerRun=" + runId;
        string text = ReadShared(requestedLog);
        if (text.Contains(marker)) return text;
        // Shipping 4.00 ignores -abslog and chooses a numbered file when the client is open.
        string directory = Path.GetDirectoryName(legacyLog)!;
        if (!Directory.Exists(directory)) return "";
        foreach (string file in Directory.EnumerateFiles(directory, "FortniteGame*.log"))
        {
            string name = Path.GetFileNameWithoutExtension(file);
            if (name != "FortniteGame" &&
                !(name.StartsWith("FortniteGame_") && int.TryParse(name.Substring("FortniteGame_".Length), out _))) continue;
            text = ReadShared(file);
            if (text.Contains(marker)) return text;
        }
        return "";
    }

	private static string ReadShared(string path)
	{
		try
		{
			using FileStream stream = new FileStream(path, FileMode.Open, FileAccess.Read, FileShare.ReadWrite);
			// Old clients can leave multi-gigabyte logs. Inspect a bounded snapshot,
            // preserving the command-line run ID at the front and current status at the tail.
            const int limit=2*1024*1024,head=256*1024;
            long length=stream.Length;
            if(length<=limit){byte[] small=new byte[(int)length];int count=0,n;while(count<small.Length && (n=stream.Read(small,count,small.Length-count))>0)count+=n;return System.Text.Encoding.UTF8.GetString(small,0,count);}
            byte[] bytes=new byte[limit];
            int first=stream.Read(bytes,0,head);
            stream.Seek(Math.Max(first,length-(limit-head)),SeekOrigin.Begin);
            int last=0,read;
            while(last<limit-head && (read=stream.Read(bytes,head+last,limit-head-last))>0)last+=read;
            return System.Text.Encoding.UTF8.GetString(bytes,0,first)+"\n"+System.Text.Encoding.UTF8.GetString(bytes,head,last);
		}
		catch (IOException)
		{
			return "";
		}
	}

	private static async Task<bool> PortOpen(int port)
	{
		try
		{
			using TcpClient client = new TcpClient();
			await client.ConnectAsync("127.0.0.1", port).WaitAsync(TimeSpan.FromSeconds(1.0));
			return true;
		}
		catch
		{
			return false;
		}
	}

	private static async Task EnsureMatchmaker(LauncherPaths paths, Action<string> status)
	{
		if (await PortOpen(6969))
		{
			if (!await Task.Run(() => SessionProcesses.OwnedRunning(paths, "matchmaker")))
			{
				throw new InvalidOperationException("Another Syndicate copy is using matchmaking. Close its services and relaunch.");
			}
			return;
		}
		await Task.Run(() => SessionProcesses.StopUnresponsiveMatchmaker(paths)); // waits up to 10 s: not on the window thread
        status("Starting matchmaking…");
		ProcessStartInfo startInfo = new ProcessStartInfo(PortableServices.Node(paths), "Matchmaker/index.js")
		{
			WorkingDirectory = paths.BackendDir,
			UseShellExecute = false,
			CreateNoWindow = true,
            RedirectStandardError = true
		};
		Process process = Process.Start(startInfo) ?? throw new InvalidOperationException("Could not start matchmaking.");
		var stderr=process.StandardError.ReadToEndAsync();
        SessionProcesses.Track(paths, "matchmaker", process);
		for (int i = 0; i < 20; i++)
		{
			if (process.HasExited)
			{
				var detail=await stderr;
                File.WriteAllText(Path.Combine(paths.Root,"Runtime","Server","matchmaker-startup-error.log"),detail);
                throw new InvalidOperationException(detail.Contains("MODULE_NOT_FOUND")?"Matchmaking files are missing. Reinstall the latest standalone launcher.":detail.Contains("EADDRINUSE")?"Matchmaking port 6969 is occupied. Close the other Syndicate copy and retry.":"Matchmaking exited (code "+process.ExitCode+"). Restart Syndicate; if repeated, reinstall the latest standalone launcher.");
			}
			if (await PortOpen(6969))
			{
				if (!SessionProcesses.OwnedRunning(paths, "matchmaker"))
				{
					throw new InvalidOperationException("Another Syndicate copy is using matchmaking. Close its services and relaunch.");
				}
				return;
			}
			await Task.Delay(500);
		}
		throw new TimeoutException("Matchmaking did not start on port 6969.");
	}
}