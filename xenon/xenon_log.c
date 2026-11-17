/* Log assincrono. O pendrive as vezes leva 2 a 4 s para uma gravacao pequena; como o SDK grava cada linha na hora, o
 * jogo inteiro parava junto. Aqui o texto vai para uma fila na RAM e uma thread propria grava em lotes. As duas funcoes
 * de log do SDK sao "embrulhadas" no link (-Wl,--wrap), entao printf/stderr e xsdk_log_write caem aqui sem mudar o
 * resto do codigo. */
#include <pthread.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "xenon_perf.h"

extern void __real_xsdk_log_write_bytes(const char *bytes, size_t length_bytes);

#define RING_BYTES (512 * 1024)

static char g_ring[RING_BYTES];
static char g_out[RING_BYTES];
static size_t g_len, g_dropped;
static volatile int g_async;
static pthread_mutex_t g_mu = PTHREAD_MUTEX_INITIALIZER;   /* a fila */
static pthread_mutex_t g_wmu = PTHREAD_MUTEX_INITIALIZER;  /* quem grava de verdade */
static pthread_t g_thread;
static volatile int g_run;

static void enqueue(const char *b, size_t n)
{
    if (!g_async) {
        __real_xsdk_log_write_bytes(b, n);
        return;
    }
    pthread_mutex_lock(&g_mu);
    if (g_len + n > RING_BYTES) {
        g_dropped += n;
    } else {
        memcpy(g_ring + g_len, b, n);
        g_len += n;
    }
    pthread_mutex_unlock(&g_mu);
}

void __wrap_xsdk_log_write_bytes(const char *bytes, size_t length_bytes)
{
    enqueue(bytes, length_bytes);
}

void __wrap_xsdk_log_write(const char *format, ...)
{
    char line[1024];
    va_list args;
    int n;
    va_start(args, format);
    n = vsnprintf(line, sizeof(line) - 2, format, args);
    va_end(args);
    if (n < 0) return;
    if ((size_t)n > sizeof(line) - 2) n = (int)sizeof(line) - 2;
    line[n++] = '\n';
    enqueue(line, (size_t)n);
}

/* Grava o que esta na fila. Pode demorar (e o pendrive que decide); por isso so a thread do log e o fim do jogo
 * chamam. */
static void drain(void)
{
    size_t n, dropped;
    pthread_mutex_lock(&g_wmu);
    pthread_mutex_lock(&g_mu);
    n = g_len;
    dropped = g_dropped;
    memcpy(g_out, g_ring, n);
    g_len = 0;
    g_dropped = 0;
    pthread_mutex_unlock(&g_mu);
    if (n) __real_xsdk_log_write_bytes(g_out, n);
    if (dropped) {
        char note[96];
        const int m = snprintf(note, sizeof(note), "xenon: log: %u bytes descartados (fila cheia)\n", (unsigned)dropped);
        if (m > 0) __real_xsdk_log_write_bytes(note, (size_t)m);
    }
    pthread_mutex_unlock(&g_wmu);
}

static void *log_thread(void *arg)
{
    struct timespec ts = { 0, 200 * 1000 * 1000 };
    (void)arg;
    while (g_run) {
        nanosleep(&ts, NULL);
        drain();
        xperf_flush_if_due();
    }
    return NULL;
}

/* Grava tudo agora e passa a gravar direto (falha, saida do jogo). */
void xlog_async_flush(void)
{
    if (!g_async) return;
    drain();
}

void xlog_async_sync(void)
{
    g_async = 0;
}

void xlog_async_start(void)
{
    if (g_async) return;
    g_run = 1;
    g_async = 1;
    if (pthread_create(&g_thread, NULL, log_thread, NULL) != 0) {
        g_async = 0;
        g_run = 0;
        return;
    }
    atexit(xlog_async_flush);
}
