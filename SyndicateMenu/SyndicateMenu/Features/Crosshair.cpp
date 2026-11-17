#include "Crosshair.h"

#include <imgui.h>
#include <windows.h>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

// Read on the render thread only (TickCrosshairConfig and DrawCrosshair run inside the Present
// hook), so there is no lock. No std::mutex anywhere in this DLL: Season X's own old msvcp140.dll
// crashes on it (see AGENTS.md).
struct CrosshairConfig {
    bool enabled = false;
    bool lines = true;       // the four arms
    bool tStyle = false;     // no top arm (straight arms only)
    bool diagonal = false;   // arms turned 45 degrees (an X)
    int length = 6;          // arm length, px
    int thickness = 2;       // arm thickness, px
    int gap = 3;             // empty space between the centre and each arm, px
    bool dot = false;
    int dotSize = 2;
    bool dotRound = false;
    bool circle = false;
    int circleRadius = 10;
    int circleThickness = 1;
    bool outline = true;
    int outlineThickness = 1;
    ImU32 color = IM_COL32(0, 255, 120, 255);
    ImU32 outlineColor = IM_COL32(0, 0, 0, 200);
    bool hideWithCursor = true;  // hidden in the lobby, menus, map and inventory (the mouse cursor is showing)
    bool hideWhenAiming = false; // hidden while the right mouse button is held
};

static CrosshairConfig g_cross;

static bool LocalPath(wchar_t* out, size_t size, const wchar_t* sub, const wchar_t* file) {
    wchar_t base[MAX_PATH] = {};
    if (!GetEnvironmentVariableW(L"LOCALAPPDATA", base, MAX_PATH))
        return false;
    return swprintf_s(out, size, L"%s\\%s\\%s", base, sub, file) > 0;
}

// The launcher writes to %LOCALAPPDATA%\ProjectTilted, or to SyndicateLauncher\ProjectTilted on a PC
// that never had that folder (Services/ClientMods.cs).
static bool FindSharedFile(const wchar_t* file, wchar_t* out, size_t size) {
    if (LocalPath(out, size, L"ProjectTilted", file) && GetFileAttributesW(out) != INVALID_FILE_ATTRIBUTES)
        return true;
    if (LocalPath(out, size, L"SyndicateLauncher\\ProjectTilted", file) && GetFileAttributesW(out) != INVALID_FILE_ATTRIBUTES)
        return true;
    return false;
}

bool CrosshairOnlyMode() {
    static int mode = -1;
    if (mode >= 0)
        return mode == 1;
    mode = 0;
    wchar_t path[MAX_PATH] = {};
    if (FindSharedFile(L"menu-load.txt", path, MAX_PATH)) {
        FILE* f = nullptr;
        if (_wfopen_s(&f, path, L"rb") == 0 && f) {
            char text[32] = {};
            fread(text, 1, sizeof(text) - 1, f);
            fclose(f);
            if (_strnicmp(text, "crosshair", 9) == 0)
                mode = 1;
        }
    }
    printf("[crosshair] %s\n", mode == 1 ? "crosshair-only mode (menu off)" : "full menu mode");
    return mode == 1;
}

static int ClampI(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

// "AARRGGBB" or "RRGGBB" (with or without #).
static bool ParseColor(const char* text, ImU32& out) {
    while (*text == ' ' || *text == '#')
        ++text;
    char* end = nullptr;
    unsigned long v = strtoul(text, &end, 16);
    const size_t n = (size_t)(end - text);
    if (n != 6 && n != 8)
        return false;
    const unsigned a = n == 8 ? (unsigned)((v >> 24) & 0xFF) : 255u;
    out = IM_COL32((v >> 16) & 0xFF, (v >> 8) & 0xFF, v & 0xFF, a);
    return true;
}

static void ParseConfig(char* text, CrosshairConfig& c) {
    char* next = nullptr;
    for (char* line = strtok_s(text, "\r\n", &next); line; line = strtok_s(nullptr, "\r\n", &next)) {
        char* eq = strchr(line, '=');
        if (!eq)
            continue;
        *eq = 0;
        const char* key = line;
        const char* val = eq + 1;
        const int n = atoi(val);
        const bool b = n != 0;
        if (!_stricmp(key, "enabled")) c.enabled = b;
        else if (!_stricmp(key, "lines")) c.lines = b;
        else if (!_stricmp(key, "t")) c.tStyle = b;
        else if (!_stricmp(key, "diagonal")) c.diagonal = b;
        else if (!_stricmp(key, "length")) c.length = ClampI(n, 0, 100);
        else if (!_stricmp(key, "thickness")) c.thickness = ClampI(n, 1, 20);
        else if (!_stricmp(key, "gap")) c.gap = ClampI(n, 0, 100);
        else if (!_stricmp(key, "dot")) c.dot = b;
        else if (!_stricmp(key, "dotsize")) c.dotSize = ClampI(n, 1, 20);
        else if (!_stricmp(key, "dotround")) c.dotRound = b;
        else if (!_stricmp(key, "circle")) c.circle = b;
        else if (!_stricmp(key, "circleradius")) c.circleRadius = ClampI(n, 2, 200);
        else if (!_stricmp(key, "circlethickness")) c.circleThickness = ClampI(n, 1, 20);
        else if (!_stricmp(key, "outline")) c.outline = b;
        else if (!_stricmp(key, "outlinethickness")) c.outlineThickness = ClampI(n, 1, 5);
        else if (!_stricmp(key, "color")) ParseColor(val, c.color);
        else if (!_stricmp(key, "outlinecolor")) ParseColor(val, c.outlineColor);
        else if (!_stricmp(key, "hidewithcursor")) c.hideWithCursor = b;
        else if (!_stricmp(key, "hidewhenaiming")) c.hideWhenAiming = b;
    }
}

void TickCrosshairConfig() {
    static ULONGLONG nextCheck = 0;
    static FILETIME lastWrite{};
    static bool had = false;
    const ULONGLONG now = GetTickCount64();
    if (now < nextCheck)
        return;
    nextCheck = now + 500;

    wchar_t path[MAX_PATH] = {};
    WIN32_FILE_ATTRIBUTE_DATA info{};
    if (!FindSharedFile(L"crosshair.ini", path, MAX_PATH) || !GetFileAttributesExW(path, GetFileExInfoStandard, &info)) {
        if (had) {
            printf("[crosshair] crosshair.ini removed: crosshair off\n");
            g_cross = CrosshairConfig{};
            had = false;
        }
        return;
    }
    if (had && CompareFileTime(&info.ftLastWriteTime, &lastWrite) == 0)
        return;

    FILE* f = nullptr;
    if (_wfopen_s(&f, path, L"rb") != 0 || !f)
        return; // the launcher is writing it: try again next check
    char text[4096] = {};
    const size_t read = fread(text, 1, sizeof(text) - 1, f);
    fclose(f);
    text[read] = 0;
    CrosshairConfig c;
    ParseConfig(text, c);
    g_cross = c;
    lastWrite = info.ftLastWriteTime;
    had = true;
    printf("[crosshair] settings loaded: %s, length %d thickness %d gap %d dot %d circle %d\n",
        c.enabled ? "on" : "off", c.length, c.thickness, c.gap, c.dot ? c.dotSize : 0, c.circle ? c.circleRadius : 0);
}

static bool CursorShowing() {
    CURSORINFO info{ sizeof(info) };
    return GetCursorInfo(&info) && (info.flags & CURSOR_SHOWING) && info.hCursor;
}

static bool GameInFront() {
    DWORD pid = 0;
    if (HWND front = GetForegroundWindow())
        GetWindowThreadProcessId(front, &pid);
    return pid == GetCurrentProcessId();
}

// Start of a span of `size` pixels centred in `total` pixels, on a whole pixel so edges stay sharp.
// Exactly centred when total and size are both even or both odd; otherwise half a pixel to the
// upper left, the same way the game's own reticle sits.
static float Start(float total, float size) { return floorf((total - size) * 0.5f); }

void DrawCrosshair(float W, float H, bool menuOpen) {
    const CrosshairConfig& c = g_cross;
    if (!c.enabled || W < 16.f || H < 16.f || menuOpen)
        return;
    if (c.hideWithCursor && CursorShowing())
        return;
    if (c.hideWhenAiming && GameInFront() && (GetAsyncKeyState(VK_RBUTTON) & 0x8000))
        return;

    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    const float o = c.outline ? (float)c.outlineThickness : 0.f;
    const float t = (float)c.thickness;
    const float len = (float)c.length;
    const float gap = (float)c.gap;
    const ImVec2 mid(W * 0.5f, H * 0.5f);

    // Outlines first, then every fill, so an arm's outline never covers the next arm.
    for (int pass = 0; pass < 2; ++pass) {
        const bool edgePass = pass == 0;
        if (edgePass && !c.outline)
            continue;
        const ImU32 col = edgePass ? c.outlineColor : c.color;
        const float grow = edgePass ? o : 0.f;

        if (c.circle) {
            const float ct = (float)c.circleThickness + grow * 2.f;
            dl->AddCircle(mid, (float)c.circleRadius, col, 64, ct);
        }

        if (c.lines && len > 0.f) {
            if (!c.diagonal) {
                const float cx = Start(W, t), cy = Start(H, t); // the centre column / row, t pixels wide
                auto rect = [&](float x0, float y0, float x1, float y1) {
                    dl->AddRectFilled(ImVec2(x0 - grow, y0 - grow), ImVec2(x1 + grow, y1 + grow), col);
                };
                rect(cx + t + gap, cy, cx + t + gap + len, cy + t);    // right
                rect(cx - gap - len, cy, cx - gap, cy + t);            // left
                rect(cx, cy + t + gap, cx + t, cy + t + gap + len);    // bottom
                if (!c.tStyle)
                    rect(cx, cy - gap - len, cx + t, cy - gap);        // top
                if (gap <= 0.f && !c.dot)
                    rect(cx, cy, cx + t, cy + t);                      // no gap: a solid plus
            } else {
                const float r0 = gap + t * 0.5f, r1 = r0 + len;
                const float k = 0.70710678f;
                const float dirs[4][2] = { { k, k }, { -k, k }, { k, -k }, { -k, -k } };
                for (int i = 0; i < 4; ++i) {
                    const float a = r0 - grow, b = r1 + grow;
                    dl->AddLine(ImVec2(mid.x + dirs[i][0] * a, mid.y + dirs[i][1] * a),
                                ImVec2(mid.x + dirs[i][0] * b, mid.y + dirs[i][1] * b), col, t + grow * 2.f);
                }
            }
        }

        if (c.dot) {
            const float d = (float)c.dotSize;
            if (c.dotRound) {
                dl->AddCircleFilled(mid, d * 0.5f + grow, col, 24);
            } else {
                const float x0 = Start(W, d), y0 = Start(H, d);
                dl->AddRectFilled(ImVec2(x0 - grow, y0 - grow), ImVec2(x0 + d + grow, y0 + d + grow), col);
            }
        }
    }
}
