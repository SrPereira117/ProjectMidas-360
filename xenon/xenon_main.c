/* Ponto de entrada do GoldenEye (Project Midas) no Xbox 360.
 *
 * Passo atual: achar a ROM do usuario ao lado do .xex, conferir e gravar o resultado no log
 * (<nome do xex>.log, na mesma pasta). Se existir um arquivo "run.txt" ao lado do .xex, depois
 * disso o nucleo do jogo e iniciado (ainda sem video nem audio).
 *
 * A main() do Midas (port/src/main.c) foi renomeada para midas_main() na compilacao. */
#include <dirent.h>
#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <time.h>
#include "xenon_perf.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "xsdk/log.h"
#include "xsdk/thread.h"

/* Depois de pthread.h/xsdk/thread.h a macro errno some neste toolchain; __errno() e o que ela expande. */
extern int *__errno(void);
#define XERRNO (*__errno())

extern int midas_main(int argc, char **argv);

#define ROM_SIZE   0xC00000u    /* 12.582.912 bytes: a cartucho (U) original */
#define ROM_CRC32  0xB6330846u  /* crc32 da imagem inteira, em ordem z64 */


/* ---- captador de falhas ------------------------------------------------------------------
 * O xsdk ainda nao tem um. Usa RtlSetVectoredExceptionHandler (docs/HARDWARE.md do xsdk, "How the
 * kernel hands a title its faults"): r3 aponta para {registro da excecao, contexto}. Grava no log o
 * tipo, o endereco da instrucao, o endereco acessado, os registradores e a pilha; depois encerra. */
extern uint32_t RtlSetVectoredExceptionHandler(void *first, void *handler);
extern void XamLoaderTerminateTitle(void);

static int in_fault;

static uint32_t rd32(const void *base, uint32_t off)
{
    return *(const uint32_t *)((const uint8_t *)base + off);
}

static int fault_handler(void **ptrs)
{
    const void *rec, *ctx;
    uint32_t i, sp, n;

    if (in_fault) XamLoaderTerminateTitle();
    in_fault = 1;
    xlog_async_flush();
    xlog_async_sync();
    rec = ptrs[0];
    ctx = ptrs[1];
    xsdk_log_write("FALHA: codigo %08X, instrucao em %08X, %u parametros",
                   rd32(rec, 0), rd32(rec, 0xC), rd32(rec, 0x10));
    if (rd32(rec, 0x10) >= 2)
        xsdk_log_write("FALHA: %s no endereco %08X",
                       rd32(rec, 0x14) ? "escrita" : "leitura", rd32(rec, 0x18));
    xsdk_log_write("FALHA: IAR=%08X LR=%08X CTR=%08X CR=%08X XER=%08X",
                   rd32(ctx, 0x8), rd32(ctx, 0xC), rd32(ctx, 0x14), rd32(ctx, 0x118), rd32(ctx, 0x11C));
    for (i = 0; i < 32; i += 4)
        xsdk_log_write("FALHA: r%u=%08X r%u=%08X r%u=%08X r%u=%08X",
                       i, rd32(ctx, 0x1C + 8 * i), i + 1, rd32(ctx, 0x1C + 8 * (i + 1)),
                       i + 2, rd32(ctx, 0x1C + 8 * (i + 2)), i + 3, rd32(ctx, 0x1C + 8 * (i + 3)));
    sp = rd32(ctx, 0x1C + 8);
    for (n = 0; n < 24; n++) {
        uint32_t next = *(const uint32_t *)(uintptr_t)sp;
        if (next <= sp || next - sp > 0x1000000u || (next & 3)) break;
        xsdk_log_write("FALHA: pilha %2u: retorno %08X (sp %08X)", n, *(const uint32_t *)(uintptr_t)(next + 4), next);
        sp = next;
    }
    xsdk_log_write("FALHA: fim do relatorio");
    XamLoaderTerminateTitle();
    return 0;
}

static uint32_t crc32_table[256];

static void crc32_init(void)
{
    uint32_t i, j, c;
    for (i = 0; i < 256; i++) {
        c = i;
        for (j = 0; j < 8; j++)
            c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
        crc32_table[i] = c;
    }
}

static uint32_t crc32_of(const uint8_t *p, size_t n)
{
    uint32_t c = 0xFFFFFFFFu;
    size_t i;
    for (i = 0; i < n; i++)
        c = crc32_table[(c ^ p[i]) & 0xFF] ^ (c >> 8);
    return c ^ 0xFFFFFFFFu;
}

/* Procura uma imagem do tamanho certo em 'dir'. Devolve 1 e o caminho em 'found'. */
static int scan_dir(const char *dir, char *found, size_t found_size)
{
    DIR *d = opendir(dir);
    struct dirent *e;
    int seen = 0, hit = 0;

    if (!d) {
        xsdk_log_write("xenon: pasta %s: nao abre (errno %d)", dir, XERRNO);
        return 0;
    }
    xsdk_log_write("xenon: pasta %s:", dir);
    while ((e = readdir(d)) != NULL) {
        char path[512];
        struct stat st;
        if (strcmp(e->d_name, ".") == 0 || strcmp(e->d_name, "..") == 0)
            continue;
        {
            size_t dl = strlen(dir);
            snprintf(path, sizeof(path), "%s%s%s", dir, (dl && dir[dl - 1] == '/') ? "" : "/", e->d_name);
        }
        if (stat(path, &st) != 0)
            continue;
        if (seen++ < 30)
            xsdk_log_write("xenon:   %s  (%lld bytes)", e->d_name, (long long)st.st_size);
        if (!hit && S_ISREG(st.st_mode) && (uint32_t)st.st_size == ROM_SIZE) {
            snprintf(found, found_size, "%s", path);
            hit = 1;
        }
    }
    closedir(d);
    return hit;
}

/* Le a imagem e a coloca em ordem z64 (big-endian). Devolve NULL se falhar. */
static uint8_t *load_rom(const char *path, const char **format)
{
    uint8_t *buf = malloc(ROM_SIZE);
    FILE *f;
    size_t got, i;

    if (!buf) {
        xsdk_log_write("xenon: sem memoria para a ROM (%u bytes)", (unsigned)ROM_SIZE);
        return NULL;
    }
    f = fopen(path, "rb");
    if (!f) {
        xsdk_log_write("xenon: nao consegui abrir %s (errno %d)", path, XERRNO);
        free(buf);
        return NULL;
    }
    {
        struct timespec a, b;
        clock_gettime(CLOCK_MONOTONIC, &a);
        got = fread(buf, 1, ROM_SIZE, f);
        clock_gettime(CLOCK_MONOTONIC, &b);
        xperf_note_rom_read((uint32_t)got, ((double)(b.tv_sec - a.tv_sec) + (double)(b.tv_nsec - a.tv_nsec) * 1e-9) * 1000.0);
    }
    fclose(f);
    if (got != ROM_SIZE) {
        xsdk_log_write("xenon: leitura curta: %u de %u bytes", (unsigned)got, (unsigned)ROM_SIZE);
        free(buf);
        return NULL;
    }
    if (buf[0] == 0x80 && buf[1] == 0x37 && buf[2] == 0x12 && buf[3] == 0x40) {
        *format = "z64 (big-endian)";
    } else if (buf[0] == 0x37 && buf[1] == 0x80 && buf[2] == 0x40 && buf[3] == 0x12) {
        *format = "v64 (bytes trocados)";
        for (i = 0; i + 1 < ROM_SIZE; i += 2) { uint8_t t = buf[i]; buf[i] = buf[i + 1]; buf[i + 1] = t; }
    } else if (buf[0] == 0x40 && buf[1] == 0x12 && buf[2] == 0x37 && buf[3] == 0x80) {
        *format = "n64 (palavras trocadas)";
        for (i = 0; i + 3 < ROM_SIZE; i += 4) {
            uint8_t a = buf[i], b = buf[i + 1];
            buf[i] = buf[i + 3]; buf[i + 1] = buf[i + 2]; buf[i + 2] = b; buf[i + 3] = a;
        }
    } else {
        xsdk_log_write("xenon: formato desconhecido (primeiros bytes %02X %02X %02X %02X)",
                       buf[0], buf[1], buf[2], buf[3]);
        free(buf);
        return NULL;
    }
    return buf;
}

/* O Midas pede 16 MB de pilha no Windows (percursos recursivos de salas/portais estouram 1 MB).
 * A thread principal do 360 so tem 1 MB, entao o nucleo roda numa thread propria. */
#define MIDAS_STACK_BYTES (16u * 1024u * 1024u)

typedef struct {
    int argc;
    char **argv;
    int ret;
} MidasRun;

static void *midas_thread(void *p)
{
    MidasRun *r = p;
    r->ret = midas_main(r->argc, r->argv);
    return NULL;
}

/* Nesta fase o jogo roda sem video: sem isto ele ficaria rodando para sempre. Passado o tempo, a
 * thread grava no log e encerra o programa, devolvendo o console ao dashboard. */
static int g_run_seconds;

extern void xenon_video_request_stop(void);
static void *watchdog(void *arg)
{
    (void)arg;
    (void)xsdk_thread_sleep((uint32_t)g_run_seconds * 1000);
    xsdk_log_write("xenon: tempo limite de %d s (run.txt): encerrando", g_run_seconds);
    xenon_video_request_stop(); /* o proximo fim de quadro fecha a GPU e sai */
    (void)xsdk_thread_sleep(4000);
    exit(0);
    return NULL;
}

int main(int argc, char **argv)
{
    static const char *const dirs[] = { "game:/", "game:/Data" };
    char found[512] = "";
    const char *format = "";
    uint8_t *rom;
    uint32_t crc;
    char name[21];
    FILE *flag;
    unsigned i;
    int ok = 0;

    xsdk_log_write("xenon: GoldenEye (Project Midas) para Xbox 360, build %s %s", __DATE__, __TIME__);
    xlog_async_start();
    xperf_init();
    xperf_usb_benchmark();
    xsdk_log_write("xenon: argc=%d argv[0]=%s", argc, argc > 0 && argv[0] ? argv[0] : "(nulo)");
    crc32_init();

    for (i = 0; i < sizeof(dirs) / sizeof(dirs[0]) && !found[0]; i++)
        scan_dir(dirs[i], found, sizeof(found));
    if (!found[0]) {
        xsdk_log_write("xenon: RESULTADO: nenhuma ROM de %u bytes ao lado do .xex (nem em Data/)", (unsigned)ROM_SIZE);
        return 1;
    }
    xsdk_log_write("xenon: candidata: %s", found);

    rom = load_rom(found, &format);
    if (!rom)
        return 2;
    xsdk_log_write("xenon: formato: %s", format);

    memcpy(name, rom + 0x20, 20);
    name[20] = '\0';
    xsdk_log_write("xenon: nome no cabecalho: \"%s\"", name);

    crc = crc32_of(rom, ROM_SIZE);
    xsdk_log_write("xenon: crc32 = %08X (esperado %08X)", (unsigned)crc, (unsigned)ROM_CRC32);
    ok = (crc == ROM_CRC32);
    xsdk_log_write("xenon: RESULTADO: %s",
                   ok ? "ROM OK: GoldenEye 007 (USA) original" :
                        "ROM NAO confere com o cartucho (U) original (modificada, outra regiao ou copia ruim)");
    free(rom);

    flag = fopen("game:/run.txt", "rb");
    if (!flag || !ok) {
        if (flag) fclose(flag);
        xsdk_log_write("xenon: fim do teste (crie run.txt ao lado do .xex para iniciar o nucleo)");
        return ok ? 0 : 3;
    }
    fclose(flag);
    xsdk_log_write("xenon: run.txt encontrado: iniciando o nucleo do jogo");
    /* Padroes do 360, gravados so se o midas.ini ainda nao existe (o jogo cria um para PC, a 120 Hz). */
    {
        FILE *ini = fopen("game:/midas.ini", "rb");
        if (ini) {
            fclose(ini);
        } else if ((ini = fopen("game:/midas.ini", "wb")) != NULL) {
            fputs("[display]\n"
                  "window_width=640\n"
                  "window_height=480\n"
                  "fullscreen=fullscreen\n"
                  "msaa=1\n"
                  "vsync=0\n"
                  "maxfps=360\n"
                  "tickhz=60\n"
                  "render_thread=0\n"
                  "music_volume=70\n"
                  "sfx_volume=70\n"
                  "widescreen=1\n"
                  "filtering=3point\n"
                  "anisotropy=0\n"
                  "filter_2d=0\n"
                  "high_lod=1\n"
                  "low_res=240\n"
                  "mipmaps=0\n"
                  "n64_aa=0\n"
                  "classic_res=1\n"
                  "classic_filter=1\n"
                  "classic_aniso=1\n"
                  "classic_pacing=1\n"
                  "classic_msaa=1\n"
                  "classic_mipmaps=1\n"
                  "classic_filter2d=1\n"
                  "classic_pillars=1\n"
                  "classic_hitches=1\n"
                  "classic_n64aa=1\n"
                  "classic_composite=1\n"
                  "hfov_wide=100\n"
                  "weapon_fov=0\n"
                  "show_fps=0\n"
                  "mission_timer=0\n"
                  "pause_pillars=0\n"
                  "health_wide=0\n"
                  "hud_wide=0\n"
                  "hud_scale=0\n"
                  "scope_steady=0\n"
                  "distance_fading=0\n"
                  "fix_shootthrough=1\n"
                  "true_enemy_rockets=1\n"
                  "unlock_all=0\n"
                  "always_crosshair=0\n"
                  "aim_lock=0\n"
                  "head_roll=1\n"
                  "mp_sight_on_screen=1\n"
                  "mp_no_auto_aim=0\n"
                  "mp_aim_lock=0\n"
                  "mp_show_names=1\n"
                  "mp_always_crosshair=0\n"
                  "mp_fix_shootthrough=1\n"
                  "mp_spawn_weapon=0\n"
                  "mp_random_spawns=0\n"
                  "mp_head_roll=1\n"
                  "mp_unlimited_explosions=1\n"
                  "mp_ltk_health=0\n"
                  "mp_no_armor=0\n"
                  "mp_friendly_fire=1\n"
                  "unlimited_explosions=1\n"
                  "mouse_auto_aim=1\n"
                  "watch_static=1\n"
                  "fast_pause=0\n"
                  "armor_hud=0\n"
                  "mp_invisibility=0\n"
                  "mp_dk_mode=0\n"
                  "mp_tiny_players=0\n"
                  "mp_paintball=0\n"
                  "mp_no_radar=0\n"
                  "mp_turbo_mode=0\n"
                  "mp_unlimited_ammo=0\n"
                  "mp_hud_wide=0\n"
                  "mp_pillarbox=0\n"
                  "look_updown=1\n"
                  "auto_aim=1\n"
                  "aim_control=0\n"
                  "sight_on_screen=1\n"
                  "look_ahead=1\n"
                  "ammo_on_screen=1\n"
                  "stereo_pan=1\n"
                  "mute_unfocused=0\n"
                  "setting_help=1\n"
                  "\n"
                  "[input]\n"
                  "detonate_use_reload=1\n"
                  "shoot_next_prev=1\n"
                  "no_aim_crouch=0\n"
, ini);
            fclose(ini);
            xsdk_log_write("xenon: midas.ini criado com os padroes do 360 (60 Hz)");
        }
    }
    /* Lista de shaders pre-compilados (de uma partida completa): evita o travamento de ~1 s quando um shader
     * novo aparece no meio da fase. Gravada so se ainda nao existe; o jogo nunca a altera. */
    {
        FILE *sb = fopen("game:/Data/shaders_base.txt", "rb");
        if (sb) {
            fclose(sb);
        } else {
            mkdir("game:/Data", 0777);
            if ((sb = fopen("game:/Data/shaders_base.txt", "wb")) != NULL) {
                fputs(
                  "0000000010001000 10000000\n"
                  "0000000001081000 10000001\n"
                  "0000000001080108 10000000\n"
                  "010d010d80008000 10000010\n"
                  "0000000010000108 10000000\n"
                  "d000030d12181218 00000012\n"
                  "d000030d12181218 00000112\n"
                  "d000030d12181218 00000013\n"
                  "0108010800000000 10000011\n"
                  "0000000001080108 10000001\n"
                  "0000000010001000 10000001\n"
                  "0000000010001000 10002001\n"
                  "0000000001080108 10002001\n"
                  "0000000010000108 10002001\n"
                  "0000000010000108 10002301\n"
                  "0000000010000108 10002101\n"
                  "020d020d818a818a 10002011\n"
                  "020d020d818a818a 10002511\n"
                  "0000000010001218 10000000\n"
                  "1000020d0000818a 10002011\n"
                  "020d020d818a818a 10000f11\n"
                  "020d020d818a818a 10000011\n"
                  "1000020d0000818a 10002f11\n"
                  "0000000001080108 10000301\n"
                  "0000000001080108 10000101\n"
                  "020d020d818a818a 10000511\n"
                  "d000d00010000108 10002311\n"
                  "320d020d818a818a 00000012\n"
                  "d000d00010001000 10002011\n"
                  "320d020d818a818a 00000f12\n"
                  "320d020d818a818a 00000512\n"
                  "320d020d818a818a 00000013\n"
                  "320d020d818a818a 00000f13\n"
                  "020d020d818a818a 00000012\n"
                  "020d020d818a818a 00000512\n"
                  "020d020d818a818a 00000013\n"
                  "010d010d0a080a08 10000c11\n"
                  "d000030d01081218 00000013\n"
                  "d000030d01081218 00000113\n"
                  "020d020d818a818a 10000010\n"
                  "1000020d0000818a 10000010\n"
                  "d000d00001080108 10002011\n"
                  "0000000010001000 10000009\n"
                  "0000000001081000 10000009\n"
                  "0000000010001000 10000008\n"
                  "0108010800000000 10000010\n"
                  "d000020d210c818a 00000017\n"
                  "d000020d210c818a 00000013\n"
                  "d000020d210c818a 00000012\n"
                  "020d020d818a818a 10000711\n"
                  "020d020d0108818a 00000013\n"
                  "020d020d010c818a 00000013\n"
                  "020d020d010c818a 00000513\n"
                  "1000020d0000818a 10000011\n"
                  "d000030d12181218 00000312\n"
                  "020d020d818a818a 10000f10\n"
                  "020d020d818a818a 10000510\n"
                  "320d020d818a818a 00000513\n"
                  "d30c020d0821818a 00000017\n"
                  "020d020d0108818a 00000513\n"
                  "020d020d818a818a 10002f11\n"
                  "d000020d210c818a 00000517\n"
                  "010d020d0a0c818a 10000011\n"
                  "d000020d210c818a 00000f17\n"
                  "d30c020d0821818a 00000f17\n"
                  "020d020d0108818a 00000f13\n"
                  "020d020d010c818a 00000f13\n"
                  "0000000001080108 10000300\n"
                  "0000000010002821 40000000\n"
                  "020d020d818a818a 20002013\n"
                  "1000020d0000818a 20002013\n"
                  "010d020d0a0c818a 20000013\n"
                  "020d020d818a818a 20000013\n"
                  "020d020d818a818a 20002f13\n"
                  "020d020d818a818a 20000012\n"
                  "020d020d818a818a 20000512\n"
                  "d000020d210c818a 00000513\n"
                  "1000020d0000818a 20000012\n"
                  "020d020d818a818a 20000f13\n"
                  "320d020d818a818a 00000c12\n"
                  "020d020d818a818a 00000412\n"
                  "d000030d12181218 00000313\n"
                  "d000d00010000108 10002011\n"
                  "d000d00001080108 00000013\n"
                  "020d020d818a818a 00000513\n"
                  "020d020d818a818a 20000f12\n"
                  "d000d00010001000 20002013\n"
                  "d000d00001080108 20002013\n"
                  "1000020d0000818a 20000013\n"
                  "020d020d818a818a 40000010\n"
                  "020d020d818a818a 00000f12\n"
                  "020d020d818a818a 00000f13\n"
                  "1000020d0000818a 20002f13\n"
                  , sb);
                fclose(sb);
                xsdk_log_write("xenon: Data/shaders_base.txt criado (93 shaders)");
            }
        }
    }
    /* O Midas procura a ROM em Data/ ou como baserom.u.z64; aqui dizemos qual e. */
    setenv("GE_ROM", found, 1);
    xsdk_log_write("xenon: captador de falhas: %u", RtlSetVectoredExceptionHandler((void *)fault_handler, (void *)fault_handler));
    /* run.txt vazio: sem limite de tempo (segure BACK por 2 s para sair). Com um numero dentro (segundos), o jogo
     * fecha sozinho depois desse tempo: serve para testes sem controle por perto. */
    {
        FILE *rt = fopen("game:/run.txt", "rb");
        char buf[32] = "";
        if (rt != NULL) {
            size_t n = fread(buf, 1, sizeof(buf) - 1, rt);
            buf[n] = 0;
            fclose(rt);
        }
        g_run_seconds = atoi(buf);
        if (g_run_seconds > 0) {
            pthread_t t;
            if (pthread_create(&t, NULL, watchdog, NULL) == 0)
                pthread_detach(t);
            else
                xsdk_log_write("xenon: AVISO: nao criei a thread de tempo limite");
        } else {
            xsdk_log_write("xenon: sem limite de tempo; segure BACK por 2 s para sair");
        }
    }
    {
        MidasRun run = { argc, argv, 0 };
        pthread_attr_t attr;
        pthread_t t;

        pthread_attr_init(&attr);
        pthread_attr_setstacksize(&attr, MIDAS_STACK_BYTES);
        if (pthread_create(&t, &attr, midas_thread, &run) != 0) {
            xsdk_log_write("xenon: AVISO: sem thread de 16 MB, rodando na principal (1 MB)");
            return midas_main(argc, argv);
        }
        xsdk_log_write("xenon: nucleo em thread propria, pilha de %u MB", MIDAS_STACK_BYTES >> 20);
        pthread_join(t, NULL);
        xsdk_log_write("xenon: nucleo terminou, codigo %d", run.ret);
        return run.ret;
    }
}
