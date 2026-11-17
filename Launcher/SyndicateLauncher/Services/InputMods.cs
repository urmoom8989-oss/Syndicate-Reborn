using System;
using System.Collections.Concurrent;
using System.IO;
using System.Linq;
using System.Runtime.InteropServices;
using System.Threading;

namespace SyndicateLauncher.Services;

/// <summary>
/// Syndicate's launcher-side input mods: Sprint by default and Auto run.
/// They work while Fortnite is the foreground window and only for the process
/// handed to <see cref="Attach"/>.
/// </summary>
public static class InputMods
{
    private static bool _started;
    private static int _gamePid;
    private static int _runningSeason;
    private static volatile bool _sessionStarted;
    private static volatile string _sprintKey = "key:160";
    // The key the sprint loop really holds: the game's own Sprint bind when the settings save names one,
    // otherwise the launcher's sprint key. Empty when that key belongs to another action in game.
    private static volatile string _activeSprintKey = "key:160";
    private static long _bindsCheckedAt;
    private static volatile string _autoRunKeyCache = "key:187";

    // ---- switches and keys (shared flag files with Syndicate) --------------
    public static bool SprintByDefaultEnabled
    {
        get => ClientMods.Flag("sprint-default-on.txt");
        set { ClientMods.SetFlag("sprint-default-on.txt", value); _sprintFlag = value; }
    }

    public static bool AutoRunEnabled
    {
        get => ClientMods.Flag("autorun-on.txt");
        set
        {
            ClientMods.SetFlag("autorun-on.txt", value);
            _autoRunFlag = value;
            if (!value) _autoRunActive = false;
        }
    }

    // The input loop runs every 10 ms while a game is attached. Reading the two flag files on every pass
    // was 200 file checks a second for as long as the launcher was open; read them once a second instead.
    private static volatile bool _sprintFlag, _autoRunFlag;
    private static long _flagsReadAt;

    private static void RefreshFlags(long now)
    {
        if (now - _flagsReadAt < 1000 && _flagsReadAt != 0) return;
        _flagsReadAt = now;
        _sprintFlag = SprintByDefaultEnabled;
        _autoRunFlag = AutoRunEnabled;
    }

    public static string SprintKey
    {
        get
        {
            string value = ClientMods.Text("sprint-key.txt", "key:160");
            return ValidKey(value) ? value : "key:160";
        }
        set
        {
            if (!ValidKey(value)) return;
            _sprintKey = value;
            ClientMods.SetText("sprint-key.txt", value);
        }
    }

    public static string AutoRunKey
    {
        get
        {
            string value = ClientMods.Text("autorun-key.txt", "key:187");
            return ValidKey(value) ? value : "key:187";
        }
        set
        {
            if (!ValidKey(value)) return;
            _autoRunKeyCache = value;
            ClientMods.SetText("autorun-key.txt", value);
        }
    }

    public static bool ValidKey(string value) =>
        value is "mouse:middle" or "mouse:x1" or "mouse:x2" ||
        value.StartsWith("key:", StringComparison.Ordinal) &&
        int.TryParse(value.AsSpan(4), out int vk) && vk is > 7 and < 255;

    // ---- lifecycle -----------------------------------------------------
    public static void Start()
    {
        if (_started) return;
        _started = true;
        _sprintKey = SprintKey;
        _autoRunKeyCache = AutoRunKey;
        _sprintFlag = SprintByDefaultEnabled;
        _autoRunFlag = AutoRunEnabled;
        InstallSprintKeyHook();
        var thread = new Thread(SprintLoop) { IsBackground = true, Name = "Syndicate sprint by default" };
        thread.Start();    }

    /// <summary>Called when Fortnite starts. The loop follows the flags live.</summary>
    public static void Attach(int gamePid, int season)
    {
        Start();
        _gamePid = gamePid;
        _runningSeason = season;
        _bindsCheckedAt = 0;
        _sessionStarted = true;
        _autoRunFlag = AutoRunEnabled;
        // A fresh pair of hooks for every match: Windows silently drops a low-level hook that once answered too
        // slowly, and then the auto run key (and sprint's modifier fix) stopped working until the launcher restarted.
        RequestRehook("new match");
        ClientMods.SetFlag("sprint-native-season.txt", season >= 5);
        ClientMods.SetFlag("controller-binds-native-season.txt", season >= 6);
    }

    public static void Detach()
    {
        _gamePid = 0;
        _sessionStarted = false;
        _autoRunActive = false;
    }

    // ---- auto run ------------------------------------------------------
    private static volatile bool _autoRunActive;
    private static bool _autoRunHoldingW;
    private static long _autoRunPressedAt;
    private static int _autoRunRepresses;
    private static volatile bool _autoRunHandedToW;
    private static IntPtr _autoRunMouseHook;
    private static LowLevelKeyboardProc? _autoRunMouseProc;

    private static bool AutoRunGameFocused()
    {
        int pid = _gamePid;
        if (pid == 0) return false;
        GetWindowThreadProcessId(GetForegroundWindow(), out uint foreground);
        return foreground == (uint)pid;
    }

    private static void ToggleAutoRun(string how)
    {
        _autoRunActive = !_autoRunActive;
        Debug("auto run " + (_autoRunActive ? "on" : "off") + " (" + how + ")");
    }

    private static bool AutoRunKeyEvent(int vk, bool isDown, bool isUp)
    {
        // No file reads in here: this runs inside the keyboard hook, and a hook that answers slowly is removed by Windows.
        if (!_autoRunFlag || !AutoRunGameFocused()) return false;
        string key = _autoRunKeyCache.Length > 0 ? _autoRunKeyCache : AutoRunKey;
        if (key.StartsWith("key:", StringComparison.Ordinal) && VirtualKeyOf(key) == vk)
        {
            if (isDown && Interlocked.Read(ref _realKeyDown[vk]) == 0)
            {
                Interlocked.Exchange(ref _realKeyDown[vk], 1);
                ToggleAutoRun("key");
            }
            if (isUp) Interlocked.Exchange(ref _realKeyDown[vk], 0);
            return true;
        }
        if (isDown && _autoRunActive && (vk == VK_W || vk == VK_S))
        {
            _autoRunActive = false;
            _autoRunHandedToW = vk == VK_W;
            Debug("auto run off (" + (vk == VK_W ? "W" : "S") + " pressed)");
        }
        return false;
    }

    private static IntPtr AutoRunMouseProc(int nCode, IntPtr wParam, IntPtr lParam)
    {
        try
        {
            Interlocked.Exchange(ref _lastHookEvent, Environment.TickCount64);
            int msg = (int)wParam;
            if (nCode >= 0 && (msg is WM_MBUTTONDOWN or WM_XBUTTONDOWN or WM_MBUTTONUP or WM_XBUTTONUP) &&
                _autoRunFlag && AutoRunGameFocused())
            {
                var m = Marshal.PtrToStructure<MSLLHOOKSTRUCT>(lParam);
                if ((m.flags & LLMHF_INJECTED) == 0)
                {
                    string button = msg is WM_MBUTTONDOWN or WM_MBUTTONUP ? "mouse:middle"
                        : ((m.mouseData >> 16) & 0xFFFF) == 1 ? "mouse:x1" : "mouse:x2";
                    if (button == (_autoRunKeyCache.Length > 0 ? _autoRunKeyCache : AutoRunKey))
                    {
                        if (msg is WM_MBUTTONDOWN or WM_XBUTTONDOWN) ToggleAutoRun(button);
                        return (IntPtr)1;
                    }
                }
            }
        }
        catch { }
        return CallNextHookEx(_autoRunMouseHook, nCode, wParam, lParam);
    }

    private static void AutoRunTick()
    {
        bool focused = AutoRunGameFocused();
        if (!_autoRunFlag) _autoRunActive = false;
        if (_autoRunActive && !focused && _gamePid != 0 && _autoRunHoldingW)
        {
            _autoRunActive = false;
            Debug("auto run off (Fortnite not focused)");
        }
        if (_gamePid == 0) _autoRunActive = false;
        bool want = _autoRunActive && focused;
        long now = Environment.TickCount64;
        if (want && !_autoRunHoldingW)
        {
            SendInput(1, new[] { WKey(down: true) }, Marshal.SizeOf<INPUT>());
            _autoRunHoldingW = true;
            _autoRunHandedToW = false;
            _autoRunPressedAt = now;
            Debug("auto run: holding W");
        }
        else if (want && !Down(VK_W) && now - _autoRunPressedAt > 250)
        {
            // W is no longer down although auto run holds it (the game or Windows reset the key state, e.g. a
            // loading screen, the bus or a focus change). Press it again so the character keeps running.
            SendInput(1, new[] { WKey(down: true) }, Marshal.SizeOf<INPUT>());
            _autoRunPressedAt = now;
            if (_autoRunRepresses++ < 20) Debug("auto run: W was let go by the game, pressed again");
        }
        else if (!want && _autoRunHoldingW)
        {
            if (!_autoRunHandedToW) SendInput(1, new[] { WKey(down: false) }, Marshal.SizeOf<INPUT>());
            _autoRunHoldingW = false;
            _autoRunHandedToW = false;
        }
    }

    private static INPUT WKey(bool down)
    {
        var input = new INPUT { type = INPUT_KEYBOARD };
        uint scan = MapVirtualKey(VK_W, MAPVK_VK_TO_VSC_EX);
        input.ki.wVk = VK_W;
        input.ki.wScan = (ushort)(scan & 0xFF);
        input.ki.dwFlags = (scan != 0 ? KEYEVENTF_SCANCODE : 0) | (down ? 0 : KEYEVENTF_KEYUP);
        return input;
    }

    // ---- sprint by default --------------------------------------------
    private static readonly object _sprintSend = new();
    private static string? _sprintDown;
    private static readonly long[] _realKeyDown = new long[256];
    private static long _jumpReleasedAt;
    private static int _jumpRetaps;
    private static IntPtr _sprintHook;
    private static LowLevelKeyboardProc? _sprintHookProc;
    private static Thread? _sprintHookThread;

    private static bool IsModifierKey(string key) =>
        key.StartsWith("key:", StringComparison.Ordinal) && VirtualKeyOf(key) is 0x10 or 0x11 or 0x12 or >= 0xA0 and <= 0xA5;

    private static bool IsSprintVk(int vk, string key)
    {
        if (!key.StartsWith("key:", StringComparison.Ordinal)) return false;
        int s = VirtualKeyOf(key);
        int generic = s switch { 0xA0 or 0xA1 => 0x10, 0xA2 or 0xA3 => 0x11, 0xA4 or 0xA5 => 0x12, _ => s };
        return vk == s || vk == generic;
    }

    private static bool Movement(int vk) => vk is VK_W or VK_A or VK_S or VK_D;

    private static uint _hookThreadId;
    private static long _lastHookEvent;
    private const uint WM_REHOOK = 0x8000 + 0x51; // WM_APP + 0x51

    private static void HookUp()
    {
        if (_sprintHook != IntPtr.Zero) { UnhookWindowsHookEx(_sprintHook); _sprintHook = IntPtr.Zero; }
        if (_autoRunMouseHook != IntPtr.Zero) { UnhookWindowsHookEx(_autoRunMouseHook); _autoRunMouseHook = IntPtr.Zero; }
        _sprintHookProc ??= SprintHookProc;
        _autoRunMouseProc ??= AutoRunMouseProc;
        _sprintHook = SetWindowsHookEx(WH_KEYBOARD_LL, _sprintHookProc, GetModuleHandle(null), 0);
        if (_sprintHook == IntPtr.Zero) { Debug("hook: could not install (error " + Marshal.GetLastWin32Error() + ")"); return; }
        _autoRunMouseHook = SetWindowsHookEx(WH_MOUSE_LL, _autoRunMouseProc, GetModuleHandle(null), 0);
        Interlocked.Exchange(ref _lastHookEvent, Environment.TickCount64);
    }

    private static void RequestRehook(string why)
    {
        uint id = _hookThreadId;
        if (id != 0 && PostThreadMessage(id, WM_REHOOK, IntPtr.Zero, IntPtr.Zero)) Debug("hook: reinstalling (" + why + ")");
    }

    private static void InstallSprintKeyHook()
    {
        if (_sprintHookThread != null) return;
        _sprintHookThread = new Thread(() =>
        {
            _hookThreadId = GetCurrentThreadId();
            HookUp();
            Debug(_sprintHook == IntPtr.Zero ? "hook: not installed" : "hook: installed");
            while (GetMessage(out MSG msg, IntPtr.Zero, 0, 0) > 0)
                if (msg.message == WM_REHOOK) HookUp();
        })
        { IsBackground = true, Name = "Syndicate sprint key hook" };
        _sprintHookThread.Start();
    }

    // Windows removes a low-level hook without telling anyone when it once took too long to answer (a busy PC, a
    // slow disk). After that the auto run key reached the game as a normal key and nothing happened. While a game
    // runs, compare the last real input Windows saw with the last event our hooks saw, and hook up again if the
    // hooks have gone quiet while you were typing or moving the mouse.
    private static long _hookCheckedAt;
    private static void CheckHooksAlive(long now)
    {
        if (now - _hookCheckedAt < 2000) return;
        _hookCheckedAt = now;
        if (!AutoRunGameFocused()) return; // input to other (possibly elevated) windows never reaches our hooks
        var info = new LASTINPUTINFO { cbSize = (uint)Marshal.SizeOf<LASTINPUTINFO>() };
        if (!GetLastInputInfo(ref info)) return;
        long sinceInput = (uint)Environment.TickCount - info.dwTime; // both are 32-bit tick counts
        long lastInput = now - sinceInput;
        if (sinceInput < 300) return; // let the hook see the newest input first
        if (lastInput - Interlocked.Read(ref _lastHookEvent) > 1500) RequestRehook("Windows dropped the input hooks");
    }

    private static IntPtr SprintHookProc(int nCode, IntPtr wParam, IntPtr lParam)
    {
        try
        {
            if (nCode >= 0)
            {
                Interlocked.Exchange(ref _lastHookEvent, Environment.TickCount64);
                var k = Marshal.PtrToStructure<KBDLLHOOKSTRUCT>(lParam);
                int vk = (int)k.vkCode & 0xFF;
                bool injected = (k.flags & LLKHF_INJECTED) != 0;
                bool isDown = wParam == (IntPtr)WM_KEYDOWN || wParam == (IntPtr)WM_SYSKEYDOWN;
                bool isUp = wParam == (IntPtr)WM_KEYUP || wParam == (IntPtr)WM_SYSKEYUP;
                string key = _activeSprintKey.Length > 0 ? _activeSprintKey : _sprintKey;
                if (!injected && AutoRunKeyEvent(vk, isDown, isUp)) return (IntPtr)1;
                if (!injected && vk == VK_SPACE && isUp)
                {
                    Interlocked.Exchange(ref _jumpReleasedAt, Environment.TickCount64);
                    _jumpRetaps = 0;
                }
                if (!injected && !Movement(vk) && !IsSprintVk(vk, key))
                {
                    if (isUp) Interlocked.Exchange(ref _realKeyDown[vk], 0);
                    else if (isDown)
                    {
                        bool first = Interlocked.Read(ref _realKeyDown[vk]) == 0;
                        if (first) Interlocked.Exchange(ref _realKeyDown[vk], Environment.TickCount64);
                        if (first && IsModifierKey(key))
                        {
                            // Never wait on the sprint loop inside the hook: a hook that blocks is removed by Windows.
                            if (Monitor.TryEnter(_sprintSend))
                            {
                                try
                                {
                                    if (_sprintDown != null)
                                    {
                                        var batch = ReleaseInputs(_sprintDown).Append(KeyInput(k, up: false)).Append(SprintInput(_sprintDown, down: true)).ToArray();
                                        SendInput((uint)batch.Length, batch, Marshal.SizeOf<INPUT>());
                                        return (IntPtr)1;
                                    }
                                }
                                finally { Monitor.Exit(_sprintSend); }
                            }
                        }
                    }
                }
            }
        }
        catch { }
        return CallNextHookEx(_sprintHook, nCode, wParam, lParam);
    }

    private static void SprintLoop()
    {
        string lastState = "";
        while (true)
        {
            try
            {
                int pid = _gamePid;
                // No game attached and nothing held down: nothing to do, so check again in a fifth of a second
                // instead of spinning every 10 ms while the launcher just sits open.
                FlushDebug();
                if (pid == 0 && _sprintDown == null && !_autoRunHoldingW)
                {
                    Thread.Sleep(200);
                    continue;
                }
                bool want = false, pause = false;
                long now = Environment.TickCount64;
                RefreshFlags(now);
                if (pid != 0) CheckHooksAlive(now);
                string state = "off";
                bool enabled = _sessionStarted && _sprintFlag && _runningSeason >= 0 && _runningSeason < 5;
                if (enabled && now - _bindsCheckedAt > 2000)
                {
                    _bindsCheckedAt = now;
                    _activeSprintKey = ChooseSprintKey();
                }
                string key = _activeSprintKey;
                if (enabled && key.Length == 0) { enabled = false; state = "sprint key is another action in game"; }
                if (pid != 0 && enabled)
                {
                    GetWindowThreadProcessId(GetForegroundWindow(), out uint foreground);
                    bool focused = foreground == (uint)pid;
                    bool forward = Down(VK_W) && !Down(VK_S);
                    want = focused && forward;
                    state = !focused ? "game not focused" : !forward ? "not moving forward" : "sprinting";
                }
                lock (_sprintSend)
                {
                    long jumped = Interlocked.Read(ref _jumpReleasedAt);
                    if (jumped != 0 && _jumpRetaps < RetapAfterJumpMs.Length && now >= jumped + RetapAfterJumpMs[_jumpRetaps])
                    {
                        _jumpRetaps++;
                        if (_jumpRetaps >= RetapAfterJumpMs.Length) Interlocked.Exchange(ref _jumpReleasedAt, 0);
                        if (want && _sprintDown != null)
                        {
                            var up = ReleaseInputs(_sprintDown);
                            SendInput((uint)up.Length, up, Marshal.SizeOf<INPUT>());
                            _sprintDown = null;
                            pause = true;
                        }
                    }
                    if (_sprintDown != null && (!want || _sprintDown != key))
                    {
                        var up = ReleaseInputs(_sprintDown);
                        SendInput((uint)up.Length, up, Marshal.SizeOf<INPUT>());
                        _sprintDown = null;
                    }
                    if (want && !pause && (_sprintDown == null || !Down(VirtualKeyOf(key))))
                    {
                        SendInput(1, new[] { SprintInput(key, down: true) }, Marshal.SizeOf<INPUT>());
                        _sprintDown = key;
                    }
                }
                if (state != lastState)
                {
                    Debug("state: " + state + " (key " + key + ")");
                    lastState = state;
                }
                AutoRunTick();
            }
            catch { }
            Thread.Sleep(10);
        }
    }

    /// <summary>
    /// Pick the key to hold for sprint by default. The game's own Sprint bind wins (unless it is also a
    /// Crouch key). Otherwise the launcher's key, but never one the game uses for something else: holding
    /// a key that is bound to Crouch made the player crouch every time they pressed W.
    /// </summary>
    private static string ChooseSprintKey()
    {
        string configured = _sprintKey;
        var binds = GameKeybinds.For(_runningSeason) ?? (_runningSeason == 1 ? GameKeybinds.For(0) : null);
        if (binds is null) return configured;
        foreach (int vk in binds.Sprint)
            if (!binds.Crouch.Contains(vk) && vk is not (VK_W or VK_A or VK_S or VK_D))
            {
                string game = GameKeybinds.LauncherKey(vk);
                if (game != _activeSprintKey) Debug("sprint key from game binds: " + game);
                return game;
            }
        int mine = VirtualKeyOf(configured);
        int[] same = mine switch { 0x10 or 0xA0 or 0xA1 => new[] { 0xA0, 0xA1 }, 0x11 or 0xA2 or 0xA3 => new[] { 0xA2, 0xA3 }, 0x12 or 0xA4 or 0xA5 => new[] { 0xA4, 0xA5 }, _ => new[] { mine } };
        foreach (int vk in same)
            if (binds.Owner.TryGetValue(vk, out var action) && !action.Equals("Sprint", StringComparison.OrdinalIgnoreCase))
            {
                if (_activeSprintKey.Length != 0) Debug("sprint by default paused: " + configured + " is " + action + " in game");
                return "";
            }
        return configured;
    }

    private static bool Down(int vk) => (GetAsyncKeyState(vk) & 0x8000) != 0;

    // Lines are queued and written by the sprint loop, never from inside a hook (file writes there could make
    // Windows drop the hook).
    private static readonly ConcurrentQueue<string> _debugLines = new();
    private static void Debug(string line)
    {
        if (_debugLines.Count < 500) _debugLines.Enqueue(DateTime.Now.ToString("HH:mm:ss.fff") + " " + line);
    }

    private static void FlushDebug()
    {
        if (_debugLines.IsEmpty) return;
        try
        {
            string file = Path.Combine(ClientMods.Folder, "sprint-debug.log");
            if (File.Exists(file) && new FileInfo(file).Length > 200_000) File.Delete(file);
            var text = new System.Text.StringBuilder();
            while (_debugLines.TryDequeue(out var line)) text.Append(line).Append(Environment.NewLine);
            File.AppendAllText(file, text.ToString());
        }
        catch { }
    }

    private static int VirtualKeyOf(string key) => key switch
    {
        "mouse:middle" => 0x04,
        "mouse:x1" => 0x05,
        "mouse:x2" => 0x06,
        _ => int.TryParse(key.AsSpan(4), out int vk) ? vk : 0xA0
    };

    private static INPUT[] ReleaseInputs(string key)
    {
        var up = SprintInput(key, down: false);
        if (!key.StartsWith("key:", StringComparison.Ordinal) || VirtualKeyOf(key) is not (0x12 or 0xA4 or 0xA5)) return new[] { up };
        var mask = new INPUT { type = INPUT_KEYBOARD };
        mask.ki.wVk = 0xE8;
        var maskUp = mask;
        maskUp.ki.dwFlags = KEYEVENTF_KEYUP;
        return new[] { mask, maskUp, up };
    }

    private static INPUT SprintInput(string key, bool down)
    {
        var input = new INPUT();
        if (key.StartsWith("mouse:", StringComparison.Ordinal))
        {
            input.type = INPUT_MOUSE;
            if (key == "mouse:middle") input.mi.dwFlags = down ? MOUSEEVENTF_MIDDLEDOWN : MOUSEEVENTF_MIDDLEUP;
            else
            {
                input.mi.dwFlags = down ? MOUSEEVENTF_XDOWN : MOUSEEVENTF_XUP;
                input.mi.mouseData = key == "mouse:x1" ? 1u : 2u;
            }
        }
        else
        {
            int vk = VirtualKeyOf(key);
            uint scan = MapVirtualKey((uint)vk, MAPVK_VK_TO_VSC_EX);
            input.type = INPUT_KEYBOARD;
            input.ki.wVk = (ushort)vk;
            input.ki.wScan = (ushort)(scan & 0xFF);
            input.ki.dwFlags = (scan != 0 ? KEYEVENTF_SCANCODE : 0) | ((scan & 0xFF00) == 0xE000 ? KEYEVENTF_EXTENDEDKEY : 0) | (down ? 0 : KEYEVENTF_KEYUP);
        }
        return input;
    }

    private static INPUT KeyInput(KBDLLHOOKSTRUCT k, bool up)
    {
        var input = new INPUT { type = INPUT_KEYBOARD };
        input.ki.wVk = (ushort)k.vkCode;
        input.ki.wScan = (ushort)k.scanCode;
        input.ki.dwFlags = ((k.flags & LLKHF_EXTENDED) != 0 ? KEYEVENTF_EXTENDEDKEY : 0) | (up ? KEYEVENTF_KEYUP : 0);
        return input;
    }

    private const int VK_W = 0x57, VK_S = 0x53, VK_A = 0x41, VK_D = 0x44, VK_SPACE = 0x20;
    private static readonly long[] RetapAfterJumpMs = { 650, 1000 };
    private const uint INPUT_MOUSE = 0, INPUT_KEYBOARD = 1;
    private const uint KEYEVENTF_EXTENDEDKEY = 0x0001, KEYEVENTF_KEYUP = 0x0002, KEYEVENTF_SCANCODE = 0x0008;
    private const uint MOUSEEVENTF_MIDDLEDOWN = 0x0020, MOUSEEVENTF_MIDDLEUP = 0x0040, MOUSEEVENTF_XDOWN = 0x0080, MOUSEEVENTF_XUP = 0x0100;
    private const uint MAPVK_VK_TO_VSC_EX = 4;

    [StructLayout(LayoutKind.Sequential)]
    private struct KEYBDINPUT { public ushort wVk, wScan; public uint dwFlags, time; public IntPtr dwExtraInfo; }
    [StructLayout(LayoutKind.Sequential)]
    private struct MOUSEINPUT { public int dx, dy; public uint mouseData, dwFlags, time; public IntPtr dwExtraInfo; }
    [StructLayout(LayoutKind.Explicit)]
    private struct INPUT
    {
        [FieldOffset(0)] public uint type;
        [FieldOffset(8)] public KEYBDINPUT ki;
        [FieldOffset(8)] public MOUSEINPUT mi;
    }

    private delegate IntPtr LowLevelKeyboardProc(int nCode, IntPtr wParam, IntPtr lParam);
    [StructLayout(LayoutKind.Sequential)]
    private struct KBDLLHOOKSTRUCT { public uint vkCode, scanCode, flags, time; public IntPtr dwExtraInfo; }
    [StructLayout(LayoutKind.Sequential)]
    private struct MSLLHOOKSTRUCT { public int x, y; public uint mouseData, flags, time; public IntPtr dwExtraInfo; }
    [StructLayout(LayoutKind.Sequential)]
    private struct MSG { public IntPtr hwnd; public uint message; public IntPtr wParam, lParam; public uint time; public int ptX, ptY; }

    private const int WH_KEYBOARD_LL = 13, WH_MOUSE_LL = 14;
    private const int WM_KEYDOWN = 0x0100, WM_KEYUP = 0x0101, WM_SYSKEYDOWN = 0x0104, WM_SYSKEYUP = 0x0105;
    private const int WM_MBUTTONDOWN = 0x0207, WM_XBUTTONDOWN = 0x020B, WM_MBUTTONUP = 0x0208, WM_XBUTTONUP = 0x020C;
    private const uint LLKHF_EXTENDED = 0x01, LLKHF_INJECTED = 0x10, LLMHF_INJECTED = 0x01;

    [DllImport("user32.dll")] private static extern uint SendInput(uint nInputs, INPUT[] pInputs, int cbSize);
    [DllImport("user32.dll", SetLastError = true)] private static extern IntPtr SetWindowsHookEx(int idHook, LowLevelKeyboardProc lpfn, IntPtr hMod, uint dwThreadId);
    [DllImport("user32.dll")] private static extern bool UnhookWindowsHookEx(IntPtr hhk);
    [DllImport("user32.dll")] private static extern IntPtr CallNextHookEx(IntPtr hhk, int nCode, IntPtr wParam, IntPtr lParam);
    [DllImport("kernel32.dll", CharSet = CharSet.Unicode)] private static extern IntPtr GetModuleHandle(string? lpModuleName);
    [DllImport("user32.dll")] private static extern int GetMessage(out MSG lpMsg, IntPtr hWnd, uint wMsgFilterMin, uint wMsgFilterMax);
    [DllImport("user32.dll")] private static extern short GetAsyncKeyState(int vKey);
    [DllImport("user32.dll")] private static extern uint MapVirtualKey(uint uCode, uint uMapType);
    [DllImport("user32.dll")] private static extern IntPtr GetForegroundWindow();
    [DllImport("user32.dll", SetLastError = true)] private static extern bool PostThreadMessage(uint idThread, uint msg, IntPtr wParam, IntPtr lParam);
    [DllImport("kernel32.dll")] private static extern uint GetCurrentThreadId();
    [DllImport("user32.dll")] private static extern bool GetLastInputInfo(ref LASTINPUTINFO plii);
    [StructLayout(LayoutKind.Sequential)]
    private struct LASTINPUTINFO { public uint cbSize, dwTime; }
    [DllImport("user32.dll")] private static extern uint GetWindowThreadProcessId(IntPtr hWnd, out uint lpdwProcessId);
}
