/* Camada de video do Xbox 360 para o Project Midas, sobre o xsdk (GPU Xenos).
 *
 * O Midas desenha pelo Fast3D (port/fast3d/gfx_pc.cpp), que fala com dois objetos: um "gerenciador de janela"
 * (gfx_sdl) e uma "API de renderizacao" (gfx_opengl_api). No PC sao SDL2 e OpenGL; aqui sao estes, escritos
 * a partir do backend gfx_xsdk do libultraship-xenon (xenosworks), com o gerador de shaders do Midas
 * (xenon_glsl.cpp). Tudo desenha direto na tela (1280x720, 1 amostra): os framebuffers extras ficam desligados
 * (gfx_framebuffers_enabled = false). */
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include <PR/gbi.h>
#include "gfx_api.h"
#include "gfx_cc.h"
#include "gfx_sdl.h"
#include "gfx_opengl.h"

#include "xsdk/display.h"
#include "xsdk/gpu.h"
#include "xsdk/log.h"
#include "xsdk/present.h"
#include "xsdk/shader.h"
#include "xsdk/splash.h"
#include "xsdk/texture.h"

#include "xenon_glsl.h"
#include "xenon_shader.h"

/* ---- Constantes --------------------------------------------------------------------------------- */

namespace {

constexpr uint32_t WIN_W = 1280, WIN_H = 720;
constexpr uint32_t BUFFERS = 2;
constexpr uint32_t SWAP_INTERVAL = 1;
constexpr uint32_t FENCE_TIMEOUT_MS = 5000;
constexpr uint32_t RING_SEGMENTS = 8;
constexpr uint32_t SEGMENT_BYTES = 512 * 1024;
constexpr uint32_t RING_BYTES = RING_SEGMENTS * SEGMENT_BYTES;
constexpr uint32_t DRAW_BYTES_ALIGN = 4096;
constexpr uint32_t PAGE_BYTES = 4096;
constexpr uint32_t BLOCK_PAGES = 256;
constexpr uint32_t BLOCK_REGISTERS = xn::BLOCK_FLOATS / 4;
constexpr uint32_t CLEAR_ARGB = 0xFF000000;
constexpr float DECAL_BIAS_UNITS = -8.0f / 16777216.0f;
constexpr float DECAL_BIAS_SLOPE = -4.0f;

enum : uint32_t {
    DIRTY_DEPTH = 1 << 0,
    DIRTY_BLEND = 1 << 1,
    DIRTY_RASTER = 1 << 2,
    DIRTY_VIEWPORT = 1 << 3,
    DIRTY_ALL = DIRTY_DEPTH | DIRTY_BLEND | DIRTY_RASTER | DIRTY_VIEWPORT,
};

struct Memory {
    volatile uint32_t *texels;
    int32_t block;
    uint32_t firstPage;
    uint32_t pages;
};

struct Texture {
    XsdkTexture texture;
    XsdkSamplerState sampler;
    Memory memory;
    uint32_t generation;
    bool threePoint;
    float size[2];
};

struct PageBlock {
    volatile uint32_t *memory;
    bool used[BLOCK_PAGES];
};

struct PendingFree {
    Memory memory;
    uint32_t fence;
};

struct FetchBinding {
    const Texture *texture;
    uint32_t generation;
};

struct ConstantRun {
    uint32_t first;
    uint32_t count;
    std::vector<float> values;
};

} // namespace

struct ShaderProgram {
    uint64_t id0;
    uint32_t id1;
    uint8_t num_inputs;
    bool used_textures[2];
    bool drawable;
    uint32_t vertexFloats;
    int8_t fetchSlots[2];
    int32_t block;
    XsdkShader vertex;
    XsdkShader pixel;
    std::vector<uint32_t> vertexContainer;
    std::vector<uint32_t> pixelContainer;
    std::vector<ConstantRun> vertexLiterals;
    std::vector<ConstantRun> pixelLiterals;
};

namespace {

/* ---- Estado --------------------------------------------------------------------------------------- */

std::map<std::pair<uint64_t, uint32_t>, ShaderProgram> g_shaders;
ShaderProgram g_clear;
ShaderProgram *g_shader = nullptr;
ShaderProgram *g_applied = nullptr;

std::vector<Texture> g_textures;
std::vector<PageBlock> g_blocks;
std::vector<PendingFree> g_pending;
uint32_t g_cur_tex[2] = { 0, 0 };
int g_cur_tile = 0;
FetchBinding g_bound[32];

volatile uint32_t *g_ring = nullptr;
uint32_t g_ring_addr = 0;
uint32_t g_ring_segment = 0;
uint32_t g_ring_offset = 0;
uint32_t g_ring_fences[RING_SEGMENTS];

XsdkDepthStencilState g_depth_states[7];
XsdkBlendState g_blend_opaque, g_blend_alpha, g_blend_modulate, g_blend_nocolor;
XsdkRasterState g_raster_plain, g_raster_decal;
XsdkDepthStencilState g_depth_clear_all, g_depth_clear_color_only;

std::string g_shader_folder;
bool g_ready = false;        /* GPU, anel e estados prontos */
bool g_gpu_open = false;
bool g_presenting = false;
bool g_presented = false;
bool g_target_set = false;   /* xsdk_gpu_set_target feito desde o ultimo resolve */
bool g_cleared = false;      /* a eDRAM esta limpa (o resolve do quadro anterior a limpou) */
uint32_t g_dirty = DIRTY_ALL;
uint32_t g_system_draws = 0;
volatile int g_stop = 0;

/* estado de desenho pedido pelo Fast3D */
bool g_depth_test, g_depth_update, g_depth_compare, g_depth_prim;
uint16_t g_zmode;
bool g_use_alpha, g_modulate;
float g_znear = 0.0f, g_zfar = 1.0f;
struct Rect { int x, y, w, h; };
Rect g_viewport = { 0, 0, (int) WIN_W, (int) WIN_H };
Rect g_scissor = { 0, 0, (int) WIN_W, (int) WIN_H };

uint32_t g_frame_count = 0;
float g_noise_scale = 1.0f;
enum FilteringMode g_filter = FILTER_LINEAR;

uint64_t g_frames, g_tris, g_draws, g_skipped, g_uploads, g_shaders_compiled, g_shaders_read;
bool g_logged_layout = false;

void log_once(bool &flag, const char *what)
{
    if (!flag) {
        flag = true;
        xsdk_log_write("xenon: video: %s (so uma vez)", what);
    }
}

bool wait_fence(uint32_t fence, const char *what)
{
    if (fence == 0) return true;
    const NTSTATUS st = xsdk_gpu_wait(fence, FENCE_TIMEOUT_MS);
    if (!NT_SUCCESS(st)) xsdk_log_write("xenon: video: espera de fence (%s) falhou: 0x%08X", what, (unsigned) st);
    return NT_SUCCESS(st);
}

/* ---- Memoria de texturas --------------------------------------------------------------------------- */

Memory allocate(uint32_t bytes)
{
    const uint32_t pages = (bytes + PAGE_BYTES - 1) / PAGE_BYTES;
    if (pages > BLOCK_PAGES) return { xsdk_gpu_alloc(bytes), -1, 0, 0 };
    for (size_t b = 0;; b++) {
        if (b == g_blocks.size()) {
            volatile uint32_t *m = xsdk_gpu_alloc(BLOCK_PAGES * PAGE_BYTES);
            if (m == nullptr) return {};
            PageBlock pb;
            pb.memory = m;
            std::memset(pb.used, 0, sizeof(pb.used));
            g_blocks.push_back(pb);
        }
        PageBlock &blk = g_blocks[b];
        uint32_t run = 0;
        for (uint32_t page = 0; page < BLOCK_PAGES; page++) {
            run = blk.used[page] ? 0 : run + 1;
            if (run == pages) {
                const uint32_t first = page + 1 - pages;
                for (uint32_t p = first; p <= page; p++) blk.used[p] = true;
                return { blk.memory + first * PAGE_BYTES / 4, (int32_t) b, first, pages };
            }
        }
    }
}

void free_now(Memory &m)
{
    if (m.texels == nullptr) return;
    if (m.block < 0) {
        xsdk_gpu_free(m.texels);
    } else {
        for (uint32_t p = m.firstPage; p < m.firstPage + m.pages; p++) g_blocks[m.block].used[p] = false;
    }
    m = {};
}

void free_after_frame(Memory &m)
{
    if (m.texels != nullptr) g_pending.push_back({ m, 0 });
    m = {};
}

void release_passed()
{
    auto passed = [](PendingFree &p) {
        if (p.fence == 0 || !xsdk_gpu_fence_done(p.fence)) return false;
        free_now(p.memory);
        return true;
    };
    g_pending.erase(std::remove_if(g_pending.begin(), g_pending.end(), passed), g_pending.end());
}

bool replace_texels(Texture &t, uint32_t w, uint32_t h)
{
    XsdkTexture layout;
    NTSTATUS st = xsdk_texture_init(&layout, XSDK_FORMAT_R8G8B8A8, w, h, 1, true);
    if (!NT_SUCCESS(st)) {
        xsdk_log_write("xenon: video: xsdk_texture_init(%ux%u) falhou: 0x%08X", (unsigned) w, (unsigned) h, (unsigned) st);
        return false;
    }
    const Memory m = allocate(layout.size_bytes);
    if (m.texels == nullptr) {
        xsdk_log_write("xenon: video: sem memoria de GPU para textura %ux%u", (unsigned) w, (unsigned) h);
        return false;
    }
    free_after_frame(t.memory);
    xsdk_texture_attach(&layout, m.texels);
    t.texture = layout;
    t.size[0] = (float) w;
    t.size[1] = (float) h;
    t.memory = m;
    t.generation++;
    return true;
}

const Texture &texture_for(uint32_t id)
{
    return id < g_textures.size() && g_textures[id].memory.texels != nullptr ? g_textures[id] : g_textures[0];
}

/* ---- Programas ------------------------------------------------------------------------------------- */

void make_runs(std::vector<xn::XsdkLiteral> literals, std::vector<ConstantRun> &out)
{
    std::sort(literals.begin(), literals.end(),
              [](const xn::XsdkLiteral &a, const xn::XsdkLiteral &b) { return a.reg < b.reg; });
    for (const xn::XsdkLiteral &l : literals) {
        if (out.empty() || out.back().first + out.back().count != l.reg) out.push_back({ l.reg, 0, {} });
        out.back().count++;
        out.back().values.insert(out.back().values.end(), l.value, l.value + 4);
    }
}

bool finish_program(ShaderProgram &prg, const std::string &name, const std::string &vs, const std::string &fs,
                    const XsdkVertexElement *elements, uint32_t element_count)
{
    prg.drawable = false;
    prg.block = -1;
    prg.fetchSlots[0] = prg.fetchSlots[1] = -1;
    xn::XsdkCompiledProgram compiled;
    std::string error;
    if (!xn::XsdkShaderProgram(g_shader_folder, name, vs, fs, BLOCK_REGISTERS, compiled, error)) {
        xsdk_log_write("xenon: video: programa %s nao desenha: %s", name.c_str(), error.c_str());
        return false;
    }
    if (!error.empty()) xsdk_log_write("xenon: video: %s", error.c_str());
    if (compiled.read) {
        g_shaders_read++;
    } else {
        g_shaders_compiled++;
        xsdk_log_write("xenon: video: programa %s compilado: glslang %.1f ms, xost %.1f ms", name.c_str(), compiled.glslangMs,
                       compiled.xostMs);
    }
    prg.vertexContainer = std::move(compiled.vertex);
    prg.pixelContainer = std::move(compiled.pixel);
    const uint32_t stride = prg.vertexFloats * 4;
    NTSTATUS st = xsdk_shader_load(&prg.vertex, prg.vertexContainer.data(), (uint32_t) prg.vertexContainer.size() * 4);
    if (NT_SUCCESS(st)) {
        st = xsdk_shader_load(&prg.pixel, prg.pixelContainer.data(), (uint32_t) prg.pixelContainer.size() * 4);
        if (!NT_SUCCESS(st)) xsdk_shader_free(&prg.vertex);
    }
    if (NT_SUCCESS(st)) {
        st = xsdk_shader_set_vertex_layout(&prg.vertex, elements, element_count, &stride);
        st = NT_SUCCESS(st) ? xsdk_shader_link(&prg.vertex, &prg.pixel) : st;
        if (!NT_SUCCESS(st)) {
            xsdk_shader_free(&prg.pixel);
            xsdk_shader_free(&prg.vertex);
        }
    }
    if (!NT_SUCCESS(st)) {
        xsdk_log_write("xenon: video: carregar o programa %s falhou: 0x%08X", name.c_str(), (unsigned) st);
        return false;
    }
    make_runs(compiled.vertexLiterals, prg.vertexLiterals);
    make_runs(compiled.pixelLiterals, prg.pixelLiterals);
    prg.block = compiled.layout.block;
    prg.fetchSlots[0] = compiled.layout.samplers[0];
    prg.fetchSlots[1] = compiled.layout.samplers[1];
    prg.drawable = true;
    return true;
}

void load_program(ShaderProgram &prg, const CCFeatures &cc)
{
    const bool three = g_filter == FILTER_THREE_POINT;
    char name[40];
    snprintf(name, sizeof name, "%016llX-%08X-%X", (unsigned long long) prg.id0, (unsigned) prg.id1, three ? 1u : 0u);
    xn::Glsl glsl = xn::BuildGlsl(cc, three);
    std::vector<XsdkVertexElement> el(glsl.attributes.size() + 1);
    el[0] = { XSDK_USAGE_POSITION, 0, XSDK_VERTEX_FLOAT4, 0, 0, {}, false };
    for (size_t i = 0; i < glsl.attributes.size(); i++) {
        el[i + 1] = { XSDK_USAGE_TEXCOORD, (uint32_t) i, XSDK_VERTEX_FLOAT1 + glsl.attributes[i].floats - 1, 0,
                      glsl.attributes[i].offsetFloats * 4, {}, false };
    }
    prg.vertexFloats = glsl.vertexFloats;
    if (!finish_program(prg, name, glsl.vertex, glsl.fragment, el.data(), (uint32_t) el.size())) {
        /* deixa o texto do shader no log uma vez, para depurar */
        static int dumped = 0;
        if (dumped < 2) {
            dumped++;
            xsdk_log_write("xenon: video: ---- vertex ----\n%s\n---- fragment ----\n%s", glsl.vertex.c_str(),
                           glsl.fragment.c_str());
        }
    }
}

/* ---- Estados ---------------------------------------------------------------------------------------- */

uint32_t address_mode(uint32_t cm)
{
    if (cm == 0x10) return XSDK_ADDRESS_BORDER; /* GE_TX_BORDER */
    switch (cm & (G_TX_MIRROR | G_TX_CLAMP)) {
    case G_TX_NOMIRROR | G_TX_CLAMP: return XSDK_ADDRESS_CLAMP;
    case G_TX_MIRROR | G_TX_WRAP: return XSDK_ADDRESS_MIRROR;
    case G_TX_MIRROR | G_TX_CLAMP: return XSDK_ADDRESS_MIRROR_ONCE;
    default: return XSDK_ADDRESS_WRAP;
    }
}

int depth_index()
{
    if (!g_depth_test) return 0;
    int func; /* 0 LESS, 1 LEQUAL, 2 ALWAYS */
    if (!g_depth_compare) {
        func = 2;
    } else if (g_zmode == ZMODE_INTER || g_zmode == ZMODE_DEC) {
        func = 1;
    } else {
        func = g_depth_prim ? 1 : 0;
    }
    return 1 + func * 2 + (g_depth_update ? 1 : 0);
}

bool decal_mode() { return g_depth_test && g_depth_compare && g_zmode == ZMODE_DEC; }

void apply_state()
{
    if (g_dirty & DIRTY_DEPTH) xsdk_gpu_set_depth_stencil(&g_depth_states[depth_index()]);
    if (g_dirty & DIRTY_RASTER) xsdk_gpu_set_raster(decal_mode() ? &g_raster_decal : &g_raster_plain);
    if (g_dirty & DIRTY_BLEND)
        xsdk_gpu_set_blend(!g_use_alpha ? &g_blend_opaque : g_modulate ? &g_blend_modulate : &g_blend_alpha);
    if (g_dirty & DIRTY_VIEWPORT) {
        const int height = (int) WIN_H;
        const int top = height - g_viewport.y - g_viewport.h;
        xsdk_gpu_set_viewport((float) g_viewport.x - 0.5f, (float) top - 0.5f, (float) g_viewport.w, (float) g_viewport.h,
                              g_znear, g_zfar);
        const int left = std::max({ g_scissor.x, g_viewport.x, 0 });
        const int right = std::min({ g_scissor.x + g_scissor.w, g_viewport.x + g_viewport.w, (int) WIN_W });
        const int upper = std::max({ height - g_scissor.y - g_scissor.h, top, 0 });
        const int lower = std::min({ height - g_scissor.y, top + g_viewport.h, height });
        xsdk_gpu_set_scissor((uint32_t) std::min(left, (int) WIN_W), (uint32_t) std::min(upper, height),
                             (uint32_t) std::max(right - left, 0), (uint32_t) std::max(lower - upper, 0));
    }
    g_dirty = 0;
}

void apply_program(ShaderProgram &prg)
{
    if (&prg == g_applied) return;
    xsdk_gpu_set_shaders(&prg.vertex, &prg.pixel);
    for (const ConstantRun &r : prg.vertexLiterals) xsdk_gpu_set_vertex_constants(r.first, r.values.data(), r.count);
    for (const ConstantRun &r : prg.pixelLiterals) xsdk_gpu_set_pixel_constants(r.first, r.values.data(), r.count);
    g_applied = &prg;
}

float g_block[xn::BLOCK_FLOATS];
int32_t g_block_reg = -1;
bool g_block_valid = false;

void apply_textures(const ShaderProgram &prg)
{
    for (uint32_t t = 0; t < 2; t++) {
        const int slot = prg.fetchSlots[t];
        if (slot < 0) continue;
        const Texture &tex = texture_for(g_cur_tex[t]);
        FetchBinding &b = g_bound[slot];
        if (b.texture != &tex || b.generation != tex.generation) {
            xsdk_gpu_set_texture((uint32_t) slot, &tex.texture, &tex.sampler);
            b = { &tex, tex.generation };
        }
    }
}

void apply_block(const ShaderProgram &prg)
{
    if (prg.block < 0) return;
    float block[xn::BLOCK_FLOATS] = {};
    for (uint32_t i = 0; i < 2; i++) {
        if (prg.fetchSlots[i] >= 0) {
            const Texture &tex = texture_for(g_cur_tex[i]);
            block[i * 4 + 0] = tex.size[0];
            block[i * 4 + 1] = tex.size[1];
            block[8 + i] = tex.threePoint ? 1.0f : 0.0f;
        }
    }
    block[10] = (float) g_frame_count;
    block[11] = g_noise_scale;
    if (g_block_valid && prg.block == g_block_reg && std::memcmp(block, g_block, sizeof(block)) == 0) return;
    xsdk_gpu_set_pixel_constants((uint32_t) prg.block, block, BLOCK_REGISTERS);
    std::memcpy(g_block, block, sizeof(block));
    g_block_reg = prg.block;
    g_block_valid = true;
}

/* Depois de um resolve (ou no inicio do quadro) o alvo e o resto precisam ser postos de novo. */
void reset_after_resolve()
{
    g_target_set = false;
    g_dirty = DIRTY_ALL;
    g_applied = nullptr;
    g_block_valid = false;
    for (FetchBinding &b : g_bound) b = {};
}

void set_target()
{
    xsdk_gpu_set_target(WIN_W, WIN_H);
    g_target_set = true;
    g_dirty = DIRTY_ALL;
    g_applied = nullptr;
    g_block_valid = false;
    for (FetchBinding &b : g_bound) b = {};
}

/* ---- Anel de vertices -------------------------------------------------------------------------------- */

void next_segment()
{
    g_ring_fences[g_ring_segment] = xsdk_gpu_fence();
    g_ring_segment = (g_ring_segment + 1) % RING_SEGMENTS;
    g_ring_offset = 0;
    wait_fence(g_ring_fences[g_ring_segment], "segmento do anel de vertices");
}

uint32_t write_vertices(const float *floats, uint32_t bytes)
{
    if (g_ring_offset + bytes > SEGMENT_BYTES) next_segment();
    if (g_ring_offset == 0) xsdk_gpu_invalidate(g_ring_addr + g_ring_segment * SEGMENT_BYTES, SEGMENT_BYTES);
    const uint32_t offset = g_ring_segment * SEGMENT_BYTES + g_ring_offset;
    std::memcpy((void *) ((uintptr_t) g_ring + offset), floats, bytes);
    g_ring_offset += (bytes + DRAW_BYTES_ALIGN - 1) & ~(DRAW_BYTES_ALIGN - 1);
    return g_ring_addr + offset;
}

/* ---- API de renderizacao ------------------------------------------------------------------------------ */

const char *r_get_name() { return "Xenos (xsdk)"; }
int r_get_max_texture_size() { return 2048; }
GfxClipParameters r_get_clip_parameters() { return { true, false }; }
void r_unload_shader(ShaderProgram *) {}
void r_load_shader(ShaderProgram *p) { g_shader = p; }

ShaderProgram *r_lookup_shader(uint64_t id0, uint32_t id1)
{
    auto it = g_shaders.find({ id0, id1 });
    return it == g_shaders.end() ? nullptr : &it->second;
}

ShaderProgram *r_create_and_load_new_shader(uint64_t id0, uint32_t id1)
{
    CCFeatures cc;
    std::memset(&cc, 0, sizeof cc);
    gfx_cc_get_features(id0, id1, &cc);
    ShaderProgram &p = g_shaders[{ id0, id1 }];
    p.id0 = id0;
    p.id1 = id1;
    p.num_inputs = (uint8_t) cc.num_inputs;
    p.used_textures[0] = cc.used_textures[0];
    p.used_textures[1] = cc.used_textures[1];
    p.drawable = false;
    if (g_ready) load_program(p, cc);
    g_shader = &p;
    return &p;
}

void r_shader_get_info(ShaderProgram *p, uint8_t *num_inputs, bool used_textures[2])
{
    *num_inputs = p->num_inputs;
    used_textures[0] = p->used_textures[0];
    used_textures[1] = p->used_textures[1];
}

void r_clear_shaders()
{
    if (g_ready) wait_fence(xsdk_gpu_fence(), "limpar shaders");
    for (auto &e : g_shaders) {
        if (e.second.drawable) {
            xsdk_shader_free(&e.second.pixel);
            xsdk_shader_free(&e.second.vertex);
        }
    }
    g_shaders.clear();
    g_shader = nullptr;
    g_applied = nullptr;
}

uint32_t r_new_texture()
{
    g_textures.emplace_back();
    return (uint32_t) g_textures.size() - 1;
}

void r_select_texture(int tile, uint32_t id, bool linear)
{
    g_cur_tile = tile;
    g_cur_tex[tile] = id;
    if (id != 0 && id < g_textures.size()) g_textures[id].threePoint = linear;
}

void r_upload_texture(const uint8_t *rgba, uint32_t w, uint32_t h)
{
    g_uploads++;
    const uint32_t id = g_cur_tex[g_cur_tile];
    if (!g_ready || id == 0 || id >= g_textures.size() || w == 0 || h == 0) return;
    Texture &t = g_textures[id];
    if (!replace_texels(t, w, h)) return;
    xsdk_texture_upload(&t.texture, 0, rgba, w * 4);
    xsdk_gpu_invalidate(t.texture.address, t.texture.size_bytes);
}

void r_set_sampler_parameters(int tile, bool linear, uint32_t cms, uint32_t cmt)
{
    const uint32_t id = g_cur_tex[tile];
    if (!g_ready || id == 0 || id >= g_textures.size()) return;
    Texture &t = g_textures[id];
    const uint32_t filter = linear && g_filter == FILTER_LINEAR ? XSDK_FILTER_LINEAR : XSDK_FILTER_POINT;
    XsdkSampler s = {};
    s.mag_filter = filter;
    s.min_filter = filter;
    s.mip_filter = XSDK_FILTER_POINT;
    s.address_u = address_mode(cms);
    s.address_v = address_mode(cmt);
    s.address_w = XSDK_ADDRESS_WRAP;
    xsdk_texture_sampler_state(&t.sampler, &s);
    t.threePoint = linear;
    t.generation++;
}

void r_set_depth_mode(bool test, bool update, bool compare, bool prim, uint16_t zmode)
{
    g_depth_test = test;
    g_depth_update = update;
    g_depth_compare = compare;
    g_depth_prim = prim;
    g_zmode = zmode;
    g_dirty |= DIRTY_DEPTH | DIRTY_RASTER;
}

void r_set_depth_range(float znear, float zfar)
{
    if (zfar > 0.9999995f) zfar = 0.9999995f;
    g_znear = znear;
    g_zfar = zfar;
    g_dirty |= DIRTY_VIEWPORT;
}

void r_set_viewport(int x, int y, int w, int h)
{
    g_viewport = { x, y, w, h };
    g_dirty |= DIRTY_VIEWPORT;
}

void r_set_scissor(int x, int y, int w, int h)
{
    g_scissor = { x, y, w, h };
    g_dirty |= DIRTY_VIEWPORT;
}

void r_set_use_alpha(bool use_alpha, bool modulate)
{
    g_use_alpha = use_alpha;
    g_modulate = modulate;
    g_dirty |= DIRTY_BLEND;
}

void r_draw_triangles(float *buf, size_t len, size_t num_tris)
{
    g_draws++;
    g_tris += num_tris;
    const ShaderProgram *prg = g_shader;
    if (!g_ready || prg == nullptr || !prg->drawable || !g_target_set) {
        g_skipped++;
        return;
    }
    if (len != num_tris * 3 * prg->vertexFloats) {
        log_once(g_logged_layout, "os vertices de um desenho nao tem o tamanho que o programa le; pulados");
        g_skipped++;
        return;
    }
    if (g_dirty != 0) apply_state();
    apply_program(*const_cast<ShaderProgram *>(prg));
    apply_textures(*prg);
    apply_block(*prg);
    const uint32_t bytes = (uint32_t) len * 4;
    const uint32_t address = write_vertices(buf, bytes);
    xsdk_gpu_set_vertices(0, address, bytes);
    xsdk_gpu_draw(XSDK_PRIMITIVE_TRIANGLE_LIST, (uint32_t) num_tris * 3);
    g_cleared = false;
}

void r_init()
{
    gfx_framebuffers_enabled = false;
    if (!g_gpu_open) {
        xsdk_log_write("xenon: video: GPU nao abriu; nada sera desenhado");
        return;
    }
    g_ring = xsdk_gpu_alloc(RING_BYTES);
    if (g_ring == nullptr) {
        xsdk_log_write("xenon: video: sem memoria para o anel de vertices; nada sera desenhado");
        return;
    }
    g_ring_addr = xsdk_gpu_address(g_ring);
    g_textures.resize(1);
    static const uint8_t black[4] = { 0, 0, 0, 255 };
    if (replace_texels(g_textures[0], 1, 1)) {
        xsdk_texture_upload(&g_textures[0].texture, 0, black, sizeof(black));
        xsdk_gpu_invalidate(g_textures[0].texture.address, g_textures[0].texture.size_bytes);
    }
    XsdkSampler point = {};
    xsdk_texture_sampler_state(&g_textures[0].sampler, &point);

    /* profundidade: indice 0 = sem teste; 1 + func*2 + escreve, func 0 LESS, 1 LEQUAL, 2 ALWAYS */
    {
        XsdkDepthStencil d = {};
        d.depth_test = false;
        d.depth_write = false;
        d.depth_compare = XSDK_COMPARE_ALWAYS;
        xsdk_gpu_depth_stencil_state(&g_depth_states[0], &d);
        const uint32_t cmp[3] = { XSDK_COMPARE_LESS, XSDK_COMPARE_LESS_EQUAL, XSDK_COMPARE_ALWAYS };
        for (int f = 0; f < 3; f++) {
            for (int w = 0; w < 2; w++) {
                XsdkDepthStencil e = {};
                e.depth_test = true;
                e.depth_write = w != 0;
                e.depth_compare = cmp[f];
                xsdk_gpu_depth_stencil_state(&g_depth_states[1 + f * 2 + w], &e);
            }
        }
        XsdkDepthStencil e = {};
        e.depth_test = true;
        e.depth_write = true;
        e.depth_compare = XSDK_COMPARE_ALWAYS;
        xsdk_gpu_depth_stencil_state(&g_depth_clear_all, &e);
    }
    XsdkBlend blend = {};
    blend.color_source = XSDK_BLEND_ONE;
    blend.color_dest = XSDK_BLEND_ZERO;
    blend.alpha_source = XSDK_BLEND_ONE;
    blend.alpha_dest = XSDK_BLEND_ZERO;
    blend.write_mask = XSDK_WRITE_ALL;
    xsdk_gpu_blend_state(&g_blend_opaque, &blend);
    blend.write_mask = 0;
    xsdk_gpu_blend_state(&g_blend_nocolor, &blend);
    blend.color_source = XSDK_BLEND_SRC_ALPHA;
    blend.color_dest = XSDK_BLEND_ONE_MINUS_SRC_ALPHA;
    blend.alpha_source = XSDK_BLEND_SRC_ALPHA;
    blend.alpha_dest = XSDK_BLEND_ONE_MINUS_SRC_ALPHA;
    blend.write_mask = XSDK_WRITE_ALL;
    xsdk_gpu_blend_state(&g_blend_alpha, &blend);
    blend.color_source = XSDK_BLEND_DST_COLOR;
    blend.color_dest = XSDK_BLEND_ZERO;
    blend.alpha_source = XSDK_BLEND_DST_COLOR;
    blend.alpha_dest = XSDK_BLEND_ZERO;
    xsdk_gpu_blend_state(&g_blend_modulate, &blend);

    XsdkRaster raster = {};
    raster.cull = XSDK_CULL_NONE;
    raster.fill = XSDK_FILL_SOLID;
    xsdk_gpu_raster_state(&g_raster_plain, &raster);
    raster.depth_bias = DECAL_BIAS_UNITS;
    raster.depth_bias_slope = DECAL_BIAS_SLOPE;
    xsdk_gpu_raster_state(&g_raster_decal, &raster);

    g_shader_folder = xn::XsdkShaderCacheOpen("game:/shaders/midas", xn::Templates());
    if (g_shader_folder.empty())
        xsdk_log_write("xenon: video: game:/shaders/midas nao pode ser criada; shaders compilados nao serao guardados");
    else
        xsdk_log_write("xenon: video: shaders guardados em %s", g_shader_folder.c_str());

    const XsdkVertexElement position = { XSDK_USAGE_POSITION, 0, XSDK_VERTEX_FLOAT4, 0, 0, {}, false };
    g_clear.vertexFloats = 4;
    finish_program(g_clear, "clear", xn::CLEAR_VERTEX, xn::CLEAR_FRAGMENT, &position, 1);
    g_ready = true;
    xsdk_log_write("xenon: video: renderizador Xenos pronto (%ux%u)", (unsigned) WIN_W, (unsigned) WIN_H);
}

void r_on_resize() {}

void r_start_frame()
{
    g_frame_count++;
    if (!g_ready) return;
    release_passed();
}

void r_end_frame()
{
    g_frames++;
    if ((g_frames % 300) == 0)
        xsdk_log_write("xenon: video: %llu quadros, %llu desenhos (%llu pulados), %llu triangulos, %llu texturas, shaders: "
                       "%llu compilados %llu lidos",
                       (unsigned long long) g_frames, (unsigned long long) g_draws, (unsigned long long) g_skipped,
                       (unsigned long long) g_tris, (unsigned long long) g_uploads, (unsigned long long) g_shaders_compiled,
                       (unsigned long long) g_shaders_read);
}

/* Depois do present da janela, que resolveu a tela e a limpou. */
void r_finish_render()
{
    if (!g_ready) return;
    reset_after_resolve();
    XsdkGpuStats stats;
    xsdk_gpu_stats(&stats);
    g_cleared = g_presented && stats.system_draws == g_system_draws;
    g_system_draws = stats.system_draws;
    const uint32_t fence = xsdk_gpu_fence();
    for (PendingFree &p : g_pending)
        if (p.fence == 0) p.fence = fence;
}

int r_create_framebuffer() { return 1; }
void r_update_framebuffer_parameters(int, uint32_t, uint32_t, uint32_t, bool, bool, bool, bool) {}

bool r_start_draw_to_framebuffer(int fb, float noise_scale)
{
    if (noise_scale != 0.0f) g_noise_scale = 1.0f / noise_scale;
    if (!g_ready) return fb == 0;
    if (fb != 0) return false;
    if (!g_target_set) set_target();
    return true;
}

void r_copy_framebuffer(int, int, int, int, bool, bool) {}

void r_clear_framebuffer(bool color, bool depth)
{
    if (!g_ready || !g_target_set || !g_clear.drawable || (!color && !depth)) return;
    if (g_cleared && color && depth) return;
    static const float corners[12] = { -1.0f, -1.0f, 1.0f, 1.0f, 3.0f, -1.0f, 1.0f, 1.0f, -1.0f, 3.0f, 1.0f, 1.0f };
    xsdk_gpu_set_viewport(0.0f, 0.0f, (float) WIN_W, (float) WIN_H, 0.0f, 1.0f);
    xsdk_gpu_set_scissor(0, 0, WIN_W, WIN_H);
    if (depth)
        xsdk_gpu_set_depth_stencil(&g_depth_clear_all);
    else
        xsdk_gpu_set_depth_stencil(&g_depth_states[0]);
    xsdk_gpu_set_blend(color ? &g_blend_opaque : &g_blend_nocolor);
    xsdk_gpu_set_raster(&g_raster_plain);
    apply_program(g_clear);
    const uint32_t address = write_vertices(corners, sizeof(corners));
    xsdk_gpu_set_vertices(0, address, sizeof(corners));
    xsdk_gpu_draw(XSDK_PRIMITIVE_TRIANGLE_LIST, 3);
    g_dirty = DIRTY_ALL;
    g_block_valid = false;
    g_cleared = color && depth;
}

void r_resolve_msaa_color_buffer(int, int) {}
void *r_get_framebuffer_texture_id(int fb) { return (void *) (uintptr_t) fb; }
void r_select_texture_fb(int) {}

void r_delete_texture(uint32_t id)
{
    if (g_ready && id != 0 && id < g_textures.size()) {
        free_after_frame(g_textures[id].memory);
        g_textures[id].texture = {};
        g_textures[id].generation++;
    }
}

void r_set_texture_filter(enum FilteringMode m) { g_filter = m; }
enum FilteringMode r_get_texture_filter() { return g_filter; }

GfxRenderingAPI make_rapi()
{
    GfxRenderingAPI r;
    std::memset(&r, 0, sizeof(r));
    r.get_name = r_get_name;
    r.get_max_texture_size = r_get_max_texture_size;
    r.get_clip_parameters = r_get_clip_parameters;
    r.unload_shader = r_unload_shader;
    r.load_shader = r_load_shader;
    r.create_and_load_new_shader = r_create_and_load_new_shader;
    r.lookup_shader = r_lookup_shader;
    r.shader_get_info = r_shader_get_info;
    r.clear_shaders = r_clear_shaders;
    r.new_texture = r_new_texture;
    r.select_texture = r_select_texture;
    r.upload_texture = r_upload_texture;
    r.set_sampler_parameters = r_set_sampler_parameters;
    r.set_depth_mode = r_set_depth_mode;
    r.set_depth_range = r_set_depth_range;
    r.set_viewport = r_set_viewport;
    r.set_scissor = r_set_scissor;
    r.set_use_alpha = r_set_use_alpha;
    r.draw_triangles = r_draw_triangles;
    r.init = r_init;
    r.on_resize = r_on_resize;
    r.start_frame = r_start_frame;
    r.end_frame = r_end_frame;
    r.finish_render = r_finish_render;
    r.create_framebuffer = r_create_framebuffer;
    r.update_framebuffer_parameters = r_update_framebuffer_parameters;
    r.start_draw_to_framebuffer = r_start_draw_to_framebuffer;
    r.copy_framebuffer = r_copy_framebuffer;
    r.clear_framebuffer = r_clear_framebuffer;
    r.resolve_msaa_color_buffer = r_resolve_msaa_color_buffer;
    r.get_framebuffer_texture_id = r_get_framebuffer_texture_id;
    r.select_texture_fb = r_select_texture_fb;
    r.delete_texture = r_delete_texture;
    r.set_texture_filter = r_set_texture_filter;
    r.get_texture_filter = r_get_texture_filter;
    /* retained_* e resolve_n64_aa ficam NULL: o Fast3D usa o caminho simples. */
    return r;
}

/* ---- Gerenciador de janela ---------------------------------------------------------------------------- */

int g_swap_interval = 1;
int g_target_fps = 60;
void (*g_fs_cb)(bool) = nullptr;

void shutdown_video()
{
    static bool done = false;
    if (done) return;
    done = true;
    if (g_ready) {
        wait_fence(xsdk_gpu_fence(), "encerrar");
    }
    if (g_presenting) {
        xsdk_present_close();
        g_presenting = false;
    }
    if (g_gpu_open) {
        xsdk_gpu_close();
        g_gpu_open = false;
    }
    xsdk_log_write("xenon: video: GPU encerrada");
}

void w_init(const GfxWindowInitSettings *)
{
    NTSTATUS st = xsdk_gpu_open();
    if (!NT_SUCCESS(st)) {
        xsdk_log_write("xenon: video: xsdk_gpu_open falhou: 0x%08X", (unsigned) st);
        return;
    }
    g_gpu_open = true;
    st = xsdk_splash_begin();
    if (!NT_SUCCESS(st)) xsdk_log_write("xenon: video: xsdk_splash_begin falhou: 0x%08X", (unsigned) st);
    uint32_t w = WIN_W, h = WIN_H;
    st = xsdk_present_open(XSDK_DISPLAY_720P, BUFFERS, SWAP_INTERVAL, &w, &h);
    if (!NT_SUCCESS(st)) {
        xsdk_log_write("xenon: video: xsdk_present_open falhou: 0x%08X", (unsigned) st);
        xsdk_splash_end();
        return;
    }
    g_presenting = true;
    xsdk_log_write("xenon: video: tela aberta %ux%u", (unsigned) w, (unsigned) h);
}

void w_close() { shutdown_video(); }
int w_get_display_mode(int, int *w, int *h) { *w = WIN_W; *h = WIN_H; return 1; }
int w_get_current_display_mode(int *w, int *h) { *w = WIN_W; *h = WIN_H; return 1; }
int w_get_num_display_modes() { return 1; }
int32_t w_get_fullscreen_state() { return 1; }
void w_set_fullscreen_changed_callback(void (*cb)(bool)) { g_fs_cb = cb; }
void w_set_fullscreen(bool) {}
void w_set_fullscreen_exclusive(bool) {}
void w_set_fullscreen_flag(int32_t) {}
int32_t w_get_fullscreen_flag_mode() { return 0; }
int32_t w_get_maximized_state() { return 0; }
void w_set_maximize(bool) {}
void w_get_active_window_refresh_rate(uint32_t *hz) { *hz = 60; }
void w_set_cursor_visibility(bool) {}
void w_set_closest_resolution(int32_t, int32_t, bool) {}
void w_set_dimensions(uint32_t, uint32_t, int32_t, int32_t) {}
void w_get_dimensions(uint32_t *w, uint32_t *h, int32_t *x, int32_t *y)
{
    if (w) *w = WIN_W;
    if (h) *h = WIN_H;
    if (x) *x = 0;
    if (y) *y = 0;
}
void w_get_centered_positions(int32_t, int32_t, int32_t *x, int32_t *y) { *x = 0; *y = 0; }
void w_handle_events() {}
bool w_start_frame() { return true; }

void w_swap_buffers_begin()
{
    g_presented = false;
    if (!g_presenting || !g_ready) return;
    uint32_t address;
    const NTSTATUS st = xsdk_present_begin(&address, FENCE_TIMEOUT_MS);
    if (!NT_SUCCESS(st)) {
        xsdk_log_write("xenon: video: xsdk_present_begin falhou: 0x%08X", (unsigned) st);
        return;
    }
    xsdk_gpu_resolve(address, WIN_W, WIN_H, CLEAR_ARGB);
    xsdk_present_end();
    g_presented = true;
}

void w_swap_buffers_end()
{
    if (g_stop) {
        xsdk_log_write("xenon: video: encerrando a pedido");
        shutdown_video();
        exit(0);
    }
}

double w_get_time()
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (double) t.tv_sec + (double) t.tv_nsec * 1e-9;
}
int32_t w_get_target_fps() { return g_target_fps; }
void w_set_target_fps(int fps) { g_target_fps = fps; }
bool w_can_disable_vsync() { return false; }
void *w_get_window_handle() { return nullptr; }
void w_set_window_title(const char *) {}
int w_get_swap_interval() { return g_swap_interval; }
bool w_set_swap_interval(int i) { g_swap_interval = i; return true; }

GfxWindowManagerAPI make_wapi()
{
    GfxWindowManagerAPI w;
    std::memset(&w, 0, sizeof(w));
    w.init = w_init;
    w.close = w_close;
    w.get_display_mode = w_get_display_mode;
    w.get_current_display_mode = w_get_current_display_mode;
    w.get_num_display_modes = w_get_num_display_modes;
    w.get_fullscreen_state = w_get_fullscreen_state;
    w.set_fullscreen_changed_callback = w_set_fullscreen_changed_callback;
    w.set_fullscreen = w_set_fullscreen;
    w.set_fullscreen_exclusive = w_set_fullscreen_exclusive;
    w.set_fullscreen_flag = w_set_fullscreen_flag;
    w.get_fullscreen_flag_mode = w_get_fullscreen_flag_mode;
    w.get_maximized_state = w_get_maximized_state;
    w.set_maximize = w_set_maximize;
    w.get_active_window_refresh_rate = w_get_active_window_refresh_rate;
    w.set_cursor_visibility = w_set_cursor_visibility;
    w.set_closest_resolution = w_set_closest_resolution;
    w.set_dimensions = w_set_dimensions;
    w.get_dimensions = w_get_dimensions;
    w.get_centered_positions = w_get_centered_positions;
    w.handle_events = w_handle_events;
    w.start_frame = w_start_frame;
    w.swap_buffers_begin = w_swap_buffers_begin;
    w.swap_buffers_end = w_swap_buffers_end;
    w.get_time = w_get_time;
    w.get_target_fps = w_get_target_fps;
    w.set_target_fps = w_set_target_fps;
    w.can_disable_vsync = w_can_disable_vsync;
    w.get_window_handle = w_get_window_handle;
    w.set_window_title = w_set_window_title;
    w.get_swap_interval = w_get_swap_interval;
    w.set_swap_interval = w_set_swap_interval;
    return w;
}

} // namespace

/* Pede o encerramento: o proximo fim de quadro fecha a GPU e sai (chamado pelo vigia de tempo). */
extern "C" void xenon_video_request_stop(void) { g_stop = 1; }

/* Os objetos que o video.c do Midas espera (no PC vem de gfx_sdl2.cpp e gfx_opengl.cpp). */
GfxRenderingAPI gfx_opengl_api = make_rapi();
GfxWindowManagerAPI gfx_sdl = make_wapi();

/* ---- Simbolos que o Fast3D e o video.c esperam de gfx_opengl.cpp / gfx_sdl2.cpp -------------------- */
extern "C" {
unsigned g_GfxUploadError, g_GfxUploadErrorCount;
unsigned g_GfxUpW, g_GfxUpH;
unsigned long long g_GfxUpDrainUs, g_GfxUpTexUs, g_GfxUpMipUs, g_GfxUpErrUs;
int g_GfxPresentX = 0;
int g_GfxPresentW = 0;
int ge_shot_force = 0;

unsigned gfx_opengl_poll_gl_error(void) { return 0; }
void gfx_opengl_retained_stats(unsigned *draws, unsigned *tris, unsigned *buffers, unsigned *descs)
{
    if (draws) *draws = 0;
    if (tris) *tris = 0;
    if (buffers) *buffers = 0;
    if (descs) *descs = 0;
}
int gfx_vsync_pacing(void) { return 0; }
int gfx_display_refresh_hz(void) { return 60; }
void gfx_sdl_gl_make_current(void) {}
void gfx_sdl_gl_release_current(void) {}
int gfx_sdl_get_saved_geometry(int *, int *, int *, int *, int *) { return 0; }
}
