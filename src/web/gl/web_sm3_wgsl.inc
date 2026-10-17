// Gen methods: decoded instructions and register analysis are shared with GLSL.
static void wgReplace(std::string &s, const std::string &a, const std::string &b)
{
    size_t p = 0;
    while ((p = s.find(a, p)) != std::string::npos) { s.replace(p, a.size(), b); p += b.size(); }
}

std::string wgExpr(std::string s)
{
    for (auto p : {std::pair{"ivec4(", "vec4i("}, {"vec4(", "vec4f("}, {"vec3(", "vec3f("},
                   {"vec2(", "vec2f("}, {"inversesqrt(", "inverseSqrt("},
                   {"dFdx(", "dpdx("}, {"dFdy(", "dpdy("}, {"float(", "f32("},
                   {"int(", "i32("}}) wgReplace(s, p.first, p.second);
    for (auto p : {std::pair{"vc[", "uni.fc["}, {"pc[", "uni.fc["}, {"vi[", "uni.ic["},
                   {"pi[", "uni.ic["}, {"vb[", "uni.bc["}, {"pb[", "uni.bc["}})
        wgReplace(s, p.first, p.second);
    // Boolean constants are replicated as numeric registers in SM3.
    for (const auto &kv : defB)
        wgReplace(s, "bd" + std::to_string(kv.first) + " ? 1.0 : 0.0",
                  "select(0.0, 1.0, bd" + std::to_string(kv.first) + ")");
    for (int n : usedBool)
        wgReplace(s, "uni.bc[" + std::to_string(n) + "].x != 0 ? 1.0 : 0.0",
                  "select(0.0, 1.0, uni.bc[" + std::to_string(n) + "].x != 0)");
    wgReplace(s, "vec4f(p0)", "select(vec4f(0.0), vec4f(1.0), p0)");
    return s;
}

std::string wgslSample(const Inst &in)
{
    if (in.ns < 2) { out.error = "texture instruction missing sampler"; return "vec4f(0.0)"; }
    const int r = in.s[1].r.num, type = samplerType(r);
    const std::string sn = "s" + std::to_string(r), c = "(" + src(in.s[0]) + ")";
    // texture_depth_* accepts comparison sampling and textureLoad only.
    // A raw read of the same register cannot use textureSampleLevel.
    const bool depth = (wgslDepthMask & (1u << r)) != 0;
    const std::string dim = type == 2 ? ".xy" : ".xyz";
    std::string coord = c + dim;
    if (in.op == OP_TEXLD && (in.ctrl & 1)) coord = "(" + coord + " / " + c + ".w)";
    std::string t;
    if (depth && (in.ctrl & 1))
        t = "vec4f(textureSampleCompareLevel(" + sn + "t, " + sn + "s, " + coord + ", " + c + ".z / " + c + ".w))";
    else if (depth && type == 2)
        t = "vec4f(textureLoad(" + sn + "t, vec2i(" + coord + " * vec2f(textureDimensions(" + sn + "t))), 0))";
    else if (in.op == OP_TEXLDL || !ps)
        t = "textureSampleLevel(" + sn + "t, " + sn + "s, " + coord + ", " + (in.op == OP_TEXLDL ? c + ".w" : "0.0") + ")";
    else if (in.op == OP_TEXLDD)
        t = "textureSampleGrad(" + sn + "t, " + sn + "s, " + coord + ", " + src(in.s[2]) + dim + ", " + src(in.s[3]) + dim + ")";
    else if (in.ctrl & 2)
        t = "textureSampleBias(" + sn + "t, " + sn + "s, " + coord + ", " + c + ".w)";
    else t = "textureSample(" + sn + "t, " + sn + "s, " + coord + ")";
    return ps ? "k_srgb(" + std::to_string(r) + "u, " + t + ")" : t;
}

bool buildWgsl(const Result &pixel, std::string &g, const int *vertexTypes)
{
    analyze(); noteSampleUses();
    g = "diagnostic(off, derivative_uniformity);\n";
    g += R"WGSL(
struct Constants {
    fc: array<vec4f, 256>, ic: array<vec4i, 16>, bc: array<vec4i, 16>,
    at: vec2f, srgbTex: u32, srgbEnc: u32,
    swzTex: u32, srgbW: u32, bgra: u32, pad: u32,
}
)WGSL";
    wgReplace(g, "fc: array<vec4f, 256>", "fc: array<vec4f, " + std::to_string(std::max(1, out.floatCount)) + ">");
    const unsigned stage = ps ? 1 : 0;
    g += "@group(0) @binding(" + std::to_string(stage) + ") var<uniform> uni: Constants;\n";
    for (auto &s : out.samplers)
    {
        const std::string n = "s" + std::to_string(s.reg);
        const bool dep = (wgslDepthMask & (1u << s.reg)) != 0;
        const std::string dim = s.type == 3 ? "cube" : s.type == 4 ? "3d" : "2d";
        g += "@group(" + std::to_string(stage + 1) + ") @binding(" + std::to_string(s.reg * 3) + ") var " + n + "t: texture_" + (dep ? "depth_" + dim : dim + "<f32>") + ";\n";
        g += "@group(" + std::to_string(stage + 1) + ") @binding(" + std::to_string(s.reg * 3 + 1) + ") var " + n + "s: " + (dep ? "sampler_comparison" : "sampler") + ";\n";
    }
    auto var = [&](const std::string &n, const std::string &t, const std::string &v) { g += "var<private> " + n + ": " + t + " = " + v + ";\n"; };
    for (int i = 0; i <= maxTemp; ++i) var("r" + std::to_string(i), "vec4f", "vec4f(0.0)");
    for (int i = 0; i <= maxIn; ++i) var("v" + std::to_string(i), "vec4f", "vec4f(0.0)");
    for (int i = 0; i <= maxOut; ++i) var("o" + std::to_string(i), "vec4f", i == outPos ? "vec4f(0.0, 0.0, 0.0, 1.0)" : "vec4f(0.0)");
    if (outPos == -2) var("opos", "vec4f", "vec4f(0.0, 0.0, 0.0, 1.0)");
    if (ps) for (int i = 0; i <= std::max(0, maxColor); ++i) var("oC" + std::to_string(i), "vec4f", "vec4f(0.0)");
    if (usesDepth) var("oDepth", "f32", "0.0");
    if (usesA0) var("a0", "vec4i", "vec4i(0)");
    if (usesAL) var("aL", "i32", "0");
    if (usesP0) var("p0", "vec4<bool>", "vec4<bool>(false)");
    if (usesVPos) var("vPos", "vec4f", "vec4f(0.0)");
    if (usesVFace) var("vFace", "vec4f", "vec4f(1.0)");
    var("rdummy", "vec4f", "vec4f(0.0)");
    for (auto &kv : defF) { g += "const cd" + std::to_string(kv.first) + " = vec4f("; for (int i = 0; i < 4; ++i) g += (i ? ", " : "") + F(kv.second.v[i]); g += ");\n"; }
    for (auto &kv : defI) { g += "const id" + std::to_string(kv.first) + " = vec4i("; for (int i = 0; i < 4; ++i) g += (i ? ", " : "") + std::to_string(kv.second.v[i]); g += ");\n"; }
    for (auto &kv : defB) g += "const bd" + std::to_string(kv.first) + " = " + (kv.second ? "true" : "false") + ";\n";
    g += R"WGSL(
fn k_lit(s: vec4f) -> vec4f {
    var r = vec4f(1.0, max(s.x, 0.0), 0.0, 1.0);
    if (s.x > 0.0) { r.z = pow(max(s.y, 0.0), clamp(s.w, -128.0, 128.0)); }
    return r;
}
fn k_nrm(s: vec4f) -> vec4f { return s * inverseSqrt(dot(s.xyz, s.xyz)); }
fn k_srgb(i: u32, texel: vec4f) -> vec4f {
    var c = texel; let swz = (uni.swzTex >> (i * 2u)) & 3u;
    if (swz == 1u) { c = vec4f(c.r, c.r, c.r, 1.0); }
    if (swz == 2u) { c = vec4f(0.0, 0.0, 0.0, c.r); }
    if (swz == 3u) { c = vec4f(c.r, c.r, c.r, c.g); }
    if (((uni.srgbTex >> i) & 1u) != 0u) {
        let lo = c.rgb / 12.92; let hi = pow((c.rgb + vec3f(0.055)) / 1.055, vec3f(2.4));
        c = vec4f(select(lo, hi, c.rgb > vec3f(0.04045)), c.a);
    } else if (((uni.srgbEnc >> i) & 1u) != 0u) {
        let x = clamp(c.rgb, vec3f(0.0), vec3f(1.0));
        c = vec4f(select(x * 12.92, 1.055 * pow(x, vec3f(1.0 / 2.4)) - vec3f(0.055), x > vec3f(0.0031308)), c.a);
    }
    return c;
}
)WGSL";
    std::string body, functions, cur;
    std::string *dest = &body;
    int depth = 0, uid = 0;
    std::vector<std::string> closers;
    bool inFunc = false;
    for (auto &in : insts)
    {
        if (in.op == OP_LABEL) { cur = "fn L" + std::to_string(in.s[0].r.num) + "() {\n"; dest = &cur; inFunc = true; depth = 0; continue; }
        if (in.op == OP_RET && depth == 0) { if (inFunc) { functions += cur + "}\n"; dest = &body; inFunc = false; } continue; }
        if (in.op == OP_MOVA)
        {
            *dest += "{ let address = vec4i(round(" + wgExpr(src(in.s[0])) + ")); ";
            for (char c : MaskStr(in.d.mask)) *dest += "a0." + std::string(1, c) + " = address." + c + "; ";
            *dest += "}\n"; continue;
        }
        if (in.op == OP_LOOP || in.op == OP_REP)
        {
            ++uid; const int n = in.s[in.op == OP_LOOP ? 1 : 0].r.num;
            const std::string iv = defI.count(n) ? "id" + std::to_string(n) : "uni.ic[" + std::to_string(n) + "]";
            const std::string suffix = std::to_string(uid);
            if (in.op == OP_LOOP) *dest += "{ let saved" + suffix + " = aL; let lv" + suffix + " = " + iv + "; aL = lv" + suffix + ".y;\n";
            *dest += "for (var lc" + suffix + " = 0; lc" + suffix + " < " + iv + ".x; lc" + suffix + "++) {\n";
            closers.push_back(in.op == OP_LOOP ? "aL += lv" + suffix + ".z; } aL = saved" + suffix + "; }" : "}");
            ++depth; continue;
        }
        std::string stmt;
        if (!emitInst(in, stmt, depth, closers, uid)) return false;
        if (in.op == OP_TEXKILL) stmt = "if (any(" + dstName(in.d) + ".xyz < vec3f(0.0))) { discard; }\n";
        // WGSL requires blocks for all conditionals, even a single statement.
        if (in.op == OP_BREAKC || in.op == OP_BREAKP) wgReplace(stmt, ") break;", ") { break; }");
        if (in.op == OP_CALLNZ) { const size_t p = stmt.find(") L"); if (p != std::string::npos) { stmt.insert(p + 2, "{ "); stmt += " }"; } }
        *dest += wgExpr(stmt);
    }
    if (inFunc) functions += cur + "}\n";
    g += functions;
    g += "fn executeProgram() {\n" + body + "}\n";
    std::map<int, Input> varyings;
    for (auto &i : pixel.inputs) varyings[i.usage * 16 + i.index] = i;
    auto varyingLocation = [&](int usage, int index) { return std::distance(varyings.begin(), varyings.find(usage * 16 + index)); };
    g += "struct StageOut {\n";
    if (!ps) g += "@builtin(position) position: vec4f,\n";
    if (!ps) for (auto &kv : varyings) g += "@location(" + std::to_string(varyingLocation(kv.second.usage, kv.second.index)) + ") io" + std::to_string(kv.first) + ": vec4f,\n";
    if (ps) for (int i = 0; i <= std::max(0, maxColor); ++i) g += "@location(" + std::to_string(i) + ") color" + std::to_string(i) + ": vec4f,\n";
    if (ps && usesDepth) g += "@builtin(frag_depth) depth: f32,\n";
    g += "}\n";
    g += ps ? "@fragment fn main(" : "@vertex fn main(";
    for (auto &i : out.inputs)
    {
        const int loc = ps ? int(varyingLocation(i.usage, i.index)) : AttribLocation(i.usage, i.index);
        if (loc < 0) { out.error = "unsupported vertex semantic"; return false; }
        const int type = !ps && vertexTypes ? vertexTypes[loc] : 4;
        if (!type) continue;
        const int components = type % 100;
        const std::string scalar = type >= 200 ? "i32" : type >= 100 ? "u32" : "f32";
        const std::string name = components == 1 ? scalar : "vec" + std::to_string(components) + "<" + scalar + ">";
        g += "@location(" + std::to_string(loc) + ") input" + std::to_string(i.reg) + ": " + name + ", ";
    }
    if (ps) g += "@builtin(position) fragCoord: vec4f, @builtin(front_facing) front: bool";
    g += ") -> StageOut {\nvar result: StageOut;\n";
    // Private globals persist conceptually across invocations: explicitly reset
    // register state so calls and early returns have the same SM3 defaults.
    for (int i = 0; i <= maxTemp; ++i) g += "r" + std::to_string(i) + " = vec4f(0.0);\n";
    for (auto &i : out.inputs) {
        const int loc = AttribLocation(i.usage, i.index);
        const int type = !ps && vertexTypes ? vertexTypes[loc] : 4;
        std::string expr = type ? "input" + std::to_string(i.reg) : "vec4f(0.0, 0.0, 0.0, 1.0)";
        if (!ps && type) {
            const int n = type % 100;
            if (type >= 100) expr = n == 1 ? "f32(" + expr + ")" : "vec" + std::to_string(n) + "f(" + expr + ")";
            if (n < 4) expr = "vec4f(" + expr + (n == 1 ? ", 0.0, 0.0, 1.0)" : n == 2 ? ", 0.0, 1.0)" : ", 1.0)");
            expr = "select(" + expr + ", (" + expr + ").zyxw, (uni.bgra & " + std::to_string(1u << loc) + "u) != 0u)";
        }
        g += "v" + std::to_string(i.reg) + " = " + expr + ";\n";
    }
    if (usesVPos) g += "vPos = vec4f(floor(fragCoord.xy), fragCoord.z, 1.0);\n";
    if (usesVFace) g += "vFace = vec4f(select(-1.0, 1.0, front));\n";
    g += "executeProgram();\n";
    if (!ps)
    {
        const std::string pos = outPos >= 0 ? "o" + std::to_string(outPos) : outPos == -2 ? "opos" : "vec4f(0.0, 0.0, 0.0, 1.0)";
        // WebGPU uses D3D's [0,w] clip depth and top-down attachment rows.
        g += "result.position = " + pos + ";\n";
        for (auto &kv : varyings)
        {
            std::string expr = "vec4f(0.0)";
            for (auto &o : out.outputs) if (o.usage == kv.second.usage && o.index == kv.second.index) expr = "o" + std::to_string(o.reg);
            g += "result.io" + std::to_string(kv.first) + " = " + expr + ";\n";
        }
    }
    else
    {
        g += R"WGSL(
if (uni.at.x != 0.0) {
    let alpha = i32(clamp(oC0.a, 0.0, 1.0) * 255.0 + 0.5);
    let reference = i32(uni.at.y * 255.0 + 0.5); let f = i32(uni.at.x);
    var passed = true;
    switch f {
        case 1: { passed = false; } case 2: { passed = alpha < reference; }
        case 3: { passed = alpha == reference; } case 4: { passed = alpha <= reference; }
        case 5: { passed = alpha > reference; } case 6: { passed = alpha != reference; }
        case 7: { passed = alpha >= reference; } default: {}
    }
    if (!passed) { discard; }
}
if (uni.srgbW != 0u) {
    let c = clamp(oC0.rgb, vec3f(0.0), vec3f(1.0));
    oC0 = vec4f(select(c * 12.92, 1.055 * pow(c, vec3f(1.0 / 2.4)) - vec3f(0.055), c > vec3f(0.0031308)), oC0.a);
}
)WGSL";
        for (int i = 0; i <= std::max(0, maxColor); ++i) g += "result.color" + std::to_string(i) + " = oC" + std::to_string(i) + ";\n";
        if (usesDepth) g += "result.depth = oDepth;\n";
    }
    g += "return result;\n}\n";
    return out.error.empty();
}
