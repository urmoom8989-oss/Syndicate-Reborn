// Syndicate © 2026 Syndicate. All rights reserved. Proprietary — no copying, distribution, or derivative works. See LICENSE.
using System;
using System.Runtime.InteropServices;
using System.Threading;

namespace SyndicateClassic;

// Auto run, every season (Pre-season to Season 2 have none; later seasons have their own, so pick a
// different key here or leave theirs unbound). Settings > Modded: on/off
// (%LOCALAPPDATA%\ProjectTilted\autorun-on.txt) and the key (autorun-key.txt, "key:<vk>" or
// "mouse:middle|x1|x2"; default = the "=" key, Fortnite's later auto run key).
//
// Press the key in Fortnite: your character runs forward on its own (the launcher holds W for you), and
// with sprint by default on it sprints too. Press the key again, W or S to stop. Switching away from
// Fortnite stops it, so W is never held in another window. The key itself is not passed to the game.
// The keyboard hook and the mouse hook (for mouse button keys) live on the sprint hook thread.
public partial class MainWindow
{
    private static int _autoRunGamePid;            // Fortnite's process id while auto run applies, else 0
    private static volatile bool _autoRunActive;   // the player switched auto run on
    private static bool _autoRunHoldingW;          // the launcher is holding W (sprint loop thread only)
    private static IntPtr _autoRunMouseHook;
    private static LowLevelKeyboardProc? _autoRunMouseProc;

    internal static bool AutoRunEnabled
    {
        get { try { return System.IO.File.Exists(HudFlag("autorun-on.txt")); } catch { return false; } }
        set { SetHudFlag("autorun-on.txt", value); if (!value) _autoRunActive = false; }
    }

    private static volatile string _autoRunKeyCache = "";
    internal static string AutoRunKey
    {
        get
        {
            try { var f = HudFlag("autorun-key.txt"); if (System.IO.File.Exists(f)) { var v = System.IO.File.ReadAllText(f).Trim(); if (ValidSprintKey(v)) return _autoRunKeyCache = v; } }
            catch { }
            return _autoRunKeyCache = "key:187"; // "=" (VK_OEM_PLUS)
        }
        set
        {
            if (!ValidSprintKey(value)) return;
            _autoRunKeyCache = value;
            try { var f = HudFlag("autorun-key.txt"); System.IO.Directory.CreateDirectory(System.IO.Path.GetDirectoryName(f)!); System.IO.File.WriteAllText(f, value); } catch { }
        }
    }
    private static string AutoRunKeyNow => _autoRunKeyCache.Length > 0 ? _autoRunKeyCache : AutoRunKey;

    private static bool AutoRunGameFocused()
    {
        int pid = Volatile.Read(ref _autoRunGamePid);
        if (pid == 0) return false;
        GetWindowThreadProcessId(GetForegroundWindow(), out uint foreground);
        return foreground == (uint)pid;
    }

    private static void ToggleAutoRun(string how)
    {
        _autoRunActive = !_autoRunActive;
        SprintDebug("auto run " + (_autoRunActive ? "on" : "off") + " (" + how + ")");
    }

    // Keyboard hook (real key events only). True = swallow this event.
    private static bool AutoRunKeyEvent(int vk, bool isDown, bool isUp)
    {
        if (!AutoRunGameFocused()) return false;
        string key = AutoRunKeyNow;
        if (key.StartsWith("key:") && VirtualKeyOf(key) == vk)
        {
            if (isDown && Volatile.Read(ref _realKeyDown[vk]) == 0) { Volatile.Write(ref _realKeyDown[vk], 1); ToggleAutoRun("key"); }
            if (isUp) Volatile.Write(ref _realKeyDown[vk], 0);
            return true;
        }
        // Your own W or S takes over (W keeps going because you hold it; S stops).
        if (isDown && _autoRunActive && (vk == VK_W || vk == VK_S)) { _autoRunActive = false; _autoRunHandedToW = vk == VK_W; SprintDebug("auto run off (" + (vk == VK_W ? "W" : "S") + " pressed)"); }
        return false;
    }
    private static volatile bool _autoRunHandedToW; // stopped by your own W press: do not send W up (you are holding it)

    private const int WH_MOUSE_LL = 14, WM_MBUTTONDOWN = 0x0207, WM_XBUTTONDOWN = 0x020B, WM_MBUTTONUP = 0x0208, WM_XBUTTONUP = 0x020C;
    private const uint LLMHF_INJECTED = 0x01;
    [StructLayout(LayoutKind.Sequential)]
    private struct MSLLHOOKSTRUCT { public int x, y; public uint mouseData, flags, time; public IntPtr dwExtraInfo; }

    private static IntPtr AutoRunMouseProc(int nCode, IntPtr wParam, IntPtr lParam)
    {
        try
        {
            int msg = (int)wParam;
            if (nCode >= 0 && (msg is WM_MBUTTONDOWN or WM_XBUTTONDOWN or WM_MBUTTONUP or WM_XBUTTONUP) && AutoRunGameFocused())
            {
                var m = Marshal.PtrToStructure<MSLLHOOKSTRUCT>(lParam);
                if ((m.flags & LLMHF_INJECTED) == 0)
                {
                    string button = msg is WM_MBUTTONDOWN or WM_MBUTTONUP ? "mouse:middle" : ((m.mouseData >> 16) & 0xFFFF) == 1 ? "mouse:x1" : "mouse:x2";
                    if (button == AutoRunKeyNow)
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

    // Sprint loop thread, every 10 ms: hold or release W.
    private static void AutoRunTick()
    {
        bool focused = AutoRunGameFocused();
        if (_autoRunActive && !focused && Volatile.Read(ref _autoRunGamePid) != 0 && _autoRunHoldingW) { _autoRunActive = false; SprintDebug("auto run off (Fortnite not focused)"); }
        if (Volatile.Read(ref _autoRunGamePid) == 0) _autoRunActive = false;
        bool want = _autoRunActive && focused;
        if (want && !_autoRunHoldingW)
        {
            SendInput(1, new[] { WKey(down: true) }, Marshal.SizeOf<INPUT>());
            _autoRunHoldingW = true; _autoRunHandedToW = false;
        }
        else if (!want && _autoRunHoldingW)
        {
            if (!_autoRunHandedToW) SendInput(1, new[] { WKey(down: false) }, Marshal.SizeOf<INPUT>());
            _autoRunHoldingW = false; _autoRunHandedToW = false;
        }
    }

    private static INPUT WKey(bool down)
    {
        var input = new INPUT { type = INPUT_KEYBOARD };
        uint scan = MapVirtualKey((uint)VK_W, MAPVK_VK_TO_VSC_EX);
        input.ki.wVk = (ushort)VK_W;
        input.ki.wScan = (ushort)(scan & 0xFF);
        input.ki.dwFlags = (scan != 0 ? KEYEVENTF_SCANCODE : 0) | (down ? 0 : KEYEVENTF_KEYUP);
        return input;
    }
}