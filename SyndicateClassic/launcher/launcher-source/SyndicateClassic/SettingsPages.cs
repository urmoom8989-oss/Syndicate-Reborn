// Syndicate © 2026 Syndicate. All rights reserved. Proprietary — no copying, distribution, or derivative works. See LICENSE.
using System;
using System.Diagnostics;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Media;

namespace SyndicateClassic;

// Settings pages added with the sidebar redesign: Controllers and Discord.
public partial class MainWindow
{
    private static void OpenLink(string url)
    {
        try { Process.Start(new ProcessStartInfo(url) { UseShellExecute = true }); } catch { }
    }

    private CheckBox SettingsToggle(string title, string detail, bool value, Action<bool> changed)
    {
        var words = new StackPanel();
        words.Children.Add(NightText(title, 13.5, Night.Text, bold: true));
        if (detail.Length > 0) words.Children.Add(NightText(detail, 12, Night.Sub).Also(t => { t.TextWrapping = TextWrapping.Wrap; t.Margin = new Thickness(0, 3, 0, 0); }));
        var box = new CheckBox { Content = words, IsChecked = value, Margin = new Thickness(0, 0, 0, 16), Foreground = Hex(Night.Text), Template = SwitchTemplate.Value, Cursor = System.Windows.Input.Cursors.Hand };
        box.Checked += (_, __) => changed(true); box.Unchecked += (_, __) => changed(false);
        return box;
    }

    // A sliding on/off switch on the right of the row instead of a tick box: grey when off, gold
    // with the knob on the right when on. The whole row is clickable, and Space toggles it.
    private static readonly Lazy<ControlTemplate> SwitchTemplate = new(() => (ControlTemplate)System.Windows.Markup.XamlReader.Parse($$"""
        <ControlTemplate TargetType="CheckBox" xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation" xmlns:x="http://schemas.microsoft.com/winfx/2006/xaml">
          <Border Background="Transparent">
            <Grid>
              <Grid.ColumnDefinitions><ColumnDefinition Width="*"/><ColumnDefinition Width="Auto"/></Grid.ColumnDefinitions>
              <ContentPresenter Margin="0,0,24,0" VerticalAlignment="Center"/>
              <Border x:Name="Track" Grid.Column="1" Width="46" Height="26" CornerRadius="13" VerticalAlignment="Center"
                      Background="{{Night.Hover}}" BorderBrush="{{Night.Line}}" BorderThickness="1">
                <Ellipse x:Name="Knob" Width="18" Height="18" Margin="3,0,0,0" HorizontalAlignment="Left" VerticalAlignment="Center" Fill="{{Night.Sub}}">
                  <Ellipse.RenderTransform><TranslateTransform x:Name="Slide" X="0"/></Ellipse.RenderTransform>
                  <Ellipse.Effect><DropShadowEffect BlurRadius="6" ShadowDepth="1" Opacity="0.45" Color="#000000"/></Ellipse.Effect>
                </Ellipse>
              </Border>
            </Grid>
          </Border>
          <ControlTemplate.Triggers>
            <Trigger Property="IsMouseOver" Value="True">
              <Setter TargetName="Track" Property="BorderBrush" Value="{{Night.Muted}}"/>
              <Setter TargetName="Knob" Property="Fill" Value="{{Night.Text}}"/>
            </Trigger>
            <Trigger Property="IsKeyboardFocused" Value="True">
              <Setter TargetName="Track" Property="BorderBrush" Value="{{Night.Sky}}"/>
            </Trigger>
            <Trigger Property="IsChecked" Value="True">
              <Setter TargetName="Track" Property="Background" Value="{{Night.Gold}}"/>
              <Setter TargetName="Track" Property="BorderBrush" Value="{{Night.GoldDeep}}"/>
              <Setter TargetName="Knob" Property="Fill" Value="{{Night.OnGold}}"/>
              <Trigger.EnterActions>
                <BeginStoryboard><Storyboard>
                  <DoubleAnimation Storyboard.TargetName="Slide" Storyboard.TargetProperty="X" To="20" Duration="0:0:0.16">
                    <DoubleAnimation.EasingFunction><CubicEase EasingMode="EaseOut"/></DoubleAnimation.EasingFunction>
                  </DoubleAnimation>
                </Storyboard></BeginStoryboard>
              </Trigger.EnterActions>
              <Trigger.ExitActions>
                <BeginStoryboard><Storyboard>
                  <DoubleAnimation Storyboard.TargetName="Slide" Storyboard.TargetProperty="X" To="0" Duration="0:0:0.16">
                    <DoubleAnimation.EasingFunction><CubicEase EasingMode="EaseOut"/></DoubleAnimation.EasingFunction>
                  </DoubleAnimation>
                </Storyboard></BeginStoryboard>
              </Trigger.ExitActions>
            </Trigger>
            <Trigger Property="IsEnabled" Value="False">
              <Setter Property="Opacity" Value="0.45"/>
            </Trigger>
          </ControlTemplate.Triggers>
        </ControlTemplate>
        """));

    // Controllers: live status (updates the moment a pad is plugged in or paired), the PlayStation
    // switch, and the one-time ViGEmBus driver step when it is missing.
    private void AddControllerSettings(Panel group)
    {
        var badgeText = NightText("None", 11, Night.Sub, bold: true).Also(t => t.TextWrapping = TextWrapping.NoWrap);
        var badge = new Border { Child = badgeText, CornerRadius = new CornerRadius(9), Padding = new Thickness(9, 3, 9, 3), Background = Hex(Night.Hover), HorizontalAlignment = HorizontalAlignment.Left, Margin = new Thickness(0, 0, 0, 8) };
        var status = NightText("No controller connected", 15, Night.Text, bold: true).Also(t => { t.TextWrapping = TextWrapping.Wrap; t.Margin = new Thickness(0, 0, 0, 4); });
        var mirroring = NightText("", 12, Night.Sub).Also(t => { t.TextWrapping = TextWrapping.Wrap; t.Margin = new Thickness(0, 0, 0, 16); });
        group.Children.Add(badge); group.Children.Add(status); group.Children.Add(mirroring);


        var driverPanel = new Border { Background = Hex("#26FFD23F"), BorderBrush = Hex("#40FFD23F"), BorderThickness = new Thickness(1), CornerRadius = TiltCorners(12), Padding = new Thickness(16, 12, 16, 12), Margin = new Thickness(0, 0, 0, 14), Visibility = Visibility.Collapsed };
        var driverWords = new StackPanel();
        driverWords.Children.Add(NightText("One-time step for PlayStation controllers", 13.5, Night.Gold, bold: true));
        driverWords.Children.Add(NightText("Install the free ViGEmBus driver (it creates the virtual Xbox controller), then press Check again. No restart of Syndicate needed.", 12, Night.Text).Also(t => { t.TextWrapping = TextWrapping.Wrap; t.Margin = new Thickness(0, 4, 0, 10); }));
        var driverButtons = new WrapPanel();
        var download = new Button { Content = "Download ViGEmBus", Height = 34, Padding = new Thickness(14, 0, 14, 0), Margin = new Thickness(0, 0, 8, 0) };
        NightButton(download, "gold");
        download.Click += (_, __) => OpenLink(ControllerService.DriverUrl);
        var retry = new Button { Content = "Check again", Height = 34, Padding = new Thickness(14, 0, 14, 0) };
        NightButton(retry, "base");
        retry.Click += (_, __) => { try { _controllers?.RetryDriver(); } catch { } Refresh(); };
        driverButtons.Children.Add(download); driverButtons.Children.Add(retry);
        driverWords.Children.Add(driverButtons);
        driverPanel.Child = driverWords;
        group.Children.Add(SettingsToggle("PS4 / PS5 controller support",
            "DualShock 4, DualSense and DualSense Edge, over USB or Bluetooth. While Fortnite runs, the pad appears to the game as an Xbox controller (old Fortnite builds only understand Xbox pads). Xbox controllers need nothing.",
            LauncherPrefs.PlayStationControllers, on => { LauncherPrefs.PlayStationControllers = on; try { _controllers?.SetEnabled(on); } catch { } Refresh(); }));
        group.Children.Add(driverPanel);

        group.Children.Add(NightText("Tips: plug the controller in (or pair it) before or after starting Syndicate, both work. If a pad stops responding in game, unplug it and plug it back in: the virtual pad is replaced at once. Close Steam's controller support or DS4Windows if your inputs double up.", 12, Night.Muted).Also(t => t.TextWrapping = TextWrapping.Wrap));

        void Refresh()
        {
            var c = _controllers;
            if (c == null)
            {
                status.Text = "Controller support could not start";
                mirroring.Text = "Xbox controllers still work in game. Run UPDATE FROM GIT (or reinstall Syndicate) to restore PlayStation support.";
                badgeText.Text = "Unavailable"; badge.Background = Hex("#40FF5A6A"); driverPanel.Visibility = Visibility.Collapsed;
                return;
            }
            int pads = c.XboxControllers + c.PlayStationControllers.Count;
            status.Text = c.Summary();
            badgeText.Text = pads == 0 ? "None" : pads == 1 ? "1 connected" : pads + " connected";
            badge.Background = Hex(pads > 0 ? "#333BE48B" : Night.Hover);
            badgeText.Foreground = Hex(pads > 0 ? Night.Good : Night.Sub);
            bool ps = c.PlayStationControllers.Count > 0;
            driverPanel.Visibility = ps && c.DriverMissing && LauncherPrefs.PlayStationControllers ? Visibility.Visible : Visibility.Collapsed;
            mirroring.Text = !ps ? (c.XboxControllers > 0 ? "Ready: Fortnite reads Xbox controllers directly." : "Plug in or pair an Xbox, PS4 or PS5 controller.")
                : !LauncherPrefs.PlayStationControllers ? "PlayStation support is off: Fortnite will not see this controller."
                : c.DriverMissing ? "Waiting for the ViGEmBus driver (see below)."
                : c.Mirroring ? "Active: Fortnite sees your PlayStation controller as an Xbox controller."
                : "Ready: it becomes an Xbox controller as soon as Fortnite starts from Syndicate.";
        }
        ControllersChanged += Refresh;
        group.IsVisibleChanged += (_, __) => { if (group.IsVisible) Refresh(); };
        Refresh();
    }

    // Discord: Rich Presence switch with an example of what friends see, and the community invite.
    private void AddDiscordSettings(Panel group)
    {
        group.Children.Add(SettingsToggle("Show what I'm playing in my Discord status",
            "Friends see your Syndicate version, the season you are playing, and whether you are in the lobby or in a match with how many players are left. A \"Join Discord\" button on your status invites them to the Syndicate server. Needs the Discord app running on this PC.",
            LauncherPrefs.DiscordStatus, on => { LauncherPrefs.DiscordStatus = on; _presence?.SetEnabled(on); }));

        var example = new StackPanel();
        example.Children.Add(NightText("EXAMPLE", 10.5, Night.Gold, bold: true));
        example.Children.Add(NightText("Project Tilted", 14, Night.Text, bold: true).Also(t => t.Margin = new Thickness(0, 6, 0, 2)));
        example.Children.Add(NightText("Season 4 (4.5) · Syndicate v" + LauncherVersion(), 12.5, Night.Text));
        example.Children.Add(NightText("In a match · 37 players left", 12.5, Night.Sub));
        example.Children.Add(new Border { Background = Hex("#33FFFFFF"), CornerRadius = new CornerRadius(6), Padding = new Thickness(10, 4, 10, 4), Margin = new Thickness(0, 10, 0, 0), HorizontalAlignment = HorizontalAlignment.Left, Child = NightText("Join Discord", 12, Night.Text, bold: true) });
        group.Children.Add(new Border { Child = example, Background = Hex("#1E2124"), CornerRadius = TiltCorners(12), Padding = new Thickness(16, 14, 16, 14), Margin = new Thickness(0, 0, 0, 16), HorizontalAlignment = HorizontalAlignment.Left, MinWidth = 320 });

        var join = new Button { Content = "Join the Syndicate Discord", Height = 38, Padding = new Thickness(16, 0, 16, 0), HorizontalAlignment = HorizontalAlignment.Left };
        NightButton(join, "gold");
        join.Click += (_, __) => OpenLink(DiscordPresence.DiscordInvite);
        group.Children.Add(join);
    }
}