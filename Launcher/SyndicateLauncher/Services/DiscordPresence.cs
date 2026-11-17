using System;
using System.Buffers.Binary;
using System.Diagnostics;
using System.IO;
using System.IO.Pipes;
using System.Text;
using System.Text.Json;
using System.Text.Json.Nodes;
using System.Threading;
using System.Threading.Tasks;

namespace SyndicateLauncher.Services;

/// <summary>
/// Discord Rich Presence: "Playing SYNDICATE" with the Syndicate logo, the season, the mode, how many players are
/// left (and how many of them are bots), a match timer and a "Join Discord" button.
///
/// Gameplay-neutral by design. It runs on its own background task while the launcher is open and only ever:
///   * reads two small files the match server already writes for the matchmaker (server.json, occupancy.json),
///     with shared read access, every few seconds;
///   * talks to the Discord app on this PC over Discord's local named pipe (discord-ipc-N).
/// It never writes a file, never touches the game, the match server, the backend or the matchmaker, and every
/// error (Discord closed, pipe gone, file mid-write) is swallowed, so it can't delay or break a queue or a match.
///
/// Setup: a Discord application named SYNDICATE (its name is the "Playing ..." text) with the logo uploaded under
/// Rich Presence > Art Assets as "syndicate". Put its Application ID in <see cref="ApplicationId"/>.
/// </summary>
public static class DiscordPresence
{
    /// <summary>The SYNDICATE Discord application (discord.com/developers). Empty would turn presence off.</summary>
    public const string ApplicationId = "1571246344931119285";
    private const string LogoAsset = "syndicate";
    private const string InviteUrl = "https://discord.gg/synfn";

    private static CancellationTokenSource? _stop;
    private static Func<(string Season, string Profile)>? _launcherState;
    private static bool _toldNoDiscord;

    /// <summary>Starts the presence loop (called once, after the main window is shown). Safe to call when off.</summary>
    public static void Start(Func<(string Season, string Profile)> launcherState)
    {
        if (ApplicationId.Length == 0 || _stop is not null) return;
        _launcherState = launcherState;
        _stop = new CancellationTokenSource();
        var token = _stop.Token;
        _ = Task.Run(() => RunAsync(token), token);
    }

    /// <summary>Clears the presence and stops (launcher closing, or the setting turned off).</summary>
    public static void Stop()
    {
        try { _stop?.Cancel(); } catch (ObjectDisposedException) { }
        _stop = null;
    }

    private static async Task RunAsync(CancellationToken token)
    {
        long launcherStarted = DateTimeOffset.UtcNow.ToUnixTimeSeconds();
        while (!token.IsCancellationRequested)
        {
            NamedPipeClientStream? pipe = null;
            try
            {
                pipe = await ConnectAsync(token);
                if (pipe is null) { if (!_toldNoDiscord) Log("Discord is not running (no discord-ipc pipe)"); _toldNoDiscord = true; await Task.Delay(TimeSpan.FromSeconds(20), token); continue; }
                await WriteFrameAsync(pipe, 0, new JsonObject { ["v"] = 1, ["client_id"] = ApplicationId }, token);
                // Discord must answer the handshake with READY before it accepts SET_ACTIVITY; activity sent earlier
                // is dropped. A CLOSE here (opcode 2) means it refused the application ID.
                var (op, reply) = await ReadFrameAsync(pipe, token).WaitAsync(TimeSpan.FromSeconds(10), token);
                if (op != 1 || !reply.Contains("\"READY\"", StringComparison.Ordinal))
                {
                    Log("handshake refused (op " + op + "): " + reply);
                    throw new IOException("Discord refused the handshake");
                }
                Log("connected to Discord");
                _ = DrainAsync(pipe, token); // Discord answers every frame; reading keeps its pipe buffer from filling

                string last = "";
                long matchStarted = 0;
                while (!token.IsCancellationRequested && pipe.IsConnected)
                {
                    var activity = BuildActivity(launcherStarted, ref matchStarted);
                    string json = activity?.ToJsonString() ?? "null";
                    if (json != last) // Discord rate-limits presence updates; only send changes
                    {
                        await WriteFrameAsync(pipe, 1, new JsonObject
                        {
                            ["cmd"] = "SET_ACTIVITY",
                            ["nonce"] = Guid.NewGuid().ToString("N"),
                            ["args"] = new JsonObject { ["pid"] = Environment.ProcessId, ["activity"] = activity }
                        }, token);
                        last = json;
                        Log("status: " + activity?["details"] + " / " + activity?["state"]);
                    }
                    await Task.Delay(TimeSpan.FromSeconds(5), token);
                }
            }
            catch (OperationCanceledException) { break; }
            catch (Exception e) { Log("disconnected: " + e.Message); /* reconnect below */ }
            finally { try { pipe?.Dispose(); } catch (Exception) { } }
            try { await Task.Delay(TimeSpan.FromSeconds(15), token); } catch (OperationCanceledException) { break; }
        }
    }

    private static JsonObject? BuildActivity(long launcherStarted, ref long matchStarted)
    {
        var (season, profile) = SafeLauncherState();
        var assets = new JsonObject { ["large_image"] = LogoAsset, ["large_text"] = "SYNDICATE" };
        var buttons = new JsonArray { new JsonObject { ["label"] = "Join Discord", ["url"] = InviteUrl } };

        if (!GameLauncher.IsRunning)
        {
            matchStarted = 0;
            return new JsonObject
            {
                ["details"] = "In the launcher",
                ["state"] = season.Length > 0 ? "Next up: " + season : "Choosing a season",
                ["timestamps"] = new JsonObject { ["start"] = launcherStarted },
                ["assets"] = assets,
                ["buttons"] = buttons
            };
        }

        var (mode, connected, accepting, ended, playersLeft, humansAlive) = ReadMatch();
        string details = season.Length > 0 ? season + (mode.Length > 0 ? " · " + mode : "") : (mode.Length > 0 ? mode : "Playing");
        string state;
        if (connected < 0) state = "Playing"; // no live counts for this build (Season X's own server)
        else if (connected == 0) { state = "In the lobby"; matchStarted = 0; }
        else if (ended) state = "Match over";
        else if (accepting) { state = "Warming up on the island"; matchStarted = 0; }
        else
        {
            if (matchStarted == 0) matchStarted = DateTimeOffset.UtcNow.ToUnixTimeSeconds();
            if (playersLeft > 0)
            {
                int bots = humansAlive >= 0 ? Math.Max(0, playersLeft - humansAlive) : -1;
                state = playersLeft + (playersLeft == 1 ? " player left" : " players left") + (bots >= 0 ? " (" + bots + (bots == 1 ? " bot)" : " bots)") : "");
            }
            else state = "In a match";
        }

        var activity = new JsonObject
        {
            ["details"] = details,
            ["state"] = state,
            ["assets"] = assets,
            ["buttons"] = buttons
        };
        if (matchStarted > 0) activity["timestamps"] = new JsonObject { ["start"] = matchStarted };
        if (profile.Length > 0) assets["large_text"] = "SYNDICATE · " + profile;
        return activity;
    }

    private static (string Season, string Profile) SafeLauncherState()
    {
        try { return _launcherState?.Invoke() ?? ("", ""); } catch (Exception) { return ("", ""); }
    }

    /// <summary>Mode and head-count from the files the match server writes for the matchmaker (read-only).</summary>
    private static (string Mode, int Connected, bool Accepting, bool Ended, int PlayersLeft, int HumansAlive) ReadMatch()
    {
        try
        {
            string? runtime = SyndicateBackend.RuntimeDirectory;
            if (runtime is null) return ("", -1, false, false, -1, -1);
            using var server = JsonDocument.Parse(ReadShared(Path.Combine(runtime, "server.json")));
            using var live = JsonDocument.Parse(ReadShared(Path.Combine(runtime, "occupancy.json")));
            var s = server.RootElement; var o = live.RootElement;
            int pid = s.TryGetProperty("Pid", out var p) ? p.GetInt32() : 0;
            if (!o.TryGetProperty("Pid", out var op) || op.GetInt32() != pid) return (ModeName(s), -1, false, false, -1, -1);
            return (ModeName(s),
                o.TryGetProperty("Connected", out var c) ? c.GetInt32() : 0,
                o.TryGetProperty("Accepting", out var a) && a.GetBoolean(),
                o.TryGetProperty("Ended", out var e) && e.GetBoolean(),
                o.TryGetProperty("PlayersLeft", out var l) ? l.GetInt32() : -1,
                o.TryGetProperty("HumansAlive", out var h) ? h.GetInt32() : -1);
        }
        catch (Exception) { return ("", -1, false, false, -1, -1); }

        static string ModeName(JsonElement s) =>
            s.TryGetProperty("Mode", out var m) && m.GetString() is { Length: > 0 } mode ? MatchOptions.PlaylistName(mode) : "";
    }

    private static string ReadShared(string path)
    {
        using var stream = new FileStream(path, FileMode.Open, FileAccess.Read, FileShare.ReadWrite | FileShare.Delete);
        using var reader = new StreamReader(stream);
        return reader.ReadToEnd();
    }

    private static async Task<NamedPipeClientStream?> ConnectAsync(CancellationToken token)
    {
        for (int i = 0; i < 10; i++)
        {
            var pipe = new NamedPipeClientStream(".", "discord-ipc-" + i, PipeDirection.InOut, PipeOptions.Asynchronous);
            try
            {
                await pipe.ConnectAsync(200, token);
                return pipe;
            }
            catch (OperationCanceledException) { pipe.Dispose(); throw; }
            catch (Exception) { pipe.Dispose(); }
        }
        return null;
    }

    private static async Task WriteFrameAsync(Stream pipe, int opcode, JsonNode payload, CancellationToken token)
    {
        byte[] body = Encoding.UTF8.GetBytes(payload.ToJsonString());
        byte[] frame = new byte[8 + body.Length];
        BinaryPrimitives.WriteInt32LittleEndian(frame.AsSpan(0), opcode);
        BinaryPrimitives.WriteInt32LittleEndian(frame.AsSpan(4), body.Length);
        body.CopyTo(frame, 8);
        await pipe.WriteAsync(frame, token);
        await pipe.FlushAsync(token);
    }

    private static async Task DrainAsync(Stream pipe, CancellationToken token)
    {
        try
        {
            while (!token.IsCancellationRequested)
            {
                var (op, body) = await ReadFrameAsync(pipe, token);
                // Discord explains a rejected activity in an ERROR event; keep it for debugging.
                if (op == 2 || body.Contains("\"evt\":\"ERROR\"", StringComparison.Ordinal)) Log("Discord said: " + body);
                if (op == 2) { pipe.Dispose(); return; }
            }
        }
        catch (Exception) { /* pipe closed: the main loop reconnects */ }
    }

    private static async Task<(int Op, string Body)> ReadFrameAsync(Stream pipe, CancellationToken token)
    {
        var header = new byte[8];
        await pipe.ReadExactlyAsync(header, token);
        int op = BinaryPrimitives.ReadInt32LittleEndian(header);
        int length = BinaryPrimitives.ReadInt32LittleEndian(header.AsSpan(4));
        if (length < 0 || length > 1 << 20) throw new IOException("bad Discord frame");
        var body = new byte[length];
        await pipe.ReadExactlyAsync(body, token);
        return (op, Encoding.UTF8.GetString(body));
    }

    /// <summary>discord-presence.log next to the launcher: connect/refuse/errors only, a few lines per session.</summary>
    private static void Log(string line)
    {
        try
        {
            string path = Path.Combine(AppContext.BaseDirectory, "discord-presence.log");
            if (File.Exists(path) && new FileInfo(path).Length > 256 * 1024) File.Delete(path);
            File.AppendAllText(path, DateTime.Now.ToString("HH:mm:ss") + "  " + line + Environment.NewLine);
        }
        catch (Exception) { }
    }
}
