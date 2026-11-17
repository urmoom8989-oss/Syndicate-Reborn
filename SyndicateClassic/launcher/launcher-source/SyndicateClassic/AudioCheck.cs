// Syndicate © 2026 Syndicate. All rights reserved. Proprietary — no copying, distribution, or derivative works. See LICENSE.
using System;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Threading.Tasks;

namespace SyndicateClassic;

// Game sound check. The old Fortnite builds pick the Windows output device once, at start-up, and play
// nothing when that device cannot be opened (192 kHz or 24-bit "studio" formats, exclusive mode taken by
// another app, Spatial sound on some drivers, or no default device at all). They never switch to a device
// plugged in later either. About 40 s after Fortnite starts, this reads the game's own log; when the audio
// device did not start it says so in the launcher status and explains the fix, instead of a silent match.
public partial class MainWindow
{
    private async Task CheckGameAudio(Process game, LauncherPaths paths)
    {
        try
        {
            var started = DateTime.Now;
            await Task.Delay(TimeSpan.FromSeconds(40));
            if (game.HasExited) return;
            string? log = await Task.Run(() => NewestClientLog(paths, started));
            if (log == null) return;
            string text = await Task.Run(() => ReadClientLog(log));
            if (!text.Contains("Game Engine Initialized", StringComparison.Ordinal)) return; // still starting
            bool ok = text.Contains("FAudioDevice initialized", StringComparison.Ordinal) || text.Contains("AudioDevice initialized", StringComparison.Ordinal);
            var failure = text.Split('\n').FirstOrDefault(l =>
                (l.Contains("LogAudio", StringComparison.Ordinal) || l.Contains("XAudio", StringComparison.Ordinal) || l.Contains("LogAudioMixer", StringComparison.Ordinal))
                && l.Contains("device", StringComparison.OrdinalIgnoreCase)
                && (l.Contains("Error", StringComparison.Ordinal) || l.Contains("Failed", StringComparison.OrdinalIgnoreCase) || l.Contains("No audio device", StringComparison.OrdinalIgnoreCase)));
            // Only device problems count: the game logs many harmless audio warnings (missing sound classes...).
            if (ok && failure == null) { Log("Game sound: audio device started."); return; }
            // Seasons up to X use XAudio2 2.7, which Windows does not ship: it comes with the DirectX End-User
            // Runtime (June 2010). Without it the game logs "Failed to create XAudio2 interface" and is silent.
            if (text.Contains("Failed to create XAudio2 interface", StringComparison.Ordinal))
            {
                Log("Game sound problem: XAudio2 2.7 is missing (DirectX End-User Runtime not installed) (" + log + ")");
                SetStatus("No game sound: this PC is missing the DirectX End-User Runtime (June 2010) that older Fortnite versions need for sound. " +
                          "Install it from Microsoft (search \"DirectX End-User Runtime Web Installer\"), then launch again.", okay: false);
                return;
            }
            Log("Game sound problem: " + (failure?.Trim() ?? "the audio device was not started") + " (" + log + ")");
            SetStatus("No game sound? Close Fortnite, then in Windows Sound settings set your output device's format to 48000 Hz, " +
                      "turn off Spatial sound and exclusive mode for it, and launch again. Plug headphones in before launching.", okay: false);
        }
        catch { }
    }

    // Fortnite writes FortniteGame.log under %LOCALAPPDATA%\FortniteGame\Saved\Logs (shipping builds) or in the
    // build's own FortniteGame\Saved\Logs. The one written since this launch is the right one.
    private static string? NewestClientLog(LauncherPaths paths, DateTime since)
    {
        var candidates = new[]
        {
            Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "FortniteGame", "Saved", "Logs", "FortniteGame.log"),
            Path.GetFullPath(Path.Combine(Path.GetDirectoryName(paths.GameExe) ?? "", "..", "..", "Saved", "Logs", "FortniteGame.log"))
        };
        return candidates.Where(File.Exists).Select(f => new FileInfo(f)).Where(f => f.LastWriteTime >= since.AddSeconds(-5))
            .OrderByDescending(f => f.LastWriteTime).Select(f => f.FullName).FirstOrDefault();
    }

    private static string ReadClientLog(string file)
    {
        using var stream = new FileStream(file, FileMode.Open, FileAccess.Read, FileShare.ReadWrite | FileShare.Delete);
        if (stream.Length > 4_000_000) stream.Seek(0, SeekOrigin.Begin); // the start-up part is at the top
        var buffer = new byte[Math.Min(stream.Length, 4_000_000)];
        int read = 0, n;
        while (read < buffer.Length && (n = stream.Read(buffer, read, buffer.Length - read)) > 0) read += n;
        return System.Text.Encoding.UTF8.GetString(buffer, 0, read);
    }
}