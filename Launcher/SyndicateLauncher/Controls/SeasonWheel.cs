using System;
using System.Collections.Generic;
using System.Collections.Specialized;
using System.Globalization;
using System.Windows;
using System.Windows.Automation.Peers;
using System.Windows.Controls;
using System.Windows.Input;
using System.Windows.Media;
using SyndicateLauncher.Models;

namespace SyndicateLauncher.Controls;

/// <summary>
/// A segmented season wheel. Image-backed seasons use their supplied artwork; the rest
/// fall back to the cover-brush placeholders.
/// </summary>
public sealed class SeasonWheel : Control
{
    public static readonly DependencyProperty ItemsSourceProperty = DependencyProperty.Register(
        nameof(ItemsSource),
        typeof(IEnumerable<SeasonInfo>),
        typeof(SeasonWheel),
        new FrameworkPropertyMetadata(null, FrameworkPropertyMetadataOptions.AffectsRender, OnItemsSourceChanged));

    public static readonly DependencyProperty SelectedItemProperty = DependencyProperty.Register(
        nameof(SelectedItem),
        typeof(SeasonInfo),
        typeof(SeasonWheel),
        new FrameworkPropertyMetadata(
            null,
            FrameworkPropertyMetadataOptions.AffectsRender | FrameworkPropertyMetadataOptions.BindsTwoWayByDefault,
            OnSelectedItemChanged));

    public static readonly DependencyProperty OuterRadiusProperty = DependencyProperty.Register(
        nameof(OuterRadius),
        typeof(double),
        typeof(SeasonWheel),
        new FrameworkPropertyMetadata(268.0, FrameworkPropertyMetadataOptions.AffectsRender));

    public static readonly DependencyProperty InnerRadiusProperty = DependencyProperty.Register(
        nameof(InnerRadius),
        typeof(double),
        typeof(SeasonWheel),
        new FrameworkPropertyMetadata(136.0, FrameworkPropertyMetadataOptions.AffectsRender));

    private static readonly Typeface LabelTypeface = new(
        new FontFamily("Bahnschrift SemiCondensed, Bahnschrift, Segoe UI"),
        FontStyles.Italic,
        FontWeights.Bold,
        FontStretches.SemiCondensed);

    private static readonly Brush SeparatorBrush = FrozenBrush(120, 3, 12, 18);
    private static readonly Brush HoverBrush = FrozenBrush(170, 255, 255, 255);
    private static readonly Brush AccentBrush = FrozenBrush(255, 255, 255, 255);
    private static readonly Brush AccentSoftBrush = FrozenBrush(50, 242, 243, 245);
    private static readonly Brush AccentFaintBrush = FrozenBrush(20, 242, 243, 245);
    private static readonly Brush LabelBrush = FrozenBrush(255, 255, 255, 255);
    private static readonly Brush LabelMutedBrush = FrozenBrush(230, 255, 255, 255);
    private static readonly Brush LabelShadowBrush = FrozenBrush(205, 0, 9, 15);
    private static readonly Brush LockedTintBrush = FrozenBrush(55, 2, 8, 13);
    private static readonly Brush LockedSelectedTintBrush = FrozenBrush(24, 2, 8, 13);

    private readonly List<SeasonInfo> _items = new();
    private INotifyCollectionChanged? _observedCollection;
    private SeasonInfo? _hovered;
    // Hover is drawn on its own layer: moving the mouse across the wheel used to re-render all twelve wedges
    // (HighQuality artwork, vignettes, labels) every time the hovered wedge changed.
    private readonly DrawingVisual _hoverLayer = new();

    public SeasonWheel()
    {
        Focusable = true;
        IsTabStop = true;
        Cursor = Cursors.Hand;
        FocusVisualStyle = null;
        UseLayoutRounding = true;
        SnapsToDevicePixels = true;
        RenderOptions.SetBitmapScalingMode(this, BitmapScalingMode.LowQuality);
        ToolTipService.SetInitialShowDelay(this, 450);
        ToolTipService.SetShowDuration(this, 15000);
        AddVisualChild(_hoverLayer);
    }

    // Control's own Measure/Arrange treat visual child 0 as the template root (a UIElement); the wheel has no
    // template, and its only child is the hover DrawingVisual, so it sizes itself.
    protected override Size MeasureOverride(Size constraint) => new(0, 0);
    protected override Size ArrangeOverride(Size arrangeBounds) => arrangeBounds;

    protected override int VisualChildrenCount => base.VisualChildrenCount + 1;

    protected override Visual GetVisualChild(int index) =>
        index < base.VisualChildrenCount ? base.GetVisualChild(index) : _hoverLayer;

    private void RenderHover()
    {
        using var dc = _hoverLayer.RenderOpen();
        int count = _items.Count;
        int hovered = IndexOf(_hovered);
        if (count == 0 || hovered < 0 || hovered == IndexOf(SelectedItem) || ActualWidth <= 0) return;
        double step = 360.0 / count;
        double gap = Math.Min(1.15, step * 0.06);
        dc.PushTransform(new TranslateTransform(ActualWidth / 2.0, ActualHeight / 2.0));
        var geometry = BuildWedge(InnerRadius, OuterRadius, AngleFor(hovered, count), step, gap);
        dc.DrawGeometry(null, new Pen(HoverBrush, 1.25), geometry);
        var season = _items[hovered];
        if (season.WheelImage is null)
        {
            string text = "SEASON " + season.Code.ToUpperInvariant();
            var point = Polar((InnerRadius + OuterRadius) / 2.0, AngleFor(hovered, count));
            var label = new FormattedText(text, CultureInfo.InvariantCulture, FlowDirection.LeftToRight, LabelTypeface, 14.0,
                LabelBrush, VisualTreeHelper.GetDpi(this).PixelsPerDip);
            dc.DrawText(label, new Point(point.X - label.Width / 2.0, point.Y - label.Height / 2.0));
        }
        dc.Pop();
    }

    public IEnumerable<SeasonInfo>? ItemsSource
    {
        get => (IEnumerable<SeasonInfo>?)GetValue(ItemsSourceProperty);
        set => SetValue(ItemsSourceProperty, value);
    }

    public SeasonInfo? SelectedItem
    {
        get => (SeasonInfo?)GetValue(SelectedItemProperty);
        set => SetValue(SelectedItemProperty, value);
    }

    public double OuterRadius
    {
        get => (double)GetValue(OuterRadiusProperty);
        set => SetValue(OuterRadiusProperty, value);
    }

    public double InnerRadius
    {
        get => (double)GetValue(InnerRadiusProperty);
        set => SetValue(InnerRadiusProperty, value);
    }

    protected override AutomationPeer OnCreateAutomationPeer() => new SeasonWheelAutomationPeer(this);

    protected override HitTestResult? HitTestCore(PointHitTestParameters hitTestParameters)
    {
        var center = new Point(ActualWidth / 2.0, ActualHeight / 2.0);
        var offset = hitTestParameters.HitPoint - center;
        return offset.Length <= OuterRadius + 12
            ? new PointHitTestResult(this, hitTestParameters.HitPoint)
            : null;
    }

    protected override void OnRender(DrawingContext drawingContext)
    {
        base.OnRender(drawingContext);

        if (_items.Count == 0 || ActualWidth <= 0 || ActualHeight <= 0)
            return;

        var center = new Point(ActualWidth / 2.0, ActualHeight / 2.0);
        double pixelsPerDip = VisualTreeHelper.GetDpi(this).PixelsPerDip;

        drawingContext.PushTransform(new TranslateTransform(center.X, center.Y));
        try
        {
            DrawBackdrop(drawingContext);
            DrawWheel(drawingContext, pixelsPerDip);
        }
        finally
        {
            drawingContext.Pop();
        }
    }

    private void DrawBackdrop(DrawingContext dc)
    {
        var ambient = new RadialGradientBrush
        {
            GradientStops =
            {
                new GradientStop(Color.FromArgb(66, 242, 243, 245), 0.56),
                new GradientStop(Color.FromArgb(24, 242, 243, 245), 0.76),
                new GradientStop(Color.FromArgb(0, 242, 243, 245), 1.0)
            }
        };
        ambient.Freeze();

        dc.DrawEllipse(ambient, null, new Point(0, 0), OuterRadius + 18, OuterRadius + 18);
        dc.DrawEllipse(
            new SolidColorBrush(Color.FromArgb(212, 3, 11, 18)),
            new Pen(FrozenBrush(80, 242, 243, 245), 1),
            new Point(0, 0),
            OuterRadius + 1,
            OuterRadius + 1);
    }

    private void DrawWheel(DrawingContext dc, double pixelsPerDip)
    {
        int count = _items.Count;
        double step = 360.0 / count;
        double gap = Math.Min(1.15, step * 0.06);
        int selectedIndex = IndexOf(SelectedItem);
        const int hoveredIndex = -1; // hover is drawn by RenderHover on its own layer

        Geometry? selectedGeometry = null;
        for (int i = 0; i < count; i++)
        {
            double centerAngle = AngleFor(i, count);
            var geometry = BuildWedge(InnerRadius, OuterRadius, centerAngle, step, gap);
            if (i == selectedIndex)
            {
                selectedGeometry = BuildWedge(
                    Math.Max(0, InnerRadius - 2),
                    OuterRadius + 5,
                    centerAngle,
                    step,
                    gap * 0.2);
                continue;
            }

            DrawWedge(
                dc,
                _items[i],
                geometry,
                i,
                count,
                selected: false,
                hovered: i == hoveredIndex,
                pixelsPerDip);
        }

        if (selectedIndex >= 0 && selectedGeometry is not null)
        {
            DrawWedge(
                dc,
                _items[selectedIndex],
                selectedGeometry,
                selectedIndex,
                count,
                selected: true,
                hovered: selectedIndex == hoveredIndex,
                pixelsPerDip);
        }

        for (int i = 0; i < count; i++)
        {
            DrawLabel(
                dc,
                _items[i],
                i,
                count,
                selected: i == selectedIndex,
                hovered: i == hoveredIndex,
                pixelsPerDip);
        }

        dc.DrawEllipse(
            null,
            new Pen(FrozenBrush(18, 242, 243, 245), 18),
            new Point(0, 0),
            OuterRadius + 2,
            OuterRadius + 2);
        dc.DrawEllipse(
            null,
            new Pen(FrozenBrush(54, 242, 243, 245), 7),
            new Point(0, 0),
            OuterRadius + 1,
            OuterRadius + 1);
        dc.DrawEllipse(
            null,
            new Pen(FrozenBrush(210, 255, 255, 255), 1.3),
            new Point(0, 0),
            OuterRadius + 0.5,
            OuterRadius + 0.5);
        dc.DrawEllipse(
            null,
            new Pen(FrozenBrush(48, 242, 243, 245), 4),
            new Point(0, 0),
            Math.Max(0, InnerRadius - 1),
            Math.Max(0, InnerRadius - 1));

        if (selectedGeometry is not null)
        {
            dc.DrawGeometry(null, new Pen(FrozenBrush(20, 242, 243, 245), 22), selectedGeometry);
            dc.DrawGeometry(null, new Pen(AccentSoftBrush, 11), selectedGeometry);
            dc.DrawGeometry(null, new Pen(AccentBrush, 2.1), selectedGeometry);
        }

        if (IsKeyboardFocused)
        {
            var focusPen = new Pen(FrozenBrush(180, 255, 255, 255), 1)
            {
                DashStyle = new DashStyle(new[] { 3.0, 4.0 }, 0)
            };
            dc.DrawEllipse(null, focusPen, new Point(0, 0), OuterRadius + 10, OuterRadius + 10);
        }
    }

    private void DrawWedge(
        DrawingContext dc,
        SeasonInfo season,
        Geometry geometry,
        int index,
        int count,
        bool selected,
        bool hovered,
        double pixelsPerDip)
    {
        dc.PushClip(geometry);
        var image = season.WheelImage;
        bool hasArtwork = image is not null;
        if (image is not null)
        {
            DrawWheelArtwork(dc, image, season.WheelImageFocus, index, count);
        }
        else if (season.CoverBrush is not null)
        {
            dc.DrawGeometry(season.CoverBrush, null, geometry);
        }

        if (!hasArtwork)
        {
            var accent = PrimaryColor(season.CoverBrush);
            var outerGlow = new RadialGradientBrush
            {
                GradientStops =
                {
                    new GradientStop(Color.FromArgb((byte)(selected ? 105 : 54), accent.R, accent.G, accent.B), 0),
                    new GradientStop(Color.FromArgb(0, accent.R, accent.G, accent.B), 1)
                }
            };
            outerGlow.Freeze();
            var glowCenter = Polar(OuterRadius * 0.72, AngleFor(index, count));
            dc.DrawEllipse(outerGlow, null, glowCenter, 68, 56);

            // Placeholder texture for seasons that do not have artwork yet.
            var contourPen = new Pen(FrozenBrush((byte)(selected ? 32 : 18), 235, 236, 238), 0.8);
            for (int ring = 0; ring < 3; ring++)
            {
                double radius = InnerRadius + 26 + ring * 34;
                dc.DrawEllipse(null, contourPen, new Point(0, 0), radius, radius);
            }
        }

        var vignette = new RadialGradientBrush
        {
            GradientStops =
            {
                new GradientStop(Color.FromArgb(0, 2, 8, 13), 0.34),
                new GradientStop(Color.FromArgb(38, 2, 8, 13), 0.72),
                new GradientStop(Color.FromArgb(140, 2, 8, 13), 1.0)
            }
        };
        vignette.Freeze();
        dc.DrawEllipse(vignette, null, new Point(0, 0), OuterRadius + 2, OuterRadius + 2);

        if (!season.IsReleased)
            dc.DrawGeometry(selected ? LockedSelectedTintBrush : LockedTintBrush, null, geometry);

        dc.Pop();
        dc.DrawGeometry(null, new Pen(SeparatorBrush, 1.1), geometry);

        if (hovered && !selected)
            dc.DrawGeometry(null, new Pen(HoverBrush, 1.25), geometry);
    }

    private void DrawWheelArtwork(
        DrawingContext dc,
        ImageSource image,
        Point focus,
        int index,
        int count)
    {
        double angle = AngleFor(index, count);
        double step = 360.0 / count;
        double gap = Math.Min(1.15, step * 0.06);
        Rect bounds = BuildWedge(InnerRadius, OuterRadius, angle, step, gap).Bounds;
        Point target = Polar((InnerRadius + OuterRadius) / 2.0, angle);

        double focusX = Math.Clamp(focus.X, 0.2, 0.8);
        double focusY = Math.Clamp(focus.Y, 0.2, 0.8);
        double sourceWidth = Math.Max(1.0, image.Width);
        double sourceHeight = Math.Max(1.0, image.Height);

        double left = Math.Max(0.0, target.X - bounds.Left);
        double right = Math.Max(0.0, bounds.Right - target.X);
        double top = Math.Max(0.0, target.Y - bounds.Top);
        double bottom = Math.Max(0.0, bounds.Bottom - target.Y);

        double scale = Math.Max(
            Math.Max(left / (focusX * sourceWidth), right / ((1.0 - focusX) * sourceWidth)),
            Math.Max(top / (focusY * sourceHeight), bottom / ((1.0 - focusY) * sourceHeight)));

        // Guarantee coverage under the selected wedge's slightly expanded outline.
        scale *= 1.05;
        double width = sourceWidth * scale;
        double height = sourceHeight * scale;
        var destination = new Rect(
            target.X - focusX * width,
            target.Y - focusY * height,
            width,
            height);
        dc.DrawImage(image, destination);
    }

    private void DrawLabel(
        DrawingContext dc,
        SeasonInfo season,
        int index,
        int count,
        bool selected,
        bool hovered,
        double pixelsPerDip)
    {
        // The supplied season logos already contain their large season number.
        if (season.WheelImage is not null)
            return;

        string text = "SEASON " + season.Code.ToUpperInvariant();
        double fontSize = selected ? 16.2 : 14.0;
        var point = Polar((InnerRadius + OuterRadius) / 2.0 + (selected ? 2 : 0), AngleFor(index, count));

        var label = new FormattedText(
            text,
            CultureInfo.InvariantCulture,
            FlowDirection.LeftToRight,
            LabelTypeface,
            fontSize,
            selected || hovered ? LabelBrush : LabelMutedBrush,
            pixelsPerDip);

        var shadow = new FormattedText(
            text,
            CultureInfo.InvariantCulture,
            FlowDirection.LeftToRight,
            LabelTypeface,
            fontSize,
            LabelShadowBrush,
            pixelsPerDip);

        double x = point.X - label.Width / 2.0;
        double y = point.Y - label.Height / 2.0;
        dc.DrawText(shadow, new Point(x + 1, y + 1.5));
        dc.DrawText(label, new Point(x, y));
    }

    private static Geometry BuildWedge(
        double innerRadius,
        double outerRadius,
        double centerAngle,
        double step,
        double gap)
    {
        double startAngle = centerAngle - step / 2.0 + gap;
        double endAngle = centerAngle + step / 2.0 - gap;
        if (endAngle <= startAngle)
            endAngle = startAngle + 0.1;

        Point innerStart = Polar(innerRadius, startAngle);
        Point outerStart = Polar(outerRadius, startAngle);
        Point outerEnd = Polar(outerRadius, endAngle);
        Point innerEnd = Polar(innerRadius, endAngle);

        var figure = new PathFigure
        {
            StartPoint = innerStart,
            IsClosed = true,
            IsFilled = true
        };
        figure.Segments.Add(new LineSegment(outerStart, true));
        figure.Segments.Add(new ArcSegment(
            outerEnd,
            new Size(outerRadius, outerRadius),
            0,
            false,
            SweepDirection.Clockwise,
            true));
        figure.Segments.Add(new LineSegment(innerEnd, true));
        figure.Segments.Add(new ArcSegment(
            innerStart,
            new Size(innerRadius, innerRadius),
            0,
            false,
            SweepDirection.Counterclockwise,
            true));

        var geometry = new PathGeometry();
        geometry.Figures.Add(figure);
        geometry.Freeze();
        return geometry;
    }

    private static Point Polar(double radius, double angleDegrees)
    {
        double radians = angleDegrees * Math.PI / 180.0;
        return new Point(radius * Math.Cos(radians), radius * Math.Sin(radians));
    }

    private static double AngleFor(int index, int count) => -90.0 + 360.0 * index / count;

    private int IndexOf(SeasonInfo? season) => season is null ? -1 : _items.IndexOf(season);

    private int HitTestIndex(Point position)
    {
        if (_items.Count == 0)
            return -1;

        var center = new Point(ActualWidth / 2.0, ActualHeight / 2.0);
        double dx = position.X - center.X;
        double dy = position.Y - center.Y;
        double distance = Math.Sqrt(dx * dx + dy * dy);
        if (distance < InnerRadius - 3 || distance > OuterRadius + 9)
            return -1;

        double angle = Math.Atan2(dy, dx) * 180.0 / Math.PI;
        double offset = (angle + 90.0 + 720.0) % 360.0;
        double step = 360.0 / _items.Count;
        double relative = (offset + step / 2.0) % 360.0;
        int index = (int)(relative / step);
        return Math.Clamp(index, 0, _items.Count - 1);
    }

    private static Color PrimaryColor(Brush? brush) => brush switch
    {
        GradientBrush gradient when gradient.GradientStops.Count > 0 => gradient.GradientStops[0].Color,
        SolidColorBrush solid => solid.Color,
        _ => Color.FromRgb(242, 243, 245)
    };

    private static Brush FrozenBrush(byte alpha, byte red, byte green, byte blue)
    {
        var brush = new SolidColorBrush(Color.FromArgb(alpha, red, green, blue));
        brush.Freeze();
        return brush;
    }

    private static void OnItemsSourceChanged(DependencyObject dependencyObject, DependencyPropertyChangedEventArgs args)
    {
        var wheel = (SeasonWheel)dependencyObject;
        wheel.UnsubscribeFromItemsSource();
        wheel.RebuildItems();

        if (args.NewValue is INotifyCollectionChanged observable)
        {
            observable.CollectionChanged += wheel.OnItemsCollectionChanged;
            wheel._observedCollection = observable;
        }

        wheel._hovered = null;
        wheel.ToolTip = null;
        wheel.InvalidateVisual();
    }

    private static void OnSelectedItemChanged(DependencyObject dependencyObject, DependencyPropertyChangedEventArgs args)
    {
        var wheel = (SeasonWheel)dependencyObject;
        wheel.InvalidateVisual();
        wheel.RenderHover();
    }

    private void OnItemsCollectionChanged(object? sender, NotifyCollectionChangedEventArgs e)
    {
        RebuildItems();
        if (_hovered is not null && !_items.Contains(_hovered))
            _hovered = null;
        ToolTip = _hovered?.AvailabilityNote;
        InvalidateVisual();
    }

    private void RebuildItems()
    {
        _items.Clear();
        if (ItemsSource is null)
            return;

        foreach (var season in ItemsSource)
        {
            if (season is not null)
                _items.Add(season);
        }
    }

    private void UnsubscribeFromItemsSource()
    {
        if (_observedCollection is not null)
        {
            _observedCollection.CollectionChanged -= OnItemsCollectionChanged;
            _observedCollection = null;
        }
    }

    protected override void OnMouseMove(MouseEventArgs e)
    {
        base.OnMouseMove(e);
        if (!IsEnabled)
            return;

        int index = HitTestIndex(e.GetPosition(this));
        var season = index >= 0 ? _items[index] : null;
        if (!ReferenceEquals(season, _hovered))
        {
            _hovered = season;
            ToolTip = season?.AvailabilityNote;
            RenderHover();
        }
    }

    protected override void OnMouseLeave(MouseEventArgs e)
    {
        base.OnMouseLeave(e);
        if (_hovered is null)
            return;

        _hovered = null;
        ToolTip = null;
        RenderHover();
    }

    protected override void OnMouseLeftButtonDown(MouseButtonEventArgs e)
    {
        base.OnMouseLeftButtonDown(e);
        if (!IsEnabled)
            return;

        Focus();
        int index = HitTestIndex(e.GetPosition(this));
        if (index < 0)
            return;

        SetCurrentValue(SelectedItemProperty, _items[index]);
        e.Handled = true;
    }

    protected override void OnMouseWheel(MouseWheelEventArgs e)
    {
        base.OnMouseWheel(e);
        if (!IsEnabled || _items.Count == 0)
            return;

        int index = IndexOf(SelectedItem);
        if (index < 0)
            index = 0;
        index = e.Delta > 0
            ? (index - 1 + _items.Count) % _items.Count
            : (index + 1) % _items.Count;

        SetCurrentValue(SelectedItemProperty, _items[index]);
        e.Handled = true;
    }

    protected override void OnKeyDown(KeyEventArgs e)
    {
        base.OnKeyDown(e);
        if (!IsEnabled || _items.Count == 0)
            return;

        int index = IndexOf(SelectedItem);
        if (index < 0)
            index = 0;

        int next = e.Key switch
        {
            Key.Left or Key.Up => (index - 1 + _items.Count) % _items.Count,
            Key.Right or Key.Down => (index + 1) % _items.Count,
            Key.Home => 0,
            Key.End => _items.Count - 1,
            _ => -1
        };

        if (next < 0)
            return;

        SetCurrentValue(SelectedItemProperty, _items[next]);
        e.Handled = true;
    }

    private sealed class SeasonWheelAutomationPeer : FrameworkElementAutomationPeer
    {
        public SeasonWheelAutomationPeer(SeasonWheel owner) : base(owner)
        {
        }

        protected override string GetClassNameCore() => nameof(SeasonWheel);

        protected override AutomationControlType GetAutomationControlTypeCore() => AutomationControlType.Pane;
    }
}
