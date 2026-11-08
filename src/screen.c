/* screen.c — diff renderer to ANSI TrueColor */
#include "screen.h"
#include "term.h"

#include <stdlib.h>
#include <string.h>

#define CP_UPPER_HALF 0x2580u   /* ▀ */

void screen_init(screen_t *s, int w, int h)
{
    memset(s, 0, sizeof *s);
    screen_resize(s, w, h);
}

void screen_free(screen_t *s)
{
    free(s->cur);
    free(s->prev);
    free(s->out);
    memset(s, 0, sizeof *s);
}

void screen_resize(screen_t *s, int w, int h)
{
    free(s->cur);
    free(s->prev);
    s->w = w > 0 ? w : 1;
    s->h = h > 0 ? h : 1;
    s->cur = calloc((size_t)s->w * (size_t)s->h, sizeof *s->cur);
    s->prev = calloc((size_t)s->w * (size_t)s->h, sizeof *s->prev);
    if (!s->cur || !s->prev) exit(1);
    for (int i = 0; i < s->w * s->h; i++) s->cur[i].cp = ' ';
    s->force = 1;
}

void screen_from_raster(screen_t *s, const raster_t *r, int y0)
{
    int rows = r->h / 2;
    for (int y = 0; y < rows && y + y0 < s->h; y++) {
        const rgb_t *top = &r->c[(2 * y) * r->w];
        const rgb_t *bot = &r->c[(2 * y + 1) * r->w];
        cell_t *row = &s->cur[(y + y0) * s->w];
        int n = NF_MIN(r->w, s->w);
        for (int x = 0; x < n; x++) {
            cell_t *c = &row[x];
            if (rgb_eq(top[x], bot[x])) {
                c->cp = ' ';
                c->fg = c->bg = bot[x];
            } else {
                c->cp = CP_UPPER_HALF;
                c->fg = top[x];
                c->bg = bot[x];
            }
        }
    }
}

void screen_fill(screen_t *s, int x, int y, int w, int h, rgb_t bg)
{
    for (int yy = NF_MAX(y, 0); yy < y + h && yy < s->h; yy++)
        for (int xx = NF_MAX(x, 0); xx < x + w && xx < s->w; xx++) {
            cell_t *c = &s->cur[yy * s->w + xx];
            c->cp = ' ';
            c->fg = c->bg = bg;
        }
}

static uint32_t utf8_next(const char **ps)
{
    const unsigned char *p = (const unsigned char *)*ps;
    uint32_t cp;
    int n;
    if (p[0] < 0x80) { cp = p[0]; n = 1; }
    else if ((p[0] & 0xE0) == 0xC0 && p[1]) { cp = ((p[0] & 0x1Fu) << 6) | (p[1] & 0x3Fu); n = 2; }
    else if ((p[0] & 0xF0) == 0xE0 && p[1] && p[2]) {
        cp = ((p[0] & 0x0Fu) << 12) | ((p[1] & 0x3Fu) << 6) | (p[2] & 0x3Fu); n = 3;
    } else if ((p[0] & 0xF8) == 0xF0 && p[1] && p[2] && p[3]) {
        cp = ((p[0] & 0x07u) << 18) | ((p[1] & 0x3Fu) << 12) | ((p[2] & 0x3Fu) << 6) | (p[3] & 0x3Fu); n = 4;
    } else { cp = '?'; n = 1; }
    *ps += n;
    return cp;
}

int screen_text(screen_t *s, int x, int y, const char *utf8, rgb_t fg, const rgb_t *bg)
{
    int start = x;
    if (y < 0 || y >= s->h) return 0;
    while (*utf8) {
        uint32_t cp = utf8_next(&utf8);
        if (x >= 0 && x < s->w) {
            cell_t *c = &s->cur[y * s->w + x];
            if (!bg && c->cp == CP_UPPER_HALF) {
                /* text over a half block: background = average of both pixels */
                c->bg = rgb_mix(c->fg, c->bg, 0.5f);
            }
            c->cp = cp;
            c->fg = fg;
            if (bg) c->bg = *bg;
        }
        x++;
    }
    return x - start;
}

/* --- output buffer --- */

static void out_reserve(screen_t *s, size_t extra)
{
    if (s->outn + extra <= s->outcap) return;
    size_t cap = s->outcap ? s->outcap : 65536;
    while (cap < s->outn + extra) cap *= 2;
    s->out = realloc(s->out, cap);
    if (!s->out) exit(1);
    s->outcap = cap;
}

static inline void out_ch(screen_t *s, char c) { s->out[s->outn++] = c; }

static inline void out_str(screen_t *s, const char *str)
{
    while (*str) s->out[s->outn++] = *str++;
}

static inline void out_uint(screen_t *s, unsigned v)
{
    char tmp[12];
    int n = 0;
    do { tmp[n++] = (char)('0' + v % 10); v /= 10; } while (v);
    while (n) s->out[s->outn++] = tmp[--n];
}

static inline void out_utf8(screen_t *s, uint32_t cp)
{
    if (cp < 0x80) out_ch(s, (char)cp);
    else if (cp < 0x800) {
        out_ch(s, (char)(0xC0 | (cp >> 6)));
        out_ch(s, (char)(0x80 | (cp & 0x3F)));
    } else if (cp < 0x10000) {
        out_ch(s, (char)(0xE0 | (cp >> 12)));
        out_ch(s, (char)(0x80 | ((cp >> 6) & 0x3F)));
        out_ch(s, (char)(0x80 | (cp & 0x3F)));
    } else {
        out_ch(s, (char)(0xF0 | (cp >> 18)));
        out_ch(s, (char)(0x80 | ((cp >> 12) & 0x3F)));
        out_ch(s, (char)(0x80 | ((cp >> 6) & 0x3F)));
        out_ch(s, (char)(0x80 | (cp & 0x3F)));
    }
}

static inline int cell_eq(const cell_t *a, const cell_t *b)
{
    if (a->cp != b->cp || !rgb_eq(a->bg, b->bg)) return 0;
    return a->cp == ' ' || rgb_eq(a->fg, b->fg);
}

void screen_flush(screen_t *s)
{
    s->outn = 0;
    out_reserve(s, 64);
    out_str(s, "\x1b[?2026h");   /* synchronized output: begin */

    int cx = -1, cy = -1;        /* known cursor position */
    int have_fg = 0, have_bg = 0;
    rgb_t fg = {0, 0, 0}, bg = {0, 0, 0};
    int changed = 0;

    for (int y = 0; y < s->h; y++) {
        for (int x = 0; x < s->w; x++) {
            int i = y * s->w + x;
            cell_t *c = &s->cur[i];
            if (!s->force && cell_eq(c, &s->prev[i])) continue;
            changed++;
            out_reserve(s, 64);
            if (cx != x || cy != y) {
                out_str(s, "\x1b[");
                out_uint(s, (unsigned)y + 1);
                out_ch(s, ';');
                out_uint(s, (unsigned)x + 1);
                out_ch(s, 'H');
            }
            if (!have_bg || !rgb_eq(bg, c->bg)) {
                out_str(s, "\x1b[48;2;");
                out_uint(s, c->bg.r); out_ch(s, ';');
                out_uint(s, c->bg.g); out_ch(s, ';');
                out_uint(s, c->bg.b); out_ch(s, 'm');
                bg = c->bg;
                have_bg = 1;
            }
            if (c->cp != ' ' && (!have_fg || !rgb_eq(fg, c->fg))) {
                out_str(s, "\x1b[38;2;");
                out_uint(s, c->fg.r); out_ch(s, ';');
                out_uint(s, c->fg.g); out_ch(s, ';');
                out_uint(s, c->fg.b); out_ch(s, 'm');
                fg = c->fg;
                have_fg = 1;
            }
            out_utf8(s, c->cp);
            cx = x + 1;
            cy = y;
            s->prev[i] = *c;
        }
    }
    out_reserve(s, 32);
    out_str(s, "\x1b[?2026l");   /* synchronized output: end */
    s->force = 0;
    s->last_cells = changed;
    s->last_bytes = s->outn;
    if (changed) term_write(s->out, s->outn);
}
