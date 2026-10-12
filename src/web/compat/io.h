// <io.h> for the web build: the MSVC CRT file helpers over POSIX.
#pragma once
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <web/web_assets.h>
#ifndef _A_SUBDIR
#define _A_NORMAL 0x00
#define _A_RDONLY 0x01
#define _A_HIDDEN 0x02
#define _A_SYSTEM 0x04
#define _A_SUBDIR 0x10
#define _A_ARCH 0x20
#endif
inline int _access(const char *path, int mode) { char p[1024]; return access(Web_FixPath(path, p, sizeof(p)), mode); }
inline int _unlink(const char *path) { char p[1024]; return unlink(Web_FixPath(path, p, sizeof(p))); }
inline int _close(int fd) { return close(fd); }
inline int _read(int fd, void *buf, unsigned n) { return (int)read(fd, buf, n); }
inline int _write(int fd, const void *buf, unsigned n) { return (int)write(fd, buf, n); }
inline long _lseek(int fd, long off, int whence) { return (long)lseek(fd, off, whence); }
