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
    std::puts("PASS SM3 relative addressing, MOVA masks, position w, std140 constants, CMP selection");
}
