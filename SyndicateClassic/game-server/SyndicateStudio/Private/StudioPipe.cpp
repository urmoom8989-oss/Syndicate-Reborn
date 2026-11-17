#include "pch.h"
#include "../Public/StudioPipe.h"
#include "../Public/StudioScene.h"
#include "../Public/StudioCamera.h"
#include <deque>
#include <memory>

// The game window (defined in dllmain.cpp)
extern HWND StudioGetGameWindow();
extern void StudioLog(const char* Text);
extern void StudioWake(); // post the Studio message now instead of waiting for the next tick

namespace StudioPipe {

    static std::mutex QueueMutex;
    static std::deque<std::shared_ptr<Request>> PendingQueue;
    static HANDLE PipeThread = nullptr;
    static volatile bool Running = false;

    // ---- helpers ----

    static void WriteU8(std::vector<uint8_t>& buf, uint8_t v) { buf.push_back(v); }
    static void WriteU32(std::vector<uint8_t>& buf, uint32_t v) { auto p = reinterpret_cast<const uint8_t*>(&v); buf.insert(buf.end(), p, p + 4); }
    static void WriteFloat(std::vector<uint8_t>& buf, float v) { auto p = reinterpret_cast<const uint8_t*>(&v); buf.insert(buf.end(), p, p + 4); }
    static void WriteU64(std::vector<uint8_t>& buf, uint64_t v) { auto p = reinterpret_cast<const uint8_t*>(&v); buf.insert(buf.end(), p, p + 8); }
    static void WriteString(std::vector<uint8_t>& buf, const std::string& s) {
        buf.insert(buf.end(), s.begin(), s.end());
    }

    static uint32_t ReadU32(const uint8_t* p) { uint32_t v; memcpy(&v, p, 4); return v; }
    static float ReadFloat(const uint8_t* p) { float v; memcpy(&v, p, 4); return v; }
    static uint8_t ReadU8(const uint8_t* p) { return *p; }

    // JSON string escaping: actor and class names go straight into the reply, and one stray quote or
    // backslash used to make the whole list unreadable on the launcher side.
    static void AppendJsonString(std::string& out, const std::string& s) {
        out += '"';
        for (unsigned char c : s) {
            if (c == '"' || c == '\\') { out += '\\'; out += (char)c; }
            else if (c < 0x20) { char u[8]; snprintf(u, sizeof(u), "\\u%04x", c); out += u; }
            else out += (char)c;
        }
        out += '"';
    }

    // ---- Execute a request on the game thread ----

    static void ExecuteRequest(Request* req) {
        auto& pay = req->Payload;
        auto& resp = req->Response;

        switch (req->Cmd) {
        case CMD_PING:
            WriteU8(resp, STATUS_OK);
            break;

        case CMD_GET_ACTORS: {
            // Optional payload: center x,y,z + radius (4 floats) + max count (u32). Without it: around the live
            // camera, 200 m, 800 actors (the Outliner stays responsive; its filter box finds the rest). The whole Athena island is tens of thousands of actors — sending them all
            // made the reply bigger than the launcher accepts, which knocked every later command out of step.
            bool haveCenter = false; float cx = 0, cy = 0, cz = 0, radius = 20000.f; uint32_t maxCount = 800;
            if (pay.size() >= 20) {
                cx = ReadFloat(pay.data()); cy = ReadFloat(pay.data() + 4); cz = ReadFloat(pay.data() + 8);
                radius = ReadFloat(pay.data() + 12); maxCount = ReadU32(pay.data() + 16);
                haveCenter = true;
            } else {
                auto cam = StudioCamera::GetCamera();
                if (cam.Valid) { cx = cam.X; cy = cam.Y; cz = cam.Z; haveCenter = true; }
            }
            if (maxCount == 0 || maxCount > 5000) maxCount = 800;
            StudioScene::RefreshActors(haveCenter, cx, cy, cz, radius, maxCount);
            auto& actors = StudioScene::GetActors();
            std::string json;
            json.reserve(actors.size() * 140 + 2);
            json += "[";
            for (size_t i = 0; i < actors.size(); i++) {
                if (json.size() > 900 * 1024) break; // hard cap, well under the 1 MB frame limit
                if (i > 0) json += ",";
                auto& a = actors[i];
                char num[32];
                json += "{\"i\":"; snprintf(num, sizeof(num), "%u", a.Index); json += num;
                json += ",\"n\":"; AppendJsonString(json, a.Name);
                json += ",\"c\":"; AppendJsonString(json, a.ClassName);
                char rest[200];
                snprintf(rest, sizeof(rest), ",\"x\":%.1f,\"y\":%.1f,\"z\":%.1f,\"p\":%.2f,\"w\":%.2f,\"r\":%.2f}",
                    a.X, a.Y, a.Z, a.Pitch, a.Yaw, a.Roll);
                json += rest;
            }
            json += "]";
            WriteString(resp, json);
            static ULONGLONG NextLog = 0;
            if (GetTickCount64() >= NextLog) {
                NextLog = GetTickCount64() + 20000;
                char Line[200];
                snprintf(Line, sizeof(Line), "actors: %zu listed around (%.0f,%.0f,%.0f) center=%d, %zu bytes",
                    actors.size(), cx, cy, cz, haveCenter ? 1 : 0, json.size());
                StudioLog(Line);
            }
            break;
        }

        case CMD_SELECT_ACTOR:
        case CMD_GET_ACTOR_PROPS: {
            if (pay.size() < 4) { WriteU8(resp, STATUS_FAIL); break; }
            uint32_t idx = ReadU32(pay.data());
            std::string json = StudioScene::GetActorPropsJson(idx);
            if (json.empty()) { WriteU8(resp, STATUS_FAIL); break; }
            WriteString(resp, json);
            break;
        }

        case CMD_SET_ACTOR_POS: {
            if (pay.size() < 16) { WriteU8(resp, STATUS_FAIL); break; }
            uint32_t idx = ReadU32(pay.data());
            float x = ReadFloat(pay.data() + 4), y = ReadFloat(pay.data() + 8), z = ReadFloat(pay.data() + 12);
            WriteU8(resp, StudioScene::SetActorPosition(idx, x, y, z) ? STATUS_OK : STATUS_FAIL);
            break;
        }

        case CMD_SET_ACTOR_ROT: {
            if (pay.size() < 16) { WriteU8(resp, STATUS_FAIL); break; }
            uint32_t idx = ReadU32(pay.data());
            float p = ReadFloat(pay.data() + 4), w = ReadFloat(pay.data() + 8), r = ReadFloat(pay.data() + 12);
            WriteU8(resp, StudioScene::SetActorRotation(idx, p, w, r) ? STATUS_OK : STATUS_FAIL);
            break;
        }

        case CMD_PROJECT_POINT: {
            if (pay.size() < 12) { WriteU8(resp, STATUS_FAIL); break; }
            float wx = ReadFloat(pay.data()), wy = ReadFloat(pay.data() + 4), wz = ReadFloat(pay.data() + 8);
            float sx = 0, sy = 0;
            bool on = StudioScene::ProjectWorldToScreen(wx, wy, wz, sx, sy);
            WriteFloat(resp, sx);
            WriteFloat(resp, sy);
            WriteU8(resp, on ? 1 : 0);
            break;
        }

        case CMD_BATCH_PROJECT: {
            if (pay.size() < 4) { WriteU8(resp, STATUS_FAIL); break; }
            uint32_t count = ReadU32(pay.data());
            if (pay.size() < 4 + count * 12) { WriteU8(resp, STATUS_FAIL); break; }
            const float* pts = reinterpret_cast<const float*>(pay.data() + 4);
            std::vector<StudioScene::ScreenPoint> out(count);
            StudioScene::BatchProjectWorldToScreen(pts, count, out.data());
            for (uint32_t i = 0; i < count; i++) {
                WriteFloat(resp, out[i].X);
                WriteFloat(resp, out[i].Y);
                WriteU8(resp, out[i].OnScreen);
            }
            break;
        }

        case CMD_RAYCAST: {
            if (pay.size() < 8) { WriteU8(resp, STATUS_FAIL); break; }
            float sx = ReadFloat(pay.data()), sy = ReadFloat(pay.data() + 4);
            uint32_t hit = StudioScene::RaycastScreen(sx, sy);
            WriteU32(resp, hit);
            break;
        }

        case CMD_GET_CAMERA: {
            auto cam = StudioCamera::GetCamera();
            WriteFloat(resp, cam.X); WriteFloat(resp, cam.Y); WriteFloat(resp, cam.Z);
            WriteFloat(resp, cam.Pitch); WriteFloat(resp, cam.Yaw); WriteFloat(resp, cam.Roll);
            WriteFloat(resp, cam.FOV);
            WriteU8(resp, cam.Valid ? 1 : 0); // byte 28: 0 = no player camera yet
            break;
        }

        case CMD_SET_CAMERA: {
            if (pay.size() < 24) { WriteU8(resp, STATUS_FAIL); break; }
            float x = ReadFloat(pay.data()), y = ReadFloat(pay.data() + 4), z = ReadFloat(pay.data() + 8);
            float p = ReadFloat(pay.data() + 12), w = ReadFloat(pay.data() + 16), r = ReadFloat(pay.data() + 20);
            StudioCamera::SetCamera(x, y, z, p, w, r);
            WriteU8(resp, STATUS_OK);
            break;
        }

        case CMD_FREE_CAM: {
            if (pay.size() < 1) { WriteU8(resp, STATUS_FAIL); break; }
            WriteU8(resp, StudioCamera::SetFreeCam(pay[0] != 0) ? STATUS_OK : STATUS_FAIL);
            break;
        }

        case CMD_GET_VIEWPORT: {
            uint32_t w = 0, h = 0;
            StudioScene::GetViewportSize(w, h);
            WriteU32(resp, w);
            WriteU32(resp, h);
            break;
        }

        case CMD_GAME_HWND: {
            WriteU64(resp, (uint64_t)(uintptr_t)StudioGetGameWindow());
            break;
        }

        case CMD_GET_VERSION: {
            WriteFloat(resp, VersionInfo.FortniteVersion);
            break;
        }

        case CMD_SPAWN_ACTOR: {
            if (pay.size() < 13) { WriteU8(resp, STATUS_FAIL); break; } // at least 1 char + null + 12 floats
            const char* classPath = reinterpret_cast<const char*>(pay.data());
            size_t len = strnlen(classPath, pay.size());
            if (len + 1 + 12 > pay.size()) { WriteU8(resp, STATUS_FAIL); break; }
            float x = ReadFloat(pay.data() + len + 1);
            float y = ReadFloat(pay.data() + len + 5);
            float z = ReadFloat(pay.data() + len + 9);
            uint32_t idx = StudioScene::SpawnActorAtLocation(classPath, x, y, z);
            WriteU32(resp, idx);
            break;
        }

        case CMD_DELETE_ACTOR: {
            if (pay.size() < 4) { WriteU8(resp, STATUS_FAIL); break; }
            uint32_t idx = ReadU32(pay.data());
            WriteU8(resp, StudioScene::DestroyActor(idx) ? STATUS_OK : STATUS_FAIL);
            break;
        }

        case CMD_TRACE_POINT: {
            if (pay.size() < 8) { WriteU8(resp, STATUS_FAIL); break; }
            float xyz[3] = {}; uint32_t idx = 0xFFFFFFFF;
            bool hit = StudioScene::TraceScreenPoint(ReadFloat(pay.data()), ReadFloat(pay.data() + 4), xyz, idx);
            WriteU8(resp, hit ? 1 : 0);
            WriteFloat(resp, xyz[0]); WriteFloat(resp, xyz[1]); WriteFloat(resp, xyz[2]);
            WriteU32(resp, idx);
            break;
        }

        case CMD_POLL_INPUT: {
            float box[4] = {};
            bool dragging = StudioCamera::GetPickBox(box);
            auto events = StudioCamera::TakeInputEvents();
            WriteU8(resp, StudioCamera::IsFreeCam() ? 1 : 0);
            WriteU8(resp, dragging ? 1 : 0);
            for (float f : box) WriteFloat(resp, f);
            WriteU32(resp, (uint32_t)events.size());
            for (auto& e : events) {
                WriteU8(resp, e.Type);
                WriteU8(resp, e.Mods);
                resp.push_back(uint8_t(e.Vk & 0xFF)); resp.push_back(uint8_t(e.Vk >> 8));
                WriteFloat(resp, e.X0); WriteFloat(resp, e.Y0); WriteFloat(resp, e.X1); WriteFloat(resp, e.Y1);
            }
            break;
        }

        case CMD_GET_LOOK: {
            if (pay.size() < 4) { WriteU8(resp, STATUS_FAIL); break; }
            uint32_t n = ReadU32(pay.data());
            if (n > 2000 || pay.size() < 4 + size_t(n) * 4) { WriteU8(resp, STATUS_FAIL); break; }
            std::vector<uint32_t> idx(n);
            for (uint32_t k = 0; k < n; k++) idx[k] = ReadU32(pay.data() + 4 + k * 4);
            WriteString(resp, StudioScene::GetLooksJson(idx.data(), n));
            break;
        }

        default:
            WriteU8(resp, STATUS_FAIL);
            break;
        }
    }

    // ---- SEH wrapper for ExecuteRequest (no C++ objects with destructors) ----

    static void ExecuteRequest_Safe(Request* req) {
        __try {
            ExecuteRequest(req);
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            req->Response.clear();
            WriteU8(req->Response, STATUS_FAIL);
        }
    }

    // ---- Game thread processing ----
    // Note: this function has std::deque (destructor) so it must NOT contain __try.
    // The SEH protection is inside ExecuteRequest_Safe.

    void ProcessPendingRequests() {
        std::deque<std::shared_ptr<Request>> batch;
        {
            std::lock_guard<std::mutex> lock(QueueMutex);
            batch.swap(PendingQueue);
        }
        for (auto& req : batch) {
            ExecuteRequest_Safe(req.get());
            req->Done.store(true, std::memory_order_release);
            if (req->DoneEvent) SetEvent(req->DoneEvent);
        }
    }

    // ---- Pipe server thread ----

    static bool PipeRead(HANDLE pipe, void* buf, DWORD size) {
        DWORD read = 0;
        while (read < size) {
            DWORD got = 0;
            if (!ReadFile(pipe, (uint8_t*)buf + read, size - read, &got, nullptr) || got == 0) return false;
            read += got;
        }
        return true;
    }

    static bool PipeWrite(HANDLE pipe, const void* buf, DWORD size) {
        DWORD written = 0;
        while (written < size) {
            DWORD sent = 0;
            if (!WriteFile(pipe, (const uint8_t*)buf + written, size - written, &sent, nullptr) || sent == 0) return false;
            written += sent;
        }
        return true;
    }

    static void HandleClient(HANDLE pipe) {
        while (Running) {
            // Read header: [cmd(1)] [payloadSize(4)]
            uint8_t header[5];
            if (!PipeRead(pipe, header, 5)) break;

            Command cmd = static_cast<Command>(header[0]);
            uint32_t payloadSize = 0;
            memcpy(&payloadSize, header + 1, 4);
            if (payloadSize > MAX_PAYLOAD) break;

            // Read payload
            std::vector<uint8_t> payload(payloadSize);
            if (payloadSize > 0 && !PipeRead(pipe, payload.data(), payloadSize)) break;

            // Queue the request for game-thread execution. Heap-owned and shared with the queue: if we time out
            // below, the game thread may still run it later, and it must not write into a dead stack frame.
            auto req = std::make_shared<Request>();
            req->Cmd = cmd;
            req->Payload = std::move(payload);
            req->Done.store(false, std::memory_order_relaxed);

            {
                std::lock_guard<std::mutex> lock(QueueMutex);
                PendingQueue.push_back(req);
            }

            // Wake the game thread now, then wait for it (10 s: loading the island can stall it for a while)
            StudioWake();
            if (req->DoneEvent) WaitForSingleObject(req->DoneEvent, 10000);
            else for (int wait = 0; wait < 10000 && !req->Done.load(std::memory_order_acquire); wait++) Sleep(1);

            if (!req->Done.load(std::memory_order_acquire)) {
                // Timed out — drop this connection; the launcher reconnects with a clean stream
                break;
            }

            // Write response: [cmd | 0x80 (1)] [responseSize(4)] [response data]
            uint8_t respCmd = cmd | RESPONSE_BIT;
            uint32_t respSize = static_cast<uint32_t>(req->Response.size());
            if (!PipeWrite(pipe, &respCmd, 1)) break;
            if (!PipeWrite(pipe, &respSize, 4)) break;
            if (respSize > 0 && !PipeWrite(pipe, req->Response.data(), respSize)) break;
        }
    }

    static DWORD WINAPI PipeServerThread(void*) {
        wchar_t pipeName[128];
        swprintf_s(pipeName, PIPE_NAME_FMT, GetCurrentProcessId());

        while (Running) {
            HANDLE pipe = CreateNamedPipeW(
                pipeName,
                PIPE_ACCESS_DUPLEX,
                PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
                PIPE_UNLIMITED_INSTANCES,
                65536, 65536,
                0, nullptr);

            if (pipe == INVALID_HANDLE_VALUE) {
                Sleep(1000);
                continue;
            }

            // Wait for a client
            if (ConnectNamedPipe(pipe, nullptr) || GetLastError() == ERROR_PIPE_CONNECTED) {
                HandleClient(pipe);
            }

            DisconnectNamedPipe(pipe);
            CloseHandle(pipe);
        }
        return 0;
    }

    // ---- Public API ----

    void Init() {
        Running = true;
        PipeThread = CreateThread(nullptr, 0, PipeServerThread, nullptr, 0, nullptr);
    }

    void Shutdown() {
        Running = false;
        if (PipeThread) {
            // Unblock the pipe wait by connecting and disconnecting
            wchar_t pipeName[128];
            swprintf_s(pipeName, PIPE_NAME_FMT, GetCurrentProcessId());
            HANDLE wake = CreateFileW(pipeName, GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
            if (wake != INVALID_HANDLE_VALUE) CloseHandle(wake);
            WaitForSingleObject(PipeThread, 3000);
            CloseHandle(PipeThread);
            PipeThread = nullptr;
        }
    }
}
