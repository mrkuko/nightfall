/* term.c — POSIX implementation of the terminal layer (Linux, macOS, BSD; Windows: term_win.c) */
#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#if defined(__APPLE__)
#define _DARWIN_C_SOURCE        /* SIGWINCH, TIOCGWINSZ */
#elif defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__NetBSD__)
#define __BSD_VISIBLE 1
#endif
#include "term.h"

#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>
#include <errno.h>
#include <sys/ioctl.h>

static struct termios g_orig;
static int g_active;
static volatile sig_atomic_t g_resize_flag, g_quit_flag;

static void on_signal(int sig)
{
    if (sig == SIGWINCH) g_resize_flag = 1;
    else g_quit_flag = 1;
}

void term_write(const char *buf, size_t n)
{
    while (n > 0) {
        ssize_t w = write(STDOUT_FILENO, buf, n);
        if (w < 0) {
            if (errno == EINTR) continue;
            return;
        }
        buf += w;
        n -= (size_t)w;
    }
}

static void term_puts(const char *s) { term_write(s, strlen(s)); }

int term_init(void)
{
    if (!isatty(STDIN_FILENO) || !isatty(STDOUT_FILENO)) return -1;
    if (tcgetattr(STDIN_FILENO, &g_orig) != 0) return -1;
    struct termios raw = g_orig;
    raw.c_lflag &= ~(tcflag_t)(ECHO | ICANON);
    raw.c_iflag &= ~(tcflag_t)(IXON | ICRNL);
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 0;
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) != 0) return -1;
    g_active = 1;

    struct sigaction sa;
    memset(&sa, 0, sizeof sa);
    sa.sa_handler = on_signal;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGHUP, &sa, NULL);
    sigaction(SIGWINCH, &sa, NULL);

    /* alternate screen, hidden cursor, no line wrapping */
    term_puts("\x1b[?1049h\x1b[?25l\x1b[?7l\x1b[2J");
    atexit(term_restore);
    return 0;
}

void term_restore(void)
{
    if (!g_active) return;
    g_active = 0;
    term_puts("\x1b[0m\x1b[?7h\x1b[?25h\x1b[?1049l");
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &g_orig);
}

void term_size(int *cols, int *rows)
{
    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 0 && ws.ws_row > 0) {
        *cols = ws.ws_col;
        *rows = ws.ws_row;
    } else {
        const char *c = getenv("COLUMNS"), *l = getenv("LINES");
        *cols = c ? atoi(c) : 80;
        *rows = l ? atoi(l) : 24;
        if (*cols <= 0) *cols = 80;
        if (*rows <= 0) *rows = 24;
    }
}

int term_key(void)
{
    unsigned char b[16];
    ssize_t n = read(STDIN_FILENO, b, sizeof b);
    if (n <= 0) return KEY_NONE;
    if (b[0] != 0x1b) return b[0];
    if (n == 1) return KEY_ESC;
    if (n >= 3 && (b[1] == '[' || b[1] == 'O')) {
        switch (b[2]) {
        case 'A': return KEY_UP;
        case 'B': return KEY_DOWN;
        case 'C': return KEY_RIGHT;
        case 'D': return KEY_LEFT;
        case 'H': return KEY_HOME;
        case 'F': return KEY_END;
        case '5': return KEY_PGUP;
        case '6': return KEY_PGDN;
        default: break;
        }
    }
    return KEY_ESC;
}

int term_resized(void)
{
    if (!g_resize_flag) return 0;
    g_resize_flag = 0;
    return 1;
}

int term_quit_requested(void) { return g_quit_flag != 0; }

double term_now(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}

void term_sleep(double sec)
{
    if (sec <= 0) return;
    struct timespec ts;
    ts.tv_sec = (time_t)sec;
    ts.tv_nsec = (long)((sec - (double)ts.tv_sec) * 1e9);
    while (nanosleep(&ts, &ts) != 0 && errno == EINTR && !g_quit_flag) {}
}

#else
typedef int nf_term_posix_unused;   /* ISO C: a translation unit must not be empty (Windows uses term_win.c) */
#endif /* !_WIN32 */
