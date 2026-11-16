/* Mini "SDL" para o Xbox 360: so o que o video.c do Midas usa (texto, mutex/thread via pthread,
 * relogio). Nao ha teclado, janela GL nem eventos no console; essas funcoes sao inertes. */
#ifndef XENON_SDL_H
#define XENON_SDL_H
#include <ctype.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SDL_strcasecmp strcasecmp
#define SDL_strncasecmp strncasecmp
#define SDL_atoi atoi
#define SDL_atof atof
#define SDL_calloc calloc
#define SDL_free free
#define SDL_getenv getenv
#define SDL_memcpy memcpy
#define SDL_memmove memmove
#define SDL_memset memset
#define SDL_snprintf snprintf
#define SDL_sscanf sscanf
#define SDL_strchr strchr
#define SDL_strcmp strcmp
#define SDL_strdup strdup
#define SDL_strlen strlen
#define SDL_zero(x) memset(&(x), 0, sizeof(x))

static inline size_t SDL_strlcpy(char *dst, const char *src, size_t n)
{
    size_t l = strlen(src);
    if (n) { size_t c = l >= n ? n - 1 : l; memcpy(dst, src, c); dst[c] = 0; }
    return l;
}

typedef enum {
    SDL_SCANCODE_UNKNOWN = 0,
    SDL_SCANCODE_COMMA,SDL_SCANCODE_DELETE,SDL_SCANCODE_E,SDL_SCANCODE_ESCAPE,SDL_SCANCODE_F1,SDL_SCANCODE_GRAVE,SDL_SCANCODE_INTERNATIONAL1,SDL_SCANCODE_INTERNATIONAL2,SDL_SCANCODE_INTERNATIONAL3,SDL_SCANCODE_INTERNATIONAL4,SDL_SCANCODE_INTERNATIONAL5,SDL_SCANCODE_INTERNATIONAL6,SDL_SCANCODE_INTERNATIONAL7,SDL_SCANCODE_INTERNATIONAL8,SDL_SCANCODE_INTERNATIONAL9,SDL_SCANCODE_KP_COMMA,SDL_SCANCODE_LALT,SDL_SCANCODE_LANG1,SDL_SCANCODE_LANG2,SDL_SCANCODE_LANG3,SDL_SCANCODE_LANG4,SDL_SCANCODE_LANG5,SDL_SCANCODE_LANG6,SDL_SCANCODE_LANG7,SDL_SCANCODE_LANG8,SDL_SCANCODE_LANG9,SDL_SCANCODE_LCTRL,SDL_SCANCODE_LGUI,SDL_SCANCODE_NONUSBACKSLASH,SDL_SCANCODE_Q,SDL_SCANCODE_RALT,SDL_SCANCODE_RETURN,SDL_SCANCODE_RGUI,SDL_SCANCODE_SPACE,SDL_SCANCODE_UP,
    SDL_NUM_SCANCODES = 512
} SDL_Scancode;
typedef int SDL_Keymod;
typedef uint8_t Uint8;
typedef uint16_t Uint16;
typedef uint32_t Uint32;
typedef int32_t Sint32;
typedef uint64_t Uint64;
enum { KMOD_CAPS = 0x2000, KMOD_SHIFT = 0x3, KMOD_CTRL = 0xC0, KMOD_ALT = 0x300, KMOD_GUI = 0xC00 };

typedef pthread_mutex_t SDL_mutex;
typedef pthread_cond_t SDL_cond;
typedef pthread_t SDL_Thread;
typedef unsigned long SDL_threadID;
typedef unsigned long SDL_ThreadID_t;

static inline SDL_mutex *SDL_CreateMutex(void)
{
    SDL_mutex *m = (SDL_mutex *) malloc(sizeof(*m));
    if (m) pthread_mutex_init(m, NULL);
    return m;
}
static inline int SDL_LockMutex(SDL_mutex *m) { return pthread_mutex_lock(m); }
static inline int SDL_UnlockMutex(SDL_mutex *m) { return pthread_mutex_unlock(m); }
static inline SDL_cond *SDL_CreateCond(void)
{
    SDL_cond *c = (SDL_cond *) malloc(sizeof(*c));
    if (c) pthread_cond_init(c, NULL);
    return c;
}
static inline int SDL_CondSignal(SDL_cond *c) { return pthread_cond_signal(c); }
static inline int SDL_CondWait(SDL_cond *c, SDL_mutex *m) { return pthread_cond_wait(c, m); }
static inline int SDL_CondWaitTimeout(SDL_cond *c, SDL_mutex *m, uint32_t ms)
{
    struct timespec t;
    clock_gettime(CLOCK_REALTIME, &t);
    t.tv_sec += ms / 1000;
    t.tv_nsec += (long) (ms % 1000) * 1000000L;
    if (t.tv_nsec >= 1000000000L) { t.tv_sec++; t.tv_nsec -= 1000000000L; }
    return pthread_cond_timedwait(c, m, &t) == 0 ? 0 : 1;
}
typedef int (*SDL_ThreadFunction)(void *);
typedef struct { SDL_ThreadFunction fn; void *arg; int ret; pthread_t t; } XenonSdlThread;
static inline void *xenon_sdl_thread_main(void *p)
{
    XenonSdlThread *x = (XenonSdlThread *) p;
    x->ret = x->fn(x->arg);
    return NULL;
}
static inline XenonSdlThread *SDL_CreateThread(SDL_ThreadFunction fn, const char *name, void *arg)
{
    XenonSdlThread *x = (XenonSdlThread *) calloc(1, sizeof(*x));
    (void) name;
    if (!x) return NULL;
    x->fn = fn; x->arg = arg;
    if (pthread_create(&x->t, NULL, xenon_sdl_thread_main, x) != 0) { free(x); return NULL; }
    return x;
}
static inline void SDL_WaitThread(XenonSdlThread *x, int *status)
{
    if (!x) return;
    pthread_join(x->t, NULL);
    if (status) *status = x->ret;
    free(x);
}
static inline unsigned long SDL_ThreadID(void) { return (unsigned long) pthread_self(); }
static inline unsigned long SDL_GetThreadID(XenonSdlThread *x) { return x ? (unsigned long) x->t : 0; }

static inline uint32_t SDL_GetTicks(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (uint32_t) (t.tv_sec * 1000u + t.tv_nsec / 1000000u);
}

typedef struct { uint32_t type; struct { char text[32]; } text; struct { int y; } wheel; } SDL_Event;
enum { SDL_QUIT = 0x100, SDL_TEXTINPUT = 0x303, SDL_MOUSEWHEEL = 0x403 };
#define SDL_BUTTON(x) (1 << ((x) - 1))
#define SDL_BUTTON_LEFT 1
#define SDL_BUTTON_LMASK SDL_BUTTON(SDL_BUTTON_LEFT)
#define SDL_WINDOW_FULLSCREEN 1
#define SDL_MESSAGEBOX_ERROR 0x10

static inline int SDL_PushEvent(SDL_Event *e) { (void) e; return 0; }
static inline const uint8_t *SDL_GetKeyboardState(int *n)
{
    static uint8_t keys[SDL_NUM_SCANCODES];
    if (n) *n = SDL_NUM_SCANCODES;
    return keys;
}
static inline SDL_Keymod SDL_GetModState(void) { return 0; }
static inline uint32_t SDL_GetMouseState(int *x, int *y) { if (x) *x = 0; if (y) *y = 0; return 0; }
static inline void *SDL_GetKeyboardFocus(void) { return (void *) 1; }
static inline void SDL_ResetKeyboard(void) {}
static inline void SDL_StartTextInput(void) {}
static inline void SDL_StopTextInput(void) {}
static inline int SDL_IsTextInputActive(void) { return 0; }
static inline const char *SDL_GetScancodeName(SDL_Scancode s) { (void) s; return ""; }
static inline SDL_Scancode SDL_GetScancodeFromName(const char *n) { (void) n; return SDL_SCANCODE_UNKNOWN; }
static inline int SDL_GL_GetSwapInterval(void) { return 1; }
static inline int SDL_Init(uint32_t f) { (void) f; return 0; }
static inline const char *SDL_GetError(void) { return ""; }
static inline int SDL_ShowSimpleMessageBox(uint32_t f, const char *t, const char *m, void *w)
{
    (void) f; (void) w;
    printf("ge: erro: %s: %s\n", t ? t : "", m ? m : "");
    return 0;
}
#define SDL_INIT_VIDEO 0x20
#define SDL_INIT_GAMECONTROLLER 0x2000
#define SDL_INIT_AUDIO 0x10

#ifdef __cplusplus
}
#endif
#endif
