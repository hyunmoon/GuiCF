#pragma once
#include "stdafx.h"
#include "wincodec.h"
#pragma comment(lib, "windowscodecs.lib")

HBITMAP LoadSplashImage(int resID);
void GetFullScreenShot(HBITMAP * hBitmap, int dWidth, int dHeight);