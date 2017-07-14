#pragma once
#include "stdafx.h"
// ReSharper disable CppUnusedIncludeDirective
#include "TlHelp32.h"

#ifdef ReCa
#undef ReCa
#endif
#define ReCa reinterpret_cast

#define ERR_SUCCESS                 0x00000000
#define ERR_INVALID_PROC_HANDLE     0x00000001
#define ERR_USER32DLL_MISSING       0x00000008
#define ERR_CANT_CREATE_THREAD      0x80000001
#define ERR_CANT_ALLOC_MEM          0x80000002
#define ERR_WPM_FAIL                0x80000003
#define INJ_ERR_SETWDA_MISSING      0x00000009
#define ERR_SET_PRIV_FAIL           0x8000000B
#define ERR_CANT_FIND_HWND          0x0000000D
#define ERR_ADV_INV_PROC            0x00000001
#define ERR_ADV_NO_THREADS          0x00000003
#define ERR_ADV_CANT_OPEN_THREAD    0x00000004
#define ERR_ADV_SUSPEND_FAIL        0x00000005
#define ERR_ADV_GET_CONTEXT_FAIL    0x00000006
#define ERR_ADV_OUT_OF_MEMORY       0x00000007
#define ERR_ADV_WPM_FAIL            0x00000008
#define ERR_ADV_SET_CONTEXT_FAIL    0x00000009
#define ERR_ADV_RESUME_FAIL         0x0000000A

DWORD FixBlackScreen(const std::wstring& tProcName, bool HijackThread);
std::vector<HWND> GetHwnds(DWORD pid);