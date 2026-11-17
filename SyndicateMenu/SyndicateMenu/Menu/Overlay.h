#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

extern HWND overlay;
extern HDC memDC;
extern HBITMAP hbm;
extern HBITMAP curBmp;
extern void* dibBits;
extern int dibW, dibH;
extern int W, H;
extern HFONT fontSm, fontMd;
extern HWND gameWnd;
extern volatile bool running;

void RequestUnload();

void DrawText2(HDC dc, int x, int y, COLORREF col, const char* txt, HFONT f);
void Line(HDC dc, int x1, int y1, int x2, int y2, COLORREF col, int wdt);
void Rect(HDC dc, int x, int y, int w, int h, COLORREF col, int wdt);
void Fill(HDC dc, int x, int y, int w, int h, COLORREF col);
void EnsureBackbuffer(int w, int h);
void ResizeOverlay();
HWND FindGameWindow();
bool CreateOverlay();
