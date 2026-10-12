#pragma once
#include <windows.h>
#define CSIDL_PERSONAL 0x0005
#define CSIDL_LOCAL_APPDATA 0x001c
#define CSIDL_FLAG_CREATE 0x8000
// Implemented in src/web/web_win32.cpp: maps every folder to the OPFS profile directory.
HRESULT SHGetFolderPathA(HWND hwnd, int csidl, HANDLE hToken, DWORD dwFlags, LPSTR pszPath);
