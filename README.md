# Project Midas para Xbox 360 (em desenvolvimento)

Port do **GoldenEye 007** (N64) para o Xbox 360, feito a partir do
[Project Midas](https://github.com/murkantor/Project-Midas) (port de PC da decompilação do jogo) e do
[xsdk](https://github.com/xenosworks/xsdk) (SDK livre para gerar `.xex`).

> **Estado atual:** ainda **não roda**. Hoje o repositório só contém as ferramentas de build e o mapa de
> compilação. Veja `docs/PLANO.md`.

## Como vai funcionar (meta)

1. Você baixa o `.xex` gerado por este repositório (ou compila o seu pelo GitHub Actions, sem instalar nada).
2. Você coloca **a sua própria ROM de GoldenEye 007 (USA)** ao lado do `.xex`, no pendrive.
3. Roda pelo Aurora/Freestyle, com o seu exploit ou modificação habitual.

## Legal

- **Este repositório não contém ROM nem arquivos do jogo.** Cada pessoa usa a própria cópia legal.
- O build usa o modo do Midas em que o executável **não carrega os dados da Rare** (`GE_ROMDATA_FALLBACK=OFF`):
  tudo vem da ROM do usuário, que o jogo confere antes de iniciar.
- Não publique `.xex` compilados junto com dados do jogo, nem ROMs.
- O código novo daqui é MIT. O Midas, o xsdk e a decompilação mantêm as licenças deles
  (a decompilação do GoldenEye não tem licença; o jogo pertence aos detentores dos direitos).

## English (short)

GoldenEye 007 for Xbox 360, built from Project Midas (PC port of the N64 decompilation) and xsdk (open
Xbox 360 SDK). Work in progress: it does not run yet. You supply your own legal US ROM; no game data is
in this repository.
