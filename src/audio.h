/* Software mixer: synthesized sound effects plus two songs, menu and game (MP3, decoded with minimp3).
   audio_mix() is called from the platform's audio thread. Everything else is called
   from the game thread and only touches atomics or a small queue. */
#ifndef AUDIO_H
#define AUDIO_H
#include <stdint.h>

#define AUDIO_RATE 48000
#define AUDIO_GRAIN 256

enum {
    SFX_HIT, SFX_WALL, SFX_SCORE, SFX_CONCEDE, SFX_WIN, SFX_LOSE,
    SFX_MOVE, SFX_SELECT, SFX_BACK, SFX_POWERUP, SFX_COUNT3, SFX_GO,
    SFX_DASH, SFX_PAUSE, SFX_LIFE_LOST, SFX_FREEZE, SFX_SPLASH, SFX__N
};

void audio_init(void);
enum { TRACK_NONE = 0, TRACK_MENU = 1, TRACK_GAME = 2, TRACK__N = 3 };
int  audio_load_music(int track, const char *path);  /* 0 = ok; call before the audio thread starts */
void audio_music_select(int track);           /* fades the current song out, then fades this one in (TRACK_NONE = silence) */
int  audio_music_selected(void);
float audio_music_level(int track);           /* current fade gain 0..1 of one song, for tests */
void audio_music_duck(int on);                /* lowers the song while paused */
void audio_set_volumes(int music_0_10, int sfx_0_10);
void audio_sfx(int id);
void audio_sfx_pitch(int id, int semitones);  /* same sound shifted up or down */
void audio_mix(int16_t *out, int frames);     /* interleaved stereo S16 */

#endif
