/* world.c — environment: objects, places, wind, stimuli, rendering */
#include "world.h"
#include "noise.h"

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void world_init(world_t *w, library_t *lib, int width, int height, uint64_t seed)
{
    memset(w, 0, sizeof *w);
    w->lib = lib;
    w->w = width < 1024 ? width : 1024;
    w->h = height;
    w->rng = rng_seed(seed);
    w->ground_z = 100;
    w->fg_z = 130;
    w->next_id = 1;
    w->next_spawn = 1.0f;
    w->wind_target = rng_chance(&w->rng, 0.5f) ? rng_range(&w->rng, 0.6f, 1.0f) : -rng_range(&w->rng, 0.6f, 1.0f);
    w->wind_dir = w->wind_target;
    w->wind_timer = rng_range(&w->rng, 60, 180);
    w->wave_pos = rng_range(&w->rng, 0, 1000);
    /* gently undulating ground in the lower quarter */
    uint32_t ns = rng_u32(&w->rng);
    float base = (float)height * 0.80f;
    for (int x = 0; x < w->w; x++)
        w->ground[x] = base + (fbm2((float)x * 0.02f, 0.5f, 3, ns) - 0.5f) * (float)height * 0.10f;
}

void world_free(world_t *w)
{
    if (w->bake.c) raster_free(&w->bake);
    w->bake_ok = 0;
    for (int i = 0; i < w->nobj; i++)
        if (w->obj[i].owned) sprite_free(w->obj[i].spr);
    w->nobj = 0;
}

float world_ground(const world_t *w, float x)
{
    int xi = (int)floorf(x);
    if (xi < 0) xi = 0;
    if (xi >= w->w) xi = w->w - 1;
    return w->ground[xi];
}

void world_log(world_t *w, const char *fmt, ...)
{
    char msg[96];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(msg, sizeof msg, fmt, ap);
    va_end(ap);
    if (w->nlog == 6) {
        memmove(w->log[0], w->log[1], sizeof w->log[0] * 5);
        w->nlog = 5;
    }
    snprintf(w->log[w->nlog++], sizeof w->log[0], "%6.1f %.80s", (double)w->t, msg);
}

/* --- objects --------------------------------------------------------- */

static void object_bounds(object_t *o)
{
    const frame_t *f = &o->spr->frames[o->spr->clips[o->base_clip].seq[0]];
    int l = (int)o->x - f->ax, r = (int)o->x + (f->w - f->ax);
    if (o->flip) { l = (int)o->x - (f->w - f->ax); r = (int)o->x + f->ax; }
    o->bx0 = l;
    o->bx1 = r;
}

int world_add_object(world_t *w, const char *sprite, float x, float y, int z, int flip, uint64_t seed)
{
    if (w->nobj >= MAX_OBJ) return -1;
    sprite_t *base = lib_find(w->lib, sprite);
    if (!base) { world_log(w, "missing shape '%s'", sprite); return -1; }
    object_t *o = &w->obj[w->nobj];
    memset(o, 0, sizeof *o);
    if (base->gen && seed) {
        /* procedural shape: an own instance from the given seed */
        sprite_t *s = sprite_new(base->name);
        snprintf(s->tags, sizeof s->tags, "%s", base->tags);
        snprintf(s->src, sizeof s->src, "%s", base->src);
        s->gen = base->gen;
        sprite_regen(s, seed);
        o->spr = s;
        o->owned = 1;
    } else {
        o->spr = base;
    }
    o->x = x; o->y = y; o->z = z; o->flip = flip;
    w->bake_ok = 0;
    o->base_clip = sprite_clip_index(o->spr, "idle");
    if (o->base_clip < 0) o->base_clip = 0;
    o->sway_clip = sprite_clip_index(o->spr, "sway");
    o->is_static = o->sway_clip < 0 && o->spr->nframes == 1;
    o->clip = o->base_clip;
    o->phase = rng_range(&w->rng, 0, 100);
    o->stiff = rng_range(&w->rng, 0.6f, 1.2f);
    if (sprite_has_tag(o->spr, "hanging") || sprite_has_tag(o->spr, "canopy") || sprite_has_tag(o->spr, "big"))
        o->stiff *= 1.7f;
    object_bounds(o);
    return w->nobj++;
}

void world_play_object_clip(world_t *w, int obj, const char *clip)
{
    if (obj < 0 || obj >= w->nobj) return;
    object_t *o = &w->obj[obj];
    int c = sprite_clip_index(o->spr, clip);
    if (c < 0) return;
    o->clip = c;
    o->t = 0;
    /* "open*" clips hold the last frame until "close*" arrives */
    o->oneshot = strncmp(clip, "open", 4) == 0 ? 2 : 1;
}

int world_add_place(world_t *w, int type, float x, float y, int z, int nav, int obj, int sub)
{
    if (w->nplace >= MAX_PLACE) return -1;
    place_t *p = &w->place[w->nplace];
    memset(p, 0, sizeof *p);
    p->type = type; p->x = x; p->y = y; p->z = z; p->nav = nav;
    p->obj = obj; p->sub = sub; p->occupant = -1;
    p->climb_x = x;
    p->path = -1;
    return w->nplace++;
}

static int prop_xy(const sprite_t *s, const char *key, int *x, int *y)
{
    const char *v = sprite_prop(s, key);
    return v && sscanf(v, "%d %d", x, y) == 2;
}

void world_places_from_object(world_t *w, int oi)
{
    object_t *o = &w->obj[oi];
    const sprite_t *s = o->spr;
    int dx, dy, fl = o->flip ? -1 : 1;
    const char *zi = sprite_prop(s, "z.inside");
    int zin = zi ? atoi(zi) : -6;
    char key[NF_NAME];
    for (int k = 0; k < 4; k++) {
        snprintf(key, sizeof key, "point.peek%d", k);
        if (prop_xy(s, key, &dx, &dy))
            world_add_place(w, PL_PEEK, o->x + (float)(dx * fl), o->y + (float)dy, o->z + zin, NAV_HIDDEN, oi, k);
    }
    if (prop_xy(s, "point.entry", &dx, &dy)) {
        /* opening index for the clip rustle%d = number of peek openings */
        const char *pk = sprite_prop(s, "peeks");
        int sub = pk ? atoi(pk) : 0;
        world_add_place(w, PL_ENTRY, o->x + (float)(dx * fl), world_ground(w, o->x + (float)(dx * fl)),
                        o->z + zin, NAV_GROUND, oi, sub);
    }
    if (prop_xy(s, "point.top", &dx, &dy) &&
        (sprite_has_tag(s, "bush") || sprite_has_tag(s, "stump") || sprite_has_tag(s, "large")))
        world_add_place(w, PL_PERCH, o->x + (float)(dx * fl), o->y + (float)dy, o->z + 4, NAV_PERCH, oi, 0);
    if (prop_xy(s, "point.under", &dx, &dy))
        world_add_place(w, PL_UNDER, o->x + (float)(dx * fl), world_ground(w, o->x + (float)(dx * fl)),
                        o->z - 4, NAV_GROUND, oi, 0);
    for (int k = 0; k < 4; k++) {
        snprintf(key, sizeof key, "point.perch%d", k);
        if (prop_xy(s, key, &dx, &dy))
            world_add_place(w, PL_PERCH, o->x + (float)(dx * fl), o->y + (float)dy, o->z + 3, NAV_PERCH, oi, k);
    }
    /* paths: path.climbN / path.walkN = "x y x y …" relative to the anchor */
    for (int k = 0; k < 8; k++) {
        for (int kind = 0; kind < 3; kind++) {
            static const char *pk[] = { "path.climb%d", "path.walk%d", "path.hang%d" };
            snprintf(key, sizeof key, pk[kind], k);
            const char *v = sprite_prop(s, key);
            if (!v || w->npath >= MAX_PATH) continue;
            navpath_t *np = &w->path[w->npath];
            memset(np, 0, sizeof *np);
            np->kind = kind;
            np->obj = oi;
            np->z = o->z + (kind == PATH_CLIMB ? 5 : kind == PATH_WALK ? 4 : 2);
            int px, py, used;
            while (np->n < PATH_PTS && sscanf(v, "%d %d%n", &px, &py, &used) == 2) {
                np->x[np->n] = o->x + (float)(px * fl);
                np->y[np->n] = o->y + (float)py;
                np->n++;
                v += used;
            }
            if (np->n < 2) continue;
            for (int i = 1; i < np->n; i++)
                np->len += hypotf(np->x[i] - np->x[i - 1], np->y[i] - np->y[i - 1]);
            w->npath++;
        }
    }
    const char *cl = sprite_prop(s, "climb");
    int lane = 0;
    if (cl) {
        int a, b;
        if (sscanf(cl, "%d %d", &a, &b) == 2) lane = (a + b) / 2 * fl;
        /* trunk base: climbing starts here */
        float bx = o->x + (float)lane + (float)(rng_between(&w->rng, -2, 2));
        int pi = world_add_place(w, PL_TRUNK, bx, world_ground(w, bx), o->z + 5, NAV_GROUND, oi, 0);
        if (pi >= 0) w->place[pi].climb_x = bx;
    }
    if (prop_xy(s, "point.hollow", &dx, &dy)) {
        int pi = world_add_place(w, PL_HOLLOW, o->x + (float)(dx * fl), o->y + (float)dy, o->z + 0, NAV_CLIMB, oi, 0);
        if (pi >= 0) {
            w->place[pi].climb_x = o->x + (float)(dx * fl);
            int ex, ey, ew, eh;
            if (prop_xy(s, "point.hollow_eye", &ex, &ey)) {
                w->place[pi].has_eye = 1;
                w->place[pi].ex = o->x + (float)(ex * fl);
                w->place[pi].ey = o->y + (float)ey;
                const char *hs = sprite_prop(s, "hollow_size");
                if (hs && sscanf(hs, "%d %d", &ew, &eh) == 2) { w->place[pi].ew = ew; w->place[pi].eh = eh; }
            }
        }
    }
}

void path_point(const navpath_t *p, float t, float *x, float *y)
{
    if (t <= 0) { *x = p->x[0]; *y = p->y[0]; return; }
    for (int i = 1; i < p->n; i++) {
        float seg = hypotf(p->x[i] - p->x[i - 1], p->y[i] - p->y[i - 1]);
        if (t <= seg || i == p->n - 1) {
            float u = seg > 0 ? fminf(t / seg, 1.0f) : 0;
            *x = p->x[i - 1] + (p->x[i] - p->x[i - 1]) * u;
            *y = p->y[i - 1] + (p->y[i] - p->y[i - 1]) * u;
            return;
        }
        t -= seg;
    }
    *x = p->x[p->n - 1];
    *y = p->y[p->n - 1];
}

float path_nearest(const navpath_t *p, float x, float y, float *dist)
{
    float best = 1e9f, bt = 0, acc = 0;
    for (int i = 1; i < p->n; i++) {
        float ax = p->x[i - 1], ay = p->y[i - 1], vx = p->x[i] - ax, vy = p->y[i] - ay;
        float vv = vx * vx + vy * vy, seg = sqrtf(vv);
        float u = vv > 0 ? clampf(((x - ax) * vx + (y - ay) * vy) / vv, 0, 1) : 0;
        float dx = ax + vx * u - x, dy = ay + vy * u - y;
        float d = dx * dx + dy * dy;
        if (d < best) { best = d; bt = acc + u * seg; }
        acc += seg;
    }
    if (dist) *dist = sqrtf(best);
    return bt;
}

void world_link_paths(world_t *w)
{
    w->nlink = 0;
    /* endpoints of one path near another path -> link (branch on a trunk, trunk on a trunk …) */
    for (int a = 0; a < w->npath; a++)
        for (int b = 0; b < w->npath; b++) {
            if (a == b || w->nlink >= MAX_LINK) continue;
            navpath_t *pa = &w->path[a], *pb = &w->path[b];
            if (pa->kind == PATH_HANG || pb->kind == PATH_HANG) continue;
            for (int end = 0; end < 2; end++) {
                float ex = end ? pa->x[pa->n - 1] : pa->x[0], ey = end ? pa->y[pa->n - 1] : pa->y[0];
                float d, tb = path_nearest(pb, ex, ey, &d);
                float lim = (pa->kind != pb->kind) ? 12.0f : 4.0f;
                if (d > lim) continue;
                /* do not add a duplicate reverse link */
                int dup = 0;
                for (int i = 0; i < w->nlink; i++)
                    if ((w->link[i].a == b && w->link[i].b == a) || (w->link[i].a == a && w->link[i].b == b)) dup = 1;
                if (dup) continue;
                w->link[w->nlink++] = (navlink_t){ a, b, end ? pa->len : 0.0f, tb };
            }
        }
    /* hollows and entrances at height -> nearest climbing path of the same object */
    for (int i = 0; i < w->nplace; i++) {
        place_t *pl = &w->place[i];
        if (pl->type != PL_HOLLOW && pl->type != PL_PERCH) continue;
        float bd = 1e9f;
        for (int k = 0; k < w->npath; k++) {
            if (w->path[k].kind == PATH_HANG) continue;
            float d, t = path_nearest(&w->path[k], pl->x, pl->y, &d);
            if (d < bd && d < 10.0f) { bd = d; pl->path = k; pl->path_t = t; }
        }
    }
}

void world_stimulus(world_t *w, float x, float y, float r, int kind, float strength, float ttl, int src)
{
    /* same source and kind -> refresh */
    for (int i = 0; i < w->nstim; i++)
        if (w->stim[i].src == src && src >= 0 && w->stim[i].kind == kind) {
            w->stim[i] = (stim_t){ x, y, r, kind, strength, ttl, src };
            return;
        }
    if (w->nstim >= MAX_STIM) return;
    w->stim[w->nstim++] = (stim_t){ x, y, r, kind, strength, ttl, src };
}

/* --- update ---------------------------------------------------------- */

static void update_objects(world_t *w, float dt)
{
    for (int i = 0; i < w->nobj; i++) {
        object_t *o = &w->obj[i];
        o->t += dt;
        if (o->oneshot == 1) {
            const clip_t *c = &o->spr->clips[o->clip];
            if (o->t >= clip_duration(c)) {
                o->oneshot = 0;
                o->clip = o->base_clip;
                o->t = 0;
            }
        }
    }
}

/* Creatures on the ground set plants in motion as they pass through: the push in the movement
 * direction goes into the same spring as the wind, so the plant bends, returns with a delay and rings out. */
static void touch_plants(world_t *w)
{
    for (int i = 0; i < w->nobj; i++) w->obj[i].push = 0;
    for (int a = 0; a < w->nact; a++) {
        actor_t *ac = &w->act[a];
        if (!ac->alive || ac->hidden || ac->nav != NAV_GROUND || fabsf(ac->moved) < 0.01f) continue;
        for (int i = 0; i < w->nobj; i++) {
            object_t *o = &w->obj[i];
            if (o->sway_clip < 0) continue;
            if (ac->x < (float)o->bx0 + 2 || ac->x > (float)o->bx1 - 2) continue;
            if (fabsf((float)o->z - ac->zf) > 40) continue;
            float mid = ((float)o->bx0 + (float)o->bx1) * 0.5f;
            float half = ((float)o->bx1 - (float)o->bx0) * 0.5f;
            float k = 1.0f - fabsf(ac->x - mid) / (half + 0.01f);
            if (k <= 0) continue;
            o->push += (ac->moved > 0 ? 1.0f : -1.0f) * (0.6f + 0.8f * k);
        }
    }
}

float world_wind_at(const world_t *w, float x)
{
    float dir = w->wind_dir;
    float sgn = dir < 0 ? -1.0f : 1.0f;
    /* waves travel in the wind direction: u decreases over time for sgn > 0 -> the crest moves right */
    float u = x * sgn - w->wave_pos;
    float wave = 0.5f + 0.5f * sinf(u * 0.045f);
    float env = 0.6f + 0.4f * sinf(u * 0.013f + 1.7f);
    float gust = wave * wave * env;
    float turb = vnoise1s(x * 0.08f + w->t * 0.7f, 77u) * 0.12f;
    return dir * w->wind_str * (0.3f + 1.0f * gust) + turb * w->wind_str;
}

static void update_wind(world_t *w, float dt)
{
    /* direction: a new target every 1–3 min; reversing only occasionally, the transition takes tens of seconds */
    w->wind_timer -= dt;
    if (w->wind_timer <= 0) {
        w->wind_timer = rng_range(&w->rng, 60, 180);
        float sgn = w->wind_target < 0 ? -1.0f : 1.0f;
        if (rng_chance(&w->rng, 0.3f)) sgn = -sgn;
        w->wind_target = sgn * rng_range(&w->rng, 0.55f, 1.0f);
    }
    w->wind_dir += (w->wind_target - w->wind_dir) * fminf(1.0f, dt * 0.05f);
    /* strength: slow undulation, never completely calm */
    w->wind_str = 0.4f + 0.6f * (vnoise1s(w->t * 0.015f, 31u) * 0.5f + 0.5f);
    w->wave_pos += dt * (10.0f + 14.0f * w->wind_str);
    w->wind = w->wind_dir * w->wind_str;
    w->gust = world_wind_at(w, (float)w->w * 0.5f);
}

static void update_plants(world_t *w, float dt)
{
    for (int i = 0; i < w->nobj; i++) {
        object_t *o = &w->obj[i];
        if (o->sway_clip < 0) continue;
        float target = world_wind_at(w, o->x) / o->stiff + clampf(o->push, -1.6f, 1.6f);
        /* damped spring: stiffer (bigger) plants are slower */
        float k = 16.0f / o->stiff, c = 4.0f;
        o->bvel += (k * (target - o->bend) - c * o->bvel) * dt;
        o->bend += o->bvel * dt;
        o->bend = clampf(o->bend, -1.4f, 1.4f);
    }
}

void world_update(world_t *w, float dt)
{
    w->t += dt;
    update_wind(w, dt);

    for (int i = 0; i < w->nstim; i++) {
        w->stim[i].ttl -= dt;
        if (w->stim[i].ttl <= 0) { w->stim[i--] = w->stim[--w->nstim]; }
    }
    update_objects(w, dt);
    for (int i = 0; i < w->nact; i++)
        if (w->act[i].alive) actor_update(w, &w->act[i], dt);
    touch_plants(w);
    update_plants(w, dt);
    /* clean up dead creatures at the end of the array */
    while (w->nact > 0 && !w->act[w->nact - 1].alive) w->nact--;
}

/* --- render ---------------------------------------------------------- */

static int object_frame(const world_t *w, const object_t *o)
{
    (void)w;
    if (o->sway_clip >= 0) return clip_frame_param(&o->spr->clips[o->sway_clip], o->flip ? -o->bend : o->bend);
    return clip_frame_at(&o->spr->clips[o->clip], o->t);
}

static void render_static(world_t *w, raster_t *r)
{
    /* background: almost black blue-green, slightly lighter towards the bottom */
    for (int y = 0; y < r->h; y++) {
        float t = (float)y / (float)r->h;
        rgb_t c = rgb_mix(mkrgb(4, 8, 10), mkrgb(8, 14, 15), t);
        for (int x = 0; x < r->w; x++) {
            r->c[y * r->w + x] = c;
            r->z[y * r->w + x] = Z_EMPTY;
        }
    }
    /* ground */
    for (int x = 0; x < r->w && x < w->w; x++) {
        int gy = (int)ceilf(w->ground[x]) + 1;
        for (int y = gy; y < r->h; y++) {
            float n = hash2f(x, y, 7u);
            float d = (float)(y - gy) / 12.0f;
            rgb_t c = rgb_mix(mkrgb(20, 18, 14), mkrgb(10, 9, 8), clampf(d, 0, 1));
            if (n > 0.92f) c = rgb_add(c, 6);
            raster_plot(r, x, y, c, w->ground_z - 20);
        }
    }
    for (int i = 0; i < w->nobj; i++) {
        const object_t *o = &w->obj[i];
        if (!o->is_static) continue;
        raster_blit(r, &o->spr->frames[0], (int)floorf(o->x + 0.5f), (int)floorf(o->y + 0.5f), o->z,
                    o->flip ? BLIT_FLIPX : 0, 1, NULL);
    }
}

void world_render(world_t *w, raster_t *r, int debug)
{
    /* the static layer is drawn once; every frame it is only copied */
    if (!w->bake_ok || w->bake.w != r->w || w->bake.h != r->h) {
        if (!w->bake.c) raster_init(&w->bake, r->w, r->h);
        else if (w->bake.w != r->w || w->bake.h != r->h) raster_resize(&w->bake, r->w, r->h);
        w->bake.fog = r->fog;
        w->bake.fog_near = r->fog_near;
        w->bake.fog_far = r->fog_far;
        w->bake.fog_max = r->fog_max;
        render_static(w, &w->bake);
        w->bake_ok = 1;
    }
    raster_copy_rect(r, &w->bake, 0, 0, r->w, r->h);
    for (int i = 0; i < w->nobj; i++) {
        const object_t *o = &w->obj[i];
        if (o->is_static) continue;
        int fi = object_frame(w, o);
        raster_blit(r, &o->spr->frames[fi], (int)floorf(o->x + 0.5f), (int)floorf(o->y + 0.5f), o->z,
                    o->flip ? BLIT_FLIPX : 0, 1, NULL);
    }
    for (int i = 0; i < w->nact; i++) {
        const actor_t *a = &w->act[i];
        if (!a->alive || a->hidden) continue;
        int fi = clip_frame_at(&a->spr->clips[a->clip], a->ct);
        raster_blit(r, &a->spr->frames[fi], (int)floorf(a->x + 0.5f), (int)floorf(a->y + 0.5f),
                    (int)floorf(a->zf + 0.5f), a->facing < 0 ? BLIT_FLIPX : 0, 1, NULL);
    }
    if (debug) {
        static const rgb_t pc[PL_COUNT] = {
            {200, 60, 60}, {240, 200, 60}, {60, 200, 240}, {200, 120, 255},
            {120, 255, 120}, {255, 140, 60}, {255, 255, 255}, {100, 100, 255}
        };
        for (int k = 0; k < w->npath; k++) {
            const navpath_t *np = &w->path[k];
            rgb_t c = np->kind == PATH_CLIMB ? mkrgb(255, 120, 200) : np->kind == PATH_WALK ? mkrgb(120, 220, 255)
                                                                       : mkrgb(255, 170, 60);
            for (float t = 0; t <= np->len; t += 1.0f) {
                float x, y;
                path_point(np, t, &x, &y);
                raster_plot(r, (int)x, (int)y, c, 30000);
            }
        }
        for (int k = 0; k < w->nlink; k++) {
            float x, y;
            path_point(&w->path[w->link[k].a], w->link[k].ta, &x, &y);
            for (int d = -2; d <= 2; d++) raster_plot(r, (int)x + d, (int)y + d, mkrgb(255, 255, 0), 30001);
        }
        for (int i = 0; i < w->nplace; i++) {
            const place_t *p = &w->place[i];
            for (int k = -1; k <= 1; k++) {
                raster_plot(r, (int)p->x + k, (int)p->y, pc[p->type], 30000);
                raster_plot(r, (int)p->x, (int)p->y + k, pc[p->type], 30000);
            }
        }
    }
}
