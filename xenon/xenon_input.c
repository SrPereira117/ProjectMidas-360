/* Entrada do Midas no Xbox 360 (no PC: port/src/input_pc.c, SDL): controles do 360 via XamInputGetState.
 *
 * Mapeamento (estilo N64): A=A, B=X, Z=gatilho direito (RT) ou esquerdo (LT), R=RB, L=LB, Start=Start,
 * D-pad=D-pad, botoes C = analogico direito, analogico esquerdo = analogico do N64. Vibracao: XamInputSetState. */
#include <stdint.h>
#include <stdio.h>
#include <xsdk/kernel.h>

#define N64_A 0x8000
#define N64_B 0x4000
#define N64_Z 0x2000
#define N64_START 0x1000
#define N64_DU 0x0800
#define N64_DD 0x0400
#define N64_DL 0x0200
#define N64_DR 0x0100
#define N64_L 0x0020
#define N64_R 0x0010
#define N64_CU 0x0008
#define N64_CD 0x0004
#define N64_CL 0x0002
#define N64_CR 0x0001

typedef struct {
    uint16_t button;
    int8_t stick_x;
    int8_t stick_y;
    uint8_t errno_;
} PadRaw;

static struct {
    int ok;
    uint16_t button;
    int8_t sx, sy;
} g_pad[4];
static uint32_t g_connected = 1;

static int8_t scale_stick(int v)
{
    const int dead = 7500;
    if (v > -dead && v < dead) return 0;
    v = v > 0 ? v - dead : v + dead;
    v = v * 80 / (32767 - dead);
    if (v > 80) v = 80;
    if (v < -80) v = -80;
    return (int8_t)v;
}

static void poll_pad(int i)
{
    INPUT_STATE st;
    g_pad[i].ok = 0;
    g_pad[i].button = 0;
    g_pad[i].sx = g_pad[i].sy = 0;
    if (XamInputGetState((uint32_t)i, 0, &st) != 0) return;
    g_pad[i].ok = 1;
    const INPUT_GAMEPAD *g = &st.gamepad;
    uint16_t b = 0;
    if (g->buttons & INPUT_GAMEPAD_A) b |= N64_A;
    if (g->buttons & (INPUT_GAMEPAD_X | INPUT_GAMEPAD_B)) b |= N64_B;
    if (g->buttons & INPUT_GAMEPAD_START) b |= N64_START;
    if (g->buttons & INPUT_GAMEPAD_DPAD_UP) b |= N64_DU;
    if (g->buttons & INPUT_GAMEPAD_DPAD_DOWN) b |= N64_DD;
    if (g->buttons & INPUT_GAMEPAD_DPAD_LEFT) b |= N64_DL;
    if (g->buttons & INPUT_GAMEPAD_DPAD_RIGHT) b |= N64_DR;
    if (g->buttons & INPUT_GAMEPAD_LEFT_SHOULDER) b |= N64_L;
    if (g->buttons & INPUT_GAMEPAD_RIGHT_SHOULDER) b |= N64_R;
    if (g->right_trigger > 100 || g->left_trigger > 100) b |= N64_Z;
    if (g->thumb_ry > 16000) b |= N64_CU;
    if (g->thumb_ry < -16000) b |= N64_CD;
    if (g->thumb_rx < -16000) b |= N64_CL;
    if (g->thumb_rx > 16000) b |= N64_CR;
    g_pad[i].button = b;
    g_pad[i].sx = scale_stick(g->thumb_lx);
    g_pad[i].sy = scale_stick(g->thumb_ly);
}

typedef int32_t s32;
typedef uint32_t u32;
typedef float f32;
typedef int8_t s8;

void portInputPollTick(void);
void portInputInit(void) { portInputPollTick(); }
extern void xenon_video_request_stop(void);
static int g_back_ticks;

void portInputPollTick(void)
{
    uint32_t mask = 0;
    {
        /* BACK segurado por ~2 s (120 leituras) em qualquer controle: sair para o dashboard */
        INPUT_STATE st;
        int back = 0;
        for (int i = 0; i < 4; i++)
            if (XamInputGetState((uint32_t)i, 0, &st) == 0 && (st.gamepad.buttons & INPUT_GAMEPAD_BACK)) back = 1;
        g_back_ticks = back ? g_back_ticks + 1 : 0;
        if (g_back_ticks == 120) xenon_video_request_stop();
    }
    for (int i = 0; i < 4; i++) {
        poll_pad(i);
        if (g_pad[i].ok) mask |= 1u << i;
    }
    /* o controle 1 sempre "existe" (le neutro se estiver desligado), para o jogo nao pedir para ligar um */
    g_connected = mask | 1u;
}
void portInputPadMerge(s32 idx, void *pad)
{
    PadRaw *p = (PadRaw *)pad;
    if (idx < 0 || idx > 3 || !g_pad[idx].ok) return;
    p->button |= g_pad[idx].button;
    if (g_pad[idx].sx || g_pad[idx].sy) {
        p->stick_x = g_pad[idx].sx;
        p->stick_y = g_pad[idx].sy;
    }
}
void portInputPadNeutral(s32 idx) { (void)idx; }
void portInputPadGadgetChord(s32 idx, void *pad) { (void)idx; (void)pad; }
void portInputStickGate(void *pad, s32 slot) { (void)pad; (void)slot; }
u32 portInputPadMask(void) { return g_connected; }
f32 portInputPadActionLevel(s32 slot, s32 act) { (void)slot; (void)act; return 0.0f; }
s32 portInputPadTwinStickCapable(s32 idx) { (void)idx; return 0; }
s32 portInputPadTwinStick(s32 idx, s8 *x, s8 *y, s32 muteC, s32 lookOnPad1)
{
    (void)idx; (void)muteC; (void)lookOnPad1;
    if (x) *x = 0;
    if (y) *y = 0;
    return 0;
}
s32 portInputPadSeedForKey(const char *key) { (void)key; return 0; }
s32 portInputSetOption(const char *key, const char *value) { (void)key; (void)value; return 0; }
void portInputRumble(s32 channel, s32 on)
{
    INPUT_VIBRATION v;
    if (channel < 0 || channel > 3) return;
    v.left_motor_speed = v.right_motor_speed = on ? 40000 : 0;
    (void)XamInputSetState((uint32_t)channel, 0, &v);
}

s32 portInputP1KbmMuted(void) { return 1; }
s32 portInputP1Device(void) { return 1; }
void portInputP1ClaimWindow(s32 open) { (void)open; }

void portInputMouseWindowPos(s32 *x, s32 *y, s32 *moved)
{
    if (x) *x = 0;
    if (y) *y = 0;
    if (moved) *moved = 0;
}
void portInputMouseUpdate(void) {}
u32 portInputMouseButtons(void) { return 0; }
void portInputMouseWarp(s32 x, s32 y) { (void)x; (void)y; }
void portInputMousePark(s32 x, s32 y, s32 axes) { (void)x; (void)y; (void)axes; }
s32 portInputMouseMovedThisTick(void) { return 0; }
void portInputMouseLookDelta(f32 *dx, f32 *dy) { if (dx) *dx = 0; if (dy) *dy = 0; }
void portInputMouseLookDeltaDeg(f32 *dx, f32 *dy) { if (dx) *dx = 0; if (dy) *dy = 0; }
s32 portInputMouseDirectMode(void) { return 0; }

const char *portInputKbmRecPath(void) { return ""; }
void portInputDevRecHeader(FILE *f, const char *id) { (void)f; (void)id; }

void portInputListPadsAndExit(void) {}
void portInputCapturePadAndExit(const char *arg) { (void)arg; }
void portInputCalibratePadAndExit(const char *arg) { (void)arg; }
void portInputRumbleTestAndExit(const char *arg) { (void)arg; }
f32 portInputMouseAimScale(void) { return 1.0f; }
s32 portInputDeviceModeRaw(s32 dev) { (void)dev; return 0; }          /* 0: automatico */
const char *portInputLayoutShown(s32 dev) { (void)dev; return ""; }
s32 portInputLayoutInUse(s32 dev) { (void)dev; return 0; }
s32 portInputPadResetDefaults(s32 dev) { (void)dev; return 0; }
