using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text.Json;
using SyndicateLauncher.Models;

namespace SyndicateLauncher.Services;

/// <summary>
/// Settings &gt; Item shop: writes Runtime\Server\shop-settings.json, which the backend reads
/// (Utils/syndicateShopSettings.js): how many hours each shop lasts, and each season's own shop.
/// </summary>
public static class ShopSettings
{
    private static readonly object Gate = new();
    private static int _hours = 6;
    private static Dictionary<string, CustomShop> _custom = new();
    private static readonly JsonSerializerOptions Json = new() { PropertyNamingPolicy = JsonNamingPolicy.CamelCase, WriteIndented = true };

    public static void Load(int hours, Dictionary<string, CustomShop>? custom)
    {
        lock (Gate)
        {
            _hours = Math.Clamp(hours <= 0 ? 6 : hours, 1, 168);
            _custom = (custom ?? new()).Where(pair => pair.Value is not null)
                .ToDictionary(pair => pair.Key, pair => new CustomShop { Featured = pair.Value.Featured?.ToList() ?? new(), Daily = pair.Value.Daily?.ToList() ?? new() });
        }
    }

    public static void WriteForBackend()
    {
        try
        {
            string? runtime = SyndicateBackend.RuntimeDirectory;
            if (string.IsNullOrEmpty(runtime)) return;
            Directory.CreateDirectory(runtime);
            string json;
            lock (Gate) json = JsonSerializer.Serialize(new { version = 1, hours = _hours, custom = _custom }, Json);
            string file = Path.Combine(runtime, "shop-settings.json");
            string temp = file + ".tmp";
            File.WriteAllText(temp, json);
            File.Move(temp, file, true);
        }
        catch
        {
            // The backend keeps its automatic shop when the file cannot be written.
        }
    }
}
