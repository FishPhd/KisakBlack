// DXGetError*: the web build has no DirectX error tables.
#pragma once
#include <windows.h>
inline const char *DXGetErrorStringA(HRESULT) { return "HRESULT"; }
inline const char *DXGetErrorDescriptionA(HRESULT) { return "Direct3D is not available in the web build"; }
