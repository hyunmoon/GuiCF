#pragma once
#include "stdafx.h"
#include "TlHelp32.h"  

#ifdef ReCa
#undef ReCa
#endif
#define ReCa reinterpret_cast

#ifdef UNICODE
#undef Module32First
#undef Module32Next
#undef MODULEENTRY32
#endif

#define MONITOR_CENTER   0x0001        // center rect to monitor 
#define MONITOR_CLIP     0x0000        // clip rect to monitor 
#define MONITOR_WORKAREA 0x0002        // use monitor work area 
#define MONITOR_AREA     0x0000        // use monitor entire area 

typedef _Return_type_success_(return >= 0) LONG NTSTATUS;

typedef NTSTATUS(__stdcall * f_NtCreateThreadEx)(HANDLE * pHandle, ACCESS_MASK DesiredAccess,
	void * pAttr, HANDLE hProc, void * pFunc, void * pArg, ULONG Flags, SIZE_T ZeroBits,
	SIZE_T StackSize, SIZE_T MaxStackSize, void * pAttrListOut);

typedef BOOL(__stdcall * f_SetWindowDisplayAffinity)(HWND hWnd, DWORD dwAffinity);

void ClipOrCenterWindowToMonitor(HWND hwnd, UINT flags);

std::vector<HWND> GetHwnds(DWORD pid);

std::string FindFile(const std::string fileFullPath, BOOL excludeSelf);

bool SetPrivilegeA(const char * szPrivilege, bool bState);

DWORD FindProcessId(const std::wstring& processName);

HANDLE StartRoutine(HANDLE hTargetProc, void * pRoutine, void * pArg, bool Hijack, bool Fastcall);

void ErrorMsg(DWORD Err1, DWORD Err2);

#define INJ_ERR_SUCCESS					0x00000000
#define INJ_ERR_INVALID_PROC_HANDLE		0x00000001
#define INJ_ERR_FILE_DOESNT_EXIST		0x00000002
#define INJ_ERR_OUT_OF_MEMORY			0x00000003
#define INJ_ERR_INVALID_FILE			0x00000004
#define INJ_ERR_NO_X64FILE				0x00000005
#define INJ_ERR_NO_X86FILE				0x00000006
#define INJ_ERR_IMAGE_CANT_RELOC		0x00000007
#define INJ_ERR_NTDLL_MISSING			0x00000008
#define INJ_ERR_LDRLOADDLL_MISSING		0x00000009
#define INJ_ERR_LDRPLOADDLL_MISSING		0x0000000A
#define INJ_ERR_INVALID_FLAGS			0x0000000B
#define INJ_ERR_CANT_FIND_MOD			0x0000000C
#define INJ_ERR_CANT_FIND_MOD_PEB		0x0000000D

#define INJ_ERR_UNKNOWN					0x80000000
#define INJ_ERR_CANT_CREATE_THREAD		0x80000001
#define INJ_ERR_CANT_ALLOC_MEM			0x80000002
#define INJ_ERR_WPM_FAIL				0x80000003
#define INJ_ERR_TH32_FAIL				0x80000004
#define INJ_ERR_CANT_GET_PEB			0x80000005
#define INJ_ERR_CANT_ACCESS_PEB			0x80000006
#define INJ_ERR_CANT_ACCESS_PEB_LDR		0x80000007
#define INJ_ERR_CHECK_WIN32_ERROR		0x80000008
#define INJ_ERR_VPE_FAIL				0x80000009
#define INJ_ERR_INVALID_ARGC			0x8000000A
#define INJ_ERR_SET_PRIV_FAIL			0x8000000B
#define INJ_ERR_CANT_OPEN_PROCESS		0x8000000C
#define INJ_ERR_CANT_START_X64_INJ		0x8000000D
#define INJ_ERR_INVALID_PID				0x8000000E

#define INJ_ERR_ADV_UNKNOWN				0x00000000
#define INJ_ERR_ADV_INV_PROC			0x00000001
#define INJ_ERR_ADV_TH32_FAIL			0x00000002
#define INJ_ERR_ADV_NO_THREADS			0x00000003
#define INJ_ERR_ADV_CANT_OPEN_THREAD	0x00000004
#define INJ_ERR_ADV_SUSPEND_FAIL		0x00000005
#define INJ_ERR_ADV_GET_CONTEXT_FAIL	0x00000006
#define INJ_ERR_ADV_OUT_OF_MEMORY		0x00000007
#define INJ_ERR_ADV_WPM_FAIL			0x00000008
#define INJ_ERR_ADV_SET_CONTEXT_FAIL	0x00000009
#define INJ_ERR_ADV_RESUME_FAIL			0x0000000A
#define INJ_ERR_ADV_QIP_MISSING			0x0000000B
#define INJ_ERR_ADV_QIP_FAIL			0x0000000C
#define INJ_ERR_ADV_CANT_FIND_MODULE	0x0000000D