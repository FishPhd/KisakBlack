#pragma once
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_set>

namespace webgpu {
// Share each shader independently: many vertex/pixel combinations reuse one
// side. Pair-level sharing still retained a copy for every combination.
class SourcePool {
    using Source = std::shared_ptr<const std::string>;
    struct Hash { size_t operator()(const Source &s) const { return std::hash<std::string_view>{}(*s); } };
    struct Equal { bool operator()(const Source &a, const Source &b) const { return *a == *b; } };
    std::unordered_set<Source, Hash, Equal> sources;
    std::mutex mutex;
public:
    void Prune() {
        std::lock_guard lock(mutex);
        for (auto it=sources.begin(); it!=sources.end();)
            if (it->use_count() == 1) it=sources.erase(it); else ++it;
    }
    Source Intern(std::string source) {
        auto candidate = std::make_shared<const std::string>(std::move(source));
        std::lock_guard lock(mutex);
        return *sources.insert(std::move(candidate)).first;
    }
};
struct ShaderSources { std::shared_ptr<const std::string> vertex, pixel; };
}
