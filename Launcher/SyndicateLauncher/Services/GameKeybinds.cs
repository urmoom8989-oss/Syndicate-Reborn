using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text;

namespace SyndicateLauncher.Services;

/// <summary>
/// Reads the player's in-game keybinds from the ClientSettings.Sav the Syndicate backend stores
/// (%LOCALAPPDATA%\ProjectTilted\UserData\ClientSettings\ClientSettings-&lt;account&gt;-&lt;season&gt;.Sav).
///
/// Sprint by default (seasons below 5) works by holding the sprint key for the player. When the player
/// moves Crouch onto that key (for example Shift in Season 3), holding it made them crouch every time
/// they pressed W. This finds which keys the game really has on Sprint and Crouch, so the sprint loop
/// presses the game's own Sprint key and never a key that belongs to another action.
///
/// The save is an Unreal property stream. Each key mapping stores its action name (an FName such as
/// "Sprint" or "Crouch") followed by its keys ("LeftShift", "C", ...). Rather than decode the whole
/// format, this walks the length-prefixed strings in order and gives every key name to the action
/// name seen before it.
/// </summary>
internal static class GameKeybinds
{
    public sealed record Binds(IReadOnlyList<int> Sprint, IReadOnlyList<int> Crouch, IReadOnlyDictionary<int, string> Owner);

    private static string _file = "";
    private static DateTime _stamp;
    private static Binds? _cached;

    public static string Folder =>
        Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "ProjectTilted", "UserData", "ClientSettings");

    /// <summary>The binds of the newest settings save for this settings slot, or null when there is none.</summary>
    public static Binds? For(int slot)
    {
        try
        {
            if (!Directory.Exists(Folder)) return null;
            var newest = new DirectoryInfo(Folder).GetFiles("ClientSettings-*-" + slot + ".Sav")
                .OrderByDescending(f => f.LastWriteTimeUtc).FirstOrDefault();
            if (newest is null) return null;
            if (newest.FullName == _file && newest.LastWriteTimeUtc == _stamp) return _cached;
            _file = newest.FullName;
            _stamp = newest.LastWriteTimeUtc;
            _cached = Parse(File.ReadAllBytes(newest.FullName));
            return _cached;
        }
        catch (IOException) { return _cached; }
        catch (UnauthorizedAccessException) { return _cached; }
    }

    private static readonly HashSet<string> Actions = new(StringComparer.OrdinalIgnoreCase)
    {
        "Sprint", "Crouch", "Jump", "Fire", "Reload", "Use", "Interact", "Harvest", "HarvestingTool", "Build",
        "Edit", "Map", "Inventory", "Emote", "SquadComms", "PushToTalk", "Repair", "Rotate", "ResetBuild",
        "ChangeMaterial", "Trap", "ToggleCrouch", "Weapon1", "Weapon2", "Weapon3", "Weapon4", "Weapon5", "Weapon6",
        "Wall", "Floor", "Stairs", "Roof", "Ping", "Throw", "Melee", "Zoom", "TargetingMode", "AutoRun",
        "MoveForward", "MoveBackward", "MoveLeft", "MoveRight", "Forward", "Backward", "Left", "Right"
    };

    internal static Binds Parse(byte[] data)
    {
        var sprint = new List<int>();
        var crouch = new List<int>();
        var owner = new Dictionary<int, string>();
        string? action = null;
        foreach (var text in Strings(data))
        {
            if (Actions.Contains(text)) { action = text; continue; }
            int vk = VirtualKey(text);
            if (vk == 0 || action is null) continue;
            if (action.Equals("Sprint", StringComparison.OrdinalIgnoreCase)) { if (!sprint.Contains(vk)) sprint.Add(vk); }
            else if (action.Contains("Crouch", StringComparison.OrdinalIgnoreCase)) { if (!crouch.Contains(vk)) crouch.Add(vk); }
            if (!owner.ContainsKey(vk)) owner[vk] = action;
        }
        return new Binds(sprint, crouch, owner);
    }

    /// <summary>Unreal FStrings: int32 length (negative = UTF-16) followed by the characters and a 0.</summary>
    private static IEnumerable<string> Strings(byte[] data)
    {
        for (int i = 0; i + 4 < data.Length; i++)
        {
            int length = BitConverter.ToInt32(data, i);
            if (length is >= 2 and <= 64)
            {
                int end = i + 4 + length;
                if (end <= data.Length && data[end - 1] == 0 && Printable(data, i + 4, length - 1))
                {
                    yield return Encoding.ASCII.GetString(data, i + 4, length - 1);
                    i = end - 1;
                }
            }
            else if (length is <= -2 and >= -64)
            {
                int count = -length, end = i + 4 + count * 2;
                if (end <= data.Length && data[end - 2] == 0 && data[end - 1] == 0)
                {
                    var text = Encoding.Unicode.GetString(data, i + 4, (count - 1) * 2);
                    if (text.All(c => c is >= ' ' and <= '~')) { yield return text; i = end - 1; }
                }
            }
        }
    }

    private static bool Printable(byte[] data, int start, int count)
    {
        for (int i = start; i < start + count; i++) if (data[i] is < 0x20 or > 0x7E) return false;
        return count > 0;
    }

    /// <summary>Unreal key name to Windows virtual key; 0 when it is not a keyboard or mouse button we can press.</summary>
    internal static int VirtualKey(string name)
    {
        if (name.Length == 1 && char.IsLetter(name[0])) return char.ToUpperInvariant(name[0]);
        return name switch
        {
            "LeftShift" => 0xA0, "RightShift" => 0xA1, "LeftControl" => 0xA2, "RightControl" => 0xA3,
            "LeftAlt" => 0xA4, "RightAlt" => 0xA5, "CapsLock" => 0x14, "Tab" => 0x09, "SpaceBar" => 0x20,
            "Zero" => 0x30, "One" => 0x31, "Two" => 0x32, "Three" => 0x33, "Four" => 0x34, "Five" => 0x35,
            "Six" => 0x36, "Seven" => 0x37, "Eight" => 0x38, "Nine" => 0x39,
            "MiddleMouseButton" => 0x04, "ThumbMouseButton" => 0x05, "ThumbMouseButton2" => 0x06,
            "F1" => 0x70, "F2" => 0x71, "F3" => 0x72, "F4" => 0x73, "F5" => 0x74, "F6" => 0x75,
            "Tilde" => 0xC0, "Equals" => 0xBB, "Hyphen" => 0xBD, "Comma" => 0xBC, "Period" => 0xBE,
            "Slash" => 0xBF, "Semicolon" => 0xBA, "Apostrophe" => 0xDE, "LeftBracket" => 0xDB, "RightBracket" => 0xDD,
            "Backslash" => 0xDC, "Enter" => 0x0D, "BackSpace" => 0x08, "Insert" => 0x2D, "Delete" => 0x2E,
            "Home" => 0x24, "End" => 0x23, "PageUp" => 0x21, "PageDown" => 0x22,
            _ => 0
        };
    }

    /// <summary>The launcher key string ("key:160", "mouse:x1", ...) for a virtual key.</summary>
    internal static string LauncherKey(int vk) => vk switch
    {
        0x04 => "mouse:middle",
        0x05 => "mouse:x1",
        0x06 => "mouse:x2",
        _ => "key:" + vk
    };
}
