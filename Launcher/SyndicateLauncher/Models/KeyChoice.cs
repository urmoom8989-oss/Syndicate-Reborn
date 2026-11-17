using System.Collections.Generic;

namespace SyndicateLauncher.Models;

/// <summary>A friendly name for one of the keys SyndicateClient accepts, with its flag-file value.</summary>
public sealed record KeyChoice(string Name, string Value);

public static class EditKeyChoices
{
    public static IReadOnlyList<KeyChoice> All { get; } = Build();

    private static IReadOnlyList<KeyChoice> Build()
    {
        var keys = new List<KeyChoice>
        {
            new("Mouse middle", "mouse:middle"),
            new("Mouse 4", "mouse:x1"),
            new("Mouse 5", "mouse:x2")
        };
        for (char c = 'A'; c <= 'Z'; c++)
            keys.Add(new(c.ToString(), "key:" + (int)c));
        for (char c = '0'; c <= '9'; c++)
            keys.Add(new(c.ToString(), "key:" + (int)c));
        for (int i = 1; i <= 12; i++)
            keys.Add(new("F" + i, "key:" + (0x70 + i - 1)));
        keys.AddRange(new[]
        {
            new KeyChoice("Space", "key:32"),
            new KeyChoice("Left Shift", "key:160"),
            new KeyChoice("Right Shift", "key:161"),
            new KeyChoice("Left Ctrl", "key:162"),
            new KeyChoice("Right Ctrl", "key:163"),
            new KeyChoice("Left Alt", "key:164"),
            new KeyChoice("Right Alt", "key:165"),
            new KeyChoice("Tab", "key:9"),
            new KeyChoice("Caps Lock", "key:20"),
            new KeyChoice("Enter", "key:13"),
            new KeyChoice("Backspace", "key:8"),
            new KeyChoice("Comma", "key:188"),
            new KeyChoice("Period", "key:190"),
            new KeyChoice("Slash", "key:191"),
            new KeyChoice("Semicolon", "key:186"),
            new KeyChoice("Apostrophe", "key:222"),
            new KeyChoice("Left Bracket", "key:219"),
            new KeyChoice("Right Bracket", "key:221"),
            new KeyChoice("Backslash", "key:220"),
            new KeyChoice("Minus", "key:189"),
            new KeyChoice("Equals", "key:187"),
            new KeyChoice("Tilde", "key:192")
        });
        return keys;
    }
}
