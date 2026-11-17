#include "xenon_perf.h"
#include <stdarg.h>
#include <pthread.h>
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
static pthread_mutex_t g_mu = PTHREAD_MUTEX_INITIALIZER;
static volatile int g_flush_due;
static char g_copy[BUF_BYTES];

/* acumuladores do segundo atual */
static double g_sec_start, g_frame_begin, g_last_end;
static uint32_t g_n;
static double g_sum_ms, g_max_ms, g_sum_render_ms;
static uint64_t g_pd, g_pt, g_pu, g_ps;
static uint32_t g_pund;
static uint32_t g_rows;
#define NACC 16
static double g_acc[NACC];        /* ms acumulados no segundo */
static double g_frm[NACC];        /* ms acumulados no quadro atual (para o log de picos) */
static uint32_t g_cnt[4];         /* contadores do segundo (0 = triangulos cortados no plano proximo) */
static uint32_t g_frm_cnt[4];
static double g_tb_hz = 49875000.0;   /* relogio mftb do Xenon; recalibrado a cada segundo */
static uint64_t g_tb_prev;
static double g_tb_prev_s;
static uint64_t tb_now(void) { uint64_t v; __asm__ volatile("mftb %0" : "=r"(v)); return v; }

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
    pthread_mutex_lock(&g_mu);
    if (g_len + 256 > BUF_BYTES) { /* cheio: descarta (flush a cada 20 s evita isso) */
        pthread_mutex_unlock(&g_mu);
        return;
    }
    va_start(ap, fmt);
    n = vsnprintf(g_buf + g_len, 256, fmt, ap);
    va_end(ap);
    if (n > 0) g_len += (size_t)(n < 255 ? n : 255);
    pthread_mutex_unlock(&g_mu);
}

void xperf_flush(void)
{
    double t0;
    size_t n;
    if (!g_file) return;
    pthread_mutex_lock(&g_mu);
    n = g_len;
    memcpy(g_copy, g_buf, n);
    g_len = 0;
    pthread_mutex_unlock(&g_mu);
    if (n == 0) return;
    t0 = now_s();
    fwrite(g_copy, 1, n, g_file);
    fflush(g_file);
    g_flush_ms = (now_s() - t0) * 1000.0;
    g_last_flush = now_s();
}

void xperf_flush_if_due(void)
{
    if (g_flush_due) {
        g_flush_due = 0;
        xperf_flush();
    }
}

/* Amostrador de secoes: o jogo marca em ge_prof_sec em que trecho do Fast3D esta; esta thread le o marcador
 * ~a cada 100 us e conta. A cada 10 s grava "# prof" no CSV com a porcentagem por secao. */
volatile int ge_prof_sec;
static uint32_t g_prof_hist[32];
static void *prof_thread(void *arg)
{
    double last = now_s();
    (void)arg;
    for (;;) {
        struct timespec ts = {0, 100000};
        int sec;
        nanosleep(&ts, NULL);
        sec = ge_prof_sec;
        if (sec < 0 || sec > 31) sec = 31;
        g_prof_hist[sec]++;
        if (now_s() - last >= 10.0) {
            uint32_t tot = 0;
            char line[400];
            int n = 0, i;
            for (i = 0; i < 32; i++) tot += g_prof_hist[i];
            n = snprintf(line, sizeof line, "# prof t=%.0f amostras=%u", now_s() - g_t0, tot);
            for (i = 0; i < 32 && tot; i++) {
                if (g_prof_hist[i] * 200 >= tot && n < 215)   /* >= 0,5% */
                    n += snprintf(line + n, sizeof line - n, " s%d=%.1f%%", i, 100.0 * g_prof_hist[i] / tot);
                g_prof_hist[i] = 0;
            }
            put("%s\n", line);
            last = now_s();
        }
    }
    return NULL;
}

void xperf_init(void)
{
    {
        pthread_t th;
        if (pthread_create(&th, NULL, prof_thread, NULL) == 0) pthread_detach(th);
    }
    g_file = fopen("game:/midas_perf.csv", "wb");
    g_t0 = g_sec_start = g_last_flush = now_s();
    if (!g_file) {
        xsdk_log_write("xenon: perf: nao consegui criar midas_perf.csv");
        return;
    }
    put("# midas_perf.csv: uma linha por segundo\n");
    put("t_s,quadros,fps,ms_medio,ms_max,ms_render_medio,desenhos,triangulos,texturas_novas,shaders_novos,"
        "audio_underruns,flush_pendrive_ms,ms_submit,ms_present,ms_textura,"
        "ms_fast3d,ms_vertices,ms_triangulos,ms_flush_draw,ms_audio,ms_import_tex,ms_sorter_xlu,ms_matriz,tris_cortados_perto\n");
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
            put("%.0f,%u,%.1f,%.2f,%.1f,%.2f,%llu,%llu,%llu,%llu,%u,%.0f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%u\n", t - g_t0, g_n, (double)g_n / span,
                g_sum_ms / g_n, g_max_ms, g_sum_render_ms / g_n, (unsigned long long)(draws - g_pd),
                (unsigned long long)(tris - g_pt), (unsigned long long)(uploads - g_pu),
                (unsigned long long)(shaders - g_ps), und - g_pund, g_flush_ms, g_acc[0] / g_n, g_acc[1] / g_n,
                g_acc[2] / g_n, g_acc[3] / g_n, g_acc[4] / g_n, g_acc[5] / g_n, g_acc[6] / g_n, g_acc[7] / g_n,
                g_acc[9] / g_n, g_acc[10] / g_n, g_acc[11] / g_n, g_cnt[0]);
            for (int i = 0; i < NACC; i++) g_acc[i] = 0.0;
            for (int i = 0; i < 4; i++) g_cnt[i] = 0;
            g_flush_ms = 0.0;
            g_rows++;
        }
        {   /* recalibra o relogio da CPU */
            const uint64_t tb = tb_now();
            if (g_tb_prev && span > 0.5) g_tb_hz = (double)(tb - g_tb_prev) / (t - g_tb_prev_s);
            g_tb_prev = tb; g_tb_prev_s = t;
        }
        g_pd = draws; g_pt = tris; g_pu = uploads; g_ps = shaders; g_pund = und;
        g_n = 0; g_sum_ms = g_max_ms = g_sum_render_ms = 0.0;
        g_sec_start = t;
    }
    if (t - g_last_flush >= FLUSH_EVERY_S) g_flush_due = 1;
}

void xperf_acc(int id, double ms)
{
    if (id >= 0 && id < NACC) { g_acc[id] += ms; g_frm[id] += ms; }
}

void xperf_acc_ticks(int id, uint64_t ticks)
{
    xperf_acc(id, (double)ticks * 1000.0 / g_tb_hz);
}

void xperf_count(int id, int n)
{
    if (id >= 0 && id < 4) { g_cnt[id] += (uint32_t)n; g_frm_cnt[id] += (uint32_t)n; }
}

void xperf_frame_reset(void)
{
    for (int i = 0; i < NACC; i++) g_frm[i] = 0.0;
    for (int i = 0; i < 4; i++) g_frm_cnt[i] = 0;
}

double xperf_frame_ms(int id) { return (id >= 0 && id < NACC) ? g_frm[id] : 0.0; }
uint32_t xperf_frame_count(int id) { return (id >= 0 && id < 4) ? g_frm_cnt[id] : 0; }

double xperf_now_ms(void)
{
    return now_s() * 1000.0;
}
