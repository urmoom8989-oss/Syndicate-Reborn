// Syndicate © 2026 Syndicate. All rights reserved. Proprietary — see LICENSE.
// The launcher window. For now it hosts the ported home screen (HomeView); as more
// screens are ported, this gains the page switching the WPF MainWindow does.
using Avalonia.Controls;
using Avalonia.Media;

namespace SyndicateLauncher.Linux;

public class MainWindow : Window
{
    public MainWindow()
    {
        Title = "Project Tilted";
        Width = 1280;
        Height = 760;
        MinWidth = 1040;
        MinHeight = 620;
        Background = Ui.Hex(Night.Bg);
        WindowStartupLocation = WindowStartupLocation.CenterScreen;

        Content = new Shell();
    }
}
