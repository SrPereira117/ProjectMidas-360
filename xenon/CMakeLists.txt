# Project Midas (GoldenEye 007) para o Xbox 360, construido com o xsdk.
#
#   cmake -S xenon -B build-midas -G Ninja \
#         -DCMAKE_TOOLCHAIN_FILE=<xsdk>/cmake/xsdk.cmake -DXSDK_TOOLCHAIN=<prefixo> -DMIDAS_DIR=<Project-Midas>
#
# Versao "sem video e sem audio" (o modo GE_HEADLESS do Midas) enquanto a camada
# de plataforma do 360 nao existe. Sem GE_ROMDATA_FALLBACK: o executavel nao carrega
# os dados grandes do jogo; eles vem da ROM do usuario.
cmake_minimum_required(VERSION 3.25)
project(midas_xenon C CXX)

set(MIDAS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../midas" CACHE PATH "Pasta do Project Midas")
get_filename_component(MIDAS_DIR "${MIDAS_DIR}" ABSOLUTE)
if(NOT EXISTS "${MIDAS_DIR}/CMakeLists.txt")
    message(FATAL_ERROR "Project Midas nao encontrado em ${MIDAS_DIR}")
endif()

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_EXTENSIONS ON)

set(MIDAS_DEFS VERSION_US LANG_US REFRESH_NTSC LEFTOVERDEBUG LEFTOVERSPECTRUM BUGFIX_R0
               NONMATCHING AVOID_UB _LANGUAGE_C GE_VIDEO=1)
# Flags do Midas, menos o que e x86 (-m32, -msse2, frame pointer do crash handler).
# -ffp-contract=off: sem fused multiply-add, para os floats arredondarem como no N64.
set(MIDAS_CFLAGS -ffreestanding -fms-extensions -fno-strict-aliasing -fwrapv -funsigned-char
                 -fcommon -ffp-contract=off -O2 -w
                 -Wno-error=implicit-function-declaration -Wno-error=incompatible-pointer-types
                 -Wno-error=int-conversion -Wno-error=implicit-int -Wno-error=return-mismatch)
set(MIDAS_INCS ${MIDAS_DIR} ${MIDAS_DIR}/include ${MIDAS_DIR}/include/ultra64 ${MIDAS_DIR}/include/PR
               ${MIDAS_DIR}/src ${MIDAS_DIR}/src/game ${MIDAS_DIR}/src/inflate
               ${MIDAS_DIR}/port/include ${MIDAS_DIR}/src/libultra)

# Carimbo de versao (o Midas gera isto com um script de git; aqui e fixo).
file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/generated)
string(TIMESTAMP BUILD_DAY "%Y%m%d")
file(WRITE ${CMAKE_BINARY_DIR}/generated/port_buildinfo.h
"#ifndef PORT_BUILDINFO_H
#define PORT_BUILDINFO_H
#define PORT_BUILD_NUMBER 0
#define PORT_BUILD_DATE   \"${BUILD_DAY}\"
#define PORT_BUILD_STRING \"xenon-${BUILD_DAY}\"
#define PORT_BUILD_COMMIT \"1.0.2b\"
#define PORT_BUILD_BRANCH \"xenon\"
#endif
")

# --- Fontes (mesma selecao do CMakeLists do Midas) ---------------------------------
file(GLOB GAME_SRC CONFIGURE_DEPENDS ${MIDAS_DIR}/src/game/*.c)
file(GLOB ENGINE_SRC_ALL CONFIGURE_DEPENDS ${MIDAS_DIR}/src/*.c)
set(ENGINE_EXCLUDE boot tlb_manage tlb_hardwire rmon usb debug crash motor)
set(ENGINE_SRC)
foreach(f ${ENGINE_SRC_ALL})
    get_filename_component(base ${f} NAME_WE)
    if(NOT base IN_LIST ENGINE_EXCLUDE)
        list(APPEND ENGINE_SRC ${f})
    endif()
endforeach()

file(READ ${MIDAS_DIR}/CMakeLists.txt MIDAS_CMAKE_TEXT)
string(REGEX MATCHALL "src/libultra(re)?/[A-Za-z0-9_/]+\\.c" GU_SRC_REL "${MIDAS_CMAKE_TEXT}")
list(REMOVE_DUPLICATES GU_SRC_REL)
set(GU_SRC)
foreach(f ${GU_SRC_REL})
    list(APPEND GU_SRC ${MIDAS_DIR}/${f})
endforeach()

file(GLOB PORT_SRC CONFIGURE_DEPENDS ${MIDAS_DIR}/port/src/*.c)
# Sem SDL por enquanto: video, audio e controle entram em outra etapa.
list(REMOVE_ITEM PORT_SRC
    ${MIDAS_DIR}/port/src/audio_pc.c ${MIDAS_DIR}/port/src/headless_stubs.c
    ${MIDAS_DIR}/port/src/input_pc.c ${MIDAS_DIR}/port/src/raphnet.c)

set(LOOSE_SRC ${MIDAS_DIR}/assets/oddtextures.c ${MIDAS_DIR}/assets/rarewarelogo.c
              ${MIDAS_DIR}/assets/font_dl.c)

# --- Os 3 modelos de menu que ficam dentro do executavel ----------------------------
set(KEEP_SYMS PlegalpageZ PnintendologoZ PgoldeneyelogoZ)
set(ASSET_OBJ_DIR ${CMAKE_BINARY_DIR}/asset_obj)
file(MAKE_DIRECTORY ${ASSET_OBJ_DIR})
set(ASSET_C_FLAGS -std=gnu11 -mcpu=cell -fms-extensions -ffreestanding -fno-strict-aliasing -fwrapv
                  -funsigned-char -fno-common -fno-zero-initialized-in-bss -fno-toplevel-reorder
                  -fno-pic -ffp-contract=off -Og -w)
foreach(d ${MIDAS_DEFS})
    list(APPEND ASSET_C_FLAGS -D${d})
endforeach()
foreach(i ${MIDAS_INCS})
    list(APPEND ASSET_C_FLAGS -I${i})
endforeach()
set(MIDAS_OBJCOPY ${XSDK_PREFIX}objcopy)
set(KEEP_OBJS)

function(port_add_asset_direct src sym)
    if(NOT sym IN_LIST KEEP_SYMS)
        return()
    endif()
    # Esses modelos nao estao no repositorio do Midas: sao extraidos da ROM do usuario na hora de compilar.
    if(NOT EXISTS ${MIDAS_DIR}/${src})
        message(STATUS "asset ${sym}: ${src} ausente (vem da ROM), pulando")
        return()
    endif()
    set(obj ${ASSET_OBJ_DIR}/${sym}.o)
    add_custom_command(OUTPUT ${obj}
        COMMAND ${CMAKE_C_COMPILER} ${ASSET_C_FLAGS} -c ${MIDAS_DIR}/${src} -o ${obj}.raw
        COMMAND ${MIDAS_OBJCOPY} -G ${sym} ${obj}.raw ${obj}
        DEPENDS ${MIDAS_DIR}/${src} VERBATIM)
    set_property(GLOBAL APPEND PROPERTY KEEP_OBJS_PROP ${obj})
endfunction()

function(port_add_asset_seg src sym)
    if(NOT sym IN_LIST KEEP_SYMS)
        return()
    endif()
    # Esses modelos nao estao no repositorio do Midas: sao extraidos da ROM do usuario na hora de compilar.
    if(NOT EXISTS ${MIDAS_DIR}/${src})
        message(STATUS "asset ${sym}: ${src} ausente (vem da ROM), pulando")
        return()
    endif()
    set(obj ${ASSET_OBJ_DIR}/${sym}.o)
    add_custom_command(OUTPUT ${obj}
        COMMAND ${CMAKE_C_COMPILER} ${ASSET_C_FLAGS} -c ${MIDAS_DIR}/${src} -o ${obj}.raw
        COMMAND ${MIDAS_OBJCOPY} -G ${sym} --add-symbol ${sym}=.data:0,global,object ${obj}.raw ${obj}
        DEPENDS ${MIDAS_DIR}/${src} VERBATIM)
    set_property(GLOBAL APPEND PROPERTY KEEP_OBJS_PROP ${obj})
endfunction()

# O manifesto do Midas chama estas duas funcoes para cada asset; so as 3 acima geram objeto.
include(${MIDAS_DIR}/port/generated/asset_manifest.cmake)
get_property(KEEP_OBJS GLOBAL PROPERTY KEEP_OBJS_PROP)
set_source_files_properties(${KEEP_OBJS} PROPERTIES EXTERNAL_OBJECT TRUE GENERATED TRUE)

# --- O desenhista (Fast3D em C++) numa biblioteca propria -------------------------------
# Sem a pasta src/ nos includes: o src/sched.h do jogo esconderia o <sched.h> do sistema.
set(CMAKE_CXX_STANDARD 20)
add_library(midas_gfx STATIC ${MIDAS_DIR}/port/fast3d/gfx_pc.cpp ${MIDAS_DIR}/port/fast3d/gfx_cc.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/xenon_video.cpp)
target_compile_definitions(midas_gfx PRIVATE ${MIDAS_DEFS})
target_compile_options(midas_gfx PRIVATE -fno-strict-aliasing -fwrapv -funsigned-char -ffp-contract=off -O2 -w)
target_include_directories(midas_gfx PRIVATE ${MIDAS_DIR} ${MIDAS_DIR}/include ${MIDAS_DIR}/include/ultra64
    ${MIDAS_DIR}/include/PR ${MIDAS_DIR}/port/include ${MIDAS_DIR}/port/fast3d
    ${CMAKE_CURRENT_SOURCE_DIR}/compat ${CMAKE_BINARY_DIR}/generated ${MIDAS_DIR}/src/game)

# --- O programa ---------------------------------------------------------------------
add_executable(midas_xenon ${GAME_SRC} ${ENGINE_SRC} ${GU_SRC} ${PORT_SRC}
               ${MIDAS_DIR}/src/libultrare/io/vitbl.c ${LOOSE_SRC} ${KEEP_OBJS}
               ${CMAKE_CURRENT_SOURCE_DIR}/xenon_stubs.c ${CMAKE_CURRENT_SOURCE_DIR}/xenon_input.c ${CMAKE_CURRENT_SOURCE_DIR}/xenon_main.c)
target_compile_definitions(midas_xenon PRIVATE ${MIDAS_DEFS})
target_compile_options(midas_xenon PRIVATE ${MIDAS_CFLAGS})
target_include_directories(midas_xenon PRIVATE ${MIDAS_INCS}
    ${CMAKE_CURRENT_SOURCE_DIR}/compat ${CMAKE_BINARY_DIR}/generated)
# A strtol do jogo (src/str.c) colide com a da newlib; as chamadas passam a usar a da newlib,
# que se comporta igual para esses usos (argumentos de linha de comando e stan.c).
set_source_files_properties(${MIDAS_DIR}/src/str.c PROPERTIES COMPILE_DEFINITIONS "strtol=midas_strtol")
# A main() do Midas vira midas_main(); a nossa (xenon_main.c) acha a ROM e chama a dele.
set_source_files_properties(${MIDAS_DIR}/port/src/main.c PROPERTIES COMPILE_DEFINITIONS "main=midas_main")
target_link_libraries(midas_xenon midas_gfx midas_gfx m)
xsdk_add_xex(midas_xenon TITLE_ID 0x47454D44 NAME "GoldenEye Midas" VERSION 0.0.1.0)
