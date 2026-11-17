// Syndicate © 2026 Syndicate. All rights reserved. Proprietary — see LICENSE.
using System;
using Avalonia;

namespace SyndicateLauncher.Linux;

internal static class Program
{
    // Avalonia entry point. STAThread is required on Windows and harmless on Linux.
    [STAThread]
    public static void Main(string[] args) =>
        BuildAvaloniaApp().StartWithClassicDesktopLifetime(args);

    public static AppBuilder BuildAvaloniaApp() =>
        AppBuilder.Configure<App>()
            .UsePlatformDetect()   // X11/Wayland on Linux, Win32 on Windows — picked at runtime
            .WithInterFont()
            .LogToTrace();
}
