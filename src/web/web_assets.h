#pragma once

#include <cstddef>
#include <string>
#include <cstdio>

// Mounts the game and profile file systems (see web_assets.cpp) and returns the engine command line
// with the platform options removed and fs_basepath / fs_homepath filled in.
std::string Web_InitFilesystem(int argc, char **argv);
const char *Web_SessionId();
// Bounded slow-work events on the same epoch clock as frame telemetry.
void Web_ReportAssetWork(const char *kind, const char *path, double began, unsigned bytes = 0);

// Directory that holds main/ and zone/ (the engine's install path).
const char *Web_GameRoot();

// Maps an engine path (backslashes, any case) to the real path in the web file system. Returns `out`.
const char *Web_FixPath(const char *in, char *out, size_t cap);
// Returns a read-only stream over a bounded immutable archive cache, or null for normal I/O.
FILE *Web_OpenCachedArchive(const char *path, const char *mode);

// --- On-demand assets (browser only; no-ops under Node, where the whole game directory is mounted) ---
//
// The page writes /opfs/manifest.txt: one "relative/path<TAB>size<TAB>map" line per file the build can
// fetch ("map" is empty for the boot set). A file listed there that is not in OPFS yet is "remote":
//   Web_RemoteKnown   - true (and the size) if the file is remote or local, so existence checks pass
//   Web_EnsureFile    - blocks the calling thread until the page has copied the file into OPFS;
//                       true when the file is present afterwards. Call it from a worker thread only.
//   Web_PrefetchMap   - asks the page to start fetching a map's files in the background
bool Web_RemoteKnown(const char *osPath, unsigned long long *sizeOut);
bool Web_EnsureFile(const char *osPath);
// Copies one loose file out of the install's IWDs into OPFS (main/images/..., and so on).
// rel is a game-relative path such as "main/images/foo.iwi". False if the install has no such entry.
bool Web_EnsureLoose(const char *rel);
void Web_PrefetchMap(const char *mapName);
// Ensure a generated map IWD is present before FS_Restart enumerates search paths.
// Called with the mapname dvar. Names are lowercase [a-z0-9_] and must start with mp_.
void Web_EnsureMapPack(const char *mapName);
// Non-blocking: ask the page to ensureEntry every path in parallel. The database thread still
// waits in Web_EnsureFile before it inflates each fastfile.
void Web_KickEnsureFiles(const char *const *osPaths, int count);
