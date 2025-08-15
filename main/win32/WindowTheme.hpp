#pragma once
#ifndef NOMINMAX
#define NOMINMAX_DEFINED
#define NOMINMAX
#endif
#include <windows.h>
#ifdef NOMINMAX_DEFINED
#undef NOMINMAX_DEFINED
#undef NOMINMAX
#endif

namespace win32 {
	class WindowTheme {
	public:
		static BOOL IsSystemDarkModeEnabled();
		static BOOL ShouldApplicationEnableDarkMode();
		static BOOL SetDarkMode(HWND hWnd, BOOL bEnable, BOOL bFocus);
		static BOOL UpdateColorMode(HWND hWnd, BOOL bFocus);
	};
}
