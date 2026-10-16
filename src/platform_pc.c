#ifndef PS4_BUILD
/* PC backend: keyboard stands in for two controllers, light bar and rumble are recorded
   so tests can read them. Used for developing and testing the game without a console. */
#include "platform.h"
#include "audio.h"
#include <SDL2/SDL.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

int pc_light[2][3];
int pc_rumble[2][2];
int pc_light_calls, pc_rumble_calls;
static int g_inj[2];
static PadState g_inj_state[2];

void pc_inject(int idx, uint32_t held, float lx, float ly, float rx, float ry) {
    g_inj[idx] = 1;
    g_inj_state[idx].held = held; g_inj_state[idx].lx = lx; g_inj_state[idx].ly = ly;
    g_inj_state[idx].rx = rx; g_inj_state[idx].ry = ry; g_inj_state[idx].connected = 1;
}

void plat_log(const char *fmt, ...) {
    char buf[256];
    va_list ap; va_start(ap, fmt); vsnprintf(buf, sizeof buf, fmt, ap); va_end(ap);
    SDL_Log("%s", buf);
}

int plat_init(void) { return 0; }
void plat_shutdown(void) {}
void plat_exit(void) {}
int plat_pad_count(void) { return g_inj[1] ? 2 : 1; }
int plat_pad_native(void) { return 1; }

void plat_pad_read(int idx, PadState *s) {
    memset(s, 0, sizeof *s);
    if (idx < 0 || idx > 1) return;
    if (g_inj[idx]) { *s = g_inj_state[idx]; return; }
    if (idx != 0) return;
    const Uint8 *k = SDL_GetKeyboardState(NULL);
    s->connected = 1;
    if (k[SDL_SCANCODE_LEFT] || k[SDL_SCANCODE_A]) s->lx -= 1;
    if (k[SDL_SCANCODE_RIGHT] || k[SDL_SCANCODE_D]) s->lx += 1;
    if (k[SDL_SCANCODE_UP] || k[SDL_SCANCODE_W]) s->held |= B_UP;
    if (k[SDL_SCANCODE_DOWN] || k[SDL_SCANCODE_S]) s->held |= B_DOWN;
    if (k[SDL_SCANCODE_RETURN] || k[SDL_SCANCODE_SPACE]) s->held |= B_CROSS;
    if (k[SDL_SCANCODE_BACKSPACE]) s->held |= B_CIRCLE;
    if (k[SDL_SCANCODE_ESCAPE] || k[SDL_SCANCODE_P]) s->held |= B_OPTIONS;
    if (k[SDL_SCANCODE_LSHIFT]) s->held |= B_R1;
    if (k[SDL_SCANCODE_J]) s->rx -= 1;
    if (k[SDL_SCANCODE_L]) s->rx += 1;
}

void plat_pad_light(int idx, uint8_t r, uint8_t g, uint8_t b) {
    if (idx < 0 || idx > 1) return;
    pc_light[idx][0] = r; pc_light[idx][1] = g; pc_light[idx][2] = b; pc_light_calls++;
}
void plat_pad_light_reset(int idx) {
    if (idx < 0 || idx > 1) return;
    pc_light[idx][0] = pc_light[idx][1] = pc_light[idx][2] = -1; pc_light_calls++;
}
void plat_pad_rumble(int idx, uint8_t large, uint8_t small) {
    if (idx < 0 || idx > 1) return;
    pc_rumble[idx][0] = large; pc_rumble[idx][1] = small; pc_rumble_calls++;
}
#endif /* !PS4_BUILD */
