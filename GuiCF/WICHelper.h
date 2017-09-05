#pragma once
#include "stdafx.h"
#include "wincodec.h"
#pragma comment(lib, "windowscodecs.lib")

HBITMAP LoadSplashImage(int resID);
IStream * CreateStreamOnResource(LPCTSTR lpName, LPCTSTR lpType);
IWICBitmapSource * LoadBitmapFromStream(IStream * ipImageStream);
void GetFullScreenShot(HBITMAP * hBitmap, int dWidth, int dHeight);