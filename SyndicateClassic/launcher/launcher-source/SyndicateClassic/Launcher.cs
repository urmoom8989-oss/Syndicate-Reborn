// Syndicate © 2026 Syndicate. All rights reserved. Proprietary — no copying, distribution, or derivative works. See LICENSE.
using System;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Net.Sockets;
using System.Text.Encodings.Web;
using System.Text.Json;
using System.Threading.Tasks;

namespace SyndicateClassic;

public static class Launcher
{
	private sealed class Config
	{
		public string? Root { get; set; }
		public string? GameInstallRoot { get; set; }
        public System.Collections.Generic.Dictionary<string,string> GameInstallRoots { get; set; } = new();
	}

	private const int BackendPort = 3551;

	private const string GameExeRel = "FortniteGame\\Binaries\\Win64\\FortniteClient-Win64-Shipping.exe";

	private static string BackendBatRel => BackendFolder.Name + "\\start.bat";

	private static readonly string ConfigPath = Path.Combine(AppContext.BaseDirectory, "launcher.config.json");

    // The folder chosen for a build (e.g. "7.40"), or null when it has not been set or its game files
    // are gone (a folder that merely exists used to count as linked, so PLAY showed for empty folders).
    internal static string? GameFolderFor(string build)
    {
        var config=ReadConfig();
        return config.GameInstallRoots!=null && config.GameInstallRoots.TryGetValue(build,out var folder) && File.Exists(Path.Combine(folder,GameExeRel)) && Directory.Exists(Path.Combine(folder,"Engine")) ? folder : null;
    }

    // The folder linked for a build even when its files are incomplete (for messages), or null.
    internal static string? LinkedFolderFor(string build)
    {
        var config=ReadConfig();
        return config.GameInstallRoots!=null && config.GameInstallRoots.TryGetValue(build,out var folder) ? folder : null;
    }

    // What is wrong with a game folder, in words a player can act on; null when it is ready.
    // "Copying" is reported while Windows still has the exe or a pak open for writing.
    internal static string? FolderProblem(string folder)
    {
        try
        {
            if(!Directory.Exists(folder))return "The folder "+folder+" no longer exists.";
            var exe=Path.Combine(folder,GameExeRel);
            if(!File.Exists(exe))return "Missing "+GameExeRel+" in "+folder+".";
            if(!Directory.Exists(Path.Combine(folder,"Engine")))return "Missing the Engine folder in "+folder+".";
            var paks=Path.Combine(folder,"FortniteGame","Content","Paks");
            if(!Directory.Exists(paks) || !Directory.EnumerateFiles(paks,"*.pak").Any())return "Missing the game data (FortniteGame\\Content\\Paks\\*.pak) in "+folder+".";
            foreach(var file in Directory.EnumerateFiles(paks,"*.pak").Prepend(exe))
                if(!CanRead(file))return "Still copying files into "+folder+". It will be ready when the copy finishes.";
            return null;
        }
        catch(Exception error){return "Couldn't check "+folder+": "+error.Message;}
    }

    private static bool CanRead(string file)
    {
        try { using var stream=new FileStream(file,FileMode.Open,FileAccess.Read,FileShare.Read); return true; }
        catch(IOException){return false;}
        catch(UnauthorizedAccessException){return false;}
    }

    // Folders worth watching for season files: every linked folder's parent (e.g. "Fortnite Seasons")
    // and the linked folders themselves, so files copied in or removed are noticed.
    internal static System.Collections.Generic.List<string> SeasonSearchRoots()
    {
        var config=ReadConfig();var roots=new System.Collections.Generic.List<string>();
        void Add(string? folder){try{if(!string.IsNullOrWhiteSpace(folder) && Directory.Exists(folder)){var full=Path.GetFullPath(folder).TrimEnd('\\');if(!roots.Any(r=>r.Equals(full,StringComparison.OrdinalIgnoreCase)))roots.Add(full);}}catch{}}
        foreach(var folder in (config.GameInstallRoots?.Values ?? Enumerable.Empty<string>()).Append(config.GameInstallRoot))
        {
            if(string.IsNullOrWhiteSpace(folder))continue;
            try{Add(Directory.GetParent(Path.GetFullPath(folder).TrimEnd('\\'))?.FullName);}catch{}
        }
        Add(Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.DesktopDirectory),"Fortnite Seasons"));
        return roots;
    }

    // Looks for complete, supported game folders in the search roots (the root itself and its direct
    // subfolders) and returns the builds that have no working folder linked yet. Runs off the UI thread.
    internal static System.Collections.Generic.List<(string Build,string Folder)> FindUnlinkedSeasons()
    {
        var found=new System.Collections.Generic.List<(string,string)>();
        var config=ReadConfig();
        var linked=(config.GameInstallRoots?.Values ?? Enumerable.Empty<string>()).Select(f=>{try{return Path.GetFullPath(f).TrimEnd('\\');}catch{return f;}}).ToList();
        foreach(var root in SeasonSearchRoots())
        {
            System.Collections.Generic.IEnumerable<string> candidates;
            try{candidates=Directory.EnumerateDirectories(root).Prepend(root).ToList();}catch{continue;}
            foreach(var folder in candidates)
            {
                if(!File.Exists(Path.Combine(folder,GameExeRel)))continue;
                if(linked.Any(l=>l.Equals(folder.TrimEnd('\\'),StringComparison.OrdinalIgnoreCase)))continue;   // already linked (its state is GameFolderFor's job)
                if(FolderProblem(folder)!=null)continue;
                string build;try{build=GameBuild.Detect(Path.Combine(folder,GameExeRel));}catch{continue;}
                if(GameFolderFor(build)==null && !found.Any(f=>f.Item1==build))found.Add((build,folder));
            }
        }
        return found;
    }

    // Links a folder found by FindUnlinkedSeasons (the same record a manual LINK FOLDER writes).
    internal static void LinkSeasonFolder(string build,string folder)
    {
        var config=ReadConfig();config.GameInstallRoots ??= new();
        config.GameInstallRoots[build]=folder;
        if(string.IsNullOrWhiteSpace(config.GameInstallRoot) || !File.Exists(Path.Combine(config.GameInstallRoot,GameExeRel)))config.GameInstallRoot=folder;
        File.WriteAllText(ConfigPath,JsonSerializer.Serialize(config,new JsonSerializerOptions{WriteIndented=true}));
    }

    private static Config ReadConfig()
    {
        try { return JsonSerializer.Deserialize<Config>(File.ReadAllText(ConfigPath)) ?? new Config(); }
        catch { return new Config(); }
    }

    private static string? FindRuntimeRoot(Config config)
    {
        var folder = new DirectoryInfo(AppContext.BaseDirectory);
        for(int depth=0; depth<12 && folder!=null; depth++,folder=folder.Parent)
            if(IsRootCandidatesPresent(folder.FullName))return folder.FullName;
        if(!string.IsNullOrWhiteSpace(config.Root) && IsRootCandidatesPresent(config.Root))return config.Root;
        var installed=Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),"Project Tilted");
        return IsRootCandidatesPresent(installed)?installed:null;
    }

    internal static bool TryNormalizeGameFolder(string selected,out string game)
    {
        game="";
        try {
            var folder=Path.GetFullPath(selected);
            if(Path.GetFileName(folder.TrimEnd(Path.DirectorySeparatorChar)).Equals("FortniteGame",StringComparison.OrdinalIgnoreCase)
                || Path.GetFileName(folder.TrimEnd(Path.DirectorySeparatorChar)).Equals("Engine",StringComparison.OrdinalIgnoreCase))
                folder=Directory.GetParent(folder.TrimEnd(Path.DirectorySeparatorChar))!.FullName;
            if(!File.Exists(Path.Combine(folder,GameExeRel)) || !Directory.Exists(Path.Combine(folder,"Engine")))return false;
            game=folder;return true;
        } catch { return false; }
    }

    private static LauncherPaths MakePaths(string runtime,string? game) => new LauncherPaths {
        Root=runtime,GameInstallRootOverride=game,RedirectDll=ResolveRedirectDll(runtime)
    };

    public static bool TryAutoLocate(out LauncherPaths paths,out string message)
    {
        var config=ReadConfig();var runtime=FindRuntimeRoot(config);
        if(runtime!=null)BackendFolder.Migrate(runtime);
        if(runtime==null){paths=null;message="Syndicate's backend files are missing. Extract the complete Syndicate package, then choose your Fortnite 1.7.2, 1.8.2, 1.11, 3.5.1, 4.5, 5.10, 6.20, 7.40, 8.50, 9.40 or 10.31 game folder in Settings.";return false;}
        paths=MakePaths(runtime,config.GameInstallRoot);
        message="Syndicate: "+runtime+" | Game: "+paths.GameInstallRoot;
        return true;
    }

    public static bool TryLocateRoot(string selected,out LauncherPaths paths,out string message, string? expectedBuild = null)
    {
        paths=null;
        if(!TryNormalizeGameFolder(selected,out var game)){
            message="Choose the extracted Fortnite 1.7.2, 1.8.2, 1.11, 3.5.1, 4.5, 5.10, 6.20, 7.40, 8.50, 9.40 or 10.31 folder containing both FortniteGame and Engine. The game executable is missing or the folder is incomplete.";
            return false;
        }
        string build;
        try { build=GameBuild.Detect(Path.Combine(game,GameExeRel)); } catch(Exception error) {message=error.Message;return false;}
        if(expectedBuild!=null&&build!=expectedBuild){message="That folder contains Fortnite "+build+", not "+expectedBuild+".";return false;}
        var config=ReadConfig();var runtime=FindRuntimeRoot(config);
        if(runtime==null && IsRootCandidatesPresent(game))runtime=game;
        if(runtime==null){message="Syndicate's backend files are missing. Extract the complete Syndicate package first; your game can stay in the selected folder.";return false;}
        paths=MakePaths(runtime,game);
        // Save both locations independently; never move the user's game files.
        RememberPreviousGame(config);
        config.Root=runtime;config.GameInstallRoot=game;config.GameInstallRoots[build]=game;
        File.WriteAllText(ConfigPath,JsonSerializer.Serialize(config,new JsonSerializerOptions{WriteIndented=true}));
        message="Game folder set to "+game;
        return true;
    }

    private static void RememberPreviousGame(Config config)
    {
        config.GameInstallRoots ??= new();
        try {if(!string.IsNullOrWhiteSpace(config.GameInstallRoot))config.GameInstallRoots[GameBuild.Detect(Path.Combine(config.GameInstallRoot,GameExeRel))]=config.GameInstallRoot;}catch(IOException){}
    }
    internal static bool TrySelectBuild(string build,out LauncherPaths paths,out string message)
    {
        var config=ReadConfig();RememberPreviousGame(config);
        paths=null;message="Choose your Fortnite "+build+" folder.";
        return config.GameInstallRoots.TryGetValue(build,out var folder) && TryLocateRoot(folder,out paths,out message,build);
    }

	public static bool EnsureRedirectNextToExe(LauncherPaths paths)
	{
		string text = Path.Combine(AppContext.BaseDirectory, "cobalt.dll");
		if (File.Exists(text))
		{
			return true;
		}
		string[] array = new string[1] { paths.RedirectDll };
		foreach (string text2 in array)
		{
			if (!string.IsNullOrEmpty(text2) && File.Exists(text2))
			{
				File.Copy(text2, text, overwrite: true);
				return true;
			}
		}
		DirectoryInfo directoryInfo = new DirectoryInfo(AppContext.BaseDirectory);
		int num = 0;
		while (num < 12 && directoryInfo != null)
		{
			string text3 = Path.Combine(directoryInfo.FullName, "Launcher", "cobalt.dll");
			if (File.Exists(text3))
			{
				File.Copy(text3, text, overwrite: true);
				return true;
			}
			text3 = Path.Combine(directoryInfo.FullName, "cobalt.dll");
			if (File.Exists(text3))
			{
				File.Copy(text3, text, overwrite: true);
				return true;
			}
			num++;
			directoryInfo = directoryInfo.Parent;
		}
		return false;
	}

	// Names of the antivirus products Windows Security Center knows about (e.g. "Avast Antivirus",
	// "Windows Defender"), so a missing cobalt.dll can say which one most likely removed it.
	public static string[] AntivirusNames()
	{
		try
		{
			var info = new System.Diagnostics.ProcessStartInfo("powershell.exe", "-NoProfile -NonInteractive -Command \"(Get-CimInstance -Namespace root/SecurityCenter2 -ClassName AntiVirusProduct).displayName\"")
			{ UseShellExecute = false, CreateNoWindow = true, RedirectStandardOutput = true };
			using var process = System.Diagnostics.Process.Start(info);
			if (process == null) return Array.Empty<string>();
			var output = process.StandardOutput.ReadToEnd();
			if (!process.WaitForExit(5000)) { try { process.Kill(); } catch { } return Array.Empty<string>(); }
			return output.Split(new[] { '\r', '\n' }, StringSplitOptions.RemoveEmptyEntries).Select(n => n.Trim()).Where(n => n.Length > 0 && n.Length < 80).Distinct().ToArray();
		}
		catch { return Array.Empty<string>(); }
	}

	public static string RedirectDllForInjection()
	{
		string text = Path.Combine(AppContext.BaseDirectory, "cobalt.dll");
		if (!File.Exists(text))
		{
			return "";
		}
		return text;
	}

	// Called from the window thread in many places. Connecting to a closed local port takes about a
	// second on Windows, so a check could freeze the window for 1.5 s each time. Now: a short timeout,
	// and a result less than 2 s old is reused.
	private static (bool Up, DateTime At) _backendCheck;
	public static bool IsBackendUp()
	{
		var last = _backendCheck;
		if (DateTime.UtcNow - last.At < TimeSpan.FromSeconds(2)) return last.Up;
		bool up = IsPortOpen("127.0.0.1", 3551, 300);
		_backendCheck = (up, DateTime.UtcNow);
		return up;
	}

	public static void ForgetBackendCheck() => _backendCheck = default;

	// The port being open is not enough: a stuck backend still accepts connections but never answers, and
	// Fortnite then waits on "Signing in to Epic services" and falls back to the login screen. This asks for a
	// real HTTP answer (any status code counts).
	private static readonly System.Net.Http.HttpClient HealthHttp = new System.Net.Http.HttpClient { Timeout = TimeSpan.FromSeconds(4) };
	public static async Task<bool> IsBackendRespondingAsync() => (await CheckBackendAsync()).Responding;

	// Responding: it answered at all. Current: it is a backend from this version, which writes its own log.
	// A backend started by an older launcher still sends its output to that launcher, and freezes for good
	// once that launcher is closed (it answers a quick check, then hangs on the first real request).
	public static async Task<(bool Responding, bool Current)> CheckBackendAsync()
	{
		try
		{
			using var response = await HealthHttp.GetAsync("http://127.0.0.1:3551/");
			bool current = false;
			try
			{
				using var body = System.Text.Json.JsonDocument.Parse(await response.Content.ReadAsStringAsync());
				current = body.RootElement.TryGetProperty("ownLog", out var own) && own.ValueKind == System.Text.Json.JsonValueKind.True;
			}
			catch { }
			return (true, current);
		}
		catch { return (false, false); }
	}

	private static async Task<bool> IsPortOpenAsync(string host, int port, int timeoutMs)
	{
		try
		{
			using var client = new TcpClient();
			var connect = client.ConnectAsync(host, port);
			if (await Task.WhenAny(connect, Task.Delay(timeoutMs)) != connect) return false;
			await connect;
			return client.Connected;
		}
		catch { return false; }
	}

	public static async Task<bool> WaitForBackendAsync(int timeoutMs, Action<string> onTry, Process? backend = null, LauncherPaths? paths = null)
	{
		DateTime deadline = DateTime.UtcNow.AddMilliseconds(timeoutMs);
		int attempt = 0;
		while (DateTime.UtcNow < deadline)
        {
            if(backend!=null && backend.HasExited)
                throw new InvalidOperationException($"Node/backend exited (code {backend.ExitCode}). See Runtime/Server/backend-startup.log.");
            if(paths!=null){
                var env=Path.Combine(paths.BackendDir,".env");
                if(!SessionProcesses.OwnedRunning(paths,"database"))
                    throw new InvalidOperationException("MongoDB exited during startup. See " + Path.Combine(AccountStorage.Database,"mongodb.log") + ". No database files have been removed.");
            }
			if (await IsPortOpenAsync("127.0.0.1", 3551, 1000)) // awaited: the window keeps responding while it waits
			{
				_backendCheck = (true, DateTime.UtcNow);
				return true;
			}
			attempt++;
			// Checked every 150 ms (was every 2 s, which alone added up to 2 s to every start).
			if (attempt % 13 == 0) onTry?.Invoke($"Waiting for backend on 127.0.0.1:{3551} ({attempt * 150 / 1000}s)...");
			await Task.Delay(150);
		}
		return await IsPortOpenAsync("127.0.0.1", 3551, 1000);
	}

	public static Process StartBackend(LauncherPaths paths)
	{
		PortableServices.Prepare(paths);
		ProcessStartInfo processStartInfo = new ProcessStartInfo();
		processStartInfo.FileName = PortableServices.Node(paths);
		processStartInfo.Arguments = "src/index.js";
		processStartInfo.WorkingDirectory = paths.BackendDir;
		processStartInfo.UseShellExecute = false;
		processStartInfo.CreateNoWindow = true;
		processStartInfo.Environment["BOT_TOKEN"] = "";
		processStartInfo.Environment["DISCORD_TOKEN"] = "";
        // Developer diagnostics API (match reports, Discord relay test) only for Dev Kit builds. Public builds
        // clear it, so a user environment variable or .env line cannot switch it on there.
        processStartInfo.Environment["TILTED_DEVKIT"] = MatchTelemetry.DevKit ? "1" : "";
        // Use the shared player store in AppData; offer the .env database as a one-time import source.
        processStartInfo.Environment["MONGO_URI"] = PortableServices.SharedMongoUri;
        try {
            var envPath = Path.Combine(paths.BackendDir, ".env");
            var configured = File.Exists(envPath) ? PortableServices.ConfiguredMongoUri(File.ReadAllText(envPath)) : null;
            if (configured != null && !PortableServices.UsesBundledDatabase("MONGO_URI=" + configured))
                processStartInfo.Environment["LEGACY_MONGO_URI"] = configured;
        } catch (IOException) { }
        var log=Path.Combine(paths.Root,"Runtime","Server","backend-startup.log");
        Directory.CreateDirectory(Path.GetDirectoryName(log)!);
        // Keep the last run's log (backend-startup.previous.log): a backend that stopped by itself left no trace
        // before, because this start overwrote its log.
        try { if (File.Exists(log)) File.Copy(log, Path.Combine(Path.GetDirectoryName(log)!, "backend-startup.previous.log"), overwrite: true); } catch (IOException) { }
        File.WriteAllText(log,$"{DateTime.UtcNow:O} Starting local backend\n");
        // The backend writes this log itself (src/Utils/syndicateConsoleLog.js). It used to send its output through a
        // pipe to this launcher: when the launcher closed or restarted while the backend kept running, the pipe
        // broke and the backend stopped (or hung), and open games fell back to the Epic login screen.
        processStartInfo.Environment["TILTED_BACKEND_LOG"]=log;
        Process process=new Process{StartInfo=processStartInfo};
        if(!process.Start())throw new InvalidOperationException("Failed to start local services.");
		SessionProcesses.Track(paths, "backend", process);
		return process;
	}

	public static void StopBackend(int backendPid)
	{
		if (backendPid <= 0)
		{
			return;
		}
		try
		{
			using Process process = Process.Start(new ProcessStartInfo
			{
				FileName = "taskkill.exe",
				Arguments = $"/PID {backendPid} /T /F",
				UseShellExecute = false,
				CreateNoWindow = true
			});
			process?.WaitForExit(10000);
		}
		catch
		{
		}
	}

	private static bool IsRootCandidatesPresent(string root)
	{
		return BackendFolder.Present(root);
	}

	private static string ResolveRedirectDll(string root)
	{
		string text = Path.Combine(root, "Launcher", "cobalt.dll");
		if (File.Exists(text))
		{
			return text;
		}
		string text2 = Path.Combine(AppContext.BaseDirectory, "cobalt.dll");
		if (File.Exists(text2))
		{
			return text2;
		}
		return text;
	}

	private static bool IsPortOpen(string host, int port, int timeoutMs)
	{
		try
		{
			using TcpClient tcpClient = new TcpClient();
			if (tcpClient.ConnectAsync(host, port).Wait(timeoutMs) && tcpClient.Connected)
			{
				return true;
			}
			tcpClient.Close();
			return false;
		}
		catch
		{
			return false;
		}
	}
}