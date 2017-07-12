#include "stdafx.h"
#include "Win32Helper.h"

DWORD LastError = INJ_ERR_SUCCESS;
DWORD dwError = ERROR_SUCCESS;

// 
//  ClipOrCenterRectToMonitor 
// 
//  The most common problem apps have when running on a 
//  multimonitor system is that they "clip" or "pin" windows 
//  based on the SM_CXSCREEN and SM_CYSCREEN system metrics. 
//  Because of app compatibility reasons these system metrics 
//  return the size of the primary monitor. 
// 
//  This shows how you use the multi-monitor functions 
//  to do the same thing. 
// 
void ClipOrCenterRectToMonitor(LPRECT prc, UINT flags)
{
	HMONITOR hMonitor;
	MONITORINFO mi;
	RECT        rc;
	int         w = prc->right - prc->left;
	int         h = prc->bottom - prc->top;

	// 
	// get the nearest monitor to the passed rect. 
	// 
	hMonitor = MonitorFromRect(prc, MONITOR_DEFAULTTONEAREST);

	// 
	// get the work area or entire monitor rect. 
	// 
	mi.cbSize = sizeof(mi);
	GetMonitorInfo(hMonitor, &mi);

	if (flags & MONITOR_WORKAREA)
		rc = mi.rcWork;
	else
		rc = mi.rcMonitor;

	// 
	// center or clip the passed rect to the monitor rect 
	// 
	if (flags & MONITOR_CENTER)
	{
		prc->left = rc.left + (rc.right - rc.left - w) / 2;
		prc->top = rc.top + (rc.bottom - rc.top - h) / 2;
		prc->right = prc->left + w;
		prc->bottom = prc->top + h;
	}
	else
	{
		prc->left = max(rc.left, min(rc.right - w, prc->left));
		prc->top = max(rc.top, min(rc.bottom - h, prc->top));
		prc->right = prc->left + w;
		prc->bottom = prc->top + h;
	}
}

void ClipOrCenterWindowToMonitor(HWND hwnd, UINT flags)
{
	RECT rc;
	GetWindowRect(hwnd, &rc);
	ClipOrCenterRectToMonitor(&rc, flags);
	SetWindowPos(hwnd, NULL, rc.left, rc.top, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
}

struct EnumWindowsCallbackArgs {
	EnumWindowsCallbackArgs(DWORD p) : pid(p) { }
	const DWORD pid;
	std::vector<HWND> handles;
};

static BOOL CALLBACK EnumWindowsCallback(HWND hnd, LPARAM lParam)
{
	EnumWindowsCallbackArgs *args = (EnumWindowsCallbackArgs *)lParam;

	DWORD windowPID;
	(void)::GetWindowThreadProcessId(hnd, &windowPID);
	if (windowPID == args->pid) {
		args->handles.push_back(hnd);
	}

	return TRUE;
}

std::vector<HWND> GetHwnds(DWORD pid)
{
	EnumWindowsCallbackArgs args(pid);
	if (::EnumWindows(&EnumWindowsCallback, (LPARAM)&args) == FALSE) {
		return std::vector<HWND>();
	}

	return args.handles;
}

std::string FindFile(const std::string fileFullPath, BOOL excludeSelf)
{
	std::string foundFileName = ""; // blank string returned if not found
	WIN32_FIND_DATAA FindFileData;
	HANDLE hFind = FindFirstFileA(fileFullPath.c_str(), &FindFileData);

	if (excludeSelf) {
		char buffer[MAX_PATH];
		GetModuleFileNameA(NULL, buffer, MAX_PATH);
		std::string::size_type left = std::string(buffer).find_last_of("\\/");
		std::string fName = std::string(buffer).substr(left + 1);

		while (true) {
			if (hFind == INVALID_HANDLE_VALUE) {
				// FindFirstFileA failed
				break;
			}
			else if (FindFileData.cFileName != fName) {
				// Found a matching file that's not current module
				foundFileName = FindFileData.cFileName;
				FindClose(hFind);
				break;
			}
			else if (FindNextFileA(hFind, &FindFileData) == false) {
				// FindNextFileA failed
				break;
			}
		}
	}
	else if (hFind != INVALID_HANDLE_VALUE) {
		// found a matching file
		foundFileName = FindFileData.cFileName;
		FindClose(hFind);
	}

	return foundFileName;
}

bool SetPrivilegeA(const char * szPrivilege, bool bState)
{
	HANDLE hToken = nullptr;
	if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY | TOKEN_ADJUST_PRIVILEGES, &hToken))
	{
		dwError = GetLastError();
		return false;
	}

	TOKEN_PRIVILEGES TokenPrivileges = { 0 };
	TokenPrivileges.PrivilegeCount = 1;
	TokenPrivileges.Privileges[0].Attributes = bState ? SE_PRIVILEGE_ENABLED : 0;

	if (!LookupPrivilegeValueA(nullptr, szPrivilege, &TokenPrivileges.Privileges[0].Luid))
	{
		dwError = GetLastError();
		CloseHandle(hToken);
		return false;
	}

	if (!AdjustTokenPrivileges(hToken, FALSE, &TokenPrivileges, sizeof(TOKEN_PRIVILEGES), nullptr, nullptr))
	{
		dwError = GetLastError();
		CloseHandle(hToken);
		return false;
	}

	CloseHandle(hToken);

	return true;
}

DWORD FindProcessId(const std::wstring& processName)
{
	PROCESSENTRY32 processInfo;
	processInfo.dwSize = sizeof(processInfo);

	HANDLE processSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, NULL);
	if (processSnapshot == INVALID_HANDLE_VALUE) {
		return 0;
	}

	Process32First(processSnapshot, &processInfo);
	if (!processName.compare(processInfo.szExeFile))
	{
		CloseHandle(processSnapshot);
		return processInfo.th32ProcessID;
	}

	while (Process32Next(processSnapshot, &processInfo))
	{
		if (!processName.compare(processInfo.szExeFile))
		{
			CloseHandle(processSnapshot);
			return processInfo.th32ProcessID;
		}
	}

	CloseHandle(processSnapshot);
	return 0;
}

HANDLE StartRoutine(HANDLE hTargetProc, void * pRoutine, void * pArg, bool Hijack, bool Fastcall)
{
	if (!Hijack)
	{
		auto _NtCTE = reinterpret_cast<f_NtCreateThreadEx>(GetProcAddress(GetModuleHandleA("ntdll.dll"), "NtCreateThreadEx"));
		if (!_NtCTE)
		{
			HANDLE hThread = CreateRemoteThreadEx(hTargetProc, nullptr, 0, ReCa<LPTHREAD_START_ROUTINE>(pRoutine), pArg, 0, nullptr, nullptr);
			if (!hThread)
				LastError = GetLastError();

			return hThread;
		}

		HANDLE hThread = nullptr;
		_NtCTE(&hThread, THREAD_ALL_ACCESS, nullptr, hTargetProc, pRoutine, pArg, 0, 0, 0, 0, nullptr);
		if (!hThread)
			LastError = GetLastError();

		return hThread;
	}

	DWORD dwProcId = GetProcessId(hTargetProc);
	if (!dwProcId)
	{
		LastError = INJ_ERR_ADV_INV_PROC;
		return nullptr;
	}

	HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
	if (!hSnap)
	{
		LastError = GetLastError();
		return nullptr;
	}

	THREADENTRY32 TE32 = { 0 };
	TE32.dwSize = sizeof(THREADENTRY32);

	BOOL Ret = Thread32First(hSnap, &TE32);
	while (Ret)
	{
		if (TE32.th32OwnerProcessID == dwProcId && TE32.th32ThreadID != GetCurrentThreadId())
			break;
		Ret = Thread32Next(hSnap, &TE32);
	}
	CloseHandle(hSnap);

	if (!Ret)
	{
		LastError = INJ_ERR_ADV_NO_THREADS;
		return nullptr;
	}

	HANDLE hThread = OpenThread(THREAD_ALL_ACCESS, FALSE, TE32.th32ThreadID);
	if (!hThread)
	{
		LastError = INJ_ERR_ADV_CANT_OPEN_THREAD;
		return nullptr;
	}

	if (SuspendThread(hThread) == (DWORD)-1)
	{
		LastError = INJ_ERR_ADV_SUSPEND_FAIL;
		CloseHandle(hThread);
		return nullptr;
	}

	CONTEXT OldContext;
	OldContext.ContextFlags = CONTEXT_CONTROL;
	if (!GetThreadContext(hThread, &OldContext))
	{
		LastError = INJ_ERR_ADV_GET_CONTEXT_FAIL;
		ResumeThread(hThread);
		CloseHandle(hThread);
		return nullptr;
	}

	void * pCodecave = VirtualAllocEx(hTargetProc, nullptr, 0x100, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
	if (!pCodecave)
	{
		LastError = INJ_ERR_ADV_OUT_OF_MEMORY;
		ResumeThread(hThread);
		CloseHandle(hThread);
		return nullptr;
	}

#ifdef _WIN64

	Fastcall = true;

	BYTE Shellcode[] =
	{
		0x48, 0x83, 0xEC, 0x08,														// + 0x00			-> sub rsp, 08

		0xC7, 0x04, 0x24, 0x00, 0x00, 0x00, 0x00,									// + 0x04 (+ 0x07)	-> mov [rsp], RipLowPart
		0xC7, 0x44, 0x24, 0x04, 0x00, 0x00, 0x00, 0x00,								// + 0x0B (+ 0x0F)	-> mov [rsp + 04], RipHighPart		

		0x50, 0x51, 0x52, 0x53, 0x41, 0x50, 0x41, 0x51, 0x41, 0x52, 0x41, 0x53,		// + 0x13			-> push r(acdb)x r(8-11)

		0x48, 0xBB, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,					// + 0x1F (+ 0x21)	-> mov rbx, pFunc
		0x48, 0xB9, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,					// + 0x29 (+ 0x2B)	-> mov rcx, pArg

		0x48, 0x83, 0xEC, 0x20,														// + 0x33			-> sub rsp, 0x20
		0xFF, 0xD3,																	// + 0x37			-> call rbx
		0x48, 0x83, 0xC4, 0x20,														// + 0x39			-> add rsp, 0x20

		0x41, 0x5B, 0x41, 0x5A, 0x41, 0x59, 0x41, 0x58, 0x5B, 0x5A, 0x59, 0x58,		// + 0x3D			-> pop r(11-8) r(bdca)x

		0xC6, 0x05, 0xB0, 0xFF, 0xFF, 0xFF, 0x00,									// + 0x49			-> mov byte ptr[pCodecave - 0x49], 0

		0xC3																		// + 0x50			-> ret
	}; // SIZE = 0x51

	DWORD dwLoRIP = (DWORD)(OldContext.Rip & 0xFFFFFFFF);
	DWORD dwHiRIP = (DWORD)((OldContext.Rip >> 0x20) & 0xFFFFFFFF);

	*ReCa<DWORD*>(Shellcode + 0x07) = dwLoRIP;
	*ReCa<DWORD*>(Shellcode + 0x0F) = dwHiRIP;
	*ReCa<void**>(Shellcode + 0x21) = pRoutine;
	*ReCa<void**>(Shellcode + 0x2B) = pArg;

	OldContext.Rip = ReCa<DWORD64>(pCodecave);

#else

	BYTE Shellcode[] =
	{
		0x60,										// + 0x00			-> pushad
		0xE8, 0x00, 0x00, 0x00, 0x00,				// + 0x01			-> call pCodecave + 6

		0x58,										// + 0x06			-> pop eax

		0xB9, 0x00, 0x00, 0x00, 0x00,				// + 0x07 (+ 0x08)	-> mov ecx, pArg
		0xB8, 0x00, 0x00, 0x00, 0x00,				// + 0x0C (+ 0x0D)	-> mov eax, pFunc
		0x90,										// + 0x11			-> __fastcall(default): nop
													//					-> __stdcall(assumed):  push ecx
													0xFF, 0xD0,									// + 0x12			-> call eax

													0x61,										// + 0x14			-> popad

													0x68, 0x00, 0x00, 0x00, 0x00,				// + 0x15 (+ 0x16)	-> push eip

													0xC6, 0x05, 0x00, 0x00, 0x00, 0x00,	0x00,	// + 0x1A (+ 0x1C)	-> mov byte ptr[pCodecave], 0

													0xC3,										// + 0x21			-> ret
	}; // SIZE = 0x22

	if (!Fastcall)
		Shellcode[0x11] = 0x51;

	*ReCa<void**>(Shellcode + 0x08) = pArg;
	*ReCa<void**>(Shellcode + 0x0D) = pRoutine;
	*ReCa<DWORD*>(Shellcode + 0x16) = OldContext.Eip;
	*ReCa<void**>(Shellcode + 0x1C) = pCodecave;

	OldContext.Eip = ReCa<DWORD>(pCodecave);

#endif

	if (!WriteProcessMemory(hTargetProc, pCodecave, Shellcode, sizeof(Shellcode), nullptr))
	{
		LastError = INJ_ERR_ADV_WPM_FAIL;
		VirtualFreeEx(hTargetProc, pCodecave, MEM_RELEASE, 0);
		ResumeThread(hThread);
		CloseHandle(hThread);
		return nullptr;
	}

	if (!SetThreadContext(hThread, &OldContext))
	{
		LastError = INJ_ERR_ADV_SET_CONTEXT_FAIL;
		VirtualFreeEx(hTargetProc, pCodecave, MEM_RELEASE, 0);
		ResumeThread(hThread);
		CloseHandle(hThread);
		return nullptr;
	}

	if (ResumeThread(hThread) == (DWORD)-1)
	{
		LastError = INJ_ERR_ADV_RESUME_FAIL;
		VirtualFreeEx(hTargetProc, pCodecave, MEM_RELEASE, 0);
		CloseHandle(hThread);
		return nullptr;
	}

	BYTE CheckByte = 1;
	while (CheckByte)
		ReadProcessMemory(hTargetProc, pCodecave, &CheckByte, 1, nullptr);

	CloseHandle(hThread);
	VirtualFreeEx(hTargetProc, pCodecave, MEM_RELEASE, 0);

	return (HANDLE)1;
}

void ErrorMsg(DWORD Err1, DWORD Err2)
{
	char szRet[9]{ 0 };
	char szAdv[9]{ 0 };
	_ultoa_s(Err1, szRet, 0x10);
	_ultoa_s(Err2, szAdv, 0x10);
	std::string Msg = "Error code: 0x";
	Msg += szRet;
	Msg += "\nAdvanced info: 0x";
	Msg += szAdv;

	MessageBoxA(0, Msg.c_str(), "Injection failed", MB_ICONERROR);
}