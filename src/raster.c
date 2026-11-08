/* raster.c — z-buffer compositing */
#include "raster.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void raster_init(raster_t *r, int w, int h)
{
    memset(r, 0, sizeof *r);
    r->fog = mkrgb(6, 10, 12);
    r->fog_near = 200;
    r->fog_far = 0;
    r->fog_max = 0.0f;
    raster_resize(r, w, h);
}

void raster_free(raster_t *r)
{
    free(r->c);
    free(r->z);
    r->c = NULL;
    r->z = NULL;
    r->w = r->h = 0;
}

void raster_resize(raster_t *r, int w, int h)
{
    free(r->c);
    free(r->z);
    r->w = w > 0 ? w : 1;
    r->h = h > 0 ? h : 1;
    r->c = calloc((size_t)r->w * (size_t)r->h, sizeof *r->c);
    r->z = calloc((size_t)r->w * (size_t)r->h, sizeof *r->z);
    if (!r->c || !r->z) exit(1);
}

void raster_clear(raster_t *r, rgb_t bg)
{
    size_t n = (size_t)r->w * (size_t)r->h;
    for (size_t i = 0; i < n; i++) {
        r->c[i] = bg;
        r->z[i] = Z_EMPTY;
    }
}

void raster_copy_rect(raster_t *dst, const raster_t *src, int x, int y, int w, int h)
{
    int x0 = NF_MAX(x, 0), y0 = NF_MAX(y, 0);
    int x1 = NF_MIN(x + w, NF_MIN(dst->w, src->w));
    int y1 = NF_MIN(y + h, NF_MIN(dst->h, src->h));
    if (x1 <= x0) return;
    for (int yy = y0; yy < y1; yy++) {
        memcpy(&dst->c[yy * dst->w + x0], &src->c[yy * src->w + x0], (size_t)(x1 - x0) * sizeof *dst->c);
        memcpy(&dst->z[yy * dst->w + x0], &src->z[yy * src->w + x0], (size_t)(x1 - x0) * sizeof *dst->z);
    }
}

static inline rgb_t fogged(const raster_t *r, rgb_t c, int z)
{
    if (r->fog_max <= 0.0f || z >= r->fog_near) return c;
    float t = z <= r->fog_far ? 1.0f : (float)(r->fog_near - z) / (float)(r->fog_near - r->fog_far);
    return rgb_mix(c, r->fog, t * r->fog_max);
}

void raster_blit(raster_t *r, const frame_t *f, int x, int y, int z, int flags,
                 int scale, const rgb_t *tint)
{
    if (scale < 1) scale = 1;
    int flip = flags & BLIT_FLIPX;
    for (int fy = 0; fy < f->h; fy++) {
        for (int fx = 0; fx < f->w; fx++) {
            const px_t *p = &f->px[fy * f->w + fx];
            if (!p->a) continue;
            int dx = fx - f->ax;
            if (flip) dx = -dx;
            int dy = fy - f->ay;
            int zz = z + p->z;
            rgb_t c = tint ? *tint : mkrgb(p->r, p->g, p->b);
            if (!(flags & BLIT_NOFOG) && !(p->f & PX_GLOW)) c = fogged(r, c, zz);
            for (int sy = 0; sy < scale; sy++) {
                int py = y + dy * scale + sy;
                if (py < 0 || py >= r->h) continue;
                for (int sx = 0; sx < scale; sx++) {
                    int pxx = x + dx * scale + sx;
                    if (pxx < 0 || pxx >= r->w) continue;
                    if (r->mask_on && py < r->mask_floor) {
                        float mx = ((float)pxx + 0.5f - r->mask_cx) / r->mask_rx;
                        float my = ((float)py + 0.5f - r->mask_cy) / r->mask_ry;
                        if (mx * mx + my * my > 1.0f) continue;
                    }
                    int i = py * r->w + pxx;
                    if (flags & BLIT_NOZ) { r->c[i] = c; continue; }
                    if (zz < r->z[i]) continue;
                    if (p->a < 255) {
                        /* translucent (glow, steam): blend, do not write z */
                        r->c[i] = rgb_mix(r->c[i], c, (float)p->a / 255.0f);
                    } else {
                        r->c[i] = c;
                        r->z[i] = (int16_t)zz;
                    }
                }
            }
        }
    }
}

void raster_plot(raster_t *r, int x, int y, rgb_t c, int z)
{
    if (x < 0 || y < 0 || x >= r->w || y >= r->h) return;
    int i = y * r->w + x;
    if (z < r->z[i]) return;
    r->c[i] = fogged(r, c, z);
    r->z[i] = (int16_t)z;
}

void raster_fill_rect(raster_t *r, int x, int y, int w, int h, rgb_t c, int z)
{
    for (int yy = y; yy < y + h; yy++)
        for (int xx = x; xx < x + w; xx++) raster_plot(r, xx, yy, c, z);
}

void raster_depth_view(raster_t *r, int zmin, int zmax)
{
    size_t n = (size_t)r->w * (size_t)r->h;
    rgb_t far_c = mkrgb(20, 40, 120), mid_c = mkrgb(40, 170, 120), near_c = mkrgb(255, 210, 90);
    for (size_t i = 0; i < n; i++) {
        if (r->z[i] == Z_EMPTY) { r->c[i] = mkrgb(8, 8, 10); continue; }
        float t = (float)(r->z[i] - zmin) / (float)(zmax - zmin > 0 ? zmax - zmin : 1);
        t = clampf(t, 0.0f, 1.0f);
        r->c[i] = t < 0.5f ? rgb_mix(far_c, mid_c, t * 2.0f) : rgb_mix(mid_c, near_c, (t - 0.5f) * 2.0f);
    }
}

int raster_save_ppm(const raster_t *r, const char *path, int scale)
{
    FILE *fp = fopen(path, "wb");
    if (!fp) return -1;
    if (scale < 1) scale = 1;
    fprintf(fp, "P6\n%d %d\n255\n", r->w * scale, r->h * scale);
    for (int y = 0; y < r->h; y++)
        for (int sy = 0; sy < scale; sy++)
            for (int x = 0; x < r->w; x++) {
                rgb_t c = r->c[y * r->w + x];
                for (int sx = 0; sx < scale; sx++) {
                    fputc(c.r, fp); fputc(c.g, fp); fputc(c.b, fp);
                }
            }
    fclose(fp);
    return 0;
}
