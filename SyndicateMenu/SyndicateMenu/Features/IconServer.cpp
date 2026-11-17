// A one-file HTTP server on 127.0.0.1 for the Phantom's locker icon (see CustomOutfits.cpp, "own locker icon").
// Season 3 can only make a texture from an image it downloads (UMG AsyncTaskDownloadImage), so the menu serves
// syndicate-phantom-icon.png itself. Loopback only, a free port picked by Windows, any GET gets the PNG. The thread
// holds a reference on this DLL (so End can unload the menu safely) and stops when asked or after two minutes.
// Kept in its own file: winsock2.h has to come before windows.h.
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <cstdio>

#include "IconServer.h"

#pragma comment(lib, "ws2_32.lib")

namespace {

volatile LONG g_port = 0;      // 0 starting or not started, > 0 listening, -1 failed, -2 stopped
volatile LONG g_stop = 0;
volatile LONG g_started = 0;
char* g_bytes = nullptr;
DWORD g_size = 0;

bool SendAll(SOCKET c, const char* data, int size) {
    while (size > 0) {
        const int sent = send(c, data, size, 0);
        if (sent <= 0)
            return false;
        data += sent;
        size -= sent;
    }
    return true;
}

DWORD WINAPI ServerThread(LPVOID) {
    HMODULE self = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS, reinterpret_cast<LPCWSTR>(&ServerThread), &self);
    WSADATA wsa{};
    SOCKET listener = INVALID_SOCKET;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) == 0) {
        listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        addr.sin_port = 0;
        int len = sizeof(addr);
        if (listener != INVALID_SOCKET && bind(listener, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == 0 &&
            listen(listener, 4) == 0 && getsockname(listener, reinterpret_cast<sockaddr*>(&addr), &len) == 0) {
            InterlockedExchange(&g_port, ntohs(addr.sin_port));
            printf("[outfits] icon: serving syndicate-phantom-icon.png on 127.0.0.1:%d\n", (int)ntohs(addr.sin_port));
            const ULONGLONG until = GetTickCount64() + 120000;
            int served = 0;
            while (!g_stop && GetTickCount64() < until && served < 8) {
                fd_set set;
                FD_ZERO(&set);
                FD_SET(listener, &set);
                timeval wait{ 0, 250000 };
                if (select(0, &set, nullptr, nullptr, &wait) <= 0)
                    continue;
                const SOCKET c = accept(listener, nullptr, nullptr);
                if (c == INVALID_SOCKET)
                    continue;
                DWORD timeout = 3000;
                setsockopt(c, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeout), sizeof(timeout));
                setsockopt(c, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char*>(&timeout), sizeof(timeout));
                char request[2048];
                recv(c, request, sizeof(request), 0); // which path was asked for does not matter
                char head[256];
                const int n = sprintf_s(head, "HTTP/1.1 200 OK\r\nContent-Type: image/png\r\nContent-Length: %lu\r\nConnection: close\r\n\r\n", g_size);
                const bool ok = SendAll(c, head, n) && SendAll(c, g_bytes, (int)g_size);
                shutdown(c, SD_SEND);
                closesocket(c);
                ++served;
                printf("[outfits] icon: the game downloaded the icon (%s, %lu bytes)\n", ok ? "sent" : "send failed", g_size);
            }
        } else {
            printf("[outfits] icon: could not open a local port for the icon (error %d)\n", WSAGetLastError());
            InterlockedExchange(&g_port, -1);
        }
        if (listener != INVALID_SOCKET)
            closesocket(listener);
        WSACleanup();
    } else {
        printf("[outfits] icon: Winsock did not start\n");
        InterlockedExchange(&g_port, -1);
    }
    if (g_port > 0)
        InterlockedExchange(&g_port, -2);
    FreeLibraryAndExitThread(self, 0);
    return 0;
}

} // namespace

bool StartIconServer(const wchar_t* file) {
    if (InterlockedCompareExchange(&g_started, 1, 0) != 0)
        return true;
    const HANDLE h = CreateFileW(file, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        InterlockedExchange(&g_port, -1);
        return false;
    }
    const DWORD size = GetFileSize(h, nullptr);
    char* bytes = size && size < 16 * 1024 * 1024 ? static_cast<char*>(HeapAlloc(GetProcessHeap(), 0, size)) : nullptr;
    DWORD read = 0;
    const bool ok = bytes && ReadFile(h, bytes, size, &read, nullptr) && read == size;
    CloseHandle(h);
    if (!ok) {
        InterlockedExchange(&g_port, -1);
        return false;
    }
    g_bytes = bytes; // kept for the life of the process: the server thread may still be sending
    g_size = size;
    const HANDLE thread = CreateThread(nullptr, 0, ServerThread, nullptr, 0, nullptr);
    if (!thread) {
        InterlockedExchange(&g_port, -1);
        return false;
    }
    CloseHandle(thread);
    return true;
}

int IconServerPort() {
    return (int)g_port;
}

void StopIconServer() {
    InterlockedExchange(&g_stop, 1);
}
