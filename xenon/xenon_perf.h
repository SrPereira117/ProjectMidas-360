#ifndef XENON_PERF_H
#define XENON_PERF_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Monitor de desempenho: escreve game:/midas_perf.csv (uma linha por segundo, gravada em
 * lotes a cada 20 s para nao castigar o pendrive) e mede a velocidade do pendrive. */
void xperf_init(void);
void xperf_usb_benchmark(void);                          /* leitura e escrita sequencial, 1 vez */
void xperf_note_rom_read(uint32_t bytes, double ms);      /* tempo real da leitura da ROM */
void xperf_frame_begin(void);
void xperf_frame_end(uint64_t draws, uint64_t tris, uint64_t uploads, uint64_t shaders);
void xperf_flush(void);
void xperf_flush_if_due(void);           /* chamado pela thread do log, fora do jogo */
void xlog_async_start(void);
void xlog_async_flush(void);
void xlog_async_sync(void);
/* tempo acumulado por categoria (0 submit, 1 present, 2 textura, 3 conversao de DL) em ms */
void xperf_acc(int id, double ms);
double xperf_now_ms(void);
void xperf_acc_ticks(int id, uint64_t ticks);   /* ticks do mftb */
void xperf_count(int id, int n);
void xperf_frame_reset(void);                   /* zera o acumulado do quadro (depois do log de pico) */
double xperf_frame_ms(int id);                  /* acumulado no quadro atual */
uint32_t xperf_frame_count(int id);
/* ids: 0 submit, 1 present, 2 upload textura, 3 gfx_run (Fast3D todo), 4 vertices, 5 triangulos (inclui flush
 * que ele dispara), 6 flush/draw, 7 audio (portAudioPump), 9 import_texture, 10 sorter XLU, 11 matriz */
uint32_t xenon_audio_underruns(void);
#ifdef __cplusplus
}
#endif
#endif
