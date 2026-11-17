#pragma once
// SyndicateStudio scene query: actor enumeration, selection, projection, raycasting.
// All functions must be called on the game thread.

#include <cstdint>
#include <string>
#include <vector>

namespace StudioScene {

    struct ActorInfo {
        uint32_t Index;
        std::string Name;       // display name or GetName()
        std::string ClassName;  // UClass name
        float X, Y, Z;         // world location
        float Pitch, Yaw, Roll; // rotation
    };

    // Rebuild the cached actor list from the current UWorld. Returns the count.
    // Every actor keeps a stable index (slot) for as long as it exists, so the launcher can keep moving or
    // deleting it across refreshes. Only the actors closest to (cx,cy,cz) — within radius (0 = anywhere) and at
    // most maxCount of them — are put in the cached list; the full Athena island has far too many to send.
    uint32_t RefreshActors(bool haveCenter, float cx, float cy, float cz, float radius, uint32_t maxCount);

    // Get the last refreshed actor list.
    const std::vector<ActorInfo>& GetActors();

    // Get a single actor's properties as a JSON string.
    std::string GetActorPropsJson(uint32_t index);

    // Move an actor to a new world position.
    bool SetActorPosition(uint32_t index, float x, float y, float z);

    // Rotate an actor.
    bool SetActorRotation(uint32_t index, float pitch, float yaw, float roll);

    // Project a world point to screen coordinates. Returns false if behind camera.
    bool ProjectWorldToScreen(float wx, float wy, float wz, float& sx, float& sy);

    // Batch project N world points to screen.
    struct ScreenPoint { float X, Y; uint8_t OnScreen; };
    void BatchProjectWorldToScreen(const float* worldPoints, uint32_t count, ScreenPoint* out);

    // Get viewport size.
    void GetViewportSize(uint32_t& width, uint32_t& height);

    // Pick an actor by screen position (line trace). Returns actor index or 0xFFFFFFFF.
    uint32_t RaycastScreen(float screenX, float screenY);

    // Where the view ray under a screen point hits the world (x,y,z), and the actor it hit (index, or 0xFFFFFFFF for
    // the ground and other things that can't be picked). False when nothing is hit.
    bool TraceScreenPoint(float screenX, float screenY, float* outXYZ, uint32_t& outIndex);

    // Spawn an actor of the given class at a location. Returns index or 0xFFFFFFFF.
    uint32_t SpawnActorAtLocation(const char* classPath, float x, float y, float z);

    // What a piece looks like, for copying it (House Plots): its scale and, for building pieces, the asset paths of
    // its mesh (m) and its four texture sets (t), the alternate mesh (a) and whether it is mirrored (mi). JSON array,
    // one object per index that still exists.
    std::string GetLooksJson(const uint32_t* indices, uint32_t count);

    // Destroy an actor by index.
    bool DestroyActor(uint32_t index);
}
