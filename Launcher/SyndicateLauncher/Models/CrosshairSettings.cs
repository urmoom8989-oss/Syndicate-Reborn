namespace SyndicateLauncher.Models;

/// <summary>
/// Settings &gt; Display &gt; Crosshair. Drawn in game by SyndicateMenu.dll (Features/Crosshair.cpp) at the exact
/// centre of the game's frame; Services/CrosshairService.cs writes these values to crosshair.ini for it.
/// Sizes are in screen pixels.
/// </summary>
public sealed class CrosshairSettings
{
    public bool Enabled { get; set; }
    public string Preset { get; set; } = "Classic";
    public bool Lines { get; set; } = true;
    /// <summary>T shape: no top arm.</summary>
    public bool TStyle { get; set; }
    /// <summary>Arms turned 45 degrees (an X).</summary>
    public bool Diagonal { get; set; }
    public int Length { get; set; } = 6;
    public int Thickness { get; set; } = 2;
    public int Gap { get; set; } = 3;
    public bool Dot { get; set; }
    public int DotSize { get; set; } = 2;
    public bool DotRound { get; set; }
    public bool Circle { get; set; }
    public int CircleRadius { get; set; } = 10;
    public int CircleThickness { get; set; } = 1;
    public bool Outline { get; set; } = true;
    public int OutlineThickness { get; set; } = 1;
    /// <summary>#RRGGBB.</summary>
    public string Color { get; set; } = "#00FF78";
    /// <summary>0-100 %.</summary>
    public int Opacity { get; set; } = 100;
    public string OutlineColor { get; set; } = "#000000";
    public int OutlineOpacity { get; set; } = 80;
    /// <summary>Hidden in the lobby, menus, map and inventory (while the mouse cursor shows).</summary>
    public bool HideInMenus { get; set; } = true;
    /// <summary>Hidden while the right mouse button is held (aiming down sights).</summary>
    public bool HideWhenAiming { get; set; }

    public CrosshairSettings Copy() => (CrosshairSettings)MemberwiseClone();
}
