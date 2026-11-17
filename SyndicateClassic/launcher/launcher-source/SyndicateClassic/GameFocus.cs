// Syndicate © 2026 Syndicate. All rights reserved. Proprietary — no copying, distribution, or derivative works. See LICENSE.
using System;
using System.Diagnostics;
using System.Runtime.InteropServices;
using System.Threading.Tasks;
using System.Windows;

namespace SyndicateClassic;

// When Fortnite opens, the launcher steps aside: once the game's window shows, it is brought to the front and the
// launcher minimizes, so a controller player never has to reach for the mouse. When the game closes, the launcher
// comes back. Not while Syndicate Studio is open (it docks the game in its own window) or for a second client.
public partial class MainWindow
{
    [DllImport("user32.dll")] private static extern bool AllowSetForegroundWindow(int processId);
    [DllImport("user32.dll", EntryPoint = "SetForegroundWindow")] private static extern bool FocusGameWindow(IntPtr hWnd);
    [DllImport("user32.dll", EntryPoint = "ShowWindow")] private static extern bool ShowGameWindow(IntPtr hWnd, int cmd);
    [DllImport("user32.dll", EntryPoint = "IsIconic")] private static extern bool GameWindowMinimized(IntPtr hWnd);
    [DllImport("user32.dll", EntryPoint = "GetForegroundWindow")] private static extern IntPtr ForegroundWindowNow();
    [DllImport("user32.dll", EntryPoint = "GetWindowThreadProcessId")] private static extern uint WindowProcess(IntPtr hWnd, out uint processId);

    private bool _minimizedForGame;
    private WindowState _stateBeforeGame = WindowState.Normal;

    private async Task HandFocusToGameAsync(Process game)
    {
        try
        {
            try { AllowSetForegroundWindow(game.Id); } catch { }
            IntPtr window = IntPtr.Zero;
            // The window shows 5-40 seconds after the process starts (slower on a first launch).
            for (int i = 0; i < 180 && window == IntPtr.Zero; i++)
            {
                await Task.Delay(500);
                if (game.HasExited || _game != game) return;
                int pid = game.Id;
                window = await Task.Run(() => NativeEmbed.FindRenderingWindow(pid));
            }
            if (window == IntPtr.Zero || game.HasExited || _game != game) return;
#if TILTED_DEVKIT
            if (_studio is { IsVisible: true }) return;   // Dev Kit only: Studio docks the game in its own window
#endif
            // Only when the launcher or the game is in front: if the player moved on to another app while it
            // loaded, their windows are left alone.
            bool gameInFront = false;
            try { var front = ForegroundWindowNow(); WindowProcess(front, out uint frontPid); gameInFront = front != IntPtr.Zero && frontPid == (uint)game.Id; } catch { }
            if (!IsActive && !gameInFront) return;
            if (GameWindowMinimized(window)) ShowGameWindow(window, 9 /* SW_RESTORE */);
            FocusGameWindow(window);
            _stateBeforeGame = WindowState;
            WindowState = WindowState.Minimized;
            _minimizedForGame = true;
            Log("Launcher minimized so Fortnite has the focus (it comes back when the game closes).");
        }
        catch (Exception ex) { Log("Could not hand focus to Fortnite: " + ex.Message); }
    }

    private void RestoreAfterGame()
    {
        if (!_minimizedForGame) return;
        _minimizedForGame = false;
        try
        {
            if (WindowState == WindowState.Minimized) WindowState = _stateBeforeGame == WindowState.Maximized ? WindowState.Maximized : WindowState.Normal;
            Activate();
        }
        catch { }
    }
}
