/* gen.c — procedural generators: grass, ferns, bushes, trunks, roots, branches,
 * vines, mushrooms, stones, fallen logs, big leaves, forest litter, fireflies, spider webs.
 *
 * Conventions:
 *   - light (the moon) comes from the upper right -> lighter top/right edges,
 *   - per-pixel z offset: + = closer to the camera, - = deeper (inside a bush, a hollow),
 *   - param clip "sway": frames for a bend of -1 … +1 (wind / creature contact).
 */
#include "gen.h"
#include "rng.h"
#include "noise.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#define NF_PI 3.14159265358979f
#define SWAY_FRAMES 9

/* ------------------------------------------------------------------ */
/* drawing into a frame (with an internal z-test)                      */
/* ------------------------------------------------------------------ */

static void cv_set(frame_t *f, int x, int y, rgb_t c, int z)
{
    if (x < 0 || y < 0 || x >= f->w || y >= f->h) return;
    px_t *p = &f->px[y * f->w + x];
    if (p->a == 255 && p->z > z) return;
    p->r = c.r; p->g = c.g; p->b = c.b;
    p->a = 255;
    p->z = (int8_t)clampi(z, -120, 120);
    p->f = 0;
}

static void cv_force(frame_t *f, int x, int y, rgb_t c, int z)
{
    if (x < 0 || y < 0 || x >= f->w || y >= f->h) return;
    px_t *p = &f->px[y * f->w + x];
    p->r = c.r; p->g = c.g; p->b = c.b;
    p->a = 255;
    p->z = (int8_t)clampi(z, -120, 120);
    p->f = 0;
}

static void cv_glow(frame_t *f, int x, int y, rgb_t c, int z)
{
    cv_set(f, x, y, c, z);
    if (x >= 0 && y >= 0 && x < f->w && y < f->h) f->px[y * f->w + x].f |= PX_GLOW;
}

/* translucent glow: only into empty / weaker pixels */
static void cv_halo(frame_t *f, int x, int y, rgb_t c, int alpha)
{
    if (x < 0 || y < 0 || x >= f->w || y >= f->h) return;
    px_t *p = &f->px[y * f->w + x];
    if (p->a == 255 || p->a >= alpha) return;
    p->r = c.r; p->g = c.g; p->b = c.b;
    p->a = (uint8_t)alpha;
    p->z = 0;
    p->f = PX_GLOW;
}

static int cv_opaque(const frame_t *f, int x, int y)
{
    if (x < 0 || y < 0 || x >= f->w || y >= f->h) return 0;
    return f->px[y * f->w + x].a == 255;
}

static void cv_line(frame_t *f, float x0, float y0, float x1, float y1, rgb_t c, int z)
{
    float dx = x1 - x0, dy = y1 - y0;
    int steps = (int)(fmaxf(fabsf(dx), fabsf(dy)) * 1.5f) + 1;
    for (int i = 0; i <= steps; i++) {
        float t = (float)i / (float)steps;
        cv_set(f, (int)floorf(x0 + dx * t + 0.5f), (int)floorf(y0 + dy * t + 0.5f), c, z);
    }
}

static void cv_disc(frame_t *f, float cx, float cy, float r, rgb_t c, int z)
{
    int x0 = (int)floorf(cx - r), x1 = (int)ceilf(cx + r);
    int y0 = (int)floorf(cy - r), y1 = (int)ceilf(cy + r);
    for (int y = y0; y <= y1; y++)
        for (int x = x0; x <= x1; x++) {
            float dx = (float)x - cx, dy = (float)y - cy;
            if (dx * dx + dy * dy <= r * r + 0.25f) cv_set(f, x, y, c, z);
        }
}

/* Thick line (capsule) with cylindrical shading; light from the upper right. */
static void cv_capsule(frame_t *f, float x0, float y0, float r0, float x1, float y1, float r1,
                       rgb_t base, int z, int zbulge)
{
    float rmax = fmaxf(r0, r1);
    int bx0 = (int)floorf(fminf(x0, x1) - rmax - 1), bx1 = (int)ceilf(fmaxf(x0, x1) + rmax + 1);
    int by0 = (int)floorf(fminf(y0, y1) - rmax - 1), by1 = (int)ceilf(fmaxf(y0, y1) + rmax + 1);
    float vx = x1 - x0, vy = y1 - y0;
    float vv = vx * vx + vy * vy;
    for (int y = by0; y <= by1; y++)
        for (int x = bx0; x <= bx1; x++) {
            float px = (float)x - x0, py = (float)y - y0;
            float t = vv > 0 ? clampf((px * vx + py * vy) / vv, 0.0f, 1.0f) : 0.0f;
            float cx = x0 + vx * t, cy = y0 + vy * t;
            float r = r0 + (r1 - r0) * t;
            float dx = (float)x - cx, dy = (float)y - cy;
            float d2 = dx * dx + dy * dy;
            if (d2 > r * r + 0.3f) continue;
            float d = sqrtf(d2);
            float nx = d > 0 ? dx / d : 0, ny = d > 0 ? dy / d : -1;
            float lit = nx * 0.6f + ny * -0.8f;               /* light from the upper right */
            float k = 0.62f + 0.38f * lit * (d / (r + 0.01f));
            int zz = z + (int)((float)zbulge * (1.0f - d / (r + 0.5f)));
            cv_set(f, x, y, rgb_scale(base, k), zz);
        }
}

/* Pointed leaf: length len, max half-width wid, angle ang (radians). */
static void cv_leaf(frame_t *f, float cx, float cy, float len, float wid, float ang,
                    rgb_t light, rgb_t dark, int z)
{
    float ca = cosf(ang), sa = sinf(ang);
    float rr = len * 0.5f + 1.0f;
    for (int y = (int)floorf(cy - rr); y <= (int)ceilf(cy + rr); y++)
        for (int x = (int)floorf(cx - rr); x <= (int)ceilf(cx + rr); x++) {
            float dx = (float)x - cx, dy = (float)y - cy;
            float u = dx * ca + dy * sa;          /* along the leaf */
            float v = -dx * sa + dy * ca;         /* across */
            float tu = u / len + 0.5f;
            if (tu < 0.0f || tu > 1.0f) continue;
            float hw = wid * powf(sinf(NF_PI * tu), 0.7f) + 0.35f;
            if (fabsf(v) > hw) continue;
            /* the half of the leaf facing the light (upper right) is lighter */
            float ox = -sa * v, oy = ca * v;      /* perpendicular component of the offset from the axis */
            rgb_t c = (ox * 0.6f - oy * 0.8f) >= 0.0f ? light : dark;
            cv_set(f, x, y, c, z);
        }
}

/* Lightens edges facing the light (top / right). */
static void frame_rimlight(frame_t *f, rgb_t tint, float amount)
{
    for (int y = 0; y < f->h; y++)
        for (int x = 0; x < f->w; x++) {
            px_t *p = &f->px[y * f->w + x];
            if (p->a != 255 || (p->f & PX_GLOW)) continue;
            int edge = !cv_opaque(f, x, y - 1) + !cv_opaque(f, x + 1, y) + !cv_opaque(f, x + 1, y - 1);
            if (!edge) continue;
            float t = amount * (edge >= 2 ? 1.0f : 0.6f);
            rgb_t c = rgb_mix(mkrgb(p->r, p->g, p->b), tint, t);
            p->r = c.r; p->g = c.g; p->b = c.b;
        }
}

static rgb_t jitter(rgb_t c, rng_t *r, int amt)
{
    return rgb_add(c, rng_between(r, -amt, amt));
}

static rgb_t vary(rgb_t c, rng_t *r, float amt)
{
    return mkrgb((int)(c.r * (1.0f + rng_range(r, -amt, amt))),
                 (int)(c.g * (1.0f + rng_range(r, -amt, amt))),
                 (int)(c.b * (1.0f + rng_range(r, -amt, amt))));
}

static void set_point(sprite_t *s, const char *name, int x, int y)
{
    char key[NF_NAME + 8], val[32];
    snprintf(key, sizeof key, "point.%.30s", name);
    snprintf(val, sizeof val, "%d %d", x, y);
    sprite_set_prop(s, key, val);
}

/* Appends a path point "x y " to a string (relative to the frame anchor). */
static void path_add(char *buf, size_t sz, const frame_t *f, float x, float y)
{
    size_t l = strlen(buf);
    if (l + 16 < sz) snprintf(buf + l, sz - l, "%d %d ", (int)floorf(x + 0.5f) - f->ax, (int)floorf(y + 0.5f) - f->ay);
}

/* Walkway along the top of a shape: the highest opaque pixel in columns x0..x1. */
static void export_top_path(sprite_t *s, const frame_t *f, const char *key, int x0, int x1, int step)
{
    char buf[384] = "";
    int n = 0;
    for (int x = x0; x <= x1 && n < 30; x += step) {
        for (int y = 0; y < f->h; y++)
            if (cv_opaque(f, x, y)) { path_add(buf, sizeof buf, f, (float)x, (float)y - 1.0f); n++; break; }
    }
    if (n >= 2) sprite_set_prop(s, key, buf);
}

/* Clips for a plant with SWAY_FRAMES bend frames starting at index first. */
static void add_sway_clips(sprite_t *s, int first)
{
    int16_t seq[SWAY_FRAMES];
    for (int i = 0; i < SWAY_FRAMES; i++) seq[i] = (int16_t)(first + i);
    int mid = first + SWAY_FRAMES / 2;
    int16_t idle = (int16_t)mid;
    sprite_add_clip(s, "idle", CLIP_LOOP, 1, 0, &idle, 1);
    sprite_add_clip(s, "sway", CLIP_PARAM, 1, 0, seq, SWAY_FRAMES);
    /* damped oscillation after a creature's touch */
    static const float damp[] = { 1.0f, 0.55f, -0.6f, -0.3f, 0.4f, 0.15f, -0.25f, -0.1f, 0.12f, 0.0f, 0.0f };
    int16_t rs[NF_COUNT(damp)], rsm[NF_COUNT(damp)];
    for (size_t i = 0; i < NF_COUNT(damp); i++) {
        int k = (int)((damp[i] + 1.0f) * 0.5f * (SWAY_FRAMES - 1) + 0.5f);
        int km = (int)((-damp[i] + 1.0f) * 0.5f * (SWAY_FRAMES - 1) + 0.5f);
        rs[i] = (int16_t)(first + k);
        rsm[i] = (int16_t)(first + km);
    }
    sprite_add_clip(s, "rustle_r", CLIP_ONCE, 12, 0, rs, (int)NF_COUNT(damp));
    sprite_add_clip(s, "rustle_l", CLIP_ONCE, 12, 0, rsm, (int)NF_COUNT(damp));
}

static float sway_bend(int i) { return -1.0f + 2.0f * (float)i / (float)(SWAY_FRAMES - 1); }

/* night colours */
#define C_GRASS_DARK  mkrgb(14, 36, 28)
#define C_GRASS_MID   mkrgb(30, 66, 46)
#define C_GRASS_TIP   mkrgb(62, 104, 70)
#define C_TEAL        mkrgb(24, 62, 60)
#define C_MOON        mkrgb(150, 185, 190)
#define C_BARK_DARK   mkrgb(22, 18, 16)
#define C_BARK        mkrgb(52, 44, 38)
#define C_BARK_LIGHT  mkrgb(84, 74, 64)
#define C_MOSS        mkrgb(46, 74, 40)
#define C_MOSS_LIGHT  mkrgb(78, 110, 56)
#define C_SOIL        mkrgb(18, 15, 12)

/* ------------------------------------------------------------------ */
/* grass                                                               */
/* ------------------------------------------------------------------ */

typedef struct {
    float x0, h, lean, curl, w0;
    rgb_t base, tip;
    int z, seedhead;
} blade_t;

typedef struct {
    int nblades;
    blade_t b[48];
    int w, h, ax, ay;
} grass_t;

static void grass_make(grass_t *g, rng_t *r, int minh, int maxh, int minn, int maxn, float spread,
                       float seedheads)
{
    g->nblades = rng_between(r, minn, maxn);
    float maxlean = 0;
    int hmax = 0;
    for (int i = 0; i < g->nblades; i++) {
        blade_t *b = &g->b[i];
        b->x0 = rng_range(r, -spread, spread);
        b->h = (float)rng_between(r, minh, maxh);
        b->lean = rng_range(r, -0.55f, 0.55f) * b->h + b->x0 * 0.6f;
        b->curl = rng_range(r, 0.6f, 1.6f);
        b->w0 = b->h > 12 ? 1.6f : 1.0f;
        int kind = rng_int(r, 10);
        rgb_t tip = kind < 6 ? C_GRASS_TIP : kind < 8 ? mkrgb(48, 92, 86) : mkrgb(92, 96, 58);
        b->base = jitter(C_GRASS_DARK, r, 4);
        b->tip = vary(tip, r, 0.15f);
        b->z = rng_between(r, -3, 3);
        b->seedhead = rng_chance(r, seedheads);
        if (fabsf(b->lean) > maxlean) maxlean = fabsf(b->lean);
        if ((int)b->h > hmax) hmax = (int)b->h;
    }
    int reach = (int)(spread + maxlean + 6);
    g->w = 2 * reach + 1;
    g->h = hmax + 3;
    g->ax = reach;
    g->ay = g->h - 1;
}

static void grass_draw(frame_t *f, const grass_t *g, float bend)
{
    for (int i = 0; i < g->nblades; i++) {
        const blade_t *b = &g->b[i];
        float amp = 1.5f + b->h * 0.28f;
        int steps = (int)(b->h * 2.0f) + 2;
        float px = 0, py = 0;
        for (int k = 0; k <= steps; k++) {
            float t = (float)k / (float)steps;
            float x = (float)g->ax + b->x0 + (b->lean * powf(t, b->curl) + bend * amp * t * t);
            float y = (float)g->ay - b->h * t * (1.0f - 0.18f * t * fabsf(b->lean) / (b->h + 1.0f));
            rgb_t c = rgb_mix(b->base, b->tip, powf(t, 0.8f));
            cv_set(f, (int)floorf(x + 0.5f), (int)floorf(y + 0.5f), c, b->z);
            if (t < 0.35f && b->w0 > 1.2f)
                cv_set(f, (int)floorf(x + 0.5f) + 1, (int)floorf(y + 0.5f), rgb_scale(c, 0.8f), b->z);
            px = x; py = y;
        }
        if (b->seedhead) {
            rgb_t sc = mkrgb(96, 92, 70);
            cv_set(f, (int)floorf(px + 0.5f), (int)floorf(py + 0.5f) - 1, sc, b->z);
            cv_set(f, (int)floorf(px + 0.5f) + 1, (int)floorf(py + 0.5f), rgb_scale(sc, 0.8f), b->z);
            cv_set(f, (int)floorf(px + 0.5f) - 1, (int)floorf(py + 0.5f) + 1, rgb_scale(sc, 0.7f), b->z);
        }
    }
}

static void gen_grass_common(sprite_t *s, uint64_t seed, int minh, int maxh, int minn, int maxn,
                             float spread, float seedheads)
{
    rng_t r = rng_seed(seed);
    grass_t g;
    grass_make(&g, &r, minh, maxh, minn, maxn, spread, seedheads);
    for (int i = 0; i < SWAY_FRAMES; i++) {
        char name[16];
        snprintf(name, sizeof name, "bend%d", i);
        frame_t *f = sprite_add_frame(s, name, g.w, g.h, g.ax, g.ay);
        grass_draw(f, &g, sway_bend(i));
        frame_rimlight(f, C_MOON, 0.12f);
    }
    add_sway_clips(s, 0);
}

static void gen_grass(sprite_t *s, uint64_t seed) { gen_grass_common(s, seed, 6, 15, 9, 16, 5, 0.0f); }
static void gen_grass_tall(sprite_t *s, uint64_t seed) { gen_grass_common(s, seed, 14, 28, 8, 14, 6, 0.35f); }
static void gen_grass_low(sprite_t *s, uint64_t seed) { gen_grass_common(s, seed, 3, 7, 14, 24, 9, 0.0f); }

/* ------------------------------------------------------------------ */
/* fern                                                                */
/* ------------------------------------------------------------------ */

typedef struct {
    float ang, len, droop;
    int z;
    rgb_t rach, pin, tip;
} frond_t;

typedef struct {
    int n;
    frond_t fr[12];
    int w, h, ax, ay;
} fern_t;

static void fern_draw(frame_t *f, const fern_t *fe, float bend)
{
    for (int i = 0; i < fe->n; i++) {
        const frond_t *fr = &fe->fr[i];
        float x = (float)fe->ax, y = (float)fe->ay;
        float a = fr->ang;
        int steps = (int)(fr->len * 3.0f);
        float ds = fr->len / (float)steps;
        float dirsign = cosf(fr->ang) >= 0 ? 1.0f : -1.0f;
        int side = 1;
        float since = 0;
        for (int k = 0; k < steps; k++) {
            float t = (float)k / (float)steps;
            /* the wind rotates the frond around its base (length is preserved) */
            float wa = a + bend * 0.45f * t;
            float dirx = cosf(wa), diry = sinf(wa);
            x += dirx * ds;
            y += diry * ds;
            /* arc: the direction gradually turns outward and down (grows with t squared) */
            a += fr->droop * ds / fr->len * dirsign * (0.2f + 1.6f * t * t);
            rgb_t rc = rgb_mix(fr->rach, fr->tip, t * 0.7f);
            cv_set(f, (int)floorf(x + 0.5f), (int)floorf(y + 0.5f), rc, fr->z);
            since += ds;
            if (since >= 1.2f && t > 0.12f && t < 0.97f) {
                since = 0;
                /* leaflet (pinna): longest in the first third, shrinks towards the tip */
                float pl = (1.0f + fr->len * 0.14f) * sinf(NF_PI * (0.18f + 0.82f * t)) * (1.05f - t * 0.55f);
                float pa = a + (float)side * (NF_PI * 0.5f - 0.55f);
                float ex = x + cosf(pa) * pl, ey = y + sinf(pa) * pl + 0.45f * pl;
                rgb_t pc = rgb_mix(fr->pin, fr->tip, t * 0.9f);
                /* leaflets on the upper side of the frond are lit */
                if (ey < y) pc = rgb_mix(pc, C_MOON, 0.12f);
                else pc = rgb_scale(pc, 0.82f);
                cv_line(f, x, y, ex, ey, pc, fr->z);
                side = -side;
            }
        }
    }
}

static void gen_fern_scaled(sprite_t *s, uint64_t seed, float scale)
{
    rng_t r = rng_seed(seed);
    fern_t fe;
    fe.n = rng_between(&r, 5, 9);
    float maxlen = 0;
    for (int i = 0; i < fe.n; i++) {
        frond_t *fr = &fe.fr[i];
        float u = fe.n > 1 ? (float)i / (float)(fe.n - 1) : 0.5f;
        /* from the left (-165°) to the right (-15°) edge, middle ones taller */
        fr->ang = -NF_PI * (0.78f - 0.56f * u) + rng_range(&r, -0.08f, 0.08f);
        float center = 1.0f - fabsf(u - 0.5f) * 1.4f;
        fr->len = (rng_range(&r, 12.0f, 16.0f) + center * rng_range(&r, 5.0f, 9.0f)) * scale;
        fr->droop = rng_range(&r, 1.0f, 1.7f);
        fr->z = rng_between(&r, -3, 4);
        fr->rach = vary(mkrgb(26, 54, 38), &r, 0.1f);
        fr->pin = vary(mkrgb(34, 76, 50), &r, 0.15f);
        fr->tip = vary(mkrgb(70, 118, 74), &r, 0.12f);
        if (fr->len > maxlen) maxlen = fr->len;
    }
    int reach = (int)(maxlen * 1.1f) + 6;
    fe.w = 2 * reach + 1;
    fe.h = (int)(maxlen * 1.0f) + 4;
    fe.ax = reach;
    fe.ay = fe.h - 1;
    for (int i = 0; i < SWAY_FRAMES; i++) {
        char name[16];
        snprintf(name, sizeof name, "bend%d", i);
        frame_t *f = sprite_add_frame(s, name, fe.w, fe.h, fe.ax, fe.ay);
        fern_draw(f, &fe, sway_bend(i) * 1.1f);
        frame_rimlight(f, C_MOON, 0.1f);
    }
    add_sway_clips(s, 0);
}

static void gen_fern(sprite_t *s, uint64_t seed) { gen_fern_scaled(s, seed, 1.0f); }
static void gen_fern_big(sprite_t *s, uint64_t seed) { gen_fern_scaled(s, seed, 1.7f); }

/* ------------------------------------------------------------------ */
/* bush                                                                */
/* ------------------------------------------------------------------ */

typedef struct { float cx, cy, rx, ry; } lobe_t;
typedef struct { float x, y, len, wid, ang; rgb_t light, dark; int z, curtain; } bleaf_t;
typedef struct { float x, y, rx, ry; } hole_t;

#define MAX_BLEAVES 3200

typedef struct {
    int w, h, ax, ay;
    int nlobes;
    lobe_t lobe[7];
    uint32_t nseed;
    int nholes;           /* 0..1 = peek openings, the last one = the bottom passage */
    hole_t hole[3];
    int nleaves;
    bleaf_t leaf[MAX_BLEAVES];
    int ntwigs;
    float twig[24][4];
    int nberries;
    float berry[24][2];
    rgb_t berry_c;
} bush_t;

static bush_t g_bush; /* large structure; the generator is not reentrant */

static float bush_mask(const bush_t *b, float x, float y)
{
    float best = -10.0f;
    for (int i = 0; i < b->nlobes; i++) {
        const lobe_t *l = &b->lobe[i];
        float dx = (x - l->cx) / l->rx, dy = (y - l->cy) / l->ry;
        float v = 1.0f - (dx * dx + dy * dy);
        if (v > best) best = v;
    }
    best += (fbm2(x * 0.22f, y * 0.22f, 3, b->nseed) - 0.5f) * 0.55f;
    if (y > (float)b->ay) best = -1.0f;
    return best;
}

static int in_hole(const hole_t *h, float x, float y, float scale)
{
    float dx = (x - h->x) / (h->rx * scale), dy = (y - h->y) / (h->ry * scale);
    return dx * dx + dy * dy <= 1.0f;
}

static void bush_make(bush_t *b, rng_t *r, float wmul, float hmul)
{
    int W = (int)(rng_range(r, 44, 60) * wmul), H = (int)(rng_range(r, 26, 34) * hmul);
    b->w = W + 6;
    b->h = H + 2;
    b->ax = b->w / 2;
    b->ay = b->h - 1;
    b->nseed = rng_u32(r);
    b->nlobes = rng_between(r, 3, 5) + 1;
    b->lobe[0] = (lobe_t){ (float)b->ax, (float)b->ay - H * 0.45f, W * 0.40f, H * 0.52f };
    for (int i = 1; i < b->nlobes; i++) {
        lobe_t *l = &b->lobe[i];
        l->cx = (float)b->ax + rng_range(r, -W * 0.28f, W * 0.28f);
        l->rx = rng_range(r, W * 0.18f, W * 0.30f);
        l->ry = rng_range(r, H * 0.30f, H * 0.48f);
        l->cy = (float)b->ay - rng_range(r, l->ry * 0.6f, (float)H - l->ry);
    }
    /* openings: 1–2 for peeking in the upper half + a bottom passage */
    int peeks = rng_between(r, 1, 2);
    b->nholes = 0;
    for (int i = 0; i < peeks; i++) {
        hole_t *h = &b->hole[b->nholes];
        for (int tries = 0; tries < 40; tries++) {
            h->x = (float)b->ax + rng_range(r, -W * 0.28f, W * 0.28f);
            h->y = (float)b->ay - H * rng_range(r, 0.38f, 0.66f);
            h->rx = rng_range(r, 3.0f, 4.5f);
            h->ry = rng_range(r, 2.5f, 3.4f);
            if (bush_mask(b, h->x, h->y) > 0.35f &&
                (i == 0 || fabsf(h->x - b->hole[0].x) > 12)) break;
        }
        b->nholes++;
    }
    hole_t *bot = &b->hole[b->nholes++];
    bot->x = (float)b->ax + (rng_chance(r, 0.5f) ? -1.0f : 1.0f) * rng_range(r, W * 0.08f, W * 0.22f);
    bot->y = (float)b->ay - 1.0f;
    bot->rx = rng_range(r, 4.0f, 5.5f);
    bot->ry = rng_range(r, 3.5f, 4.5f);

    /* twigs from the root */
    b->ntwigs = 0;
    int nt = rng_between(r, 4, 7);
    for (int i = 0; i < nt && b->ntwigs < 22; i++) {
        float x0 = (float)b->ax + rng_range(r, -3, 3), y0 = (float)b->ay;
        float a = -NF_PI * 0.5f + rng_range(r, -1.0f, 1.0f);
        float l = rng_range(r, H * 0.35f, H * 0.7f);
        float x1 = x0 + cosf(a) * l, y1 = y0 + sinf(a) * l;
        b->twig[b->ntwigs][0] = x0; b->twig[b->ntwigs][1] = y0;
        b->twig[b->ntwigs][2] = x1; b->twig[b->ntwigs][3] = y1;
        b->ntwigs++;
        float a2 = a + rng_range(r, -0.8f, 0.8f), l2 = l * 0.5f;
        float sx = x0 + (x1 - x0) * 0.55f, sy = y0 + (y1 - y0) * 0.55f;
        b->twig[b->ntwigs][0] = sx; b->twig[b->ntwigs][1] = sy;
        b->twig[b->ntwigs][2] = sx + cosf(a2) * l2; b->twig[b->ntwigs][3] = sy + sinf(a2) * l2;
        b->ntwigs++;
    }

    /* leaves in clusters: each cluster is lit from the upper right -> lumpy volume */
    static const rgb_t hues[] = {
        {24, 54, 36}, {22, 58, 48}, {42, 68, 34}, {26, 52, 52}, {32, 62, 40}
    };
    int area = 0;
    for (int y = 0; y < b->h; y++)
        for (int x = 0; x < b->w; x++) area += bush_mask(b, (float)x, (float)y) > 0;
    int nclusters = NF_MAX(6, area / 26);
    int hue_main = rng_int(r, (int)NF_COUNT(hues));
    b->nleaves = 0;
    for (int c = 0; c < nclusters && b->nleaves < MAX_BLEAVES - 40; c++) {
        float cx = 0, cy = 0, m = 0;
        for (int tries = 0; tries < 30; tries++) {
            cx = rng_range(r, 0, (float)b->w);
            cy = rng_range(r, 0, (float)b->h);
            m = bush_mask(b, cx, cy);
            if (m > 0.05f) break;
        }
        if (m <= 0.05f) continue;
        float rc = rng_range(r, 2.6f, 5.0f);
        /* cluster depth: closer at the edge and the top, deeper inside */
        float edge = 1.0f - clampf(m * 1.5f, 0.0f, 1.0f);
        int cz = (int)(-3.0f + edge * 3.0f + rng_range(r, 0.0f, 3.5f));
        float lx = (cx - (float)b->ax) / (float)W, ly = ((float)b->ay - cy) / (float)H;
        float glit = clampf(0.25f + lx * 0.5f + ly * 0.65f + (float)cz * 0.07f, 0.05f, 1.2f);
        rgb_t hue = vary(hues[rng_chance(r, 0.7f) ? hue_main : rng_int(r, (int)NF_COUNT(hues))], r, 0.1f);
        int nl = (int)(rc * rc * 0.55f) + 2;
        for (int k = 0; k < nl && b->nleaves < MAX_BLEAVES; k++) {
            float a = rng_range(r, 0, NF_PI * 2.0f), d = sqrtf(rng_float(r)) * rc;
            float x = cx + cosf(a) * d, y = cy + sinf(a) * d * 0.85f;
            if (bush_mask(b, x, y) <= -0.05f) continue;
            bleaf_t *L = &b->leaf[b->nleaves];
            L->curtain = 0;
            for (int q = 0; q < b->nholes; q++)
                if (in_hole(&b->hole[q], x, y, 1.0f)) L->curtain = 1;
            if (L->curtain && rng_chance(r, 0.5f)) continue;
            L->x = x; L->y = y;
            L->len = rng_range(r, 3.2f, 5.5f);
            L->wid = rng_range(r, 1.1f, 1.8f);
            L->ang = a + rng_range(r, -0.7f, 0.7f) + 0.5f;
            /* local light within a cluster: the upper right side is lighter */
            float loc = (cosf(a) * 0.6f - sinf(a) * 0.8f) * (d / rc);
            float lit = clampf(glit + loc * 0.35f, 0.0f, 1.3f);
            L->z = cz + (loc > 0.2f ? 1 : 0);
            if (L->curtain) L->z = 1;
            rgb_t base = rgb_scale(hue, 0.45f + lit * 0.85f);
            L->light = rgb_mix(base, C_MOON, 0.05f + lit * 0.1f);
            L->dark = rgb_scale(base, 0.68f);
            b->nleaves++;
        }
    }
    /* berries */
    b->nberries = rng_chance(r, 0.5f) ? rng_between(r, 4, 14) : 0;
    b->berry_c = rng_chance(r, 0.5f) ? mkrgb(92, 34, 52) : mkrgb(48, 52, 108);
    for (int i = 0; i < b->nberries; i++) {
        for (int tries = 0; tries < 20; tries++) {
            float x = rng_range(r, 0, (float)b->w), y = rng_range(r, 0, (float)b->h * 0.8f);
            if (bush_mask(b, x, y) > 0.1f) { b->berry[i][0] = x; b->berry[i][1] = y; break; }
            b->berry[i][0] = -100;
        }
    }
}

/* rustle_hole: -1 = none, otherwise the index of the opening around which the leaves tremble.
 * part_hole / part: parting of the leaves above the opening (0 … 1). */
static void bush_draw(frame_t *f, const bush_t *b, int rustle_hole, int rustle_phase,
                      int part_hole, float part)
{
    /* 1. dark inner mass */
    for (int y = 0; y < b->h; y++)
        for (int x = 0; x < b->w; x++) {
            float m = bush_mask(b, (float)x, (float)y);
            if (m <= 0) continue;
            float n = hash2f(x, y, b->nseed ^ 0x55u);
            rgb_t c = rgb_scale(mkrgb(10, 26, 20), 1.0f - clampf(m, 0.0f, 1.0f) * 0.45f);
            if (n > 0.86f) c = mkrgb(16, 36, 28);
            cv_set(f, x, y, c, -5);
        }
    /* 2. twigs */
    for (int i = 0; i < b->ntwigs; i++)
        cv_line(f, b->twig[i][0], b->twig[i][1], b->twig[i][2], b->twig[i][3], mkrgb(40, 30, 24), -4);
    /* 3. hollows (openings) — the deepest */
    for (int k = 0; k < b->nholes; k++) {
        const hole_t *h = &b->hole[k];
        for (int y = (int)(h->y - h->ry - 1); y <= (int)(h->y + h->ry + 1); y++)
            for (int x = (int)(h->x - h->rx - 1); x <= (int)(h->x + h->rx + 1); x++) {
                if (!in_hole(h, (float)x, (float)y, 1.0f)) continue;
                if (bush_mask(b, (float)x, (float)y) <= 0 && k < b->nholes - 1) continue;
                float dx = ((float)x - h->x) / h->rx, dy = ((float)y - h->y) / h->ry;
                float d = sqrtf(dx * dx + dy * dy);
                rgb_t c = rgb_mix(mkrgb(3, 6, 5), mkrgb(9, 20, 16), d * d);
                cv_force(f, x, y, c, -8);
            }
    }
    /* 4. leaves */
    for (int i = 0; i < b->nleaves; i++) {
        const bleaf_t *L = &b->leaf[i];
        float x = L->x, y = L->y;
        if (rustle_hole >= 0) {
            const hole_t *h = &b->hole[rustle_hole];
            if (in_hole(h, x, y, 2.2f)) {
                int k = (int)(hash2i(i, rustle_phase, 77u) % 3u) - 1;
                x += (float)k;
                y += (float)((int)(hash2i(i, rustle_phase, 91u) % 2u)) * (rustle_phase & 1 ? -1.0f : 0.0f);
            }
        }
        if (part_hole >= 0 && part > 0) {
            const hole_t *h = &b->hole[part_hole];
            if (in_hole(h, x, y, 1.9f)) {
                float dx = x - h->x, dy = y - h->y;
                float d = sqrtf(dx * dx + dy * dy) + 0.01f;
                float push = part * (h->rx * 1.3f) * (1.0f - fminf(d / (h->rx * 2.0f), 1.0f));
                x += dx / d * push;
                y += dy / d * push * 0.7f;
            }
        }
        cv_leaf(f, x, y, L->len, L->wid, L->ang, L->light, L->dark, L->z);
    }
    /* 5. berries */
    for (int i = 0; i < b->nberries; i++) {
        if (b->berry[i][0] < 0) continue;
        int x = (int)b->berry[i][0], y = (int)b->berry[i][1];
        cv_set(f, x, y, b->berry_c, 3);
        cv_set(f, x + 1, y, rgb_scale(b->berry_c, 0.7f), 3);
    }
    frame_rimlight(f, C_MOON, 0.16f);
}

static void gen_bush_common(sprite_t *s, uint64_t seed, float wmul, float hmul)
{
    rng_t r = rng_seed(seed);
    bush_t *b = &g_bush;
    memset(b, 0, sizeof *b);
    bush_make(b, &r, wmul, hmul);
    frame_t *f = sprite_add_frame(s, "base", b->w, b->h, b->ax, b->ay);
    bush_draw(f, b, -1, 0, -1, 0);

    int16_t base = 0;
    sprite_add_clip(s, "idle", CLIP_LOOP, 1, 0, &base, 1);

    char name[NF_NAME], cname[NF_NAME];
    for (int k = 0; k < b->nholes; k++) {
        int i0 = s->nframes;
        for (int ph = 0; ph < 2; ph++) {
            snprintf(name, sizeof name, "rustle%d_%c", k, 'a' + ph);
            f = sprite_add_frame(s, name, b->w, b->h, b->ax, b->ay);
            bush_draw(f, b, k, ph + 1, -1, 0);
        }
        int16_t rs[] = { 0, (int16_t)i0, (int16_t)(i0 + 1), (int16_t)i0, (int16_t)(i0 + 1), (int16_t)i0, 0 };
        snprintf(cname, sizeof cname, "rustle%d", k);
        sprite_add_clip(s, cname, CLIP_ONCE, 10, 0, rs, (int)NF_COUNT(rs));

        int p0 = s->nframes;
        for (int ph = 1; ph <= 3; ph++) {
            snprintf(name, sizeof name, "part%d_%d", k, ph);
            f = sprite_add_frame(s, name, b->w, b->h, b->ax, b->ay);
            bush_draw(f, b, -1, 0, k, (float)ph / 3.0f);
        }
        int16_t op[] = { (int16_t)i0, (int16_t)p0, (int16_t)(p0 + 1), (int16_t)(p0 + 2) };
        int16_t cl[] = { (int16_t)(p0 + 2), (int16_t)(p0 + 1), (int16_t)p0, (int16_t)(i0 + 1), 0 };
        snprintf(cname, sizeof cname, "open%d", k);
        sprite_add_clip(s, cname, CLIP_ONCE, 6, 0, op, (int)NF_COUNT(op));
        snprintf(cname, sizeof cname, "close%d", k);
        sprite_add_clip(s, cname, CLIP_ONCE, 5, 0, cl, (int)NF_COUNT(cl));

        snprintf(name, sizeof name, k == b->nholes - 1 ? "entry" : "peek%d", k);
        set_point(s, name, (int)b->hole[k].x - b->ax, (int)(b->hole[k].y + b->hole[k].ry * 0.5f) - b->ay);
    }
    sprite_set_prop(s, "z.inside", "-6");
    float top = (float)b->ay;
    for (int y = 0; y < b->h; y++)
        if (bush_mask(b, (float)b->ax, (float)y) > 0) { top = (float)y; break; }
    set_point(s, "top", 0, (int)top - b->ay);
    char holes[16];
    snprintf(holes, sizeof holes, "%d", b->nholes - 1);
    sprite_set_prop(s, "peeks", holes);
}

static void gen_bush(sprite_t *s, uint64_t seed) { gen_bush_common(s, seed, 1.0f, 1.0f); }
static void gen_bush_wide(sprite_t *s, uint64_t seed) { gen_bush_common(s, seed, 1.35f, 0.75f); }
static void gen_bush_small(sprite_t *s, uint64_t seed) { gen_bush_common(s, seed, 0.62f, 0.7f); }
static void gen_bush_big(sprite_t *s, uint64_t seed) { gen_bush_common(s, seed, 1.55f, 1.45f); }

/* ------------------------------------------------------------------ */
/* tree trunk                                                          */
/* ------------------------------------------------------------------ */

enum { TRUNK_OAK, TRUNK_BIRCH, TRUNK_PINE, TRUNK_DEAD, TRUNK_GIANT, TRUNK_BEECH, TRUNK_SPRUCE, TRUNK_OLD, TRUNK_IVY,
       TRUNK_KINDS };

/* every tree species has its own branch (property "branch" of the trunk) */
static const char *BRANCH_FOR[TRUNK_KINDS] = {
    "branch", "branch_birch", "branch_pine", "branch_dead", "branch_giant",
    "branch_beech", "branch_spruce", "branch_old", "branch_ivy"
};

/* Bark colour by tree species. u: -1…1 across, lit: lighting, fromb: px from the ground. */
static rgb_t trunk_bark(int kind, int x, int y, float u, float lit, uint32_t ns, float fromb)
{
    float fine = hash2f(x, y, ns ^ 0xabu);
    rgb_t c;
    switch (kind) {
    case TRUNK_BIRCH: {
        /* white bark, dark horizontal lenticels, black patches near the ground */
        c = rgb_scale(mkrgb(176, 178, 168), 0.35f + lit * 0.7f);
        float len = vnoise2((float)x * 0.25f, (float)y * 1.1f, ns ^ 0x31u);
        if (len > 0.74f && fine > 0.2f) c = rgb_scale(mkrgb(40, 36, 34), 0.7f + lit * 0.4f);
        float patch = vnoise2((float)x * 0.3f, (float)y * 0.12f, ns ^ 0x55u) + (fromb < 24 ? (24 - fromb) / 40.0f : 0);
        if (patch > 0.8f) c = rgb_scale(mkrgb(30, 28, 26), 0.8f + lit * 0.3f);
        else if (patch > 0.72f) c = rgb_scale(mkrgb(110, 104, 92), 0.5f + lit * 0.5f);
        break;
    }
    case TRUNK_PINE: {
        /* red-brown scales / plates separated by dark crevices */
        float px = (float)x * 0.32f, py = (float)y * 0.13f;
        float cell = vnoise2(px, py, ns);
        float edge = fabsf(vnoise2(px * 1.7f, py * 1.7f, ns ^ 0x99u) - 0.5f);
        rgb_t base = rgb_mix(mkrgb(92, 52, 34), mkrgb(120, 74, 48), cell);
        c = rgb_scale(base, 0.4f + lit * 0.7f);
        if (edge < 0.06f) c = rgb_scale(mkrgb(30, 18, 14), 0.8f + lit * 0.4f);
        if (fromb > 60 && fine > 0.6f) c = rgb_mix(c, mkrgb(150, 96, 60), 0.2f);
        break;
    }
    case TRUNK_BEECH: {
        /* beech: smooth grey bark, fine horizontal stripes, light lichen patches */
        float n = vnoise2((float)x * 0.12f, (float)y * 0.5f, ns);
        c = rgb_scale(rgb_mix(mkrgb(96, 100, 98), mkrgb(124, 128, 124), n), 0.38f + lit * 0.68f);
        if (vnoise2((float)x * 0.15f, (float)y * 1.6f, ns ^ 0x21u) > 0.8f) c = rgb_add(c, -14);
        if (vnoise2((float)x * 0.3f, (float)y * 0.2f, ns ^ 0x44u) > 0.82f) c = rgb_mix(c, mkrgb(140, 150, 136), 0.4f);
        break;
    }
    case TRUNK_SPRUCE: {
        /* spruce: dark grey-brown bark with small round scales */
        float sc = vnoise2((float)x * 0.7f, (float)y * 0.45f, ns);
        c = rgb_scale(rgb_mix(mkrgb(52, 42, 36), mkrgb(84, 66, 52), sc), 0.4f + lit * 0.7f);
        if (sc < 0.25f) c = rgb_scale(mkrgb(22, 18, 16), 0.8f + lit * 0.3f);
        break;
    }
    case TRUNK_OLD: {
        /* old twisted oak: deep furrows, strong contrast */
        float bark = fbm2((float)x * 0.45f + vnoise2(0, (float)y * 0.05f, ns) * 3.0f, (float)y * 0.07f, 4, ns);
        if (bark < 0.4f) c = rgb_scale(mkrgb(14, 12, 10), 0.8f + lit * 0.4f);
        else if (bark > 0.6f) c = rgb_scale(mkrgb(92, 80, 66), 0.45f + lit * 0.75f);
        else c = rgb_scale(mkrgb(56, 46, 38), 0.45f + lit * 0.75f);
        break;
    }
    case TRUNK_DEAD: {
        /* grey dried-out wood, long cracks, no bark */
        float bark = fbm2((float)x * 0.7f, (float)y * 0.05f, 3, ns);
        c = rgb_scale(bark > 0.5f ? mkrgb(96, 94, 88) : mkrgb(74, 72, 68), 0.4f + lit * 0.7f);
        if (bark < 0.33f) c = rgb_scale(mkrgb(26, 24, 22), 0.9f + lit * 0.3f);
        break;
    }
    default: {
        float bark = fbm2((float)x * 0.55f, (float)y * 0.09f, 3, ns);
        if (bark < 0.36f) c = rgb_scale(C_BARK_DARK, 0.8f + lit * 0.4f);
        else if (bark > 0.62f) c = rgb_scale(C_BARK_LIGHT, 0.45f + lit * 0.75f);
        else c = rgb_scale(C_BARK, 0.45f + lit * 0.75f);
        break;
    }
    }
    if (fine > 0.93f) c = rgb_add(c, -8);
    return c;
}

static void gen_trunk_kind(sprite_t *s, uint64_t seed, int kind)
{
    rng_t r = rng_seed(seed);
    /*                                 oak      birch    pine     dead     giant    beech    spruce   old      ivy */
    static const int wr[TRUNK_KINDS][2] = { {18, 26}, {10, 15}, {14, 20}, {14, 22}, {34, 46}, {16, 24}, {12, 18}, {24, 34}, {18, 26} };
    static const int fr[TRUNK_KINDS][2] = { {8, 14}, {3, 6}, {5, 9}, {6, 10}, {12, 18}, {10, 16}, {4, 8}, {12, 20}, {8, 14} };
    static const float moss_thr[TRUNK_KINDS] = { 0.78f, 0.95f, 0.88f, 9.0f, 0.74f, 0.86f, 0.9f, 0.7f, 0.8f };
    static const float hollow_p[TRUNK_KINDS] = { 0.8f, 0.1f, 0.3f, 0.9f, 0.95f, 0.5f, 0.2f, 0.95f, 0.6f };
    static const float wob_amp[TRUNK_KINDS] = { 3.0f, 2.0f, 1.5f, 3.0f, 3.5f, 2.0f, 1.2f, 8.0f, 3.0f };
    int W = rng_between(&r, wr[kind][0], wr[kind][1]);
    int H = 128;
    int flare = rng_between(&r, fr[kind][0], fr[kind][1]);
    int fw = W + flare * 2 + 4 + (int)(wob_amp[kind] * 2.0f);
    frame_t *f = sprite_add_frame(s, "base", fw, H, fw / 2, H - 1);
    uint32_t ns = rng_u32(&r);
    float cx = (float)fw / 2.0f;
    float hol_y = (float)H * rng_range(&r, 0.38f, 0.58f);
    float hol_x = cx + rng_range(&r, -W * 0.15f, W * 0.15f);
    float hol_rx = rng_range(&r, 2.8f, 4.0f), hol_ry = rng_range(&r, 5.0f, 7.5f);
    if (kind == TRUNK_GIANT) { hol_rx *= 1.6f; hol_ry *= 1.4f; }
    int has_hollow = rng_chance(&r, hollow_p[kind]);
    float moss_side = rng_chance(&r, 0.7f) ? -1.0f : 1.0f;

    for (int y = 0; y < H; y++) {
        float fromb = (float)(H - 1 - y);
        float fl = fromb < (float)flare * 1.6f ? powf(1.0f - fromb / ((float)flare * 1.6f), 2.2f) : 0.0f;
        float half = (float)W * 0.5f + fl * (float)flare
                   + (vnoise2(0.5f, (float)y * 0.06f, ns) - 0.5f) * 2.5f;
        float wob = (vnoise2(3.5f, (float)y * 0.04f, ns) - 0.5f) * wob_amp[kind];
        for (int x = 0; x < fw; x++) {
            float u = ((float)x - cx - wob) / half;     /* -1 … 1 across the trunk */
            if (u < -1.0f || u > 1.0f) continue;
            /* cylindrical shading, light from the right */
            float nz = sqrtf(fmaxf(0.0f, 1.0f - u * u));
            float lit = clampf(0.25f + 0.55f * (u * 0.55f + nz * 0.6f), 0.08f, 1.0f);
            float fine = hash2f(x, y, ns ^ 0xabu);
            rgb_t c = trunk_bark(kind, x, y, u, lit, ns, fromb);
            /* moss on the shaded side and near the ground */
            float moss = vnoise2((float)x * 0.18f, (float)y * 0.07f, ns ^ 0x3cu)
                       + (u * moss_side > 0.2f ? 0.22f : -0.1f) + (fromb < 18 ? 0.25f : 0.0f);
            if (moss > moss_thr[kind]) {
                rgb_t mc = hash2f(x, y, ns ^ 0x11u) > 0.6f ? C_MOSS_LIGHT : C_MOSS;
                c = rgb_scale(mc, 0.55f + lit * 0.6f);
            }
            /* lichens */
            if (vnoise2((float)x * 0.5f, (float)y * 0.35f, ns ^ 0x77u) > (kind == TRUNK_DEAD ? 0.78f : 0.86f) &&
                fine > 0.4f && kind != TRUNK_BIRCH)
                c = rgb_scale(mkrgb(110, 132, 124), 0.5f + lit * 0.5f);
            int z = (int)(nz * 5.0f) - 2;
            cv_set(f, x, y, c, z);
        }
    }
    /* old oak: burls (growths) with a spiral pattern */
    if (kind == TRUNK_OLD) {
        int nb = rng_between(&r, 2, 4);
        for (int i = 0; i < nb; i++) {
            float by = rng_range(&r, (float)H * 0.3f, (float)H * 0.85f);
            float side = rng_chance(&r, 0.5f) ? 1.0f : -1.0f;
            float bx = cx + side * (float)W * 0.42f, br = rng_range(&r, 3.0f, 5.0f);
            for (int yy = (int)(by - br); yy <= (int)(by + br); yy++)
                for (int xx = (int)(bx - br); xx <= (int)(bx + br); xx++) {
                    float dx = (float)xx - bx, dy = (float)yy - by, d = sqrtf(dx * dx + dy * dy);
                    if (d > br) continue;
                    float sw = sinf(d * 2.2f + atan2f(dy, dx) * 2.0f);
                    float lit = clampf(0.5f + dx / br * 0.3f - dy / br * 0.3f, 0.2f, 1.0f);
                    cv_force(f, xx, yy, rgb_scale(sw > 0.3f ? mkrgb(92, 80, 66) : mkrgb(40, 32, 26), 0.5f + lit * 0.6f),
                             3 + (int)((1.0f - d / br) * 2.0f));
                }
        }
    }
    /* spruce: small dry branch stubs along the whole length */
    if (kind == TRUNK_SPRUCE) {
        for (int y = 6; y < H - 20; y += rng_between(&r, 5, 11)) {
            float side = rng_chance(&r, 0.5f) ? 1.0f : -1.0f;
            float x0 = cx + side * (float)W * 0.45f;
            cv_line(f, x0, (float)y, x0 + side * rng_range(&r, 3, 7), (float)y + rng_range(&r, -1, 2), mkrgb(46, 38, 32), 1);
        }
    }
    /* ivy: climbing stems with leaves on the front of the trunk */
    if (kind == TRUNK_IVY) {
        int nv = rng_between(&r, 2, 4);
        for (int v = 0; v < nv; v++) {
            float x = cx + rng_range(&r, -W * 0.35f, W * 0.35f);
            int top = rng_between(&r, 0, H / 2);
            for (int y = H - 1; y > top; y--) {
                x += rng_range(&r, -0.7f, 0.7f);
                x = clampf(x, cx - W * 0.42f, cx + W * 0.42f);
                cv_set(f, (int)x, y, mkrgb(40, 44, 30), 4);
                if (y % 3 == 0) {
                    float side = (y / 3) % 2 ? 1.0f : -1.0f;
                    rgb_t lc = vary(mkrgb(36, 74, 46), &r, 0.15f);
                    cv_leaf(f, x + side * 1.6f, (float)y, 2.8f, 1.2f, side > 0 ? 0.4f : NF_PI - 0.4f,
                            rgb_mix(lc, C_MOON, 0.12f), rgb_scale(lc, 0.65f), 5);
                }
            }
        }
    }
    /* cracks: dark vertical lines */
    int ncr = rng_between(&r, 2, 5);
    for (int i = 0; i < ncr; i++) {
        float x = cx + rng_range(&r, -W * 0.35f, W * 0.35f);
        float y0 = rng_range(&r, 0, (float)H * 0.8f), len = rng_range(&r, 10, 30);
        for (float y = y0; y < y0 + len && y < H; y += 1.0f) {
            x += rng_range(&r, -0.6f, 0.6f);
            int ix = (int)floorf(x + 0.5f), iy = (int)y;
            if (cv_opaque(f, ix, iy)) {
                f->px[iy * f->w + ix].r = 10; f->px[iy * f->w + ix].g = 8; f->px[iy * f->w + ix].b = 8;
            }
        }
    }
    /* hollow */
    if (has_hollow) {
        for (int y = (int)(hol_y - hol_ry - 2); y <= (int)(hol_y + hol_ry + 2); y++)
            for (int x = (int)(hol_x - hol_rx - 2); x <= (int)(hol_x + hol_rx + 2); x++) {
                float dx = ((float)x - hol_x) / hol_rx, dy = ((float)y - hol_y) / hol_ry;
                float d = dx * dx + dy * dy;
                if (d <= 1.0f) cv_force(f, x, y, rgb_mix(mkrgb(2, 3, 3), mkrgb(12, 10, 9), d), -1);   /* interior just below the bark: it is not a hole all the way through */
                else if (d <= 1.7f && cv_opaque(f, x, y))
                    cv_force(f, x, y, rgb_scale(C_BARK_LIGHT, dy > 0 ? 0.9f : 0.55f), 3);
            }
        set_point(s, "hollow", (int)(hol_x - (float)f->ax), (int)(hol_y + hol_ry * 0.6f) - f->ay);
        /* centre of the opening: the eyes of the creature inside (owl) sit here */
        set_point(s, "hollow_eye", (int)(hol_x - (float)f->ax), (int)(hol_y - hol_ry * 0.15f) - f->ay);
        char sz[24];
        snprintf(sz, sizeof sz, "%d %d", (int)(hol_rx * 2.0f), (int)(hol_ry * 2.0f));
        sprite_set_prop(s, "hollow_size", sz);
    }
    /* bracket fungi on the side */
    /* woodpecker holes on a dead tree */
    if (kind == TRUNK_DEAD) {
        int nh = rng_between(&r, 2, 5);
        for (int i = 0; i < nh; i++) {
            int hx = (int)(cx + rng_range(&r, -W * 0.3f, W * 0.3f)), hy = rng_between(&r, 10, H - 30);
            cv_force(f, hx, hy, mkrgb(6, 6, 6), -5);
            cv_force(f, hx, hy + 1, mkrgb(10, 9, 8), -5);
        }
    }
    int nf = rng_between(&r, 0, kind == TRUNK_DEAD ? 5 : kind == TRUNK_BIRCH ? 1 : 3);
    for (int i = 0; i < nf; i++) {
        float side = rng_chance(&r, 0.5f) ? -1.0f : 1.0f;
        float y = rng_range(&r, (float)H * 0.3f, (float)H * 0.85f);
        float x = cx + side * ((float)W * 0.5f - 1.0f);
        int fwid = rng_between(&r, 4, 7);
        for (int k = 0; k < fwid; k++) {
            float t = (float)k / (float)fwid;
            int px = (int)(x + side * (float)k * 0.9f);
            int th = (int)(2.2f * sinf(NF_PI * (1.0f - t) * 0.5f + 0.5f)) + 1;
            for (int j = 0; j < th; j++)
                cv_force(f, px, (int)y + j, j == 0 ? mkrgb(150, 120, 86) : mkrgb(96, 74, 54), 4);
        }
    }
    frame_rimlight(f, C_MOON, 0.14f);

    /* climbing lanes: follow the waviness and flare of the trunk, bottom to top */
    static const float lanes3[] = { -0.5f, 0.0f, 0.5f }, lanes2[] = { -0.35f, 0.35f };
    const float *lanes = kind == TRUNK_BIRCH ? lanes2 : lanes3;
    int nl = kind == TRUNK_BIRCH ? 2 : 3;
    for (int k = 0; k < nl; k++) {
        char buf[384] = "", key[24];
        for (int y = H - 1; y >= 0; y -= 6) {
            float fromb = (float)(H - 1 - y);
            float fl = fromb < (float)flare * 1.6f ? powf(1.0f - fromb / ((float)flare * 1.6f), 2.2f) : 0.0f;
            float half = (float)W * 0.5f + fl * (float)flare + (vnoise2(0.5f, (float)y * 0.06f, ns) - 0.5f) * 2.5f;
            float wob = (vnoise2(3.5f, (float)y * 0.04f, ns) - 0.5f) * wob_amp[kind];
            path_add(buf, sizeof buf, f, cx + wob + lanes[k] * half, (float)y);
        }
        snprintf(key, sizeof key, "path.climb%d", k);
        sprite_set_prop(s, key, buf);
    }
    int16_t z0 = 0;
    sprite_add_clip(s, "idle", CLIP_LOOP, 1, 0, &z0, 1);
    char lane[32];
    snprintf(lane, sizeof lane, "%d %d", -W / 2 + 1, W / 2 - 1);
    sprite_set_prop(s, "climb", lane);
    char wv[16];
    snprintf(wv, sizeof wv, "%d", W);
    sprite_set_prop(s, "width", wv);
    sprite_set_prop(s, "branch", BRANCH_FOR[kind]);
}

static void gen_trunk(sprite_t *s, uint64_t seed) { gen_trunk_kind(s, seed, TRUNK_OAK); }
static void gen_trunk_birch(sprite_t *s, uint64_t seed) { gen_trunk_kind(s, seed, TRUNK_BIRCH); }
static void gen_trunk_pine(sprite_t *s, uint64_t seed) { gen_trunk_kind(s, seed, TRUNK_PINE); }
static void gen_trunk_dead(sprite_t *s, uint64_t seed) { gen_trunk_kind(s, seed, TRUNK_DEAD); }
static void gen_trunk_giant(sprite_t *s, uint64_t seed) { gen_trunk_kind(s, seed, TRUNK_GIANT); }
static void gen_trunk_beech(sprite_t *s, uint64_t seed) { gen_trunk_kind(s, seed, TRUNK_BEECH); }
static void gen_trunk_spruce(sprite_t *s, uint64_t seed) { gen_trunk_kind(s, seed, TRUNK_SPRUCE); }
static void gen_trunk_old(sprite_t *s, uint64_t seed) { gen_trunk_kind(s, seed, TRUNK_OLD); }
static void gen_trunk_ivy(sprite_t *s, uint64_t seed) { gen_trunk_kind(s, seed, TRUNK_IVY); }

/* ------------------------------------------------------------------ */
/* roots                                                               */
/* ------------------------------------------------------------------ */

static void gen_roots(sprite_t *s, uint64_t seed)
{
    rng_t r = rng_seed(seed);
    int w = rng_between(&r, 48, 64), h = rng_between(&r, 18, 24);
    frame_t *f = sprite_add_frame(s, "base", w, h, 4, 2);
    int n = rng_between(&r, 3, 5);
    for (int i = 0; i < n; i++) {
        /* roots start at different heights of the trunk base and spread out */
        float x = 2.0f + rng_range(&r, 0, 3), y = 2.0f + (float)i * ((float)h * 0.45f / (float)n);
        float a = 0.15f + (float)i / (float)n * 0.7f + rng_range(&r, -0.1f, 0.1f);
        float rad = rng_range(&r, 2.2f, 3.4f) * (1.0f - (float)i * 0.08f);
        float len = rng_range(&r, (float)w * 0.5f, (float)w * 0.95f) * (1.0f - (float)i * 0.1f);
        int z = (n - i) - 2 + rng_between(&r, 0, 1);
        rgb_t c = vary(C_BARK, &r, 0.12f);
        float t = 0;
        while (t < len) {
            float nx = x + cosf(a) * 2.0f, ny = y + sinf(a) * 2.0f;
            float nr = fmaxf(0.7f, rad * (1.0f - t / len * 0.85f));
            cv_capsule(f, x, y, nr + 0.25f, nx, ny, nr, c, z, 2);
            /* side root shoot */
            if (t > 6 && rng_chance(&r, 0.08f)) {
                float ba = a + rng_range(&r, 0.4f, 0.9f);
                cv_capsule(f, nx, ny, nr * 0.5f, nx + cosf(ba) * 5, ny + sinf(ba) * 5, 0.5f,
                           rgb_scale(c, 0.85f), z - 1, 1);
            }
            x = nx; y = ny; t += 2.0f;
            a += rng_range(&r, -0.22f, 0.26f);
            a = clampf(a, -0.15f, 1.25f);
        }
    }
    /* moss on top of the roots */
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            if (!cv_opaque(f, x, y) || cv_opaque(f, x, y - 1)) continue;
            if (vnoise2((float)x * 0.2f, (float)y * 0.2f, (uint32_t)seed) > 0.45f) {
                px_t *p = &f->px[y * w + x];
                rgb_t m = hash2f(x, y, 9) > 0.5f ? C_MOSS_LIGHT : C_MOSS;
                p->r = m.r; p->g = m.g; p->b = m.b;
                if (cv_opaque(f, x, y + 1) && hash2f(x, y, 10) > 0.4f) {
                    px_t *q = &f->px[(y + 1) * w + x];
                    q->r = C_MOSS.r; q->g = C_MOSS.g; q->b = C_MOSS.b;
                }
            }
        }
    frame_rimlight(f, C_MOON, 0.12f);
    int16_t z0 = 0;
    sprite_add_clip(s, "idle", CLIP_LOOP, 1, 0, &z0, 1);
}

/* ------------------------------------------------------------------ */
/* branch                                                              */
/* ------------------------------------------------------------------ */

enum { FOL_NONE, FOL_BROAD, FOL_SMALL, FOL_NEEDLE, FOL_DROOP, FOL_IVY };

typedef struct {
    rgb_t bark;            /* base bark colour of the branch */
    rgb_t mark;            /* markings (birch dashes, crevices, scales) */
    float mark_p;          /* marking density */
    float rmin, rmax;      /* thickness at the trunk */
    float a0, a1;          /* initial angle (- = up) */
    float drift;           /* bend per step (+ = droops) */
    int   wmin, wmax;
    int   forks;           /* max. number of side branches */
    int   foliage;
    rgb_t leaf;            /* leaf / needle colour */
    float leaf_p;          /* foliage density */
    int   moss;            /* moss on top */
} bstyle_t;

static bstyle_t branch_style(int kind)
{
    bstyle_t b;
    memset(&b, 0, sizeof b);
    b.bark = C_BARK; b.mark = C_BARK_DARK; b.mark_p = 0.12f;
    b.rmin = 2.6f; b.rmax = 3.6f; b.a0 = -0.1f; b.a1 = 0.15f; b.drift = 0.0f;
    b.wmin = 40; b.wmax = 64; b.forks = 2; b.foliage = FOL_BROAD; b.leaf = mkrgb(40, 80, 50);
    b.leaf_p = 0.14f; b.moss = 1;
    switch (kind) {
    case TRUNK_BIRCH:
        b.bark = mkrgb(168, 168, 158); b.mark = mkrgb(34, 30, 28); b.mark_p = 0.22f;
        b.rmin = 1.6f; b.rmax = 2.3f; b.a0 = -0.35f; b.a1 = -0.08f; b.forks = 3;
        b.foliage = FOL_SMALL; b.leaf = mkrgb(70, 110, 58); b.leaf_p = 0.25f; b.moss = 0; break;
    case TRUNK_PINE:
        b.bark = mkrgb(112, 68, 42); b.mark = mkrgb(48, 28, 18); b.mark_p = 0.18f;
        b.rmin = 2.0f; b.rmax = 2.8f; b.a0 = -0.08f; b.a1 = 0.1f; b.forks = 2;
        b.foliage = FOL_NEEDLE; b.leaf = mkrgb(26, 66, 44); b.leaf_p = 0.2f; b.moss = 0; break;
    case TRUNK_SPRUCE:
        b.bark = mkrgb(62, 50, 42); b.mark = mkrgb(26, 20, 16); b.mark_p = 0.2f;
        b.rmin = 1.5f; b.rmax = 2.1f; b.a0 = 0.12f; b.a1 = 0.3f; b.drift = 0.018f; b.forks = 1;
        b.foliage = FOL_DROOP; b.leaf = mkrgb(20, 52, 40); b.leaf_p = 0.6f; b.moss = 0; break;
    case TRUNK_DEAD:
        b.bark = mkrgb(84, 82, 78); b.mark = mkrgb(30, 28, 26); b.mark_p = 0.15f;
        b.rmin = 2.0f; b.rmax = 3.0f; b.a0 = -0.25f; b.a1 = 0.2f; b.forks = 4;
        b.foliage = FOL_NONE; b.moss = 0; break;
    case TRUNK_GIANT:
        b.rmin = 3.8f; b.rmax = 5.0f; b.wmin = 56; b.wmax = 84; b.forks = 3; b.leaf_p = 0.18f; break;
    case TRUNK_OLD:
        b.bark = mkrgb(56, 46, 38); b.mark = mkrgb(16, 14, 12); b.mark_p = 0.25f;
        b.rmin = 3.4f; b.rmax = 4.6f; b.a0 = -0.2f; b.a1 = 0.2f; b.drift = 0.0f; b.wmin = 48; b.wmax = 76;
        b.forks = 3; b.leaf = mkrgb(34, 66, 42); b.leaf_p = 0.1f; break;
    case TRUNK_BEECH:
        b.bark = mkrgb(108, 112, 108); b.mark = mkrgb(76, 80, 76); b.mark_p = 0.1f;
        b.rmin = 2.2f; b.rmax = 3.0f; b.a0 = -0.28f; b.a1 = -0.02f; b.forks = 2;
        b.leaf = mkrgb(62, 92, 46); b.leaf_p = 0.18f; break;
    case TRUNK_IVY:
        b.foliage = FOL_IVY; b.leaf = mkrgb(34, 72, 46); b.leaf_p = 0.4f; break;
    default: break;
    }
    return b;
}

/* foliage at the end of a twig / along the branch */
static void foliage_at(frame_t *f, const bstyle_t *st, rng_t *r, float x, float y, float a, float size)
{
    rgb_t lc = vary(st->leaf, r, 0.15f);
    rgb_t ll = rgb_mix(lc, C_MOON, 0.12f), ld = rgb_scale(lc, 0.62f);
    switch (st->foliage) {
    case FOL_BROAD:
        for (int k = 0; k < (int)(3 * size) + 2; k++)
            cv_leaf(f, x + rng_range(r, -2.5f, 2.5f) * size, y + rng_range(r, -2.0f, 1.5f) * size, 3.4f, 1.1f,
                    rng_range(r, 0, NF_PI * 2), ll, ld, rng_between(r, 0, 2));
        break;
    case FOL_SMALL:     /* birch: small leaves hanging on thin twigs */
        for (int k = 0; k < (int)(5 * size) + 2; k++) {
            float lx = x + rng_range(r, -3, 3), ly = y + rng_range(r, -1, 4);
            cv_line(f, x, y, lx, ly - 1, rgb_scale(st->bark, 0.5f), 0);
            cv_leaf(f, lx, ly, 2.2f, 0.8f, NF_PI * 0.5f + rng_range(r, -0.5f, 0.5f), ll, ld, 1);
        }
        break;
    case FOL_NEEDLE:    /* pine: a fan of needles at the end */
        for (int k = 0; k < 9; k++) {
            float na = a - 1.4f + (float)k * 0.35f + rng_range(r, -0.1f, 0.1f);
            float nl = rng_range(r, 2.0f, 3.6f) * size;
            cv_line(f, x, y, x + cosf(na) * nl, y + sinf(na) * nl, k % 2 ? ld : ll, 1);
        }
        break;
    case FOL_DROOP:     /* spruce: a curtain of needles hanging from the branch */
        for (int k = 0; k < 3; k++) {
            float hx = x + rng_range(r, -1.5f, 1.5f), hl = rng_range(r, 2, 5) * size;
            cv_line(f, hx, y, hx + rng_range(r, -0.8f, 0.8f), y + hl, k % 2 ? ld : lc, 1);
            cv_set(f, (int)hx, (int)y - 1, ll, 1);
        }
        break;
    case FOL_IVY:
        cv_leaf(f, x + rng_range(r, -1.5f, 1.5f), y + rng_range(r, -1.0f, 1.5f), 2.8f, 1.2f,
                rng_range(r, 0, NF_PI * 2), ll, ld, 3);
        break;
    default:            /* dead: lichen */
        if (rng_chance(r, 0.5f)) cv_set(f, (int)x, (int)y, mkrgb(120, 140, 128), 1);
        break;
    }
}

/* one limb (main or side): a chain of capsules with tapering */
static void branch_limb(frame_t *f, const bstyle_t *st, rng_t *r, float x, float y, float a, float rad, float len,
                        int depth, char *walk, char *hangp, int *nw, sprite_t *s, int *perch)
{
    rgb_t c = vary(st->bark, r, 0.08f);
    float t = 0;
    while (t < len) {
        float nx = x + cosf(a) * 2.0f, ny = y + sinf(a) * 2.0f;
        float rr = fmaxf(0.5f, rad * (1.0f - t / len * 0.78f));
        if (depth == 0 && ((int)t % 6) == 0 && t < len - 8 && *nw < 30) {
            path_add(walk, 384, f, nx, ny - rr - 0.5f);
            if (t > 6) path_add(hangp, 384, f, nx, ny + rr + 0.5f);
            (*nw)++;
        }
        cv_capsule(f, x, y, rr + 0.2f, nx, ny, rr, c, depth ? -1 : 0, 2);
        /* side limb */
        if (depth == 0 && t > len * 0.2f && t < len * 0.8f && st->forks > 0 && rng_chance(r, 0.07f * (float)st->forks)) {
            float side = rng_chance(r, 0.6f) ? -1.0f : 1.0f;      /* more often upward */
            branch_limb(f, st, r, nx, ny, a + side * rng_range(r, 0.45f, 0.9f), rr * 0.6f,
                        rng_range(r, len * 0.2f, len * 0.45f), 1, walk, hangp, nw, s, perch);
        }
        /* twig + foliage */
        if (rng_chance(r, depth ? 0.18f : 0.12f)) {
            float ta = a + (rng_chance(r, 0.5f) ? -1.0f : 1.0f) * rng_range(r, 0.5f, 1.1f);
            float tl = rng_range(r, 3, 8);
            float ex = nx + cosf(ta) * tl, ey = ny + sinf(ta) * tl;
            cv_line(f, nx, ny, ex, ey, rgb_scale(c, 0.8f), rng_between(r, -2, 2));
            if (rng_float(r) < st->leaf_p * 4.0f) foliage_at(f, st, r, ex, ey, ta, 1.0f);
        }
        if (st->foliage == FOL_DROOP || st->foliage == FOL_IVY)
            if (rng_float(r) < st->leaf_p) foliage_at(f, st, r, nx, ny + rr, a, 1.0f);
        if (depth == 0 && t > 8 && *perch < 3 && rng_chance(r, 0.1f)) {
            char pname[24];
            snprintf(pname, sizeof pname, "perch%d", (*perch)++);
            set_point(s, pname, (int)nx - f->ax, (int)(ny - rr) - f->ay);
        }
        x = nx; y = ny; t += 2.0f;
        a += rng_range(r, -0.1f, 0.1f) + st->drift;
        a = clampf(a, -0.7f, 0.8f);
    }
    if (st->foliage != FOL_NONE) foliage_at(f, st, r, x, y, a, depth ? 1.0f : 1.3f);
}

static void gen_branch_kind(sprite_t *s, uint64_t seed, int kind)
{
    rng_t r = rng_seed(seed);
    bstyle_t st = branch_style(kind);
    int w = rng_between(&r, st.wmin, st.wmax), h = 40;
    frame_t *f = sprite_add_frame(s, "base", w, h, 0, 16);
    int perch = 0, nw = 0;
    char walk[384] = "", hangp[384] = "";
    branch_limb(f, &st, &r, 0, 16, rng_range(&r, st.a0, st.a1), rng_range(&r, st.rmin, st.rmax), (float)w - 6,
                0, walk, hangp, &nw, s, &perch);
    /* bark markings: birch dashes, crevices, scales */
    uint32_t ns = rng_u32(&r);
    for (int yy = 0; yy < h; yy++)
        for (int xx = 0; xx < w; xx++) {
            px_t *p = &f->px[yy * w + xx];
            if (p->a != 255 || p->g > p->r + 10) continue;          /* wood only, not foliage */
            float n = kind == TRUNK_BIRCH ? vnoise2((float)xx * 1.1f, (float)yy * 0.35f, ns) : hash2f(xx, yy, ns);
            if (n > 1.0f - st.mark_p) { p->r = st.mark.r; p->g = st.mark.g; p->b = st.mark.b; }
        }
    /* moss on top (or lichen on dead wood) */
    if (st.moss || kind == TRUNK_DEAD)
        for (int yy = 0; yy < h; yy++)
            for (int xx = 0; xx < w; xx++) {
                px_t *p = &f->px[yy * w + xx];
                if (p->a != 255 || cv_opaque(f, xx, yy - 1) || p->g > p->r + 10) continue;
                if (vnoise2((float)xx * 0.25f, 1.0f, (uint32_t)seed) > (kind == TRUNK_DEAD ? 0.7f : 0.5f)) {
                    rgb_t m = kind == TRUNK_DEAD ? mkrgb(120, 138, 126) : C_MOSS_LIGHT;
                    p->r = m.r; p->g = m.g; p->b = m.b;
                }
            }
    frame_rimlight(f, C_MOON, 0.15f);
    if (nw >= 2) sprite_set_prop(s, "path.walk0", walk);
    if (nw >= 3) sprite_set_prop(s, "path.hang0", hangp);
    int16_t z0 = 0;
    sprite_add_clip(s, "idle", CLIP_LOOP, 1, 0, &z0, 1);
}

static void gen_branch(sprite_t *s, uint64_t seed) { gen_branch_kind(s, seed, TRUNK_OAK); }
static void gen_branch_birch(sprite_t *s, uint64_t seed) { gen_branch_kind(s, seed, TRUNK_BIRCH); }
static void gen_branch_pine(sprite_t *s, uint64_t seed) { gen_branch_kind(s, seed, TRUNK_PINE); }
static void gen_branch_dead(sprite_t *s, uint64_t seed) { gen_branch_kind(s, seed, TRUNK_DEAD); }
static void gen_branch_giant(sprite_t *s, uint64_t seed) { gen_branch_kind(s, seed, TRUNK_GIANT); }
static void gen_branch_beech(sprite_t *s, uint64_t seed) { gen_branch_kind(s, seed, TRUNK_BEECH); }
static void gen_branch_spruce(sprite_t *s, uint64_t seed) { gen_branch_kind(s, seed, TRUNK_SPRUCE); }
static void gen_branch_old(sprite_t *s, uint64_t seed) { gen_branch_kind(s, seed, TRUNK_OLD); }
static void gen_branch_ivy(sprite_t *s, uint64_t seed) { gen_branch_kind(s, seed, TRUNK_IVY); }

/* ------------------------------------------------------------------ */
/* vine                                                                */
/* ------------------------------------------------------------------ */

static void gen_vine(sprite_t *s, uint64_t seed)
{
    rng_t r = rng_seed(seed);
    int len = rng_between(&r, 18, 38);
    int w = 22, h = len + 4;
    float ph = rng_range(&r, 0, 6.28f), freq = rng_range(&r, 0.15f, 0.3f);
    int nleaf = len / 3;
    float lt[16], ls[16];
    for (int i = 0; i < nleaf && i < 16; i++) {
        lt[i] = rng_range(&r, 0.1f, 1.0f);
        ls[i] = rng_chance(&r, 0.5f) ? -1.0f : 1.0f;
    }
    for (int i = 0; i < SWAY_FRAMES; i++) {
        float bend = sway_bend(i) * 4.0f;
        char name[16];
        snprintf(name, sizeof name, "bend%d", i);
        frame_t *f = sprite_add_frame(s, name, w, h, w / 2, 0);
        float px = (float)(w / 2), py = 0;
        for (int y = 0; y <= len; y++) {
            float t = (float)y / (float)len;
            float x = (float)(w / 2) + sinf(ph + (float)y * freq) * 1.5f + bend * t * t;
            cv_line(f, px, py, x, (float)y, mkrgb(38, 46, 30), 0);
            px = x; py = (float)y;
        }
        for (int k = 0; k < nleaf && k < 16; k++) {
            float t = lt[k];
            float y = t * (float)len;
            float x = (float)(w / 2) + sinf(ph + y * freq) * 1.5f + bend * t * t;
            cv_leaf(f, x + ls[k] * 2.0f, y + 1.0f, 4.0f, 1.3f, ls[k] > 0 ? 0.6f : 2.5f,
                    mkrgb(52, 90, 58), mkrgb(30, 60, 42), 1);
        }
        frame_rimlight(f, C_MOON, 0.12f);
    }
    add_sway_clips(s, 0);
}

/* ------------------------------------------------------------------ */
/* mushrooms                                                           */
/* ------------------------------------------------------------------ */

enum { MUSH_BROWN, MUSH_RED, MUSH_GLOW, MUSH_THIN, MUSH_KINDS };

static void mushroom_draw(frame_t *f, int x, int y, int kind, int size, rng_t *r, float glow)
{
    rgb_t stem = mkrgb(150, 142, 126), cap, capd;
    switch (kind) {
    case MUSH_RED:  cap = mkrgb(140, 44, 32); capd = mkrgb(84, 24, 20); break;
    case MUSH_GLOW: cap = mkrgb(90, 210, 180); capd = mkrgb(40, 120, 112); stem = mkrgb(110, 160, 142); break;
    case MUSH_THIN: cap = mkrgb(128, 116, 100); capd = mkrgb(80, 72, 64); break;
    default:        cap = mkrgb(120, 82, 50); capd = mkrgb(66, 44, 28); break;
    }
    int lean = rng_between(r, -1, 1);
    int sh = kind == MUSH_THIN ? size * 2 + 2 : size + 1;       /* stem height */
    float cw = kind == MUSH_THIN ? (float)size * 0.8f + 0.8f : (float)size * 0.95f + 1.0f;
    float ch = kind == MUSH_THIN ? cw * 0.9f : cw * (kind == MUSH_RED ? 0.75f : 0.62f);
    int z = rng_between(r, -1, 2);
    int sw = (kind == MUSH_THIN || size < 3) ? 1 : 2;
    for (int k = 0; k < sh; k++) {
        int sx = x + (k * lean) / NF_MAX(sh, 1);
        for (int q = 0; q < sw; q++)
            cv_set(f, sx + q, y - k, rgb_scale(stem, (q ? 0.62f : 0.85f) + 0.15f * (float)k / (float)sh), z);
    }
    float ccx = (float)(x + lean) + (sw == 2 ? 0.5f : 0.0f);
    int cy = y - sh;                       /* underside of the cap */
    /* underside of the cap (gills) */
    for (int i = (int)floorf(ccx - cw + 0.5f); i <= (int)ceilf(ccx + cw - 0.5f); i++)
        cv_set(f, i, cy, kind == MUSH_GLOW ? rgb_scale(capd, 0.8f + 0.4f * glow) : capd, z + 1);
    /* dome */
    for (int j = 1; j <= (int)ceilf(ch); j++) {
        float t = (float)j / (ch + 0.6f);
        float hw = cw * sqrtf(fmaxf(0.0f, 1.0f - t * t));
        for (int i = (int)floorf(ccx - hw + 0.5f); i <= (int)ceilf(ccx + hw - 0.5f); i++) {
            float u = ((float)i - ccx) / (cw + 0.01f);
            float lit = clampf(0.65f + u * 0.35f + t * 0.3f, 0.3f, 1.2f);
            rgb_t c = rgb_scale(cap, lit);
            if (kind == MUSH_RED && hash2f(i, cy - j, 5) > 0.74f && j < (int)ch) c = mkrgb(206, 200, 184);
            if (kind == MUSH_GLOW) cv_glow(f, i, cy - j, rgb_scale(c, 0.65f + 0.45f * glow), z + 1);
            else cv_set(f, i, cy - j, c, z + 1);
        }
    }
    if (kind == MUSH_GLOW) {
        rgb_t hc = mkrgb(70, 200, 170);
        float rad = cw + 2.0f + glow * 2.0f;
        int ir = (int)ceilf(rad);
        for (int dy = -ir; dy <= ir; dy++)
            for (int dx = -ir - 1; dx <= ir + 1; dx++) {
                float d = sqrtf((float)(dx * dx) + (float)(dy * dy) * 1.3f);
                if (d > rad) continue;
                int a = (int)((1.0f - d / rad) * (30.0f + 70.0f * glow));
                if (a > 4) cv_halo(f, (int)ccx + dx, cy - (int)(ch * 0.5f) + dy, hc, a);
            }
    }
}

static void gen_mushrooms_kind(sprite_t *s, uint64_t seed, int forced_kind)
{
    rng_t r = rng_seed(seed);
    int kind = forced_kind >= 0 ? forced_kind : rng_int(&r, MUSH_KINDS);
    int n = rng_between(&r, 1, kind == MUSH_THIN ? 6 : 4);
    int w = 26, h = 18;
    int xs[8], sz[8];
    for (int i = 0; i < n; i++) {
        xs[i] = w / 2 + rng_between(&r, -7, 7);
        sz[i] = kind == MUSH_THIN ? rng_between(&r, 1, 3) : rng_between(&r, 2, 5);
    }
    int nfr = kind == MUSH_GLOW ? 4 : 1;
    for (int k = 0; k < nfr; k++) {
        char name[16];
        snprintf(name, sizeof name, "f%d", k);
        frame_t *f = sprite_add_frame(s, name, w, h, w / 2, h - 1);
        rng_t rr = rng_seed(seed ^ 0x77u);
        for (int i = 0; i < n; i++)
            mushroom_draw(f, xs[i], h - 1, kind, sz[i], &rr, (float)k / 3.0f);
        if (kind != MUSH_GLOW) frame_rimlight(f, C_MOON, 0.1f);
    }
    if (kind == MUSH_GLOW) {
        int16_t seq[] = { 0, 1, 2, 3, 3, 2, 1, 0 };
        sprite_add_clip(s, "glow", CLIP_LOOP, 3, 0, seq, (int)NF_COUNT(seq));
        int16_t i1 = 1;
        sprite_add_clip(s, "idle", CLIP_LOOP, 1, 0, &i1, 1);
    } else {
        int16_t z0 = 0;
        sprite_add_clip(s, "idle", CLIP_LOOP, 1, 0, &z0, 1);
    }
}

static void gen_mushrooms(sprite_t *s, uint64_t seed) { gen_mushrooms_kind(s, seed, -1); }
static void gen_mushroom_glow(sprite_t *s, uint64_t seed) { gen_mushrooms_kind(s, seed, MUSH_GLOW); }
static void gen_mushroom_red(sprite_t *s, uint64_t seed) { gen_mushrooms_kind(s, seed, MUSH_RED); }

/* ------------------------------------------------------------------ */
/* stone                                                               */
/* ------------------------------------------------------------------ */

static void gen_stone(sprite_t *s, uint64_t seed)
{
    rng_t r = rng_seed(seed);
    int w = rng_between(&r, 9, 22), h = rng_between(&r, 6, 12);
    frame_t *f = sprite_add_frame(s, "base", w + 2, h + 1, (w + 2) / 2, h);
    uint32_t ns = rng_u32(&r);
    rgb_t base = vary(mkrgb(58, 62, 64), &r, 0.1f);
    float cx = (float)(w + 2) / 2.0f, cy = (float)h;
    for (int y = 0; y <= h; y++)
        for (int x = 0; x < w + 2; x++) {
            float dx = ((float)x - cx) / ((float)w * 0.5f), dy = ((float)y - cy) / (float)h;
            float d = dx * dx + dy * dy + (vnoise2((float)x * 0.4f, (float)y * 0.4f, ns) - 0.5f) * 0.3f;
            if (d > 1.0f || y > h) continue;
            float lit = clampf(0.5f + dx * 0.3f - dy * 0.5f, 0.2f, 1.1f);
            rgb_t c = rgb_scale(base, 0.45f + lit * 0.55f);
            if (hash2f(x, y, ns) > 0.9f) c = rgb_add(c, -10);
            /* moss on top */
            if (dy < -0.55f && vnoise2((float)x * 0.3f, (float)y * 0.3f, ns ^ 3u) > 0.45f)
                c = rgb_scale(C_MOSS_LIGHT, 0.6f + lit * 0.3f);
            cv_set(f, x, y, c, (int)((1.0f - d) * 3.0f));
        }
    frame_rimlight(f, C_MOON, 0.2f);
    int16_t z0 = 0;
    sprite_add_clip(s, "idle", CLIP_LOOP, 1, 0, &z0, 1);
}

/* ------------------------------------------------------------------ */
/* fallen log                                                          */
/* ------------------------------------------------------------------ */

enum { LOG_PLAIN, LOG_MOSSY, LOG_BIRCH, LOG_HOLLOW, LOG_ROTTEN };

static void gen_log_kind(sprite_t *s, uint64_t seed, int kind)
{
    rng_t r = rng_seed(seed);
    int w = rng_between(&r, 40, 72), rad = rng_between(&r, 5, kind == LOG_BIRCH ? 6 : 8);
    int h = rad * 2 + 6;
    frame_t *f = sprite_add_frame(s, "base", w, h, w / 2, h - 1);
    uint32_t ns = rng_u32(&r);
    float cy = (float)(h - 1 - rad);
    float moss_amt = kind == LOG_MOSSY ? 0.15f : kind == LOG_ROTTEN ? 0.35f : kind == LOG_BIRCH ? 0.75f : 0.42f;
    for (int y = 0; y < h; y++)
        for (int x = 3; x < w; x++) {
            /* slightly curved, thinner towards the end */
            float t = (float)x / (float)w;
            float rr = (float)rad * (1.0f - t * 0.18f);
            float sag = sinf(t * NF_PI) * 0.8f;
            float v = ((float)y - cy - sag + (1.0f - rr / (float)rad) * (float)rad) / rr;
            if (v < -1.0f || v > 1.0f) continue;
            float nz = sqrtf(1.0f - v * v);
            float lit = clampf(0.3f + 0.6f * (-v * 0.7f + nz * 0.4f), 0.1f, 1.0f);
            rgb_t c;
            if (kind == LOG_BIRCH) {
                c = rgb_scale(mkrgb(170, 170, 160), 0.35f + lit * 0.65f);
                if (vnoise2((float)x * 1.2f, (float)y * 0.25f, ns) > 0.72f) c = rgb_scale(mkrgb(36, 32, 30), 0.8f + lit * 0.3f);
            } else {
                float bark = fbm2((float)x * 0.09f, (float)y * 0.6f, 3, ns);
                c = bark < 0.38f ? C_BARK_DARK : bark > 0.6f ? C_BARK_LIGHT : C_BARK;
                if (kind == LOG_ROTTEN) c = rgb_mix(c, mkrgb(40, 30, 24), 0.4f);
                c = rgb_scale(c, 0.5f + lit * 0.7f);
            }
            if (v < -0.2f - (1.0f - moss_amt) * 0.6f &&
                vnoise2((float)x * 0.15f, 0.0f, ns ^ 9u) > moss_amt)
                c = rgb_scale(hash2f(x, y, ns) > 0.5f ? C_MOSS_LIGHT : C_MOSS, 0.6f + lit * 0.5f);
            /* rotten wood: holes and crumbly edges */
            if (kind == LOG_ROTTEN && vnoise2((float)x * 0.3f, (float)y * 0.3f, ns ^ 5u) > 0.7f) {
                if (fabsf(v) > 0.7f) continue;
                c = rgb_scale(mkrgb(70, 48, 30), 0.4f + lit * 0.5f);
            }
            cv_set(f, x, y, c, (int)(nz * 4.0f) - 1);
        }
    /* log end: growth rings or a hollow */
    for (int y = 0; y < h; y++)
        for (int x = 0; x < 9; x++) {
            float dx = ((float)x - 4.0f) / 3.4f, dy = ((float)y - cy) / (float)rad;
            float d = sqrtf(dx * dx + dy * dy);
            if (d > 1.0f) continue;
            rgb_t c;
            int z = 4;
            if (kind == LOG_HOLLOW && d < 0.78f) { c = rgb_mix(mkrgb(2, 3, 3), mkrgb(14, 11, 9), d); z = -8; }
            else if (d > 0.82f) c = kind == LOG_BIRCH ? mkrgb(150, 150, 140) : C_BARK_DARK;
            else c = ((int)(d * 5.0f)) % 2 ? mkrgb(98, 82, 62) : mkrgb(122, 104, 78);
            if (kind == LOG_ROTTEN) c = rgb_scale(c, 0.7f);
            cv_force(f, x, y, c, z);
        }
    /* broken branch stubs sticking up */
    int nst = kind == LOG_ROTTEN ? 0 : rng_between(&r, 0, 2);
    for (int i = 0; i < nst; i++) {
        float x0 = rng_range(&r, (float)w * 0.3f, (float)w * 0.9f);
        float y0 = cy - (float)rad + 1.0f;
        float a = -NF_PI * 0.5f + rng_range(&r, -0.6f, 0.6f);
        float l = rng_range(&r, 3, 6);
        cv_capsule(f, x0, y0, 1.3f, x0 + cosf(a) * l, y0 + sinf(a) * l, 0.7f,
                   kind == LOG_BIRCH ? mkrgb(150, 150, 140) : C_BARK, 1, 1);
    }
    /* mushrooms on a rotten / mossy log */
    int nm = kind == LOG_ROTTEN ? rng_between(&r, 3, 6) : kind == LOG_MOSSY ? rng_between(&r, 0, 3) : 0;
    rng_t mr = rng_seed(seed ^ 0x6d75u);
    for (int i = 0; i < nm; i++) {
        int x = rng_between(&r, 10, w - 4);
        int y = (int)(cy - (float)rad) + 1;
        mushroom_draw(f, x, y, rng_chance(&r, 0.3f) ? MUSH_GLOW : MUSH_THIN, rng_between(&r, 1, 2), &mr, 0.6f);
    }
    if (kind == LOG_HOLLOW) {
        set_point(s, "entry", 4 - w / 2, 0);
        sprite_set_prop(s, "peeks", "0");
        sprite_set_prop(s, "z.inside", "-6");
    } else {
        set_point(s, "under", rng_between(&r, -w / 4, w / 4), 0);
    }
    set_point(s, "top", 0, -2 * rad - 1);
    frame_rimlight(f, C_MOON, 0.15f);
    export_top_path(s, f, "path.walk0", 6, w - 3, 5);
    int16_t z0 = 0;
    sprite_add_clip(s, "idle", CLIP_LOOP, 1, 0, &z0, 1);
}

static void gen_log(sprite_t *s, uint64_t seed) { gen_log_kind(s, seed, LOG_PLAIN); }
static void gen_log_mossy(sprite_t *s, uint64_t seed) { gen_log_kind(s, seed, LOG_MOSSY); }
static void gen_log_birch(sprite_t *s, uint64_t seed) { gen_log_kind(s, seed, LOG_BIRCH); }
static void gen_log_hollow(sprite_t *s, uint64_t seed) { gen_log_kind(s, seed, LOG_HOLLOW); }
static void gen_log_rotten(sprite_t *s, uint64_t seed) { gen_log_kind(s, seed, LOG_ROTTEN); }

/* ------------------------------------------------------------------ */
/* big leaves (burdock)                                                */
/* ------------------------------------------------------------------ */

static void gen_broadleaf_scaled(sprite_t *s, uint64_t seed, float scale)
{
    rng_t r = rng_seed(seed);
    int n = rng_between(&r, 3, 6);
    float ang[8], len[8], st[8];
    int zz[8];
    for (int i = 0; i < n; i++) {
        float u = n > 1 ? (float)i / (float)(n - 1) : 0.5f;
        ang[i] = -NF_PI * (0.85f - 0.7f * u) + rng_range(&r, -0.15f, 0.15f);
        len[i] = rng_range(&r, 9, 15) * scale;
        st[i] = rng_range(&r, 4, 9) * scale;
        zz[i] = rng_between(&r, -2, 4);
    }
    int w = (int)(52 * scale), h = (int)(30 * scale);
    for (int fi = 0; fi < SWAY_FRAMES; fi++) {
        float bend = sway_bend(fi) * 1.6f * scale;
        char name[16];
        snprintf(name, sizeof name, "bend%d", fi);
        frame_t *f = sprite_add_frame(s, name, w, h, w / 2, h - 1);
        for (int i = 0; i < n; i++) {
            float x0 = (float)(w / 2), y0 = (float)(h - 1);
            float sx = x0 + cosf(ang[i]) * st[i] + bend * 0.6f, sy = y0 + sinf(ang[i]) * st[i];
            cv_line(f, x0, y0, sx, sy, mkrgb(34, 62, 40), zz[i]);
            float la = ang[i] + 0.35f * (cosf(ang[i]) > 0 ? 1.0f : -1.0f);
            float lcx = sx + cosf(la) * len[i] * 0.5f + bend, lcy = sy + sinf(la) * len[i] * 0.5f + 1.0f;
            cv_leaf(f, lcx, lcy, len[i], len[i] * 0.3f, la, mkrgb(46, 92, 58), mkrgb(26, 58, 40), zz[i]);
            /* vein */
            cv_line(f, sx, sy, sx + cosf(la) * len[i] * 0.9f + bend, sy + sinf(la) * len[i] * 0.9f + 1.0f,
                    mkrgb(72, 112, 72), zz[i]);
        }
        frame_rimlight(f, C_MOON, 0.14f);
    }
    add_sway_clips(s, 0);
}

static void gen_broadleaf(sprite_t *s, uint64_t seed) { gen_broadleaf_scaled(s, seed, 1.0f); }
static void gen_broadleaf_big(sprite_t *s, uint64_t seed) { gen_broadleaf_scaled(s, seed, 1.8f); }

/* ------------------------------------------------------------------ */
/* litter (ground: leaves, twigs, pebbles)                             */
/* ------------------------------------------------------------------ */

static void gen_litter(sprite_t *s, uint64_t seed)
{
    rng_t r = rng_seed(seed);
    int w = rng_between(&r, 40, 70), h = 8;
    frame_t *f = sprite_add_frame(s, "base", w, h, w / 2, h - 1);
    uint32_t ns = rng_u32(&r);
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            float edge = fminf((float)x, (float)(w - 1 - x)) / 6.0f;
            float top = 3.0f - fminf(edge, 1.0f) * 2.0f + vnoise2((float)x * 0.3f, 0, ns) * 2.0f;
            if ((float)y < top) continue;
            float n = hash2f(x, y, ns);
            rgb_t c = rgb_scale(C_SOIL, 0.8f + n * 0.5f);
            cv_set(f, x, y, c, -2);
        }
    int nl = w / 2;
    static const rgb_t lc[] = { {78, 52, 30}, {96, 70, 36}, {60, 44, 30}, {70, 64, 34}, {40, 52, 30} };
    for (int i = 0; i < nl; i++) {
        float x = rng_range(&r, 2, (float)w - 2), y = rng_range(&r, 3, (float)h - 1);
        rgb_t c = rgb_scale(lc[rng_int(&r, (int)NF_COUNT(lc))], rng_range(&r, 0.55f, 0.95f));
        cv_leaf(f, x, y, rng_range(&r, 2.5f, 4.0f), 0.9f, rng_range(&r, -0.4f, 0.4f), c, rgb_scale(c, 0.7f),
                rng_between(&r, -1, 1));
    }
    int nt = rng_between(&r, 1, 4);
    for (int i = 0; i < nt; i++) {
        float x = rng_range(&r, 0, (float)w - 8), y = rng_range(&r, 4, (float)h - 1);
        cv_line(f, x, y, x + rng_range(&r, 4, 10), y + rng_range(&r, -1.5f, 1.5f), mkrgb(56, 44, 34), 1);
    }
    int np = rng_between(&r, 2, 6);
    for (int i = 0; i < np; i++) {
        int x = rng_between(&r, 1, w - 2), y = rng_between(&r, 5, h - 1);
        cv_set(f, x, y, mkrgb(70, 72, 74), 1);
        cv_set(f, x + 1, y, mkrgb(48, 50, 52), 1);
    }
    int16_t z0 = 0;
    sprite_add_clip(s, "idle", CLIP_LOOP, 1, 0, &z0, 1);
}

/* ------------------------------------------------------------------ */
/* sprouts / clover                                                    */
/* ------------------------------------------------------------------ */

static void gen_sprouts(sprite_t *s, uint64_t seed)
{
    rng_t r = rng_seed(seed);
    int w = 16, h = 10;
    int n = rng_between(&r, 2, 5);
    float xs[6], hs[6];
    int clover = rng_chance(&r, 0.5f);
    for (int i = 0; i < n; i++) { xs[i] = rng_range(&r, 3, (float)w - 3); hs[i] = rng_range(&r, 3, 7); }
    for (int fi = 0; fi < SWAY_FRAMES; fi++) {
        float bend = sway_bend(fi);
        char name[16];
        snprintf(name, sizeof name, "bend%d", fi);
        frame_t *f = sprite_add_frame(s, name, w, h, w / 2, h - 1);
        for (int i = 0; i < n; i++) {
            float tx = xs[i] + bend, ty = (float)(h - 1) - hs[i];
            cv_line(f, xs[i], (float)(h - 1), tx, ty, mkrgb(40, 70, 44), 0);
            if (clover) {
                cv_disc(f, tx - 1, ty, 1.0f, mkrgb(46, 96, 60), 1);
                cv_disc(f, tx + 1, ty, 1.0f, mkrgb(40, 84, 54), 1);
                cv_set(f, (int)tx, (int)ty - 1, mkrgb(60, 110, 70), 1);
            } else {
                cv_leaf(f, tx - 1.5f, ty, 3.0f, 0.9f, NF_PI * 0.85f, mkrgb(60, 106, 64), mkrgb(36, 70, 44), 1);
                cv_leaf(f, tx + 1.5f, ty, 3.0f, 0.9f, NF_PI * 0.15f, mkrgb(60, 106, 64), mkrgb(36, 70, 44), 1);
            }
        }
        frame_rimlight(f, C_MOON, 0.12f);
    }
    add_sway_clips(s, 0);
}

/* ------------------------------------------------------------------ */
/* spider web                                                          */
/* ------------------------------------------------------------------ */

static void gen_web(sprite_t *s, uint64_t seed)
{
    rng_t r = rng_seed(seed);
    int w = rng_between(&r, 14, 22), h = rng_between(&r, 12, 18);
    frame_t *f = sprite_add_frame(s, "base", w, h, 0, 0);
    rgb_t c = mkrgb(150, 170, 176);
    float cx = (float)w * 0.45f, cy = (float)h * 0.45f;
    int spokes = rng_between(&r, 6, 9);
    for (int i = 0; i < spokes; i++) {
        float a = (float)i / (float)spokes * NF_PI * 2.0f + rng_range(&r, -0.2f, 0.2f);
        float len = rng_range(&r, 0.8f, 1.1f) * (float)NF_MAX(w, h);
        for (float t = 0; t < len; t += 0.7f) {
            int x = (int)(cx + cosf(a) * t), y = (int)(cy + sinf(a) * t);
            if (x >= 0 && y >= 0 && x < w && y < h) cv_halo(f, x, y, c, 34);
        }
    }
    for (float rr = 2.0f; rr < (float)NF_MAX(w, h) * 0.6f; rr += rng_range(&r, 1.6f, 2.4f))
        for (float a = 0; a < NF_PI * 2.0f; a += 0.6f / rr) {
            int x = (int)(cx + cosf(a) * rr), y = (int)(cy + sinf(a) * rr * 0.9f);
            if (x >= 0 && y >= 0 && x < w && y < h) cv_halo(f, x, y, c, 22);
        }
    /* dew drops */
    for (int i = 0; i < 4; i++) {
        int x = rng_between(&r, 1, w - 2), y = rng_between(&r, 1, h - 2);
        if (f->px[y * w + x].a) cv_glow(f, x, y, mkrgb(190, 210, 214), 1);
    }
    int16_t z0 = 0;
    sprite_add_clip(s, "idle", CLIP_LOOP, 1, 0, &z0, 1);
}

/* ------------------------------------------------------------------ */
/* firefly (a creature, but generated)                                 */
/* ------------------------------------------------------------------ */

static void gen_firefly(sprite_t *s, uint64_t seed)
{
    rng_t r = rng_seed(seed);
    rgb_t core = vary(mkrgb(250, 232, 120), &r, 0.06f);
    rgb_t halo = mkrgb(200, 220, 90);
    static const float lv[] = { 0.0f, 0.35f, 0.7f, 1.0f };
    for (int k = 0; k < 4; k++) {
        char name[16];
        snprintf(name, sizeof name, "glow%d", k);
        frame_t *f = sprite_add_frame(s, name, 9, 9, 4, 4);
        float l = lv[k];
        if (l <= 0.01f) {
            cv_set(f, 4, 4, mkrgb(46, 44, 30), 0);
            continue;
        }
        cv_glow(f, 4, 4, rgb_mix(mkrgb(90, 86, 40), core, l), 0);
        for (int dy = -4; dy <= 4; dy++)
            for (int dx = -4; dx <= 4; dx++) {
                float d = sqrtf((float)(dx * dx + dy * dy));
                if (d < 0.5f || d > 1.0f + 3.0f * l) continue;
                int a = (int)((1.0f - d / (1.0f + 3.0f * l)) * 150.0f * l);
                if (a > 3) cv_halo(f, 4 + dx, 4 + dy, halo, a);
            }
    }
    int16_t blink[] = { 0, 0, 0, 0, 0, 0, 1, 2, 3, 3, 3, 3, 2, 3, 3, 2, 1 };
    int16_t on = 3, off = 0;
    sprite_add_clip(s, "blink", CLIP_LOOP, 8, 0, blink, (int)NF_COUNT(blink));
    sprite_add_clip(s, "on", CLIP_LOOP, 1, 0, &on, 1);
    sprite_add_clip(s, "off", CLIP_LOOP, 1, 0, &off, 1);
    int16_t idle = 3;
    sprite_add_clip(s, "idle", CLIP_LOOP, 1, 0, &idle, 1);
    snprintf(s->tags, sizeof s->tags, "creature air tiny glow");
    sprite_set_prop(s, "move", "air");
    sprite_set_prop(s, "speed", "walk=6 run=14");
    sprite_set_prop(s, "anim", "idle=blink move=blink run=on");
    sprite_set_prop(s, "intents", "wander=6 hover=4 follow=1 leave=1");
    sprite_set_prop(s, "size", "tiny");
}

/* ================================================================== */
/* BACKGROUND AND ENVIRONMENT — more variants                       */
/* ================================================================== */

/* distant trunk: a dark silhouette for the back layers (fog darkens it further) */
static void gen_tree_far(sprite_t *s, uint64_t seed)
{
    rng_t r = rng_seed(seed);
    int W = rng_between(&r, 5, 12), H = 128;
    int fw = W + 10;
    frame_t *f = sprite_add_frame(s, "base", fw, H, fw / 2, H - 1);
    uint32_t ns = rng_u32(&r);
    rgb_t base = vary(mkrgb(34, 36, 36), &r, 0.15f);
    float cx = (float)fw / 2.0f;
    for (int y = 0; y < H; y++) {
        float fromb = (float)(H - 1 - y);
        float half = (float)W * 0.5f + (fromb < 10 ? (10 - fromb) * 0.35f : 0)
                   + (vnoise2(0.5f, (float)y * 0.05f, ns) - 0.5f) * 1.5f;
        float wob = (vnoise2(2.5f, (float)y * 0.03f, ns) - 0.5f) * 2.0f;
        for (int x = 0; x < fw; x++) {
            float u = ((float)x - cx - wob) / half;
            if (u < -1.0f || u > 1.0f) continue;
            float nz = sqrtf(1.0f - u * u);
            float lit = clampf(0.3f + 0.5f * (u * 0.5f + nz * 0.5f), 0.1f, 1.0f);
            float n = vnoise2((float)x * 0.6f, (float)y * 0.08f, ns);
            cv_set(f, x, y, rgb_scale(base, 0.5f + lit * 0.5f + (n - 0.5f) * 0.3f), (int)(nz * 2.0f));
        }
    }
    /* stub of a side branch */
    if (rng_chance(&r, 0.6f)) {
        float y = rng_range(&r, 10, 60), side = rng_chance(&r, 0.5f) ? 1.0f : -1.0f;
        cv_capsule(f, cx, y, 1.6f, cx + side * rng_range(&r, 3, 5), y - rng_range(&r, 2, 5), 0.8f, base, 1, 1);
    }
    frame_rimlight(f, C_MOON, 0.08f);
    int16_t z0 = 0;
    sprite_add_clip(s, "idle", CLIP_LOOP, 1, 0, &z0, 1);
}

/* stumps: cut (growth rings), broken (jagged top), birch, mossy giant, hollow, tall dead snag */
enum { STUMP_CUT, STUMP_BROKEN, STUMP_BIRCH, STUMP_MOSSY, STUMP_HOLLOW, STUMP_SNAG, STUMP_RANDOM };

static void gen_stump_kind(sprite_t *s, uint64_t seed, int kind)
{
    rng_t r = rng_seed(seed);
    if (kind == STUMP_RANDOM) kind = rng_chance(&r, 0.5f) ? STUMP_CUT : STUMP_BROKEN;
    int bark = kind == STUMP_BIRCH ? TRUNK_BIRCH : kind == STUMP_SNAG ? TRUNK_DEAD : kind == STUMP_MOSSY ? TRUNK_OLD : TRUNK_OAK;
    int W = kind == STUMP_MOSSY ? rng_between(&r, 26, 36) : kind == STUMP_BIRCH ? rng_between(&r, 10, 15)
          : kind == STUMP_SNAG ? rng_between(&r, 12, 18) : rng_between(&r, 14, 26);
    int H = kind == STUMP_SNAG ? rng_between(&r, 30, 56) : kind == STUMP_MOSSY ? rng_between(&r, 16, 24)
          : kind == STUMP_BIRCH ? rng_between(&r, 14, 24) : rng_between(&r, 10, 22);
    int cut = kind == STUMP_CUT || kind == STUMP_HOLLOW || kind == STUMP_MOSSY || (kind == STUMP_BIRCH && rng_chance(&r, 0.5f));
    float moss_t = kind == STUMP_MOSSY ? 0.45f : kind == STUMP_SNAG ? 9.0f : kind == STUMP_BIRCH ? 0.85f : 0.72f;
    int fw = W + 14, fh = H + 8;
    frame_t *f = sprite_add_frame(s, "base", fw, fh, fw / 2, fh - 1);
    uint32_t ns = rng_u32(&r);
    float cx = (float)fw / 2.0f;
    int top = fh - 1 - H;
    for (int y = top - 2; y < fh; y++) {
        float fromb = (float)(fh - 1 - y);
        float half = (float)W * 0.5f + (fromb < 6 ? (6 - fromb) * 1.0f : 0);
        for (int x = 0; x < fw; x++) {
            float u = ((float)x - cx) / half;
            if (u < -1.0f || u > 1.0f) continue;
            if (y < top) continue;
            if (!cut) {
                float jag = hash2f(x / 2, 0, ns) * (kind == STUMP_SNAG ? 12.0f : kind == STUMP_BIRCH ? 4.0f : 6.0f)
                          + (fabsf(u) > 0.6f ? 2.0f : 0.0f);
                if ((float)(y - top) < jag) continue;
            }
            float nz = sqrtf(1.0f - u * u);
            float lit = clampf(0.25f + 0.55f * (u * 0.55f + nz * 0.6f), 0.08f, 1.0f);
            /* do not darken birch near the ground (a stump is just the bottom of a trunk, same pattern as above) */
            rgb_t c = trunk_bark(bark, x, y, u, lit, ns, fromb + (bark == TRUNK_BIRCH ? 40.0f : 0.0f));
            float moss = vnoise2((float)x * 0.2f, (float)y * 0.2f, ns ^ 7u) + (fromb < 6 ? 0.25f : 0)
                       + (kind == STUMP_MOSSY && y - top < 5 ? 0.3f : 0);
            if (moss > moss_t)
                c = rgb_scale(hash2f(x, y, 3) > 0.5f ? C_MOSS_LIGHT : C_MOSS, 0.6f + lit * 0.5f);
            cv_set(f, x, y, c, (int)(nz * 4.0f) - 1);
        }
    }
    if (cut) {
        /* top surface: growth rings, or a dark hollow */
        float ry = 2.2f + (float)W * 0.03f;
        for (int y = top - 3; y <= top + 3; y++)
            for (int x = 0; x < fw; x++) {
                float dx = ((float)x - cx) / ((float)W * 0.5f), dy = ((float)y - (float)top) / ry;
                float d = sqrtf(dx * dx + dy * dy);
                if (d > 1.0f) continue;
                rgb_t c;
                int z = 3;
                if (kind == STUMP_HOLLOW && d < 0.72f) { c = rgb_mix(mkrgb(4, 4, 4), mkrgb(20, 14, 10), d); z = -8; }
                else if (d > 0.86f) c = kind == STUMP_BIRCH ? mkrgb(160, 160, 150) : C_BARK_DARK;
                else c = ((int)(d * 6.0f)) % 2 ? mkrgb(104, 86, 64) : mkrgb(128, 108, 80);
                if (kind == STUMP_MOSSY && hash2f(x, y, ns) > 0.45f) c = rgb_scale(C_MOSS_LIGHT, 0.8f);
                cv_force(f, x, y, c, z);
            }
        if (kind == STUMP_MOSSY) {
            /* sprouts and a small fern grow on top */
            for (int i = 0; i < 4; i++) {
                float x = cx + rng_range(&r, -W * 0.35f, W * 0.35f);
                float hgt = rng_range(&r, 3, 7);
                cv_line(f, x, (float)top - 1, x + rng_range(&r, -2, 2), (float)top - hgt, mkrgb(56, 100, 60), 4);
                cv_set(f, (int)x - 1, top - (int)hgt + 1, mkrgb(70, 120, 70), 4);
            }
        }
    } else {
        for (int i = 0; i < 5; i++) {
            int x = (int)(cx + rng_range(&r, -W * 0.4f, W * 0.4f));
            for (int k = 0; k < 4; k++)
                if (!cv_opaque(f, x, top + 6 - k)) { cv_set(f, x, top + 7 - k, mkrgb(140, 120, 90), 3); break; }
        }
    }
    if (kind == STUMP_SNAG) {
        int nh = rng_between(&r, 1, 3);
        for (int i = 0; i < nh; i++) {
            int hx = (int)(cx + rng_range(&r, -W * 0.25f, W * 0.25f)), hy = top + rng_between(&r, 10, H - 8);
            cv_force(f, hx, hy, mkrgb(6, 6, 6), -5);
            cv_force(f, hx, hy + 1, mkrgb(10, 9, 8), -5);
        }
    }
    int nm = kind == STUMP_SNAG ? rng_between(&r, 1, 4) : kind == STUMP_MOSSY ? rng_between(&r, 2, 5) : rng_between(&r, 0, 3);
    rng_t mr = rng_seed(seed ^ 0x51u);
    for (int i = 0; i < nm; i++)
        mushroom_draw(f, (int)(cx + rng_range(&r, -W * 0.6f, W * 0.6f)), fh - 1,
                      rng_chance(&r, 0.4f) ? MUSH_GLOW : MUSH_BROWN, rng_between(&r, 1, 3), &mr, 0.5f);
    frame_rimlight(f, C_MOON, 0.15f);
    set_point(s, "top", 0, -H - 2);
    if (kind == STUMP_HOLLOW) set_point(s, "under", 0, 0);
    if (cut || kind == STUMP_MOSSY)
        export_top_path(s, f, "path.walk0", (int)(cx - W * 0.4f), (int)(cx + W * 0.4f), 3);
    if (kind == STUMP_SNAG) {
        char buf[384] = "";
        for (int y = fh - 1; y > top + 4; y -= 5) path_add(buf, sizeof buf, f, cx, (float)y);
        sprite_set_prop(s, "path.climb0", buf);
    }
    int16_t z0 = 0;
    sprite_add_clip(s, "idle", CLIP_LOOP, 1, 0, &z0, 1);
}

static void gen_stump(sprite_t *s, uint64_t seed) { gen_stump_kind(s, seed, STUMP_RANDOM); }
static void gen_stump_birch(sprite_t *s, uint64_t seed) { gen_stump_kind(s, seed, STUMP_BIRCH); }
static void gen_stump_mossy(sprite_t *s, uint64_t seed) { gen_stump_kind(s, seed, STUMP_MOSSY); }
static void gen_stump_hollow(sprite_t *s, uint64_t seed) { gen_stump_kind(s, seed, STUMP_HOLLOW); }
static void gen_snag(sprite_t *s, uint64_t seed) { gen_stump_kind(s, seed, STUMP_SNAG); }

/* uprooted tree: an overturned root plate (a tangle of roots and soil) with a piece of trunk */
static void gen_rootplate(sprite_t *s, uint64_t seed)
{
    rng_t r = rng_seed(seed);
    int R = rng_between(&r, 13, 19);
    int w = R * 2 + 40, h = R * 2 + 6;
    frame_t *f = sprite_add_frame(s, "base", w, h, R + 4, h - 1);
    uint32_t ns = rng_u32(&r);
    float cx = (float)(R + 4), cy = (float)(h - 1 - R);
    /* a fallen trunk extending from the plate to the right */
    float lr = (float)R * 0.45f;
    for (int y = 0; y < h; y++)
        for (int x = (int)cx; x < w; x++) {
            float v = ((float)y - (cy + (float)R * 0.35f)) / lr;
            if (v < -1 || v > 1) continue;
            float nz = sqrtf(1 - v * v), lit = clampf(0.3f + 0.6f * (-v * 0.7f + nz * 0.4f), 0.1f, 1.0f);
            rgb_t c = trunk_bark(TRUNK_OAK, x, y, v, lit, ns, 50);
            if (v < -0.5f && vnoise2((float)x * 0.15f, 0, ns ^ 3u) > 0.45f) c = rgb_scale(C_MOSS, 0.6f + lit * 0.5f);
            cv_set(f, x, y, c, (int)(nz * 3.0f) - 3);
        }
    /* root plate: soil + pebbles */
    for (int y = 0; y < h; y++)
        for (int x = 0; x < (int)(cx + R + 2); x++) {
            float dx = ((float)x - cx) / ((float)R * 0.75f), dy = ((float)y - cy) / (float)R;
            float d = dx * dx + dy * dy + (vnoise2((float)x * 0.3f, (float)y * 0.3f, ns) - 0.5f) * 0.5f;
            if (d > 1.0f) continue;
            float n = hash2f(x, y, ns);
            rgb_t c = rgb_scale(mkrgb(40, 32, 24), 0.6f + (1.0f - d) * 0.4f + (n - 0.5f) * 0.3f);
            if (n > 0.95f) c = mkrgb(84, 86, 88);
            cv_set(f, x, y, c, 0);
        }
    /* a tangle of roots sticking out in all directions */
    int nr = rng_between(&r, 10, 16);
    for (int i = 0; i < nr; i++) {
        float a = rng_range(&r, 0, NF_PI * 2.0f);
        if (cosf(a) > 0.6f) continue;                 /* the trunk is on the right */
        float x = cx + cosf(a) * (float)R * 0.4f, y = cy + sinf(a) * (float)R * 0.55f;
        float len = rng_range(&r, (float)R * 0.6f, (float)R * 1.3f), rad = rng_range(&r, 0.6f, 1.6f);
        rgb_t c = vary(C_BARK, &r, 0.15f);
        for (float t = 0; t < len; t += 1.5f) {
            float nx = x + cosf(a) * 1.5f, ny = y + sinf(a) * 1.5f;
            cv_capsule(f, x, y, rad * (1.0f - t / len * 0.7f) + 0.2f, nx, ny, rad * (1.0f - t / len * 0.7f), c, 2, 1);
            x = nx; y = ny;
            a += rng_range(&r, -0.25f, 0.25f);
        }
    }
    frame_rimlight(f, C_MOON, 0.15f);
    set_point(s, "under", (int)((float)R * 0.9f), 0);          /* hideout under the trunk by the plate */
    export_top_path(s, f, "path.walk0", (int)cx + R + 2, w - 4, 4);
    int16_t z0 = 0;
    sprite_add_clip(s, "idle", CLIP_LOOP, 1, 0, &z0, 1);
}

/* boulder: a big stone with cracks, lichens and moss */
static void gen_boulder(sprite_t *s, uint64_t seed)
{
    rng_t r = rng_seed(seed);
    int w = rng_between(&r, 24, 44), h = rng_between(&r, 14, 26);
    frame_t *f = sprite_add_frame(s, "base", w + 4, h + 2, (w + 4) / 2, h + 1);
    uint32_t ns = rng_u32(&r);
    rgb_t base = vary(mkrgb(62, 66, 70), &r, 0.12f);
    float cx = (float)(w + 4) / 2.0f, cy = (float)(h + 1);
    for (int y = 0; y <= h + 1; y++)
        for (int x = 0; x < w + 4; x++) {
            float dx = ((float)x - cx) / ((float)w * 0.5f), dy = ((float)y - cy) / (float)h;
            /* more angular shape: a mix of an ellipse and a "superellipse" */
            float d = powf(fabsf(dx), 2.6f) + powf(fabsf(dy), 2.2f)
                    + (vnoise2((float)x * 0.18f, (float)y * 0.18f, ns) - 0.5f) * 0.45f;
            if (d > 1.0f) continue;
            float facet = vnoise2((float)x * 0.12f, (float)y * 0.12f, ns ^ 2u);
            float lit = clampf(0.45f + dx * 0.3f - dy * 0.45f + (facet - 0.5f) * 0.5f, 0.15f, 1.15f);
            rgb_t c = rgb_scale(base, 0.4f + lit * 0.6f);
            if (fabsf(vnoise2((float)x * 0.25f, (float)y * 0.25f, ns ^ 4u) - 0.5f) < 0.025f) c = mkrgb(16, 16, 18);
            if (vnoise2((float)x * 0.45f, (float)y * 0.45f, ns ^ 6u) > 0.8f) c = rgb_scale(mkrgb(120, 138, 128), 0.5f + lit * 0.4f);
            if (dy < -0.5f && vnoise2((float)x * 0.22f, (float)y * 0.3f, ns ^ 8u) > 0.4f)
                c = rgb_scale(hash2f(x, y, ns) > 0.5f ? C_MOSS_LIGHT : C_MOSS, 0.6f + lit * 0.4f);
            cv_set(f, x, y, c, (int)((1.0f - d) * 5.0f) - 1);
        }
    frame_rimlight(f, C_MOON, 0.2f);
    set_point(s, "top", 0, -h);
    export_top_path(s, f, "path.walk0", 3, w, 4);
    int16_t z0 = 0;
    sprite_add_clip(s, "idle", CLIP_LOOP, 1, 0, &z0, 1);
}

/* pile of brushwood (a hideout for small animals) */
static void gen_brushwood(sprite_t *s, uint64_t seed)
{
    rng_t r = rng_seed(seed);
    int w = rng_between(&r, 30, 46), h = rng_between(&r, 10, 15);
    frame_t *f = sprite_add_frame(s, "base", w, h, w / 2, h - 1);
    int n = rng_between(&r, 9, 16);
    for (int i = 0; i < n; i++) {
        float y0 = rng_range(&r, (float)h * 0.3f, (float)h - 1);
        float x0 = rng_range(&r, 0, (float)w * 0.4f), x1 = rng_range(&r, (float)w * 0.6f, (float)w);
        float y1 = clampf(y0 + rng_range(&r, -5, 5), 1, (float)h - 1);
        rgb_t c = vary(rng_chance(&r, 0.3f) ? mkrgb(80, 76, 70) : C_BARK, &r, 0.15f);
        cv_capsule(f, x0, y0, rng_range(&r, 0.6f, 1.3f), x1, y1, 0.5f, c, rng_between(&r, -2, 3), 1);
        if (rng_chance(&r, 0.4f)) {
            float t = rng_range(&r, 0.3f, 0.8f);
            float bx = x0 + (x1 - x0) * t, by = y0 + (y1 - y0) * t;
            cv_line(f, bx, by, bx + rng_range(&r, -4, 4), by - rng_range(&r, 2, 5), rgb_scale(c, 0.8f), 1);
        }
    }
    static const rgb_t lc[] = { {78, 52, 30}, {96, 70, 36}, {60, 44, 30} };
    for (int i = 0; i < w / 3; i++)
        cv_leaf(f, rng_range(&r, 2, (float)w - 2), rng_range(&r, 2, (float)h - 1), 3.0f, 0.9f,
                rng_range(&r, 0, 6.28f), lc[rng_int(&r, 3)], rgb_scale(lc[0], 0.6f), rng_between(&r, -1, 2));
    frame_rimlight(f, C_MOON, 0.12f);
    set_point(s, "under", 0, 0);
    int16_t z0 = 0;
    sprite_add_clip(s, "idle", CLIP_LOOP, 1, 0, &z0, 1);
}

/* puddle: a dark surface with a moon reflection that gently shimmers */
static void gen_puddle(sprite_t *s, uint64_t seed)
{
    rng_t r = rng_seed(seed);
    int w = rng_between(&r, 20, 42), h = rng_between(&r, 5, 8);
    uint32_t ns = rng_u32(&r);
    float gx = rng_range(&r, (float)w * 0.3f, (float)w * 0.7f);
    for (int k = 0; k < 4; k++) {
        char name[8];
        snprintf(name, sizeof name, "f%d", k);
        frame_t *f = sprite_add_frame(s, name, w, h, w / 2, h - 1);
        float cx = (float)w / 2.0f, cy = (float)h / 2.0f;
        for (int y = 0; y < h; y++)
            for (int x = 0; x < w; x++) {
                float dx = ((float)x + 0.5f - cx) / ((float)w * 0.5f), dy = ((float)y + 0.5f - cy) / ((float)h * 0.5f);
                float d = dx * dx + dy * dy + (vnoise2((float)x * 0.3f, (float)y * 0.5f, ns) - 0.5f) * 0.4f;
                if (d > 1.0f) continue;
                rgb_t c;
                if (d > 0.75f) c = mkrgb(22, 20, 16);           /* muddy edge */
                else c = rgb_mix(mkrgb(10, 18, 26), mkrgb(4, 8, 12), (float)y / (float)h);
                cv_set(f, x, y, c, -3);
                /* reflection: horizontal dashes that move and flicker */
                float wave = sinf((float)x * 0.9f + (float)k * 1.6f + (float)y * 2.0f);
                if (d < 0.6f && fabsf((float)x - gx) < 4.0f - fabsf((float)y - cy) && wave > 0.45f) {
                    cv_glow(f, x, y, rgb_mix(mkrgb(90, 110, 120), mkrgb(190, 205, 210), (wave - 0.45f) * 1.8f), -3);
                }
            }
    }
    int16_t seq[] = { 0, 1, 2, 3, 2, 1 };
    sprite_add_clip(s, "idle", CLIP_LOOP, 3, 0, seq, 6);
}

/* moss cushion with spore capsules */
static void gen_moss_patch(sprite_t *s, uint64_t seed)
{
    rng_t r = rng_seed(seed);
    int w = rng_between(&r, 14, 32), h = rng_between(&r, 4, 8);
    frame_t *f = sprite_add_frame(s, "base", w, h + 4, w / 2, h + 3);
    uint32_t ns = rng_u32(&r);
    for (int y = 0; y < h + 4; y++)
        for (int x = 0; x < w; x++) {
            float dx = ((float)x - (float)w / 2.0f) / ((float)w * 0.5f);
            float top = (float)(h + 4) - (float)h * sqrtf(fmaxf(0, 1.0f - dx * dx)) - (vnoise2((float)x * 0.4f, 0, ns) - 0.5f) * 2.0f;
            if ((float)y < top) continue;
            float n = hash2f(x, y, ns);
            float lit = 1.0f - ((float)y - top) / (float)(h + 1);
            rgb_t c = rgb_scale(n > 0.5f ? C_MOSS_LIGHT : C_MOSS, 0.5f + lit * 0.6f);
            cv_set(f, x, y, c, 0);
        }
    int ns_ = rng_between(&r, 3, 10);
    for (int i = 0; i < ns_; i++) {
        int x = rng_between(&r, 2, w - 3);
        int y0 = 0;
        for (int y = 0; y < h + 4; y++) if (cv_opaque(f, x, y)) { y0 = y; break; }
        int l = rng_between(&r, 2, 4);
        for (int k = 1; k <= l; k++) cv_set(f, x, y0 - k, mkrgb(110, 70, 50), 1);
        cv_set(f, x, y0 - l - 1, mkrgb(150, 120, 80), 1);
    }
    int16_t z0 = 0;
    sprite_add_clip(s, "idle", CLIP_LOOP, 1, 0, &z0, 1);
}

/* small plants with stems bent by the wind: flowers, horsetail, nettle, sapling */
enum { STEM_FLOWERS, STEM_HORSETAIL, STEM_NETTLE, STEM_SAPLING };

static void gen_stems(sprite_t *s, uint64_t seed, int kind)
{
    rng_t r = rng_seed(seed);
    int n = kind == STEM_SAPLING ? 1 : rng_between(&r, 2, kind == STEM_FLOWERS ? 7 : 5);
    float xs[8], hs[8], cur[8];
    int fk[8];
    int hmin = kind == STEM_FLOWERS ? 5 : kind == STEM_HORSETAIL ? 12 : kind == STEM_NETTLE ? 12 : 24;
    int hmax = kind == STEM_FLOWERS ? 13 : kind == STEM_HORSETAIL ? 22 : kind == STEM_NETTLE ? 22 : 38;
    int w = kind == STEM_SAPLING ? 34 : 24, h = hmax + 6;
    rgb_t petal = rng_chance(&r, 0.5f) ? mkrgb(196, 198, 188) : mkrgb(96, 104, 176);
    for (int i = 0; i < n; i++) {
        xs[i] = (float)w / 2.0f + (kind == STEM_SAPLING ? 0 : rng_range(&r, -7, 7));
        hs[i] = rng_range(&r, (float)hmin, (float)hmax);
        cur[i] = rng_range(&r, -2.5f, 2.5f);
        fk[i] = rng_int(&r, 2);
    }
    uint32_t ls = rng_u32(&r);
    for (int fi = 0; fi < SWAY_FRAMES; fi++) {
        float bend = sway_bend(fi) * (kind == STEM_SAPLING ? 3.0f : 2.0f);
        char name[16];
        snprintf(name, sizeof name, "bend%d", fi);
        frame_t *f = sprite_add_frame(s, name, w, h, w / 2, h - 1);
        rng_t lr = rng_seed(ls);
        for (int i = 0; i < n; i++) {
            int steps = (int)(hs[i] * 2.0f);
            float px = xs[i], py = (float)(h - 1);
            for (int k = 1; k <= steps; k++) {
                float t = (float)k / (float)steps;
                float x = xs[i] + (cur[i] + bend) * t * t, y = (float)(h - 1) - hs[i] * t;
                rgb_t sc = kind == STEM_SAPLING ? rgb_mix(mkrgb(60, 46, 36), mkrgb(70, 90, 50), t)
                         : kind == STEM_HORSETAIL ? mkrgb(64, 100, 62) : mkrgb(44, 76, 46);
                cv_line(f, px, py, x, y, sc, 0);
                int node = (k % 5) == 0;
                if (kind == STEM_HORSETAIL && node && t > 0.15f) {
                    /* a whorl of thin twigs */
                    float bl = 3.2f * (1.0f - t * 0.6f);
                    cv_line(f, x, y, x - bl, y + bl * 0.6f, mkrgb(80, 120, 74), 1);
                    cv_line(f, x, y, x + bl, y + bl * 0.6f, mkrgb(70, 110, 66), -1);
                    cv_set(f, (int)x, (int)y, mkrgb(40, 52, 36), 0);
                }
                if (kind == STEM_NETTLE && (k % 7) == 0 && t > 0.15f) {
                    float ll = 3.6f * (1.1f - t * 0.6f);
                    cv_leaf(f, x - ll * 0.6f, y + 0.8f, ll, ll * 0.32f, NF_PI * 0.85f, mkrgb(52, 90, 54), mkrgb(30, 58, 38), 1);
                    cv_leaf(f, x + ll * 0.6f, y + 0.8f, ll, ll * 0.32f, NF_PI * 0.15f, mkrgb(56, 96, 58), mkrgb(32, 60, 40), -1);
                }
                if (kind == STEM_SAPLING && t > 0.35f && (k % 4) == 0) {
                    float side = (k / 4) % 2 ? 1.0f : -1.0f;
                    float ll = rng_range(&lr, 3.0f, 5.0f);
                    cv_leaf(f, x + side * ll * 0.55f, y + 0.5f, ll, ll * 0.35f, side > 0 ? 0.3f : NF_PI - 0.3f,
                            mkrgb(58, 104, 60), mkrgb(34, 66, 42), rng_between(&lr, -1, 2));
                }
                px = x; py = y;
            }
            if (kind == STEM_FLOWERS) {
                int fx = (int)floorf(px + 0.5f), fy = (int)floorf(py + 0.5f);
                if (fk[i] == 0) {
                    /* star-shaped flower (wood anemone) */
                    cv_set(f, fx, fy, mkrgb(210, 190, 90), 2);
                    cv_set(f, fx - 1, fy, petal, 1); cv_set(f, fx + 1, fy, petal, 1);
                    cv_set(f, fx, fy - 1, petal, 1); cv_set(f, fx, fy + 1, rgb_scale(petal, 0.8f), 1);
                } else {
                    /* bellflower: drooping */
                    cv_set(f, fx + 1, fy, rgb_scale(petal, 0.9f), 1);
                    cv_set(f, fx + 1, fy + 1, petal, 1);
                    cv_set(f, fx + 2, fy + 1, rgb_scale(petal, 0.7f), 1);
                    cv_set(f, fx + 1, fy + 2, rgb_scale(petal, 0.6f), 1);
                }
                cv_leaf(f, xs[i] + 1.5f, (float)(h - 2), 3.5f, 0.9f, -0.5f, mkrgb(50, 90, 54), mkrgb(30, 60, 40), -1);
            }
        }
        frame_rimlight(f, C_MOON, 0.12f);
    }
    add_sway_clips(s, 0);
}

static void gen_flowers(sprite_t *s, uint64_t seed) { gen_stems(s, seed, STEM_FLOWERS); }
static void gen_horsetail(sprite_t *s, uint64_t seed) { gen_stems(s, seed, STEM_HORSETAIL); }
static void gen_nettle(sprite_t *s, uint64_t seed) { gen_stems(s, seed, STEM_NETTLE); }
static void gen_sapling(sprite_t *s, uint64_t seed) { gen_stems(s, seed, STEM_SAPLING); }

/* cones and needles on the ground */
static void gen_cones(sprite_t *s, uint64_t seed)
{
    rng_t r = rng_seed(seed);
    int w = rng_between(&r, 20, 38), h = 7;
    frame_t *f = sprite_add_frame(s, "base", w, h, w / 2, h - 1);
    for (int i = 0; i < w * 2; i++) {
        float x = rng_range(&r, 1, (float)w - 1), y = rng_range(&r, 3, (float)h - 1);
        float a = rng_range(&r, -0.6f, 0.6f);
        rgb_t c = vary(mkrgb(110, 70, 40), &r, 0.2f);
        cv_line(f, x, y, x + cosf(a) * 2.5f, y + sinf(a) * 1.0f, rgb_scale(c, rng_range(&r, 0.5f, 1.0f)), -1);
    }
    int nc = rng_between(&r, 1, 4);
    for (int i = 0; i < nc; i++) {
        int x = rng_between(&r, 3, w - 5), y = h - 3;
        int horiz = rng_chance(&r, 0.6f);
        for (int k = 0; k < 4; k++)
            for (int j = 0; j < 2; j++) {
                int px = horiz ? x + k : x + j, py = horiz ? y + j - 1 : y - k + 1;
                rgb_t c = ((k + j) % 2) ? mkrgb(92, 60, 36) : mkrgb(124, 84, 50);
                cv_set(f, px, py, c, 1);
            }
    }
    int16_t z0 = 0;
    sprite_add_clip(s, "idle", CLIP_LOOP, 1, 0, &z0, 1);
}

/* bearded lichen hanging from a branch */
static void gen_hanging_moss(sprite_t *s, uint64_t seed)
{
    rng_t r = rng_seed(seed);
    int n = rng_between(&r, 4, 10), w = 24, h = 26;
    float xs[12], ls[12], ph[12];
    for (int i = 0; i < n; i++) {
        xs[i] = rng_range(&r, 4, (float)w - 4);
        ls[i] = rng_range(&r, 6, (float)h - 2);
        ph[i] = rng_range(&r, 0, 6.28f);
    }
    for (int fi = 0; fi < SWAY_FRAMES; fi++) {
        float bend = sway_bend(fi) * 3.0f;
        char name[16];
        snprintf(name, sizeof name, "bend%d", fi);
        frame_t *f = sprite_add_frame(s, name, w, h, w / 2, 0);
        for (int i = 0; i < n; i++)
            for (int y = 0; y < (int)ls[i]; y++) {
                float t = (float)y / ls[i];
                float x = xs[i] + sinf(ph[i] + (float)y * 0.5f) * 0.7f + bend * t * t;
                rgb_t c = rgb_mix(mkrgb(96, 112, 96), mkrgb(140, 156, 132), t);
                cv_set(f, (int)floorf(x + 0.5f), y, c, (i % 3) - 1);
                if (hash2f(i, y, 3) > 0.7f) cv_set(f, (int)floorf(x + 0.5f) + 1, y, rgb_scale(c, 0.7f), (i % 3) - 1);
            }
    }
    add_sway_clips(s, 0);
}

/* moonbeam: a translucent slanted band of light with specks of dust */
static void gen_moonbeam(sprite_t *s, uint64_t seed)
{
    rng_t r = rng_seed(seed);
    int bw = rng_between(&r, 10, 22), h = 128;
    float slant = rng_range(&r, 0.12f, 0.3f);
    int w = bw + (int)(slant * (float)h) + 4;
    rgb_t c = mkrgb(150, 180, 192);
    float mx[16], my[16];
    for (int i = 0; i < 16; i++) { mx[i] = rng_range(&r, 0, 1); my[i] = rng_range(&r, 0, (float)h); }
    for (int k = 0; k < 6; k++) {
        char name[8];
        snprintf(name, sizeof name, "f%d", k);
        frame_t *f = sprite_add_frame(s, name, w, h, w - bw / 2 - 2, 0);
        for (int y = 0; y < h; y++) {
            float x0 = (float)(w - bw - 2) - slant * (float)y;
            float vfade = fminf(1.0f, (float)y / 20.0f) * fminf(1.0f, (float)(h - y) / 30.0f);
            for (int x = 0; x < w; x++) {
                float u = ((float)x - x0) / (float)bw;
                if (u < 0 || u > 1) continue;
                float edge = sinf(u * NF_PI);
                int a = (int)(22.0f * edge * vfade * (0.8f + 0.2f * sinf((float)y * 0.05f + (float)k * 0.3f)));
                if (a > 2) cv_halo(f, x, y, c, a);
            }
        }
        /* dust slowly sinks */
        for (int i = 0; i < 16; i++) {
            float y = fmodf(my[i] + (float)k * 1.5f, (float)h);
            float x = (float)(w - bw - 2) - slant * y + mx[i] * (float)bw;
            int ix = (int)x, iy = (int)y;
            if (ix >= 0 && iy >= 0 && ix < w && iy < h) {
                px_t *p = &f->px[iy * w + ix];
                p->r = 210; p->g = 225; p->b = 230; p->a = 140; p->f = PX_GLOW;
            }
        }
    }
    int16_t seq[] = { 0, 1, 2, 3, 4, 5 };
    sprite_add_clip(s, "idle", CLIP_LOOP, 3, 0, seq, 6);
}

/* ground mist: a translucent band that slowly drifts */
static void gen_mist(sprite_t *s, uint64_t seed)
{
    rng_t r = rng_seed(seed);
    int w = rng_between(&r, 60, 110), h = rng_between(&r, 8, 16);
    uint32_t ns = rng_u32(&r);
    rgb_t c = mkrgb(120, 140, 146);
    for (int k = 0; k < 8; k++) {
        char name[8];
        snprintf(name, sizeof name, "f%d", k);
        frame_t *f = sprite_add_frame(s, name, w, h, w / 2, h - 1);
        for (int y = 0; y < h; y++)
            for (int x = 0; x < w; x++) {
                float n = fbm2(((float)x + (float)k * 1.2f) * 0.06f, (float)y * 0.18f, 3, ns);
                float vy = (float)y / (float)h;                 /* denser near the ground */
                float ex = fminf(1.0f, fminf((float)x, (float)(w - 1 - x)) / 14.0f);
                int a = (int)((n - 0.35f) * 90.0f * vy * ex);
                if (a > 3) cv_halo(f, x, y, c, NF_MIN(a, 48));
            }
    }
    int16_t seq[] = { 0, 1, 2, 3, 4, 5, 6, 7 };
    sprite_add_clip(s, "idle", CLIP_LOOP, 1.5f, 0, seq, 8);
}

/* overhead canopy: a mass of leaves hanging from the top edge (no sky) */
static void gen_canopy(sprite_t *s, uint64_t seed)
{
    rng_t r = rng_seed(seed);
    int w = rng_between(&r, 50, 90), h = rng_between(&r, 18, 30);
    uint32_t ns = rng_u32(&r);
    float bottom[128];
    for (int x = 0; x < w; x++)
        bottom[x] = (float)h * (0.35f + 0.55f * vnoise2((float)x * 0.08f, 0.5f, ns))
                  * fminf(1.0f, 0.4f + fminf((float)x, (float)(w - 1 - x)) / 12.0f);
    typedef struct { float x, y, len, ang; rgb_t l, d; int z; } cl_t;
    static cl_t lv[900];
    int nl = 0;
    for (int i = 0; i < w * h / 3 && nl < 900; i++) {
        float x = rng_range(&r, 0, (float)w), y = rng_range(&r, 0, (float)h);
        if (y > bottom[(int)x]) continue;
        cl_t *L = &lv[nl++];
        L->x = x; L->y = y;
        L->len = rng_range(&r, 3.0f, 5.0f);
        L->ang = NF_PI * 0.5f + rng_range(&r, -1.0f, 1.0f);   /* leaves hang down */
        float lit = clampf(0.2f + (x / (float)w) * 0.3f - (y / (float)h) * 0.2f, 0.05f, 0.6f);
        rgb_t b = rgb_scale(vary(mkrgb(24, 50, 38), &r, 0.15f), 0.5f + lit);
        L->l = rgb_mix(b, C_MOON, 0.05f);
        L->d = rgb_scale(b, 0.65f);
        L->z = rng_between(&r, -3, 3);
    }
    for (int fi = 0; fi < SWAY_FRAMES; fi++) {
        float bend = sway_bend(fi) * 1.5f;
        char name[16];
        snprintf(name, sizeof name, "bend%d", fi);
        frame_t *f = sprite_add_frame(s, name, w, h + 4, w / 2, 0);
        for (int y = 0; y < h; y++)
            for (int x = 0; x < w; x++)
                if ((float)y < bottom[x] - 2.0f) cv_set(f, x, y, mkrgb(6, 14, 12), -5);
        for (int i = 0; i < nl; i++) {
            float t = lv[i].y / (float)h;
            cv_leaf(f, lv[i].x + bend * t * t, lv[i].y, lv[i].len, 1.2f, lv[i].ang, lv[i].l, lv[i].d, lv[i].z);
        }
        frame_rimlight(f, C_MOON, 0.08f);
    }
    add_sway_clips(s, 0);
}

/* ------------------------------------------------------------------ */
/* registration                                                        */
/* ------------------------------------------------------------------ */

typedef struct { const char *name; gen_fn fn; const char *tags; } genreg_t;

static const genreg_t GENS[] = {
    { "grass",          gen_grass,          "plant ground foreground sway" },
    { "grass_tall",     gen_grass_tall,     "plant ground foreground sway" },
    { "grass_low",      gen_grass_low,      "plant ground foreground sway" },
    { "fern",           gen_fern,           "plant ground foreground sway" },
    { "broadleaf",      gen_broadleaf,      "plant ground foreground sway" },
    { "sprouts",        gen_sprouts,        "plant ground small sway" },
    { "bush",           gen_bush,           "plant bush ground hideout" },
    { "bush_wide",      gen_bush_wide,      "plant bush ground hideout" },
    { "bush_small",     gen_bush_small,     "plant bush ground hideout" },
    { "trunk",          gen_trunk,          "tree trunk climbable oak" },
    { "trunk_birch",    gen_trunk_birch,    "tree trunk climbable birch" },
    { "trunk_pine",     gen_trunk_pine,     "tree trunk climbable pine" },
    { "trunk_dead",     gen_trunk_dead,     "tree trunk climbable dead" },
    { "trunk_giant",    gen_trunk_giant,    "tree trunk climbable oak giant" },
    { "trunk_beech",    gen_trunk_beech,    "tree trunk climbable beech" },
    { "trunk_spruce",   gen_trunk_spruce,   "tree trunk climbable spruce" },
    { "trunk_old",      gen_trunk_old,      "tree trunk climbable oak old" },
    { "trunk_ivy",      gen_trunk_ivy,      "tree trunk climbable oak ivy" },
    { "branch_birch",   gen_branch_birch,   "tree branch perch birch" },
    { "branch_pine",    gen_branch_pine,    "tree branch perch pine" },
    { "branch_spruce",  gen_branch_spruce,  "tree branch perch spruce" },
    { "branch_dead",    gen_branch_dead,    "tree branch perch dead" },
    { "branch_giant",   gen_branch_giant,   "tree branch perch oak" },
    { "branch_beech",   gen_branch_beech,   "tree branch perch beech" },
    { "branch_old",     gen_branch_old,     "tree branch perch oak" },
    { "branch_ivy",     gen_branch_ivy,     "tree branch perch oak ivy" },
    { "stump_birch",    gen_stump_birch,    "tree stump ground perch" },
    { "stump_mossy",    gen_stump_mossy,    "tree stump ground perch large" },
    { "stump_hollow",   gen_stump_hollow,   "tree stump ground perch hideout" },
    { "snag",           gen_snag,           "tree stump ground climbable dead" },
    { "rootplate",      gen_rootplate,      "tree roots ground large hideout" },
    { "bush_big",       gen_bush_big,       "plant bush ground hideout big" },
    { "fern_big",       gen_fern_big,       "plant ground foreground sway big" },
    { "broadleaf_big",  gen_broadleaf_big,  "plant ground foreground sway big" },
    { "tree_far",       gen_tree_far,       "tree background far" },
    { "stump",          gen_stump,          "tree stump ground perch" },
    { "canopy",         gen_canopy,         "plant hanging canopy sway" },
    { "hanging_moss",   gen_hanging_moss,   "plant hanging sway" },
    { "roots",          gen_roots,          "tree roots ground" },
    { "branch",         gen_branch,         "tree branch perch" },
    { "vine",           gen_vine,           "plant hanging sway" },
    { "mushrooms",      gen_mushrooms,      "fungus ground small" },
    { "mushroom_glow",  gen_mushroom_glow,  "fungus ground small glow" },
    { "mushroom_red",   gen_mushroom_red,   "fungus ground small" },
    { "stone",          gen_stone,          "ground stone" },
    { "log",            gen_log,            "ground log hideout" },
    { "log_mossy",      gen_log_mossy,      "ground log hideout" },
    { "log_birch",      gen_log_birch,      "ground log hideout" },
    { "log_hollow",     gen_log_hollow,     "ground log hideout" },
    { "log_rotten",     gen_log_rotten,     "ground log hideout fungus" },
    { "boulder",        gen_boulder,        "ground stone large perch" },
    { "brushwood",      gen_brushwood,      "ground hideout" },
    { "puddle",         gen_puddle,         "ground water" },
    { "moss_patch",     gen_moss_patch,     "ground small" },
    { "cones",          gen_cones,          "ground floor" },
    { "flowers",        gen_flowers,        "plant ground small sway" },
    { "horsetail",      gen_horsetail,      "plant ground foreground sway" },
    { "nettle",         gen_nettle,         "plant ground foreground sway" },
    { "sapling",        gen_sapling,        "plant ground sway" },
    { "moonbeam",       gen_moonbeam,       "light atmosphere" },
    { "mist",           gen_mist,           "atmosphere" },
    { "litter",         gen_litter,         "ground floor" },
    { "web",            gen_web,            "detail web" },
    { "firefly",        gen_firefly,        "creature air tiny glow" },
};

void gen_register_all(library_t *lib, uint64_t seed)
{
    for (size_t i = 0; i < NF_COUNT(GENS); i++) {
        sprite_t *s = sprite_new(GENS[i].name);
        snprintf(s->tags, sizeof s->tags, "%s", GENS[i].tags);
        snprintf(s->src, sizeof s->src, "gen:%s", GENS[i].name);
        s->gen = GENS[i].fn;
        uint64_t sd = rng_derive(seed, i + 1);
        sprite_regen(s, sd);
        /* the generator may have overwritten the tags; registered tags take priority if they are empty */
        if (!s->tags[0]) snprintf(s->tags, sizeof s->tags, "%s", GENS[i].tags);
        lib_add(lib, s);
    }
}
