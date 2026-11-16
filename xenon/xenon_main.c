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
    got = fread(buf, 1, ROM_SIZE, f);
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
    xsdk_log_write("xenon: run.txt encontrado: iniciando o nucleo do jogo (sem video nem audio, %d s)", RUN_SECONDS);
    /* Padroes do 360, gravados so se o midas.ini ainda nao existe (o jogo cria um para PC, a 120 Hz). */
    {
        FILE *ini = fopen("game:/midas.ini", "rb");
        if (ini) {
            fclose(ini);
        } else if ((ini = fopen("game:/midas.ini", "wb")) != NULL) {
            fputs("[display]\nwindow_width=640\nwindow_height=480\nfullscreen=fullscreen\n"
                  "msaa=1\nvsync=1\nmaxfps=60\ntickhz=60\nrender_thread=0\n"
                  "music_volume=70\nsfx_volume=70\n", ini);
            fclose(ini);
            xsdk_log_write("xenon: midas.ini criado com os padroes do 360 (60 Hz)");
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
