/* Som do Midas no Xbox 360. O audio_pc.c do Midas emula o sintetizador do N64 e entrega s16 estereo a 22050 Hz
 * pela API de audio do SDL; aqui essa API (declarada em compat/SDL.h) vira uma fila circular, e uma thread
 * alimenta o xsdk_audio com quadros de 256 amostras, 6 canais, 48 kHz, em float (reamostrando por interpolacao). */
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <SDL.h>
#include <xsdk/audio.h>

#define RING_BYTES (64 * 1024)
#define IN_RATE 22050.0

static uint8_t g_ring[RING_BYTES];
static uint32_t g_head, g_count; /* leitura em g_head, g_count bytes guardados */
static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static volatile int g_open, g_run;
static pthread_t g_thread;
static uint32_t g_underruns;

static int ring_peek_frame(uint32_t index, int16_t *l, int16_t *r)
{
    /* amostra numero `index` (4 bytes cada) a partir da cabeca; 0 se ainda nao ha */
    if ((index + 1) * 4 > g_count) return 0;
    {
        uint32_t o = (g_head + index * 4) % RING_BYTES;
        uint8_t b[4];
        uint32_t k;
        for (k = 0; k < 4; k++) b[k] = g_ring[(o + k) % RING_BYTES];
        *l = (int16_t)(((uint16_t)b[0] << 8) | b[1]); /* AUDIO_S16SYS: o 360 e big-endian */
        *r = (int16_t)(((uint16_t)b[2] << 8) | b[3]);
    }
    return 1;
}

static void *feeder(void *arg)
{
    double pos = 0.0; /* posicao fracionaria (em amostras de entrada) dentro da fila */
    (void)arg;
    while (g_run) {
        float *frame;
        NTSTATUS st = xsdk_audio_next_frame(100, &frame);
        int n;
        if (st != 0) continue;
        memset(frame, 0, sizeof(float) * XSDK_AUDIO_FRAME_FLOATS);
        pthread_mutex_lock(&g_lock);
        for (n = 0; n < XSDK_AUDIO_FRAME_SAMPLES; n++) {
            uint32_t i = (uint32_t)pos;
            double f = pos - (double)i;
            int16_t l0, r0, l1, r1;
            if (!ring_peek_frame(i, &l0, &r0) || !ring_peek_frame(i + 1, &l1, &r1)) {
                g_underruns++;
                continue; /* fila vazia: silencio, e a posicao nao avanca */
            }
            frame[XSDK_AUDIO_FRONT_LEFT * XSDK_AUDIO_FRAME_SAMPLES + n] = (float)((l0 + (l1 - l0) * f) / 32768.0);
            frame[XSDK_AUDIO_FRONT_RIGHT * XSDK_AUDIO_FRAME_SAMPLES + n] = (float)((r0 + (r1 - r0) * f) / 32768.0);
            pos += IN_RATE / (double)XSDK_AUDIO_RATE_HZ;
        }
        {   /* descarta o que ja foi consumido */
            uint32_t used = (uint32_t)pos;
            if (used * 4 > g_count) used = g_count / 4;
            g_head = (g_head + used * 4) % RING_BYTES;
            g_count -= used * 4;
            pos -= (double)used;
        }
        pthread_mutex_unlock(&g_lock);
        xsdk_audio_queue_frame();
    }
    return NULL;
}

SDL_AudioDeviceID SDL_OpenAudioDevice(const char *name, int capture, const SDL_AudioSpec *want, SDL_AudioSpec *have, int changes)
{
    (void)name; (void)capture; (void)changes;
    if (g_open) return 1;
    if (xsdk_audio_open(0) != 0) {
        printf("ge: audio: xsdk_audio_open falhou\n");
        return 0;
    }
    if (have && want) *have = *want;
    g_head = g_count = 0;
    g_run = 1;
    if (pthread_create(&g_thread, NULL, feeder, NULL) != 0) {
        g_run = 0;
        xsdk_audio_close();
        return 0;
    }
    g_open = 1;
    printf("ge: audio: saida do 360 aberta (22050 Hz estereo -> 48 kHz)\n");
    return 1;
}

void SDL_CloseAudioDevice(SDL_AudioDeviceID dev)
{
    (void)dev;
    if (!g_open) return;
    g_run = 0;
    xsdk_audio_close();
    pthread_join(g_thread, NULL);
    g_open = 0;
}

int SDL_QueueAudio(SDL_AudioDeviceID dev, const void *data, uint32_t len)
{
    const uint8_t *p = (const uint8_t *)data;
    uint32_t k;
    (void)dev;
    pthread_mutex_lock(&g_lock);
    if (len > RING_BYTES - g_count) len = RING_BYTES - g_count;
    for (k = 0; k < len; k++) g_ring[(g_head + g_count + k) % RING_BYTES] = p[k];
    g_count += len;
    pthread_mutex_unlock(&g_lock);
    return 0;
}

uint32_t SDL_GetQueuedAudioSize(SDL_AudioDeviceID dev)
{
    uint32_t c;
    (void)dev;
    pthread_mutex_lock(&g_lock);
    c = g_count;
    pthread_mutex_unlock(&g_lock);
    return c;
}

uint32_t xenon_audio_underruns(void)
{
    return g_underruns;
}
