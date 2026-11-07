// Just enough of <windows.h> for include/Plugin.h to compile in the unit tests
// on non-Windows platforms. Never used for the plugin DLL itself.
#pragma once

#define __int64 long long
#define __declspec(x)
#define WM_USER 0x0400

typedef int BOOL;
typedef void *HWND;
typedef unsigned long COLORREF;
typedef const char *LPCTSTR;
