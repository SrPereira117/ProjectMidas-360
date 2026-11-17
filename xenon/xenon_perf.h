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
uint32_t xenon_audio_underruns(void);
#ifdef __cplusplus
}
#endif
#endif
