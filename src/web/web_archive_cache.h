#pragma once
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <vector>
#include <algorithm>
#include <string>
#if defined(__EMSCRIPTEN__) && defined(KISAK_WEB)
#include <emscripten.h>
#include "web_assets.h"
#endif
namespace {
// Minizip seeks between individual header fields. Stdio buffering cannot help
// those seeks: a small immutable block cache prevents an OPFS proxy round trip
// for every byte, without retaining a whole map archive in the wasm heap.
struct ArchiveBlocks {
    FILE *source;
    off_t position = 0, size;
    struct Block { off_t start = -1; size_t count = 0; std::vector<char> bytes; } blocks[2];
    unsigned next = 0;
    std::string path;
    static ssize_t Read(void *cookie, char *dst, size_t bytes) {
        auto &c = *static_cast<ArchiveBlocks *>(cookie);
        size_t copied = 0;
        while (bytes && c.position < c.size) {
            const off_t start = c.position / 65536 * 65536;
            Block *b = nullptr;
            for (auto &candidate : c.blocks) if (candidate.start == start) b = &candidate;
            if (!b) {
                b = &c.blocks[c.next++ % 2]; b->bytes.resize(65536);
#if defined(__EMSCRIPTEN__) && defined(KISAK_WEB)
                const double began = emscripten_get_now();
#endif
                if (fseeko(c.source, start, SEEK_SET)) return copied ? ssize_t(copied) : -1;
                b->count = fread(b->bytes.data(), 1, b->bytes.size(), c.source);
#if defined(__EMSCRIPTEN__) && defined(KISAK_WEB)
                Web_ReportAssetWork("archive-block-read", c.path.c_str(), began, unsigned(b->count));
#endif
                b->start = start;
                if (!b->count && ferror(c.source)) return copied ? ssize_t(copied) : -1;
            }
            const size_t off = size_t(c.position - start);
            if (off >= b->count) break;
            const size_t n = std::min(bytes, b->count - off);
            memcpy(dst + copied, b->bytes.data() + off, n);
            bytes -= n; copied += n; c.position += n;
        }
        return ssize_t(copied);
    }
    static int Seek(void *cookie, off_t *offset, int whence) {
        auto &c = *static_cast<ArchiveBlocks *>(cookie);
        off_t base = whence == SEEK_SET ? 0 : whence == SEEK_CUR ? c.position : c.size;
        if ((whence != SEEK_SET && whence != SEEK_CUR && whence != SEEK_END) || *offset < -base
            || (*offset > 0 && base > INT64_MAX - *offset)) return -1;
        c.position = base + *offset; *offset = c.position; return 0;
    }
    static int Close(void *cookie) { auto *c = static_cast<ArchiveBlocks *>(cookie); const int result = fclose(c->source); delete c; return result; }
};
FILE *OpenArchiveBlocks(const char *path) {
    FILE *source = fopen(path, "rb"); if (!source) return nullptr;
    if (fseeko(source, 0, SEEK_END)) { fclose(source); return nullptr; }
    const off_t size = ftello(source); if (size < 0) { fclose(source); return nullptr; }
    auto *cookie = new ArchiveBlocks{source, 0, size};
    cookie->path = path;
    const cookie_io_functions_t callbacks = {ArchiveBlocks::Read, nullptr, ArchiveBlocks::Seek, ArchiveBlocks::Close};
    FILE *stream = fopencookie(cookie, "rb", callbacks);
    if (!stream) { fclose(source); delete cookie; }
    return stream;
}
}

