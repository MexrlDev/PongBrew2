#ifndef GFX_H
#define GFX_H
#include <SDL2/SDL.h>

#define SCREEN_W 1920
#define SCREEN_H 1080

/* The game is laid out in 1920x1080 "game units". It is drawn by this file's own small software rasterizer onto a
   canvas of SCREEN_W/GFX_DIV pixels. GFX_DIV = 1 is native 1920x1080; GFX_DIV = 2 draws 960x540 and doubles it
   (build with make SD=1). SDL's software renderer spends 100+
   instructions per pixel on any blended or color-modulated blit, which is what made the PS4 version crawl.
   */
#ifndef GFX_DIV
#define GFX_DIV 1
#endif

enum { AL_LEFT = 0, AL_CENTER = 1, AL_RIGHT = 2 };

typedef struct GfxImg GfxImg;

/* win and ren are the window and its renderer; they are only used to put the finished canvas on screen.
   After gfx_end_frame() the caller still calls SDL_RenderPresent(ren). */
void gfx_init(SDL_Window *win, SDL_Renderer *ren);
void gfx_shutdown(void);
void gfx_begin_frame(void);
void gfx_end_frame(void);
SDL_Surface *gfx_canvas(void);            /* the 960x540 canvas, for screenshots in the PC test */

void gfx_snapshot_capture(int dim);       /* darken the canvas by dim (0..255) and freeze it, for pause and game over */
void gfx_snapshot_draw(void);
int  gfx_snapshot_valid(void);
void gfx_snapshot_clear(void);

/* Loads assets/<name>.bmp from /app0 (console) or ./assets (PC). Returns NULL if missing.
   w and h report the original size in game units. */
GfxImg *gfx_load_bmp(const char *name, int *w, int *h);
GfxImg *gfx_gradient(int r0, int g0, int b0, int r1, int g1, int b1);   /* used when a background file is missing */
void gfx_img_free(GfxImg *img);
void gfx_draw_bg(GfxImg *img);            /* covers the whole canvas; img NULL draws a dark fill */
void gfx_copy(GfxImg *img, int x, int y, int w, int h, int alpha);

void gfx_set_offset(int ox, int oy);      /* screen shake */
void gfx_rect(int x, int y, int w, int h, int r, int g, int b, int a);
void gfx_frame(int x, int y, int w, int h, int t, int r, int g, int b, int a);
void gfx_hline_dash(int x0, int x1, int y, int dash, int gap, int th, int r, int g, int b, int a);
void gfx_circle(float cx, float cy, float radius, int r, int g, int b, int a);
void gfx_glow(float cx, float cy, float radius, int r, int g, int b, int a);   /* additive */

/* size is in game units (1920x1080 space). y is the top of the line box. */
int  gfx_text_w(const char *s, float size);
void gfx_text_centered(int cx, int cy, float size, const char *s, int r, int g, int b, int a);
void gfx_text(int x, int y, float size, const char *s, int r, int g, int b, int a, int align);
void gfx_text_shadow(int x, int y, float size, const char *s, int r, int g, int b, int a, int align);
int  gfx_text_h(float size);

#endif
