/* PongBrew 2: menus, four game modes, power-ups, effects, rumble and light bar.
   Platform-independent: it talks to the console only through platform.h and audio.h. */
#include "game.h"
#include "gfx.h"
#include "audio.h"
#include "platform.h"
#include "save.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define W SCREEN_W
#define H SCREEN_H
#define WL 70
#define WR (W - 70)
#define BR 20
#define PH 28
#define PW0 270.0f
#define PY_BOT (H - 130)
#define PY_TOP 100
#define WALL_BOTTOM 114
#define GOAL_BOT (H - 48)
#define GOAL_TOP 48
#define MAXB 4
#define TRAIL 14
#define NPART 360
#define BASE_SPEED 10.5f
#define MAX_SPEED 23.0f
#define PAD_SPEED 19.0f

/* ================================================================ themes */
typedef struct { const char *name; int p1[3], p2[3], ball[3], acc[3]; } Theme;
static const Theme THEMES[4] = {
    { "NEON",   { 110, 225, 255 }, { 255, 110, 140 }, { 255, 235, 140 }, { 255, 90, 180 } },
    { "SUNSET", { 255, 170, 80 },  { 200, 90, 255 },  { 255, 240, 200 }, { 255, 120, 60 } },
    { "MINT",   { 110, 255, 190 }, { 120, 170, 255 }, { 240, 255, 200 }, { 90, 230, 170 } },
    { "MONO",   { 235, 235, 245 }, { 170, 175, 190 }, { 255, 255, 255 }, { 200, 205, 220 } },
};
static const Theme *TH(void) { return &THEMES[g_set.theme]; }

static const char *DIFF_NAMES[4] = { "EASY", "NORMAL", "HARD", "INSANE" };

/* ================================================================ small helpers */
static uint32_t rs = 2463534242u;
static uint32_t rnd(void) { rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5; return rs; }
static float frnd(float lo, float hi) { return lo + (hi - lo) * ((rnd() & 0xFFFF) / 65535.0f); }
static float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
static int clampi(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }
static int mixi(int a, int b, float t) { return (int)(a + (b - a) * t); }

/* ================================================================ state */
static GfxImg *tx_menu, *tx_game, *tx_credit;
static int credit_w, credit_h;
static int scr, scr_t, quit_req;
static unsigned long g_tick;
static int sel[SC__N];
static int reset_confirm;

static PadState pad[2], padp[2];
static int npads = 1;

typedef struct { int up, down, left, right, ok, back, pause, any; } Nav;
static Nav nav;
static int rep[4];
static uint32_t prev_union;

/* ---- match ---- */
typedef struct { float x, vx; int score, wide, shrink, freeze, dash, cd, dashdir; } Paddle;
typedef struct { float x, y, vx, vy; int owner, ghost, on, tn; float tx[TRAIL], ty[TRAIL]; } Ball;
enum { PS_COUNT, PS_SERVE, PS_LIVE, PS_POINT };
enum { PU_WIDE, PU_SHRINK, PU_SLOW, PU_MULTI, PU_FREEZE, PU_LIFE, PU__N };

static Paddle P[2];
static Ball B[MAXB];
static int mode, pstate, pst_t, serve_dir, rally, best_rally_match, lives, winner = -1;
static int match_ticks, surv_score, slow_t, over_new_best;
static float bspeed = BASE_SPEED;
static int cpu_cd;
static float cpu_target = W / 2;
static struct { int on, type, life; float x, y; } pu;
static int pu_timer;
static char msg[48];
static int msg_t, msg_r, msg_g, msg_b;
static float shake;
static int last_scorer = -1;

static const char *PU_LABEL[PU__N] = { "W", "S", "T", "M", "F", "+" };
static const char *PU_NAME[PU__N] = { "WIDE PADDLE", "SHRINK", "SLOW MOTION", "MULTI BALL", "FREEZE", "EXTRA LIFE" };
static const int PU_COL[PU__N][3] = { { 90, 240, 170 }, { 255, 90, 90 }, { 100, 170, 255 }, { 200, 120, 255 }, { 150, 230, 255 }, { 255, 130, 200 } };

/* ================================================================ particles */
typedef struct { float x, y, vx, vy, size, grav; int life, max; unsigned char r, g, b; } Part;
static Part pt[NPART];

static void part_add(float x, float y, float vx, float vy, float size, int life, int r, int g, int b, float grav) {
    for (int i = 0; i < NPART; i++) if (pt[i].life <= 0) {
        pt[i].x = x; pt[i].y = y; pt[i].vx = vx; pt[i].vy = vy; pt[i].size = size; pt[i].grav = grav;
        pt[i].life = pt[i].max = life; pt[i].r = (unsigned char)r; pt[i].g = (unsigned char)g; pt[i].b = (unsigned char)b;
        return;
    }
}
static void burst(float x, float y, int n, float spd, int r, int g, int b, float size, int life) {
    for (int i = 0; i < n; i++) {
        float a = frnd(0, 6.2831853f), s = frnd(spd * 0.3f, spd);
        part_add(x, y, cosf(a) * s, sinf(a) * s, size * frnd(0.6f, 1.2f), (int)(life * frnd(0.6f, 1.0f)), r, g, b, 0.05f);
    }
}
static void part_update(void) {
    for (int i = 0; i < NPART; i++) if (pt[i].life > 0) {
        pt[i].x += pt[i].vx; pt[i].y += pt[i].vy; pt[i].vy += pt[i].grav;
        pt[i].vx *= 0.985f; pt[i].life--;
    }
}
static void part_draw(void) {
    for (int i = 0; i < NPART; i++) if (pt[i].life > 0) {
        float t = (float)pt[i].life / pt[i].max;
        float s = pt[i].size * (0.4f + 0.6f * t);
        gfx_rect((int)(pt[i].x - s / 2), (int)(pt[i].y - s / 2), (int)s + 1, (int)s + 1, pt[i].r, pt[i].g, pt[i].b, (int)(230 * t));
    }
}
static void ambient(void) {
    if (g_tick % 3) return;
    const Theme *t = TH();
    int c = rnd() % 3;
    const int *col = c == 0 ? t->p1 : (c == 1 ? t->p2 : t->acc);
    part_add(frnd(0, W), H + 8, frnd(-0.3f, 0.3f), frnd(-1.8f, -0.6f), frnd(3, 7), 520, col[0], col[1], col[2], 0);
}

/* ================================================================ rumble and light bar */
static int rum_l[2], rum_s[2], rum_t[2], rum_sent0[2] = { 1, 1 };
static int fl_r[2], fl_g[2], fl_b[2], fl_t[2];
static int light_on[2];

static int pad_for(int player) { return (npads >= 2 && player == 1) ? 1 : 0; }

static void fx_rumble(int player, int l, int s, int t) {
    if (!g_set.rumble) return;
    int p = player < 0 ? 0 : pad_for(player);
    if (t > rum_t[p] || l + s > rum_l[p] + rum_s[p]) { rum_l[p] = l; rum_s[p] = s; rum_t[p] = t; }
}
static void fx_rumble_all(int l, int s, int t) { fx_rumble(0, l, s, t); if (npads >= 2) fx_rumble(1, l, s, t); }
static void fx_flash(int player, int r, int g, int b, int t) {
    int p = player < 0 ? 0 : pad_for(player);
    fl_r[p] = r; fl_g[p] = g; fl_b[p] = b; fl_t[p] = t;
}
static void fx_flash_all(int r, int g, int b, int t) { fx_flash(0, r, g, b, t); if (npads >= 2) fx_flash(1, r, g, b, t); }
static void shake_add(float m) { if (g_set.shake && m > shake) shake = m; }

static void light_color(int p, int *r, int *g, int *b) {
    const Theme *t = TH();
    const int *base = (p == 1 && npads >= 2) ? t->p2 : t->p1;
    float k = 0.5f;
    int cr = base[0], cg = base[1], cb = base[2];
    if (scr == SC_PLAY || scr == SC_PAUSE) {
        if (scr == SC_PAUSE) k = 0.22f;
        else if (pstate == PS_COUNT) { k = 0.35f + 0.6f * (0.5f + 0.5f * sinf(g_tick * 0.35f)); cr = cg = cb = 255; }
        else k = 0.35f + 0.65f * clampf((bspeed - BASE_SPEED) / (MAX_SPEED - BASE_SPEED), 0, 1);
    } else if (scr == SC_OVER) {
        int pw = (mode == M_CPU) ? 0 : (npads >= 2 ? p : winner);
        int won = (mode == M_SURV || mode == M_PRAC) ? 0 : (winner == pw);
        if (won) { cr = 255; cg = 200; cb = 60; k = 0.55f + 0.45f * sinf(g_tick * 0.18f); }
        else { cr = 255; cg = 40; cb = 40; k = 0.25f + 0.1f * sinf(g_tick * 0.06f); }
    } else {
        cr = t->acc[0]; cg = t->acc[1]; cb = t->acc[2];
        float m = 0.5f + 0.5f * sinf(g_tick * 0.021f);
        cr = mixi(cr, t->p1[0], m); cg = mixi(cg, t->p1[1], m); cb = mixi(cb, t->p1[2], m);
        k = 0.30f + 0.28f * sinf(g_tick * 0.045f);
    }
    float rr = cr * k, gg = cg * k, bb = cb * k;
    if (fl_t[p] > 0) {
        float a = clampf(fl_t[p] / 8.0f, 0, 1);
        rr = rr * (1 - a) + fl_r[p] * a; gg = gg * (1 - a) + fl_g[p] * a; bb = bb * (1 - a) + fl_b[p] * a;
    }
    *r = clampi((int)rr, 0, 255); *g = clampi((int)gg, 0, 255); *b = clampi((int)bb, 0, 255);
}

static void fx_update(void) {
    int n = npads > 2 ? 2 : npads;
    for (int p = 0; p < n; p++) {
        if (rum_t[p] > 0 && g_set.rumble) {
            float f = rum_t[p] < 10 ? rum_t[p] / 10.0f : 1.0f;
            plat_pad_rumble(p, (uint8_t)(rum_l[p] * f), (uint8_t)(rum_s[p] * f));
            rum_t[p]--; rum_sent0[p] = 0;
        } else if (!rum_sent0[p]) {
            plat_pad_rumble(p, 0, 0); rum_sent0[p] = 1; rum_t[p] = 0;
        }
        if (!g_set.lightbar) {
            if (light_on[p]) { plat_pad_light_reset(p); light_on[p] = 0; }
        } else if (g_tick % 2 == (unsigned long)p || fl_t[p] > 0) {
            int r, g, b;
            light_color(p, &r, &g, &b);
            plat_pad_light(p, (uint8_t)r, (uint8_t)g, (uint8_t)b);
            light_on[p] = 1;
        }
        if (fl_t[p] > 0) fl_t[p]--;
    }
}

/* ================================================================ input */
/* Menu navigation repeat is measured in real time, not in 60 Hz ticks. When the console is slow the main loop
   runs several ticks per drawn frame, and a tick-based repeat then moved the cursor in uneven bursts. Here it
   fires at most once per frame and, on a slow frame rate, once on every frame. */
static Uint32 rep_press[4], rep_last[4];
static Uint32 frame_dt = 16;
static int nav_step(int held, int i) {
    Uint32 now = SDL_GetTicks();
    if (held) {
        if (rep[i] == 0) { rep[i] = 1; rep_press[i] = rep_last[i] = now; return 1; }
        if (now - rep_press[i] < 380) return 0;
        Uint32 interval = frame_dt > 100 ? frame_dt : 100;
        if (now - rep_last[i] + frame_dt / 2 >= interval) { rep_last[i] = now; return 1; }
        return 0;
    }
    rep[i] = 0;
    return 0;
}

static void read_input(void) {
    for (int i = 0; i < 2; i++) { padp[i] = pad[i]; plat_pad_read(i, &pad[i]); }
    npads = plat_pad_count();
    if (npads < 1) npads = 1;

    uint32_t held = pad[0].held | pad[1].held;
    float ax = fabsf(pad[0].lx) >= fabsf(pad[1].lx) ? pad[0].lx : pad[1].lx;
    float ay = fabsf(pad[0].ly) >= fabsf(pad[1].ly) ? pad[0].ly : pad[1].ly;
    uint32_t pressed = held & ~prev_union;
    nav.up = nav_step((held & B_UP) || ay < -0.55f, 0);
    nav.down = nav_step((held & B_DOWN) || ay > 0.55f, 1);
    nav.left = nav_step((held & B_LEFT) || ax < -0.55f, 2);
    nav.right = nav_step((held & B_RIGHT) || ax > 0.55f, 3);
    nav.ok = (pressed & B_CROSS) != 0;
    nav.back = (pressed & B_CIRCLE) != 0;
    nav.pause = (pressed & (B_OPTIONS | B_TOUCH)) != 0;
    nav.any = pressed != 0;
    prev_union = held;
}

typedef struct { float axis; int dash; } Ctl;
static Ctl read_ctl(int p) {
    Ctl c = { 0, 0 };
    int shared = npads < 2;
    if (p == 0 || !shared) {
        int i = (p == 0) ? 0 : 1;
        c.axis = pad[i].lx;
        if (pad[i].held & B_LEFT) c.axis -= 1;
        if (pad[i].held & B_RIGHT) c.axis += 1;
        uint32_t mask = shared ? (B_R1 | B_R2) : (B_R1 | B_R2 | B_L1 | B_L2 | B_TRIANGLE);
        c.dash = (pad[i].held & mask) && !(padp[i].held & mask);
    } else {
        c.axis = pad[0].rx;
        if (pad[0].held & B_SQUARE) c.axis -= 1;
        if (pad[0].held & B_CIRCLE) c.axis += 1;
        uint32_t mask = B_L1 | B_L2;
        c.dash = (pad[0].held & mask) && !(padp[0].held & mask);
    }
    c.axis = clampf(c.axis, -1, 1);
    return c;
}

/* ================================================================ screens */
static void set_screen(int s) {
    int is_menu = (s >= SC_MENU && s <= SC_CREDITS);
    int in_game = (s == SC_PLAY || s == SC_PAUSE || s == SC_OVER);
    scr = s; scr_t = 0;
    /* menu song only in the menus, game song only in a match; the mixer fades one out and the other in */
    if (is_menu) audio_music_select(TRACK_MENU);
    else if (in_game) audio_music_select(TRACK_GAME);
    else audio_music_select(TRACK_NONE);
    audio_music_duck(s == SC_PAUSE);
}

/* ================================================================ match logic */
static float pwidth(int i) {
    float w = PW0;
    if (P[i].wide > 0) w *= 1.55f;
    if (P[i].shrink > 0) w *= 0.62f;
    return w;
}
static float pay(int i) { return i == 0 ? (float)PY_BOT : (float)PY_TOP; }
static int has_top_paddle(void) { return mode == M_CPU || mode == M_2P; }
static int solo(void) { return mode == M_SURV || mode == M_PRAC; }

static void msg_set(const char *s, int r, int g, int b) {
    snprintf(msg, sizeof msg, "%s", s); msg_t = 100; msg_r = r; msg_g = g; msg_b = b;
}

static void serve_ball(int dir) {
    memset(B, 0, sizeof B);
    B[0].on = 1; B[0].x = W / 2.0f; B[0].y = H / 2.0f;
    serve_dir = dir; pstate = PS_SERVE; pst_t = 0; slow_t = 0;
}

static void launch_ball(void) {
    B[0].vx = (rnd() & 1 ? 1.0f : -1.0f) * frnd(2.0f, 4.5f);
    B[0].vy = serve_dir * bspeed;
    B[0].owner = serve_dir > 0 ? 1 : 0;
    pstate = PS_LIVE; pst_t = 0;
}

static void new_match(int m) {
    mode = m;
    memset(P, 0, sizeof P);
    P[0].x = P[1].x = (W - PW0) / 2;
    lives = 3; rally = 0; best_rally_match = 0; match_ticks = 0; surv_score = 0; slow_t = 0;
    winner = -1; over_new_best = 0; pu.on = 0; pu_timer = 360; msg_t = 0; cpu_cd = 0; cpu_target = W / 2;
    bspeed = BASE_SPEED;
    memset(B, 0, sizeof B);
    pstate = PS_COUNT; pst_t = 0;
    serve_dir = solo() ? 1 : (rnd() & 1 ? 1 : -1);
    g_stats.played++;
    set_screen(SC_PLAY);
    for (int i = 0; i < NPART; i++) pt[i].life = 0;
    fx_flash_all(255, 255, 255, 10);
}

static void finish_match(int w) {
    winner = w;
    g_stats.seconds_played += match_ticks / 60;
    if (best_rally_match > g_stats.best_rally) g_stats.best_rally = best_rally_match;
    if (mode == M_CPU) {
        if (w == 0) g_stats.wins++; else g_stats.losses++;
    } else if (mode == M_SURV) {
        if (surv_score > g_stats.best_survival) { g_stats.best_survival = surv_score; over_new_best = 1; }
    }
    save_write();
    set_screen(SC_OVER);
    int good = (mode == M_SURV || mode == M_PRAC) ? 0 : (mode == M_2P ? 1 : (w == 0));
    audio_sfx(good ? SFX_WIN : SFX_LOSE);
    if (good) {
        fx_rumble_all(255, 255, 24); fx_flash_all(255, 200, 60, 30);
        burst(W / 2.0f, H / 2.0f, 90, 11, 255, 210, 90, 9, 90);
    } else {
        fx_rumble_all(255, 200, 40); fx_flash_all(255, 30, 30, 30);
    }
    shake_add(18);
}

/* a point or a lost life wipes every power-up state, so nothing carries into the next serve */
static void clear_effects(void) {
    pu.on = 0; slow_t = 0; pu_timer = 360;
    for (int i = 0; i < 2; i++) { P[i].wide = 0; P[i].shrink = 0; P[i].freeze = 0; }
}

static float miss_x = 960.0f;       /* where the ball left the court, for the burst */
static void point_scored(int w) {
    if (rally > best_rally_match) best_rally_match = rally;
    P[w].score++;
    last_scorer = w;
    rally = 0; bspeed = BASE_SPEED;
    memset(B, 0, sizeof B);
    clear_effects();
    shake_add(14);
    float bx = clampf(miss_x, (float)WL + 20, (float)WR - 20), by = (w == 0) ? (float)GOAL_TOP + 30 : (float)GOAL_BOT - 30;
    const Theme *t = TH();
    const int *c = w == 0 ? t->p1 : t->p2;
    burst(bx, by, 60, 9, c[0], c[1], c[2], 8, 70);
    if (mode == M_CPU && w == 1) {
        audio_sfx(SFX_CONCEDE); fx_rumble(0, 255, 255, 26); fx_flash(0, 255, 40, 40, 14);
        msg_set("CPU SCORES", 255, 120, 120);
    } else {
        audio_sfx(SFX_SCORE); fx_rumble(w, 170, 120, 20); fx_flash(w, 80, 255, 120, 14);
        if (mode == M_CPU) msg_set("POINT!", 120, 255, 170);
        else msg_set(w == 0 ? "P1 SCORES" : "P2 SCORES", c[0], c[1], c[2]);
        if (mode == M_2P) fx_rumble(1 - w, 120, 0, 12);
    }
    if (P[w].score >= WIN_SCORES[g_set.win_idx]) { finish_match(w); return; }
    pstate = PS_POINT; pst_t = 0;
    serve_dir = (w == 1) ? 1 : -1;
}

static void life_lost(void) {
    if (rally > best_rally_match) best_rally_match = rally;
    rally = 0; bspeed = BASE_SPEED;
    memset(B, 0, sizeof B);
    clear_effects();
    shake_add(14);
    audio_sfx(SFX_LIFE_LOST); fx_rumble(0, 255, 255, 24); fx_flash(0, 255, 40, 40, 14);
    burst(clampf(miss_x, (float)WL + 20, (float)WR - 20), GOAL_BOT - 30.0f, 50, 9, 255, 90, 90, 8, 70);
    if (mode == M_SURV) {
        lives--;
        if (lives <= 0) { finish_match(1); return; }
        msg_set(lives == 1 ? "LAST LIFE" : "LIFE LOST", 255, 120, 120);
    } else {
        msg_set("MISSED", 255, 190, 120);
    }
    pstate = PS_POINT; pst_t = 0; serve_dir = 1;
}

static void apply_pickup(int type, int owner) {
    int opp = 1 - owner;
    char buf[48];
    audio_sfx(SFX_POWERUP);
    fx_rumble(owner, 90, 200, 14);
    fx_flash(owner, PU_COL[type][0], PU_COL[type][1], PU_COL[type][2], 14);
    const char *who = (mode == M_CPU) ? (owner == 0 ? "" : "CPU ") : (owner == 0 ? "P1 " : "P2 ");
    switch (type) {
    case PU_WIDE:   P[owner].wide = 600; break;
    case PU_SHRINK: if (has_top_paddle()) P[opp].shrink = 600; else P[owner].wide = 600; break;
    case PU_SLOW:   slow_t = 420; break;
    case PU_MULTI:
        for (int k = 1; k <= 2; k++) {
            for (int j = 1; j < MAXB; j++) if (!B[j].on) {
                B[j] = B[0]; B[j].tn = 0; B[j].ghost = 1; B[j].on = 1;
                B[j].vx = B[0].vx + (k == 1 ? 4.5f : -4.5f); B[j].vy = B[0].vy * 0.92f;
                break;
            }
        }
        break;
    case PU_FREEZE:
        if (has_top_paddle()) { P[opp].freeze = 130; audio_sfx(SFX_FREEZE); fx_rumble(opp, 60, 0, 10); }
        else slow_t = 420;
        break;
    case PU_LIFE: if (lives < 5) lives++; break;
    }
    snprintf(buf, sizeof buf, "%s%s", who, PU_NAME[type]);
    msg_set(buf, PU_COL[type][0], PU_COL[type][1], PU_COL[type][2]);
}

static void spawn_pickup(int type) {
    pu.on = 1; pu.type = type; pu.life = 640;
    pu.x = frnd(WL + 160, WR - 160); pu.y = frnd(340, 740);
}

static void maybe_spawn_pickup(void) {
    if (!g_set.powerups || mode == M_PRAC || pu.on) return;
    if (--pu_timer > 0) return;
    pu_timer = (int)frnd(420, 780);
    int t;
    if (has_top_paddle()) t = (int)(rnd() % 5);
    else { static const int pool[4] = { PU_WIDE, PU_SLOW, PU_MULTI, PU_LIFE }; t = pool[rnd() % 4]; }
    spawn_pickup(t);
}

/* ---- paddles ---- */
static void move_paddle(int i, float desired_vx, int dash_press) {
    Paddle *p = &P[i];
    if (p->wide > 0) p->wide--;
    if (p->shrink > 0) p->shrink--;
    if (p->cd > 0) p->cd--;
    if (p->freeze > 0) { p->freeze--; p->vx = 0; return; }
    if (dash_press && p->cd == 0) {
        p->dash = 9; p->cd = 70;
        p->dashdir = desired_vx > 1 ? 1 : (desired_vx < -1 ? -1 : (p->vx >= 0 ? 1 : -1));
        audio_sfx(SFX_DASH); fx_rumble(i, 0, 90, 5);
        burst(p->x + pwidth(i) / 2, pay(i) + PH / 2, 14, 5, 255, 255, 255, 5, 24);
    }
    if (p->dash > 0) { p->dash--; p->vx = p->dashdir * PAD_SPEED * 2.5f; }
    else p->vx += (desired_vx - p->vx) * 0.45f;
    p->x += p->vx;
    float pw = pwidth(i);
    if (p->x < WL) { p->x = WL; p->vx = 0; }
    if (p->x > WR - pw) { p->x = WR - pw; p->vx = 0; }
}

static float fold(float x, float lo, float hi) {
    float span = hi - lo, t = fmodf(x - lo, 2 * span);
    if (t < 0) t += 2 * span;
    if (t > span) t = 2 * span - t;
    return lo + t;
}

static void cpu_update(void) {
    static const float SPD[4] = { 8.5f, 11.5f, 15.5f, 21.0f };
    static const int ERR[4] = { 150, 80, 34, 6 };
    static const int REACT[4] = { 22, 14, 8, 3 };
    int d = g_set.difficulty;
    Ball *b = &B[0];
    if (b->on && b->vy < 0) {
        if (--cpu_cd <= 0) {
            float dt = (b->y - (PY_TOP + PH + BR)) / -b->vy;
            float x = fold(b->x + b->vx * dt, WL + BR, WR - BR);
            cpu_target = x + frnd(-(float)ERR[d], (float)ERR[d]);
            cpu_cd = REACT[d];
        }
    } else if (!b->on || b->vy >= 0) {
        cpu_target = W / 2.0f; cpu_cd = 0;
    }
    float pw = pwidth(1);
    float dx = cpu_target - (P[1].x + pw / 2);
    float want = fabsf(dx) > 10 ? clampf(dx, -SPD[d], SPD[d]) : 0;
    move_paddle(1, want, 0);
}

static int paddle_hit(Ball *b, int i) {
    float pw = pwidth(i), py = pay(i);
    if (i == 0 && b->vy <= 0) return 0;
    if (i == 1 && b->vy >= 0) return 0;
    if (b->y + BR < py || b->y - BR > py + PH) return 0;
    if (b->x + BR * 0.7f < P[i].x || b->x - BR * 0.7f > P[i].x + pw) return 0;

    float rel = clampf((b->x - (P[i].x + pw / 2)) / (pw / 2), -1, 1);
    int dir = (i == 0) ? -1 : 1;
    b->y = (i == 0) ? py - BR : py + PH + BR;
    rally++;
    g_stats.total_hits++;
    if (mode == M_SURV) { surv_score += 10 + (rally < 40 ? rally : 40); bspeed = fminf(bspeed + 0.7f, MAX_SPEED); }
    else if (mode != M_PRAC) bspeed = fminf(bspeed + 0.5f, MAX_SPEED);
    float ang = rel * 1.05f;
    b->vx = sinf(ang) * bspeed + P[i].vx * 0.12f;
    b->vy = dir * cosf(ang) * bspeed;
    b->owner = i;
    int semis = rally / 3; if (semis > 12) semis = 12;
    audio_sfx_pitch(SFX_HIT, semis);
    const Theme *t = TH();
    const int *c = i == 0 ? t->p1 : t->p2;
    burst(b->x, b->y + dir * BR * -1.0f, 14, 6, c[0], c[1], c[2], 6, 30);
    fx_rumble(i, 0, 110, 7);
    fx_flash(i, 255, 255, 255, 5);
    shake_add(mode == M_SURV ? 4.0f : 3.0f);
    if (rally == 10 || rally == 20 || rally == 30 || rally == 50) {
        char buf[32]; snprintf(buf, sizeof buf, "RALLY %d", rally);
        msg_set(buf, 255, 235, 140);
        fx_rumble_all(140, 140, 10);
    }
    return 1;
}

static void update_live(void) {
    match_ticks++;
    Ctl c0 = read_ctl(0);
    move_paddle(0, c0.axis * PAD_SPEED, c0.dash);
    if (mode == M_2P) { Ctl c1 = read_ctl(1); move_paddle(1, c1.axis * PAD_SPEED, c1.dash); }
    else if (mode == M_CPU) cpu_update();

    if (slow_t > 0) slow_t--;
    float ts = slow_t > 0 ? 0.62f : 1.0f;

    for (int k = 0; k < MAXB; k++) {
        Ball *b = &B[k];
        if (!b->on) continue;
        if (g_tick % 2 == 0) {
            memmove(&b->tx[1], &b->tx[0], sizeof(float) * (TRAIL - 1));
            memmove(&b->ty[1], &b->ty[0], sizeof(float) * (TRAIL - 1));
            b->tx[0] = b->x; b->ty[0] = b->y;
            if (b->tn < TRAIL) b->tn++;
        }
        b->x += b->vx * ts; b->y += b->vy * ts;
        if (b->x - BR < WL) { b->x = WL + BR; b->vx = fabsf(b->vx); audio_sfx(SFX_WALL); fx_rumble(0, 0, 40, 4); shake_add(1.5f); }
        if (b->x + BR > WR) { b->x = WR - BR; b->vx = -fabsf(b->vx); audio_sfx(SFX_WALL); fx_rumble(0, 0, 40, 4); shake_add(1.5f); }
        if (solo() && b->vy < 0 && b->y - BR <= WALL_BOTTOM) {
            b->y = WALL_BOTTOM + BR; b->vy = -b->vy;
            audio_sfx(SFX_WALL); shake_add(2); fx_rumble(0, 0, 50, 4);
            burst(b->x, (float)WALL_BOTTOM, 8, 4, 160, 210, 255, 5, 26);
        }
        paddle_hit(b, 0);
        if (has_top_paddle()) paddle_hit(b, 1);

        if (b->y - BR > GOAL_BOT) {
            if (k == 0) { miss_x = b->x; if (solo()) life_lost(); else point_scored(1); return; }
            b->on = 0;
        } else if (has_top_paddle() && b->y + BR < GOAL_TOP) {
            if (k == 0) { miss_x = b->x; point_scored(0); return; }
            b->on = 0;
        }
    }

    maybe_spawn_pickup();
    if (pu.on) {
        if (--pu.life <= 0) pu.on = 0;
        else for (int k = 0; k < MAXB; k++) if (B[k].on) {
            float dx = B[k].x - pu.x, dy = B[k].y - (pu.y + sinf(g_tick * 0.1f) * 6);
            if (dx * dx + dy * dy < (BR + 38) * (BR + 38)) {
                burst(pu.x, pu.y, 26, 7, PU_COL[pu.type][0], PU_COL[pu.type][1], PU_COL[pu.type][2], 7, 40);
                apply_pickup(pu.type, B[k].owner);
                pu.on = 0;
                break;
            }
        }
    }
}

static void update_play(void) {
    if (nav.pause) { set_screen(SC_PAUSE); sel[SC_PAUSE] = 0; audio_sfx(SFX_PAUSE); fx_rumble_all(0, 80, 5); return; }
    if (msg_t > 0) msg_t--;
    if (g_stats.best_rally < best_rally_match) { /* tracked live so a quit still keeps it */ }

    Ctl c0 = read_ctl(0);
    switch (pstate) {
    case PS_COUNT:
        move_paddle(0, c0.axis * PAD_SPEED, 0);
        if (mode == M_2P) { Ctl c1 = read_ctl(1); move_paddle(1, c1.axis * PAD_SPEED, 0); }
        if (pst_t == 0 || pst_t == 60 || pst_t == 120) { audio_sfx(SFX_COUNT3); fx_rumble_all(0, 90, 6); }
        if (pst_t == 180) { audio_sfx(SFX_GO); fx_rumble_all(120, 160, 12); fx_flash_all(255, 255, 255, 10); serve_ball(serve_dir); return; }
        pst_t++;
        break;
    case PS_SERVE:
        move_paddle(0, c0.axis * PAD_SPEED, 0);
        if (mode == M_2P) { Ctl c1 = read_ctl(1); move_paddle(1, c1.axis * PAD_SPEED, 0); }
        else if (mode == M_CPU) cpu_update();
        if (++pst_t >= 45) launch_ball();
        break;
    case PS_POINT:
        move_paddle(0, c0.axis * PAD_SPEED, 0);
        if (mode == M_2P) { Ctl c1 = read_ctl(1); move_paddle(1, c1.axis * PAD_SPEED, 0); }
        else if (mode == M_CPU) cpu_update();
        if (++pst_t >= 75) serve_ball(serve_dir);
        break;
    default:
        update_live();
        break;
    }
}

/* ================================================================ menus */
static const char *MAIN_ITEMS[6] = { "PLAY", "SETTINGS", "STATS", "CONTROLS", "CREDITS", "EXIT" };
static const char *MODE_ITEMS[5] = { "VS CPU", "2 PLAYERS", "SURVIVAL", "PRACTICE", "BACK" };
static const char *PAUSE_ITEMS[3] = { "RESUME", "RESTART", "QUIT TO MENU" };
static const char *OVER_ITEMS[2] = { "REMATCH", "MAIN MENU" };
enum { S_MUSIC, S_SFX, S_RUMBLE, S_LIGHT, S_DIFF, S_WIN, S_POWER, S_SHAKE, S_THEME, S_RESET, S_BACK, S__N };
static const char *SET_LABELS[S__N] = { "MUSIC VOLUME", "EFFECTS VOLUME", "RUMBLE", "LIGHT BAR", "CPU DIFFICULTY",
                                        "POINTS TO WIN", "POWER-UPS", "SCREEN SHAKE", "COLOR THEME", "RESET STATS", "BACK" };

static void apply_audio_settings(void) { audio_set_volumes(g_set.music, g_set.sfx); }

static void setting_change(int id, int dir) {
    int changed = 1;
    switch (id) {
    case S_MUSIC: g_set.music = clampi(g_set.music + dir, 0, 10); break;
    case S_SFX: g_set.sfx = clampi(g_set.sfx + dir, 0, 10); break;
    case S_RUMBLE: g_set.rumble = !g_set.rumble; if (g_set.rumble) fx_rumble_all(150, 200, 14); break;
    case S_LIGHT: g_set.lightbar = !g_set.lightbar; break;
    case S_DIFF: g_set.difficulty = (g_set.difficulty + dir + 4) % 4; break;
    case S_WIN: g_set.win_idx = (g_set.win_idx + dir + 4) % 4; break;
    case S_POWER: g_set.powerups = !g_set.powerups; break;
    case S_SHAKE: g_set.shake = !g_set.shake; break;
    case S_THEME: g_set.theme = (g_set.theme + dir + 4) % 4; break;
    default: changed = 0; break;
    }
    if (changed) { apply_audio_settings(); audio_sfx(SFX_MOVE); save_write(); }
}

static void setting_activate(int id) {
    if (id == S_BACK) { set_screen(SC_MENU); audio_sfx(SFX_BACK); }
    else if (id == S_RESET) {
        if (reset_confirm > 0) { save_reset_stats(); save_write(); reset_confirm = 0; audio_sfx(SFX_SELECT); }
        else { reset_confirm = 180; audio_sfx(SFX_MOVE); }
    } else setting_change(id, 1);
}

static void menu_move(int n, int *s) {
    if (nav.up) { *s = (*s + n - 1) % n; audio_sfx(SFX_MOVE); fx_rumble_all(0, 25, 2); }
    if (nav.down) { *s = (*s + 1) % n; audio_sfx(SFX_MOVE); fx_rumble_all(0, 25, 2); }
}

static void update_screens(void) {
    scr_t++;
    switch (scr) {
    case SC_INTRO:
        if (scr_t == 5) audio_sfx(SFX_SPLASH);
        if (scr_t > 190 || (scr_t > 25 && nav.any)) set_screen(SC_MENU);
        break;
    case SC_MENU:
        ambient();
        menu_move(6, &sel[SC_MENU]);
        if (nav.ok) {
            audio_sfx(SFX_SELECT); fx_rumble_all(100, 120, 8);
            switch (sel[SC_MENU]) {
            case 0: set_screen(SC_MODE); break;
            case 1: set_screen(SC_SETTINGS); break;
            case 2: set_screen(SC_STATS); break;
            case 3: set_screen(SC_CONTROLS); break;
            case 4: set_screen(SC_CREDITS); break;
            default: quit_req = 1; break;
            }
        }
        break;
    case SC_MODE:
        ambient();
        menu_move(5, &sel[SC_MODE]);
        if (nav.back) { set_screen(SC_MENU); audio_sfx(SFX_BACK); }
        if (nav.ok) {
            if (sel[SC_MODE] == 4) { set_screen(SC_MENU); audio_sfx(SFX_BACK); }
            else { audio_sfx(SFX_SELECT); fx_rumble_all(120, 140, 10); new_match(sel[SC_MODE]); }
        }
        break;
    case SC_SETTINGS:
        ambient();
        if (reset_confirm > 0) reset_confirm--;
        menu_move(S__N, &sel[SC_SETTINGS]);
        if (nav.left) setting_change(sel[SC_SETTINGS], -1);
        if (nav.right) setting_change(sel[SC_SETTINGS], 1);
        if (nav.ok) setting_activate(sel[SC_SETTINGS]);
        if (nav.back) { set_screen(SC_MENU); audio_sfx(SFX_BACK); }
        break;
    case SC_STATS: case SC_CONTROLS: case SC_CREDITS:
        ambient();
        if (nav.back || nav.ok) { set_screen(SC_MENU); audio_sfx(SFX_BACK); }
        break;
    case SC_PLAY:
        update_play();
        break;
    case SC_PAUSE:
        menu_move(3, &sel[SC_PAUSE]);
        if (nav.pause || nav.back) { set_screen(SC_PLAY); audio_sfx(SFX_BACK); }
        else if (nav.ok) {
            audio_sfx(SFX_SELECT);
            if (sel[SC_PAUSE] == 0) set_screen(SC_PLAY);
            else if (sel[SC_PAUSE] == 1) new_match(mode);
            else {
                g_stats.seconds_played += match_ticks / 60;
                if (best_rally_match > g_stats.best_rally) g_stats.best_rally = best_rally_match;
                save_write();
                set_screen(SC_MENU);
            }
        }
        break;
    case SC_OVER:
        menu_move(2, &sel[SC_OVER]);
        if (scr_t > 20 && nav.ok) {
            audio_sfx(SFX_SELECT);
            if (sel[SC_OVER] == 0) new_match(mode); else set_screen(SC_MENU);
        }
        break;
    }
}

/* ================================================================ drawing */
static void draw_bg(GfxImg *t) { gfx_draw_bg(t); }

static void draw_footer(const char *s) {
    gfx_text(W / 2, 1020, 28, s, 170, 190, 235, 220, AL_CENTER);
}

static void draw_list(const char **items, int n, int s, int x, int y0, int pitch, int w, float size) {
    const Theme *t = TH();
    for (int i = 0; i < n; i++) {
        int y = y0 + i * pitch, rh = pitch - 12;
        int on = (i == s);
        if (on) {
            float pulse = 0.5f + 0.5f * sinf(g_tick * 0.12f);
            gfx_rect(x, y, w, rh, t->acc[0], t->acc[1], t->acc[2], 60 + (int)(40 * pulse));
            gfx_rect(x, y, 8, rh, t->acc[0], t->acc[1], t->acc[2], 255);
            gfx_text(x + 44, y + (rh - gfx_text_h(size)) / 2 + 2, size, ">", 255, 255, 255, 255, AL_LEFT);
        }
        gfx_text_shadow(x + 90, y + (rh - gfx_text_h(size)) / 2 + 2, size, items[i], on ? 255 : 175, on ? 255 : 190, on ? 255 : 225, 255, AL_LEFT);
    }
}

static void draw_panel(int x, int y, int w, int h) {
    const Theme *t = TH();
    gfx_rect(x, y, w, h, 7, 10, 30, 255);        /* opaque: an alpha-blended panel costs a blend per pixel */
    gfx_frame(x, y, w, h, 2, t->acc[0], t->acc[1], t->acc[2], 150);
}

static void draw_lines(int x, int y, float size, int pitch, const char **lines, int n, int al, int r, int g, int b) {
    for (int i = 0; i < n; i++) gfx_text(x, y + i * pitch, size, lines[i], r, g, b, 235, al);
}

static void draw_intro(void) {
    draw_bg(tx_game);
    float a = 1.0f;
    if (scr_t < 30) a = scr_t / 30.0f; else if (scr_t > 160) a = clampf((190 - scr_t) / 30.0f, 0, 1);
    int al = (int)(255 * a);
    const Theme *t = TH();
    if (tx_credit) {
        int w = 360, h = credit_h * w / credit_w;
        int x = 330, y = H / 2 - h / 2 - 10;
        gfx_rect(x - 8, y - 8, w + 16, h + 16, t->acc[0], t->acc[1], t->acc[2], (int)(200 * a));
        gfx_copy(tx_credit, x, y, w, h, al);
    }
    gfx_text(W / 2 + 120, H / 2 - 150, 36, "A GAME BY", 170, 200, 255, (int)(230 * a), AL_LEFT);
    gfx_text_shadow(W / 2 + 120, H / 2 - 110, 110, "MEXRLDEV", 255, 255, 255, al, AL_LEFT);
    gfx_rect(W / 2 + 124, H / 2 + 40, 520, 5, t->acc[0], t->acc[1], t->acc[2], al);
    gfx_text(W / 2 + 124, H / 2 + 70, 34, "PS4 HOMEBREW  -  PONG 2", 190, 215, 255, (int)(230 * a), AL_LEFT);
    if (scr_t > 40) gfx_text(W / 2, 1010, 26, "PRESS ANY BUTTON", 150, 170, 220, (int)(140 + 80 * sinf(g_tick * 0.1f)), AL_CENTER);
}

static void draw_menu(void) {
    draw_bg(tx_menu);
    part_draw();
    draw_panel(70, 410, 760, 620);
    draw_list(MAIN_ITEMS, 6, sel[SC_MENU], 96, 438, 96, 708, 46);
    char b[64];
    snprintf(b, sizeof b, "%s", npads >= 2 ? "2 CONTROLLERS READY" : "1 CONTROLLER");
    gfx_text(W - 60, 1020, 26, b, 190, 210, 255, 200, AL_RIGHT);
    draw_footer("X  SELECT        D-PAD  MOVE");
}

static void draw_mode(void) {
    draw_bg(tx_menu);
    part_draw();
    draw_panel(70, 410, 760, 620);
    draw_list(MODE_ITEMS, 5, sel[SC_MODE], 96, 438, 100, 708, 46);
    draw_panel(900, 410, 930, 620);
    static const char *D0[] = { "YOU AGAINST THE CPU.", "FIRST TO THE SET SCORE WINS.", "PICK A DIFFICULTY IN SETTINGS." };
    static const char *D1[] = { "LOCAL VERSUS.", "A SECOND CONTROLLER NEEDS A SECOND", "LOGGED-IN USER. OTHERWISE SHARE ONE PAD:", "P2 USES THE RIGHT STICK AND L1." };
    static const char *D2[] = { "ONE PADDLE, THREE LIVES.", "THE WALL SENDS EVERY BALL BACK AND", "EACH HIT MAKES IT FASTER.", "CHASE THE HIGH SCORE." };
    static const char *D3[] = { "NO LIVES AND NO SCORE.", "JUST RALLIES, FOR WARMING UP", "AND FOR TRYING POWER-UPS." };
    static const char *D4[] = { "BACK TO THE MAIN MENU." };
    const char **d[5] = { D0, D1, D2, D3, D4 };
    int cnt[5] = { 3, 4, 4, 3, 1 };
    int s = sel[SC_MODE];
    gfx_text_shadow(940, 440, 52, MODE_ITEMS[s], 255, 255, 255, 255, AL_LEFT);
    draw_lines(940, 530, 32, 50, d[s], cnt[s], AL_LEFT, 200, 220, 255);
    char b[96];
    if (s == 0) { snprintf(b, sizeof b, "DIFFICULTY  %s     FIRST TO  %d", DIFF_NAMES[g_set.difficulty], WIN_SCORES[g_set.win_idx]); gfx_text(940, 850, 32, b, 255, 235, 140, 255, AL_LEFT); }
    if (s == 1) { snprintf(b, sizeof b, "FIRST TO  %d", WIN_SCORES[g_set.win_idx]); gfx_text(940, 850, 32, b, 255, 235, 140, 255, AL_LEFT); }
    if (s == 2) { snprintf(b, sizeof b, "BEST SCORE  %d", g_stats.best_survival); gfx_text(940, 850, 32, b, 255, 235, 140, 255, AL_LEFT); }
    if (s == 3) { snprintf(b, sizeof b, "LONGEST RALLY  %d", g_stats.best_rally); gfx_text(940, 850, 32, b, 255, 235, 140, 255, AL_LEFT); }
    draw_footer("X  START        O  BACK");
}

static void draw_setting_value(int id, int x, int y, int rh, int on) {
    char b[48];
    const Theme *t = TH();
    int r = on ? 255 : 200, g = on ? 235 : 215, bl = on ? 140 : 240;
    switch (id) {
    case S_MUSIC: case S_SFX: {
        int v = id == S_MUSIC ? g_set.music : g_set.sfx;
        for (int i = 0; i < 10; i++) {
            int h = 14 + i * 3;
            gfx_rect(x - 310 + i * 24, y + rh / 2 + 12 - h, 16, h, i < v ? t->acc[0] : 70, i < v ? t->acc[1] : 80, i < v ? t->acc[2] : 110, i < v ? 255 : 150);
        }
        snprintf(b, sizeof b, "%d", v); gfx_text(x, y + (rh - gfx_text_h(32)) / 2 + 2, 32, b, r, g, bl, 255, AL_RIGHT);
        return;
    }
    case S_RUMBLE: snprintf(b, sizeof b, "%s", g_set.rumble ? "ON" : "OFF"); break;
    case S_LIGHT: snprintf(b, sizeof b, "%s", g_set.lightbar ? "ON" : "OFF"); break;
    case S_DIFF: snprintf(b, sizeof b, "%s", DIFF_NAMES[g_set.difficulty]); break;
    case S_WIN: snprintf(b, sizeof b, "%d", WIN_SCORES[g_set.win_idx]); break;
    case S_POWER: snprintf(b, sizeof b, "%s", g_set.powerups ? "ON" : "OFF"); break;
    case S_SHAKE: snprintf(b, sizeof b, "%s", g_set.shake ? "ON" : "OFF"); break;
    case S_THEME: {
        snprintf(b, sizeof b, "%s", THEMES[g_set.theme].name);
        const Theme *c = &THEMES[g_set.theme];
        int tw = gfx_text_w(b, 32);
        gfx_circle((float)(x - tw - 110), (float)(y + rh / 2), 13, c->p1[0], c->p1[1], c->p1[2], 255);
        gfx_circle((float)(x - tw - 76), (float)(y + rh / 2), 13, c->p2[0], c->p2[1], c->p2[2], 255);
        gfx_circle((float)(x - tw - 42), (float)(y + rh / 2), 13, c->ball[0], c->ball[1], c->ball[2], 255);
        break;
    }
    case S_RESET: snprintf(b, sizeof b, "%s", reset_confirm > 0 ? "PRESS X AGAIN" : "X"); break;
    default: b[0] = 0; break;
    }
    gfx_text(x, y + (rh - gfx_text_h(32)) / 2 + 2, 32, b, id == S_RESET && reset_confirm > 0 ? 255 : r, id == S_RESET && reset_confirm > 0 ? 120 : g, id == S_RESET && reset_confirm > 0 ? 120 : bl, 255, AL_RIGHT);
}

static void draw_settings(void) {
    draw_bg(tx_game);
    part_draw();
    gfx_text_shadow(W / 2, 40, 64, "SETTINGS", 255, 255, 255, 255, AL_CENTER);
    draw_panel(330, 150, 1260, 840);
    const Theme *t = TH();
    for (int i = 0; i < S__N; i++) {
        int y = 168 + i * 74, rh = 62, on = (i == sel[SC_SETTINGS]);
        if (on) {
            gfx_rect(346, y, 1228, rh, t->acc[0], t->acc[1], t->acc[2], 60 + (int)(40 * (0.5f + 0.5f * sinf(g_tick * 0.12f))));
            gfx_rect(346, y, 8, rh, t->acc[0], t->acc[1], t->acc[2], 255);
        }
        gfx_text_shadow(396, y + (rh - gfx_text_h(36)) / 2 + 2, 36, SET_LABELS[i], on ? 255 : 175, on ? 255 : 190, on ? 255 : 225, 255, AL_LEFT);
        if (i < S_BACK) {
            if (on && i != S_RESET) gfx_text(1120, y + (rh - gfx_text_h(32)) / 2 + 2, 32, "<", 255, 255, 255, 200, AL_LEFT);
            draw_setting_value(i, 1520, y, rh, on);
            if (on && i != S_RESET) gfx_text(1548, y + (rh - gfx_text_h(32)) / 2 + 2, 32, ">", 255, 255, 255, 200, AL_LEFT);
        }
    }
    draw_footer("LEFT / RIGHT  CHANGE        O  BACK");
}

static void draw_stats(void) {
    draw_bg(tx_game);
    part_draw();
    gfx_text_shadow(W / 2, 40, 64, "STATS", 255, 255, 255, 255, AL_CENTER);
    draw_panel(430, 170, 1060, 780);
    char a[48], b[48];
    int total = g_stats.wins + g_stats.losses;
    int rate = total > 0 ? g_stats.wins * 100 / total : 0;
    int secs = g_stats.seconds_played;
    const char *labels[8] = { "GAMES PLAYED", "WINS VS CPU", "LOSSES VS CPU", "WIN RATE", "LONGEST RALLY", "BEST SURVIVAL SCORE", "TOTAL HITS", "TIME PLAYED" };
    for (int i = 0; i < 8; i++) {
        switch (i) {
        case 0: snprintf(b, sizeof b, "%d", g_stats.played); break;
        case 1: snprintf(b, sizeof b, "%d", g_stats.wins); break;
        case 2: snprintf(b, sizeof b, "%d", g_stats.losses); break;
        case 3: snprintf(b, sizeof b, "%d%%", rate); break;
        case 4: snprintf(b, sizeof b, "%d", g_stats.best_rally); break;
        case 5: snprintf(b, sizeof b, "%d", g_stats.best_survival); break;
        case 6: snprintf(b, sizeof b, "%d", g_stats.total_hits); break;
        default: snprintf(b, sizeof b, "%dh %02dm %02ds", secs / 3600, (secs / 60) % 60, secs % 60); break;
        }
        snprintf(a, sizeof a, "%s", labels[i]);
        int y = 200 + i * 92;
        if (i % 2 == 0) gfx_rect(446, y - 8, 1028, 80, 255, 255, 255, 14);
        gfx_text(490, y + 4, 38, a, 190, 210, 255, 255, AL_LEFT);
        gfx_text_shadow(1430, y + 4, 40, b, 255, 235, 140, 255, AL_RIGHT);
    }
    draw_footer("O  BACK");
}

static void draw_controls(void) {
    draw_bg(tx_game);
    part_draw();
    gfx_text_shadow(W / 2, 40, 64, "CONTROLS", 255, 255, 255, 255, AL_CENTER);
    draw_panel(240, 160, 1440, 830);
    static const char *K[9] = { "MOVE", "DASH", "PAUSE", "CONFIRM / BACK", "2 PLAYERS, TWO PADS", "2 PLAYERS, ONE PAD (P1)", "2 PLAYERS, ONE PAD (P2)", "POWER-UPS", "LIGHT BAR AND RUMBLE" };
    static const char *V[9] = { "LEFT STICK OR D-PAD", "R1  R2  OR  TRIANGLE", "OPTIONS OR TOUCHPAD", "X  AND  O",
                                "NEEDS A SECOND LOGGED-IN USER", "LEFT STICK + R1", "RIGHT STICK OR SQUARE/CIRCLE + L1",
                                "W WIDE  S SHRINK  T SLOW  M MULTI  F FREEZE  + LIFE", "REACT TO HITS, POINTS AND WINS" };
    for (int i = 0; i < 9; i++) {
        int y = 190 + i * 88;
        if (i % 2 == 0) gfx_rect(256, y - 8, 1408, 76, 255, 255, 255, 14);
        gfx_text(290, y + 6, 32, K[i], 255, 235, 140, 255, AL_LEFT);
        gfx_text(1640, y + 6, 32, V[i], 215, 230, 255, 255, AL_RIGHT);
    }
    draw_footer("O  BACK");
}

static void draw_credits(void) {
    draw_bg(tx_game);
    part_draw();
    const Theme *t = TH();
    float slide = clampf(scr_t / 40.0f, 0, 1);
    int al = (int)(255 * slide);
    gfx_text_shadow(W / 2, 40, 64, "CREDITS", 255, 255, 255, 255, AL_CENTER);
    if (tx_credit) {
        int w = 560, h = credit_h * w / credit_w;
        int x = 200, y = 250;
        gfx_rect(x - 10, y - 10, w + 20, h + 20, t->acc[0], t->acc[1], t->acc[2], al);
        gfx_copy(tx_credit, x, y, w, h, al);
    }
    int tx = 880;
    gfx_text(tx, 210, 34, "CREATED BY", 170, 200, 255, al, AL_LEFT);
    gfx_text_shadow(tx, 250, 130, "MEXRLDEV", 255, 255, 255, al, AL_LEFT);
    gfx_rect(tx + 4, 440, 600, 5, t->acc[0], t->acc[1], t->acc[2], al);
    static const char *L1[] = { "PONG 2 FOR PS4 HOMEBREW", "MENU AND GAME MUSIC INCLUDED" };
    static const char *L2[] = { "BUILT WITH THE OPENORBIS TOOLCHAIN AND SDL2", "MP3 DECODING: MINIMP3 (CC0)", "FONT: POPPINS (SIL OPEN FONT LICENSE)" };
    for (int i = 0; i < 2; i++) { int a2 = clampi((scr_t - 40 - i * 15) * 8, 0, 255); gfx_text(tx, 480 + i * 56, 38, L1[i], 255, 235, 140, a2, AL_LEFT); }
    for (int i = 0; i < 3; i++) { int a2 = clampi((scr_t - 80 - i * 15) * 8, 0, 255); gfx_text(tx, 640 + i * 52, 30, L2[i], 200, 220, 255, a2, AL_LEFT); }
    int a3 = clampi((scr_t - 150) * 6, 0, 255);
    gfx_text_shadow(tx, 880, 52, "THANKS FOR PLAYING!", 255, 255, 255, a3, AL_LEFT);
    draw_footer("O  BACK");
}

static void draw_paddle(int i) {
    const Theme *t = TH();
    const int *c = i == 0 ? t->p1 : t->p2;
    float pw = pwidth(i), py = pay(i), x = P[i].x;
    gfx_glow(x + pw / 2, py + PH / 2.0f, pw * 0.8f, c[0], c[1], c[2], 90);
    gfx_circle(x + PH / 2.0f, py + PH / 2.0f, PH / 2.0f, c[0], c[1], c[2], 255);
    gfx_circle(x + pw - PH / 2.0f, py + PH / 2.0f, PH / 2.0f, c[0], c[1], c[2], 255);
    gfx_rect((int)(x + PH / 2), (int)py, (int)(pw - PH), PH, c[0], c[1], c[2], 255);
    gfx_rect((int)(x + PH / 2), (int)py + 4, (int)(pw - PH), 6, 255, 255, 255, 90);
    if (P[i].freeze > 0) gfx_rect((int)x, (int)py, (int)pw, PH, 170, 235, 255, 150);
    if (P[i].cd > 0) {
        int w = (int)(pw * (1.0f - P[i].cd / 70.0f));
        gfx_rect((int)x, (int)(i == 0 ? py + PH + 8 : py - 12), w, 4, 255, 255, 255, 110);
    }
}

static void draw_balls(void) {
    const Theme *t = TH();
    float hot = clampf((bspeed - BASE_SPEED) / (MAX_SPEED - BASE_SPEED), 0, 1);
    for (int k = MAXB - 1; k >= 0; k--) {
        Ball *b = &B[k];
        if (!b->on) continue;
        int r = mixi(t->ball[0], 255, hot), g = mixi(t->ball[1], 120, hot), bl = mixi(t->ball[2], 80, hot);
        if (b->ghost) { r = 200; g = 160; bl = 255; }
        for (int i = b->tn - 1; i >= 0; i--) {
            float f = 1.0f - (float)i / TRAIL;
            gfx_circle(b->tx[i], b->ty[i], BR * (0.30f + 0.62f * f), r, g, bl, (int)(110 * f));
        }
        gfx_glow(b->x, b->y, BR * 3.6f, r, g, bl, b->ghost ? 90 : 130);
        gfx_circle(b->x, b->y, (float)BR, r, g, bl, 255);
        gfx_circle(b->x - 5, b->y - 6, 7, 255, 255, 255, 150);
    }
}

static void draw_pickup(void) {
    if (!pu.on) return;
    if (pu.life < 120 && (pu.life / 8) % 2) return;
    float bob = sinf(g_tick * 0.1f) * 6;
    const int *c = PU_COL[pu.type];
    gfx_glow(pu.x, pu.y + bob, 96, c[0], c[1], c[2], 150);
    gfx_circle(pu.x, pu.y + bob, 40, c[0], c[1], c[2], 255);
    gfx_circle(pu.x, pu.y + bob, 33, 8, 12, 34, 235);
    gfx_text_centered((int)pu.x, (int)(pu.y + bob), 34, PU_LABEL[pu.type], c[0], c[1], c[2], 255);
}

static void draw_hud(void) {
    const Theme *t = TH();
    char b[64];
    if (!solo()) {
        snprintf(b, sizeof b, "%d", P[1].score); gfx_text(150, 340, 150, b, t->p2[0], t->p2[1], t->p2[2], 120, AL_LEFT);
        snprintf(b, sizeof b, "%d", P[0].score); gfx_text(150, 560, 150, b, t->p1[0], t->p1[1], t->p1[2], 120, AL_LEFT);
        gfx_text(160, 300, 30, mode == M_CPU ? "CPU" : "P2", t->p2[0], t->p2[1], t->p2[2], 200, AL_LEFT);
        gfx_text(160, 740, 30, mode == M_CPU ? "YOU" : "P1", t->p1[0], t->p1[1], t->p1[2], 200, AL_LEFT);
        snprintf(b, sizeof b, "FIRST TO %d", WIN_SCORES[g_set.win_idx]);
        gfx_text(W / 2, 46, 26, b, 190, 205, 245, 200, AL_CENTER);
    } else {
        snprintf(b, sizeof b, "%d", surv_score);
        if (mode == M_SURV) {
            gfx_text(150, 340, 150, b, t->p1[0], t->p1[1], t->p1[2], 120, AL_LEFT);
            gfx_text(160, 300, 30, "SCORE", t->p1[0], t->p1[1], t->p1[2], 200, AL_LEFT);
            snprintf(b, sizeof b, "BEST %d", g_stats.best_survival);
            gfx_text(160, 520, 30, b, 190, 205, 245, 200, AL_LEFT);
            for (int i = 0; i < lives; i++) gfx_circle(W - 150.0f - i * 46, 70, 15, 255, 110, 160, 255);
            gfx_text(W - 130, 100, 26, "LIVES", 190, 205, 245, 200, AL_RIGHT);
        } else {
            gfx_text(W / 2, 46, 26, "PRACTICE", 190, 205, 245, 200, AL_CENTER);
        }
    }
    snprintf(b, sizeof b, "RALLY %d", rally);
    gfx_text(W - 130, 520, 36, b, 255, 235, 140, 230, AL_RIGHT);
    int bar = (int)(240 * clampf((bspeed - BASE_SPEED) / (MAX_SPEED - BASE_SPEED), 0, 1));
    gfx_text(W - 130, 575, 24, "SPEED", 170, 190, 235, 200, AL_RIGHT);
    gfx_rect(W - 370, 612, 240, 10, 60, 70, 110, 200);
    gfx_rect(W - 370, 612, bar, 10, 255, mixi(220, 90, (float)bar / 240), 90, 255);
    if (slow_t > 0) gfx_text(W - 130, 650, 24, "SLOW MOTION", 100, 170, 255, 230, AL_RIGHT);
}

static void draw_world(void) {
    const Theme *t = TH();
    draw_bg(tx_game);
    float sx = 0, sy = 0;
    if (shake > 0.3f) { sx = frnd(-shake, shake); sy = frnd(-shake, shake); }
    gfx_set_offset((int)sx, (int)sy);
    for (int y = 70; y < H - 60; y += 60) gfx_rect(W / 2 - 3, y, 6, 30, 120, 150, 230, 55);
    if (solo()) {
        gfx_rect(WL, WALL_BOTTOM - 22, WR - WL, 22, 120, 170, 255, 200);
        gfx_rect(WL, WALL_BOTTOM - 22, WR - WL, 5, 255, 255, 255, 110);
    }
    draw_hud();
    draw_pickup();
    draw_paddle(0);
    if (has_top_paddle()) draw_paddle(1);
    draw_balls();
    part_draw();

    if (msg_t > 0) {
        float a = msg_t > 20 ? 1.0f : msg_t / 20.0f;
        float pop = msg_t > 85 ? 1.0f + (msg_t - 85) * 0.03f : 1.0f;
        gfx_text_shadow(W / 2, 220, 60 * pop, msg, msg_r, msg_g, msg_b, (int)(255 * a), AL_CENTER);
    }
    if (pstate == PS_COUNT) {
        int step = pst_t / 60;
        char b[8];
        if (step < 3) snprintf(b, sizeof b, "%d", 3 - step); else snprintf(b, sizeof b, "GO");
        float f = 1.0f - (float)(pst_t % 60) / 60.0f;
        gfx_glow(W / 2.0f, H / 2.0f, 260, t->acc[0], t->acc[1], t->acc[2], (int)(90 * f));
        gfx_text_shadow(W / 2, H / 2 - (int)(gfx_text_h(180 + 60 * f) / 2), 180 + 60 * f, b, 255, 255, 255, (int)(150 + 105 * f), AL_CENTER);
    }
    gfx_set_offset(0, 0);
}

static void draw_pause(void) {
    gfx_text_shadow(W / 2, 270, 80, "PAUSED", 255, 255, 255, 255, AL_CENTER);
    draw_panel(660, 400, 600, 330);
    draw_list(PAUSE_ITEMS, 3, sel[SC_PAUSE], 676, 420, 100, 568, 40);
    draw_footer("X  SELECT        OPTIONS  RESUME");
}

static void draw_over(void) {
    const Theme *t = TH();
    char b[96];
    const char *title;
    if (mode == M_SURV) title = "GAME OVER";
    else if (mode == M_PRAC) title = "PRACTICE OVER";
    else if (mode == M_2P) title = winner == 0 ? "PLAYER 1 WINS" : "PLAYER 2 WINS";
    else title = winner == 0 ? "YOU WIN!" : "YOU LOSE";
    int good = (mode == M_CPU && winner == 0) || mode == M_2P;
    gfx_text_shadow(W / 2, 200, 104, title, good ? 255 : 255, good ? 215 : 120, good ? 90 : 120, 255, AL_CENTER);
    draw_panel(560, 360, 800, 500);
    if (mode == M_SURV) {
        snprintf(b, sizeof b, "SCORE  %d", surv_score); gfx_text(W / 2, 390, 56, b, 255, 235, 140, 255, AL_CENTER);
        snprintf(b, sizeof b, over_new_best ? "NEW BEST!" : "BEST  %d", g_stats.best_survival);
        gfx_text(W / 2, 470, 40, b, over_new_best ? 120 : 190, over_new_best ? 255 : 205, over_new_best ? 170 : 245, 255, AL_CENTER);
    } else {
        snprintf(b, sizeof b, "%d  -  %d", P[0].score, P[1].score);
        gfx_text(W / 2, 390, 90, b, 255, 255, 255, 255, AL_CENTER);
    }
    snprintf(b, sizeof b, "LONGEST RALLY  %d", best_rally_match);
    gfx_text(W / 2, 540, 34, b, 200, 220, 255, 255, AL_CENTER);
    snprintf(b, sizeof b, "TIME  %d:%02d", match_ticks / 3600, (match_ticks / 60) % 60);
    gfx_text(W / 2, 586, 34, b, 200, 220, 255, 255, AL_CENTER);
    draw_list(OVER_ITEMS, 2, sel[SC_OVER], 780, 664, 80, 360, 38);
    (void)t;
    draw_footer("X  SELECT");
}

/* ================================================================ public API */
void game_init(SDL_Window *win, SDL_Renderer *ren) {
    gfx_init(win, ren);
    save_load();
    apply_audio_settings();
    tx_menu = gfx_load_bmp("menu_bg.bmp", NULL, NULL);
    if (!tx_menu) tx_menu = gfx_gradient(10, 14, 50, 120, 40, 120);
    tx_game = gfx_load_bmp("game_bg.bmp", NULL, NULL);
    if (!tx_game) tx_game = gfx_gradient(6, 9, 28, 14, 24, 64);
    tx_credit = gfx_load_bmp("credits.bmp", &credit_w, &credit_h);
    if (audio_load_music(TRACK_MENU, "/app0/assets/menu_music.mp3") != 0 && audio_load_music(TRACK_MENU, "assets/menu_music.mp3") != 0)
        plat_log("PONG: menu music not found, menu will be silent");
    if (audio_load_music(TRACK_GAME, "/app0/assets/game_music.mp3") != 0 && audio_load_music(TRACK_GAME, "assets/game_music.mp3") != 0)
        plat_log("PONG: game music not found, matches will have sound effects only");
    rs ^= (uint32_t)SDL_GetTicks() * 2654435761u;
    scr = SC_INTRO; scr_t = 0;
    plat_log("PONG: game ready (pads=%d)", plat_pad_count());
}

void game_tick(void) {
    read_input();
    update_screens();
    part_update();
    if (shake > 0) shake *= 0.86f;
    fx_update();
    g_tick++;
}

void game_draw(void) {
    { static Uint32 last; Uint32 n = SDL_GetTicks(); if (last) { frame_dt = n - last; if (frame_dt > 250) frame_dt = 250; if (frame_dt < 8) frame_dt = 8; } last = n; }
    gfx_begin_frame();
    if (scr != SC_PAUSE && scr != SC_OVER) gfx_snapshot_clear();
    switch (scr) {
    case SC_INTRO: draw_intro(); break;
    case SC_MENU: draw_menu(); break;
    case SC_MODE: draw_mode(); break;
    case SC_SETTINGS: draw_settings(); break;
    case SC_STATS: draw_stats(); break;
    case SC_CONTROLS: draw_controls(); break;
    case SC_CREDITS: draw_credits(); break;
    case SC_PLAY: draw_world(); break;
    case SC_PAUSE:
        if (!gfx_snapshot_valid()) { draw_world(); gfx_snapshot_capture(150); } else gfx_snapshot_draw();
        draw_pause(); break;
    case SC_OVER:
        if (!gfx_snapshot_valid()) { draw_world(); gfx_snapshot_capture(190); } else gfx_snapshot_draw();
        draw_over(); break;
    }
    gfx_end_frame();
}

int game_quit_requested(void) { return quit_req; }

void game_shutdown(void) {
    save_write();
    for (int p = 0; p < 2; p++) { plat_pad_rumble(p, 0, 0); plat_pad_light_reset(p); }
    gfx_img_free(tx_menu);
    gfx_img_free(tx_game);
    gfx_img_free(tx_credit);
    gfx_shutdown();
}

/* ---- test hooks ---- */
int game_dbg(int what) {
    switch (what) {
    case DBG_SCREEN: return scr;
    case DBG_SCORE0: return P[0].score;
    case DBG_SCORE1: return P[1].score;
    case DBG_RALLY: return rally;
    case DBG_LIVES: return lives;
    case DBG_MODE: return mode;
    case DBG_PSTATE: return pstate;
    case DBG_SURV: return surv_score;
    case DBG_BX: return (int)B[0].x;
    case DBG_BY: return (int)B[0].y;
    case DBG_PX0: return (int)(P[0].x + pwidth(0) / 2);
    case DBG_PX1: return (int)(P[1].x + pwidth(1) / 2);
    case DBG_BALLS: { int n = 0; for (int i = 0; i < MAXB; i++) n += B[i].on; return n; }
    }
    return 0;
}
void game_dbg_goto(int s) { set_screen(s); }
void game_dbg_start(int m) { new_match(m); }
void game_dbg_ball(float x, float y, float vx, float vy) {
    if (!B[0].on) { B[0].on = 1; }
    B[0].x = x; B[0].y = y; B[0].vx = vx; B[0].vy = vy;
    if (pstate != PS_LIVE) { pstate = PS_LIVE; }
}
void game_dbg_spawn_pickup(int type) { spawn_pickup(type); }
void game_dbg_message(const char *s) { msg_set(s, 255, 235, 140); }
