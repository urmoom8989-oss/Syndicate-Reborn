// Syndicate © 2026 Syndicate. All rights reserved. Proprietary — see LICENSE.
// Code-only Application (no XAML) to match the hand-built style of the Windows launcher.
using Avalonia;
using Avalonia.Controls.ApplicationLifetimes;
using Avalonia.Styling;
using Avalonia.Themes.Fluent;

namespace SyndicateLauncher.Linux;

public class App : Application
{
    public override void Initialize()
    {
        // Syndicate is a dark UI; force the dark variant so it doesn't follow the desktop theme.
        RequestedThemeVariant = ThemeVariant.Dark;
        Styles.Add(new FluentTheme());
    }

    public override void OnFrameworkInitializationCompleted()
    {
        if (ApplicationLifetime is IClassicDesktopStyleApplicationLifetime desktop)
            desktop.MainWindow = new MainWindow();

        base.OnFrameworkInitializationCompleted();
    }
}
