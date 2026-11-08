/* sheet.c — library export to an image (PPM): all shapes and their frames in a grid.
 * For quick visual checks without a terminal. */
#include "sheet.h"
#include "raster.h"

#include <stdio.h>
#include <string.h>

int sheet_export(const library_t *lib, const char *path, const char *filter, int depth, int scale,
                 int maxw)
{
    const int pad = 3;
    if (maxw <= 0) maxw = 360;
    /* 1. layout */
    int x = pad, y = pad, rowh = 0, total_h = pad;
    typedef struct { int si, fi, x, y; } place_t;
    static place_t pl[8192];
    int np = 0;
    for (int i = 0; i < lib->n; i++) {
        const sprite_t *s = lib->items[i];
        if (filter && !strstr(s->name, filter) && !sprite_has_tag(s, filter)) continue;
        /* new shape = new row */
        if (x > pad) { x = pad; y += rowh + pad * 2; rowh = 0; }
        int shown = 0;
        for (int f = 0; f < s->nframes && np < 8192; f++) {
            const frame_t *fr = &s->frames[f];
            /* for plants with 9 bend frames every second one is enough */
            if (s->nframes == 9 && (f % 2)) continue;
            if (shown >= 24) break;
            if (x + fr->w > maxw && x > pad) { x = pad; y += rowh + pad; rowh = 0; }
            pl[np++] = (place_t){ i, f, x, y };
            x += fr->w + pad;
            if (fr->h > rowh) rowh = fr->h;
            shown++;
        }
        total_h = y + rowh + pad;
    }
    raster_t r;
    raster_init(&r, maxw, total_h + pad);
    raster_clear(&r, mkrgb(6, 10, 11));
    for (int k = 0; k < np; k++) {
        const frame_t *fr = &lib->items[pl[k].si]->frames[pl[k].fi];
        /* a thin frame for orientation */
        rgb_t bc = mkrgb(16, 22, 24);
        for (int xx = -1; xx <= fr->w; xx++) {
            raster_plot(&r, pl[k].x + xx, pl[k].y - 1, bc, -999);
            raster_plot(&r, pl[k].x + xx, pl[k].y + fr->h, bc, -999);
        }
        raster_blit(&r, fr, pl[k].x + fr->ax, pl[k].y + fr->ay, 100, BLIT_NOFOG, 1, NULL);
    }
    if (depth) raster_depth_view(&r, 90, 110);
    int rc = raster_save_ppm(&r, path, scale);
    raster_free(&r);
    /* legend on stdout */
    int last = -1;
    for (int k = 0; k < np; k++)
        if (pl[k].si != last) {
            last = pl[k].si;
            const sprite_t *s = lib->items[last];
            printf("y=%4d  %-16s frames=%d clips=%d  [%s]\n", pl[k].y, s->name, s->nframes, s->nclips, s->tags);
        }
    return rc;
}
