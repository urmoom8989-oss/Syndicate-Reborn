using System;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Media;
using System.Windows.Media.Animation;
using System.Windows.Shapes;

namespace SyndicateClassic;

// The side menu can be slid out of the way. A round arrow button sits on the menu's right edge, vertically centred.
// Click it and the whole rail slides off to the left; the button rides out to the screen edge (and turns gold) so it
// is easy to click to bring the menu back. Everything keeps the current look — this only hides and shows the rail.
public partial class MainWindow
{
    private TranslateTransform? _railSlide;    // moves the rail off-screen and back
    private TranslateTransform? _toggleSlide;   // keeps the button reachable when the rail is gone
    private RotateTransform? _railArrow;        // flips the arrow
    private FrameworkElement? _railHost;         // the menu itself
    private bool _railCollapsed;
    // Each screen is inset from the left by the menu's width. When the menu slides away these insets animate down so
    // the page (and its artwork) fills the freed space instead of leaving a dark strip.
    private readonly System.Collections.Generic.List<(FrameworkElement El, DependencyProperty Dp, Thickness Base)> _railInsets = new();

    // Register a screen's left inset so it reflows with the menu. pad: the inset is Padding (a Border), not Margin.
    internal void RailTrack(FrameworkElement? el, bool pad = false)
    {
        if (el == null) return;
        var dp = !pad ? FrameworkElement.MarginProperty : (el is Border ? Border.PaddingProperty : Control.PaddingProperty);
        _railInsets.Add((el, dp, (Thickness)el.GetValue(dp)));
    }

    // Called from the shell once the rail and title bar are in the page.
    private void InstallRailCollapse(Panel page, FrameworkElement rail, int rows)
    {
        if (rail == null || page == null) return;

        _railHost = rail;
        _railSlide = new TranslateTransform();
        rail.RenderTransform = _railSlide;

        // The edge button. Round, in the current panel colours, hover gold — same style as the rest of the launcher.
        var arrow = new Path
        {
            Data = Geometry.Parse("M 15,6 L 9,12 L 15,18"),
            Stroke = Hex(Night.Sub), StrokeThickness = 2.4,
            StrokeStartLineCap = PenLineCap.Round, StrokeEndLineCap = PenLineCap.Round, StrokeLineJoin = PenLineJoin.Round,
            Width = 24, Height = 24, Stretch = Stretch.None,
            HorizontalAlignment = HorizontalAlignment.Center, VerticalAlignment = VerticalAlignment.Center,
            RenderTransformOrigin = new Point(0.5, 0.5)
        };
        _railArrow = new RotateTransform();
        arrow.RenderTransform = _railArrow;

        var face = new Border
        {
            Width = 34, Height = 34, CornerRadius = new CornerRadius(17),
            Background = Hex(Night.Surface2), BorderBrush = Hex(Night.Line), BorderThickness = new Thickness(1),
            Child = arrow
        };
        var toggle = new Button
        {
            Content = face, Background = Brushes.Transparent, BorderThickness = new Thickness(0), Padding = new Thickness(0),
            Cursor = System.Windows.Input.Cursors.Hand, Focusable = false, ToolTip = "Hide / show the menu",
            HorizontalAlignment = HorizontalAlignment.Left, VerticalAlignment = VerticalAlignment.Center,
            // Sit on the menu's right edge (the menu is RailWidth wide; the 34px button straddles the seam).
            Margin = new Thickness(RailWidth - 17, 0, 0, 0)
        };
        toggle.Template = BareButtonTemplate();
        _toggleSlide = new TranslateTransform();
        toggle.RenderTransform = _toggleSlide;
        toggle.MouseEnter += (_, __) => { face.Background = Hex(_railCollapsed ? Night.Gold : Night.FrostHover); arrow.Stroke = Hex(_railCollapsed ? Night.OnGold : Night.Text); };
        toggle.MouseLeave += (_, __) => { face.Background = Hex(_railCollapsed ? Night.Gold : Night.Surface2); face.BorderBrush = Hex(_railCollapsed ? Night.Gold : Night.Line); arrow.Stroke = Hex(_railCollapsed ? Night.OnGold : Night.Sub); };
        toggle.Click += (_, __) => ToggleRail(toggle, face, arrow);

        Grid.SetRowSpan(toggle, rows);
        page.Children.Add(toggle);
    }

    private void ToggleRail(Button toggle, Border face, Path arrow)
    {
        _railCollapsed = !_railCollapsed;
        var ease = new CubicEase { EasingMode = EasingMode.EaseInOut };
        var dur = new Duration(TimeSpan.FromMilliseconds(380));

        _railSlide?.BeginAnimation(TranslateTransform.XProperty, new DoubleAnimation(_railCollapsed ? -RailWidth : 0, dur) { EasingFunction = ease });
        // When hidden, the button rides out to a small inset from the screen's left edge so it stays easy to hit.
        _toggleSlide?.BeginAnimation(TranslateTransform.XProperty, new DoubleAnimation(_railCollapsed ? -(RailWidth - 31) : 0, dur) { EasingFunction = ease });
        _railArrow?.BeginAnimation(RotateTransform.AngleProperty, new DoubleAnimation(_railCollapsed ? 180 : 0, dur) { EasingFunction = ease });

        // Reflow every registered screen so the content fills the space the menu left behind (no dark strip).
        foreach (var (el, dp, baseT) in _railInsets)
        {
            var target = _railCollapsed ? new Thickness(Math.Max(0, baseT.Left - RailWidth), baseT.Top, baseT.Right, baseT.Bottom) : baseT;
            el.BeginAnimation(dp, new ThicknessAnimation(target, dur) { EasingFunction = ease });
        }

        // Gold, filled-in look while the menu is away (matches its hover state).
        face.Background = Hex(_railCollapsed ? Night.Gold : Night.Surface2);
        face.BorderBrush = Hex(_railCollapsed ? Night.Gold : Night.Line);
        arrow.Stroke = Hex(_railCollapsed ? Night.OnGold : Night.Sub);

        if (_railHost != null) _railHost.IsHitTestVisible = !_railCollapsed;   // the hidden menu shouldn't catch clicks
    }

    // A button template with no chrome, so only our round face shows.
    private static ControlTemplate BareButtonTemplate()
    {
        var presenter = new FrameworkElementFactory(typeof(ContentPresenter));
        presenter.SetValue(ContentPresenter.HorizontalAlignmentProperty, HorizontalAlignment.Center);
        presenter.SetValue(ContentPresenter.VerticalAlignmentProperty, VerticalAlignment.Center);
        return new ControlTemplate(typeof(Button)) { VisualTree = presenter };
    }
}
