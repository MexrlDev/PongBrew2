/* Platform layer: controllers, light bar, rumble, audio thread, clock.
   platform_ps4.c implements it with the OpenOrbis pad and audio APIs.
   platform_pc.c implements it with SDL keyboard input, for testing on a PC. */
#ifndef PLATFORM_H
#define PLATFORM_H
#include <stdint.h>

enum {
    B_UP = 1 << 0, B_DOWN = 1 << 1, B_LEFT = 1 << 2, B_RIGHT = 1 << 3,
    B_CROSS = 1 << 4, B_CIRCLE = 1 << 5, B_SQUARE = 1 << 6, B_TRIANGLE = 1 << 7,
    B_L1 = 1 << 8, B_R1 = 1 << 9, B_L2 = 1 << 10, B_R2 = 1 << 11,
    B_OPTIONS = 1 << 12, B_TOUCH = 1 << 13
};

typedef struct {
    uint32_t held;          /* B_* bits */
    float lx, ly, rx, ry;   /* sticks, -1..1, y positive = down */
    int connected;
} PadState;

int  plat_init(void);                    /* pads + audio thread; 0 = ok */
void plat_shutdown(void);
void plat_exit(void);                    /* leave the app: on the console this asks the system to close it cleanly */
int  plat_pad_count(void);               /* 1 or 2 usable pads */
int  plat_pad_native(void);              /* 1 when light bar and rumble work */
void plat_pad_read(int idx, PadState *s);
void plat_pad_light(int idx, uint8_t r, uint8_t g, uint8_t b);
void plat_pad_light_reset(int idx);
void plat_pad_rumble(int idx, uint8_t large, uint8_t small);
void plat_log(const char *fmt, ...);

#endif
