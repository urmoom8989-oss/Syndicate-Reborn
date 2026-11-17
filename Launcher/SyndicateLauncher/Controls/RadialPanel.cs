using System;
using System.Windows;
using System.Windows.Controls;

namespace SyndicateLauncher.Controls;

/// <summary>Arranges items evenly around a circle, starting at the top.</summary>
public sealed class RadialPanel : Panel
{
    public static readonly DependencyProperty RadiusProperty = DependencyProperty.Register(
        nameof(Radius),
        typeof(double),
        typeof(RadialPanel),
        new FrameworkPropertyMetadata(205.0, FrameworkPropertyMetadataOptions.AffectsArrange));

    public static readonly DependencyProperty DiameterProperty = DependencyProperty.Register(
        nameof(Diameter),
        typeof(double),
        typeof(RadialPanel),
        new FrameworkPropertyMetadata(520.0, FrameworkPropertyMetadataOptions.AffectsMeasure));

    public double Radius
    {
        get => (double)GetValue(RadiusProperty);
        set => SetValue(RadiusProperty, value);
    }

    public double Diameter
    {
        get => (double)GetValue(DiameterProperty);
        set => SetValue(DiameterProperty, value);
    }

    protected override Size MeasureOverride(Size availableSize)
    {
        var constraint = new Size(double.PositiveInfinity, double.PositiveInfinity);
        foreach (UIElement child in InternalChildren)
            child.Measure(constraint);

        return new Size(Diameter, Diameter);
    }

    protected override Size ArrangeOverride(Size finalSize)
    {
        int count = InternalChildren.Count;
        if (count == 0) return finalSize;

        double centerX = finalSize.Width / 2.0;
        double centerY = finalSize.Height / 2.0;

        for (int i = 0; i < count; i++)
        {
            UIElement child = InternalChildren[i];
            double angle = (-90.0 + (360.0 * i / count)) * Math.PI / 180.0;
            double x = centerX + Radius * Math.Cos(angle);
            double y = centerY + Radius * Math.Sin(angle);
            double width = child.DesiredSize.Width;
            double height = child.DesiredSize.Height;

            child.Arrange(new Rect(x - width / 2.0, y - height / 2.0, width, height));
        }

        return finalSize;
    }
}
