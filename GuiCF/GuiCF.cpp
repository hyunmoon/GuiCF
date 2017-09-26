#include "stdafx.h"
#include "SimpleIni.h"
#include "ImageHelper.h"
#include "FixBlackScreen.h"
#include "Psapi.h"
#include "Shellapi.h"
#include "Mmsystem.h"
#pragma comment(lib, "Psapi.lib")
#pragma comment(lib, "Shell32.lib")
#pragma comment(lib, "Winmm.lib")
#pragma warning(disable : 4800)

#define MAX_LOADSTRING   100
#define MONITOR_AREA     0
#define MONITOR_CENTER   1
#define MONITOR_WORKAREA 2
#define WIN_WIDTH        216
#define WIN_HEIGHT       180
#define IDC_CHANGE       100
#define IDC_TEST         101
#define IDC_FIX          102
#define IDC_CHECK        103

struct SettingsKeys {
	UINT s76pause;
	UINT s76pausekey;
	UINT ultkey;
	UINT flagged;
	UINT delay;
	UINT hidecf;
	UINT alwaysontop;
	UINT opacity;
	UINT playsound;
	INT xpos;
	INT ypos;
} Settings;

// Global Variables:
HINSTANCE hInst;                                   // current instance
WCHAR szTitle[MAX_LOADSTRING];                     // The title bar text
WCHAR szWindowClass[MAX_LOADSTRING];               // the main window class nam

std::string         WORKING_DIR;                   // CfGui current directory
std::string         CF_FILEPATH;                   // CF file path

BOOL                IsCfActive;                    // CfBot unpaused or paused
BOOL                Is76PauseActive;               // S76 pause related
INPUT               CfPauseKeyInput;               // Cf pause key info from ini
std::string         CfCurrIni;                     // Cf currently loaded ini file
std::string         CfCurrSpeed;                   // Cf currently loaded speed value
std::string         CfCurrPull;                    // Cf currently loaded pull value
CSimpleIniA         hIni;                          // Ini handler

UINT                hBarHeight;                    // main window title bar height
UINT                hIndex = 1;                    // mode switch index. start normal.
UINT                hSavedOpacity;                 // opacity saved
const INT           NUM_STATE = 3;                 // gui height change on right click
const INT           WIN_HEIGHTS[NUM_STATE] = { 140, 180, 220 };
const COLORREF      COLOR_MAIN  = RGB(43, 43, 43);
const std::wstring  TARGET_PROC = L"Overwatch.exe";

BOOL   EXIT_REQUESTED;                             // Signal threads to exit
HANDLE PipeMonitorThread  = nullptr;               // Thread for ReadFromPipeProc
HANDLE KeyMonitorThread   = nullptr;               // Thread for PauseOnUltProc
HANDLE g_hChildStd_OUT_Rd = nullptr;               // CF exe stdout read
HANDLE g_hChildStd_OUT_Wr = nullptr;               // CF exe stdout write

HWND     mHwnd;         // Main window
HWND     hWndDlg;       // Dialog window
HWND     S76Check;      // Checkbox button
HWND     S76Txt;        // S76 PS static
HWND     SpeedPull;     // Temporary fix
HWND     SpeedValue;    // Speed static
HWND     IniValue;      // Ini static
HWND     PullValue;     // Pull static
HWND     ImgBox;        // Image static
HBITMAP  hImageOn;      // Bot Unpaused image
HBITMAP  hImageOff;     // Bot Paused image

// Forward declarations of functions included in this code module:
ATOM                MyRegisterClass(HINSTANCE hInstance);
BOOL                InitInstance(HINSTANCE, int);
LRESULT CALLBACK    WndProc(HWND, UINT, WPARAM, LPARAM);
LRESULT CALLBACK    DlgProc(HWND, UINT, WPARAM, LPARAM);
DWORD WINAPI        PauseOnUltProc(LPVOID lpParameter);
DWORD WINAPI        CFHandleProc(LPVOID lpParameter);
PROCESS_INFORMATION CreateChildProcess(std::string filePath);
std::string         ReadFromPipe();
void                ClipOrCenterWindowToMonitor(HWND hWnd, UINT flags);
void                SwitchGuiMode(HWND hWnd);
std::string         FindFile(const std::string fileFullPath, BOOL excludeSelf);

// WinMain
// -----------------------------------------------------
int APIENTRY wWinMain(_In_ HINSTANCE hInstance,
	_In_opt_ HINSTANCE hPrevInstance,
	_In_ LPWSTR    lpCmdLine,
	_In_ int       nCmdShow)
{
	UNREFERENCED_PARAMETER(hPrevInstance);
	UNREFERENCED_PARAMETER(lpCmdLine);
	// LoadStringW(hInstance, IDS_APP_TITLE, szTitle, MAX_LOADSTRING);
	LoadStringW(hInstance, IDC_GUICF, szWindowClass, MAX_LOADSTRING);
	MyRegisterClass(hInstance);
	// Init & load settings.ini
	if (!InitInstance(hInstance, nCmdShow)) {
		return FALSE;
	}
	HACCEL hAccelTable = LoadAccelerators(hInstance, MAKEINTRESOURCE(IDC_GUICF));

	// Run CF exe
	PipeMonitorThread = CreateThread(nullptr, 0, CFHandleProc, nullptr, 0, nullptr);
	IsCfActive = true;

	// Start monitoring ult key
	KeyMonitorThread = CreateThread(nullptr, 0, PauseOnUltProc, nullptr, 0, nullptr);

	// Main message loop:
	MSG msg;
	while (GetMessage(&msg, nullptr, 0, 0)) 
	{
		if (!TranslateAccelerator(msg.hwnd, hAccelTable, &msg)) 
		{
			if (IsDialogMessage(msg.hwnd, &msg) == 0) 
			{
				TranslateMessage(&msg);
				DispatchMessage(&msg);
			}
		}
	}

	// Clean up before exit
	DWORD dwRet = WaitForSingleObject(PipeMonitorThread, WAIT_TIMEOUT);
	if (dwRet != WAIT_OBJECT_0) 
	{
		TerminateThread(PipeMonitorThread, 0);
	}

	dwRet = WaitForSingleObject(KeyMonitorThread, WAIT_TIMEOUT);
	if (dwRet != WAIT_OBJECT_0) 
	{
		TerminateThread(KeyMonitorThread, 0);
	}
	return static_cast<int>(msg.wParam);
}

ATOM MyRegisterClass(HINSTANCE hInstance)
{
	WNDCLASSEXW wcex;

	wcex.cbSize = sizeof(WNDCLASSEX);
	wcex.style = CS_HREDRAW | CS_VREDRAW;
	wcex.lpfnWndProc = WndProc;
	wcex.cbClsExtra = 0;
	wcex.cbWndExtra = 0;
	wcex.hInstance = hInstance;
	wcex.hIcon = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_GUICF));
	wcex.hCursor = LoadCursor(nullptr, IDC_ARROW);
	wcex.hbrBackground = static_cast<HBRUSH>(CreateSolidBrush(COLOR_MAIN));
	wcex.lpszMenuName = nullptr;
	wcex.lpszClassName = szWindowClass;
	wcex.hIconSm = LoadIcon(wcex.hInstance, MAKEINTRESOURCE(IDI_SMALL));

	return RegisterClassExW(&wcex);
}

BOOL InitInstance(HINSTANCE hInstance, int nCmdShow)
{
	hInst = hInstance;

	// Init stuffs --------------------------------------------------------------------------------
	// Get Working Dir
	char buffer[MAX_PATH];
	GetModuleFileNameA(nullptr, buffer, MAX_PATH);
	const auto path = std::string(buffer);
	size_t last = path.find_last_of("\\/");
	WORKING_DIR = path.substr(0, last);
	std::string moduleFileName = path.substr(last + 1, path.length());

	// Check configs\\F1.ini exists
	if (FindFile((WORKING_DIR + "\\configs\\F1.ini").c_str(), FALSE).empty()) 
	{
		MessageBoxA(nullptr, "configs\\F1.ini not found", "ERROR", MB_ICONERROR);
		return FALSE;
	}

	// Find CF exe (the only other exe in same dir)
	std::string cfFileName = FindFile((WORKING_DIR + "\\*.exe").c_str(), TRUE);
	if (cfFileName.empty()) 
	{
		MessageBoxA(nullptr, "CF exe not found in current directory", "ERROR", MB_ICONERROR);
		return FALSE;
	}
	CF_FILEPATH = WORKING_DIR + "\\" + cfFileName;

	// Load resource
	::CoInitializeEx(nullptr, COINIT_MULTITHREADED);
	hImageOn = LoadSplashImage(IDB_PNG1);
	hImageOff = LoadSplashImage(IDB_PNG2);

	// Read from settings.ini
	hIni.SetUnicode();
	SI_Error rc = hIni.LoadFile("settings.ini");
	if (rc < 0) 
	{
		hIni.SetValue("", "s76pause", "0");
		hIni.SetValue("", "s76pausekey", "55");
		hIni.SetValue("", "ultkey", "81");
		hIni.SetValue("", "flagged", "0");
		hIni.SetValue("", "hidecf", "0");
		hIni.SetValue("", "alwaysontop", "1");
		hIni.SetValue("", "opacity", "75");
		hIni.SetValue("", "playsound", "1");
		hIni.SetValue("", "xpos", "0");
		hIni.SetValue("", "ypos", "0");
		if (hIni.SaveFile("settings.ini") < 0)
		{
			MessageBoxA(nullptr, "Failed to create settings.ini.\nCheck your anti-virus program.",
				"ERROR", MB_ICONERROR);
		}
	}
	auto strtoi = [](const char * str, UINT defValue) 
	{
		std::stringstream ss(str);
		UINT result;
		return ss >> result ? result : defValue;
	};
	Settings.s76pause =    strtoi(hIni.GetValue("", "s76pause", "0"), 0);
	Settings.s76pausekey = strtoi(hIni.GetValue("", "s76pausekey", "55"), 0);
	Settings.ultkey =      strtoi(hIni.GetValue("", "ultkey", "81"), 81);
	Settings.flagged =     strtoi(hIni.GetValue("", "flagged", "0"), 0);
	Settings.delay =       strtoi(hIni.GetValue("", "delay", "0"), 0);
	Settings.hidecf =      strtoi(hIni.GetValue("", "hidecf", "0"), 0);
	Settings.alwaysontop = strtoi(hIni.GetValue("", "alwaysontop", "1"), 1);
	Settings.opacity =     strtoi(hIni.GetValue("", "opacity", "75"), 70);
	Settings.playsound =   strtoi(hIni.GetValue("", "playsound", "1"), 1);
	Settings.xpos =        strtoi(hIni.GetValue("", "xpos", "0"), 0);
	Settings.ypos =        strtoi(hIni.GetValue("", "ypos", "0"), 0);

	// Create main window -------------------------------------------------------------------------
	DWORD dwExStyle = WS_EX_LAYERED;
	if (Settings.alwaysontop) 
	{
		dwExStyle |= WS_EX_TOPMOST; // AlwaysOntop
	}
	// Use last closed position if appropriate
	int xPos = Settings.xpos == 0 ? CW_USEDEFAULT : Settings.xpos;
	int yPos = Settings.ypos == 0 ? CW_USEDEFAULT : Settings.ypos;

	// Sync window title with module file name
	std::fill_n(szTitle, MAX_LOADSTRING, 0);
	mbstowcs_s(nullptr, szTitle, moduleFileName.c_str(), MAX_LOADSTRING);

	HWND hWnd = CreateWindowEx(dwExStyle, szWindowClass, szTitle,
		WS_OVERLAPPEDWINDOW&~WS_MAXIMIZEBOX&~WS_THICKFRAME,
		xPos, yPos,
		WIN_WIDTH, WIN_HEIGHTS[hIndex],
		nullptr, nullptr, hInstance, nullptr);

	// temporary fix


	if (!hWnd) 
	{
		return FALSE;
	}
	SetLayeredWindowAttributes(hWnd, 0, (255 * 90) / 100, LWA_ALPHA);

	// Make sure the window appears in visible monitor area
	ClipOrCenterWindowToMonitor(hWnd, MONITOR_AREA);

	ShowWindow(hWnd, nCmdShow);
	UpdateWindow(hWnd);

	RECT rcClient, rcWind;
	GetClientRect(hWnd, &rcClient);
	GetWindowRect(hWnd, &rcWind);
	hBarHeight = rcWind.bottom - rcWind.top - rcClient.bottom; // height of title bar
	mHwnd = hWnd;

	return TRUE;
}

LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
	switch (message)
	{
	case WM_CREATE:
	{
		// Create fonts
		LOGFONT fontAttributes = { 0 };
		::GetObject(ReCa<HFONT>(SendMessage(hWnd, WM_GETFONT, 0, 0)),
			sizeof fontAttributes, &fontAttributes);
		fontAttributes.lfHeight = 15;
		HFONT hFont_15 = ::CreateFontIndirect(&fontAttributes);
		fontAttributes.lfHeight = 17;
		HFONT hFont_17 = ::CreateFontIndirect(&fontAttributes);

		ImgBox = CreateWindow(L"STATIC", L"", WS_VISIBLE | WS_CHILD | SS_BITMAP,
			0, 2, 96, 96, hWnd, NULL, hInst, NULL);

		S76Check = CreateWindowEx(NULL, L"BUTTON", L"", WS_CHILD | WS_VISIBLE | BS_CHECKBOX,
			118, 70, 20, 22, hWnd, HMENU(IDC_CHECK), hInst, nullptr);

		HWND S76Txt = CreateWindow(L"STATIC", L"S76", WS_VISIBLE | WS_CHILD | SS_NOTIFY,
			138, 71, 60, 22, hWnd, HMENU(IDC_CHECK), hInst, NULL);

		HWND FixBtn = CreateWindow(L"BUTTON", L"FIX", WS_VISIBLE | WS_CHILD,
			7, 142, 75, 32, hWnd, HMENU(IDC_FIX), hInst, NULL);

		IniValue = CreateWindow(L"STATIC", L"...", WS_VISIBLE | WS_CHILD,
			118, 18, 75, 20, hWnd, NULL, hInst, NULL);

		SpeedPull = CreateWindow(L"STATIC", L"...", WS_VISIBLE | WS_CHILD,
			118, 44, 75, 15, hWnd, NULL, hInst, NULL);

		HWND ChangeBtn = CreateWindow(L"BUTTON", L"CHANGE", WS_VISIBLE | WS_CHILD,
			118, 103, 75, 32, hWnd, HMENU(IDC_CHANGE), hInst, NULL);

		HWND TestBtn = CreateWindow(L"BUTTON", L"TEST", WS_VISIBLE | WS_CHILD,
			118, 142, 75, 32, hWnd, HMENU(IDC_TEST), hInst, NULL);

		SendMessage(ChangeBtn,  WM_SETFONT, LPARAM(GetStockObject(DEFAULT_GUI_FONT)), TRUE);
		SendMessage(TestBtn,    WM_SETFONT, LPARAM(GetStockObject(DEFAULT_GUI_FONT)), TRUE);
		SendMessage(FixBtn,     WM_SETFONT, LPARAM(GetStockObject(DEFAULT_GUI_FONT)), TRUE);
		SendMessage(S76Check,   WM_SETFONT, WPARAM(hFont_17), TRUE);
		SendMessage(S76Txt,     WM_SETFONT, WPARAM(hFont_17), TRUE);
		SendMessage(SpeedPull,  WM_SETFONT, WPARAM(hFont_17), TRUE);
		SendMessage(IniValue, WM_SETFONT, WPARAM(hFont_17), TRUE);
		SendMessage(ImgBox, STM_SETIMAGE, IMAGE_BITMAP, LPARAM(hImageOff));

		if (Settings.s76pause) 
		{
			CheckDlgButton(hWnd, IDC_CHECK, BST_CHECKED);
			Is76PauseActive = TRUE;
		}
		else 
		{
			CheckDlgButton(hWnd, IDC_CHECK, BST_UNCHECKED);
			Is76PauseActive = FALSE;
		}
	}
	break;
	case WM_COMMAND:
	{
		int wmId = LOWORD(wParam);
		RECT r;
		// Parse the menu selections:
		switch (wmId)
		{
		case IDC_CHANGE:
		{
			PROCESS_INFORMATION NpProcInfo;
			STARTUPINFOA NpStartupInfo;
			// open F*.ini in notepad, move & resize window for convenience
			std::string configsIni = " configs\\" + CfCurrIni;
			memset(&NpStartupInfo, 0, sizeof(NpStartupInfo));
			memset(&NpProcInfo, 0, sizeof(NpProcInfo));
			NpStartupInfo.cb = sizeof(NpStartupInfo);
			NpStartupInfo.wShowWindow = SW_HIDE;
			NpStartupInfo.dwFlags = STARTF_USESHOWWINDOW;
			if (CreateProcessA("C:\\Windows\\notepad.exe",
				_strdup(configsIni.c_str()),
				nullptr,
				nullptr,
				FALSE,
				CREATE_DEFAULT_ERROR_MODE,
				nullptr,
				WORKING_DIR.c_str(),
				&NpStartupInfo,
				&NpProcInfo) != FALSE) {
				GetWindowRect(mHwnd, &r);
				Sleep(10);
				std::vector<HWND> hwnds = GetHwnds(NpProcInfo.dwProcessId);
				if (hwnds.size() > 0) {
					HWND npHwnd = hwnds[0];
					MoveWindow(npHwnd, 
						r.left, 
						r.top + WIN_HEIGHTS[hIndex], 
						r.right - r.left, 
						WIN_HEIGHTS[hIndex] * 2,
						FALSE);
					ShowWindow(npHwnd, SW_SHOW);
				}
			}
		}
		break;
		case IDC_TEST:
		{
			if (!IsWindow(hWndDlg)) 
			{
				hWndDlg = CreateDialog(hInst, MAKEINTRESOURCE(IDD_DIALOG1), hWnd, 
					ReCa<DLGPROC>(DlgProc));
				ShowWindow(hWndDlg, SW_SHOW);
			}
		}
		break;
		case IDC_FIX:
		{
			// hijack thread & inject SetWindowDisplayAffinity call
			FixBlackScreen(TARGET_PROC, true);
			if (Settings.playsound) MessageBeep(MB_OK);
		}
		break;
		case IDC_CHECK:
		{
			if (IsDlgButtonChecked(hWnd, IDC_CHECK)) 
			{
				CheckDlgButton(hWnd, IDC_CHECK, BST_UNCHECKED);
				Is76PauseActive = FALSE;
				hIni.SetValue("", "s76pause", "0");
			}
			else 
			{
				// reset key state readings (clean slate)
				GetAsyncKeyState(Settings.ultkey);
				GetAsyncKeyState(Settings.s76pausekey);
				CheckDlgButton(hWnd, IDC_CHECK, BST_CHECKED);
				Is76PauseActive = TRUE;
				hIni.SetValue("", "s76pause", "1");
			}
		}
		break;
		default:
			return DefWindowProc(hWnd, message, wParam, lParam);
		}
	}
	break;
	case WM_PAINT:
	{
		PAINTSTRUCT ps;
		BeginPaint(hWnd, &ps);
		EndPaint(hWnd, &ps);
	}
	break;
	case WM_CTLCOLORSTATIC:
	{
		HDC hdc = ReCa<HDC>(wParam);
		SetTextColor(hdc, RGB(255, 0, 0));
		SetBkColor(hdc, COLOR_MAIN);
		SetDCBrushColor(hdc, COLOR_MAIN);
		return ReCa<LRESULT>(GetStockObject(DC_BRUSH));
	}
	case WM_LBUTTONDOWN:
	{
		// enable dragging window from client area
		// avoid using WM_NCHITTEST as it blocks WM_RBUTTONDOWN
		ReleaseCapture();
		SendMessage(hWnd, WM_SYSCOMMAND, 0xf012, 0);
	}
	break;
	case WM_RBUTTONDOWN:
	{
		SwitchGuiMode(hWnd);
	}
	break;
	case WM_MOUSEWHEEL:
	{
		// up or down opacity by 5 within 40~100 % range.
		auto pn = [](short delta) { return (delta > 0) ? 5 : -5; };
		int op = Settings.opacity + pn(GET_WHEEL_DELTA_WPARAM(wParam));
		if (op >= 40 && op <= 100) 
		{
			Settings.opacity = op;
			SetLayeredWindowAttributes(hWnd, 0, 
				255 * static_cast<int>(Settings.opacity) / 100, LWA_ALPHA);
		}
	}
		break;
	case WM_DESTROY:
		EXIT_REQUESTED = TRUE;
		CancelSynchronousIo(PipeMonitorThread);
		PostQuitMessage(0);
		break;
	case WM_CLOSE:
		// Save window x,y pos in settings.ini before closing
		RECT rect;
		GetWindowRect(hWnd, &rect);
		hIni.SetValue("", "opacity", std::to_string(Settings.opacity).c_str());
		hIni.SetValue("", "xpos", std::to_string(rect.left).c_str());
		hIni.SetValue("", "ypos", std::to_string(rect.top).c_str());
		if (hIni.SaveFile("settings.ini") < 0)
		{
			MessageBoxA(nullptr, "Failed to update settings.ini.\nCheck your anti-virus program.", 
				"ERROR", MB_ICONERROR);
		}
		DestroyWindow(hWnd);
		break;
	default:
		return DefWindowProc(hWnd, message, wParam, lParam);
	}
	return 0;
}

LRESULT CALLBACK DlgProc(HWND hWndDlg, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	HBITMAP hBitmap;
	RECT dRect;
	static HANDLE hCopied = nullptr;

	switch (uMsg)
	{
	case WM_INITDIALOG:
	{
		GetWindowRect(GetDlgItem(hWndDlg, IDC_STATIC), &dRect);
		GetFullScreenShot(&hBitmap, dRect.right - dRect.left, dRect.bottom - dRect.top);
		hCopied = ReCa<HANDLE>(SendDlgItemMessage(hWndDlg,
			IDC_STATIC, STM_SETIMAGE, IMAGE_BITMAP, LPARAM(hBitmap)));
		return TRUE;
	}
	case WM_LBUTTONDOWN:
	{
		EndDialog(hWndDlg, 0);
		DestroyWindow(hWndDlg);
		return TRUE;
	}
	case WM_DESTROY:
		// clean up
		hCopied = ReCa<HANDLE>(SendDlgItemMessage(hWndDlg,
			IDC_STATIC, STM_SETIMAGE, IMAGE_BITMAP, LPARAM(hCopied)));
		DeleteObject(hCopied);
		return true;
	case WM_CLOSE:
		EndDialog(hWndDlg, 0);
		DestroyWindow(hWndDlg);
		return TRUE;
	default: ;
	}
	return FALSE;
}

// -------------------------------------------------------------------------

DWORD WINAPI PauseOnUltProc(LPVOID lpParameter) 
{
	auto toggle_pause = []() 
	{
		// virtual keyboard input does not trigger flag
		CfPauseKeyInput.ki.dwFlags = 0;
		SendInput(1, &CfPauseKeyInput, sizeof(INPUT));
		Sleep(30);
		CfPauseKeyInput.ki.dwFlags = KEYEVENTF_KEYUP;
		SendInput(1, &CfPauseKeyInput, sizeof(INPUT));
	};

	auto toggle_image = [](HBITMAP * bitmap) 
	{
		HANDLE CopiedBmp = ReCa<HANDLE>(
			SendMessage(ImgBox, STM_SETIMAGE, IMAGE_BITMAP, LPARAM(*bitmap)));
		DeleteObject(CopiedBmp);
	};

	while (!EXIT_REQUESTED) 
	{
		if (IsCfActive && Is76PauseActive && GetAsyncKeyState(Settings.ultkey)) 
		{
			// pause, wait about 8 secs, unpause
			toggle_pause();
			for (int i = 0; i < 8; i++) 
			{
				Sleep(500);
				// abort if bot was manually unpaused or ult key was pressed again
				if (IsCfActive || GetAsyncKeyState(Settings.ultkey))
					break;
				toggle_image(&hImageOff);
				Sleep(500);
				// check every 0.5 sec
				if (IsCfActive || GetAsyncKeyState(Settings.ultkey))
					break;
				toggle_image(&hImageOn);
			}
			if (IsCfActive) 
			{
				toggle_image(&hImageOn);
			}
			else 
			{
				toggle_image(&hImageOff);
				toggle_pause();
			}
		}
		else if (IsCfActive && GetAsyncKeyState(Settings.s76pausekey)) {
			if (Is76PauseActive) {
				CheckDlgButton(mHwnd, IDC_CHECK, BST_UNCHECKED);
				Is76PauseActive = FALSE;
				hIni.SetValue("", "s76pause", "0");
			}
			else {
				GetAsyncKeyState(Settings.ultkey);
				GetAsyncKeyState(Settings.s76pausekey);
				CheckDlgButton(mHwnd, IDC_CHECK, BST_CHECKED);
				Is76PauseActive = TRUE;
				hIni.SetValue("", "s76pause", "1");
			}
			MessageBeep(MB_OK);
		}
		Sleep(150);
	}
	// Main thread requested exit
	return 0;
}

DWORD WINAPI CFHandleProc(LPVOID lpParameter) 
{
	// Create pipe
	SECURITY_ATTRIBUTES sa;
	// Set the bInheritHandle flag so pipe handles are inherited. 
	sa.nLength = sizeof(SECURITY_ATTRIBUTES);
	sa.bInheritHandle = TRUE;
	sa.lpSecurityDescriptor = nullptr;

	// Create a pipe for the child process's STDOUT. 
	if (!CreatePipe(&g_hChildStd_OUT_Rd, &g_hChildStd_OUT_Wr, &sa, 0)) 
	{
		MessageBoxA(nullptr, "CreatePipe", "ERROR", MB_ICONERROR);
		PostMessage(mHwnd, WM_CLOSE, NULL, NULL);
	}
	// Ensure the read handle to the pipe for STDOUT is not inherited
	if (!SetHandleInformation(g_hChildStd_OUT_Rd, HANDLE_FLAG_INHERIT, 0)) 
	{
		MessageBoxA(nullptr, "SetHandleInformation", "ERROR", MB_ICONERROR);
		PostMessage(mHwnd, WM_CLOSE, NULL, NULL);
	}
	
	// Run CF
	PROCESS_INFORMATION CfProcInfo = CreateChildProcess(CF_FILEPATH);

	std::string ExitReason = ReadFromPipe();
	if (!ExitReason.empty()) 
	{
		MessageBoxA(nullptr, ExitReason.c_str(), "Notice", MB_ICONERROR);
		PostMessage(mHwnd, WM_CLOSE, NULL, NULL);
	}

	// Clean up before exit.
	CloseHandle(g_hChildStd_OUT_Rd);
	TerminateProcess(CfProcInfo.hProcess, 1);
	CloseHandle(CfProcInfo.hProcess);
	CloseHandle(CfProcInfo.hThread);

	return 0;
}

std::string ReadFromPipe() 
{
	auto paused = [](BOOL paused) 
	{
		IsCfActive = !paused;
		if (Settings.playsound) 
		{
			PlaySound(paused ? TEXT("DeviceDisconnect") : TEXT("DeviceConnect"),
				nullptr, SND_ALIAS | SND_ASYNC);
		}
		HANDLE CopiedBmp = ReCa<HANDLE>(SendMessage(ImgBox, STM_SETIMAGE, IMAGE_BITMAP, 
			LPARAM(paused ? hImageOff : hImageOn)));
		DeleteObject(CopiedBmp);
	};

	auto updated = [](HWND control, std::string * globalvar, char * nval) 
	{
		*globalvar = std::string(nval);
		SetWindowTextA(control, (*globalvar).c_str());
	};

	auto strtoi = [](const char * str, UINT defValue) 
	{
		std::stringstream ss(str);
		UINT result;
		return ss >> result ? result : defValue;
	};

	DWORD dwRead;
	char chBuf[256];
	std::string ErrorMsg;
	while (!EXIT_REQUESTED) 
	{
		bool bSuccess = ReadFile(g_hChildStd_OUT_Rd, chBuf, 256, &dwRead, nullptr);
		if (!bSuccess || dwRead == 0) 
		{
			// Exit silently if requested by main thread
			ErrorMsg = (EXIT_REQUESTED) ? "" : "CF process was terminated. Program will exit";
			break;
		}
		std::stringstream ss;
		std::string s(chBuf, dwRead);
		ss.str(s);
		std::string line;
		char val[10];
		while (getline(ss, line)) 
		{
			if (line[0] == '\r') 
			{
				continue;
			}
			if(line.find("Bot Paused") != std::string::npos) 
			{
				paused(TRUE);
			}
			else if (line.find("Bot Unpaused") != std::string::npos) 
			{
				// reset ult key state reading (clean slate) when unpaused.
				// without this, given s76 pause is active,
				// if ult key was pressed while the bot was paused, 
				// s76 pause will be triggered as soon as the bot is unpaused.
				GetAsyncKeyState(Settings.ultkey);
				GetAsyncKeyState(Settings.s76pausekey);
				paused(FALSE);
			}
			else if (line.find("Login Successful") != std::string::npos) 
			{
				paused(FALSE);
				SwitchGuiMode(mHwnd);
			}
			else if (sscanf_s(line.c_str(), "Loaded file %s", val, 10) == 1) 
			{
				if (Settings.playsound && !CfCurrIni.empty()) 
				{
					MessageBeep(MB_OK);
				}
				updated(IniValue, &CfCurrIni, val);
			}
			else if (sscanf_s(line.c_str(), "Speed = %s", val, 10) == 1) 
			{
				updated(SpeedValue, &CfCurrSpeed, val);
			}
			else if (sscanf_s(line.c_str(), "Pull = %s", val, 10) == 1) 
			{
				// temporary fix
				/*updated(PullValue, &CfCurrPull, val);*/
				SetWindowTextA(SpeedPull, (CfCurrSpeed + "  /  " + std::string(val)).c_str());
			}
			else if (sscanf_s(line.c_str(), "Pause Key = %s", val, 10) == 1) 
			{
				CfPauseKeyInput.type = INPUT_KEYBOARD;
				CfPauseKeyInput.ki.wVk = strtoi(val, 20); // 20: CAPSLOCK
			}
			// Errors ------------------------------------------------------
			else if (line.find("Please enter your key") != std::string::npos) 
			{
				EXIT_REQUESTED = TRUE;
				ErrorMsg = line;
				break;
			}
			else if (line.find("Could not find") != std::string::npos) 
			{
				MessageBoxA(nullptr, "Close all running CF processes.", "Hint", MB_ICONERROR);
				EXIT_REQUESTED = TRUE;
				ErrorMsg = line;
				break;
			}
			else if (line.find("Unsuccessful") != std::string::npos) 
			{
				EXIT_REQUESTED = TRUE;
				ErrorMsg = line;
				break;
			}
		}
		std::fill_n(val, 10, 0);
		std::fill_n(chBuf, dwRead, 0);
	}

	return ErrorMsg;
}

PROCESS_INFORMATION CreateChildProcess(std::string filePath) 
{
	LPSTR szCmdline = _strdup(filePath.c_str());
	PROCESS_INFORMATION piProcInfo;
	STARTUPINFOA siStartInfo;

	ZeroMemory(&piProcInfo, sizeof(PROCESS_INFORMATION));

	ZeroMemory(&siStartInfo, sizeof(STARTUPINFO));
	siStartInfo.cb = sizeof(STARTUPINFO);
	siStartInfo.hStdOutput = g_hChildStd_OUT_Wr;
	siStartInfo.dwFlags |= STARTF_USESTDHANDLES;
	// start minimized, unactivated
	siStartInfo.dwFlags |= STARTF_USESHOWWINDOW;
	// Starting cf with SW_HIDE is not recommended b/c cf will not be terminated
	// if GuiCF is killed (not closed). However, if specified in settings do it.
	siStartInfo.wShowWindow = (Settings.hidecf) ? SW_HIDE : SW_SHOWMINNOACTIVE;

	bool bSuccess = CreateProcessA(nullptr,
	                               szCmdline,     // command line 
	                               nullptr,       // process security attributes 
	                               nullptr,       // primary thread security attributes 
	                               TRUE,          // handles are inherited 
	                               0,             // creation flags 
	                               nullptr,       // use parent's environment 
	                               nullptr,       // use parent's current directory 
	                               &siStartInfo,  // STARTUPINFO pointer 
	                               &piProcInfo);  // receives PROCESS_INFORMATION
	CloseHandle(g_hChildStd_OUT_Wr);

	if (!bSuccess) 
	{
		MessageBoxA(nullptr, "CreateChildProcess failure", "ERROR", MB_ICONERROR);
		PostMessage(mHwnd, WM_CLOSE, NULL, NULL);
		exit(1);
	}

	//---------------------------------------------------------------------------------
	if (Settings.delay > 0 && Settings.delay < 2000) 
	{
		Sleep(Settings.delay);

		typedef LONG(NTAPI *NtSuspendProcess)(IN HANDLE ProcessHandle);
		NtSuspendProcess pfnNtSuspendProcess = ReCa<NtSuspendProcess>(GetProcAddress(
			GetModuleHandleA("ntdll"), "NtSuspendProcess"));
		NtSuspendProcess pfnNtResumeProcess = ReCa<NtSuspendProcess>(GetProcAddress(
			GetModuleHandleA("ntdll"), "NtResumeProcess"));

		DWORD_PTR   base_addr = 0;
		DWORD       u_bytes;

		// Get base address
		if (EnumProcessModules(piProcInfo.hProcess, nullptr, 0, &u_bytes)) 
		{
			if (u_bytes) 
			{
				LPBYTE moduleArrayBytes = static_cast<LPBYTE>(LocalAlloc(LPTR, u_bytes));
				if (moduleArrayBytes) 
				{
					HMODULE *moduleArray = ReCa<HMODULE *>(moduleArrayBytes);
					if (EnumProcessModules(piProcInfo.hProcess, moduleArray, u_bytes, &u_bytes)) 
					{
						base_addr = ReCa<DWORD_PTR>(moduleArray[0]);
					}
					LocalFree(moduleArrayBytes);
				}
			}
		}

		if (base_addr) 
		{
			pfnNtSuspendProcess(piProcInfo.hProcess);

			// [PUSH "auth.cfaa.me"] to[PUSH "Exists"]
			byte new_byte = 0x2C;
			WriteProcessMemory(piProcInfo.hProcess, ReCa<LPVOID>(base_addr + 0x12253), 
				&new_byte, sizeof new_byte, nullptr);

			// FF D6(call esi) to 90 D6(nop db)
			new_byte = 0x90;
			WriteProcessMemory(piProcInfo.hProcess, ReCa<LPVOID>(base_addr + 0x1235F), 
				&new_byte, sizeof new_byte, nullptr);

			pfnNtResumeProcess(piProcInfo.hProcess);
		}
	}
	//---------------------------------------------------------------------------------

	return piProcInfo;
}

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
void ClipOrCenterRectToMonitor(LPRECT prc, UINT flags)
{
	MONITORINFO mi;
	RECT        rc;
	int         w = prc->right - prc->left;
	int         h = prc->bottom - prc->top;

	// get the nearest monitor to the passed rect. 
	HMONITOR hMonitor = MonitorFromRect(prc, MONITOR_DEFAULTTONEAREST);

	// get the work area or entire monitor rect. 
	mi.cbSize = sizeof(mi);
	GetMonitorInfo(hMonitor, &mi);

	rc = flags & MONITOR_WORKAREA ? mi.rcWork : mi.rcMonitor;

	// center or clip the passed rect to the monitor rect
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
	SetWindowPos(hwnd, nullptr, rc.left, rc.top, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
}

void SwitchGuiMode(HWND hWnd)
{
	// switch gui mode on right click. re-position window to compensate tool bar height
	// hide fix and show button if flagged (using flagged account) is not set. b/c useless.
	UINT mod = Settings.flagged ? NUM_STATE : NUM_STATE - 1;
	hIndex = ++hIndex % mod;
	int wHeight = WIN_HEIGHTS[hIndex];
	RECT r;
	GetWindowRect(hWnd, &r);
	if (hIndex <= 1)
	{
		LONG_PTR oStyle = GetWindowLongPtr(hWnd, GWL_STYLE);
		if (hIndex == 0)
		{
			wHeight -= hBarHeight; // toolbarless window
			r.top += hBarHeight;
			SetWindowLongPtr(hWnd, GWL_STYLE, oStyle ^ WS_CAPTION);
			SetLayeredWindowAttributes(hWnd, 0,
				255 * static_cast<int>(Settings.opacity) / 100, LWA_ALPHA);
		}
		else
		{
			SetWindowLongPtr(hWnd, GWL_STYLE, oStyle | WS_CAPTION);
			r.top -= hBarHeight; // restore toolbar
			SetLayeredWindowAttributes(hWnd, 0, (255 * 90) / 100, LWA_ALPHA);
		}
		RedrawWindow(hWnd, nullptr, nullptr, RDW_INVALIDATE); // immediate redraw
	}
	SetWindowPos(hWnd, nullptr, r.left, r.top, WIN_WIDTH, wHeight, SWP_NOZORDER | SWP_NOACTIVATE);
}

std::string FindFile(const std::string fileFullPath, BOOL excludeSelf)
{
	std::string foundFileName = "";
	WIN32_FIND_DATAA FindFileData;
	HANDLE hFind = FindFirstFileA(fileFullPath.c_str(), &FindFileData);

	if (excludeSelf)
	{
		char buffer[MAX_PATH];
		GetModuleFileNameA(nullptr, buffer, MAX_PATH);
		auto left = std::string(buffer).find_last_of("\\/");
		auto fName = std::string(buffer).substr(left + 1);

		while (true)
		{
			if (hFind == INVALID_HANDLE_VALUE)
			{
				break;
			}
			if (FindFileData.cFileName != fName)
			{
				// Found a matching file that's not current module
				foundFileName = FindFileData.cFileName;
				FindClose(hFind);
				break;
			}
			if (FindNextFileA(hFind, &FindFileData) == false)
			{
				break;
			}
		}
	}
	else if (hFind != INVALID_HANDLE_VALUE)
	{
		// found a matching file
		foundFileName = FindFileData.cFileName;
		FindClose(hFind);
	}

	// returns "" if not found
	return foundFileName;
}