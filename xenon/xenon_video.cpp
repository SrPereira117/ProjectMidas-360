/* Camada de video do Xbox 360 para o Project Midas.
 *
 * O Midas desenha pelo Fast3D (port/fast3d/gfx_pc.cpp), que fala com dois objetos: um "gerenciador de
 * janela" (gfx_sdl) e uma "API de renderizacao" (gfx_opengl_api). No PC sao SDL2 e OpenGL; aqui sao estes.
 *
 * ETAPA ATUAL: o gerenciador de janela e fixo (640x480) e a API de renderizacao e NULA: ela aceita tudo e
 * nao desenha nada, mas conta os quadros e os triangulos no log. Serve para provar que o Fast3D roda inteiro
 * no 360 (interpretando as listas de desenho do jogo). O desenho de verdade sobre o Xenos vem depois. */
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <vector>

#include <PR/gbi.h>
#include "gfx_api.h"
#include "gfx_cc.h"
#include "gfx_sdl.h"
#include "gfx_opengl.h"

extern "C" {
void xsdk_log_write(const char *fmt, ...);
}

/* ---- API de renderizacao nula ------------------------------------------------------------------ */

struct ShaderProgram {
    uint64_t id0;
    uint32_t id1;
    uint8_t num_inputs;
    bool used_textures[2];
};

namespace {

std::vector<ShaderProgram *> g_shaders;
uint32_t g_next_texture = 1;
int g_next_fb = 1;
uint64_t g_frames, g_tris, g_draws;
enum FilteringMode g_filter = FILTER_LINEAR;

const char *n_get_name() { return "Xenos (nulo)"; }
int n_get_max_texture_size() { return 2048; }
GfxClipParameters n_get_clip_parameters() { return { true, false }; }
void n_unload_shader(ShaderProgram *) {}
void n_load_shader(ShaderProgram *) {}

ShaderProgram *n_lookup_shader(uint64_t id0, uint32_t id1)
{
    for (ShaderProgram *p : g_shaders)
        if (p->id0 == id0 && p->id1 == id1) return p;
    return nullptr;
}

ShaderProgram *n_create_and_load_new_shader(uint64_t id0, uint32_t id1)
{
    CCFeatures cc;
    gfx_cc_get_features(id0, id1, &cc);
    ShaderProgram *p = new ShaderProgram();
    p->id0 = id0;
    p->id1 = id1;
    p->num_inputs = (uint8_t) cc.num_inputs;
    p->used_textures[0] = cc.used_textures[0];
    p->used_textures[1] = cc.used_textures[1];
    g_shaders.push_back(p);
    return p;
}

void n_shader_get_info(ShaderProgram *p, uint8_t *num_inputs, bool used_textures[2])
{
    *num_inputs = p->num_inputs;
    used_textures[0] = p->used_textures[0];
    used_textures[1] = p->used_textures[1];
}

void n_clear_shaders()
{
    for (ShaderProgram *p : g_shaders) delete p;
    g_shaders.clear();
}

uint32_t n_new_texture() { return g_next_texture++; }
void n_select_texture(int, uint32_t, bool) {}
void n_upload_texture(const uint8_t *, uint32_t, uint32_t) {}
void n_set_sampler_parameters(int, bool, uint32_t, uint32_t) {}
void n_set_depth_mode(bool, bool, bool, bool, uint16_t) {}
void n_set_depth_range(float, float) {}
void n_set_viewport(int, int, int, int) {}
void n_set_scissor(int, int, int, int) {}
void n_set_use_alpha(bool, bool) {}

void n_draw_triangles(float *, size_t, size_t num_tris)
{
    g_tris += num_tris;
    g_draws++;
}

void n_init() { xsdk_log_write("xenon: video: Fast3D iniciado (renderizador nulo)"); }
void n_on_resize() {}
void n_start_frame() {}

void n_end_frame()
{
    g_frames++;
    if ((g_frames % 120) == 0)
        xsdk_log_write("xenon: video: %llu quadros, %llu desenhos, %llu triangulos",
                       (unsigned long long) g_frames, (unsigned long long) g_draws,
                       (unsigned long long) g_tris);
}

void n_finish_render() {}
int n_create_framebuffer() { return g_next_fb++; }
void n_update_framebuffer_parameters(int, uint32_t, uint32_t, uint32_t, bool, bool, bool, bool) {}
bool n_start_draw_to_framebuffer(int, float) { return true; }
void n_copy_framebuffer(int, int, int, int, bool, bool) {}
void n_clear_framebuffer(bool, bool) {}
void n_resolve_msaa_color_buffer(int, int) {}
void *n_get_framebuffer_texture_id(int fb) { return (void *) (uintptr_t) fb; }
void n_select_texture_fb(int) {}
void n_delete_texture(uint32_t) {}
void n_set_texture_filter(enum FilteringMode m) { g_filter = m; }
enum FilteringMode n_get_texture_filter() { return g_filter; }

GfxRenderingAPI make_rapi()
{
    GfxRenderingAPI r;
    std::memset(&r, 0, sizeof(r));
    r.get_name = n_get_name;
    r.get_max_texture_size = n_get_max_texture_size;
    r.get_clip_parameters = n_get_clip_parameters;
    r.unload_shader = n_unload_shader;
    r.load_shader = n_load_shader;
    r.create_and_load_new_shader = n_create_and_load_new_shader;
    r.lookup_shader = n_lookup_shader;
    r.shader_get_info = n_shader_get_info;
    r.clear_shaders = n_clear_shaders;
    r.new_texture = n_new_texture;
    r.select_texture = n_select_texture;
    r.upload_texture = n_upload_texture;
    r.set_sampler_parameters = n_set_sampler_parameters;
    r.set_depth_mode = n_set_depth_mode;
    r.set_depth_range = n_set_depth_range;
    r.set_viewport = n_set_viewport;
    r.set_scissor = n_set_scissor;
    r.set_use_alpha = n_set_use_alpha;
    r.draw_triangles = n_draw_triangles;
    r.init = n_init;
    r.on_resize = n_on_resize;
    r.start_frame = n_start_frame;
    r.end_frame = n_end_frame;
    r.finish_render = n_finish_render;
    r.create_framebuffer = n_create_framebuffer;
    r.update_framebuffer_parameters = n_update_framebuffer_parameters;
    r.start_draw_to_framebuffer = n_start_draw_to_framebuffer;
    r.copy_framebuffer = n_copy_framebuffer;
    r.clear_framebuffer = n_clear_framebuffer;
    r.resolve_msaa_color_buffer = n_resolve_msaa_color_buffer;
    r.get_framebuffer_texture_id = n_get_framebuffer_texture_id;
    r.select_texture_fb = n_select_texture_fb;
    r.delete_texture = n_delete_texture;
    r.set_texture_filter = n_set_texture_filter;
    r.get_texture_filter = n_get_texture_filter;
    /* retained_* e resolve_n64_aa ficam NULL: o Fast3D usa o caminho simples. */
    return r;
}

/* ---- Gerenciador de janela fixo ---------------------------------------------------------------- */

constexpr uint32_t WIN_W = 640, WIN_H = 480;
int g_swap_interval = 1;
int g_target_fps = 60;
void (*g_fs_cb)(bool) = nullptr;

void w_init(const GfxWindowInitSettings *) {}
void w_close() {}
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
void w_swap_buffers_begin() {}
void w_swap_buffers_end() {}
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
