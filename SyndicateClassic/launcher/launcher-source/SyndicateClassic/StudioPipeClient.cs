// Syndicate © 2026 Syndicate. All rights reserved. Proprietary — no copying, distribution, or derivative works. See LICENSE.
#if TILTED_DEVKIT
using System;
using System.Buffers.Binary;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.IO.Pipes;
using System.Text;
using System.Text.Json;
using System.Threading;
using System.Threading.Tasks;

namespace SyndicateClassic
{
    /// <summary>
    /// Named-pipe client for TiltedStudio.dll.
    /// Connects to \\.\pipe\TiltedStudio_{pid} and sends binary commands
    /// matching the protocol in StudioPipe.h.
    ///
    /// Wire format:
    ///   Request:  [cmd(1)] [payloadSize(4 LE)] [payload bytes]
    ///   Response: [cmd|0x80(1)] [respSize(4 LE)] [response bytes]
    ///
    /// All public methods are thread-safe; the pipe is used under a SemaphoreSlim
    /// so only one command is in flight at a time.
    /// </summary>
    internal sealed class StudioPipeClient : IDisposable
    {
        // ---- Command IDs (mirror StudioPipe.h) ----
        const byte CMD_PING          = 0x01;
        const byte CMD_GET_ACTORS    = 0x02;
        const byte CMD_SELECT_ACTOR  = 0x03;
        const byte CMD_GET_ACTOR_PROPS = 0x04;
        const byte CMD_SET_ACTOR_POS = 0x05;
        const byte CMD_SET_ACTOR_ROT = 0x06;
        const byte CMD_PROJECT_POINT = 0x07;
        const byte CMD_RAYCAST       = 0x08;
        const byte CMD_GET_CAMERA    = 0x09;
        const byte CMD_SET_CAMERA    = 0x0A;
        const byte CMD_FREE_CAM      = 0x0B;
        const byte CMD_GET_VIEWPORT  = 0x0C;
        const byte CMD_GAME_HWND     = 0x0D;
        const byte CMD_GET_VERSION   = 0x0E;
        const byte CMD_SPAWN_ACTOR   = 0x0F;
        const byte CMD_DELETE_ACTOR  = 0x10;
        const byte CMD_BATCH_PROJECT = 0x11;
        const byte RESPONSE_BIT     = 0x80;
        const byte STATUS_OK        = 1;

        readonly int _pid;
        NamedPipeClientStream? _pipe;
        readonly SemaphoreSlim _lock = new(1, 1);
        bool _disposed;

        public bool Connected => _pipe is { IsConnected: true };
        public int GamePid => _pid;

        public StudioPipeClient(int gamePid) { _pid = gamePid; }

        // ---- Connection ----

        /// <summary>Connect to the SyndicateStudio pipe. Retries internally for up to <paramref name="timeoutMs"/>.</summary>
        public async Task<bool> ConnectAsync(int timeoutMs = 5000, CancellationToken ct = default)
        {
            if (_disposed) return false;
            for (int waited = 0; waited < timeoutMs && !ct.IsCancellationRequested; waited += 200)
            {
                try
                {
                    var pipe = NewPipe();
                    try { await pipe.ConnectAsync(200, ct); }
                    catch { pipe.Dispose(); throw; }
                    _pipe = pipe;
                    return true;
                }
                catch (TimeoutException) { }
                catch (IOException) { await Task.Delay(200, ct); }
            }
            return false;
        }

        // ---- Raw send/receive ----

        /// <summary>Why the last command returned nothing (for the Studio's Output log).</summary>
        public string LastFailure { get; private set; } = "";

        NamedPipeClientStream NewPipe() =>
            new NamedPipeClientStream(".", $"TiltedStudio_{_pid}", PipeDirection.InOut, PipeOptions.Asynchronous);

        // Every failure after a request has been written drops the connection. The stream is framed
        // ([cmd][size][bytes]); if one reply is left half-read (a timeout, an oversize reply, a mismatched header),
        // every later reply would be read out of step and fail forever. A fresh connection is always in step:
        // the DLL starts a new pipe instance as soon as the old one closes, and the next command reconnects.
        async Task<byte[]?> SendCommandAsync(byte cmd, byte[]? payload = null, CancellationToken ct = default)
        {
            if (_disposed) { LastFailure = "the Studio pipe is closed"; return null; }
            await _lock.WaitAsync(ct);
            try
            {
                if (_pipe == null || !_pipe.IsConnected)
                {
                    Disconnect();
                    var fresh = NewPipe();
                    try { await fresh.ConnectAsync(3000, ct); }
                    catch (Exception ex)
                    {
                        fresh.Dispose();
                        LastFailure = "the game's Studio pipe isn't answering (" + ex.GetType().Name + ")";
                        return null;
                    }
                    _pipe = fresh;
                }

                var pipe = _pipe;
                // One command may take a while on the game thread (the DLL itself gives up after 10 s)
                using var timeout = CancellationTokenSource.CreateLinkedTokenSource(ct);
                timeout.CancelAfter(TimeSpan.FromSeconds(12));
                var tct = timeout.Token;

                // Write: [cmd(1)] [payloadSize(4)] [payload]
                var payloadLen = payload?.Length ?? 0;
                var frame = new byte[5 + payloadLen];
                frame[0] = cmd;
                BinaryPrimitives.WriteUInt32LittleEndian(frame.AsSpan(1), (uint)payloadLen);
                if (payloadLen > 0) Buffer.BlockCopy(payload!, 0, frame, 5, payloadLen);
                await pipe.WriteAsync(frame, 0, frame.Length, tct);
                await pipe.FlushAsync(tct);

                // Read response: [cmd|0x80(1)] [respSize(4)] [response]
                var respHeader = new byte[5];
                if (!await ReadExactAsync(pipe, respHeader, 5, tct)) return Fail("the game closed the Studio pipe (it was busy too long)");
                if (respHeader[0] != (cmd | RESPONSE_BIT)) return Fail($"out-of-step reply (0x{respHeader[0]:X2} for 0x{cmd:X2})");
                var respSize = BinaryPrimitives.ReadUInt32LittleEndian(respHeader.AsSpan(1));
                if (respSize > 16 * 1024 * 1024) return Fail("reply too large (" + respSize + " bytes)");
                var resp = new byte[respSize];
                if (respSize > 0 && !await ReadExactAsync(pipe, resp, (int)respSize, tct)) return Fail("reply cut short");
                LastFailure = "";
                return resp;
            }
            catch (OperationCanceledException) when (!ct.IsCancellationRequested) { return Fail("the game took longer than 12 s to answer"); }
            catch (Exception ex) { return Fail(ex.GetType().Name + ": " + ex.Message); }
            finally { _lock.Release(); }
        }

        byte[]? Fail(string why) { LastFailure = why; Disconnect(); return null; }

        static async Task<bool> ReadExactAsync(Stream s, byte[] buf, int count, CancellationToken ct)
        {
            int read = 0;
            while (read < count)
            {
                int got = await s.ReadAsync(buf, read, count - read, ct);
                if (got == 0) return false;
                read += got;
            }
            return true;
        }

        // ---- Payload builders ----

        static byte[] PackU32(uint v) { var b = new byte[4]; BinaryPrimitives.WriteUInt32LittleEndian(b, v); return b; }
        static byte[] PackFloat(float v) { var b = new byte[4]; BinaryPrimitives.WriteSingleLittleEndian(b, v); return b; }

        static byte[] PackU32F3(uint idx, float a, float b, float c)
        {
            var buf = new byte[16];
            BinaryPrimitives.WriteUInt32LittleEndian(buf, idx);
            BinaryPrimitives.WriteSingleLittleEndian(buf.AsSpan(4), a);
            BinaryPrimitives.WriteSingleLittleEndian(buf.AsSpan(8), b);
            BinaryPrimitives.WriteSingleLittleEndian(buf.AsSpan(12), c);
            return buf;
        }

        static float ReadFloat(byte[] buf, int off) => BinaryPrimitives.ReadSingleLittleEndian(buf.AsSpan(off));
        static uint ReadU32(byte[] buf, int off) => BinaryPrimitives.ReadUInt32LittleEndian(buf.AsSpan(off));

        // ---- Public API ----

        /// <summary>Ping the DLL. Returns true if it responds.</summary>
        public async Task<bool> PingAsync(CancellationToken ct = default)
        {
            var resp = await SendCommandAsync(CMD_PING, null, ct);
            return resp != null && resp.Length >= 1 && resp[0] == STATUS_OK;
        }

        /// <summary>Refresh the actor list in the DLL and return it as parsed JSON items.</summary>
        public async Task<List<StudioActor>?> GetActorsAsync(CancellationToken ct = default)
        {
            var resp = await SendCommandAsync(CMD_GET_ACTORS, null, ct);
            if (resp == null || resp.Length == 0) return null;
            try
            {
                var json = Encoding.UTF8.GetString(resp);
                var items = new List<StudioActor>();
                using var doc = JsonDocument.Parse(json);
                foreach (var el in doc.RootElement.EnumerateArray())
                {
                    items.Add(new StudioActor
                    {
                        Index = el.TryGetProperty("i", out var i) ? i.GetUInt32() : 0,
                        Name = el.TryGetProperty("n", out var n) ? n.GetString() ?? "" : "",
                        ClassName = el.TryGetProperty("c", out var c) ? c.GetString() ?? "" : "",
                        X = el.TryGetProperty("x", out var x) ? x.GetSingle() : 0,
                        Y = el.TryGetProperty("y", out var y) ? y.GetSingle() : 0,
                        Z = el.TryGetProperty("z", out var z) ? z.GetSingle() : 0,
                        Pitch = el.TryGetProperty("p", out var p) ? p.GetSingle() : 0,
                        Yaw = el.TryGetProperty("w", out var w) ? w.GetSingle() : 0,
                        Roll = el.TryGetProperty("r", out var r) ? r.GetSingle() : 0,
                    });
                }
                return items;
            }
            catch { return null; }
        }

        /// <summary>Get detailed properties for one actor by index.</summary>
        public async Task<StudioActor?> GetActorPropsAsync(uint index, CancellationToken ct = default)
        {
            var resp = await SendCommandAsync(CMD_GET_ACTOR_PROPS, PackU32(index), ct);
            if (resp == null || resp.Length <= 1) return null;
            try
            {
                var json = Encoding.UTF8.GetString(resp);
                using var doc = JsonDocument.Parse(json);
                var el = doc.RootElement;
                return new StudioActor
                {
                    Index = el.TryGetProperty("i", out var i) ? i.GetUInt32() : index,
                    Name = el.TryGetProperty("n", out var n) ? n.GetString() ?? "" : "",
                    ClassName = el.TryGetProperty("c", out var c) ? c.GetString() ?? "" : "",
                    X = el.TryGetProperty("x", out var x) ? x.GetSingle() : 0,
                    Y = el.TryGetProperty("y", out var y) ? y.GetSingle() : 0,
                    Z = el.TryGetProperty("z", out var z) ? z.GetSingle() : 0,
                    Pitch = el.TryGetProperty("p", out var p) ? p.GetSingle() : 0,
                    Yaw = el.TryGetProperty("w", out var w) ? w.GetSingle() : 0,
                    Roll = el.TryGetProperty("r", out var r) ? r.GetSingle() : 0,
                };
            }
            catch { return null; }
        }

        /// <summary>Move an actor by its cached index.</summary>
        public async Task<bool> SetActorPositionAsync(uint index, float x, float y, float z, CancellationToken ct = default)
        {
            var resp = await SendCommandAsync(CMD_SET_ACTOR_POS, PackU32F3(index, x, y, z), ct);
            return resp != null && resp.Length >= 1 && resp[0] == STATUS_OK;
        }

        /// <summary>Rotate an actor by its cached index.</summary>
        public async Task<bool> SetActorRotationAsync(uint index, float pitch, float yaw, float roll, CancellationToken ct = default)
        {
            var resp = await SendCommandAsync(CMD_SET_ACTOR_ROT, PackU32F3(index, pitch, yaw, roll), ct);
            return resp != null && resp.Length >= 1 && resp[0] == STATUS_OK;
        }

        /// <summary>Project a world location to screen coordinates.</summary>
        public async Task<(float ScreenX, float ScreenY, bool OnScreen)?> ProjectPointAsync(float wx, float wy, float wz, CancellationToken ct = default)
        {
            var payload = new byte[12];
            BinaryPrimitives.WriteSingleLittleEndian(payload.AsSpan(0), wx);
            BinaryPrimitives.WriteSingleLittleEndian(payload.AsSpan(4), wy);
            BinaryPrimitives.WriteSingleLittleEndian(payload.AsSpan(8), wz);
            var resp = await SendCommandAsync(CMD_PROJECT_POINT, payload, ct);
            if (resp == null || resp.Length < 9) return null;
            return (ReadFloat(resp, 0), ReadFloat(resp, 4), resp[8] != 0);
        }

        /// <summary>Batch-project world points to screen coordinates.</summary>
        public async Task<(float X, float Y, bool OnScreen)[]?> BatchProjectAsync(float[] worldXYZ, CancellationToken ct = default)
        {
            if (worldXYZ.Length % 3 != 0) return null;
            uint count = (uint)(worldXYZ.Length / 3);
            var payload = new byte[4 + count * 12];
            BinaryPrimitives.WriteUInt32LittleEndian(payload, count);
            for (int i = 0; i < (int)count; i++)
            {
                BinaryPrimitives.WriteSingleLittleEndian(payload.AsSpan(4 + i * 12), worldXYZ[i * 3]);
                BinaryPrimitives.WriteSingleLittleEndian(payload.AsSpan(4 + i * 12 + 4), worldXYZ[i * 3 + 1]);
                BinaryPrimitives.WriteSingleLittleEndian(payload.AsSpan(4 + i * 12 + 8), worldXYZ[i * 3 + 2]);
            }
            var resp = await SendCommandAsync(CMD_BATCH_PROJECT, payload, ct);
            if (resp == null) return null;
            // Each result: float sx, float sy, byte onScreen = 9 bytes
            if (resp.Length < count * 9) return null;
            var results = new (float, float, bool)[count];
            for (int i = 0; i < (int)count; i++)
            {
                int off = i * 9;
                results[i] = (ReadFloat(resp, off), ReadFloat(resp, off + 4), resp[off + 8] != 0);
            }
            return results;
        }

        /// <summary>Pick the nearest actor to a screen point (20 px hit radius). Returns actor index or null.</summary>
        public async Task<uint?> RaycastAsync(float screenX, float screenY, CancellationToken ct = default)
        {
            var payload = new byte[8];
            BinaryPrimitives.WriteSingleLittleEndian(payload.AsSpan(0), screenX);
            BinaryPrimitives.WriteSingleLittleEndian(payload.AsSpan(4), screenY);
            var resp = await SendCommandAsync(CMD_RAYCAST, payload, ct);
            if (resp == null || resp.Length < 4) return null;
            var idx = ReadU32(resp, 0);
            return idx == 0xFFFFFFFF ? null : idx;
        }

        /// <summary>Get the current camera state (position, rotation, FOV).</summary>
        public async Task<StudioCameraState?> GetCameraAsync(CancellationToken ct = default)
        {
            var resp = await SendCommandAsync(CMD_GET_CAMERA, null, ct);
            if (resp == null) return null;
            if (resp.Length < 28) { LastFailure = "short camera reply"; return null; }
            // Byte 28 (newer DLL): 0 = the game has no player camera yet (still loading, or between worlds)
            if (resp.Length >= 29 && resp[28] == 0) { LastFailure = "the game has no player camera yet — wait until you are in the match"; return null; }
            return new StudioCameraState
            {
                X = ReadFloat(resp, 0), Y = ReadFloat(resp, 4), Z = ReadFloat(resp, 8),
                Pitch = ReadFloat(resp, 12), Yaw = ReadFloat(resp, 16), Roll = ReadFloat(resp, 20),
                FOV = ReadFloat(resp, 24),
            };
        }

        /// <summary>Set the free camera position and rotation.</summary>
        public async Task<bool> SetCameraAsync(float x, float y, float z, float pitch, float yaw, float roll, CancellationToken ct = default)
        {
            var payload = new byte[24];
            BinaryPrimitives.WriteSingleLittleEndian(payload.AsSpan(0), x);
            BinaryPrimitives.WriteSingleLittleEndian(payload.AsSpan(4), y);
            BinaryPrimitives.WriteSingleLittleEndian(payload.AsSpan(8), z);
            BinaryPrimitives.WriteSingleLittleEndian(payload.AsSpan(12), pitch);
            BinaryPrimitives.WriteSingleLittleEndian(payload.AsSpan(16), yaw);
            BinaryPrimitives.WriteSingleLittleEndian(payload.AsSpan(20), roll);
            var resp = await SendCommandAsync(CMD_SET_CAMERA, payload, ct);
            return resp != null && resp.Length >= 1 && resp[0] == STATUS_OK;
        }

        /// <summary>Enable or disable free camera mode in the DLL.</summary>
        public async Task<bool> SetFreeCamAsync(bool enable, CancellationToken ct = default)
        {
            var resp = await SendCommandAsync(CMD_FREE_CAM, new[] { (byte)(enable ? 1 : 0) }, ct);
            return resp != null && resp.Length >= 1 && resp[0] == STATUS_OK;
        }

        /// <summary>Get the game viewport dimensions in pixels.</summary>
        public async Task<(uint Width, uint Height)?> GetViewportAsync(CancellationToken ct = default)
        {
            var resp = await SendCommandAsync(CMD_GET_VIEWPORT, null, ct);
            if (resp == null || resp.Length < 8) return null;
            return (ReadU32(resp, 0), ReadU32(resp, 4));
        }

        /// <summary>Get the game window HWND for embedding.</summary>
        public async Task<IntPtr> GetGameHwndAsync(CancellationToken ct = default)
        {
            var resp = await SendCommandAsync(CMD_GAME_HWND, null, ct);
            if (resp == null || resp.Length < 8) return IntPtr.Zero;
            var val = BinaryPrimitives.ReadUInt64LittleEndian(resp);
            return (IntPtr)val;
        }

        /// <summary>Get the Fortnite version the DLL detected.</summary>
        public async Task<float> GetVersionAsync(CancellationToken ct = default)
        {
            var resp = await SendCommandAsync(CMD_GET_VERSION, null, ct);
            if (resp == null || resp.Length < 4) return 0;
            return ReadFloat(resp, 0);
        }

        /// <summary>Spawn an actor by class path at a world location. Returns its index, or null on failure.</summary>
        public async Task<uint?> SpawnActorAsync(string classPath, float x, float y, float z, CancellationToken ct = default)
        {
            var classBytes = Encoding.UTF8.GetBytes(classPath + '\0');
            var payload = new byte[classBytes.Length + 12];
            Array.Copy(classBytes, payload, classBytes.Length);
            BinaryPrimitives.WriteSingleLittleEndian(payload.AsSpan(classBytes.Length), x);
            BinaryPrimitives.WriteSingleLittleEndian(payload.AsSpan(classBytes.Length + 4), y);
            BinaryPrimitives.WriteSingleLittleEndian(payload.AsSpan(classBytes.Length + 8), z);
            var resp = await SendCommandAsync(CMD_SPAWN_ACTOR, payload, ct);
            if (resp == null || resp.Length < 4) return null;
            var idx = ReadU32(resp, 0);
            return idx == 0xFFFFFFFF ? null : idx;
        }

        /// <summary>Destroy an actor by its cached index.</summary>
        public async Task<bool> DeleteActorAsync(uint index, CancellationToken ct = default)
        {
            var resp = await SendCommandAsync(CMD_DELETE_ACTOR, PackU32(index), ct);
            return resp != null && resp.Length >= 1 && resp[0] == STATUS_OK;
        }

        // ---- Lifecycle ----

        public void Disconnect()
        {
            try { _pipe?.Dispose(); } catch { }
            _pipe = null;
        }

        public void Dispose()
        {
            if (_disposed) return;
            _disposed = true;
            Disconnect();
            // _lock is not disposed: a command may still be awaiting it, and SemaphoreSlim needs no cleanup
        }

        // ---- Injection helper ----

        /// <summary>
        /// Inject TiltedStudio.dll into the game process and connect the pipe.
        /// The DLL path should be next to the launcher (e.g. AppContext.BaseDirectory + "TiltedStudio.dll").
        /// Uses the existing NativeMethods.InjectDll mechanism.
        /// </summary>
        public static async Task<StudioPipeClient?> InjectAndConnectAsync(int gamePid, string dllPath, Action<string>? log = null, CancellationToken ct = default)
        {
            if (!File.Exists(dllPath)) { log?.Invoke("TiltedStudio.dll not found: " + dllPath); return null; }

            try
            {
                await Task.Run(() => NativeMethods.InjectDll((uint)gamePid, dllPath), ct);
                log?.Invoke("TiltedStudio.dll injected into game (pid " + gamePid + ").");
            }
            catch (Exception ex) { log?.Invoke("TiltedStudio.dll injection failed: " + ex.Message); return null; }

            // Give the DLL a moment to start its pipe server (it waits for the game window first)
            var client = new StudioPipeClient(gamePid);
            if (await client.ConnectAsync(timeoutMs: 10000, ct))
            {
                log?.Invoke("Studio pipe connected.");
                return client;
            }

            log?.Invoke("Studio pipe did not appear within 10 s — the DLL may still be waiting for the game window.");
            client.Dispose();
            return null;
        }
    }

    // ---- Data records ----

    internal class StudioActor
    {
        public uint Index { get; set; }
        public string Name { get; set; } = "";
        public string ClassName { get; set; } = "";
        public float X { get; set; }
        public float Y { get; set; }
        public float Z { get; set; }
        public float Pitch { get; set; }
        public float Yaw { get; set; }
        public float Roll { get; set; }
    }

    internal class StudioCameraState
    {
        public float X { get; set; }
        public float Y { get; set; }
        public float Z { get; set; }
        public float Pitch { get; set; }
        public float Yaw { get; set; }
        public float Roll { get; set; }
        public float FOV { get; set; }
    }
}
#endif
