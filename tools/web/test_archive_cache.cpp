#include "web/web_archive_cache.h"
#include <cstdlib>
static void check(bool ok, const char *message) { if (!ok) { fprintf(stderr, "FAIL %s\n", message); exit(1); } }
int main() {
    const char *path = "/tmp/test-immutable-archive";
    std::vector<unsigned char> bytes(300000);
    for (size_t i = 0; i < bytes.size(); ++i) bytes[i] = (i * 17 + i / 251) & 255;
    FILE *out = fopen(path, "wb"); check(out != nullptr, "create fixture");
    check(fwrite(bytes.data(), 1, bytes.size(), out) == bytes.size(), "write fixture"); fclose(out);
    FILE *in = OpenArchiveBlocks(path); check(in != nullptr, "open cached archive");
    std::vector<unsigned char> got(150000);
    check(fread(got.data(), 1, got.size(), in) == got.size(), "multi-block read");
    check(!memcmp(got.data(), bytes.data(), got.size()), "multi-block contents");
    for (int i = 0; i < 100; ++i) {
        const long offset = i * 2633;
        check(fseek(in, offset, SEEK_SET) == 0, "absolute seek");
        check(fread(got.data(), 1, 129, in) == 129 && !memcmp(got.data(), bytes.data() + offset, 129), "seek contents");
        check(fseek(in, -64, SEEK_CUR) == 0 && ftell(in) == offset + 65, "relative seek respects stdio buffering");
    }
    check(fseek(in, -10, SEEK_END) == 0, "end seek");
    check(fread(got.data(), 1, 50, in) == 10 && !memcmp(got.data(), bytes.data() + bytes.size() - 10, 10), "short EOF read");
    check(fseek(in, -1, SEEK_SET) != 0, "negative seek refused");
    check(fseek(in, 350000, SEEK_SET) == 0 && fread(got.data(), 1, 1, in) == 0, "seek past EOF");
    check(fclose(in) == 0, "close underlying file"); remove(path);
    puts("PASS bounded archive cache: read, seek, block eviction, EOF, close");
}
