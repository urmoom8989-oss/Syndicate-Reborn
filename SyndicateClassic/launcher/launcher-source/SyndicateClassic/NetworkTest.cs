// Syndicate © 2026 Syndicate. All rights reserved. Proprietary — no copying, distribution, or derivative works. See LICENSE.
using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Net.NetworkInformation;
using System.Net.Sockets;
using System.Threading.Tasks;
using System.Windows;
using System.Windows.Controls;

namespace SyndicateClassic;

// Multiplayer > "Test connection to host": measures the path to the host independently of the game.
//   - 40 pings over the VPN: average, best, worst, jitter (average change between pings), loss
//   - time to open the host's backend port (TCP 3551) and whether the game port answers
//   - the host's own server numbers (tick time, raised bandwidth) are in the host's bot log
// Results go to the diagnostics log, so they can be copied with "Copy log" and compared before/after.
public partial class MainWindow
{
    internal sealed record PingResult(int Sent, int Received, double Average, double Best, double Worst, double Jitter)
    {
        public double LossPercent => Sent == 0 ? 0 : 100.0 * (Sent - Received) / Sent;
    }

    internal static PingResult Summarize(IReadOnlyList<long?> samples)
    {
        var ok = samples.Where(s => s.HasValue).Select(s => (double)s!.Value).ToList();
        double jitter = 0;
        for (int i = 1; i < ok.Count; i++) jitter += Math.Abs(ok[i] - ok[i - 1]);
        return new PingResult(samples.Count, ok.Count, ok.Count > 0 ? ok.Average() : 0, ok.Count > 0 ? ok.Min() : 0, ok.Count > 0 ? ok.Max() : 0, ok.Count > 1 ? jitter / (ok.Count - 1) : 0);
    }

    private async Task RunNetworkTest(string host, TextBlock output, Button button)
    {
        button.IsEnabled = false;
        output.Visibility = Visibility.Visible;
        output.Foreground = Hex(Night.Sub);
        try
        {
            output.Text = "Pinging " + host + " (40 pings, about 10 s)…";
            var samples = new List<long?>();
            using (var ping = new Ping())
                for (int i = 0; i < 40; i++)
                {
                    try { var reply = await ping.SendPingAsync(host, 1000); samples.Add(reply.Status == IPStatus.Success ? reply.RoundtripTime : null); }
                    catch (PingException) { samples.Add(null); }
                    await Task.Delay(200);
                }
            var r = Summarize(samples);
            double? tcp = null;
            try
            {
                var watch = System.Diagnostics.Stopwatch.StartNew();
                using var client = new TcpClient();
                var connect = client.ConnectAsync(host, 3551);
                if (await Task.WhenAny(connect, Task.Delay(3000)) == connect && client.Connected) tcp = watch.Elapsed.TotalMilliseconds;
            }
            catch { }
            string verdict =
                r.Received == 0 ? "The host does not answer pings. Check that both PCs are in the same Radmin network and that the host allowed friends through the firewall (Multiplayer > Allow friends to join)."
                : r.LossPercent >= 5 ? "Packet loss is high: expect rubber-banding. Use a cable instead of Wi-Fi on both PCs, close downloads/streams, and in Radmin check the connection is Direct, not Relay."
                : r.Average > 90 || r.Jitter > 15 ? "The VPN path itself is slow or unstable. In Radmin, right-click the host > Properties: if the tunnel says Relay, the traffic goes through a relay server (adds latency). Port forwarding or a UPnP router usually gives a Direct tunnel."
                : "The connection is fine. In-game ping can read about one server frame (17-33 ms) above this; anything much higher was bandwidth: this version raises it.";
            string line = $"Network test to {host}: ping avg {r.Average:0} ms (best {r.Best:0}, worst {r.Worst:0}), jitter {r.Jitter:0.0} ms, loss {r.LossPercent:0}% ({r.Received}/{r.Sent}); backend port {(tcp is double t ? t.ToString("0") + " ms" : "no answer")}.";
            Log(line);
            Log("Network test verdict: " + verdict);
            output.Foreground = Hex(r.Received == 0 || r.LossPercent >= 5 ? Night.Danger : r.Average > 90 || r.Jitter > 15 ? Night.Gold : Night.Good);
            output.Text = line + "\n" + verdict;
        }
        catch (Exception error) { output.Text = "Network test failed: " + error.Message; output.Foreground = Hex(Night.Danger); }
        finally { button.IsEnabled = true; }
    }

    // Button + result line under the host address box in the Multiplayer page.
    private UIElement BuildNetworkTestControls()
    {
        var panel = new StackPanel { Margin = new Thickness(0, 10, 0, 0) };
        var button = new Button { Content = "Test connection to host", Height = 34, Padding = new Thickness(14, 0, 14, 0), HorizontalAlignment = HorizontalAlignment.Left, ToolTip = "Measures ping, jitter and packet loss to the host over the VPN (independent of the game). Results also go to the log." };
        NightButton(button, "base");
        var output = NightText("", 12, Night.Sub).Also(t => { t.TextWrapping = TextWrapping.Wrap; t.Margin = new Thickness(0, 8, 0, 0); t.Visibility = Visibility.Collapsed; });
        button.Click += async (_, __) =>
        {
            var host = RadminConnection.Remote ? RadminConnection.Host : RadminHostBox.Text.Trim();
            if (string.IsNullOrWhiteSpace(host)) { output.Visibility = Visibility.Visible; output.Foreground = Hex(Night.Sub); output.Text = "Join a host first (or type the host's address above), then test."; return; }
            await RunNetworkTest(host, output, button);
        };
        panel.Children.Add(button); panel.Children.Add(output);
        return panel;
    }
}