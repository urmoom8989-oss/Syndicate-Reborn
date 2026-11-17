#include "Overlay.h"

#include <cstring>

HWND overlay = nullptr;
HDC memDC = nullptr;
HBITMAP hbm = nullptr;
HBITMAP curBmp = nullptr;
void* dibBits = nullptr;
int dibW = 0;
int dibH = 0;
int W = 1920;
int H = 1080;
HFONT fontSm = nullptr;
HFONT fontMd = nullptr;
HWND gameWnd = nullptr;
volatile bool running = true;

void RequestUnload() {
    running = false;
}

void DrawText2(HDC dc, int x, int y, COLORREF col, const char* txt, HFONT f) {
    SelectObject(dc, f);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, col);
    TextOutA(dc, x, y, txt, (int)strlen(txt));
}

void Line(HDC dc, int x1, int y1, int x2, int y2, COLORREF col, int wdt) {
    HPEN p = CreatePen(PS_SOLID, wdt, col);
    HPEN op = (HPEN)SelectObject(dc, p);
    MoveToEx(dc, x1, y1, nullptr);
    LineTo(dc, x2, y2);
    SelectObject(dc, op);
    DeleteObject(p);
}

void Rect(HDC dc, int x, int y, int w, int h, COLORREF col, int wdt) {
    HPEN p = CreatePen(PS_SOLID, wdt, col);
    HPEN op = (HPEN)SelectObject(dc, p);
    HBRUSH ob = (HBRUSH)SelectObject(dc, GetStockObject(NULL_BRUSH));
    Rectangle(dc, x, y, x + w, y + h);
    SelectObject(dc, ob);
    SelectObject(dc, op);
    DeleteObject(p);
}

void Fill(HDC dc, int x, int y, int w, int h, COLORREF col) {
    RECT r;
    r.left = x;
    r.top = y;
    r.right = x + w;
    r.bottom = y + h;
    HBRUSH b = CreateSolidBrush(col);
    FillRect(dc, &r, b);
    DeleteObject(b);
}

void EnsureBackbuffer(int w, int h) {
    if (w <= 0 || h <= 0) return;
    if (memDC && hbm && w == dibW && h == dibH) return;
    if (!memDC) {
        HDC s = GetDC(nullptr);
        memDC = CreateCompatibleDC(s);
        ReleaseDC(nullptr, s);
        if (!memDC) return;
    }
    HDC s = GetDC(nullptr);
    BITMAPINFO bi;
    ZeroMemory(&bi, sizeof(bi));
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = w;
    bi.bmiHeader.biHeight = -h;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    HBITMAP nb = CreateDIBSection(s, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    ReleaseDC(nullptr, s);
    if (!nb) return;
    SelectObject(memDC, nb);
    if (curBmp) DeleteObject(curBmp);
    curBmp = nb;
    hbm = nb;
    dibBits = bits;
    dibW = w;
    dibH = h;
}

void ResizeOverlay() {
    if (!overlay || !gameWnd || !IsWindow(gameWnd)) return;
    RECT r;
    if (!GetClientRect(gameWnd, &r)) return;
    int nw = r.right - r.left;
    int nh = r.bottom - r.top;
    if (nw <= 0 || nh <= 0) return;
    POINT tl;
    tl.x = 0;
    tl.y = 0;
    if (!ClientToScreen(gameWnd, &tl)) return;
    W = nw;
    H = nh;
    EnsureBackbuffer(W, H);
    SetWindowPos(overlay, HWND_TOPMOST, tl.x, tl.y, nw, nh, SWP_NOACTIVATE | SWP_SHOWWINDOW);
}

static LRESULT CALLBACK WndProc(HWND h, UINT m, WPARAM wp, LPARAM lp) {
    return DefWindowProcA(h, m, wp, lp);
}

struct WinCtx {
    DWORD pid;
    HWND best;
    LONG area;
};

static BOOL CALLBACK EnumGameWnd(HWND h, LPARAM lp) {
    WinCtx* c = (WinCtx*)lp;
    DWORD pid = 0;
    GetWindowThreadProcessId(h, &pid);
    if (pid != c->pid || !IsWindowVisible(h) || GetWindow(h, GW_OWNER)) return TRUE;
    RECT r;
    GetClientRect(h, &r);
    LONG a = (r.right - r.left) * (r.bottom - r.top);
    if (a > c->area) {
        c->area = a;
        c->best = h;
    }
    return TRUE;
}

HWND FindGameWindow() {
    WinCtx c;
    c.pid = GetCurrentProcessId();
    c.best = nullptr;
    c.area = 0;
    EnumWindows(EnumGameWnd, (LPARAM)&c);
    return c.best;
}

bool CreateOverlay() {
    WNDCLASSEXA wc;
    ZeroMemory(&wc, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = WndProc;
    wc.hInstance = GetModuleHandle(nullptr);
    wc.lpszClassName = "VeniceOverlayDLL";
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    RegisterClassExA(&wc);

    overlay = CreateWindowExA(
        WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
        "VeniceOverlayDLL", "", WS_POPUP, 0, 0, W, H, nullptr, nullptr, wc.hInstance, nullptr);
    if (!overlay) return false;

    SetLayeredWindowAttributes(overlay, RGB(0, 0, 0), 255, LWA_COLORKEY);
    ShowWindow(overlay, SW_SHOW);
    UpdateWindow(overlay);
    SetWindowPos(overlay, HWND_TOPMOST, 0, 0, W, H, SWP_NOACTIVATE | SWP_SHOWWINDOW);

    fontSm = CreateFontA(14, 0, 0, 0, FW_NORMAL, 0, 0, 0, 0, 0, 0, 0, 0, "Consolas");
    fontMd = CreateFontA(17, 0, 0, 0, FW_BOLD, 0, 0, 0, 0, 0, 0, 0, 0, "Consolas");
    return true;
}
