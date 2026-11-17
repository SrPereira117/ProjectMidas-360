# Changelog

## 1.0.0
Primeira versão completa jogável de ponta a ponta.

- Campanha completa, menus, cutscenes e áudio funcionando no Xbox 360.
- ROM própria do usuário, conferida por CRC; o `run.txt` não é mais necessário.
- Corte de geometria perto da câmera feito corretamente: acabaram as paredes transparentes e os modelos que sumiam
  perto de escadas, paredes e chão.
- Céu, névoa e cores corrigidos (tabelas big-endian).
- Memória reorganizada (blocos de 8 MB): fim das corrupções e travamentos na fase Train e em fases grandes.
- Correção do fechamento ao explodir o laboratório (Facility).
- Log assíncrono: escrever no pendrive não congela mais o jogo.
- Menu com *Field of View* e *Weapon FOV*.
- Shaders pré-compilados na abertura: sem travada de ~1 s no meio da fase.
- Config de vídeo padrão (FOV 100, widescreen, vsync desligado, sem limite de FPS).
- Monitor de desempenho `midas_perf.csv` (FPS, tempos de render, velocidade do pendrive).
- O executável agora se chama `ProjectMidas360.xex`.
