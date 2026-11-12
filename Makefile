# NIGHTFALL — C99, no dependencies (libc + libm)
#
#   make                      build ./nightfall
#   make install              install to $(PREFIX) (default /usr/local); DESTDIR for packaging
#   make uninstall
#   make CC=clang             any C99 compiler; on Windows use MSYS2 / MinGW-w64
VERSION ?= 1.0.0
CC      ?= cc
CFLAGS  ?= -std=c99 -O2 -Wall -Wextra -Wno-unused-parameter
CPPFLAGS += -DNF_VERSION=\"$(VERSION)\"
LDLIBS  = -lm

PREFIX  ?= /usr/local
BINDIR  ?= $(PREFIX)/bin
MANDIR  ?= $(PREFIX)/share/man
DOCDIR  ?= $(PREFIX)/share/doc/nightfall

ifeq ($(OS),Windows_NT)
EXE     = .exe
SCR     = nightfall.scr
endif
BIN     = nightfall$(EXE)

SRC     = $(wildcard src/*.c)
OBJ     = $(patsubst src/%.c,build/%.o,$(SRC)) build/assets_embed.o
ASSETS  = $(sort $(wildcard assets/*.spr assets/*/*.spr))

all: $(BIN) $(SCR)

$(BIN): $(OBJ)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(OBJ) $(LDLIBS)

build/%.o: src/%.c $(wildcard src/*.h) | build
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

build/embed$(EXE): tools/embed.c | build
	$(CC) -std=c99 -O2 -Wall -o $@ $<

build/assets_embed.c: build/embed$(EXE) $(ASSETS)
	build/embed$(EXE) $@ $(ASSETS)

build/assets_embed.o: build/assets_embed.c
	$(CC) $(CFLAGS) -c -o $@ $<

# Windows screensaver (win/scr.c #includes src/gallery.c, so it links every object except gallery and main)
SCR_OBJ = $(filter-out build/gallery.o build/main.o,$(OBJ))
# build/nightfall-saver.exe is the same file: run it from a shell with arguments (a .scr is started by
# the shell association, which drops them): build/nightfall-saver.exe /s --height 400
nightfall.scr: build/nightfall-saver.exe
	cp $< $@

build/nightfall-saver.exe: win/scr.c $(SCR_OBJ) $(wildcard src/*.h src/gallery.c) | build
	$(CC) $(CPPFLAGS) $(CFLAGS) -mwindows -o $@ win/scr.c $(SCR_OBJ) $(LDLIBS) -lgdi32 -luser32 -ladvapi32

build:
	mkdir -p build

install: $(BIN)
	install -d $(DESTDIR)$(BINDIR) $(DESTDIR)$(MANDIR)/man6 $(DESTDIR)$(DOCDIR)
	install -m 755 $(BIN) $(DESTDIR)$(BINDIR)/$(BIN)
	install -m 644 docs/nightfall.6 $(DESTDIR)$(MANDIR)/man6/nightfall.6
	install -m 644 README.md LICENSE assets/FORMAT.md $(DESTDIR)$(DOCDIR)/

uninstall:
	rm -f $(DESTDIR)$(BINDIR)/$(BIN) $(DESTDIR)$(MANDIR)/man6/nightfall.6
	rm -rf $(DESTDIR)$(DOCDIR)

clean:
	rm -rf build $(BIN) $(SCR)

.PHONY: all install uninstall clean
