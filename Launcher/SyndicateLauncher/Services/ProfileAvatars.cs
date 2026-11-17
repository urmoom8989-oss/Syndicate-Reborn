using System;
using System.Collections.Generic;
using System.IO;
using System.Windows.Media;
using System.Windows.Media.Imaging;
using SyndicateLauncher.Models;

namespace SyndicateLauncher.Services;

/// <summary>Syndicate's OG profile pictures, embedded in the launcher, and pictures picked from this PC.</summary>
public static class ProfileAvatars
{
    private const string CustomPrefix = "custom:";
    private const int CustomMaxSize = 512;

    public static IReadOnlyList<string> Ids { get; } = BuildIds();

    private static readonly Dictionary<string, ImageSource?> Cache = new(StringComparer.OrdinalIgnoreCase);

    private static IReadOnlyList<string> BuildIds()
    {
        var ids = new string[23];
        for (int i = 1; i <= ids.Length; i++) ids[i - 1] = "og" + i.ToString("D2");
        return ids;
    }

    /// <summary>True when the id is a picture the player picked from this PC.</summary>
    public static bool IsCustom(string? id) =>
        !string.IsNullOrWhiteSpace(id) && id.StartsWith(CustomPrefix, StringComparison.OrdinalIgnoreCase);

    /// <summary>Custom pictures live next to settings.json, so they survive the original file moving.</summary>
    public static string CustomDirectory => Path.Combine(SettingsService.DataDirectory, "Avatars");

    public static string? CustomPath(string? id)
    {
        if (!IsCustom(id) || id is null) return null;
        string name = id.Substring(CustomPrefix.Length);
        if (name.Length == 0 || name is "." or ".." || name.IndexOfAny(Path.GetInvalidFileNameChars()) >= 0) return null;
        return Path.Combine(CustomDirectory, name);
    }

    public static ImageSource? Load(string? id)
    {
        if (string.IsNullOrWhiteSpace(id)) return null;
        if (Cache.TryGetValue(id, out ImageSource? cached)) return cached;

        ImageSource? image = null;
        try
        {
            if (IsCustom(id))
            {
                string? path = CustomPath(id);
                if (path is not null && File.Exists(path))
                {
                    using var stream = new FileStream(path, FileMode.Open, FileAccess.Read, FileShare.ReadWrite | FileShare.Delete);
                    var bitmap = new BitmapImage();
                    bitmap.BeginInit();
                    bitmap.StreamSource = stream;
                    bitmap.CacheOption = BitmapCacheOption.OnLoad;
                    bitmap.DecodePixelWidth = 112;
                    bitmap.EndInit();
                    bitmap.Freeze();
                    image = bitmap;
                }
            }
            else
            {
                string assembly = typeof(ProfileAvatars).Assembly.GetName().Name ?? "SyndicateLauncher";
                var bitmap = new BitmapImage();
                bitmap.BeginInit();
                bitmap.UriSource = new Uri($"pack://application:,,,/{assembly};component/Assets/Avatars/{id}.png", UriKind.Absolute);
                bitmap.CacheOption = BitmapCacheOption.OnLoad;
                bitmap.DecodePixelWidth = 112;
                bitmap.EndInit();
                bitmap.Freeze();
                image = bitmap;
            }
        }
        catch { }
        Cache[id] = image;
        return image;
    }

    /// <summary>Copies a picture the player picked into the launcher's data folder and returns its avatar id.</summary>
    public static string? SaveCustom(string sourcePath)
    {
        try
        {
            var info = new FileInfo(sourcePath);
            if (info.Length <= 0 || info.Length > 32 * 1024 * 1024) return null;
            using var input = new FileStream(sourcePath, FileMode.Open, FileAccess.Read, FileShare.ReadWrite | FileShare.Delete);
            var decoder = BitmapDecoder.Create(input, BitmapCreateOptions.PreservePixelFormat, BitmapCacheOption.OnLoad);
            if (decoder.Frames.Count == 0) return null;
            BitmapSource frame = decoder.Frames[0];
            if (frame.PixelWidth <= 0 || frame.PixelHeight <= 0) return null;

            BitmapSource scaled = frame;
            if (frame.PixelWidth > CustomMaxSize || frame.PixelHeight > CustomMaxSize)
            {
                double scale = Math.Min((double)CustomMaxSize / frame.PixelWidth, (double)CustomMaxSize / frame.PixelHeight);
                scaled = new TransformedBitmap(frame, new ScaleTransform(scale, scale));
            }

            Directory.CreateDirectory(CustomDirectory);
            string name = "avatar-" + Guid.NewGuid().ToString("N") + ".png";
            string path = Path.Combine(CustomDirectory, name);
            var encoder = new PngBitmapEncoder();
            encoder.Frames.Add(BitmapFrame.Create(scaled));
            using (var output = new FileStream(path, FileMode.CreateNew, FileAccess.Write, FileShare.None))
                encoder.Save(output);

            return CustomPrefix + name;
        }
        catch { return null; }
    }

    /// <summary>Deletes a custom picture once no profile uses it.</summary>
    public static void DeleteCustom(string? id)
    {
        string? path = CustomPath(id);
        if (path is null) return;
        try
        {
            Cache.Remove(id!);
            if (File.Exists(path)) File.Delete(path);
        }
        catch (IOException) { }
        catch (UnauthorizedAccessException) { }
    }

    public static string Ensure(Profile profile)
    {
        if (IsCustom(profile.Avatar))
        {
            string? path = CustomPath(profile.Avatar);
            if (path is not null && File.Exists(path)) return profile.Avatar;
        }
        foreach (string id in Ids)
            if (string.Equals(id, profile.Avatar, StringComparison.OrdinalIgnoreCase)) return profile.Avatar;
        profile.Avatar = Ids[Random.Shared.Next(Ids.Count)];
        return profile.Avatar;
    }
}
