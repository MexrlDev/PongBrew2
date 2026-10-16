#include "audio.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define MINIMP3_IMPLEMENTATION
#define MINIMP3_ONLY_MP3
#define MINIMP3_NO_SIMD
#include "third_party/minimp3.h"

#define LD(p) __atomic_load_n((p), __ATOMIC_ACQUIRE)
#define ST(p, v) __atomic_store_n((p), (v), __ATOMIC_RELEASE)

/* ---------------- music: two songs, each with its own decoder and fade gain ---------------- */
typedef struct {
    uint8_t *mp3; int size, pos;
    mp3dec_t dec;
    short pcm[MINIMP3_MAX_SAMPLES_PER_FRAME];
    int pcm_len, pcm_pos, pcm_ch;
    float gain;                 /* audio thread only */
} Song;
static Song g_song[TRACK__N];

static int g_sel;               /* requested by the game thread */
static int g_duck;
static int g_vol_music = 7, g_vol_sfx = 8;
static float g_duck_gain = 1.0f;

/* ---------------- sfx queue (single producer, single consumer) ---------------- */
#define QN 64
static int q_id[QN], q_st[QN];
static int q_head, q_tail;

typedef struct { int on, wave, len, pos, delay; float f0, f1, ph, vol; } Voice;
#define NV 20
static Voice voices[NV];
static uint32_t rng = 0x1234567u;

static float frand(void) {
    rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
    return (float)(rng & 0xFFFFFF) / 8388608.0f - 1.0f;
}

void audio_init(void) {
    memset(voices, 0, sizeof voices);
    for (int i = 0; i < TRACK__N; i++) g_song[i].gain = 0;
    g_sel = TRACK_NONE; g_duck = 0; g_duck_gain = 1.0f; q_head = q_tail = 0;
}

int audio_load_music(int track, const char *path) {
    if (track <= 0 || track >= TRACK__N) return -9;
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (n <= 1024 || n > 40 * 1024 * 1024) { fclose(f); return -2; }
    uint8_t *buf = (uint8_t *)malloc((size_t)n);
    if (!buf) { fclose(f); return -3; }
    size_t got = fread(buf, 1, (size_t)n, f);
    fclose(f);
    if (got != (size_t)n) { free(buf); return -4; }
    int pos = 0;
    if (n > 10 && buf[0] == 'I' && buf[1] == 'D' && buf[2] == '3') {   /* skip an ID3v2 tag */
        pos = 10 + ((buf[6] & 0x7F) << 21 | (buf[7] & 0x7F) << 14 | (buf[8] & 0x7F) << 7 | (buf[9] & 0x7F));
        if (pos >= n) pos = 0;
    }
    Song *s = &g_song[track];
    mp3dec_init(&s->dec);
    s->size = (int)n; s->pos = pos; s->pcm_len = s->pcm_pos = 0; s->pcm_ch = 2;
    ST(&s->mp3, buf);
    return 0;
}

void audio_music_select(int track) { if (track < 0 || track >= TRACK__N) track = 0; ST(&g_sel, track); }
int  audio_music_selected(void) { return LD(&g_sel); }
float audio_music_level(int track) { return (track > 0 && track < TRACK__N) ? g_song[track].gain : 0.0f; }
void audio_music_duck(int on) { ST(&g_duck, on ? 1 : 0); }
void audio_set_volumes(int m, int s) {
    if (m < 0) m = 0; if (m > 10) m = 10; if (s < 0) s = 0; if (s > 10) s = 10;
    ST(&g_vol_music, m); ST(&g_vol_sfx, s);
}

static int decode_next(Song *s) {
    mp3dec_frame_info_t info;
    for (int tries = 0; tries < 6; tries++) {
        if (s->pos >= s->size) { s->pos = 0; mp3dec_init(&s->dec); }   /* loop */
        int samples = mp3dec_decode_frame(&s->dec, s->mp3 + s->pos, s->size - s->pos, s->pcm, &info);
        s->pos += info.frame_bytes;
        if (samples > 0 && info.channels > 0) {
            s->pcm_len = samples; s->pcm_ch = info.channels; s->pcm_pos = 0;
            return 1;
        }
        if (info.frame_bytes == 0) { s->pos = 0; mp3dec_init(&s->dec); }
    }
    return 0;
}

/* ---------------- sfx definitions ---------------- */
enum { W_SINE, W_SQUARE, W_TRI, W_SAW, W_NOISE };

static void add_voice(int wave, float f0, float f1, int ms, float vol, int delay_ms) {
    for (int i = 0; i < NV; i++) {
        if (!voices[i].on) {
            Voice *v = &voices[i];
            v->wave = wave; v->f0 = f0; v->f1 = f1; v->ph = 0; v->vol = vol;
            v->len = AUDIO_RATE / 1000 * ms; v->pos = 0; v->delay = AUDIO_RATE / 1000 * delay_ms;
            v->on = 1;
            return;
        }
    }
}

static float semi(int st) { return powf(2.0f, (float)st / 12.0f); }

static void start_sfx(int id, int st) {
    float k = semi(st);
    switch (id) {
    case SFX_HIT:   add_voice(W_SQUARE, 520 * k, 330 * k, 95, 0.34f, 0); add_voice(W_NOISE, 1, 1, 18, 0.22f, 0); break;
    case SFX_WALL:  add_voice(W_TRI, 330 * k, 210 * k, 70, 0.42f, 0); break;
    case SFX_SCORE:
        add_voice(W_SQUARE, 523, 523, 80, 0.30f, 0); add_voice(W_SQUARE, 659, 659, 80, 0.30f, 80);
        add_voice(W_SQUARE, 784, 784, 80, 0.30f, 160); add_voice(W_TRI, 1046, 1046, 260, 0.36f, 240); break;
    case SFX_CONCEDE: add_voice(W_SAW, 330, 90, 340, 0.30f, 0); add_voice(W_SQUARE, 165, 60, 340, 0.20f, 0); break;
    case SFX_WIN:
        add_voice(W_SQUARE, 523, 523, 110, 0.30f, 0); add_voice(W_SQUARE, 659, 659, 110, 0.30f, 120);
        add_voice(W_SQUARE, 784, 784, 110, 0.30f, 240); add_voice(W_SQUARE, 1046, 1046, 160, 0.30f, 360);
        add_voice(W_TRI, 1046, 1046, 700, 0.34f, 520); add_voice(W_TRI, 784, 784, 700, 0.26f, 520);
        add_voice(W_TRI, 659, 659, 700, 0.22f, 520); break;
    case SFX_LOSE:
        add_voice(W_TRI, 392, 392, 200, 0.40f, 0); add_voice(W_TRI, 330, 330, 200, 0.40f, 220);
        add_voice(W_TRI, 262, 262, 200, 0.40f, 440); add_voice(W_TRI, 196, 150, 600, 0.42f, 660); break;
    case SFX_MOVE:   add_voice(W_SQUARE, 880, 880, 28, 0.20f, 0); break;
    case SFX_SELECT: add_voice(W_SQUARE, 660, 990, 110, 0.28f, 0); add_voice(W_TRI, 990, 1320, 90, 0.2f, 70); break;
    case SFX_BACK:   add_voice(W_SQUARE, 520, 330, 110, 0.26f, 0); break;
    case SFX_POWERUP:
        add_voice(W_SQUARE, 400, 1300, 220, 0.26f, 0); add_voice(W_TRI, 800, 1800, 220, 0.22f, 60); break;
    case SFX_COUNT3: add_voice(W_SQUARE, 440, 440, 130, 0.32f, 0); break;
    case SFX_GO:     add_voice(W_SQUARE, 880, 880, 280, 0.34f, 0); add_voice(W_TRI, 1320, 1320, 280, 0.26f, 0); break;
    case SFX_DASH:   add_voice(W_NOISE, 1, 1, 110, 0.20f, 0); add_voice(W_SINE, 300, 900, 110, 0.18f, 0); break;
    case SFX_PAUSE:  add_voice(W_SQUARE, 600, 600, 70, 0.26f, 0); add_voice(W_SQUARE, 450, 450, 90, 0.26f, 80); break;
    case SFX_LIFE_LOST: add_voice(W_SAW, 400, 80, 420, 0.32f, 0); add_voice(W_NOISE, 1, 1, 220, 0.22f, 0); break;
    case SFX_FREEZE: add_voice(W_SINE, 1500, 500, 380, 0.30f, 0); add_voice(W_SQUARE, 1200, 400, 380, 0.12f, 0); break;
    case SFX_SPLASH: add_voice(W_TRI, 220, 220, 260, 0.30f, 0); add_voice(W_TRI, 330, 330, 260, 0.26f, 120);
                     add_voice(W_TRI, 440, 440, 500, 0.30f, 240); break;
    default: break;
    }
}

void audio_sfx(int id) { audio_sfx_pitch(id, 0); }
void audio_sfx_pitch(int id, int semitones) {
    int h = LD(&q_head);
    int next = (h + 1) % QN;
    if (next == LD(&q_tail)) return;      /* queue full, drop */
    q_id[h] = id; q_st[h] = semitones;
    ST(&q_head, next);
}

/* ---------------- mixer ---------------- */
void audio_mix(int16_t *out, int frames) {
    /* drain queue */
    int t = LD(&q_tail), h = LD(&q_head);
    while (t != h) { start_sfx(q_id[t], q_st[t]); t = (t + 1) % QN; }
    ST(&q_tail, t);

    int sel = LD(&g_sel);
    float duck_target = LD(&g_duck) ? 0.35f : 1.0f;
    float mv = (float)LD(&g_vol_music) / 10.0f; mv = mv * mv * 0.85f;
    float sv = (float)LD(&g_vol_sfx) / 10.0f;   sv = sv * sv * 0.9f;

    for (int i = 0; i < frames; i++) {
        float l = 0, r = 0;
        g_duck_gain += (duck_target - g_duck_gain) * 0.0004f;
        /* a song fades out first; the newly selected one starts rising once the others are nearly silent */
        float others = 0;
        for (int t = 1; t < TRACK__N; t++) if (t != sel) others += g_song[t].gain;
        for (int t = 1; t < TRACK__N; t++) {
            Song *sg = &g_song[t];
            float target = (t == sel && others < 0.03f) ? 1.0f : 0.0f;
            float rate = target > sg->gain ? 0.00008f : 0.00020f;     /* out is quicker than in */
            sg->gain += (target - sg->gain) * rate;
            if (sg->gain < 0.0005f && target == 0.0f) sg->gain = 0;    /* settle at exactly silent */
            uint8_t *mp3 = (uint8_t *)LD(&sg->mp3);
            if (!mp3 || sg->gain <= 0.0f) continue;
            if (sg->pcm_pos >= sg->pcm_len) decode_next(sg);
            if (sg->pcm_pos < sg->pcm_len) {
                float ml, mr;
                if (sg->pcm_ch >= 2) { ml = sg->pcm[sg->pcm_pos * sg->pcm_ch]; mr = sg->pcm[sg->pcm_pos * sg->pcm_ch + 1]; }
                else ml = mr = sg->pcm[sg->pcm_pos];
                sg->pcm_pos++;
                float g = sg->gain * mv * g_duck_gain / 32768.0f;
                l += ml * g; r += mr * g;
            }
        }
        float s = 0;
        for (int k = 0; k < NV; k++) {
            Voice *v = &voices[k];
            if (!v->on) continue;
            if (v->delay > 0) { v->delay--; continue; }
            float p = (float)v->pos / (float)v->len;
            float f = v->f0 + (v->f1 - v->f0) * p;
            v->ph += f / AUDIO_RATE; if (v->ph >= 1.0f) v->ph -= 1.0f;
            float x;
            switch (v->wave) {
            case W_SINE:   x = sinf(v->ph * 6.2831853f); break;
            case W_SQUARE: x = v->ph < 0.5f ? 1.0f : -1.0f; break;
            case W_TRI:    x = v->ph < 0.5f ? 4 * v->ph - 1 : 3 - 4 * v->ph; break;
            case W_SAW:    x = 2 * v->ph - 1; break;
            default:       x = frand(); break;
            }
            float env = 1.0f - p;
            if (v->pos < 96) env *= (float)v->pos / 96.0f;
            s += x * env * v->vol;
            if (++v->pos >= v->len) v->on = 0;
        }
        l += s * sv; r += s * sv;
        if (l > 0.98f) l = 0.98f; if (l < -0.98f) l = -0.98f;
        if (r > 0.98f) r = 0.98f; if (r < -0.98f) r = -0.98f;
        out[2 * i] = (int16_t)(l * 32767.0f);
        out[2 * i + 1] = (int16_t)(r * 32767.0f);
    }
}
