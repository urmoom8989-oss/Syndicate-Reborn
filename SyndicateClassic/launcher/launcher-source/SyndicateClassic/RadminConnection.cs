// Syndicate © 2026 Syndicate. All rights reserved. Proprietary — no copying, distribution, or derivative works. See LICENSE.
using System;
using System.IO;
using System.Net;
using System.Net.Http;
using System.Net.Sockets;
using System.Text.Json;
using System.Threading;
using System.Threading.Tasks;

namespace SyndicateClassic;

internal static class RadminConnection
{
	private static readonly string Preference = Path.Combine(AppContext.BaseDirectory, "radmin-host.txt");

	private static readonly SemaphoreSlim Gate = new SemaphoreSlim(1, 1);

	private static LocalRelay? _relay;

	public static string Host { get; private set; } = Load();

	public static bool Remote => Host.Length > 0;

	private static string Load()
	{
		try
		{
			return Validate(File.ReadAllText(Preference));
		}
		catch
		{
			return "";
		}
	}

	public static string Validate(string value)
	{
		value = value.Trim();
		if (value.Length == 0)
		{
			return "";
		}
		if (!IPAddress.TryParse(value, out IPAddress address) || address.AddressFamily != AddressFamily.InterNetwork || IPAddress.IsLoopback(address) || address.Equals(IPAddress.Any) || address.GetAddressBytes()[0] >= 224)
		{
			throw new ArgumentException("Enter the host's Radmin IPv4 address only, without a port. Leave blank to host locally.");
		}
		return address.ToString();
	}

	public static void Save(string value)
	{
		string text = Validate(value);
		File.WriteAllText(Preference + ".tmp", text);
		File.Move(Preference + ".tmp", Preference, overwrite: true);
		Stop();
		Host = text;
	}

	// Direct play (share code): the join key that goes with the host it was given for. A Radmin / LAN host has none.
	private static readonly string DirectJoinFile = Path.Combine(AppContext.BaseDirectory, "direct-join.json");
	private sealed record DirectJoin(string host, string key);
	public static string DirectKey
	{
		get
		{
			try { var j = JsonSerializer.Deserialize<DirectJoin>(File.ReadAllText(DirectJoinFile)); return j != null && j.host == Host && System.Text.RegularExpressions.Regex.IsMatch(j.key ?? "", "^[a-f0-9]{32}$") ? j.key! : ""; }
			catch { return ""; }
		}
	}
	public static void SaveDirectKey(string host, string key)
	{
		File.WriteAllText(DirectJoinFile + ".tmp", JsonSerializer.Serialize(new DirectJoin(host, key)));
		File.Move(DirectJoinFile + ".tmp", DirectJoinFile, overwrite: true);
	}
	public static void AddDirectHeader(HttpClient http) { var k = DirectKey; if (k.Length > 0) http.DefaultRequestHeaders.TryAddWithoutValidation("x-tilted-direct", k); }

	public static void Configure(LauncherPaths paths)
	{
		string text = Path.Combine(paths.Root, "Runtime", "Server", "remote-host.json");
		Directory.CreateDirectory(Path.GetDirectoryName(text));
		File.WriteAllText(text + ".tmp", JsonSerializer.Serialize(new
		{
			host = Host,
			key = DirectKey
		}));
		File.Move(text + ".tmp", text, overwrite: true);
	}

	public static async Task EnsureConnected()
	{
		if (Launcher.TryAutoLocate(out LauncherPaths paths, out string _))
		{
			Configure(paths);
		}
		if (!Remote)
		{
			return;
		}
		// Entering this PC's own Radmin IP makes it try to join itself.
		foreach (var nic in System.Net.NetworkInformation.NetworkInterface.GetAllNetworkInterfaces())
		{
			foreach (var unicast in nic.GetIPProperties().UnicastAddresses)
			{
				if (unicast.Address.ToString() == Host)
				{
					throw new IOException("You entered this PC's own Radmin IP (" + Host + "). Enter the HOST's Radmin IP in Settings > Play with friends. The host leaves that box empty.");
				}
			}
		}
		using HttpClient http = new HttpClient
		{
			Timeout = TimeSpan.FromSeconds(5.0)
		};
		AddDirectHeader(http);
		try
		{
			using JsonDocument jsonDocument = JsonDocument.Parse(await http.GetStringAsync("http://" + Host + ":3552/status"));
			// Hosts report channel "public"; the old "testing" check rejected every host.
            if (jsonDocument.RootElement.TryGetProperty("channel",out var channel) && channel.GetString() is string name && name!="public" && name!="testing")throw new IOException("That host runs a different Syndicate channel ("+name+").");
            if (jsonDocument.RootElement.GetProperty("protocol").GetInt32() < 3)
			{
				throw new IOException();
			}
			if (_relay == null)
			{
				LocalRelay localRelay = new LocalRelay();
				try
				{
					localRelay.Start(Host, new int[1] { 6969 }, 7777);
					_relay = localRelay;
					return;
				}
				catch
				{
					localRelay.Dispose();
					throw;
				}
			}
		}
		catch (IOException error) when (error.Message.StartsWith("That host"))
		{
			throw;
		}
		catch (Exception error) when (_relay == null && error is SocketException)
		{
			throw new IOException("Reached the host, but this PC could not open its local relay on TCP 6969 / UDP 7777 (" + error.Message + "). Close any other Fortnite server, backend or Syndicate window on this PC and try again.");
		}
		catch (Exception error)
		{
			throw new IOException(DirectKey.Length > 0
				? "Cannot reach your friend's PC (" + error.GetType().Name + ": " + error.Message + "). Ask them to keep Direct play open in their Multiplayer tab and Syndicate running; if it still fails, their router may not allow direct play (use Radmin instead)."
				: "Cannot reach the match host at http://" + Host + ":3552/status (" + error.GetType().Name + ": " + error.Message + "). Both players need the updated launcher and the same Radmin network. Have the host launch first; allow TCP 3551, 3552, 70, 6969 and UDP 7777 on the Radmin network.");
		}
	}

	public static void Stop()
	{
		_relay?.Dispose();
		_relay = null;
	}
}