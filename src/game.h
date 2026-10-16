#ifndef GAME_H
#define GAME_H
#include <SDL2/SDL.h>

void game_init(SDL_Window *win, SDL_Renderer *ren);
void game_tick(void);        /* one 60 Hz step: input, logic, sound, rumble, light bar */
void game_draw(void);        /* draws and presents nothing itself; the caller presents */
int  game_quit_requested(void);
void game_shutdown(void);

/* test hooks, used by test/test_main.c only */
enum { DBG_SCREEN, DBG_SCORE0, DBG_SCORE1, DBG_RALLY, DBG_LIVES, DBG_MODE, DBG_PSTATE, DBG_BALLS, DBG_SURV,
       DBG_BX, DBG_BY, DBG_PX0, DBG_PX1 };
int  game_dbg(int what);
void game_dbg_goto(int screen);
void game_dbg_start(int mode);
void game_dbg_ball(float x, float y, float vx, float vy);
void game_dbg_spawn_pickup(int type);
void game_dbg_message(const char *s);

enum { SC_INTRO, SC_MENU, SC_MODE, SC_SETTINGS, SC_STATS, SC_CONTROLS, SC_CREDITS, SC_PLAY, SC_PAUSE, SC_OVER, SC__N };
enum { M_CPU, M_2P, M_SURV, M_PRAC };

#endif
