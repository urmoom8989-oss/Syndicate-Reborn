// Syndicate © 2026 Syndicate. All rights reserved. Proprietary — no copying, distribution, or derivative works. See LICENSE.
using System;
using System.IO;
using System.Linq;
using System.Runtime.InteropServices;
using System.Threading;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Input;
using System.Windows.Threading;

namespace SyndicateClassic;

// Sprint by default, every season (Pre-season to Season 4 have no such setting: you hold your sprint
// key; later seasons have their own, which should stay off while this is on). While a season runs from Syndicate:
//   Keyboard: holding W (alone, or W+A / W+D) also holds your sprint key, while Fortnite is the
//             active window. The sprint key is chosen in Settings > Modded (a key or a mouse button).
//   Controller: PS4/PS5 pads (mirrored as an Xbox pad by ControllerService) get L3 held while the
//             left stick is pushed forward.
// Toggle: Settings > Modded (on = %LOCALAPPDATA%\ProjectTilted\sprint-default-on.txt,
// key = sprint-key.txt: "key:<virtual-key code>" or "mouse:middle|x1|x2").
public partial class MainWindow
{
    private DispatcherTimer? _sprintTimer;
    private static int _sprintGamePid; // Fortnite's process id while sprint by default applies, else 0
    private static Thread? _sprintThread;
    private bool? _sprintNativeSeason, _bindsNativeSeason;
    private static volatile string _sprintKey = "key:160"; // Left Shift

    internal static bool SprintByDefaultEnabled
    {
        get { try { return File.Exists(HudFlag("sprint-default-on.txt")); } catch { return false; } }
        set { SetHudFlag("sprint-default-on.txt", value); }
    }

    // Turbo build, every season (TiltedClient.dll does the work; on = turbo-build-on.txt).
    internal static bool TurboBuildEnabled
    {
        get { try { return File.Exists(HudFlag("turbo-build-on.txt")); } catch { return false; } }
        set { SetHudFlag("turbo-build-on.txt", value); }
    }

    internal static string SprintKey
    {
        get
        {
            try { var f = HudFlag("sprint-key.txt"); if (File.Exists(f)) { var v = File.ReadAllText(f).Trim(); if (ValidSprintKey(v)) return v; } }
            catch { }
            return "key:160";
        }
        set
        {
            if (!ValidSprintKey(value)) return;
            _sprintKey = value;
            try { var f = HudFlag("sprint-key.txt"); Directory.CreateDirectory(System.IO.Path.GetDirectoryName(f)!); File.WriteAllText(f, value); } catch { }
        }
    }

    private static bool ValidSprintKey(string v) =>
        v is "mouse:middle" or "mouse:x1" or "mouse:x2" ||
        (v.StartsWith("key:") && int.TryParse(v.AsSpan(4), out int vk) && vk is > 7 and < 255);

    internal static string SprintKeyName(string v)
    {
        switch (v)
        {
            case "mouse:middle": return "Middle mouse button";
            case "mouse:x1": return "Mouse button 4";
            case "mouse:x2": return "Mouse button 5";
        }
        if (!int.TryParse(v.AsSpan(4), out int vk)) return "Left Shift";
        var key = KeyInterop.KeyFromVirtualKey(vk);
        return key switch
        {
            Key.LeftShift => "Left Shift", Key.RightShift => "Right Shift",
            Key.LeftCtrl => "Left Ctrl", Key.RightCtrl => "Right Ctrl",
            Key.LeftAlt => "Left Alt", Key.RightAlt => "Right Alt",
            Key.CapsLock => "Caps Lock", Key.Space => "Space", Key.Tab => "Tab",
            >= Key.D0 and <= Key.D9 => ((int)(key - Key.D0)).ToString(),
            _ => key.ToString()
        };
    }

    // "Your sprint key: Left Shift  [Change]". Change waits for the next key or mouse button (Esc cancels).
    private FrameworkElement SprintKeyPicker() => KeyPicker("Your sprint key", "sprint key", () => SprintKey, v => SprintKey = v);

    private FrameworkElement KeyPicker(string caption, string what, Func<string> get, Action<string> set)
    {
        var label = NightText(caption + ": " + SprintKeyName(get()), 12.5, Night.Sub).Also(t => t.VerticalAlignment = VerticalAlignment.Center);
        var change = new Button { Content = "Change", Height = 32, Padding = new Thickness(14, 0, 14, 0), Margin = new Thickness(12, 0, 0, 0) };
        NightButton(change, "base");
        bool waiting = false;
        void Done(string? key)
        {
            waiting = false;
            if (key != null) set(key);
            label.Text = caption + ": " + SprintKeyName(get());
            change.Content = "Change";
        }
        change.Click += (_, __) =>
        {
            if (waiting) { Done(null); return; }
            waiting = true;
            label.Text = $"Press your {what} or mouse button… (Esc cancels)";
            change.Content = "Cancel";
            change.Focus();
        };
        PreviewKeyDown += (_, e) =>
        {
            if (!waiting) return;
            e.Handled = true;
            var key = e.Key == Key.System ? e.SystemKey : e.Key;
            if (key == Key.Escape) { Done(null); return; }
            int vk = KeyInterop.VirtualKeyFromKey(key);
            // WPF reports both Shifts as LeftShift/RightShift already; keep them apart.
            if (vk is > 7 and < 255) Done("key:" + vk);
        };
        PreviewMouseDown += (_, e) =>
        {
            if (!waiting) return;
            string? m = e.ChangedButton switch { MouseButton.Middle => "mouse:middle", MouseButton.XButton1 => "mouse:x1", MouseButton.XButton2 => "mouse:x2", _ => null };
            if (m != null) { e.Handled = true; Done(m); }
        };
        var row = new DockPanel { Margin = new Thickness(0, -6, 0, 18) };
        DockPanel.SetDock(change, Dock.Right);
        row.Children.Add(change); row.Children.Add(label);
        return row;
    }

    private void StartSprintByDefault()
    {
        if (_sprintTimer != null) return;
        _sprintKey = SprintKey;
        // The UI thread decides whether it applies (season, session, toggle); a background thread
        // does the key work every 10 ms so it reacts as fast as your finger.
        _sprintTimer = new DispatcherTimer { Interval = TimeSpan.FromMilliseconds(250) };
        _sprintTimer.Tick += (_, __) =>
        {
            int pid = 0;
            try
            {
                // Season 5 and later: Fortnite has its own sprint by default, and holding the sprint key for
                // you there made players unable to sprint or run, so Syndicate's stays off in those seasons.
                if (SprintByDefaultEnabled && _sessionStarted && _runningSeason is >= 0 and < 5 && _game is { HasExited: false } game)
                    pid = game.Id;
            }
            catch { pid = 0; }
            bool native = _sessionStarted && _runningSeason >= 5;
            if (native != _sprintNativeSeason) { _sprintNativeSeason = native; SetHudFlag("sprint-native-season.txt", native); } // read by TiltedPadAssist.dll
            // Custom controller binds are for Pre-season to Season 5 only: Season 6 brought Fortnite's own (TiltedPadAssist.dll).
            bool ownBinds = _sessionStarted && _runningSeason >= 6;
            if (ownBinds != _bindsNativeSeason) { _bindsNativeSeason = ownBinds; SetHudFlag("controller-binds-native-season.txt", ownBinds); }
            Volatile.Write(ref _sprintGamePid, pid);
            int autoPid = 0;
            try { if (AutoRunEnabled && _sessionStarted && _runningSeason is >= 0 && _game is { HasExited: false } g) autoPid = g.Id; } catch { autoPid = 0; }
            Volatile.Write(ref _autoRunGamePid, autoPid);
            if (pid != 0 || autoPid != 0) InstallSprintKeyHook(); // only once a season that uses it is running
            ControllerService.SprintAssist = pid != 0;
        };
        _sprintTimer.Start();
        _sprintThread = new Thread(SprintLoop) { IsBackground = true, Name = "Syndicate sprint by default" };
        _sprintThread.Start();
        Closed += (_, __) => { _sprintTimer?.Stop(); Volatile.Write(ref _sprintGamePid, 0); ControllerService.SprintAssist = false; RemoveSprintKeyHook(); };
    }

    // How the sprint key and your other keys are kept apart.
    // When the sprint key is a modifier (Shift/Ctrl/Alt), the old builds read any key pressed while it is held as
    // "Shift + key" and ignore it (no jump, no build switch, no weapon swap). Polling for other keys was too slow:
    // the key reached the game before the sprint key came up. A low-level keyboard hook now sees each real key
    // press first: if the launcher is holding the sprint key, it sends "sprint key up" and then your key, in that
    // order, so the game gets a plain key press. The sprint key stays up while that key is held and is pressed
    // again after it comes up (tapped a few times, so a sprint interrupted by a jump starts again on landing).
    // Mouse buttons and plain keys as the sprint key need none of this: other keys are passed through untouched.
    private static readonly object _sprintSend = new();
    private static string? _sprintDown;                  // the sprint key the launcher is holding down, if any
    private static readonly long[] _realKeyDown = new long[256]; // real (not injected) key presses still held, by time
    private static long _jumpReleasedAt;                // when you last let go of Space (jump), 0 = no re-press due
    private static int _jumpRetaps;                      // re-presses already sent for that jump
    private static IntPtr _sprintHook;
    private static LowLevelKeyboardProc? _sprintHookProc; // kept alive while the hook is installed

    private static bool IsModifierKey(string key) => key.StartsWith("key:") && VirtualKeyOf(key) is 0x10 or 0x11 or 0x12 or >= 0xA0 and <= 0xA5;
    private static bool IsSprintVk(int vk, string key)
    {
        if (!key.StartsWith("key:")) return false;
        int s = VirtualKeyOf(key);
        int generic = s switch { 0xA0 or 0xA1 => 0x10, 0xA2 or 0xA3 => 0x11, 0xA4 or 0xA5 => 0x12, _ => s };
        return vk == s || vk == generic;
    }
    private static bool Movement(int vk) => vk is VK_W or VK_A or VK_S or VK_D;

    // The hook runs on its own thread with its own message loop, so a busy launcher window never delays your keys.
    private static Thread? _sprintHookThread;
    private static void InstallSprintKeyHook()
    {
        if (_sprintHookThread != null) return;
        _sprintHookThread = new Thread(() =>
        {
            _sprintHookProc = SprintHookProc;
            _sprintHook = SetWindowsHookEx(WH_KEYBOARD_LL, _sprintHookProc, GetModuleHandle(null), 0);
            SprintDebug(_sprintHook == IntPtr.Zero ? "hook: could not install (error " + Marshal.GetLastWin32Error() + ")" : "hook: installed");
            if (_sprintHook == IntPtr.Zero) return;
            _autoRunMouseProc = AutoRunMouseProc;
            _autoRunMouseHook = SetWindowsHookEx(WH_MOUSE_LL, _autoRunMouseProc, GetModuleHandle(null), 0);
            while (GetMessage(out _, IntPtr.Zero, 0, 0) > 0) { }
        }) { IsBackground = true, Name = "Syndicate sprint key hook" };
        _sprintHookThread.Start();
    }
    private static void RemoveSprintKeyHook()
    {
        if (_sprintHook != IntPtr.Zero) { UnhookWindowsHookEx(_sprintHook); _sprintHook = IntPtr.Zero; }
    }

    private static IntPtr SprintHookProc(int nCode, IntPtr wParam, IntPtr lParam)
    {
        try
        {
            if (nCode >= 0)
            {
                var k = Marshal.PtrToStructure<KBDLLHOOKSTRUCT>(lParam);
                int vk = (int)k.vkCode & 0xFF;
                bool injected = (k.flags & LLKHF_INJECTED) != 0;
                bool isDown = wParam == (IntPtr)WM_KEYDOWN || wParam == (IntPtr)WM_SYSKEYDOWN;
                bool isUp = wParam == (IntPtr)WM_KEYUP || wParam == (IntPtr)WM_SYSKEYUP;
                string key = _sprintKey;
                if (!injected && AutoRunKeyEvent(vk, isDown, isUp)) return (IntPtr)1; // the auto run key itself goes no further
                if (!injected && vk == VK_SPACE && isUp) { Volatile.Write(ref _jumpReleasedAt, Environment.TickCount64); _jumpRetaps = 0; }
                if (!injected && !Movement(vk) && !IsSprintVk(vk, key))
                {
                    if (isUp) Volatile.Write(ref _realKeyDown[vk], 0);
                    else if (isDown)
                    {
                        bool first = Volatile.Read(ref _realKeyDown[vk]) == 0;
                        if (first) Volatile.Write(ref _realKeyDown[vk], Environment.TickCount64);
                        if (first && IsModifierKey(key))
                            lock (_sprintSend)
                            {
                                if (_sprintDown != null)
                                {
                                    // Sprint key up, this key, sprint key down again, as one batch so they arrive in this
                                    // order: the game reads a plain key press (no Shift+E), and sprint is back the same
                                    // instant. Before, sprint stayed up for as long as the key was held, so picking up
                                    // loot or opening a chest (holding E) slowed you to a walk.
                                    var batch = ReleaseInputs(_sprintDown).Append(KeyInput(k, up: false)).Append(SprintInput(_sprintDown, down: true)).ToArray();
                                    SendInput((uint)batch.Length, batch, Marshal.SizeOf<INPUT>());
                                    return (IntPtr)1; // the original press is replaced by the one just sent
                                }
                            }
                    }
                }
            }
        }
        catch { }
        return CallNextHookEx(_sprintHook, nCode, wParam, lParam);
    }

    private static bool OtherKeyHeld(long now)
    {
        // A key the hook saw go down and not come up. One that has read as held for 1.5 s is ignored, so a
        // missed key-up (focus change, alt-tab) can never keep sprinting off.
        for (int vk = 0; vk < 256; vk++)
        {
            long t = Volatile.Read(ref _realKeyDown[vk]);
            if (t != 0 && now - t < OtherKeyLimitMs) return true;
        }
        return false;
    }

    private static void SprintLoop()
    {
        string lastState = "";
        while (true)
        {
            try
            {
                int pid = Volatile.Read(ref _sprintGamePid);
                string key = _sprintKey;
                bool want = false, pause = false;
                long now = Environment.TickCount64;
                string state = "off";
                if (pid != 0)
                {
                    GetWindowThreadProcessId(GetForegroundWindow(), out uint foreground);
                    bool focused = foreground == (uint)pid, forward = Down(VK_W) && !Down(VK_S);
                    // Only a modifier sprint key has to come up for other keys (see above).
                    // Other keys no longer stop sprinting: the hook lifts the sprint key only for the instant of their press.
                    want = focused && forward;
                    state = !focused ? "game not focused" : !forward ? "not moving forward" : "sprinting";
                }
                lock (_sprintSend)
                {
                    // After a jump the old builds stop sprinting on landing until the key is pressed again. Re-press it
                    // twice, around when you land (RetapAfterJumpMs). This used to happen after ANY key (building,
                    // weapon switch, crouch), five times in 1.3 s: each re-press dropped sprint for a moment, which
                    // felt like stuttering or slowing down, worst with auto run on.
                    long jumped = Volatile.Read(ref _jumpReleasedAt);
                    if (jumped != 0 && _jumpRetaps < RetapAfterJumpMs.Length && now >= jumped + RetapAfterJumpMs[_jumpRetaps])
                    {
                        _jumpRetaps++;
                        if (_jumpRetaps >= RetapAfterJumpMs.Length) Volatile.Write(ref _jumpReleasedAt, 0);
                        if (want && _sprintDown != null)
                        {
                            { var up = ReleaseInputs(_sprintDown); SendInput((uint)up.Length, up, Marshal.SizeOf<INPUT>()); }
                            _sprintDown = null; pause = true;
                        }
                    }
                    if (_sprintDown != null && (!want || _sprintDown != key))
                    {
                        { var up = ReleaseInputs(_sprintDown); SendInput((uint)up.Length, up, Marshal.SizeOf<INPUT>()); }
                        _sprintDown = null;
                    }
                    if (want && !pause && (_sprintDown == null || !Down(VirtualKeyOf(key))))
                    {
                        // Also re-presses the key if you tapped and released it yourself while running.
                        SendInput(1, new[] { SprintInput(key, down: true) }, Marshal.SizeOf<INPUT>());
                        _sprintDown = key;
                    }
                }
                if (state != lastState) { SprintDebug("state: " + state + " (key " + key + ")"); lastState = state; }
                AutoRunTick();
            }
            catch { }
            Thread.Sleep(10);
        }
    }

    private static bool Down(int vk) => (GetAsyncKeyState(vk) & 0x8000) != 0;

    // %LOCALAPPDATA%\ProjectTilted\sprint-debug.log: what sprint by default is doing, for bug reports (kept small).
    private static void SprintDebug(string line)
    {
        try
        {
            var f = HudFlag("sprint-debug.log");
            if (File.Exists(f) && new FileInfo(f).Length > 200_000) File.Delete(f);
            File.AppendAllText(f, DateTime.Now.ToString("HH:mm:ss.fff") + " " + line + Environment.NewLine);
        }
        catch { }
    }

    private static int VirtualKeyOf(string key) => key switch
    {
        "mouse:middle" => 0x04, "mouse:x1" => 0x05, "mouse:x2" => 0x06,
        _ => int.TryParse(key.AsSpan(4), out int vk) ? vk : 0xA0
    };

    // Letting go of Alt on its own makes Windows open the window menu, which takes keyboard input away from the
    // game until Alt is pressed again. A tap of an unassigned key first (0xE8, what AutoHotkey uses for the same
    // reason) makes the release count as a combination, so no menu opens.
    private static INPUT[] ReleaseInputs(string key)
    {
        var up = SprintInput(key, down: false);
        if (!key.StartsWith("key:") || VirtualKeyOf(key) is not (0x12 or 0xA4 or 0xA5)) return new[] { up };
        var mask = new INPUT { type = INPUT_KEYBOARD }; mask.ki.wVk = 0xE8;
        var maskUp = mask; maskUp.ki.dwFlags = KEYEVENTF_KEYUP;
        return new[] { mask, maskUp, up };
    }

    private static INPUT SprintInput(string key, bool down)
    {
        var input = new INPUT();
        if (key.StartsWith("mouse:"))
        {
            input.type = INPUT_MOUSE;
            if (key == "mouse:middle") input.mi.dwFlags = down ? MOUSEEVENTF_MIDDLEDOWN : MOUSEEVENTF_MIDDLEUP;
            else { input.mi.dwFlags = down ? MOUSEEVENTF_XDOWN : MOUSEEVENTF_XUP; input.mi.mouseData = key == "mouse:x1" ? 1u : 2u; }
        }
        else
        {
            int vk = VirtualKeyOf(key);
            uint scan = MapVirtualKey((uint)vk, MAPVK_VK_TO_VSC_EX); // the game tells left from right keys by it
            input.type = INPUT_KEYBOARD;
            input.ki.wVk = (ushort)vk;
            input.ki.wScan = (ushort)(scan & 0xFF);
            input.ki.dwFlags = (scan != 0 ? KEYEVENTF_SCANCODE : 0) | ((scan & 0xFF00) == 0xE000 ? KEYEVENTF_EXTENDEDKEY : 0) | (down ? 0 : KEYEVENTF_KEYUP);
        }
        return input;
    }

    // Replays a key press the hook caught, with the same key code, scan code and extended flag.
    private static INPUT KeyInput(KBDLLHOOKSTRUCT k, bool up)
    {
        var input = new INPUT { type = INPUT_KEYBOARD };
        input.ki.wVk = (ushort)k.vkCode;
        input.ki.wScan = (ushort)k.scanCode;
        input.ki.dwFlags = ((k.flags & LLKHF_EXTENDED) != 0 ? KEYEVENTF_EXTENDEDKEY : 0) | (up ? KEYEVENTF_KEYUP : 0);
        return input;
    }

    private const int VK_W = 0x57, VK_S = 0x53, VK_A = 0x41, VK_D = 0x44, VK_SPACE = 0x20;
    private const long OtherKeyLimitMs = 1500; // a key that reads as held longer than this no longer keeps the sprint key up
    private static readonly long[] RetapAfterJumpMs = { 650, 1000 }; // after letting go of jump: re-press sprint at these times
    private const uint INPUT_MOUSE = 0, INPUT_KEYBOARD = 1;
    private const uint KEYEVENTF_EXTENDEDKEY = 0x0001, KEYEVENTF_KEYUP = 0x0002, KEYEVENTF_SCANCODE = 0x0008;
    private const uint MOUSEEVENTF_MIDDLEDOWN = 0x0020, MOUSEEVENTF_MIDDLEUP = 0x0040, MOUSEEVENTF_XDOWN = 0x0080, MOUSEEVENTF_XUP = 0x0100;
    private const uint MAPVK_VK_TO_VSC_EX = 4;

    [StructLayout(LayoutKind.Sequential)]
    private struct KEYBDINPUT { public ushort wVk, wScan; public uint dwFlags, time; public IntPtr dwExtraInfo; }
    [StructLayout(LayoutKind.Sequential)]
    private struct MOUSEINPUT { public int dx, dy; public uint mouseData, dwFlags, time; public IntPtr dwExtraInfo; }
    // INPUT is a union; the mouse member is the largest, so it sets the size SendInput expects.
    [StructLayout(LayoutKind.Explicit)]
    private struct INPUT
    {
        [FieldOffset(0)] public uint type;
        [FieldOffset(8)] public KEYBDINPUT ki;
        [FieldOffset(8)] public MOUSEINPUT mi;
    }

    [DllImport("user32.dll")] private static extern uint SendInput(uint nInputs, INPUT[] pInputs, int cbSize);
    private delegate IntPtr LowLevelKeyboardProc(int nCode, IntPtr wParam, IntPtr lParam);
    [StructLayout(LayoutKind.Sequential)]
    private struct KBDLLHOOKSTRUCT { public uint vkCode, scanCode, flags, time; public IntPtr dwExtraInfo; }
    private const int WH_KEYBOARD_LL = 13, WM_KEYDOWN = 0x0100, WM_KEYUP = 0x0101, WM_SYSKEYDOWN = 0x0104, WM_SYSKEYUP = 0x0105;
    private const uint LLKHF_EXTENDED = 0x01, LLKHF_INJECTED = 0x10;
    [DllImport("user32.dll", SetLastError = true)] private static extern IntPtr SetWindowsHookEx(int idHook, LowLevelKeyboardProc lpfn, IntPtr hMod, uint dwThreadId);
    [DllImport("user32.dll")] private static extern bool UnhookWindowsHookEx(IntPtr hhk);
    [DllImport("user32.dll")] private static extern IntPtr CallNextHookEx(IntPtr hhk, int nCode, IntPtr wParam, IntPtr lParam);
    [DllImport("kernel32.dll", CharSet = CharSet.Unicode)] private static extern IntPtr GetModuleHandle(string? lpModuleName);
    [StructLayout(LayoutKind.Sequential)]
    private struct MSG { public IntPtr hwnd; public uint message; public IntPtr wParam, lParam; public uint time; public int ptX, ptY; }
    [DllImport("user32.dll")] private static extern int GetMessage(out MSG lpMsg, IntPtr hWnd, uint wMsgFilterMin, uint wMsgFilterMax);
    [DllImport("user32.dll")] private static extern short GetAsyncKeyState(int vKey);
    [DllImport("user32.dll")] private static extern uint MapVirtualKey(uint uCode, uint uMapType);
    [DllImport("user32.dll")] private static extern IntPtr GetForegroundWindow();
    [DllImport("user32.dll")] private static extern uint GetWindowThreadProcessId(IntPtr hWnd, out uint lpdwProcessId);
}