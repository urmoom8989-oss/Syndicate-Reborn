using System.IO;

namespace SyndicateLauncher.Services.Controllers;

/// <summary>
/// The controller button layout mod was removed from Settings. SyndicatePadAssist.dll still reads
/// controller-binds.txt from the client mods folder, so the launcher deletes a layout saved by an older
/// version at startup and every pad goes back to the game's normal buttons.
/// </summary>
public static class ControllerBinds
{
    public const string FileName = "controller-binds.txt";

    public static void Clear()
    {
        try
        {
            string file = Path.Combine(ClientMods.Folder, FileName);
            if (File.Exists(file)) File.Delete(file);
        }
        catch { }
    }
}
