#pragma once

// Serves one PNG file on 127.0.0.1 (a free port) for the game to download. See IconServer.cpp.
bool StartIconServer(const wchar_t* file);
// > 0 the port, 0 starting, -1 failed, -2 stopped.
int IconServerPort();
void StopIconServer();
