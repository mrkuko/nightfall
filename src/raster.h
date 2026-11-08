/* raster.h — colour raster with a depth buffer (z-buffer)
 *
 * One terminal cell = 2 pixels stacked (the ▀ character with fg/bg colour),
 * so the raster has dimensions cols × (2 * rows).
 */
#ifndef NF_RASTER_H
#define NF_RASTER_H

#include "common.h"
#include "sprite.h"

typedef struct {
    int      w, h;
    rgb_t   *c;
    int16_t *z;
    /* depth fog: pixels with z <= fog_far are fully fogged (fog_max),
     * pixels with z >= fog_near have no fog */
    rgb_t    fog;
    int      fog_near, fog_far;
    float    fog_max;
} raster_t;

enum {
    BLIT_FLIPX   = 1,   /* mirroring (creature facing left) */
    BLIT_NOFOG   = 2,
    BLIT_NOZ     = 4,   /* draw without z-test and without z write (UI, debugging) */
    BLIT_SILHOUETTE = 8 /* shape only, in a single colour (shadow, debugging) */
};

void raster_init(raster_t *r, int w, int h);
void raster_free(raster_t *r);
void raster_resize(raster_t *r, int w, int h);
void raster_clear(raster_t *r, rgb_t bg);
void raster_copy_rect(raster_t *dst, const raster_t *src, int x, int y, int w, int h);

/* Draws a frame so that its anchor is at (x, y), with base depth z.
 * scale >= 1 enlarges the pixels (preview). tint != NULL -> silhouette colour. */
void raster_blit(raster_t *r, const frame_t *f, int x, int y, int z, int flags,
                 int scale, const rgb_t *tint);

void raster_fill_rect(raster_t *r, int x, int y, int w, int h, rgb_t c, int z);
void raster_plot(raster_t *r, int x, int y, rgb_t c, int z);

/* Recolours the raster by depth (debug view): near = warm, far = cold. */
void raster_depth_view(raster_t *r, int zmin, int zmax);

/* Saves the raster as PPM (P6), every pixel enlarged scale times. */
int  raster_save_ppm(const raster_t *r, const char *path, int scale);

#endif
