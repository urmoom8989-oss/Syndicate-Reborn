using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using System.Threading;
using HidSharp;

namespace SyndicateLauncher.Services.Controllers;

/// <summary>Buttons the launcher's controller navigation understands (Xbox names; PlayStation pads map onto them).</summary>
[Flags]
public enum PadButtons : uint
{
    None = 0, Up = 1, Down = 2, Left = 4, Right = 8, A = 16, B = 32, X = 64, Y = 128,
    LB = 256, RB = 512, Start = 1024, Back = 2048
}

/// <summary>One reading of every controller, merged: any pad can drive the launcher.</summary>
public readonly record struct PadSnapshot(PadButtons Buttons, bool Connected, bool PlayStation, double LX, double LY, double RY);

/// <summary>
/// Reads controllers for the launcher's own menus: Xbox pads through XInput, and the first PS4 / PS5 pad straight
/// from HID while the game is closed (while it runs, ControllerService turns PlayStation pads into a virtual Xbox
/// pad, which XInput sees).
/// </summary>
public sealed class PadReader : IDisposable
{
    private SonyNavReader? _sony;

    public void Dispose() { _sony?.Dispose(); _sony = null; }

    public PadSnapshot Read(bool readPlayStation)
    {
        if (readPlayStation && _sony == null) _sony = new SonyNavReader();
        else if (!readPlayStation && _sony != null) { _sony.Dispose(); _sony = null; }

        PadButtons state = PadButtons.None; bool connected = false, playStation = false; double lx = 0, ly = 0, ry = 0;
        for (uint i = 0; i < 4; i++)
        {
            if (XInput.Get(i, out var s) != 0) continue;
            connected = true;
            var g = s.Pad;
            if ((g.Buttons & 0x0001) != 0) state |= PadButtons.Up;
            if ((g.Buttons & 0x0002) != 0) state |= PadButtons.Down;
            if ((g.Buttons & 0x0004) != 0) state |= PadButtons.Left;
            if ((g.Buttons & 0x0008) != 0) state |= PadButtons.Right;
            if ((g.Buttons & 0x0010) != 0) state |= PadButtons.Start;
            if ((g.Buttons & 0x0020) != 0) state |= PadButtons.Back;
            if ((g.Buttons & 0x0100) != 0) state |= PadButtons.LB;
            if ((g.Buttons & 0x0200) != 0) state |= PadButtons.RB;
            if ((g.Buttons & 0x1000) != 0) state |= PadButtons.A;
            if ((g.Buttons & 0x2000) != 0) state |= PadButtons.B;
            if ((g.Buttons & 0x4000) != 0) state |= PadButtons.X;
            if ((g.Buttons & 0x8000) != 0) state |= PadButtons.Y;
            Bigger(ref lx, g.LX / 32768.0); Bigger(ref ly, g.LY / 32768.0); Bigger(ref ry, g.RY / 32768.0);
        }
        if (_sony?.Latest is ControllerService.PadState p)
        {
            connected = true; playStation = true;
            int hat = p.Face & 0x0F;
            if (hat is 0 or 1 or 7) state |= PadButtons.Up;
            if (hat is 1 or 2 or 3) state |= PadButtons.Right;
            if (hat is 3 or 4 or 5) state |= PadButtons.Down;
            if (hat is 5 or 6 or 7) state |= PadButtons.Left;
            if ((p.Face & 0x20) != 0) state |= PadButtons.A;   // Cross
            if ((p.Face & 0x40) != 0) state |= PadButtons.B;   // Circle
            if ((p.Face & 0x10) != 0) state |= PadButtons.X;   // Square
            if ((p.Face & 0x80) != 0) state |= PadButtons.Y;   // Triangle
            if ((p.Shoulders & 0x01) != 0) state |= PadButtons.LB;
            if ((p.Shoulders & 0x02) != 0) state |= PadButtons.RB;
            if ((p.Shoulders & 0x10) != 0) state |= PadButtons.Back;
            if ((p.Shoulders & 0x20) != 0) state |= PadButtons.Start;
            // HID sticks: 0..255, 128 = centre, Y down is positive.
            Bigger(ref lx, (p.LX - 128) / 128.0); Bigger(ref ly, (128 - p.LY) / 128.0); Bigger(ref ry, (128 - p.RY) / 128.0);
        }
        return new PadSnapshot(state, connected, playStation, lx, ly, ry);

        static void Bigger(ref double v, double n) { if (Math.Abs(n) > Math.Abs(v)) v = n; }
    }

    private static class XInput
    {
        [StructLayout(LayoutKind.Sequential)] public struct Gamepad { public ushort Buttons; public byte LeftTrigger, RightTrigger; public short LX, LY, RX, RY; }
        [StructLayout(LayoutKind.Sequential)] public struct State { public uint PacketNumber; public Gamepad Pad; }
        [DllImport("xinput1_4.dll", EntryPoint = "XInputGetState")] private static extern uint Get14(uint user, out State state);
        [DllImport("xinput9_1_0.dll", EntryPoint = "XInputGetState")] private static extern uint Get910(uint user, out State state);
        private static int _which; // 0 = xinput1_4, 1 = xinput9_1_0, 2 = none
        private static long _retryAt;
        private static readonly long[] _absentUntil = new long[4];

        public static uint Get(uint user, out State state)
        {
            state = default;
            // Asking an empty slot is slow on some PCs: look again only once a second.
            long now = Environment.TickCount64;
            if (now < _absentUntil[user]) return 1167; // ERROR_DEVICE_NOT_CONNECTED
            uint result = 1167;
            try
            {
                if (_which == 0) result = Get14(user, out state);
                else if (_which == 1) result = Get910(user, out state);
                else if (now >= _retryAt) { _which = 0; _retryAt = now + 30000; }
            }
            catch (DllNotFoundException) { _which++; }
            catch (EntryPointNotFoundException) { _which++; }
            if (result != 0) _absentUntil[user] = now + 1000;
            return result;
        }
    }

    /// <summary>Reads the first PS4 / PS5 pad straight from HID (USB or Bluetooth).</summary>
    private sealed class SonyNavReader : IDisposable
    {
        private static readonly Dictionary<int, bool> Models = new() { [0x05C4] = false, [0x09CC] = false, [0x0BA0] = false, [0x0CE6] = true, [0x0DF2] = true };
        private volatile bool _stop;
        private ControllerService.PadState _latest;
        private long _latestAt;

        public ControllerService.PadState? Latest
        {
            get
            {
                long at = Interlocked.Read(ref _latestAt);
                return at != 0 && Environment.TickCount64 - at < 1500 ? _latest : null;
            }
        }

        public SonyNavReader()
        {
            new Thread(Run) { IsBackground = true, Name = "Syndicate launcher PS pad" }.Start();
        }

        private void Run()
        {
            while (!_stop)
            {
                HidDevice? device = null; bool dualSense = false;
                try
                {
                    foreach (var d in DeviceList.Local.GetHidDevices(0x054C))
                    {
                        if (!Models.TryGetValue(d.ProductID, out dualSense)) continue;
                        int len; try { len = d.GetMaxInputReportLength(); } catch { continue; }
                        if (len < 10) continue; // not the gamepad collection
                        device = d; break;
                    }
                }
                catch { device = null; }
                if (device == null) { Sleep(2000); continue; }
                try
                {
                    if (!device.TryOpen(out HidStream stream)) { Sleep(2000); continue; }
                    using (stream)
                    {
                        stream.ReadTimeout = 500;
                        bool bluetooth = ControllerService.IsBluetooth(device);
                        if (bluetooth) ControllerService.RequestFullReports(device, stream, dualSense);
                        var buf = new byte[Math.Max(128, SafeLength(device))];
                        while (!_stop)
                        {
                            int n;
                            try { n = stream.Read(buf, 0, buf.Length); }
                            catch (TimeoutException) { continue; }
                            if (n > 0 && ControllerService.PadState.TryParse(buf, n, dualSense, bluetooth, out var s))
                            {
                                _latest = s;
                                Interlocked.Exchange(ref _latestAt, Environment.TickCount64);
                            }
                        }
                    }
                }
                catch { Sleep(1000); } // unplugged or Bluetooth dropped: look again
            }
        }

        private static int SafeLength(HidDevice d) { try { return d.GetMaxInputReportLength(); } catch { return 128; } }
        private void Sleep(int ms) { for (int t = 0; t < ms && !_stop; t += 100) Thread.Sleep(100); }
        public void Dispose() => _stop = true;
    }
}
