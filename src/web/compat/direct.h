// <direct.h> for the web build: the MSVC CRT directory functions over POSIX. Engine paths use
// backslashes and arbitrary case, so every call goes through Web_FixPath first.
#pragma once
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <stdlib.h>
#include <web/web_assets.h>
inline int _mkdir(const char *path) { char p[1024]; return mkdir(Web_FixPath(path, p, sizeof(p)), 0777); }
inline int _rmdir(const char *path) { char p[1024]; return rmdir(Web_FixPath(path, p, sizeof(p))); }
inline int _chdir(const char *path) { char p[1024]; return chdir(Web_FixPath(path, p, sizeof(p))); }
inline char *_getcwd(char *buf, int size) { return getcwd(buf, (size_t)size); }