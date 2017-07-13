#include "stdafx.h"
#include "FixBlackScreen.h"

#ifdef UNICODE
#undef Module32First
#undef Module32Next
#undef MODULEENTRY32
#endif

typedef _Return_type_success_(return >= 0) LONG NTSTATUS;

typedef NTSTATUS(__stdcall * f_NtCreateThreadEx)(HANDLE * pHandle, ACCESS_MASK DesiredAccess,
	void * pAttr, HANDLE hProc, void * pFunc, void * pArg, ULONG  Flags, SIZE_T ZeroBits,
	SIZE_T StackSize, SIZE_T MaxStackSize, void * pAttrListOut);

typedef BOOL(__stdcall * f_SetWindowDisplayAffinity)(HWND hWnd, DWORD dwAffinity);

struct SET_WDA_DATA
{
	f_SetWindowDisplayAffinity	pSetWindowsDisplayAffinity;
	DWORD                       dwAffinity;
	HWND                        hWnd;
};

struct EnumWindowsCallbackArgs {
	EnumWindowsCallbackArgs(DWORD p) : pid(p) { }
	const DWORD pid;
	std::vector<HWND> handles;
};

void __stdcall		SetWdaShell(SET_WDA_DATA * pData);
std::vector<HWND>	GetHwnds(DWORD pid);
bool				SetPrivilegeA(const char * szPrivilege, bool bState);
DWORD				FindProcessId(const std::wstring& processName);
HANDLE				StartRoutine(HANDLE hTargetProc, void * pRoutine, void * pArg, bool Hijack, bool Fastcall);
void				ErrorMsg(DWORD Err1, DWORD Err2);
DWORD LastError =	ERR_SUCCESS;
DWORD dwError =		ERROR_SUCCESS;

/**
 * Enable screen capture on a window protected by SetWindowsDisplayAffinity(WDA_MONITOR)
 * 
* @param tProcName :    target process name (Overwatch.exe)
* @param HijackThread : true to hijack thread or false to create one
*/
DWORD FixBlackScreen(const std::wstring& tProcName, bool HijackThread)
{
	DWORD PID = FindProcessId(tProcName);
	if (!PID)
	{
		MessageBoxA(nullptr, "Target process not found", "ERROR", MB_ICONERROR);
		return 0;
	}

	if (!SetPrivilegeA("SeDebugPrivilege", true))
	{
		ErrorMsg(ERR_SET_PRIV_FAIL, dwError);
		return 0;
	}

	HANDLE hProc = OpenProcess(PROCESS_ALL_ACCESS, FALSE, PID);
	if (!hProc)
	{
		return ERR_INVALID_PROC_HANDLE;
	}

	SET_WDA_DATA data;
	std::vector<HWND> hwnds = GetHwnds(PID);
	if (hwnds.size() > 0)
	{
		data.hWnd = hwnds[0];
		data.dwAffinity = WDA_NONE;
	}
	else
	{
		ErrorMsg(ERR_CANT_FIND_HWND, GetLastError());
		return 0;
	}

	HINSTANCE hU32DLL = GetModuleHandleA("User32.dll");
	if (!hU32DLL)
	{
		LastError = GetLastError();
		return ERR_USER32DLL_MISSING;
	}

	FARPROC pFunc = GetProcAddress(hU32DLL, "SetWindowDisplayAffinity");
	if (!pFunc)
	{
		LastError = GetLastError();
		return INJ_ERR_SETWDA_MISSING;
	}

	data.pSetWindowsDisplayAffinity = ReCa<f_SetWindowDisplayAffinity>(pFunc);

	BYTE* pArg = ReCa<BYTE*>(VirtualAllocEx(hProc, nullptr, sizeof(SET_WDA_DATA) + 0x200,
	                                        MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
	if (!pArg)
	{
		LastError = GetLastError();
		return ERR_CANT_ALLOC_MEM;
	}

	if (!WriteProcessMemory(hProc, pArg, &data, sizeof(SET_WDA_DATA), nullptr))
	{
		LastError = GetLastError();
		VirtualFreeEx(hProc, pArg, MEM_RELEASE, 0);
		return ERR_WPM_FAIL;
	}

	if (!WriteProcessMemory(hProc, pArg + sizeof(SET_WDA_DATA), SetWdaShell, 0x100, nullptr))
	{
		LastError = GetLastError();
		VirtualFreeEx(hProc, pArg, MEM_RELEASE, 0);
		return ERR_WPM_FAIL;
	}

	HANDLE hThread = StartRoutine(hProc, pArg + sizeof(SET_WDA_DATA), pArg, HijackThread, false);
	if (!hThread)
	{
		VirtualFreeEx(hProc, pArg, 0, MEM_RELEASE);
		return ERR_CANT_CREATE_THREAD;
	}
	if (!HijackThread)
	{
		WaitForSingleObject(hThread, INFINITE);
		CloseHandle(hThread);
	}
	VirtualFreeEx(hProc, pArg, 0, MEM_RELEASE);

	return ERR_SUCCESS;
}

void __stdcall SetWdaShell(SET_WDA_DATA * pData)
{
	pData->pSetWindowsDisplayAffinity(pData->hWnd, pData->dwAffinity);
}

static BOOL CALLBACK EnumWindowsCallback(HWND hnd, LPARAM lParam)
{
	EnumWindowsCallbackArgs* args = ReCa<EnumWindowsCallbackArgs *>(lParam);
	DWORD windowPID;
	(void)::GetWindowThreadProcessId(hnd, &windowPID);
	if (windowPID == args->pid)
	{
		args->handles.push_back(hnd);
	}

	return TRUE;
}

std::vector<HWND> GetHwnds(DWORD pid)
{
	EnumWindowsCallbackArgs args(pid);
	if (::EnumWindows(&EnumWindowsCallback, ReCa<LPARAM>(&args)) == FALSE)
	{
		return std::vector<HWND>();
	}

	return args.handles;
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
		auto _NtCTE = ReCa<f_NtCreateThreadEx>(GetProcAddress(GetModuleHandleA("ntdll.dll"), "NtCreateThreadEx"));
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
		LastError = ERR_ADV_INV_PROC;
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
		LastError = ERR_ADV_NO_THREADS;
		return nullptr;
	}

	HANDLE hThread = OpenThread(THREAD_ALL_ACCESS, FALSE, TE32.th32ThreadID);
	if (!hThread)
	{
		LastError = ERR_ADV_CANT_OPEN_THREAD;
		return nullptr;
	}

	if (SuspendThread(hThread) == static_cast<DWORD>(-1))
	{
		LastError = ERR_ADV_SUSPEND_FAIL;
		CloseHandle(hThread);
		return nullptr;
	}

	CONTEXT OldContext;
	OldContext.ContextFlags = CONTEXT_CONTROL;
	if (!GetThreadContext(hThread, &OldContext))
	{
		LastError = ERR_ADV_GET_CONTEXT_FAIL;
		ResumeThread(hThread);
		CloseHandle(hThread);
		return nullptr;
	}

	void * pCodecave = VirtualAllocEx(hTargetProc, nullptr, 0x100, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
	if (!pCodecave)
	{
		LastError = ERR_ADV_OUT_OF_MEMORY;
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
		LastError = ERR_ADV_WPM_FAIL;
		VirtualFreeEx(hTargetProc, pCodecave, MEM_RELEASE, 0);
		ResumeThread(hThread);
		CloseHandle(hThread);
		return nullptr;
	}

	if (!SetThreadContext(hThread, &OldContext))
	{
		LastError = ERR_ADV_SET_CONTEXT_FAIL;
		VirtualFreeEx(hTargetProc, pCodecave, MEM_RELEASE, 0);
		ResumeThread(hThread);
		CloseHandle(hThread);
		return nullptr;
	}

	if (ResumeThread(hThread) == static_cast<DWORD>(-1))
	{
		LastError = ERR_ADV_RESUME_FAIL;
		VirtualFreeEx(hTargetProc, pCodecave, MEM_RELEASE, 0);
		CloseHandle(hThread);
		return nullptr;
	}

	BYTE CheckByte = 1;
	while (CheckByte)
		ReadProcessMemory(hTargetProc, pCodecave, &CheckByte, 1, nullptr);

	CloseHandle(hThread);
	VirtualFreeEx(hTargetProc, pCodecave, MEM_RELEASE, 0);

	return ReCa<HANDLE>(1);
}

void ErrorMsg(DWORD Err1, DWORD Err2)
{
	char szRet[9]{0};
	char szAdv[9]{0};
	_ultoa_s(Err1, szRet, 0x10);
	_ultoa_s(Err2, szAdv, 0x10);
	std::string Msg = "Error code: 0x";
	Msg += szRet;
	Msg += "\nAdvanced info: 0x";
	Msg += szAdv;

	MessageBoxA(nullptr, Msg.c_str(), "Failed to fix", MB_ICONERROR);
}