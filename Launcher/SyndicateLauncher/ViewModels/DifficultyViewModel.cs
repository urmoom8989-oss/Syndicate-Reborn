using System;
using System.Collections.ObjectModel;
using System.IO;
using System.Linq;
using System.Windows.Input;
using Microsoft.Win32;
using SyndicateLauncher.Models;
using SyndicateLauncher.Services;
namespace SyndicateLauncher.ViewModels;
public sealed class SkillSlider : ViewModelBase
{
    private int _value;
    private readonly Action _changed;
    public SkillSlider(SkillDefinition definition,int value,Action changed){Definition=definition;_value=value;_changed=changed;}
    public SkillDefinition Definition {get;}
    public int Value {get=>_value;set{if(Set(ref _value,Math.Clamp(value,0,100)))_changed();}}
}
public sealed partial class MainViewModel
{
    private readonly PresetStore _presets;
    private BotPreset _currentPreset=BotPreset.Builtin(2);
    private BotPreset? _selectedPreset;
    private bool _applyingPreset;
    private int _difficultyIndex=2;
    private string _presetName="My preset",_presetStatus="";
    public ObservableCollection<BotPreset> DifficultyPresets {get;}=new();
    public ObservableCollection<SkillSlider> SkillSliders {get;}=new();
    public ICommand SavePresetCommand {get;private set;}=null!;
    public ICommand ImportPresetCommand {get;private set;}=null!;
    public ICommand ExportPresetCommand {get;private set;}=null!;
    public string PresetName {get=>_presetName;set=>Set(ref _presetName,value??"");}
    public string PresetStatus {get=>_presetStatus;private set=>Set(ref _presetStatus,value);}
    public BotPreset? SelectedDifficultyPreset
    {
        get=>_selectedPreset;
        set {if(value is null){Set(ref _selectedPreset,null);return;}ApplyPreset(value);}
    }
    private void InitializeDifficulty()
    {
        for(int i=0;i<6;i++)DifficultyPresets.Add(BotPreset.Builtin(i));
        foreach(var p in _presets.Load())DifficultyPresets.Add(p);
        foreach(var d in BotPreset.Definitions)SkillSliders.Add(new(d,50,SliderChanged));
        SavePresetCommand=new RelayCommand(()=>PresetAction(()=>{
            var p=_currentPreset.Copy();p.Name=PresetName.Trim();p.BuiltinIndex=-1;
            var saved=_presets.Save(p);DifficultyPresets.Add(saved);ApplyPreset(saved);PresetStatus="Preset saved. Use Export .fnp to share it.";
        }));
        ImportPresetCommand=new RelayCommand(()=>PresetAction(()=>{
            var dialog=new OpenFileDialog {Filter="Fortnite bot presets (*.fnp)|*.fnp",Title="Import a bot preset",CheckFileExists=true};
            if(dialog.ShowDialog()!=true)return;
            var saved=_presets.Save(PresetStore.Read(dialog.FileName));DifficultyPresets.Add(saved);ApplyPreset(saved);PresetStatus="Preset imported and selected.";
        }));
        ExportPresetCommand=new RelayCommand(()=>PresetAction(()=>{
            var p=_currentPreset.Copy();p.Name=PresetName.Trim();p.Validate();
            string safe=new string(p.Name.Select(c=>Path.GetInvalidFileNameChars().Contains(c)?'_':c).ToArray());
            var dialog=new SaveFileDialog {Filter="Fortnite bot presets (*.fnp)|*.fnp",DefaultExt=".fnp",AddExtension=true,FileName=safe+".fnp",Title="Export a bot preset"};
            if(dialog.ShowDialog()!=true)return;PresetStore.Write(dialog.FileName,p);PresetStatus="Preset exported.";
        }));
        var initial=_settings.BotPreset;
        try{initial?.Validate();}catch{initial=null;}
        ApplyPreset(initial??BotPreset.Builtin((int)_settings.Difficulty+1));
    }
    private void PresetAction(Action action)
    {
        try{action();}catch(Exception e)when(e is IOException or UnauthorizedAccessException or InvalidOperationException){PresetStatus=e.Message;}
    }
    private void ApplyPreset(BotPreset preset)
    {
        preset.Validate();_applyingPreset=true;
        try {
            _currentPreset=preset.Copy();
            if(preset.BuiltinIndex>=0)_difficultyIndex=preset.BuiltinIndex;
            _selectedPreset=DifficultyPresets.FirstOrDefault(p=>ReferenceEquals(p,preset))??DifficultyPresets.FirstOrDefault(p=>p.Name==preset.Name&&p.Mixed==preset.Mixed&&p.Argument()==preset.Argument());
            if(_selectedPreset?.BuiltinIndex>=0)_difficultyIndex=_selectedPreset.BuiltinIndex;
            foreach(var slider in SkillSliders)slider.Value=preset.Values[slider.Definition.Key];
            PresetName=preset.Name;_settings.BotPreset=_currentPreset.Copy();Save();
        }finally{_applyingPreset=false;}
        NotifyDifficulty();
    }
    private void SliderChanged()
    {
        if(_applyingPreset)return;
        if(_currentPreset.BuiltinIndex>=0)PresetName="My preset";
        _currentPreset=new(){Name="Custom",Values=SkillSliders.ToDictionary(s=>s.Definition.Key,s=>s.Value)};
        _selectedPreset=null;_settings.BotPreset=_currentPreset.Copy();Save();NotifyDifficulty();
    }
    private void NotifyDifficulty()
    {
        OnPropertyChanged(nameof(DifficultyIndex));OnPropertyChanged(nameof(DifficultyName));
        OnPropertyChanged(nameof(DifficultyDescription));OnPropertyChanged(nameof(SelectedDifficultyPreset));
        OnPropertyChanged(nameof(DifficultyMeta));
    }
}
