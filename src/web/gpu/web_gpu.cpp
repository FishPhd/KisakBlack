// Dawn C++ executor. The D3D producer never owns WebGPU handles and never
// lends engine pointers to the executor. Uploads and draw state are snapshots.
#include "web_gpu.h"
#include "web_gpu_policy.h"
#include "web_gpu_cache_codec.h"
#include "web_gpu_sources.h"
#include "web_gpu_shader_interface.h"
#include "web_gpu_settings.h"
#include <web/gl/web_d3d9_device.h>
#include <web/web_assets.h>
#include <web/web_graphics.h>
#include <web/web_preparation_policy.h>
#include <gfx_d3d/r_state.h>
#include <gfx_d3d/r_shade.h>
#include <gfx_d3d/r_draw_method.h>
#include <gfx_d3d/r_draw_material.h>
#include <gfx_d3d/r_dvars.h>
#include <gfx_d3d/r_rendertarget.h>
#include <gfx_d3d/r_image.h>
#include <gfx_d3d/r_rendercmds.h>
#include <gfx_d3d/r_material.h>
#include <gfx_d3d/rb_backend.h>
#include <database/db_registry.h>
#include <database/db_file_load.h>
#include <client_mp/cl_input_mp.h>
#include <client_mp/cl_scrn_mp.h>
#include <set>
#include <xanim/xmodel.h>
#include <bgame/bg_weapons.h>
#include <xanim/xanim.h>
#include <emscripten.h>
#include <emscripten/threading.h>
#include <emscripten/html5.h>
#include <emscripten/heap.h>
#include <emscripten/proxying.h>
#include <unistd.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <condition_variable>
#include <cstring>
#include <deque>
#include <functional>
#include <memory>
#include <sstream>
#include <thread>
#include <unordered_map>
#include <unordered_set>
extern int g_layerDataStride[18], g_stream2Stride[18];
#ifdef KISAK_WEB_WEBGPU
#include <webgpu/webgpu_cpp.h>
#endif

namespace webgpu {
#ifndef KISAK_WEB_WEBGPU
bool Requested() { return false; }
bool Start(unsigned, unsigned) { return false; }
void Resize(unsigned, unsigned) {}
void Draw(kd::Device &, D3DPRIMITIVETYPE, unsigned, int, unsigned, const void *, unsigned, unsigned, const void *, D3DFORMAT, unsigned, unsigned) {}
void WarmMap(const char *, int) {}
void WarmFrontend() {}
void CheckPreparation() {}
void NotifyPreparationAsset(int, const char *) {}
void BeginMapPreparation() {}
void CancelWarmup() {}
bool Settling() { return false; }
void Clear(kd::Device &, DWORD, const D3DRECT *, DWORD, D3DCOLOR, float, DWORD) {}
void Fill(kd::KSurface *, const RECT *, D3DCOLOR) {}
void Copy(kd::KSurface *, const RECT *, kd::KSurface *, const RECT *, D3DTEXTUREFILTERTYPE) {}
void Present(kd::Device &) {}
void Release(uint64_t) {}
#else
namespace {
constexpr uint64_t QueueLimit = 64ull << 20;
constexpr size_t UniformSize = 256 * 16 + 16 * 16 * 2 + 32;
constexpr size_t UniformTailSize = UniformSize - 256 * 16;
constexpr size_t UniformCapacity = 16 << 20;
std::atomic<uint64_t> nextId{1};
std::atomic<int> started{0};
std::atomic<bool> failed{false};
std::atomic<bool> requested{false};
std::atomic<bool> gpuValidation{false};
std::atomic<bool> profiling{false}, callbackWake{true}, mergeDepth{true}, hoistUploads{true};
std::atomic<bool> stableTextureBindings{true};
std::atomic<bool> captureRecipes{false}, bestCasePreparation{false}, enumerateRecipes{false};
std::atomic<bool> submissionWake{false}, submissionWakeQueued{false};
std::atomic<bool> pipelineNostall{true};
std::atomic<unsigned> runtimeJobs{8};
std::atomic<bool> settling{false}, settleCancel{false}, mapReadyForSettle{false}, settleStarted{false}, settleReportedDone{false};
std::atomic<double> settleBegan{0};
double settleReportAt = 0;
pthread_t ownerThread;
em_proxying_queue *ownerQueue = nullptr;
void WakeSubmitted();
std::atomic<int> preparationMode{2};
std::atomic<unsigned> preparationJobs{8};
std::mutex dependencyMu;
std::set<std::pair<int, std::string>> pendingDependencies;
uint64_t mapGeneration = 0, preparedGeneration = ~uint64_t(0);
std::string preparedMap;
std::mutex queueMu;
std::condition_variable queueSpace;
unsigned width, height;
std::string session;

EM_JS(void, report, (const char *id, const char *kind, double value), {
    const name = UTF8ToString(kind);
    if (!Module.kisakGpuStats) Module.kisakGpuStats = new BroadcastChannel('kisak-stats-' + UTF8ToString(id));
    Module.kisakGpuStats.postMessage({gpu: {[name]: value}, renderer: 'webgpu'});
});
EM_JS(void, reportJson, (const char *id, const char *json), {
    if (!Module.kisakGpuStats) Module.kisakGpuStats = new BroadcastChannel('kisak-stats-' + UTF8ToString(id));
    Module.kisakGpuStats.postMessage(JSON.parse(UTF8ToString(json)));
});
void Fail(const std::string &message) {
    if (!failed.exchange(true)) fprintf(stderr, "WEB_GPU_FALLBACK %s\n", message.c_str());
    started.store(-1); queueSpace.notify_all();
}
void LogShaderError(std::string message) {
    if (message.empty() || message.find("previous error") != std::string::npos) return;
    if (message.size() > 480) message.resize(480);
    static std::mutex mu;
    static std::unordered_set<std::string> seen;
    std::lock_guard lock(mu);
    if (seen.size() >= 8 || !seen.insert(message).second) return;
    fprintf(stderr, "WEB_GPU_SHADER %s\n", message.c_str());
}
uint64_t Id(uint64_t &id) { if (!id) id = nextId.fetch_add(1); return id; }
uint64_t Align(uint64_t n, uint64_t a) { return (n + a - 1) / a * a; }
uint64_t Fnv(const void *data, size_t n, uint64_t hash = 14695981039346656037ull) {
    const auto *bytes = static_cast<const uint8_t *>(data);
    for (size_t i = 0; i < n; ++i) { hash ^= bytes[i]; hash *= 1099511628211ull; }
    return hash ? hash : 1;
}
template<class Shader> uint64_t ShaderHash(Shader &shader) {
    static std::mutex mu; std::lock_guard lock(mu);
    if (shader.gpuHash) return shader.gpuHash;
    uint64_t hash = 14695981039346656037ull;
    const auto *bytes = reinterpret_cast<const uint8_t *>(shader.code.data());
    for (size_t i = 0; i < shader.code.size() * 4; ++i) { hash ^= bytes[i]; hash *= 1099511628211ull; }
    return shader.gpuHash = hash ? hash : 1;
}
template<class Shader> const sm3::Result *ShaderInfo(Shader &shader) {
    static std::map<uint64_t, sm3::Result> cache;
    static std::mutex mu; std::lock_guard lock(mu);
    const auto hash = ShaderHash(shader);
    auto it = cache.find(hash);
    if (it == cache.end()) {
        sm3::Result info;
        info.inputs = shader.tr.inputs; info.outputs = shader.tr.outputs; info.samplers = shader.tr.samplers;
        info.floatCount = shader.tr.floatCount; info.relativeConst = shader.tr.relativeConst;
        info.writesDepth = shader.tr.writesDepth;
        it = cache.emplace(hash, std::move(info)).first;
    }
    return &it->second;
}
wgpu::CompareFunction Compare(DWORD c) {
    static const wgpu::CompareFunction map[] = {wgpu::CompareFunction::Always,
        wgpu::CompareFunction::Never, wgpu::CompareFunction::Less, wgpu::CompareFunction::Equal,
        wgpu::CompareFunction::LessEqual, wgpu::CompareFunction::Greater, wgpu::CompareFunction::NotEqual,
        wgpu::CompareFunction::GreaterEqual, wgpu::CompareFunction::Always};
    return map[std::min<DWORD>(c, 8)];
}
wgpu::BlendFactor Blend(DWORD b) {
    switch (b) {
    case D3DBLEND_ZERO: return wgpu::BlendFactor::Zero;
    case D3DBLEND_ONE: return wgpu::BlendFactor::One;
    case D3DBLEND_SRCCOLOR: return wgpu::BlendFactor::Src;
    case D3DBLEND_INVSRCCOLOR: return wgpu::BlendFactor::OneMinusSrc;
    case D3DBLEND_SRCALPHA: return wgpu::BlendFactor::SrcAlpha;
    case D3DBLEND_INVSRCALPHA: return wgpu::BlendFactor::OneMinusSrcAlpha;
    case D3DBLEND_DESTALPHA: return wgpu::BlendFactor::DstAlpha;
    case D3DBLEND_INVDESTALPHA: return wgpu::BlendFactor::OneMinusDstAlpha;
    case D3DBLEND_DESTCOLOR: return wgpu::BlendFactor::Dst;
    case D3DBLEND_INVDESTCOLOR: return wgpu::BlendFactor::OneMinusDst;
    case D3DBLEND_SRCALPHASAT: return wgpu::BlendFactor::SrcAlphaSaturated;
    case D3DBLEND_BLENDFACTOR: return wgpu::BlendFactor::Constant;
    case D3DBLEND_INVBLENDFACTOR: return wgpu::BlendFactor::OneMinusConstant;
    default: return wgpu::BlendFactor::One;
    }
}
wgpu::BlendOperation BlendOp(DWORD b) {
    switch (b) { case D3DBLENDOP_SUBTRACT: return wgpu::BlendOperation::Subtract;
    case D3DBLENDOP_REVSUBTRACT: return wgpu::BlendOperation::ReverseSubtract;
    case D3DBLENDOP_MIN: return wgpu::BlendOperation::Min;
    case D3DBLENDOP_MAX: return wgpu::BlendOperation::Max; default: return wgpu::BlendOperation::Add; }
}
wgpu::StencilOperation Stencil(DWORD b) {
    switch (b) { case D3DSTENCILOP_ZERO: return wgpu::StencilOperation::Zero;
    case D3DSTENCILOP_REPLACE: return wgpu::StencilOperation::Replace;
    case D3DSTENCILOP_INCRSAT: return wgpu::StencilOperation::IncrementClamp;
    case D3DSTENCILOP_DECRSAT: return wgpu::StencilOperation::DecrementClamp;
    case D3DSTENCILOP_INVERT: return wgpu::StencilOperation::Invert;
    case D3DSTENCILOP_INCR: return wgpu::StencilOperation::IncrementWrap;
    case D3DSTENCILOP_DECR: return wgpu::StencilOperation::DecrementWrap;
    default: return wgpu::StencilOperation::Keep; }
}
wgpu::TextureFormat Format(D3DFORMAT f) {
    switch (f) {
    case D3DFMT_DXT1: return wgpu::TextureFormat::BC1RGBAUnorm;
    case D3DFMT_DXT2: case D3DFMT_DXT3: return wgpu::TextureFormat::BC2RGBAUnorm;
    case D3DFMT_DXT4: case D3DFMT_DXT5: return wgpu::TextureFormat::BC3RGBAUnorm;
    case D3DFMT_A8: case D3DFMT_L8: return wgpu::TextureFormat::R8Unorm;
    case D3DFMT_A8L8: return wgpu::TextureFormat::RG8Unorm;
    case D3DFMT_L16: return wgpu::TextureFormat::R32Float;
    case D3DFMT_G16R16: return wgpu::TextureFormat::RG32Float;
    case D3DFMT_A16B16G16R16: return wgpu::TextureFormat::RGBA32Float;
    case D3DFMT_R16F: return wgpu::TextureFormat::R16Float;
    case D3DFMT_G16R16F: return wgpu::TextureFormat::RG16Float;
    case D3DFMT_A16B16G16R16F: return wgpu::TextureFormat::RGBA16Float;
    case D3DFMT_R32F: return wgpu::TextureFormat::R32Float;
    case D3DFMT_G32R32F: return wgpu::TextureFormat::RG32Float;
    case D3DFMT_A32B32G32R32F: return wgpu::TextureFormat::RGBA32Float;
    case D3DFMT_V8U8: return wgpu::TextureFormat::RG8Snorm;
    case D3DFMT_Q8W8V8U8: return wgpu::TextureFormat::RGBA8Snorm;
    case D3DFMT_A2B10G10R10: case D3DFMT_A2R10G10B10: return wgpu::TextureFormat::RGB10A2Unorm;
    default: { auto *fi = kd::FindFmt(f); if (fi && fi->depth) return fi->stencil ? wgpu::TextureFormat::Depth24PlusStencil8 : wgpu::TextureFormat::Depth32Float; }
    }
    return wgpu::TextureFormat::RGBA8Unorm;
}
bool Float32Texture(wgpu::TextureFormat f) {
    return f == wgpu::TextureFormat::R32Float || f == wgpu::TextureFormat::RG32Float || f == wgpu::TextureFormat::RGBA32Float;
}
struct Texture {
    wgpu::Texture handle;
    wgpu::TextureFormat format;
    unsigned w, h, layers, levels, type;
    bool depth, stencil;
    uint64_t generation = 1;
    std::map<uint64_t, wgpu::TextureView> views;
    wgpu::TextureView View(unsigned level = 0, unsigned face = 0, bool attachment = false) {
        const uint64_t key = level | (uint64_t(face) << 16) | (uint64_t(attachment) << 32);
        auto it = views.find(key); if (it != views.end()) return it->second;
        wgpu::TextureViewDescriptor d{};
        d.dimension = attachment ? wgpu::TextureViewDimension::e2D : type == 1 ? wgpu::TextureViewDimension::Cube : type == 2 ? wgpu::TextureViewDimension::e3D : wgpu::TextureViewDimension::e2D;
        d.baseMipLevel = level; d.mipLevelCount = attachment ? 1 : levels - level;
        d.baseArrayLayer = face; d.arrayLayerCount = attachment ? 1 : type == 1 ? 6 : 1;
        d.aspect = !attachment && depth ? wgpu::TextureAspect::DepthOnly : wgpu::TextureAspect::All;
        return views[key] = handle.CreateView(&d);
    }
};
struct Surface { uint64_t id = 0; unsigned level = 0, face = 0, w = 0, h = 0; };
struct Uniforms {
    float fc[256 * 4]; int ic[16 * 4]; int bc[16 * 4];
    float at[2]; uint32_t srgbTex, srgbEnc, swzTex, srgbW, bgra, pad;
};
static_assert(sizeof(Uniforms) == UniformSize);
size_t FloatBytes(const sm3::Result &info) { return size_t(std::clamp(info.floatCount, 1, 256)) * 16; }
size_t ConstantSize(const sm3::Result &info) { return FloatBytes(info) + UniformTailSize; }
struct ConstantRef { const uint8_t *data = nullptr; uint32_t size = 0; };
struct ConstantArena {
    struct Chunk { std::unique_ptr<uint8_t[]> data; size_t cap = 0; };
    std::vector<Chunk> chunks;
    int current = -1;
    size_t used = 0;
    uint8_t *Alloc(size_t n) {
        if (current < 0 || used + n > chunks[current].cap) {
            const size_t cap = std::max(n, size_t(1 << 20));
            chunks.push_back(Chunk{std::unique_ptr<uint8_t[]>(new uint8_t[cap]), cap});
            current = int(chunks.size()) - 1;
            used = 0;
        }
        uint8_t *p = chunks[current].data.get() + used;
        used += n;
        return p;
    }
};
struct PrepareData {
    std::string cacheSource, cacheScope;
    std::string_view vs, ps;
    std::shared_ptr<const ShaderSources> sources;
    std::vector<D3DVERTEXELEMENT9> decl;
    std::array<DWORD, 256> rs{};
    std::array<unsigned, 4> strides{};
    sm3::Result vertexInfo, pixelInfo;
    wgpu::TextureFormat colorFormat{}, depthFormat{};
    uint32_t depthMask = 0, stripFormat = 0;
    D3DPRIMITIVETYPE topology{};
    uint64_t id = 0, hash = 0;
    std::vector<uint8_t> key;
};
std::mutex recipeMu;
SourcePool sourcePool;
std::shared_ptr<const ShaderSources> ShareSources(std::string vertex, std::string pixel) {
    return std::make_shared<const ShaderSources>(sourcePool.Intern(std::move(vertex)), sourcePool.Intern(std::move(pixel)));
}
std::unordered_map<uint64_t, std::vector<std::shared_ptr<const PrepareData>>> recipeRegistry;
std::unordered_map<std::string, std::shared_ptr<const ShaderSources>> translations;
std::shared_ptr<const PrepareData> MakeRecipe(kd::KVShader &vs, kd::KPShader &ps,
    const std::vector<D3DVERTEXELEMENT9> &decl, const std::array<unsigned, 4> &strides,
    const DWORD *rs, wgpu::TextureFormat color, wgpu::TextureFormat depth,
    uint32_t depthMask, D3DPRIMITIVETYPE topology, unsigned stripFormat, double *translationMs = nullptr) {
    const auto *vi = ShaderInfo(vs), *pi = ShaderInfo(ps);
    const auto state = policy::CanonicalState(rs, depth != wgpu::TextureFormat::Undefined);
    std::array<D3DVERTEXELEMENT9, 32> used{}; size_t usedCount = 0;
    std::array<unsigned, 4> usedStrides{};
    for (auto v : decl) {
        if (v.Stream >= 4) continue;
        const int loc = sm3::AttribLocation(v.Usage, v.UsageIndex);
        for (auto &input : vi->inputs) if (loc >= 0 && sm3::AttribLocation(input.usage, input.index) == loc) {
            if (usedCount == used.size()) return {};
            used[usedCount++] = v; usedStrides[v.Stream] = strides[v.Stream]; break;
        }
    }
    std::array<uint8_t, 2048> key{}; size_t n = 0;
    auto append = [&](const void *p, size_t size) { if (n + size > key.size()) return false; memcpy(key.data() + n, p, size); n += size; return true; };
    const uint64_t hashes[] = {ShaderHash(vs), ShaderHash(ps)};
    append(hashes, sizeof hashes); append(&depthMask, sizeof depthMask);
    for (auto s : {D3DRS_CULLMODE, D3DRS_COLORWRITEENABLE, D3DRS_ALPHABLENDENABLE,
        D3DRS_SRCBLEND,D3DRS_DESTBLEND,D3DRS_BLENDOP,D3DRS_SEPARATEALPHABLENDENABLE,
        D3DRS_SRCBLENDALPHA,D3DRS_DESTBLENDALPHA,D3DRS_BLENDOPALPHA,D3DRS_ZENABLE,
        D3DRS_ZWRITEENABLE,D3DRS_ZFUNC,D3DRS_DEPTHBIAS,D3DRS_SLOPESCALEDEPTHBIAS,
        D3DRS_STENCILENABLE,D3DRS_STENCILFUNC,D3DRS_STENCILFAIL,D3DRS_STENCILZFAIL,
        D3DRS_STENCILPASS,D3DRS_STENCILMASK,D3DRS_STENCILWRITEMASK,D3DRS_TWOSIDEDSTENCILMODE,
        D3DRS_CCW_STENCILFUNC,D3DRS_CCW_STENCILFAIL,D3DRS_CCW_STENCILZFAIL,D3DRS_CCW_STENCILPASS}) append(&state[s], 4);
    if (!append(used.data(), usedCount * sizeof(D3DVERTEXELEMENT9))) return {};
    append(usedStrides.data(), sizeof usedStrides); append(&color, sizeof color); append(&depth, sizeof depth);
    append(&topology, sizeof topology); append(&stripFormat, sizeof stripFormat);
    const uint64_t hash = Fnv(key.data(), n);
    std::lock_guard lock(recipeMu);
    auto &bucket = recipeRegistry[hash];
    for (auto &p : bucket) if (p->key.size() == n && !memcmp(p->key.data(), key.data(), n)) return p;
    auto p = std::make_shared<PrepareData>();
    p->id = nextId.fetch_add(1); p->hash = hash; p->key.assign(key.data(), key.data() + n);
    p->decl.assign(used.data(), used.data() + usedCount); p->strides = usedStrides; p->rs = state;
    p->vertexInfo = PipelineInterface(*vi); p->pixelInfo = PipelineInterface(*pi); p->depthMask = depthMask;
    p->colorFormat = color; p->depthFormat = depth; p->topology = topology; p->stripFormat = stripFormat;
    std::string error; int vertexTypes[16]{};
    for (auto &v : p->decl) {
        const int loc = sm3::AttribLocation(v.Usage, v.UsageIndex); if (loc < 0 || loc >= 16) continue;
        const int components = v.Type <= D3DDECLTYPE_FLOAT4 ? v.Type + 1 : v.Type == D3DDECLTYPE_SHORT2 || v.Type == D3DDECLTYPE_SHORT2N || v.Type == D3DDECLTYPE_USHORT2N || v.Type == D3DDECLTYPE_FLOAT16_2 ? 2 : 4;
        vertexTypes[loc] = components + (v.Type == D3DDECLTYPE_UBYTE4 ? 100 : v.Type == D3DDECLTYPE_SHORT2 || v.Type == D3DDECLTYPE_SHORT4 ? 200 : 0);
    }
    const double begin = emscripten_get_now();
    std::string translationKey(reinterpret_cast<const char *>(hashes), sizeof hashes);
    translationKey.append(reinterpret_cast<const char *>(&depthMask), sizeof depthMask);
    translationKey.append(reinterpret_cast<const char *>(vertexTypes), sizeof vertexTypes);
    auto found = translations.find(translationKey);
    if (found == translations.end()) {
        std::string vertexSource, pixelSource;
        if (!sm3::TranslateWgsl(vs.code.data(), *pi, 0, vertexSource, error, vertexTypes) || !sm3::TranslateWgsl(ps.code.data(), *pi, depthMask, pixelSource, error)) return {};
        found = translations.emplace(std::move(translationKey), ShareSources(std::move(vertexSource), std::move(pixelSource))).first;
    }
    p->sources = found->second; p->vs = *p->sources->vertex; p->ps = *p->sources->pixel;
    if (translationMs) *translationMs += emscripten_get_now() - begin;
    bucket.push_back(p); return p;
}
struct WarmRequest {
    uint32_t token = 0;
    std::string key;
    std::string profile, map;
    double began = emscripten_get_now();
    bool catalog = false;
    bool capture = false;
    std::vector<std::shared_ptr<const PrepareData>> recipes;
    std::set<std::vector<uint8_t>> seen;
    std::unordered_set<const void *> sources;
    size_t recipeBytes = 0;
    std::atomic<bool> collecting{true}, done{false}, cancel{false};
    std::atomic<size_t> count{0};
    bool cacheRequested = false, cacheReady = false;
    size_t cursor = 0, completed = 0, compileErrors = 0;
    std::deque<uint64_t> pendingPipelines;
    double cacheAt = 0;
    size_t failures = 0;
    std::atomic<size_t> rejected{0};
    std::atomic<uint64_t> uploadedBytes{0}, images{0}, buffers{0};
    std::atomic<size_t> utilityTotal{0}, utilityCompleted{0}, utilityFailures{0};
    double progressAt = 0;
    size_t lastCompleted = 0;
    bool cacheOk = true, pruned = false;
    bool catalogUsed = false;
    bool draining = false;
    double drainAt = 0;
    std::atomic<bool> hiddenDrawsDone{true};
    std::shared_ptr<std::atomic<bool>> uploadFence = std::make_shared<std::atomic<bool>>(false);
};
std::mutex warmMu;
std::shared_ptr<WarmRequest> warmRequest;
std::mutex lateRecipeMu;
std::vector<std::shared_ptr<const PrepareData>> lateRecipes;
thread_local WarmRequest *harvestWarm = nullptr;
void QueueLateRecipe(std::shared_ptr<const PrepareData> recipe) {
    if (!recipe) return;
    std::lock_guard lock(lateRecipeMu);
    if (lateRecipes.size() < 4096) lateRecipes.push_back(std::move(recipe));
}
bool catalogReady = false;
std::string catalogProfile;
std::atomic<uint32_t> warmSequence{0};
std::atomic<bool> loadingWarm{false};
void AddWarmRecipe(WarmRequest &r, std::shared_ptr<const PrepareData> recipe) {
    if (preparationMode == 1 && r.recipes.size() >= 4096) { if (recipe && !r.seen.count(recipe->key)) ++r.rejected; return; }
    if (!recipe || r.seen.count(recipe->key)) return;
    size_t bytes = sizeof(PrepareData) + recipe->key.size() + recipe->decl.size()*sizeof(D3DVERTEXELEMENT9);
    if (!r.sources.count(recipe->sources->vertex.get())) bytes += recipe->vs.size();
    if (!r.sources.count(recipe->sources->pixel.get())) bytes += recipe->ps.size();
    if (r.recipeBytes + bytes > (128u << 20) || r.recipes.size() >= 65536) { ++r.rejected; r.cacheOk = false; return; }
    r.recipeBytes += bytes;
    r.sources.insert(recipe->sources->vertex.get()); r.sources.insert(recipe->sources->pixel.get());
    r.seen.insert(recipe->key); r.recipes.push_back(std::move(recipe)); r.count = r.recipes.size();
}
EM_JS(void, cacheChannel, (const char *id), {
    if (Module.kisakGpuCache) return;
    const bc = Module.kisakGpuCache = new BroadcastChannel('kisak-gpu-cache-' + UTF8ToString(id));
    bc.onmessage = ({data}) => {
        if (data.kind !== 'loaded') return;
        const bytes = data.bytes;
        if (!(bytes instanceof Uint8Array) || bytes.length > 16777216) return;
        const source = new TextEncoder().encode(data.sha256 || '');
        const ptr = _malloc(bytes.length + source.length + 1);
        if (!ptr) { Module._Kisak_GpuCacheRestore(0, 0, data.token, true, false, 0); return; }
        HEAPU8.set(bytes, ptr); HEAPU8.set(source, ptr + bytes.length); HEAPU8[ptr + bytes.length + source.length] = 0;
        const accepted = Module._Kisak_GpuCacheRestore(ptr, bytes.length, data.token, data.final !== false, data.ok !== false, ptr + bytes.length);
        if (!accepted) bc.postMessage({kind:'quarantine',key:data.sourceKey || '',sha256:data.sha256,reason:'native recipe validation failed'});
        if (ptr) _free(ptr);
    };
});
EM_JS(void, cacheLoad, (const char *key, const char *scope, const char *learned, uint32_t token, int catalog), {
    Module.kisakGpuCache?.postMessage({kind:catalog ? 'catalog' : 'load',key:UTF8ToString(key),scope:UTF8ToString(scope),learnedKey:UTF8ToString(learned),token});
});
EM_JS(void, cacheQuarantine, (const char *key, const char *sha, const char *reason), {
    Module.kisakGpuCache?.postMessage({kind:'quarantine',key:UTF8ToString(key),sha256:UTF8ToString(sha),reason:UTF8ToString(reason)});
});
EM_JS(void, cacheSavePart, (const char *id, const char *key, const uint8_t *ptr, unsigned size,
                          unsigned stream, unsigned part, unsigned ticket, unsigned recipes, int last, const char *observed), {
    Module.kisakGpuWriter ||= new BroadcastChannel('kisak-gpu-cache-' + UTF8ToString(id));
    const begin = ptr >>> 0;
    Module.kisakGpuWriter.postMessage({kind:'saveChunk',key:UTF8ToString(key),bytes:HEAPU8.slice(begin,begin+size),
        stream,part,ticket,recipes,final:!!last,observed:part===0?JSON.parse(UTF8ToString(observed)):undefined});
});
struct CacheWriteJob {
    std::string key;
    std::vector<std::shared_ptr<const PrepareData>> recipes;
    std::vector<uint64_t> observed;
};
std::mutex cacheWriterMu;
std::condition_variable cacheWriterCv;
std::deque<CacheWriteJob> cacheWriteJobs;
uint32_t cacheAckTicket = 0;
bool cacheAckOk = false;
std::atomic<bool> cacheWriterStarted{false};

void CacheWriterLoop() {
    uint32_t sequence = 0, ticket = 0;
    for (;;) {
        CacheWriteJob job;
        {
            std::unique_lock lock(cacheWriterMu);
            cacheWriterCv.wait(lock, [] { return !cacheWriteJobs.empty(); });
            job = std::move(cacheWriteJobs.front()); cacheWriteJobs.pop_front();
        }
        const uint32_t stream = ++sequence;
        std::ostringstream observedJson;
        observedJson << '[';
        for (size_t i = 0; i < job.observed.size(); ++i) { if (i) observedJson << ','; observedJson << '"' << job.observed[i] << '"'; }
        observedJson << ']';
        const std::string observed = observedJson.str();
        const double start = emscripten_get_now();
        double encodeMs = 0;
        uint64_t totalBytes = 0;
        unsigned part = 0;
        size_t expanded = 0;
        std::vector<cache::Recipe> chunk;
        // Serialize only a bounded chunk. Wait for the page's disk acknowledgement
        // before sending another, so neither Wasm nor the page buffers the set.
        auto flush = [&](bool last) {
            if (chunk.empty()) return true;
            const double begin = emscripten_get_now();
            auto bytes = cache::Encode(chunk); chunk.clear(); expanded = 0;
            encodeMs += emscripten_get_now() - begin;
            if (bytes.empty()) return false;
            totalBytes += bytes.size();
            const auto current = ++ticket;
            cacheSavePart(session.c_str(), job.key.c_str(), bytes.data(), bytes.size(), stream, part++, current, job.recipes.size(), last, observed.c_str());
            std::unique_lock lock(cacheWriterMu);
            return cacheWriterCv.wait_for(lock, std::chrono::seconds(30), [=] { return cacheAckTicket == current; }) && cacheAckOk;
        };
        bool ok = true;
        for (const auto &recipe : job.recipes) {
            const auto &p = *recipe;
            const size_t bytes = p.vs.size() + p.ps.size() + sizeof(cache::Recipe);
            if (!chunk.empty() && (chunk.size() >= 128 || expanded + bytes > (24u << 20))) {
                if (!flush(false)) { ok = false; break; }
            }
            cache::Recipe r;
            r.vs = p.vs; r.ps = p.ps; r.key = p.key; r.state = p.rs; r.decl = p.decl; r.strides = p.strides;
            r.info = {p.vertexInfo, p.pixelInfo}; r.color = unsigned(p.colorFormat); r.depth = unsigned(p.depthFormat);
            r.depthMask = p.depthMask; r.topology = p.topology; r.strip = p.stripFormat;
            chunk.push_back(std::move(r)); expanded += bytes;
        }
        if (ok) ok = flush(true);
        std::ostringstream event;
        event << "{\"gpuCache\":{\"recipes\":" << job.recipes.size() << ",\"observed\":" << job.observed.size() << ",\"bytes\":" << totalBytes
            << ",\"encodeMs\":" << encodeMs << ",\"wallMs\":" << emscripten_get_now() - start
            << ",\"ok\":" << (ok ? "true" : "false") << "}}";
        reportJson(session.c_str(), event.str().c_str());
        if (!ok) fprintf(stderr, "WEB_GPU_RECIPE_CACHE save incomplete; rendering continues\n");
    }
}
bool RestoreRecipes(const uint8_t *data, size_t size, uint32_t token, bool final, bool ok, const char *source) {
    std::vector<cache::Recipe> recipes;
    const bool valid = !size || cache::Decode(data, size, recipes);
    std::lock_guard lock(warmMu);
    if (!warmRequest || warmRequest->token != token || warmRequest->done || warmRequest->cacheReady) return true;
    if (valid) for (auto &saved : recipes) {
        if (warmRequest->rejected.load()) break;
        const auto hash = Fnv(saved.key.data(), saved.key.size());
        std::shared_ptr<const PrepareData> known;
        {
            std::lock_guard recipeLock(recipeMu);
            for (const auto &existing : recipeRegistry[hash]) if (existing->key == saved.key) { known = existing; break; }
        }
        if (known) { AddWarmRecipe(*warmRequest, std::move(known)); continue; }
        auto p = std::make_shared<PrepareData>();
        p->cacheSource = source ? source : ""; p->cacheScope = warmRequest->key;
        p->sources = ShareSources(std::move(saved.vs), std::move(saved.ps));
        p->vs = *p->sources->vertex; p->ps = *p->sources->pixel;
        p->key = std::move(saved.key); p->hash = Fnv(p->key.data(), p->key.size()); p->id = nextId.fetch_add(1);
        p->rs = saved.state; p->decl = std::move(saved.decl); p->strides = saved.strides;
        p->vertexInfo = std::move(saved.info[0]); p->pixelInfo = std::move(saved.info[1]);
        p->colorFormat = static_cast<wgpu::TextureFormat>(saved.color); p->depthFormat = static_cast<wgpu::TextureFormat>(saved.depth);
        p->depthMask = saved.depthMask; p->topology = static_cast<D3DPRIMITIVETYPE>(saved.topology); p->stripFormat = saved.strip;
        known = p;
        {
            std::lock_guard recipeLock(recipeMu);
            auto &bucket = recipeRegistry[p->hash];
            for (const auto &existing : bucket) if (existing->key == p->key) { known = existing; break; }
            if (known == p) bucket.push_back(p);
        }
        AddWarmRecipe(*warmRequest, std::move(known));
    }
    warmRequest->cacheOk &= valid && ok;
    warmRequest->catalogUsed = preparationMode == 2 && warmRequest->cacheOk;
    warmRequest->cacheAt = emscripten_get_now();
    warmRequest->cacheReady = final;
    fprintf(stderr, "WEB_GPU_RECIPE_CACHE loaded=%zu valid=%d\n", valid ? recipes.size() : 0, valid);
    return valid;
}
struct DrawState {
    uint64_t pipelineHash = 0, pipelineId = 0;
    std::vector<uint8_t> keyBytes;
    std::shared_ptr<const PrepareData> prepare;
    const sm3::Result *vertexInfo = nullptr, *pixelInfo = nullptr;
    std::array<uint64_t, 4> buffers{};
    std::array<unsigned, 4> offsets{};
    std::array<unsigned, 4> strides{}, bufferSizes{};
    uint64_t index = 0;
    bool wide = false;
    DWORD ss[20][16];
    std::array<uint64_t, 16> textures{};
    Surface color, depth;
    D3DVIEWPORT9 viewport{};
    RECT scissor{};
    bool scissorEnable = false, zEnable = false, zWrite = false;
    DWORD blendFactor = 0, stencilRef = 0;
    ConstantRef constants[2];
    D3DPRIMITIVETYPE topology{};
    unsigned count = 0, first = 0;
    int base = 0;
    unsigned minVertex = 0, vertexCount = 0;
};
struct Pipeline {
    wgpu::RenderPipeline handle;
    wgpu::BindGroup constants;
    std::array<wgpu::BindGroupLayout, 3> layouts;
    uint64_t constantSizes[2]{};
    std::vector<unsigned> vertexStreams;
    bool pending = false;
    std::string error;
    uint64_t id = 0, hash = 0;
    std::vector<uint8_t> key;
    std::shared_ptr<const PrepareData> recipe;
    bool learned = false, imported = false;
    uint32_t learnedToken = 0;
    std::set<uint32_t> learnedScopes;
    std::set<uint32_t> observedScopes;
};
struct Executor;
struct Upload {
    uint64_t id = 0, generation = 0;
    uint32_t dest = 0, source = 0, size = 0;
    uint32_t writeBegin = 0, writeEnd = 0;
    bool hoisted = false;
};
struct Command {
    std::function<void(Executor &)> run;
    std::shared_ptr<DrawState> draw;
    uint64_t bytes = 0;
    Upload upload;
    uint64_t bufferId = 0, bufferSize = 0;
};
struct Span { uint32_t offset = 0, size = 0; };
struct Batch {
    std::vector<Command> commands;
    uint64_t bytes = 0;
    ConstantArena constants;
    std::vector<uint8_t> immediate;
    std::unordered_map<uint64_t, Span> spans;
    bool immediateReady = false;
    bool resourcesReady = false;
    size_t prepareCursor = 0;
    uint64_t frameId = 0, pipelineMisses = 0, skippedDraws = 0, uploadBytes = 0, hoistedBytes = 0;
    double translationMs = 0, queueWaitMs = 0, cpuMs = 0, pipelineWaitMs = 0;
    double blockedAt = 0, queuedAt = 0, ownerQueueWaitMs = 0;
    uint64_t initialDraws = 0, initialPasses = 0, initialUploads = 0;
    std::array<uint64_t, 6> passEnds{};
    int timingSlot = -1;
    bool preparation = false;
    uint64_t firstTextureUploads = 0, firstStaticBufferUploads = 0;
    std::shared_ptr<std::atomic<bool>> completion;
};
Batch recording;
thread_local Batch *resourceRecording = nullptr;
std::deque<Batch> batches;
uint64_t queuedBytes = 0;
std::atomic<uint64_t> resourceBatches{0}, resourceCommands{0}, resourceBatchHighWater{0};
uint64_t nextFrame = 0;
std::vector<uint64_t> released;
ConstantRef constantReuse[2];
void Enqueue(std::function<void(Executor &)> fn, uint64_t bytes = 0) {
    auto &target = resourceRecording ? *resourceRecording : recording;
    target.commands.push_back({std::move(fn), {}, bytes}); target.bytes += bytes;
}
void SubmitResources(Batch &batch, bool flush = true) {
    if (!Web_SubmitPreparationResources(batch.commands.size(), batch.bytes, bool(batch.completion), flush)) return;
    batch.preparation = true;
    std::unique_lock lock(queueMu);
    if (!queueSpace.wait_for(lock, std::chrono::seconds(60), [&] {
        // A single oversized asset may drain alone; never admit another batch
        // on top of it or wait forever for it to fit the normal queue budget.
        return failed || (batches.size() < 2 && (!queuedBytes || batch.bytes <= QueueLimit - std::min(queuedBytes, QueueLimit)));
    })) {
        lock.unlock(); Fail("resource preparation queue stalled"); return;
    }
    if (failed) return;
    batch.queuedAt = emscripten_get_now();
    ++resourceBatches; resourceCommands += batch.commands.size();
    auto highWater = resourceBatchHighWater.load();
    while (highWater < batch.bytes && !resourceBatchHighWater.compare_exchange_weak(highWater, batch.bytes)) {}
    queuedBytes += batch.bytes; batches.push_back(std::move(batch)); batch = {};
    lock.unlock(); WakeSubmitted();
}
void ResetConstantReuse() { constantReuse[0] = constantReuse[1] = {}; }
ConstantRef SnapshotConstants(const Uniforms &u, const sm3::Result &info, unsigned stage) {
    const auto bytes = FloatBytes(info), size = ConstantSize(info);
    auto &last = constantReuse[stage];
    if (last.data && last.size == size && !memcmp(last.data, u.fc, bytes) && !memcmp(last.data + bytes, u.ic, UniformTailSize)) return last;
    auto *block = recording.constants.Alloc(size);
    memcpy(block, u.fc, bytes); memcpy(block + bytes, u.ic, UniformTailSize);
    recording.bytes += size;
    return last = {block, uint32_t(size)};
}
Span PackImmediate(Batch &batch, const void *src, unsigned n) {
    const uint32_t size = uint32_t(Align(n, 4)), offset = uint32_t(batch.immediate.size());
    batch.immediate.resize(offset + size);
    if (n) memcpy(batch.immediate.data() + offset, src, n);
    batch.bytes += size;
    return {offset, size};
}
Span PackImmediate(const void *src, unsigned n) { return PackImmediate(recording, src, n); }
struct Executor {
    wgpu::Instance instance;
    wgpu::Adapter adapter;
    wgpu::Device device;
    wgpu::Surface surface;
    wgpu::TextureFormat canvasFormat;
    struct GroupIdentity {
        uint64_t pipelineId = 0;
        uint8_t stage = 0, count = 0;
        struct Slot { uint64_t texture = 0, generation = 0; DWORD ss[16]; } slots[16];
    };
    struct GroupRecord {
        GroupIdentity identity{};
        uint64_t hash = 0;
        wgpu::BindGroup group;
        std::array<uint64_t, 16> textures{};
        uint8_t textureCount = 0;
    };
    struct SamplerRecord { DWORD words[16]; uint32_t comparison = 0; wgpu::Sampler sampler; };
    struct Resolved { wgpu::Buffer buffer; uint64_t offset = 0, size = wgpu::kWholeSize; };
    std::unordered_map<uint64_t, Texture> textures;
    std::unordered_map<uint64_t, wgpu::Buffer> buffers;
    std::unordered_map<uint64_t, std::unique_ptr<Pipeline>> pipelines;
    std::unordered_map<uint64_t, std::vector<uint64_t>> pipelinesByHash;
    std::unordered_map<uint64_t, uint64_t> pipelineAliases;
    std::unordered_set<uint64_t> frontendPipelines;
    std::unordered_map<std::string, Pipeline> utility;
    std::unordered_map<uint64_t, std::vector<SamplerRecord>> samplers;
    std::unordered_map<uint64_t, GroupRecord> groups;
    std::unordered_map<uint64_t, std::vector<uint64_t>> groupsByHash, groupsByTexture;
    uint64_t nextGroup = 1;
    std::unordered_map<std::string, wgpu::ShaderModule> shaderModules;
    std::unordered_map<std::string, wgpu::BindGroupLayout> layoutCache;
    wgpu::Buffer uniforms;
    ConstantRef previousConstants[2];
    const uint8_t *immediateCpu = nullptr;
    uint64_t immediateBytes = 0, immediateCapacity = 0;
    wgpu::Buffer immediateBuffer;
    bool immediateReferenced = false;
    std::unordered_map<uint64_t, Span> *spans = nullptr;
    Batch *active = nullptr;
    struct PooledBuffer { wgpu::Buffer buffer; uint64_t capacity = 0; };
    std::vector<PooledBuffer> immediatePool;
    std::vector<PooledBuffer> persistentPool;
    uint64_t persistentPoolBytes = 0;
    std::unordered_set<uint64_t> encoderBuffers, encoderTextures;
    uint32_t previousOffsets[2]{};
    wgpu::CommandEncoder encoder;
    wgpu::RenderPassEncoder pass;
    Surface passColor, passDepth;
    bool passDepthReadOnly = false;
    struct PassState {
        WGPURenderPipeline pipeline = nullptr;
        WGPUBindGroup constants = nullptr, textures[2]{};
        uint32_t offsets[2]{};
        // Temporary uploads can recycle Dawn's JS handles within a pass.
        // Engine resource identities remain unique across their lifetimes.
        uint64_t vertices[4]{}, index = 0;
        WGPUBuffer vertexHandles[4]{}, indexHandle = nullptr;
        uint64_t resolvedOffsets[4]{}, resolvedSizes[4]{}, indexOffset = 0, indexSize = 0;
        unsigned vertexOffsets[4]{};
        bool wide = false, valid = false;
        D3DVIEWPORT9 viewport{};
        RECT scissor{};
        DWORD blend = 0, stencil = 0;
    } bound;
    uint64_t uploadedUniformOffset = 0;
    std::vector<uint8_t> uniformBytes;
    uint64_t uniformOffset = 0, frames = 0, draws = 0, uploads = 0, waits = 0, passes = 0, flushes = 0, bindGroups = 0;
    uint64_t statDraws = 0, statPasses = 0, statFlushes = 0, statGroups = 0;
    double statWindow = 0;
    unsigned inflight = 0;
    unsigned pipelineJobs = 0;
    uint64_t compiledPipelines = 0;
    double telemetryAt = 0;
    std::vector<double> intervals;
    std::vector<std::string> frameTelemetry, eventTelemetry;
    struct TimingSlot { wgpu::QuerySet queries; wgpu::Buffer resolve, readback; bool busy = false, began = false; };
    std::array<TimingSlot, 4> timing;
    bool timestampQueries = false;
    double warmReportAt = 0, cacheSaveAt = 0;
    std::string cacheKey;
    uint32_t cacheToken = 0, cacheScope = 0;
    std::map<std::string,uint32_t> cacheScopes;
    uint64_t savedRecipes = 0, savedObserved = 0;
    std::shared_ptr<WarmRequest> utilityRequest;
    bool preparingUtilities = false;
    struct UtilityJob {
        Pipeline *target;
        std::shared_ptr<WarmRequest> request;
        wgpu::RenderPipelineDescriptor descriptor;
        wgpu::FragmentState fragment;
        wgpu::ColorTargetState color;
        wgpu::BlendState blend;
        wgpu::DepthStencilState depth;
        UtilityJob(Pipeline *p, std::shared_ptr<WarmRequest> r, const wgpu::RenderPipelineDescriptor &d)
            : target(p), request(std::move(r)), descriptor(d) {
            // Utility descriptors have no vertex buffers and at most one color
            // attachment. Own their pointed-to state until the job is started.
            if (d.fragment) {
                fragment = *d.fragment; descriptor.fragment = &fragment;
                if (fragment.targetCount) {
                    color = fragment.targets[0]; fragment.targets = &color;
                    if (color.blend) { blend = *color.blend; color.blend = &blend; }
                }
            }
            if (d.depthStencil) { depth = *d.depthStencil; descriptor.depthStencil = &depth; }
        }
    };
    std::deque<std::unique_ptr<UtilityJob>> utilityJobs;
    void CompileUtility(Pipeline &p, const wgpu::RenderPipelineDescriptor &descriptor) {
        if (p.handle || p.pending || !utilityRequest) return;
        ++utilityRequest->utilityTotal; p.pending = true;
        utilityJobs.push_back(std::make_unique<UtilityJob>(&p, utilityRequest, descriptor));
    }
    void WarmUtilities(const std::vector<std::pair<wgpu::TextureFormat,wgpu::TextureFormat>> &, std::shared_ptr<WarmRequest>);
    void ProcessWarmup();
    void ProcessLateRecipes();
    void UpdateMapSettle();
    void SaveRecipes();
    void SendTelemetry();
    void FrameDone(Batch &);
    void PrepareAhead(Batch &);
    void PrepareUploads(Batch &);
    wgpu::RenderPassEncoder BeginPass(wgpu::RenderPassDescriptor &, bool last = false);
    void FinishTiming(Batch &);
    void ReadTiming(int slot, uint64_t frameId);
    bool float32Filterable = false, float32Blendable = false;
    bool tickScheduled = false;
    // Owner-thread callbacks can finish just after rAF. Resume queued work on
    // the next event-loop turn instead of adding a display frame per pipeline.
    void Wake() {
        if (tickScheduled || failed || !callbackWake) return;
        tickScheduled = true;
        emscripten_set_timeout([](void *arg) {
            auto &e = *static_cast<Executor *>(arg);
            e.tickScheduled = false;
            e.Tick();
        }, 0, this);
    }
    bool CanSample(const DrawState &s) {
        if (float32Filterable) return true;
        for (auto *info : {s.vertexInfo, s.pixelInfo}) for (auto &sampler : info->samplers) {
            if (Float32Texture(textures.at(s.textures[sampler.reg]).format)) {
                Fail("scene requires float32-filterable for texture sampling"); return false;
            }
        }
        return true;
    }
    double lastFrame = 0;
    void Configure() {
        wgpu::SurfaceConfiguration c{}; c.device = device; c.format = canvasFormat;
        c.width = width; c.height = height; c.alphaMode = wgpu::CompositeAlphaMode::Opaque;
        c.usage = wgpu::TextureUsage::RenderAttachment; surface.Configure(&c);
    }
    wgpu::ShaderModule Shader(const std::string &source) {
        auto found = shaderModules.find(source);
        if (found != shaderModules.end()) return found->second;
        wgpu::ShaderSourceWGSL code{}; code.code = source.c_str();
        wgpu::ShaderModuleDescriptor d{}; d.nextInChain = &code;
        // Speculative modules may be rejected. The async pipeline callback
        // records that failure; an actual draw decides whether to fall back.
        device.PushErrorScope(wgpu::ErrorFilter::Validation);
        auto module = device.CreateShaderModule(&d);
        device.PopErrorScope(wgpu::CallbackMode::AllowSpontaneous,
            [](wgpu::PopErrorScopeStatus, wgpu::ErrorType type, wgpu::StringView message) {
                if (type == wgpu::ErrorType::NoError || !message.length) return;
                LogShaderError(std::string(message.data, message.length));
            });
        return shaderModules.emplace(source, std::move(module)).first->second;
    }
    wgpu::BindGroupLayout Layout(const std::vector<wgpu::BindGroupLayoutEntry> &entries) {
        std::string key;
        auto add = [&](const auto &v) { key.append(reinterpret_cast<const char *>(&v), sizeof v); };
        for (auto &b : entries) {
            add(b.binding); add(b.visibility); add(b.buffer.type); add(b.buffer.hasDynamicOffset); add(b.buffer.minBindingSize);
            add(b.texture.sampleType); add(b.texture.viewDimension); add(b.texture.multisampled); add(b.sampler.type);
        }
        auto it = layoutCache.find(key); if (it != layoutCache.end()) return it->second;
        wgpu::BindGroupLayoutDescriptor d{}; d.entryCount = entries.size(); d.entries = entries.data();
        return layoutCache.emplace(std::move(key), device.CreateBindGroupLayout(&d)).first->second;
    }
    Pipeline &Prepare(const DrawState &s, bool required = true);
    void Execute(const DrawState &s);
    void Tick();
    void RotateUniformArena() {
        EndPass(UniformReason);
        Flush();
        auto retired = std::move(uniforms);
        device.GetQueue().OnSubmittedWorkDone(wgpu::CallbackMode::AllowSpontaneous,
            [retired = std::move(retired)](wgpu::QueueWorkDoneStatus, wgpu::StringView) { retired.Destroy(); });
        wgpu::BufferDescriptor d{}; d.size = UniformCapacity; d.usage = wgpu::BufferUsage::Uniform | wgpu::BufferUsage::CopyDst;
        uniforms = device.CreateBuffer(&d); uniformOffset = uploadedUniformOffset = 0;
        previousConstants[0] = previousConstants[1] = {};
        for (auto &[id, pipeline] : pipelines) pipeline->constants = nullptr;
    }
    enum EndReason { Target, BufferHazard, FlushReason, CopyReason, ClearReason, UniformReason };
    void EndPass(EndReason reason = CopyReason) {
        if (pass) { if (active && profiling) ++active->passEnds[reason]; pass.End(); pass = nullptr; }
    }
    void Flush();
    void EnsureImmediate();
    void RetireImmediate();
    void Adopt(Batch &batch);
    void ReleaseBatch();
    Resolved Resolve(uint64_t id, uint64_t extra);
    void UploadFromImmediate(uint64_t id, uint64_t destOffset, uint32_t span, uint32_t size);
    void DropGroupsForTexture(uint64_t texture);
    Pipeline *FindPipeline(const DrawState &s);
    wgpu::Sampler Sampler(const DWORD *s, bool comparison = false);
    wgpu::BindGroup Textures(const DrawState &, const Pipeline &, unsigned stage);
    void Blit(Surface src, RECT sr, Surface dst, RECT dr, bool linear, bool toCanvas = false);
    void ClearRects(Surface color, Surface depth, const std::vector<D3DRECT> &, DWORD flags, D3DCOLOR, float, DWORD);
};
Executor executor;
void WakeSubmitted() {
    if (!submissionWake || started != 1 || !ownerQueue || submissionWakeQueued.exchange(true)) return;
    // The proxy only schedules work on the existing GPU owner. Producers never
    // touch GPU handles. Coalesce notifications until the owner processes one.
    if (!emscripten_proxy_async(ownerQueue, ownerThread, [](void *) {
        submissionWakeQueued = false;
        executor.Tick();
    }, nullptr)) submissionWakeQueued = false;
}
Surface pendingDepthSource, pendingDepthTarget;
uint64_t pendingDepthColor = 0;
bool pendingDepthWritten = false;
void FlushDepthWriteback() {
    if (pendingDepthWritten && pendingDepthSource.id && pendingDepthTarget.id) {
        const Surface src = pendingDepthTarget, dst = pendingDepthSource;
        const RECT overlap{0, 0, LONG(std::min(src.w, dst.w)), LONG(std::min(src.h, dst.h))};
        Enqueue([=](Executor &e) { e.Blit(src, overlap, dst, overlap, false); });
    }
    pendingDepthSource = pendingDepthTarget = {}; pendingDepthColor = 0; pendingDepthWritten = false;
}

wgpu::VertexFormat VertexFormat(BYTE type) {
    switch (type) {
    case D3DDECLTYPE_FLOAT1: return wgpu::VertexFormat::Float32;
    case D3DDECLTYPE_FLOAT2: return wgpu::VertexFormat::Float32x2;
    case D3DDECLTYPE_FLOAT3: return wgpu::VertexFormat::Float32x3;
    case D3DDECLTYPE_FLOAT4: return wgpu::VertexFormat::Float32x4;
    case D3DDECLTYPE_D3DCOLOR: case D3DDECLTYPE_UBYTE4N: return wgpu::VertexFormat::Unorm8x4;
    case D3DDECLTYPE_UBYTE4: return wgpu::VertexFormat::Uint8x4;
    case D3DDECLTYPE_SHORT2: return wgpu::VertexFormat::Sint16x2;
    case D3DDECLTYPE_SHORT4: return wgpu::VertexFormat::Sint16x4;
    case D3DDECLTYPE_SHORT2N: return wgpu::VertexFormat::Snorm16x2;
    case D3DDECLTYPE_SHORT4N: return wgpu::VertexFormat::Snorm16x4;
    case D3DDECLTYPE_USHORT2N: return wgpu::VertexFormat::Unorm16x2;
    case D3DDECLTYPE_USHORT4N: return wgpu::VertexFormat::Unorm16x4;
    case D3DDECLTYPE_FLOAT16_2: return wgpu::VertexFormat::Float16x2;
    case D3DDECLTYPE_FLOAT16_4: return wgpu::VertexFormat::Float16x4;
    default: return static_cast<wgpu::VertexFormat>(0);
    }
}
void Executor::RetireImmediate() {
    if (!immediateBuffer) { immediateReferenced = false; immediateCapacity = 0; return; }
    auto buffer = std::move(immediateBuffer);
    const uint64_t cap = immediateCapacity;
    immediateBuffer = nullptr; immediateCapacity = 0; immediateReferenced = false;
    device.GetQueue().OnSubmittedWorkDone(wgpu::CallbackMode::AllowSpontaneous,
        [this, buffer = std::move(buffer), cap](wgpu::QueueWorkDoneStatus, wgpu::StringView) mutable {
            immediatePool.push_back({std::move(buffer), cap});
        });
}
void Executor::EnsureImmediate() {
    if (!immediateBytes || !immediateCpu) return;
    if (active && active->immediateReady && immediateBuffer) return;
    if (immediateReferenced) RetireImmediate();
    if (immediateBuffer && immediateCapacity < immediateBytes) { immediateBuffer.Destroy(); immediateBuffer = nullptr; immediateCapacity = 0; }
    if (!immediateBuffer) {
        for (size_t i = 0; i < immediatePool.size(); ++i) if (immediatePool[i].capacity >= immediateBytes) {
            immediateBuffer = std::move(immediatePool[i].buffer); immediateCapacity = immediatePool[i].capacity;
            immediatePool.erase(immediatePool.begin() + i); break;
        }
    }
    if (!immediateBuffer) {
        wgpu::BufferDescriptor desc{}; desc.size = std::max(immediateBytes, uint64_t(1 << 20));
        desc.usage = wgpu::BufferUsage::Vertex | wgpu::BufferUsage::Index | wgpu::BufferUsage::CopySrc | wgpu::BufferUsage::CopyDst;
        immediateBuffer = device.CreateBuffer(&desc); immediateCapacity = desc.size;
    }
    device.GetQueue().WriteBuffer(immediateBuffer, 0, immediateCpu, immediateBytes);
    immediateReferenced = false;
    if (active) active->immediateReady = true;
}
void Executor::Adopt(Batch &batch) {
    active = &batch; spans = &batch.spans;
    immediateCpu = batch.immediate.empty() ? nullptr : batch.immediate.data();
    immediateBytes = batch.immediate.size();
}
void Executor::ReleaseBatch() { active = nullptr; spans = nullptr; immediateCpu = nullptr; immediateBytes = 0; }
void Executor::Flush() {
    EndPass(FlushReason);
    const bool uniformsPending = uniformOffset > uploadedUniformOffset;
    if (!encoder && !uniformsPending) return;
    if (uniformsPending) {
        device.GetQueue().WriteBuffer(uniforms, uploadedUniformOffset, uniformBytes.data() + uploadedUniformOffset, uniformOffset - uploadedUniformOffset);
        uploadedUniformOffset = uniformOffset;
    }
    if (encoder) { auto cb = encoder.Finish(); device.GetQueue().Submit(1, &cb); encoder = nullptr; ++flushes; }
    encoderBuffers.clear(); encoderTextures.clear();
    if (immediateReferenced) { RetireImmediate(); if (active) active->immediateReady = false; }
}
Executor::Resolved Executor::Resolve(uint64_t id, uint64_t extra) {
    if (spans) {
        auto it = spans->find(id);
        if (it != spans->end()) {
            if (!active || !active->immediateReady) EnsureImmediate();
            immediateReferenced = true;
            const uint64_t size = it->second.size > extra ? it->second.size - extra : 0;
            return {immediateBuffer, it->second.offset + extra, size};
        }
    }
    if (id) encoderBuffers.insert(id);
    return {buffers.at(id), extra, wgpu::kWholeSize};
}
void Executor::UploadFromImmediate(uint64_t id, uint64_t destOffset, uint32_t span, uint32_t size) {
    // The destination is not in the open encoder, so queue.writeBuffer is legal
    // without ending the pass. A buffer the pass already bound still updates
    // through an ordered copy after the pass ends.
    if (!encoderBuffers.count(id)) { device.GetQueue().WriteBuffer(buffers.at(id), destOffset, immediateCpu + span, size); ++uploads; return; }
    if (!active || !active->immediateReady) EnsureImmediate();
    EndPass(BufferHazard);
    if (!encoder) encoder = device.CreateCommandEncoder();
    encoder.CopyBufferToBuffer(immediateBuffer, span, buffers.at(id), destOffset, size);
    encoderBuffers.insert(id); immediateReferenced = true; ++uploads;
}
void Executor::PrepareUploads(Batch &batch) {
    for (auto &cmd : batch.commands) if (cmd.bufferId && !buffers.count(cmd.bufferId)) {
        bool reused = false;
        for (size_t i = 0; i < persistentPool.size(); ++i) if (persistentPool[i].capacity == cmd.bufferSize) {
            buffers[cmd.bufferId] = std::move(persistentPool[i].buffer); persistentPoolBytes -= cmd.bufferSize;
            persistentPool.erase(persistentPool.begin() + i); reused = true; break;
        }
        if (reused) continue;
        wgpu::BufferDescriptor d{}; d.size = cmd.bufferSize;
        d.usage = wgpu::BufferUsage::Vertex | wgpu::BufferUsage::Index | wgpu::BufferUsage::CopyDst;
        buffers[cmd.bufferId] = device.CreateBuffer(&d);
    }
    if (!hoistUploads) { batch.resourcesReady = true; return; }
    policy::UploadOrder order;
    std::unordered_map<uint64_t, const Upload *> previousUploads;
    std::vector<policy::Range> ranges;
    for (auto &cmd : batch.commands) {
        auto &u = cmd.upload;
        if (u.id) {
            const policy::Range physical{u.id,u.dest,uint64_t(u.dest)+u.size}, changed{u.id,u.writeBegin,u.writeEnd};
            const auto *old = previousUploads[u.id];
            const auto paddingSafe = policy::PaddingUnchanged(old ? policy::Range{old->id,old->dest,uint64_t(old->dest)+old->size} : policy::Range{},
                old ? batch.immediate.data()+old->source : nullptr, physical, batch.immediate.data()+u.source, changed, order.ReadSpan(u.id));
            u.hoisted = order.Hoist(physical, changed, paddingSafe);
            if (u.hoisted) ranges.push_back({u.id, u.dest, uint64_t(u.dest) + u.size});
            if (u.hoisted) previousUploads[u.id] = &u;
        }
        if (!cmd.draw) continue;
        const auto &s = *cmd.draw;
        if (s.index) { const uint64_t step = s.wide ? 4 : 2; order.Read({s.index, s.first * step, (uint64_t(s.first) + s.count) * step}); }
        for (unsigned stream = 0; stream < 4; ++stream) if (s.buffers[stream]) {
            const int64_t first = s.index ? int64_t(s.base) + s.minVertex : s.first;
            const uint64_t count = s.index ? s.vertexCount : s.count;
            uint64_t begin = s.offsets[stream], end = UINT64_MAX;
            if (first >= 0 && count && s.strides[stream]) {
                begin += uint64_t(first) * s.strides[stream];
                end = begin + count * s.strides[stream];
                if (s.bufferSizes[stream] && end > s.bufferSizes[stream]) { begin = 0; end = UINT64_MAX; }
            }
            order.Read({s.buffers[stream], begin, end});
        }
    }
    // Merge adjacent writes, but replay source copies in recording order so
    // overlapping alignment padding retains the last recorded value.
    const auto merged = policy::MergeRanges(std::move(ranges));
    std::vector<std::vector<uint8_t>> sources;
    sources.reserve(merged.size());
    for (const auto &r : merged) sources.emplace_back(size_t(r.end - r.begin));
    // Locate each upload once instead of rescanning every command for every
    // merged range. Preserve recording order when padding ranges overlap.
    for (const auto &cmd : batch.commands) {
        const auto &u = cmd.upload; if (!u.hoisted) continue;
        const policy::Range start{u.id,u.dest,0};
        auto end = std::upper_bound(merged.begin(), merged.end(), start, [](const auto &a, const auto &b) {
            return a.id < b.id || (a.id == b.id && a.begin < b.begin);
        });
        if (end == merged.begin()) continue;
        const auto &r = *--end;
        if (r.id != u.id || u.dest < r.begin || uint64_t(u.dest) + u.size > r.end) continue;
        auto &bytes = sources[size_t(end - merged.begin())];
        memcpy(bytes.data() + u.dest - r.begin, batch.immediate.data() + u.source, u.size);
    }
    for (size_t i = 0; i < merged.size(); ++i) {
        const auto &r = merged[i]; const auto &bytes = sources[i];
        device.GetQueue().WriteBuffer(buffers.at(r.id), r.begin, bytes.data(), bytes.size());
        batch.hoistedBytes += bytes.size(); ++uploads;
    }
    batch.resourcesReady = true;
}
void Executor::DropGroupsForTexture(uint64_t texture) {
    auto found = groupsByTexture.find(texture);
    if (found == groupsByTexture.end()) return;
    const auto ids = std::move(found->second);
    groupsByTexture.erase(found);
    for (auto gid : ids) {
        auto it = groups.find(gid);
        if (it == groups.end()) continue;
        auto &rec = it->second;
        auto &bucket = groupsByHash[rec.hash];
        bucket.erase(std::remove(bucket.begin(), bucket.end(), gid), bucket.end());
        for (uint8_t i = 0; i < rec.textureCount; ++i) {
            if (rec.textures[i] == texture) continue;
            auto &list = groupsByTexture[rec.textures[i]];
            list.erase(std::remove(list.begin(), list.end(), gid), list.end());
        }
        groups.erase(it);
    }
}
Pipeline *Executor::FindPipeline(const DrawState &s) {
    const auto alias = pipelineAliases.find(s.pipelineId);
    const uint64_t id = alias == pipelineAliases.end() ? s.pipelineId : alias->second;
    auto direct = pipelines.find(id);
    if (direct != pipelines.end()) return direct->second.get();
    if (!s.prepare) return nullptr;
    auto bucket = pipelinesByHash.find(s.pipelineHash);
    if (bucket == pipelinesByHash.end()) return nullptr;
    for (auto cachedId : bucket->second) {
        auto &p = *pipelines.at(cachedId);
        if (p.key == s.prepare->key) { pipelineAliases[s.pipelineId] = p.id; return &p; }
    }
    return nullptr;
}
Pipeline &Executor::Prepare(const DrawState &s, bool required) {
    static Pipeline missing;
    if (Pipeline *found = FindPipeline(s)) {
        if (required) {
            found->learned = true;
            if (cacheToken && found->learnedToken != cacheToken) {
                found->learnedScopes.insert(cacheScope); found->observedScopes.insert(cacheScope); found->learnedToken = cacheToken;
                std::ostringstream event;
                event << "{\"kind\":\"pipeline-use\",\"at\":" << std::fixed << emscripten_date_now()
                    << ",\"frameId\":" << (active ? active->frameId : 0) << ",\"recipeHash\":\"" << found->hash
                    << "\",\"prepared\":" << (found->imported ? "true" : "false") << "}";
                eventTelemetry.push_back(event.str());
            }
        }
        if (required && !found->error.empty()) Fail("pipeline: " + found->error);
        return *found;
    }
    if (!s.prepare || s.prepare->key.empty() || failed) { if (required) Fail("draw missing pipeline state"); return missing; }
    if (pipelineJobs >= (loadingWarm ? preparationJobs.load() : runtimeJobs.load())) return missing;
    auto created = std::make_unique<Pipeline>();
    created->id = s.pipelineId; created->hash = s.pipelineHash; created->key = s.prepare->key;
    Pipeline &p = *created;
    // PrepareAhead discovers commands from the gameplay batch before Execute
    // reaches them. Those are runtime misses too, even if compilation finishes
    // before the eventual required lookup.
    if ((profiling && required) || (!loadingWarm && active && !active->preparation)) {
        std::ostringstream event; event << "{\"frameId\":" << (active ? active->frameId : 0)
            << ",\"kind\":\"pipeline-miss\",\"at\":" << std::fixed << emscripten_date_now()
            << ",\"requiredPipeline\":true,\"pipelineId\":" << s.pipelineId << ",\"recipeHash\":\"" << s.pipelineHash
            << "\",\"resourceIds\":[";
        for (size_t i = 0; i < s.textures.size(); ++i) { if (i) event << ','; event << s.textures[i]; }
        event << "]}";
        eventTelemetry.push_back(event.str());
    }
    p.recipe = s.prepare; p.learned = required; p.imported = loadingWarm;
    if (required && cacheToken) { p.learnedScopes.insert(cacheScope); p.observedScopes.insert(cacheScope); p.learnedToken = cacheToken; }
    pipelines.emplace(p.id, std::move(created));
    pipelinesByHash[p.hash].push_back(p.id);
    const auto &prep = *s.prepare;
    const auto &rs = prep.rs;
    auto reject = [&](const char *error) { p.error = error; if (required) Fail(error); };
    std::array<std::vector<wgpu::VertexAttribute>, 4> attrs;
    std::array<wgpu::VertexBufferLayout, 4> layouts{};
    unsigned bufferCount = 0;
    for (auto &v : prep.decl) {
        if (v.Stream >= 4) continue;
        const int loc = sm3::AttribLocation(v.Usage, v.UsageIndex);
        if (loc < 0) continue;
        wgpu::VertexAttribute a{}; a.format = VertexFormat(v.Type); a.offset = v.Offset; a.shaderLocation = loc;
        if (unsigned(a.format) == 0) { reject("unsupported vertex declaration"); return p; }
        attrs[v.Stream].push_back(a);
    }
    // D3D stream indices may have holes. WebGPU layouts must not create a
    // fictitious required binding for an unused slot between streams 0 and 2.
    unsigned streamMask = 0;
    for (unsigned stream = 0; stream < attrs.size(); ++stream) if (!attrs[stream].empty()) streamMask |= 1u << stream;
    p.vertexStreams = policy::CompactVertexStreams(streamMask);
    for (unsigned stream : p.vertexStreams) {
        auto &layout = layouts[bufferCount++];
        layout.arrayStride = prep.strides[stream]; layout.attributeCount = attrs[stream].size(); layout.attributes = attrs[stream].data();
        layout.stepMode = wgpu::VertexStepMode::Vertex;
    }
    std::vector<wgpu::BindGroupLayoutEntry> entries;
    for (unsigned stage = 0; stage < 2; ++stage) {
        wgpu::BindGroupLayoutEntry b{}; b.binding = stage;
        b.visibility = stage ? wgpu::ShaderStage::Fragment : wgpu::ShaderStage::Vertex;
        p.constantSizes[stage] = ConstantSize(stage ? prep.pixelInfo : prep.vertexInfo);
        b.buffer.type = wgpu::BufferBindingType::Uniform; b.buffer.hasDynamicOffset = true; b.buffer.minBindingSize = p.constantSizes[stage];
        entries.push_back(b);
    }
    wgpu::BindGroupLayoutDescriptor layout{}; layout.entryCount = entries.size(); layout.entries = entries.data();
    p.layouts[0] = Layout(entries);
    for (unsigned stage = 0; stage < 2; ++stage) {
        entries.clear(); const auto &info = stage ? prep.pixelInfo : prep.vertexInfo;
        for (auto &sampler : info.samplers) {
            const bool dep = (prep.depthMask & (1u << sampler.reg)) != 0;
            wgpu::BindGroupLayoutEntry t{}; t.binding = sampler.reg * 3;
            t.visibility = stage ? wgpu::ShaderStage::Fragment : wgpu::ShaderStage::Vertex;
            t.texture.sampleType = dep ? wgpu::TextureSampleType::Depth : wgpu::TextureSampleType::Float;
            t.texture.viewDimension = sampler.type == 3 ? wgpu::TextureViewDimension::Cube : sampler.type == 4 ? wgpu::TextureViewDimension::e3D : wgpu::TextureViewDimension::e2D;
            entries.push_back(t);
            wgpu::BindGroupLayoutEntry b{}; b.binding = sampler.reg * 3 + 1; b.visibility = t.visibility;
            b.sampler.type = dep ? wgpu::SamplerBindingType::Comparison : wgpu::SamplerBindingType::Filtering;
            entries.push_back(b);
        }
        p.layouts[stage + 1] = Layout(entries);
    }
    wgpu::PipelineLayoutDescriptor pl{}; pl.bindGroupLayoutCount = 3; pl.bindGroupLayouts = p.layouts.data();
    wgpu::BindGroupEntry uniformEntries[2]{};
    for (unsigned i = 0; i < 2; ++i) { uniformEntries[i].binding = i; uniformEntries[i].buffer = uniforms; uniformEntries[i].size = p.constantSizes[i]; }
    wgpu::BindGroupDescriptor uniformGroup{}; uniformGroup.layout = p.layouts[0]; uniformGroup.entryCount = 2; uniformGroup.entries = uniformEntries;
    p.constants = device.CreateBindGroup(&uniformGroup); ++bindGroups;
    wgpu::RenderPipelineDescriptor desc{}; desc.layout = device.CreatePipelineLayout(&pl);
    desc.vertex.module = Shader(std::string(prep.vs)); desc.vertex.entryPoint = "main";
    desc.vertex.bufferCount = bufferCount; desc.vertex.buffers = layouts.data();
    wgpu::ColorTargetState target{}; target.format = prep.colorFormat;
    target.writeMask = static_cast<wgpu::ColorWriteMask>(rs[D3DRS_COLORWRITEENABLE]);
    wgpu::BlendState blend{};
    if (rs[D3DRS_ALPHABLENDENABLE]) {
        if (Float32Texture(target.format) && !float32Blendable) {
            reject("scene requires float32-blendable for render target blending"); return p;
        }
        blend.color = {BlendOp(rs[D3DRS_BLENDOP]), Blend(rs[D3DRS_SRCBLEND]), Blend(rs[D3DRS_DESTBLEND])};
        blend.alpha = rs[D3DRS_SEPARATEALPHABLENDENABLE] ? wgpu::BlendComponent{BlendOp(rs[D3DRS_BLENDOPALPHA]), Blend(rs[D3DRS_SRCBLENDALPHA]), Blend(rs[D3DRS_DESTBLENDALPHA])} : blend.color;
        target.blend = &blend;
    }
    wgpu::FragmentState frag{}; frag.module = Shader(std::string(prep.ps)); frag.entryPoint = "main"; frag.targetCount = 1; frag.targets = &target; desc.fragment = &frag;
    desc.primitive.topology = prep.topology == D3DPT_TRIANGLESTRIP ? wgpu::PrimitiveTopology::TriangleStrip : prep.topology == D3DPT_LINELIST ? wgpu::PrimitiveTopology::LineList : prep.topology == D3DPT_LINESTRIP ? wgpu::PrimitiveTopology::LineStrip : prep.topology == D3DPT_POINTLIST ? wgpu::PrimitiveTopology::PointList : wgpu::PrimitiveTopology::TriangleList;
    if (prep.stripFormat) desc.primitive.stripIndexFormat = prep.stripFormat == 2 ? wgpu::IndexFormat::Uint32 : wgpu::IndexFormat::Uint16;
    // GLSL flips clip Y; WGSL preserves D3D coordinates, so winding reverses.
    desc.primitive.frontFace = wgpu::FrontFace::CW;
    desc.primitive.cullMode = rs[D3DRS_CULLMODE] == D3DCULL_NONE ? wgpu::CullMode::None : rs[D3DRS_CULLMODE] == D3DCULL_CW ? wgpu::CullMode::Front : wgpu::CullMode::Back;
    wgpu::DepthStencilState depth{};
    if (prep.depthFormat != wgpu::TextureFormat::Undefined) {
        depth.format = prep.depthFormat;
        depth.depthWriteEnabled = rs[D3DRS_ZENABLE] && rs[D3DRS_ZWRITEENABLE] ? wgpu::OptionalBool::True : wgpu::OptionalBool::False;
        depth.depthCompare = rs[D3DRS_ZENABLE] ? Compare(rs[D3DRS_ZFUNC]) : wgpu::CompareFunction::Always;
        if (rs[D3DRS_STENCILENABLE]) {
            depth.stencilFront = {Compare(rs[D3DRS_STENCILFUNC]), Stencil(rs[D3DRS_STENCILFAIL]), Stencil(rs[D3DRS_STENCILZFAIL]), Stencil(rs[D3DRS_STENCILPASS])};
            depth.stencilBack = rs[D3DRS_TWOSIDEDSTENCILMODE] ? wgpu::StencilFaceState{Compare(rs[D3DRS_CCW_STENCILFUNC]), Stencil(rs[D3DRS_CCW_STENCILFAIL]), Stencil(rs[D3DRS_CCW_STENCILZFAIL]), Stencil(rs[D3DRS_CCW_STENCILPASS])} : depth.stencilFront;
            depth.stencilReadMask = rs[D3DRS_STENCILMASK]; depth.stencilWriteMask = rs[D3DRS_STENCILWRITEMASK];
        }
        float bias; memcpy(&bias, &rs[D3DRS_DEPTHBIAS], 4); depth.depthBias = int(bias * 16777216.0f);
        memcpy(&depth.depthBiasSlopeScale, &rs[D3DRS_SLOPESCALEDEPTHBIAS], 4);
        desc.depthStencil = &depth;
    }
    p.pending = true;
    ++pipelineJobs;
    const double compileAt = emscripten_get_now();
    const uint64_t frameId = active ? active->frameId : 0;
    device.CreateRenderPipelineAsync(&desc, wgpu::CallbackMode::AllowSpontaneous,
        [this, raw = &p, compileAt, frameId](wgpu::CreatePipelineAsyncStatus status, wgpu::RenderPipeline pipeline, wgpu::StringView message) {
            raw->pending = false;
            --pipelineJobs;
            if (profiling || frameId) {
                std::ostringstream json; json << "{\"frameId\":" << frameId << ",\"pipelineId\":" << raw->id
                    << ",\"kind\":\"pipeline-compile\",\"at\":" << std::fixed << emscripten_date_now() - (emscripten_get_now() - compileAt)
                    << ",\"end\":" << emscripten_date_now()
                    << ",\"compileMs\":" << emscripten_get_now() - compileAt << ",\"ok\":"
                    << (status == wgpu::CreatePipelineAsyncStatus::Success ? "true" : "false") << "}";
                eventTelemetry.push_back(json.str());
            }
            if (status != wgpu::CreatePipelineAsyncStatus::Success) {
                raw->error.assign(message.data ? message.data : "", message.length);
                if (raw->error.empty()) raw->error = "pipeline compilation failed";
                LogShaderError(raw->error);
                if (raw->recipe && !raw->recipe->cacheScope.empty())
                    cacheQuarantine(raw->recipe->cacheScope.c_str(), raw->recipe->cacheSource.c_str(), raw->error.c_str());
            }
            else { raw->handle = std::move(pipeline); ++compiledPipelines; }
            Wake();
            SendTelemetry();
        });
    return p;
}

wgpu::Sampler Executor::Sampler(const DWORD *words, bool comparison) {
    const uint32_t flag = comparison ? 1u : 0u;
    uint64_t hash = Fnv(words, 16 * sizeof(DWORD));
    hash = Fnv(&flag, sizeof flag, hash);
    auto &bucket = samplers[hash];
    for (auto &rec : bucket) if (rec.comparison == flag && !memcmp(rec.words, words, 16 * sizeof(DWORD))) return rec.sampler;
    auto addr = [](DWORD a) { return a == D3DTADDRESS_WRAP ? wgpu::AddressMode::Repeat : a == D3DTADDRESS_MIRROR ? wgpu::AddressMode::MirrorRepeat : wgpu::AddressMode::ClampToEdge; };
    wgpu::SamplerDescriptor d{}; d.addressModeU = addr(words[D3DSAMP_ADDRESSU]); d.addressModeV = addr(words[D3DSAMP_ADDRESSV]); d.addressModeW = addr(words[D3DSAMP_ADDRESSW]);
    d.minFilter = words[D3DSAMP_MINFILTER] >= D3DTEXF_LINEAR ? wgpu::FilterMode::Linear : wgpu::FilterMode::Nearest;
    d.magFilter = words[D3DSAMP_MAGFILTER] >= D3DTEXF_LINEAR ? wgpu::FilterMode::Linear : wgpu::FilterMode::Nearest;
    d.mipmapFilter = words[D3DSAMP_MIPFILTER] >= D3DTEXF_LINEAR ? wgpu::MipmapFilterMode::Linear : wgpu::MipmapFilterMode::Nearest;
    d.lodMinClamp = float(words[D3DSAMP_MAXMIPLEVEL]); d.lodMaxClamp = words[D3DSAMP_MIPFILTER] == D3DTEXF_NONE ? d.lodMinClamp : 32.0f;
    if (comparison) d.compare = wgpu::CompareFunction::LessEqual;
    if (words[D3DSAMP_MINFILTER] == D3DTEXF_ANISOTROPIC) { d.minFilter = d.magFilter = wgpu::FilterMode::Linear; d.mipmapFilter = wgpu::MipmapFilterMode::Linear; d.maxAnisotropy = std::max<DWORD>(1, std::min<DWORD>(16, words[D3DSAMP_MAXANISOTROPY])); }
    SamplerRecord rec{}; memcpy(rec.words, words, sizeof rec.words); rec.comparison = flag; rec.sampler = device.CreateSampler(&d);
    bucket.push_back(std::move(rec));
    return bucket.back().sampler;
}
wgpu::BindGroup Executor::Textures(const DrawState &s, const Pipeline &p, unsigned stage) {
    const auto &info = *(stage ? s.pixelInfo : s.vertexInfo);
    GroupIdentity identity{};
    identity.pipelineId = p.id; identity.stage = uint8_t(stage);
    for (auto &sampler : info.samplers) {
        const unsigned r = sampler.reg;
        if (identity.count >= 16) break;
        auto &t = textures.at(s.textures[r]);
        auto &slot = identity.slots[identity.count++];
        // Uploading pixels does not replace the texture or its view. Retaining
        // content generations here created a new group on every animated update
        // and kept the obsolete groups alive until the texture was destroyed.
        slot.texture = s.textures[r]; slot.generation = stableTextureBindings ? 0 : t.generation;
        memcpy(slot.ss, s.ss[r], sizeof slot.ss);
        encoderTextures.insert(slot.texture);
    }
    uint64_t hash = Fnv(&identity.pipelineId, sizeof identity.pipelineId);
    hash = Fnv(&identity.stage, sizeof identity.stage, hash);
    hash = Fnv(&identity.count, sizeof identity.count, hash);
    for (uint8_t i = 0; i < identity.count; ++i) {
        hash = Fnv(&identity.slots[i].texture, sizeof identity.slots[i].texture, hash);
        hash = Fnv(&identity.slots[i].generation, sizeof identity.slots[i].generation, hash);
        const uint64_t words = Fnv(identity.slots[i].ss, sizeof identity.slots[i].ss);
        hash = Fnv(&words, sizeof words, hash);
    }
    if (auto bucket = groupsByHash.find(hash); bucket != groupsByHash.end()) {
        for (auto gid : bucket->second) {
            auto &rec = groups.at(gid);
            if (!memcmp(&rec.identity, &identity, sizeof identity)) return rec.group;
        }
    }
    std::vector<wgpu::BindGroupEntry> entries;
    for (auto &sampler : info.samplers) {
        const unsigned r = sampler.reg;
        auto &t = textures.at(s.textures[r]);
        wgpu::BindGroupEntry e{}; e.binding = r * 3; e.textureView = t.View(); entries.push_back(e);
        e = {}; e.binding = r * 3 + 1; e.sampler = Sampler(s.ss[r], t.depth); entries.push_back(e);
    }
    wgpu::BindGroupDescriptor d{}; d.layout = p.layouts[stage + 1]; d.entryCount = entries.size(); d.entries = entries.data();
    const uint64_t gid = nextGroup++;
    GroupRecord rec{}; rec.identity = identity; rec.hash = hash; rec.group = device.CreateBindGroup(&d); ++bindGroups;
    for (uint8_t i = 0; i < identity.count; ++i) {
        bool seen = false;
        for (uint8_t j = 0; j < rec.textureCount; ++j) seen |= rec.textures[j] == identity.slots[i].texture;
        if (seen) continue;
        rec.textures[rec.textureCount++] = identity.slots[i].texture;
        groupsByTexture[identity.slots[i].texture].push_back(gid);
    }
    groupsByHash[hash].push_back(gid);
    auto inserted = groups.emplace(gid, std::move(rec));
    return inserted.first->second.group;
}
void Executor::Execute(const DrawState &s) {
    // A cached pipeline can later receive a different texture format.
    if (!CanSample(s)) return;
    Pipeline *found = FindPipeline(s);
    if (!found || !found->handle) { Fail("pipeline unavailable"); return; }
    auto &p = *found;
    if (uniformOffset + Align(p.constantSizes[0], 256) + Align(p.constantSizes[1], 256) > UniformCapacity) RotateUniformArena();
    if (!p.constants) {
        wgpu::BindGroupEntry entries[2]{};
        for (unsigned i = 0; i < 2; ++i) { entries[i].binding = i; entries[i].buffer = uniforms; entries[i].size = p.constantSizes[i]; }
        wgpu::BindGroupDescriptor d{}; d.layout = p.layouts[0]; d.entryCount = 2; d.entries = entries; p.constants = device.CreateBindGroup(&d); ++bindGroups;
    }
    uint32_t offsets[2];
    for (unsigned stage = 0; stage < 2; ++stage) {
        if (previousConstants[stage].data != s.constants[stage].data) {
            previousOffsets[stage] = uint32_t(uniformOffset);
            memcpy(uniformBytes.data() + uniformOffset, s.constants[stage].data, s.constants[stage].size);
            previousConstants[stage] = s.constants[stage];
            uniformOffset += Align(p.constantSizes[stage], 256);
        }
        offsets[stage] = previousOffsets[stage];
    }
    wgpu::RenderPassColorAttachment color{}; color.view = textures.at(s.color.id).View(s.color.level, s.color.face, true); color.loadOp = wgpu::LoadOp::Load; color.storeOp = wgpu::StoreOp::Store;
    wgpu::RenderPassDepthStencilAttachment dep{};
    dep.depthClearValue = 1.0f; // Emdawn serializes this even for LoadOp::Load.
    wgpu::RenderPassDescriptor d{}; d.colorAttachmentCount = 1; d.colorAttachments = &color;
    // Disabling depth writes is a pipeline state, not a reason to split the
    // pass. Read-only attachment usage is needed when sampling this depth
    // texture at the same time. Avoid repeated load/store and state rebinding
    // between opaque and translucent draws, particularly on tiled GPUs.
    const bool readOnly = mergeDepth ? s.depth.id && !(s.zEnable && s.zWrite)
        && std::find(s.textures.begin(), s.textures.end(), s.depth.id) != s.textures.end() : !s.zEnable || !s.zWrite;
    if (s.depth.id) { auto &t = textures.at(s.depth.id); dep.view = t.View(s.depth.level, s.depth.face, true); dep.depthLoadOp = wgpu::LoadOp::Load; dep.depthStoreOp = wgpu::StoreOp::Store;
        if (readOnly) { dep.depthReadOnly = true; dep.depthLoadOp = wgpu::LoadOp::Undefined; dep.depthStoreOp = wgpu::StoreOp::Undefined; }
        if (t.stencil) { dep.stencilLoadOp = wgpu::LoadOp::Load; dep.stencilStoreOp = wgpu::StoreOp::Store; } d.depthStencilAttachment = &dep; }
    if (s.color.id) encoderTextures.insert(s.color.id);
    if (s.depth.id) encoderTextures.insert(s.depth.id);
    if (pass && (passColor.id != s.color.id || passColor.level != s.color.level || passColor.face != s.color.face || passDepth.id != s.depth.id || passDepth.level != s.depth.level || passDepth.face != s.depth.face || passDepthReadOnly != readOnly)) { EndPass(Target); }
    if (!encoder) encoder = device.CreateCommandEncoder();
    if (!pass) { pass = BeginPass(d); ++passes; passColor = s.color; passDepth = s.depth; passDepthReadOnly = readOnly; bound = {}; }
    if (bound.pipeline != p.handle.Get()) { pass.SetPipeline(p.handle); bound.pipeline = p.handle.Get(); }
    if (bound.constants != p.constants.Get() || memcmp(bound.offsets, offsets, sizeof(offsets))) {
        pass.SetBindGroup(0, p.constants, 2, offsets); bound.constants = p.constants.Get(); memcpy(bound.offsets, offsets, sizeof(offsets));
    }
    for (unsigned stage = 0; stage < 2; ++stage) {
        auto group = Textures(s, p, stage);
        if (bound.textures[stage] != group.Get()) { pass.SetBindGroup(stage + 1, group); bound.textures[stage] = group.Get(); }
    }
    for (unsigned slot = 0; slot < p.vertexStreams.size(); ++slot) {
        const unsigned stream = p.vertexStreams[slot];
        if (!s.buffers[stream]) {
            std::ostringstream error; error << "draw missing vertex stream=" << stream << " slot=" << slot
                << " recipe=" << p.hash << " pipeline=" << p.id << " stride=" << s.strides[stream];
            Fail(error.str()); return;
        }
        const auto resolved = Resolve(s.buffers[stream], s.offsets[stream]);
        if (bound.vertices[slot] != s.buffers[stream] || bound.vertexOffsets[slot] != s.offsets[stream]
            || bound.vertexHandles[slot] != resolved.buffer.Get() || bound.resolvedOffsets[slot] != resolved.offset || bound.resolvedSizes[slot] != resolved.size) {
            pass.SetVertexBuffer(slot, resolved.buffer, resolved.offset, resolved.size);
            bound.vertices[slot] = s.buffers[stream]; bound.vertexOffsets[slot] = s.offsets[stream];
            bound.vertexHandles[slot] = resolved.buffer.Get(); bound.resolvedOffsets[slot] = resolved.offset; bound.resolvedSizes[slot] = resolved.size;
        }
    }
    if (!bound.valid || memcmp(&bound.viewport, &s.viewport, sizeof(s.viewport))) {
        pass.SetViewport(s.viewport.X, s.viewport.Y, std::min<unsigned>(s.viewport.Width, s.color.w), std::min<unsigned>(s.viewport.Height, s.color.h), s.viewport.MinZ, s.viewport.MaxZ);
        bound.viewport = s.viewport;
    }
    RECT clip{0, 0, LONG(s.color.w), LONG(s.color.h)};
    if (s.scissorEnable) { const unsigned x = std::clamp<LONG>(s.scissor.left, 0, s.color.w), y = std::clamp<LONG>(s.scissor.top, 0, s.color.h);
        clip = {LONG(x), LONG(y), std::clamp<LONG>(s.scissor.right, x, s.color.w), std::clamp<LONG>(s.scissor.bottom, y, s.color.h)}; }
    if (!bound.valid || memcmp(&bound.scissor, &clip, sizeof(clip))) {
        pass.SetScissorRect(clip.left, clip.top, clip.right - clip.left, clip.bottom - clip.top); bound.scissor = clip;
    }
    const DWORD bc = s.blendFactor;
    if (!bound.valid || bound.blend != bc) {
        wgpu::Color blend{double((bc >> 16) & 255) / 255, double((bc >> 8) & 255) / 255, double(bc & 255) / 255, double(bc >> 24) / 255}; pass.SetBlendConstant(&blend); bound.blend = bc;
    }
    if (!bound.valid || bound.stencil != s.stencilRef) { pass.SetStencilReference(s.stencilRef); bound.stencil = s.stencilRef; }
    bound.valid = true;
    if (s.index) {
        const auto resolved = Resolve(s.index, 0);
        if (bound.index != s.index || bound.wide != s.wide || bound.indexHandle != resolved.buffer.Get() || bound.indexOffset != resolved.offset || bound.indexSize != resolved.size) {
            pass.SetIndexBuffer(resolved.buffer, s.wide ? wgpu::IndexFormat::Uint32 : wgpu::IndexFormat::Uint16, resolved.offset, resolved.size);
            bound.index = s.index; bound.wide = s.wide; bound.indexHandle = resolved.buffer.Get(); bound.indexOffset = resolved.offset; bound.indexSize = resolved.size;
        }
        pass.DrawIndexed(s.count, 1, s.first, s.base);
    }
    else pass.Draw(s.count, 1, s.first);
    ++draws;
}

void Executor::ProcessWarmup() {
    std::lock_guard lock(warmMu);
    while (!utilityJobs.empty() && (utilityJobs.front()->request->cancel || pipelineJobs < preparationJobs)) {
        auto job = std::move(utilityJobs.front()); utilityJobs.pop_front();
        auto request = job->request;
        if (request->cancel) { job->target->pending = false; ++request->utilityCompleted; continue; }
        ++pipelineJobs;
        device.CreateRenderPipelineAsync(&job->descriptor, wgpu::CallbackMode::AllowSpontaneous,
            [this, target = job->target, request](wgpu::CreatePipelineAsyncStatus status, wgpu::RenderPipeline pipeline, wgpu::StringView message) {
                target->pending = false; --pipelineJobs; ++request->utilityCompleted;
                if (status == wgpu::CreatePipelineAsyncStatus::Success) target->handle = std::move(pipeline);
                else { target->error.assign(message.data ? message.data : "utility compilation failed", message.length); ++request->utilityFailures; }
                Wake();
            });
    }
    if (!warmRequest || warmRequest->done) return;
    auto &r = *warmRequest;
    const double now = emscripten_get_now();
    if (!r.cacheRequested) {
        cacheKey = r.key; cacheToken = r.token; savedRecipes = savedObserved = 0; r.cacheRequested = true; r.cacheAt = now;
        auto scope = cacheScopes.try_emplace(r.key, uint32_t(cacheScopes.size() + 1)); cacheScope = scope.first->second;
        // Capture enumerates this build's frontend directly. Importing an
        // existing all-map catalog here would export it again for every map.
        if (r.capture) r.cacheReady = true;
        else cacheLoad((preparationMode == 2 ? r.profile : r.key).c_str(),
            (bestCasePreparation ? "*" : r.map.c_str()), r.key.c_str(), r.token, preparationMode == 2);
    }
    if (!r.cacheReady && now - r.cacheAt > 60000) { r.cacheReady = true; r.cacheOk = false; }
    if (r.catalog && !r.capture && r.cacheReady && r.cacheOk) { catalogReady = true; catalogProfile = r.profile; }
    if (r.cancel) { r.done = true; loadingWarm = false; }
    else while (!r.capture && r.cursor < r.recipes.size() && pipelineJobs < preparationJobs && emscripten_get_now() - now < 4) {
        auto recipe = r.recipes[r.cursor++];
        DrawState s; s.prepare = recipe; s.pipelineId = recipe->id; s.pipelineHash = recipe->hash;
        auto &pipeline = Prepare(s, false);
        if (pipeline.pending) r.pendingPipelines.push_back(pipeline.id);
        else { ++r.completed; if (!pipeline.error.empty() || !pipeline.handle) ++r.compileErrors; }
    }
    // Only in-flight work needs polling (bounded by compilation concurrency).
    // Completed recipes never re-enter the per-frame lookup path.
    for (auto it = r.pendingPipelines.begin(); it != r.pendingPipelines.end();) {
        auto found = pipelines.find(*it);
        if (found != pipelines.end() && found->second->pending) { ++it; continue; }
        ++r.completed;
        if (found == pipelines.end() || !found->second->error.empty()) ++r.compileErrors;
        it = r.pendingPipelines.erase(it);
    }
    size_t completed = r.completed, errors = r.compileErrors;
    errors += r.utilityFailures.load();
    r.failures = errors;
    const auto progress = completed + r.utilityCompleted.load();
    if (progress != r.lastCompleted || r.collecting) { r.lastCompleted = progress; r.progressAt = now; }
    if (!r.progressAt) r.progressAt = now;
    const size_t totalRecipes = r.capture ? 0 : r.recipes.size();
    // Compiled recipe/utility counts are the gate. A missing catalog or a late
    // upload fence must not hold the overlay at N/N; nostall compiles the rest.
    const bool compilesDone = !r.collecting && r.hiddenDrawsDone.load() && completed == totalRecipes
        && r.utilityCompleted == r.utilityTotal;
    const bool recipesReady = compilesDone;
    if (recipesReady && !r.draining && !r.done) { r.draining = true; r.drainAt = now; }
    if (!r.done && r.draining && Web_SettleFinished(pipelineJobs, now - r.drainAt, r.cancel)) {
        r.done = true; loadingWarm = false;
    }
    if (!r.done && !r.collecting && now - r.progressAt > 60000) { r.cacheOk = false; r.done = true; loadingWarm = false; }
    if (r.done && !r.cancel && !r.pruned && !pipelineJobs) {
        r.pruned = true;
        if (r.map == "@frontend") for (const auto &[id,p] : pipelines) frontendPipelines.insert(id);
        else if (!r.capture && !bestCasePreparation) {
            EndPass(); bound = {};
            for (auto it=pipelines.begin(); it!=pipelines.end();) {
                if (!frontendPipelines.count(it->first) && !r.seen.count(it->second->key)) it=pipelines.erase(it);
                else ++it;
            }
            pipelineAliases.clear(); pipelinesByHash.clear();
            for (const auto &[id,p] : pipelines) pipelinesByHash[p->hash].push_back(id);
            groups.clear(); groupsByHash.clear(); groupsByTexture.clear(); shaderModules.clear();
            std::lock_guard lock(recipeMu);
            for (auto it=recipeRegistry.begin(); it!=recipeRegistry.end();) {
                auto &bucket=it->second;
                std::erase_if(bucket, [](const auto &p){return p.use_count()==1;});
                if (bucket.empty()) it=recipeRegistry.erase(it); else ++it;
            }
            std::erase_if(translations, [](const auto &entry){return entry.second.use_count()==1;});
            sourcePool.Prune();
        }
    }
    if (r.done || now - warmReportAt >= 250) {
        warmReportAt = now; std::ostringstream json;
        json << "{\"warmup\":{\"done\":" << completed + r.utilityCompleted.load() << ",\"total\":" << totalRecipes + r.utilityTotal.load()
            << ",\"candidates\":" << (r.capture ? r.recipes.size() : 0)
            << ",\"active\":" << (r.done ? "false" : "true")
            << ",\"stage\":\"" << (r.capture ? "catalog-capture" : r.catalog ? "catalog" : r.draining && !r.done ? "settle" : "map") << "\",\"map\":\"" << r.map
            << "\",\"uploadedBytes\":" << r.uploadedBytes.load() << ",\"images\":" << r.images.load() << ",\"buffers\":" << r.buffers.load()
            << ",\"resourceBatches\":" << resourceBatches.load() << ",\"resourceCommands\":" << resourceCommands.load()
            << ",\"resourceBatchHighWater\":" << resourceBatchHighWater.load()
            << ",\"failures\":" << errors << ",\"rejected\":" << r.rejected.load()
            << ",\"pend\":" << pipelineJobs
            << ",\"catalogUsed\":" << (r.catalogUsed ? "true" : "false")
            << ",\"coverage\":\"loaded-materials-and-learned-recipes\""
            << ",\"status\":\"" << (!r.done ? "preparing" : r.cancel ? "skipped" :
                errors || r.rejected || completed != totalRecipes ? "incomplete" : r.capture ? "captured" : "complete") << "\"}}";
        reportJson(session.c_str(), json.str().c_str());
        if (r.done) fprintf(stderr, "WEB_GPU_WARMUP_DONE completed=%zu total=%zu candidates=%zu capture=%d cancelled=%d\n", completed, totalRecipes, r.capture ? r.recipes.size() : 0, r.capture, bool(r.cancel));
    }
    if (r.done && !r.cancel && !r.catalog && r.map != "@frontend") {
        mapReadyForSettle.store(true);
    }
}
void Executor::ProcessLateRecipes() {
    if (failed) return;
    std::vector<std::shared_ptr<const PrepareData>> pending;
    { std::lock_guard lock(lateRecipeMu); pending.swap(lateRecipes); }
    const double begin = emscripten_get_now();
    size_t i = 0;
    for (; i < pending.size(); ++i) {
        const unsigned cap = loadingWarm ? preparationJobs.load() : runtimeJobs.load();
        if (pipelineJobs >= cap || emscripten_get_now() - begin >= 4) break;
        auto &recipe = pending[i];
        if (!recipe) continue;
        DrawState s; s.prepare = recipe; s.pipelineId = recipe->id; s.pipelineHash = recipe->hash;
        Prepare(s, false);
    }
    if (i < pending.size()) {
        std::lock_guard lock(lateRecipeMu);
        lateRecipes.insert(lateRecipes.begin(), pending.begin() + i, pending.end());
    }
}
void Executor::UpdateMapSettle() {
    if (!settling && !settleStarted) return;
    const double now = emscripten_get_now();
    const unsigned pend = pipelineJobs;
    const double held = settleStarted ? now - settleBegan.load() : 0;
    if (settling && Web_SettleFinished(pend, held, settleCancel))
        settling.store(false);
    if (!settling) {
        if (!settleStarted || settleReportedDone.exchange(true)) return;
    } else if (now - settleReportAt < 250) return;
    settleReportAt = now;
    std::ostringstream json;
    json << "{\"warmup\":{\"done\":0,\"total\":" << std::max(1u, pend)
        << ",\"pend\":" << pend << ",\"active\":" << (settling ? "true" : "false")
        << ",\"stage\":\"settle\",\"map\":\"" << preparedMap << "\""
        << ",\"status\":\"" << (settling ? "preparing" : settleCancel ? "skipped" : "complete") << "\"}}";
    reportJson(session.c_str(), json.str().c_str());
}
void Executor::SaveRecipes() {
    if (cacheKey.empty() || loadingWarm || emscripten_get_now() - cacheSaveAt < 5000) return;
    cacheSaveAt = emscripten_get_now();
    if (!cacheWriterStarted) return;
    CacheWriteJob job; job.key = cacheKey;
    {
        std::lock_guard lock(warmMu);
        const bool capture = warmRequest && warmRequest->token == cacheToken && warmRequest->capture;
        if (capture) job.recipes = warmRequest->recipes;
        for (const auto &[id, p] : pipelines) if (p->handle && p->recipe) {
            if (p->observedScopes.count(cacheScope)) job.observed.push_back(p->hash);
            if (capture ? p->observedScopes.count(cacheScope) && !warmRequest->seen.count(p->recipe->key) :
                Web_PersistWarmRecipe(false, p->learned) && p->learnedScopes.count(cacheScope)) job.recipes.push_back(p->recipe);
        }
    }
    const size_t count = job.recipes.size(), observed = job.observed.size();
    if (!count || (count == savedRecipes && observed == savedObserved)) return;
    {
        std::lock_guard lock(cacheWriterMu);
        // New learning supersedes a pending snapshot for the same map.
        for (auto it = cacheWriteJobs.begin(); it != cacheWriteJobs.end();)
            if (it->key == job.key) it = cacheWriteJobs.erase(it); else ++it;
        if (cacheWriteJobs.size() >= 4) cacheWriteJobs.pop_front();
        cacheWriteJobs.push_back(std::move(job));
    }
    cacheWriterCv.notify_one();
    savedRecipes = count; savedObserved = observed;
}
void Executor::SendTelemetry() {
    const double now = emscripten_get_now();
    if (now - telemetryAt < 250) return;
    telemetryAt = now;
    std::ostringstream json;
    json << "{\"renderer\":\"webgpu\",\"gpu\":{\"submitted\":" << frames
        << ",\"intervalMs\":" << (intervals.empty() ? 0 : intervals.back())
        << ",\"draws\":" << draws - statDraws << ",\"passes\":" << passes - statPasses
        << ",\"flushes\":" << flushes - statFlushes << ",\"bindGroups\":" << bindGroups - statGroups
        << ",\"pipelines\":" << pipelines.size() << ",\"pipelineJobs\":" << pipelineJobs
        << ",\"groupsStored\":" << groups.size() << ",\"texturesStored\":" << textures.size()
        << ",\"cacheQueuedRecipes\":" << savedRecipes
        << ",\"cacheQueuedObserved\":" << savedObserved
        << ",\"heapBytes\":" << emscripten_get_heap_size() << ",\"programBreakBytes\":" << reinterpret_cast<uintptr_t>(sbrk(0))
        << ",\"timestampQueries\":" << (timestampQueries ? "true" : "false") << "},\"submissionIntervals\":[";
    for (size_t i = 0; i < intervals.size(); ++i) { if (i) json << ','; json << intervals[i]; }
    auto array = [&](const char *name, std::vector<std::string> &values) {
        json << "],\"" << name << "\":[";
        for (size_t i = 0; i < values.size(); ++i) { if (i) json << ','; json << values[i]; }
        values.clear();
    };
    array("gpuFrames", frameTelemetry); array("gpuEvents", eventTelemetry); json << "]}";
    reportJson(session.c_str(), json.str().c_str()); intervals.clear();
    statDraws = draws; statPasses = passes; statFlushes = flushes; statGroups = bindGroups;
}
void Executor::FrameDone(Batch &b) {
    const double now = emscripten_get_now(); ++frames;
    const double intervalMs = lastFrame ? now - lastFrame : 0;
    if (lastFrame) intervals.push_back(now - lastFrame);
    lastFrame = now;
    if (profiling || intervalMs > 50) {
        std::ostringstream json; json << "{\"frameId\":" << b.frameId << ",\"submittedAt\":" << now
            << ",\"at\":" << std::fixed << emscripten_date_now() << ",\"intervalMs\":" << intervalMs
            << ",\"executorMs\":" << b.cpuMs << ",\"producerQueueWaitMs\":" << b.queueWaitMs
            << ",\"ownerQueueWaitMs\":" << b.ownerQueueWaitMs
            << ",\"translationMs\":" << b.translationMs << ",\"pipelineMisses\":" << b.pipelineMisses
            << ",\"skippedDraws\":" << b.skippedDraws
            << ",\"pipelineWaitMs\":" << b.pipelineWaitMs << ",\"uploadBytes\":" << b.uploadBytes
            << ",\"hoistedBytes\":" << b.hoistedBytes << ",\"draws\":" << draws - b.initialDraws
            << ",\"firstTextureUploads\":" << b.firstTextureUploads
            << ",\"firstStaticBufferUploads\":" << b.firstStaticBufferUploads
            << ",\"passes\":" << passes - b.initialPasses << ",\"uploadCalls\":" << uploads - b.initialUploads
            << ",\"passEnds\":[";
        for (unsigned i = 0; i < b.passEnds.size(); ++i) { if (i) json << ','; json << b.passEnds[i]; }
        json << "]}"; frameTelemetry.push_back(json.str());
    }
    SendTelemetry();
}
wgpu::RenderPassEncoder Executor::BeginPass(wgpu::RenderPassDescriptor &desc, bool last) {
    wgpu::PassTimestampWrites writes{};
    if (active && active->timingSlot >= 0) {
        auto &slot = timing[active->timingSlot];
        if (!slot.began || last) {
            writes.querySet = slot.queries;
            // The pinned JS bridge serializes the undefined-index sentinel as
            // 4294967295. Supply two valid indices for each timestamped pass.
            writes.beginningOfPassWriteIndex = slot.began ? 2 : 0;
            writes.endOfPassWriteIndex = last ? 3 : 1;
            slot.began = true;
            desc.timestampWrites = &writes;
        }
    }
    auto result = encoder.BeginRenderPass(&desc); desc.timestampWrites = nullptr; return result;
}
void Executor::FinishTiming(Batch &batch) {
    if (batch.timingSlot < 0) return;
    auto &slot = timing[batch.timingSlot];
    if (!slot.began) { slot.busy = false; batch.timingSlot = -1; return; }
    if (!encoder) encoder = device.CreateCommandEncoder();
    EndPass(); encoder.ResolveQuerySet(slot.queries, 0, 4, slot.resolve, 0);
    encoder.CopyBufferToBuffer(slot.resolve, 0, slot.readback, 0, 32);
}
void Executor::ReadTiming(int index, uint64_t frameId) {
    auto &slot = timing[index];
    slot.readback.MapAsync(wgpu::MapMode::Read, 0, 32, wgpu::CallbackMode::AllowSpontaneous,
        [this, index, frameId](wgpu::MapAsyncStatus status, wgpu::StringView) {
            auto &slot = timing[index];
            if (status == wgpu::MapAsyncStatus::Success) {
                uint64_t times[4]{}; memcpy(times, slot.readback.GetConstMappedRange(0, 32), 32); slot.readback.Unmap();
                if (times[3] >= times[0]) {
                    std::ostringstream json; json << "{\"frameId\":" << frameId << ",\"gpuSpanMs\":" << double(times[3] - times[0]) / 1000000.0 << "}";
                    eventTelemetry.push_back(json.str());
                }
            }
            slot.busy = false;
        });
}
void Executor::PrepareAhead(Batch &batch) {
    const double begin = emscripten_get_now();
    const unsigned jobLimit = loadingWarm ? preparationJobs.load() : runtimeJobs.load();
    while (batch.prepareCursor < batch.commands.size() && pipelineJobs < jobLimit && emscripten_get_now() - begin < 1) {
        const auto &cmd = batch.commands[batch.prepareCursor++];
        if (cmd.draw) Prepare(*cmd.draw, false);
    }
}
void Executor::Tick() {
    SendTelemetry();
    if (failed || started != 1) return;
    ProcessWarmup();
    ProcessLateRecipes();
    UpdateMapSettle();
    SaveRecipes();
    Batch batch;
    { std::lock_guard lock(queueMu); if (batches.empty() || inflight >= 2) return; batch = std::move(batches.front()); batches.pop_front(); }
    const double tickAt = emscripten_get_now();
    if (mapReadyForSettle && !batch.preparation) {
        if (!settleStarted.exchange(true) && !settleCancel) {
            settleBegan.store(tickAt);
            settling.store(true);
        }
    }
    Adopt(batch);
    if (!batch.resourcesReady) {
        batch.ownerQueueWaitMs = std::max(0.0, tickAt - batch.queuedAt);
        batch.initialDraws = draws; batch.initialPasses = passes; batch.initialUploads = uploads;
        if (!batch.preparation && timestampQueries) for (int i = 0; i < int(timing.size()); ++i) if (!timing[i].busy) {
            batch.timingSlot = i; timing[i].busy = true; timing[i].began = false; break;
        }
        PrepareUploads(batch);
    }
    PrepareAhead(batch);
    for (auto &cmd : batch.commands) {
        if (cmd.draw) {
            auto &p = Prepare(*cmd.draw);
            if (failed) { ReleaseBatch(); return; }
            if (!p.handle) {
                if (pipelineNostall && !batch.preparation) {
                    ++batch.pipelineMisses;
                    ++batch.skippedDraws;
                    if (!p.pending && cmd.draw->prepare) QueueLateRecipe(cmd.draw->prepare);
                    std::ostringstream event; event << "{\"kind\":\"pipeline-skip\",\"at\":" << std::fixed << emscripten_date_now()
                        << ",\"frameId\":" << batch.frameId << ",\"recipeHash\":\"" << cmd.draw->pipelineHash << "\"}";
                    eventTelemetry.push_back(event.str());
                    continue;
                }
                ++waits;
                ++batch.pipelineMisses;
                if (!batch.blockedAt) batch.blockedAt = emscripten_get_now();
                const size_t i = &cmd - batch.commands.data();
                batch.commands.erase(batch.commands.begin(), batch.commands.begin() + i);
                batch.prepareCursor = batch.prepareCursor > i ? batch.prepareCursor - i : 0;
                batch.cpuMs += emscripten_get_now() - tickAt;
                ReleaseBatch();
                std::lock_guard lock(queueMu); batches.push_front(std::move(batch)); return;
            }
            if (batch.blockedAt) { batch.pipelineWaitMs += emscripten_get_now() - batch.blockedAt; batch.blockedAt = 0; }
            Execute(*cmd.draw);
        } else if (cmd.upload.id && !cmd.upload.hoisted) {
            const auto &u = cmd.upload; UploadFromImmediate(u.id, u.dest, u.source, u.size);
        } else if (cmd.run) cmd.run(*this);
        if (failed) { ReleaseBatch(); return; }
    }
    FinishTiming(batch);
    Flush(); uniformOffset = uploadedUniformOffset = 0; previousConstants[0] = previousConstants[1] = {};
    // Upload-only batches may never create a command encoder. Explicitly
    // submit their queue work before installing the readiness fence.
    if (batch.preparation) device.GetQueue().Submit(0, nullptr);
    batch.cpuMs += emscripten_get_now() - tickAt;
    if (!batch.preparation) FrameDone(batch);
    ReleaseBatch();
    { std::lock_guard lock(queueMu); queuedBytes -= batch.bytes; }
    queueSpace.notify_all();
    ++inflight;
    const double submittedAt = emscripten_get_now();
    const auto frameId = batch.frameId; const int slot = batch.timingSlot;
    device.GetQueue().OnSubmittedWorkDone(wgpu::CallbackMode::AllowSpontaneous,
        [this, submittedAt, frameId, completion = batch.completion](wgpu::QueueWorkDoneStatus status, wgpu::StringView) {
            if (status != wgpu::QueueWorkDoneStatus::Success) Fail("GPU submission failed");
            if (completion && status == wgpu::QueueWorkDoneStatus::Success) completion->store(true);
            if (inflight) --inflight;
            if (profiling) { std::ostringstream json; json << "{\"frameId\":" << frameId << ",\"queueCompletionMs\":" << emscripten_get_now() - submittedAt << "}"; eventTelemetry.push_back(json.str()); }
            Wake();
        });
    if (slot >= 0) ReadTiming(slot, frameId);
}

void MakeTexture(uint64_t id, unsigned w, unsigned h, unsigned layers, unsigned levels, unsigned type, D3DFORMAT fmt) {
    Enqueue([=](Executor &e) {
        if (e.textures.count(id)) return;
        const auto *fi = kd::FindFmt(fmt);
        wgpu::TextureDescriptor d{}; d.size = {w, h, layers}; d.mipLevelCount = levels; d.format = Format(fmt);
        d.dimension = type == 2 ? wgpu::TextureDimension::e3D : wgpu::TextureDimension::e2D;
        d.usage = wgpu::TextureUsage::TextureBinding | wgpu::TextureUsage::CopySrc | wgpu::TextureUsage::CopyDst;
        if (fi && (fi->depth || fi->colorRenderable)) d.usage |= wgpu::TextureUsage::RenderAttachment;
        if (fmt == D3DFMT_A8R8G8B8 || fmt == D3DFMT_X8R8G8B8 || fmt == D3DFMT_A8B8G8R8 || fmt == D3DFMT_X8B8G8R8) d.usage |= wgpu::TextureUsage::RenderAttachment;
        e.textures.emplace(id, Texture{e.device.CreateTexture(&d), d.format, w, h, layers, levels, type, fi && fi->depth, fi && fi->stencil});
    });
}
std::vector<uint8_t> ConvertTexels(const kd::TexCore &t, unsigned l, const std::vector<uint8_t> &src) {
    if (!t.fi || t.fi->compressed) return src;
    const size_t pixels = size_t(t.levelW(l)) * t.levelH(l) * t.levelD(l);
    if (t.fmt == D3DFMT_L16 || t.fmt == D3DFMT_G16R16 || t.fmt == D3DFMT_A16B16G16R16) {
        // Core WebGPU has no 16-bit normalized texture formats. Float32 keeps
        // every normalized source value without reducing its precision.
        const size_t components = t.fi->cpuBytes / 2;
        std::vector<uint8_t> out(pixels * components * sizeof(float));
        for (size_t i = 0; i < pixels * components; ++i) {
            uint16_t value; memcpy(&value, src.data() + i * 2, 2);
            const float normalized = float(value) / 65535.0f;
            memcpy(out.data() + i * sizeof(float), &normalized, sizeof(float));
        }
        return out;
    }
    if (t.fmt == D3DFMT_A2R10G10B10) {
        std::vector<uint8_t> out(src);
        for (size_t i = 0; i < pixels; ++i) {
            uint32_t value; memcpy(&value, src.data() + i * 4, 4);
            value = (value & 0xc00ffc00u) | ((value & 0x3ffu) << 20) | ((value >> 20) & 0x3ffu);
            memcpy(out.data() + i * 4, &value, 4);
        }
        return out;
    }
    if (t.fmt == D3DFMT_R5G6B5) {
        std::vector<uint8_t> out(pixels * 4);
        for (size_t i = 0; i < pixels; ++i) {
            const uint16_t value = src[i * 2] | uint16_t(src[i * 2 + 1]) << 8;
            auto *d = out.data() + i * 4;
            d[0] = ((value >> 11) & 31) * 255 / 31;
            d[1] = ((value >> 5) & 63) * 255 / 63;
            d[2] = (value & 31) * 255 / 31; d[3] = 255;
        }
        return out;
    }
    if (t.fi->conv != kd::CV_NONE || t.fi->swz == kd::SWZ_BGRA || t.fi->swz == kd::SWZ_BGR1 || t.fi->swz == kd::SWZ_RGB1) {
        std::vector<uint8_t> out(pixels * 4);
        for (size_t i = 0; i < pixels; ++i) {
            const uint8_t *s = src.data() + i * t.fi->cpuBytes; uint8_t *d = out.data() + i * 4;
            if (t.fi->cpuBytes >= 3) { d[0] = s[2]; d[1] = s[1]; d[2] = s[0]; d[3] = t.fi->cpuBytes == 4 && t.fi->swz != kd::SWZ_BGR1 ? s[3] : 255;
                if (t.fi->swz == kd::SWZ_RGB1) { d[0] = s[0]; d[2] = s[2]; d[3] = 255; } }
            else { const uint16_t v = s[0] | uint16_t(s[1]) << 8;
                if (t.fi->conv == kd::CV_A4R4G4B4 || t.fi->conv == kd::CV_X4R4G4B4) { d[0] = ((v >> 8) & 15) * 17; d[1] = ((v >> 4) & 15) * 17; d[2] = (v & 15) * 17; d[3] = t.fi->conv == kd::CV_X4R4G4B4 ? 255 : (v >> 12) * 17; }
                else { d[0] = ((v >> 10) & 31) * 255 / 31; d[1] = ((v >> 5) & 31) * 255 / 31; d[2] = (v & 31) * 255 / 31; d[3] = t.fi->conv == kd::CV_X1R5G5B5 || (v & 32768) ? 255 : 0; } }
        }
        return out;
    }
    return src;
}
uint64_t TextureId(kd::TexCore *t) {
    if (!t) return 0;
    std::lock_guard lock(t->mu); const bool fresh = !t->gpuId; const uint64_t id = Id(t->gpuId);
    if (!fresh && !t->anyDirty) return id;
    if (fresh) MakeTexture(id, t->w, t->h, t->type == 1 ? 6 : t->d, t->levels, t->type, t->fmt);
    for (unsigned f = 0; f < unsigned(t->faces()); ++f) for (unsigned l = 0; l < t->levels; ++l) {
        auto &lv = t->lv[f * t->levels + l]; if ((!fresh && !lv.dirty) || lv.data.empty()) continue;
        auto bytes = ConvertTexels(*t, l, lv.data);
        const unsigned w = t->fi->compressed ? unsigned(Align(t->levelW(l), 4)) : t->levelW(l);
        const unsigned h = t->fi->compressed ? unsigned(Align(t->levelH(l), 4)) : t->levelH(l), d = t->levelD(l);
        const unsigned rows = unsigned(kd::LevelRows(*t->fi, h));
        const unsigned pitch = unsigned(bytes.size() / (size_t(rows) * d));
        const uint64_t size = bytes.size();
        Enqueue([=, bytes = std::move(bytes)](Executor &e) {
            if (e.encoderTextures.count(id)) e.Flush();
            auto &tex = e.textures.at(id); wgpu::TexelCopyTextureInfo dst{}; dst.texture = tex.handle; dst.mipLevel = l; dst.origin.z = f; dst.aspect = wgpu::TextureAspect::All;
            wgpu::TexelCopyBufferLayout layout{}; layout.bytesPerRow = pitch; layout.rowsPerImage = rows;
            wgpu::Extent3D extent{w, h, d}; e.device.GetQueue().WriteTexture(&dst, bytes.data(), bytes.size(), &layout, &extent); ++tex.generation; ++e.uploads;
        }, size);
        lv.dirty = false;
    }
    if (fresh && !t->isRT() && !t->isDS()) {
        Enqueue([id](Executor &e) {
            if (e.active) ++e.active->firstTextureUploads;
            if (profiling || (e.active && !e.active->preparation)) {
                std::ostringstream event; event << "{\"frameId\":" << (e.active ? e.active->frameId : 0)
                    << ",\"kind\":\"texture-first-upload\",\"at\":" << std::fixed << emscripten_date_now()
                    << ",\"firstTextureUpload\":true,\"textureId\":" << id
                    << ",\"preparation\":" << (e.active && e.active->preparation ? "true" : "false") << "}";
                e.eventTelemetry.push_back(event.str());
            }
        });
    }
    t->anyDirty = false;
    if (resourceRecording) {
        Enqueue([id](Executor &e) { e.textures.at(id).View(); });
        SubmitResources(*resourceRecording, false);
    }
    return id;
}
Surface SurfaceId(kd::KSurface *s) {
    if (!s) return {};
    uint64_t id;
    if (s->parent) id = TextureId(s->parent);
    else { const bool fresh = !s->gpuId; id = Id(s->gpuId); if (fresh) MakeTexture(id, s->w, s->h, 1, 1, 0, s->fmt); }
    return {id, s->level, s->face, s->w, s->h};
}
uint64_t BufferId(kd::BufferCore &b) {
    std::lock_guard lock(b.mu); const bool fresh = !b.gpuId; const uint64_t id = Id(b.gpuId);
    auto &target = resourceRecording ? *resourceRecording : recording;
    if (fresh) { Command cmd; cmd.bufferId = id; cmd.bufferSize = Align(b.size, 4); target.commands.push_back(std::move(cmd)); }
    auto upload = [&](const uint8_t *src, unsigned off, unsigned n, unsigned begin, unsigned end) {
        if (!n) return; const Span span = PackImmediate(target, src, n);
        Command cmd; cmd.upload = {id, b.gpuGeneration, off, span.offset, span.size, begin, end}; target.commands.push_back(std::move(cmd));
        target.uploadBytes += span.size;
    };
    if (fresh && !(b.usage & D3DUSAGE_DYNAMIC) && !b.data.empty()) upload(b.data.data(), 0, b.size, 0, b.size);
    else if (b.dirtyHi > b.dirtyLo && !b.data.empty()) { const unsigned off = b.dirtyLo & ~3u, end = std::min<unsigned>(b.size, Align(b.dirtyHi, 4)); upload(b.data.data() + off, off, end - off, b.dirtyLo, b.dirtyHi); }
    if (b.external && b.externalBytes) { upload(b.external, b.externalOff, b.externalBytes, b.externalOff, b.externalOff + b.externalBytes); b.external = nullptr; b.externalBytes = 0; }
    if (fresh && !(b.usage & D3DUSAGE_DYNAMIC)) {
        Enqueue([id](Executor &e) {
            if (e.active) ++e.active->firstStaticBufferUploads;
            if (profiling || (e.active && !e.active->preparation)) {
                std::ostringstream event; event << "{\"frameId\":" << (e.active ? e.active->frameId : 0)
                    << ",\"firstStaticBufferUpload\":true,\"bufferId\":" << id
                    << ",\"kind\":\"static-buffer-first-upload\",\"at\":" << std::fixed << emscripten_date_now()
                    << ",\"preparation\":" << (e.active && e.active->preparation ? "true" : "false") << "}";
                e.eventTelemetry.push_back(event.str());
            }
        });
    }
    b.dirtyLo = 0xFFFFFFFFu; b.dirtyHi = 0; return id;
}
uint64_t TemporaryBuffer(const void *ptr, unsigned n) {
    const uint64_t id = nextId.fetch_add(1);
    recording.spans[id] = PackImmediate(ptr, n);
    return id;
}
void *OwnerMain(void *) {
    auto &e = executor;
    ownerThread = pthread_self(); ownerQueue = em_proxying_queue_create();
    cacheChannel(session.c_str());
    e.instance = wgpu::CreateInstance();
    wgpu::EmscriptenSurfaceSourceCanvasHTMLSelector canvas{}; canvas.selector = "#game";
    wgpu::SurfaceDescriptor surface{}; surface.nextInChain = &canvas; e.surface = e.instance.CreateSurface(&surface);
    wgpu::RequestAdapterOptions options{}; options.powerPreference = wgpu::PowerPreference::HighPerformance; options.compatibleSurface = e.surface;
    e.instance.RequestAdapter(&options, wgpu::CallbackMode::AllowSpontaneous,
        [&e](wgpu::RequestAdapterStatus status, wgpu::Adapter adapter, wgpu::StringView message) {
            if (status != wgpu::RequestAdapterStatus::Success) { Fail(std::string(message.data, message.length)); return; }
            e.adapter = std::move(adapter);
            if (!e.adapter.HasFeature(wgpu::FeatureName::TextureCompressionBC)) { Fail("missing texture-compression-bc"); return; }
            std::vector<wgpu::FeatureName> features{wgpu::FeatureName::TextureCompressionBC};
            for (auto f : {wgpu::FeatureName::Float32Filterable, wgpu::FeatureName::Float32Blendable})
                if (e.adapter.HasFeature(f)) features.push_back(f);
            if (profiling && e.adapter.HasFeature(wgpu::FeatureName::TimestampQuery)) features.push_back(wgpu::FeatureName::TimestampQuery);
            wgpu::DeviceDescriptor d{}; d.requiredFeatureCount = features.size(); d.requiredFeatures = features.data();
            // Dawn's own validation runs before the browser's. Skip it on the play device.
            // gpuValidation=1 leaves the toggle off. Robustness stays enabled.
            struct DawnSkipValidation : wgpu::ChainedStruct {
                size_t enabledToggleCount = 0;
                char const * const * enabledToggles = nullptr;
                size_t disabledToggleCount = 0;
                char const * const * disabledToggles = nullptr;
            };
            const char *const skipValidation[] = {"skip_validation"};
            DawnSkipValidation toggles;
            if (!gpuValidation.load()) {
                toggles.sType = static_cast<wgpu::SType>(0x0005000Au);
                toggles.enabledToggleCount = 1;
                toggles.enabledToggles = skipValidation;
                d.nextInChain = &toggles;
            }
            d.SetUncapturedErrorCallback([](const wgpu::Device &, wgpu::ErrorType, wgpu::StringView message) { Fail(std::string(message.data, message.length)); });
            d.SetDeviceLostCallback(wgpu::CallbackMode::AllowSpontaneous, [](const wgpu::Device &, wgpu::DeviceLostReason, wgpu::StringView message) { Fail(std::string(message.data, message.length)); });
            e.adapter.RequestDevice(&d, wgpu::CallbackMode::AllowSpontaneous,
                [&e](wgpu::RequestDeviceStatus status, wgpu::Device device, wgpu::StringView message) {
                    if (status != wgpu::RequestDeviceStatus::Success) { Fail(std::string(message.data, message.length)); return; }
                    e.device = std::move(device);
                    e.float32Filterable = e.device.HasFeature(wgpu::FeatureName::Float32Filterable);
                    e.float32Blendable = e.device.HasFeature(wgpu::FeatureName::Float32Blendable);
                    fprintf(stderr, "WEB_GPU_FEATURES float32-filterable=%d float32-blendable=%d\n", e.float32Filterable, e.float32Blendable);
                    wgpu::SurfaceCapabilities caps{}; e.surface.GetCapabilities(e.adapter, &caps); e.canvasFormat = caps.formats[0]; e.Configure();
                    wgpu::BufferDescriptor b{}; b.size = UniformCapacity; b.usage = wgpu::BufferUsage::Uniform | wgpu::BufferUsage::CopyDst; e.uniforms = e.device.CreateBuffer(&b); e.uniformBytes.resize(UniformCapacity);
                    e.timestampQueries = profiling && e.device.HasFeature(wgpu::FeatureName::TimestampQuery);
                    if (e.timestampQueries) for (auto &slot : e.timing) {
                        wgpu::QuerySetDescriptor q{}; q.type = wgpu::QueryType::Timestamp; q.count = 4; slot.queries = e.device.CreateQuerySet(&q);
                        wgpu::BufferDescriptor buffer{}; buffer.size = 32; buffer.usage = wgpu::BufferUsage::QueryResolve | wgpu::BufferUsage::CopySrc;
                        slot.resolve = e.device.CreateBuffer(&buffer); buffer.usage = wgpu::BufferUsage::MapRead | wgpu::BufferUsage::CopyDst;
                        slot.readback = e.device.CreateBuffer(&buffer);
                    }
                    started.store(1); fprintf(stderr, "WEB_GPU_DEVICE_READY owner=dawn-pthread\n");
                });
        });
    emscripten_set_main_loop_arg([](void *) { executor.Tick(); }, nullptr, 0, true);
    return nullptr;
}
} // namespace

bool Requested() { return requested.load(); }
void CancelWarmup() {
    std::lock_guard lock(warmMu); if (warmRequest) warmRequest->cancel = true;
    settleCancel.store(true);
    settling.store(false);
}
bool Settling() {
    return settling.load();
}
std::string preparedProfile;
std::string PreparationProfile() {
    std::ostringstream key; key << "recipes-v4/" << KISAK_GPU_CACHE_VERSION
        << '/' << executor.float32Filterable << '/' << executor.float32Blendable;
    for (auto name : {"r_picmip","r_picmip_bump","r_picmip_spec","sm_enable","r_dof_enable",
                      "web_disableDistortion","web_disableMotionBlur","web_reduceDynamicLights","r_aaSamples",
                      "r_polygonOffsetBias","r_polygonOffsetScale","sm_polygonOffsetBias","sm_polygonOffsetScale"}) {
        key << '/' << name << '=' << PipelineSettingValue(Dvar_FindVar(name));
    }
    return key.str();
}
void WarmMap(const char *map, int localClientNum) {
    if (!Web_ShaderWarmingEnabled()) return;
    if (!Requested() || started != 1 || preparationMode == 0) return;
    const auto profile = PreparationProfile();
    if (preparedMap == map && preparedProfile == profile && preparedGeneration == mapGeneration) return;
    auto request = std::make_shared<WarmRequest>(); request->token = ++warmSequence;
    request->map = map; request->catalog = localClientNum < 0;
    request->capture = captureRecipes;
    request->profile = PreparationProfile(); request->key = request->profile + "/" + map;
    request->hiddenDrawsDone = request->catalog || request->capture;
    preparedProfile = request->profile;
    { std::lock_guard lock(warmMu); if (catalogProfile != request->profile) catalogReady = false; warmRequest = request; loadingWarm = true; }
    const double begin = emscripten_get_now();
    double keepaliveAt = 0;
    auto keepalive = [&] {
        const double now = emscripten_get_now();
        if (localClientNum >= 0 && now - keepaliveAt >= 1000) { CL_WritePacket(localClientNum); keepaliveAt = now; }
    };
    // Dependency snapshots must follow pending DB loads, including late model
    // registrations. Taking them before synchronization silently omits assets.
    DB_SyncXAssets();
    { std::lock_guard lock(dependencyMu); pendingDependencies.clear(); }
    preparedMap = map; preparedGeneration = mapGeneration;
    // Track actual mesh usage instead of multiplying every loaded material by
    // all eighteen layouts. World and model assets are the priority candidates.
    std::map<Material *, unsigned> usage;
    if (rgp.world) for (int i = 0; i < rgp.world->surfaceCount; ++i) {
        auto *material = rgp.world->dpvs.surfaces[i].material; if (material) usage[material] |= 1;
    }
    DB_EnumXAssets_FastFile(ASSET_TYPE_XMODEL, [](XAssetHeader h, void *p) {
        auto &usage = *static_cast<std::map<Material *, unsigned> *>(p);
        auto *model = h.model; if (!model || !model->materialHandles) return;
        for (unsigned i = 0; i < model->numsurfs; ++i) if (model->materialHandles[i]) usage[model->materialHandles[i]] |= 2;
    }, &usage, false);
    DB_EnumXAssets_FastFile(ASSET_TYPE_WEAPON, [](XAssetHeader h, void *p) {
        auto *variant = h.weapon;
        if (!variant || !variant->weapDef) return;
        auto &usage = *static_cast<std::map<Material *, unsigned> *>(p);
        auto add = [&](XModel *model) {
            if (!model || !model->materialHandles) return;
            for (unsigned i = 0; i < model->numsurfs; ++i) if (model->materialHandles[i]) usage[model->materialHandles[i]] |= 2;
        };
        if (variant->weapDef->gunXModel)
            for (int i = 0; i < 16 && variant->weapDef->gunXModel[i]; ++i) add(variant->weapDef->gunXModel[i]);
        add(variant->weapDef->handXModel);
    }, &usage, false);
    std::vector<Material *> materials;
    for (auto [material, flags] : usage) materials.push_back(material);
    DB_EnumXAssets_FastFile(ASSET_TYPE_MATERIAL, [](XAssetHeader h, void *p) {
        auto &materials = *static_cast<std::vector<Material *> *>(p);
        if (h.material) materials.push_back(h.material);
    }, &materials, false);
    std::set<Material *> visited;
    std::unordered_set<const void *> budgetedSources;
    size_t recipeBudget = 0;
    if (preparationMode == 2) {
        // Snapshot COM references under DBHASH; never wait for queue space
        // while holding the database lock that loading/rendering may need.
        R_SyncRenderThread();
        std::vector<IDirect3DBaseTexture9 *> images;
        DB_EnumXAssets_FastFile(ASSET_TYPE_IMAGE, [](XAssetHeader header, void *ptr) {
            auto *image = header.image;
            if (!image || image->delayLoadPixels || !image->texture.basemap) return;
            auto *texture = kd::TexCoreOf(image->texture.basemap);
            if (!texture || texture->isRT() || texture->isDS()) return;
            image->texture.basemap->AddRef();
            static_cast<std::vector<IDirect3DBaseTexture9 *> *>(ptr)->push_back(image->texture.basemap);
        }, &images, false);
        Batch resourceBatch;
        resourceRecording = &resourceBatch;
        for (auto *image : images) {
            keepalive();
            if (!request->cancel && !failed) {
                auto *texture = kd::TexCoreOf(image);
                uint64_t bytes = 0;
                { std::lock_guard lock(texture->mu); for (const auto &level : texture->lv) if (!texture->gpuId || level.dirty) bytes += level.data.size(); }
                TextureId(texture); ++request->images; request->uploadedBytes += bytes;
            }
            image->Release();
        }
        SubmitResources(resourceBatch);
        // Static meshes are lazy GPU resources too. Preparing only images left
        // world layers and models to allocate/upload on their first visible draw.
        // Hold COM references while snapshotting; never block under DBHASH.
        std::set<IDirect3DVertexBuffer9 *> vertexBuffers;
        std::set<IDirect3DIndexBuffer9 *> indexBuffers;
        if (rgp.world) {
            for (auto *buffer : {rgp.world->draw.vd.worldVb, rgp.world->draw.vld.layerVb})
                if (buffer && vertexBuffers.insert(buffer).second) buffer->AddRef();
        }
        struct MeshBuffers {
            std::set<IDirect3DVertexBuffer9 *> &vertices;
            std::set<IDirect3DIndexBuffer9 *> &indices;
        } meshBuffers{vertexBuffers, indexBuffers};
        DB_EnumXAssets_FastFile(ASSET_TYPE_XMODEL, [](XAssetHeader header, void *ptr) {
            auto *model = header.model;
            if (!model || !model->surfs) return;
            auto &buffers = *static_cast<MeshBuffers *>(ptr);
            for (unsigned i = 0; i < model->numsurfs; ++i) {
                const auto &surface = model->surfs[i];
                if (surface.vb0 && buffers.vertices.insert(surface.vb0).second) surface.vb0->AddRef();
                if (surface.indexBuffer && buffers.indices.insert(surface.indexBuffer).second) surface.indexBuffer->AddRef();
            }
        }, &meshBuffers, false);
        auto prepareBuffer = [&](kd::BufferCore &buffer) {
            keepalive();
            if (request->cancel || failed || (buffer.usage & D3DUSAGE_DYNAMIC)) return;
            const auto before = resourceBatch.uploadBytes;
            BufferId(buffer);
            request->uploadedBytes += resourceBatch.uploadBytes - before;
            ++request->buffers;
            SubmitResources(resourceBatch, false);
        };
        for (auto *buffer : vertexBuffers) {
            prepareBuffer(static_cast<kd::KVertexBuffer *>(buffer)->c);
            buffer->Release();
        }
        for (auto *buffer : indexBuffers) {
            prepareBuffer(static_cast<kd::KIndexBuffer *>(buffer)->c);
            buffer->Release();
        }
        SubmitResources(resourceBatch);
        resourceRecording = nullptr;
    } else request->uploadFence->store(true);
    std::set<std::pair<wgpu::TextureFormat, wgpu::TextureFormat>> targets;
    for (const auto &target : gfxRenderTargets) if (target.surface.color) {
        auto *color = static_cast<kd::KSurface *>(target.surface.color);
        auto *depth = static_cast<kd::KSurface *>(target.surface.depthStencil);
        targets.emplace(Format(color->fmt), depth ? Format(depth->fmt) : wgpu::TextureFormat::Undefined);
    }
    if (preparationMode == 2) {
        Batch utilities;
        std::vector<std::pair<wgpu::TextureFormat,wgpu::TextureFormat>> formats(targets.begin(), targets.end());
        utilities.completion = request->uploadFence;
        utilities.commands.push_back({[formats = std::move(formats), request](Executor &e) { e.WarmUtilities(formats, request); }, {}, 0});
        // The final resource fence also follows all utility descriptor requests.
        SubmitResources(utilities);
    }
    auto device = std::make_unique<kd::Device>();
    // The real renderer enables separate alpha blending and two-sided stencil
    // before initializing command buffers. Device defaults are different: using
    // them here prepared different keys for blended/stencil passes and left
    // first-use decals/effects compiling despite a "complete" gate.
    R_SetInitialContextState(device.get());
    auto state = std::make_unique<GfxCmdBufState>(); state->prim.device = device.get(); R_InitCmdBufState(state.get());
    const bool collectLoaded = !request->catalog && preparationMode != 0;
    const bool collectCapture = request->capture && enumerateRecipes;
    unsigned hiddenDraws = 0;
    if (collectLoaded) {
        R_SyncRenderThread();
        auto &scene = gfxRenderTargets[R_RENDERTARGET_SCENE];
        if (scene.surface.color) device->SetRenderTarget(0, scene.surface.color);
        if (scene.surface.depthStencil) device->SetDepthStencilSurface(scene.surface.depthStencil);
    }
    for (auto *material : materials) {
        // Frontend stays catalog-only. Map loads compile loaded world/model
        // recipes here without persisting them; SaveRecipes still requires
        // a later required gameplay draw before a recipe is learned.
        if (!collectLoaded && !collectCapture) break;
        if (request->cancel || failed) break;
        if (recipeBudget >= (preparationMode == 2 ? (256u << 20) : (32u << 20)) || (preparationMode == 1 && request->count >= 4096)) { ++request->rejected; break; }
        if (!visited.insert(material).second) continue;
        auto *set = material->techniqueSet;
        if (!set || !material->stateBitsTable) continue;
        const unsigned flags = usage[material];
        std::set<unsigned> techniques{0,1,4,gfxDrawMethod.baseTechType,gfxDrawMethod.emissiveTechType,7};
        if (flags & 1) for (unsigned t = 10; t <= 37; ++t) techniques.insert(t);
        else if (flags & 2) for (unsigned t = 10; t <= 37; ++t) techniques.insert(t);
        // Use the same depth-sampling technique remap as actual material draws.
        const auto originalTechniques = techniques;
        for (auto t : originalTechniques) techniques.insert(R_Get_NvFloatZLitTech(material, t));
        for (auto type : techniques) {
            const auto *tech = set->techniques[type];
            if (!tech || material->stateBitsEntry[type] >= material->stateBitsCount) continue;
            for (unsigned passIndex = 0; passIndex < tech->passCount; ++passIndex) {
                if (passIndex + material->stateBitsEntry[type] >= material->stateBitsCount) continue;
                const auto &pass = tech->passArray[passIndex];
                if (!pass.vertexDecl || !pass.vertexShader || !pass.pixelShader || !pass.vertexShader->prog.vs || !pass.pixelShader->prog.ps) continue;
                auto &vs = *static_cast<kd::KVShader *>(pass.vertexShader->prog.vs);
                auto &ps = *static_cast<kd::KPShader *>(pass.pixelShader->prog.ps);
                const auto &bits = material->stateBitsTable[passIndex + material->stateBitsEntry[type]];
                R_SetState(state.get(), const_cast<unsigned *>(bits.loadBits));
                uint32_t shadowMask = 0;
                for (auto &s : ps.tr.samplers) if (s.type == 2 && s.proj && s.reg >= 0 && s.reg < 16) shadowMask |= 1u << s.reg;
                std::set<unsigned> vertexTypes;
                if (flags & 1) vertexTypes.insert((tech->flags & 8) ? set->worldVertFormat + 2 : VERTDECL_WORLD);
                if (flags & 2) { vertexTypes.insert(VERTDECL_PACKED); vertexTypes.insert(VERTDECL_STATICMODELCACHE); }
                if (!flags) vertexTypes.insert(VERTDECL_GENERIC);
                if (pass.vertexDecl->routing.decl[VERTDECL_PARTICLECLOUD]) vertexTypes.insert(VERTDECL_PARTICLECLOUD);
                if (pass.vertexDecl->routing.decl[VERTDECL_WATER]) vertexTypes.insert(VERTDECL_WATER);
                for (unsigned vertexType : vertexTypes) {
                    if (vertexType >= 18) continue;
                    auto *decl = static_cast<kd::KDecl *>(pass.vertexDecl->routing.decl[vertexType]);
                    if (!decl || decl->el.empty()) continue;
                    std::array<unsigned, 4> strides{};
                    if (vertexType == VERTDECL_PARTICLECLOUD) strides[0] = 8; // RB_TessParticleCloud
                    else if (vertexType == VERTDECL_WATER) strides = {16,16,0,0}; // RB_TessWater's two streams
                    else if (vertexType >= VERTDECL_WORLD && vertexType <= VERTDECL_WORLD_T4N2) {
                        strides = {44, unsigned(g_layerDataStride[vertexType]), unsigned(g_stream2Stride[vertexType]), 0};
                    } else if (vertexType == VERTDECL_GENERIC || vertexType == VERTDECL_PACKED || vertexType == VERTDECL_STATICMODELCACHE) strides[0] = 32;
                    else {
                        for (auto &v : decl->el) if (v.Stream < 4) {
                            static const unsigned sizes[] = {4,8,12,16,4,4,4,8,4,4,8,4,8,4,4,4,8};
                            if (v.Type < std::size(sizes)) strides[v.Stream] = std::max(strides[v.Stream], unsigned(v.Offset) + sizes[v.Type]);
                        }
                    }
                    for (auto [color, depth] : targets) for (auto mask : {0u, shadowMask}) {
                        if (request->cancel) break;
                        const auto actualDepth = !device->rs[D3DRS_ZENABLE] && !device->rs[D3DRS_STENCILENABLE] && !ps.tr.writesDepth ? wgpu::TextureFormat::Undefined : depth;
                        const std::pair<D3DPRIMITIVETYPE, unsigned> topologies[] = {
                            {D3DPT_TRIANGLELIST, 0}, {D3DPT_TRIANGLESTRIP, 1}, {D3DPT_TRIANGLESTRIP, 2},
                        };
                        for (auto [topology, strip] : topologies) {
                            auto recipe = MakeRecipe(vs, ps, decl->el, strides, device->rs, color, actualDepth, mask, topology, strip);
                            if (recipe) {
                                {
                                    std::lock_guard lock(warmMu);
                                    if (!request->seen.count(recipe->key)) {
                                        recipeBudget += sizeof(PrepareData) + recipe->key.size() + recipe->decl.size() * sizeof(D3DVERTEXELEMENT9);
                                        if (budgetedSources.insert(recipe->sources->vertex.get()).second) recipeBudget += recipe->vs.size();
                                        if (budgetedSources.insert(recipe->sources->pixel.get()).second) recipeBudget += recipe->ps.size();
                                    }
                                    AddWarmRecipe(*request, std::move(recipe));
                                }
                                if (collectLoaded && hiddenDraws < 512 && device->rt[0] && topology == D3DPT_TRIANGLELIST && strip == 0) {
                                    device->SetVertexShader(&vs);
                                    device->SetPixelShader(&ps);
                                    device->SetVertexDeclaration(decl);
                                    harvestWarm = request.get();
                                    const unsigned stride = strides[0] ? strides[0] : 32;
                                    alignas(16) unsigned char dummy[512]{};
                                    Draw(*device, D3DPT_TRIANGLELIST, 1, 0, 0, dummy, std::min<unsigned>(3 * stride, sizeof dummy), stride);
                                    harvestWarm = nullptr;
                                    ++hiddenDraws;
                                }
                            }
                        }
                    }
                }
            }
        }
    }
    recording = {}; ResetConstantReuse();
    request->collecting = false;

    fprintf(stderr, "WEB_GPU_WARMUP_COLLECT map=%s materials=%zu hiddenDraws=%u ms=%.2f\n", map, materials.size(), hiddenDraws, emscripten_get_now() - begin);
    // Hidden actual-draw pass: present a few loading frames so exact spawn-view
    // recipes compile before the overlay is released.
    if (localClientNum >= 0 && !request->cancel && !failed && preparationMode != 0) {
        R_BeginRemoteScreenUpdate();
        const bool remote = R_IsInRemoteScreenUpdate();
        const double hiddenBegin = emscripten_get_now();
        for (int frame = 0; frame < 8 && !request->cancel && !failed && emscripten_get_now() - hiddenBegin < 2000; ++frame) {
            keepalive();
            if (!remote) SCR_UpdateScreen();
            emscripten_thread_sleep(2);
        }
        R_EndRemoteScreenUpdate(nullptr);
    }
    request->hiddenDrawsDone = true;
    while (!request->done && !request->cancel && !failed) {
        keepalive();
        emscripten_thread_sleep(2);
    }
}
void WarmFrontend() {
    if (preparationMode == 2 && Requested() && started == 1) {
        WarmMap("@frontend", -1);
    }
}
void BeginMapPreparation() {
    ++mapGeneration;
    std::lock_guard lock(dependencyMu); pendingDependencies.clear();
    mapReadyForSettle.store(false);
    settleStarted.store(false);
    settling.store(false);
    settleCancel.store(false);
    settleReportedDone.store(false);
    std::lock_guard late(lateRecipeMu); lateRecipes.clear();
}
void NotifyPreparationAsset(int type, const char *name) {
    if (!name || !Requested() || preparationMode == 0) return;
    // Store names, never DB pointers: assets can be replaced/unloaded before
    // the game thread drains this queue. DBHASH is held by the caller.
    std::lock_guard lock(dependencyMu); pendingDependencies.emplace(type, name);
}
void CheckPreparation() {
    if (preparationMode == 0 || !Requested() || started != 1 || loadingWarm || preparedProfile.empty()) return;
    if (PreparationProfile() != preparedProfile) {
        if (rgp.world && !preparedMap.empty() && preparedMap != "@frontend")
            WarmMap(preparedMap.c_str(), 0);
        else WarmFrontend();
        return;
    }
    if (preparationMode != 2 || !DB_FinishedLoadingAssets()) return;
    std::vector<std::pair<int,std::string>> dependencies;
    { std::lock_guard lock(dependencyMu);
      while (!pendingDependencies.empty() && dependencies.size() < 16) {
          auto it = pendingDependencies.begin(); dependencies.push_back(*it); pendingDependencies.erase(it);
      }
    }
    if (dependencies.empty()) return;
    Batch resources; resourceRecording = &resources;
    std::vector<Material *> lateMaterials;
    auto image = [](GfxImage *img) {
        if (img && !img->delayLoadPixels && img->texture.basemap) {
            auto *texture = kd::TexCoreOf(img->texture.basemap);
            if (texture && !texture->isRT() && !texture->isDS()) TextureId(texture);
        }
    };
    auto material = [&](Material *mat) {
        if (!mat) return;
        lateMaterials.push_back(mat);
        if (mat->textureTable) for (unsigned i=0; i<mat->textureCount; ++i)
            if (mat->textureTable[i].semantic != 11) image(mat->textureTable[i].u.image);
    };
    for (auto &[type,name] : dependencies) {
        auto header = DB_FindXAssetHeader(static_cast<XAssetType>(type), name.data(), false, 0);
        if (type == ASSET_TYPE_IMAGE) image(header.image);
        else if (type == ASSET_TYPE_MATERIAL) material(header.material);
        else if (type == ASSET_TYPE_XMODEL && header.model) {
            auto *model = header.model;
            for (unsigned i=0; i<model->numsurfs; ++i) {
                if (model->materialHandles) material(model->materialHandles[i]);
                if (!model->surfs) continue;
                const auto &surface = model->surfs[i];
                if (surface.vb0) BufferId(static_cast<kd::KVertexBuffer *>(surface.vb0)->c);
                if (surface.indexBuffer) BufferId(static_cast<kd::KIndexBuffer *>(surface.indexBuffer)->c);
            }
        }
        SubmitResources(resources);
    }
    resourceRecording = nullptr;
    if (lateMaterials.empty()) return;
    std::set<std::pair<wgpu::TextureFormat, wgpu::TextureFormat>> targets;
    for (const auto &target : gfxRenderTargets) if (target.surface.color) {
        auto *color = static_cast<kd::KSurface *>(target.surface.color);
        auto *depth = static_cast<kd::KSurface *>(target.surface.depthStencil);
        targets.emplace(Format(color->fmt), depth ? Format(depth->fmt) : wgpu::TextureFormat::Undefined);
    }
    auto device = std::make_unique<kd::Device>();
    R_SetInitialContextState(device.get());
    auto state = std::make_unique<GfxCmdBufState>(); state->prim.device = device.get(); R_InitCmdBufState(state.get());
    std::set<Material *> visited;
    for (auto *mat : lateMaterials) {
        if (!mat || !visited.insert(mat).second || !mat->techniqueSet || !mat->stateBitsTable) continue;
        auto *set = mat->techniqueSet;
        std::set<unsigned> techniques{0,1,4,gfxDrawMethod.baseTechType,gfxDrawMethod.emissiveTechType,7};
        for (unsigned t = 10; t <= 37; ++t) techniques.insert(t);
        const auto originalTechniques = techniques;
        for (auto t : originalTechniques) techniques.insert(R_Get_NvFloatZLitTech(mat, t));
        for (auto type : techniques) {
            const auto *tech = set->techniques[type];
            if (!tech || mat->stateBitsEntry[type] >= mat->stateBitsCount) continue;
            for (unsigned passIndex = 0; passIndex < tech->passCount; ++passIndex) {
                if (passIndex + mat->stateBitsEntry[type] >= mat->stateBitsCount) continue;
                const auto &pass = tech->passArray[passIndex];
                if (!pass.vertexDecl || !pass.vertexShader || !pass.pixelShader || !pass.vertexShader->prog.vs || !pass.pixelShader->prog.ps) continue;
                auto &vs = *static_cast<kd::KVShader *>(pass.vertexShader->prog.vs);
                auto &ps = *static_cast<kd::KPShader *>(pass.pixelShader->prog.ps);
                const auto &bits = mat->stateBitsTable[passIndex + mat->stateBitsEntry[type]];
                R_SetState(state.get(), const_cast<unsigned *>(bits.loadBits));
                uint32_t shadowMask = 0;
                for (auto &sampler : ps.tr.samplers) if (sampler.type == 2 && sampler.proj && sampler.reg >= 0 && sampler.reg < 16) shadowMask |= 1u << sampler.reg;
                for (unsigned vertexType : {unsigned(VERTDECL_PACKED), unsigned(VERTDECL_STATICMODELCACHE), unsigned(VERTDECL_GENERIC)}) {
                    auto *decl = static_cast<kd::KDecl *>(pass.vertexDecl->routing.decl[vertexType]);
                    if (!decl || decl->el.empty()) continue;
                    std::array<unsigned, 4> strides{}; strides[0] = 32;
                    for (auto [color, depth] : targets) for (auto mask : {0u, shadowMask}) {
                        const auto actualDepth = !device->rs[D3DRS_ZENABLE] && !device->rs[D3DRS_STENCILENABLE] && !ps.tr.writesDepth ? wgpu::TextureFormat::Undefined : depth;
                        if (auto recipe = MakeRecipe(vs, ps, decl->el, strides, device->rs, color, actualDepth, mask, D3DPT_TRIANGLELIST, 0)) {
                            std::lock_guard lock(lateRecipeMu);
                            if (lateRecipes.size() < 4096) lateRecipes.push_back(std::move(recipe));
                        }
                    }
                }
            }
        }
    }
}
bool Start(unsigned w, unsigned h) {
    if (started == 1) return true;
    width = w; height = h; session = Web_SessionId();
    if (!cacheWriterStarted) {
        try { std::thread(CacheWriterLoop).detach(); cacheWriterStarted = true; }
        catch (...) { fprintf(stderr, "WEB_GPU_RECIPE_CACHE writer unavailable; rendering continues\n"); }
    }
    pthread_attr_t attr; pthread_attr_init(&attr);
    // Emscripten transfers this canvas from the page to this owner only. Proxy
    // main and the blocking game/render threads never become canvas owners.
    emscripten_pthread_attr_settransferredcanvases(&attr, "game");
    pthread_t thread; const int result = pthread_create(&thread, &attr, OwnerMain, nullptr); pthread_attr_destroy(&attr);
    if (result) { Fail("could not launch Dawn owner thread"); return false; }
    pthread_detach(thread);
    const double until = emscripten_get_now() + 30000;
    while (!started && emscripten_get_now() < until) emscripten_thread_sleep(1);
    if (!started) Fail("Dawn initialization timed out");
    return started == 1;
}
void Resize(unsigned w, unsigned h) { Enqueue([=](Executor &e) { width = w; height = h; e.Configure(); }); }
// KISAK_GPU_AUXILIARY_BEGIN
// No pipeline descriptors or shader sources may be constructed in this block.
void WarmBuiltinResources() {
    if (!Requested() || started != 1 || preparationMode != 2 || failed) return;
    R_SyncRenderThread();
    Batch resources; resourceRecording = &resources;
    VisitBuiltinResources([](GfxImage *image) {
        if (!image || image->delayLoadPixels || !image->texture.basemap) return;
        auto *texture = kd::TexCoreOf(image->texture.basemap);
        if (!texture || texture->isRT() || texture->isDS()) return;
        const bool fresh = !texture->gpuId;
        const auto id = TextureId(texture);
        if (fresh) fprintf(stderr, "WEB_GPU_BUILTIN image=%s id=%llu\n", image->name, (unsigned long long)id);
    }, [](IDirect3DVertexBuffer9 *buffer, const char *name) {
        if (!buffer) return;
        auto &core = static_cast<kd::KVertexBuffer *>(buffer)->c;
        if (core.usage & D3DUSAGE_DYNAMIC) return;
        const bool fresh = !core.gpuId;
        const auto id = BufferId(core);
        if (fresh) fprintf(stderr, "WEB_GPU_BUILTIN buffer=%s id=%llu bytes=%u\n", name, (unsigned long long)id, core.size);
    }, [](IDirect3DIndexBuffer9 *buffer, const char *name) {
        if (!buffer) return;
        auto &core = static_cast<kd::KIndexBuffer *>(buffer)->c;
        if (core.usage & D3DUSAGE_DYNAMIC) return;
        const bool fresh = !core.gpuId;
        const auto id = BufferId(core);
        if (fresh) fprintf(stderr, "WEB_GPU_BUILTIN buffer=%s id=%llu bytes=%u\n", name, (unsigned long long)id, core.size);
    });
    resourceRecording = nullptr;
    SubmitResources(resources);
}
int TestDeviceLoss() {
    if (started != 1 || failed || !ownerQueue) return 0;
    return emscripten_proxy_async(ownerQueue, ownerThread, [](void *) { executor.device.Destroy(); }, nullptr);
}
// KISAK_GPU_AUXILIARY_END
void Draw(kd::Device &d, D3DPRIMITIVETYPE topology, unsigned count, int base, unsigned first,
          const void *vertices, unsigned vertexBytes, unsigned stride, const void *indices, D3DFORMAT indexFormat,
          unsigned minVertex, unsigned vertexCount) {
    if (failed || !d.vs || !d.ps || !d.decl || !d.rt[0]) return;
    auto s = std::make_shared<DrawState>(); s->topology = topology; s->count = count; s->base = base; s->first = first;
    Uniforms constants[2];
    for (auto &u : constants) memset(u.at, 0, 32);
    memcpy(s->ss, d.ss, sizeof(s->ss)); s->viewport = d.vp; s->scissor = d.scissor;
    s->scissorEnable = d.rs[D3DRS_SCISSORTESTENABLE] != 0; s->zEnable = d.rs[D3DRS_ZENABLE] != 0; s->zWrite = d.rs[D3DRS_ZWRITEENABLE] != 0;
    s->minVertex = minVertex; s->vertexCount = vertexCount;
    s->blendFactor = d.rs[D3DRS_BLENDFACTOR]; s->stencilRef = d.rs[D3DRS_STENCILREF];
    const auto &decl = d.decl->el;
    std::array<unsigned, 4> strides{};
    s->vertexInfo = ShaderInfo(*d.vs); s->pixelInfo = ShaderInfo(*d.ps);
    s->color = SurfaceId(d.rt[0]); s->depth = SurfaceId(d.ds);
    if (pendingDepthColor && (pendingDepthColor != s->color.id || pendingDepthSource.id != s->depth.id)) FlushDepthWriteback();
    if (!d.rs[D3DRS_ZENABLE] && !d.rs[D3DRS_STENCILENABLE] && !s->pixelInfo->writesDepth) s->depth = {};
    if (s->depth.id && (s->depth.w != s->color.w || s->depth.h != s->color.h)) {
        if (!d.rs[D3DRS_ZENABLE] && !d.rs[D3DRS_STENCILENABLE] && !s->pixelInfo->writesDepth) {
            // Postprocessing often leaves the full-size scene depth bound while
            // drawing to a smaller color target. It neither tests nor writes it.
            s->depth = {};
        } else {
            if (d.rs[D3DRS_STENCILENABLE]) { Fail("stencil-tested mismatched depth targets are not supported"); return; }
            static std::map<std::pair<uint64_t, uint64_t>, uint64_t> temporaryDepth;
            const auto key = std::make_pair(s->color.id, s->depth.id);
            auto [it, fresh] = temporaryDepth.try_emplace(key, 0);
            if (fresh) it->second = nextId.fetch_add(1);
            const Surface real = s->depth;
            s->depth = {it->second, 0, 0, s->color.w, s->color.h};
            if (fresh) MakeTexture(s->depth.id, s->depth.w, s->depth.h, 1, 1, 0, d.ds->fmt);
            if (pendingDepthColor != s->color.id || pendingDepthSource.id != real.id) {
                const auto temporary = s->depth;
                const RECT overlap{0, 0, LONG(std::min(real.w, temporary.w)), LONG(std::min(real.h, temporary.h))};
                Enqueue([=](Executor &e) { e.Blit(real, overlap, temporary, overlap, false); });
                pendingDepthColor = s->color.id; pendingDepthSource = real; pendingDepthTarget = temporary;
            }
            pendingDepthWritten |= d.rs[D3DRS_ZENABLE] && d.rs[D3DRS_ZWRITEENABLE];
        }
    }
    for (unsigned i = 0; i < 4; ++i) if (d.streams[i].vb) { s->buffers[i] = BufferId(d.streams[i].vb->c); s->offsets[i] = d.streams[i].off; strides[i] = d.streams[i].stride; s->bufferSizes[i] = d.streams[i].vb->c.size; }
    if (vertices) { s->buffers[0] = TemporaryBuffer(vertices, vertexBytes); strides[0] = stride; s->offsets[0] = 0; }
    if (d.ib && !vertices) { s->index = BufferId(d.ib->c); s->wide = d.ib->c.size && d.ib->fmt == D3DFMT_INDEX32; }
    if (indices) { s->wide = indexFormat == D3DFMT_INDEX32; s->index = TemporaryBuffer(indices, count * (s->wide ? 4 : 2)); }
    s->strides = strides;
    uint32_t depthMask = 0;
    for (auto *info : {s->vertexInfo, s->pixelInfo}) for (auto &sampler : info->samplers) {
        const unsigned r = sampler.reg; if (r >= 16) { Fail("vertex texture register unsupported"); return; }
        if (d.tex[r]) { s->textures[r] = TextureId(d.tex[r]); if (d.tex[r]->fi && d.tex[r]->fi->depth) depthMask |= 1u << r;
            if (d.tex[r]->fi) { const auto swz = d.tex[r]->fi->swz; const unsigned code = swz == kd::SWZ_L8 ? 1 : swz == kd::SWZ_A8 ? 2 : swz == kd::SWZ_A8L8 ? 3 : 0; constants[1].swzTex |= code << (r * 2); }
            if (d.ss[r][D3DSAMP_SRGBTEXTURE]) constants[1].srgbTex |= 1u << r;
        } else { // White is the engine's multiplicative identity for absent detail textures.
            const uint64_t dummy = sampler.type == 3 ? 2 : sampler.type == 4 ? 3 : 1;
            const uint64_t id = UINT64_MAX - dummy; s->textures[r] = id;
            MakeTexture(id, 1, 1, sampler.type == 3 ? 6 : 1, 1, sampler.type == 3 ? 1 : sampler.type == 4 ? 2 : 0, D3DFMT_A8B8G8R8);
            Enqueue([=](Executor &e) { auto &t = e.textures.at(id); if (t.generation != 1) return; const uint32_t white[6] = {~0u, ~0u, ~0u, ~0u, ~0u, ~0u}; wgpu::TexelCopyTextureInfo dst{}; dst.texture = t.handle; wgpu::TexelCopyBufferLayout layout{}; layout.bytesPerRow = 4; layout.rowsPerImage = 1; wgpu::Extent3D extent{1, 1, t.layers}; e.device.GetQueue().WriteTexture(&dst, white, t.layers * 4, &layout, &extent); ++t.generation; });
        }
    }
    memcpy(constants[0].fc, d.vsF, FloatBytes(*s->vertexInfo)); memcpy(constants[1].fc, d.psF, FloatBytes(*s->pixelInfo));
    for (unsigned stage = 0; stage < 2; ++stage) { auto &u = constants[stage]; memcpy(u.ic, stage ? d.psI : d.vsI, sizeof(u.ic)); memset(u.bc, 0, sizeof(u.bc)); const auto *bc = stage ? d.psB : d.vsB; for (unsigned i = 0; i < 16; ++i) u.bc[i * 4] = bc[i]; }
    constants[1].at[0] = d.rs[D3DRS_ALPHATESTENABLE] ? float(d.rs[D3DRS_ALPHAFUNC]) : 0; constants[1].at[1] = float(d.rs[D3DRS_ALPHAREF] & 255) / 255;
    constants[1].srgbW = d.rs[D3DRS_SRGBWRITEENABLE]; constants[0].bgra = d.decl->bgraMask;
    s->constants[0] = SnapshotConstants(constants[0], *s->vertexInfo, 0);
    s->constants[1] = SnapshotConstants(constants[1], *s->pixelInfo, 1);
    const unsigned strip = (topology == D3DPT_TRIANGLESTRIP || topology == D3DPT_LINESTRIP) && s->index ? (s->wide ? 2 : 1) : 0;
    s->prepare = MakeRecipe(*d.vs, *d.ps, decl, strides, d.rs, Format(d.rt[0]->fmt),
        s->depth.id ? Format(d.ds->fmt) : wgpu::TextureFormat::Undefined, depthMask, topology, strip, &recording.translationMs);
    if (!s->prepare) { Fail("SM3 recipe translation failed"); return; }
    s->pipelineHash = s->prepare->hash; s->pipelineId = s->prepare->id;
    if (harvestWarm) {
        std::lock_guard lock(warmMu);
        AddWarmRecipe(*harvestWarm, s->prepare);
        return;
    }
    recording.commands.push_back({{}, s, sizeof(DrawState)}); recording.bytes += sizeof(DrawState);
}
void Executor::WarmUtilities(const std::vector<std::pair<wgpu::TextureFormat,wgpu::TextureFormat>> &formats, std::shared_ptr<WarmRequest> request) {
    preparingUtilities = true; utilityRequest = request;
    for (auto [color, depth] : formats) {
        auto create = [&](wgpu::TextureFormat format, bool isDepth) -> Surface {
            if (format == wgpu::TextureFormat::Undefined) return {};
            const uint64_t id = nextId.fetch_add(1);
            wgpu::TextureDescriptor d{}; d.size = {1,1,1}; d.format = format;
            d.usage = wgpu::TextureUsage::TextureBinding | wgpu::TextureUsage::RenderAttachment;
            textures.emplace(id, Texture{device.CreateTexture(&d),format,1,1,1,1,0,isDepth,format == wgpu::TextureFormat::Depth24PlusStencil8});
            return {id,0,0,1,1};
        };
        auto c = create(color, false), z = create(depth, true);
        if (c.id && (!Float32Texture(color) || float32Filterable)) {
            Blit(c, {0,0,1,1}, c, {0,0,1,1}, false, false);
            Blit(c, {0,0,1,1}, {}, {0,0,1,1}, false, true);
        }
        if (z.id) Blit(z, {0,0,1,1}, z, {0,0,1,1}, false, false);
        for (DWORD flags = 1; flags <= 7; ++flags) {
            if ((flags & D3DCLEAR_STENCIL) && depth != wgpu::TextureFormat::Depth24PlusStencil8) continue;
            if (c.id && z.id) ClearRects(c, z, {{0,0,1,1}}, flags, 0, 1, 0);
            if (c.id && flags == D3DCLEAR_TARGET) ClearRects(c, {}, {{0,0,1,1}}, flags, 0, 1, 0);
            if (z.id && !(flags & D3DCLEAR_TARGET)) ClearRects({}, z, {{0,0,1,1}}, flags, 0, 1, 0);
        }
        if (c.id) textures.erase(c.id);
        if (z.id) textures.erase(z.id);
    }
    preparingUtilities = false; utilityRequest.reset();
}
void Executor::ClearRects(Surface c, Surface dep, const std::vector<D3DRECT> &rects, DWORD flags, D3DCOLOR color, float z, DWORD stencil) {
    EndPass(ClearReason);
    if (!c.id && !dep.id) return;
    if (c.id && dep.id && (c.w != dep.w || c.h != dep.h)) {
        if (flags & D3DCLEAR_TARGET) ClearRects(c, {}, rects, D3DCLEAR_TARGET, color, z, stencil);
        if (flags & (D3DCLEAR_ZBUFFER | D3DCLEAR_STENCIL)) ClearRects({}, dep, rects, flags & (D3DCLEAR_ZBUFFER | D3DCLEAR_STENCIL), color, z, stencil);
        return;
    }
    const auto colorFormat = c.id ? textures.at(c.id).format : wgpu::TextureFormat::Undefined;
    const auto depthFormat = dep.id ? textures.at(dep.id).format : wgpu::TextureFormat::Undefined;
    const std::string key = "clear/" + std::to_string(unsigned(colorFormat)) + "/" + std::to_string(unsigned(depthFormat)) + "/" + std::to_string(flags);
    auto &p = utility[key];
    if (!p.handle) {
        std::string code = R"WGSL(
struct Value { color: vec4f, depth: f32 }
@group(0) @binding(0) var<uniform> value: Value;
@vertex fn vs(@builtin(vertex_index) i: u32) -> @builtin(position) vec4f {
    let pos = array(vec2f(-1.0,-1.0),vec2f(3.0,-1.0),vec2f(-1.0,3.0))[i];
    return vec4f(pos,0.0,1.0);
}
)WGSL";
        if (c.id && dep.id) code += "struct Out { @location(0) color: vec4f, @builtin(frag_depth) depth: f32 }\n@fragment fn fs() -> Out { return Out(value.color,value.depth); }";
        else if (c.id) code += "@fragment fn fs() -> @location(0) vec4f { return value.color; }";
        else code += "@fragment fn fs() -> @builtin(frag_depth) f32 { return value.depth; }";
        auto shader = Shader(code); wgpu::ColorTargetState target{}; target.format = colorFormat;
        target.writeMask = flags & D3DCLEAR_TARGET ? wgpu::ColorWriteMask::All : wgpu::ColorWriteMask::None;
        wgpu::FragmentState f{}; f.module = shader; f.entryPoint = "fs"; f.targetCount = c.id ? 1 : 0; f.targets = c.id ? &target : nullptr;
        wgpu::DepthStencilState ds{}; ds.format = depthFormat;
        ds.depthWriteEnabled = flags & D3DCLEAR_ZBUFFER ? wgpu::OptionalBool::True : wgpu::OptionalBool::False;
        ds.depthCompare = wgpu::CompareFunction::Always;
        ds.stencilFront.compare = ds.stencilBack.compare = wgpu::CompareFunction::Always;
        ds.stencilFront.passOp = ds.stencilBack.passOp = wgpu::StencilOperation::Replace;
        ds.stencilWriteMask = flags & D3DCLEAR_STENCIL ? 255 : 0;
        wgpu::RenderPipelineDescriptor desc{}; desc.vertex.module = shader; desc.vertex.entryPoint = "vs"; desc.fragment = &f; desc.depthStencil = dep.id ? &ds : nullptr;
        if (preparingUtilities) { CompileUtility(p, desc); return; }
        p.handle = device.CreateRenderPipeline(&desc);
    }
    if (preparingUtilities) return;
    const float value[8] = {float((color >> 16) & 255) / 255, float((color >> 8) & 255) / 255, float(color & 255) / 255, float(color >> 24) / 255, z, 0, 0, 0};
    wgpu::BufferDescriptor b{}; b.size = sizeof(value); b.usage = wgpu::BufferUsage::Uniform | wgpu::BufferUsage::CopyDst; auto buffer = device.CreateBuffer(&b); device.GetQueue().WriteBuffer(buffer, 0, value, sizeof(value));
    wgpu::BindGroupEntry entry{}; entry.binding = 0; entry.buffer = buffer; entry.size = sizeof(value); wgpu::BindGroupDescriptor group{}; group.layout = p.handle.GetBindGroupLayout(0); group.entryCount = 1; group.entries = &entry;
    wgpu::RenderPassDescriptor rp{}; wgpu::RenderPassColorAttachment ca{}; wgpu::RenderPassDepthStencilAttachment da{};
    if (c.id) { ca.view = textures.at(c.id).View(c.level, c.face, true); ca.loadOp = wgpu::LoadOp::Load; ca.storeOp = wgpu::StoreOp::Store; rp.colorAttachmentCount = 1; rp.colorAttachments = &ca; }
    if (dep.id) { da.view = textures.at(dep.id).View(dep.level, dep.face, true); da.depthLoadOp = wgpu::LoadOp::Load; da.depthStoreOp = wgpu::StoreOp::Store; da.depthClearValue = 1;
        if (textures.at(dep.id).stencil) { da.stencilLoadOp = wgpu::LoadOp::Load; da.stencilStoreOp = wgpu::StoreOp::Store; } rp.depthStencilAttachment = &da; }
    const unsigned w = c.id ? c.w : dep.w, h = c.id ? c.h : dep.h;
    if (c.id) encoderTextures.insert(c.id);
    if (dep.id) encoderTextures.insert(dep.id);
    if (!encoder) encoder = device.CreateCommandEncoder();
    auto clearPass = BeginPass(rp); ++passes; clearPass.SetPipeline(p.handle); clearPass.SetBindGroup(0, device.CreateBindGroup(&group)); ++bindGroups; clearPass.SetStencilReference(stencil);
    for (auto &r : rects) { const unsigned x = std::clamp<LONG>(r.x1, 0, w), y = std::clamp<LONG>(r.y1, 0, h);
        const unsigned rw = std::clamp<LONG>(r.x2, x, w) - x, rh = std::clamp<LONG>(r.y2, y, h) - y;
        if (!rw || !rh) continue; clearPass.SetScissorRect(x, y, rw, rh); clearPass.Draw(3); }
    clearPass.End();
}
void Clear(kd::Device &d, DWORD count, const D3DRECT *rectangles, DWORD flags, D3DCOLOR color, float z, DWORD stencil) {
    if (failed) return;
    FlushDepthWriteback();
    const auto c = SurfaceId(d.rt[0]), dep = SurfaceId(d.ds);
    const bool partial = count || d.vp.X || d.vp.Y || (c.id && (d.vp.Width != c.w || d.vp.Height != c.h)) || (dep.id && (d.vp.Width != dep.w || d.vp.Height != dep.h));
    if (partial) {
        if (count && !rectangles) { Fail("null clear rectangles"); return; }
        std::vector<D3DRECT> rects;
        if (count) rects.assign(rectangles, rectangles + count);
        else rects.push_back({LONG(d.vp.X), LONG(d.vp.Y), LONG(d.vp.X + d.vp.Width), LONG(d.vp.Y + d.vp.Height)});
        Enqueue([=, rects = std::move(rects)](Executor &e) { e.ClearRects(c, dep, rects, flags, color, z, stencil); }); return;
    }
    Enqueue([=](Executor &e) {
        e.EndPass(Executor::ClearReason);
        wgpu::RenderPassColorAttachment ca{}; wgpu::RenderPassDepthStencilAttachment da{}; wgpu::RenderPassDescriptor rp{};
        if (c.id) { ca.view = e.textures.at(c.id).View(c.level, c.face, true); ca.loadOp = flags & D3DCLEAR_TARGET ? wgpu::LoadOp::Clear : wgpu::LoadOp::Load; ca.storeOp = wgpu::StoreOp::Store;
            ca.clearValue = {double((color >> 16) & 255) / 255, double((color >> 8) & 255) / 255, double(color & 255) / 255, double(color >> 24) / 255}; rp.colorAttachmentCount = 1; rp.colorAttachments = &ca; e.encoderTextures.insert(c.id); }
        if (dep.id) { auto &t = e.textures.at(dep.id); da.view = t.View(dep.level, dep.face, true); da.depthLoadOp = flags & D3DCLEAR_ZBUFFER ? wgpu::LoadOp::Clear : wgpu::LoadOp::Load; da.depthStoreOp = wgpu::StoreOp::Store; da.depthClearValue = z;
            if (t.stencil) { da.stencilLoadOp = flags & D3DCLEAR_STENCIL ? wgpu::LoadOp::Clear : wgpu::LoadOp::Load; da.stencilStoreOp = wgpu::StoreOp::Store; da.stencilClearValue = stencil; } rp.depthStencilAttachment = &da; e.encoderTextures.insert(dep.id); }
        if (!c.id && !dep.id) return;
        if (!e.encoder) e.encoder = e.device.CreateCommandEncoder();
        if (c.id && dep.id && (c.w != dep.w || c.h != dep.h)) {
            // D3D clears the real depth surface even when it cannot be attached
            // alongside this color target. Keep the two clears independent.
            rp.depthStencilAttachment = nullptr;
            auto colorPass = e.BeginPass(rp); ++e.passes; colorPass.End();
            rp.colorAttachmentCount = 0; rp.colorAttachments = nullptr; rp.depthStencilAttachment = &da;
        }
        auto pass = e.BeginPass(rp); ++e.passes; pass.End();
    });
}
void Fill(kd::KSurface *surface, const RECT *rectangle, D3DCOLOR color) {
    if (!surface || failed) return; FlushDepthWriteback(); const auto s = SurfaceId(surface);
    const RECT r = rectangle ? *rectangle : RECT{0, 0, LONG(s.w), LONG(s.h)};
    Enqueue([=](Executor &e) { e.ClearRects(s, {}, {{r.left, r.top, r.right, r.bottom}}, D3DCLEAR_TARGET, color, 1, 0); });
}
void Executor::Blit(Surface src, RECT sr, Surface dst, RECT dr, bool linear, bool toCanvas) {
    EndPass();
    auto &t = textures.at(src.id);
    encoderTextures.insert(src.id);
    if (dst.id) encoderTextures.insert(dst.id);
    if (t.depth) {
        if (!dst.id || !textures.at(dst.id).depth) { Fail("incompatible depth copy"); return; }
        const auto fmt = textures.at(dst.id).format;
        const std::string key = "depthcopy" + std::to_string(unsigned(fmt)); auto &p = utility[key];
        const char *code = R"WGSL(
@group(0) @binding(0) var source: texture_depth_2d;
@vertex fn vs(@builtin(vertex_index) i: u32) -> @builtin(position) vec4f {
    let pos = array(vec2f(-1.0,-1.0),vec2f(3.0,-1.0),vec2f(-1.0,3.0))[i];
    return vec4f(pos,0.0,1.0);
}
@fragment fn fs(@builtin(position) p: vec4f) -> @builtin(frag_depth) f32 {
    return textureLoad(source,vec2i(p.xy),0);
}
)WGSL";
        if (!p.handle) {
            auto shader = Shader(code); wgpu::FragmentState f{}; f.module = shader; f.entryPoint = "fs";
            wgpu::DepthStencilState ds{}; ds.format = fmt; ds.depthWriteEnabled = wgpu::OptionalBool::True; ds.depthCompare = wgpu::CompareFunction::Always;
            wgpu::RenderPipelineDescriptor desc{}; desc.vertex.module = shader; desc.vertex.entryPoint = "vs"; desc.fragment = &f; desc.depthStencil = &ds;
            if (preparingUtilities) { CompileUtility(p, desc); return; }
        p.handle = device.CreateRenderPipeline(&desc);
        }
        if (preparingUtilities) return;
        wgpu::BindGroupEntry entry{}; entry.binding = 0; entry.textureView = t.View(src.level, src.face);
        wgpu::BindGroupDescriptor group{}; group.layout = p.handle.GetBindGroupLayout(0); group.entryCount = 1; group.entries = &entry;
        auto &dest = textures.at(dst.id); wgpu::RenderPassDepthStencilAttachment a{}; a.view = dest.View(dst.level, dst.face, true); a.depthLoadOp = wgpu::LoadOp::Load; a.depthStoreOp = wgpu::StoreOp::Store; a.depthClearValue = 1;
        if (dest.stencil) { a.stencilLoadOp = wgpu::LoadOp::Load; a.stencilStoreOp = wgpu::StoreOp::Store; }
        wgpu::RenderPassDescriptor rp{}; rp.depthStencilAttachment = &a;
        if (!encoder) encoder = device.CreateCommandEncoder();
        auto copyPass = BeginPass(rp); ++passes;
        copyPass.SetPipeline(p.handle); copyPass.SetBindGroup(0, device.CreateBindGroup(&group)); ++bindGroups; copyPass.SetViewport(dr.left, dr.top, dr.right - dr.left, dr.bottom - dr.top, 0, 1); copyPass.SetScissorRect(dr.left, dr.top, dr.right - dr.left, dr.bottom - dr.top); copyPass.Draw(3); copyPass.End();
        return;
    }
    // A render blit handles scaling, BGRA swapchain formats and D3D row order.
    if (Float32Texture(t.format) && !float32Filterable) {
        Fail("scene requires float32-filterable for render blit"); return;
    }
    const char *shader = R"WGSL(
struct Rect { src: vec4f }
@group(0) @binding(0) var tex: texture_2d<f32>;
@group(0) @binding(1) var samp: sampler;
@group(0) @binding(2) var<uniform> rect: Rect;
struct Out { @builtin(position) pos: vec4f, @location(0) uv: vec2f }
@vertex fn vs(@builtin(vertex_index) i: u32) -> Out {
    let p = array(vec2f(-1.0, -1.0), vec2f(3.0, -1.0), vec2f(-1.0, 3.0))[i];
    return Out(vec4f(p, 0.0, 1.0), rect.src.xy + vec2f((p.x + 1.0) * 0.5, (1.0 - p.y) * 0.5) * rect.src.zw);
}
@fragment fn fs(v: Out) -> @location(0) vec4f { return textureSampleLevel(tex, samp, v.uv, 0.0); }
)WGSL";
    const auto fmt = toCanvas ? canvasFormat : textures.at(dst.id).format;
    const std::string key = "blit" + std::to_string(unsigned(fmt)); auto &p = utility[key];
    if (!p.handle) { auto module = Shader(shader); wgpu::ColorTargetState target{}; target.format = fmt; wgpu::FragmentState f{}; f.module = module; f.entryPoint = "fs"; f.targetCount = 1; f.targets = &target;
        wgpu::RenderPipelineDescriptor d{}; d.vertex.module = module; d.vertex.entryPoint = "vs"; d.fragment = &f; if (preparingUtilities) { CompileUtility(p, d); return; } p.handle = device.CreateRenderPipeline(&d); }
    if (preparingUtilities) return;
    wgpu::BufferDescriptor b{}; b.size = 16; b.usage = wgpu::BufferUsage::Uniform | wgpu::BufferUsage::CopyDst; auto rect = device.CreateBuffer(&b);
    const float uv[] = {float(sr.left) / src.w, float(sr.top) / src.h, float(sr.right - sr.left) / src.w, float(sr.bottom - sr.top) / src.h}; device.GetQueue().WriteBuffer(rect, 0, uv, sizeof(uv));
    DWORD ss[16]{}; ss[D3DSAMP_ADDRESSU] = ss[D3DSAMP_ADDRESSV] = ss[D3DSAMP_ADDRESSW] = D3DTADDRESS_CLAMP; ss[D3DSAMP_MINFILTER] = ss[D3DSAMP_MAGFILTER] = linear ? D3DTEXF_LINEAR : D3DTEXF_POINT;
    wgpu::BindGroupEntry entries[3]{}; entries[0].binding = 0; entries[0].textureView = t.View(src.level, src.face, true); entries[1].binding = 1; entries[1].sampler = Sampler(ss); entries[2].binding = 2; entries[2].buffer = rect; entries[2].size = 16;
    wgpu::BindGroupDescriptor group{}; group.layout = p.handle.GetBindGroupLayout(0); group.entryCount = 3; group.entries = entries;
    wgpu::SurfaceTexture frame{}; wgpu::TextureView target;
    if (toCanvas) { surface.GetCurrentTexture(&frame); if (!frame.texture) return; target = frame.texture.CreateView(); } else target = textures.at(dst.id).View(dst.level, dst.face, true);
    wgpu::RenderPassColorAttachment a{}; a.view = target; a.loadOp = wgpu::LoadOp::Load; a.storeOp = wgpu::StoreOp::Store; wgpu::RenderPassDescriptor rp{}; rp.colorAttachmentCount = 1; rp.colorAttachments = &a;
    if (!encoder) encoder = device.CreateCommandEncoder();
    auto pass = BeginPass(rp, toCanvas); ++passes; pass.SetPipeline(p.handle); pass.SetBindGroup(0, device.CreateBindGroup(&group)); ++bindGroups; pass.SetViewport(dr.left, dr.top, dr.right - dr.left, dr.bottom - dr.top, 0, 1); pass.Draw(3); pass.End();
}
void Copy(kd::KSurface *src, const RECT *sr, kd::KSurface *dst, const RECT *dr, D3DTEXTUREFILTERTYPE filter) {
    if (!src || !dst || failed) return; FlushDepthWriteback(); const auto a = SurfaceId(src), b = SurfaceId(dst);
    const RECT ar = sr ? *sr : RECT{0, 0, LONG(a.w), LONG(a.h)}, br = dr ? *dr : RECT{0, 0, LONG(b.w), LONG(b.h)};
    Enqueue([=](Executor &e) { e.Blit(a, ar, b, br, filter == D3DTEXF_LINEAR); });
}
void Present(kd::Device &d) {
    if (failed) return; FlushDepthWriteback(); const auto src = SurfaceId(d.backBuffer);
    Enqueue([=](Executor &e) {
        e.Blit(src, RECT{0, 0, LONG(src.w), LONG(src.h)}, {}, RECT{0, 0, LONG(width), LONG(height)}, false, true);
    });
    recording.frameId = ++nextFrame;
    const double waitAt = emscripten_get_now();
    std::unique_lock lock(queueMu);
    queueSpace.wait(lock, [] { return failed || (batches.size() < 2 && queuedBytes < QueueLimit); });
    recording.queueWaitMs = emscripten_get_now() - waitAt;
    if (failed) { ResetConstantReuse(); recording = {}; return; }
    if (!released.empty()) {
        auto ids = std::move(released); released.clear();
        recording.commands.push_back({[ids = std::move(ids)](Executor &e) {
            e.Flush();
            for (auto id : ids) {
                e.DropGroupsForTexture(id); e.textures.erase(id);
                auto found = e.buffers.find(id); if (found == e.buffers.end()) continue;
                auto buffer = std::move(found->second); e.buffers.erase(found); const uint64_t size = buffer.GetSize();
                e.device.GetQueue().OnSubmittedWorkDone(wgpu::CallbackMode::AllowSpontaneous,
                    [&e, buffer = std::move(buffer), size](wgpu::QueueWorkDoneStatus, wgpu::StringView) mutable {
                        if (e.persistentPoolBytes + size <= QueueLimit) { e.persistentPool.push_back({std::move(buffer),size}); e.persistentPoolBytes += size; }
                        else buffer.Destroy();
                    });
            }
        }, {}, 0});
    }
    recording.queuedAt = emscripten_get_now();
    queuedBytes += recording.bytes; batches.push_back(std::move(recording)); recording = {}; ResetConstantReuse();
    lock.unlock(); WakeSubmitted();
}
void Release(uint64_t id) {
    // Lifetime notifications can originate on streaming threads. Queue them
    // after current frames, never mutate producer-owned recording from there.
    if (!id || started != 1) return;
    std::lock_guard lock(queueMu); released.push_back(id);
}
#endif
} // namespace webgpu

extern "C" EMSCRIPTEN_KEEPALIVE void Kisak_SetRenderer(int gpu) {
#ifdef KISAK_WEB_WEBGPU
    webgpu::requested.store(gpu != 0);
#else
    if (gpu) fprintf(stderr, "WEB_GPU_FALLBACK build does not include Dawn\n");
#endif
}
extern "C" EMSCRIPTEN_KEEPALIVE void Kisak_SetGpuValidation(int on) {
#ifdef KISAK_WEB_WEBGPU
    webgpu::gpuValidation.store(on != 0);
#else
    (void)on;
#endif
}
extern "C" EMSCRIPTEN_KEEPALIVE void Kisak_SetGpuOptions(unsigned flags) {
#ifdef KISAK_WEB_WEBGPU
    webgpu::profiling.store(flags & 1); webgpu::callbackWake.store(flags & 2);
    webgpu::mergeDepth.store(flags & 4); webgpu::hoistUploads.store(flags & 8);
    webgpu::stableTextureBindings.store(flags & 16);
    webgpu::captureRecipes.store(flags & 32);
    webgpu::bestCasePreparation.store(flags & 128);
    webgpu::enumerateRecipes.store(flags & 256);
    webgpu::submissionWake.store(flags & 64);
    webgpu::pipelineNostall.store(Web_PipelineNostallPolicy((flags & 512) == 0));
#endif
}
extern "C" EMSCRIPTEN_KEEPALIVE int Kisak_GpuCacheRestore(const uint8_t *data, unsigned size, uint32_t token, int final, int ok, const char *source) {
#ifdef KISAK_WEB_WEBGPU
    return webgpu::RestoreRecipes(data, size, token, final != 0, ok != 0, source);
#else
    return 0;
#endif
}

extern "C" EMSCRIPTEN_KEEPALIVE void Kisak_SetPreparation(int mode, unsigned jobs) {
    Web_SetPreparationMode(std::clamp(mode, 0, 2));
#ifdef KISAK_WEB_WEBGPU
    webgpu::preparationMode.store(std::clamp(mode, 0, 2));
    webgpu::preparationJobs.store(Web_RuntimePipelineJobs(jobs));
    webgpu::runtimeJobs.store(Web_RuntimePipelineJobs(jobs));
#endif
}


extern "C" EMSCRIPTEN_KEEPALIVE void Kisak_GpuCacheSaved(uint32_t ticket, int ok) {
#ifdef KISAK_WEB_WEBGPU
    {
        std::lock_guard lock(webgpu::cacheWriterMu);
        webgpu::cacheAckTicket = ticket; webgpu::cacheAckOk = ok != 0;
    }
    webgpu::cacheWriterCv.notify_all();
#endif
}
