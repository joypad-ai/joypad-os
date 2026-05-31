# sinput-verify — SDL3 smoke test for the Joypad OS SInput firmware.
#
# Builds completely independently of the firmware (no pico-sdk, no CMake).
# Requires SDL3 (>= 3.2 recommended for the HIDAPI SInput driver).
#
#   macOS:  brew install sdl3
#   linux:  build SDL3 from source or use a recent distro package
#
# Usage:
#   make           — build ./sinput-verify
#   make run       — build and run
#   make clean

BIN      := sinput-verify
SRC      := main.c

# Prefer pkg-config; on macOS fall back to /opt/homebrew if it's not on
# PKG_CONFIG_PATH yet (common right after `brew install sdl3`).
UNAME_S  := $(shell uname -s)
ifeq ($(UNAME_S),Darwin)
  HOMEBREW_PREFIX ?= $(shell brew --prefix 2>/dev/null || echo /opt/homebrew)
  export PKG_CONFIG_PATH := $(HOMEBREW_PREFIX)/lib/pkgconfig:$(PKG_CONFIG_PATH)
endif

PKG_OK   := $(shell pkg-config --exists sdl3 && echo yes)

ifeq ($(PKG_OK),yes)
  SDL_CFLAGS := $(shell pkg-config --cflags sdl3)
  SDL_LIBS   := $(shell pkg-config --libs   sdl3)
else
  # Last-ditch hardcoded fallback for a stock homebrew install on Apple Silicon.
  ifeq ($(UNAME_S),Darwin)
    HOMEBREW_PREFIX ?= /opt/homebrew
    SDL_CFLAGS := -I$(HOMEBREW_PREFIX)/include
    SDL_LIBS   := -L$(HOMEBREW_PREFIX)/lib -lSDL3
  else
    $(error SDL3 not found via pkg-config — install SDL3 dev package or set PKG_CONFIG_PATH)
  endif
endif

CC       ?= cc
CSTD     := -std=c11
WARN     := -Wall -Wextra -Wno-unused-parameter
OPT      := -O2 -g
CFLAGS   += $(CSTD) $(WARN) $(OPT) $(SDL_CFLAGS)
LDFLAGS  += $(SDL_LIBS)

.PHONY: all run clean
all: $(BIN)

$(BIN): $(SRC)
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

run: $(BIN)
	./$(BIN)

clean:
	rm -f $(BIN)
