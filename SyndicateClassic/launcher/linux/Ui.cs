// Syndicate © 2026 Syndicate. All rights reserved. Proprietary — see LICENSE.
// The Syndicate "Night" look, ported from the WPF launcher's NightDrop.cs / ModernShell.cs
// to Avalonia. Same shapes: leaning titles, two-rounded/two-sharp corners, glass tiles,
// gold accents. Avalonia differences handled here so the screens read like the originals.
//
// Fonts: the Windows build uses Bahnschrift Condensed + Segoe Fluent Icons, which don't
// exist on Linux. Body/display fall back to Inter (bundled); the forward lean is what
// carries the Syndicate feel. Icons use Unicode glyphs via Icon(); a proper icon font is a
// later polish item. Clickable things are Borders with pointer handlers (not themed
// Buttons) so the look is fully controlled and doesn't fight the Fluent theme — good
// enough for Deck Desktop Mode, where stick/trackpad act as a mouse. Keyboard/controller
// focus nav comes when we wire real behaviour.
using System;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Input;
using Avalonia.Layout;
using Avalonia.Media;

namespace SyndicateLauncher.Linux;

public static class Ui
{
    public static readonly FontFamily FontBody = new("Inter");
    public static readonly FontFamily FontDisplay = new("Inter");

    // --- small extensions / helpers --------------------------------------------------
    public static T Also<T>(this T self, Action<T> act) { act(self); return self; }

    public static SolidColorBrush Hex(string hex) => new(Color.Parse(hex));

    // WPF's LinearGradientBrush(c1, c2, angleDeg) — centred gradient line at the angle.
    public static LinearGradientBrush Linear(string c1, string c2, double angleDeg)
    {
        double r = angleDeg * Math.PI / 180.0;
        double dx = Math.Cos(r) / 2, dy = Math.Sin(r) / 2;
        return new LinearGradientBrush
        {
            StartPoint = new RelativePoint(0.5 - dx, 0.5 - dy, RelativeUnit.Relative),
            EndPoint = new RelativePoint(0.5 + dx, 0.5 + dy, RelativeUnit.Relative),
            GradientStops =
            {
                new GradientStop(Color.Parse(c1), 0),
                new GradientStop(Color.Parse(c2), 1)
            }
        };
    }

    public static LinearGradientBrush GlassFill() => Linear("#EB10131F", "#E0090B14", 45);

    public static LinearGradientBrush GlassEdgeFill() => new()
    {
        StartPoint = new RelativePoint(0, 0, RelativeUnit.Relative),
        EndPoint = new RelativePoint(1, 1, RelativeUnit.Relative),
        GradientStops =
        {
            new GradientStop(Color.Parse("#99FFD23F"), 0),
            new GradientStop(Color.Parse("#33FFFFFF"), 0.22),
            new GradientStop(Color.Parse("#1AFFFFFF"), 1)
        }
    };

    public static LinearGradientBrush GoldFill() => Linear("#FFE46B", "#FFC21A", 90);

    public static LinearGradientBrush BrandFill() => new()
    {
        StartPoint = new RelativePoint(0, 0, RelativeUnit.Relative),
        EndPoint = new RelativePoint(1, 0, RelativeUnit.Relative),
        GradientStops =
        {
            new GradientStop(Color.Parse("#FFD23F"), 0),
            new GradientStop(Color.Parse("#FF8A3D"), 0.55),
            new GradientStop(Color.Parse("#8B6CFF"), 1)
        }
    };

    // Two rounded corners (top-left, bottom-right) and two nearly sharp ones.
    public static CornerRadius TiltCorners(double radius) =>
        radius >= 16 && Math.Abs(radius - 18) > 0.01
            ? new CornerRadius(radius)
            : new CornerRadius(radius, Math.Min(3, radius), radius, Math.Min(3, radius));

    public static SkewTransform Lean(double angleX) => new() { AngleX = angleX, AngleY = 0 };

    // --- text ------------------------------------------------------------------------
    public static TextBlock Display(string text, double size, string color = "#FFFFFF") => new()
    {
        Text = text,
        FontSize = size,
        Foreground = Hex(color),
        FontFamily = FontDisplay,
        FontWeight = FontWeight.SemiBold,
        FontStretch = FontStretch.Condensed,
        TextWrapping = TextWrapping.NoWrap,
        RenderTransform = Lean(-8),
        RenderTransformOrigin = RelativePoint.Center
    };

    public static TextBlock Body(string text, double size, string color) => new()
    {
        Text = text,
        FontSize = size,
        Foreground = Hex(color),
        FontFamily = FontBody,
        TextWrapping = TextWrapping.Wrap
    };

    public static TextBlock NightText(string text, double size = 13, string color = Night.Text, bool bold = false) => new()
    {
        Text = text,
        FontSize = size,
        Foreground = Hex(color),
        FontFamily = FontBody,
        TextWrapping = TextWrapping.Wrap,
        FontWeight = bold ? FontWeight.SemiBold : FontWeight.Normal
    };

    public static TextBlock NightTitle(string text, double size, string color = Night.Text) => new()
    {
        Text = text.ToUpperInvariant(),
        FontSize = size,
        Foreground = Hex(color),
        FontFamily = FontDisplay,
        FontWeight = FontWeight.SemiBold,
        FontStretch = FontStretch.Condensed,
        TextWrapping = TextWrapping.NoWrap,
        RenderTransform = Lean(-8),
        RenderTransformOrigin = RelativePoint.Center
    };

    // Small heading with a leaning gold bar in front of it.
    public static Control NightEyebrow(string text, string color = Night.Gold)
    {
        var row = new StackPanel { Orientation = Orientation.Horizontal };
        row.Children.Add(TiltBar(3, 10, color).Also(b => b.Margin = new Thickness(0, 0, 8, 0)));
        row.Children.Add(new TextBlock
        {
            Text = text.ToUpperInvariant(),
            FontSize = 11.5,
            Foreground = Hex(color),
            FontFamily = FontBody,
            FontWeight = FontWeight.Bold,
            VerticalAlignment = VerticalAlignment.Center
        });
        return row;
    }

    // --- shapes ----------------------------------------------------------------------
    // A small leaning gold bar.
    public static Border TiltBar(double width = 4, double height = 14, string color = Night.Gold) => new()
    {
        Width = width,
        Height = height,
        Background = Hex(color),
        RenderTransform = Lean(-18),
        RenderTransformOrigin = RelativePoint.Center,
        VerticalAlignment = VerticalAlignment.Center
    };

    // A leaning glass tile with a gold notch on its top-left edge.
    public static Border TiltTile(Control child, Thickness padding)
    {
        var inner = new Grid();
        inner.Children.Add(child);
        inner.Children.Add(TiltBar(26, 3).Also(b =>
        {
            b.HorizontalAlignment = HorizontalAlignment.Left;
            b.VerticalAlignment = VerticalAlignment.Top;
            b.Margin = new Thickness(0, -padding.Top, 0, 0);
        }));
        return new Border
        {
            Child = inner,
            Background = GlassFill(),
            BorderBrush = GlassEdgeFill(),
            BorderThickness = new Thickness(1),
            CornerRadius = new CornerRadius(16, 3, 16, 3),
            Padding = padding
        };
    }

    // --- icons (Unicode fallback; swap for an icon font later) -----------------------
    public static TextBlock Icon(string key, double size, string color = Night.Text) => new()
    {
        Text = Glyph(key),
        FontSize = size,
        Foreground = Hex(color),
        HorizontalAlignment = HorizontalAlignment.Center,
        VerticalAlignment = VerticalAlignment.Center
    };

    private static string Glyph(string key) => key switch
    {
        "home" => "⌂",        // ⌂
        "play" => "▶",        // ▶
        "store" => "▤",       // ▤
        "hosting" => "⛶",     // ⛶ (people-ish placeholder)
        "online" => "☁",      // ☁
        "stats" => "▦",       // ▦
        "settings" => "⚙",    // ⚙
        "shop" => "▩",        // ▩
        "discord" => "◈",     // ◈
        "log" => "☰",         // ☰
        "arrow" => "→",       // →
        "chevron" => "›",     // ›
        "update" => "⟳",      // ⟳
        "friends" => "☻",     // ☻
        "edit" => "✎",        // ✎
        _ => "∙"              // ∙
    };

    // --- clickable (Border acting as a button) ---------------------------------------
    public static Border Clickable(Control content, Action onClick, string? tip = null)
    {
        var b = new Border { Child = content, Cursor = new Cursor(StandardCursorType.Hand), Background = Hex("#00000000") };
        if (tip != null) ToolTip.SetTip(b, tip);
        b.PointerPressed += (_, e) =>
        {
            if (e.GetCurrentPoint(b).Properties.IsLeftButtonPressed) onClick();
        };
        return b;
    }

    // Swap a border's background on hover (for nav rows, shortcuts).
    public static void Hover(Border b, string normal, string over)
    {
        b.Background = Hex(normal);
        b.PointerEntered += (_, _) => b.Background = Hex(over);
        b.PointerExited += (_, _) => b.Background = Hex(normal);
    }

    // A flat, rounded, filled button-looking border (LAUNCH, SEASONS, etc).
    public static Border FlatButton(Control content, string bg, double radius, string? border = null, Action? onClick = null, string? tip = null)
    {
        var b = new Border
        {
            Child = content,
            Background = bg.StartsWith('#') ? Hex(bg) : Hex(bg),
            BorderBrush = Hex(border ?? "#00000000"),
            BorderThickness = new Thickness(border == null ? 0 : 1),
            CornerRadius = new CornerRadius(radius),
            Cursor = new Cursor(StandardCursorType.Hand)
        };
        if (tip != null) ToolTip.SetTip(b, tip);
        if (onClick != null)
            b.PointerPressed += (_, e) => { if (e.GetCurrentPoint(b).Properties.IsLeftButtonPressed) onClick(); };
        return b;
    }
}
