# Project Midas 360 (GoldenEye 007 para Xbox 360)

Port do **GoldenEye 007** (N64) para o **Xbox 360**, feito a partir do
[Project Midas](https://github.com/murkantor/Project-Midas) (port de PC da decompilação do jogo) e do
[xsdk](https://github.com/xenosworks/xsdk) (SDK livre para gerar `.xex`).

Jogo completo: campanha, cutscenes, menus, áudio, FOV ajustável e widescreen. **Você precisa da sua própria ROM.**

## Como usar

1. Baixe o `ProjectMidas360.xex` (aba **Actions** → último `midas-360-build` → artefato `ProjectMidas360-xex`,
   ou a página de Releases quando houver).
2. No pendrive (ou HD), crie uma pasta, por exemplo `Goldeneye 007`, e coloque dentro:

```
Goldeneye 007\
  ProjectMidas360.xex
  GoldenEye 007 (USA).z64      <- a SUA ROM (qualquer nome, 12 MB)
```

3. Abra o `.xex` pelo Aurora/Freestyle (ou pelo seu método habitual de rodar homebrew).
4. Na primeira vez o jogo confere a ROM e cria sozinho, ao lado do `.xex`:
   `midas.ini` (configuração), `Data\shaders_base.txt` (shaders pré-compilados), `midas_xenon.log`/`ProjectMidas360.log`
   e `midas_perf.csv` (monitor de desempenho). A ROM pode estar em `.z64`, `.v64` ou `.n64`.

**ROM aceita:** GoldenEye 007 (USA), 12.582.912 bytes, CRC32 `B6330846` (versão NTSC-U 1.0). Outras versões não
funcionam.

### Controles e menus
- Segure **BACK por 2 s** para sair.
- Pause → **Audio Visual → Video** tem *Field of View* (70–140) e *Weapon FOV*.
- As opções do jogo ficam no `midas.ini`. Para voltar ao padrão, apague o arquivo.

### Se algo der errado
Mande o `ProjectMidas360.log` (e o `midas_perf.csv`) ao reportar um problema. Eles ficam na pasta do `.xex`.

## Compilar sem instalar nada
Faça um fork, vá em **Actions** → `midas-360-build` → *Run workflow*. O `.xex` sai como artefato. O build usa
o Project Midas + patches deste repositório (`xenon/patches`) e o xsdk.

## Legal
- **Este repositório não contém ROM nem arquivos do jogo.** Cada pessoa usa a própria cópia legal, que o programa
  confere pelo tamanho e CRC. Os dados (fases, modelos, sons, texturas) vêm da ROM; o build usa o modo do Midas sem
  cópia embutida (`GE_ROMDATA_FALLBACK=OFF`). Poucos trechos pequenos (algumas texturas e um logo) ficam no
  executável, como no Midas original.
- Não publique ROMs nem `.xex` junto com dados do jogo.
- Código novo daqui: MIT. O Midas, o xsdk e a decompilação mantêm as licenças deles (o jogo pertence aos detentores
  dos direitos).

## English (short)
GoldenEye 007 for Xbox 360, built from Project Midas (PC port of the N64 decompilation) and the open xsdk. Put
`ProjectMidas360.xex` and **your own** legal GoldenEye 007 (USA) ROM (12 MB, CRC32 B6330846) in the same folder on
a USB drive and launch it from Aurora/Freestyle. Hold BACK for 2 s to quit. No game data is in this repository.
