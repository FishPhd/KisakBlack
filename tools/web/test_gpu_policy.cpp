#include <web/gpu/web_gpu_cache_codec.h>
#include <web/gpu/web_gpu_sources.h>
#include <web/gpu/web_gpu_shader_interface.h>
#include <web/gpu/web_gpu_settings.h>
#include <web/web_preparation_policy.h>
#include <cassert>
#include <cstdio>

int main(int argc, char **argv) {
    static_assert(!Web_SubmitPreparationResources(0,0,false,true));
    static_assert(!Web_SubmitPreparationResources(1,1024,false,false));
    static_assert(Web_SubmitPreparationResources(512,0,false,false));
    static_assert(Web_SubmitPreparationResources(1,4u<<20,false,false));
    static_assert(Web_SubmitPreparationResources(1,0,false,true));
    static_assert(Web_SubmitPreparationResources(0,0,true,true));
    static_assert(Web_SubmitPreparationResources(0,0,true,false));
    static_assert(Web_ShaderWarmPolicy(2,false));
    static_assert(Web_ShaderWarmPolicy(2,true));
    static_assert(Web_ShaderWarmPolicy(1,true));
    static_assert(!Web_ShaderWarmPolicy(1,false));
    static_assert(!Web_ShaderWarmPolicy(0,true));
    static_assert(!Web_ShaderWarmPolicy(0,false));
    static_assert(Web_ResidentStaticImagePolicy(true,true));
    static_assert(!Web_ResidentStaticImagePolicy(false,true));
    static_assert(!Web_ResidentStaticImagePolicy(true,false));
    static_assert(Web_PipelineNostallPolicy(false));
    static_assert(!Web_PipelineNostallPolicy(true));
    static_assert(Web_RuntimePipelineJobs(0) == 8);
    static_assert(Web_RuntimePipelineJobs(8) == 8);
    static_assert(Web_RuntimePipelineJobs(4) == 4);
    static_assert(Web_RuntimePipelineJobs(16) == 16);
    static_assert(!Web_PersistWarmRecipe(false, false));
    static_assert(Web_PersistWarmRecipe(false, true));
    static_assert(Web_PersistWarmRecipe(true, false));
    static_assert(!Web_SettleFinished(1, 250));
    static_assert(!Web_SettleFinished(0, 0));
    static_assert(Web_SettleFinished(0, 250));
    static_assert(Web_SettleFinished(3, 45000));
    static_assert(Web_SettleFinished(1, 10, true));
    static_assert(Web_SkipMapLoadPreparation(2, false));
    static_assert(!Web_SkipMapLoadPreparation(2, true));
    static_assert(Web_SkipMapLoadPreparation(1, true));
    static_assert(Web_SkipMapLoadPreparation(0, false));
    using namespace webgpu;
    assert(policy::CompactVertexStreams(5) == std::vector<unsigned>({0,2}));
    assert(policy::CompactVertexStreams(8) == std::vector<unsigned>({3}));
    assert(policy::CompactVertexStreams(0).empty());
    dvar_s setting{};
    setting.type = DVAR_TYPE_BOOL;
    setting.current.integer = 0x3f333300;
    assert(PipelineSettingValue(&setting) == 0);
    setting.current.integer = int(0xbf800000u);
    assert(PipelineSettingValue(&setting) == 0);
    setting.current.enabled = true;
    assert(PipelineSettingValue(&setting) == 1);
    setting.type = DVAR_TYPE_INT;
    setting.current.integer = -2;
    assert(PipelineSettingValue(&setting) == -2);
    setting.type = DVAR_TYPE_FLOAT;
    setting.current.value = -0.0f;
    assert(PipelineSettingValue(&setting) == 0);
    setting.current.value = -2.0f;
    const int biasIdentity = PipelineSettingValue(&setting);
    setting.current.value = -1.0f;
    assert(PipelineSettingValue(&setting) != biasIdentity);
    sm3::Result reflected; reflected.glsl.assign(1000000,'s'); reflected.error="unused GLSL diagnostics";
    reflected.floatCount=37; reflected.samplers.push_back({3,2,true,false});
    const auto compact=PipelineInterface(reflected);
    assert(compact.glsl.empty() && compact.error.empty() && compact.floatCount==37);
    assert(compact.samplers.size()==1 && compact.samplers[0].reg==3 && compact.samplers[0].proj);
    SourcePool sources;
    ShaderSources first{sources.Intern("shared vertex"), sources.Intern("pixel one")};
    ShaderSources second{sources.Intern("shared vertex"), sources.Intern("pixel two")};
    assert(first.vertex == second.vertex && first.pixel != second.pixel);
    assert(first.pixel == sources.Intern("pixel one"));
    DWORD a[256]{}, b[256]{};
    a[D3DRS_SRCBLEND] = D3DBLEND_SRCALPHA; b[D3DRS_SRCBLEND] = D3DBLEND_ONE;
    a[D3DRS_STENCILFUNC] = D3DCMP_LESS; b[D3DRS_STENCILFUNC] = D3DCMP_EQUAL;
    a[D3DRS_ZFUNC] = D3DCMP_LESS; b[D3DRS_ZFUNC] = D3DCMP_GREATER;
    assert(policy::CanonicalState(a, true) == policy::CanonicalState(b, true));
    a[D3DRS_ALPHABLENDENABLE] = b[D3DRS_ALPHABLENDENABLE] = 1;
    assert(policy::CanonicalState(a, true) != policy::CanonicalState(b, true));
    a[D3DRS_ALPHABLENDENABLE] = b[D3DRS_ALPHABLENDENABLE] = 0;
    a[D3DRS_DEPTHBIAS] = 0x80000000; assert(policy::CanonicalState(a, true) == policy::CanonicalState(b, true));
    a[D3DRS_ZENABLE] = b[D3DRS_ZENABLE] = 1;
    assert(policy::CanonicalState(a, true) != policy::CanonicalState(b, true));
    assert(policy::CanonicalState(a, false) == policy::CanonicalState(b, false));

    policy::UploadOrder order;
    assert(order.Hoist({1,0,8})); order.Read({1,0,8});
    assert(order.Hoist({1,8,16})); order.Read({1,8,16});
    assert(!order.Hoist({1,4,8}));
    assert(order.Hoist({2,0,8})); // DISCARD generation has a new identity.
    assert(!order.Hoist({1,0,4}));
    policy::UploadOrder oddIndices;
    assert(oddIndices.Hoist({7,0,8})); oddIndices.Read({7,0,6});
    assert(!oddIndices.Hoist({7,4,12})); // Alignment padding must retain ordering.
    policy::UploadOrder provenPadding;
    const uint8_t before[]={1,2,3,4,5,6,0,0}, after[]={5,6,7,8,9,10,11,12};
    assert(provenPadding.Hoist({7,0,8},{7,0,6},false)); provenPadding.Read({7,0,6});
    const auto proof=policy::PaddingUnchanged({7,0,8},before,{7,4,12},after,{7,6,12},provenPadding.ReadSpan(7));
    assert(proof && provenPadding.Hoist({7,4,12},{7,6,12},proof));
    const uint8_t changedPadding[]={9,6,7,8,9,10,11,12};
    assert(!policy::PaddingUnchanged({7,0,8},before,{7,4,12},changedPadding,{7,6,12},{7,0,6}));
    assert(!policy::PaddingUnchanged({},nullptr,{7,4,12},after,{7,6,12},{7,0,6}));
    assert(!provenPadding.Hoist({7,0,8},{7,4,6},true)); // Real overlap stays ordered.
    policy::UploadOrder partial;
    partial.Read({9,16,32});
    assert(partial.Hoist({9,0,8}));
    assert(!partial.Hoist({9,20,24}));
    assert(!partial.Hoist({9,22,28})); // A dependent write stays after the first.
    assert(partial.Hoist({10,20,24})); // Wrapped generation is independent.
    auto merged = policy::MergeRanges({{1,8,16},{1,0,8},{2,0,4}});
    assert(merged.size() == 2 && merged[0].begin == 0 && merged[0].end == 16);

    cache::Recipe r; r.vs = "vertex source"; r.ps = "pixel source";
    r.key = {1,2,3}; r.strides[0] = 16;
    r.decl.push_back({0,0,D3DDECLTYPE_FLOAT4,0,D3DDECLUSAGE_POSITION,0});
    r.info[0].floatCount = 4; r.info[1].floatCount = 7;
    r.info[1].samplers.push_back({3,2,true,false}); r.topology = D3DPT_TRIANGLELIST; r.color = 1;
    auto encoded = cache::Encode({r,r}); std::vector<cache::Recipe> decoded;
    assert(cache::Decode(encoded.data(),encoded.size(),decoded));
    assert(decoded.size() == 2 && decoded[0].vs == r.vs && decoded[1].key == r.key && decoded[0].info[1].samplers[0].reg == 3);
    assert(!cache::Decode(encoded.data(),encoded.size()-1,decoded));
    encoded[20] ^= 1; assert(!cache::Decode(encoded.data(),encoded.size(),decoded));
    encoded = cache::Encode({r}); encoded[4] = 255;
    auto sum = cache::Checksum(encoded.data(),encoded.size()-8); memcpy(encoded.data()+encoded.size()-8,&sum,8);
    assert(!cache::Decode(encoded.data(),encoded.size(),decoded));
    assert(decoded.empty());
    assert(!cache::Decode(nullptr,0,decoded));
    assert(!cache::Decode(encoded.data(),cache::MapLimit+1,decoded));
    r.vs.assign(700000,'v'); r.ps.assign(700000,'p');
    encoded = cache::Encode(std::vector<cache::Recipe>(30,r));
    assert(encoded.size() < cache::MapLimit);
    assert(!cache::Decode(encoded.data(),encoded.size(),decoded)); // Expansion budget.
    for (int i = 1; i < argc; ++i) {
        FILE *file = std::fopen(argv[i], "rb");
        if (!file) return 1;
        std::fseek(file, 0, SEEK_END); const long length = std::ftell(file); std::rewind(file);
        if (length < 24 || size_t(length) > cache::MapLimit) { std::fclose(file); return 1; }
        std::vector<uint8_t> bytes(size_t(length), 0);
        const bool read = std::fread(bytes.data(), 1, bytes.size(), file) == bytes.size(); std::fclose(file);
        if (!read || !cache::Decode(bytes.data(), bytes.size(), decoded)) return 1;
        std::printf("Catalog chunk checked: %zu recipes\n", decoded.size());
    }
    std::puts("GPU policy/cache checks passed");
}
