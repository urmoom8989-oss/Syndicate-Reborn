// Syndicate © 2026 Syndicate. All rights reserved. Proprietary — no copying, distribution, or derivative works. See LICENSE.
using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Net;
using System.Net.Sockets;
using System.Security.Cryptography;
using System.Text.Json;
using System.Threading.Tasks;

namespace SyndicateClassic;

// Direct play: play with a friend over the internet without Radmin (like Hytale's "Allow other players to join").
//
// Host: the launcher asks the router to open Syndicate's ports (UPnP), finds the PC's public IP and makes a share code:
// the public IP plus a random 128-bit join key. The key goes to the backend (Runtime/Server/direct-host.json); any
// request that comes from the internet without it is refused (see backend/src/Utils/syndicateDirect.js).
// Guest: pasting the code saves the host's IP (the same place a Radmin IP goes) and the key, which every request to
// the host then carries.
//
// The code is not plain text, but it still leads to the host's IP: anyone who joins can see it. Friends only.
internal static class DirectPlay
{
    // TCP: lobby/backend 3551, account transfer + playlist 3552, chat relay 70, matchmaker 6969. UDP: the match 7777.
    private static readonly (int Port, string Protocol)[] Ports = { (3551, "TCP"), (3552, "TCP"), (70, "TCP"), (6969, "TCP"), (7777, "UDP") };
    private const string MappingName = "Syndicate direct play";
    private const string CodePrefix = "TILT-";

    internal sealed record HostResult(bool Ok, string Code, string PublicIp, string Message);

    private static string SettingsFile(LauncherPaths paths) => Path.Combine(paths.Root, "Runtime", "Server", "direct-host.json");
    // expires: ms since 1970. A code works for CodeHours, then the backend refuses it and the launcher closes direct play.
    private sealed record HostSettings(bool on, string key, long expires);
    internal const int CodeHours = 6;
    internal static readonly TimeSpan IdleClose = TimeSpan.FromMinutes(30);

    internal static bool IsOn(LauncherPaths paths)
    {
        try { return JsonSerializer.Deserialize<HostSettings>(File.ReadAllText(SettingsFile(paths)))?.on == true; } catch { return false; }
    }

    internal static DateTime? ExpiresAt(LauncherPaths paths)
    {
        try { var s = JsonSerializer.Deserialize<HostSettings>(File.ReadAllText(SettingsFile(paths))); return s?.on == true && s.expires > 0 ? DateTimeOffset.FromUnixTimeMilliseconds(s.expires).LocalDateTime : null; } catch { return null; }
    }

    private static string CurrentKey(LauncherPaths paths)
    {
        try { var s = JsonSerializer.Deserialize<HostSettings>(File.ReadAllText(SettingsFile(paths))); return s?.on == true ? s.key ?? "" : ""; } catch { return ""; }
    }

    private static void WriteSettings(LauncherPaths paths, bool on, string key)
    {
        var file = SettingsFile(paths);
        Directory.CreateDirectory(Path.GetDirectoryName(file)!);
        long expires = on ? DateTimeOffset.UtcNow.AddHours(CodeHours).ToUnixTimeMilliseconds() : 0;
        File.WriteAllText(file + ".tmp", JsonSerializer.Serialize(new HostSettings(on, key, expires)));
        File.Move(file + ".tmp", file, overwrite: true);
    }

    // ---- Join approval ------------------------------------------------------------------------------------------------
    // A friend with the right code still waits until the host presses Allow. The backend lists who is waiting
    // (direct-pending.json); the host's answers go to direct-approved.json, tied to the current code (a new code starts
    // a fresh list).
    internal sealed record Pending(string Ip, string Name);
    private sealed class Approvals { public string key { get; set; } = ""; public Dictionary<string, string> allowed { get; set; } = new(); public Dictionary<string, string> denied { get; set; } = new(); }
    private static string ApprovedFile(LauncherPaths paths) => Path.Combine(paths.Root, "Runtime", "Server", "direct-approved.json");
    private static string PendingFile(LauncherPaths paths) => Path.Combine(paths.Root, "Runtime", "Server", "direct-pending.json");
    private static string ActivityFile(LauncherPaths paths) => Path.Combine(paths.Root, "Runtime", "Server", "direct-activity.json");

    private static Approvals ReadApprovals(LauncherPaths paths)
    {
        var key = CurrentKey(paths);
        try { var a = JsonSerializer.Deserialize<Approvals>(File.ReadAllText(ApprovedFile(paths))); if (a != null && a.key == key) return a; } catch { }
        return new Approvals { key = key };
    }

    private static void WriteApprovals(LauncherPaths paths, Approvals a)
    {
        var file = ApprovedFile(paths);
        Directory.CreateDirectory(Path.GetDirectoryName(file)!);
        File.WriteAllText(file + ".tmp", JsonSerializer.Serialize(a));
        File.Move(file + ".tmp", file, overwrite: true);
    }

    // People who have the code and are waiting for an answer (asked within the last 2 minutes).
    internal static List<Pending> Waiting(LauncherPaths paths)
    {
        var list = new List<Pending>();
        if (!IsOn(paths)) return list;
        var a = ReadApprovals(paths);
        try
        {
            using var doc = JsonDocument.Parse(File.ReadAllText(PendingFile(paths)));
            long now = DateTimeOffset.UtcNow.ToUnixTimeMilliseconds();
            foreach (var p in doc.RootElement.EnumerateObject())
            {
                if (a.allowed.ContainsKey(p.Name) || a.denied.ContainsKey(p.Name)) continue;
                long last = p.Value.TryGetProperty("last", out var l) && l.TryGetInt64(out var v) ? v : 0;
                if (now - last > 120000) continue;
                string name = p.Value.TryGetProperty("name", out var n) ? n.GetString() ?? "" : "";
                list.Add(new Pending(p.Name, name));
            }
        }
        catch { }
        return list;
    }

    internal static void Answer(LauncherPaths paths, Pending who, bool allow)
    {
        var a = ReadApprovals(paths);
        var label = who.Name.Length > 0 ? who.Name : "unknown player";
        a.allowed.Remove(who.Ip); a.denied.Remove(who.Ip);
        if (allow) a.allowed[who.Ip] = label; else a.denied[who.Ip] = label;
        WriteApprovals(paths, a);
    }

    // Players let in with the current code: name per address (the address is never shown in the launcher).
    internal static List<Pending> Allowed(LauncherPaths paths) => ReadApprovals(paths).allowed.Select(p => new Pending(p.Key, p.Value)).ToList();

    // When a let-in friend last talked to this PC (or null if nobody has yet).
    internal static DateTime? LastActivity(LauncherPaths paths)
    {
        try { using var doc = JsonDocument.Parse(File.ReadAllText(ActivityFile(paths))); return DateTimeOffset.FromUnixTimeMilliseconds(doc.RootElement.GetProperty("at").GetInt64()).UtcDateTime; } catch { return null; }
    }

    // ---- Share code: "TILT-" + base64url(version, IPv4, key) -------------------------------------------------------
    internal static string MakeCode(IPAddress ip, string keyHex)
    {
        var bytes = new List<byte> { 1 };
        bytes.AddRange(ip.GetAddressBytes());
        bytes.AddRange(Convert.FromHexString(keyHex));
        return CodePrefix + Convert.ToBase64String(bytes.ToArray()).TrimEnd('=').Replace('+', '-').Replace('/', '_');
    }

    internal static bool TryReadCode(string text, out string host, out string key)
    {
        host = ""; key = "";
        text = (text ?? "").Trim();
        if (text.StartsWith(CodePrefix, StringComparison.OrdinalIgnoreCase)) text = text[CodePrefix.Length..];
        text = text.Replace('-', '+').Replace('_', '/');
        while (text.Length % 4 != 0) text += "=";
        byte[] bytes;
        try { bytes = Convert.FromBase64String(text); } catch { return false; }
        if (bytes.Length != 21 || bytes[0] != 1) return false;
        var ip = new IPAddress(bytes[1..5]);
        if (!PublicAddress(ip)) return false;
        host = ip.ToString();
        key = Convert.ToHexString(bytes[5..]).ToLowerInvariant();
        return true;
    }

    // ---- Host --------------------------------------------------------------------------------------------------------
    internal static async Task<HostResult> Open(LauncherPaths paths)
    {
        return await Task.Run(() =>
        {
            var local = LocalAddress();
            if (local == null) return new HostResult(false, "", "", "This PC has no network connection.");
            // Our own UPnP first (UpnpClient.cs, like games do); Windows' built-in helper only if it found no router.
            var own = UpnpClient.Discover(local, out var ownWhy);
            if (own != null)
            {
                var ownFailed = new List<string>(); string lastError = "";
                foreach (var (port, protocol) in Ports)
                    if (!own.Map(port, protocol, local, MappingName, LeaseSeconds)) { ownFailed.Add(protocol + " " + port); lastError = own.LastError; }
                if (ownFailed.Count < Ports.Length)
                {
                    _upnp = own; _upnpLocal = local; StartRenewing();
                    return Finish(paths, own.ExternalAddress(), ownFailed, lastError);
                }
                ownWhy = "Your router refused every port (" + UpnpClient.Explain(lastError) + ").";
            }
            dynamic? mappings;
            try
            {
                var type = Type.GetTypeFromProgID("HNetCfg.NATUPnP");
                if (type == null) return new HostResult(false, "", "", "Windows' UPnP support is missing on this PC.");
                dynamic nat = Activator.CreateInstance(type)!;
                mappings = nat.StaticPortMappingCollection;
            }
            catch (Exception error) { return new HostResult(false, "", "", ownWhy + " Windows' UPnP did not answer either (" + error.Message + "). " + Guide); }
            if (mappings == null)
                return new HostResult(false, "", "", ownWhy + " " + Guide);
            string external = "";
            var failed = new List<string>();
            foreach (var (port, protocol) in Ports)
            {
                try
                {
                    try { mappings.Remove(port, protocol); } catch { }
                    dynamic mapping = mappings.Add(port, protocol, port, local.ToString(), true, MappingName);
                    if (external.Length == 0) { try { external = (string)mapping.ExternalIPAddress; } catch { } }
                }
                catch { failed.Add(protocol + " " + port); }
            }
            if (failed.Count == Ports.Length)
                return new HostResult(false, "", "", ownWhy + " " + Guide);
            return Finish(paths, external, failed, "");
        });
    }

    // One shared ending for both ways of opening the ports: check the public address and make the share code.
    private static HostResult Finish(LauncherPaths paths, string external, List<string> failed, string lastError)
    {
        {
            if (!IPAddress.TryParse(external, out var publicIp) || !PublicAddress(publicIp))
            {
                Close(paths);
                return new HostResult(false, "", external, "Your router is behind another router or your internet provider shares one address between customers (" + (external.Length > 0 ? external : "no public address") + "). Friends can't reach this PC directly; use Radmin.");
            }
            var key = Convert.ToHexString(RandomNumberGenerator.GetBytes(16)).ToLowerInvariant();
            try { File.Delete(ApprovedFile(paths)); File.Delete(PendingFile(paths)); File.Delete(ActivityFile(paths)); } catch { }
            WriteSettings(paths, true, key);
            var note = failed.Count > 0 ? " Some ports did not open (" + string.Join(", ", failed) + (lastError.Length > 0 ? ": " + UpnpClient.Explain(lastError) : "") + "); joining may fail." : "";
            return new HostResult(true, MakeCode(publicIp, key), publicIp.ToString(), "Connected via UPnP. Ports are open while Direct play is on." + note);
        }
    }

    // Openings made by UpnpClient last LeaseSeconds and are renewed every RenewEvery while Direct play is on, so a
    // crash or power cut never leaves ports open for long.
    private const int LeaseSeconds = 7200;
    private static readonly TimeSpan RenewEvery = TimeSpan.FromMinutes(45);
    private static UpnpClient? _upnp; private static IPAddress? _upnpLocal; private static System.Threading.Timer? _renew;
    private static void StartRenewing()
    {
        _renew?.Dispose();
        _renew = new System.Threading.Timer(_ => { var c = _upnp; var l = _upnpLocal; if (c == null || l == null) return; foreach (var (port, protocol) in Ports) c.Map(port, protocol, l, MappingName, LeaseSeconds); }, null, RenewEvery, RenewEvery);
    }

    // Shown when UPnP fails: what to check, in order.
    internal const string Guide = "To fix it: 1) In your router's page (often 192.168.0.1 or 192.168.1.1), turn on UPnP and save; on some routers also turn on \"Allow devices to change port forwarding\" or turn off \"UPnP secure mode\". 2) If you have two routers (ISP box + your own, or a mesh), UPnP must be on in the one connected to the internet, or put the ISP box in bridge mode. 3) Restart the router after changing it. 4) Still refused? Forward TCP 70, 3551, 3552, 6969 and UDP 7777 to this PC by hand, or use Radmin.";

    // Closes the router ports and switches the join key off (a new code is made next time).
    internal static void Close(LauncherPaths? paths)
    {
        try { if (paths != null) WriteSettings(paths, false, ""); } catch { }
        try { if (paths != null) { File.Delete(ApprovedFile(paths)); File.Delete(PendingFile(paths)); File.Delete(ActivityFile(paths)); } } catch { }
        _renew?.Dispose(); _renew = null;
        try { var c = _upnp; _upnp = null; if (c != null) foreach (var (port, protocol) in Ports) c.Unmap(port, protocol); } catch { }
        try
        {
            var type = Type.GetTypeFromProgID("HNetCfg.NATUPnP");
            if (type == null) return;
            dynamic nat = Activator.CreateInstance(type)!;
            dynamic? mappings = nat.StaticPortMappingCollection;
            if (mappings == null) return;
            foreach (var (port, protocol) in Ports) { try { mappings.Remove(port, protocol); } catch { } }
        }
        catch { }
    }

    // The address this PC uses to reach the internet (the router forwards to it). No packet is sent.
    private static IPAddress? LocalAddress()
    {
        try
        {
            using var socket = new Socket(AddressFamily.InterNetwork, SocketType.Dgram, ProtocolType.Udp);
            socket.Connect("8.8.8.8", 53);
            return (socket.LocalEndPoint as IPEndPoint)?.Address;
        }
        catch { return null; }
    }

    private static bool PublicAddress(IPAddress ip)
    {
        if (ip.AddressFamily != AddressFamily.InterNetwork) return false;
        var b = ip.GetAddressBytes();
        if (b[0] == 0 || b[0] == 10 || b[0] == 127 || b[0] >= 224) return false;
        if (b[0] == 192 && b[1] == 168) return false;
        if (b[0] == 172 && b[1] >= 16 && b[1] <= 31) return false;
        if (b[0] == 169 && b[1] == 254) return false;
        if (b[0] == 100 && b[1] >= 64 && b[1] <= 127) return false; // carrier-grade NAT
        return true;
    }
}