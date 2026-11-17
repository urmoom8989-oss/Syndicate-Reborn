using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Threading.Tasks;
using System.Text.Json;
using SyndicateLauncher.Models;
namespace SyndicateLauncher.Services;
public sealed class SettingsService
{
    private static readonly JsonSerializerOptions Options = new() { WriteIndented = true };
    public static string DataDirectory => LegacyData.RoamingDirectory;
    private readonly string _directory;
    public SettingsService(string? directory=null){_directory=directory??DataDirectory;}
    private string FilePath => Path.Combine(_directory, "settings.json");
    public AppSettings Load()
    {
        try
        {
            if (File.Exists(FilePath))
            {
                var loaded = JsonSerializer.Deserialize<AppSettings>(File.ReadAllText(FilePath), Options);
                if (loaded is not null) return loaded;
            }
        }
        catch
        {
            // A corrupt settings file shouldn't stop the launcher from opening.
        }
        return new AppSettings();
    }
    public void Save(AppSettings settings)
    {
        try
        {
            Directory.CreateDirectory(_directory);
            File.WriteAllText(FilePath, JsonSerializer.Serialize(settings, Options));
        }
        catch
        {
            // Saving is best-effort.
        }
    }
}
