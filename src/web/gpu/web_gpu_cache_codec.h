#pragma once
#include "web_gpu_policy.h"
#include <web/gl/web_sm3.h>
#include <cstring>
#include <string>

namespace webgpu::cache {
constexpr size_t MapLimit = 16u << 20;
constexpr uint32_t Version = 2;
struct Recipe {
    std::string vs, ps;
    std::vector<uint8_t> key;
    std::array<DWORD, 256> state{};
    std::vector<D3DVERTEXELEMENT9> decl;
    std::array<unsigned, 4> strides{};
    std::array<sm3::Result, 2> info;
    uint32_t color = 0, depth = 0, depthMask = 0, topology = 0, strip = 0;
};
inline uint64_t Checksum(const uint8_t *p, size_t n) {
    uint64_t h = 14695981039346656037ull;
    for (size_t i = 0; i < n; ++i) { h ^= p[i]; h *= 1099511628211ull; }
    return h;
}
struct Writer {
    std::vector<uint8_t> bytes;
    void Data(const void *p, size_t n) { if (!n) return; const auto *b = static_cast<const uint8_t *>(p); bytes.insert(bytes.end(), b, b + n); }
    template<class T> void Value(const T &v) { Data(&v, sizeof v); }
    void Blob(const void *p, size_t n) { Value(uint32_t(n)); Data(p, n); }
};
struct Reader {
    const uint8_t *p; size_t left; bool ok = true;
    bool Data(void *out, size_t n) { if (n > left) { ok = false; return false; } if (n) memcpy(out, p, n); p += n; left -= n; return true; }
    template<class T> T Value() { T v{}; Data(&v, sizeof v); return v; }
    std::vector<uint8_t> Blob(size_t cap) { const auto n = Value<uint32_t>(); if (n > cap || n > left) { ok = false; return {}; } std::vector<uint8_t> b(n); Data(b.data(), n); return b; }
};
inline std::vector<uint8_t> Encode(const std::vector<Recipe> &recipes) {
    Writer w; w.Value(uint32_t(0x4b475243)); w.Value(Version);
    std::vector<std::string> sources; std::unordered_map<std::string, uint32_t> ids;
    for (auto &r : recipes) for (auto *source : {&r.vs, &r.ps}) if (!ids.count(*source)) { ids[*source] = uint32_t(sources.size()); sources.push_back(*source); }
    w.Value(uint32_t(sources.size()));
    for (auto &s : sources) { w.Blob(s.data(), s.size()); if (w.bytes.size() > MapLimit - 8) return {}; }
    w.Value(uint32_t(recipes.size()));
    for (auto &r : recipes) {
        w.Value(ids.at(r.vs)); w.Value(ids.at(r.ps)); w.Blob(r.key.data(), r.key.size());
        w.Data(r.state.data(), sizeof r.state); w.Blob(r.decl.data(), r.decl.size() * sizeof(D3DVERTEXELEMENT9));
        w.Data(r.strides.data(), sizeof r.strides);
        for (auto &info : r.info) {
            w.Value(uint32_t(info.floatCount)); w.Value(uint32_t(info.samplers.size()));
            for (auto &s : info.samplers) { w.Value(uint32_t(s.reg)); w.Value(uint32_t(s.type)); w.Value(uint32_t(s.proj)); w.Value(uint32_t(s.raw)); }
        }
        for (auto v : {r.color,r.depth,r.depthMask,r.topology,r.strip}) w.Value(v);
        if (w.bytes.size() > MapLimit - 8) return {};
    }
    const uint64_t sum = Checksum(w.bytes.data(), w.bytes.size()); w.Value(sum); return w.bytes;
}
inline bool Decode(const uint8_t *p, size_t n, std::vector<Recipe> &out) {
    out.clear(); if (!p || n < 24 || n > MapLimit) return false;
    uint64_t sum; memcpy(&sum, p + n - 8, 8); if (sum != Checksum(p, n - 8)) return false;
    Reader reader{p, n - 8};
    if (reader.Value<uint32_t>() != 0x4b475243 || reader.Value<uint32_t>() != Version) return false;
    const uint32_t sourceCount = reader.Value<uint32_t>(); if (sourceCount > 8192) return false;
    std::vector<std::string> sources;
    for (uint32_t i = 0; i < sourceCount; ++i) { auto b = reader.Blob(1u << 20); if (!reader.ok || b.empty()) return false; sources.emplace_back(b.begin(), b.end()); }
    const uint32_t count = reader.Value<uint32_t>(); if (count > 4096) return false;
    std::vector<Recipe> result;
    size_t expandedBytes = 0;
    for (uint32_t i = 0; i < count; ++i) {
        Recipe r; const auto vs = reader.Value<uint32_t>(), ps = reader.Value<uint32_t>();
        if (vs >= sources.size() || ps >= sources.size()) return false;
        // The on-disk sources are deduplicated. Bound their expanded in-memory
        // representation too, even for a valid-checksum malicious cache.
        expandedBytes += sources[vs].size() + sources[ps].size() + sizeof(Recipe);
        if (expandedBytes > (32u << 20)) return false;
        r.vs = sources[vs]; r.ps = sources[ps]; r.key = reader.Blob(2048); if (r.key.empty()) return false;
        reader.Data(r.state.data(), sizeof r.state);
        auto decl = reader.Blob(32 * sizeof(D3DVERTEXELEMENT9)); if (decl.size() % sizeof(D3DVERTEXELEMENT9)) return false;
        r.decl.resize(decl.size() / sizeof(D3DVERTEXELEMENT9)); if (!decl.empty()) memcpy(r.decl.data(), decl.data(), decl.size());
        reader.Data(r.strides.data(), sizeof r.strides);
        for (auto &v : r.decl) if (v.Stream >= 4 || v.Type > D3DDECLTYPE_FLOAT16_4 || !r.strides[v.Stream] || r.strides[v.Stream] > 2048 || v.Offset >= r.strides[v.Stream]) return false;
        for (auto &info : r.info) {
            info.floatCount = reader.Value<uint32_t>(); const uint32_t samplers = reader.Value<uint32_t>();
            if (info.floatCount > 256 || samplers > 16) return false;
            for (uint32_t j = 0; j < samplers; ++j) {
                sm3::Sampler s; s.reg = reader.Value<uint32_t>(); s.type = reader.Value<uint32_t>();
                s.proj = reader.Value<uint32_t>(); s.raw = reader.Value<uint32_t>();
                if (s.reg < 0 || s.reg >= 16 || s.type < 2 || s.type > 4) return false;
                info.samplers.push_back(s);
            }
        }
        r.color = reader.Value<uint32_t>(); r.depth = reader.Value<uint32_t>(); r.depthMask = reader.Value<uint32_t>();
        r.topology = reader.Value<uint32_t>(); r.strip = reader.Value<uint32_t>();
        if (!reader.ok || r.topology < D3DPT_POINTLIST || r.topology > D3DPT_TRIANGLESTRIP || r.strip > 2) return false;
        result.push_back(std::move(r));
    }
    if (!reader.ok || reader.left) return false;
    out = std::move(result); return true;
}
}
