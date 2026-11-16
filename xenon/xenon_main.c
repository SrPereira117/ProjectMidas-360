/* Ponto de entrada do GoldenEye (Project Midas) no Xbox 360.
 *
 * Passo atual: achar a ROM do usuario ao lado do .xex, conferir e gravar o resultado no log
 * (<nome do xex>.log, na mesma pasta). Se existir um arquivo "run.txt" ao lado do .xex, depois
 * disso o nucleo do jogo e iniciado (ainda sem video nem audio).
 *
 * A main() do Midas (port/src/main.c) foi renomeada para midas_main() na compilacao. */
#include <dirent.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "xsdk/log.h"

extern int midas_main(int argc, char **argv);

#define ROM_SIZE   0xC00000u    /* 12.582.912 bytes: a cartucho (U) original */
#define ROM_CRC32  0xB6330846u  /* crc32 da imagem inteira, em ordem z64 */

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
        xsdk_log_write("xenon: pasta %s: nao abre (errno %d)", dir, errno);
        return 0;
    }
    xsdk_log_write("xenon: pasta %s:", dir);
    while ((e = readdir(d)) != NULL) {
        char path[512];
        struct stat st;
        if (strcmp(e->d_name, ".") == 0 || strcmp(e->d_name, "..") == 0)
            continue;
        snprintf(path, sizeof(path), "%s/%s", dir, e->d_name);
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
        xsdk_log_write("xenon: nao consegui abrir %s (errno %d)", path, errno);
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
    xsdk_log_write("xenon: run.txt encontrado: iniciando o nucleo do jogo (sem video nem audio)");
    return midas_main(argc, argv);
}
