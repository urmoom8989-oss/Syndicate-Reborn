// Syndicate © 2026 Syndicate. All rights reserved. Proprietary — no copying, distribution, or derivative works. See LICENSE.
using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Windows;
using System.Windows.Controls;

namespace SyndicateClassic;

// Custom controller binds, every season (Settings > Modded > Controller button layout).
// Fortnite got its own controller binding screen only in Season 6; Pre-season to Season 5 have fixed layouts
// (Combat Pro, Builder Pro...). TiltedPadAssist.dll, already in the game for sprint by default, moves your
// buttons before Fortnite reads them: pick which game button each of your buttons presses. Xbox and PS pads.
// File: %LOCALAPPDATA%\ProjectTilted\controller-binds.txt, "<your button>=<game button>" lines (only the moved ones)
// and "SWAPSTICKS=1". The game picks up a change within half a second, mid-match too.
public partial class MainWindow
{
    internal static readonly string[] PadInputs = { "A", "B", "X", "Y", "LB", "RB", "LT", "RT", "LS", "RS", "UP", "DOWN", "LEFT", "RIGHT", "BACK", "START" };
    private static readonly Dictionary<string, string> PadInputNames = new()
    {
        ["A"] = "A (Cross)", ["B"] = "B (Circle)", ["X"] = "X (Square)", ["Y"] = "Y (Triangle)",
        ["LB"] = "LB (L1)", ["RB"] = "RB (R1)", ["LT"] = "LT (L2)", ["RT"] = "RT (R2)",
        ["LS"] = "Left stick click (L3)", ["RS"] = "Right stick click (R3)",
        ["UP"] = "D-pad up", ["DOWN"] = "D-pad down", ["LEFT"] = "D-pad left", ["RIGHT"] = "D-pad right",
        ["BACK"] = "View / Share", ["START"] = "Menu / Options", ["NONE"] = "Nothing (off)"
    };
    private static string ControllerBindsFile => HudFlag("controller-binds.txt");

    internal static (Dictionary<string, string> Binds, bool SwapSticks) ReadControllerBinds()
    {
        var binds = PadInputs.ToDictionary(b => b, b => b);
        bool swap = false;
        try
        {
            if (File.Exists(ControllerBindsFile))
                foreach (var line in File.ReadAllLines(ControllerBindsFile))
                {
                    var parts = line.Trim().ToUpperInvariant().Split('=');
                    if (parts.Length != 2) continue;
                    if (parts[0] == "SWAPSTICKS") swap = parts[1] == "1";
                    else if (binds.ContainsKey(parts[0]) && (parts[1] == "NONE" || binds.ContainsKey(parts[1]))) binds[parts[0]] = parts[1];
                }
        }
        catch { }
        return (binds, swap);
    }

    internal static void SaveControllerBinds(Dictionary<string, string> binds, bool swap)
    {
        try
        {
            var lines = PadInputs.Where(b => binds.TryGetValue(b, out var to) && to != b).Select(b => b + "=" + binds[b]).ToList();
            if (swap) lines.Add("SWAPSTICKS=1");
            if (lines.Count == 0) { if (File.Exists(ControllerBindsFile)) File.Delete(ControllerBindsFile); return; }
            Directory.CreateDirectory(Path.GetDirectoryName(ControllerBindsFile)!);
            File.WriteAllLines(ControllerBindsFile + ".tmp", lines);
            File.Move(ControllerBindsFile + ".tmp", ControllerBindsFile, true);
        }
        catch { }
    }

    private void AddControllerBinds(StackPanel panel)
    {
        var (binds, swap) = ReadControllerBinds();
        var combos = new Dictionary<string, ComboBox>();
        var swapBox = new CheckBox { Content = "Swap the sticks (move with the right stick, aim with the left)", IsChecked = swap, Foreground = Hex(Night.Sub), Margin = new Thickness(0, 4, 0, 14) };
        void Save() => SaveControllerBinds(combos.ToDictionary(c => c.Key, c => (c.Value.SelectedItem as ComboBoxItem)?.Tag as string ?? c.Key), swapBox.IsChecked == true);
        panel.Children.Add(OptionNote("Pick which game button each of your buttons presses. Fortnite's own layout setting still decides what the game button does (for example B = edit in Combat Pro). Changes apply right away, also mid-match."));
        foreach (var input in PadInputs)
        {
            var combo = new ComboBox { MinWidth = 210, Height = 32, Margin = new Thickness(12, 0, 0, 0) };
            foreach (var to in PadInputs.Append("NONE")) combo.Items.Add(new ComboBoxItem { Content = PadInputNames[to], Tag = to });
            combo.SelectedIndex = binds[input] == "NONE" ? PadInputs.Length : Array.IndexOf(PadInputs, binds[input]);
            combo.SelectionChanged += (_, __) => Save();
            combos[input] = combo;
            var label = NightText(PadInputNames[input], 12.5, Night.Sub).Also(t => t.VerticalAlignment = VerticalAlignment.Center);
            var row = new DockPanel { Margin = new Thickness(0, 0, 0, 6) };
            DockPanel.SetDock(combo, Dock.Right);
            row.Children.Add(combo); row.Children.Add(label);
            panel.Children.Add(row);
        }
        swapBox.Checked += (_, __) => Save(); swapBox.Unchecked += (_, __) => Save();
        panel.Children.Add(swapBox);
        var reset = new Button { Content = "Reset to the normal layout", Height = 32, Padding = new Thickness(14, 0, 14, 0), HorizontalAlignment = HorizontalAlignment.Left };
        NightButton(reset, "base");
        reset.Click += (_, __) =>
        {
            foreach (var (input, combo) in combos) combo.SelectedIndex = Array.IndexOf(PadInputs, input);
            swapBox.IsChecked = false;
            SaveControllerBinds(PadInputs.ToDictionary(b => b, b => b), false);
        };
        panel.Children.Add(reset);
    }
}
