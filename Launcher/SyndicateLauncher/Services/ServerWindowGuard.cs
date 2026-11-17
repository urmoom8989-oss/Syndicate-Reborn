using System;
using System.Threading;
using System.Threading.Tasks;
using System.Diagnostics;
using System.Runtime.InteropServices;
namespace SyndicateLauncher.Services;
internal static class ServerWindowGuard
{
    private delegate bool WindowVisitor(IntPtr window, IntPtr state);
    [DllImport("user32.dll")] private static extern bool EnumWindows(WindowVisitor visitor, IntPtr state);
    [DllImport("user32.dll")] private static extern uint GetWindowThreadProcessId(IntPtr window, out uint processId);
    [DllImport("user32.dll")] private static extern bool IsWindowVisible(IntPtr window);
    [DllImport("user32.dll")] private static extern bool ShowWindowAsync(IntPtr window, int show);
    public static void HideOwnedWindows(Process ownedServer)
    {
        // Never enumerate by title or executable name: the visible player client
        // uses the same executable. Only this launcher's owned server PID qualifies.
        var id = (uint)ownedServer.Id;
        EnumWindows((window, _) => {
            GetWindowThreadProcessId(window, out var owner);
            if (owner == id && IsWindowVisible(window)) ShowWindowAsync(window, 0);
            return true;
        }, IntPtr.Zero);
    }
    public static async Task KeepHiddenAsync(Process ownedServer, CancellationToken stop)
    {
        try {
            while (!stop.IsCancellationRequested && !ownedServer.HasExited) {
                HideOwnedWindows(ownedServer);
                await Task.Delay(100, stop).ConfigureAwait(false);
            }
        } catch (OperationCanceledException) when (stop.IsCancellationRequested) {
        } catch (ObjectDisposedException) {
            // The launcher disposed its old server while starting the next match.
        } catch (InvalidOperationException) {
            // The owned process has ended before its next visibility check.
        }
    }
}
