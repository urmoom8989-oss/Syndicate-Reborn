using System;
using System.ComponentModel;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Threading.Tasks;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Controls.Primitives;
using System.Windows.Input;
using System.Windows.Media;
using System.Windows.Media.Animation;
using System.Windows.Media.Effects;
using Shape = System.Windows.Shapes.Shape;
using SyndicateLauncher.ViewModels;
using SyndicateLauncher.Services;
using SyndicateLauncher.Models;

namespace SyndicateLauncher;

public partial class MainWindow : Window
{
    private readonly MainViewModel _model;
    private bool _scrubbing;

    public MainWindow() : this(new MainViewModel()) { }

    public MainWindow(MainViewModel model)
    {
        StartupTrace.Mark("view model built");
        InitializeComponent();
        StartupTrace.Mark("xaml loaded");
        DataContext = model;
        _model = model;
        model.PropertyChanged += OnModelPropertyChanged;
        model.LogAdded += OnDiagnosticsLog;
        model.ProblemReported += ShowProblem;
        model.ProblemsResolved += ResolveProblems;
        AddDiagnosticsLine("Syndicate launcher ready.");
        BrowserTimeline.PositionChanged += OnTimelinePositionChanged;
        BrowserSlider.AddHandler(Thumb.DragStartedEvent, new DragStartedEventHandler(OnSliderDragStarted));
        BrowserSlider.AddHandler(Thumb.DragDeltaEvent, new DragDeltaEventHandler(OnSliderDragDelta));
        BrowserSlider.AddHandler(Thumb.DragCompletedEvent, new DragCompletedEventHandler(OnSliderDragCompleted));
        Loaded += OnLoaded;
        Closing += (_, _) => { _model.Themes.FlushSave(); DiscordPresence.Stop(); };
        // Discord status runs on its own background task and only reads files; it can't slow the launcher or a match.
        ContentRendered += (_, _) => _model.StartDiscordPresence();
        model.Themes.PropertyChanged += OnThemesPropertyChanged;
        ThemeService.Changed += OnThemeChanged;
        Closed += (_, _) => ThemeService.Changed -= OnThemeChanged;
        InputMods.Start();
#if DEV_BUILD
        DevBadge.Visibility = Visibility.Visible;
        StudioNavButton.Visibility = Visibility.Visible;
        Title = "Syndicate Launcher (DEV)";
#endif
    }

    // ---------------------------------------------------------------- Syndicate Studio
    // The Studio page (sidebar Studio button, dev launcher) opens the Studio window. One window at a time. Its Start
    // runs this launcher's Play for the season picked in the Studio, without SyndicateMenu.dll; while it is open, every
    // Play starts the match server with the Studio's options (Studio/StudioSession.cs).
    private Studio.StudioWindow? _studio;
    private bool _studioOpening;
    private async void OnOpenStudio(object sender, RoutedEventArgs e)
    {
        if (_studio is not null)
        {
            if (_studio.WindowState == WindowState.Minimized) _studio.WindowState = WindowState.Normal;
            _studio.Activate();
            return;
        }
        if (_studioOpening) return;
        _studioOpening = true;
        try
        {
            await PlayStudioLaunchAnimationAsync();
            var seasons = _model.StudioSeasons
                .Select(s => (s.Key, Label: s.Name + "  (" + s.BuildVersion + ")" + (_model.HasBuildFolder(s) ? "" : "  - no folder yet"), Build: s.BuildVersion))
                .ToList();
            _studio = new Studio.StudioWindow(
                () => SyndicateBackend.RuntimeDirectory is { } server ? new Studio.LauncherPaths(server, _model.Build?.Version) : null,
                () => _model.SelectedProfile?.Name ?? "",
                message => AddDiagnosticsLine(message),
                seasons, _model.ActiveSeasonKey,
                (season, status) => _model.StartStudioAsync(season, status));
            _studio.Closed += (_, _) => { _studio = null; Studio.StudioSession.Clear(); };
            _studio.Show();
        }
        finally
        {
            _studioOpening = false;
            ResetStudioPage();
        }
    }

    // Page in: the key art eases back from a slight push-in while the copy lifts in.
    private void AnimateStudioPage()
    {
        if (!ThemeService.MotionAllowed) { ResetStudioPage(); return; }
        var ease = new CubicEase { EasingMode = EasingMode.EaseOut };
        foreach (var p in new[] { ScaleTransform.ScaleXProperty, ScaleTransform.ScaleYProperty })
            StudioArtZoom.BeginAnimation(p, new DoubleAnimation(1.12, 1.04, TimeSpan.FromMilliseconds(900)) { EasingFunction = ease });
        StudioFlash.BeginAnimation(OpacityProperty, null); StudioFlash.Opacity = 0;
        AnimateCardIn(StudioCopy, 120);
    }

    // OPEN STUDIO: the camera pushes into the scene with a soft flash while the copy fades, then the Studio opens.
    private async System.Threading.Tasks.Task PlayStudioLaunchAnimationAsync()
    {
        if (!ThemeService.MotionAllowed) return;
        var ease = new CubicEase { EasingMode = EasingMode.EaseIn };
        foreach (var p in new[] { ScaleTransform.ScaleXProperty, ScaleTransform.ScaleYProperty })
            StudioArtZoom.BeginAnimation(p, new DoubleAnimation(1.04, 1.14, TimeSpan.FromMilliseconds(420)) { EasingFunction = ease });
        StudioCopy.BeginAnimation(OpacityProperty, new DoubleAnimation(1, 0, TimeSpan.FromMilliseconds(260)));
        var flash = new DoubleAnimationUsingKeyFrames();
        flash.KeyFrames.Add(new LinearDoubleKeyFrame(0, KeyTime.FromTimeSpan(TimeSpan.FromMilliseconds(200))));
        flash.KeyFrames.Add(new EasingDoubleKeyFrame(0.28, KeyTime.FromTimeSpan(TimeSpan.FromMilliseconds(400))) { EasingFunction = new CubicEase { EasingMode = EasingMode.EaseIn } });
        flash.KeyFrames.Add(new EasingDoubleKeyFrame(0, KeyTime.FromTimeSpan(TimeSpan.FromMilliseconds(700))) { EasingFunction = new CubicEase { EasingMode = EasingMode.EaseOut } });
        StudioFlash.BeginAnimation(OpacityProperty, flash);
        await System.Threading.Tasks.Task.Delay(430);
    }

    // Back to rest after the window opens, so the page looks right when you come back to it.
    private void ResetStudioPage()
    {
        StudioArtZoom.BeginAnimation(ScaleTransform.ScaleXProperty, null);
        StudioArtZoom.BeginAnimation(ScaleTransform.ScaleYProperty, null);
        StudioArtZoom.ScaleX = StudioArtZoom.ScaleY = 1.04;
        StudioCopy.BeginAnimation(OpacityProperty, null);
        StudioCopy.Opacity = 1;
        if (StudioCopy.RenderTransform is TranslateTransform slide) { slide.BeginAnimation(TranslateTransform.YProperty, null); slide.Y = 0; }
    }

    /// <summary>The startup splash (App.OnStartup). OnLoaded hands over from it once the first frame is drawn.</summary>
    internal LaunchSplash? Splash { get; set; }

    private async void OnLoaded(object sender, RoutedEventArgs e)
    {
        SelectSettingsPage(0);
        OnThemeChanged();
        Closed += (_, _) => _model.StopControllers();
        var splash = Splash;
        Splash = null;
        StartupTrace.Mark("main loaded");
        StartupTrace.WatchFrames("main");
        if (FrameMonitor.Enabled)
        {
            // SYNDICATE_FRAME_TRACE=1: frame pacing for the whole session, page by page (FrameMonitor.cs).
            void TrackPage() => FrameMonitor.Page = _model.IsSettingsOpen ? "Settings" : _model.CurrentScreen.ToString();
            TrackPage();
            _model.PropertyChanged += (_, e) =>
            {
                if (e.PropertyName is nameof(MainViewModel.CurrentScreen) or nameof(MainViewModel.IsSettingsOpen)) TrackPage();
            };
            FrameMonitor.Start();
        }
        if (StartupTrace.Enabled)
            _ = Task.Delay(9000).ContinueWith(_ => StartupTrace.Flush());

        // Keep the expensive background work out of the hand-over. The hero drift, theme effects
        // and controller navigation start once the launcher is showing.
        SetLaunchEffectsSuspended(true);
        bool revealed = false;
        try
        {
            if (splash is not null)
            {
                // The launcher is drawn (under the splash) before the splash leaves, so nothing pops in.
                await FirstFrameAsync();
                StartupTrace.Mark("main first frame");
                if (!IsLoaded) return;
                // Let the work queued behind the first frame (bindings, images, layout) finish before the outro
                // starts, so none of it lands in the middle of the hand-over. Capped.
                await Task.WhenAny(Dispatcher.InvokeAsync(() => { }, System.Windows.Threading.DispatcherPriority.ApplicationIdle).Task,
                    Task.Delay(400));
                if (!IsLoaded) return;
                PrepareReveal();
                // Take focus now, still hidden under the (topmost) splash: activating changes focus and window state,
                // which cost the reveal's first frame when done at the hand-over.
                Activate();
                // The splash plays out its intro, fades to the plain background and closes. Capped, so a stuck
                // splash can never keep the launcher hidden.
                var finished = splash.FinishAsync();
                if (await Task.WhenAny(finished, Task.Delay(6000)) != finished) splash.Close();
                if (!IsLoaded) return;
            }

            AnimateScreenIn(fadeIn: false);
            AnimateChapterCards();
            revealed = true;
            if (ThemeService.MotionAllowed)
            {
                // Mirrors the splash fade (LaunchSplash.Finish): it starts at speed where the splash fade ended and
                // slows down, at the display's refresh rate, so the hand-over is one continuous movement.
                var fade = new DoubleAnimation(1, 0, TimeSpan.FromMilliseconds(SplashWindow.OutroMs))
                {
                    EasingFunction = new QuadraticEase { EasingMode = EasingMode.EaseOut }
                };
                Timeline.SetDesiredFrameRate(fade, LaunchSplash.DisplayRefreshRate);
                var faded = new TaskCompletionSource();
                fade.Completed += (_, _) => faded.TrySetResult();
                StartupTrace.Mark("cover fade");
                LaunchCover.BeginAnimation(OpacityProperty, fade);
                await Task.WhenAny(faded.Task, Task.Delay(SplashWindow.OutroMs + 300));
            }
        }
        catch (Exception ex)
        {
            splash?.Close();
            AddDiagnosticsLine($"Opening animation skipped: {ex.Message}");
        }
        finally
        {
            StartupTrace.Mark("shell start");
            LaunchCover.Visibility = Visibility.Collapsed;
            SetLaunchEffectsSuspended(false);
            if (revealed && ThemeService.MotionAllowed)
            {
                // The theme effects ease in instead of appearing all at once at the end of the reveal.
                foreach (var layer in new UIElement[] { ThemeBackdrop, ThemeOverlay })
                    layer.BeginAnimation(OpacityProperty, Fade(0, 1, 600, 0, new QuadraticEase { EasingMode = EasingMode.EaseOut }));
            }
            if (IsLoaded)
            {
                StartLauncherShell();
                StartupTrace.Mark("idle");
                if (!revealed)
                {
                    AnimateScreenIn();
                    AnimateChapterCards();
                }
            }
        }
    }

    // Completes when the window has drawn its first frame (or after a short cap).
    private Task FirstFrameAsync()
    {
        var drawn = new TaskCompletionSource();
        EventHandler? handler = null;
        handler = (_, _) =>
        {
            ContentRendered -= handler;
            drawn.TrySetResult();
        };
        ContentRendered += handler;
        return Task.WhenAny(drawn.Task, Task.Delay(1500));
    }

    private void StartLauncherShell()
    {
        if (ThemeService.MotionAllowed)
        {
            // perf-switches.txt next to the exe (FrameMonitor.Off) can hold these back while measuring frame times.
            if (!FrameMonitor.Off("no-logo-pulse")) StartLogoPulse();
        }
        // Controllers: PS4 / PS5 pads in game (Settings > Mods > Controller) and moving around the launcher with a pad.
        _model.StartControllers();
        StartControllerNavigation();
    }

    private void SetLaunchEffectsSuspended(bool suspended)
    {
        ThemeBackdrop.Suspended = suspended;
        ThemeOverlay.Suspended = suspended;
        LobbyArt.Suspended = suspended;
    }

    private static DoubleAnimation Fade(double from, double to, int milliseconds, int delayMilliseconds = 0,
        IEasingFunction? easing = null)
    {
        var animation = new DoubleAnimation(from, to, TimeSpan.FromMilliseconds(milliseconds))
        {
            BeginTime = TimeSpan.FromMilliseconds(delayMilliseconds)
        };
        if (easing is not null) animation.EasingFunction = easing;
        Timeline.SetDesiredFrameRate(animation, 30);
        return animation;
    }

    // ------------------------------------------------------------------ Themes tab

    /// <summary>Keeps the bits that can't use a DynamicResource (the logo glow) on the theme's accent.</summary>
    private void OnThemeChanged()
    {
        if (NavLogoGlow is not null) NavLogoGlow.Color = ThemeService.Target.Accent;
    }

    private void OnThemesPropertyChanged(object? sender, PropertyChangedEventArgs e)
    {
        if (e.PropertyName == nameof(ThemesViewModel.IsEditorOpen) && _model.Themes.IsEditorOpen)
            Dispatcher.BeginInvoke(new Action(AnimateThemeEditor));
        else if (e.PropertyName == nameof(ThemesViewModel.HasCustomThemes))
            Dispatcher.BeginInvoke(new Action(() => AnimateThemeList(CustomThemeList, 0)),
                System.Windows.Threading.DispatcherPriority.Loaded);
    }

    /// <summary>Theme cards rise in one after another, and the effects panel slides in from the right.</summary>
    private void AnimateThemesPage()
    {
        if (SettingsPageThemes is null) return;
        int delay = AnimateThemeList(SignatureThemeList, 60);
        delay = AnimateThemeList(ColourThemeList, delay);
        AnimateThemeList(CustomThemeList, delay);
        if (ThemesEffectsPanel is null) return;
        if (!ThemeService.MotionAllowed) { ThemesEffectsPanel.BeginAnimation(OpacityProperty, null); ThemesEffectsPanel.Opacity = 1; return; }
        if (ThemesEffectsPanel.RenderTransform is not TranslateTransform slide)
            ThemesEffectsPanel.RenderTransform = slide = new TranslateTransform();
        var ease = new CubicEase { EasingMode = EasingMode.EaseOut };
        ThemesEffectsPanel.Opacity = 0;
        ThemesEffectsPanel.BeginAnimation(OpacityProperty,
            new DoubleAnimation(0, 1, TimeSpan.FromMilliseconds(320)) { BeginTime = TimeSpan.FromMilliseconds(120), EasingFunction = ease });
        slide.BeginAnimation(TranslateTransform.XProperty,
            new DoubleAnimation(36, 0, TimeSpan.FromMilliseconds(460)) { BeginTime = TimeSpan.FromMilliseconds(120), EasingFunction = ease });
    }

    private static int AnimateThemeList(ItemsControl? list, int delay)
    {
        if (list is null) return delay;
        for (int i = 0; i < list.Items.Count; i++)
        {
            if (list.ItemContainerGenerator.ContainerFromIndex(i) is not UIElement card) continue;
            AnimateCardIn(card, delay);
            delay += 28;
        }
        return delay;
    }

    private void AnimateThemeEditor()
    {
        if (ThemeEditorSheet is null || !ThemeService.MotionAllowed) return;
        var ease = new BackEase { EasingMode = EasingMode.EaseOut, Amplitude = 0.35 };
        ThemeEditorSheet.Opacity = 0;
        ThemeEditorSheet.BeginAnimation(OpacityProperty, new DoubleAnimation(0, 1, TimeSpan.FromMilliseconds(200)));
        ThemeEditorScale.BeginAnimation(ScaleTransform.ScaleXProperty, new DoubleAnimation(0.94, 1, TimeSpan.FromMilliseconds(340)) { EasingFunction = ease });
        ThemeEditorScale.BeginAnimation(ScaleTransform.ScaleYProperty, new DoubleAnimation(0.94, 1, TimeSpan.FromMilliseconds(340)) { EasingFunction = ease });
        ThemeEditorSlide.BeginAnimation(TranslateTransform.YProperty,
            new DoubleAnimation(22, 0, TimeSpan.FromMilliseconds(340)) { EasingFunction = new CubicEase { EasingMode = EasingMode.EaseOut } });
    }

    private void OnModelPropertyChanged(object? sender, PropertyChangedEventArgs e)
    {
        if (e.PropertyName == nameof(MainViewModel.CurrentScreen))
        {
            Dispatcher.BeginInvoke(new Action(() =>
            {
                AnimateScreenIn();
                if (_model.IsHomeScreen) AnimateChapterCards();
                if (_model.IsStudioScreen) AnimateStudioPage();
            }), System.Windows.Threading.DispatcherPriority.Loaded);
        }
        else if (e.PropertyName == nameof(MainViewModel.BrowserSeason))
        {
            Dispatcher.BeginInvoke(new Action(AnimateBrowserDetails));
        }
        else if (e.PropertyName == nameof(MainViewModel.IsSettingsOpen))
        {
            Dispatcher.BeginInvoke(new Action(AnimateSettings));
        }
        else if (e.PropertyName == nameof(MainViewModel.IsPlayerSelectOpen))
        {
            Dispatcher.BeginInvoke(new Action(AnimatePlayerSelect));
        }
        else if (e.PropertyName == nameof(MainViewModel.ProblemMessage) &&
                 !string.IsNullOrWhiteSpace(_model.ProblemMessage))
        {
            ShowBuildProblem(_model.ProblemMessage);
        }
        else if (e.PropertyName == nameof(MainViewModel.Build) && _model.Build is not null)
        {
            if (string.IsNullOrWhiteSpace(_model.ProblemMessage)) ResolveBuildProblems();
            OnDiagnosticsLog($"Detected Fortnite {_model.Build.Version} ({_model.Build.SourceLabel.ToLowerInvariant()}).");
        }
    }

    private void AnimatePlayerSelect()
    {
        if (!_model.IsPlayerSelectOpen) return;
        var ease = new CubicEase { EasingMode = EasingMode.EaseOut };
        PlayerSelectRoot.Opacity = 0;
        PlayerSelectScale.ScaleX = PlayerSelectScale.ScaleY = 0.96;
        PlayerSelectSlide.Y = 28;
        PlayerSelectScribble.StrokeDashOffset = 300;

        PlayerSelectRoot.BeginAnimation(OpacityProperty,
            new DoubleAnimation(0, 1, TimeSpan.FromMilliseconds(180)));
        PlayerSelectScale.BeginAnimation(ScaleTransform.ScaleXProperty,
            new DoubleAnimation(0.96, 1, TimeSpan.FromMilliseconds(300)) { EasingFunction = ease });
        PlayerSelectScale.BeginAnimation(ScaleTransform.ScaleYProperty,
            new DoubleAnimation(0.96, 1, TimeSpan.FromMilliseconds(300)) { EasingFunction = ease });
        PlayerSelectSlide.BeginAnimation(TranslateTransform.YProperty,
            new DoubleAnimation(28, 0, TimeSpan.FromMilliseconds(320)) { EasingFunction = ease });
        PlayerSelectScribble.BeginAnimation(Shape.StrokeDashOffsetProperty,
            new DoubleAnimation(300, 0, TimeSpan.FromMilliseconds(700))
            {
                BeginTime = TimeSpan.FromMilliseconds(120),
                EasingFunction = ease
            });
    }

    private void AnimateSettings()
    {
        if (!_model.IsSettingsOpen || SettingsSheet is null) return;
        SettingsSheet.Opacity = 0;
        SettingsSlide.X = 46;
        SettingsSheet.BeginAnimation(OpacityProperty,
            new DoubleAnimation(0, 1, TimeSpan.FromMilliseconds(220))
            {
                EasingFunction = new CubicEase { EasingMode = EasingMode.EaseOut }
            });
        SettingsSlide.BeginAnimation(TranslateTransform.XProperty,
            new DoubleAnimation(46, 0, TimeSpan.FromMilliseconds(320))
            {
                EasingFunction = new CubicEase { EasingMode = EasingMode.EaseOut }
            });
    }

    private void StartLogoPulse()
    {
        // A slow glow pulse: 15 frames a second is plenty and keeps the shadow effect from redrawing at 60.
        var pulse = new DoubleAnimation(0.18, 0.42, TimeSpan.FromSeconds(2.4))
        {
            AutoReverse = true,
            RepeatBehavior = RepeatBehavior.Forever,
            EasingFunction = new SineEase { EasingMode = EasingMode.EaseInOut }
        };
        Timeline.SetDesiredFrameRate(pulse, (int)Controls.AmbientClock.SlowFps);
        // Start where the eased pulse passes the glow's resting opacity (XAML), so it doesn't jump when it starts.
        double rest = Math.Clamp((NavLogoGlow.Opacity - 0.18) / (0.42 - 0.18), 0, 1);
        var clock = pulse.CreateClock();
        NavLogoGlow.ApplyAnimationClock(DropShadowEffect.OpacityProperty, clock);
        clock.Controller?.Seek(TimeSpan.FromSeconds(2.4 * Math.Acos(1 - 2 * rest) / Math.PI), TimeSeekOrigin.BeginTime);
        _ambientClocks.Add(clock);
    }

    // fadeIn: false at startup, where the launcher's cover fade already fades the page in (a second fade under it
    // kept the middle of the screen blank for the first frames of the reveal); the page still lifts in.
    private void AnimateScreenIn(bool fadeIn = true)
    {
        if (ContentHost is null) return;
        if (!ThemeService.MotionAllowed)
        {
            ContentHost.BeginAnimation(OpacityProperty, null);
            ContentHost.Opacity = 1;
            return;
        }

        var (scale, slide) = ScreenInTransforms();

        // Syndicate-style menu change: fade in while lifting and easing out of a slight zoom.
        slide.Y = 24;
        scale.ScaleX = 0.99;
        scale.ScaleY = 0.99;

        // Explicit From values: a previous animation holds its end value, so a To-only
        // animation would start (and end) at 1 and never be seen.
        if (fadeIn)
        {
            ContentHost.Opacity = 0;
            ContentHost.BeginAnimation(OpacityProperty,
                new DoubleAnimation(0, 1, TimeSpan.FromMilliseconds(280))
                {
                    EasingFunction = new CubicEase { EasingMode = EasingMode.EaseOut }
                });
        }
        else
        {
            ContentHost.BeginAnimation(OpacityProperty, null);
            ContentHost.Opacity = 1;
        }
        slide.BeginAnimation(TranslateTransform.YProperty,
            new DoubleAnimation(24, 0, TimeSpan.FromMilliseconds(380))
            {
                EasingFunction = new CubicEase { EasingMode = EasingMode.EaseOut }
            });
        scale.BeginAnimation(ScaleTransform.ScaleXProperty,
            new DoubleAnimation(0.99, 1, TimeSpan.FromMilliseconds(380))
            {
                EasingFunction = new CubicEase { EasingMode = EasingMode.EaseOut }
            });
        scale.BeginAnimation(ScaleTransform.ScaleYProperty,
            new DoubleAnimation(0.99, 1, TimeSpan.FromMilliseconds(380))
            {
                EasingFunction = new CubicEase { EasingMode = EasingMode.EaseOut }
            });
    }

    private (ScaleTransform Scale, TranslateTransform Slide) ScreenInTransforms()
    {
        if (ContentHost.RenderTransform is TransformGroup existing &&
            existing.Children.Count >= 2 &&
            existing.Children[0] is ScaleTransform existingScale &&
            existing.Children[1] is TranslateTransform existingSlide)
            return (existingScale, existingSlide);
        var group = new TransformGroup();
        var scale = new ScaleTransform(1, 1);
        var slide = new TranslateTransform();
        group.Children.Add(scale);
        group.Children.Add(slide);
        ContentHost.RenderTransform = group;
        ContentHost.RenderTransformOrigin = new Point(0.5, 0.5);
        return (scale, slide);
    }

    // Puts the page in the first frame of its startup reveal (AnimateScreenIn and AnimateChapterCards start from
    // exactly here) while the splash still covers it, so the render thread draws that state before the hand-over
    // instead of in the reveal's first frame, which made that frame late.
    private void PrepareReveal()
    {
        if (!ThemeService.MotionAllowed || ContentHost is null) return;
        var (scale, slide) = ScreenInTransforms();
        slide.Y = 24;
        scale.ScaleX = scale.ScaleY = 0.99;
        foreach (var element in HomePanels())
        {
            if (element is null) continue;
            if (element.RenderTransform is not TranslateTransform lift) element.RenderTransform = lift = new TranslateTransform();
            lift.Y = 18;
            element.Opacity = 0;
        }
    }

    private UIElement?[] HomePanels() =>
        HomeDashboard is null ? [] : [HomeDashboard.Hub, HomeDashboard.StatsRow, HomeDashboard.AchievementsRow];

    /// <summary>Home's reveal: the hub, the stats row and the achievements strip rise in one after another.</summary>
    private void AnimateChapterCards()
    {
        var panels = HomePanels();
        for (int i = 0; i < panels.Length; i++) AnimateCardIn(panels[i], i * 70);
    }

    private static void AnimateCardIn(UIElement? element, int delayMs)
    {
        if (element is null) return;
        if (!ThemeService.MotionAllowed)
        {
            element.BeginAnimation(OpacityProperty, null);
            element.Opacity = 1;
            return;
        }

        if (element.RenderTransform is not TranslateTransform slide)
        {
            slide = new TranslateTransform();
            element.RenderTransform = slide;
        }

        var begin = TimeSpan.FromMilliseconds(delayMs);
        element.Opacity = 0;
        slide.Y = 18;
        element.BeginAnimation(OpacityProperty,
            new DoubleAnimation(0, 1, TimeSpan.FromMilliseconds(260))
            {
                BeginTime = begin,
                EasingFunction = new CubicEase { EasingMode = EasingMode.EaseOut }
            });
        slide.BeginAnimation(TranslateTransform.YProperty,
            new DoubleAnimation(18, 0, TimeSpan.FromMilliseconds(340))
            {
                BeginTime = begin,
                EasingFunction = new CubicEase { EasingMode = EasingMode.EaseOut }
            });
    }

    private void AnimateBrowserDetails()
    {
        if (BrowserText is null) return;

        if (BrowserText.RenderTransform is not TranslateTransform slide)
        {
            slide = new TranslateTransform();
            BrowserText.RenderTransform = slide;
        }

        BrowserText.Opacity = 0;
        slide.X = 14;
        BrowserText.BeginAnimation(OpacityProperty,
            new DoubleAnimation(0, 1, TimeSpan.FromMilliseconds(220))
            {
                EasingFunction = new CubicEase { EasingMode = EasingMode.EaseOut }
            });
        slide.BeginAnimation(TranslateTransform.XProperty,
            new DoubleAnimation(14, 0, TimeSpan.FromMilliseconds(300))
            {
                EasingFunction = new CubicEase { EasingMode = EasingMode.EaseOut }
            });

        if (BrowserArtCard is not null)
        {
            BrowserArtCard.Opacity = 0.35;
            BrowserArtCard.BeginAnimation(OpacityProperty,
                new DoubleAnimation(0.35, 1, TimeSpan.FromMilliseconds(320))
                {
                    EasingFunction = new CubicEase { EasingMode = EasingMode.EaseOut }
                });
        }
    }

    /// <summary>The scrubber follows the timeline's single animated position, exactly like Syndicate.</summary>
    private void OnTimelinePositionChanged(double position)
    {
        if (BrowserSlider is null || _scrubbing) return;

        BrowserSlider.BeginAnimation(Slider.ValueProperty, null);
        BrowserSlider.SetCurrentValue(Slider.ValueProperty, position);
    }

    private void OnSliderDragStarted(object sender, DragStartedEventArgs e)
    {
        _scrubbing = true;
        BrowserTimeline.IsScrubbing = true;
        BrowserSlider.BeginAnimation(Slider.ValueProperty, null);
    }

    private void OnSliderDragDelta(object sender, DragDeltaEventArgs e)
    {
        if (!_scrubbing) return;
        BrowserTimeline.ScrubTo(BrowserSlider.Value);
        _model.BrowserSliderValue = BrowserSlider.Value;
    }

    private void OnSliderDragCompleted(object sender, DragCompletedEventArgs e)
    {
        _scrubbing = false;
        BrowserTimeline.IsScrubbing = false;
        BrowserTimeline.Settle();
    }

    private void PlayerList_KeyDown(object sender, KeyEventArgs e)
    {
        if (e.Key == Key.Delete && sender is ListBox { SelectedItem: Profile profile })
        {
            ConfirmDeleteProfile(profile);
            e.Handled = true;
        }
    }

    private void DeleteProfileMenuItem_Click(object sender, RoutedEventArgs e)
    {
        if (sender is MenuItem { CommandParameter: Profile profile })
            ConfirmDeleteProfile(profile);
    }

    // The same confirmation step as the Delete button (MainViewModel.Players.cs): it says what is deleted and asks
    // for the player's name. The old Yes/No box deleted only the launcher profile and left the game save behind.
    private void ConfirmDeleteProfile(Profile profile) => _model.BeginDeletePlayer(profile);

    private void PlayerSelectBackdrop_MouseLeftButtonDown(object sender, MouseButtonEventArgs e)
    {
        if (e.OriginalSource == sender) _model.IsPlayerSelectOpen = false;
    }

    private void ModOptionsToggle_Click(object sender, RoutedEventArgs e)
    {
        if (sender is not Button { Tag: Border options } button) return;
        bool show = options.Visibility != Visibility.Visible;
        options.Visibility = show ? Visibility.Visible : Visibility.Collapsed;
        if (button.Content is TextBlock glyph) glyph.Text = show ? "\uE70E" : "\uE70D";
        button.ToolTip = show ? "Hide options" : "Show options";
    }

    private void EditOptionsToggle_Click(object sender, RoutedEventArgs e)
    {
        bool show = EditOptions.Visibility != Visibility.Visible;
        EditOptions.Visibility = show ? Visibility.Visible : Visibility.Collapsed;
        EditOptionsArrowGlyph.Text = show ? "\uE70E" : "\uE70D";
        EditOptionsArrow.ToolTip = show ? "Hide edit on release options" : "Show edit on release options";
    }

    private void SettingsNav_Checked(object sender, RoutedEventArgs e)
    {
        if (sender is RadioButton { Tag: not null } radio &&
            int.TryParse(radio.Tag.ToString(), out int index))
            SelectSettingsPage(index);
    }

    private void SelectSettingsPage(int index)
    {
        if (SettingsPageMatch is null) return;

        var pages = new FrameworkElement[]
        {
            SettingsPageMatch, SettingsPageMods, SettingsPageDisplay, SettingsPageFiles, SettingsPageThemes
        };
        index = Math.Clamp(index, 0, pages.Length - 1);
        for (int i = 0; i < pages.Length; i++)
            pages[i].Visibility = i == index ? Visibility.Visible : Visibility.Collapsed;

        string[] titles = { "MATCH & BOTS", "MODS", "DISPLAY", "LAUNCHER & FILES", "THEMES" };
        string[] details =
        {
            "Your local match: how many bots, how hard they are and what they're called. Changes apply to the next match.",
            "Extras that go beyond the original game, loaded with the next match.",
            "Your custom crosshair, field of view, stretched resolution and extra launch flags.",
            "Your game folder, Syndicate's local files and version information.",
            "Colours, your own themes and live effects. Saved on this PC and applied every time Syndicate opens."
        };

        SettingsPageTitle.Text = titles[index];
        SettingsPageDetail.Text = details[index];
        SettingsPageIndex.Text = $"{index + 1:00}  /  {pages.Length:00}";
        AnimateSettingsPage(pages[index]);
        if (pages[index] == SettingsPageThemes && SettingsPageThemes.IsLoaded)
            Dispatcher.BeginInvoke(new Action(AnimateThemesPage), System.Windows.Threading.DispatcherPriority.Loaded);
    }

    private void AnimateSettingsPage(FrameworkElement page)
    {
        if (!page.IsLoaded) return;

        if (page.RenderTransform is not TranslateTransform slide)
        {
            slide = new TranslateTransform();
            page.RenderTransform = slide;
        }

        page.Opacity = 0;
        slide.X = -18;
        page.BeginAnimation(OpacityProperty,
            new DoubleAnimation(0, 1, TimeSpan.FromMilliseconds(240))
            {
                EasingFunction = new CubicEase { EasingMode = EasingMode.EaseOut }
            });
        slide.BeginAnimation(TranslateTransform.XProperty,
            new DoubleAnimation(-18, 0, TimeSpan.FromMilliseconds(300))
            {
                EasingFunction = new CubicEase { EasingMode = EasingMode.EaseOut }
            });
    }

    private void OpenLogs_Click(object sender, RoutedEventArgs e)
    {
        string runtime = Path.Combine(
            Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),
            "SyndicateLauncher", "Runtime");
        if (!Directory.Exists(runtime)) runtime = Path.Combine(AppContext.BaseDirectory, "Runtime");
        OpenFolder(runtime);
    }

    private void OpenData_Click(object sender, RoutedEventArgs e)
    {
        OpenFolder(Services.SettingsService.DataDirectory);
    }

    private static void OpenFolder(string path)
    {
        try
        {
            Directory.CreateDirectory(path);
            Process.Start(new ProcessStartInfo(path) { UseShellExecute = true });
        }
        catch { }
    }

    private void ChapterChoice_Click(object sender, RoutedEventArgs e)
    {
        ChapterDrop.IsChecked = false;
        ChapterPageDrop.IsChecked = false;
    }

    /// <summary>
    /// A WPF Border's CornerRadius does not clip its children, so images inside rounded cards
    /// show square corners. Clip the border to its own rounded rectangle instead.
    /// </summary>
    private void RoundedClip_SizeChanged(object sender, SizeChangedEventArgs e)
    {
        if (sender is not Border border) return;
        double radius = border.CornerRadius.TopLeft;
        border.Clip = new RectangleGeometry(
            new Rect(0, 0, border.ActualWidth, border.ActualHeight), radius, radius);
    }

    private void TitleBar_MouseLeftButtonDown(object sender, MouseButtonEventArgs e)
    {
        if (e.ButtonState == MouseButtonState.Pressed)
            DragMove();
    }

    private void Minimize_Click(object sender, RoutedEventArgs e) => WindowState = WindowState.Minimized;
    private void Close_Click(object sender, RoutedEventArgs e) => Close();
}
