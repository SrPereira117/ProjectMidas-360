#include "xenon_perf.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "xsdk/log.h"

#define BUF_BYTES (256 * 1024)
#define FLUSH_EVERY_S 20.0

static char g_buf[BUF_BYTES];
static size_t g_len;
static FILE *g_file;
static double g_t0, g_last_flush, g_flush_ms;

/* acumuladores do segundo atual */
static double g_sec_start, g_frame_begin, g_last_end;
static uint32_t g_n;
static double g_sum_ms, g_max_ms, g_sum_render_ms;
static uint64_t g_pd, g_pt, g_pu, g_ps;
static uint32_t g_pund;
static uint32_t g_rows;

static double now_s(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}

static void put(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static void put(const char *fmt, ...)
{
    va_list ap;
    int n;
    if (g_len + 256 > BUF_BYTES) return; /* cheio: descarta (flush a cada 20 s evita isso) */
    va_start(ap, fmt);
    n = vsnprintf(g_buf + g_len, 256, fmt, ap);
    va_end(ap);
    if (n > 0) g_len += (size_t)(n < 255 ? n : 255);
}

void xperf_flush(void)
{
    double t0;
    if (!g_file || g_len == 0) return;
    t0 = now_s();
    fwrite(g_buf, 1, g_len, g_file);
    fflush(g_file);
    g_flush_ms = (now_s() - t0) * 1000.0;
    g_len = 0;
    g_last_flush = now_s();
}

void xperf_init(void)
{
    g_file = fopen("game:/midas_perf.csv", "wb");
    g_t0 = g_sec_start = g_last_flush = now_s();
    if (!g_file) {
        xsdk_log_write("xenon: perf: nao consegui criar midas_perf.csv");
        return;
    }
    put("# midas_perf.csv: uma linha por segundo\n");
    put("t_s,quadros,fps,ms_medio,ms_max,ms_render_medio,desenhos,triangulos,texturas_novas,shaders_novos,"
        "audio_underruns,flush_pendrive_ms\n");
    xperf_flush();
}

void xperf_note_rom_read(uint32_t bytes, double ms)
{
    const double mbs = ms > 0.0 ? ((double)bytes / 1048576.0) / (ms / 1000.0) : 0.0;
    xsdk_log_write("xenon: perf: ROM lida em %.0f ms (%.1f MB/s)", ms, mbs);
    put("# rom_leitura,%u bytes,%.0f ms,%.1f MB/s\n", bytes, ms, mbs);
}

void xperf_usb_benchmark(void)
{
    const size_t SZ = 4u * 1024u * 1024u;
    unsigned char *b = (unsigned char *)malloc(SZ);
    FILE *f;
    double t0, w_ms = 0.0, r_ms = 0.0;
    size_t i;
    if (!b) return;
    for (i = 0; i < SZ; i++) b[i] = (unsigned char)(i * 131u + (i >> 8));
    t0 = now_s();
    f = fopen("game:/midas_iotest.tmp", "wb");
    if (f) {
        for (i = 0; i < SZ; i += 65536) fwrite(b + i, 1, 65536, f);
        fflush(f);
        fclose(f);
        w_ms = (now_s() - t0) * 1000.0;
    }
    t0 = now_s();
    f = fopen("game:/midas_iotest.tmp", "rb");
    if (f) {
        for (i = 0; i < SZ; i += 65536) if (fread(b + i, 1, 65536, f) != 65536) break;
        fclose(f);
        r_ms = (now_s() - t0) * 1000.0;
    }
    remove("game:/midas_iotest.tmp");
    free(b);
    xsdk_log_write("xenon: perf: pendrive escrita 4 MB em %.0f ms (%.1f MB/s), leitura em %.0f ms (%.1f MB/s)",
                   w_ms, w_ms > 0 ? 4000.0 / w_ms : 0.0, r_ms, r_ms > 0 ? 4000.0 / r_ms : 0.0);
    put("# pendrive_escrita_4MB,%.0f ms,%.1f MB/s\n", w_ms, w_ms > 0 ? 4000.0 / w_ms : 0.0);
    put("# pendrive_leitura_4MB,%.0f ms,%.1f MB/s\n", r_ms, r_ms > 0 ? 4000.0 / r_ms : 0.0);
    xperf_flush();
}

void xperf_frame_begin(void)
{
    g_frame_begin = now_s();
}

void xperf_frame_end(uint64_t draws, uint64_t tris, uint64_t uploads, uint64_t shaders)
{
    const double t = now_s();
    double ms;
    if (g_last_end > 0.0) {
        ms = (t - g_last_end) * 1000.0;
        g_sum_ms += ms;
        if (ms > g_max_ms) g_max_ms = ms;
        g_n++;
    }
    if (g_frame_begin > 0.0) g_sum_render_ms += (t - g_frame_begin) * 1000.0;
    g_last_end = t;
    if (t - g_sec_start >= 1.0) {
        const double span = t - g_sec_start;
        const uint32_t und = xenon_audio_underruns();
        if (g_n > 0) {
            put("%.0f,%u,%.1f,%.2f,%.1f,%.2f,%llu,%llu,%llu,%llu,%u,%.0f\n", t - g_t0, g_n, (double)g_n / span,
                g_sum_ms / g_n, g_max_ms, g_sum_render_ms / g_n, (unsigned long long)(draws - g_pd),
                (unsigned long long)(tris - g_pt), (unsigned long long)(uploads - g_pu),
                (unsigned long long)(shaders - g_ps), und - g_pund, g_flush_ms);
            g_flush_ms = 0.0;
            g_rows++;
        }
        g_pd = draws; g_pt = tris; g_pu = uploads; g_ps = shaders; g_pund = und;
        g_n = 0; g_sum_ms = g_max_ms = g_sum_render_ms = 0.0;
        g_sec_start = t;
    }
    if (t - g_last_flush >= FLUSH_EVERY_S) xperf_flush();
}
