// Private original-image loader; undeclared code traps. Derived from the local original-code runner.
static unsigned char* map_original(const char* path, const char* regions, const char* relocations,
                                   const char* blocked = "-") {
    FILE* file = std::fopen(path, "rb");
    if (!file) return nullptr;
    std::fseek(file, 0, SEEK_END); const auto length = std::ftell(file);
    if (length < 0) { std::fclose(file); return nullptr; }
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
    struct Region { unsigned start, size; };
    std::vector<Region> declared;
    for (const char* cursor = regions; *cursor;) {
        if (*cursor < '0' || *cursor > '9') return nullptr;
        char* next;
        const auto rva = std::strtoul(cursor, &next, 0);
        if (next == cursor || *next++ != ':' || *next < '0' || *next > '9') return nullptr;
        const auto size = std::strtoul(next, &next, 0);
        if (!size || uint64_t(rva) + size > nt->OptionalHeader.SizeOfImage) return nullptr;
        declared.push_back({unsigned(rva), unsigned(size)});
        for (unsigned chunk = rva & ~65535u; chunk <= (rva + size - 1) / 65536 * 65536; chunk += 65536)
            chunks.insert(chunk);
        if (*next && (*next != ',' || !next[1])) return nullptr;
        cursor = *next ? next + 1 : next;
    }
    if (declared.empty()) return nullptr;
    const auto contains = [&declared](unsigned rva, unsigned size) {
        for (const auto& region : declared)
            if (region.start <= rva && uint64_t(rva) + size <= uint64_t(region.start) + region.size) return true;
        return false;
    };
    std::set<unsigned> traps;
    if (std::strcmp(blocked, "-")) {
        for (const char* cursor = blocked; *cursor;) {
            if (*cursor < '0' || *cursor > '9') return nullptr;
            char* next;
            const auto rva = std::strtoul(cursor, &next, 10);
            if (next == cursor || uint64_t(rva) + 2 > nt->OptionalHeader.SizeOfImage) return nullptr;
            for (const auto& region : declared)
                if (rva < uint64_t(region.start) + region.size && region.start < uint64_t(rva) + 2)
                    return nullptr;
            auto following = traps.lower_bound(rva);
            if (following != traps.end() && uint64_t(*following) < uint64_t(rva) + 2) return nullptr;
            if (following != traps.begin()) {
                auto preceding = following; --preceding;
                if (uint64_t(*preceding) + 2 > rva) return nullptr;
            }
            traps.insert(unsigned(rva));
            chunks.insert(unsigned(rva) & ~65535u);
            chunks.insert(unsigned(rva + 1) & ~65535u);
            if (*next && (*next != ',' || !next[1])) return nullptr;
            cursor = *next ? next + 1 : next;
        }
        if (traps.empty()) return nullptr;
    }
    auto section = IMAGE_FIRST_SECTION(nt);
    if (reinterpret_cast<const unsigned char*>(section + nt->FileHeader.NumberOfSections) > bytes.data() + bytes.size())
        return nullptr;
    for (const auto chunk : chunks) {
        if (!VirtualAlloc(image + chunk, 65536, MEM_COMMIT, PAGE_READWRITE)) {
            std::fprintf(stderr, "VirtualAlloc RVA %x failed: %lu\n", chunk, GetLastError()); return nullptr;
        }
        // Keep every undeclared byte trapping, including unsupported bodies
        // sharing a committed chunk with verified code. Copy only declarations.
        std::memset(image + chunk, 0xcc, 65536);
        for (const auto& region : declared) {
            const unsigned zero_start = (std::max)(chunk, region.start);
            const unsigned zero_end = unsigned((std::min)(uint64_t(chunk) + 65536,
                                                        uint64_t(region.start) + region.size));
            if (zero_end > zero_start) std::memset(image + zero_start, 0, zero_end - zero_start);
        }
        for (unsigned i = 0; i < nt->FileHeader.NumberOfSections; ++i) {
            const auto& s = section[i];
            if (uint64_t(s.PointerToRawData) + s.SizeOfRawData > bytes.size() ||
                uint64_t(s.VirtualAddress) + s.SizeOfRawData > nt->OptionalHeader.SizeOfImage) return nullptr;
            const unsigned start = (chunk > s.VirtualAddress) ? chunk : s.VirtualAddress;
            const unsigned limit = ((chunk + 65536) < (s.VirtualAddress + s.SizeOfRawData)) ?
                                   chunk + 65536 : s.VirtualAddress + s.SizeOfRawData;
            if (limit > start) {
                for (const auto& region : declared) {
                    const unsigned copy_start = (std::max)(start, region.start);
                    const unsigned copy_end = unsigned((std::min)(uint64_t(limit), uint64_t(region.start) + region.size));
                    if (copy_end > copy_start)
                        std::memcpy(image + copy_start,
                                    bytes.data() + s.PointerToRawData + copy_start - s.VirtualAddress,
                                    copy_end - copy_start);
                }
            }
        }
    }
    for (const auto rva : traps) {
        image[rva] = 0x0f;
        image[rva + 1] = 0x0b; // UD2: reaching a boundary must fail, never return.
    }
    // The checked disassembler supplies absolute operands and bounded switch
    // pointer entries. Independently require both ends to be declared, require
    // the source word to contain that exact original VA, and reject overlapping
    // writes. A table entry must point into this private image after relocation.
    std::set<unsigned> fixed;
    for (const char* cursor = relocations; *cursor;) {
        if (*cursor < '0' || *cursor > '9') return nullptr;
        char* next; const auto operand = std::strtoul(cursor, &next, 0);
        if (next == cursor || *next++ != ':' || *next < '0' || *next > '9') return nullptr;
        const auto target = std::strtoul(next, &next, 0);
        if (!contains(operand, 4) || !contains(target, 1) ||
            uint64_t(nt->OptionalHeader.ImageBase) + target > 0xffffffffu) return nullptr;
        auto following = fixed.lower_bound(operand);
        if (following != fixed.end() && uint64_t(*following) < uint64_t(operand) + 4) return nullptr;
        if (following != fixed.begin()) {
            auto preceding = following; --preceding;
            if (uint64_t(*preceding) + 4 > operand) return nullptr;
        }
        uint32_t original;
        std::memcpy(&original, image + operand, sizeof(original));
        if (original != nt->OptionalHeader.ImageBase + target) return nullptr;
        const auto relocated = uint32_t(image + target);
        std::memcpy(image + operand, &relocated, sizeof(relocated));
        fixed.insert(operand);
        if (*next && (*next != ',' || !next[1])) return nullptr;
        cursor = *next ? next + 1 : next;
    }
    for (const auto chunk : chunks) {
        DWORD old;
        if (!VirtualProtect(image + chunk, 65536, PAGE_EXECUTE_READ, &old)) return nullptr;
    }
    return image;
}

