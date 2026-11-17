using System;
using System.Collections.Generic;
using System.Linq;
using System.Text.Json.Serialization;
namespace SyndicateLauncher.Models;
public sealed record SkillDefinition(string Key,string Name,string Description);
public sealed class BotPreset
{
    [JsonRequired] public string Format {get;set;}="Syndicate.FNP";
    [JsonRequired] public int Version {get;set;}=1;
    [JsonRequired] public string Name {get;set;}="Medium";
    [JsonRequired] public bool Mixed {get;set;}
    [JsonRequired] public Dictionary<string,int> Values {get;set;}=Definitions.ToDictionary(d=>d.Key,_=>50);
    [JsonIgnore] public string? LocalPath {get;set;}
    [JsonIgnore] public int BuiltinIndex {get;set;}=-1;
    public static readonly string[] Levels={"Bot","Easy","Medium","Hard","Sweat","Variety"};
    public static readonly SkillDefinition[] Definitions={
        new("aimAccuracy","Aim accuracy","Higher means less added aiming error. Native weapon spread still applies."),
        new("aimTracking","Aim tracking","How quickly bots turn their aim toward an opponent."),
        new("reactionSpeed","Reaction speed","How soon bots react after spotting an enemy or taking fire."),
        new("firingFrequency","Firing frequency","How much time bots spend shooting rather than pausing between bursts."),
        new("aggression","Aggression","How readily bots pressure and close the gap to an enemy."),
        new("pursuit","Pursuit persistence","How long bots pursue the last seen location after losing sight."),
        new("buildFrequency","Build frequency","How often bots choose combat builds; terrain traversal remains available."),
        new("buildSpeed","Build speed","Pause before placing a validated building piece. Movement and physics stay native."),
        new("heightSeeking","High ground priority","How often bots start a climb to contest enemy height."),
        new("defensiveBuilding","Defensive building","How reliably bots choose cover or repair it under fire."),
        new("editFrequency","Window edit frequency","How often bots attempt tactical window edits."),
        new("editSpeed","Edit speed","Recovery time between tactical window edit attempts."),
        new("strafing","Strafing","Amount of sideways movement during close gunfights."),
        new("jumping","Combat jumping","Chance of an occasional jump during close gunfights."),
        new("awareness","Awareness","Visual search distance and response to nearby sounds."),
        new("prediction","Movement prediction","How much bots lead shots based on observed enemy movement."),
        new("looting","Looting pace","How frequently bots search for more useful nearby loot."),
        new("harvesting","Material gathering","Material reserve bots try to gather before returning to other tasks."),
        new("healing","Healing priority","Health threshold at which bots prioritize a protected heal."),
        new("rotation","Early rotation","Extra time budget for starting the next-circle rotation; storm safety remains active.")
    };
    public static BotPreset Builtin(int index)
    {
        index=Math.Clamp(index,0,5);
        int value=index==5?50:new[]{5,25,50,75,100}[index];
        return new(){Name=Levels[index],Mixed=index==5,BuiltinIndex=index,Values=Definitions.ToDictionary(d=>d.Key,_=>value)};
    }
    public BotPreset Copy()=>new(){Name=Name,Mixed=Mixed,Values=new(Values),BuiltinIndex=BuiltinIndex,LocalPath=LocalPath};
    public void Validate()
    {
        if(Format!="Syndicate.FNP"||Version!=1)throw new InvalidOperationException("This .fnp format or version is not supported.");
        if(string.IsNullOrWhiteSpace(Name)||Name.Length>60||Name.Any(char.IsControl))throw new InvalidOperationException("Use a preset name of 1 to 60 characters.");
        if(Values is null||Values.Count!=Definitions.Length||Definitions.Any(d=>!Values.TryGetValue(d.Key,out int n)||n<0||n>100))
            throw new InvalidOperationException("The preset must contain all 20 sliders, each from 0 to 100.");
    }
    public string Argument()
    {
        Validate();return Mixed?"mixed":string.Join(",",Definitions.Select(d=>Values[d.Key]));
    }
}
