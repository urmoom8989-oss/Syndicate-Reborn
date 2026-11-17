// Syndicate © 2026 Syndicate. All rights reserved. Proprietary — no copying, distribution, or derivative works. See LICENSE.
using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Threading.Tasks;
using System.Windows;
using System.Windows.Threading;

namespace SyndicateClassic;

// Season files appearing or disappearing while the launcher is open.
// Before: a season counted as linked as soon as its folder existed, and PLAY / LINK FOLDER was only
// recomputed when a launch button's text changed, so linking a folder (or copying files into one) left
// the old button showing until a restart. Now:
//  - a season is ready only when its folder has the exe, Engine and the paks, and nothing is mid-copy;
//  - FileSystemWatchers on the season folders (and their parent, e.g. "Fortnite Seasons") notice
//    files being added, changed or removed, wait until things settle, then re-check;
//  - a complete folder for a season that has none linked is linked automatically;
//  - a timer re-checks every few seconds anyway, in case a watcher fails or misses events
//    (network drives, too many changes at once);
//  - every re-check refreshes the home controls, so PLAY replaces LINK FOLDER (and back) at once.
public partial class MainWindow
{
    private readonly List<FileSystemWatcher> _seasonWatchers = new();
    private DispatcherTimer? _seasonSettle, _seasonPoll;
    private bool _seasonScanRunning, _seasonScanAgain;
    private string _seasonSignature = "";

    private void StartSeasonWatch()
    {
        _seasonSettle = new DispatcherTimer { Interval = TimeSpan.FromSeconds(2) };
        _seasonSettle.Tick += (_, __) => { _seasonSettle.Stop(); _ = RescanSeasonsAsync(); };
        _seasonPoll = new DispatcherTimer { Interval = TimeSpan.FromSeconds(6) };
        _seasonPoll.Tick += (_, __) => _ = RescanSeasonsAsync();
        _seasonPoll.Start();
        Activated += (_, __) => _ = RescanSeasonsAsync();
        Closed += (_, __) => { _seasonPoll?.Stop(); _seasonSettle?.Stop(); DisposeSeasonWatchers(); };
        WatchSeasonRoots();
        _ = RescanSeasonsAsync();
    }

    // (Re)creates one watcher per search root. Called again whenever the set of roots changes.
    private void WatchSeasonRoots()
    {
        var roots = Launcher.SeasonSearchRoots();
        var watched = _seasonWatchers.Select(w => w.Path).ToList();
        if (roots.Count == watched.Count && roots.All(r => watched.Any(w => w.Equals(r, StringComparison.OrdinalIgnoreCase)))) return;
        DisposeSeasonWatchers();
        foreach (var root in roots)
        {
            try
            {
                var watcher = new FileSystemWatcher(root)
                {
                    IncludeSubdirectories = true, InternalBufferSize = 64 * 1024,
                    NotifyFilter = NotifyFilters.DirectoryName | NotifyFilters.FileName | NotifyFilters.Size | NotifyFilters.LastWrite
                };
                watcher.Created += SeasonFilesChanged; watcher.Deleted += SeasonFilesChanged;
                watcher.Renamed += SeasonFilesChanged; watcher.Changed += SeasonFilesChanged;
                // A failed watcher (buffer overflow, drive gone) is dropped; the poll timer keeps checking.
                watcher.Error += (_, e) => Dispatcher.InvokeAsync(() => { Log("Season folder watcher stopped (" + e.GetException().Message + "); checking every few seconds instead."); DisposeSeasonWatchers(); });
                watcher.EnableRaisingEvents = true;
                _seasonWatchers.Add(watcher);
            }
            catch (Exception error) { Log("Can't watch " + root + " for season files: " + error.Message); }
        }
    }

    private void DisposeSeasonWatchers()
    {
        foreach (var watcher in _seasonWatchers) { try { watcher.EnableRaisingEvents = false; watcher.Dispose(); } catch { } }
        _seasonWatchers.Clear();
    }

    // Watcher threads only restart the settle timer; the check runs once the copy has gone quiet.
    private void SeasonFilesChanged(object sender, FileSystemEventArgs e) =>
        Dispatcher.InvokeAsync(() => { if (_seasonSettle == null) return; _seasonSettle.Stop(); _seasonSettle.Start(); });

    // Finds and links new complete season folders, then refreshes the UI if anything changed.
    private async Task RescanSeasonsAsync()
    {
        if (_seasonScanRunning) { _seasonScanAgain = true; return; }
        _seasonScanRunning = true;
        try
        {
            do
            {
                _seasonScanAgain = false;
                List<(string Build, string Folder)> found;
                string signature;
                try
                {
                    (found, signature) = await Task.Run(() => (Launcher.FindUnlinkedSeasons(), SeasonSignature()));
                }
                catch (Exception error) { Log("Season check failed: " + error.Message); return; }
                bool canLink = !_busy && !_sessionStarted;
                foreach (var (build, folder) in found)
                {
                    if (!canLink) break;
                    try
                    {
                        Launcher.LinkSeasonFolder(build, folder);
                        Log("Found Fortnite " + build + " in " + folder + ". Linked it.");
                        SetStatus("Fortnite " + build + " found in " + folder + ". Ready to play.");
                    }
                    catch (Exception error) { Log("Couldn't link Fortnite " + build + ": " + error.Message); }
                }
                if (found.Count > 0 && canLink) signature = await Task.Run(SeasonSignature);
                WatchSeasonRoots();
                if (signature != _seasonSignature) { _seasonSignature = signature; RefreshSeasonState(); }
            }
            while (_seasonScanAgain);
        }
        finally { _seasonScanRunning = false; }
    }

    // One line per season with its linked folder and whether it is ready; any change means the UI is stale.
    private static string SeasonSignature() =>
        string.Join("|", ModernSeasons.Select(s =>
        {
            var folder = Launcher.LinkedFolderFor(s.Build);
            return s.Build + "=" + (folder ?? "") + ":" + (folder == null ? "none" : Launcher.FolderProblem(folder) == null ? "ready" : "problem");
        }));

    // Re-reads every season's state into the home controls (PLAY / LINK FOLDER, picker, browser details).
    private void RefreshSeasonState()
    {
        try
        {
            _seasonImages.Clear(); // a newly linked folder brings its own splash picture
            SelectHeroSeason(_heroSeason, animate: false);
            UpdateButtons();
            if (_seasonBrowserView != null && _browserChapter == 1) ShowBrowserDetails(0, animate: false);
        }
        catch (Exception error) { Log("Couldn't refresh the season state: " + error.Message); }
    }

    // Shown under PLAY in the season browser when the selected season can't start yet.
    private string? SeasonHint(SeasonInfo season)
    {
        if (SeasonLinked(season)) return null;
        var linked = Launcher.LinkedFolderFor(season.Build);
        if (linked != null) return Launcher.FolderProblem(linked);
        var roots = Launcher.SeasonSearchRoots();
        var place = roots.Count > 0 ? Path.Combine(roots[0], season.Build) : "a folder of your choice";
        return "Fortnite " + season.Build + " isn't linked yet. Copy its files (FortniteGame and Engine) into " + place + " and it will be found automatically, or press LINK FOLDER.";
    }
}