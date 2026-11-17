using System;
using System.Runtime.InteropServices;
using System.Text;

namespace SyndicateClassic
{
    // Win32 helpers to embed an external program's top-level window (the rendering Fortnite client, and the Edge
    // app-mode map editor) inside one of our WPF windows: reparent with SetParent, strip the frame, then we position
    // it ourselves. Dev Kit only - this is the in-engine Level Editor's one fragile bit, so callers log every step.
    internal static class NativeEmbed
    {
        const int GWL_STYLE = -16, GWL_EXSTYLE = -20;
        const long WS_CHILD = 0x40000000L, WS_POPUP = 0x80000000L, WS_CAPTION = 0x00C00000L, WS_THICKFRAME = 0x00040000L,
                   WS_MINIMIZEBOX = 0x00020000L, WS_MAXIMIZEBOX = 0x00010000L, WS_SYSMENU = 0x00080000L,
                   WS_BORDER = 0x00800000L, WS_DLGFRAME = 0x00400000L;
        const long WS_EX_APPWINDOW = 0x00040000L, WS_EX_WINDOWEDGE = 0x00000100L, WS_EX_CLIENTEDGE = 0x00000200L,
                   WS_EX_DLGMODALFRAME = 0x00000001L, WS_EX_STATICEDGE = 0x00020000L;
        const uint SWP_NOSIZE = 0x1, SWP_NOMOVE = 0x2, SWP_NOZORDER = 0x4, SWP_NOACTIVATE = 0x10, SWP_FRAMECHANGED = 0x20, SWP_SHOWWINDOW = 0x40;
        const int SW_SHOW = 5;

        [StructLayout(LayoutKind.Sequential)] struct RECT { public int Left, Top, Right, Bottom; }
        [StructLayout(LayoutKind.Sequential)] struct POINT { public int X, Y; }
        delegate bool EnumWindowsProc(IntPtr hWnd, IntPtr p);

        [DllImport("user32.dll")] static extern IntPtr SetParent(IntPtr child, IntPtr newParent);
        [DllImport("user32.dll", EntryPoint = "GetWindowLongW")] static extern int GetWindowLong(IntPtr h, int i);
        [DllImport("user32.dll", EntryPoint = "SetWindowLongW")] static extern int SetWindowLong(IntPtr h, int i, int v);
        [DllImport("user32.dll", EntryPoint = "GetWindowLongPtrW")] static extern IntPtr GetWindowLongPtr(IntPtr h, int i);
        [DllImport("user32.dll", EntryPoint = "SetWindowLongPtrW")] static extern IntPtr SetWindowLongPtr(IntPtr h, int i, IntPtr v);
        [DllImport("user32.dll")] static extern bool SetWindowPos(IntPtr h, IntPtr after, int x, int y, int cx, int cy, uint flags);
        [DllImport("user32.dll")] static extern bool MoveWindow(IntPtr h, int x, int y, int w, int ht, bool repaint);
        [DllImport("user32.dll")] static extern bool ShowWindow(IntPtr h, int cmd);
        [DllImport("user32.dll")] static extern bool IsWindow(IntPtr h);
        [DllImport("user32.dll")] static extern bool IsWindowVisible(IntPtr h);
        [DllImport("user32.dll")] static extern bool EnumWindows(EnumWindowsProc cb, IntPtr p);
        [DllImport("user32.dll")] static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
        [DllImport("user32.dll")] static extern int GetWindowTextLength(IntPtr h);
        [DllImport("user32.dll", CharSet = CharSet.Unicode)] static extern int GetWindowText(IntPtr h, StringBuilder s, int max);
        [DllImport("user32.dll", CharSet = CharSet.Unicode)] static extern int GetClassName(IntPtr h, StringBuilder s, int max);
        [DllImport("user32.dll")] static extern bool GetClientRect(IntPtr h, out RECT r);
        [DllImport("user32.dll")] static extern bool ClientToScreen(IntPtr h, ref POINT p);
        [DllImport("user32.dll")] static extern bool AttachThreadInput(uint idAttach, uint idAttachTo, bool fAttach);
        [DllImport("user32.dll")] static extern IntPtr SetFocus(IntPtr h);
        [DllImport("user32.dll")] static extern bool SetForegroundWindow(IntPtr h);
        [DllImport("user32.dll")] static extern bool BringWindowToTop(IntPtr h);
        [DllImport("kernel32.dll")] static extern uint GetCurrentThreadId();

        static bool Is64 => IntPtr.Size == 8;
        static long GetStyle(IntPtr h, int idx) => Is64 ? GetWindowLongPtr(h, idx).ToInt64() : (uint)GetWindowLong(h, idx);
        static void SetStyle(IntPtr h, int idx, long v) { if (Is64) SetWindowLongPtr(h, idx, new IntPtr(v)); else SetWindowLong(h, idx, unchecked((int)v)); }

        public static bool Alive(IntPtr h) => h != IntPtr.Zero && IsWindow(h);

        public static int PidOf(IntPtr h) { if (!Alive(h)) return 0; GetWindowThreadProcessId(h, out var pid); return (int)pid; }

        static string Title(IntPtr h) { int n = GetWindowTextLength(h); if (n == 0) return ""; var sb = new StringBuilder(n + 2); GetWindowText(h, sb, sb.Capacity); return sb.ToString(); }
        static string Cls(IntPtr h) { var sb = new StringBuilder(256); GetClassName(h, sb, 256); return sb.ToString(); }

        // The main visible, titled top-level window of a process (optionally matching a window class exactly).
        public static IntPtr FindWindowForProcess(int pid, string? classEquals = null)
        {
            IntPtr found = IntPtr.Zero;
            EnumWindows((h, _) =>
            {
                GetWindowThreadProcessId(h, out var wpid);
                if (wpid != (uint)pid || !IsWindowVisible(h)) return true;
                if (classEquals != null) { if (!string.Equals(Cls(h), classEquals, StringComparison.OrdinalIgnoreCase)) return true; }
                else if (Title(h).Length == 0) return true;
                found = h; return false;
            }, IntPtr.Zero);
            return found;
        }

        // The RENDERING Fortnite window of a process: visible, a real UnrealWindow, and a non-trivial size. The
        // headless match host (-nullrhi) draws nothing and has no such window, so this skips it and finds the client
        // you actually see - which is the one worth embedding (the host window would just be black).
        public static IntPtr FindRenderingWindow(int pid)
        {
            IntPtr found = IntPtr.Zero;
            EnumWindows((h, _) =>
            {
                GetWindowThreadProcessId(h, out var wpid);
                if (wpid != (uint)pid || !IsWindowVisible(h)) return true;
                if (!GetClientRect(h, out var r) || r.Right - r.Left < 320 || r.Bottom - r.Top < 200) return true;
                var cn = new StringBuilder(64); GetClassName(h, cn, 64);
                if (!cn.ToString().Equals("UnrealWindow", StringComparison.OrdinalIgnoreCase) && Title(h).Length == 0) return true;
                found = h; return false;
            }, IntPtr.Zero);
            return found;
        }

        // Any visible top-level window whose title contains text (for the Edge app-mode editor window).
        public static IntPtr FindWindowByTitle(string titleContains)
        {
            IntPtr found = IntPtr.Zero;
            EnumWindows((h, _) =>
            {
                if (!IsWindowVisible(h)) return true;
                if (Title(h).IndexOf(titleContains, StringComparison.OrdinalIgnoreCase) >= 0) { found = h; return false; }
                return true;
            }, IntPtr.Zero);
            return found;
        }

        // Make the game a FRAMELESS TOP-LEVEL window (NOT a child). Reparenting it as a child broke all input: the
        // game - and the fly code - ignore keys/mouse unless their window is the foreground window, and a child window
        // can never be foreground. As a frameless top-level it stays foreground-capable (input works) while we position
        // it over the editor's left area and keep it just above the editor in z-order.
        public static void AttachOverlay(IntPtr game)
        {
            SetParent(game, IntPtr.Zero); // ensure it is nobody's child
            long style = GetStyle(game, GWL_STYLE);
            style &= ~(WS_CHILD | WS_CAPTION | WS_THICKFRAME | WS_MINIMIZEBOX | WS_MAXIMIZEBOX | WS_SYSMENU | WS_BORDER | WS_DLGFRAME);
            style |= WS_POPUP;
            SetStyle(game, GWL_STYLE, style);
            long ex = GetStyle(game, GWL_EXSTYLE);
            ex &= ~(WS_EX_CLIENTEDGE | WS_EX_WINDOWEDGE | WS_EX_DLGMODALFRAME | WS_EX_STATICEDGE);
            SetStyle(game, GWL_EXSTYLE, ex);
            SetWindowPos(game, IntPtr.Zero, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED | SWP_SHOWWINDOW);
        }

        // Position the overlay at SCREEN coords, inserted just above the editor window (no focus steal).
        public static void PlaceOverlay(IntPtr game, IntPtr aboveEditor, int sx, int sy, int w, int h)
        {
            if (Alive(game)) SetWindowPos(game, aboveEditor, sx, sy, Math.Max(1, w), Math.Max(1, h), SWP_NOACTIVATE);
        }

        // Syndicate Studio: make the frameless game window OWNED by the editor window. An owned window always sits just
        // above its owner, hides when the owner is minimised and stays a top-level window, so input keeps working
        // and no z-order juggling is needed. Zero gives it back to the desktop.
        public static void SetOwner(IntPtr game, IntPtr owner)
        {
            if (Alive(game)) SetWindowLongPtr(game, -8 /* GWLP_HWNDPARENT */, owner);
        }
        public static void PlaceOwned(IntPtr game, int sx, int sy, int w, int h)
        {
            if (Alive(game)) SetWindowPos(game, IntPtr.Zero, sx, sy, Math.Max(1, w), Math.Max(1, h), SWP_NOACTIVATE | SWP_NOZORDER);
        }

        // Hide the overlay while its panel is out of sight (minimised editor, another tab in front), show it again after.
        public static void SetShown(IntPtr game, bool shown)
        {
            if (Alive(game) && IsWindowVisible(game) != shown) ShowWindow(game, shown ? 4 /* SW_SHOWNOACTIVATE */ : 0 /* SW_HIDE */);
        }

        // Screen position of a window's client-area top-left (to convert the editor's left region to screen coords).
        public static void ClientTopLeftOnScreen(IntPtr h, out int sx, out int sy) { var p = new POINT { X = 0, Y = 0 }; ClientToScreen(h, ref p); sx = p.X; sy = p.Y; }

        // A reparented (WS_CHILD) game window doesn't get keyboard/mouse focus on its own. Share the input queues
        // between our UI thread and the game's thread, bring our window forward, then hand focus to the game.
        public static void FocusGame(IntPtr game, IntPtr parent)
        {
            if (!Alive(game)) return;
            uint gameThread = GetWindowThreadProcessId(game, out _);
            uint me = GetCurrentThreadId();
            bool attached = gameThread != me && AttachThreadInput(me, gameThread, true);
            try { BringWindowToTop(game); SetForegroundWindow(game); SetFocus(game); }
            finally { if (attached) AttachThreadInput(me, gameThread, false); }
        }

        // Client area size of our own window, in pixels (child coords are relative to this).
        public static void GetClient(IntPtr h, out int w, out int height)
        {
            if (GetClientRect(h, out var r)) { w = r.Right - r.Left; height = r.Bottom - r.Top; } else { w = 0; height = 0; }
        }

        // Pop a child back out to a normal top-level window (on close) so the program keeps running on its own.
        public static void Release(IntPtr child)
        {
            if (!Alive(child)) return;
            long style = GetStyle(child, GWL_STYLE);
            style &= ~WS_CHILD; style |= WS_POPUP | WS_CAPTION | WS_THICKFRAME | WS_SYSMENU | WS_MINIMIZEBOX | WS_MAXIMIZEBOX;
            SetStyle(child, GWL_STYLE, style);
            SetParent(child, IntPtr.Zero);
            SetWindowPos(child, IntPtr.Zero, 120, 120, 1280, 760, SWP_NOZORDER | SWP_FRAMECHANGED | SWP_SHOWWINDOW);
        }
    }
}
