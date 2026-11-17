// Syndicate standalone bootstrapper.
// One encrypted exe the player downloads and runs. It carries the whole Syndicate install as an
// AES-256-GCM encrypted payload (embedded resource "Syndicate.Payload"). On every start it decrypts the
// payload in memory, then verifies every file on disk against a SHA-256 list that lives INSIDE the encrypted
// payload (so it cannot be forged) and re-extracts any file that is missing or changed - which is exactly
// what happens when antivirus quarantines one of the game DLLs. Because the payload contains everything,
// an AV-eaten file always restores instead of dead-ending at "bundle incomplete". Then it launches the
// real SyndicateLauncher.exe and waits. Saved accounts/settings live elsewhere and are never touched.
//
// Self-test modes (no UI, used by the build script to prove the bundle before shipping):
//   --verify-only <empty dir>   authenticate + fully extract + verify every hash
//   --exercise    <empty dir>   the above plus tamper-rejection, path-containment, repair and cleanup checks
using System;
using System.IO;
using System.IO.Compression;
using System.Reflection;
using System.Security.Cryptography;
using System.Diagnostics;
using System.Threading;
using System.Windows;
using System.Linq;

static class Bootstrap
{
    static string Version => Assembly.GetExecutingAssembly().GetName().Version?.ToString(3) ?? "1.0.0";
    static string FullVersion => Assembly.GetExecutingAssembly().GetName().Version?.ToString() ?? "1.0.0.0";
    const string ManifestName = "syndicate-hashes";
    const string ResourceName = "Syndicate.Payload";
    const string LauncherExe = "SyndicateLauncher.exe";

    static Window splash;
    static System.Windows.Controls.TextBlock status;
    static System.Windows.Controls.TextBlock detail;
    static System.Windows.Controls.ProgressBar bar;
    static bool closing;
    static readonly System.Text.RegularExpressions.Regex Percent = new(@"(\d{1,3})%");

    // Updates the splash. The first line is the headline; anything after a newline goes in the small grey line.
    // A "NN%" in the text drives the progress bar; without one the bar is hidden.
    static void Progress(string message)
    {
        if (status == null) return;
        var lines = message.Split('\n', 2);
        status.Text = lines[0].Trim();
        detail.Text = lines.Length > 1 ? lines[1].Trim() : "Your accounts and settings stay saved.";
        var m = Percent.Match(lines[0]);
        if (m.Success && int.TryParse(m.Groups[1].Value, out var pct))
        {
            bar.Visibility = Visibility.Visible;
            bar.Value = Math.Clamp(pct, 0, 100);
        }
        else bar.Visibility = Visibility.Hidden;
        status.Dispatcher.Invoke(() => { }, System.Windows.Threading.DispatcherPriority.Background);
    }

    static System.Windows.Media.SolidColorBrush Brush(uint argb)
    {
        var b = new System.Windows.Media.SolidColorBrush(System.Windows.Media.Color.FromArgb(
            (byte)(argb >> 24), (byte)(argb >> 16), (byte)(argb >> 8), (byte)argb));
        b.Freeze();
        return b;
    }

    static System.Windows.Media.ImageSource Logo()
    {
        try
        {
            using var s = Assembly.GetExecutingAssembly().GetManifestResourceStream("Syndicate.Logo");
            if (s == null) return null;
            var img = new System.Windows.Media.Imaging.BitmapImage();
            img.BeginInit();
            img.CacheOption = System.Windows.Media.Imaging.BitmapCacheOption.OnLoad;
            img.StreamSource = s;
            img.EndInit();
            img.Freeze();
            return img;
        }
        catch { return null; }
    }

    // A small borderless card in the launcher's style: dark glass, logo, SYNDICATE wordmark, status, thin white bar.
    static void ShowProgress(string first)
    {
        try
        {
            var display = new System.Windows.Media.FontFamily("Bahnschrift SemiCondensed, Bahnschrift, Segoe UI");
            var body = new System.Windows.Media.FontFamily("Segoe UI Variable Text, Segoe UI");

            var logo = new System.Windows.Controls.Border
            {
                Width = 52, Height = 52, CornerRadius = new CornerRadius(10), ClipToBounds = true,
                Background = Brush(0xFF2D2D2D), VerticalAlignment = VerticalAlignment.Top,
                Child = new System.Windows.Controls.Image { Source = Logo(), Stretch = System.Windows.Media.Stretch.UniformToFill }
            };
            var word = new System.Windows.Controls.TextBlock
            {
                Text = "SYNDICATE", FontFamily = display, FontSize = 24, FontWeight = FontWeights.Bold,
                FontStyle = FontStyles.Italic, Foreground = System.Windows.Media.Brushes.White
            };
            var rule = new System.Windows.Controls.Border
            {
                Width = 26, Height = 2, Background = System.Windows.Media.Brushes.White, HorizontalAlignment = HorizontalAlignment.Left,
                Margin = new Thickness(1, 3, 0, 10), Opacity = 0.85
            };
            status = new System.Windows.Controls.TextBlock
            {
                FontFamily = body, FontSize = 13, FontWeight = FontWeights.SemiBold, Foreground = Brush(0xFFF2F3F5),
                TextWrapping = TextWrapping.Wrap
            };
            detail = new System.Windows.Controls.TextBlock
            {
                FontFamily = body, FontSize = 11, Foreground = Brush(0xFF9A9A9A), Margin = new Thickness(0, 3, 0, 0),
                TextWrapping = TextWrapping.Wrap
            };
            bar = new System.Windows.Controls.ProgressBar
            {
                Height = 3, Minimum = 0, Maximum = 100, Margin = new Thickness(0, 12, 0, 0),
                Foreground = System.Windows.Media.Brushes.White, Background = Brush(0x1FFFFFFF), BorderThickness = new Thickness(0),
                Visibility = Visibility.Hidden
            };
            var text = new System.Windows.Controls.StackPanel { Margin = new Thickness(16, 0, 0, 0) };
            text.Children.Add(word);
            text.Children.Add(rule);
            text.Children.Add(status);
            text.Children.Add(detail);
            text.Children.Add(bar);

            var grid = new System.Windows.Controls.Grid();
            grid.ColumnDefinitions.Add(new System.Windows.Controls.ColumnDefinition { Width = GridLength.Auto });
            grid.ColumnDefinitions.Add(new System.Windows.Controls.ColumnDefinition());
            System.Windows.Controls.Grid.SetColumn(text, 1);
            grid.Children.Add(logo);
            grid.Children.Add(text);

            var card = new System.Windows.Controls.Border
            {
                CornerRadius = new CornerRadius(12), BorderThickness = new Thickness(1), BorderBrush = Brush(0x24FFFFFF),
                Padding = new Thickness(22, 20, 24, 20), Margin = new Thickness(12),
                Background = new System.Windows.Media.LinearGradientBrush(
                    System.Windows.Media.Color.FromArgb(0xFF, 0x16, 0x16, 0x18),
                    System.Windows.Media.Color.FromArgb(0xFF, 0x0B, 0x0B, 0x0C), 45),
                Effect = new System.Windows.Media.Effects.DropShadowEffect { BlurRadius = 18, ShadowDepth = 0, Opacity = 0.55 },
                Child = grid
            };
            var win = new Window
            {
                Title = "Syndicate",
                Width = 470,
                SizeToContent = SizeToContent.Height,
                ResizeMode = ResizeMode.NoResize,
                WindowStyle = WindowStyle.None,
                AllowsTransparency = true,
                Background = System.Windows.Media.Brushes.Transparent,
                WindowStartupLocation = WindowStartupLocation.CenterScreen,
                Content = card
            };
            win.MouseLeftButtonDown += (_, _) => { try { win.DragMove(); } catch { } };
            win.Closing += (_, e) => e.Cancel = !closing;
            win.Show();
            splash = win;
            Progress(first);
        }
        catch
        {
            // Some machines fail WPF font-cache init in a single-file app ("MS.Internal.FontCache..."). The
            // splash is only a loading graphic; if it can't show, continue headless so extraction and launch
            // still work. Progress() and HideProgress() already no-op when status/splash are null.
            status = null;
            detail = null;
            bar = null;
            splash = null;
        }
    }

    static void HideProgress() { closing = true; splash?.Close(); splash = null; status = null; detail = null; bar = null; }

    static string SafePath(string root, string name)
    {
        if (string.IsNullOrEmpty(name) || name.Contains(':') || name.Replace('\\', '/').Split('/').Any(p => p is "" or "." or ".."))
            throw new InvalidDataException("Invalid bundled path");
        var path = Path.GetFullPath(Path.Combine(root, name));
        var prefix = Path.GetFullPath(root).TrimEnd(Path.DirectorySeparatorChar) + Path.DirectorySeparatorChar;
        if (!path.StartsWith(prefix, StringComparison.OrdinalIgnoreCase))
            throw new InvalidDataException("Invalid bundled path");
        for (var dir = Path.GetDirectoryName(path); dir != null; dir = Path.GetDirectoryName(dir))
            if (Directory.Exists(dir) && (File.GetAttributes(dir) & FileAttributes.ReparsePoint) != 0)
                throw new IOException("Linked runtime folders are not supported");
        if (File.Exists(path) && (File.GetAttributes(path) & FileAttributes.ReparsePoint) != 0)
            throw new IOException("Linked runtime files are not supported");
        return path;
    }

    static byte[] Embedded()
    {
        using var s = Assembly.GetExecutingAssembly().GetManifestResourceStream(ResourceName)
            ?? throw new InvalidDataException("This build has no embedded payload.");
        using var m = new MemoryStream();
        s.CopyTo(m);
        return m.ToArray();
    }

    static byte[] Decrypt(byte[] encrypted)
    {
        if (encrypted.Length < 28) throw new InvalidDataException("Incomplete bundle");
        var plain = new byte[encrypted.Length - 28];
        using var aes = new AesGcm(PayloadKey.Bytes(), 16);
        aes.Decrypt(encrypted.AsSpan(0, 12), encrypted.AsSpan(28), encrypted.AsSpan(12, 16), plain);
        return plain;
    }

    static string Hash(Stream s) => Convert.ToHexString(SHA256.HashData(s));

    static bool Matches(ZipArchiveEntry e, string file)
    {
        try
        {
            if (!File.Exists(file) || new FileInfo(file).Length != e.Length) return false;
            using var a = e.Open();
            using var b = File.OpenRead(file);
            return Hash(a) == Hash(b);
        }
        catch (IOException) { return false; }
    }

    // When every file on disk already matches the in-payload hash list, skip re-extraction entirely.
    static bool Ready(ZipArchive zip, string root)
    {
        try
        {
            var manifest = zip.GetEntry(ManifestName);
            if (manifest == null) return false;
            string text;
            using (var r = new StreamReader(manifest.Open())) text = r.ReadToEnd();
            foreach (var raw in text.Split('\n'))
            {
                var line = raw.TrimEnd('\r');
                if (line.Length == 0) continue;
                if (line.Length < 66 || line[64] != ' ') return false;
                var hash = line.Substring(0, 64);
                var path = SafePath(root, line.Substring(65));
                if (!File.Exists(path)) return false;
                using var fs = File.OpenRead(path);
                if (!Hash(fs).Equals(hash, StringComparison.OrdinalIgnoreCase)) return false;
            }
            return true;
        }
        catch (Exception e) when (e is IOException or FormatException or UnauthorizedAccessException or InvalidDataException) { return false; }
    }

    // Fast start: the files from this exact payload were fully verified before (stamp), so only check that every
    // file is still there at the right size and that the launcher and game DLLs (what antivirus removes or
    // changes) still hash right. Anything off falls back to the full verify + repair in Extract.
    static bool QuickReady(ZipArchive zip, string root, string stampPath, string payloadId)
    {
        try
        {
            if (!File.Exists(stampPath) || File.ReadAllText(stampPath).Trim() != payloadId) return false;
            var manifest = zip.GetEntry(ManifestName);
            if (manifest == null) return false;
            var hashes = new System.Collections.Generic.Dictionary<string, string>(StringComparer.OrdinalIgnoreCase);
            using (var r = new StreamReader(manifest.Open()))
                foreach (var raw in r.ReadToEnd().Split('\n'))
                {
                    var line = raw.TrimEnd('\r');
                    if (line.Length >= 66 && line[64] == ' ') hashes[line.Substring(65)] = line.Substring(0, 64);
                }
            foreach (var e in zip.Entries)
            {
                if (e.FullName.EndsWith("/")) continue;
                var path = SafePath(root, e.FullName);
                var info = new FileInfo(path);
                if (!info.Exists || info.Length != e.Length) return false;
                bool critical = e.FullName.EndsWith(".dll", StringComparison.OrdinalIgnoreCase) && e.FullName.StartsWith("Runtime/dlls/", StringComparison.OrdinalIgnoreCase)
                    || e.FullName.Equals(LauncherExe, StringComparison.OrdinalIgnoreCase);
                if (critical && hashes.TryGetValue(e.FullName, out var want))
                {
                    using var fs = File.OpenRead(path);
                    if (!Hash(fs).Equals(want, StringComparison.OrdinalIgnoreCase)) return false;
                }
            }
            return true;
        }
        catch (Exception e) when (e is IOException or FormatException or UnauthorizedAccessException or InvalidDataException) { return false; }
    }

    // After an update, remove the extracted copies of older versions (each version lives in its own folder).
    static void RemoveOldVersions(string home)
    {
        try
        {
            var parent = Path.GetDirectoryName(home)!;
            foreach (var dir in Directory.GetDirectories(parent))
            {
                if (string.Equals(Path.GetFullPath(dir).TrimEnd('\\'), Path.GetFullPath(home).TrimEnd('\\'), StringComparison.OrdinalIgnoreCase)) continue;
                if (!File.Exists(Path.Combine(dir, "Syndicate.encrypted")) && !Directory.Exists(Path.Combine(dir, "RuntimeFiles"))) continue;
                try { KillLeftovers(Path.Combine(dir, "RuntimeFiles")); Directory.Delete(dir, true); } catch { }
            }
        }
        catch { }
    }

    static void Extract(ZipArchive zip, string root)
    {
        Directory.CreateDirectory(root);
        if (Ready(zip, root)) return;
        // Preflight every destination before changing any program file. Accounts are not in this archive.
        foreach (var e in zip.Entries)
        {
            var path = SafePath(root, e.FullName);
            if (File.Exists(path) && !Matches(e, path))
                using (new FileStream(path, FileMode.Open, FileAccess.ReadWrite, FileShare.None)) { }
        }
        int count = 0;
        foreach (var e in zip.Entries)
        {
            if (count++ % 200 == 0)
                Progress($"Installing the update\u2026 {count * 100 / zip.Entries.Count}%\nOnly happens after an update. Your accounts and settings stay saved.");
            var path = SafePath(root, e.FullName);
            if (Matches(e, path)) continue;
            Directory.CreateDirectory(Path.GetDirectoryName(path)!);
            var temp = path + ".restore-" + Guid.NewGuid().ToString("N");
            try
            {
                e.ExtractToFile(temp);
                if (!Matches(e, temp))
                    throw new IOException("File verification failed: " + e.FullName +
                        (File.Exists(temp) ? "" : " (the file was removed right after it was written)") +
                        "\n\nThis usually means Windows Security or another antivirus removed or changed it. " +
                        "Open Windows Security > Virus & threat protection > Protection history to see what it did, " +
                        "then add Syndicate to Exclusions.");
                File.Move(temp, path, true);
            }
            finally { if (File.Exists(temp)) File.Delete(temp); }
        }
        foreach (var e in zip.Entries)
            if (!Matches(e, SafePath(root, e.FullName)))
                throw new IOException("Bundle incomplete: " + e.FullName);
    }

    // Ask once, then add the Syndicate standalone folder to Windows Defender exclusions (with a UAC prompt).
    // Without this, Defender quarantines the injection DLLs as they are written and the game cannot start.
    // User-consented and transparent; any decline or failure is non-fatal (the verify step then guides the
    // player to add it by hand). A marker file means the player is asked at most once per machine.
    static void EnsureDefenderExclusion(string standaloneRoot)
    {
        try
        {
            Directory.CreateDirectory(standaloneRoot);
            var marker = Path.Combine(standaloneRoot, ".defender-asked");
            if (File.Exists(marker)) return;
            File.WriteAllText(marker, DateTime.UtcNow.ToString("O"));
            var choice = MessageBox.Show(
                "Syndicate's game files are often falsely flagged by antivirus, which can stop the game from starting.\n\n" +
                "Add Syndicate to Windows Security exclusions so its files are not removed? Windows will ask you to approve.",
                "Syndicate", MessageBoxButton.YesNo, MessageBoxImage.Information);
            if (choice != MessageBoxResult.Yes) return;
            var cmd = "-NoProfile -WindowStyle Hidden -Command \"Add-MpPreference -ExclusionPath '" +
                standaloneRoot.Replace("'", "''") + "'\"";
            var psi = new ProcessStartInfo("powershell.exe", cmd)
            { UseShellExecute = true, Verb = "runas", WindowStyle = ProcessWindowStyle.Hidden };
            using var p = Process.Start(psi);
            p?.WaitForExit(30000);
        }
        catch { }
    }

    // Close any process whose exe lives under the extracted folder (a leftover Node, Mongo or launcher from a
    // previous session). They hold the backend files open; without this a re-extract fails with "Access to the
    // path is denied". Scoped to the folder so unrelated apps are never touched. Reached only after the mutex is
    // free, so a Syndicate that is genuinely still open is caught earlier as "already running", not killed here.
    static void KillLeftovers(string root)
    {
        try
        {
            var prefix = Path.GetFullPath(root).TrimEnd(Path.DirectorySeparatorChar) + Path.DirectorySeparatorChar;
            foreach (var p in Process.GetProcesses())
            {
                try
                {
                    var exe = p.MainModule?.FileName;
                    if (exe != null && exe.StartsWith(prefix, StringComparison.OrdinalIgnoreCase))
                    {
                        p.Kill(entireProcessTree: true);
                        p.WaitForExit(5000);
                    }
                }
                catch { }
                finally { p.Dispose(); }
            }
        }
        catch { }
    }

    static int Cleanup(ZipArchive zip, string root)
    {
        int retained = 0;
        foreach (var e in zip.Entries)
        {
            var path = SafePath(root, e.FullName);
            try { if (File.Exists(path)) File.Delete(path); }
            catch (IOException) { retained++; }
            catch (UnauthorizedAccessException) { retained++; }
        }
        return retained;
    }

    [STAThread]
    static int Main(string[] args)
    {
        var home = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "SyndicateStandalone", Version);
        bool test = args.Length == 2 && (args[0] == "--verify-only" || args[0] == "--exercise");
        try
        {
            using var mutex = new Mutex(false, "Local\\SyndicateStandalone");
            bool acquired;
            try { acquired = mutex.WaitOne(test ? 0 : Updater.MutexWaitMs(args)); }
            catch (AbandonedMutexException) { acquired = true; }
            if (!acquired) { if (!test) MessageBox.Show("Syndicate is already running. Use its open launcher window.", "Syndicate"); return 2; }

            // Self-update: if a newer standalone exe is published to GitHub, download it, verify its SHA-256,
            // swap it in under the same name and relaunch - then this process exits. Any failure just continues
            // with the current version. Skipped in test mode and on the freshly-relaunched updated copy.
            if (!test)
            {
                ShowProgress("Starting Syndicate\u2026");
                if (Updater.TryUpdate(FullVersion, args, Progress, () => { try { mutex.ReleaseMutex(); } catch { } }))
                    return 0;
            }

            var root = test ? Path.GetFullPath(args[1]) : Path.Combine(home, "RuntimeFiles");
            if (test && Directory.Exists(root)) throw new IOException("Use a new empty test destination");
            var encrypted = Embedded();

            if (!test)
            {
                Directory.CreateDirectory(home);
                var vault = Path.Combine(home, "Syndicate.encrypted");
                SafePath(home, "Syndicate.encrypted");
                var payloadId = Convert.ToHexString(SHA256.HashData(encrypted));
                var vaultId = vault + ".sha256";
                bool valid = File.Exists(vault) && File.Exists(vaultId) &&
                    new FileInfo(vault).Length == encrypted.Length && File.ReadAllText(vaultId).Trim() == payloadId;
                if (!valid)
                {
                    var temp = vault + ".new";
                    File.WriteAllBytes(temp, encrypted);
                    File.Move(temp, vault, true);
                    File.WriteAllText(vaultId, payloadId);
                }
            }

            // Before writing any game DLL to disk, make sure antivirus won't eat it. Syndicate's injection DLLs
            // are false-flagged; without an exclusion Defender quarantines them as fast as they are written,
            // which is what "File verification failed" means. Asked once, consented, elevated via UAC.
            if (!test) EnsureDefenderExclusion(Path.GetDirectoryName(home)!);

            var bytes = Decrypt(encrypted);
            using var stream = new MemoryStream(bytes, false);
            using var zip = new ZipArchive(stream, ZipArchiveMode.Read);
            // A previous session can leave Node/Mongo/the launcher running from the extracted folder, holding
            // backend files open - which turns a re-extract into "Access to the path is denied". Close them first.
            if (!test) KillLeftovers(root);
            var stamp = Path.Combine(home, "RuntimeFiles.verified");
            var thisPayload = test ? "" : Convert.ToHexString(SHA256.HashData(encrypted));
            if (test || !QuickReady(zip, root, stamp, thisPayload))
            {
                // First start after a download or an update (or a file went missing): verify everything and
                // restore what's needed. Every other start skips straight to the launcher.
                if (!test) Progress("Installing the update\u2026 0%\nOnly happens after an update. Your accounts and settings stay saved.");
                Extract(zip, root);
                if (!test)
                {
                    try { File.WriteAllText(stamp, thisPayload); } catch { }
                    RemoveOldVersions(home);
                }
            }

            // Keep the extracted runtime out of plain sight: mark it Hidden. Only a speed bump (processes read
            // a Hidden folder normally, so Node/Mongo/the game server are unaffected). Never fatal.
            if (!test) try { new DirectoryInfo(root).Attributes |= FileAttributes.Hidden; } catch { }

            if (test)
            {
                if (args[0] == "--exercise")
                {
                    bool rejected = false;
                    var bad = (byte[])encrypted.Clone(); bad[^1] ^= 1;
                    try { Decrypt(bad); } catch (CryptographicException) { rejected = true; }
                    if (!rejected) throw new Exception("Tampered encrypted payload accepted");

                    foreach (var name in new[] { "../escape", "C:/escape", "folder/../../escape" })
                    {
                        rejected = false;
                        try { SafePath(root, name); } catch (InvalidDataException) { rejected = true; }
                        if (!rejected) throw new Exception("Traversal accepted");
                    }

                    var sentinel = Path.Combine(root, "user-account-preservation.txt");
                    File.WriteAllText(sentinel, "preserve");
                    var entry = zip.Entries.First();
                    var path = SafePath(root, entry.FullName);
                    File.WriteAllText(path, "damaged");

                    var locked = SafePath(root, zip.Entries[1].FullName);
                    using (new FileStream(locked, FileMode.Open, FileAccess.ReadWrite, FileShare.None))
                    {
                        rejected = false;
                        try { Extract(zip, root); } catch (IOException) { rejected = true; }
                        if (!rejected || File.ReadAllText(path) != "damaged") throw new Exception("Lock preflight changed files");
                    }
                    Extract(zip, root);
                    if (!Matches(entry, path)) throw new Exception("Repair failed");
                    if (Cleanup(zip, root) != 0) throw new Exception("Cleanup failed");
                    if (File.ReadAllText(sentinel) != "preserve") throw new Exception("User data changed");
                }
                File.WriteAllText(root + ".verified.txt",
                    "PASS encrypted payload authentication, full extraction and component hashes" +
                    (args[0] == "--exercise" ? ", corruption recovery, path containment, cleanup and user-data preservation" : ""));
                return 0;
            }

            var start = new ProcessStartInfo(SafePath(root, LauncherExe)) { WorkingDirectory = root, UseShellExecute = false };
            start.ArgumentList.Add("--skip-update");
            using var child = Process.Start(start) ?? throw new IOException("Launcher could not start");
            HideProgress();
            child.WaitForExit();
            // The extracted files stay in place (hidden) so the next start is instant; they are only verified and
            // rewritten again when an update arrives or a file goes missing.
            File.AppendAllText(Path.Combine(home, "standalone.log"),
                $"{DateTime.UtcNow:O} Launcher exit={child.ExitCode}\n");
            return child.ExitCode;
        }
        catch (Exception e)
        {
            HideProgress();
            try { Directory.CreateDirectory(home); File.AppendAllText(Path.Combine(home, "standalone.log"), DateTime.UtcNow.ToString("O") + " " + e + "\n"); }
            catch { }
            if (!test)
                MessageBox.Show("Syndicate could not start. Your saved accounts have not been deleted.\n\n" + e.Message +
                    "\n\nClose other Syndicate windows and retry. Details: " + Path.Combine(home, "standalone.log"), "Syndicate");
            return 1;
        }
    }
}
