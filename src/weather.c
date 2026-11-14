/* weather.c — fog and rain that come and go
 *
 * Every layer (fog, rain) has a strength and a band [tail, lead] that travels across the screen in the
 * wind direction (coordinates: 0 = the side it comes from, 1 = the other side):
 *   arriving   the leading edge sweeps in, everything behind it is covered (tail = -inf),
 *   on         it covers everything (lead = +inf, tail = -inf),
 *   leaving    the trailing edge follows the same way out (lead = +inf).
 * Arriving and then leaving before the front reached the far side makes a bank that drifts through.
 * The edges are soft and ragged (noise along the height, changing in time), and the fog front hugs the
 * ground: its lower part leads, so mist seems to pour in from the side along the forest floor.
 * A pattern (clear, mist, fog, drizzle, rain, rain+fog) only sets the target of each layer, so the
 * transitions mix naturally: fog rolls out while rain sweeps in, and "12 % fog, 88 % rain" is just two
 * layers at different strengths.
 *
 * Rain = particles (streaks slanted by the local wind, closer ones longer and faster, depth-tested
 * against the frame), splashes on the ground and rings on puddles. Fog = an overlay over the finished
 * frame: wisps of noise drifting with the wind, thicker at the ground and in the distance (by z),
 * computed on a coarse grid and interpolated. Rain also darkens the scene a little.
 */
#include "world.h"
#include "noise.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define WX_INF 1.0e6f

/* About how long one weather lasts, in minutes (--weather-time, the Windows settings). The real length of each
 * weather is random around it (0.6 … 1.4 ×). Only the duration changes: the fronts always move at the same speed
 * (a very short weather simply leaves again before its fog front has crossed the screen). 0 = no weather. */
float g_weather_minutes = 6.0f;

static float wx_base(void) { return g_weather_minutes * 60.0f; }

static const struct {
    const char *name;
    float fog, rain, weight;
} PAT[] = {
    { "clear",   0.00f, 0.00f, 4.0f },
    { "mist",    0.45f, 0.00f, 2.0f },
    { "fog",     0.90f, 0.00f, 1.3f },
    { "drizzle", 0.25f, 0.35f, 1.4f },
    { "rain",    0.00f, 0.85f, 1.2f },
    { "rainfog", 0.55f, 0.65f, 0.7f },
};
#define NPAT ((int)(sizeof PAT / sizeof PAT[0]))

const char *weather_pattern_name(int p) { return p >= 0 && p < NPAT ? PAT[p].name : "?"; }

/* puddles of the scene (rings when a drop lands in one) */
#define MAX_PUDDLE 24
static struct { float x0, x1, y; int z; } g_pud[MAX_PUDDLE];
static int g_npud;

/* big leaves of the scene (butterbur): rain lands on them, the dry spot is below */
#define MAX_LEAF 48
static struct { float x0, x1, y; int z, obj; } g_leaf[MAX_LEAF];
static int g_nleaf;

/* drip points (leaf tips): while the leaves are wet a drop swells there and falls */
#define MAX_DRIP 64
static struct { float x, y; int z; float t, hang; } g_drip[MAX_DRIP];
static int g_ndrip;

/* coarse fog grid (one value per GRID x GRID pixels) */
#define GRID 4
static float *g_fog, *g_dark;
static int g_cap;

/* --- layers ------------------------------------------------------------ */

static float edge(float e, float soft) { return smooth01(clampf(e / soft, 0.0f, 1.0f)); }

/* coverage of a layer at screen (x, y), 0 … amount */
static float cover(const world_t *w, int k, float x, float y)
{
    const wx_layer_t *L = &w->wx.layer[k];
    if (L->amount <= 0.001f || L->lead <= -WX_INF * 0.5f) return 0.0f;
    if (L->lead >= WX_INF * 0.5f && L->tail <= -WX_INF * 0.5f) return L->amount;
    float u = L->dir > 0 ? x / (float)w->w : 1.0f - x / (float)w->w;
    float h = (float)w->h;
    /* ragged, slowly changing edges; the fog's lower part leads (it pours in along the ground) */
    float n = (vnoise2(y * 0.045f + (float)k * 17.0f, w->t * 0.012f, 911u) - 0.5f) * L->soft * 1.2f
            + (vnoise2(y * 0.16f, w->t * 0.04f, 913u) - 0.5f) * 0.03f;
    if (k == WX_FOG) n += (y / h - 0.6f) * 0.22f;
    float c = L->amount;
    if (L->lead < WX_INF * 0.5f) c *= edge(L->lead + n - u, L->soft);
    if (L->tail > -WX_INF * 0.5f) c *= edge(u - L->tail - n, L->soft);
    return c;
}

float weather_at(const world_t *w, int layer, float x)
{
    if (layer < 0 || layer >= WX_COUNT) return 0.0f;
    return cover(w, layer, x, (float)w->h * 0.6f);
}

static float wind_sign(const world_t *w) { return w->wind_dir < 0 ? -1.0f : 1.0f; }

/* how far off-screen a new edge starts / an old one ends (the ragged noise and the fog's ground lead) */
static float margin(int k) { return k == WX_FOG ? 0.3f : 0.08f; }

static void arrive(world_t *w, int k, float target)
{
    weather_t *x = &w->wx;
    wx_layer_t *L = &x->layer[k];
    L->dir = wind_sign(w);
    L->soft = k == WX_FOG ? 0.35f : 0.12f;
    L->speed = 1.0f / (k == WX_FOG ? rng_range(&x->rng, 240.0f, 420.0f) : rng_range(&x->rng, 18.0f, 35.0f));
    L->lead = -L->soft - margin(k);
    L->tail = -WX_INF;
    L->amount = L->target = target;
    L->state = WXS_ARRIVE;
}

/* the wish of a pattern for one layer: come in, go out, or change strength */
static void layer_want(world_t *w, int k, float target)
{
    wx_layer_t *L = &w->wx.layer[k];
    int active = L->state != WXS_NONE;
    if (target <= 0.001f) {
        L->target = 0.0f;
        if (active && L->state != WXS_LEAVE) {
            L->state = WXS_LEAVE;
            L->tail = -L->soft - margin(k);
        }
        return;
    }
    if (!active) { arrive(w, k, target); return; }
    L->target = target;           /* already here (or still leaving: it comes back once it is gone) */
}

static void set_pattern(world_t *w, int p, int instant)
{
    weather_t *x = &w->wx;
    x->pattern = p;
    float want[WX_COUNT] = { PAT[p].fog, PAT[p].rain };
    for (int k = 0; k < WX_COUNT; k++) {
        if (instant) {
            wx_layer_t *L = &x->layer[k];
            memset(L, 0, sizeof *L);
            L->dir = wind_sign(w);
            L->soft = k == WX_FOG ? 0.35f : 0.12f;
            L->speed = k == WX_FOG ? 1.0f / 320.0f : 1.0f / 25.0f;
            L->lead = want[k] > 0 ? WX_INF : -WX_INF;
            L->tail = -WX_INF;
            L->amount = L->target = want[k];
            L->state = want[k] > 0 ? WXS_ON : WXS_NONE;
        } else {
            layer_want(w, k, want[k]);
        }
    }
    /* the base length (setting) with a random spread: 0.6 … 1.4 ×; clear spells are a bit shorter */
    float hold = wx_base() * rng_range(&x->rng, 0.6f, 1.4f) * (p == 0 ? 0.85f : 1.0f);
    x->next_change = fmaxf(hold, 15.0f);
    world_log(w, "weather → %s", PAT[p].name);
}

static int pick_pattern(world_t *w)
{
    weather_t *x = &w->wx;
    float wt[NPAT], sum = 0;
    for (int i = 0; i < NPAT; i++) {
        wt[i] = i == x->pattern ? 0.0f : PAT[i].weight;
        if (PAT[i].fog > 0) wt[i] *= x->bias[WX_FOG];
        if (PAT[i].rain > 0) wt[i] *= x->bias[WX_RAIN];
        sum += wt[i];
    }
    if (sum <= 0) return 0;
    float r = rng_range(&x->rng, 0.0f, sum);
    for (int i = 0; i < NPAT; i++) {
        r -= wt[i];
        if (wt[i] > 0 && r <= 0) return i;
    }
    return 0;
}

void weather_init(world_t *w, uint64_t seed)
{
    weather_t *x = &w->wx;
    memset(x, 0, sizeof *x);
    x->rng = rng_seed(seed ^ 0x77ea7e3d1ull);
    x->bias[WX_FOG] = x->bias[WX_RAIN] = 1.0f;
    for (int k = 0; k < WX_COUNT; k++) { x->layer[k].lead = -WX_INF; x->layer[k].tail = -WX_INF; }
}

void weather_begin(world_t *w, const char *arg)
{
    weather_t *x = &w->wx;
    g_npud = 0;
    for (int i = 0; i < w->nobj && g_npud < MAX_PUDDLE; i++) {
        const object_t *o = &w->obj[i];
        if (strcmp(o->spr->name, "puddle") != 0) continue;
        g_pud[g_npud].x0 = (float)o->bx0 + 1.0f;
        g_pud[g_npud].x1 = (float)o->bx1 - 1.0f;
        g_pud[g_npud].y = o->y - 1.0f;
        g_pud[g_npud].z = o->z;
        g_npud++;
    }
    g_nleaf = 0;
    for (int i = 0; i < w->nobj; i++) {
        const object_t *o = &w->obj[i];
        const char *lv = sprite_prop(o->spr, "leaves");
        if (!lv) continue;
        int a, b, c, used;
        while (g_nleaf < MAX_LEAF && sscanf(lv, "%d %d %d%n", &a, &b, &c, &used) == 3) {
            float fa = o->flip ? (float)-b : (float)a, fb = o->flip ? (float)-a : (float)b;
            g_leaf[g_nleaf].x0 = o->x + fa;
            g_leaf[g_nleaf].x1 = o->x + fb;
            g_leaf[g_nleaf].y = o->y + (float)c;
            g_leaf[g_nleaf].z = o->z;
            g_leaf[g_nleaf].obj = i;
            g_nleaf++;
            lv += used;
        }
    }
    g_ndrip = 0;
    for (int i = 0; i < w->nobj; i++) {
        const object_t *o = &w->obj[i];
        const char *dp = sprite_prop(o->spr, "drips");
        if (!dp) continue;
        int a, b, used;
        while (g_ndrip < MAX_DRIP && sscanf(dp, "%d %d%n", &a, &b, &used) == 2) {
            g_drip[g_ndrip].x = o->x + (float)(o->flip ? -a : a);
            g_drip[g_ndrip].y = o->y + (float)b;
            g_drip[g_ndrip].z = o->z + 4;
            g_drip[g_ndrip].t = 0;
            g_drip[g_ndrip].hang = rng_range(&x->rng, 0.8f, 3.0f);
            g_ndrip++;
            dp += used;
        }
    }
    x->wet = 0;
    x->fixed = 0;
    if (g_weather_minutes <= 0.0f) arg = "clear";             /* weather switched off */
    if (arg && strcmp(arg, "auto") != 0) {
        for (int i = 0; i < NPAT; i++)
            if (!strcmp(arg, PAT[i].name)) {
                set_pattern(w, i, 1);
                x->fixed = 1;
                return;
            }
    }
    /* auto: now and then the weather is already there; the first change comes soon */
    int p = rng_chance(&x->rng, 0.35f) ? pick_pattern(w) : 0;
    set_pattern(w, p, 1);
    x->next_change = fmaxf(20.0f, wx_base() * rng_range(&x->rng, 0.1f, 0.5f));      /* the first change comes sooner */
}

void weather_next(world_t *w)
{
    set_pattern(w, pick_pattern(w), 0);
}

/* --- update ------------------------------------------------------------ */

static void update_layer(world_t *w, int k, float dt)
{
    wx_layer_t *L = &w->wx.layer[k];
    if (L->state == WXS_NONE) return;
    float end = 1.0f + L->soft + margin(k) + 0.05f;
    if (L->lead < WX_INF * 0.5f) {
        L->lead += L->speed * dt;
        if (L->lead > end) {
            L->lead = WX_INF;
            if (L->state == WXS_ARRIVE) L->state = WXS_ON;
        }
    }
    if (L->state == WXS_LEAVE) {
        L->tail += L->speed * dt;
        if (L->tail > end) {
            /* gone; if a pattern wants it back in the meantime, a new front comes */
            float again = L->target;
            L->state = WXS_NONE;
            L->amount = 0.0f;
            L->lead = L->tail = -WX_INF;
            if (again > 0.001f) arrive(w, k, again);
            return;
        }
    }
    /* strength changes (without a front) are slow */
    float rate = (k == WX_FOG ? 0.012f : 0.03f) * dt;
    if (L->state != WXS_LEAVE) {
        if (L->amount < L->target) L->amount = fminf(L->target, L->amount + rate);
        else L->amount = fmaxf(L->target, L->amount - rate);
    }
}

static float drop_floor(world_t *w, float x, float z)
{
    if (z >= (float)w->ground_z - 4.0f) return world_ground_at(w, x, z);
    /* far drops end somewhere among the distant trunks */
    return world_ground(w, x) - rng_range(&w->wx.rng, 0.0f, 22.0f);
}

static void add_splash(world_t *w, float x, float y, int z, int ring)
{
    weather_t *wx = &w->wx;
    if (wx->nsplash >= MAX_SPLASH) return;
    wx_splash_t *s = &wx->splash[wx->nsplash++];
    s->x = x; s->y = y; s->z = z; s->age = 0; s->ring = ring;
    s->life = ring ? rng_range(&wx->rng, 0.45f, 0.7f) : rng_range(&wx->rng, 0.08f, 0.16f);
}

static void update_rain(world_t *w, float dt)
{
    weather_t *x = &w->wx;
    const wx_layer_t *L = &x->layer[WX_RAIN];
    float amt = L->state != WXS_NONE ? L->amount : 0.0f;
    /* new drops: about 2.4 per column per second at full rain, only where the rain is */
    x->spawn_acc += dt * 2.4f * (float)w->w * amt;
    while (x->spawn_acc >= 1.0f) {
        x->spawn_acc -= 1.0f;
        if (x->ndrop >= MAX_DROP) { x->spawn_acc = 0; break; }
        float px = rng_range(&x->rng, -30.0f, (float)w->w + 30.0f);
        if (rng_float(&x->rng) * amt >= cover(w, WX_RAIN, px, 0.0f)) continue;
        wx_drop_t *d = &x->drop[x->ndrop++];
        float nearness = rng_chance(&x->rng, 0.6f) ? rng_range(&x->rng, 0.0f, 0.55f) : rng_range(&x->rng, 0.55f, 1.0f);
        d->z = 20.0f + nearness * (float)(w->fg_z + 10 - 20);
        d->vy = 110.0f + nearness * 130.0f + rng_range(&x->rng, -10.0f, 10.0f);
        d->len = 2.0f + nearness * 4.5f;
        d->vx = world_wind_at(w, px) * 45.0f + w->wind_dir * 8.0f;
        d->x = px;
        d->y = rng_range(&x->rng, -24.0f, -2.0f);
        d->floor_y = drop_floor(w, px + d->vx * ((float)w->h / d->vy), d->z);
    }
    for (int i = 0; i < x->ndrop; i++) {
        wx_drop_t *d = &x->drop[i];
        d->vx += (world_wind_at(w, d->x) * 45.0f + w->wind_dir * 8.0f - d->vx) * fminf(1.0f, dt * 2.0f);
        float y0 = d->y;
        d->x += d->vx * dt;
        d->y += d->vy * dt;
        /* a drop at the depth of a big leaf lands on it: a splash on top, the leaf twitches, dry below */
        int hit = 0;
        for (int l = 0; l < g_nleaf && !hit; l++) {
            if (y0 > g_leaf[l].y || d->y < g_leaf[l].y || d->x < g_leaf[l].x0 || d->x > g_leaf[l].x1) continue;
            if (d->z > (float)g_leaf[l].z + 6.0f || d->z < (float)g_leaf[l].z - 14.0f) continue;
            if (rng_chance(&x->rng, 0.7f)) add_splash(w, d->x, g_leaf[l].y, g_leaf[l].z + 3, 0);
            object_t *o = &w->obj[g_leaf[l].obj];
            o->bvel += rng_range(&x->rng, -0.25f, 0.25f);
            hit = 1;
        }
        if (hit) { x->drop[i--] = x->drop[--x->ndrop]; continue; }
        if (d->y >= d->floor_y) {
            if (d->z >= (float)w->ground_z - 4.0f) {
                int ring = 0;
                for (int p = 0; p < g_npud; p++)
                    if (d->x >= g_pud[p].x0 && d->x <= g_pud[p].x1 && fabsf(d->z - (float)g_pud[p].z) < 10.0f) {
                        add_splash(w, d->x, g_pud[p].y, g_pud[p].z + 1, 1);
                        ring = 1;
                        break;
                    }
                if (!ring && rng_chance(&x->rng, 0.6f)) add_splash(w, d->x, d->floor_y, (int)d->z, 0);
            }
            x->drop[i--] = x->drop[--x->ndrop];
            continue;
        }
        if (d->x < -40.0f || d->x > (float)w->w + 40.0f) x->drop[i--] = x->drop[--x->ndrop];
    }
    /* the leaves get wet in the rain and keep dripping for a while after it (~1.5 min) */
    float rain_here = 0;
    for (int i = 0; i < 8; i++) rain_here += cover(w, WX_RAIN, ((float)i + 0.5f) * (float)w->w / 8.0f, 0.0f);
    rain_here /= 8.0f;
    if (rain_here > 0.15f) x->wet = fminf(1.0f, x->wet + dt * 0.08f);
    else x->wet = fmaxf(0.0f, x->wet - dt * 0.011f);
    for (int i = 0; i < g_ndrip; i++) {
        if (x->wet <= 0.02f) { g_drip[i].t = 0; continue; }
        g_drip[i].t += dt * (0.4f + x->wet);
        if (g_drip[i].t < g_drip[i].hang) continue;
        /* the drop lets go */
        g_drip[i].t = 0;
        g_drip[i].hang = rng_range(&x->rng, 0.8f, 3.0f) / (0.3f + x->wet);
        if (x->ndrop >= MAX_DROP) continue;
        wx_drop_t *d = &x->drop[x->ndrop++];
        d->x = g_drip[i].x; d->y = g_drip[i].y + 1.0f; d->z = (float)g_drip[i].z;
        d->vx = 0; d->vy = 70.0f; d->len = 2.0f;
        d->floor_y = world_ground_at(w, d->x, d->z);
    }
    for (int i = 0; i < x->nsplash; i++) {
        x->splash[i].age += dt;
        if (x->splash[i].age >= x->splash[i].life) x->splash[i--] = x->splash[--x->nsplash];
    }
}

void weather_update(world_t *w, float dt)
{
    weather_t *x = &w->wx;
    if (!x->fixed) {
        x->next_change -= dt;
        if (x->next_change <= 0) weather_next(w);
    }
    for (int k = 0; k < WX_COUNT; k++) update_layer(w, k, dt);
    update_rain(w, dt);
}

void weather_describe(const world_t *w, char *out, int cap)
{
    float avg[WX_COUNT] = { 0, 0 };
    for (int k = 0; k < WX_COUNT; k++) {
        for (int i = 0; i < 16; i++) avg[k] += weather_at(w, k, ((float)i + 0.5f) * (float)w->w / 16.0f);
        avg[k] /= 16.0f;
    }
    int f = (int)lroundf(avg[WX_FOG] * 100.0f), r = (int)lroundf(avg[WX_RAIN] * 100.0f);
    if (f == 0 && r == 0) snprintf(out, (size_t)cap, "%s, clear sky", PAT[w->wx.pattern].name);
    else snprintf(out, (size_t)cap, "%s, fog %d%% rain %d%%", PAT[w->wx.pattern].name, f, r);
}

/* --- render ------------------------------------------------------------ */

static void blend(raster_t *r, int x, int y, int z, rgb_t c, float a)
{
    if (x < 0 || y < 0 || x >= r->w || y >= r->h) return;
    int i = y * r->w + x;
    if (r->z[i] > z) return;                    /* something closer is in front */
    r->c[i] = rgb_mix(r->c[i], c, a);
}

static void render_rain(world_t *w, raster_t *r)
{
    const weather_t *x = &w->wx;
    const rgb_t rc = mkrgb(150, 166, 178);
    for (int i = 0; i < x->ndrop; i++) {
        const wx_drop_t *d = &x->drop[i];
        float nearness = (d->z - 20.0f) / (float)(w->fg_z + 10 - 20);
        float a = 0.2f + 0.38f * nearness;
        float sx = d->vx / d->vy;
        for (int k = 0; k < (int)ceilf(d->len); k++) {
            float yy = d->y - (float)k;
            blend(r, (int)floorf(d->x - sx * (float)k + 0.5f), (int)floorf(yy), (int)d->z, rc, a * (1.0f - (float)k / (d->len + 1.0f)));
        }
    }
    /* drops swelling at the leaf tips */
    for (int i = 0; i < g_ndrip; i++) {
        if (x->wet <= 0.02f || g_drip[i].t <= 0) continue;
        float grow = clampf(g_drip[i].t / g_drip[i].hang, 0.0f, 1.0f);
        int dx = (int)floorf(g_drip[i].x + 0.5f), dy = (int)floorf(g_drip[i].y + 0.5f);
        blend(r, dx, dy, g_drip[i].z, mkrgb(170, 190, 200), 0.35f + 0.4f * grow);
        if (grow > 0.6f) blend(r, dx, dy + 1, g_drip[i].z, mkrgb(190, 205, 215), 0.5f * grow);
    }
    for (int i = 0; i < x->nsplash; i++) {
        const wx_splash_t *s = &x->splash[i];
        float t = s->age / s->life;
        int sx = (int)floorf(s->x + 0.5f), sy = (int)floorf(s->y + 0.5f);
        if (s->ring) {
            /* a ring spreading over the water */
            float rx = 1.0f + t * 4.0f, ry = rx * 0.35f;
            float a = 0.45f * (1.0f - t);
            for (int k = 0; k < 16; k++) {
                float ang = (float)k * (6.2831853f / 16.0f);
                blend(r, (int)floorf(s->x + cosf(ang) * rx + 0.5f), (int)floorf(s->y + sinf(ang) * ry + 0.5f), s->z, rc, a);
            }
        } else {
            float a = 0.5f * (1.0f - t);
            blend(r, sx - 1, sy - 1, s->z, rc, a);
            blend(r, sx + 1, sy - 1, s->z, rc, a);
            blend(r, sx, sy - 2, s->z, rc, a * 0.7f);
        }
    }
}

static void render_air(world_t *w, raster_t *r)
{
    const wx_layer_t *F = &w->wx.layer[WX_FOG], *R = &w->wx.layer[WX_RAIN];
    int fog_on = F->state != WXS_NONE && F->amount > 0.001f;
    int rain_on = R->state != WXS_NONE && R->amount > 0.001f;
    if (!fog_on && !rain_on) return;
    int gw = r->w / GRID + 2, gh = r->h / GRID + 2;
    if (gw * gh > g_cap) {
        free(g_fog); free(g_dark);
        g_fog = malloc(sizeof(float) * (size_t)(gw * gh));
        g_dark = malloc(sizeof(float) * (size_t)(gw * gh));
        if (!g_fog || !g_dark) { free(g_fog); free(g_dark); g_fog = g_dark = NULL; g_cap = 0; return; }
        g_cap = gw * gh;
    }
    float h = (float)r->h;
    /* wisps creep with the wind at a few pixels per second (noise units: 1 = ~55 px) */
    float drift = w->t * (0.035f + 0.06f * fabsf(w->wind)) * (w->wind_dir < 0 ? 1.0f : -1.0f);
    for (int gy = 0; gy < gh; gy++)
        for (int gx = 0; gx < gw; gx++) {
            float X = (float)(gx * GRID), Y = (float)(gy * GRID);
            float f = 0.0f, dk = 1.0f;
            if (fog_on) {
                float c = cover(w, WX_FOG, X, Y);
                if (c > 0.002f) {
                    /* wisps drifting with the wind; thicker down at the ground */
                    float wisp = fbm2(X * 0.018f + drift, Y * 0.05f + w->t * 0.002f, 3, 4242u);
                    float gy0 = w->ground[(int)clampf(X, 0.0f, (float)(w->w - 1))];
                    float low = clampf(1.0f - (gy0 - Y) / (h * 0.75f), 0.0f, 1.0f);
                    f = c * (0.2f + 1.15f * wisp * wisp + 0.2f * wisp) * (0.6f + 0.5f * low);
                }
            }
            if (rain_on) dk = 1.0f - 0.22f * cover(w, WX_RAIN, X, Y);
            g_fog[gy * gw + gx] = f;
            g_dark[gy * gw + gx] = dk;
        }
    const rgb_t fc = mkrgb(52, 62, 68);
    float zn = (float)(w->fg_z + 12), zf = 30.0f;
    for (int y = 0; y < r->h; y++) {
        int gy = y / GRID;
        float ty = (float)(y % GRID) / (float)GRID;
        for (int x = 0; x < r->w; x++) {
            int gx = x / GRID;
            float tx = (float)(x % GRID) / (float)GRID;
            int i0 = gy * gw + gx;
            float f = (g_fog[i0] * (1 - tx) + g_fog[i0 + 1] * tx) * (1 - ty)
                    + (g_fog[i0 + gw] * (1 - tx) + g_fog[i0 + gw + 1] * tx) * ty;
            float dk = (g_dark[i0] * (1 - tx) + g_dark[i0 + 1] * tx) * (1 - ty)
                     + (g_dark[i0 + gw] * (1 - tx) + g_dark[i0 + gw + 1] * tx) * ty;
            int i = y * r->w + x;
            rgb_t c = r->c[i];
            if (dk < 0.999f) c = rgb_scale(c, dk);
            if (f > 0.002f) {
                int z = r->z[i];
                float far = z == Z_EMPTY ? 1.0f : clampf((zn - (float)z) / (zn - zf), 0.0f, 1.0f);
                float a = clampf(f * (0.3f + 0.9f * far), 0.0f, 0.92f);
                c = rgb_mix(c, fc, a);
            }
            r->c[i] = c;
        }
    }
}

void weather_render(world_t *w, raster_t *r)
{
    render_rain(w, r);
    render_air(w, r);
}
