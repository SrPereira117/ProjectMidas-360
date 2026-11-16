#pragma once
/* Gerador de shaders do Midas para o Xenos: o mesmo codigo do gfx_opengl.cpp (create_and_load_new_shader),
 * reescrito em GLSL para Vulkan que o glslang aceita e o xost (compilador de shaders do xsdk) consegue
 * compilar: sem chamadas de funcao (so macros), sem textureSize (os tamanhos vem em um bloco de uniformes),
 * um atributo por posicao de local i+2 (TEXCOORD i) e interpoladores nos locais i. */
#include <cstdint>
#include <string>
#include <vector>

struct CCFeatures;

namespace xn {

struct Attribute {
    uint32_t offsetFloats; /* onde comeca, em floats, dentro do vertice */
    uint32_t floats;       /* quantos floats */
};

struct Glsl {
    std::string vertex;
    std::string fragment;
    std::vector<Attribute> attributes; /* depois da posicao (4 floats) */
    uint32_t vertexFloats;             /* floats por vertice, contando a posicao */
};

/* O bloco de uniformes (set 0, binding 0): vec4 texture_size[2]; vec4 frame; */
constexpr uint32_t BLOCK_FLOATS = 12;

Glsl BuildGlsl(const CCFeatures &features, bool threePointFiltering);

/* Programa que limpa a tela: posicao no local 0, preto opaco. */
extern const char *const CLEAR_VERTEX;
extern const char *const CLEAR_FRAGMENT;

/* Identifica a versao do gerador: muda quando o texto gerado muda, e invalida os shaders guardados no disco. */
std::string Templates();

} // namespace xn
