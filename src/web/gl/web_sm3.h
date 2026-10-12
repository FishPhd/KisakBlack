// Direct3D 9 shader model 3 (vs_3_0 / ps_3_0) bytecode -> GLSL ES 3.00 translator.
//
// Pure C++ (no GL, no engine dependencies) so it can be exercised anywhere. The device layer keeps one
// Result per created shader and links (vertex, pixel) pairs; the vertex shader text carries two
// placeholders that the linker fills in so the vertex stage can write every varying the pixel stage
// reads (D3D allows a pixel shader to read inputs the vertex shader never wrote; GLSL does not):
//     /*KISAK_EXTRA_DECL*/    extra "out vec4 io_<usage>_<index>;" declarations
//     /*KISAK_EXTRA_WRITE*/   matching "io_<usage>_<index> = vec4(0.0);" writes at the end of main
#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace sm3
{
// D3DDECLUSAGE values.
enum
{
    USAGE_POSITION = 0, USAGE_BLENDWEIGHT, USAGE_BLENDINDICES, USAGE_NORMAL, USAGE_PSIZE, USAGE_TEXCOORD,
    USAGE_TANGENT, USAGE_BINORMAL, USAGE_TESSFACTOR, USAGE_POSITIONT, USAGE_COLOR, USAGE_FOG, USAGE_DEPTH,
    USAGE_SAMPLE
};

// Vertex attribute location a (usage, index) pair is bound to. Shared by the translator (layout(location))
// and the device (vertexAttribPointer). Returns -1 for a combination that has no slot.
int AttribLocation(int usage, int index);

struct Input
{
    int usage, index, reg;
};

struct Sampler
{
    int reg;  // sN
    int type; // 2 = 2D, 3 = cube, 4 = volume
    bool proj = false; // projective texld of a 2D sampler (candidate for sampler2DShadow)
    bool raw = false;  // any other sample of the same register
};

struct Result
{
    bool ok = false;
    bool isPixel = false;
    int majorVersion = 0, minorVersion = 0;
    std::string error;
    std::string glsl;

    int floatCount = 0;     // number of float constant registers the shader reads (highest index + 1)
    bool relativeConst = false;
    std::vector<Input> inputs;     // VS: declared attributes; PS: declared varyings
    std::vector<Input> outputs;    // VS: declared varyings (non-position)
    std::vector<Sampler> samplers; // declared samplers
    bool writesColor0 = false;
    bool writesDepth = false;
    bool usesTexkill = false;
};

// tokens: the shader function as passed to CreateVertexShader/CreatePixelShader (terminated by 0x0000FFFF).
bool Translate(const uint32_t *tokens, Result &out);

// Combines the placeholders of a vertex shader with the varyings its paired pixel shader reads.
std::string LinkVertexGlsl(const Result &vs, const Result &ps);

// Copy of a pixel shader with projective 2D samplers in shadowMask declared as sampler2DShadow.
// shadowMask is a bit per sampler register. Samplers that are also read raw stay sampler2D.
std::string PixelVariant(const Result &ps, uint32_t shadowMask);

} // namespace sm3
