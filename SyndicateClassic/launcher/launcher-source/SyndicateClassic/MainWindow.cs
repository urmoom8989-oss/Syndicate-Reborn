// Syndicate © 2026 Syndicate. All rights reserved. Proprietary — no copying, distribution, or derivative works. See LICENSE.
using System;
using System.CodeDom.Compiler;
using System.Collections.Generic;
using System.ComponentModel;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Net.Http;
using System.Text.Json;
using System.Threading;
using System.Threading.Tasks;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Input;
using System.Windows.Markup;
using System.Windows.Media;
using System.Windows.Media.Imaging;
using System.Windows.Shapes;
using System.Windows.Threading;
using Microsoft.Win32;

namespace SyndicateClassic;

public partial class MainWindow : Window, IComponentConnector
{
	private readonly AccountClient _account = new AccountClient();

	private bool _register;

	private bool _authBusy;

	private static readonly string EmailPreference = System.IO.Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "Project Tilted", "remembered-email.txt");

	private bool _loadingMode;

	private readonly Queue<double> _speedHistory = new Queue<double>();

	private bool _shopBusy;

	private DispatcherTimer? _shopTimer;

	private static readonly string[] BackendProcessNames = new string[1] { "node.exe" };

	private CancellationTokenSource? _matchWatch;

	private Task? _matchWatchTask;
	private string? _matchWatchExe;

	private CancellationTokenSource? _download;

	private bool _busy;

	private bool _sessionStarted;

	private Process? _game;

	private int _backendPid;

	private LauncherPaths? _paths;

	private bool _autoLocateFailed;

	internal Button AccountButton;

	internal Ellipse BackendDot;

	internal TextBlock BackendBadgeText;

	internal Grid LaunchControls;

	internal Button LaunchButton;

	internal Border DownloadPanel;

	internal TextBlock DownloadTitle;

	internal Polyline SpeedLine;

	internal ProgressBar DownloadBar;

	internal TextBlock DownloadPercent;

	internal Button PauseDownloadButton;

	internal TextBlock StatusText;

	internal Border SocialArt;

	internal Border BackgroundHost;

	internal Border ClassicArt;

	internal Border SeasonArt;

	internal Border KnightArt;

	internal Button LogToggleButton;

	internal Border PassShop;

	internal TextBlock PassBalance;

	internal TextBlock PassMessage;

	internal StackPanel PassCards;

	internal Border LogPanel;

	internal TextBlock GameFolderText;

	internal TextBlock BackendFolderText;

	internal TextBox LogBox;

	internal Border SettingsPanel;

	internal ComboBox ModeSelector;

	internal TextBox RadminHostBox;

	internal Button SaveRadminButton;

	internal TextBlock RadminStatus;

	internal Button StopBackendButton;

	internal Border AuthOverlay;

	internal TextBlock AuthHeading;

	internal TextBlock AuthSubtitle;

	internal StackPanel UsernamePanel;

	internal TextBox UsernameBox;

	internal TextBox EmailBox;

	internal PasswordBox PasswordInput;

	internal StackPanel ConfirmPanel;

	internal PasswordBox ConfirmPasswordInput;

	internal CheckBox RememberEmail;

	internal TextBlock AuthMessage;

	internal Button AuthSubmit;

	internal Button AuthSwitch;

	private bool _contentLoaded;

	private async void AccountButton_Click(object sender, RoutedEventArgs e)
	{
		if (_busy || _authBusy)
		{
			return;
		}
		if (_account.SignedIn)
		{
			try
			{
				await _account.SignOut();
			}
			catch
			{
			}
			AccountButton.Content = "CHOOSE PLAYER";
			SetStatus("Pick a player to continue.");
			_storeBody?.Children.Clear();
            _storeSelection=null;
            PassCards.Children.Clear();
			PassBalance.Text = "V-BUCKS: SIGN IN";
			ShowAuth();
		}
		else
		{
			ShowAuth();
		}
	}

    private readonly System.Windows.Threading.DispatcherTimer _loginRetry = new() { Interval=TimeSpan.FromSeconds(15) };
    // A backend left running by an earlier session can lose its MongoDB connection (a database restart under it),
    // after which every account call returns 503 and the saved login never restores. Restart it once per launcher
    // run so it reconnects, instead of retrying against the wedged backend forever.
    private bool _loginBackendRestarted;
    private async Task RestoreRememberedLogin()
    {
        if (!RememberedLogin.Exists || _account.SignedIn) { _loginRetry.Stop(); return; }
        if (_authBusy) return;
        _authBusy = true;
        try
        {
            SetStatus("Restoring your login…");
            await EnsureAccountBackend();
            if (await RestoreWithTimeout())
            {
                AccountButton.Content = _account.Username + "  ·  SWITCH";
                AuthOverlay.Visibility = Visibility.Collapsed;
                SetStatus("Welcome back, " + _account.Username + ". Ready to play.");
                // The store may have opened while the saved login was still restoring: fill it now
                // (before, it stayed on "Sign in" until Refresh was pressed).
                if (PassShop.Visibility == Visibility.Visible) _ = Dispatcher.BeginInvoke(new Action(async () => await RefreshPasses()));
            }
            else SetStatus("Please sign in again.");
        }
        catch (Exception error)
        {
            Log("Saved login could not be restored: " + error.Message);
            bool haveToken = RememberedLogin.Exists;
            // A reachable backend that still fails the account call is usually one left running by an earlier session
            // whose MongoDB connection has since gone (a database restart). Restart it once so it reconnects, then let
            // the retry pick it up. Never while a match this PC hosts or plays could be live.
            if (haveToken && !_loginBackendRestarted && Launcher.IsBackendUp()
                && Launcher.TryAutoLocate(out var p, out _)
                && !SessionProcesses.ClientRunning(p) && !GameServer.HostMayHavePlayers(p))
            {
                _loginBackendRestarted = true;
                Log("The local backend answered but the account service failed: restarting it so it can reconnect to the database.");
                SetStatus("Restarting local services…", okay: false);
                try
                {
                    await Task.Run(() => SessionProcesses.StopOwned(p, "backend"));
                    Launcher.ForgetBackendCheck();
                    await EnsureAccountBackend();
                    SetStatus("Restoring your login…");
                }
                catch (Exception restart) { Log("Could not restart local services: " + restart.Message); SetStatus("Account service isn't ready. Saved login will retry automatically…", okay: false); }
            }
            else SetStatus(haveToken ? "Account service isn't ready. Saved login will retry automatically…" : "Saved login has expired or was revoked. Please sign in again.", okay: false);
        }
        finally {
            _authBusy = false; UpdateButtons();
            if(RememberedLogin.Exists && !_account.SignedIn)_loginRetry.Start();else _loginRetry.Stop();
        }
    }

    // A saved login takes well under a second. If the backend does not answer within 12 s it is stuck:
    // restart it and try once more, instead of sitting on "Restoring your login…" for minutes.
    private async Task<bool> RestoreWithTimeout()
    {
        for (int attempt = 1; ; attempt++)
        {
            var restore = _account.Restore();
            if (await Task.WhenAny(restore, Task.Delay(12000)) == restore) return await restore;
            _ = restore.ContinueWith(t => { _ = t.Exception; }, TaskContinuationOptions.OnlyOnFaulted);
            if (attempt == 2) throw new TimeoutException("The local backend did not answer.");
            Log("Restoring your login took too long: restarting the local backend.");
            SetStatus("Restarting local services…");
            if (Launcher.TryAutoLocate(out var paths, out _))
            {
                await Task.Run(() => SessionProcesses.StopOwned(paths, "backend"));
                Launcher.ForgetBackendCheck();
            }
            await EnsureAccountBackend();
            SetStatus("Restoring your login…");
        }
    }

    private bool _resetPassword;
    private Button? _resetPasswordButton;
	private void ShowAuth()
	{
		_register = false;
        _resetPassword=false;
		UpdateAuthMode();
		try
		{
			if (File.Exists(EmailPreference))
			{
				EmailBox.Text = File.ReadAllText(EmailPreference);
				RememberEmail.IsChecked = RememberedLogin.Exists;
			}
		}
		catch
		{
		}
		AuthOverlay.Visibility = Visibility.Visible;
		EmailBox.Focus();
	}

	private void UpdateAuthMode()
	{
        if(_resetPasswordButton==null && AuthSwitch.Parent is Panel authPanel){
            _resetPasswordButton=new Button {Content="RESET PASSWORD",Margin=new Thickness(0,8,0,0),Padding=new Thickness(8),HorizontalAlignment=HorizontalAlignment.Left};
            _resetPasswordButton.Click += (_,_)=>{if(_authBusy)return;_resetPassword=true;_register=false;UpdateAuthMode();};
            authPanel.Children.Insert(authPanel.Children.IndexOf(AuthSwitch)+1,_resetPasswordButton);
        }
		AuthHeading.Text = (_register ? "CREATE ACCOUNT" : "SIGN IN");
		AuthSubtitle.Text = (_register ? "One account for your Chapter One library." : "Welcome back. Your island is waiting.");
		StackPanel usernamePanel = UsernamePanel;
		Visibility visibility = (ConfirmPanel.Visibility = ((!_register) ? Visibility.Collapsed : Visibility.Visible));
		usernamePanel.Visibility = visibility;
		AuthSubmit.Content = (_register ? "CREATE ACCOUNT" : "SIGN IN");
		AuthSwitch.Content = (_register ? "Already have an account? SIGN IN" : "Don't have an account? SIGN UP");
        if(_resetPassword){AuthHeading.Text="RESET PASSWORD";AuthSubtitle.Text="Reset an account saved on this PC.";UsernamePanel.Visibility=Visibility.Collapsed;ConfirmPanel.Visibility=Visibility.Visible;AuthSubmit.Content="RESET PASSWORD";AuthSwitch.Content="Back to SIGN IN";}
        if(_resetPasswordButton!=null)_resetPasswordButton.Visibility=(_resetPassword||_register)?Visibility.Collapsed:Visibility.Visible;
		AuthMessage.Text = "";
		PasswordInput.Clear();
		ConfirmPasswordInput.Clear();
	}

	private void AuthSwitch_Click(object sender, RoutedEventArgs e)
	{
		if (!_authBusy)
		{
			if(_resetPassword){_resetPassword=false;_register=false;}else _register = !_register;
			UpdateAuthMode();
		}
	}

	private void CloseAuth_Click(object sender, RoutedEventArgs e)
	{
		if (!_authBusy)
		{
			AuthOverlay.Visibility = Visibility.Collapsed;
			PasswordInput.Clear();
			ConfirmPasswordInput.Clear();
		}
	}

	// One start-up shared by everyone who needs the local services (opening the launcher, sign-in, the player
	// picker, Launch), so they never start MongoDB/Node twice and a later caller just waits for the first.
	private Task? _accountBackendStart;
	private Task EnsureAccountBackend()
	{
		if (_accountBackendStart == null || _accountBackendStart.IsFaulted || _accountBackendStart.IsCanceled
			|| (_accountBackendStart.IsCompleted && !Launcher.IsBackendUp()))
			_accountBackendStart = EnsureAccountBackendCore();
		return _accountBackendStart;
	}

	// Starts the local services in the background as soon as the launcher opens, so the backend is already
	// up by the time you pick a player or press Play (it used to start only then, adding several seconds).
	private async Task PrewarmBackend()
	{
		try
		{
			if (!Launcher.TryAutoLocate(out _, out _)) return; // first run: wait until a game folder is chosen
			var started = System.Diagnostics.Stopwatch.StartNew();
			await EnsureAccountBackend();
			Log($"Local services ready {started.ElapsedMilliseconds} ms after the launcher opened.");
		}
		catch (Exception error) { Log("Local services will start when needed: " + error.Message); }
	}

	private async Task EnsureAccountBackendCore()
	{
		await App.WaitForComponents(message => AuthMessage.Text = message);
		if (Launcher.TryAutoLocate(out LauncherPaths paths, out string _))
		{
			RadminConnection.Configure(paths);
            if(Launcher.IsBackendUp() && !SessionProcesses.OwnedRunning(paths,"backend"))
            {
                // Another Syndicate copy's services hold the ports: stop them for the player (they used to have to end
                // node.exe and mongod.exe in Task Manager), then start this installation's own.
                AuthMessage.Text = "Closing another Syndicate copy's services…";
                int stopped = await Task.Run(() => SessionProcesses.StopOtherSyndicateServices(paths, Log)); // Log hops back to the window thread itself
                if (stopped > 0) { await Task.Delay(1500); Launcher.ForgetBackendCheck(); }
                if(Launcher.IsBackendUp() && !SessionProcesses.OwnedRunning(paths,"backend"))
                    throw new InvalidOperationException("Another program is using Syndicate's backend port 3551. Close the other Syndicate launcher (and Fortnite), then press Retry.");
            }
            // Running but not answering (stuck), or left over from an older launcher (it freezes once the launcher
            // that started it closes): restart it, or sign-in hangs and Fortnite ends up on the Epic login screen.
            var health = Launcher.IsBackendUp() ? await Launcher.CheckBackendAsync() : (Responding: true, Current: true);
            bool stale = health.Responding && !health.Current && !SessionProcesses.ClientRunning(paths) && !GameServer.HostMayHavePlayers(paths);
            if(!health.Responding || stale)
            {
                Log(stale ? "The local backend was started by an older launcher: restarting it." : "The local backend is running but not answering: restarting it.");
                AuthMessage.Text = "Restarting local services…";
                await Task.Run(() => SessionProcesses.StopOwned(paths, "backend"));
                Launcher.ForgetBackendCheck();
            }
		}
		if (!Launcher.IsBackendUp())
		{
			LauncherPaths paths2 = (await EnsurePathsAsync()) ?? throw new InvalidOperationException("Choose your game folder first.");
			// Off the window thread: starting MongoDB waits up to 30 s and froze the launcher meanwhile.
			var accountBackend = await Task.Run(() => Launcher.StartBackend(paths2));
			_backendPid = accountBackend.Id;
			if (!(await Launcher.WaitForBackendAsync(90000, delegate
			{
				AuthMessage.Text = "Starting database and backend…";
			}, accountBackend, paths2)))
			{
				throw new InvalidOperationException("Local services timed out. Check Runtime/Server/backend-startup.log and Runtime/Database/mongodb.log.");
			}
			SetBackendBadge(up: true);
			UpdateButtons();
		}
	}

	private async void AuthSubmit_Click(object sender, RoutedEventArgs e)
	{
		if (_authBusy)
		{
			return;
		}
		if (string.IsNullOrWhiteSpace(EmailBox.Text) || string.IsNullOrEmpty(PasswordInput.Password))
		{
			AuthMessage.Text = "Enter your email and password.";
			return;
		}
		if ((_register || _resetPassword) && PasswordInput.Password != ConfirmPasswordInput.Password)
		{
			AuthMessage.Text = "Passwords do not match.";
			return;
		}
		_authBusy = true;
		Button authSubmit = AuthSubmit;
		bool isEnabled = (AuthSwitch.IsEnabled = false);
		authSubmit.IsEnabled = isEnabled;
		AuthMessage.Text = "Connecting to your local serverâ€¦";
		try
		{
			await EnsureAccountBackend();
            if(_resetPassword){
                await _account.ResetPassword(EmailBox.Text.Trim(),PasswordInput.Password,ConfirmPasswordInput.Password);
                _resetPassword=false;_register=false;UpdateAuthMode();
                AuthMessage.Text="Password reset. Sign in with your new password.";return;
            }
			await _account.SignIn(EmailBox.Text.Trim(), PasswordInput.Password, _register ? UsernameBox.Text.Trim() : null, RememberEmail.IsChecked == true);
			try
			{
				if (RememberEmail.IsChecked == true)
				{
					Directory.CreateDirectory(System.IO.Path.GetDirectoryName(EmailPreference));
					File.WriteAllText(EmailPreference, EmailBox.Text.Trim());
				}
				else if (File.Exists(EmailPreference))
				{
					File.Delete(EmailPreference);
				}
			}
			catch
			{
			}
			AccountButton.Content = _account.Username + "  ·  SWITCH";
			AuthOverlay.Visibility = Visibility.Collapsed;
			SetStatus("Welcome, " + _account.Username + ". Ready to play.");
			if (PassShop.Visibility == Visibility.Visible)
			{
				await RefreshPasses();
			}
		}
		catch (Exception ex)
		{
			AuthMessage.Text = ((ex is HttpRequestException) ? "Cannot reach the account service. Check Diagnostics and try again." : ((ex is TaskCanceledException) ? "The account service took too long. Please try again." : ex.Message));
		}
		finally
		{
			PasswordInput.Clear();
			ConfirmPasswordInput.Clear();
			_authBusy = false;
			Button authSubmit2 = AuthSubmit;
			isEnabled = (AuthSwitch.IsEnabled = true);
			authSubmit2.IsEnabled = isEnabled;
		}
	}

	private void SettingsButton_Click(object sender, RoutedEventArgs e)
	{
		_loadingMode = true;
		string text = ((_paths == null) ? "solo" : GameServer.ReadMode(_paths));
		ModeSelector.SelectedIndex = ((text == "duo") ? 1 : ((text == "squad") ? 2 : 0));
        ModeSelector.Visibility = Visibility.Collapsed;
        ModeSelector.ToolTip = "Choose your playlist in Fortnite.";
		_loadingMode = false;
		ModeSelector.IsEnabled = !_busy && !_sessionStarted && _paths != null && !RadminConnection.Remote;
		RadminHostBox.Text = RadminConnection.Host;
		TextBox radminHostBox = RadminHostBox;
		bool isEnabled = (SaveRadminButton.IsEnabled = !_busy && !_sessionStarted && !_authBusy);
		radminHostBox.IsEnabled = isEnabled;
		RadminStatus.Text = (RadminConnection.Remote ? ("Joining " + RadminConnection.Host + " — keep the launcher open while playing.") : "Hosting locally.");
		SettingsPanel.Visibility = ((SettingsPanel.Visibility == Visibility.Visible) ? Visibility.Collapsed : Visibility.Visible);
	}

    private async Task LeaveSyndicateSession(LauncherPaths paths)
    {
        string? warning=null;
        bool wasRemote=RadminConnection.Remote;
        bool keepHost=!wasRemote && GameServer.HostMayHavePlayers(paths);
        _matchWatch?.Cancel();
        try
        {
            // Every step here has a time limit: a stuck backend used to leave the launcher on
            // "Closing Fortnite and leaving the session…" with QUIT greyed out for minutes.
            if(_matchWatchTask!=null)await Task.WhenAny(_matchWatchTask, Task.Delay(5000));
            SetStatus("Closing Fortnite and leaving the session…");
            await Task.Run(()=>SessionProcesses.StopClient(paths));
            if(!wasRemote){await Task.Delay(1500);keepHost=GameServer.HostMayHavePlayers(paths);}
            if(_account.SignedIn)
            {
                await Task.Delay(2500);
                try{
                    var finish=_account.FinishTravel();
                    if(await Task.WhenAny(finish, Task.Delay(8000))!=finish){
                        _=finish.ContinueWith(t=>{_=t.Exception;},TaskContinuationOptions.OnlyOnFaulted);
                        throw new TimeoutException("the local backend did not answer in time");
                    }
                    var outcome=await finish;if(outcome.TryGetProperty("message",out var message))warning=message.GetString();
                }
                catch(Exception error){warning="Connection closed. Account sync remains pending: "+error.Message;Log(warning);}
                // Ending a game is not an account sign-out; preserve the session and remembered token.
            }
        }
        finally
        {
            // A failed sync must never skip relay/process cleanup or keep remote mode selected.
            try{RadminConnection.Save("");RadminConnection.Configure(paths);}
            finally
            {
                RadminConnection.Stop();
                try{
                    keepHost = keepHost || (!wasRemote && GameServer.HostMayHavePlayers(paths));
                    if(!keepHost)await Task.Run(()=>SessionProcesses.StopAll(paths, keepAccountServices: _account.SignedIn));
                }
                finally
                {
                    _game=null;if(!_account.SignedIn)_backendPid=0;_sessionStarted=false;_runningSeason=null;
                    RadminHostBox.Text="";RadminStatus.Text="Hosting locally.";
                    AccountButton.Content=_account.SignedIn?_account.Username+"  ·  SWITCH":"CHOOSE PLAYER";SetBackendBadge(_account.SignedIn && Launcher.IsBackendUp());
                    if(keepHost){SetBackendBadge(true);StartMatchWatch(paths);SetStatus("Your game closed. Hosting continues until everyone leaves; keep Syndicate and this PC running.");}
                    else SetStatus(warning??"Disconnected. Ready to play locally.");
                }
            }
        }
    }

	private async void SaveRadmin_Click(object sender, RoutedEventArgs e)
	{
		if (_busy || _sessionStarted || _authBusy)
		{
			return;
		}
		try
		{
			string host = RadminConnection.Validate(RadminHostBox.Text);
			if (host == RadminConnection.Host && host.Length > 0)
			{
				RadminStatus.Text = "Connection already saved.";
				return;
			}
			_busy = true;
			UpdateButtons();
            if(_paths!=null)await LeaveSyndicateSession(_paths);
			RadminConnection.Save(host);
			if (_paths != null)
			{
				RadminConnection.Configure(_paths);
			}
			await Task.CompletedTask;
			RadminStatus.Text = ((host.Length == 0) ? "Hosting locally. Sign in to your local account." : "Saved. Your account stays local. Launch to join this match host.");
			SetBackendBadge(Launcher.IsBackendUp());
		}
		catch (Exception ex)
		{
			RadminStatus.Text = ex.Message;
		}
		finally
		{
			_busy = false;
			UpdateButtons();
		}
	}

	private void ModeSelector_Changed(object sender, SelectionChangedEventArgs e)
	{
		if (_loadingMode || _paths == null || _busy || _sessionStarted || !(ModeSelector.SelectedItem is ComboBoxItem comboBoxItem))
		{
			return;
		}
		try
		{
			GameServer.SaveMode(_paths, (string)comboBoxItem.Tag);
			Log($"Next launch: {comboBoxItem.Content}.");
		}
		catch (Exception ex)
		{
			MessageBox.Show(this, ex.Message, "Could not save game mode");
		}
	}

	private void BeginDownloadPanel()
	{
		_speedHistory.Clear();
		SpeedLine.Points.Clear();
		LaunchControls.Visibility = Visibility.Collapsed;
		DownloadPanel.Visibility = Visibility.Visible;
		DownloadTitle.Text = "Installing";
		PauseDownloadButton.Content = "Ⅱ";
		PauseDownloadButton.ToolTip = "Pause download";
		DownloadBar.IsIndeterminate = true;
	}

	private void PauseDownload_Click(object sender, RoutedEventArgs e)
	{
		if (_download != null)
		{
			_download.Cancel();
		}
		else
		{
			LaunchButton_Click(sender, e);
		}
	}

	private void DownloadStage(string message)
	{
		if (!message.StartsWith("Downloading"))
		{
			SetStatus(message);
			if (message.StartsWith("Extracting") || message.StartsWith("Installing") || message.StartsWith("Checking"))
			{
				DownloadTitle.Text = (message.StartsWith("Extracting") ? "Extracting" : "Installing");
				DownloadBar.IsIndeterminate = true;
				DownloadPercent.Text = "…";
				_speedHistory.Clear();
				SpeedLine.Points.Clear();
			}
		}
	}

	private void UpdateDownloadGraph(DownloadSample sample)
	{
		_speedHistory.Enqueue(sample.BytesPerSecond);
		while (_speedHistory.Count > 50)
		{
			_speedHistory.Dequeue();
		}
		double num = Math.Max(1048576.0, _speedHistory.Max());
		PointCollection pointCollection = new PointCollection();
		int num2 = 50 - _speedHistory.Count;
		foreach (double item in _speedHistory)
		{
			pointCollection.Add(new Point((double)num2++ * 76.0 / 49.0, 19.0 - 18.0 * item / num));
		}
		SpeedLine.Points = pointCollection;
		ProgressBar downloadBar = DownloadBar;
		long? total = sample.Total;
		downloadBar.IsIndeterminate = !total.HasValue || total.GetValueOrDefault() <= 0;
		double value = ((sample.Total > 0) ? ((double)sample.Received * 100.0 / (double)sample.Total.Value) : 0.0);
		DownloadBar.Value = value;
		DownloadPercent.Text = $"{value:0}%";
		SetStatus($"{sample.BytesPerSecond / 1000000.0:0.0} MB/s · {(double)sample.Received / 1000000000.0:0.00} / {(double)sample.Total.GetValueOrDefault() / 1000000000.0:0.00} GB");
	}

	private static ImageSource PassArt(string id,string fallback){
        // Seasons 6-X: the same art as the season picker.
        if(id.StartsWith("season") && int.TryParse(id.Substring(6),out var n) && n>=6)return SeasonArtImage(n);
        // Season 3's store tile uses the same key art as the Season 3 card.
        if(id=="season3")return SeasonCardImage(3);
        // Chapter 2 Season 1 and 2 use the same key arts as their cards in the play menu.
        if(id.Equals("chapter2-season1",StringComparison.OrdinalIgnoreCase))return SeasonArtImage(11);
        if(id.Equals("chapter2-season2",StringComparison.OrdinalIgnoreCase))return SeasonArtImage(12);
        if(id!="season4" && id!="season5")return new BitmapImage(new Uri("pack://application:,,,/SyndicateLauncher;component/Assets/"+fallback));
        using var stream=typeof(MainWindow).Assembly.GetManifestResourceStream("SyndicateClassic.SeasonCard"+(id=="season4"?"4":"5"));
        var image=new BitmapImage();image.BeginInit();image.CacheOption=BitmapCacheOption.OnLoad;image.StreamSource=stream;image.EndInit();image.Freeze();return image;
    }
    private bool _showPacks;
	private async void PassesTab_Click(object sender, RoutedEventArgs e)
	{
		_showPacks=(sender as Button)?.Tag as string == "packs";
        _storeSelection=null;
        if (_showPacks) PreviewStoreSeason(null);
		PassShop.Visibility = Visibility.Visible;
		if (_shopTimer == null)
		{
			_shopTimer = new DispatcherTimer
			{
				Interval = TimeSpan.FromSeconds(20.0)
			};
		}
		_shopTimer.Stop();
		_shopTimer.Tick -= ShopTimer_Tick;
		_shopTimer.Tick += ShopTimer_Tick;
		_shopTimer.Start();
		await RefreshPasses();
	}

	private async void ShopTimer_Tick(object? sender, EventArgs e)
	{
		if (PassShop.Visibility == Visibility.Visible && _account.SignedIn)
		{
			await RefreshPasses();
		}
	}

	private void FortniteTab_Click(object sender, RoutedEventArgs e)
	{
		PassShop.Visibility = Visibility.Collapsed;
		PreviewStoreSeason(null);
		_shopTimer?.Stop();
	}

	private async void RefreshPasses_Click(object sender, RoutedEventArgs e)
	{
		await RefreshPasses();
	}

	private async Task RefreshPasses()
	{
		if (_shopBusy)
		{
			return;
		}
		if (!_account.SignedIn)
		{
            EnsureStoreDesign();
			_storeBody?.Children.Clear();
            _storeSelection=null;
            PassCards.Children.Clear();
            if (RememberedLogin.Exists)
            {
                // A saved login is still being restored: wait for it instead of asking to sign in.
                // RestoreRememberedLogin refreshes the store as soon as it succeeds.
                PassBalance.Text = "V-BUCKS: …";
                PassMessage.Text = "Signing you in… the store loads in a moment.";
                if (!_authBusy) _ = RestoreRememberedLogin();
                return;
            }
			PassBalance.Text = "V-BUCKS: SIGN IN";
			PassMessage.Text = "Sign in to see your in-game balance and purchase a complete pass.";
			ShowAuth();
			return;
		}
		_shopBusy = true;
		try
		{
			await EnsureAccountBackend();
			RenderPasses(await _account.Passes());
		}
		catch (Exception ex)
		{
			PassMessage.Text = ex.Message;
		}
		finally
		{
			_shopBusy = false;
		}
	}

	private void RenderPasses(JsonElement data) => DrawStore(data);

    private bool _exitCleaning, _exitComplete;
	public MainWindow()
	{
		InitializeComponent();
        App.Mark("xaml");
        _loginRetry.Tick += async (_,__) => await RestoreRememberedLogin();
        Closed += (_,__) => _loginRetry.Stop();
        BuildSeasonGrid();
        App.Mark("season grid");
        InitializeLauncherMotion();
        if(StopBackendButton.Parent is Panel settingsButtons) {
            int index=settingsButtons.Children.IndexOf(StopBackendButton);
            var group=new StackPanel();Grid.SetRow(group,Grid.GetRow(StopBackendButton));Grid.SetColumn(group,Grid.GetColumn(StopBackendButton));
            settingsButtons.Children.Remove(StopBackendButton);group.Children.Add(StopBackendButton);
            var repairButton=new Button {Content="Repair Local Services",Margin=new Thickness(0,8,0,0),Padding=new Thickness(10,6,10,6),ToolTip="Re-checks every Syndicate file, restores anything damaged, then restarts the local account and match services. Accounts and settings are kept."};
            repairButton.Click+=async (_,__)=>{
                if(_busy || _authBusy)return;
                _busy=true;repairButton.IsEnabled=false;UpdateButtons();
                try{
                    _matchWatch?.Cancel();
                    if(_paths!=null && SessionProcesses.ClientRunning(_paths)){SetStatus("Close Fortnite first, then press Repair again.",okay:false);return;}
                    await App.WaitForComponents(m => SetStatus(m));
                    SetStatus("Checking Syndicate files…");
                    // Re-verify every local component against the bundled payload and restart services.
                    await System.Threading.Tasks.Task.Run(()=>ComponentUpdates.Apply(forceRepair:true));
                    App.ComponentFailure=null;
                    if(_paths!=null){
                        var repaired=_paths;SetStatus("Files OK. Restarting account and match services…");
                        await System.Threading.Tasks.Task.Run(()=>{SessionProcesses.StopAll(repaired);Launcher.StartBackend(repaired);});
                        await GameServer.PrepareLobby(repaired,message=>Dispatcher.Invoke(()=>SetStatus(message)));
                    }
                    SetStatus(ComponentUpdates.Warning ?? "Repair finished. Everything checks out — you can press Launch.",okay:ComponentUpdates.Warning==null);
                }
                catch(Exception error){SetStatus("Repair could not finish: "+error.GetBaseException().Message,okay:false);}
                finally{_busy=false;repairButton.IsEnabled=true;UpdateButtons();if(_sessionStarted && _paths!=null)StartMatchWatch(_paths);}
            };
            group.Children.Add(repairButton);
            var botLabel = new TextBlock { Text="Local match bots", Foreground=Brushes.White, Margin=new Thickness(0,10,0,4) };
            var botCount = new TextBox { Name="PolishBotCount", Text=MatchPerformanceSettings.Load().ToString(), MinWidth=100, MaxLength=2 };
            var botHint = new TextBlock { Name="PolishBotHint", Text="Fewer bots = higher FPS and smoother play (the bot server runs on this PC). 30 or fewer also runs the server at 60 ticks/sec. Applies to the next match.", Foreground=Brushes.LightGray, TextWrapping=TextWrapping.Wrap, MaxWidth=340, Margin=new Thickness(0,4,0,8) };
            void SaveBotCount() {
                if(!int.TryParse(botCount.Text.Trim(), out int count) || count<10 || count>99) {
                    botHint.Text="Enter a whole number from 10 to 99. Your previous setting is unchanged.";
                    return;
                }
                try { MatchPerformanceSettings.Save(count); botHint.Text="Saved. Applies to newly started local hosts; remote hosts choose their own count."; SetStatus("Saved " + count + " bots for newly started local matches."); }
                catch(Exception error) { SetStatus("Could not save bot count: " + error.Message, okay:false); }
            }
            botCount.LostKeyboardFocus += (_,__) => SaveBotCount();
            botCount.KeyDown += (_,e) => { if(e.Key==Key.Enter){SaveBotCount();e.Handled=true;} };
            group.Children.Add(botLabel);group.Children.Add(botCount);group.Children.Add(botHint);
            var skillLabel=new TextBlock {Text="Bot skill",Foreground=Brushes.White,Margin=new Thickness(0,8,0,4)};
            var skill=new ComboBox {Name="PolishBotSkill",MinWidth=140};
            foreach(var name in new[]{"Bot","Easy","Medium","Hard","Variety"})skill.Items.Add(name);
            skill.SelectedIndex=Array.IndexOf(BotSkillSettings.Values,BotSkillSettings.Load());
            skill.ToolTip="Bot: rarely fires/builds. Easy: weak aim, limited cover. Medium: balanced. Hard: accurate, ramp pushes and boxing. Variety: a realistic lobby — 15% beginner, 30% average, 30% above-average, 20% advanced, 5% pro. Applies to new local matches.";
            skill.SelectionChanged+=(_,__)=>{if(skill.SelectedIndex<0)return;try{BotSkillSettings.Save(BotSkillSettings.Values[skill.SelectedIndex]);SetStatus("Bot skill saved. Start a new local match to apply it.");}catch(Exception error){SetStatus("Could not save bot skill: "+error.Message,okay:false);}};
            group.Children.Add(skillLabel);group.Children.Add(skill);
            settingsButtons.Children.Insert(index,group);
        }
        if(SettingsPanel.Child is UIElement settingsContent && settingsContent is not ScrollViewer) {
            SettingsPanel.Child=null;
            SettingsPanel.Child=new ScrollViewer { Content=settingsContent, VerticalScrollBarVisibility=ScrollBarVisibility.Auto, HorizontalScrollBarVisibility=ScrollBarVisibility.Disabled };
        }
        RedesignHeaderAndSettings();
        App.Mark("settings");
        HookSettingsAnimation();
        RedesignAuthentication();
        using var iconStream=System.Reflection.Assembly.GetExecutingAssembly().GetManifestResourceStream("SyndicateClassic.Icon.ico");
        Icon=System.Windows.Media.Imaging.BitmapFrame.Create(iconStream!, System.Windows.Media.Imaging.BitmapCreateOptions.None, System.Windows.Media.Imaging.BitmapCacheOption.OnLoad);
        var version=System.Reflection.Assembly.GetExecutingAssembly().GetName().Version!.ToString(3);
        var originalContent=(UIElement)Content;Content=null;
        var frame=new DockPanel { LastChildFill=true, Background=new SolidColorBrush(Color.FromRgb(20,37,47)) };
        var versionLabel=new TextBlock { Name="LauncherVersionLabel", Text="Launcher v"+version, FontSize=12, Foreground=new SolidColorBrush(Color.FromRgb(205,216,225)), HorizontalAlignment=HorizontalAlignment.Right, Margin=new Thickness(12,5,16,5), ToolTip="Installed launcher version" };
        DockPanel.SetDock(versionLabel,Dock.Bottom);frame.Children.Add(versionLabel);frame.Children.Add(originalContent);Content=frame;
        ApplyModernShell(frame,versionLabel);
        if(!double.IsNaN(Height))Height+=26;
        Title="Syndicate Launcher v"+System.Reflection.Assembly.GetExecutingAssembly().GetName().Version!.ToString(3);
        RememberEmail.Content = "Remember login";
        RememberEmail.ToolTip = "Sign in automatically on this Windows account for 30 days. Sign out to forget.";
        ModeSelector.Visibility=Visibility.Collapsed;
        ModeSelector.IsEnabled=false;
		base.Loaded += async delegate
		{
			App.Mark("window shown");
			CrashGuard.Report = message => Log(message);
			if (CrashGuard.TakeCrashNotice() is string crashNotice) SetStatus(crashNotice, okay: false);
			lock (App.StartupMarks) Log("Startup: " + string.Join(", ", App.StartupMarks));
			// Keep the timings in a file too (with the background setup once it finishes), so slow starts can be checked.
			_ = App.ComponentsReady.ContinueWith(_ =>
			{
				try
				{
					string line;
					lock (App.StartupMarks) line = DateTime.Now.ToString("yyyy-MM-dd HH:mm:ss") + "  " + string.Join(", ", App.StartupMarks);
					var file = System.IO.Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "ProjectTilted", "startup-timing.txt");
					System.IO.Directory.CreateDirectory(System.IO.Path.GetDirectoryName(file)!);
					var lines = System.IO.File.Exists(file) ? System.IO.File.ReadAllLines(file).TakeLast(19).ToList() : new System.Collections.Generic.List<string>();
					lines.Add(line);
					System.IO.File.WriteAllLines(file, lines);
				}
				catch { }
			});
			ApplyBackground();
			RefreshDetection();
			_sessionStarted = _paths != null && SessionProcesses.ClientRunning(_paths);
			UpdateButtons();
			StartGameExitWatch();
			StartLobbyTag();
			StartDevConsole(); // Dev Kit builds only (DevConsole.cs); does nothing in public builds
			_ = PrewarmBackend();
			if (_sessionStarted && RadminConnection.Remote)
			{
				try
				{
					await RadminConnection.EnsureConnected();
				}
				catch (Exception ex)
				{
					SetStatus(ex.Message, okay: false);
				}
			}
			if (_paths != null && (_sessionStarted || SessionProcesses.OwnedRunning(_paths,"host")))
			{
				StartMatchWatch(_paths);
			}
            await RestoreRememberedLogin();
            if (!_account.SignedIn && !RememberedLogin.Exists) ShowAuth(); // first start: ask who's playing
            _ = CheckForUpdateNotice();
		};
		Title="Syndicate Launcher";
        base.Closing += async delegate(object? sender, CancelEventArgs e)
        {
            if (_exitComplete) return;
            e.Cancel=true;
            if (_exitCleaning) return;
            if (_download!=null) { _download.Cancel();SetStatus("Pausing download… Close again when it finishes.");return; }
            if (_paths!=null && (SessionProcesses.ClientRunning(_paths) || GameServer.HostMayHavePlayers(_paths)))
                if(MessageBox.Show(this,"Closing Syndicate will close your Fortnite session and stop this hosted match for connected players. Close everything?","Close Syndicate",MessageBoxButton.YesNo,MessageBoxImage.Question)!=MessageBoxResult.Yes)return;
            _exitCleaning=true;IsEnabled=false;_shopTimer?.Stop();_loginRetry.Stop();_matchWatch?.Cancel();
            SetStatus("Closing Syndicate services and saving the database…");
            try {
                if(_matchWatchTask!=null)try{await _matchWatchTask;}catch(OperationCanceledException){}
                RadminConnection.Stop();
                var paths=_paths;
                if(paths==null && Launcher.TryAutoLocate(out var located,out _))paths=located;
                if(paths!=null)await Task.Run(()=>SessionProcesses.StopAll(paths));
                // The database is stopped now: take today's account copy here instead of at start-up.
                await Task.Run(AccountStorage.SnapshotAfterStop);
                _exitComplete=true;Close();
            } catch(Exception error) {
                IsEnabled=true;_exitCleaning=false;
                SetStatus("Cleanup could not finish. Close Syndicate again to retry.");
                MessageBox.Show(this,error.GetBaseException().Message,"Could not finish closing",MessageBoxButton.OK,MessageBoxImage.Warning);
            }
        };
		base.Closed += delegate
		{
			RestoreDesktop();
		};
		base.Closed += delegate
		{
			_matchWatch?.Cancel();
			RadminConnection.Stop();
		};
	}

	private void ApplyBackground()
	{
		BitmapImage image = new BitmapImage(new Uri("pack://application:,,,/SyndicateLauncher;component/Assets/Season4Reference.png"));
		image.Freeze();
		BackgroundHost.Background = Region(340.0, 46.0, 940.0, 437.0);
		ClassicArt.Background = Region(183.0, 503.0, 306.0, 178.0);
		SeasonArt.Background = new ImageBrush(new BitmapImage(new Uri("pack://application:,,,/SyndicateLauncher;component/Assets/IslandClean.png")))
		{
			Stretch = Stretch.UniformToFill
		};
		KnightArt.Background = Region(812.0, 503.0, 285.0, 178.0);
		SocialArt.Background = Region(26.0, 451.0, 173.0, 20.0);
		ImageBrush Region(double x, double y, double width, double height)
		{
			return new ImageBrush(image)
			{
				Viewbox = new Rect(x / 1280.0, y / 720.0, width / 1280.0, height / 720.0),
				ViewboxUnits = BrushMappingMode.RelativeToBoundingBox,
				Stretch = Stretch.Fill
			};
		}
	}

	private void TitleBar_MouseLeftButtonDown(object sender, MouseButtonEventArgs e)
	{
		if (e.OriginalSource is TextBlock || e.OriginalSource is Grid || e.OriginalSource is Border)
		{
			DragMove();
		}
	}

	private void Minimize_Click(object sender, RoutedEventArgs e)
	{
		base.WindowState = WindowState.Minimized;
	}

	private void Maximize_Click(object sender, RoutedEventArgs e)
	{
		base.WindowState = ((base.WindowState != WindowState.Maximized) ? WindowState.Maximized : WindowState.Normal);
	}

	private void CloseWindow_Click(object sender, RoutedEventArgs e)
	{
		Close();
	}

	private void StartMatchWatch(LauncherPaths paths)
	{
		CancellationToken token;
		if (!RadminConnection.Remote)
		{
			Task matchWatchTask = _matchWatchTask;
			if (matchWatchTask == null || matchWatchTask.IsCompleted)
			{
				_matchWatch = new CancellationTokenSource();
				token = _matchWatch.Token;
				_matchWatchExe = paths.GameExe;
				_matchWatchTask = Watch();
			}
			else if (!string.Equals(_matchWatchExe, paths.GameExe, StringComparison.OrdinalIgnoreCase))
			{
				// A different season was launched while the previous season's host kept running:
				// the old watcher would refuse this season's match requests ("different Fortnite
				// version"), so replace it with one for the season now being played.
				_matchWatch?.Cancel();
				_matchWatch = new CancellationTokenSource();
				token = _matchWatch.Token;
				_matchWatchExe = paths.GameExe;
				_matchWatchTask = AfterPrevious(matchWatchTask);
			}
		}
		async Task AfterPrevious(Task previous)
		{
			try { await previous; } catch { }
			await Watch();
		}
		async Task Watch()
		{
			try
			{
				await GameServer.WatchMatches(paths, delegate(string message)
				{
					SetStatus(message);
					Log(message);
				}, token);
			}
			catch (OperationCanceledException)
			{
			}
			catch (Exception ex2)
			{
				Log("Automatic match restart failed: " + ex2.Message);
				SetStatus("Couldn't prepare the next match. Press Quit, then Launch.", okay: false);
			}
		}
	}

	// Safe from any thread. Background work (stopping another Syndicate copy's services, the game server, downloads)
	// reports through Log and SetStatus; called off the window thread they threw "The calling thread cannot access
	// this object because a different thread owns it", which failed the backend start and every saved-login retry
	// (stuck on "Account service isn't ready" and "Couldn't load players" forever).
	private void Log(string message)
	{
		if (!Dispatcher.CheckAccess()) { Dispatcher.BeginInvoke(new Action(() => Log(message))); return; }
		LogBox.AppendText($"[{DateTime.Now:HH:mm:ss}] {message}{Environment.NewLine}");
        ErrorReporter.Remember(message);
        AddDiagnosticsLine(message);
		LogBox.ScrollToEnd();
	}

	private void LogToggleButton_Click(object sender, RoutedEventArgs e)
	{
		ShowLog(_diagBar != null ? !_diagOpen : LogPanel.Visibility != Visibility.Visible);
	}

	private void ShowLog(bool visible)
	{
        if (SetDiagnosticsOpen(visible)) { LogToggleButton.Content = (visible ? "Hide Log" : "Log"); return; }   // docked diagnostics bar
		LogPanel.Visibility = ((!visible) ? Visibility.Collapsed : Visibility.Visible);
		LogToggleButton.Content = (visible ? "Hide Log" : "Log");
		if (visible)
		{
			LogBox.ScrollToEnd();
		}
	}

	private void SetStatus(string message, bool okay = true)
	{
		if (!Dispatcher.CheckAccess()) { Dispatcher.BeginInvoke(new Action(() => SetStatus(message, okay))); return; }
		_lastStatus = message; _lastStatusAt = DateTime.UtcNow; // also read by the lobby loading tag (LobbyTag.cs)
		StatusText.Text = message;
		StatusText.Foreground = new SolidColorBrush(okay ? Colors.White : ((Color)ColorConverter.ConvertFromString("#FF9B9B")));
        SetDiagnosticsStatus(message);
        if (!okay) Log(message);   // problems also land in the diagnostics log (and its error count)
		if (!okay && _diagBar == null)
		{
			ShowLog(visible: true);
		}
	}

	private void SetBackendBadge(bool up)
	{
		BackendDot.Fill = new SolidColorBrush(up ? ((Color)ColorConverter.ConvertFromString("#2ECC71")) : ((Color)ColorConverter.ConvertFromString("#E74C3C")));
		BackendBadgeText.Text = (up ? "ONLINE" : "OFFLINE");
        SyncModernStatus(up);
	}

	private void UpdateButtons()
	{
		ModeSelector.IsEnabled = !_busy && !_sessionStarted && !RadminConnection.Remote;
		TextBox radminHostBox = RadminHostBox;
		bool isEnabled = (SaveRadminButton.IsEnabled = !_busy && !_sessionStarted && !_authBusy);
		radminHostBox.IsEnabled = isEnabled;
		LaunchButton.IsEnabled = !_busy || _download != null;
		LaunchButton.Content = ((_download != null) ? "Cancel" : (_sessionStarted ? "Quit" : ((_paths != null && !GameDownload.Ready(_paths)) ? "Choose folder" : "Launch")));
		StopBackendButton.IsEnabled = !_busy && _backendPid > 0;
        foreach(var button in _seasonLaunchButtons){button.IsEnabled=LaunchButton.IsEnabled;button.Content=_sessionStarted?"Quit":"Launch";}
        foreach(var choose in _seasonFolderButtons)choose.IsEnabled=!_busy&&!_sessionStarted;
	}

	private void RefreshDetection()
	{
		if (_paths != null)
		{
			GameFolderText.Text = "Game: " + _paths.GameExe;
			BackendFolderText.Text = "Backend: " + _paths.StartBat;
			return;
		}
		if (Launcher.TryAutoLocate(out LauncherPaths paths, out string message))
		{
			_paths = paths;
			Log(message);
			GameFolderText.Text = "Game: " + paths.GameExe;
			BackendFolderText.Text = "Backend: " + paths.StartBat;
		}
		else
		{
			_autoLocateFailed = true;
			Log(message + " Click “Set Game Folder” to choose it manually.");
			GameFolderText.Text = "Game: not detected";
			BackendFolderText.Text = "Backend: not detected";
		}
		SetBackendBadge(Launcher.IsBackendUp());
		UpdateButtons();
	}

	private async Task<LauncherPaths?> EnsurePathsAsync()
	{
		if (_paths != null)
		{
			return _paths;
		}
		if (!_autoLocateFailed && Launcher.TryAutoLocate(out LauncherPaths paths, out string _))
		{
			_paths = paths;
			return paths;
		}
		return await PickFolderAsync();
	}

	private async Task<LauncherPaths?> PickFolderAsync(bool launching = false, string? expectedBuild = null)
	{
        if((_busy && !launching) || _sessionStarted || (_paths!=null && SessionProcesses.ClientRunning(_paths))){SetStatus("Quit Fortnite before changing the game folder.",okay:false);return null;}
		OpenFolderDialog openFolderDialog = new OpenFolderDialog
		{
			Title = "Select extracted Fortnite "+(expectedBuild ?? "1.7.2, 1.8.2, 1.11, 3.5.1, 4.5, 5.10, 6.20, 7.40, 8.50, 9.40 or 10.31")+" (folder containing FortniteGame and Engine)",
			Multiselect = false
		};
		if (openFolderDialog.ShowDialog(this) != true)
		{
			return null;
		}
		if (!Launcher.TryLocateRoot(openFolderDialog.FolderName, out LauncherPaths paths, out string message, expectedBuild))
		{
			Log("ERROR: " + message);
			SetStatus("That folder doesn't look like a Fortnite build. Pick the folder that contains FortniteGame and Engine.", okay: false);
			return null;
		}
		_paths = paths;
		_autoLocateFailed = false;
		Log(message);
		RefreshPathLabels();
		SetStatus("Game folder saved. Press Launch when ready.");
        RefreshSeasonState();   // PLAY replaces LINK FOLDER right away (it used to wait for a restart)
		UpdateButtons();
		return paths;
	}

	private void RefreshPathLabels()
	{
		if (_paths != null)
		{
			GameFolderText.Text = "Game: " + _paths.GameExe;
			BackendFolderText.Text = "Backend: " + _paths.StartBat;
		}
	}

	private async void LaunchButton_Click(object sender, RoutedEventArgs e)
	{
		if (_download != null)
		{
			_download.Cancel();
		}
		else
		{
			if (_busy || _authBusy)
			{
				return;
			}
			_busy = true;
			UpdateButtons();
			try
			{
                if (_sessionStarted)
                {
                    var activePaths=_paths ?? await EnsurePathsAsync();
                    if(activePaths!=null)await LeaveSyndicateSession(activePaths);
                    return;
                }
				string requestedBuild=(sender as Button)?.Tag as string ?? "4.5";
                LauncherPaths paths;
                if(!Launcher.TrySelectBuild(requestedBuild,out paths,out var selectionMessage))paths=await PickFolderAsync(true,requestedBuild);
                _paths=paths;
				if (paths == null)
				{
					SetStatus("No game folder selected.", okay: false);
					return;
				}
                if (!GameDownload.Ready(paths))
                {
                    var selected = await PickFolderAsync(launching: true, expectedBuild: requestedBuild);
                    if (selected == null) { SetStatus("Step 1: choose your Fortnite folder (the one with FortniteGame and Engine inside)."); return; }
                    paths = selected;
                }
				var detectedBuild=await Task.Run(()=>GameBuild.Detect(paths.GameExe));
                Log("Detected Fortnite "+detectedBuild+" from executable.");
                GameServer.SelectBuild(paths,detectedBuild);
                // Save the World loot is PARADOX only: re-affirm the stw-loot.on flag from the launcher's PARADOX
                // selection right before the server starts, so a plain Season X launch (10.31) always gets normal
                // loot even if an earlier PARADOX session left the flag behind.
                SetParadoxStwLoot(_paradoxLoot && detectedBuild == "10.31");
                SetStatus("Preparing Fortnite "+detectedBuild+"...");
				Log("---- Launch sequence ----");
				if (!Launcher.EnsureRedirectNextToExe(paths))
				{
					// The release unpacker checks every file before the launcher starts, so a missing cobalt.dll
					// was removed afterwards - almost always by antivirus when it was loaded into Fortnite.
					var antivirus = await Task.Run(Launcher.AntivirusNames);
					var folder = AppContext.BaseDirectory.TrimEnd('\\');
					var who = antivirus.Length > 0 ? string.Join(" / ", antivirus) : "your antivirus";
					SetStatus($"cobalt.dll (connects Fortnite to Syndicate) was removed, most likely by {who}. Close Syndicate and open it again to restore it, then add this folder as an exclusion in {who}: {folder}", okay: false);
					Log("ERROR: cobalt.dll is missing next to SyndicateLauncher.exe in " + folder + ". Antivirus installed: " + (antivirus.Length > 0 ? string.Join(", ", antivirus) : "unknown") + ". Check that antivirus's quarantine, not only Windows Defender's.");
					return;
				}
				string redirectDll = Launcher.RedirectDllForInjection();
				Log("Redirect DLL: " + redirectDll);
				RadminConnection.Configure(paths);
				if (RadminConnection.Remote)
				{
					if (!_account.SignedIn)
					{
						ShowAuth();
						SetStatus("Sign in first — then press Launch.");
						return;
					}
					await RadminConnection.EnsureConnected();
				}
				await App.WaitForComponents(message => { SetStatus(message); Log(message); });
				// Port and process checks on a worker thread: on a busy PC they took seconds and froze the window.
				if (await Task.Run(() => Launcher.IsBackendUp()))
				{
					if (!await Task.Run(() => SessionProcesses.OwnedRunning(paths, "backend")))
					{
						await EnsureAccountBackend(); // stops another Syndicate copy's services and starts this one's
						if (!await Task.Run(() => SessionProcesses.OwnedRunning(paths, "backend")))
							throw new InvalidOperationException("Another program is using Syndicate's backend port 3551. Close the other Syndicate launcher (and Fortnite), then press Play again.");
					}
					SetBackendBadge(up: true);
					Log("Backend already running on 127.0.0.1:3551.");
				}
				else
				{
					SetBackendBadge(up: false);
					Log("Starting backend: " + paths.StartBat);
					Process process = await Task.Run(() => Launcher.StartBackend(paths)); // off the window thread (MongoDB start waits up to 30 s)
					_backendPid = process.Id;
					Log($"Backend console started (PID {process.Id}). Waiting for it to answer...");
					if (!(await Launcher.WaitForBackendAsync(90000, Log, process, paths)))
					{
						SetBackendBadge(up: false);
						SetStatus("Account service didn't start. Open Settings > Repair Local Services, then try again.", okay: false);
						Log("ERROR: backend not reachable on 127.0.0.1:3551 within 90s.");
						return;
					}
					SetBackendBadge(up: true);
					Log("Backend is up.");
				}
				TaskCompletionSource<bool> hostLoading = new TaskCompletionSource<bool>(TaskCreationOptions.RunContinuationsAsynchronously);
				Task<Exception?> hostReady = Task.FromResult<Exception>(null);
				if (!RadminConnection.Remote)
				{
					hostReady = PrepareHost();
					await hostLoading.Task;
					bool flag = hostReady.IsCompleted;
					Exception earlyFailure = default(Exception);
					if (flag)
					{
						earlyFailure = await hostReady;
						flag = earlyFailure != null;
					}
					if (flag)
					{
						throw earlyFailure;
					}
				}
				else
				{
					SetStatus("Joining Radmin host " + RadminConnection.Host + "…");
				}
				string? text = await GameLoginCode();
				if (text == null)
				{
					ShowAuth();
					SetStatus("Type a name for your player, press LET'S GO, then press Play.", okay: false);
					Log("Launch stopped: no signed-in player, so Fortnite would open its login screen.");
					return;
				}
				string text2 = text;
				string? clientBuild = EpicLaunchStub.BuildOf(paths.GameExe);
				try { LegacyXmppTrust.Ensure(paths.GameExe, clientBuild, Log); } catch (Exception trustError) { Log("Season 1 chat certificate not installed: " + trustError.Message); }
				ProcessStartInfo startInfo = new ProcessStartInfo
				{
					FileName = paths.GameExe,
					Arguments = "-game -epicapp=Fortnite -epicenv=Prod -epiclocale=en-us -epicportal -skippatchcheck -USEALLAVAILABLECORES -nosplash" + EpicLaunchStub.ArgumentsFor(clientBuild) + ((text2 == null) ? "" : (" -AUTH_LOGIN=unused -AUTH_PASSWORD=" + text2 + " -AUTH_TYPE=exchangecode")),
					WorkingDirectory = paths.GameWorkDir,
					UseShellExecute = false
				};
				Log("Launching: " + paths.GameExe);
				NativeMethods.ConfigureSyndicateCompatibility(startInfo);
				var launchStubs = EpicLaunchStub.StartFrozen(paths.GameExe, clientBuild);
				try { StretchDesktopForGame(); } catch (Exception stretchError) { Log("Stretched desktop not set: " + stretchError.Message); }
				Process game = Process.Start(startInfo);
				EpicLaunchStub.Attach(game, launchStubs);
				if (game == null)
				{
					SetStatus("Fortnite didn't start. Check your game folder in Settings, then press Launch again.", okay: false);
					Log("ERROR: Process.Start returned null.");
					RestoreDesktop();
					return;
				}
                                _game = game;
                                OnGameStarted(game, clientBuild); // Discord status + PlayStation controller bridge
                                _ = HandFocusToGameAsync(game);   // GameFocus.cs: game to the front, launcher minimized
				_sessionStarted = true;
				// The season actually running: use the DETECTED build, not the button's requested Tag (which defaults
				// to 4.5). Otherwise an 11.00 host whose launch Tag didn't match advertised itself as Season 4.
				_runningSeason = ModernSeasons.FirstOrDefault(s => s.Build == detectedBuild)?.Number;
				SessionProcesses.Track(paths, "client", game);
				game.EnableRaisingEvents = true;
				game.Exited += delegate
				{
					base.Dispatcher.Invoke(delegate
					{
						if(RadminConnection.Remote || !SessionProcesses.OwnedRunning(paths,"host"))_matchWatch?.Cancel();
						Log("Fortnite exited.");
						RestoreDesktop();
						OnGameExited();
						RestoreAfterGame();   // GameFocus.cs: the launcher comes back
						SetStatus("Fortnite closed.");
						_game = null;
						UpdateButtons();
						_ = CheckGameExited(exitedNow: true); // back to PLAY without pressing QUIT
					});
				};
				SetStatus("Starting Fortnite… this takes about 30 seconds.");
				_ = CheckGameAudio(game, paths); // tells the player how to fix it when the game starts without sound
				// Chapter 2 (11.00): TiltedClient.dll sends the game to Syndicate's backend itself (an ordinary inline hook).
				// cobalt.dll's page-guard hook let some login requests through to Epic on 11.00 (the email/password screen).
				// Opt-in for now (chapter2-redirect-tilted.txt): the first test crashed 11.00 right after engine start.
				if (GameBuild.Chapter2 && clientBuild == "11.00" && File.Exists(HudFlag("chapter2-redirect-tilted.txt"))) await InjectChapter2RedirectAsync(game);
				else await InjectBackendRedirectAsync(game, redirectDll);
                var consoleDll=System.IO.Path.Combine(AppContext.BaseDirectory,"SyndicateConsole.dll");
                if(File.Exists(consoleDll) && !game.HasExited && await Task.Run(()=>ConsoleCompatibility.Supports(paths.GameExe)))
                {
                    try { await Task.Run(()=>NativeMethods.InjectDll((uint)game.Id,consoleDll));Log("In-game console module loaded; press Tilde (~)."); }
                    catch(Exception consoleError) { Log("In-game console could not load: "+consoleError.Message); }
                }
                else Log("Optional console skipped: missing module or game build not verified for the 4.00 console hook. Backend and matchmaking remain enabled.");
                // Sprint by default and turbo build on Xbox controllers (every season): TiltedPadAssist.dll makes L3 read
                // as held while the left stick is pushed forward, only while Settings > Modded has it on.
                var padDll=System.IO.Path.Combine(AppContext.BaseDirectory,"TiltedPadAssist.dll");
                if(_runningSeason is >= 0 && File.Exists(padDll) && !game.HasExited)
                {
                    try { await Task.Run(()=>NativeMethods.InjectDll((uint)game.Id,padDll));Log("Controller sprint helper loaded."); }
                    catch(Exception padError) { Log("Controller sprint helper could not load: "+padError.Message); }
                }
                // Client mods (TiltedClient.dll): custom FOV (Settings > Display), turbo build, edit on release and
                // bubble wrap builds (Settings > Modded), every season. Loaded with every season so they can be
                // turned on or changed while Fortnite runs; it does nothing while all are off.
                var clientDll=ClientModsDll();
                if(File.Exists(clientDll) && !game.HasExited)
                {
                    try { await Task.Run(()=>NativeMethods.InjectDll((uint)game.Id,clientDll));Log("Client mods loaded (FOV and turbo build can be changed in Settings while you play).");ClientModsLog($"launcher: loaded {clientDll} into pid {game.Id} (season {_runningSeason}, FOV {CustomFov:0.#}, turbo build {(TurboBuildEnabled?"on":"off")}, edit on release {(EditOnReleaseEnabled?"on":"off")} key {EditKey})");}
                    catch(Exception clientError) { Log("Client mods could not load: "+clientError.Message);ClientModsLog("launcher: could not load TiltedClient.dll: "+clientError.Message); }
                }
                else { Log("TiltedClient.dll is missing: run UPDATE FROM GIT (it builds with the game server). FOV and turbo build are off.");ClientModsLog("launcher: TiltedClient.dll missing in "+AppContext.BaseDirectory); }
				Exception ex2 = await hostReady;
				if (ex2 != null)
				{
					throw ex2;
				}
				if (!game.HasExited)
				{
					StartMatchWatch(paths);
					SetStatus("Choose Solo, Duos, or Squads in Fortnite, then press Ready.");
				}
				async Task<Exception?> PrepareHost()
				{
					try
					{
						await GameServer.PrepareLobby(paths, delegate(string message)
						{
							SetStatus(message);
							Log(message);
						});
						return null;
					}
					catch (Exception ex4)
					{
						Log("Server startup failed: " + ex4.Message);
						return ex4;
					}
					finally
					{
						hostLoading.TrySetResult(result: true);
					}
				}
			}
			catch (Exception ex3)
			{
				SetStatus("Error: " + ex3.Message, okay: false);
				Log($"ERROR: {ex3}");
			}
			finally
			{
				_busy = false;
				UpdateButtons();
			}
		}
	}

	// Login code passed to Fortnite so it signs in straight away. Never launches without one: if the
	// launcher's session went stale (services restarted), it signs the saved player back in and retries.
	private async Task<string?> GameLoginCode()
	{
		for (int attempt = 0; attempt < 3; attempt++)
		{
			try
			{
				if (!_account.SignedIn && !(RememberedLogin.Exists && await _account.Restore()))
				{
					// No player chosen yet: pick one for them (the first saved player on this PC) instead of stopping.
					// Without a player Fortnite opened its Epic email/password screen and then "Login Failed".
					var saved = await _account.Players();
					if (saved.Length == 0) return null; // nobody on this PC yet: the picker asks for a name
					await _account.QuickPlay(saved[0]);
					AccountButton.Content = _account.Username + "  ·  SWITCH";
					Log("No player was selected: playing as " + _account.Username + " (press the player button to switch).");
				}
				return await _account.Exchange();
			}
			catch (Exception ex)
			{
				Log($"Login code attempt {attempt + 1} failed: {ex.Message}");
				if (attempt == 2) throw;
				try { if (RememberedLogin.Exists) await _account.Restore(); } catch (Exception restoreError) { Log("Re-sign-in failed: " + restoreError.Message); }
				await Task.Delay(1500);
			}
		}
		return null;
	}

#if TILTED_DEVKIT
	// Dev Kit: open a SECOND Fortnite client as a different saved player, joined to the same match, for duo
	// self-testing. Kept out of the main Play path (its own method), uses a separate -UserDir so the two clients
	// don't share settings, and never disturbs the primary account's saved login. Phase 1 = launch only; splitting
	// input between keyboard/mouse and a controller comes later.
	private async void LaunchSecondClient()
	{
		var paths = _paths;
		if (paths == null) { Log("2nd client: open the home screen once so Syndicate finds its files."); return; }
		if (!_sessionStarted && !SessionProcesses.ClientRunning(paths)) { Log("2nd client: press Play on your own game first, then launch the second player."); return; }
		if (!File.Exists(paths.GameExe)) { Log("2nd client: no Fortnite folder is linked."); return; }
		ClearInputRoles(); // a launcher that closed mid-session can leave old role files behind
		try
		{
			var account2 = new AccountClient();
			var players = await account2.Players();
			var mine = (_account.Username ?? "").Trim();
			var second = players.FirstOrDefault(p => !p.Equals(mine, StringComparison.OrdinalIgnoreCase));
			if (second == null) { Log("2nd client: make a second player first (the player button, then add one), then try again."); return; }
			// QuickPlay overwrites the saved login, so keep the primary's and put it back right after.
			var primaryRemember = RememberedLogin.Load();
			string code2;
			try { await account2.QuickPlay(second); code2 = await account2.Exchange(); }
			finally { if (primaryRemember != null) RememberedLogin.Save(primaryRemember); else RememberedLogin.Clear(); }

			var clientBuild = EpicLaunchStub.BuildOf(paths.GameExe);
			var userDir = System.IO.Path.Combine(paths.Root, "Runtime", "Client2", "UserData");
			Directory.CreateDirectory(userDir);
			var startInfo = new ProcessStartInfo
			{
				FileName = paths.GameExe,
				Arguments = "-game -epicapp=Fortnite -epicenv=Prod -epiclocale=en-us -epicportal -skippatchcheck -USEALLAVAILABLECORES -nosplash"
					+ EpicLaunchStub.ArgumentsFor(clientBuild)
					+ " -AUTH_LOGIN=unused -AUTH_PASSWORD=" + code2 + " -AUTH_TYPE=exchangecode"
					+ " -UserDir=\"" + userDir + "\"",
				WorkingDirectory = paths.GameWorkDir,
				UseShellExecute = false
			};
			NativeMethods.ConfigureSyndicateCompatibility(startInfo);
			var stubs = EpicLaunchStub.StartFrozen(paths.GameExe, clientBuild);
			var game = Process.Start(startInfo);
			EpicLaunchStub.Attach(game, stubs);
			if (game == null) { Log("2nd client: Fortnite did not start."); return; }
			SessionProcesses.Track(paths, "client2", game);
			Log("2nd client: launched as " + second + " (own settings in Runtime\\Client2).");
			// Split the input: your game keeps keyboard/mouse, the 2nd player gets the controller ("Swap 2nd player
			// controls" flips it). The roles are cleared when the 2nd client closes, so normal play is never filtered.
			_secondClient = game;
			WriteInputRoles();
			game.EnableRaisingEvents = true;
			game.Exited += (_, __) => { _secondClient = null; ClearInputRoles(); };
			await InjectBackendRedirectAsync(game, Launcher.RedirectDllForInjection());
			var clientDll = ClientModsDll();
			if (File.Exists(clientDll) && !game.HasExited)
			{
				try { await Task.Run(() => NativeMethods.InjectDll((uint)game.Id, clientDll)); Log("2nd client: mods loaded."); }
				catch (Exception e) { Log("2nd client: mods could not load: " + e.Message); }
			}
		}
		catch (Exception ex) { Log("2nd client failed: " + ex.Message); }
	}

	// Input split between the two clients: TiltedClient.dll in each one reads input-role-<pid>.txt ("kbm" or
	// "controller") from %LOCALAPPDATA%\ProjectTilted and ignores the other kind of device.
	private Process? _secondClient;
	private bool _inputSwapped;
	private static string InputRoleFolder => System.IO.Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "ProjectTilted");

	private void WriteInputRoles()
	{
		var second = _secondClient;
		if (second == null || second.HasExited) { ClearInputRoles(); Log("2nd client: launch the 2nd player first."); return; }
		var mine = _inputSwapped ? "controller" : "kbm";
		var theirs = _inputSwapped ? "kbm" : "controller";
		try
		{
			Directory.CreateDirectory(InputRoleFolder);
			foreach (var p in Process.GetProcessesByName("FortniteClient-Win64-Shipping"))
				using (p) File.WriteAllText(System.IO.Path.Combine(InputRoleFolder, "input-role-" + p.Id + ".txt"), p.Id == second.Id ? theirs : mine);
			Log("2nd client: your game uses " + (mine == "kbm" ? "keyboard + mouse" : "the controller") + ", the 2nd player uses " + (theirs == "kbm" ? "keyboard + mouse" : "the controller") + ".");
		}
		catch (Exception e) { Log("2nd client: could not split the controls: " + e.Message); }
	}

	private void SwapInputRoles()
	{
		_inputSwapped = !_inputSwapped;
		WriteInputRoles();
	}

	// No UI here: also called from the 2nd client's Exited event (a background thread).
	private static void ClearInputRoles()
	{
		try { if (Directory.Exists(InputRoleFolder)) foreach (var f in Directory.GetFiles(InputRoleFolder, "input-role-*.txt")) { try { File.Delete(f); } catch { } } } catch { }
	}
#endif

	// TiltedClient.dll as a copy in Runtime\ClientMods, so Launcher\TiltedClient.dll is never locked by a running game
	// and UPDATE FROM GIT can always replace it. Falls back to the launcher folder's file.
	private string ClientModsDll()
	{
		var clientDll = System.IO.Path.Combine(AppContext.BaseDirectory, "TiltedClient.dll");
		try
		{
			if (File.Exists(clientDll))
			{
				var copies = System.IO.Path.Combine(_paths?.Root ?? AppContext.BaseDirectory, "Runtime", "ClientMods"); Directory.CreateDirectory(copies);
				string stamp; using (var sha = System.Security.Cryptography.SHA256.Create()) using (var read = File.OpenRead(clientDll)) stamp = Convert.ToHexString(sha.ComputeHash(read))[..12];
				var copy = System.IO.Path.Combine(copies, "SyndicateClient-" + stamp + ".dll");
				if (!File.Exists(copy)) File.Copy(clientDll, copy, true);
				foreach (var stale in Directory.GetFiles(copies, "SyndicateClient-*.dll")) if (!string.Equals(stale, copy, StringComparison.OrdinalIgnoreCase)) try { File.Delete(stale); } catch { }
				clientDll = copy;
			}
		}
		catch (Exception error) { Log("Client mods load from the launcher folder (" + error.Message + ")."); }
		return clientDll;
	}

	// Chapter 2: load TiltedClient.dll right away; its redirect guard hooks curl as soon as it loads (no cobalt.dll).
	// Loading it again later for the client mods is harmless (the same file is already loaded).
	private async Task InjectChapter2RedirectAsync(Process game)
	{
		var dll = ClientModsDll();
		if (!File.Exists(dll))
		{
			SetStatus("TiltedClient.dll is missing, so Chapter 2 cannot reach Syndicate. Run UPDATE FROM GIT, then Launch again.", okay: false);
			throw new InvalidOperationException("TiltedClient.dll is missing (Chapter 2 needs it to connect).");
		}
		for (int attempt = 1; attempt <= 30 && !game.HasExited; attempt++)
		{
			try
			{
				await Task.Run(() => NativeMethods.InjectDll((uint)game.Id, dll));
				Log("Chapter 2: TiltedClient.dll loaded early; it connects Fortnite to Syndicate (no cobalt.dll).");
				ClientModsLog("launcher: Chapter 2 redirect, loaded " + dll + " into pid " + game.Id);
				SetStatus("Connected. Have fun!");
				return;
			}
			catch (Exception ex) { Log($"Chapter 2 redirect attempt {attempt}: {ex.Message}"); await Task.Delay(1000); }
		}
		if (!game.HasExited) throw new InvalidOperationException("TiltedClient.dll did not load. Close Fortnite and launch again.");
	}

	private async Task InjectBackendRedirectAsync(Process game, string dllPath)
	{
		DateTime deadline = DateTime.UtcNow.AddSeconds(90.0);
		int attempt = 0;
		while (DateTime.UtcNow < deadline)
		{
			if (game.HasExited)
			{
				Log("Game exited during injection wait.");
				return;
			}
			if (await Task.Run(() => IsRedirectLoaded(game)))
			{
				Log("Cobalt redirect loaded into Fortnite.");
				SetStatus("Connected. Have fun!");
				return;
			}
			attempt++;
			try
			{
				// On a worker thread: InjectDll waits up to 15 s for the game to finish loading the DLL, which froze the
				// launcher window ("froze for 14.6 s" right after Launching).
				await Task.Run(() => NativeMethods.InjectDll((uint)game.Id, dllPath));
				Log($"Injection attempt {attempt}: LoadLibrary(cobalt.dll) OK.");
			}
			catch (Exception ex)
			{
				Log($"Injection attempt {attempt}: {ex.Message}");
				await Task.Delay(2500);
				continue;
			}
			await Task.Delay(2000);
		}
		if (await Task.Run(() => IsRedirectLoaded(game)))
		{
			Log("Cobalt redirect loaded into Fortnite.");
			SetStatus("Connected. Have fun!");
		}
		else
		{
			SetStatus("Fortnite started, but the connection fix could not be confirmed. If the lobby won't load, press Quit and Launch again.", okay: false);
			throw new InvalidOperationException("Cobalt did not load. Close Fortnite and relaunch before matchmaking.");
		}
	}

	private static bool IsRedirectLoaded(Process p)
	{
		try
		{
			p.Refresh();
			foreach (ProcessModule module in p.Modules)
			{
				if (module.ModuleName.Equals("cobalt.dll", StringComparison.OrdinalIgnoreCase))
				{
					return true;
				}
			}
		}
		catch
		{
		}
		return false;
	}

	private async void StopBackendButton_Click(object sender, RoutedEventArgs e)
	{
		if(_paths!=null && GameServer.HostMayHavePlayers(_paths)){SetStatus("Players are still connected. Leave the match running until everyone returns to the lobby.");return;}
		int pid = _backendPid;
		if (pid > 0)
		{
			SetStatus("Stopping local services…");
			await Task.Run(delegate
			{
				Launcher.StopBackend(pid);
			});
			// Testing must never stop processes belonging to another installation.
			_backendPid = 0;
			SetBackendBadge(up: false);
			SetStatus("Local services stopped.");
			Log("Backend console stopped.");
			UpdateButtons();
		}
	}

	private static void UnusedLegacyKillBackendNodeProcesses()
	{
		try
		{
			Process[] processesByName = Process.GetProcessesByName("node");
			foreach (Process process in processesByName)
			{
				try
				{
					string text = process.MainModule?.FileName ?? "";
					if ((System.IO.Path.GetDirectoryName(text) ?? "").Contains("Helix-backend-main", StringComparison.OrdinalIgnoreCase) || (System.IO.Path.GetDirectoryName(text) ?? "").Contains(BackendFolder.Name, StringComparison.OrdinalIgnoreCase) || text.Contains("Helix", StringComparison.OrdinalIgnoreCase))
					{
						process.Kill(entireProcessTree: true);
					}
				}
				catch
				{
				}
			}
		}
		catch
		{
		}
	}

	private async void SetGameFolderButton_Click(object sender, RoutedEventArgs e)
    {
        try { await PickFolderAsync(); }
        catch(Exception ex) {Log("ERROR: "+ex.Message);SetStatus("Could not save the game folder.",okay:false);}
    }

	private async void OpenFolderButton_Click(object sender, RoutedEventArgs e)
	{
		LauncherPaths launcherPaths = await EnsurePathsAsync();
		if (launcherPaths == null)
		{
			return;
		}
		try
		{
			Process.Start(new ProcessStartInfo
			{
				FileName = launcherPaths.GameInstallRoot,
				UseShellExecute = true
			});
		}
		catch (Exception ex)
		{
			Log("ERROR: could not open folder: " + ex.Message);
		}
	}

	[DebuggerNonUserCode]
	[GeneratedCode("PresentationBuildTasks", "10.0.11.0")]
	public void InitializeComponent()
	{
		if (!_contentLoaded)
		{
			_contentLoaded = true;
			Uri resourceLocator = new Uri("/SyndicateLauncher;component/mainwindow.xaml", UriKind.Relative);
			Application.LoadComponent(this, resourceLocator);
		}
	}

	[DebuggerNonUserCode]
	[GeneratedCode("PresentationBuildTasks", "10.0.11.0")]
	[EditorBrowsable(EditorBrowsableState.Never)]
	void IComponentConnector.Connect(int connectionId, object target)
	{
		switch (connectionId)
		{
		case 1:
			((Grid)target).MouseLeftButtonDown += TitleBar_MouseLeftButtonDown;
			break;
		case 2:
			((Button)target).Click += FortniteTab_Click;
			break;
		case 3:
			((Button)target).Click += PassesTab_Click;
			break;
		case 4:
			AccountButton = (Button)target;
			AccountButton.Click += AccountButton_Click;
			break;
		case 5:
			((Button)target).Click += SettingsButton_Click;
			break;
		case 6:
			((Button)target).Click += Minimize_Click;
			break;
		case 7:
			((Button)target).Click += Maximize_Click;
			break;
		case 8:
			((Button)target).Click += CloseWindow_Click;
			break;
		case 9:
			BackendDot = (Ellipse)target;
			break;
		case 10:
			BackendBadgeText = (TextBlock)target;
			break;
		case 11:
			LaunchControls = (Grid)target;
			break;
		case 12:
			LaunchButton = (Button)target;
			LaunchButton.Click += LaunchButton_Click;
			break;
		case 13:
			((Button)target).Click += SettingsButton_Click;
			break;
		case 14:
			DownloadPanel = (Border)target;
			break;
		case 15:
			DownloadTitle = (TextBlock)target;
			break;
		case 16:
			SpeedLine = (Polyline)target;
			break;
		case 17:
			DownloadBar = (ProgressBar)target;
			break;
		case 18:
			DownloadPercent = (TextBlock)target;
			break;
		case 19:
			PauseDownloadButton = (Button)target;
			PauseDownloadButton.Click += PauseDownload_Click;
			break;
		case 20:
			StatusText = (TextBlock)target;
			break;
		case 21:
			SocialArt = (Border)target;
			break;
		case 22:
			BackgroundHost = (Border)target;
			break;
		case 23:
			ClassicArt = (Border)target;
			break;
		case 24:
			SeasonArt = (Border)target;
			break;
		case 25:
			KnightArt = (Border)target;
			break;
		case 26:
			LogToggleButton = (Button)target;
			LogToggleButton.Click += LogToggleButton_Click;
			break;
		case 27:
			PassShop = (Border)target;
			break;
		case 28:
			((Button)target).Click += RefreshPasses_Click;
			break;
		case 29:
			PassBalance = (TextBlock)target;
			break;
		case 30:
			PassMessage = (TextBlock)target;
			break;
		case 31:
			PassCards = (StackPanel)target;
			break;
		case 32:
			LogPanel = (Border)target;
			break;
		case 33:
			((Button)target).Click += LogToggleButton_Click;
			break;
		case 34:
			GameFolderText = (TextBlock)target;
			break;
		case 35:
			BackendFolderText = (TextBlock)target;
			break;
		case 36:
			LogBox = (TextBox)target;
			break;
		case 37:
			SettingsPanel = (Border)target;
			break;
		case 38:
			ModeSelector = (ComboBox)target;
			ModeSelector.SelectionChanged += ModeSelector_Changed;
			break;
		case 39:
			RadminHostBox = (TextBox)target;
			break;
		case 40:
			SaveRadminButton = (Button)target;
			SaveRadminButton.Click += SaveRadmin_Click;
			break;
		case 41:
			RadminStatus = (TextBlock)target;
			break;
		case 42:
			((Button)target).Content = "Set game folder";
			((Button)target).Click += SetGameFolderButton_Click;
			break;
		case 43:
			StopBackendButton = (Button)target;
			StopBackendButton.Click += StopBackendButton_Click;
			break;
		case 44:
			((Button)target).Click += SettingsButton_Click;
			break;
		case 45:
			AuthOverlay = (Border)target;
			break;
		case 46:
			((Button)target).Click += CloseAuth_Click;
			break;
		case 47:
			AuthHeading = (TextBlock)target;
			break;
		case 48:
			AuthSubtitle = (TextBlock)target;
			break;
		case 49:
			UsernamePanel = (StackPanel)target;
			break;
		case 50:
			UsernameBox = (TextBox)target;
			break;
		case 51:
			EmailBox = (TextBox)target;
			break;
		case 52:
			PasswordInput = (PasswordBox)target;
			break;
		case 53:
			ConfirmPanel = (StackPanel)target;
			break;
		case 54:
			ConfirmPasswordInput = (PasswordBox)target;
			break;
		case 55:
			RememberEmail = (CheckBox)target;
			break;
		case 56:
			AuthMessage = (TextBlock)target;
			break;
		case 57:
			AuthSubmit = (Button)target;
			AuthSubmit.Click += AuthSubmit_Click;
			break;
		case 58:
			AuthSwitch = (Button)target;
			AuthSwitch.Click += AuthSwitch_Click;
			break;
		default:
			_contentLoaded = true;
			break;
		}
	}
}