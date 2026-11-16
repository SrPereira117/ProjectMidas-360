/* Substitutas minimas para o que o Midas pega do SDL (video, controle) e do Linux (clock_nanosleep).
 * Sao temporarias: cada grupo some quando a camada real do 360 entrar. */
#include <errno.h>
#include <stdint.h>
#include <time.h>

#include "xsdk/thread.h"

typedef int32_t s32;

/* --- input_pc.c do Midas (SDL) ------------------------------------------------ */
s32 portInputDeviceModeRaw(s32 dev) { (void)dev; return 0; }          /* 0: automatico */
const char *portInputLayoutShown(s32 dev) { (void)dev; return ""; }
s32 portInputLayoutInUse(s32 dev) { (void)dev; return 0; }
s32 portInputPadResetDefaults(s32 dev) { (void)dev; return 0; }

/* --- audio_pc.c do Midas (SDL): sem som por enquanto ------------------------------- */
void portAudioPump(void) {}
void portAudioPumpForce(void) {}

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
