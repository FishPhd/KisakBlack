// Web file system bootstrap: where the game files and the profile live, and how a file the engine
// is about to open gets there.
//
//   Browser : OPFS (origin private file system) is mounted at /opfs. The page shell imports the game
//             files into /opfs/game once; after that a launch reads them locally, so a second launch
//             transfers nothing. The profile (configs, stats) lives in /opfs/profile.
//   Node    : (headless tests) a host directory is mounted at /game through the Node backend.
//
// Both mounts are wrapped in the case-insensitive backend: the engine, like Windows, treats
// "Zone/English/X.ff" and "zone/english/x.ff" as the same file.

#include <emscripten.h>
#include <emscripten/threading.h>
#include <emscripten/wasmfs.h>

#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cctype>
#include <cerrno>
#include <climits>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "web_assets.h"

static char s_gameRoot[256] = "/game";
static char s_webSession[33] = "";
static bool s_zombieBoot = false;
static bool s_readCache = true;
bool Web_IsZombieBoot() { return s_zombieBoot; }
const char *Web_SessionId() { return s_webSession; }

namespace
{
    struct RemoteEntry
    {
        unsigned long long size;
        std::string map;
        std::string path; // as written in OPFS (the install's case)
    };

    // Lower-case game-relative path -> entry. Filled once, before the engine starts; read-only after.
    std::unordered_map<std::string, RemoteEntry> s_remote;

    // "/opfs/game\zone\English\X.ff" -> "zone/english/x.ff" (relative to the game root, lower case).
    std::string RelKey(const char *osPath)
    {
        std::string p(osPath ? osPath : "");
        for (char &c : p)
        {
            if (c == '\\')
                c = '/';
            else
                c = (char)tolower((unsigned char)c);
        }
        std::string root(s_gameRoot);
        for (char &c : root)
            c = (char)tolower((unsigned char)c);
        if (p.compare(0, root.size(), root) == 0)
        {
            p.erase(0, root.size());
            while (!p.empty() && p[0] == '/')
                p.erase(0, 1);
        }
        while (p.compare(0, 2, "./") == 0)
            p.erase(0, 2);
        return p;
    }

    void LoadManifest(const char *path)
    {
        FILE *f = fopen(path, "r");
        if (!f)
            return;
        char line[1024];
        while (fgets(line, sizeof(line), f))
        {
            char *tab1 = strchr(line, '\t');
            if (!tab1)
                continue;
            *tab1++ = 0;
            char *tab2 = strchr(tab1, '\t');
            std::string map;
            if (tab2)
            {
                *tab2++ = 0;
                map = tab2;
                while (!map.empty() && (map.back() == '\n' || map.back() == '\r'))
                    map.pop_back();
            }
            RemoteEntry e;
            e.size = strtoull(tab1, nullptr, 10);
            e.map = map;
            e.path = line;
            s_remote[RelKey(line)] = e;
        }
        fclose(f);
        fprintf(stderr, "web: manifest lists %u remote-capable files\n", (unsigned)s_remote.size());
    }

    bool LocalExists(const char *osPath)
    {
        char fixed[1024];
        Web_FixPath(osPath, fixed, sizeof(fixed));
        struct stat st;
        if (stat(fixed, &st) != 0)
            return false;
        // Creating a destination exposes an empty file before the page commits
        // its download. Prefetch (including another tab) can make a map exist
        // before it is safe to open; require the complete manifest size.
        const auto it = s_remote.find(RelKey(osPath));
        return it == s_remote.end()
            || (S_ISREG(st.st_mode) && st.st_size >= 0 && (unsigned long long)st.st_size == it->second.size);
    }
}

bool Web_RemoteKnown(const char *osPath, unsigned long long *sizeOut)
{
    if (s_remote.empty())
        return false;
    const auto it = s_remote.find(RelKey(osPath));
    if (it == s_remote.end())
        return false;
    if (sizeOut)
        *sizeOut = it->second.size;
    return true;
}

// Blocks the calling worker until the page has copied the file into OPFS (Module.kisakEnsureFile,
// see web/kisak.js). The page completes by storing into `state` and notifying the futex, straight from
// the browser's main thread, so the wait never depends on any thread in the engine making progress.
bool Web_EnsureFile(const char *osPath)
{
    if (LocalExists(osPath))
        return true;
    if (s_remote.empty())
        return false;
    const std::string rel = RelKey(osPath);
    const auto it = s_remote.find(rel);
    if (it == s_remote.end())
        return false;

    alignas(4) volatile int state = 0;
    const double t0 = emscripten_get_now();
    // clang-format off
    MAIN_THREAD_ASYNC_EM_ASM({
        const rel = UTF8ToString($0);
        const ptr = $1;
        const done = (v) => { Atomics.store(HEAP32, ptr >> 2, v); Atomics.notify(HEAP32, ptr >> 2); };
        if (!Module.kisakEnsureFile) { done(2); return; }
        Module.kisakEnsureFile(rel).then(() => done(1), (e) => { console.error('kisakEnsureFile', rel, e); done(2); });
    }, rel.c_str(), &state);
    // clang-format on
    while (__atomic_load_n(&state, __ATOMIC_ACQUIRE) == 0)
        emscripten_futex_wait((void *)&state, 0, INFINITY);

    const bool ok = state == 1 && LocalExists(osPath);
    fprintf(stderr, "web: fetch %s %s (%llu bytes, %.1f s)\n", rel.c_str(), ok ? "ok" : "FAILED", it->second.size,
            (emscripten_get_now() - t0) / 1000.0);
    if (!ok)
    {
        // The page says the file is in OPFS, so a failure here is the file system's view of it.
        char fixed[1024];
        Web_FixPath(osPath, fixed, sizeof(fixed));
        struct stat st;
        const int rc = stat(fixed, &st);
        fprintf(stderr, "web:   page state=%d, stat(%s)=%d errno=%d\n", state, fixed, rc, rc ? errno : 0);
        {
            const std::string exact = std::string(s_gameRoot) + "/" + it->second.path;
            struct stat es;
            const int erc = stat(exact.c_str(), &es);
            fprintf(stderr, "web:   exact-case stat(%s) = %d errno=%d\n", exact.c_str(), erc, erc ? errno : 0);
        }
        // Which prefix of the path does the file system fail to see?
        std::string prefix;
        const std::string full(fixed);
        for (size_t i = 1; i <= full.size(); ++i)
        {
            if (i != full.size() && full[i] != '/')
                continue;
            prefix = full.substr(0, i);
            struct stat ps;
            const int prc = stat(prefix.c_str(), &ps);
            fprintf(stderr, "web:     stat(%s) = %d%s\n", prefix.c_str(), prc, prc ? "" : (S_ISDIR(ps.st_mode) ? " dir" : " file"));
            if (prc)
            {
                const size_t slash = prefix.rfind('/');
                DIR *d = opendir(slash == 0 ? "/" : prefix.substr(0, slash).c_str());
                int n = 0;
                while (d && n < 12)
                {
                    struct dirent *e = readdir(d);
                    if (!e)
                        break;
                    fprintf(stderr, "web:       parent has '%s'\n", e->d_name);
                    ++n;
                }
                if (d)
                    closedir(d);
                break;
            }
        }
    }
    return ok;
}

bool Web_EnsureLoose(const char *rel)
{
    if (!rel || !rel[0] || strstr(rel, ".."))
        return false;
    char osPath[1024];
    snprintf(osPath, sizeof(osPath), "%s/%s", s_gameRoot, rel);
    if (LocalExists(osPath))
        return true;
    static std::mutex missMu;
    static std::unordered_set<std::string> missing;
    {
        std::lock_guard<std::mutex> lk(missMu);
        if (missing.count(rel))
            return false;
    }
    alignas(4) volatile int state = 0;
    // clang-format off
    MAIN_THREAD_ASYNC_EM_ASM({
        const rel = UTF8ToString($0);
        const ptr = $1;
        const done = (v) => { Atomics.store(HEAP32, ptr >> 2, v); Atomics.notify(HEAP32, ptr >> 2); };
        if (!Module.kisakEnsureLoose) { done(2); return; }
        Module.kisakEnsureLoose(rel).then(() => done(1), () => done(2));
    }, rel, &state);
    // clang-format on
    while (__atomic_load_n(&state, __ATOMIC_ACQUIRE) == 0)
        emscripten_futex_wait((void *)&state, 0, INFINITY);
    if (state == 1 && LocalExists(osPath))
        return true;
    std::lock_guard<std::mutex> lk(missMu);
    missing.insert(rel);
    return false;
}

void Web_KickEnsureFiles(const char *const *osPaths, int count)
{
    if (!osPaths || count <= 0 || s_remote.empty())
        return;
    struct Batch
    {
        int n;
        char rel[32][300];
    };
    static Batch ring[4];
    static unsigned next;
    Batch *b = &ring[next++ & 3];
    b->n = 0;
    for (int i = 0; i < count && b->n < 32; ++i)
    {
        if (!osPaths[i] || !osPaths[i][0] || LocalExists(osPaths[i]))
            continue;
        const std::string rel = RelKey(osPaths[i]);
        if (!s_remote.count(rel))
            continue;
        snprintf(b->rel[b->n], sizeof(b->rel[0]), "%s", rel.c_str());
        ++b->n;
    }
    if (!b->n)
        return;
    // clang-format off
    MAIN_THREAD_ASYNC_EM_ASM({
        const n = $0;
        const base = $1;
        const stride = $2;
        if (!Module.kisakEnsureEntry) return;
        for (let i = 0; i < n; ++i)
            Module.kisakEnsureEntry(UTF8ToString(base + i * stride));
    }, b->n, b->rel[0], (int)sizeof(b->rel[0]));
    // clang-format on
}

void Web_PrefetchMap(const char *mapName)
{
    if (s_remote.empty() || !mapName || !*mapName)
        return;
    // The page reads the name later, on its own thread: hand it a slot of a small ring that outlives the
    // call (a map change is seconds apart, the ring is four deep).
    static char ring[4][64];
    static unsigned next;
    char *slot = ring[next++ & 3];
    snprintf(slot, sizeof(ring[0]), "%s", mapName);
    // clang-format off
    MAIN_THREAD_ASYNC_EM_ASM({
        if (Module.kisakPrefetchMap) Module.kisakPrefetchMap(UTF8ToString($0));
    }, slot);
    // clang-format on
}

const char *Web_GameRoot()
{
    return s_gameRoot;
}

namespace
{
    bool RunningUnderNode()
    {
        return EM_ASM_INT({
                   return (typeof process !== 'undefined' && process.versions && process.versions.node) ? 1 : 0;
               }) != 0;
    }
}

// Consumes the platform options from argv and returns the remaining engine command line.
//   --gamedir <host path>   Node only: host directory with main/ and zone/
std::string Web_InitFilesystem(int argc, char **argv)
{
    std::string gameDirHost;
    std::string profileDirHost;
    std::vector<std::string> engineArgs;
    std::string guestProfile;
    for (int i = 1; i < argc; ++i)
    {
        if (!strcmp(argv[i], "--no-read-cache"))
        {
            s_readCache = false;
            continue;
        }
        if (!strcmp(argv[i], "--zombies"))
        {
            s_zombieBoot = true;
            continue;
        }
        if (!strcmp(argv[i], "--web-session") && i + 1 < argc)
        {
            const char *id = argv[++i];
            if (strlen(id) == 32 && strspn(id, "0123456789abcdef") == 32)
                snprintf(s_webSession, sizeof(s_webSession), "%s", id);
            continue;
        }
        if (!strcmp(argv[i], "--gamedir") && i + 1 < argc)
        {
            gameDirHost = argv[++i];
            continue;
        }
        if (!strcmp(argv[i], "--profiledir") && i + 1 < argc)
        {
            profileDirHost = argv[++i];
            continue;
        }
        if (!strcmp(argv[i], "--guest-profile") && i + 1 < argc)
        {
            guestProfile = argv[++i];
            if (guestProfile.size() != 32 || guestProfile.find_first_not_of("0123456789abcdef") != std::string::npos)
                guestProfile.clear();
            continue;
        }
        engineArgs.emplace_back(argv[i]);
    }

    const char *gameRoot = "/game";
    const char *homeRoot = "/profile";

    if (RunningUnderNode())
    {
        if (gameDirHost.empty())
        {
            fprintf(stderr, "web: running under Node needs --gamedir <host path to main/ and zone/>\n");
        }
        else
        {
            backend_t node = wasmfs_create_node_backend(gameDirHost.c_str());
            backend_t icase = wasmfs_create_icase_backend(node);
            if (wasmfs_create_directory("/game", 0777, icase) != 0)
                fprintf(stderr, "web: could not mount %s at /game\n", gameDirHost.c_str());
        }
        if (profileDirHost.empty())
            mkdir("/profile", 0777);
        else
        {
            backend_t profile = wasmfs_create_node_backend(profileDirHost.c_str());
            if (wasmfs_create_directory("/profile", 0777, profile) != 0)
                fprintf(stderr, "web: could not mount persistent profile %s\n", profileDirHost.c_str());
        }
    }
    else
    {
        backend_t opfs = wasmfs_create_opfs_backend();
        backend_t icase = wasmfs_create_icase_backend(opfs);
        if (wasmfs_create_directory("/opfs", 0777, icase) != 0)
            fprintf(stderr, "web: could not mount OPFS\n");
        mkdir("/opfs/game", 0777);
        mkdir("/opfs/profile", 0777);
        gameRoot = "/opfs/game";
        // Keep profile I/O on its own OPFS worker and native directory backend.
        // Asset enumeration must not starve saves or contend with virtual-directory
        // rename locks. This is an alias of the same persistent OPFS profile, not a reset.
        backend_t profile = wasmfs_create_opfs_backend();
        if (wasmfs_create_directory("/opfs-profile", 0777, profile) != 0)
            fprintf(stderr, "web: could not mount profile OPFS\n");
        mkdir("/opfs-profile/profile", 0777);
        homeRoot = "/opfs-profile/profile";
    }

    std::string modeHome;
    if (s_zombieBoot)
    {
        modeHome = std::string(homeRoot) + "/zombies";
        mkdir(modeHome.c_str(), 0777);
        homeRoot = modeHome.c_str();
    }
    std::string guestHome;
    if (!guestProfile.empty())
    {
        guestHome = std::string(homeRoot) + "/guests";
        mkdir(guestHome.c_str(), 0777);
        guestHome += "/" + guestProfile;
        mkdir(guestHome.c_str(), 0777);
        homeRoot = guestHome.c_str();
    }
    snprintf(s_gameRoot, sizeof(s_gameRoot), "%s", gameRoot);
    // Browser: the page writes the manifest. Node: the same file in the game directory lets the
    // headless runner exercise the on-demand path (tools/web/run_node.mjs --lazy-source).
    LoadManifest(RunningUnderNode() ? "/game/manifest.txt" : "/opfs/manifest.txt");
    if (chdir(gameRoot) != 0)
        fprintf(stderr, "web: chdir(%s) failed\n", gameRoot);

    std::string cmd;
    for (const std::string &a : engineArgs)
    {
        if (!cmd.empty())
            cmd += ' ';
        const bool quote = a.find(' ') != std::string::npos;
        if (quote)
            cmd += '"';
        cmd += a;
        if (quote)
            cmd += '"';
    }

    // The engine resolves everything against these two roots.
    if (cmd.find("fs_b ") == std::string::npos)
        cmd += std::string(" +set fs_b ") + gameRoot;
    if (cmd.find("fs_h ") == std::string::npos)
        cmd += std::string(" +set fs_h ") + homeRoot;
    return cmd;
}

FILE *Web_OpenCachedArchive(const char *osPath, const char *mode)
{
    if (!s_readCache || strncmp(s_gameRoot, "/opfs/", 6)
        || (strcmp(mode, "rb") && strcmp(mode, "r"))) return nullptr;
    const std::string key = RelKey(osPath);
    const auto entry = s_remote.find(key);
    if (entry == s_remote.end() || key.size() < 4 || key.compare(key.size() - 4, 4, ".iwd")
        || key.compare(0, 5, "main/") || !entry->second.map.empty()) return nullptr;
    // Bound the extra allocation within the fixed 3 GB heap. Map fastfiles and
    // writable profile files retain their normal backing storage.
    constexpr size_t budget = 512u * 1024u * 1024u;
    static std::mutex mutex;
    static size_t used;
    static std::unordered_map<std::string, std::pair<void *, size_t>> cache;
    std::lock_guard<std::mutex> lock(mutex);
    auto hit = cache.find(key);
    if (hit != cache.end()) return fmemopen(hit->second.first, hit->second.second, "rb");
    // Check the manifest's 64-bit size before narrowing it on wasm32.
    if (!entry->second.size || entry->second.size > budget - used) return nullptr;
    const size_t size = (size_t)entry->second.size;
    FILE *in = fopen(osPath, "rb");
    if (!in) return nullptr;
    void *data = malloc(size);
    if (!data) { fclose(in); return nullptr; }
    const double start = emscripten_get_now();
    const bool complete = fread(data, 1, size, in) == size;
    fclose(in);
    if (!complete) { free(data); return nullptr; }
    FILE *stream = fmemopen(data, size, "rb");
    if (!stream) { free(data); return nullptr; }
    cache.emplace(key, std::make_pair(data, size)); used += size;
    fprintf(stderr, "web: cached archive %s (%u MB in %.1f ms)\n", key.c_str(), (unsigned)(size >> 20), emscripten_get_now() - start);
    return stream;
}
