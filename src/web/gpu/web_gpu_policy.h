#pragma once
#include <d3d9.h>
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <unordered_map>
#include <vector>

namespace webgpu::policy {
inline std::vector<unsigned> CompactVertexStreams(unsigned mask) {
    std::vector<unsigned> streams;
    for (unsigned i=0; i<4; ++i) if (mask & (1u << i)) streams.push_back(i);
    return streams;
}
inline std::array<DWORD, 256> CanonicalState(const DWORD *input, bool depth) {
    std::array<DWORD, 256> out{};
    for (auto s : {D3DRS_CULLMODE, D3DRS_COLORWRITEENABLE}) out[s] = input[s];
    if (input[D3DRS_ALPHABLENDENABLE]) {
        out[D3DRS_ALPHABLENDENABLE] = 1;
        for (auto s : {D3DRS_SRCBLEND, D3DRS_DESTBLEND, D3DRS_BLENDOP}) out[s] = input[s];
        out[D3DRS_SEPARATEALPHABLENDENABLE] = !!input[D3DRS_SEPARATEALPHABLENDENABLE];
        if (out[D3DRS_SEPARATEALPHABLENDENABLE])
            for (auto s : {D3DRS_SRCBLENDALPHA, D3DRS_DESTBLENDALPHA, D3DRS_BLENDOPALPHA}) out[s] = input[s];
    }
    if (depth) {
        out[D3DRS_ZENABLE] = !!input[D3DRS_ZENABLE];
        out[D3DRS_ZWRITEENABLE] = input[D3DRS_ZENABLE] && input[D3DRS_ZWRITEENABLE];
        out[D3DRS_ZFUNC] = input[D3DRS_ZENABLE] ? input[D3DRS_ZFUNC] : D3DCMP_ALWAYS;
        // IEEE negative zero does not change the pipeline descriptor.
        for (auto s : {D3DRS_DEPTHBIAS, D3DRS_SLOPESCALEDEPTHBIAS}) out[s] = input[s] == 0x80000000u ? 0 : input[s];
        if (input[D3DRS_STENCILENABLE]) {
            out[D3DRS_STENCILENABLE] = 1;
            for (auto s : {D3DRS_STENCILFUNC, D3DRS_STENCILFAIL, D3DRS_STENCILZFAIL,
                          D3DRS_STENCILPASS, D3DRS_STENCILMASK, D3DRS_STENCILWRITEMASK}) out[s] = input[s];
            out[D3DRS_TWOSIDEDSTENCILMODE] = !!input[D3DRS_TWOSIDEDSTENCILMODE];
            if (out[D3DRS_TWOSIDEDSTENCILMODE])
                for (auto s : {D3DRS_CCW_STENCILFUNC, D3DRS_CCW_STENCILFAIL,
                              D3DRS_CCW_STENCILZFAIL, D3DRS_CCW_STENCILPASS}) out[s] = input[s];
        }
    }
    return out;
}
struct Range { uint64_t id = 0, begin = 0, end = 0; };
inline bool Overlaps(const Range &a, const Range &b) {
    return a.id == b.id && a.begin < b.end && b.begin < a.end;
}
// A later write can move ahead of draws only when it changes no earlier read
// and cannot be overwritten by an earlier write that must remain ordered.
struct UploadOrder {
    std::unordered_map<uint64_t, std::vector<Range>> reads, ordered;
    void Read(Range r) {
        if (!r.id || r.end <= r.begin) return;
        auto &list = reads[r.id];
        if (list.empty()) list.push_back(r);
        else { list[0].begin = std::min(list[0].begin, r.begin); list[0].end = std::max(list[0].end, r.end); }
    }
    Range ReadSpan(uint64_t id) const {
        auto it = reads.find(id);
        return it == reads.end() || it->second.empty() ? Range{} : it->second[0];
    }
    bool Hoist(Range r, Range changed, bool paddingUnchanged) {
        auto remember = [&] { auto &v = ordered[r.id]; if (v.empty()) v.push_back(r); else { v[0].begin = std::min(v[0].begin, r.begin); v[0].end = std::max(v[0].end, r.end); } };
        for (const auto &old : reads[r.id]) if (Overlaps(old, changed) || (Overlaps(old, r) && !paddingUnchanged)) { remember(); return false; }
        for (const auto &old : ordered[r.id]) if (Overlaps(old, r)) { remember(); return false; }
        return true;
    }
    bool Hoist(Range r) { return Hoist(r, r, false); }
};
// Alignment padding may overlap an earlier read only if a previous immutable
// hoisted snapshot proves those exact bytes are unchanged. Logical writes must
// still be disjoint; an ordered conflicting update always prevents hoisting.
inline bool PaddingUnchanged(Range previous, const uint8_t *before, Range upload,
    const uint8_t *after, Range changed, Range read) {
    for (auto padding : {Range{upload.id,upload.begin,changed.begin}, Range{upload.id,changed.end,upload.end}}) {
        if (!Overlaps(padding, read)) continue;
        const uint64_t begin = std::max(padding.begin,read.begin), end = std::min(padding.end,read.end);
        if (!before || previous.id != upload.id || previous.begin > begin || previous.end < end
            || memcmp(before + begin - previous.begin, after + begin - upload.begin, size_t(end - begin))) return false;
    }
    return true;
}
inline std::vector<Range> MergeRanges(std::vector<Range> ranges) {
    std::sort(ranges.begin(), ranges.end(), [](auto a, auto b) { return a.id < b.id || (a.id == b.id && a.begin < b.begin); });
    std::vector<Range> result;
    for (auto r : ranges) {
        if (!result.empty() && r.id == result.back().id && r.begin <= result.back().end)
            result.back().end = std::max(result.back().end, r.end);
        else result.push_back(r);
    }
    return result;
}
}
