#ifndef SAVE_H
#define SAVE_H

typedef struct {
    int music;        /* 0..10 */
    int sfx;          /* 0..10 */
    int rumble;       /* 0/1 */
    int lightbar;     /* 0/1 */
    int difficulty;   /* 0 easy, 1 normal, 2 hard, 3 insane */
    int win_idx;      /* index into WIN_SCORES */
    int powerups;     /* 0/1 */
    int shake;        /* 0/1 */
    int theme;        /* 0..3 */
} Settings;

typedef struct {
    int played, wins, losses;
    int best_rally;        /* longest rally in any mode */
    int best_survival;     /* best survival score */
    int total_hits;
    int seconds_played;
} Stats;

extern Settings g_set;
extern Stats g_stats;
extern const int WIN_SCORES[4];

void save_defaults(void);
void save_load(void);
void save_write(void);
void save_reset_stats(void);

#endif
