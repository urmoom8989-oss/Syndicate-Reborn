using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text.Json;
using SyndicateLauncher.Models;

namespace SyndicateLauncher.Services;

/// <summary>
/// Settings &gt; News &amp; tournaments: writes Runtime\Server\news-settings.json, which the backend reads
/// (Utils/syndicateNewsSettings.js): posts for the game's News tab and tournaments for the Compete tab.
/// Pictures are copied to %APPDATA%\SyndicateLauncher\NewsImages; the file only names them.
/// </summary>
public static class NewsSettings
{
    private static readonly object Gate = new();
    private static List<NewsPost> _news = new();
    private static List<CustomTournament> _tournaments = new();
    private static readonly JsonSerializerOptions Json = new() { PropertyNamingPolicy = JsonNamingPolicy.CamelCase, WriteIndented = true };

    public static string ImageFolder =>
        Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData), "SyndicateLauncher", "NewsImages");

    public static void Load(List<NewsPost>? news, List<CustomTournament>? tournaments)
    {
        lock (Gate)
        {
            _news = (news ?? new()).Where(n => n is not null).ToList();
            _tournaments = (tournaments ?? new()).Where(t => t is not null).ToList();
        }
    }

    private static string Utc(DateTime time) =>
        (time.Kind == DateTimeKind.Utc ? time : DateTime.SpecifyKind(time, DateTimeKind.Local).ToUniversalTime()).ToString("o");

    public static void WriteForBackend()
    {
        try
        {
            string? runtime = SyndicateBackend.RuntimeDirectory;
            if (string.IsNullOrEmpty(runtime)) return;
            Directory.CreateDirectory(runtime);
            string json;
            lock (Gate)
            {
                json = JsonSerializer.Serialize(new
                {
                    version = 1,
                    news = _news.Select(n => new { id = n.Id, title = n.Title, body = n.Body, image = n.Image, builds = n.Builds }),
                    tournaments = _tournaments.Select(t => new
                    {
                        id = t.Id, title = t.Title, subtitle = t.Subtitle, description = t.Description, schedule = t.Schedule,
                        mode = t.Mode, begin = Utc(t.Begin), end = Utc(t.End), image = t.Image, color = t.Color,
                        postInNews = t.PostInNews, builds = t.Builds,
                    }),
                }, Json);
            }
            string file = Path.Combine(runtime, "news-settings.json");
            string temp = file + ".tmp";
            File.WriteAllText(temp, json);
            File.Move(temp, file, true);
        }
        catch
        {
            // The game shows no custom news or tournaments when the file cannot be written.
        }
    }
}
