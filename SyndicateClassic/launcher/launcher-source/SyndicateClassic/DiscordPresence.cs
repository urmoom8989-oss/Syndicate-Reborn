// Syndicate © 2026 Syndicate. All rights reserved. Proprietary — no copying, distribution, or derivative works. See LICENSE.
using System;
using System.Buffers.Binary;
using System.IO;
using System.IO.Pipes;
using System.Text;
using System.Text.Json.Nodes;
using System.Threading;
using System.Threading.Tasks;

namespace SyndicateClassic;

internal sealed record PresenceInfo(
    string Details,
    string State,
    DateTimeOffset? StartedAt = null,
    int? PartySize = null,
    int? PartyMax = null,
    int? OwnerPid = null);

// Discord Rich Presence over Discord's local IPC pipe (\\.\pipe\discord-ipc-N). No packages, no
// account credentials; nothing leaves the PC except to the Discord app already running. Runs while
// the launcher is open, reconnects when Discord starts later, and shows a "Join Discord" button.
// What to show is decided by MainWindow (SyndicatePresence.cs); this class only keeps Discord in sync
// with the latest PresenceInfo.
internal sealed class DiscordPresence : IDisposable
{
    private const string ApplicationId = "1561368778653302965";
    internal const string DiscordInvite = "https://discord.gg/DvatfqjJUg";
    private const int OpHandshake = 0, OpFrame = 1, OpClose = 2, OpPing = 3, OpPong = 4;

    private readonly CancellationTokenSource _cts = new();
    private readonly SemaphoreSlim _writeLock = new(1, 1);
    private readonly object _stateLock = new();
    private NamedPipeClientStream? _pipe;
    private PresenceInfo? _desired;
    private bool _dirty, _enabled = true, _acceptedLogged;
    private Task? _loop;
    private readonly Action<string> _log;

    internal DiscordPresence(Action<string> log) { _log = log; }

    internal bool IsConnected => _pipe is { IsConnected: true };

    internal void Start(bool enabled)
    {
        lock (_stateLock) { _enabled = enabled; _dirty = true; }
        _loop ??= Task.Run(() => LoopAsync(_cts.Token));
    }

    internal void SetEnabled(bool enabled) { lock (_stateLock) { _enabled = enabled; _dirty = true; } }

    internal void Set(PresenceInfo? info)
    {
        lock (_stateLock)
        {
            if (info == _desired) return;
            _desired = info; _dirty = true;
        }
    }

    private async Task LoopAsync(CancellationToken ct)
    {
        while (!ct.IsCancellationRequested)
        {
            try
            {
                bool enabled; lock (_stateLock) enabled = _enabled;
                if (!enabled)
                {
                    if (IsConnected) { await SendActivityAsync(null, ct); Disconnect(); }
                    await Task.Delay(1500, ct);
                    continue;
                }
                if (!IsConnected)
                {
                    if (!await ConnectAsync(ct)) { await Task.Delay(15000, ct); continue; } // Discord not running: retry quietly
                    lock (_stateLock) _dirty = true;
                }
                PresenceInfo? send; bool push;
                lock (_stateLock) { push = _dirty; _dirty = false; send = _desired; }
                if (push) await SendActivityAsync(send, ct);
                await Task.Delay(1000, ct);
            }
            catch (OperationCanceledException) { break; }
            catch (Exception)
            {
                Disconnect();
                try { await Task.Delay(5000, ct); } catch { break; }
            }
        }
    }

    private async Task<bool> ConnectAsync(CancellationToken ct)
    {
        for (int i = 0; i < 10; i++)
        {
            var pipe = new NamedPipeClientStream(".", $"discord-ipc-{i}", PipeDirection.InOut, PipeOptions.Asynchronous);
            try
            {
                await pipe.ConnectAsync(300, ct);
                await WriteFrameAsync(pipe, OpHandshake, new JsonObject { ["v"] = 1, ["client_id"] = ApplicationId }, ct);
                var (op, json) = await ReadFrameAsync(pipe, ct);
                if (op == OpClose || json.Contains("\"code\":4000", StringComparison.Ordinal)) { pipe.Dispose(); return false; }
                _pipe = pipe;
                _ = Task.Run(() => ReadLoopAsync(pipe, ct));
                return true;
            }
            catch (OperationCanceledException) when (ct.IsCancellationRequested) { pipe.Dispose(); throw; }
            catch { pipe.Dispose(); }
        }
        return false;
    }

    // Drains replies so the pipe never fills; answers pings.
    private async Task ReadLoopAsync(NamedPipeClientStream pipe, CancellationToken ct)
    {
        try
        {
            while (!ct.IsCancellationRequested && pipe.IsConnected)
            {
                var (op, json) = await ReadFrameAsync(pipe, ct);
                if (op == OpPing)
                {
                    await _writeLock.WaitAsync(ct);
                    try { await WriteRawAsync(pipe, OpPong, Encoding.UTF8.GetBytes(json), ct); }
                    finally { _writeLock.Release(); }
                }
                else if (op == OpClose) break;
                else if (json.Contains("\"evt\":\"ERROR\"", StringComparison.Ordinal)) _log("Discord status: " + (json.Length > 200 ? json[..200] : json));
                else if (json.Contains("\"cmd\":\"SET_ACTIVITY\"", StringComparison.Ordinal) && !_acceptedLogged) { _acceptedLogged = true; _log("Discord status connected."); }
            }
        }
        catch { }
        if (ReferenceEquals(pipe, _pipe)) Disconnect();
    }

    private async Task SendActivityAsync(PresenceInfo? p, CancellationToken ct)
    {
        var pipe = _pipe;
        if (pipe is not { IsConnected: true }) return;
        JsonNode? activity = null;
        if (p != null)
        {
            var a = new JsonObject
            {
                ["details"] = Clamp(p.Details),
                ["state"] = Clamp(p.State),
                // "logo" is the art asset uploaded to the Syndicate Discord application (Rich Presence > Art Assets).
                ["assets"] = new JsonObject { ["large_image"] = "logo", ["large_text"] = "Project Tilted" },
                ["instance"] = false,
                ["buttons"] = new JsonArray(new JsonObject { ["label"] = "Join Discord", ["url"] = DiscordInvite }),
            };
            if (p.StartedAt is { } t) a["timestamps"] = new JsonObject { ["start"] = t.ToUnixTimeSeconds() };
            if (p.PartySize is > 0 && p.PartyMax is > 0 && p.PartyMax >= p.PartySize)
                a["party"] = new JsonObject { ["id"] = "syndicate-match", ["size"] = new JsonArray(p.PartySize.Value, p.PartyMax.Value) };
            activity = a;
        }
        var payload = new JsonObject
        {
            ["cmd"] = "SET_ACTIVITY",
            ["nonce"] = Guid.NewGuid().ToString("N"),
            // Tied to Fortnite's process while it runs, so Discord shows Syndicate instead of its own "Fortnite" card.
            ["args"] = new JsonObject { ["pid"] = p?.OwnerPid ?? Environment.ProcessId, ["activity"] = activity },
        };
        await _writeLock.WaitAsync(ct);
        try { await WriteFrameAsync(pipe, OpFrame, payload, ct); }
        finally { _writeLock.Release(); }
    }

    // Discord requires 2..128 characters for details and state.
    private static string Clamp(string s)
    {
        s = (s ?? "").Trim();
        if (s.Length < 2) s = (s + "  ")[..2];
        return s.Length > 128 ? s[..127] + "…" : s;
    }

    private static Task WriteFrameAsync(Stream s, int op, JsonNode body, CancellationToken ct) => WriteRawAsync(s, op, Encoding.UTF8.GetBytes(body.ToJsonString()), ct);

    private static async Task WriteRawAsync(Stream s, int op, byte[] data, CancellationToken ct)
    {
        byte[] buf = new byte[8 + data.Length];
        BinaryPrimitives.WriteInt32LittleEndian(buf.AsSpan(0, 4), op);
        BinaryPrimitives.WriteInt32LittleEndian(buf.AsSpan(4, 4), data.Length);
        data.CopyTo(buf, 8);
        await s.WriteAsync(buf, ct);
        await s.FlushAsync(ct);
    }

    private static async Task<(int Op, string Json)> ReadFrameAsync(Stream s, CancellationToken ct)
    {
        byte[] header = new byte[8];
        await s.ReadExactlyAsync(header, ct);
        int op = BinaryPrimitives.ReadInt32LittleEndian(header.AsSpan(0, 4));
        int len = BinaryPrimitives.ReadInt32LittleEndian(header.AsSpan(4, 4));
        if (len is < 0 or > 1 << 20) throw new IOException("Bad Discord frame.");
        byte[] body = new byte[len];
        await s.ReadExactlyAsync(body, ct);
        return (op, Encoding.UTF8.GetString(body));
    }

    private void Disconnect()
    {
        var p = _pipe; _pipe = null;
        if (p != null) try { p.Dispose(); } catch { }
    }

    // Clears the status and closes the pipe (on exit).
    public void Dispose()
    {
        try
        {
            var pipe = _pipe;
            if (pipe is { IsConnected: true } && _writeLock.Wait(500))
            {
                try
                {
                    var payload = new JsonObject { ["cmd"] = "SET_ACTIVITY", ["nonce"] = Guid.NewGuid().ToString("N"), ["args"] = new JsonObject { ["pid"] = Environment.ProcessId, ["activity"] = null } };
                    WriteFrameAsync(pipe, OpFrame, payload, CancellationToken.None).Wait(800);
                }
                finally { _writeLock.Release(); }
            }
        }
        catch { }
        _cts.Cancel();
        Disconnect();
    }
}