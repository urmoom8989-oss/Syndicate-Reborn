using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using System.Windows;
using System.Windows.Interop;
using System.Windows.Media.Animation;
using SyndicateLauncher.Services;

namespace SyndicateLauncher;

// Keeps the launcher cheap to run:
// * The window is a normal borderless window (WindowChrome), not a layered AllowsTransparency one, so WPF
//   no longer copies every frame back from the GPU. Windows 11 rounds its corners and Windows draws the shadow.
// * The always-on animations (hero zoom, logo glow) run at a low frame rate and pause while the launcher
//   is in the background or Fortnite is running, so a launcher behind the game costs next to nothing.
public partial class MainWindow
{
    private readonly List<AnimationClock> _ambientClocks = new();
    private System.Windows.Threading.DispatcherTimer? _ambientWatch;

    [DllImport("dwmapi.dll")]
    private static extern int DwmSetWindowAttribute(IntPtr hwnd, int attribute, ref int value, int size);
    private const int DwmwaWindowCornerPreference = 33, DwmwcpRound = 2;

    [DllImport("shell32.dll", CharSet = CharSet.Unicode, EntryPoint = "ExtractIconExW")]
    private static extern uint ExtractIconEx(string file, int index, out IntPtr large, out IntPtr small, uint count);

    [DllImport("user32.dll", EntryPoint = "SetClassLongPtrW")]
    private static extern IntPtr SetClassLongPtr(IntPtr hwnd, int index, IntPtr value);

    private const int GclpHicon = -14, GclpHiconSm = -34;

    protected override void OnSourceInitialized(EventArgs e)
    {
        var hwnd = new WindowInteropHelper(this).Handle;
        ApplyTaskbarIcon(hwnd);
        base.OnSourceInitialized(e);
        try
        {
            int round = DwmwcpRound;
            DwmSetWindowAttribute(hwnd, DwmwaWindowCornerPreference, ref round, sizeof(int)); // no-op before Windows 11
        }
        catch (DllNotFoundException) { }
        catch (EntryPointNotFoundException) { }

        Activated += (_, _) => SyncAmbientAnimations();
        Deactivated += (_, _) => SyncAmbientAnimations();
        StateChanged += (_, _) => SyncAmbientAnimations();
        // GameLauncher has no "game started/stopped" event; check twice a second (cheap: one bool).
        _ambientWatch = new System.Windows.Threading.DispatcherTimer(TimeSpan.FromMilliseconds(500),
            System.Windows.Threading.DispatcherPriority.Background, (_, _) => SyncAmbientAnimations(), Dispatcher);
        _ambientWatch.Start();
        // Play pressed: pause the hero zoom and logo glow at once, not at the next half-second check.
        Action onStarting = () => SyncAmbientAnimations();
        GameLauncher.StartingChanged += onStarting;
        Closed += (_, _) =>
        {
            _ambientWatch?.Stop();
            GameLauncher.StartingChanged -= onStarting;
        };
    }

    // Windows 11 creates the taskbar entry before a slow-starting WPF app gets its main window, so the entry can keep
    // the generic placeholder even though MainWindow.Icon is set. The taskbar reads the window class icon when the
    // window appears; set that from the exe's embedded icon (ApplicationIcon) before the taskbar button is created.
    private void ApplyTaskbarIcon(IntPtr hwnd)
    {
        try
        {
            // The exe is still on disk after launch, and ExtractIconEx gives us the large/small HICONs Windows needs.
            // Keep them alive for the process lifetime: the window class holds these handles until the app exits.
            var exe = Environment.ProcessPath;
            if (hwnd == IntPtr.Zero || string.IsNullOrEmpty(exe)) return;
            if (ExtractIconEx(exe, 0, out var large, out var small, 1) == 0) return;
            if (large != IntPtr.Zero) SetClassLongPtr(hwnd, GclpHicon, large);
            if (small != IntPtr.Zero) SetClassLongPtr(hwnd, GclpHiconSm, small);
        }
        catch (DllNotFoundException) { }
        catch (EntryPointNotFoundException) { }
    }

    private bool _ambientPaused;

    private void SyncAmbientAnimations()
    {
        bool pause = !IsActive || WindowState == WindowState.Minimized || GameLauncher.Starting || GameLauncher.IsRunning;
        if (pause == _ambientPaused) return;
        _ambientPaused = pause;
        foreach (var clock in _ambientClocks)
        {
            if (clock.Controller is null) continue;
            if (pause) clock.Controller.Pause();
            else clock.Controller.Resume();
        }
    }
}
