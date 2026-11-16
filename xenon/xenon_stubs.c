/* Substitutas minimas para o que o Midas pega do SDL (video, controle) e do Linux (clock_nanosleep).
 * Sao temporarias: cada grupo some quando a camada real do 360 entrar. */
#include <errno.h>
#include <stdint.h>
#include <time.h>

#include "xsdk/thread.h"

typedef int32_t s32;

/* --- video.c do Midas (SDL) --------------------------------------------------- */
void portVideoKbdDigitalMove(s32 *dx, s32 *dy) { if (dx) *dx = 0; if (dy) *dy = 0; }
void portVideoBindsResetDefaults(void) {}
s32 portPlayerCrosshairOwn(s32 player) { (void)player; return -2; }   /* -2: sem cor propria */
s32 portPlayerCrosshairColor(s32 player) { (void)player; return -1; } /* -1: vermelho */
void portPlayerCrosshairSet(s32 player, s32 rgb) { (void)player; (void)rgb; }
s32 portHudWideCfg(void) { return -1; }                               /* -1: sem configuracao */
void portHudWideAdopt(s32 wide) { (void)wide; }

/* --- input_pc.c do Midas (SDL) ------------------------------------------------ */
s32 portInputDeviceModeRaw(s32 dev) { (void)dev; return 0; }          /* 0: automatico */
const char *portInputLayoutShown(s32 dev) { (void)dev; return ""; }
s32 portInputLayoutInUse(s32 dev) { (void)dev; return 0; }
s32 portInputPadResetDefaults(s32 dev) { (void)dev; return 0; }

/* --- POSIX que o 360 nao tem --------------------------------------------------- */
/* Dorme ate um instante (ou por um intervalo). A precisao e de 1 ms, a do xsdk_thread_sleep. */
int clock_nanosleep(clockid_t clock, int flags, const struct timespec *req, struct timespec *rem)
{
    struct timespec now;
    long long wait_ns, target_ns, now_ns;

    (void)rem;
    if (clock_gettime(clock, &now) != 0)
        return EINVAL;
    target_ns = (long long)req->tv_sec * 1000000000LL + req->tv_nsec;
    now_ns = (long long)now.tv_sec * 1000000000LL + now.tv_nsec;
    wait_ns = (flags & TIMER_ABSTIME) ? target_ns - now_ns : target_ns;
    if (wait_ns >= 1000000LL)
        (void)xsdk_thread_sleep((uint32_t)(wait_ns / 1000000LL));
    return 0;
}
