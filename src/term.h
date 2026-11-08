/* term.h — POSIX terminal: raw mode, size, keys, output */
#ifndef NF_TERM_H
#define NF_TERM_H

#include <stddef.h>

enum {
    KEY_NONE = -1,
    KEY_UP = 1000, KEY_DOWN, KEY_LEFT, KEY_RIGHT,
    KEY_PGUP, KEY_PGDN, KEY_HOME, KEY_END, KEY_ESC
};

int  term_init(void);                 /* 0 = OK */
void term_restore(void);
void term_size(int *cols, int *rows);
int  term_key(void);                  /* non-blocking; KEY_NONE if nothing */
int  term_resized(void);              /* 1 if SIGWINCH arrived (and clears the flag) */
int  term_quit_requested(void);       /* SIGINT/SIGTERM */
void term_write(const char *buf, size_t n);
double term_now(void);                /* monotonic time in seconds */
void term_sleep(double sec);

#endif
