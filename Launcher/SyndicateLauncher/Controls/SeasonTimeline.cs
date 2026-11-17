using System;
using System.Collections;
using System.Collections.Generic;
using System.Collections.Specialized;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Input;
using System.Windows.Media;
using System.Windows.Media.Animation;
using System.Windows.Media.Effects;
using System.Windows.Shapes;
using SyndicateLauncher.Models;

namespace SyndicateLauncher.Controls;

/// <summary>
/// Horizontal season timeline in the style of the Syndicate season browser: the selected card sits
/// full size in the middle, the others fan out, shrink, sink and fade with distance. Every card
/// follows one animated position value, so clicking, the wheel, the arrows and the slider all
/// move the same way.
/// </summary>
public sealed class SeasonTimeline : Canvas
{
    private const double CardWidth = 168;
    private const double CardHeight = 212;

    public static readonly DependencyProperty ItemsSourceProperty = DependencyProperty.Register(
        nameof(ItemsSource), typeof(IEnumerable), typeof(SeasonTimeline),
        new PropertyMetadata(null, (d, e) => ((SeasonTimeline)d).OnItemsSourceChanged(e.NewValue as IEnumerable)));

    public static readonly DependencyProperty SelectedItemProperty = DependencyProperty.Register(
        nameof(SelectedItem), typeof(object), typeof(SeasonTimeline),
        new FrameworkPropertyMetadata(null, FrameworkPropertyMetadataOptions.BindsTwoWayByDefault,
            (d, _) => ((SeasonTimeline)d).OnSelectionChanged()));

    private static readonly DependencyProperty PositionProperty = DependencyProperty.Register(
        nameof(Position), typeof(double), typeof(SeasonTimeline),
        new PropertyMetadata(0.0, (d, e) =>
        {
            var timeline = (SeasonTimeline)d;
            timeline.LayoutCards();
            timeline.PositionChanged?.Invoke((double)e.NewValue);
        }));

    /// <summary>Fires on every animation tick: the one position that drives the cards and the scrubber.</summary>
    public event Action<double>? PositionChanged;

    private sealed class CardVisual
    {
        public required Grid Root { get; init; }
        public required Border Ring { get; init; }
        public required TranslateTransform Offset { get; init; }
        public int ZIndex { get; set; } = int.MinValue;
        public required ScaleTransform Scale { get; init; }
        public required ScaleTransform Hover { get; init; }
        public required RotateTransform Rotate { get; init; }
        public required SeasonInfo Season { get; init; }
    }

    private readonly List<CardVisual> _cards = new();
    private INotifyCollectionChanged? _observed;
    private int _wheelAccumulator;
    private bool _ready;

    public SeasonTimeline()
    {
        ClipToBounds = true;
        Background = Brushes.Transparent;
        Focusable = true;
        FocusVisualStyle = null;
        Loaded += (_, _) => _ready = true;
        SizeChanged += (_, _) => LayoutCards();
        MouseWheel += OnWheel;
        KeyDown += OnKeyDown;
    }

    public IEnumerable? ItemsSource
    {
        get => (IEnumerable?)GetValue(ItemsSourceProperty);
        set => SetValue(ItemsSourceProperty, value);
    }

    public object? SelectedItem
    {
        get => GetValue(SelectedItemProperty);
        set => SetValue(SelectedItemProperty, value);
    }

    /// <summary>While true the timeline follows the scrubber continuously instead of animating to a season.</summary>
    public bool IsScrubbing { get; set; }

    /// <summary>Move the cards to a fractional position (0 = first card in the middle).</summary>
    public void ScrubTo(double position)
    {
        BeginAnimation(PositionProperty, null);
        Position = position;
        LayoutCards();
    }

    /// <summary>Glide from the current scrub position to the selected season.</summary>
    public void Settle()
    {
        int index = IndexOf(SelectedItem);
        if (index >= 0) AnimateTo(index);
    }

    private double Position
    {
        get => (double)GetValue(PositionProperty);
        set => SetValue(PositionProperty, value);
    }

    private int IndexOf(object? item)
    {
        if (item is null) return -1;
        for (int i = 0; i < _cards.Count; i++)
            if (ReferenceEquals(_cards[i].Season, item) || Equals(_cards[i].Season, item)) return i;
        return -1;
    }

    private void OnItemsSourceChanged(IEnumerable? source)
    {
        if (_observed is not null) _observed.CollectionChanged -= OnItemsChanged;
        _observed = source as INotifyCollectionChanged;
        if (_observed is not null) _observed.CollectionChanged += OnItemsChanged;
        Rebuild();
    }

    private void OnItemsChanged(object? sender, NotifyCollectionChangedEventArgs e) => Rebuild();

    private void Rebuild()
    {
        Children.Clear();
        _cards.Clear();
        if (ItemsSource is null) return;

        foreach (var item in ItemsSource)
        {
            if (item is not SeasonInfo season) continue;
            var card = BuildCard(season);
            _cards.Add(card);
            Children.Add(card.Root);
        }

        int index = Math.Max(0, IndexOf(SelectedItem));
        BeginAnimation(PositionProperty, null);
        Position = index;
        LayoutCards();
    }

    private CardVisual BuildCard(SeasonInfo season)
    {
        var scale = new ScaleTransform(1, 1);
        var hover = new ScaleTransform(1, 1);
        var rotate = new RotateTransform(0);
        var offset = new TranslateTransform();
        var transforms = new TransformGroup();
        transforms.Children.Add(scale);
        transforms.Children.Add(hover);
        transforms.Children.Add(rotate);
        transforms.Children.Add(offset);

        var media = new Grid();
        if (season.CardArt is not null)
        {
            // Fill the card and crop from the centre: zoomed in, but the subject stays centred.
            var fill = new Image
            {
                Source = season.CardArt,
                Stretch = Stretch.UniformToFill,
                HorizontalAlignment = HorizontalAlignment.Center,
                VerticalAlignment = VerticalAlignment.Center
            };
            RenderOptions.SetBitmapScalingMode(fill, BitmapScalingMode.HighQuality);
            media.Children.Add(fill);
        }
        else
        {
            media.Children.Add(new Border { Background = season.CoverBrush });
        }

        var shade = new Rectangle
        {
            IsHitTestVisible = false,
            Fill = new LinearGradientBrush(
                Color.FromArgb(0x00, 0, 0, 0), Color.FromArgb(0xE6, 0x05, 0x05, 0x05),
                new Point(0, 0.32), new Point(0, 1))
        };

        var chapter = new TextBlock
        {
            Text = season.ChapterText,
            FontSize = 7.5,
            FontWeight = FontWeights.Bold,
            Foreground = season.AccentSoftBrush
        };
        var label = new TextBlock
        {
            Text = season.Label,
            FontFamily = new FontFamily("Bahnschrift SemiCondensed, Segoe UI"),
            FontSize = 21,
            FontWeight = FontWeights.Bold,
            FontStyle = FontStyles.Italic,
            Foreground = season.TitleGradient,
            TextTrimming = TextTrimming.CharacterEllipsis
        };
        var theme = new TextBlock
        {
            Text = season.Tagline.ToUpperInvariant(),
            FontSize = 7.2,
            FontWeight = FontWeights.SemiBold,
            Foreground = season.AccentSoftBrush,
            Opacity = 0.85,
            TextTrimming = TextTrimming.CharacterEllipsis
        };

        var text = new StackPanel
        {
            VerticalAlignment = VerticalAlignment.Bottom,
            Margin = new Thickness(12, 0, 12, 12)
        };
        text.Children.Add(chapter);
        text.Children.Add(label);
        text.Children.Add(theme);

        var content = new Grid
        {
            // explicit rounded clip: a Border's CornerRadius does not clip its children
            Clip = new RectangleGeometry(new Rect(0, 0, CardWidth - 2, CardHeight - 2), 13, 13)
        };
        content.Children.Add(media);
        content.Children.Add(shade);
        content.Children.Add(text);

        var ring = new Border
        {
            CornerRadius = new CornerRadius(13),
            BorderBrush = season.AccentBrush,
            BorderThickness = new Thickness(1.6),
            Opacity = 0,
            IsHitTestVisible = false,
            Effect = new DropShadowEffect
            {
                Color = season.AccentColor,
                BlurRadius = 18,
                ShadowDepth = 0,
                Opacity = 0.4
            }
        };
        // The glow ring is drawn by its own cached layer, so fading it in and out never re-renders its shadow.
        ring.CacheMode = new BitmapCache(1.1) { EnableClearType = false };

        // The card body is rasterised once (BitmapCache) and then only moved, scaled and rotated on the GPU. Before,
        // every animation frame re-sampled each card's art in HighQuality and moved the cards with Canvas.Left/Top,
        // which ran a layout pass per frame: the Season Browser stuttered on every wheel step.
        var body = new Border
        {
            CornerRadius = new CornerRadius(14),
            BorderBrush = Frozen(new SolidColorBrush(Color.FromArgb(0x26, 255, 255, 255))),
            BorderThickness = new Thickness(1),
            Background = Frozen(new SolidColorBrush(Color.FromRgb(0x12, 0x12, 0x12))),
            ClipToBounds = true,
            Child = content,
            CacheMode = new BitmapCache(1.15) { EnableClearType = false, SnapsToDevicePixels = true }
        };

        var root = new Grid
        {
            Width = CardWidth,
            Height = CardHeight,
            RenderTransform = transforms,
            RenderTransformOrigin = new Point(0.5, 0.5),
            Cursor = Cursors.Hand,
            Background = Brushes.Transparent
        };
        root.Children.Add(body);
        root.Children.Add(ring);

        root.MouseLeftButtonDown += (_, e) =>
        {
            e.Handled = true;
            SelectedItem = season;
            Focus();
        };
        root.MouseEnter += (_, _) => AnimateHover(hover, 1.055, 150);
        root.MouseLeave += (_, _) => AnimateHover(hover, 1.0, 180);

        return new CardVisual
        {
            Root = root,
            Ring = ring,
            Offset = offset,
            Scale = scale,
            Hover = hover,
            Rotate = rotate,
            Season = season
        };
    }

    private static T Frozen<T>(T freezable) where T : Freezable
    {
        freezable.Freeze();
        return freezable;
    }

    private static void AnimateHover(ScaleTransform transform, double target, int ms)
    {
        var animation = new DoubleAnimation(target, TimeSpan.FromMilliseconds(ms))
        {
            EasingFunction = new CubicEase { EasingMode = EasingMode.EaseOut }
        };
        transform.BeginAnimation(ScaleTransform.ScaleXProperty, animation);
        transform.BeginAnimation(ScaleTransform.ScaleYProperty, animation);
    }

    private void OnSelectionChanged()
    {
        if (IsScrubbing) return;
        int index = IndexOf(SelectedItem);
        if (index < 0) return;

        // First selection (bindings resolving on load) should place the timeline, not fly it in.
        if (!_ready)
        {
            BeginAnimation(PositionProperty, null);
            Position = index;
            LayoutCards();
            return;
        }

        AnimateTo(index);
    }

    private void AnimateTo(int index)
    {
        double current = Position;
        if (Math.Abs(current - index) < 0.001) return;

        // Start from the current animated position so rapid wheel steps stay continuous
        // instead of jumping back to the previous target.
        var animation = new DoubleAnimation(current, index, TimeSpan.FromMilliseconds(430))
        {
            EasingFunction = new CubicEase { EasingMode = EasingMode.EaseOut }
        };
        BeginAnimation(PositionProperty, animation);
    }

    private void LayoutCards()
    {
        if (_cards.Count == 0 || ActualWidth <= 0 || ActualHeight <= 0) return;

        double centerX = ActualWidth / 2;
        double centerY = ActualHeight / 2;
        double spacing = Math.Min(154, Math.Max(104, ActualWidth / 6.2));

        for (int i = 0; i < _cards.Count; i++)
        {
            var card = _cards[i];
            double delta = i - Position;
            double distance = Math.Abs(delta);
            double scale = 1.0 / (1.0 + distance * 0.20);
            double x = centerX + delta * spacing;
            double y = centerY + distance * 12;

            card.Offset.X = x - CardWidth / 2;
            card.Offset.Y = y - CardHeight / 2;
            card.Scale.ScaleX = scale;
            card.Scale.ScaleY = scale;
            card.Rotate.Angle = Math.Clamp(delta * 3.2, -15, 15);
            card.Root.Opacity = Math.Clamp(1.0 - distance * 0.30, 0.16, 1.0);
            int z = 1000 - (int)Math.Round(distance * 100);
            if (z != card.ZIndex) { card.ZIndex = z; SetZIndex(card.Root, z); }
            double ring = Math.Clamp(1.0 - distance * 2.6, 0, 1);
            card.Ring.Opacity = ring;
            var ringVisibility = ring > 0.001 ? Visibility.Visible : Visibility.Hidden;
            if (card.Ring.Visibility != ringVisibility) card.Ring.Visibility = ringVisibility;
        }
    }

    private void OnWheel(object sender, MouseWheelEventArgs e)
    {
        e.Handled = true;
        int index = IndexOf(SelectedItem);
        if (index < 0) return;

        // Accumulate small/touchpad deltas so one notch always moves exactly one season.
        if (Math.Sign(e.Delta) != Math.Sign(_wheelAccumulator)) _wheelAccumulator = 0;
        _wheelAccumulator += e.Delta;
        if (Math.Abs(_wheelAccumulator) < 120) return;
        int steps = _wheelAccumulator / 120;
        _wheelAccumulator -= steps * 120;

        int next = Math.Clamp(index - steps, 0, _cards.Count - 1);
        if (next == index) return;
        SelectedItem = _cards[next].Season;
    }

    private void OnKeyDown(object sender, KeyEventArgs e)
    {
        int index = IndexOf(SelectedItem);
        if (index < 0) return;
        if (e.Key == Key.Left) index = Math.Max(0, index - 1);
        else if (e.Key == Key.Right) index = Math.Min(_cards.Count - 1, index + 1);
        else return;
        SelectedItem = _cards[index].Season;
        e.Handled = true;
    }
}
