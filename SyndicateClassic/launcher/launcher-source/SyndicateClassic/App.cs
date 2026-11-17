// Syndicate © 2026 Syndicate. All rights reserved. Proprietary — no copying, distribution, or derivative works. See LICENSE.
using System;
using System.CodeDom.Compiler;
using System.ComponentModel;
using System.Diagnostics;
using System.Linq;
using System.Threading.Tasks;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Media;

namespace SyndicateClassic;

public class App : Application
{
    internal static string? ComponentFailure;
    // Local match components (backend, matchmaker, game server DLL) update in the background while the
    // launcher is already open. Anything that starts local services waits for this first.
    internal static Task ComponentsReady = Task.CompletedTask;
    internal static async Task WaitForComponents(Action<string>? status = null)
    {
        if (!ComponentsReady.IsCompleted) status?.Invoke("Finishing background setup (account backup and match components)…");
        await ComponentsReady;
    }
    // Controller libraries (HidSharp, Nefarius.ViGEm.Client) sit next to SyndicateLauncher.dll but are not
    // in the original deps.json, so .NET would not find them on its own: load them from there.
    // Startup timing, written to the diagnostics log once the window is up (to find what is slow).
    internal static readonly System.Diagnostics.Stopwatch StartupClock = System.Diagnostics.Stopwatch.StartNew();
    internal static readonly System.Collections.Generic.List<string> StartupMarks = new();
    internal static void Mark(string phase) { lock (StartupMarks) StartupMarks.Add(phase + " " + StartupClock.ElapsedMilliseconds + " ms"); }

    static App()
    {
        AppDomain.CurrentDomain.AssemblyResolve += (_, args) =>
        {
            var name = new System.Reflection.AssemblyName(args.Name).Name;
            if (name is not ("HidSharp" or "Nefarius.ViGEm.Client")) return null;
            var path = System.IO.Path.Combine(AppContext.BaseDirectory, name + ".dll");
            return System.IO.File.Exists(path) ? System.Reflection.Assembly.LoadFrom(path) : null;
        };
    }
	protected override async void OnStartup(StartupEventArgs e)
	{
		// Release self-test: "--tilted-selftest <file>" opens the main window, writes OK/FAIL and exits.
		// build-launcher.ps1 runs this on the obfuscated public build so a build that obfuscation broke
		// is caught before it is ever handed out.
		int selfTestArg = Array.IndexOf(e.Args, "--tilted-selftest");
		if (selfTestArg >= 0 && selfTestArg + 1 < e.Args.Length)
		{
			string outFile = e.Args[selfTestArg + 1];
			void Finish(string result, int code)
			{
				try { System.IO.File.WriteAllText(outFile, result); } catch { }
				Environment.Exit(code);
			}
			DispatcherUnhandledException += (_, a) => Finish("FAIL " + a.Exception, 1);
			try
			{
				base.OnStartup(e);
				base.ShutdownMode = ShutdownMode.OnExplicitShutdown;
				var window = new MainWindow();
				MainWindow = window;
				window.ContentRendered += (_, __) =>
				{
					// Anonymous-type and record JSON breaks if obfuscation renames the names the serializer
					// matches on; a quick round-trip catches that in addition to the window rendering.
					try
					{
						string anon = System.Text.Json.JsonSerializer.Serialize(new { a = "x", b = true });
						if (!anon.Contains("\"b\":true")) throw new InvalidOperationException("anonymous JSON changed: " + anon);
					}
					catch (Exception ex) { Finish("FAIL JSON " + ex, 1); return; }
					Finish("OK", 0);
				};
				var timeout = new System.Windows.Threading.DispatcherTimer { Interval = TimeSpan.FromSeconds(45) };
				timeout.Tick += (_, __) => Finish("FAIL window never rendered", 1);
				timeout.Start();
				window.Show();
			}
			catch (Exception ex) { Finish("FAIL " + ex, 1); }
			return;
		}
		CrashGuard.Install(this); // crash/error logging before anything else can fail
		BuildProvenance.Check();  // verify this is a genuine, unmodified Syndicate build (never blocks; just sets the state)
		base.OnStartup(e);
		base.ShutdownMode = ShutdownMode.OnExplicitShutdown;
		TextBlock label = new TextBlock
		{
			Text = "Starting Syndicate Launcher…",
			Foreground = Brushes.White,
			Margin = new Thickness(28.0),
			VerticalAlignment = VerticalAlignment.Center,
			TextWrapping = TextWrapping.Wrap
		};
		Window splash = new Window
		{
			Title = "Syndicate Launcher",
			Width = 430.0,
			Height = 140.0,
			ResizeMode = ResizeMode.NoResize,
			WindowStartupLocation = WindowStartupLocation.CenterScreen,
			Background = new SolidColorBrush(Color.FromRgb(28, 43, 54)),
			Content = label
		};
		splash.Closing += delegate(object? _, CancelEventArgs args)
		{
			args.Cancel = true;
		};
        var repairMarker=System.IO.Path.Combine(AppContext.BaseDirectory,"repair.pending");
        bool repairing=System.IO.File.Exists(repairMarker);
        // No splash on a normal start: the main window opens straight away and the account backup and
        // component update run in the background. The small window only shows during a requested repair.
        if (repairing) splash.Show();
        Mark("splash");
        if (repairing)
        {
            // A requested repair restores files before anything starts, so the account backup runs first, up front.
            try
            {
                label.Text = "Preserving existing accounts…";
                await Task.Run(() => PortableServices.PreserveExistingAccounts(forceSnapshot: true));
            }
            catch (Exception ex)
            {
                MessageBox.Show(ex.Message, "Account backup needs attention");
                Shutdown();
                return;
            }
        }
		bool flag = !e.Args.Contains("--skip-update");
		if (flag)
		{
			flag = await LauncherUpdater.Check(delegate(string message)
			{
				label.Text = message;
			});
		}
		if (flag)
		{
			Shutdown();
			return;
		}
		Mark("accounts");
        if(repairing)
        {
            // A requested repair restores files before anything starts, so it still runs up front.
            try
            {
                label.Text = "Repairing local match components…";
                await Task.Run(() => ComponentUpdates.Apply());
            }
            catch (Exception ex)
            {
                ComponentFailure = ex.Message;
                MessageBox.Show(ex.Message + "\n\nSyndicate will still open. Fix the files above before starting a match (Settings > Repair Local Services re-checks them).", "Syndicate needs a quick repair");
            }
        }
        else
        {
            // Normal start: open the launcher right away. The account backup (a full copy of the account
            // database once a day) and the component update run in the background, in that order, before
            // any local service starts. Sign-in, Launch and Repair wait for it (App.WaitForComponents).
            var dispatcher = Dispatcher;
            ComponentsReady = Task.Run(() =>
            {
                try { PortableServices.PreserveExistingAccounts(); Mark("accounts (background)"); }
                catch (Exception ex)
                {
                    // Same rule as before: nothing is updated and no service starts until the accounts are safe.
                    ComponentFailure = "Account backup: " + ex.Message;
                    var accountError = ex.Message;
                    dispatcher.InvokeAsync(() => MessageBox.Show(accountError + "\n\nSyndicate will not start its local services until this is fixed. Close Syndicate, fix it, then reopen.", "Account backup needs attention"));
                    return;
                }
                try { ComponentUpdates.Apply(); }
                catch (Exception ex) { ComponentFailure = ex.Message; }
                Mark("components (background)");
                var failure = ComponentFailure; var warning = ComponentUpdates.Warning;
                if (failure != null)
                    dispatcher.InvokeAsync(() => MessageBox.Show(failure + "\n\nSyndicate is open, but fix the files above before starting a match (Settings > Repair Local Services re-checks them).", "Syndicate needs a quick repair"));
                else if (warning != null)
                    dispatcher.InvokeAsync(() => MessageBox.Show(warning, "Syndicate is ready (minor note)"));
            });
        }
        if(System.IO.File.Exists(repairMarker))try {
            if(!Launcher.TryAutoLocate(out var repaired,out var message))throw new System.IO.IOException(message);
            label.Text="Repair complete. Starting local account and matchmaking services...";
            await Task.Run(()=>Launcher.StartBackend(repaired));
            await GameServer.PrepareLobby(repaired,message=>label.Text=message);
            System.IO.File.Delete(repairMarker);
        }catch(Exception error){MessageBox.Show("Files repaired, but local services need attention: "+error.Message,"Syndicate repair");}
		Mark("components");
		MainWindow mainWindow = (MainWindow)(base.MainWindow = new MainWindow());
		Mark("window built");
		mainWindow.Show();
		CrashGuard.WatchForFreezes(Dispatcher);
		splash.Hide();
		base.ShutdownMode = ShutdownMode.OnMainWindowClose;
	}

	[STAThread]
	[DebuggerNonUserCode]
	[GeneratedCode("PresentationBuildTasks", "10.0.11.0")]
	public static void Main()
	{
		new App().Run();
	}
}