using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Linq;
using System.Runtime.InteropServices;
using System.Threading;
using System.Threading.Tasks;
using HidSharp;
using Nefarius.ViGEm.Client;
using Nefarius.ViGEm.Client.Exceptions;
using Nefarius.ViGEm.Client.Targets;
using Nefarius.ViGEm.Client.Targets.Xbox360;

namespace SyndicateLauncher.Services.Controllers;

// Controllers (ported from Syndicate Classic, SyndicateClassic/launcher/launcher-source/SyndicateClassic/ControllerService.cs).
//
// Old Fortnite builds only understand XInput (Xbox) controllers. Xbox pads therefore work on their
// own (they are only counted here). PlayStation pads (DualShock 4 / DualSense / DualSense Edge, USB
// or Bluetooth) are read over HID and mirrored onto a virtual Xbox 360 controller through the free
// ViGEmBus driver while a game started from Syndicate is running.
//
// Reliability rules:
//  - One physical pad = one virtual pad. Windows often lists the same controller twice (a stale
//    Bluetooth entry next to USB, or two HID collections). Every candidate is opened, but a virtual
//    pad is only plugged in once a device actually sends reports, and duplicates are dropped
//    (USB wins over Bluetooth).
//  - A pad that stops sending reports is unplugged at once, so Fortnite never keeps a frozen
//    "ghost" controller in slot 1 that swallows input.
//  - Reports are validated before use; a malformed Bluetooth packet is skipped instead of becoming
//    random button presses.
//  - Plugging in / pairing is picked up instantly from the Windows device-change event.
public sealed class ControllerService : IDisposable
{
    private readonly Action<string> _log;
    public ControllerService(Action<string> log) { _log = log; }
    private const int SonyVendorId = 0x054C;
    private static readonly Dictionary<int, string> SonyModels = new()
    {
        [0x05C4] = "DualShock 4",
        [0x09CC] = "DualShock 4",
        [0x0BA0] = "DualShock 4 (wireless adapter)",
        [0x0CE6] = "DualSense",
        [0x0DF2] = "DualSense Edge",
    };

    private readonly object _lock = new();
    private readonly object _syncLock = new();
    private readonly Dictionary<string, SonyPad> _pads = new(StringComparer.OrdinalIgnoreCase);
    private readonly CancellationTokenSource _cts = new();
    private readonly SemaphoreSlim _wake = new(0, int.MaxValue);
    private ViGEmClient? _vigem;
    private bool _bridging;
    private bool _enabled = true;
    private string _lastSummary = "";
    private int _lastXinputMask = -1;
    private readonly Dictionary<string, long> _cooldown = new(StringComparer.OrdinalIgnoreCase);
    /// <summary>Which pad currently owns each physical controller (by Bluetooth MAC).</summary>
    private readonly Dictionary<string, SonyPad> _claims = new(StringComparer.OrdinalIgnoreCase);

    /// <summary>Raised from a background thread when the controller picture changes.</summary>
    public event Action? Changed;

    public bool DriverMissing { get; private set; }
    public int XboxControllers { get; private set; }
    public IReadOnlyList<string> PlayStationControllers { get; private set; } = Array.Empty<string>();
    public bool Mirroring { get { lock (_lock) return _bridging && _pads.Values.Any(p => p.IsLive); } }

    public const string DriverUrl = "https://github.com/nefarius/ViGEmBus/releases/latest";

    public void Start(bool enabled)
    {
        _enabled = enabled;
        PadLog = _log;
        try { DeviceList.Local.Changed += OnDeviceListChanged; } catch { }
        _ = Task.Run(() => WatchAsync(_cts.Token));
    }

    public void SetEnabled(bool enabled)
    {
        _enabled = enabled;
        Poke();
    }

    /// <summary>Mirror PlayStation pads only while Fortnite (launched by us) is running.</summary>
    public void SetGameRunning(bool running)
    {
        lock (_lock) _bridging = running;
        Poke();
    }

    private void OnDeviceListChanged(object? sender, DeviceListChangedEventArgs e) => Poke();

    /// <summary>Wakes the watcher now instead of at the next poll.</summary>
    private void Poke()
    {
        try { _wake.Release(); } catch (SemaphoreFullException) { }
    }

    private async Task WatchAsync(CancellationToken ct)
    {
        while (!ct.IsCancellationRequested)
        {
            try { Sync(); } catch (Exception ex) { _log($"Controller check failed: {ex.Message}"); }
            try
            {
                // Device events wake us instantly; the short poll also catches XInput pads
                // (which raise no HID event we can see) and dead Bluetooth links.
                if (await _wake.WaitAsync(750, ct).ConfigureAwait(false))
                {
                    // Windows fires several events per plug-in; let them settle, then sync once.
                    await Task.Delay(120, ct).ConfigureAwait(false);
                    while (_wake.CurrentCount > 0) _wake.Wait(0);
                }
            }
            catch (OperationCanceledException) { break; }
        }
    }

    private void Sync()
    {
        lock (_syncLock) SyncCore();
    }

    private void SyncCore()
    {
        var found = new Dictionary<string, (HidDevice Dev, string Model)>(StringComparer.OrdinalIgnoreCase);
        try
        {
            foreach (var dev in DeviceList.Local.GetHidDevices(SonyVendorId))
            {
                if (!SonyModels.TryGetValue(dev.ProductID, out string? model)) continue;
                int len;
                try { len = dev.GetMaxInputReportLength(); } catch { continue; }
                if (len < 10) continue; // not the gamepad collection
                found[dev.DevicePath] = (dev, model);
            }
        }
        catch { /* HID enumeration can fail briefly while devices change */ }

        bool mirror;
        lock (_lock) mirror = _bridging && _enabled;

        // Check for the driver as soon as a PlayStation pad shows up, so Settings can say what's missing before you play.
        if (_enabled && found.Count > 0 && _vigem == null && !DriverMissing) _vigem = TryCreateClient();

        lock (_lock)
        {
            // Drop pads that went away, went silent, or when mirroring stops.
            foreach (var key in _pads.Keys.ToList())
            {
                var pad = _pads[key];
                if (!mirror || !found.ContainsKey(key) || pad.IsDead)
                {
                    if (pad.WasLive && mirror) _log($"{pad.Model} disconnected.");
                    // Duplicates and silent (stale) entries: don't reopen them on every check.
                    if (!pad.WasLive && found.ContainsKey(key)) _cooldown[key] = Stopwatch.GetTimestamp() + Stopwatch.Frequency * 3;
                    pad.Dispose();
                    _pads.Remove(key);
                }
            }

            // Two device paths that are really the same controller (USB + stale Bluetooth entry):
            // keep the one that's sending data, retire the other.
            var live = _pads.Values.Where(p => p.IsLive).ToList();
            foreach (var dup in live.GroupBy(p => p.Identity).Where(g => g.Key.Length > 0 && g.Count() > 1))
                foreach (var extra in dup.OrderByDescending(p => p.LastReportTicks).Skip(1))
                    extra.Retire();

            // Open new ones. They only become a virtual Xbox pad after their first valid report.
            if (mirror && _vigem != null)
            {
                foreach (var (path, info) in found)
                {
                    if (_pads.ContainsKey(path)) continue;
                    if (_cooldown.TryGetValue(path, out long until) && Stopwatch.GetTimestamp() < until) continue;
                    _cooldown.Remove(path);
                    var pad = SonyPad.TryOpen(info.Dev, info.Model, _vigem, TryClaim, OnPadWentLive, ReleaseClaim);
                    if (pad != null) _pads[path] = pad;
                }
            }
        }

        PlayStationControllers = DistinctControllers(found);
        int virtualPads;
        lock (_lock) virtualPads = _pads.Values.Count(p => p.IsLive);
        int previousXbox = XboxControllers;
        XboxControllers = Math.Max(0, BitCount(XInput.ConnectedMask()) - virtualPads);
        if (_lastXinputMask >= 0 && XboxControllers > previousXbox && found.Count == 0) _log("Xbox controller connected.");
        _lastXinputMask = XboxControllers;

        string summary = Summary() + "|" + DriverMissing + "|" + virtualPads;
        if (summary != _lastSummary)
        {
            _lastSummary = summary;
            Changed?.Invoke();
        }
    }

    /// <summary>
    /// Called by a pad when its first report arrives. Only one pad may own a physical controller:
    /// a DualSense plugged in by cable while still paired over Bluetooth shows up twice, and two
    /// virtual pads fed by one controller made inputs double up or land on the wrong slot.
    /// USB wins over Bluetooth (lower latency).
    /// </summary>
    private bool TryClaim(SonyPad candidate)
    {
        lock (_lock)
        {
            string key = candidate.Identity;
            if (key.Length == 0)
            {
                // MAC unreadable: the same model live on the other connection type is the same controller.
                var twin = _pads.Values.FirstOrDefault(p => !ReferenceEquals(p, candidate) && !p.IsDead && p.LastReportTicks > 0 && p.Model == candidate.Model && p.Connection != candidate.Connection);
                if (twin == null) return true;
                if (candidate.Connection == "USB") { twin.Retire(); _log($"{candidate.Model}: switched to the USB cable."); return true; }
                return false;
            }
            if (_claims.TryGetValue(key, out var owner) && !ReferenceEquals(owner, candidate) && !owner.IsDead)
            {
                if (candidate.Connection == "USB" && owner.Connection != "USB")
                {
                    owner.Retire();
                    _log($"{candidate.Model}: switched to the USB cable.");
                }
                else return false;
            }
            _claims[key] = candidate;
            return true;
        }
    }

    private void ReleaseClaim(SonyPad pad)
    {
        lock (_lock)
        {
            if (pad.Identity.Length > 0 && _claims.TryGetValue(pad.Identity, out var owner) && ReferenceEquals(owner, pad))
                _claims.Remove(pad.Identity);
        }
    }
    private void OnPadWentLive(SonyPad pad)
    {
        _log($"{pad.Model} connected ({pad.Connection}) - Fortnite sees it as an Xbox controller.");
        Poke();
    }

    private static List<string> DistinctControllers(Dictionary<string, (HidDevice Dev, string Model)> found)
    {
        var seen = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
        var list = new List<string>();
        foreach (var (path, info) in found)
        {
            string id = SafeSerial(info.Dev);
            if (id.Length == 0) id = path;
            if (seen.Add(id)) list.Add(info.Model);
        }
        return list;
    }

    internal static string SafeSerial(HidDevice dev)
    {
        try { return CanonicalMac((dev.GetSerialNumber() ?? "").Trim()); }
        catch { return ""; }
    }

    /// <summary>A MAC as 12 hex digits, byte order normalised (Windows and the pad report it in opposite orders).</summary>
    internal static string CanonicalMac(string raw)
    {
        string hex = new string(raw.Where(Uri.IsHexDigit).ToArray()).ToUpperInvariant();
        if (hex.Length != 12 || hex.Trim('0').Length == 0) return "";
        string reversed = string.Concat(Enumerable.Range(0, 6).Reverse().Select(i => hex.Substring(i * 2, 2)));
        return string.CompareOrdinal(hex, reversed) <= 0 ? hex : reversed;
    }

    private static int BitCount(int v) { int c = 0; for (; v != 0; v &= v - 1) c++; return c; }

    private ViGEmClient? TryCreateClient()
    {
        try
        {
            var client = new ViGEmClient();
            DriverMissing = false;
            return client;
        }
        catch (VigemBusNotFoundException)
        {
            DriverMissing = true;
            _log("PlayStation controller found, but the ViGEmBus driver isn't installed. Settings > Mods > Controller has the download link.");
        }
        catch (Exception ex)
        {
            DriverMissing = true;
            _log($"Couldn't start PlayStation controller support: {ex.Message}");
        }
        return null;
    }

    /// <summary>Re-check for ViGEmBus (after the user installs it).</summary>
    public void RetryDriver()
    {
        lock (_syncLock)
        {
            DriverMissing = false;
            if (_pads.Count == 0) { _vigem?.Dispose(); _vigem = null; }
        }
        Sync();
    }

    public string Summary()
    {
        var parts = new List<string>();
        if (XboxControllers > 0) parts.Add(XboxControllers == 1 ? "Xbox controller connected" : $"{XboxControllers} Xbox controllers connected");
        foreach (string ps in PlayStationControllers) parts.Add(ps + " connected");
        if (parts.Count == 0) return "No controller connected";
        return string.Join(" - ", parts);
    }

    public void Dispose()
    {
        _cts.Cancel();
        try { DeviceList.Local.Changed -= OnDeviceListChanged; } catch { }
        lock (_lock)
        {
            foreach (var p in _pads.Values) p.Dispose();
            _pads.Clear();
        }
        _vigem?.Dispose();
        _vigem = null;
    }

    // =====================================================================
    // One PlayStation controller → one virtual Xbox 360 controller
    // =====================================================================

    private static Action<string> PadLog = _ => { };

    private sealed class SonyPad : IDisposable
    {
        // A DualShock 4 / DualSense streams ~250 reports per second even when idle.
        private static readonly long SilentTicks = Stopwatch.Frequency * 2;

        private readonly HidStream _stream;
        private readonly ViGEmClient _client;
        private readonly bool _dualSense;
        private readonly Thread _thread;
        private readonly Func<SonyPad, bool> _tryClaim;
        private readonly Action<SonyPad> _wentLive;
        private readonly Action<SonyPad> _release;
        private IXbox360Controller? _xbox;
        private volatile bool _stop;
        private volatile bool _retired;
        private long _lastReport;
        private readonly long _opened = Stopwatch.GetTimestamp();
        private PadState _last;
        private bool _hasLast;

        public string Model { get; }
        public string Identity { get; }
        public string Connection { get; }
        public bool IsLive => _xbox != null && !_stop && !_retired;
        public bool WasLive { get; private set; }
        public long LastReportTicks => Interlocked.Read(ref _lastReport);

        /// <summary>
        /// Thread ended, retired as a duplicate, never sent a report (stale entry), or a USB pad that has been silent
        /// for 2 s. A Bluetooth pad that has worked is not dropped for silence: some pads only send a report when
        /// something changes over Bluetooth, and unplugging the virtual pad every time the controller sat still made
        /// Fortnite lose it (and pick it up again in another slot). A Bluetooth pad that really drops goes away from
        /// the device list or fails its next read; until then a silent one is held at rest (sticks centred,
        /// nothing pressed) so it never keeps a stick or button held.
        /// </summary>
        public bool IsDead
        {
            get
            {
                if (_stop || _retired) return true;
                long last = LastReportTicks;
                long now = Stopwatch.GetTimestamp();
                if (last == 0) return now - _opened > SilentTicks * 2;
                return Connection != "Bluetooth" && now - last > SilentTicks;
            }
        }

        private SonyPad(HidDevice dev, HidStream stream, string model, ViGEmClient client, Func<SonyPad, bool> tryClaim, Action<SonyPad> wentLive, Action<SonyPad> release)
        {
            _stream = stream;
            _client = client;
            Model = model;
            _dualSense = model.StartsWith("DualSense", StringComparison.Ordinal);
            _tryClaim = tryClaim;
            _wentLive = wentLive;
            _release = release;
            int len = 0;
            try { len = dev.GetMaxInputReportLength(); } catch { }
            Connection = IsBluetooth(dev) ? "Bluetooth" : "USB";
            // Bluetooth entries carry the MAC as their serial; over USB the pad reports it in a feature report.
            Identity = SafeSerial(dev);
            if (Identity.Length == 0 && Connection == "USB") Identity = ReadUsbMac(stream, _dualSense);
            if (Connection == "Bluetooth") RequestFullReports(dev, stream, _dualSense);
            _thread = new Thread(ReadLoop) { IsBackground = true, Name = "Syndicate controller", Priority = ThreadPriority.Highest };
        }

        public static SonyPad? TryOpen(HidDevice dev, string model, ViGEmClient client, Func<SonyPad, bool> tryClaim, Action<SonyPad> wentLive, Action<SonyPad> release)
        {
            HidStream? stream = null;
            try
            {
                if (!dev.TryOpen(out stream)) return null;
                stream.ReadTimeout = 500;
                var pad = new SonyPad(dev, stream, model, client, tryClaim, wentLive, release);
                pad._thread.Start();
                return pad;
            }
            catch (Exception ex)
            {
                PadLog($"Couldn't use the {model}: {ex.Message}");
                stream?.Dispose();
                return null;
            }
        }

        public void Retire()
        {
            if (_retired) return;
            _retired = true;
            try { _xbox?.Disconnect(); } catch { }
            _release(this);
        }

        /// <summary>DualSense: feature report 0x09, DualShock 4: 0x12 — both start with the pad's MAC.</summary>
        private static string ReadUsbMac(HidStream stream, bool dualSense)
        {
            try
            {
                byte[] buf = new byte[dualSense ? 20 : 16];
                buf[0] = dualSense ? (byte)0x09 : (byte)0x12;
                stream.GetFeature(buf);
                return CanonicalMac(Convert.ToHexString(buf, 1, 6));
            }
            catch { return ""; }
        }

        private void ReadLoop()
        {
            byte[] buf = new byte[Math.Max(128, SafeLen())];
            bool bluetooth = Connection == "Bluetooth";
            while (!_stop && !_retired)
            {
                int n;
                try { n = _stream.Read(buf, 0, buf.Length); }
                catch (TimeoutException) { HoldAtRestIfSilent(); continue; }
                catch (Exception) { break; } // unplugged / Bluetooth dropped
                if (n <= 0) continue;
                if (!PadState.TryParse(buf, n, _dualSense, bluetooth, out var s)) continue;
                Interlocked.Exchange(ref _lastReport, Stopwatch.GetTimestamp());

                if (_xbox == null)
                {
                    // First real data: plug in the virtual pad now (never for a duplicate of a live one).
                    if (!_tryClaim(this)) { _retired = true; break; }
                    try
                    {
                        var x = _client.CreateXbox360Controller();
                        x.AutoSubmitReport = false;
                        x.Connect();
                        _xbox = x;
                        WasLive = true;
                        _wentLive(this);
                    }
                    catch (Exception ex)
                    {
                        PadLog($"Couldn't create a virtual controller for the {Model}: {ex.Message}");
                        break;
                    }
                }

                var output = s;
                // Unchanged output: nothing to send (keeps ViGEm and the game from being flooded).
                if (_hasLast && output == _last) continue;
                _last = output;
                _hasLast = true;
                try { Apply(output); } catch { break; }
            }
            _stop = true;
        }

        /// <summary>A live pad with no report for 2 s: send "nothing held" once, so a lost link never leaves a stick pushed.</summary>
        private void HoldAtRestIfSilent()
        {
            if (_xbox == null || _retired) return;
            long last = LastReportTicks;
            if (last == 0 || Stopwatch.GetTimestamp() - last < SilentTicks) return;
            if (_hasLast && _last == PadState.AtRest) return;
            _last = PadState.AtRest;
            _hasLast = true;
            try { Apply(PadState.AtRest); } catch { }
        }

        private int SafeLen()
        {
            try { return _stream.Device.GetMaxInputReportLength(); } catch { return 128; }
        }

        private void Apply(in PadState s)
        {
            var x = _xbox!;
            x.SetAxisValue(Xbox360Axis.LeftThumbX, Axis(s.LX, false));
            x.SetAxisValue(Xbox360Axis.LeftThumbY, Axis(s.LY, true));
            x.SetAxisValue(Xbox360Axis.RightThumbX, Axis(s.RX, false));
            x.SetAxisValue(Xbox360Axis.RightThumbY, Axis(s.RY, true));
            x.SetSliderValue(Xbox360Slider.LeftTrigger, s.L2);
            x.SetSliderValue(Xbox360Slider.RightTrigger, s.R2);

            // Face buttons: Cross→A, Circle→B, Square→X, Triangle→Y (same positions as Xbox).
            x.SetButtonState(Xbox360Button.A, (s.Face & 0x20) != 0);
            x.SetButtonState(Xbox360Button.B, (s.Face & 0x40) != 0);
            x.SetButtonState(Xbox360Button.X, (s.Face & 0x10) != 0);
            x.SetButtonState(Xbox360Button.Y, (s.Face & 0x80) != 0);

            int hat = s.Face & 0x0F; // 0 = up, clockwise, 8 = released
            x.SetButtonState(Xbox360Button.Up, hat is 0 or 1 or 7);
            x.SetButtonState(Xbox360Button.Right, hat is 1 or 2 or 3);
            x.SetButtonState(Xbox360Button.Down, hat is 3 or 4 or 5);
            x.SetButtonState(Xbox360Button.Left, hat is 5 or 6 or 7);

            x.SetButtonState(Xbox360Button.LeftShoulder, (s.Shoulders & 0x01) != 0);
            x.SetButtonState(Xbox360Button.RightShoulder, (s.Shoulders & 0x02) != 0);
            // Share/Create and a touchpad click both open the map (like the console versions).
            x.SetButtonState(Xbox360Button.Back, (s.Shoulders & 0x10) != 0 || (s.System & 0x02) != 0);
            x.SetButtonState(Xbox360Button.Start, (s.Shoulders & 0x20) != 0);  // Options
            // Sprint by default, edit on release and custom binds are added in game by SyndicatePadAssist.dll,
            // which reads this virtual pad like any Xbox controller.
            x.SetButtonState(Xbox360Button.LeftThumb, (s.Shoulders & 0x40) != 0);
            x.SetButtonState(Xbox360Button.RightThumb, (s.Shoulders & 0x80) != 0);
            x.SetButtonState(Xbox360Button.Guide, (s.System & 0x01) != 0);     // PS button

            x.SubmitReport();
        }

        /// <summary>0..255 (128 = centre) → −32768..32767; Y is flipped (HID down = Xbox up).</summary>
        private static short Axis(byte v, bool invert)
        {
            int c = v >= 128 ? (v - 128) * 32767 / 127 : (v - 128) * 32768 / 128;
            if (invert) c = -c;
            return (short)Math.Clamp(c, short.MinValue, short.MaxValue);
        }

        public void Dispose()
        {
            _stop = true;
            try { _stream.Dispose(); } catch { }
            try { _thread.Join(1000); } catch { }
            try { _xbox?.Disconnect(); } catch { }
            _xbox = null;
            _release(this);
        }
    }

    /// <summary>
    /// Over Bluetooth, Windows lists PS pads with input reports longer than 64 bytes (DualSense 78, DualShock 4 547);
    /// over USB (and the DualShock 4 wireless adapter) they are 64.
    /// </summary>
    internal static bool IsBluetooth(HidDevice dev)
    {
        try { return dev.GetMaxInputReportLength() > 64; } catch { return false; }
    }

    /// <summary>
    /// Over Bluetooth a PS pad starts in a basic mode (report 0x01: fewer fields, and on some pads only sent when
    /// something changes). Reading its calibration feature report switches it to full reports that stream all the
    /// time (DualSense 0x31, DualShock 4 0x11), the same thing the PlayStation, Steam and DS4Windows do.
    /// Both layouts are understood, so this failing is harmless.
    /// </summary>
    internal static void RequestFullReports(HidDevice dev, HidStream stream, bool dualSense)
    {
        int len = 0;
        try { len = dev.GetMaxFeatureReportLength(); } catch { }
        foreach (byte id in dualSense ? new byte[] { 0x05 } : new byte[] { 0x05, 0x02 })
        {
            try
            {
                var buf = new byte[len > 0 ? len : 64];
                buf[0] = id;
                stream.GetFeature(buf);
                PadLog($"{(dualSense ? "DualSense" : "DualShock 4")} (Bluetooth): full reports on.");
                return;
            }
            catch { }
        }
    }

    /// <summary>The parts of a DualShock 4 / DualSense input report the bridge needs.</summary>
    public readonly record struct PadState(byte LX, byte LY, byte RX, byte RY, byte L2, byte R2, byte Face, byte Shoulders, byte System)
    {
        /// <summary>Sticks centred, d-pad released (8), nothing pressed.</summary>
        public static readonly PadState AtRest = new(128, 128, 128, 128, 0, 0, 0x08, 0, 0);

        /// <summary>
        /// Windows pads every read to the longest input report the pad has, so the length can't tell USB from
        /// Bluetooth (a Bluetooth DualSense's basic 0x01 report arrives 78 bytes long). The caller says which it is.
        /// Before this, that basic report was read with the USB layout: the trigger bytes became face and shoulder
        /// buttons and the d-pad read as held Up, so the pad "went ballistic" over Bluetooth only.
        /// </summary>
        public static bool TryParse(byte[] r, int n, bool dualSense, bool bluetooth, out PadState s)
        {
            s = default;
            if (n < 10) return false;
            if (dualSense)
            {
                if (r[0] == 0x31 && n >= 12)   // Bluetooth, full report
                {
                    s = new(r[2], r[3], r[4], r[5], r[6], r[7], r[9], r[10], (byte)(r[11] & 0x07));
                    return Valid(s);
                }
                if (r[0] == 0x01 && bluetooth) // Bluetooth, basic report (DualShock 4 layout)
                {
                    s = new(r[1], r[2], r[3], r[4], r[8], r[9], r[5], r[6], (byte)(r[7] & 0x03));
                    return Valid(s);
                }
                if (r[0] == 0x01 && n >= 64)   // USB
                {
                    s = new(r[1], r[2], r[3], r[4], r[5], r[6], r[8], r[9], (byte)(r[10] & 0x07));
                    return Valid(s);
                }
                return false;
            }

            int o = r[0] switch { 0x01 => 1, 0x11 when n >= 12 => 3, _ => -1 }; // USB/BT basic, BT full
            if (o < 0 || n < o + 9) return false;
            // DS4 byte 7 carries a frame counter in its top 6 bits; only PS (bit 0) and touchpad (bit 1) matter.
            s = new(r[o], r[o + 1], r[o + 2], r[o + 3], r[o + 7], r[o + 8], r[o + 4], r[o + 5], (byte)(r[o + 6] & 0x03));
            return Valid(s);
        }

        /// <summary>The d-pad nibble is 0–8; anything else means a corrupt/partial packet.</summary>
        private static bool Valid(in PadState s) => (s.Face & 0x0F) <= 8;
    }

    // =====================================================================
    // Xbox controllers (counted only, they need no help)
    // =====================================================================

    private static class XInput
    {
        [StructLayout(LayoutKind.Sequential)]
        private struct Gamepad { public ushort Buttons; public byte LeftTrigger, RightTrigger; public short LX, LY, RX, RY; }

        [StructLayout(LayoutKind.Sequential)]
        private struct State { public uint PacketNumber; public Gamepad Pad; }

        [DllImport("xinput1_4.dll", EntryPoint = "XInputGetState")]
        private static extern uint GetState(uint index, out State state);

        public static int ConnectedMask()
        {
            int mask = 0;
            try
            {
                for (uint i = 0; i < 4; i++)
                    if (GetState(i, out _) == 0) mask |= 1 << (int)i;
            }
            catch (DllNotFoundException) { }
            catch (EntryPointNotFoundException) { }
            return mask;
        }
    }
}