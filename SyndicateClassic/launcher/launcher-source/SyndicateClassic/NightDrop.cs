// Syndicate © 2026 Syndicate. All rights reserved. Proprietary — no copying, distribution, or derivative works. See LICENSE.
using System;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Media;
using System.Windows.Media.Effects;
namespace SyndicateClassic;

// The "Syndicate" look: deep ink, gold, and everything leaning. Titles lean forward, panels and buttons have two
// rounded and two sharp corners (TiltCorners), the big gold buttons are parallelograms (TiltButtonTemplate),
// panels catch light on their top-left edge, and a faint diagonal hatch runs behind every page (TiltHatch).
// Every screen builds its pieces from these helpers, so changing them restyles the whole launcher.
public partial class MainWindow
{
    private static class Night
    {
        public const string Bg = "#06070C", Surface = "#0C0F1A", Surface2 = "#131726", Line = "#232A40";
        public const string Text = "#F5F7FF", Sub = "#A3ABCB", Muted = "#636B92";
        public const string Gold = "#FFD23F", GoldDeep = "#F2B705", Sky = "#4CC2FF", Violet = "#8B6CFF";
        public const string Danger = "#FF5A6A", Good = "#3BE48B", Hover = "#1B2033";
        public const string Frost = "#990C0F1A", FrostHover = "#C21B2033", FrostEdge = "#33FFFFFF";
        public const string OnGold = "#1A1405";
    }

    private static readonly FontFamily NightDisplay = new("Bahnschrift SemiBold Condensed, Bahnschrift, Segoe UI Black");
    private static readonly FontFamily NightBody = new("Segoe UI Variable Text, Segoe UI");
    private static readonly FontFamily NightMono = new("Cascadia Mono, Consolas");

    // Each season has its own accent: it tints the window glow, the picker bar and the build text.
    private static string SeasonAccent(int season) => season switch
    {
        0 => "#B388FF", 1 => "#22C55E", 2 => "#F59E0B", 3 => "#FF8A4C", 4 => "#38BDF8", 5 => "#2DD4BF",
        6 => "#8B5CF6", 7 => "#7DD3FC", 8 => "#F97316", 9 => "#22D3EE", 11 => "#3B82F6", 12 => "#F2B135", _ => "#E11D48"
    };

    private static Color NightColor(string hex) => (Color)ColorConverter.ConvertFromString(hex);

    private static Brush GlassFill() => new LinearGradientBrush(NightColor("#EB10131F"), NightColor("#E0090B14"), 45);
    // Panels catch the light on their top-left corner (gold), fading to a plain edge.
    private static Brush GlassEdgeFill()
    {
        var edge = new LinearGradientBrush { StartPoint = new Point(0, 0), EndPoint = new Point(1, 1) };
        edge.GradientStops.Add(new GradientStop(NightColor("#99FFD23F"), 0));
        edge.GradientStops.Add(new GradientStop(NightColor("#33FFFFFF"), .22));
        edge.GradientStops.Add(new GradientStop(NightColor("#1AFFFFFF"), 1));
        return edge;
    }

    // Two rounded corners (top-left, bottom-right) and two nearly sharp ones: the launcher's leaning shape.
    // Circles and pills (radius 16 and up) stay round.
    private static CornerRadius TiltCorners(double radius) => radius >= 16 && radius != 18 ? new CornerRadius(radius) : new CornerRadius(radius, Math.Min(3, radius), radius, Math.Min(3, radius));

    // Big buttons as parallelograms: the fill leans, the text stays upright.
    private static ControlTemplate TiltButtonTemplate(double lean = -12)
    {
        var root = new FrameworkElementFactory(typeof(Grid));
        var chrome = new FrameworkElementFactory(typeof(Border));
        chrome.SetValue(Border.CornerRadiusProperty, new CornerRadius(4));
        chrome.SetValue(Border.BackgroundProperty, new TemplateBindingExtension(Control.BackgroundProperty));
        chrome.SetValue(Border.BorderBrushProperty, new TemplateBindingExtension(Control.BorderBrushProperty));
        chrome.SetValue(Border.BorderThicknessProperty, new TemplateBindingExtension(Control.BorderThicknessProperty));
        var skew = new SkewTransform(lean, 0); skew.Freeze();
        chrome.SetValue(UIElement.RenderTransformProperty, skew);
        chrome.SetValue(UIElement.RenderTransformOriginProperty, new Point(.5, .5));
        root.AppendChild(chrome);
        var content = new FrameworkElementFactory(typeof(ContentPresenter));
        content.SetValue(FrameworkElement.HorizontalAlignmentProperty, HorizontalAlignment.Center);
        content.SetValue(FrameworkElement.VerticalAlignmentProperty, VerticalAlignment.Center);
        content.SetValue(FrameworkElement.MarginProperty, new TemplateBindingExtension(Control.PaddingProperty));
        root.AppendChild(content);
        var template = new ControlTemplate(typeof(Button)) { VisualTree = root };
        var hover = new Trigger { Property = UIElement.IsMouseOverProperty, Value = true }; hover.Setters.Add(new Setter(UIElement.OpacityProperty, 0.9)); template.Triggers.Add(hover);
        var disabled = new Trigger { Property = UIElement.IsEnabledProperty, Value = false }; disabled.Setters.Add(new Setter(UIElement.OpacityProperty, 0.45)); template.Triggers.Add(disabled);
        return template;
    }

    // A faint diagonal hatch for the window background.
    private static Border TiltHatch()
    {
        var line = new GeometryDrawing(null, new Pen(new SolidColorBrush(Color.FromArgb(14, 255, 255, 255)), 1), new LineGeometry(new Point(0, 28), new Point(28, 0)));
        var hatch = new DrawingBrush(line) { TileMode = TileMode.Tile, Viewport = new Rect(0, 0, 28, 28), ViewportUnits = BrushMappingMode.Absolute, Viewbox = new Rect(0, 0, 28, 28), ViewboxUnits = BrushMappingMode.Absolute };
        hatch.Freeze();
        var fade = new LinearGradientBrush { StartPoint = new Point(0, 0), EndPoint = new Point(1, 1) };
        fade.GradientStops.Add(new GradientStop(Color.FromArgb(255, 0, 0, 0), 0));
        fade.GradientStops.Add(new GradientStop(Color.FromArgb(0, 0, 0, 0), .7));
        return new Border { Background = hatch, OpacityMask = fade, IsHitTestVisible = false };
    }

    // A small leaning gold bar, used in front of eyebrows and on the active menu item.
    private static Border TiltBar(double width = 4, double height = 14, string color = Night.Gold) => new()
    {
        Width = width, Height = height, Background = Hex(color), RenderTransform = new SkewTransform(-18, 0), VerticalAlignment = VerticalAlignment.Center
    };
    private static Brush GoldFill() => new LinearGradientBrush(NightColor("#FFE46B"), NightColor("#FFC21A"), 90);
    private static Brush BrandFill()
    {
        var brush = new LinearGradientBrush { StartPoint = new Point(0, 0), EndPoint = new Point(1, 0) };
        brush.GradientStops.Add(new GradientStop(NightColor("#FFD23F"), 0));
        brush.GradientStops.Add(new GradientStop(NightColor("#FF8A3D"), .55));
        brush.GradientStops.Add(new GradientStop(NightColor("#8B6CFF"), 1));
        return brush;
    }

    // Glass panel: leaning corners, padding 24, lit edge.
    private static Border NightPanel(UIElement child) => new()
    {
        Child = child, Background = GlassFill(), BorderBrush = GlassEdgeFill(), BorderThickness = new Thickness(1),
        CornerRadius = new CornerRadius(18, 3, 18, 3), Padding = new Thickness(24)
    };

    // Frosted glass over the backdrop (chips, round buttons).
    private static Border FrostBox(UIElement child, double radius) => new()
    {
        Child = child, Background = Hex(Night.Frost), BorderBrush = Hex(Night.FrostEdge), BorderThickness = new Thickness(1),
        CornerRadius = TiltCorners(radius)
    };

    // Titles lean forward, like the name.
    private static TextBlock NightTitle(string text, double size, string color = Night.Text) => new()
    {
        Text = text.ToUpperInvariant(), FontSize = size, Foreground = Hex(color), FontFamily = NightDisplay,
        FontWeight = FontWeights.SemiBold, FontStretch = FontStretches.Condensed, TextWrapping = TextWrapping.NoWrap,
        RenderTransform = new SkewTransform(-8, 0), RenderTransformOrigin = new Point(.5, .5)
    };

    // Small heading with a leaning bar in front of it.
    private static TextBlock NightEyebrow(string text) => NightEyebrow(text, Night.Gold);
    private static TextBlock NightEyebrow(string text, string color)
    {
        var eyebrow = new TextBlock { FontSize = 11.5, Foreground = Hex(color), FontFamily = NightBody, FontWeight = FontWeights.Bold };
        eyebrow.Inlines.Add(new System.Windows.Documents.InlineUIContainer(TiltBar(3, 10, color).Also(b => b.Margin = new Thickness(0, 0, 8, -1))) { BaselineAlignment = BaselineAlignment.Center });
        eyebrow.Inlines.Add(new System.Windows.Documents.Run(text.ToUpperInvariant()));
        return eyebrow;
    }

    private static TextBlock NightText(string text, double size = 13, string color = Night.Text, bool bold = false) => new()
    {
        Text = text, FontSize = size, Foreground = Hex(color), FontFamily = NightBody, TextWrapping = TextWrapping.Wrap,
        FontWeight = bold ? FontWeights.SemiBold : FontWeights.Normal
    };

    private static System.Windows.Shapes.Ellipse NightDot(string color, double size = 8) => new()
    {
        Width = size, Height = size, Fill = Hex(color), VerticalAlignment = VerticalAlignment.Center
    };

    // Buttons: "gold" (PLAY style), "base" (Surface2 + line), "ghost" (text only), "danger".
    private static void NightButton(Button button, string kind, double radius = 9)
    {
        switch (kind)
        {
            case "gold":
                RoundButton(button, Night.Gold, Night.OnGold, radius);
                button.Background = GoldFill();
                button.BorderBrush = Hex("#B07A00"); button.BorderThickness = new Thickness(0, 0, 0, 3);
                button.FontFamily = NightDisplay; button.FontWeight = FontWeights.SemiBold; button.FontStretch = FontStretches.Condensed;
                button.Effect = new DropShadowEffect { Color = NightColor("#FFC21A"), BlurRadius = 22, ShadowDepth = 0, Opacity = .35 };
                button.Template = TiltButtonTemplate();
                break;
            case "ghost":
                RoundButton(button, "#00000000", Night.Sub, radius);
                button.FontFamily = NightBody; button.FontWeight = FontWeights.SemiBold;
                button.MouseEnter += (_, __) => button.Foreground = Hex(Night.Text);
                button.MouseLeave += (_, __) => button.Foreground = Hex(Night.Sub);
                break;
            case "danger":
                RoundButton(button, Night.Danger, "#FFFFFF", radius);
                button.FontFamily = NightBody; button.FontWeight = FontWeights.SemiBold;
                break;
            default:
                RoundButton(button, Night.Surface2, Night.Text, radius, Night.Line);
                button.BorderThickness = new Thickness(1);
                button.FontFamily = NightBody; button.FontWeight = FontWeights.SemiBold;
                button.MouseEnter += (_, __) => { if (button.IsEnabled) { button.Background = Hex(Night.Hover); button.BorderBrush = Hex("#2B3A5C"); } };
                button.MouseLeave += (_, __) => { button.Background = Hex(Night.Surface2); button.BorderBrush = Hex(Night.Line); };
                break;
        }
        if (button.ReadLocalValue(Control.PaddingProperty) == DependencyProperty.UnsetValue) button.Padding = new Thickness(14, 8, 14, 8);
    }

    // Round frosted icon button (folder, settings, copy...).
    private static Button NightRoundIcon(string glyph, string tip, double size = 46, double glyphSize = 16)
    {
        var button = new Button { Width = size, Height = size, Content = GlyphIcon(glyph, glyphSize, Night.Text), ToolTip = tip, Padding = new Thickness(0) };
        RoundButton(button, Night.Frost, Night.Text, size / 2, Night.FrostEdge);
        button.BorderThickness = new Thickness(1);
        button.MouseEnter += (_, __) => button.Background = Hex(Night.FrostHover);
        button.MouseLeave += (_, __) => button.Background = Hex(Night.Frost);
        return button;
    }

    // Text input in the Night Drop look: Surface fill, line border that turns gold on focus.
    private static void NightField(TextBox box)
    {
        box.Background = Hex(Night.Surface); box.Foreground = Hex(Night.Text); box.CaretBrush = Hex(Night.Gold);
        box.BorderBrush = Hex(Night.Line); box.BorderThickness = new Thickness(1); box.FontSize = 14; box.FontFamily = NightBody;
        box.Padding = new Thickness(10, 2, 10, 2); box.VerticalContentAlignment = VerticalAlignment.Center;
        box.SelectionBrush = Hex(Night.Gold);
        box.GotKeyboardFocus += (_, __) => box.BorderBrush = Hex(Night.Gold);
        box.LostKeyboardFocus += (_, __) => box.BorderBrush = Hex(Night.Line);
        var chrome = new FrameworkElementFactory(typeof(Border));
        chrome.SetValue(Border.CornerRadiusProperty, TiltCorners(9));
        chrome.SetValue(Border.BackgroundProperty, new TemplateBindingExtension(Control.BackgroundProperty));
        chrome.SetValue(Border.BorderBrushProperty, new TemplateBindingExtension(Control.BorderBrushProperty));
        chrome.SetValue(Border.BorderThicknessProperty, new TemplateBindingExtension(Control.BorderThicknessProperty));
        var host = new FrameworkElementFactory(typeof(ScrollViewer)) { Name = "PART_ContentHost" };
        // No margin here: the TextBox already applies its own Padding inside the text area.
        // Binding Padding here as well doubled it and squashed the text (it showed as "- - - -").
        host.SetValue(VerticalAlignmentProperty, VerticalAlignment.Center);
        host.SetValue(FocusableProperty, false);
        chrome.AppendChild(host);
        var template = new ControlTemplate(typeof(TextBox)) { VisualTree = chrome };
        var disabled = new Trigger { Property = IsEnabledProperty, Value = false }; disabled.Setters.Add(new Setter(OpacityProperty, 0.5)); template.Triggers.Add(disabled);
        box.Template = template;
    }

    // Filter chip (pill radius 16): gold fill when selected.
    private static Button NightChip(string text)
    {
        var chip = new Button { Content = text, Height = 32, Padding = new Thickness(14, 0, 14, 0), Margin = new Thickness(0, 0, 8, 8) };
        RoundButton(chip, Night.Surface2, Night.Sub, 16, Night.Line);
        chip.BorderThickness = new Thickness(1);
        chip.FontFamily = NightBody; chip.FontWeight = FontWeights.SemiBold; chip.FontSize = 12.5;
        return chip;
    }

    private static void SetChip(Button chip, bool selected)
    {
        chip.Background = Hex(selected ? Night.Gold : Night.Surface2);
        chip.Foreground = Hex(selected ? Night.OnGold : Night.Sub);
        chip.BorderBrush = Hex(selected ? Night.Gold : Night.Line);
    }

    private static string LauncherVersion() => System.Reflection.Assembly.GetExecutingAssembly().GetName().Version?.ToString(3) ?? "?";
}