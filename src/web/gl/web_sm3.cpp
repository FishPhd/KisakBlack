// Direct3D 9 shader model 3 bytecode -> GLSL ES 3.00. See web_sm3.h.
//
// Conventions (must agree with web_d3d9_device.cpp):
//   * Vertex shaders output D3D clip space; the epilogue converts it to GL clip space:
//       - y is negated so render targets keep D3D's top-down row order (texture memory row 0 == top),
//       - z is remapped from [0,w] to [-w,w].
//         WebGL2 and D3D9 both rasterize at pixel centers, so clip space is not shifted by half a pixel.
//   * Registers are plain globals (r#, v#, o#, p0, a0, aL) so subroutines (call/label/ret) can share them.
//   * Float constants live in "uniform vec4 vc[N]" / "pc[N]" (indexed by register number).
//   * Pixel shaders get alpha test (u_at) and sRGB write (u_srgbW) emulation. Alpha is quantized to
//     0..255 before the compare (D3D's 8-bit alpha test). sRGB reads are a hardware texture format;
//     u_srgbTex / u_srgbEnc are only the mixed case one storage format cannot express.
//     u_swzTex is 2 bits per sampler, applied inside k_srgb: 1 = L8 (R replicated to RGB, alpha 1),
//     2 = A8 (alpha from R), 3 = A8L8 (RGB from R, alpha from G). 0 leaves the texel alone.
//     A projective 2D sample that is never also read raw is declared sampler2D by default and retargeted
//     to sampler2DShadow when that register is bound to a depth texture (PixelVariant). A register that
//     is both projected and read raw stays sampler2D; u_shadTex then does the compare in the shader.
#include "web_sm3.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <set>

namespace sm3
{
namespace
{
enum Op
{
    OP_NOP = 0, OP_MOV, OP_ADD, OP_SUB, OP_MAD, OP_MUL, OP_RCP, OP_RSQ, OP_DP3, OP_DP4, OP_MIN, OP_MAX, OP_SLT,
    OP_SGE, OP_EXP, OP_LOG, OP_LIT, OP_DST, OP_LRP, OP_FRC, OP_M4x4, OP_M4x3, OP_M3x4, OP_M3x3, OP_M3x2,
    OP_CALL, OP_CALLNZ, OP_LOOP, OP_RET, OP_ENDLOOP, OP_LABEL, OP_DCL, OP_POW, OP_CRS, OP_SGN, OP_ABS, OP_NRM,
    OP_SINCOS, OP_REP, OP_ENDREP, OP_IF, OP_IFC, OP_ELSE, OP_ENDIF, OP_BREAK, OP_BREAKC, OP_MOVA, OP_DEFB,
    OP_DEFI,
    OP_TEXKILL = 65, OP_TEXLD = 66,
    OP_DEF = 81,
    OP_CMP = 88, OP_DP2ADD = 90, OP_DSX = 91, OP_DSY = 92, OP_TEXLDD = 93, OP_SETP = 94, OP_TEXLDL = 95,
    OP_BREAKP = 96,
    OP_PHASE = 0xFFFD, OP_COMMENT = 0xFFFE, OP_END = 0xFFFF
};

enum RegType
{
    RT_TEMP = 0, RT_INPUT = 1, RT_CONST = 2, RT_ADDR = 3, RT_RASTOUT = 4, RT_ATTROUT = 5, RT_OUTPUT = 6,
    RT_CONSTINT = 7, RT_COLOROUT = 8, RT_DEPTHOUT = 9, RT_SAMPLER = 10, RT_CONST2 = 11, RT_CONST3 = 12,
    RT_CONST4 = 13, RT_CONSTBOOL = 14, RT_LOOP = 15, RT_TEMPFLOAT16 = 16, RT_MISC = 17, RT_LABEL = 18,
    RT_PREDICATE = 19
};

struct Reg
{
    int type = 0;
    int num = 0;
    bool rel = false;
    int relType = 0;
    int relNum = 0;
    int relComponent = 0;
};

struct SrcOp
{
    Reg r;
    uint8_t swz[4] = {0, 1, 2, 3};
    int mod = 0;
};

struct DstOp
{
    Reg r;
    int mask = 0xF;
    bool sat = false;
    int shift = 0;
};

struct Inst
{
    int op = 0;
    int ctrl = 0;
    bool pred = false;
    bool hasDst = false;
    DstOp d;
    SrcOp s[6];
    int ns = 0;
    SrcOp predSrc;
    uint32_t imm[4] = {0, 0, 0, 0};
    int declUsage = 0, declIndex = 0, declType = 0;
};

inline int RegTypeOf(uint32_t t)
{
    return (int)(((t >> 28) & 0x7) | ((t >> 8) & 0x18));
}

bool HasDst(int op)
{
    switch (op)
    {
    case OP_NOP: case OP_CALL: case OP_CALLNZ: case OP_LOOP: case OP_RET: case OP_ENDLOOP: case OP_LABEL:
    case OP_REP: case OP_ENDREP: case OP_IF: case OP_IFC: case OP_ELSE: case OP_ENDIF: case OP_BREAK:
    case OP_BREAKC: case OP_BREAKP: case OP_PHASE:
        return false;
    default:
        return true;
    }
}

struct Decoder
{
    const uint32_t *p;
    std::string err;

    bool readRel(Reg &r)
    {
        const uint32_t t = *p++;
        r.relType = RegTypeOf(t);
        r.relNum = (int)(t & 0x7FF);
        r.relComponent = (int)((t >> 16) & 3);
        return true;
    }

    void readDst(DstOp &d)
    {
        const uint32_t t = *p++;
        d.r.type = RegTypeOf(t);
        d.r.num = (int)(t & 0x7FF);
        d.mask = (int)((t >> 16) & 0xF);
        d.sat = ((t >> 20) & 1) != 0;
        int shift = (int)((t >> 24) & 0xF);
        if (shift > 7)
            shift -= 16;
        d.shift = shift;
        d.r.rel = (t & 0x2000) != 0;
        if (d.r.rel)
            readRel(d.r);
    }

    void readSrc(SrcOp &s)
    {
        const uint32_t t = *p++;
        s.r.type = RegTypeOf(t);
        s.r.num = (int)(t & 0x7FF);
        for (int i = 0; i < 4; ++i)
            s.swz[i] = (uint8_t)((t >> (16 + 2 * i)) & 3);
        s.mod = (int)((t >> 24) & 0xF);
        s.r.rel = (t & 0x2000) != 0;
        if (s.r.rel)
            readRel(s.r);
    }
};

std::string F(float v)
{
    if (std::isnan(v))
        return "0.0";
    if (std::isinf(v))
        return v > 0 ? "3.4e38" : "-3.4e38";
    char buf[48];
    snprintf(buf, sizeof(buf), "%.9g", v);
    if (!strpbrk(buf, ".eE"))
        strcat(buf, ".0");
    return buf;
}

float BitsToFloat(uint32_t u)
{
    float f;
    memcpy(&f, &u, 4);
    return f;
}

const char *kCmp[] = {"?", ">", "==", ">=", "<", "!=", "<="};

struct Gen
{
    Result &out;
    bool ps = false;
    std::vector<Inst> insts;

    // metadata
    int maxTemp = -1, maxIn = -1, maxOut = -1, maxColor = -1, maxTex = -1;
    bool usesA0 = false, usesAL = false, usesP0 = false, usesDepth = false;
    bool usesVPos = false, usesVFace = false;
    bool usesPointSize = false;
    int outPos = -1;
    int psizeReg = -1;
    std::set<int> usedFloat, usedInt, usedBool;
    struct F4 { float v[4]; };
    struct I4 { int v[4]; };
    std::map<int, F4> defF;
    std::map<int, I4> defI;
    std::map<int, int> defB;
    std::set<int> labels;
    uint32_t helpers = 0; // bit per helper function
    bool usesCalls = false;

    explicit Gen(Result &r) : out(r) {}

    // ---------------------------------------------------------------------------------- decode
    bool decode(const uint32_t *tokens)
    {
        const uint32_t ver = tokens[0];
        const uint32_t kind = ver >> 16;
        if (kind == 0xFFFE)
            ps = false;
        else if (kind == 0xFFFF)
            ps = true;
        else
        {
            out.error = "not a shader (bad version token)";
            return false;
        }
        out.isPixel = ps;
        out.majorVersion = (int)((ver >> 8) & 0xFF);
        out.minorVersion = (int)(ver & 0xFF);
        if (out.majorVersion != 3)
        {
            char b[64];
            snprintf(b, sizeof(b), "unsupported shader model %d.%d", out.majorVersion, out.minorVersion);
            out.error = b;
            return false;
        }

        Decoder dec;
        dec.p = tokens + 1;
        for (;;)
        {
            const uint32_t tok = *dec.p;
            const int op = (int)(tok & 0xFFFF);
            if (op == OP_END)
                break;
            if (op == OP_COMMENT)
            {
                dec.p += 1 + ((tok >> 16) & 0x7FFF);
                continue;
            }
            const int len = (int)((tok >> 24) & 0xF);
            const uint32_t *end = dec.p + 1 + len;
            Inst in;
            in.op = op;
            in.ctrl = (int)((tok >> 16) & 0xFF);
            in.pred = (tok & 0x10000000) != 0;
            ++dec.p;

            if (op == OP_DCL)
            {
                const uint32_t d = *dec.p++;
                in.declUsage = (int)(d & 0x1F);
                in.declIndex = (int)((d >> 16) & 0xF);
                in.declType = (int)((d >> 27) & 0xF);
                dec.readDst(in.d);
                in.hasDst = true;
            }
            else if (op == OP_DEF)
            {
                dec.readDst(in.d);
                in.hasDst = true;
                for (int i = 0; i < 4; ++i)
                    in.imm[i] = *dec.p++;
            }
            else if (op == OP_DEFI)
            {
                dec.readDst(in.d);
                in.hasDst = true;
                for (int i = 0; i < 4; ++i)
                    in.imm[i] = *dec.p++;
            }
            else if (op == OP_DEFB)
            {
                dec.readDst(in.d);
                in.hasDst = true;
                in.imm[0] = *dec.p++;
            }
            else
            {
                if (HasDst(op) && dec.p < end)
                {
                    dec.readDst(in.d);
                    in.hasDst = true;
                }
                const uint32_t *srcEnd = end - (in.pred ? 1 : 0);
                while (dec.p < srcEnd && in.ns < 6)
                    dec.readSrc(in.s[in.ns++]);
                if (in.pred)
                    dec.readSrc(in.predSrc);
            }
            dec.p = end;
            insts.push_back(in);
        }
        return true;
    }

    // ---------------------------------------------------------------------------------- analysis
    void noteSrcReg(const Reg &r)
    {
        switch (r.type)
        {
        case RT_TEMP: maxTemp = std::max(maxTemp, r.num); break;
        case RT_INPUT: maxIn = std::max(maxIn, r.num); break;
        case RT_CONST: case RT_CONST2: case RT_CONST3: case RT_CONST4:
            if (r.rel)
                out.relativeConst = true;
            else
            {
                const int n = r.num + (r.type == RT_CONST2 ? 2048 : r.type == RT_CONST3 ? 4096 : r.type == RT_CONST4 ? 6144 : 0);
                if (n < 256)
                    usedFloat.insert(n);
            }
            break;
        case RT_CONSTINT: usedInt.insert(r.num); break;
        case RT_CONSTBOOL: usedBool.insert(r.num); break;
        case RT_OUTPUT: maxOut = std::max(maxOut, r.num); break;
        case RT_COLOROUT: maxColor = std::max(maxColor, r.num); break;
        case RT_DEPTHOUT: usesDepth = true; break;
        case RT_MISC:
            if (r.num == 0) usesVPos = true; else usesVFace = true;
            break;
        case RT_LOOP: usesAL = true; break;
        case RT_PREDICATE: usesP0 = true; break;
        default: break;
        }
        if (r.rel)
        {
            if (r.relType == RT_ADDR) usesA0 = true;
            if (r.relType == RT_LOOP) usesAL = true;
        }
    }

    bool analyze()
    {
        for (Inst &in : insts)
        {
            if (in.op == OP_DCL)
            {
                const Reg &r = in.d.r;
                if (r.type == RT_SAMPLER)
                {
                    Sampler s;
                    s.reg = r.num;
                    s.type = in.declType ? in.declType : 2;
                    out.samplers.push_back(s);
                }
                else if (r.type == RT_INPUT)
                {
                    out.inputs.push_back({in.declUsage, in.declIndex, r.num});
                    maxIn = std::max(maxIn, r.num);
                }
                else if (r.type == RT_OUTPUT)
                {
                    maxOut = std::max(maxOut, r.num);
                    if (in.declUsage == USAGE_POSITION && in.declIndex == 0)
                        outPos = r.num;
                    else if (in.declUsage == USAGE_PSIZE)
                    {
                        psizeReg = r.num;
                        usesPointSize = true;
                    }
                    else
                        out.outputs.push_back({in.declUsage, in.declIndex, r.num});
                }
                else if (r.type == RT_MISC)
                {
                    if (r.num == 0) usesVPos = true; else usesVFace = true;
                }
                continue;
            }
            if (in.op == OP_DEF)
            {
                float *v = defF[in.d.r.num].v;
                for (int i = 0; i < 4; ++i)
                    v[i] = BitsToFloat(in.imm[i]);
                continue;
            }
            if (in.op == OP_DEFI)
            {
                int *v = defI[in.d.r.num].v;
                for (int i = 0; i < 4; ++i)
                    v[i] = (int)in.imm[i];
                continue;
            }
            if (in.op == OP_DEFB)
            {
                defB[in.d.r.num] = (int)(in.imm[0] != 0);
                continue;
            }
            if (in.op == OP_LABEL || in.op == OP_CALL || in.op == OP_CALLNZ)
                usesCalls = true;
            if (in.hasDst)
            {
                const Reg &r = in.d.r;
                switch (r.type)
                {
                case RT_TEMP: maxTemp = std::max(maxTemp, r.num); break;
                case RT_OUTPUT: maxOut = std::max(maxOut, r.num); break;
                case RT_COLOROUT:
                    maxColor = std::max(maxColor, r.num);
                    if (r.num == 0) out.writesColor0 = true;
                    break;
                case RT_DEPTHOUT: usesDepth = true; out.writesDepth = true; break;
                case RT_ADDR: usesA0 = true; break;
                case RT_PREDICATE: usesP0 = true; break;
                case RT_RASTOUT:
                    if (r.num == 0) outPos = -2; // oPos
                    break;
                default: break;
                }
                if (in.op == OP_TEXKILL)
                    out.usesTexkill = true;
            }
            for (int i = 0; i < in.ns; ++i)
                noteSrcReg(in.s[i].r);
            if (in.pred)
            {
                usesP0 = true;
                noteSrcReg(in.predSrc.r);
            }
            if (in.op == OP_LOOP || in.op == OP_REP)
                for (int i = 0; i < in.ns; ++i)
                    if (in.s[i].r.type == RT_CONSTINT)
                        usedInt.insert(in.s[i].r.num);
        }
        // Matrix instructions read consecutive constants.
        for (Inst &in : insts)
        {
            int rows = 0;
            switch (in.op)
            {
            case OP_M4x4: rows = 4; break;
            case OP_M4x3: rows = 4; break;
            case OP_M3x4: rows = 3; break;
            case OP_M3x3: rows = 3; break;
            case OP_M3x2: rows = 2; break;
            default: break;
            }
            // m4x3 uses 3 rows, m3x4 uses 4 rows
            if (in.op == OP_M4x3) rows = 3;
            if (in.op == OP_M3x4) rows = 4;
            if (rows && in.ns >= 2 && !in.s[1].r.rel)
                for (int k = 0; k < rows; ++k)
                    if (in.s[1].r.num + k < 256)
                        usedFloat.insert(in.s[1].r.num + k);
        }
        int maxF = -1;
        for (int n : usedFloat)
            if (!defF.count(n))
                maxF = std::max(maxF, n);
        out.floatCount = out.relativeConst ? 256 : maxF + 1;
        return true;
    }

    // ---------------------------------------------------------------------------------- expressions
    std::string cArray() const { return ps ? "pc" : "vc"; }
    std::string iArray() const { return ps ? "pi" : "vi"; }
    std::string bArray() const { return ps ? "pb" : "vb"; }

    std::string relExpr(const Reg &r) const
    {
        return r.relType == RT_LOOP ? "aL" : std::string("a0.") + "xyzw"[r.relComponent];
    }

    std::string constName(int n) const
    {
        char b[32];
        if (defF.count(n))
        {
            snprintf(b, sizeof(b), "cd%d", n);
            return b;
        }
        snprintf(b, sizeof(b), "[%d]", n);
        return cArray() + b;
    }

    std::string regRead(const Reg &r)
    {
        char b[96];
        switch (r.type)
        {
        case RT_TEMP: snprintf(b, sizeof(b), "r%d", r.num); return b;
        case RT_INPUT: snprintf(b, sizeof(b), "v%d", r.num); return b;
        case RT_CONST: case RT_CONST2: case RT_CONST3: case RT_CONST4:
        {
            const int n = r.num + (r.type == RT_CONST2 ? 2048 : r.type == RT_CONST3 ? 4096 : r.type == RT_CONST4 ? 6144 : 0);
            if (r.rel)
            {
                snprintf(b, sizeof(b), "[%d + %s]", n, relExpr(r).c_str());
                return cArray() + b;
            }
            if (n >= 256)
                return "vec4(0.0)";
            return constName(n);
        }
        case RT_ADDR:
            if (ps) { snprintf(b, sizeof(b), "v%d", r.num); return b; }
            return "vec4(a0)";
        case RT_RASTOUT:
            return r.num == 0 ? "opos" : "vec4(0.0)";
        case RT_OUTPUT: case RT_ATTROUT: snprintf(b, sizeof(b), "o%d", r.num); return b;
        case RT_CONSTINT:
            if (defI.count(r.num)) { snprintf(b, sizeof(b), "vec4(id%d)", r.num); return b; }
            snprintf(b, sizeof(b), "vec4(%s[%d])", iArray().c_str(), r.num);
            return b;
        case RT_COLOROUT: snprintf(b, sizeof(b), "oC%d", r.num); return b;
        case RT_DEPTHOUT: return "vec4(oDepth)";
        case RT_CONSTBOOL:
            if (defB.count(r.num)) { snprintf(b, sizeof(b), "vec4(bd%d ? 1.0 : 0.0)", r.num); return b; }
            snprintf(b, sizeof(b), "vec4(%s[%d].x != 0 ? 1.0 : 0.0)", bArray().c_str(), r.num);
            return b;
        case RT_LOOP: return "vec4(float(aL))";
        case RT_MISC: return r.num == 0 ? "vPos" : "vFace";
        case RT_PREDICATE: return "vec4(p0)";
        default: return "vec4(0.0)";
        }
    }

    static std::string Swz(const uint8_t *s)
    {
        if (s[0] == 0 && s[1] == 1 && s[2] == 2 && s[3] == 3)
            return "";
        static const char c[] = "xyzw";
        std::string r = ".";
        for (int i = 0; i < 4; ++i)
            r += c[s[i]];
        return r;
    }

    std::string src(const SrcOp &s)
    {
        std::string e = regRead(s.r) + Swz(s.swz);
        switch (s.mod)
        {
        case 0: return e;
        case 1: return "(-" + e + ")";
        case 2: return "(" + e + " - vec4(0.5))";
        case 3: return "(-(" + e + " - vec4(0.5)))";
        case 4: return "(" + e + " * 2.0 - vec4(1.0))";
        case 5: return "(-(" + e + " * 2.0 - vec4(1.0)))";
        case 6: return "(vec4(1.0) - " + e + ")";
        case 7: return "(" + e + " * 2.0)";
        case 8: return "(-(" + e + " * 2.0))";
        case 9: return "(" + e + " / " + e + ".z)";
        case 10: return "(" + e + " / " + e + ".w)";
        case 11: return "abs(" + e + ")";
        case 12: return "(-abs(" + e + "))";
        default: return e;
        }
    }

    // Scalar-ish source component, for compares and loop counters.
    std::string srcX(const SrcOp &s) { return src(s) + ".x"; }

    std::string dstName(const DstOp &d)
    {
        char b[48];
        switch (d.r.type)
        {
        case RT_TEMP: snprintf(b, sizeof(b), "r%d", d.r.num); return b;
        case RT_OUTPUT: case RT_ATTROUT: snprintf(b, sizeof(b), "o%d", d.r.num); return b;
        case RT_COLOROUT: snprintf(b, sizeof(b), "oC%d", d.r.num); return b;
        case RT_RASTOUT: return d.r.num == 0 ? "opos" : "ofog";
        case RT_INPUT: snprintf(b, sizeof(b), "v%d", d.r.num); return b; // texkill on a texture register
        case RT_DEPTHOUT: return "oDepth";
        default: return "rdummy";
        }
    }

    static std::string MaskStr(int mask)
    {
        std::string r;
        if (mask & 1) r += 'x';
        if (mask & 2) r += 'y';
        if (mask & 4) r += 'z';
        if (mask & 8) r += 'w';
        return r;
    }

    // Writes a vec4-typed expression to the destination, honoring mask / saturate / shift.
    std::string write(const DstOp &d, std::string e)
    {
        if (d.sat)
            e = "clamp(" + e + ", 0.0, 1.0)";
        if (d.shift > 0)
            e = "(" + e + ") * " + F((float)(1 << d.shift));
        else if (d.shift < 0)
            e = "(" + e + ") * " + F(1.0f / (float)(1 << -d.shift));
        const std::string name = dstName(d);
        if (d.r.type == RT_DEPTHOUT)
            return name + " = (" + e + ").x;";
        if (d.mask == 0xF || d.mask == 0)
            return name + " = " + e + ";";
        const std::string m = MaskStr(d.mask);
        return name + "." + m + " = (" + e + ")." + m + ";";
    }

    std::string matRow(const SrcOp &m, int k)
    {
        Reg r = m.r;
        r.num += k;
        return regRead(r);
    }

    std::string samplerName(const SrcOp &s) const
    {
        char b[32];
        snprintf(b, sizeof(b), "%s_s%d", ps ? "ps" : "vs", s.r.num);
        return b;
    }

    int samplerType(int reg) const
    {
        for (const Sampler &s : out.samplers)
            if (s.reg == reg)
                return s.type;
        return 2;
    }

    const Sampler *findSampler(int reg) const
    {
        for (const Sampler &s : out.samplers)
            if (s.reg == reg)
                return &s;
        return nullptr;
    }

    // Projective texld of a 2D sampler that is not also sampled another way can be a comparison sampler.
    void noteSampleUses()
    {
        for (const Inst &in : insts)
        {
            if (in.op != OP_TEXLD && in.op != OP_TEXLDL && in.op != OP_TEXLDD)
                continue;
            if (in.ns < 2 || in.s[1].r.type != RT_SAMPLER)
                continue;
            for (Sampler &s : out.samplers)
            {
                if (s.reg != in.s[1].r.num)
                    continue;
                const bool proj = in.op == OP_TEXLD && (in.ctrl & 1) != 0 && s.type == 2;
                if (proj)
                    s.proj = true;
                else
                    s.raw = true;
            }
        }
    }

    // ---------------------------------------------------------------------------------- emission
    std::string indent(int depth) const { return std::string((size_t)depth * 2 + 2, ' '); }

    bool emitInst(const Inst &in, std::string &o, int &depth, std::vector<std::string> &closers, int &uid)
    {
        std::string stmt;
        auto S = [&](int i) { return src(in.s[i]); };
        auto W = [&](const std::string &e) { stmt = write(in.d, e); };

        switch (in.op)
        {
        case OP_NOP: case OP_PHASE: case OP_DCL: case OP_DEF: case OP_DEFI: case OP_DEFB:
            return true;
        case OP_MOV: W(S(0)); break;
        case OP_ADD: W(S(0) + " + " + S(1)); break;
        case OP_SUB: W(S(0) + " - " + S(1)); break;
        case OP_MAD: W(S(0) + " * " + S(1) + " + " + S(2)); break;
        case OP_MUL: W(S(0) + " * " + S(1)); break;
        case OP_RCP: W("vec4(1.0) / " + S(0)); break;
        case OP_RSQ: W("inversesqrt(abs(" + S(0) + "))"); break;
        case OP_DP3: W("vec4(dot(" + S(0) + ".xyz, " + S(1) + ".xyz))"); break;
        case OP_DP4: W("vec4(dot(" + S(0) + ", " + S(1) + "))"); break;
        case OP_MIN: W("min(" + S(0) + ", " + S(1) + ")"); break;
        case OP_MAX: W("max(" + S(0) + ", " + S(1) + ")"); break;
        case OP_SLT: W("vec4(lessThan(" + S(0) + ", " + S(1) + "))"); break;
        case OP_SGE: W("vec4(greaterThanEqual(" + S(0) + ", " + S(1) + "))"); break;
        case OP_EXP: W("exp2(" + S(0) + ")"); break;
        case OP_LOG: W("log2(abs(" + S(0) + "))"); break;
        case OP_LIT: helpers |= 1; W("k_lit(" + S(0) + ")"); break;
        case OP_DST: W("vec4(1.0, " + S(0) + ".y * " + S(1) + ".y, " + S(0) + ".z, " + S(1) + ".w)"); break;
        case OP_LRP: W("mix(" + S(2) + ", " + S(1) + ", " + S(0) + ")"); break;
        case OP_FRC: W("fract(" + S(0) + ")"); break;
        case OP_M4x4:
        {
            const std::string a = S(0);
            W("vec4(dot(" + a + ", " + matRow(in.s[1], 0) + "), dot(" + a + ", " + matRow(in.s[1], 1) +
              "), dot(" + a + ", " + matRow(in.s[1], 2) + "), dot(" + a + ", " + matRow(in.s[1], 3) + "))");
            break;
        }
        case OP_M4x3:
        {
            const std::string a = S(0);
            W("vec4(dot(" + a + ", " + matRow(in.s[1], 0) + "), dot(" + a + ", " + matRow(in.s[1], 1) +
              "), dot(" + a + ", " + matRow(in.s[1], 2) + "), 1.0)");
            break;
        }
        case OP_M3x4:
        {
            const std::string a = S(0) + ".xyz";
            W("vec4(dot(" + a + ", " + matRow(in.s[1], 0) + ".xyz), dot(" + a + ", " + matRow(in.s[1], 1) +
              ".xyz), dot(" + a + ", " + matRow(in.s[1], 2) + ".xyz), dot(" + a + ", " + matRow(in.s[1], 3) + ".xyz))");
            break;
        }
        case OP_M3x3:
        {
            const std::string a = S(0) + ".xyz";
            W("vec4(dot(" + a + ", " + matRow(in.s[1], 0) + ".xyz), dot(" + a + ", " + matRow(in.s[1], 1) +
              ".xyz), dot(" + a + ", " + matRow(in.s[1], 2) + ".xyz), 1.0)");
            break;
        }
        case OP_M3x2:
        {
            const std::string a = S(0) + ".xyz";
            W("vec4(dot(" + a + ", " + matRow(in.s[1], 0) + ".xyz), dot(" + a + ", " + matRow(in.s[1], 1) +
              ".xyz), 1.0, 1.0)");
            break;
        }
        case OP_POW: W("pow(abs(" + S(0) + "), " + S(1) + ")"); break;
        case OP_CRS: W("vec4(cross(" + S(0) + ".xyz, " + S(1) + ".xyz), 0.0)"); break;
        case OP_SGN: W("sign(" + S(0) + ")"); break;
        case OP_ABS: W("abs(" + S(0) + ")"); break;
        case OP_NRM: helpers |= 2; W("k_nrm(" + S(0) + ")"); break;
        case OP_SINCOS: W("vec4(cos(" + S(0) + ".x), sin(" + S(0) + ".x), 0.0, 0.0)"); break;
        case OP_MOVA:
            stmt = "a0." + MaskStr(in.d.mask) + " = ivec4(round(" + S(0) + "))." + MaskStr(in.d.mask) + ";";
            break;
        // CMP selects a component; it does not interpolate its two values.
        // A float mask evaluates 0 * infinity from an unused branch as NaN,
        // which can poison the sky shader's height-fog calculations.
        case OP_CMP: W("mix(" + S(2) + ", " + S(1) + ", greaterThanEqual(" + S(0) + ", vec4(0.0)))"); break;
        case OP_DP2ADD: W("vec4(dot(" + S(0) + ".xy, " + S(1) + ".xy) + " + S(2) + ".x)"); break;
        case OP_DSX: W("dFdx(" + S(0) + ")"); break;
        case OP_DSY: W("dFdy(" + S(0) + ")"); break;
        case OP_TEXKILL:
            stmt = "if (any(lessThan(" + dstName(in.d) + ".xyz, vec3(0.0)))) discard;";
            break;
        case OP_TEXLD: case OP_TEXLDL: case OP_TEXLDD:
        {
            if (in.ns < 2)
            {
                out.error = "texld with too few operands";
                return false;
            }
            const int reg = in.s[1].r.num;
            const int type = samplerType(reg);
            const Sampler *sm = findSampler(reg);
            const bool dual = sm && sm->proj && sm->raw && type == 2 && in.op == OP_TEXLD && (in.ctrl & 1);
            const std::string c = S(0);
            const std::string sn = samplerName(in.s[1]);
            const char *dim = type == 3 ? ".xyz" : type == 4 ? ".xyz" : ".xy";
            std::string t;
            if (in.op == OP_TEXLDL)
                t = "textureLod(" + sn + ", " + c + dim + ", " + c + ".w)";
            else if (in.op == OP_TEXLDD)
            {
                const char *gd = type == 2 ? ".xy" : ".xyz";
                t = "textureGrad(" + sn + ", " + c + dim + ", " + S(2) + gd + ", " + S(3) + gd + ")";
            }
            else if (in.ctrl & 1) // project
            {
                // vec4() so the same text is valid when PixelVariant retargets this uniform to
                // sampler2DShadow (textureProj then returns a float).
                t = type == 2 ? "vec4(textureProj(" + sn + ", " + c + "))"
                              : "texture(" + sn + ", " + c + dim + " / " + c + ".w)";
            }
            else if (in.ctrl & 2) // bias
                t = "texture(" + sn + ", " + c + dim + ", " + c + ".w)";
            else
                t = "texture(" + sn + ", " + c + dim + ")";
            if (ps)
            {
                helpers |= 4;
                char b[96];
                if (dual)
                {
                    // One register is both a shadow test and a raw read, so it cannot be sampler2DShadow.
                    helpers |= 8;
                    const std::string zref = c + ".z / max(" + c + ".w, 1e-20)";
                    snprintf(b, sizeof(b), "k_dual(%d, ", reg);
                    t = std::string(b) + t + ", " + zref + ")";
                }
                else
                {
                    snprintf(b, sizeof(b), "k_srgb(%d, ", reg);
                    t = std::string(b) + t + ")";
                }
            }
            W(t);
            break;
        }
        case OP_SETP:
        {
            const char *cmp = kCmp[(in.ctrl & 7) < 7 ? (in.ctrl & 7) : 0];
            static const char *comp[4] = {"x", "y", "z", "w"};
            for (int i = 0; i < 4; ++i)
                if (in.d.mask & (1 << i))
                    stmt += std::string("p0.") + comp[i] + " = (" + S(0) + "." + comp[i] + " " + cmp + " " + S(1) + "." +
                            comp[i] + "); ";
            break;
        }
        case OP_IF:
        {
            std::string c;
            if (in.s[0].r.type == RT_CONSTBOOL)
            {
                char b[48];
                if (defB.count(in.s[0].r.num))
                    snprintf(b, sizeof(b), "bd%d", in.s[0].r.num);
                else
                    snprintf(b, sizeof(b), "%s[%d].x != 0", bArray().c_str(), in.s[0].r.num);
                c = b;
            }
            else
                c = "(p0." + std::string(1, "xyzw"[in.s[0].swz[0]]) + ")";
            o += indent(depth) + "if (" + c + ") {\n";
            closers.push_back("}");
            ++depth;
            return true;
        }
        case OP_IFC:
        {
            const char *cmp = kCmp[(in.ctrl & 7) < 7 ? (in.ctrl & 7) : 0];
            o += indent(depth) + "if (" + srcX(in.s[0]) + " " + cmp + " " + srcX(in.s[1]) + ") {\n";
            closers.push_back("}");
            ++depth;
            return true;
        }
        case OP_ELSE:
            if (depth > 0)
                o += indent(depth - 1) + "} else {\n";
            return true;
        case OP_ENDIF:
            if (depth > 0)
            {
                --depth;
                if (!closers.empty())
                    closers.pop_back();
                o += indent(depth) + "}\n";
            }
            return true;
        case OP_LOOP:
        {
            ++uid;
            char b[256];
            const int n = in.ns > 1 ? in.s[1].r.num : 0;
            std::string iv;
            if (defI.count(n))
                snprintf(b, sizeof(b), "id%d", n), iv = b;
            else
                snprintf(b, sizeof(b), "%s[%d]", iArray().c_str(), n), iv = b;
            snprintf(b, sizeof(b), "{ int aLs%d = aL; ivec4 lv%d = %s; aL = lv%d.y;\n", uid, uid, iv.c_str(), uid);
            o += indent(depth) + b;
            snprintf(b, sizeof(b), "for (int lc%d = 0; lc%d < lv%d.x; ++lc%d) {\n", uid, uid, uid, uid);
            o += indent(depth) + b;
            char c[96];
            snprintf(c, sizeof(c), "aL += lv%d.z; }\n%saL = aLs%d; }", uid, indent(depth).c_str(), uid);
            closers.push_back(std::string("LOOP:") + c);
            ++depth;
            return true;
        }
        case OP_REP:
        {
            ++uid;
            char b[256];
            const int n = in.ns > 0 ? in.s[0].r.num : 0;
            std::string iv;
            if (defI.count(n))
                snprintf(b, sizeof(b), "id%d", n), iv = b;
            else
                snprintf(b, sizeof(b), "%s[%d]", iArray().c_str(), n), iv = b;
            snprintf(b, sizeof(b), "for (int rc%d = 0; rc%d < %s.x; ++rc%d) {\n", uid, uid, iv.c_str(), uid);
            o += indent(depth) + b;
            closers.push_back("}");
            ++depth;
            return true;
        }
        case OP_ENDLOOP: case OP_ENDREP:
            if (depth > 0)
            {
                --depth;
                std::string c = closers.empty() ? "}" : closers.back();
                if (!closers.empty())
                    closers.pop_back();
                if (c.rfind("LOOP:", 0) == 0)
                    c = c.substr(5);
                o += indent(depth) + c + "\n";
            }
            return true;
        case OP_BREAK: stmt = "break;"; break;
        case OP_BREAKC:
        {
            const char *cmp = kCmp[(in.ctrl & 7) < 7 ? (in.ctrl & 7) : 0];
            stmt = "if (" + srcX(in.s[0]) + " " + cmp + " " + srcX(in.s[1]) + ") break;";
            break;
        }
        case OP_BREAKP: stmt = "if (p0." + std::string(1, "xyzw"[in.s[0].swz[0]]) + ") break;"; break;
        case OP_CALL:
        {
            char b[32];
            snprintf(b, sizeof(b), "L%d();", in.s[0].r.num);
            stmt = b;
            labels.insert(in.s[0].r.num);
            break;
        }
        case OP_CALLNZ:
        {
            char b[96];
            labels.insert(in.s[0].r.num);
            if (in.ns > 1 && in.s[1].r.type == RT_CONSTBOOL)
            {
                if (defB.count(in.s[1].r.num))
                    snprintf(b, sizeof(b), "if (bd%d) L%d();", in.s[1].r.num, in.s[0].r.num);
                else
                    snprintf(b, sizeof(b), "if (%s[%d].x != 0) L%d();", bArray().c_str(), in.s[1].r.num, in.s[0].r.num);
            }
            else
                snprintf(b, sizeof(b), "if (p0.x) L%d();", in.s[0].r.num);
            stmt = b;
            break;
        }
        case OP_RET: stmt = "return;"; break;
        default:
        {
            char b[64];
            snprintf(b, sizeof(b), "unsupported opcode %d", in.op);
            out.error = b;
            return false;
        }
        }
        if (stmt.empty())
            return true;
        if (in.pred)
            stmt = "if (p0." + std::string(1, "xyzw"[in.predSrc.swz[0]]) + ") { " + stmt + " }";
        o += indent(depth) + stmt + "\n";
        return true;
    }

    // ---------------------------------------------------------------------------------- program
    bool build()
    {
        analyze();
        noteSampleUses();
        std::string g; // declarations
        g += "#version 300 es\n";
        // No default precision for uint: GLSL ES 3.00 rejects "precision highp uint".
        g += "precision highp float;\nprecision highp int;\nprecision highp sampler2D;\n";
        g += "precision highp sampler2DShadow;\n";
        g += "precision highp samplerCube;\nprecision highp sampler3D;\n";

        // uniforms
        char b[256];
        // Fixed layouts let all programs share the same two stage buffers. std140 bool
        // arrays have a 16-byte stride; explicit ivec4 avoids driver-dependent packing.
        g += std::string("layout(std140) uniform ") + (ps ? "PixelConstants" : "VertexConstants")
            + " { vec4 " + cArray() + "[256]; ivec4 " + iArray()
            + "[16]; ivec4 " + bArray() + "[16]; };\n";
        for (const Sampler &s : out.samplers)
        {
            const char *t = s.type == 3 ? "samplerCube" : s.type == 4 ? "sampler3D" : "sampler2D";
            snprintf(b, sizeof(b), "uniform %s %s_s%d;\n", t, ps ? "ps" : "vs", s.reg);
            g += b;
        }
        if (ps)
        {
            g += "uniform vec2 u_at;\nuniform int u_srgbTex;\nuniform int u_srgbEnc;\nuniform int u_shadTex;\nuniform uint u_swzTex;\nuniform int u_srgbW;\n";
        }
        else
        {
            g += "uniform int u_bgra;\n";
        }

        // stage interface
        if (!ps)
        {
            for (const Input &in : out.inputs)
            {
                const int loc = AttribLocation(in.usage, in.index);
                if (loc < 0)
                {
                    snprintf(b, sizeof(b), "unsupported vertex input usage %d index %d", in.usage, in.index);
                    out.error = b;
                    return false;
                }
                snprintf(b, sizeof(b), "layout(location = %d) in vec4 at_%d;\n", loc, loc);
                g += b;
            }
            for (const Input &o : out.outputs)
            {
                snprintf(b, sizeof(b), "out vec4 io_%d_%d;\n", o.usage, o.index);
                g += b;
            }
            g += "/*KISAK_EXTRA_DECL*/\n";
        }
        else
        {
            for (const Input &in : out.inputs)
            {
                snprintf(b, sizeof(b), "in vec4 io_%d_%d;\n", in.usage, in.index);
                g += b;
            }
            if (maxColor < 0)
                maxColor = 0;
            for (int i = 0; i <= maxColor; ++i)
            {
                snprintf(b, sizeof(b), "layout(location = %d) out vec4 oC%d;\n", i, i);
                g += b;
            }
        }

        // registers
        for (int i = 0; i <= maxTemp; ++i)
        {
            snprintf(b, sizeof(b), "vec4 r%d = vec4(0.0);\n", i);
            g += b;
        }
        for (int i = 0; i <= maxIn; ++i)
        {
            snprintf(b, sizeof(b), "vec4 v%d = vec4(0.0);\n", i);
            g += b;
        }
        if (!ps)
        {
            for (int i = 0; i <= maxOut; ++i)
            {
                snprintf(b, sizeof(b), "vec4 o%d = vec4(0.0, 0.0, 0.0, %s);\n", i, i == outPos ? "1.0" : "0.0");
                g += b;
            }
            if (outPos == -2)
                g += "vec4 opos = vec4(0.0, 0.0, 0.0, 1.0);\n";
            if (outPos == -2 || outPos >= 0)
                ; // position copied in the epilogue
        }
        if (usesDepth && ps)
            g += "float oDepth = 0.0;\n";
        if (usesA0)
            g += "ivec4 a0 = ivec4(0);\n";
        if (usesAL)
            g += "int aL = 0;\n";
        if (usesP0)
            g += "bvec4 p0 = bvec4(false);\n";
        if (usesVPos)
            g += "vec4 vPos = vec4(0.0);\n";
        if (usesVFace)
            g += "vec4 vFace = vec4(1.0);\n";
        // constants defined in the shader
        for (auto &kv : defF)
        {
            snprintf(b, sizeof(b), "const vec4 cd%d = vec4(%s, %s, %s, %s);\n", kv.first, F(kv.second.v[0]).c_str(),
                     F(kv.second.v[1]).c_str(), F(kv.second.v[2]).c_str(), F(kv.second.v[3]).c_str());
            g += b;
        }
        for (auto &kv : defI)
        {
            snprintf(b, sizeof(b), "const ivec4 id%d = ivec4(%d, %d, %d, %d);\n", kv.first, kv.second.v[0], kv.second.v[1],
                     kv.second.v[2], kv.second.v[3]);
            g += b;
        }
        for (auto &kv : defB)
        {
            snprintf(b, sizeof(b), "const bool bd%d = %s;\n", kv.first, kv.second ? "true" : "false");
            g += b;
        }

        // The opos register: SM3 declares position as an ordinary output register.
        // helper functions are appended after the body is emitted (flags known then)

        // body
        std::string mainBody;
        std::vector<std::string> funcs;
        std::string cur;
        std::string *dest = &mainBody;
        std::vector<std::string> closers;
        int depth = 0;
        int uid = 0;
        bool inFunc = false;
        for (const Inst &in : insts)
        {
            if (in.op == OP_LABEL)
            {
                const int n = in.s[0].r.num;
                labels.insert(n);
                cur.clear();
                snprintf(b, sizeof(b), "void L%d() {\n", n);
                cur = b;
                dest = &cur;
                inFunc = true;
                depth = 0;
                continue;
            }
            if (in.op == OP_RET && inFunc && depth == 0)
            {
                cur += "}\n";
                funcs.push_back(cur);
                cur.clear();
                dest = &mainBody;
                inFunc = false;
                continue;
            }
            if (in.op == OP_RET && !inFunc && depth == 0)
                continue; // end of the main program
            if (!emitInst(in, *dest, depth, closers, uid))
                return false;
        }
        if (inFunc)
        {
            cur += "}\n";
            funcs.push_back(cur);
        }

        // helpers
        if (helpers & 1)
            g += "vec4 k_lit(vec4 s) { float d = 1.0; vec4 r = vec4(1.0, max(s.x, 0.0), 0.0, 1.0); "
                 "if (s.x > 0.0) r.z = pow(max(s.y, 0.0), clamp(s.w, -128.0, 128.0)); return r; }\n";
        if (helpers & 2)
            g += "vec4 k_nrm(vec4 s) { return s * inversesqrt(dot(s.xyz, s.xyz)); }\n";
        if (helpers & 4)
            g += "vec4 k_srgb(int i, vec4 c) { "
                 "uint s = (u_swzTex >> uint(i * 2)) & 3u; "
                 "if (s == 1u) c = vec4(c.r, c.r, c.r, 1.0); "
                 "else if (s == 2u) c = vec4(0.0, 0.0, 0.0, c.r); "
                 "else if (s == 3u) c = vec4(c.r, c.r, c.r, c.g); "
                 "if (((u_srgbTex >> i) & 1) != 0) { "
                 "vec3 lo = c.rgb / 12.92; vec3 hi = pow((c.rgb + vec3(0.055)) / 1.055, vec3(2.4)); "
                 "c.rgb = mix(lo, hi, vec3(greaterThan(c.rgb, vec3(0.04045)))); } "
                 "else if (((u_srgbEnc >> i) & 1) != 0) { "
                 "vec3 x = clamp(c.rgb, 0.0, 1.0); "
                 "c.rgb = mix(x * 12.92, 1.055 * pow(x, vec3(1.0 / 2.4)) - vec3(0.055), "
                 "vec3(greaterThan(x, vec3(0.0031308)))); } "
                 "return c; }\n";
        if (helpers & 8)
            g += "vec4 k_dual(int i, vec4 t, float zref) { "
                 "if (((u_shadTex >> i) & 1) != 0) t = vec4(step(zref, t.r)); "
                 "return k_srgb(i, t); }\n";
        for (int n : labels)
        {
            snprintf(b, sizeof(b), "void L%d();\n", n);
            g += b;
        }
        for (const std::string &f : funcs)
            g += f;

        // main
        g += "void main() {\n";
        if (!ps)
        {
            for (const Input &in : out.inputs)
            {
                const int loc = AttribLocation(in.usage, in.index);
                snprintf(b, sizeof(b), "  v%d = ((u_bgra & %d) != 0) ? at_%d.zyxw : at_%d;\n", in.reg, 1 << loc, loc, loc);
                g += b;
            }
        }
        else
        {
            for (const Input &in : out.inputs)
            {
                snprintf(b, sizeof(b), "  v%d = io_%d_%d;\n", in.reg, in.usage, in.index);
                g += b;
            }
            if (usesVPos)
                g += "  vPos = vec4(floor(gl_FragCoord.xy), gl_FragCoord.z, 1.0);\n";
            if (usesVFace)
                g += "  vFace = vec4(gl_FrontFacing ? 1.0 : -1.0);\n";
        }
        g += mainBody;

        // epilogue
        if (!ps)
        {
            std::string pos;
            if (outPos >= 0)
            {
                snprintf(b, sizeof(b), "o%d", outPos);
                pos = b;
            }
            else if (outPos == -2)
                pos = "opos";
            else
                pos = "vec4(0.0, 0.0, 0.0, 1.0)";
            g += "  vec4 kp = " + pos + ";\n";
            g += "  gl_Position = vec4(kp.x, -kp.y, 2.0 * kp.z - kp.w, kp.w);\n";
            if (psizeReg >= 0)
            {
                snprintf(b, sizeof(b), "  gl_PointSize = o%d.x;\n", psizeReg);
                g += b;
            }
            else
                g += "  gl_PointSize = 1.0;\n";
            for (const Input &o : out.outputs)
            {
                snprintf(b, sizeof(b), "  io_%d_%d = o%d;\n", o.usage, o.index, o.reg);
                g += b;
            }
            g += "/*KISAK_EXTRA_WRITE*/\n";
        }
        else
        {
            g += "  if (u_at.x != 0.0) {\n"
                 "    int a = int(clamp(oC0.a, 0.0, 1.0) * 255.0 + 0.5);\n"
                 "    int ref = int(u_at.y * 255.0 + 0.5);\n"
                 "    bool pass = true;\n"
                 "    int f = int(u_at.x);\n"
                 "    if (f == 1) pass = false; else if (f == 2) pass = a < ref; else if (f == 3) pass = a == ref;\n"
                 "    else if (f == 4) pass = a <= ref; else if (f == 5) pass = a > ref; else if (f == 6) pass = a != ref;\n"
                 "    else if (f == 7) pass = a >= ref;\n"
                 "    if (!pass) discard;\n"
                 "  }\n";
            g += "  if (u_srgbW != 0) { vec3 c = clamp(oC0.rgb, 0.0, 1.0); "
                 "oC0.rgb = mix(c * 12.92, 1.055 * pow(c, vec3(1.0 / 2.4)) - vec3(0.055), "
                 "vec3(greaterThan(c, vec3(0.0031308)))); }\n";
            if (usesDepth)
                g += "  gl_FragDepth = oDepth;\n";
        }
        g += "}\n";
        out.glsl = std::move(g);
        out.ok = true;
        return true;
    }
};
} // namespace

int AttribLocation(int usage, int index)
{
    switch (usage)
    {
    case USAGE_POSITION: case USAGE_POSITIONT: return index == 0 ? 0 : -1;
    case USAGE_BLENDWEIGHT: return index == 0 ? 1 : -1;
    case USAGE_BLENDINDICES: return index == 0 ? 2 : -1;
    case USAGE_NORMAL: return index == 0 ? 3 : -1;
    case USAGE_PSIZE: return index == 0 ? 4 : -1;
    case USAGE_COLOR: return index == 0 ? 5 : index == 1 ? 6 : -1;
    case USAGE_TANGENT: return index == 0 ? 7 : -1;
    case USAGE_BINORMAL: return index == 0 ? 8 : -1;
    case USAGE_TEXCOORD: return index >= 0 && index < 7 ? 9 + index : -1;
    default: return -1;
    }
}

bool Translate(const uint32_t *tokens, Result &out)
{
    out = Result();
    Gen g(out);
    if (!g.decode(tokens))
        return false;
    if (!g.build())
    {
        out.ok = false;
        return false;
    }
    return true;
}

std::string LinkVertexGlsl(const Result &vs, const Result &ps)
{
    std::string decl, write;
    char b[96];
    for (const Input &in : ps.inputs)
    {
        bool have = false;
        for (const Input &o : vs.outputs)
            have |= (o.usage == in.usage && o.index == in.index);
        if (have)
            continue;
        snprintf(b, sizeof(b), "out vec4 io_%d_%d;\n", in.usage, in.index);
        decl += b;
        snprintf(b, sizeof(b), "  io_%d_%d = vec4(0.0);\n", in.usage, in.index);
        write += b;
    }
    std::string s = vs.glsl;
    auto replace = [&](const char *marker, const std::string &with) {
        const size_t at = s.find(marker);
        if (at != std::string::npos)
            s.replace(at, strlen(marker), with);
    };
    replace("/*KISAK_EXTRA_DECL*/", decl);
    replace("/*KISAK_EXTRA_WRITE*/", write);
    return s;
}

std::string PixelVariant(const Result &ps, uint32_t shadowMask)
{
    if (!shadowMask)
        return ps.glsl;
    std::string s = ps.glsl;
    for (const Sampler &sm : ps.samplers)
    {
        if (sm.type != 2 || !sm.proj || sm.raw || sm.reg < 0 || sm.reg >= 32)
            continue;
        if ((shadowMask & (1u << sm.reg)) == 0)
            continue;
        char from[80], to[80];
        snprintf(from, sizeof(from), "uniform sampler2D ps_s%d;", sm.reg);
        snprintf(to, sizeof(to), "uniform sampler2DShadow ps_s%d;", sm.reg);
        const size_t at = s.find(from);
        if (at != std::string::npos)
            s.replace(at, strlen(from), to);
    }
    return s;
}

} // namespace sm3
