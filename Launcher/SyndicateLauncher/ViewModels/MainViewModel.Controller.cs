using System;
using System.Diagnostics;
using System.Windows;
using System.Windows.Input;
using System.Windows.Threading;
using SyndicateLauncher.Services;
using SyndicateLauncher.Services.Controllers;

namespace SyndicateLauncher.ViewModels;

// Settings > Mods > Controller (ported from Syndicate Classic's Controllers page):
//  - controller status, live (plugging in or pairing a pad updates it at once)
//  - PS4 / PS5 support: while the game runs, a PlayStation pad is mirrored onto a virtual Xbox pad (ViGEmBus),
//    because old Fortnite builds only read Xbox controllers. Xbox pads need nothing.
//  - launcher navigation on/off (MainWindow.Controller.cs)
//  (the old controller button layout mod was removed; StartControllers clears a layout saved by an older launcher)
public sealed partial class MainViewModel
{
    private ControllerService? _controllers;
    private DispatcherTimer? _controllerGameWatch;
    private bool _controllerGameRunning;
    private string _controllerStatus = "No controller connected";
    private string _controllerDetail = "Plug in or pair an Xbox, PS4 or PS5 controller.";
    private bool _controllerDriverMissing;
    private ICommand? _downloadControllerDriverCommand, _retryControllerDriverCommand;

    /// <summary>Starts PlayStation controller support (MainWindow.OnLoaded). Xbox pads work without it.</summary>
    public void StartControllers()
    {
        if (_controllers is not null) return;
        ControllerBinds.Clear();
        try
        {
            StartControllerService();
        }
        catch (Exception error)
        {
            _controllers = null;
            LogAdded?.Invoke("Controller support is unavailable: " + error.GetBaseException().Message + " (Xbox controllers still work in game).");
        }
        _controllerGameWatch = new DispatcherTimer { Interval = TimeSpan.FromMilliseconds(750) };
        _controllerGameWatch.Tick += (_, _) =>
        {
            bool running = GameLauncher.IsRunning;
            if (running == _controllerGameRunning) return;
            _controllerGameRunning = running;
            try { _controllers?.SetGameRunning(running); } catch { }
            RefreshControllerStatus();
        };
        _controllerGameWatch.Start();
        RefreshControllerStatus();
    }

    // Separate method: if the controller libraries fail to load, only this throws.
    private void StartControllerService()
    {
        var dispatcher = Application.Current?.Dispatcher ?? Dispatcher.CurrentDispatcher;
        _controllers = new ControllerService(message => dispatcher.InvokeAsync(() => LogAdded?.Invoke(message)));
        _controllers.Changed += () => dispatcher.InvokeAsync(RefreshControllerStatus);
        _controllers.Start(_settings.PlayStationControllers);
    }

    public void StopControllers()
    {
        _controllerGameWatch?.Stop();
        try { _controllers?.Dispose(); } catch { }
        _controllers = null;
    }

    /// <summary>Move around the launcher with a controller.</summary>
    public bool ControllerNavigation
    {
        get => _settings.ControllerNavigation;
        set
        {
            if (_settings.ControllerNavigation == value) return;
            _settings.ControllerNavigation = value;
            Save();
            OnPropertyChanged();
        }
    }

    public bool PlayStationControllers
    {
        get => _settings.PlayStationControllers;
        set
        {
            if (_settings.PlayStationControllers == value) return;
            _settings.PlayStationControllers = value;
            Save();
            OnPropertyChanged();
            try { _controllers?.SetEnabled(value); } catch { }
            RefreshControllerStatus();
        }
    }

    public string ControllerStatus { get => _controllerStatus; private set => Set(ref _controllerStatus, value); }
    public string ControllerDetail { get => _controllerDetail; private set => Set(ref _controllerDetail, value); }
    public bool ControllerDriverMissing { get => _controllerDriverMissing; private set => Set(ref _controllerDriverMissing, value); }

    public ICommand DownloadControllerDriverCommand => _downloadControllerDriverCommand ??= new RelayCommand(() =>
    {
        try { Process.Start(new ProcessStartInfo(ControllerService.DriverUrl) { UseShellExecute = true }); }
        catch (Exception ex) { StatusMessage = "Couldn't open the ViGEmBus download: " + ex.Message; }
    });

    public ICommand RetryControllerDriverCommand => _retryControllerDriverCommand ??= new RelayCommand(() =>
    {
        try { _controllers?.RetryDriver(); } catch { }
        RefreshControllerStatus();
    });

    private void RefreshControllerStatus()
    {
        var c = _controllers;
        if (c is null)
        {
            ControllerStatus = "Controller support could not start";
#if DEV_BUILD
            ControllerDetail = "Xbox controllers still work in game. Run UPDATE FROM GIT (or reinstall Syndicate) to restore PlayStation support.";
#else
            ControllerDetail = "Xbox controllers still work in game. Reinstall Syndicate to restore PlayStation support.";
#endif
            ControllerDriverMissing = false;
            return;
        }
        bool ps = c.PlayStationControllers.Count > 0;
        ControllerStatus = c.Summary();
        ControllerDriverMissing = ps && c.DriverMissing && PlayStationControllers;
        ControllerDetail = !ps ? (c.XboxControllers > 0 ? "Ready: Fortnite reads Xbox controllers directly." : "Plug in or pair an Xbox, PS4 or PS5 controller.")
            : !PlayStationControllers ? "PlayStation support is off: Fortnite will not see this controller."
            : c.DriverMissing ? "One step left: install the ViGEmBus driver below."
            : c.Mirroring ? "Active: Fortnite sees your PlayStation controller as an Xbox controller."
            : "Ready: it becomes an Xbox controller as soon as Fortnite starts from Syndicate.";
    }
}
