/* Entrada do Midas no Xbox 360 (no PC: port/src/input_pc.c, SDL).
 * ETAPA ATUAL: tudo neutro (sem controle). O controle de verdade (XamInputGetState) entra depois. */
#include <stdint.h>
#include <stdio.h>

typedef int32_t s32;
typedef uint32_t u32;
typedef float f32;
typedef int8_t s8;

void portInputInit(void) {}
void portInputPollTick(void) {}
void portInputPadMerge(s32 idx, void *pad) { (void)idx; (void)pad; }
void portInputPadNeutral(s32 idx) { (void)idx; }
void portInputPadGadgetChord(s32 idx, void *pad) { (void)idx; (void)pad; }
void portInputStickGate(void *pad, s32 slot) { (void)pad; (void)slot; }
u32 portInputPadMask(void) { return 0; }
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
void portInputRumble(s32 channel, s32 on) { (void)channel; (void)on; }

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
