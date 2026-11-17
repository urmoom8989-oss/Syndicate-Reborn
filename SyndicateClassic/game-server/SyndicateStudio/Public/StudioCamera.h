#pragma once
// SyndicateStudio camera: live view reads and a free camera detached from the player pawn.
// All functions must be called on the game thread.

#include <cstdint>
#include <vector>

namespace StudioCamera {

    struct CameraState {
        float X, Y, Z;           // location
        float Pitch, Yaw, Roll;  // rotation
        float FOV;
        bool Valid;              // false when there is no player camera yet (main menu loading, between worlds)
    };

    // Get the live camera state from the game's PlayerCameraManager (always the real view, never a stale copy).
    CameraState GetCamera();

    // Move the free camera (only while it is on).
    void SetCamera(float x, float y, float z, float pitch, float yaw, float roll);

    // Turn the free camera on or off. On: a camera actor becomes the view: hold the right mouse button to look,
    // WASD / Space+E up / C+Q down / Shift faster / wheel slower-faster; the character stays put, the HUD is
    // hidden and the game gets none of your input. Returns false if it couldn't.
    bool SetFreeCam(bool enable);

    // Is free cam active?
    bool IsFreeCam();

    // Called every tick on the game thread to apply free cam movement from input.
    void Tick();

    // Studio input from the game window while the free cam is on (read by the launcher with CMD_POLL_INPUT).
    struct InputEvent {
        uint8_t Type;    // 1 = click at (X0,Y0), 2 = selection box (X0,Y0)-(X1,Y1), 3 = key press (Vk)
        uint8_t Mods;    // 1 = Shift, 2 = Ctrl, 4 = Alt
        uint16_t Vk;
        float X0, Y0, X1, Y1; // game client pixels (= viewport pixels)
    };

    // Called from the message hook for every message the game thread takes. While the free cam is on, keyboard and
    // mouse-button messages are turned into Studio input and hidden from the game (returns true).
    bool FilterInput(MSG* M, bool Removing);

    // The selection box being dragged right now (x0,y0,x1,y1), if any.
    bool GetPickBox(float* Out4);

    // Clicks, finished boxes and key presses since the last call.
    std::vector<InputEvent> TakeInputEvents();
}
