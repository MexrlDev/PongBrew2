# PONG 2 homebrew - OpenOrbis v0.5.1 toolchain (Docker) + SDL2 bundled in the SDK
#
# Notes:
#   - The SDK's create-eboot only writes an OELF. tools/create-eboot-golden wraps the
#     real create-fself (tools/create-fself, built by tools/build-create-fself.sh)
#     and fails the build if eboot.bin does not start with the SELF magic.
#   - sce_module/*.prx from the SDK are small stubs (about 3 KB in v0.5.1). They ARE
#     packaged: without them the PKG installs but may fail to launch (CE-30008-1).
#   - sce_module/ and sce_sys/about/ are git-ignored, so the rules below recreate them
#     on a fresh checkout.

OO_PS4_TOOLCHAIN ?= /usr/lib/OpenOrbisSDK
SDL2_DIR         ?= $(OO_PS4_TOOLCHAIN)

PROJ        := pong2
TITLE       := PONG 2
TITLE_ID    := PONG00002
CONTENT_ID  := IV0000-PONG00002_00-PONGHOMEBREW0002

TC          := $(OO_PS4_TOOLCHAIN)
UNAME       := $(shell uname -s)
ifeq ($(findstring Linux,$(UNAME)),Linux)
CDIR := linux
else ifeq ($(findstring Darwin,$(UNAME)),Darwin)
CDIR := macos
else
CDIR := windows
endif
BIN         := $(TC)/bin/$(CDIR)
PKGTOOL     := $(BIN)/PkgTool.Core
INTDIR      := build

CC          := clang
LD          := ld.lld

CFLAGS      := --target=x86_64-pc-freebsd12-elf -fPIC -funwind-tables -c -O2 -std=gnu11 -DPS4_BUILD \
               -Isrc -Isrc/third_party \
               -isysroot $(TC) -isystem $(TC)/include \
               -I$(SDL2_DIR)/include -I$(SDL2_DIR)/include/SDL2

# 1080p canvas by default. If a console ever struggles, "make clean && make SD=1" builds the 960x540 canvas (doubled to 1080p).
ifeq ($(SD),1)
CFLAGS += -DGFX_DIV=2
endif

SDL_LIBS    := -lSDL2 -lSceVideoOut -lSceAudioOut -lScePad -lSceUserService \
               -lSceSysmodule -lSceSystemService -lkernel -lc -lc++

LDFLAGS     := -m elf_x86_64 -pie --script $(TC)/link.x --eh-frame-hdr \
               -L$(TC)/lib -L$(SDL2_DIR)/lib \
               $(SDL_LIBS) $(TC)/lib/crt1.o

SRCS        := $(wildcard src/*.c)
OBJS        := $(patsubst src/%.c,$(INTDIR)/%.o,$(SRCS))

.NOTPARALLEL:
.PHONY: all clean check pkg assets test

all: check pkg

check:
	@test -d "$(TC)" || { echo "ERROR: toolchain dir missing"; exit 1; }
	@test -x "$(PKGTOOL)" || { echo "ERROR: PkgTool.Core not found"; exit 1; }
	@test -f "$(SDL2_DIR)/lib/libSDL2.a" || { echo "ERROR: libSDL2.a missing"; exit 1; }
	@test -f sce_sys/icon0.png || { echo "ERROR: sce_sys/icon0.png missing"; exit 1; }
	@test -f assets/menu_music.mp3 && test -f assets/game_music.mp3 || { echo "ERROR: assets/*.mp3 missing"; exit 1; }
	@test -f sce_sys/pic1.png || { echo "ERROR: sce_sys/pic1.png missing"; exit 1; }
	@test -x tools/create-fself || { echo "ERROR: tools/create-fself missing or not executable (run tools/build-create-fself.sh)"; exit 1; }
	@test -f tools/create-eboot-golden || { echo "ERROR: tools/create-eboot-golden missing"; exit 1; }
	@test -f tools/create-gp4 || { echo "ERROR: tools/create-gp4 missing"; exit 1; }

# Regenerates backgrounds, icons, fonts and re-encodes the two songs from assets_src/ (needs Pillow, numpy, ffmpeg).
assets:
	python3 tools/gen_assets.py

# Headless PC test of the whole game (needs clang and SDL2 dev files).
test:
	sh test/run_tests.sh

$(INTDIR)/%.o: src/%.c | $(INTDIR)
	$(CC) $(CFLAGS) -o $@ $<

$(INTDIR):
	mkdir -p $(INTDIR)

$(INTDIR)/$(PROJ).elf: $(OBJS)
	$(LD) $(OBJS) -o $@ $(LDFLAGS)

eboot.bin: $(INTDIR)/$(PROJ).elf
	sh ./tools/create-eboot-golden $(INTDIR)/$(PROJ).elf $@

# Recreates sce_module/ (and right.sprx) when missing, e.g. on a fresh checkout.
sce_module:
	sh ./tools/copy-sce-modules.sh

sce_sys/about/right.sprx:
	sh ./tools/copy-right-sprx.sh

sce_sys/param.sfo:
	./tools/make_sfo.sh "$(TITLE_ID)" "$(CONTENT_ID)" "$(TITLE)" "$(PKGTOOL)"

pkg: eboot.bin sce_module sce_sys/about/right.sprx sce_sys/param.sfo
	@test -s sce_module/libc.prx && test -s sce_module/libSceFios2.prx || { echo "ERROR: sce_module is missing libc.prx or libSceFios2.prx"; exit 1; }
	@FILES="eboot.bin sce_sys/param.sfo sce_sys/icon0.png sce_sys/pic1.png sce_sys/about/right.sprx"; \
	EXTRA="$$(find sce_module assets -type f 2>/dev/null | sort | tr '\n' ' ')"; \
	FILES="$$FILES $$EXTRA"; \
	echo "PKG files: $$FILES"; \
	python3 ./tools/create-gp4 -out pkg.gp4 --content-id=$(CONTENT_ID) --files "$$FILES"
	$(PKGTOOL) pkg_build pkg.gp4 .
	@ls -la *.pkg

clean:
	rm -rf $(INTDIR) eboot.bin pkg.gp4 *.pkg sce_sys/param.sfo sce_sys/about sce_module test/test_run test/out
