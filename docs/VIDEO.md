# Video: plano do renderizador Xenos para o Midas

Estado (2026-11-16): `xenon/xenon_video.cpp` tem um renderizador NULO. O Fast3D inteiro (gfx_pc.cpp) roda no 360
e interpreta todas as listas de desenho do jogo (centenas de milhares de triangulos em 30 s, sem queda).
Falta desenhar de verdade.

## Peças e referencia

| Peça | Referencia pronta |
|---|---|
| Interface a implementar | `midas/port/fast3d/gfx_rendering_api.h` (struct de ponteiros) |
| Gerador de shader (GLSL a partir de `CCFeatures`) | `gfx_opengl.cpp` do Midas, `create_and_load_new_shader` (linhas ~337-868), com as opcoes GE (FARCLIP, PIXFOG, SKY_PIXDIV, ALPHA_CVG_SEL...) |
| GLSL -> SPIR-V no console | glslang-xenon como biblioteca (`add_subdirectory`), como `libultraship-xenon/cmake/dependencies/xsdk.cmake` |
| SPIR-V -> microcodigo do Xenos | `xsdk/xsc.h` (xost), como `gfx_xsdk_shader.cpp` |
| Cache de shaders em disco | `gfx_xsdk_shader.cpp` (pasta em `game:/`) |
| Backend completo equivalente | `libultraship-xenon/src/graphic/Fast3D/backends/gfx_xsdk.cpp` (+ `_glsl`, `_shader`, `_window`) |

## Passos

1. Shader: portar o gerador do Midas para GLSL estilo Vulkan (layout/location, bloco uniforme, `texture()`),
   compilar com glslang+xsc, guardar em cache.
2. Texturas: `upload_texture` recebe RGBA32; usar `xsdk_texture` (tiled R8G8B8A8); amostradores (clamp/wrap/mirror,
   filtro) em `set_sampler_parameters`.
3. Estado: profundidade (`set_depth_mode`, `set_depth_range`), mistura/alpha (`set_use_alpha`), viewport/scissor.
4. Desenho: `draw_triangles` copia o vertex stream para um anel e emite o draw.
5. Framebuffers (`create_framebuffer`, `copy_framebuffer`...): alvo na EDRAM + resolve; o 0 e a tela.
6. Janela: apresentar o quadro (substitui o `w_*` fixo de `xenon_video.cpp`).

`retained_*` e `resolve_n64_aa` ficam NULL (o Fast3D cai no caminho simples).
