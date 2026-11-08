# NIGHTFALL — C99, no dependencies (libc + libm)
CC      ?= cc
CFLAGS  ?= -std=c99 -O2 -Wall -Wextra -Wno-unused-parameter
LDLIBS  = -lm

SRC     = $(wildcard src/*.c)
OBJ     = $(patsubst src/%.c,build/%.o,$(SRC)) build/assets_embed.o
ASSETS  = $(sort $(wildcard assets/*.spr assets/*/*.spr))
BIN     = nightfall

all: $(BIN)

$(BIN): $(OBJ)
	$(CC) $(CFLAGS) -o $@ $(OBJ) $(LDLIBS)

build/%.o: src/%.c $(wildcard src/*.h) | build
	$(CC) $(CFLAGS) -c -o $@ $<

build/embed: tools/embed.c | build
	$(CC) -std=c99 -O2 -Wall -o $@ $<

build/assets_embed.c: build/embed $(ASSETS)
	build/embed $@ $(ASSETS)

build/assets_embed.o: build/assets_embed.c
	$(CC) $(CFLAGS) -c -o $@ $<

build:
	mkdir -p build

clean:
	rm -rf build $(BIN)

.PHONY: all clean
