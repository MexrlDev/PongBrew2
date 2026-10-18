/* Headless PC test for PongBrew 2. Not part of the PS4 build.
   Drives the real game code with injected controller input, saves screenshots,
   and checks audio, rumble, light bar and game logic.
   Build and run with:  sh test/run_tests.sh  (from the project root) */
#include <SDL2/SDL.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../src/game.h"
#include "../src/gfx.h"
#include "../src/audio.h"
#include "../src/platform.h"
#include "../src/save.h"

extern void pc_inject(int idx, uint32_t held, float lx, float ly, float rx, float ry);
extern int pc_light[2][3], pc_rumble[2][2], pc_light_calls, pc_rumble_calls;

static SDL_Surface *surf;
static SDL_Renderer *ren_g;
static int fails, checks;
static const char *OUT = "test/out";

#define CHECK(c, ...) do { checks++; if (!(c)) { fails++; printf("  FAIL: "); printf(__VA_ARGS__); printf("\n"); } else { printf("  ok:   "); printf(__VA_ARGS__); printf("\n"); } } while (0)

static void shot(const char *name) {
    char p[160];
    game_draw();
    SDL_RenderPresent(ren_g);
    snprintf(p, sizeof p, "%s/%s.bmp", OUT, name);
    SDL_SaveBMP(surf, p);
}
static void ticks(int n) { for (int i = 0; i < n; i++) game_tick(); }
static void press(uint32_t b) {
    pc_inject(0, b, 0, 0, 0, 0); game_tick();
    pc_inject(0, 0, 0, 0, 0, 0); game_tick(); game_tick();
}
static void bot(int pad) {
    int bx = game_dbg(DBG_BX), px = pad == 0 ? game_dbg(DBG_PX0) : game_dbg(DBG_PX1);
    float lx = (bx - px) > 25 ? 1.0f : ((bx - px) < -25 ? -1.0f : 0.0f);
    pc_inject(pad, 0, lx, 0, 0, 0);
}
static double mix_rms(int frames) {
    static int16_t buf[AUDIO_GRAIN * 2];
    double s = 0; long n = 0;
    for (int done = 0; done < frames; done += AUDIO_GRAIN) {
        audio_mix(buf, AUDIO_GRAIN);
        for (int i = 0; i < AUDIO_GRAIN * 2; i++) { s += (double)buf[i] * buf[i]; n++; }
    }
    return sqrt(s / (double)n);
}
static int peak_after_sfx(int id, int frames) {
    static int16_t buf[AUDIO_GRAIN * 2];
    audio_sfx(id);
    int pk = 0;
    for (int done = 0; done < frames; done += AUDIO_GRAIN) {
        audio_mix(buf, AUDIO_GRAIN);
        for (int i = 0; i < AUDIO_GRAIN * 2; i++) { int v = abs(buf[i]); if (v > pk) pk = v; }
    }
    return pk;
}

int main(void) {
    setenv("SDL_VIDEODRIVER", "dummy", 1);
    if (SDL_Init(SDL_INIT_VIDEO) != 0) { printf("SDL_Init: %s\n", SDL_GetError()); return 2; }
    SDL_Window *win = SDL_CreateWindow("test", 0, 0, SCREEN_W, SCREEN_H, SDL_WINDOW_HIDDEN);
    if (!win) { printf("window: %s\n", SDL_GetError()); return 2; }
    SDL_SetHint(SDL_HINT_RENDER_DRIVER, "software");
    SDL_Renderer *ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_SOFTWARE);
    if (!ren) { printf("renderer: %s\n", SDL_GetError()); return 2; }
    ren_g = ren;
    surf = SDL_GetWindowSurface(win);
    remove("pongbrew2.cfg");

    plat_init();
    audio_init();
    game_init(win, ren);

    printf("== intro and menu\n");
    ticks(70); shot("01_intro");
    CHECK(game_dbg(DBG_SCREEN) == SC_INTRO, "starts on the intro screen");
    ticks(30); press(B_CROSS);
    CHECK(game_dbg(DBG_SCREEN) == SC_MENU, "any button skips intro to the main menu");
    CHECK(audio_music_selected() == TRACK_MENU, "menu song selected");
    ticks(60); shot("02_menu");

    printf("== audio\n");
    audio_set_volumes(10, 8);
    mix_rms(AUDIO_RATE * 8);                  /* the song opens with a very quiet fade-in */
    CHECK(audio_music_level(TRACK_MENU) > 0.99f, "menu song fade-in reached full level (%.2f)", audio_music_level(TRACK_MENU));
    {
        /* compare one second of the game's decoded output with ffmpeg's decode of the same file */
        enum { SEG = 48000 };
        static int16_t buf[AUDIO_GRAIN * 2];
        static float got[SEG];
        for (int done = 0; done < SEG; done += AUDIO_GRAIN) {
            audio_mix(buf, AUDIO_GRAIN);
            for (int i = 0; i < AUDIO_GRAIN && done + i < SEG; i++) got[done + i] = buf[2 * i];
        }
        FILE *rf = fopen("test/out/ref.raw", "rb");
        if (!rf) {
            printf("  skip: test/out/ref.raw missing (needs ffmpeg), skipping decoder comparison\n");
        } else {
            long start = (long)AUDIO_RATE * 9;     /* 8 s warm-up + the second above begins at 8 s: use 8..9 */
            start = (long)AUDIO_RATE * 8;
            static int16_t ref[SEG + 8000][2];
            fseek(rf, start * 4 - 4000 * 4, SEEK_SET);
            size_t n = fread(ref, 4, SEG + 8000, rf);
            fclose(rf);
            double best = -1; int bestlag = 0;
            if (n == (size_t)(SEG + 8000)) {
                for (int lag = 0; lag <= 8000; lag += 1) {
                    double sxy = 0, sxx = 0, syy = 0;
                    for (int i = 0; i < SEG; i += 8) {
                        double x = got[i], y = ref[i + lag][0];
                        sxy += x * y; sxx += x * x; syy += y * y;
                    }
                    double c = sxy / (sqrt(sxx * syy) + 1e-9);
                    if (c > best) { best = c; bestlag = lag; }
                }
            }
            CHECK(best > 0.98, "decoded music matches ffmpeg's decode (correlation %.4f at %d samples)", best, bestlag - 4000);
        }
        double rms = mix_rms(AUDIO_RATE);
        CHECK(rms > 800.0, "menu music is audible (rms %.0f)", rms);
    }
    mix_rms(AUDIO_RATE * 200);                /* run past the end of the song: it must loop without crashing */
    CHECK(audio_music_level(TRACK_MENU) > 0.99f, "menu song keeps playing across the loop point");
    int pk = peak_after_sfx(SFX_HIT, 4800);
    CHECK(pk > 3000, "hit sound effect is audible (peak %d)", pk);
    pk = peak_after_sfx(SFX_WIN, 48000);
    CHECK(pk > 3000, "win jingle is audible (peak %d)", pk);

    printf("== menu navigation\n");
    press(B_DOWN); press(B_DOWN);
    shot("03_menu_sel2");
    press(B_UP); press(B_UP);
    press(B_CROSS);
    CHECK(game_dbg(DBG_SCREEN) == SC_MODE, "PLAY opens the mode menu");
    ticks(10); press(B_DOWN); shot("04_mode");
    press(B_CIRCLE);
    CHECK(game_dbg(DBG_SCREEN) == SC_MENU, "circle goes back");
    press(B_DOWN); press(B_CROSS);
    CHECK(game_dbg(DBG_SCREEN) == SC_SETTINGS, "SETTINGS opens");
    ticks(10); shot("05_settings");
    {
        int m0 = g_set.music;
        press(B_LEFT);
        CHECK(g_set.music == m0 - 1, "left lowers music volume (%d -> %d)", m0, g_set.music);
        press(B_RIGHT);
        CHECK(g_set.music == m0, "right raises it again");
        press(B_DOWN); press(B_DOWN); press(B_RIGHT);
        CHECK(g_set.rumble == 0, "rumble toggles off");
        press(B_LEFT);
        CHECK(g_set.rumble == 1, "rumble toggles on");
        press(B_DOWN); press(B_DOWN); press(B_RIGHT); press(B_RIGHT);
        CHECK(g_set.difficulty == 3, "difficulty reaches INSANE (%d)", g_set.difficulty);
        press(B_LEFT); press(B_LEFT); press(B_LEFT);
        CHECK(g_set.difficulty == 0, "difficulty wraps down to EASY (%d)", g_set.difficulty);
        g_set.difficulty = 1;
        for (int i = 0; i < 4; i++) press(B_DOWN);
        shot("05b_settings_theme");
        press(B_RIGHT);
        CHECK(g_set.theme == 1, "theme changes");
        press(B_LEFT);
    }
    press(B_CIRCLE);
    press(B_DOWN); press(B_CROSS);
    CHECK(game_dbg(DBG_SCREEN) == SC_STATS, "STATS opens"); ticks(10); shot("06_stats");
    press(B_CIRCLE);
    press(B_DOWN); press(B_CROSS);
    CHECK(game_dbg(DBG_SCREEN) == SC_CONTROLS, "CONTROLS opens"); ticks(10); shot("07_controls");
    press(B_CIRCLE);
    press(B_DOWN); press(B_CROSS);
    CHECK(game_dbg(DBG_SCREEN) == SC_CREDITS, "CREDITS opens"); ticks(240); shot("08_credits");
    press(B_CIRCLE);
    CHECK(game_dbg(DBG_SCREEN) == SC_MENU, "back to menu");

    printf("== vs CPU match\n");
    pc_inject(0, 0, 0, 0, 0, 0);
    game_dbg_start(M_CPU);
    CHECK(game_dbg(DBG_SCREEN) == SC_PLAY, "match starts");
    ticks(100); shot("09_countdown");
    ticks(80);
    CHECK(audio_music_selected() == TRACK_GAME, "game song selected during the match");
    {
        /* crossfade: menu song must fade out before the game song comes in, never both at full level */
        float worst = 0;
        for (int i = 0; i < AUDIO_RATE * 6 / AUDIO_GRAIN; i++) {
            static int16_t b2[AUDIO_GRAIN * 2];
            audio_mix(b2, AUDIO_GRAIN);
            float sum = audio_music_level(TRACK_MENU) + audio_music_level(TRACK_GAME);
            if (sum > worst) worst = sum;
        }
        CHECK(worst < 1.06f, "menu fade-out and game fade-in do not stack (peak combined gain %.2f)", worst);
        CHECK(audio_music_level(TRACK_MENU) == 0.0f, "menu song is silent during the match");
        CHECK(audio_music_level(TRACK_GAME) > 0.99f, "game song reached full level (%.2f)", audio_music_level(TRACK_GAME));
        double r = mix_rms(AUDIO_RATE * 2);
        CHECK(r > 500.0, "game song is audible (rms %.0f)", r);
    }
    for (int i = 0; i < 90; i++) { bot(0); game_tick(); }
    shot("10_live");
    game_dbg_spawn_pickup(3);
    for (int i = 0; i < 60; i++) { bot(0); game_tick(); }
    shot("11_pickup");
    int lightCalls0 = pc_light_calls, rumbleCalls0 = pc_rumble_calls;
    int maxRally = 0, scored = 0, state_seen[4] = { 0, 0, 0, 0 };
    for (int i = 0; i < 40000 && game_dbg(DBG_SCREEN) == SC_PLAY; i++) {
        bot(0); game_tick();
        if (game_dbg(DBG_RALLY) > maxRally) maxRally = game_dbg(DBG_RALLY);
        state_seen[game_dbg(DBG_PSTATE)] = 1;
        if (i == 700) shot("12_midgame");
    }
    scored = game_dbg(DBG_SCORE0) + game_dbg(DBG_SCORE1);
    CHECK(maxRally >= 4, "rallies happen (longest %d)", maxRally);
    CHECK(scored >= 1, "points get scored (%d - %d)", game_dbg(DBG_SCORE0), game_dbg(DBG_SCORE1));
    CHECK(game_dbg(DBG_SCREEN) == SC_OVER, "match reaches game over (screen %d)", game_dbg(DBG_SCREEN));
    CHECK(pc_light_calls > lightCalls0, "light bar is updated during play (%d calls)", pc_light_calls - lightCalls0);
    CHECK(pc_rumble_calls > rumbleCalls0, "rumble is driven during play (%d calls)", pc_rumble_calls - rumbleCalls0);
    ticks(40); shot("13_over");
    CHECK(g_stats.played >= 1, "stats count the match (%d played)", g_stats.played);

    printf("== pause\n");

    pc_inject(0, 0, 0, 0, 0, 0);
    press(B_DOWN); press(B_CROSS);                 /* MAIN MENU */
    game_dbg_start(M_CPU); ticks(240);
    press(B_OPTIONS);
    CHECK(game_dbg(DBG_SCREEN) == SC_PAUSE, "options pauses"); shot("14_pause");
    int s0 = game_dbg(DBG_SCORE0); ticks(120);
    CHECK(game_dbg(DBG_SCORE0) == s0, "game is frozen while paused");
    press(B_OPTIONS);
    CHECK(game_dbg(DBG_SCREEN) == SC_PLAY, "options resumes");

    printf("== survival\n");
    pc_inject(0, 0, 0, 0, 0, 0);
    game_dbg_start(M_SURV); ticks(240);
    shot("15_survival_start");
    int lives0 = game_dbg(DBG_LIVES);
    for (int i = 0; i < 1500 && game_dbg(DBG_SCREEN) == SC_PLAY && game_dbg(DBG_LIVES) == lives0; i++) game_tick();
    CHECK(game_dbg(DBG_LIVES) == lives0 - 1, "missing the ball costs a life (%d -> %d)", lives0, game_dbg(DBG_LIVES));
    for (int i = 0; i < 2500 && game_dbg(DBG_SCREEN) == SC_PLAY; i++) { bot(0); game_tick(); if (i == 900) shot("16_survival_play"); }
    CHECK(game_dbg(DBG_SURV) > 0, "survival scores points (%d)", game_dbg(DBG_SURV));
    shot("17_survival_end");

    printf("== two players, two pads\n");
    pc_inject(0, 0, 0, 0, 0, 0); pc_inject(1, 0, 0, 0, 0, 0);
    game_dbg_start(M_2P); ticks(200);
    for (int i = 0; i < 500; i++) { bot(0); bot(1); game_tick(); }
    shot("18_two_players");
    CHECK(game_dbg(DBG_MODE) == M_2P && game_dbg(DBG_SCREEN) == SC_PLAY, "2 player match runs");
    int pr = pc_light[1][0] + pc_light[1][1] + pc_light[1][2];
    CHECK(pr > 0, "second pad gets its own light color (%d,%d,%d)", pc_light[1][0], pc_light[1][1], pc_light[1][2]);

    printf("== audio stops outside the menu\n");
    game_dbg_goto(SC_MENU);
    CHECK(audio_music_selected() == TRACK_MENU, "back in the menu selects the menu song");
    mix_rms(AUDIO_RATE * 6);
    CHECK(audio_music_level(TRACK_GAME) == 0.0f && audio_music_level(TRACK_MENU) > 0.99f, "game song faded out, menu song back (%.2f / %.2f)", audio_music_level(TRACK_GAME), audio_music_level(TRACK_MENU));
    audio_music_select(TRACK_NONE);
    mix_rms(AUDIO_RATE * 2);
    CHECK(audio_music_level(TRACK_MENU) < 0.05f, "music faded out (%.3f)", audio_music_level(TRACK_MENU));

    printf("== performance (PC software renderer, relative numbers; the PS4 CPU is roughly 10x slower)\n");
    {
        static const struct { int sc; const char *name; } L[] = {
            { SC_INTRO, "intro" }, { SC_MENU, "menu" }, { SC_MODE, "mode select" }, { SC_SETTINGS, "settings" },
            { SC_STATS, "stats" }, { SC_CONTROLS, "controls" }, { SC_CREDITS, "credits" } };
        double worst = 0;
        for (unsigned k = 0; k < sizeof L / sizeof L[0]; k++) {
            game_dbg_goto(L[k].sc);
            ticks(200);
            Uint32 t0 = SDL_GetTicks();
            for (int i = 0; i < 40; i++) { game_tick(); game_draw(); }
            double ms = (SDL_GetTicks() - t0) / 40.0; if (ms > worst) worst = ms;
            printf("  %-12s %5.1f ms/frame\n", L[k].name, ms);
        }
        pc_inject(0, 0, 0, 0, 0, 0);
        game_dbg_start(M_CPU);
        ticks(30); { Uint32 t0 = SDL_GetTicks(); for (int i = 0; i < 40; i++) { game_tick(); game_draw(); }
            double ms = (SDL_GetTicks() - t0) / 40.0; if (ms > worst) worst = ms; printf("  %-12s %5.1f ms/frame\n", "countdown", ms); }
        ticks(260);
        { Uint32 t0 = SDL_GetTicks(); for (int i = 0; i < 120; i++) { bot(0); game_tick(); game_draw(); }
            double ms = (SDL_GetTicks() - t0) / 120.0; if (ms > worst) worst = ms; printf("  %-12s %5.1f ms/frame\n", "playing", ms); }
        game_dbg_goto(SC_PAUSE); ticks(5); game_draw();
        { Uint32 t0 = SDL_GetTicks(); for (int i = 0; i < 40; i++) { game_tick(); game_draw(); }
            double ms = (SDL_GetTicks() - t0) / 40.0; if (ms > worst) worst = ms; printf("  %-12s %5.1f ms/frame\n", "pause", ms); }
        printf("  worst screen: %.1f ms/frame\n", worst);
    }

    printf("\n%d checks, %d failed\n", checks, fails);
    game_shutdown();
    return fails ? 1 : 0;
}
