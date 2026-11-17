using System;
using System.IO;
using System.Security.Cryptography;
using System.Text;
using System.Text.Json;
using SyndicateLauncher.Models;
namespace SyndicateLauncher.Services;
public static class BackendProfiles
{
    public static string Prepare(string runtime,string backend,Profile profile,int season)
    {
        string profileName=profile.Name;
        if(string.IsNullOrWhiteSpace(profileName))throw new InvalidOperationException("Choose a launcher profile first.");
        string mode=profile.Mode==ProfileMode.Progression?"progression":"unlocked-all";
        string id=Convert.ToHexString(SHA256.HashData(Encoding.UTF8.GetBytes(profileName.Trim().ToUpperInvariant()))).ToLowerInvariant();
        Directory.CreateDirectory(backend);
        string destination=Path.Combine(backend,"Players",id);
        bool fresh=!File.Exists(Path.Combine(destination,"profile-owner.json"));
        Directory.CreateDirectory(destination);
        string migration=Path.Combine(backend,"legacy-profile-owner.txt");
        // Preserve the last old-format locker once, for the first profile
        // launched after this upgrade. Never share it with later profiles.
        if(fresh&&!File.Exists(migration)&&Directory.Exists(Path.Combine(backend,"profiles")))
            foreach(var file in Directory.EnumerateFiles(Path.Combine(backend,"profiles"),"*.json")){
                string target=Path.Combine(destination,"profiles",Path.GetFileName(file));Directory.CreateDirectory(Path.GetDirectoryName(target)!);if(!File.Exists(target))File.Copy(file,target,false);
            }
        foreach(var file in Directory.EnumerateFiles(runtime,"*",SearchOption.AllDirectories)){
            string relative=Path.GetRelativePath(runtime,file);
            if(relative.Equals("lawinserver.exe",StringComparison.OrdinalIgnoreCase)){
                File.Copy(file,Path.Combine(backend,"lawinserver.exe"),true);continue;
            }
            string target=Path.Combine(destination,relative);Directory.CreateDirectory(Path.GetDirectoryName(target)!);
            bool managed=relative.Equals("titanium-profiles.cjs",StringComparison.OrdinalIgnoreCase)||
                relative.Equals("titanium-modes.cjs",StringComparison.OrdinalIgnoreCase)||
                relative.Equals(Path.Combine("CloudStorage","DefaultEngine.ini"),StringComparison.OrdinalIgnoreCase)||
                relative.Equals(Path.Combine("CloudStorage","DefaultInput.ini"),StringComparison.OrdinalIgnoreCase)||
                relative.Equals(Path.Combine("CloudStorage","DefaultGame.ini"),StringComparison.OrdinalIgnoreCase)||
                relative.Equals(Path.Combine("CloudStorage","DefaultRuntimeOptions.ini"),StringComparison.OrdinalIgnoreCase);
            if(managed||!File.Exists(target))File.Copy(file,target,true);
            if(!relative.StartsWith("profiles"+Path.DirectorySeparatorChar,StringComparison.OrdinalIgnoreCase)){
                string shared=Path.Combine(backend,relative);Directory.CreateDirectory(Path.GetDirectoryName(shared)!);
                if(managed||!File.Exists(shared))File.Copy(file,shared,true);
            }
        }
        foreach(string directory in new[]{backend,destination})foreach(string name in new[]{"DefaultGame.ini","DefaultRuntimeOptions.ini"}){
            string old=Path.Combine(directory,"CloudStorage",name);
            // The old upstream DefaultGame.ini caused map-startup failures and
            // is disabled. Keep only Syndicate's minimal contextual-tips config.
            if(File.Exists(old)&&(name!="DefaultGame.ini"||!File.ReadAllText(old).Contains("bEnableContextTutorials")))File.Move(old,old+".preMapStartupFix",true);
        }
        if(!File.Exists(migration))File.WriteAllText(migration,id);
        File.WriteAllText(Path.Combine(destination,"profile-owner.json"),JsonSerializer.Serialize(new{profile=profileName,mode,season}));
        return destination;
    }
}
