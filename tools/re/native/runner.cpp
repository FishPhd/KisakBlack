// Binary protocol: 16 little-endian u32 inputs -> status + 4 u32 outputs.
// No text on stdout. Declared original-image regions are mapped privately,
// with validated absolute-address fixups, without startup/import execution.
#include <universal/com_pack.h>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <vector>
#include <set>
#include <chrono>
#include <algorithm>
#ifdef _WIN32
#include <windows.h>
#include <float.h>
#include <xmmintrin.h>
#include <io.h>
#include <fcntl.h>
#endif

// Unselected com_pack functions refer to these. /OPT:REF eliminates their COMDATs;
// linking must fail if a selected function accidentally starts depending on them.

struct Request { uint32_t words[16]; };
struct Response { uint32_t status; uint32_t words[4]; };
static_assert(sizeof(Request) == 64 && sizeof(Response) == 20);
extern void pm_call(bool original, void* entry, void* global, const uint32_t* input, uint32_t* output);
extern void fx_call(bool original, void* entry, const uint32_t* input, uint32_t* output);
extern void bounds_call(bool original, void* entry, const uint32_t* input, uint32_t* output);
extern void fx_velocity_call(bool original, void* entry, const uint32_t* input, uint32_t* output);

static float as_float(uint32_t bits) { float f; std::memcpy(&f, &bits, 4); return f; }

#ifdef _WIN32
static unsigned char* map_original(const char* path, const char* regions, const char* relocations) {
    FILE* file = std::fopen(path, "rb");
    if (!file) return nullptr;
    std::fseek(file, 0, SEEK_END); const auto length = std::ftell(file);
    std::rewind(file); std::vector<unsigned char> bytes(length);
    const auto got = std::fread(bytes.data(), 1, bytes.size(), file); std::fclose(file);
    if (got != bytes.size() || bytes.size() < sizeof(IMAGE_DOS_HEADER)) return nullptr;
    const auto dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(bytes.data());
    if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew < 0 ||
        size_t(dos->e_lfanew) + sizeof(IMAGE_NT_HEADERS32) > bytes.size()) return nullptr;
    const auto nt = reinterpret_cast<const IMAGE_NT_HEADERS32*>(bytes.data() + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE || nt->FileHeader.Machine != IMAGE_FILE_MACHINE_I386 ||
        nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR32_MAGIC) return nullptr;
    // Reserving the entire 80 MB image can collide with the runner's heap.
    // Reserve a private image and commit only declared code/constants. Absolute
    // data references receive validated fixups below.
    auto image = static_cast<unsigned char*>(VirtualAlloc(nullptr, nt->OptionalHeader.SizeOfImage,
                              MEM_RESERVE, PAGE_NOACCESS));
    if (!image) return nullptr;
    std::set<unsigned> chunks;
    for (const char* cursor = regions; *cursor;) {
        char* next;
        const auto rva = std::strtoul(cursor, &next, 0);
        if (*next++ != ':') return nullptr;
        const auto size = std::strtoul(next, &next, 0);
        if (!size || uint64_t(rva) + size > nt->OptionalHeader.SizeOfImage) return nullptr;
        for (unsigned chunk = rva & ~65535u; chunk <= (rva + size - 1) / 65536 * 65536; chunk += 65536)
            chunks.insert(chunk);
        if (*next && *next != ',') return nullptr;
        cursor = *next ? next + 1 : next;
    }
    auto section = IMAGE_FIRST_SECTION(nt);
    if (reinterpret_cast<const unsigned char*>(section + nt->FileHeader.NumberOfSections) > bytes.data() + bytes.size())
        return nullptr;
    for (const auto chunk : chunks) {
        if (!VirtualAlloc(image + chunk, 65536, MEM_COMMIT, PAGE_READWRITE)) {
            std::fprintf(stderr, "VirtualAlloc RVA %x failed: %lu\n", chunk, GetLastError()); return nullptr;
        }
        for (unsigned i = 0; i < nt->FileHeader.NumberOfSections; ++i) {
            const auto& s = section[i];
            if (uint64_t(s.PointerToRawData) + s.SizeOfRawData > bytes.size()) return nullptr;
            const unsigned start = (chunk > s.VirtualAddress) ? chunk : s.VirtualAddress;
            const unsigned limit = ((chunk + 65536) < (s.VirtualAddress + s.SizeOfRawData)) ?
                                   chunk + 65536 : s.VirtualAddress + s.SizeOfRawData;
            if (limit > start) std::memcpy(image + start, bytes.data() + s.PointerToRawData + start - s.VirtualAddress, limit - start);
        }
    }
    // Explicit loader fixups supplied by the disassembler: only absolute memory
    // operands referring to declared original-image constants are accepted.
    for (const char* cursor = relocations; *cursor;) {
        char* next; const auto operand = std::strtoul(cursor, &next, 0);
        if (*next++ != ':') return nullptr;
        const auto target = std::strtoul(next, &next, 0);
        if (operand + 4 > nt->OptionalHeader.SizeOfImage || target >= nt->OptionalHeader.SizeOfImage) return nullptr;
        *reinterpret_cast<uint32_t*>(image + operand) = uint32_t(image + target);
        if (*next && *next != ',') return nullptr;
        cursor = *next ? next + 1 : next;
    }
    for (const auto chunk : chunks) {
        DWORD old;
        if (!VirtualProtect(image + chunk, 65536, PAGE_EXECUTE_READ, &old)) return nullptr;
    }
    return image;
}

// Fragment boundaries are harness exits. One RET is placed immediately AFTER
// the verified original range in this private mapping. Within the range only
// validated absolute-address operands change. Full functions use their RET.
static bool set_fragment_exit(unsigned char* address) {
    DWORD old;
    if (!VirtualProtect(address, 1, PAGE_EXECUTE_READWRITE, &old)) return false;
    *address = 0xc3;
    FlushInstructionCache(GetCurrentProcess(), address, 1);
    return VirtualProtect(address, 1, old, &old) != FALSE;
}

static void original_call(int kind, void* entry, const Request& in, Response& out, const void* constant) {
    uint32_t input = in.words[0]; auto output = out.words;
    const auto alias = in.words[1];
    __asm {
        push ebx
        push esi
        push edi
        sub esp, 64
        mov edi, entry
        cmp kind, 0
        je pack9
        cmp kind, 1
        je pack10
        cmp kind, 2
        je texcoords
        // Byte4UnpackRgba inline body: ECX=input, EAX=output.
        lea ecx, input
        mov eax, output
        cmp alias, 0
        je rgba_ready
        mov ecx, eax
        mov edx, input
        mov [eax], edx
    rgba_ready:
        call edi
        jmp finished
    pack9:
        movss xmm1, input
        call edi
        movd eax, xmm1
        and eax, 511
        mov edx, output
        mov [edx], eax
        jmp finished
    pack10:
        movss xmm2, input
        mov eax, constant
        movsd xmm1, qword ptr [eax]
        call edi
        movd eax, xmm4
        and eax, 1023
        mov edx, output
        mov [edx], eax
        jmp finished
    texcoords:
        push output
        push input
        call edi
        add esp, 8
    finished:
        add esp, 64
        pop edi
        pop esi
        pop ebx
    }
}
#endif

static void cpp_call(int kind, const Request& in, Response& out) {
    struct Guarded { uint32_t left[4]; float values[4]; uint32_t right[4]; } guard{};
    std::fill_n(guard.left, 4, 0x5a55a55a); std::fill_n(guard.right, 4, 0x5a55a55a);
    switch (kind) {
    case 0: out.words[0] = FloatPack9S(as_float(in.words[0])); break;
    case 1: out.words[0] = FloatPack10S(as_float(in.words[0])); break;
    case 2: {
        Vec2UnpackTexCoords(PackedTexCoords(in.words[0]), guard.values);
        std::memcpy(out.words, guard.values, sizeof(guard.values)); break;
    }
    case 3: {
        unsigned char bytes[4]; std::memcpy(bytes, in.words, 4);
        if (in.words[1]) {
            std::memcpy(guard.values, bytes, 4);
            Byte4UnpackRgba(reinterpret_cast<const unsigned char*>(guard.values), guard.values);
        } else Byte4UnpackRgba(bytes, guard.values);
        std::memcpy(out.words, guard.values, sizeof(guard.values));
        if (std::memcmp(bytes, in.words, 4)) std::abort();
        break;
    }
    default: out.status = 0xffffffff;
    }
    for (unsigned i = 0; i < 4; ++i)
        if (guard.left[i] != 0x5a55a55a || guard.right[i] != 0x5a55a55a) std::abort();
}

static Response dispatch(int kind, bool original, void* entry, void* global, const void* constant, const Request& request) {
    Response response{};
#ifdef _WIN32
    _clearfp(); unsigned ignored;
    _controlfp_s(&ignored, _CW_DEFAULT, _MCW_EM | _MCW_RC | _MCW_PC);
    _mm_setcsr(0x1f80);
    __try {
        if (kind == 7) fx_velocity_call(original, entry, request.words, response.words);
        else if (kind == 6) bounds_call(original, entry, request.words, response.words);
        else if (kind == 5) fx_call(original, entry, request.words, response.words);
        else if (kind == 4) pm_call(original, entry, global, request.words, response.words);
        else if (original) original_call(kind, entry, request, response, constant);
        else cpp_call(kind, request, response);
    } __except(EXCEPTION_EXECUTE_HANDLER) { response.status = GetExceptionCode(); }
#else
    if (kind == 7) fx_velocity_call(false, nullptr, request.words, response.words);
    else if (kind == 6) bounds_call(false, nullptr, request.words, response.words);
    else if (kind == 5) fx_call(false, nullptr, request.words, response.words);
    else if (kind == 4) pm_call(false, nullptr, nullptr, request.words, response.words);
    else cpp_call(kind, request, response);
#endif
    return response;
}

int main(int argc, char** argv) {
    if (argc < 3) { std::fprintf(stderr, "runner cpp|original kind [binary rva end_rva]\n"); return 2; }
    const int kind = std::atoi(argv[2]);
    if (kind < 0 || kind > 7) return 2;
    const bool original = std::strcmp(argv[1], "original") == 0;
    if (!original && std::strcmp(argv[1], "cpp")) return 2;
    void* entry = nullptr;
    const void* constant = nullptr;
    void* global = nullptr;
#ifdef _WIN32
    _setmode(_fileno(stdin), _O_BINARY); _setmode(_fileno(stdout), _O_BINARY);
    if (original) {
        if (argc != 8) return 2;
        auto image = map_original(argv[3], argv[6], argv[7]);
        if (!image) { std::fprintf(stderr, "Cannot map declared original-image regions\n"); return 3; }
        auto rva = std::strtoul(argv[4], nullptr, 0), end = std::strtoul(argv[5], nullptr, 0);
        entry = image + rva;
        constant = image + 0x677150;
        global = image + 0x84f85c;
        if (end && !set_fragment_exit(image + end)) return 3;
    }
#else
    if (original) return 3;
#endif
    Request request;
    std::vector<Request> requests;
    while (true) {
        const auto count = std::fread(&request, 1, sizeof(request), stdin);
        if (!count && std::feof(stdin)) break;
        if (count != sizeof(request)) return 4;
        const Response response = dispatch(kind, original, entry, global, constant, request);
        if (std::fwrite(&response, 1, sizeof(response), stdout) != sizeof(response)) return 5;
        if (std::getenv("KISAK_RE_BENCH")) requests.push_back(request);
    }
    if (!requests.empty()) {
        using clock = std::chrono::steady_clock;
        volatile unsigned checksum = 0;
        // Setup, PE loading and I/O are excluded. The measured adapter includes
        // FP-state initialization and dispatch; this is not raw instruction time.
        constexpr unsigned repeats = 256;
        for (unsigned warm = 0; warm < 8; ++warm)
            for (const auto& r : requests) checksum = checksum ^ dispatch(kind, original, entry, global, constant, r).words[0];
        std::fputs("{\"adapter_ns_per_call\":[", stderr);
        for (unsigned sample = 0; sample < 21; ++sample) {
            const auto start = clock::now();
            for (unsigned repeat = 0; repeat < repeats; ++repeat)
                for (const auto& r : requests) {
                    const auto result = dispatch(kind, original, entry, global, constant, r);
                    if (result.status) return 6;
                    checksum = checksum ^ result.words[0];
                }
            const auto elapsed = std::chrono::duration<double, std::nano>(clock::now() - start).count();
            std::fprintf(stderr, "%s%.3f", sample ? "," : "", elapsed / (repeats * requests.size()));
        }
        std::fprintf(stderr, "],\"samples\":21,\"repeats\":256,\"checksum\":%u}", unsigned(checksum));
    }
    return std::fflush(stdout) ? 5 : 0;
}
