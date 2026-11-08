/* screen.h — grid of terminal cells + diff renderer
 *
 * Raster (2 pixels per cell) -> cells (▀ with fg/bg) -> comparison with the previous
 * frame -> ANSI TrueColor only for changed cells -> a single write().
 */
#ifndef NF_SCREEN_H
#define NF_SCREEN_H

#include "common.h"
#include "raster.h"

typedef struct {
    uint32_t cp;    /* Unicode codepoint */
    rgb_t fg, bg;
} cell_t;

typedef struct {
    int     w, h;
    cell_t *cur, *prev;
    int     force;          /* the next flush redraws everything */
    char   *out;
    size_t  outn, outcap;
    size_t  last_bytes;     /* statistics: bytes sent by the previous flush */
    int     last_cells;     /* statistics: changed cells */
} screen_t;

void screen_init(screen_t *s, int w, int h);
void screen_free(screen_t *s);
void screen_resize(screen_t *s, int w, int h);
/* Converts the raster to cells; cell row y = pixels 2y and 2y+1. Starts at cell (0, y0). */
void screen_from_raster(screen_t *s, const raster_t *r, int y0);
void screen_fill(screen_t *s, int x, int y, int w, int h, rgb_t bg);
/* UTF-8 text; if bg == NULL, keeps the cell background. Returns the width. */
int  screen_text(screen_t *s, int x, int y, const char *utf8, rgb_t fg, const rgb_t *bg);
void screen_flush(screen_t *s);

#endif
