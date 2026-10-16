#include "gfx.h"
#if GFX_DIV == 1
#include "font_data.h"
#else
#include "font_data_540.h"
#endif
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if defined(__SSE2__) && defined(__has_include)
#if __has_include(<emmintrin.h>)
#include <emmintrin.h>
#define GFX_SSE2 1
#endif
#endif

typedef Uint32 u32;
typedef Uint8 u8;

struct GfxImg { SDL_Surface *s; };

static SDL_Window *g_win;
static SDL_Renderer *g_ren;
static SDL_Surface *cs, *ss;           /* canvas and snapshot, both ARGB8888 */
static SDL_Texture *t_up;              /* only used when the renderer is not the software one */
static int g_div = GFX_DIV, CW, CH;
static int snap_ok;
static int ox, oy;
static int g_soft;

/* ---------------------------------------------------------------- setup */
void gfx_init(SDL_Window *win, SDL_Renderer *ren) {
    g_win = win; g_ren = ren;
    CW = SCREEN_W / g_div; CH = SCREEN_H / g_div;
    cs = SDL_CreateRGBSurfaceWithFormat(0, CW, CH, 32, SDL_PIXELFORMAT_ARGB8888);
    ss = SDL_CreateRGBSurfaceWithFormat(0, CW, CH, 32, SDL_PIXELFORMAT_ARGB8888);
    SDL_RendererInfo info; memset(&info, 0, sizeof info);
    if (ren) SDL_GetRendererInfo(ren, &info);
    g_soft = (info.flags & SDL_RENDERER_SOFTWARE) != 0;
    SDL_Log("PONG: canvas %dx%d, renderer %s (%s present)", CW, CH, info.name ? info.name : "none", g_soft ? "window-surface" : "texture");
}

void gfx_shutdown(void) {
    if (cs) SDL_FreeSurface(cs);
    if (ss) SDL_FreeSurface(ss);
    if (t_up) SDL_DestroyTexture(t_up);
    cs = ss = NULL; t_up = NULL;
}

SDL_Surface *gfx_canvas(void) { return cs; }
void gfx_begin_frame(void) { }

/* The PS4 SDL port's window surface is BGR888 (red in the low byte), not ARGB8888. Converting through
   SDL_BlitScaled took about 80 ms a frame on the console, so the canvas is doubled and byte-swapped by hand. */
static void upscale_to_window(SDL_Surface *ws) {
    if (SDL_MUSTLOCK(ws)) SDL_LockSurface(ws);
    int px4 = ws->format->BytesPerPixel == 4 && ws->format->Gmask == 0x0000FF00u;
    int argb = px4 && ws->format->Rmask == 0x00FF0000u && ws->format->Bmask == 0x000000FFu;
    int abgr = px4 && ws->format->Rmask == 0x000000FFu && ws->format->Bmask == 0x00FF0000u;
    int fast = ws->w == SCREEN_W && ws->h == SCREEN_H && (g_div == 1 || g_div == 2) && (argb || abgr);
    if (fast) {
        const u32 *src = (const u32 *)cs->pixels; int sp = cs->pitch / 4;
        u32 *dst = (u32 *)ws->pixels; int dp = ws->pitch / 4;
        if (g_div == 1) {                         /* native 1080p: one pass, byte swap only when the window wants it */
            for (int y = 0; y < CH; y++) {
                const u32 *s = src + (size_t)y * sp;
                u32 *d0 = dst + (size_t)y * dp;
                if (!abgr) { memcpy(d0, s, (size_t)CW * 4); continue; }
#ifdef GFX_SSE2
                const __m128i mk = _mm_set1_epi32((int)0xFF00FF00u), m8 = _mm_set1_epi32(0xFF);
                for (int x = 0; x < CW; x += 4) {
                    __m128i v = _mm_loadu_si128((const __m128i *)(s + x));
                    __m128i r = _mm_and_si128(_mm_srli_epi32(v, 16), m8);
                    __m128i b = _mm_slli_epi32(_mm_and_si128(v, m8), 16);
                    _mm_storeu_si128((__m128i *)(d0 + x), _mm_or_si128(_mm_and_si128(v, mk), _mm_or_si128(r, b)));
                }
#else
                for (int x = 0; x < CW; x++) { u32 p = s[x]; d0[x] = (p & 0xFF00FF00u) | ((p >> 16) & 0xFFu) | ((p & 0xFFu) << 16); }
#endif
            }
        } else
        for (int y = 0; y < CH; y++) {
            const u32 *s = src + (size_t)y * sp;
            u32 *d0 = dst + (size_t)(2 * y) * dp;
#ifdef GFX_SSE2
            const __m128i mk = _mm_set1_epi32((int)0xFF00FF00u), m8 = _mm_set1_epi32(0xFF);
            for (int x = 0; x < CW; x += 4) {
                __m128i v = _mm_loadu_si128((const __m128i *)(s + x));
                if (abgr) {
                    __m128i r = _mm_and_si128(_mm_srli_epi32(v, 16), m8);
                    __m128i b = _mm_slli_epi32(_mm_and_si128(v, m8), 16);
                    v = _mm_or_si128(_mm_and_si128(v, mk), _mm_or_si128(r, b));
                }
                _mm_storeu_si128((__m128i *)(d0 + 2 * x), _mm_unpacklo_epi32(v, v));
                _mm_storeu_si128((__m128i *)(d0 + 2 * x + 4), _mm_unpackhi_epi32(v, v));
            }
#else
            for (int x = 0; x < CW; x++) {
                u32 p = s[x];
                if (abgr) p = (p & 0xFF00FF00u) | ((p >> 16) & 0xFFu) | ((p & 0xFFu) << 16);
                d0[2 * x] = p; d0[2 * x + 1] = p;
            }
#endif
            memcpy(d0 + dp, d0, (size_t)CW * 2 * 4);
        }
    } else {
        SDL_BlitScaled(cs, NULL, ws, NULL);       /* PC window of another size, or an unusual pixel format */
    }
    if (SDL_MUSTLOCK(ws)) SDL_UnlockSurface(ws);
}

void gfx_end_frame(void) {
    if (g_soft && g_win) {
        SDL_Surface *ws = SDL_GetWindowSurface(g_win);
        if (ws) { upscale_to_window(ws); return; }
    }
    if (g_ren) {                                  /* accelerated renderer: upload the canvas and let the GPU stretch it */
        if (!t_up) t_up = SDL_CreateTexture(g_ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, CW, CH);
        if (t_up) { SDL_UpdateTexture(t_up, NULL, cs->pixels, cs->pitch); SDL_RenderCopy(g_ren, t_up, NULL, NULL); }
    }
}

/* ---------------------------------------------------------------- pixel helpers */
static int to_c(int v) { return (int)floorf((float)v / (float)g_div + 0.5f); }

static inline u32 pack(int r, int g, int b) { return 0xFF000000u | ((u32)(r & 255) << 16) | ((u32)(g & 255) << 8) | (u32)(b & 255); }
static inline int clamp255(int v) { return v < 0 ? 0 : (v > 255 ? 255 : v); }

/* d = d*(1-a) + col*a, with srb = (col & 0x00FF00FF) * a and sg = ((col >> 8) & 255) * a precomputed */
static inline u32 mix(u32 d, u32 srb, u32 sg, u32 ia) {
    u32 rb = (d & 0x00FF00FFu) * ia + srb;
    rb = ((rb + 0x00800080u + ((rb >> 8) & 0x00FF00FFu)) >> 8) & 0x00FF00FFu;
    u32 g = ((d >> 8) & 255u) * ia + sg;
    g = (g + 128u + (g >> 8)) >> 8;
    return 0xFF000000u | rb | (g << 8);
}
/* d = min(255, d + col*a/255) */
static inline u32 addpx(u32 d, u32 srb, u32 sg) {
    u32 rb = (srb + 0x00800080u + ((srb >> 8) & 0x00FF00FFu)) >> 8 & 0x00FF00FFu;
    u32 g = (sg + 128u + (sg >> 8)) >> 8;
    u32 s = (d & 0x00FF00FFu) + rb;
    s = (s | (((s >> 8) & 0x00010001u) * 255u)) & 0x00FF00FFu;
    u32 gg = ((d >> 8) & 255u) + g; if (gg > 255u) gg = 255u;
    return 0xFF000000u | s | (gg << 8);
}

static inline u32 *row(SDL_Surface *s, int y) { return (u32 *)((u8 *)s->pixels + (size_t)y * s->pitch); }

/* ---------------------------------------------------------------- snapshot (pause, game over) */
void gfx_snapshot_capture(int dim) {
    if (!cs || !ss) return;
    if (dim > 0) gfx_rect(0, 0, SCREEN_W, SCREEN_H, 0, 0, 12, dim);
    memcpy(ss->pixels, cs->pixels, (size_t)cs->pitch * CH);
    snap_ok = 1;
}
void gfx_snapshot_draw(void) { if (snap_ok) memcpy(cs->pixels, ss->pixels, (size_t)cs->pitch * CH); }
int  gfx_snapshot_valid(void) { return snap_ok; }
void gfx_snapshot_clear(void) { snap_ok = 0; }

/* ---------------------------------------------------------------- images */
static SDL_Surface *box_down(SDL_Surface *s, int d) {
    SDL_Surface *src = SDL_ConvertSurfaceFormat(s, SDL_PIXELFORMAT_ARGB8888, 0);
    if (!src) return NULL;
    int nw = src->w / d, nh = src->h / d;
    SDL_Surface *dst = SDL_CreateRGBSurfaceWithFormat(0, nw, nh, 32, SDL_PIXELFORMAT_ARGB8888);
    if (!dst) { SDL_FreeSurface(src); return NULL; }
    for (int y = 0; y < nh; y++) {
        u32 *o = row(dst, y);
        for (int x = 0; x < nw; x++) {
            unsigned a = 0, r = 0, g = 0, b = 0;
            for (int j = 0; j < d; j++) {
                const u32 *p = row(src, y * d + j) + x * d;
                for (int i = 0; i < d; i++) { u32 c = p[i]; a += c >> 24; r += (c >> 16) & 255; g += (c >> 8) & 255; b += c & 255; }
            }
            unsigned n = (unsigned)(d * d);
            o[x] = ((a / n) << 24) | ((r / n) << 16) | ((g / n) << 8) | (b / n);
        }
    }
    SDL_FreeSurface(src);
    return dst;
}

GfxImg *gfx_load_bmp(const char *name, int *w, int *h) {
    char path[160];
    const char *roots[] = { "/app0/assets/", "assets/", "../assets/" };
    for (int i = 0; i < 3; i++) {
        snprintf(path, sizeof path, "%s%s", roots[i], name);
        SDL_Surface *s = SDL_LoadBMP(path);
        if (!s) continue;
        if (w) *w = s->w;
        if (h) *h = s->h;
        SDL_Surface *use = g_div > 1 ? box_down(s, g_div) : SDL_ConvertSurfaceFormat(s, SDL_PIXELFORMAT_ARGB8888, 0);
        SDL_FreeSurface(s);
        if (!use) continue;
        GfxImg *im = (GfxImg *)malloc(sizeof *im);
        if (!im) { SDL_FreeSurface(use); continue; }
        im->s = use;
        SDL_Log("PONG: loaded %s", path);
        return im;
    }
    SDL_Log("PONG: could not load %s", name);
    return NULL;
}

GfxImg *gfx_gradient(int r0, int g0, int b0, int r1, int g1, int b1) {
    SDL_Surface *s = SDL_CreateRGBSurfaceWithFormat(0, CW, CH, 32, SDL_PIXELFORMAT_ARGB8888);
    if (!s) return NULL;
    for (int y = 0; y < CH; y++) {
        float t = (float)y / (float)(CH - 1);
        u32 c = pack((int)(r0 + (r1 - r0) * t), (int)(g0 + (g1 - g0) * t), (int)(b0 + (b1 - b0) * t));
        u32 *d = row(s, y);
        for (int x = 0; x < CW; x++) d[x] = c;
    }
    GfxImg *im = (GfxImg *)malloc(sizeof *im);
    if (!im) { SDL_FreeSurface(s); return NULL; }
    im->s = s;
    return im;
}

void gfx_img_free(GfxImg *im) { if (im) { SDL_FreeSurface(im->s); free(im); } }

void gfx_draw_bg(GfxImg *im) {
    if (!im) { SDL_FillRect(cs, NULL, 0xFF080C22u); return; }
    if (im->s->w == CW && im->s->h == CH) memcpy(cs->pixels, im->s->pixels, (size_t)cs->pitch * CH);
    else { SDL_SetSurfaceBlendMode(im->s, SDL_BLENDMODE_NONE); SDL_BlitScaled(im->s, NULL, cs, NULL); }
}

void gfx_copy(GfxImg *im, int x, int y, int w, int h, int alpha) {
    if (!im || alpha <= 0) return;
    int x0 = to_c(x + ox), y0 = to_c(y + oy);
    int dw = to_c(x + ox + w) - x0, dh = to_c(y + oy + h) - y0;
    if (dw <= 0 || dh <= 0) return;
    SDL_Surface *s = im->s;
    u32 ia = (u32)(255 - clamp255(alpha)), a = (u32)clamp255(alpha);
    for (int j = 0; j < dh; j++) {
        int dy = y0 + j; if (dy < 0 || dy >= CH) continue;
        u32 *d = row(cs, dy);
        /* source row by nearest, columns by bilinear when the size differs */
        float fy = (j + 0.5f) * s->h / dh - 0.5f; if (fy < 0) fy = 0;
        int sy = (int)fy, sy1 = sy + 1 < s->h ? sy + 1 : sy; float wy = fy - sy;
        const u32 *r0 = row(s, sy), *r1 = row(s, sy1);
        for (int i = 0; i < dw; i++) {
            int dx = x0 + i; if (dx < 0 || dx >= CW) continue;
            u32 p;
            if (dw == s->w && dh == s->h) p = r0[i];
            else {
                float fx = (i + 0.5f) * s->w / dw - 0.5f; if (fx < 0) fx = 0;
                int sx = (int)fx, sx1 = sx + 1 < s->w ? sx + 1 : sx; float wx = fx - sx;
                u32 c00 = r0[sx], c10 = r0[sx1], c01 = r1[sx], c11 = r1[sx1];
                u32 out = 0xFF000000u;
                for (int sh = 0; sh <= 16; sh += 8) {
                    float v = (1 - wy) * ((1 - wx) * ((c00 >> sh) & 255) + wx * ((c10 >> sh) & 255)) +
                              wy * ((1 - wx) * ((c01 >> sh) & 255) + wx * ((c11 >> sh) & 255));
                    out |= (u32)(v + 0.5f) << sh;
                }
                p = out;
            }
            if (a >= 255) d[dx] = p;
            else d[dx] = mix(d[dx], (p & 0x00FF00FFu) * a, ((p >> 8) & 255u) * a, ia);
        }
    }
}

/* ---------------------------------------------------------------- shapes */
void gfx_set_offset(int x, int y) { ox = x; oy = y; }

void gfx_rect(int x, int y, int w, int h, int r, int g, int b, int a) {
    if (a <= 0 || w <= 0 || h <= 0) return;
    int x0 = to_c(x + ox), y0 = to_c(y + oy), x1 = to_c(x + ox + w), y1 = to_c(y + oy + h);
    if (x1 <= x0) x1 = x0 + 1;
    if (y1 <= y0) y1 = y0 + 1;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > CW) x1 = CW;
    if (y1 > CH) y1 = CH;
    if (x1 <= x0 || y1 <= y0) return;
    u32 col = pack(r, g, b);
    if (a >= 255) {
        SDL_Rect q = { x0, y0, x1 - x0, y1 - y0 };
        SDL_FillRect(cs, &q, col);
        return;
    }
    u32 av = (u32)a, ia = 255u - av, srb = (col & 0x00FF00FFu) * av, sg = ((col >> 8) & 255u) * av;
    for (int yy = y0; yy < y1; yy++) {
        u32 *d = row(cs, yy);
        for (int xx = x0; xx < x1; xx++) d[xx] = mix(d[xx], srb, sg, ia);
    }
}

void gfx_frame(int x, int y, int w, int h, int t, int r, int g, int b, int a) {
    gfx_rect(x, y, w, t, r, g, b, a);
    gfx_rect(x, y + h - t, w, t, r, g, b, a);
    gfx_rect(x, y + t, t, h - 2 * t, r, g, b, a);
    gfx_rect(x + w - t, y + t, t, h - 2 * t, r, g, b, a);
}

void gfx_hline_dash(int x0, int x1, int y, int dash, int gap, int th, int r, int g, int b, int a) {
    for (int x = x0; x < x1; x += dash + gap) gfx_rect(x, y, dash, th, r, g, b, a);
}

/* An 8-bit coverage mask drawn in one color with a global alpha; add = additive light instead of normal blending. */
static void blit_mask(const u8 *m, int mstride, int mw, int mh, int dx, int dy, int r, int g, int b, int a, int add) {
    if (a <= 0) return;
    int sx0 = 0, sy0 = 0, w = mw, h = mh;
    if (dx < 0) { sx0 = -dx; w += dx; dx = 0; }
    if (dy < 0) { sy0 = -dy; h += dy; dy = 0; }
    if (dx + w > CW) w = CW - dx;
    if (dy + h > CH) h = CH - dy;
    if (w <= 0 || h <= 0) return;
    u32 col = pack(r, g, b), A = (u32)clamp255(a);
    u32 crb = col & 0x00FF00FFu, cg = (col >> 8) & 255u;
    for (int j = 0; j < h; j++) {
        const u8 *s = m + (size_t)(sy0 + j) * mstride + sx0;
        u32 *d = row(cs, dy + j) + dx;
        if (add) {
            for (int i = 0; i < w; i++) {
                u32 mv = s[i]; if (!mv) continue;
                u32 av = A == 255 ? mv : (mv * A + 127u) / 255u;
                d[i] = addpx(d[i], crb * av, cg * av);
            }
        } else {
            for (int i = 0; i < w; i++) {
                u32 mv = s[i]; if (!mv) continue;
                u32 av = A == 255 ? mv : (mv * A + 127u) / 255u;
                if (av >= 255u) d[i] = col;
                else d[i] = mix(d[i], crb * av, cg * av, 255u - av);
            }
        }
    }
}

/* same, scaled to dw x dh with bilinear sampling (only for text sizes that have no exact atlas) */
static void blit_mask_scaled(const u8 *m, int mstride, int mw, int mh, int dx, int dy, int dw, int dh, int r, int g, int b, int a) {
    if (a <= 0 || dw <= 0 || dh <= 0) return;
    u32 col = pack(r, g, b), A = (u32)clamp255(a);
    u32 crb = col & 0x00FF00FFu, cg = (col >> 8) & 255u;
    float kx = (float)mw / dw, ky = (float)mh / dh;
    for (int j = 0; j < dh; j++) {
        int yy = dy + j; if (yy < 0 || yy >= CH) continue;
        float fy = (j + 0.5f) * ky - 0.5f; if (fy < 0) fy = 0;
        int sy = (int)fy, sy1 = sy + 1 < mh ? sy + 1 : sy; float wy = fy - sy;
        const u8 *r0 = m + (size_t)sy * mstride, *r1 = m + (size_t)sy1 * mstride;
        u32 *d = row(cs, yy);
        for (int i = 0; i < dw; i++) {
            int xx = dx + i; if (xx < 0 || xx >= CW) continue;
            float fx = (i + 0.5f) * kx - 0.5f; if (fx < 0) fx = 0;
            int sx = (int)fx, sx1 = sx + 1 < mw ? sx + 1 : sx; float wx = fx - sx;
            float v = (1 - wy) * ((1 - wx) * r0[sx] + wx * r0[sx1]) + wy * ((1 - wx) * r1[sx] + wx * r1[sx1]);
            u32 mv = (u32)(v + 0.5f); if (!mv) continue;
            u32 av = A == 255 ? mv : (mv * A + 127u) / 255u;
            d[xx] = mix(d[xx], crb * av, cg * av, 255u - av);
        }
    }
}

/* ---------------------------------------------------------------- discs and glows (masks cached per size) */
#define CIRC_MAX 200
#define GLOW_STEP 8
#define GLOW_MAX 576
static u8 *m_circ[CIRC_MAX + 1];
static u8 *m_glw[GLOW_MAX / GLOW_STEP + 1];

static u8 *make_disc(int d) {
    u8 *a = (u8 *)malloc((size_t)d * d);
    if (!a) return NULL;
    float rad = d / 2.0f;
    for (int y = 0; y < d; y++) for (int x = 0; x < d; x++) {
        float dx = x + 0.5f - rad, dy = y + 0.5f - rad;
        float v = rad - sqrtf(dx * dx + dy * dy); if (v < 0) v = 0; if (v > 1) v = 1;
        a[y * d + x] = (u8)(v * 255);
    }
    return a;
}
static u8 *make_glow(int d) {
    u8 *a = (u8 *)malloc((size_t)d * d);
    if (!a) return NULL;
    float rad = d / 2.0f;
    for (int y = 0; y < d; y++) for (int x = 0; x < d; x++) {
        float dx = x + 0.5f - rad, dy = y + 0.5f - rad;
        float v = 1.0f - sqrtf(dx * dx + dy * dy) / rad; if (v < 0) v = 0;
        a[y * d + x] = (u8)(v * v * v * 255);
    }
    return a;
}

void gfx_circle(float cx, float cy, float rad, int r, int g, int b, int a) {
    if (a <= 0 || rad < 1.0f) return;
    int d = (int)(rad * 2.0f / (float)g_div + 0.5f); if (d < 2) d = 2;
    if (d > CIRC_MAX) d = CIRC_MAX;
    if (!m_circ[d]) m_circ[d] = make_disc(d);
    if (!m_circ[d]) return;
    int px = (int)((cx + ox) / g_div - d / 2.0f + 0.5f), py = (int)((cy + oy) / g_div - d / 2.0f + 0.5f);
    blit_mask(m_circ[d], d, d, d, px, py, r, g, b, a, 0);
}

void gfx_glow(float cx, float cy, float rad, int r, int g, int b, int a) {
    if (a <= 0 || rad < 1.0f) return;
    int d = (int)(rad * 2.0f / (float)g_div / GLOW_STEP + 0.5f) * GLOW_STEP;
    if (d < GLOW_STEP) d = GLOW_STEP;
    if (d > GLOW_MAX) d = GLOW_MAX;
    int i = d / GLOW_STEP;
    if (!m_glw[i]) m_glw[i] = make_glow(d);
    if (!m_glw[i]) return;
    int px = (int)((cx + ox) / g_div - d / 2.0f + 0.5f), py = (int)((cy + oy) / g_div - d / 2.0f + 0.5f);
    blit_mask(m_glw[i], d, d, d, px, py, r, g, b, a, 1);
}

/* ---------------------------------------------------------------- text */
/* Picks the atlas whose pixel size is closest to the size on the canvas. */
static int pick(float size) {
    float s = size / (float)g_div;
    int best = 0; float bd = 1e9f;
    for (int i = 0; i < FONT_N; i++) {
        float d = fabsf(logf(s / (float)FONTS[i].px));
        if (d < bd) { bd = d; best = i; }
    }
    return best;
}
static float ratio_for(float size, int k) { return (size / (float)g_div) / (float)FONTS[k].px; }

/* index of a character inside an atlas; atlases for big sizes only hold ASCII 32..95, so lowercase draws as capitals */
static inline int cidx(const FontAtlas *F, int c) {
    if (c < 32 || c > 126) c = '?';
    if (c - 32 >= F->nch) { if (c >= 'a' && c <= 'z') c -= 32; else c = '?'; }
    return c - 32;
}

int gfx_text_h(float size) { return (int)(size * 1.62f); }

int gfx_text_w(const char *s, float size) {
    int k = pick(size);
    float sc = ratio_for(size, k) * (float)g_div, w = 0;
    const float *adv = FONTS[k].adv;
    for (; *s; s++) w += adv[cidx(&FONTS[k], (unsigned char)*s)] * sc;
    return (int)w;
}

void gfx_text(int x, int y, float size, const char *s, int r, int g, int b, int a, int align) {
    if (!s || a <= 0) return;
    int k = pick(size);
    const FontAtlas *F = &FONTS[k];
    float rt = ratio_for(size, k);
    float cx = (float)(x + ox) / (float)g_div;                    /* canvas pixels */
    float cy = (float)(y + oy) / (float)g_div;
    if (align == AL_CENTER) cx -= (float)gfx_text_w(s, size) / 2.0f / (float)g_div;
    else if (align == AL_RIGHT) cx -= (float)gfx_text_w(s, size) / (float)g_div;
    int exact = fabsf(rt - 1.0f) < 0.04f;
    if (exact) rt = 1.0f;
    int dw = (int)(F->cell_w * rt + 0.5f), dh = (int)(F->cell_h * rt + 0.5f);
    int top = (int)(cy + 0.5f);
    if (top >= CH || top + dh < 0) return;
    for (; *s; s++) {
        int c = (unsigned char)*s;
        int i = cidx(F, c);
        if (i != 0) {
            const u8 *src = F->pix + (size_t)(i / F->cols) * F->cell_h * F->atlas_w + (size_t)(i % F->cols) * F->cell_w;
            int dx = (int)(cx - F->pad * rt + 0.5f);
            if (exact) blit_mask(src, F->atlas_w, F->cell_w, F->cell_h, dx, top, r, g, b, a, 0);
            else blit_mask_scaled(src, F->atlas_w, F->cell_w, F->cell_h, dx, top, dw, dh, r, g, b, a);
        }
        cx += F->adv[i] * rt;
    }
}

void gfx_text_shadow(int x, int y, float size, const char *s, int r, int g, int b, int a, int align) {
    if (size >= 50.0f) {
        int d = (int)(size / 14.0f) + 1;
        gfx_text(x + d, y + d, size, s, 0, 0, 0, a * 3 / 4, align);
    }
    gfx_text(x, y, size, s, r, g, b, a, align);
}
