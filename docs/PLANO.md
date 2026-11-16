# Plano técnico

Base: Project Midas 1.0.2b (port de PC) + xsdk (SDK/compilador para `.xex`, GCC 15 para PowerPC).

## O que o Midas assume e o 360 não oferece

| Midas (PC)                                   | 360                                                        |
|----------------------------------------------|------------------------------------------------------------|
| x86 de 32 bits, little-endian                | PowerPC de 32 bits de ponteiro, big-endian                  |
| SDL2 (janela, áudio, controle)               | xsdk (`xsdk/gpu.h`, `audio.h`, `xam.h`, `kernel.h`)         |
| OpenGL (renderer Fast3D)                     | Backend `gfx_xsdk` do `libultraship-xenon` como referência  |
| `-msse2`, EBP-chain no crash handler, CPUID  | remover; usar `-ffp-contract=off` no PowerPC                |
| `.incbin` + `objcopy` dos assets             | não usar: build sem assets (`GE_ROMDATA_FALLBACK=OFF`)      |
| ROM em `Data/`                               | ROM ao lado do `.xex` (`game:`), mesma detecção por bytes   |

## Marcos (cada um termina com um log para eu ler)

1. **Mapa de compilação** (`midas-360-compile-check`): quais arquivos compilam com o GCC do xsdk. *(em andamento)*
2. **Núcleo compila e liga**: jogo + motor + libultra, sem vídeo, ainda sem rodar.
3. **ROM carrega**: o `.xex` acha a ROM no pendrive, confere e grava um log.
4. **Núcleo roda sem imagem**: o jogo avança frames, com log.
5. **Vídeo**: backend de renderização no Xenos; primeira tela (logos/menu).
6. **Controle e áudio**.
7. **Jogabilidade e ajustes** (desempenho, resolução, opções).

## Riscos conhecidos

- Endereços: o Fast3D separa ponteiro de host de referência a segmento por `>= 0x10000000`.
  Nos testes do xsdk no console os endereços precisam ser conferidos (item aberto).
- O port usa `uintptr_t` em campos de 32 bits: ok em ILP32, que é o caso do 360.
- Shaders do Fast3D são gerados em tempo de execução no PC; no 360 virão pré-compilados.
