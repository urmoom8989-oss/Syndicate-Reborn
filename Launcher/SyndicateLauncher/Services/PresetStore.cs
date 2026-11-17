using System;
using System.Collections.Generic;
using System.IO;
using System.Text.Json;
using SyndicateLauncher.Models;
namespace SyndicateLauncher.Services;
public sealed class PresetStore
{
    public static string DefaultDirectory=>Path.Combine(SettingsService.DataDirectory,"Presets");
    private readonly string _directory;
    private static readonly JsonSerializerOptions Options=new(){WriteIndented=true,UnmappedMemberHandling=System.Text.Json.Serialization.JsonUnmappedMemberHandling.Disallow,MaxDepth=8};
    public PresetStore(string? directory=null){_directory=directory??DefaultDirectory;}
    public static BotPreset Read(string path)
    {
        if(!Path.GetExtension(path).Equals(".fnp",StringComparison.OrdinalIgnoreCase))throw new InvalidOperationException("Choose a .fnp preset file.");
        using var stream=new FileStream(path,FileMode.Open,FileAccess.Read,FileShare.Read);
        if(stream.Length>65536)throw new InvalidOperationException("The preset is too large (maximum 64 KB).");
        try {
            using var doc=JsonDocument.Parse(stream,new(){MaxDepth=8});
            CheckDuplicateNames(doc.RootElement);
            var preset=doc.RootElement.Deserialize<BotPreset>(Options)??throw new InvalidOperationException("The preset is empty.");
            preset.Validate();return preset;
        }catch(JsonException){throw new InvalidOperationException("This .fnp file is not a valid Syndicate preset.");}
    }
    private static void CheckDuplicateNames(JsonElement e)
    {
        if(e.ValueKind!=JsonValueKind.Object)return;
        var names=new HashSet<string>(StringComparer.Ordinal);
        foreach(var p in e.EnumerateObject()){
            if(!names.Add(p.Name))throw new InvalidOperationException("The preset contains duplicate settings.");
            CheckDuplicateNames(p.Value);
        }
    }
    public static void Write(string path,BotPreset preset)
    {
        preset.Validate();if(!Path.GetExtension(path).Equals(".fnp",StringComparison.OrdinalIgnoreCase))throw new InvalidOperationException("Save the preset with a .fnp extension.");
        string full=Path.GetFullPath(path),temp=full+"."+Guid.NewGuid().ToString("N")+".tmp";
        try {File.WriteAllText(temp,JsonSerializer.Serialize(preset,Options));File.Move(temp,full,true);}
        finally {if(File.Exists(temp))File.Delete(temp);}
    }
    public BotPreset Save(BotPreset preset)
    {
        Directory.CreateDirectory(_directory);string path=Path.Combine(_directory,Guid.NewGuid().ToString("N")+".fnp");
        Write(path,preset);var saved=preset.Copy();saved.LocalPath=path;saved.BuiltinIndex=-1;return saved;
    }
    public IEnumerable<BotPreset> Load()
    {
        if(!Directory.Exists(_directory))yield break;
        foreach(var path in Directory.EnumerateFiles(_directory,"*.fnp")){
            BotPreset? p=null;try{p=Read(path);p.LocalPath=path;}catch(Exception e)when(e is IOException or UnauthorizedAccessException or InvalidOperationException){}
            if(p is not null)yield return p;
        }
    }
}
