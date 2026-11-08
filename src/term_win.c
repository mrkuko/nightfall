/* term_win.c — Windows console implementation of the terminal layer.
 *
 * Needs a console with virtual terminal sequences (Windows 10 1809+ / Windows Terminal):
 * the renderer emits the same ANSI/true-colour output as on POSIX. */
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdlib.h>
#include <string.h>
#include "term.h"

#ifndef ENABLE_VIRTUAL_TERMINAL_PROCESSING
#define ENABLE_VIRTUAL_TERMINAL_PROCESSING 0x0004
#endif
#ifndef DISABLE_NEWLINE_AUTO_RETURN
#define DISABLE_NEWLINE_AUTO_RETURN 0x0008
#endif

static HANDLE g_in, g_out;
static DWORD g_in_mode, g_out_mode;
static UINT g_out_cp;
static int g_active, g_cols, g_rows;
static volatile LONG g_quit_flag, g_resize_flag;

static BOOL WINAPI on_ctrl(DWORD type)
{
    (void)type;
    InterlockedExchange(&g_quit_flag, 1);
    return TRUE;
}

void term_write(const char *buf, size_t n)
{
    while (n > 0) {
        DWORD w = 0;
        if (!WriteFile(g_out, buf, (DWORD)(n > 0x10000 ? 0x10000 : n), &w, NULL) || w == 0) return;
        buf += w;
        n -= w;
    }
}

static void term_puts(const char *s) { term_write(s, strlen(s)); }

static void query_size(int *cols, int *rows)
{
    CONSOLE_SCREEN_BUFFER_INFO bi;
    if (GetConsoleScreenBufferInfo(g_out, &bi)) {
        *cols = bi.srWindow.Right - bi.srWindow.Left + 1;
        *rows = bi.srWindow.Bottom - bi.srWindow.Top + 1;
    } else {
        *cols = 80;
        *rows = 24;
    }
}

int term_init(void)
{
    g_in = GetStdHandle(STD_INPUT_HANDLE);
    g_out = GetStdHandle(STD_OUTPUT_HANDLE);
    if (g_in == INVALID_HANDLE_VALUE || g_out == INVALID_HANDLE_VALUE) return -1;
    if (!GetConsoleMode(g_in, &g_in_mode) || !GetConsoleMode(g_out, &g_out_mode)) return -1;
    if (!SetConsoleMode(g_out, g_out_mode | ENABLE_PROCESSED_OUTPUT | ENABLE_VIRTUAL_TERMINAL_PROCESSING |
                                   DISABLE_NEWLINE_AUTO_RETURN))
        return -1;                                  /* an old console without VT support */
    SetConsoleMode(g_in, (g_in_mode & ~(DWORD)(ENABLE_LINE_INPUT | ENABLE_ECHO_INPUT | ENABLE_PROCESSED_INPUT |
                                               ENABLE_QUICK_EDIT_MODE)) | ENABLE_WINDOW_INPUT | ENABLE_EXTENDED_FLAGS);
    g_out_cp = GetConsoleOutputCP();
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCtrlHandler(on_ctrl, TRUE);
    g_active = 1;
    query_size(&g_cols, &g_rows);
    term_puts("\x1b[?1049h\x1b[?25l\x1b[?7l\x1b[2J");
    atexit(term_restore);
    return 0;
}

void term_restore(void)
{
    if (!g_active) return;
    g_active = 0;
    term_puts("\x1b[0m\x1b[?7h\x1b[?25h\x1b[?1049l");
    SetConsoleMode(g_in, g_in_mode);
    SetConsoleMode(g_out, g_out_mode);
    SetConsoleOutputCP(g_out_cp);
}

void term_size(int *cols, int *rows)
{
    query_size(cols, rows);
    g_cols = *cols;
    g_rows = *rows;
}

int term_key(void)
{
    for (;;) {
        DWORD n = 0;
        if (!GetNumberOfConsoleInputEvents(g_in, &n) || n == 0) return KEY_NONE;
        INPUT_RECORD ev;
        DWORD got = 0;
        if (!ReadConsoleInputW(g_in, &ev, 1, &got) || got == 0) return KEY_NONE;
        if (ev.EventType == WINDOW_BUFFER_SIZE_EVENT) { InterlockedExchange(&g_resize_flag, 1); continue; }
        if (ev.EventType != KEY_EVENT || !ev.Event.KeyEvent.bKeyDown) continue;
        switch (ev.Event.KeyEvent.wVirtualKeyCode) {
        case VK_UP: return KEY_UP;
        case VK_DOWN: return KEY_DOWN;
        case VK_LEFT: return KEY_LEFT;
        case VK_RIGHT: return KEY_RIGHT;
        case VK_PRIOR: return KEY_PGUP;
        case VK_NEXT: return KEY_PGDN;
        case VK_HOME: return KEY_HOME;
        case VK_END: return KEY_END;
        case VK_ESCAPE: return KEY_ESC;
        default: break;
        }
        WCHAR ch = ev.Event.KeyEvent.uChar.UnicodeChar;
        if (ch == 3) { InterlockedExchange(&g_quit_flag, 1); return KEY_NONE; }     /* Ctrl+C */
        if (ch > 0 && ch < 128) return (int)ch;
    }
}

int term_resized(void)
{
    /* resize events are not always delivered (e.g. Windows Terminal) -> also compare the size */
    int c, r;
    query_size(&c, &r);
    if (c != g_cols || r != g_rows) InterlockedExchange(&g_resize_flag, 1);
    return InterlockedExchange(&g_resize_flag, 0) != 0;
}

int term_quit_requested(void) { return g_quit_flag != 0; }

double term_now(void)
{
    static LARGE_INTEGER freq;
    LARGE_INTEGER t;
    if (!freq.QuadPart) QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&t);
    return (double)t.QuadPart / (double)freq.QuadPart;
}

void term_sleep(double sec)
{
    if (sec <= 0) return;
    Sleep((DWORD)(sec * 1000.0 + 0.5));
}

#else
typedef int nf_term_win_unused;   /* ISO C: a translation unit must not be empty */
#endif /* _WIN32 */
