#include "web/gl/web_sm3.h"
#include <cstdio>
#include <cstdlib>

static void expect(bool condition, const char *description)
{
    if (!condition) { std::fprintf(stderr, "FAIL %s\n", description); std::exit(1); }
}

int main()
{
    // vs_3_0: dcl_position v0; dcl_position o0; mova a0.y, v0;
    // mov o0.xyz, c[a0.y + 2]. No instruction writes o0.w.
    const uint32_t shader[] = {
        0xfffe0300, 0x0200001f, 0x80000000, 0x900f0000,
        0x0200001f, 0x80000000, 0xe00f0000,
        0x0200002e, 0xb0020000, 0x90e40000,
        0x03000001, 0xe0070000, 0xa0e42002, 0xb0550000, 0x0000ffff
    };
    sm3::Result result;
    expect(sm3::Translate(shader, result), result.error.c_str());
    expect(result.glsl.find("ivec4 a0") != std::string::npos, "vector address register");
    expect(result.glsl.find("a0.y = ivec4(round(") != std::string::npos, "masked MOVA");
    expect(result.glsl.find("vc[2 + a0.y]") != std::string::npos, "relative component");
    expect(result.glsl.find("vec4 o0 = vec4(0.0, 0.0, 0.0, 1.0)") != std::string::npos, "default position w");
    expect(result.glsl.find("layout(std140) uniform VertexConstants") != std::string::npos, "stage UBO");
    expect(result.glsl.find("ivec4 vb[16]") != std::string::npos, "16-byte boolean stride");
    // ps_3_0: cmp r0, c0, r1, r2; mov oC0, r0.
    // The selector must remain boolean: unused non-finite branch components
    // must not contaminate the selected value through arithmetic interpolation.
    const uint32_t cmpShader[] = {
        0xffff0300, 0x04000058, 0x800f0000, 0xa0e40000,
        0x80e40001, 0x80e40002, 0x02000001, 0x800f0800,
        0x80e40000, 0x0000ffff
    };
    expect(sm3::Translate(cmpShader, result), result.error.c_str());
    expect(result.glsl.find("mix(r2, r1, greaterThanEqual(pc[0], vec4(0.0)))") != std::string::npos,
        "CMP uses boolean component selection");
    std::string wgsl, error;
    sm3::Result pixelInterface;
    expect(sm3::TranslateWgsl(shader, pixelInterface, 0, wgsl, error), error.c_str());
    expect(wgsl.find("uni.fc[2 + a0.y]") != std::string::npos, "WGSL relative address component");
    expect(wgsl.find("fc: array<vec4f, 256>") != std::string::npos, "relative addressing retains the full register bank");
    expect(wgsl.find("a0.y = address.y") != std::string::npos, "WGSL masked address write");
    expect(wgsl.find("o0.z = masked.z") != std::string::npos, "WGSL simultaneous masked write");
    expect(wgsl.find("result.position = o0") != std::string::npos, "WebGPU native clip depth");
    auto jsonString = [](const std::string &s) {
        std::putchar('"');
        for (unsigned char c : s) { if (c == '"' || c == '\\') std::putchar('\\'); if (c == '\n') std::printf("\\n"); else if (c == '\r') std::printf("\\r"); else std::putchar(c); }
        std::putchar('"');
    };
    std::printf("WGSL_FIXTURE "); jsonString(wgsl); std::putchar('\n');
    const uint32_t depthShader[] = {
        0xffff0300,
        0x0200001f, 0x80000005, 0x900f0000, // dcl_texcoord0 v0
        0x0200001f, 0x90000000, 0xa00f0800, // dcl_2d s0
        0x03010042, 0x800f0000, 0x90e40000, 0xa0e40800, // texldp r0,v0,s0
        0x02000001, 0x800f0800, 0x80e40000, 0x0000ffff
    };
    expect(sm3::Translate(depthShader, pixelInterface), pixelInterface.error.c_str());
    expect(sm3::TranslateWgsl(depthShader, pixelInterface, 1, wgsl, error), error.c_str());
    expect(wgsl.find("texture_depth_2d") != std::string::npos, "depth texture specialization");
    expect(wgsl.find("textureSampleCompareLevel") != std::string::npos, "projected depth comparison");
    // Same sampler is both a projective shadow test and a raw read.
    const uint32_t dualShader[] = {
        0xffff0300,
        0x0200001f, 0x80000005, 0x900f0000,
        0x0200001f, 0x90000000, 0xa00f0800,
        0x03010042, 0x800f0000, 0x90e40000, 0xa0e40800,
        0x03000042, 0x800f0001, 0x90e40000, 0xa0e40800,
        0x02000001, 0x800f0800, 0x80e40000, 0x0000ffff
    };
    expect(sm3::Translate(dualShader, pixelInterface), pixelInterface.error.c_str());
    expect(sm3::TranslateWgsl(dualShader, pixelInterface, 1, wgsl, error), error.c_str());
    expect(wgsl.find("textureSampleCompareLevel") != std::string::npos, "dual sampler still compares");
    expect(wgsl.find("textureLoad(") != std::string::npos, "raw depth read is a texel fetch");
    expect(wgsl.find("textureSampleLevel") == std::string::npos, "depth textures reject textureSampleLevel");
    expect(wgsl.find("sampler;") == std::string::npos, "raw depth read does not add a filtering sampler");
    std::printf("WGSL_FIXTURE "); jsonString(wgsl); std::putchar('\n');
    int vertexTypes[16]{}; vertexTypes[0] = 3;
    expect(sm3::TranslateWgsl(shader, pixelInterface, 0, wgsl, error, vertexTypes), error.c_str());
    expect(wgsl.find("input0: vec3<f32>") != std::string::npos, "vertex input matches float3 buffer format");
    expect(wgsl.find("vec4f(input0, 1.0)") != std::string::npos, "missing vertex W defaults to one");
    expect(wgsl.find("@location(0) io80") != std::string::npos, "varying semantics use compact shader locations");
    std::printf("WGSL_FIXTURE "); jsonString(wgsl); std::putchar('\n');
    expect(sm3::TranslateWgsl(cmpShader, pixelInterface, 0, wgsl, error), error.c_str());
    expect(wgsl.find("select(r2, r1, uni.fc[0] >= vec4f(0.0))") != std::string::npos, "WGSL CMP boolean selection");
    expect(wgsl.find("fc: array<vec4f, 1>") != std::string::npos, "direct constants use a compact register bank");
    std::printf("WGSL_FIXTURE "); jsonString(wgsl); std::putchar('\n');
    std::puts("PASS SM3 relative addressing, MOVA masks, position w, std140 constants, CMP selection");
}
