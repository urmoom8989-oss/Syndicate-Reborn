using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Reflection;
using System.Threading.Tasks;
using System.Windows;
namespace SyndicateLauncher;
public partial class App : Application
{
    protected override void OnStartup(StartupEventArgs e)
    {
        if (e.Args.Length == 2 && e.Args[0] == "--check-runtime")
        {
            try { File.WriteAllText(e.Args[1], Services.GameLauncher.CheckRuntime()); Shutdown(0); }
            catch (Exception ex) { File.WriteAllText(e.Args[1], ex.Message); Shutdown(1); }
            return;
        }
        // Anything that would close the launcher without a word is written to launcher-crash.log next to the exe.
        AppDomain.CurrentDomain.UnhandledException += (_, args) => WriteCrash("unhandled", args.ExceptionObject as Exception);
        DispatcherUnhandledException += (_, args) => WriteCrash("ui thread", args.Exception);
        // Unobserved network errors (backend not up yet, a connection the HttpClient pool dropped) are expected and
        // harmless; logging each one buried real crashes in launcher-crash.log.
        TaskScheduler.UnobservedTaskException += (_, args) =>
        {
            args.SetObserved();
            if (args.Exception.Flatten().InnerExceptions.All(e => e is System.Net.Http.HttpRequestException or System.Net.Sockets.SocketException
                    or IOException or OperationCanceledException || e.InnerException is System.Net.Sockets.SocketException or IOException)) return;
            WriteCrash("background task", args.Exception);
        };
        StartupTrace.Mark("app OnStartup");
        StartupTrace.WatchOperations("main");
#if !DEV_BUILD
        // Anti-debugging and anti-RE guards. Inactive in dev builds so dev tools don't trigger them.
        Services.AntiDebug.RunChecks();
        Services.AntiDebug.StartMonitor(TimeSpan.FromSeconds(3));
#endif
        // The season catalog decodes every season's artwork when it is first used (about 0.4 s). Start that on a
        // worker now, so it runs while the splash is built instead of holding up the main window. Everything it
        // makes is frozen, so it can be used from this thread; if it fails, the first use reports it as before.
        Task.Run(() => System.Runtime.CompilerServices.RuntimeHelpers.RunClassConstructor(typeof(Models.SeasonCatalog).TypeHandle));
        // Move data saved under the old launcher names before anything reads it.
        Services.LegacyData.Migrate();
        StartupTrace.Mark("legacy migrated");
        var startupSettings = new Services.SettingsService().Load();
        // Silently try to add Defender exclusions — no UAC prompt, fire-and-forget.
        // If the launcher is not elevated this is a no-op; the user can use the button in Settings > Launcher & files.
        if (startupSettings.AutoAddExclusions)
            _ = Services.DefenderExclusions.TryAddSilentAsync(startupSettings.GameDirectory);
        var theme = startupSettings.Theme ?? new Models.ThemeSettings();
        StartupTrace.Mark("settings loaded");

        // The splash goes up first, on its own thread, and keeps animating while the main window is built below.
        // It covers the exact rectangle the main window opens in (LaunchSplash.cs).
        Rect area = SystemParameters.WorkArea;
        var splash = ShowSplash(theme, area);
        StartupTrace.Mark("splash shown");

        // Theme the launcher before the main window is created, so it opens in the saved theme.
        Services.ThemeService.Initialize(theme);
        StartupTrace.Mark("theme initialized");
        base.OnStartup(e);

        splash?.Report(0.45, "Loading your profiles...");
        MainWindow window;
        try
        {
            window = new MainWindow();
        }
        catch (Exception ex)
        {
            WriteCrash("main window", ex);
            splash?.Close();
            MessageBox.Show("Syndicate Launcher could not start.\n\n" + ex.Message, "Syndicate Launcher",
                MessageBoxButton.OK, MessageBoxImage.Error);
            Shutdown(1);
            return;
        }

        StartupTrace.Mark("main window built");
        splash?.Report(0.75, "Preparing seasons...");
        window.WindowStartupLocation = WindowStartupLocation.Manual;
        window.Left = area.Left + (area.Width - window.Width) / 2;
        window.Top = area.Top + (area.Height - window.Height) / 2;
        window.Splash = splash;
        MainWindow = window;
        window.Show();

        // Tell the player if a newer public release exists on GitHub (publish-public-release.ps1 stamps the tag
        // into Runtime\release-tag.txt; dev/untagged copies are skipped). Fire-and-forget, never blocks startup.
        // The standalone bootstrapper (single encrypted exe) manages its own updates and passes --skip-update,
        // so the inner launcher must not also prompt.
        if (System.Array.IndexOf(e.Args, "--skip-update") < 0)
            _ = SyndicateLauncher.Services.UpdateChecker.CheckAsync();

        // Runtime recovery: check for missing or AV-quarantined DLLs and
        // restore them from the embedded bundle silently in the background.
        // If anything was restored (or failed) a popup appears in the launcher.
        var recoverySettings = startupSettings; // captured for the closure
        _ = Task.Run(async () =>
        {
            try
            {
                var runtimeRoot = Services.RuntimeBundle.Resolve();
                var result = await Services.RuntimeRecovery.CheckAndRestoreAsync(
                    runtimeRoot, recoverySettings.GameDirectory);

                if (result.Status == Services.RuntimeRecovery.RecoveryStatus.Clean)
                    return; // nothing to report

                // Marshal back to the UI thread for the MessageBox.
                await System.Windows.Application.Current.Dispatcher.InvokeAsync(() =>
                {
                    if (result.Status == Services.RuntimeRecovery.RecoveryStatus.Restored)
                    {
                        var names = string.Join("\n  \x2022 ", result.Restored);
                        MessageBox.Show(
                            "Syndicate detected that one or more game files were removed (likely by antivirus) " +
                            "and has restored them automatically.\n\n" +
                            "Restored files:\n  \x2022 " + names + "\n\n" +
                            "These folders have also been re-added to your antivirus exclusion list " +
                            "so this should not happen again.",
                            "Syndicate \x2014 Files Restored",
                            MessageBoxButton.OK,
                            MessageBoxImage.Information);
                    }
                    else
                    {
                        var failed = string.Join("\n  \x2022 ", result.Failed);
                        MessageBox.Show(
                            "Syndicate detected missing game files but could not fully restore them.\n\n" +
                            "Missing files:\n  \x2022 " + failed + "\n\n" +
                            (result.ErrorMessage is not null ? "Detail: " + result.ErrorMessage + "\n\n" : "") +
                            "If this keeps happening, re-download the launcher. " +
                            "You can also try the \x22Add to Antivirus Exclusions\x22 button in " +
                            "Settings \x2192 Launcher & files.",
                            "Syndicate \x2014 File Recovery Failed",
                            MessageBoxButton.OK,
                            MessageBoxImage.Warning);
                    }
                });
            }
            catch { /* recovery is best-effort; never crash the launcher */ }
        });
    }
    internal static void WriteCrash(string where, Exception? ex)
    {
        try
        {
            string path = Path.Combine(AppContext.BaseDirectory, "launcher-crash.log");
            File.AppendAllText(path, DateTime.Now.ToString("yyyy-MM-dd HH:mm:ss") + "  [" + where + "]  " + ex + Environment.NewLine + Environment.NewLine);
        }
        catch (Exception) { /* nothing left to report to */ }
    }

    private static LaunchSplash? ShowSplash(Models.ThemeSettings theme, Rect area)
    {
        try
        {
            var palette = Models.ThemeCatalog.Current(theme);
            // Same size as MainWindow.xaml (smaller only if the screen is), centred like the main window.
            double width = Math.Min(1248, area.Width), height = Math.Min(728, area.Height);
            var bounds = new Rect(area.Left + (area.Width - width) / 2, area.Top + (area.Height - height) / 2, width, height);
            var version = Assembly.GetExecutingAssembly().GetName().Version;
            string label = version is null ? "" : $"v{version.Major}.{version.Minor}.{version.Build}";
            var splash = LaunchSplash.TryShow(new SplashLook(palette.Accent, palette.Accent2, palette.Background,
                palette.Surface, palette.Text, !theme.ReduceMotion, bounds, label));
            splash?.Report(0.2, "Initializing launcher...");
            return splash;
        }
        catch (Exception)
        {
            return null; // no splash; the launcher still opens
        }
    }

    protected override void OnExit(ExitEventArgs e)
    {
        StartupTrace.Flush();
        Services.GameLauncher.StopOwned();
        base.OnExit(e);
    }
}
