using System;
using System.Globalization;
using System.Windows;
using System.Windows.Media;
using SyndicateLauncher.Models;
using SyndicateLauncher.Services;

namespace SyndicateLauncher.Controls;

/// <summary>
/// Settings &gt; Display &gt; Crosshair preview. Draws the crosshair with the same geometry as SyndicateMenu.dll
/// (Features/Crosshair.cpp), centred in the control. <see cref="Zoom"/> 1 is the size it has in game (one game
/// pixel is one screen pixel); larger values magnify it so the pixels can be seen.
/// </summary>
public sealed class CrosshairPreview : FrameworkElement
{
    public static readonly DependencyProperty SettingsProperty = DependencyProperty.Register(nameof(Settings),
        typeof(CrosshairSettings), typeof(CrosshairPreview), new FrameworkPropertyMetadata(null, FrameworkPropertyMetadataOptions.AffectsRender));

    public static readonly DependencyProperty ZoomProperty = DependencyProperty.Register(nameof(Zoom),
        typeof(double), typeof(CrosshairPreview), new FrameworkPropertyMetadata(1.0, FrameworkPropertyMetadataOptions.AffectsRender));

    public CrosshairSettings? Settings { get => (CrosshairSettings?)GetValue(SettingsProperty); set => SetValue(SettingsProperty, value); }
    public double Zoom { get => (double)GetValue(ZoomProperty); set => SetValue(ZoomProperty, value); }

    public CrosshairPreview()
    {
        SnapsToDevicePixels = true;
        UseLayoutRounding = true;
    }

    private static Color ParseColor(string hex, int opacity)
    {
        string argb = CrosshairService.Argb(hex, opacity);
        uint v = uint.Parse(argb, NumberStyles.HexNumber, CultureInfo.InvariantCulture);
        return Color.FromArgb((byte)(v >> 24), (byte)(v >> 16), (byte)(v >> 8), (byte)v);
    }

    protected override void OnRender(DrawingContext dc)
    {
        var c = Settings;
        double w = ActualWidth, h = ActualHeight;
        dc.DrawRectangle(Brushes.Transparent, null, new Rect(0, 0, w, h));
        if (c is null || w < 4 || h < 4) return;

        // Work in game pixels: one game pixel = Zoom device pixels.
        double dpi = VisualTreeHelper.GetDpi(this).DpiScaleX;
        double px = Math.Max(1, Math.Round(Zoom)) / dpi;
        double W = Math.Floor(w / px), H = Math.Floor(h / px);
        double ox = (w - W * px) * 0.5, oy = (h - H * px) * 0.5;
        ox = Math.Round(ox * dpi) / dpi; oy = Math.Round(oy * dpi) / dpi;

        var fill = new SolidColorBrush(ParseColor(c.Color, c.Opacity)); fill.Freeze();
        var edge = new SolidColorBrush(ParseColor(c.OutlineColor, c.OutlineOpacity)); edge.Freeze();
        double o = c.Outline ? c.OutlineThickness : 0, t = c.Thickness, len = c.Length, gap = c.Gap;
        var mid = new Point(ox + W * 0.5 * px, oy + H * 0.5 * px);
        static double Start(double total, double size) => Math.Floor((total - size) * 0.5);

        void Box(double x0, double y0, double x1, double y1, Brush b) =>
            dc.DrawRectangle(b, null, new Rect(ox + x0 * px, oy + y0 * px, (x1 - x0) * px, (y1 - y0) * px));

        for (int pass = 0; pass < 2; pass++)
        {
            bool edgePass = pass == 0;
            if (edgePass && !c.Outline) continue;
            Brush b = edgePass ? edge : fill;
            double g = edgePass ? o : 0;

            if (c.Circle)
            {
                var pen = new Pen(b, (c.CircleThickness + g * 2) * px); pen.Freeze();
                dc.DrawEllipse(null, pen, mid, c.CircleRadius * px, c.CircleRadius * px);
            }

            if (c.Lines && len > 0)
            {
                if (!c.Diagonal)
                {
                    double cx = Start(W, t), cy = Start(H, t);
                    void Arm(double x0, double y0, double x1, double y1) => Box(x0 - g, y0 - g, x1 + g, y1 + g, b);
                    Arm(cx + t + gap, cy, cx + t + gap + len, cy + t);
                    Arm(cx - gap - len, cy, cx - gap, cy + t);
                    Arm(cx, cy + t + gap, cx + t, cy + t + gap + len);
                    if (!c.TStyle) Arm(cx, cy - gap - len, cx + t, cy - gap);
                    if (gap <= 0 && !c.Dot) Arm(cx, cy, cx + t, cy + t);
                }
                else
                {
                    double r0 = gap + t * 0.5, r1 = r0 + len, k = Math.Sqrt(0.5);
                    var pen = new Pen(b, (t + g * 2) * px) { StartLineCap = PenLineCap.Flat, EndLineCap = PenLineCap.Flat }; pen.Freeze();
                    foreach (var (dx, dy) in new[] { (k, k), (-k, k), (k, -k), (-k, -k) })
                    {
                        double a = r0 - g, e = r1 + g;
                        dc.DrawLine(pen, new Point(mid.X + dx * a * px, mid.Y + dy * a * px), new Point(mid.X + dx * e * px, mid.Y + dy * e * px));
                    }
                }
            }

            if (c.Dot)
            {
                double d = c.DotSize;
                if (c.DotRound)
                    dc.DrawEllipse(b, null, mid, (d * 0.5 + g) * px, (d * 0.5 + g) * px);
                else
                {
                    double x0 = Start(W, d), y0 = Start(H, d);
                    Box(x0 - g, y0 - g, x0 + d + g, y0 + d + g, b);
                }
            }
        }
    }
}
