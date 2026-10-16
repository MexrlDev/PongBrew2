#include "save.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

Settings g_set;
Stats g_stats;
const int WIN_SCORES[4] = { 5, 7, 11, 15 };

/* /data is writable for jailbroken homebrew; on a PC the file sits in the working directory.
   Every failure is ignored: the game just forgets its settings. */
#ifdef PS4_BUILD
#define SAVE_PATH "/data/pongbrew2.cfg"
#else
#define SAVE_PATH "pongbrew2.cfg"
#endif

static int clampi(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

void save_defaults(void) {
    g_set.music = 7; g_set.sfx = 8; g_set.rumble = 1; g_set.lightbar = 1;
    g_set.difficulty = 1; g_set.win_idx = 1; g_set.powerups = 1; g_set.shake = 1; g_set.theme = 0;
}

void save_reset_stats(void) { memset(&g_stats, 0, sizeof g_stats); }

void save_load(void) {
    save_defaults();
    save_reset_stats();
    FILE *f = fopen(SAVE_PATH, "r");
    if (!f) return;
    char line[96];
    while (fgets(line, sizeof line, f)) {
        char *eq = strchr(line, '=');
        if (!eq) continue;
        *eq = 0;
        int v = atoi(eq + 1);
        const char *k = line;
        if (!strcmp(k, "music")) g_set.music = clampi(v, 0, 10);
        else if (!strcmp(k, "sfx")) g_set.sfx = clampi(v, 0, 10);
        else if (!strcmp(k, "rumble")) g_set.rumble = clampi(v, 0, 1);
        else if (!strcmp(k, "lightbar")) g_set.lightbar = clampi(v, 0, 1);
        else if (!strcmp(k, "difficulty")) g_set.difficulty = clampi(v, 0, 3);
        else if (!strcmp(k, "win_idx")) g_set.win_idx = clampi(v, 0, 3);
        else if (!strcmp(k, "powerups")) g_set.powerups = clampi(v, 0, 1);
        else if (!strcmp(k, "shake")) g_set.shake = clampi(v, 0, 1);
        else if (!strcmp(k, "theme")) g_set.theme = clampi(v, 0, 3);
        else if (!strcmp(k, "played")) g_stats.played = clampi(v, 0, 9999999);
        else if (!strcmp(k, "wins")) g_stats.wins = clampi(v, 0, 9999999);
        else if (!strcmp(k, "losses")) g_stats.losses = clampi(v, 0, 9999999);
        else if (!strcmp(k, "best_rally")) g_stats.best_rally = clampi(v, 0, 9999999);
        else if (!strcmp(k, "best_survival")) g_stats.best_survival = clampi(v, 0, 9999999);
        else if (!strcmp(k, "total_hits")) g_stats.total_hits = clampi(v, 0, 999999999);
        else if (!strcmp(k, "seconds")) g_stats.seconds_played = clampi(v, 0, 999999999);
    }
    fclose(f);
}

void save_write(void) {
    FILE *f = fopen(SAVE_PATH, "w");
    if (!f) return;
    fprintf(f, "music=%d\nsfx=%d\nrumble=%d\nlightbar=%d\ndifficulty=%d\nwin_idx=%d\npowerups=%d\nshake=%d\ntheme=%d\n",
            g_set.music, g_set.sfx, g_set.rumble, g_set.lightbar, g_set.difficulty, g_set.win_idx,
            g_set.powerups, g_set.shake, g_set.theme);
    fprintf(f, "played=%d\nwins=%d\nlosses=%d\nbest_rally=%d\nbest_survival=%d\ntotal_hits=%d\nseconds=%d\n",
            g_stats.played, g_stats.wins, g_stats.losses, g_stats.best_rally, g_stats.best_survival,
            g_stats.total_hits, g_stats.seconds_played);
    fclose(f);
}
