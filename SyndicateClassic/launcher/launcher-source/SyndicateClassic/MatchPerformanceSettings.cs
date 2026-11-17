// Syndicate © 2026 Syndicate. All rights reserved. Proprietary — no copying, distribution, or derivative works. See LICENSE.
using System;
using System.Globalization;
using System.IO;

namespace SyndicateClassic;

internal static class MatchPerformanceSettings
{
    internal const int DefaultBotCount = 99;
    internal static string SettingsPath => Path.Combine(
        Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),
        "ProjectTilted", "local-bot-count.txt");

    internal static int Load(string? path = null)
    {
        try
        {
            return int.TryParse(File.ReadAllText(path ?? SettingsPath).Trim(),
                NumberStyles.Integer, CultureInfo.InvariantCulture, out int count)
                && count >= 10 && count <= 99 ? count : DefaultBotCount;
        }
        catch (IOException) { return DefaultBotCount; }
        catch (UnauthorizedAccessException) { return DefaultBotCount; }
    }

    internal static void Save(int count, string? path = null)
    {
        if (count < 10 || count > 99) throw new ArgumentOutOfRangeException(nameof(count));
        path ??= SettingsPath;
        Directory.CreateDirectory(Path.GetDirectoryName(path)!);
        string temporary = path + "." + Guid.NewGuid().ToString("N") + ".tmp";
        try
        {
            File.WriteAllText(temporary, count.ToString(CultureInfo.InvariantCulture));
            File.Move(temporary, path, true);
        }
        finally { if (File.Exists(temporary)) File.Delete(temporary); }
    }
}