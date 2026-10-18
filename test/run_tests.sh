#!/bin/sh
# Builds and runs the headless PC test. Needs clang or gcc and SDL2 development files.
# Set SDL2_PREFIX to an SDL2 install prefix if SDL2 is not in the system paths.
set -e
cd "$(dirname "$0")/.."
CC="${CC:-clang}"
mkdir -p test/out
if [ -n "$SDL2_PREFIX" ]; then
  CFLAGS_SDL="-I$SDL2_PREFIX/include -I$SDL2_PREFIX/include/SDL2"
  LIBS_SDL="-L$SDL2_PREFIX/lib -lSDL2 -lm -ldl -lpthread"
else
  CFLAGS_SDL="$(sdl2-config --cflags)"
  LIBS_SDL="$(sdl2-config --libs)"
fi
SRCS="src/audio.c src/gfx.c src/save.c src/game.c src/platform_pc.c test/test_main.c"
$CC -O1 -g -Wall -Wextra -Wno-unused-parameter -std=gnu11 $CFLAGS_SDL $SRCS -o test/test_run $LIBS_SDL
if command -v ffmpeg >/dev/null 2>&1 && [ -f assets/menu_music.mp3 ]; then
  ffmpeg -v error -y -i assets/menu_music.mp3 -f s16le -ac 2 -ar 48000 test/out/ref.raw
fi
./test/test_run
