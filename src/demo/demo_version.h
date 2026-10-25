#pragma once

struct NetFieldList;
enum netFieldTypes_t : __int32;

int __cdecl Demo_GetVersion();
bool __cdecl Demo_IsVersionAtLeast(int version);
const NetFieldList *__cdecl Demo_GetNetFieldListForType(netFieldTypes_t fieldType);
const NetFieldList *__cdecl Demo_GetStateFieldListForEntityType(int eType);
