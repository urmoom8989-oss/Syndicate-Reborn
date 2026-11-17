using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Windows;
using System.Windows.Input;
using SyndicateLauncher.Services;

namespace SyndicateLauncher.ViewModels;

/// <summary>
/// Settings &gt; Bots &gt; Bot names. Bots get a random name from the pool every match (Seasons 1-9: the 1,000+
/// names in Assets/bot-name-pool.txt, the same list Erbium uses). Names the player types here are added to every
/// match: each one goes to a random bot. The pool can be searched but not edited, so the page stays short.
/// </summary>
public sealed partial class MainViewModel
{
    public const int MaxCustomBotNames = 99;
    private string _customBotNamesText = "";
    private string _customBotNamesError = "";
    private string _botNamePoolSearch = "";
    private static IReadOnlyList<string>? _botNamePool;

    /// <summary>Erbium's name pool, shipped with the launcher (Assets/bot-name-pool.txt, one name per line).</summary>
    public static IReadOnlyList<string> BotNamePool
    {
        get
        {
            if (_botNamePool is not null) return _botNamePool;
            var names = new List<string>();
            try
            {
                var info = Application.GetResourceStream(new Uri("pack://application:,,,/Assets/bot-name-pool.txt"));
                if (info is not null)
                {
                    using var reader = new StreamReader(info.Stream);
                    string? line;
                    while ((line = reader.ReadLine()) is not null)
                        if (line.Trim().Length > 0) names.Add(line.Trim());
                }
            }
            catch (IOException) { }
            catch (InvalidOperationException) { }
            if (names.Count == 0) names.AddRange(BotNameCatalog.DefaultNames);
            return _botNamePool = names;
        }
    }

    /// <summary>The player's own names, one per line.</summary>
    public string CustomBotNamesText
    {
        get => _customBotNamesText;
        set
        {
            if (!Set(ref _customBotNamesText, value)) return;
            CustomBotNamesError = "";
            OnPropertyChanged(nameof(CustomBotNamesDirty));
        }
    }

    public string CustomBotNamesError
    {
        get => _customBotNamesError;
        private set { if (Set(ref _customBotNamesError, value)) OnPropertyChanged(nameof(HasCustomBotNamesError)); }
    }
    public bool HasCustomBotNamesError => CustomBotNamesError.Length > 0;

    public bool CustomBotNamesDirty =>
        !ParseBotNames(CustomBotNamesText).SequenceEqual(SavedCustomBotNames, StringComparer.Ordinal);

    private List<string> SavedCustomBotNames => _settings.CustomBotNames ??= new List<string>();

    public string CustomBotNamesSummary => SavedCustomBotNames.Count switch
    {
        0 => "No names of your own yet: every bot gets a random name from the pool below, new every match.",
        1 => "1 of your names is in every match (on a random bot). Every other bot gets a random name.",
        var n => n + " of your names are in every match (each on a random bot). Every other bot gets a random name."
    };

    public string BotNamePoolSearch
    {
        get => _botNamePoolSearch;
        set { if (Set(ref _botNamePoolSearch, value)) OnPropertyChanged(nameof(BotNamePoolText)); }
    }

    public string BotNamePoolHeader => "Name pool (" + BotNamePool.Count.ToString("N0") + " names)";

    /// <summary>The pool as one wrapped, read-only block, filtered by the search box.</summary>
    public string BotNamePoolText
    {
        get
        {
            string filter = BotNamePoolSearch.Trim();
            var shown = filter.Length == 0 ? BotNamePool
                : BotNamePool.Where(n => n.Contains(filter, StringComparison.OrdinalIgnoreCase)).ToList();
            if (shown.Count == 0) return "No names match \"" + filter + "\".";
            return string.Join("   ·   ", shown);
        }
    }

    public ICommand SaveCustomBotNamesCommand => _saveCustomBotNamesCommand ??= new RelayCommand(SaveCustomBotNames);
    private ICommand? _saveCustomBotNamesCommand;

    /// <summary>First run with this version: names edited in the old 98-slot list become the player's own names.</summary>
    private void InitializeCustomBotNames()
    {
        if (_settings.CustomBotNames is null)
        {
            _settings.CustomBotNames = _settings.BotNameOverrides
                .OrderBy(pair => pair.Key)
                .Select(pair => (pair.Value ?? "").Trim())
                .Where(name => MatchOptions.ValidateBotName(name).Length == 0)
                .Distinct(StringComparer.OrdinalIgnoreCase)
                .Take(MaxCustomBotNames)
                .ToList();
        }
        _customBotNamesText = string.Join(Environment.NewLine, SavedCustomBotNames);
    }

    private static List<string> ParseBotNames(string text) =>
        (text ?? "").Split(new[] { '\r', '\n' }, StringSplitOptions.RemoveEmptyEntries)
            .Select(line => line.Trim()).Where(line => line.Length > 0).ToList();

    private void SaveCustomBotNames()
    {
        var names = ParseBotNames(CustomBotNamesText);
        var seen = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
        foreach (var name in names)
        {
            string error = MatchOptions.ValidateBotName(name);
            if (error.Length > 0) { CustomBotNamesError = "\"" + name + "\": " + error; return; }
            if (!seen.Add(name)) { CustomBotNamesError = "\"" + name + "\" is in the list twice."; return; }
        }
        if (names.Count > MaxCustomBotNames)
        {
            CustomBotNamesError = "Up to " + MaxCustomBotNames + " names (a match has at most 99 bots).";
            return;
        }
        _settings.CustomBotNames = names;
        Save();
        _customBotNamesText = string.Join(Environment.NewLine, names);
        OnPropertyChanged(nameof(CustomBotNamesText));
        CustomBotNamesError = "";
        OnPropertyChanged(nameof(CustomBotNamesDirty));
        OnPropertyChanged(nameof(CustomBotNamesSummary));
    }

    /// <summary>Season X (10.40) takes a whole roster: the player's names first, then the built-in ones.</summary>
    private List<string> RosterForMatch()
    {
        var roster = new List<string>(SavedCustomBotNames);
        var used = new HashSet<string>(roster, StringComparer.OrdinalIgnoreCase);
        foreach (var name in BotNames.Select(slot => slot.Name))
            if (used.Add(name)) roster.Add(name);
        return roster;
    }
}
