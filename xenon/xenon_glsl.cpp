#include "xenon_glsl.h"

#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>

#include "gfx_cc.h"

namespace xn {

static const char *const GENERATOR_REVISION = "xenon_glsl 1";

const char *const CLEAR_VERTEX = R"glsl(#version 450
layout(location = 0) in vec4 aPos;
void main() {
    gl_Position = aPos;
}
)glsl";

const char *const CLEAR_FRAGMENT = R"glsl(#version 450
layout(location = 0) out vec4 vOutColor;
void main() {
    vOutColor = vec4(0.0, 0.0, 0.0, 1.0);
}
)glsl";

std::string Templates()
{
    return std::string(GENERATOR_REVISION) + CLEAR_VERTEX + CLEAR_FRAGMENT;
}

static void addf(std::string &s, const char *fmt, ...)
{
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    s += buf;
}

/* O ruido do N64: o mesmo do Midas, com a funcao virada macro (o xost nao aceita chamadas). */
#define RAND_NOISE "((RANDOM(vec3(floor(fragCoord * frame.w), frame.z)) + 1.0) / 2.0)"

static const char *item_to_str(uint32_t item, bool with_alpha, bool only_alpha, bool inputs_have_alpha,
                               bool hint_single_element)
{
    if (!only_alpha) {
        switch (item) {
        case SHADER_0: return with_alpha ? "vec4(0.0, 0.0, 0.0, 0.0)" : "vec3(0.0, 0.0, 0.0)";
        case SHADER_1: return with_alpha ? "vec4(1.0, 1.0, 1.0, 1.0)" : "vec3(1.0, 1.0, 1.0)";
        case SHADER_INPUT_1: return with_alpha || !inputs_have_alpha ? "vInput1" : "vInput1.rgb";
        case SHADER_INPUT_2: return with_alpha || !inputs_have_alpha ? "vInput2" : "vInput2.rgb";
        case SHADER_INPUT_3: return with_alpha || !inputs_have_alpha ? "vInput3" : "vInput3.rgb";
        case SHADER_INPUT_4: return with_alpha || !inputs_have_alpha ? "vInput4" : "vInput4.rgb";
        case SHADER_TEXEL0: return with_alpha ? "texVal0" : "texVal0.rgb";
        case SHADER_TEXEL0A:
            return hint_single_element ? "texVal0.a"
                                       : (with_alpha ? "vec4(texVal0.a, texVal0.a, texVal0.a, texVal0.a)"
                                                     : "vec3(texVal0.a, texVal0.a, texVal0.a)");
        case SHADER_TEXEL1A:
            return hint_single_element ? "texVal1.a"
                                       : (with_alpha ? "vec4(texVal1.a, texVal1.a, texVal1.a, texVal1.a)"
                                                     : "vec3(texVal1.a, texVal1.a, texVal1.a)");
        case SHADER_TEXEL1: return with_alpha ? "texVal1" : "texVal1.rgb";
        case SHADER_COMBINED: return with_alpha ? "texel" : "texel.rgb";
        case SHADER_NOISE:
            return with_alpha ? "vec4(" RAND_NOISE ", " RAND_NOISE ", " RAND_NOISE ", " RAND_NOISE ")"
                              : "vec3(" RAND_NOISE ", " RAND_NOISE ", " RAND_NOISE ")";
        }
    } else {
        switch (item) {
        case SHADER_0: return "0.0";
        case SHADER_1: return "1.0";
        case SHADER_INPUT_1: return "vInput1.a";
        case SHADER_INPUT_2: return "vInput2.a";
        case SHADER_INPUT_3: return "vInput3.a";
        case SHADER_INPUT_4: return "vInput4.a";
        case SHADER_TEXEL0: return "texVal0.a";
        case SHADER_TEXEL0A: return "texVal0.a";
        case SHADER_TEXEL1A: return "texVal1.a";
        case SHADER_TEXEL1: return "texVal1.a";
        case SHADER_COMBINED: return "texel.a";
        case SHADER_NOISE: return RAND_NOISE;
        }
    }
    return "";
}

static void append_formula(std::string &o, const uint8_t c[2][4], bool do_single, bool do_multiply, bool do_mix,
                           bool with_alpha, bool only_alpha, bool opt_alpha)
{
    if (do_single) {
        o += item_to_str(c[only_alpha][3], with_alpha, only_alpha, opt_alpha, false);
    } else if (do_multiply) {
        o += item_to_str(c[only_alpha][0], with_alpha, only_alpha, opt_alpha, false);
        o += " * ";
        o += item_to_str(c[only_alpha][2], with_alpha, only_alpha, opt_alpha, true);
    } else if (do_mix) {
        o += "mix(";
        o += item_to_str(c[only_alpha][1], with_alpha, only_alpha, opt_alpha, false);
        o += ", ";
        o += item_to_str(c[only_alpha][0], with_alpha, only_alpha, opt_alpha, false);
        o += ", ";
        o += item_to_str(c[only_alpha][2], with_alpha, only_alpha, opt_alpha, true);
        o += ")";
    } else {
        o += "(";
        o += item_to_str(c[only_alpha][0], with_alpha, only_alpha, opt_alpha, false);
        o += " - ";
        o += item_to_str(c[only_alpha][1], with_alpha, only_alpha, opt_alpha, false);
        o += ") * ";
        o += item_to_str(c[only_alpha][2], with_alpha, only_alpha, opt_alpha, true);
        o += " + ";
        o += item_to_str(c[only_alpha][3], with_alpha, only_alpha, opt_alpha, false);
    }
}

Glsl BuildGlsl(const CCFeatures &cc, bool threePoint)
{
    Glsl g;
    uint32_t loc = 0;     /* proximo atributo / interpolador */
    uint32_t floats = 4;  /* depois da posicao */
    std::string vdecl, vbody, fdecl;

    auto add_attr = [&](const char *name, const char *vname, uint32_t n, const char *fname_unused) {
        (void) fname_unused;
        const char *type = n == 1 ? "float" : n == 2 ? "vec2" : n == 3 ? "vec3" : "vec4";
        addf(vdecl, "layout(location = %u) in %s %s;\n", loc + 2, type, name);
        addf(vdecl, "layout(location = %u) out %s %s;\n", loc, type, vname);
        addf(fdecl, "layout(location = %u) in %s %s;\n", loc, type, vname);
        addf(vbody, "    %s = %s;\n", vname, name);
        g.attributes.push_back({ floats, n });
        floats += n;
        loc++;
    };

    uint32_t texn[2] = { 0, 0 };
    char an[32], vn[32];
    for (int i = 0; i < 2; i++) {
        if (!cc.used_textures[i]) continue;
        texn[i] = 2 + (cc.clamp[i][0] ? 1 : 0) + (cc.clamp[i][1] ? 1 : 0);
        snprintf(an, sizeof an, "aTex%d", i);
        snprintf(vn, sizeof vn, "vTex%d", i);
        add_attr(an, vn, texn[i], nullptr);
    }
    if (cc.opt_fog) add_attr("aFog", "vFog", 4, nullptr);
    if (cc.opt_ge_pixfog) add_attr("aGeFogP", "vGeFogP", 2, nullptr);
    if (cc.opt_ge_sky_pixdiv) add_attr("aGeSkyQ", "vGeSkyQ", 1, nullptr);
    if (cc.opt_grayscale) add_attr("aGrayscaleColor", "vGrayscaleColor", 4, nullptr);
    for (int i = 0; i < cc.num_inputs; i++) {
        snprintf(an, sizeof an, "aInput%d", i + 1);
        snprintf(vn, sizeof vn, "vInput%d", i + 1);
        add_attr(an, vn, cc.opt_alpha ? 4 : 3, nullptr);
    }
    g.vertexFloats = floats;

    /* ---- vertex ---- */
    std::string &vs = g.vertex;
    vs += "#version 450\n";
    vs += "layout(location = 0) in vec4 aVtxPos;\n";
    vs += vdecl;
    if (cc.opt_ge_farclip || cc.opt_ge_pixfog) {
        addf(vs, "layout(location = %u) out vec2 vGeZw;\n", loc);
    }
    vs += "void main() {\n";
    vs += vbody;
    if (cc.opt_ge_farclip || cc.opt_ge_pixfog) vs += "    vGeZw = aVtxPos.zw;\n";
    /* O GL_DEPTH_CLAMP do Midas, um vertice por vez: o Xenos nao tem recorte de profundidade separado. */
    vs += "    vec4 position = aVtxPos;\n";
    vs += "    position.z = aVtxPos.w > 0.0 ? min(max(aVtxPos.z, 0.0), aVtxPos.w) : aVtxPos.z;\n";
    vs += "    gl_Position = position;\n";
    vs += "}\n";

    /* ---- fragment ---- */
    std::string &fs = g.fragment;
    fs += "#version 450\n";
    fs += fdecl;
    if (cc.opt_ge_farclip || cc.opt_ge_pixfog) addf(fs, "layout(location = %u) in vec2 vGeZw;\n", loc);
    fs += "layout(location = 0) out vec4 vOutColor;\n";
    fs += "layout(set = 0, binding = 0, std140) uniform Uniforms {\n"
          "    vec4 texture_size[2];\n"
          "    vec4 frame;\n"
          "};\n";
    if (cc.used_textures[0]) fs += "layout(set = 0, binding = 1) uniform sampler2D uTex0;\n";
    if (cc.used_textures[1]) fs += "layout(set = 0, binding = 2) uniform sampler2D uTex1;\n";

    fs += "#define WRAP(x, low, high) mod((x) - (low), (high) - (low)) + (low)\n";
    /* o sin do xost so e bom de -pi a pi */
    fs += "#define SIN(x) sin((x) - 6.2831853 * floor((x) * 0.15915494 + 0.5))\n";
    fs += "#define RANDOM(value) fract(SIN(dot(SIN(value), vec3(12.9898, 78.233, 37.719))) * 143758.5453)\n";
    if (threePoint) {
        fs += "#define FILTER_3POINT(result, tex, uv, size) { \\\n"
              "    vec2 fraction = fract((uv) * (size) - vec2(0.5)); \\\n"
              "    vec2 o = fraction - step(1.0, fraction.x + fraction.y); \\\n"
              "    vec4 c0 = texture(tex, (uv) - o / (size)); \\\n"
              "    vec4 c1 = texture(tex, (uv) - vec2(o.x - sign(o.x), o.y) / (size)); \\\n"
              "    vec4 c2 = texture(tex, (uv) - vec2(o.x, o.y - sign(o.y)) / (size)); \\\n"
              "    result = c0 + abs(o.x) * (c1 - c0) + abs(o.y) * (c2 - c0); \\\n"
              "}\n";
        fs += "#define SAMPLE(result, id, tex, uv, size) if (frame[id] > 0.5) FILTER_3POINT(result, tex, uv, size) "
              "else { result = texture(tex, uv); }\n";
    } else {
        fs += "#define SAMPLE(result, id, tex, uv, size) result = texture(tex, uv);\n";
    }

    std::string body;
    body += "void main() {\n";

    if (cc.opt_ge_farclip) body += "    if (vGeZw.x > vGeZw.y) discard;\n";

    for (int i = 0; i < 2; i++) {
        if (!cc.used_textures[i]) continue;
        const bool s = cc.clamp[i][0], t = cc.clamp[i][1];
        addf(body, "    vec2 texSize%d = texture_size[%d].xy;\n", i, i);
        if (cc.opt_ge_sky_pixdiv)
            addf(body, "    vec2 base%d = vTex%d.xy / max(vGeSkyQ, 1e-7);\n", i, i);
        else
            addf(body, "    vec2 base%d = vTex%d.xy;\n", i, i);
        if (!s && !t) {
            addf(body, "    vec2 uv%d = base%d;\n", i, i);
        } else if (s && t) {
            addf(body, "    vec2 uv%d = min(max(base%d, 0.5 / texSize%d), vTex%d.zw);\n", i, i, i, i);
        } else if (s) {
            addf(body, "    vec2 uv%d = vec2(min(max(base%d.x, 0.5 / texSize%d.x), vTex%d.z), base%d.y);\n", i, i, i, i, i);
        } else {
            addf(body, "    vec2 uv%d = vec2(base%d.x, min(max(base%d.y, 0.5 / texSize%d.y), vTex%d.z));\n", i, i, i, i, i);
        }
        if (cc.opt_blur) {
            /* filtro de borrao dos fundos de menu: o laco de 16 amostras, desenrolado */
            addf(body, "    vec4 blurSum%d = vec4(0.0, 0.0, 0.0, 0.0);\n", i);
            for (int k = 0; k < 16; k++) {
                const float x = (float) (k & 3), y = (float) (k >> 2);
                const float w = 0.009947f - sqrtf(x * x + y * y) * 0.001f;
                addf(body, "    vec4 blurTap%d_%d;\n", i, k);
                addf(body, "    SAMPLE(blurTap%d_%d, %d, uTex%d, uv%d + vec2(%.1f, %.1f) / texSize%d, texSize%d)\n", i, k, i, i,
                     i, -1.5f + x, -1.5f + y, i, i);
                addf(body, "    blurSum%d += vec4(blurTap%d_%d.rgb * %.6f, %.6f);\n", i, i, k, w, w);
            }
            addf(body, "    vec4 texVal%d = vec4(blurSum%d.rgb / blurSum%d.a, 1.0);\n", i, i, i);
        } else {
            addf(body, "    vec4 texVal%d;\n", i);
            addf(body, "    SAMPLE(texVal%d, %d, uTex%d, uv%d, texSize%d)\n", i, i, i, i, i);
        }
    }

    body += cc.opt_alpha ? "    vec4 texel;\n" : "    vec3 texel;\n";
    for (int c = 0; c < (cc.opt_2cyc ? 2 : 1); c++) {
        body += "    texel = ";
        if (!cc.color_alpha_same[c] && cc.opt_alpha) {
            body += "vec4(";
            append_formula(body, cc.c[c], cc.do_single[c][0], cc.do_multiply[c][0], cc.do_mix[c][0], false, false, true);
            body += ", ";
            append_formula(body, cc.c[c], cc.do_single[c][1], cc.do_multiply[c][1], cc.do_mix[c][1], true, true, true);
            body += ")";
        } else {
            append_formula(body, cc.c[c], cc.do_single[c][0], cc.do_multiply[c][0], cc.do_mix[c][0], cc.opt_alpha, false,
                           cc.opt_alpha);
        }
        body += ";\n";
        if (c == 0) body += "    texel = WRAP(texel, -1.01, 1.01);\n";
    }
    body += "    texel = WRAP(texel, -0.51, 1.51);\n";
    body += "    texel = clamp(texel, 0.0, 1.0);\n";

    if (cc.opt_fog) {
        if (cc.opt_ge_pixfog) {
            /* z chega em 0..1 (z_is_from_0_to_1); a curva do fog do GE espera -1..1: z' = (2z - w) / w */
            body += "    float geFogA = clamp(((vGeZw.x * 2.0 - vGeZw.y) / vGeZw.y) * vGeFogP.x + vGeFogP.y, 0.0, 255.0) / 255.0;\n";
        } else {
            body += "    float geFogA = vFog.a;\n";
        }
        if (cc.opt_alpha)
            body += "    texel = vec4(mix(texel.rgb, vFog.rgb, geFogA), texel.a);\n";
        else
            body += "    texel = mix(texel, vFog.rgb, geFogA);\n";
    }
    if (cc.opt_texture_edge && cc.opt_alpha) {
        body += "    if (!(texel.a > 0.19)) discard;\n    texel.a = 1.0;\n";
    }
    if (cc.opt_alpha && cc.opt_noise) {
        body += "    texel.a *= floor(clamp(RANDOM(vec3(floor(fragCoord * frame.w), frame.z)) + texel.a, 0.0, 1.0));\n";
    }
    if (cc.opt_grayscale) {
        body += "    float intensity = (texel.r + texel.g + texel.b) / 3.0;\n";
        body += "    vec3 new_texel = vGrayscaleColor.rgb * intensity;\n";
        body += "    texel.rgb = mix(texel.rgb, new_texel, vGrayscaleColor.a);\n";
    }
    if (cc.opt_alpha) {
        if (cc.opt_alpha_threshold) body += "    if (texel.a < 8.0 / 256.0) discard;\n";
        if (cc.opt_invisible) body += "    texel.a = 0.0;\n";
        if (cc.opt_alpha_cvg_sel) {
            if (cc.opt_noise) body += "    if (texel.a <= 0.0) discard;\n";
            body += "    texel.a = 1.0;\n";
        }
        if (cc.opt_sky_rgb5) body += "    texel.rgb = floor(texel.rgb * 31.0) / 31.0;\n";
        body += "    vOutColor = texel;\n";
    } else {
        if (cc.opt_sky_rgb5) body += "    texel = floor(texel * 31.0) / 31.0;\n";
        body += "    vOutColor = vec4(texel, 1.0);\n";
    }
    body += "}\n";

    fs += "";
    fs += "void main() {\n";
    if (body.find("fragCoord") != std::string::npos) fs += "    vec2 fragCoord = gl_FragCoord.xy;\n";
    fs += body.substr(strlen("void main() {\n"));
    return g;
}

} // namespace xn
