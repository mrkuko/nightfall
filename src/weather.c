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
 * Storm: steady heavy rain, no extra wind; this is a screensaver, so the lightning is calm: mostly distant
 * sheet flashes (a dim blue-white glow from the sky, sometimes low on the horizon, sometimes high, now and then
 * a double or triple flicker), every so often a strike into the scene (a jagged bolt behind the trunks, a bit
 * brighter, the creatures turn to the thunder). Rarely a strike leaves the spirit stag standing where it hit;
 * 2-20 s later another strike takes it away. At most once per storm.
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

/* Only for recording timelapses (--weather-timelapse K): the fronts and strength changes run K times faster,
 * everything else (creatures, wind, raindrops, fog wisps) stays in real time. */
float g_weather_timelapse = 1.0f;

/* Width of the soft edge of a front, as a fraction of the screen. Rain has a WIDE edge: first a drop here
 * and there, then more and more, the full rain only well behind the leading edge (no wall of rain). */
static float soft_of(int k) { return k == WX_FOG ? 0.35f : 0.9f; }

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
    { "storm",   0.12f, 0.80f, 1.0f },
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
    float n = (vnoise2(y * 0.045f + (float)k * 17.0f, w->t * 0.012f, 911u) - 0.5f) * fminf(L->soft, 0.35f) * 1.2f
            + (vnoise2(y * 0.16f, w->t * 0.04f, 913u) - 0.5f) * 0.03f;
    if (k == WX_FOG) n += (y / h - 0.6f) * 0.22f;
    float e = 1.0f;
    if (L->lead < WX_INF * 0.5f) e *= edge(L->lead + n - u, L->soft);
    if (L->tail > -WX_INF * 0.5f) e *= edge(u - L->tail - n, L->soft);
    if (k == WX_RAIN) e *= e;                  /* a long thin start: single drops, then a shower, then the rain */
    return L->amount * e;
}

float weather_at(const world_t *w, int layer, float x)
{
    if (layer < 0 || layer >= WX_COUNT) return 0.0f;
    return cover(w, layer, x, (float)w->h * 0.6f);
}

static float wind_sign(const world_t *w) { return w->wind_dir < 0 ? -1.0f : 1.0f; }

/* how far off-screen a new edge starts / an old one ends (the ragged noise and the fog's ground lead) */
static float margin(int k) { return k == WX_FOG ? 0.3f : 0.25f; }

static void arrive(world_t *w, int k, float target)
{
    weather_t *x = &w->wx;
    wx_layer_t *L = &x->layer[k];
    L->dir = wind_sign(w);
    L->soft = soft_of(k);
    /* screen widths per second; rain: the leading edge crosses in 7-12 s, the full rain follows ~soft behind */
    L->speed = 1.0f / (k == WX_FOG ? rng_range(&x->rng, 240.0f, 420.0f) : rng_range(&x->rng, 7.0f, 12.0f));
    /* the very tip of the edge starts at the screen side right away (not a soft-width off-screen first,
     * which with fog meant minutes of nothing) */
    L->lead = -margin(k);
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
            /* the trailing edge starts where it thins nothing yet (its ragged noise and the fog's ground lead
             * reach a margin further in; starting at -soft cut a visible step at the screen side in one frame).
             * Until it reaches -soft it moves faster (update_layer), so the thinning still starts soon. */
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
            L->soft = soft_of(k);
            L->speed = k == WX_FOG ? 1.0f / 320.0f : 1.0f / 10.0f;
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
    if (instant) x->storm_k = !strcmp(PAT[p].name, "storm") ? 1.0f : 0.0f;
    if (!strcmp(PAT[p].name, "storm")) {
        x->stag_done = 0;
        x->lt_next = rng_range(&x->rng, 6.0f, 14.0f);
        x->lt_since = 0;
    }
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
    x->lt_t = -1.0f;
    x->blue_t = -1.0f;
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
    /* "in:NAME" (--weather-in): it starts clear and NAME rolls in as a front, then stays (screenshots, timelapses) */
    if (arg && !strncmp(arg, "in:", 3)) {
        for (int i = 0; i < NPAT; i++)
            if (!strcmp(arg + 3, PAT[i].name)) {
                set_pattern(w, 0, 1);
                set_pattern(w, i, 0);
                x->fixed = 1;
                return;
            }
    }
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

/* Playground keys: pattern number p rolls in as fronts and stays (no automatic change); p < 0 = back to auto. */
void weather_set(world_t *w, int p)
{
    if (p < 0) {
        w->wx.fixed = 0;
        w->wx.next_change = fmaxf(15.0f, wx_base() * rng_range(&w->wx.rng, 0.6f, 1.4f));
        world_log(w, "weather → auto");
        return;
    }
    if (p >= NPAT) return;
    set_pattern(w, p, 0);
    w->wx.fixed = 1;
}

int weather_count(void) { return NPAT; }

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
        L->tail += L->speed * dt * (L->tail < -L->soft ? 4.0f : 1.0f);
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

/* --- lightning ---------------------------------------------------------- */

static int storm_on(const world_t *w)
{
    return !strcmp(PAT[w->wx.pattern].name, "storm") && w->wx.layer[WX_RAIN].amount > 0.35f;
}

/* A jagged bolt from the clouds down to (bx, by): the core in steps of a few px, one or two branches. */
static void make_bolt(weather_t *x, float bx, float by)
{
    x->lt_nseg = 0;
    float px = bx + rng_range(&x->rng, -14.0f, 14.0f), py = -2.0f;
    float mid[16][2];
    int nm = 0;
    while (py < by && x->lt_nseg < 40) {
        float ny = fminf(by, py + rng_range(&x->rng, 3.0f, 7.0f));
        float t = ny / fmaxf(by, 1.0f);
        float nx = px + rng_range(&x->rng, -3.5f, 3.5f) + (bx - px) * 0.25f * t;
        if (ny >= by) nx = bx;
        float *s = x->lt_seg[x->lt_nseg++];
        s[0] = px; s[1] = py; s[2] = nx; s[3] = ny;
        if (nm < 16 && t > 0.2f && t < 0.75f) { mid[nm][0] = nx; mid[nm][1] = ny; nm++; }
        px = nx; py = ny;
    }
    int nb = nm ? rng_between(&x->rng, 1, 2) : 0;
    for (int b = 0; b < nb; b++) {
        float *m = mid[rng_int(&x->rng, nm)];
        float qx = m[0], qy = m[1], dir = rng_chance(&x->rng, 0.5f) ? -1.0f : 1.0f;
        int steps = rng_between(&x->rng, 2, 5);
        for (int k = 0; k < steps && x->lt_nseg < 64; k++) {
            float nx = qx + dir * rng_range(&x->rng, 2.0f, 5.0f), ny = qy + rng_range(&x->rng, 2.0f, 5.0f);
            float *s = x->lt_seg[x->lt_nseg++];
            s[0] = qx; s[1] = qy; s[2] = nx; s[3] = ny;
            qx = nx; qy = ny;
        }
    }
}

static void start_flash(world_t *w, int strike, float bx, int stag)
{
    weather_t *x = &w->wx;
    x->lt_t = 0.0f;
    x->lt_strike = strike;
    x->lt_np = rng_chance(&x->rng, strike ? 0.6f : 0.35f) ? rng_between(&x->rng, 2, 3) : 1;
    float at = 0.0f;
    for (int i = 0; i < x->lt_np; i++) {
        x->lt_pulse[i][0] = at;
        x->lt_pulse[i][1] = rng_range(&x->rng, 0.07f, 0.18f);
        x->lt_pulse[i][2] = i == 0 ? 1.0f : rng_range(&x->rng, 0.45f, 0.9f);
        at += x->lt_pulse[i][1] + rng_range(&x->rng, 0.05f, 0.22f);
    }
    if (strike) {
        /* a strike: brighter (still restrained), the bolt behind the trunks of the clearing, thunder */
        x->lt_int = rng_range(&x->rng, 0.32f, 0.45f);
        x->lt_cx = bx / (float)w->w;
        x->lt_reach = rng_range(&x->rng, 0.7f, 1.0f);
        x->lt_z = w->ground_z + 4;
        float gy = world_ground_at(w, bx, (float)(w->ground_z + 8));
        make_bolt(x, bx, gy);
        x->lt_since = 0.0f;
        world_stimulus(w, bx, gy, (float)w->w, ST_NOISE, 1.0f, 0.6f, -1);
        world_log(w, "lightning strikes at x=%d", (int)bx);
        if (stag) {
            int i = actor_spawn(w, "spirit_stag", bx, 0, NAV_GROUND, w->ground_z + 8);
            if (i >= 0) {
                actor_t *a = &w->act[i];
                a->facing = rng_chance(&x->rng, 0.5f) ? 1 : -1;
                step_t st[2];
                memset(st, 0, sizeof st);
                st[0].kind = ST_ANIM; st[0].anim = A_APPEAR; st[0].dur = 0; st[0].place = -1;
                st[1].kind = ST_ANIM; st[1].anim = A_LOOK; st[1].dur = rng_range(&x->rng, 1.5f, 3.0f); st[1].place = -1;
                actor_script(w, a, IN_LOOK, st, 2);
                x->stag = (i << 16) | (int)(a->id & 0xffff);
                x->stag_left = rng_range(&x->rng, 5.0f, 30.0f);
                x->stag_done = 1;
                x->blue_t = 0.0f;
                x->blue_k = 1.0f;
                world_log(w, "… and in its light a stag of blue fire stands there");
            }
        }
    } else {
        /* far away: a dim sheet glow from the sky, sometimes low on the horizon, sometimes high */
        x->lt_int = rng_range(&x->rng, 0.10f, 0.26f);
        x->lt_cx = rng_range(&x->rng, -0.1f, 1.1f);
        x->lt_reach = rng_range(&x->rng, 0.25f, 0.85f);
        x->lt_nseg = 0;
    }
}

/* the spirit stag, or NULL when it is gone */
static actor_t *the_stag(world_t *w)
{
    int slot = w->wx.stag >> 16;
    if (!w->wx.stag || slot < 0 || slot >= w->nact) return NULL;
    actor_t *a = &w->act[slot];
    return a->alive && (a->id & 0xffff) == (unsigned)(w->wx.stag & 0xffff) ? a : NULL;
}

int weather_strike(world_t *w, int stag)
{
    weather_t *x = &w->wx;
    if (stag && the_stag(w)) return 0;
    float bx = rng_range(&x->rng, (float)w->w * 0.15f, (float)w->w * 0.85f);
    start_flash(w, 1, bx, stag);
    return 1;
}

static void update_lightning(world_t *w, float dt)
{
    weather_t *x = &w->wx;
    if (x->lt_t >= 0.0f) {
        x->lt_t += dt;
        float end = x->lt_pulse[x->lt_np - 1][0] + x->lt_pulse[x->lt_np - 1][1] + 0.4f;
        if (x->lt_t > end) x->lt_t = -1.0f;
    }
    x->lt_since += dt;
    if (x->blue_t >= 0.0f && (x->blue_t += dt) > 2.5f) x->blue_t = -1.0f;
    /* the stag waits for the strike that takes it away (also after the storm has passed) */
    actor_t *st = the_stag(w);
    if (st) {
        x->stag_left -= dt;
        if (x->stag_left <= 0 && x->lt_t < 0.0f && st->intent != IN_VANISH) {
            start_flash(w, 1, st->x, 0);
            step_t s2[2];
            memset(s2, 0, sizeof s2);
            s2[0].kind = ST_ANIM; s2[0].anim = A_VANISH; s2[0].dur = 0; s2[0].place = -1;
            s2[1].kind = ST_DESPAWN; s2[1].place = -1;
            actor_script(w, st, IN_VANISH, s2, 2);
            x->blue_t = 0.0f;
            x->blue_k = 0.55f;
            world_log(w, "another strike — and the stag is gone");
        }
    } else {
        x->stag = 0;
    }
    if (!storm_on(w)) return;
    x->lt_next -= dt;
    if (x->lt_next > 0 || x->lt_t >= 0.0f) return;
    x->lt_next = rng_range(&x->rng, 5.0f, 16.0f);
    int strike = x->lt_since > 10.0f && rng_chance(&x->rng, 0.40f);
    if (strike) {
        int stag = !x->stag_done && !st && rng_chance(&x->rng, 0.05f);      /* only now and then a strike brings it */
        weather_strike(w, stag);
    } else {
        start_flash(w, 0, 0.0f, 0);
    }
}

/* the flash's brightness now (0..1 × lt_int) */
static float flash_level(const weather_t *x)
{
    if (x->lt_t < 0.0f) return 0.0f;
    float L = 0.0f;
    for (int i = 0; i < x->lt_np; i++) {
        float u = (x->lt_t - x->lt_pulse[i][0]) / x->lt_pulse[i][1];
        if (u < 0.0f) continue;
        float v = u < 0.15f ? u / 0.15f : powf(fmaxf(0.0f, 1.0f - (u - 0.15f) / 2.2f), 2.0f);
        L = fmaxf(L, v * x->lt_pulse[i][2]);
    }
    return L * x->lt_int;
}

void weather_update(world_t *w, float dt)
{
    update_lightning(w, dt);
    {   /* the storm's extra dimming eases in and out slowly (about 10 s either way) */
        float want = !strcmp(PAT[w->wx.pattern].name, "storm") ? 1.0f : 0.0f;
        float st = dt / 10.0f;
        w->wx.storm_k += clampf(want - w->wx.storm_k, -st, st);
    }
    weather_t *x = &w->wx;
    if (!x->fixed) {
        x->next_change -= dt;
        if (x->next_change <= 0) weather_next(w);
    }
    for (int k = 0; k < WX_COUNT; k++) update_layer(w, k, dt * g_weather_timelapse);
    /* the wisps' offset is the integral of their speed: with `t * speed` a small wind change (or the wind
     * turning) would move the whole fog pattern by t × the difference in a single frame. The wind's sign is
     * smoothed so the creep slows down, stops and turns instead of flipping. */
    x->drift += dt * (0.035f + 0.06f * fabsf(w->wind)) * clampf(-w->wind_dir * 4.0f, -1.0f, 1.0f);
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
    float storm = w->wx.storm_k;
    /* wisps creep with the wind at a few pixels per second (noise units: 1 = ~55 px) */
    float drift = w->wx.drift;
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
            if (rain_on) dk = 1.0f - (0.22f + 0.40f * storm) * cover(w, WX_RAIN, X, Y);   /* a storm dims the scene more */
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

static void render_lightning(world_t *w, raster_t *r)
{
    const weather_t *x = &w->wx;
    float L = flash_level(x);
    if (L <= 0.003f) return;
    /* the wash: from the sky down, strongest where the flash is; the far background lights up more than what
     * stands close (the trunks stay dark against it, a moment of hard silhouette) */
    const rgb_t fc = mkrgb(176, 198, 255);
    float H = (float)r->h, reach = x->lt_reach * H, cx = x->lt_cx * (float)r->w;
    for (int y = 0; y < r->h; y++) {
        float vy = clampf(1.0f - (float)y / fmaxf(reach, 1.0f), 0.0f, 1.0f);
        vy = 0.12f + 0.88f * vy * vy;
        for (int xx = 0; xx < r->w; xx++) {
            float dx = ((float)xx - cx) / ((float)r->w * 0.45f);
            float hx = 0.45f + 0.55f * expf(-dx * dx);
            int i = y * r->w + xx;
            int z = r->z[i];
            float far = (z == Z_EMPTY || z < w->ground_z - 20) ? 1.0f : 0.5f;
            float a = clampf(L * vy * hx * far, 0.0f, 0.6f);
            r->c[i] = rgb_mix(r->c[i], fc, a);
        }
    }
    /* the bolt: a white-blue core with a soft halo, behind whatever stands in front of it; only in the first,
     * brightest moments of the strike */
    if (!x->lt_strike || x->lt_t > x->lt_pulse[0][1] + 0.25f) return;
    const rgb_t core = mkrgb(236, 244, 255), halo = mkrgb(140, 180, 255);
    float k = L / fmaxf(x->lt_int, 0.01f);
    for (int s = 0; s < x->lt_nseg; s++) {
        const float *g = x->lt_seg[s];
        float len = fmaxf(fabsf(g[2] - g[0]), fabsf(g[3] - g[1]));
        int n = (int)len + 1;
        for (int j = 0; j <= n; j++) {
            float t = (float)j / (float)n;
            int px = (int)lroundf(g[0] + (g[2] - g[0]) * t), py = (int)lroundf(g[1] + (g[3] - g[1]) * t);
            if (px < 0 || py < 0 || px >= r->w || py >= r->h) continue;
            for (int oy = -1; oy <= 1; oy++)
                for (int ox = -1; ox <= 1; ox++) {
                    if (!ox && !oy) continue;
                    blend(r, px + ox, py + oy, x->lt_z, halo, 0.35f * k);
                }
            blend(r, px, py, x->lt_z, core, clampf(0.4f + 0.6f * k, 0.0f, 1.0f));
        }
    }
}

/* The spirit stag: a soft blue aura that follows its outline (pulsing slowly) and a faint glow on the ground at
 * its feet; behind whatever stands in front of it. */
static void render_spirit(world_t *w, raster_t *r)
{
    actor_t *a = the_stag(w);
    if (!a || a->hidden) return;
    const frame_t *f = &a->spr->frames[a->ov_frame ? a->ov_frame - 1 : clip_frame_at(&a->spr->clips[a->clip], a->ct)];
    int ax = (int)floorf(a->x + 0.5f), ay = (int)floorf(a->y + 0.5f), z = (int)floorf(a->zf + 0.5f);
    float pulse = 0.8f + 0.2f * sinf(w->t * 1.7f);
    const rgb_t aura = mkrgb(70, 150, 255);
    static const float fall[4] = { 0.0f, 0.16f, 0.09f, 0.045f };
    /* the ground glow: a flat ellipse of pale blue light under it */
    for (int dy = -2; dy <= 2; dy++)
        for (int dx = -26; dx <= 26; dx++) {
            float e = (float)(dx * dx) / (26.0f * 26.0f) + (float)(dy * dy) / 6.0f;
            if (e < 1.0f) blend(r, ax + dx, ay + dy, z, aura, 0.14f * (1.0f - e) * pulse);
        }
    for (int fy = 0; fy < f->h; fy++)
        for (int fx = 0; fx < f->w; fx++) {
            if (!f->px[fy * f->w + fx].a) continue;
            int px = ax + (a->facing < 0 ? -(fx - f->ax) : fx - f->ax), py = ay + fy - f->ay;
            for (int oy = -3; oy <= 3; oy++)
                for (int ox = -3; ox <= 3; ox++) {
                    int d = abs(ox) > abs(oy) ? abs(ox) : abs(oy);
                    if (!d) continue;
                    int qx = fx + (a->facing < 0 ? -ox : ox), qy = fy + oy;
                    if (qx >= 0 && qy >= 0 && qx < f->w && qy < f->h && f->px[qy * f->w + qx].a) continue;   /* only outside */
                    blend(r, px + ox, py + oy, z, aura, fall[d] * 0.35f * pulse);
                }
        }
}

/* The stag's strike: a slow blue flash across the whole screen, a bloom around where it stands. */
static void render_blue(world_t *w, raster_t *r)
{
    const weather_t *x = &w->wx;
    if (x->blue_t < 0.0f) return;
    float t = x->blue_t;
    float k = (t < 0.12f ? t / 0.12f : powf(fmaxf(0.0f, 1.0f - (t - 0.12f) / 2.3f), 1.6f)) * x->blue_k;
    if (k <= 0.004f) return;
    actor_t *a = the_stag(w);
    float cx = a ? a->x : x->lt_cx * (float)r->w, cy = a ? a->y - 30.0f : (float)r->h * 0.6f;
    const rgb_t blue = mkrgb(70, 120, 255), white = mkrgb(200, 225, 255);
    float R = (float)r->w * 0.3f;
    for (int y = 0; y < r->h; y++)
        for (int xx = 0; xx < r->w; xx++) {
            float dx = (float)xx - cx, dy = ((float)y - cy) * 1.3f;
            float bloom = 1.0f / (1.0f + (dx * dx + dy * dy) / (R * R));
            int i = y * r->w + xx;
            rgb_t c = rgb_mix(r->c[i], blue, clampf(k * (0.22f + 0.30f * bloom), 0.0f, 0.7f));
            r->c[i] = rgb_mix(c, white, clampf(k * 0.25f * bloom * bloom, 0.0f, 0.5f));
        }
}

void weather_render(world_t *w, raster_t *r)
{
    render_rain(w, r);
    render_air(w, r);
    render_spirit(w, r);
    render_lightning(w, r);
    render_blue(w, r);
}
