#pragma once
// SyndicateStudio named pipe protocol.
// The launcher connects to \\.\pipe\SyndicateStudio_<pid> and sends binary commands.
// Each message: [uint8 Command] [uint32 PayloadSize] [payload bytes]
// Each response: [uint8 Command | 0x80] [uint32 PayloadSize] [payload bytes]

#include <cstdint>

namespace StudioPipe {

    // Commands (launcher -> DLL)
    enum Command : uint8_t {
        CMD_PING            = 0x01,  // -> PONG (no payload)
        CMD_GET_ACTORS      = 0x02,  // -> actor list JSON
        CMD_SELECT_ACTOR    = 0x03,  // payload: actor index (uint32) -> actor props JSON
        CMD_GET_ACTOR_PROPS = 0x04,  // payload: actor index (uint32) -> property JSON
        CMD_SET_ACTOR_POS   = 0x05,  // payload: index(u32) + x,y,z (3x float) -> OK/FAIL
        CMD_SET_ACTOR_ROT   = 0x06,  // payload: index(u32) + pitch,yaw,roll (3x float) -> OK/FAIL
        CMD_PROJECT_POINT   = 0x07,  // payload: x,y,z (3x float) -> screenX,screenY (2x float) + onScreen(u8)
        CMD_RAYCAST         = 0x08,  // payload: screenX,screenY (2x float) -> hit actor index (u32) or 0xFFFFFFFF
        CMD_GET_CAMERA      = 0x09,  // -> location(3f) + rotation(3f) + fov(f)
        CMD_SET_CAMERA      = 0x0A,  // payload: location(3f) + rotation(3f) -> OK
        CMD_FREE_CAM        = 0x0B,  // payload: enable(u8) -> OK
        CMD_GET_VIEWPORT    = 0x0C,  // -> width(u32) + height(u32)
        CMD_GAME_HWND       = 0x0D,  // -> hwnd as uint64
        CMD_GET_VERSION     = 0x0E,  // -> fortnite version as float
        CMD_SPAWN_ACTOR     = 0x0F,  // payload: class path (null-terminated string) + location(3f) -> index(u32)
        CMD_DELETE_ACTOR    = 0x10,  // payload: index(u32) -> OK/FAIL
        CMD_BATCH_PROJECT   = 0x11,  // payload: count(u32) + N * (x,y,z float) -> N * (sx,sy float, onscreen u8)
        CMD_TRACE_POINT     = 0x13,  // payload: screenX,screenY (2f) -> hit(u8) + x,y,z (3f) + actor index (u32)
        CMD_POLL_INPUT      = 0x12,  // -> free cam(u8) + dragging(u8) + box(4f) + count(u32) + N * (type u8, mods u8, vk u16, 4f)
        CMD_GET_LOOK        = 0x14,  // payload: count(u32) + N * index(u32) -> JSON [{i, sx,sy,sz, m, t[4], a, mi}] (mesh, textures, scale)
    };

    constexpr uint8_t RESPONSE_BIT = 0x80;

    // Response status bytes (first byte of payload for simple OK/FAIL commands)
    constexpr uint8_t STATUS_OK   = 1;
    constexpr uint8_t STATUS_FAIL = 0;

    // Pipe name format string (append PID)
    constexpr const wchar_t* PIPE_NAME_FMT = L"\\\\.\\pipe\\SyndicateStudio_%lu";

    // Max message payload size (1 MB should be plenty for actor lists)
    constexpr uint32_t MAX_PAYLOAD = 1024 * 1024;

    // ---- Shared data structures for game-thread execution ----

    // Request queued from pipe thread to game thread
    struct Request {
        Command Cmd;
        std::vector<uint8_t> Payload;
        std::vector<uint8_t> Response;
        std::atomic<bool> Done{ false };
        HANDLE DoneEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr); // set by the game thread when Done
        Request() = default;
        Request(const Request&) = delete;
        Request& operator=(const Request&) = delete;
        ~Request() { if (DoneEvent) CloseHandle(DoneEvent); }
    };

    void Init();
    void Shutdown();

    // Called on the game thread (from the WH_GETMESSAGE hook) to process pending requests
    void ProcessPendingRequests();
}
