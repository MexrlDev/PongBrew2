#ifdef PS4_BUILD
/* PS4 backend (OpenOrbis). Pads use the native scePad API so the light bar and rumble work.
   If the native pad cannot be opened (for example SDL already owns it) the game falls back
   to SDL joystick input and simply has no light bar or rumble. */
#include "platform.h"
#include "audio.h"
#include <SDL2/SDL.h>
#include <orbis/Pad.h>
#include <orbis/AudioOut.h>
#include <orbis/UserService.h>
#include <orbis/libkernel.h>
#include <orbis/SystemService.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

static int g_pad[2] = { -1, -1 };
static int g_npads;
static int g_native;
static SDL_Joystick *g_joy;
static int g_audio_h = -1;
static volatile int g_audio_run;
static OrbisPthread g_audio_thread;
static int g_last_r[2] = { -1, -1 }, g_last_g[2], g_last_b[2];
static int g_last_l[2], g_last_s[2];

void plat_log(const char *fmt, ...) {
    char buf[256];
    va_list ap; va_start(ap, fmt); vsnprintf(buf, sizeof buf, fmt, ap); va_end(ap);
    SDL_Log("%s", buf);
}

static void *audio_thread_main(void *arg) {
    static int16_t buf[AUDIO_GRAIN * 2];
    (void)arg;
    while (g_audio_run) {
        audio_mix(buf, AUDIO_GRAIN);
        sceAudioOutOutput(g_audio_h, buf);
    }
    return NULL;
}

static void start_audio(void) {
    int r = sceAudioOutInit();
    plat_log("PONG: sceAudioOutInit = 0x%x", r);
    g_audio_h = sceAudioOutOpen(ORBIS_USER_SERVICE_USER_ID_SYSTEM, ORBIS_AUDIO_OUT_PORT_TYPE_MAIN, 0,
                                AUDIO_GRAIN, AUDIO_RATE, ORBIS_AUDIO_OUT_PARAM_FORMAT_S16_STEREO);
    plat_log("PONG: sceAudioOutOpen handle = %d", g_audio_h);
    if (g_audio_h < 0) return;                        /* no audio, the game still runs */
    g_audio_run = 1;
    r = scePthreadCreate(&g_audio_thread, NULL, audio_thread_main, NULL, "pong_audio");
    plat_log("PONG: audio thread = 0x%x", r);
    if (r != 0) g_audio_run = 0;
}

int plat_init(void) {
    int uid = -1;
    int r = sceUserServiceInitialize(NULL);
    plat_log("PONG: sceUserServiceInitialize = 0x%x", r);
    r = scePadInit();
    plat_log("PONG: scePadInit = 0x%x", r);
    r = sceUserServiceGetInitialUser(&uid);
    plat_log("PONG: initial user = %d (0x%x)", uid, r);

    if (uid >= 0) {
        g_pad[0] = scePadOpen(uid, ORBIS_PAD_PORT_TYPE_STANDARD, 0, NULL);
        if (g_pad[0] < 0) g_pad[0] = scePadGetHandle(uid, ORBIS_PAD_PORT_TYPE_STANDARD, 0);
        plat_log("PONG: pad 0 handle = %d", g_pad[0]);

        /* a second logged-in user gets the second controller */
        OrbisUserServiceLoginUserIdList list;
        memset(&list, 0xFF, sizeof list);
        if (sceUserServiceGetLoginUserIdList(&list) == 0) {
            for (int i = 0; i < ORBIS_USER_SERVICE_MAX_LOGIN_USERS; i++) {
                int u = list.userId[i];
                if (u > 0 && u != uid) {
                    g_pad[1] = scePadOpen(u, ORBIS_PAD_PORT_TYPE_STANDARD, 0, NULL);
                    if (g_pad[1] < 0) g_pad[1] = scePadGetHandle(u, ORBIS_PAD_PORT_TYPE_STANDARD, 0);
                    plat_log("PONG: pad 1 (user %d) handle = %d", u, g_pad[1]);
                    break;
                }
            }
        }
    }
    g_native = g_pad[0] >= 0;
    g_npads = g_native ? (g_pad[1] >= 0 ? 2 : 1) : 0;

    if (!g_native) {
        plat_log("PONG: native pad unavailable, falling back to SDL joystick");
        if (SDL_InitSubSystem(SDL_INIT_JOYSTICK) == 0 && SDL_NumJoysticks() > 0) {
            g_joy = SDL_JoystickOpen(0);
            if (g_joy) g_npads = 1;
        }
    }
    start_audio();
    return 0;
}

void plat_shutdown(void) {
    for (int i = 0; i < 2; i++) if (g_pad[i] >= 0) {
        OrbisPadVibeParam z = { 0, 0 };
        scePadSetVibration(g_pad[i], &z);
        scePadResetLightBar(g_pad[i]);
    }
    if (g_audio_h >= 0) {
        g_audio_run = 0;                       /* let the audio thread finish its last buffer, then wait for it */
        if (g_audio_thread) scePthreadJoin(g_audio_thread, NULL);
        else sceKernelUsleep(50000);
        sceAudioOutClose(g_audio_h);
        g_audio_h = -1;
    }
}

/* Returning from main() makes the system log the exit as a crash (CE-34878-0), and so did tearing SDL down
   by hand. Asking the system service to close the app is the clean way out. */
void plat_exit(void) {
    plat_log("PONG: closing application");
    sceSystemServiceLoadExec("exit", NULL);
    for (int i = 0; i < 100; i++) sceKernelUsleep(20000);   /* the system ends the process; do not run any more code */
}

int plat_pad_count(void) { return g_npads; }
int plat_pad_native(void) { return g_native; }

static uint32_t map_buttons(uint32_t b) {
    uint32_t m = 0;
    if (b & ORBIS_PAD_BUTTON_UP) m |= B_UP;
    if (b & ORBIS_PAD_BUTTON_DOWN) m |= B_DOWN;
    if (b & ORBIS_PAD_BUTTON_LEFT) m |= B_LEFT;
    if (b & ORBIS_PAD_BUTTON_RIGHT) m |= B_RIGHT;
    if (b & ORBIS_PAD_BUTTON_CROSS) m |= B_CROSS;
    if (b & ORBIS_PAD_BUTTON_CIRCLE) m |= B_CIRCLE;
    if (b & ORBIS_PAD_BUTTON_SQUARE) m |= B_SQUARE;
    if (b & ORBIS_PAD_BUTTON_TRIANGLE) m |= B_TRIANGLE;
    if (b & ORBIS_PAD_BUTTON_L1) m |= B_L1;
    if (b & ORBIS_PAD_BUTTON_R1) m |= B_R1;
    if (b & ORBIS_PAD_BUTTON_L2) m |= B_L2;
    if (b & ORBIS_PAD_BUTTON_R2) m |= B_R2;
    if (b & ORBIS_PAD_BUTTON_OPTIONS) m |= B_OPTIONS;
    if (b & ORBIS_PAD_BUTTON_TOUCH_PAD) m |= B_TOUCH;
    return m;
}

static float pad_stick(uint8_t v) {
    float f = ((int)v - 128) / 127.0f;
    if (f > -0.18f && f < 0.18f) return 0.0f;
    if (f > 1) f = 1; if (f < -1) f = -1;
    return f;
}

void plat_pad_read(int idx, PadState *s) {
    memset(s, 0, sizeof *s);
    if (idx < 0 || idx > 1) return;
    if (g_native) {
        if (g_pad[idx] < 0) return;
        OrbisPadData d;
        memset(&d, 0, sizeof d);
        if (scePadReadState(g_pad[idx], &d) != 0) return;
        s->connected = d.connected ? 1 : 0;
        s->held = map_buttons(d.buttons);
        s->lx = pad_stick(d.leftStick.x); s->ly = pad_stick(d.leftStick.y);
        s->rx = pad_stick(d.rightStick.x); s->ry = pad_stick(d.rightStick.y);
    } else if (g_joy && idx == 0) {
        SDL_JoystickUpdate();
        s->connected = 1;
        int na = SDL_JoystickNumAxes(g_joy), nb = SDL_JoystickNumButtons(g_joy), nh = SDL_JoystickNumHats(g_joy);
        if (na > 1) { s->lx = SDL_JoystickGetAxis(g_joy, 0) / 32767.0f; s->ly = SDL_JoystickGetAxis(g_joy, 1) / 32767.0f; }
        if (na > 3) { s->rx = SDL_JoystickGetAxis(g_joy, 2) / 32767.0f; s->ry = SDL_JoystickGetAxis(g_joy, 3) / 32767.0f; }
        if (s->lx > -0.2f && s->lx < 0.2f) s->lx = 0;
        if (s->rx > -0.2f && s->rx < 0.2f) s->rx = 0;
        if (nh > 0) {
            Uint8 h = SDL_JoystickGetHat(g_joy, 0);
            if (h & SDL_HAT_UP) s->held |= B_UP;
            if (h & SDL_HAT_DOWN) s->held |= B_DOWN;
            if (h & SDL_HAT_LEFT) s->held |= B_LEFT;
            if (h & SDL_HAT_RIGHT) s->held |= B_RIGHT;
        }
        /* same layout the first version used: buttons 0-3 are face buttons, 6/7/9 pause */
        if (nb > 0 && SDL_JoystickGetButton(g_joy, 0)) s->held |= B_CROSS;
        if (nb > 1 && SDL_JoystickGetButton(g_joy, 1)) s->held |= B_CIRCLE;
        if (nb > 2 && SDL_JoystickGetButton(g_joy, 2)) s->held |= B_SQUARE;
        if (nb > 3 && SDL_JoystickGetButton(g_joy, 3)) s->held |= B_TRIANGLE;
        if (nb > 4 && SDL_JoystickGetButton(g_joy, 4)) s->held |= B_L1;
        if (nb > 5 && SDL_JoystickGetButton(g_joy, 5)) s->held |= B_R1;
        if ((nb > 6 && SDL_JoystickGetButton(g_joy, 6)) || (nb > 7 && SDL_JoystickGetButton(g_joy, 7)) ||
            (nb > 9 && SDL_JoystickGetButton(g_joy, 9))) s->held |= B_OPTIONS;
    }
}

void plat_pad_light(int idx, uint8_t r, uint8_t g, uint8_t b) {
    if (!g_native || idx < 0 || idx > 1 || g_pad[idx] < 0) return;
    if (g_last_r[idx] == r && g_last_g[idx] == g && g_last_b[idx] == b) return;
    OrbisPadColor c = { r, g, b, 255 };
    scePadSetLightBar(g_pad[idx], &c);
    g_last_r[idx] = r; g_last_g[idx] = g; g_last_b[idx] = b;
}

void plat_pad_light_reset(int idx) {
    if (!g_native || idx < 0 || idx > 1 || g_pad[idx] < 0) return;
    scePadResetLightBar(g_pad[idx]);
    g_last_r[idx] = -1;
}

void plat_pad_rumble(int idx, uint8_t large, uint8_t small) {
    if (!g_native || idx < 0 || idx > 1 || g_pad[idx] < 0) return;
    if (g_last_l[idx] == large && g_last_s[idx] == small && large == 0 && small == 0) return;
    OrbisPadVibeParam v = { large, small };
    scePadSetVibration(g_pad[idx], &v);
    g_last_l[idx] = large; g_last_s[idx] = small;
}
#endif /* PS4_BUILD */
