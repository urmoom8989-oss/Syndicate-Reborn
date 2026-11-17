// Syndicate © 2026 Syndicate. All rights reserved. Proprietary — no copying, distribution, or derivative works. See LICENSE.
using System;
using System.Collections.Generic;
using System.Linq;
using System.Net;
using System.Net.Http;
using System.Net.Sockets;
using System.Text;
using System.Text.RegularExpressions;
using System.Threading;

namespace SyndicateClassic;

// Our own UPnP port mapping (Direct play). Windows' built-in helper (HNetCfg.NATUPnP) only works when Windows'
// network discovery is on, speaks only the oldest router protocol and always asks for permanent mappings, which
// many newer routers refuse (error 725 / 718 / 402) even with UPnP on: that is why "the router refused" while games
// such as Hytale, which talk to the router themselves, could open ports. This does the same as those games:
//   1. SSDP: ask the local network (UDP 239.255.255.250:1900) for an Internet Gateway Device;
//   2. read the router's description and find its WANIPConnection (v1 or v2) or WANPPPConnection service;
//   3. SOAP AddPortMapping with a 1-hour lease (renewed while Direct play is on; a permanent lease is tried only if
//      the router asks for one), GetExternalIPAddress, DeletePortMapping when closing.
internal sealed class UpnpClient
{
    internal string ControlUrl { get; }
    internal string ServiceType { get; }
    internal string LastError { get; private set; } = "";
    private static readonly HttpClient Http = new() { Timeout = TimeSpan.FromSeconds(4) };

    private UpnpClient(string controlUrl, string serviceType) { ControlUrl = controlUrl; ServiceType = serviceType; }

    // Finds the router (about 3 seconds at most). null: nothing answered (UPnP off in the router, or blocked).
    internal static UpnpClient? Discover(IPAddress local, out string why)
    {
        why = "";
        var locations = new List<string>();
        try
        {
            using var udp = new UdpClient(new IPEndPoint(local, 0));
            udp.Client.ReceiveTimeout = 700;
            foreach (var target in new[] { "urn:schemas-upnp-org:device:InternetGatewayDevice:1", "urn:schemas-upnp-org:device:InternetGatewayDevice:2", "urn:schemas-upnp-org:service:WANIPConnection:1", "urn:schemas-upnp-org:service:WANPPPConnection:1" })
            {
                var msg = Encoding.ASCII.GetBytes($"M-SEARCH * HTTP/1.1\r\nHOST: 239.255.255.250:1900\r\nMAN: \"ssdp:discover\"\r\nMX: 2\r\nST: {target}\r\n\r\n");
                udp.Send(msg, msg.Length, new IPEndPoint(IPAddress.Parse("239.255.255.250"), 1900));
            }
            var until = DateTime.UtcNow.AddSeconds(3);
            while (DateTime.UtcNow < until)
            {
                try
                {
                    var from = new IPEndPoint(IPAddress.Any, 0);
                    var text = Encoding.ASCII.GetString(udp.Receive(ref from));
                    var m = Regex.Match(text, @"(?im)^location:\s*(\S+)");
                    if (m.Success && !locations.Contains(m.Groups[1].Value)) locations.Add(m.Groups[1].Value);
                }
                catch (SocketException) { if (locations.Count > 0) break; }
            }
        }
        catch (Exception error) { why = "UPnP search failed (" + error.Message + ")."; return null; }
        if (locations.Count == 0) { why = "No router answered the UPnP search. UPnP is off in the router, or a firewall blocks it."; return null; }
        foreach (var location in locations)
        {
            try
            {
                var xml = Http.GetStringAsync(location).GetAwaiter().GetResult();
                foreach (var service in new[] { "urn:schemas-upnp-org:service:WANIPConnection:2", "urn:schemas-upnp-org:service:WANIPConnection:1", "urn:schemas-upnp-org:service:WANPPPConnection:1" })
                {
                    var block = Regex.Match(xml, @"<service>(?:(?!</service>).)*?<serviceType>\s*" + Regex.Escape(service) + @"\s*</serviceType>(?:(?!</service>).)*?</service>", RegexOptions.Singleline);
                    if (!block.Success) continue;
                    var control = Regex.Match(block.Value, @"<controlURL>\s*([^<]+?)\s*</controlURL>");
                    if (!control.Success) continue;
                    var url = new Uri(new Uri(location), control.Groups[1].Value).ToString();
                    return new UpnpClient(url, service);
                }
            }
            catch { }
        }
        why = "A router answered but offers no port-mapping service (it may be a mesh point or a second router; UPnP must be on in the main router).";
        return null;
    }

    private string? Soap(string action, string args)
    {
        var body = $"<?xml version=\"1.0\"?><s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\" s:encodingStyle=\"http://schemas.xmlsoap.org/soap/encoding/\"><s:Body><u:{action} xmlns:u=\"{ServiceType}\">{args}</u:{action}></s:Body></s:Envelope>";
        using var request = new HttpRequestMessage(HttpMethod.Post, ControlUrl) { Content = new StringContent(body, Encoding.UTF8, "text/xml") };
        request.Headers.TryAddWithoutValidation("SOAPAction", $"\"{ServiceType}#{action}\"");
        try
        {
            using var response = Http.Send(request);
            var text = response.Content.ReadAsStringAsync().GetAwaiter().GetResult();
            if (response.IsSuccessStatusCode) { LastError = ""; return text; }
            var code = Regex.Match(text, @"<errorCode>\s*(\d+)\s*</errorCode>").Groups[1].Value;
            var desc = Regex.Match(text, @"<errorDescription>\s*([^<]*)</errorDescription>").Groups[1].Value;
            LastError = code.Length > 0 ? code + (desc.Length > 0 ? " " + desc : "") : ((int)response.StatusCode).ToString();
            return null;
        }
        catch (Exception error) { LastError = error.Message; return null; }
    }

    // Opens one port to this PC. lease: seconds (0 = permanent, only when the router insists).
    internal bool Map(int port, string protocol, IPAddress local, string description, int lease = 3600)
    {
        string Args(int l) => $"<NewRemoteHost></NewRemoteHost><NewExternalPort>{port}</NewExternalPort><NewProtocol>{protocol}</NewProtocol><NewInternalPort>{port}</NewInternalPort><NewInternalClient>{local}</NewInternalClient><NewEnabled>1</NewEnabled><NewPortMappingDescription>{description}</NewPortMappingDescription><NewLeaseDuration>{l}</NewLeaseDuration>";
        if (Soap("AddPortMapping", Args(lease)) != null) return true;
        if (LastError.StartsWith("725")) return Soap("AddPortMapping", Args(0)) != null; // OnlyPermanentLeasesSupported
        if (LastError.StartsWith("718")) { Unmap(port, protocol); return Soap("AddPortMapping", Args(lease)) != null; } // already mapped (old run)
        return false;
    }

    internal void Unmap(int port, string protocol) =>
        Soap("DeletePortMapping", $"<NewRemoteHost></NewRemoteHost><NewExternalPort>{port}</NewExternalPort><NewProtocol>{protocol}</NewProtocol>");

    internal string ExternalAddress()
    {
        var text = Soap("GetExternalIPAddress", "");
        return text == null ? "" : Regex.Match(text, @"<NewExternalIPAddress>\s*([^<]*)</NewExternalIPAddress>").Groups[1].Value.Trim();
    }

    // What a router error code means, in plain words (shown to the player).
    internal static string Explain(string error)
    {
        var code = Regex.Match(error, @"^\d+").Value;
        return code switch
        {
            "606" or "401" => "the router only lets some devices use UPnP (look for \"UPnP access control\" or \"allow devices to change port forwarding\" and allow this PC)",
            "718" => "another device already uses this port on the router",
            "725" => "the router only accepts permanent openings",
            "724" or "716" => "the router wants the same port number outside and inside",
            "727" or "729" => "the router does not allow this port number (many routers block ports below 1024)",
            "501" or "402" => "the router rejected the request (UPnP may be in a read-only or \"secure\" mode)",
            _ => error.Length > 0 ? "router error " + error : "no answer"
        };
    }
}
