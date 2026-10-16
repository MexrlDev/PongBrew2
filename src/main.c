/* PongBrew 2 entry point (PS4 via OpenOrbis, or PC for testing). */
#include <SDL2/SDL.h>
#include "game.h"
#include "gfx.h"
#include "platform.h"
#ifdef PS4_BUILD
#include <orbis/SystemService.h>
#endif

int main(int argc, char *argv[]) {
    (void)argc; (void)argv;
    SDL_Log("PONG2: start");
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        SDL_Log("PONG2: SDL_Init(VIDEO) failed: %s", SDL_GetError());
        return -1;
    }
    SDL_Log("PONG2: video OK");

#ifdef PS4_BUILD
    int ww = SCREEN_W, wh = SCREEN_H;
#else
    int ww = 1280, wh = 720;
#endif
    SDL_Window *win = SDL_CreateWindow("PONG", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, ww, wh, SDL_WINDOW_SHOWN);
    if (!win) { SDL_Log("PONG2: window failed: %s", SDL_GetError()); SDL_Quit(); return -1; }
    SDL_Log("PONG2: window OK");
#ifdef PS4_BUILD
    sceSystemServiceHideSplashScreen();       /* without this the splash covers the game forever */
    SDL_Log("PONG2: splash hidden");
#endif

    SDL_Renderer *ren = NULL;
    /* the software renderer first: gfx.c draws on its own canvas and copies it straight into the window surface */
    const char *names[] = { "software", "opengles2", "opengl", NULL };
    for (int i = 0; names[i] && !ren; i++) {
        SDL_SetHint(SDL_HINT_RENDER_DRIVER, names[i]);
        ren = SDL_CreateRenderer(win, -1, 0);
        if (ren) {
            SDL_RendererInfo info;
            SDL_GetRendererInfo(ren, &info);
            SDL_Log("PONG2: renderer picked = %s", info.name);
        } else {
            SDL_Log("PONG2: renderer %s failed: %s", names[i], SDL_GetError());
        }
    }
    if (!ren) {
        SDL_SetHint(SDL_HINT_RENDER_DRIVER, "software");
        SDL_SetHint(SDL_HINT_FRAMEBUFFER_ACCELERATION, "0");
        ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_SOFTWARE);
    }
    if (!ren) { SDL_Log("PONG2: FATAL no renderer: %s", SDL_GetError()); return -1; }
    SDL_RenderSetLogicalSize(ren, SCREEN_W, SCREEN_H);

    plat_init();
    game_init(win, ren);
    SDL_Log("PONG2: entering main loop");

    int running = 1;
    Uint32 last = SDL_GetTicks();
    double acc = 0;
    while (running) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) if (e.type == SDL_QUIT) running = 0;

        Uint32 now = SDL_GetTicks();
        acc += (double)(now - last); last = now;
        if (acc > 250.0) acc = 250.0;
        int steps = 0;
        while (acc >= 16.6667 && steps < 6) { game_tick(); acc -= 16.6667; steps++; }
        if (game_quit_requested()) running = 0;

        game_draw();
        SDL_RenderPresent(ren);
        double left = 16.6667 - (acc + (double)(SDL_GetTicks() - now));
        if (left >= 2.0) SDL_Delay((Uint32)(left - 1.0));      /* nothing to simulate yet: do not redraw identical frames */
    }

    game_shutdown();
    plat_shutdown();
#ifdef PS4_BUILD
    plat_exit();                              /* no SDL_Quit and no return: the console closes the app itself */
    for (;;) SDL_Delay(100);                  /* never fall through to SDL teardown on the console */
#else
    SDL_DestroyRenderer(ren);
    SDL_DestroyWindow(win);
    SDL_Quit();
    return 0;
#endif
}
