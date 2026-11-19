/* event.c — rare, mysterious happenings (one at a time, own rng stream: they do not change how a seed builds the scene)
 *
 * Fireflies synchronize: the fireflies on the screen stop and hang in the air, their blinks (first out of step)
 * fall into one rhythm. The rhythm speeds up, every blink is brighter than the last (frames glow4, glow5 —
 * a white core and a wide halo), until one sudden flash lights the whole forest — and they are all gone,
 * as if they had never been there.
 *
 *   0 .. SYNC        out of step -> in step, a slow rhythm
 *   SYNC .. FLASH_AT faster and brighter, the dark between blinks fills with light
 *   FLASH_AT         the flash (a warm white wash, strongest around the swarm) and, a moment later, they vanish
 *   .. END           the flash fades; the forest is as it was, only a little emptier
 */
#include "world.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#define PREP_TIME   30.0f      /* the event is picked this long before it starts; the scene is made ready meanwhile */
#define FF_WANT     6          /* fireflies wanted for the event */
#define FF_SYNC     6.0f
#define FF_FLASH_AT 17.0f
#define FF_GONE_AT  (FF_FLASH_AT + 0.18f)
#define FF_END      (FF_FLASH_AT + 2.6f)

static float smooth01(float x) { x = clampf(x, 0.0f, 1.0f); return x * x * (3.0f - 2.0f * x); }

void events_init(world_t *w, uint64_t seed)
{
    event_t *e = &w->ev;
    memset(e, 0, sizeof *e);
    e->rng = rng_seed(seed ^ 0xf1ae5f11e5ull);
    e->next = rng_range(&e->rng, 120.0f, 300.0f);
}

/* Fireflies that are on the screen and free to take part. */
static int visible_fireflies(world_t *w, int *out, int cap)
{
    int n = 0;
    for (int i = 0; i < w->nact && n < cap; i++) {
        const actor_t *a = &w->act[i];
        if (!a->alive || a->hidden || a->hold || strcmp(a->spr->name, "firefly") != 0) continue;
        if (a->x < 4 || a->x > (float)w->w - 4 || a->y < 4 || a->y > (float)w->h - 4) continue;
        out[n++] = i;
    }
    return n;
}

/* the actor slot of a participant, or NULL when it is gone */
static actor_t *member(world_t *w, event_t *e, int k)
{
    int slot = e->id[k] >> 16;
    if (slot < 0 || slot >= w->nact) return NULL;
    actor_t *a = &w->act[slot];
    return a->alive && (a->id & 0xffff) == (unsigned)(e->id[k] & 0xffff) ? a : NULL;
}

static int start_fireflies(world_t *w, int force)
{
    event_t *e = &w->ev;
    int idx[EV_MAX_FF];
    int n = visible_fireflies(w, idx, EV_MAX_FF);
    if (n < 4 && force) {
        /* the playground key (or the lead time was not enough): a swarm appears in the air, on the screen */
        sprite_t *s = lib_find(w->lib, "firefly");
        if (!s) return 0;
        float cx = rng_range(&e->rng, (float)w->w * 0.25f, (float)w->w * 0.75f);
        float cy = rng_range(&e->rng, (float)w->h * 0.3f, (float)w->h * 0.55f);
        while (n < 7) {
            float x = clampf(cx + rng_range(&e->rng, -40.0f, 40.0f), 8.0f, (float)w->w - 8.0f);
            float y = clampf(cy + rng_range(&e->rng, -22.0f, 22.0f), 8.0f, (float)w->h * 0.7f);
            int z = w->ground_z + rng_between(&e->rng, 12, 26);
            int i = actor_spawn(w, "firefly", x, y, NAV_AIR, z);
            if (i < 0) break;
            idx[n++] = i;
        }
    }
    if (n < 4) return 0;
    e->kind = EV_FIREFLIES;
    e->t = 0;
    e->phase = 0;
    e->flash = 0;
    e->gone = 0;
    e->n = n;
    e->cx = e->cy = 0;
    for (int k = 0; k < n; k++) {
        actor_t *a = &w->act[idx[k]];
        a->hold = 1;
        a->bob = rng_range(&e->rng, 0.0f, 6.28f);
        e->id[k] = (idx[k] << 16) | (int)(a->id & 0xffff);
        e->off[k] = rng_float(&e->rng);
        e->cx += a->x;
        e->cy += a->y;
    }
    e->cx /= (float)n;
    e->cy /= (float)n;
    world_log(w, "the fireflies begin to blink in step");
    return 1;
}

int event_start(world_t *w, const char *name)
{
    if (w->ev.kind != EV_NONE) return 0;
    w->ev.pending = EV_NONE;
    if (!strcmp(name, "fireflies")) return start_fireflies(w, 1);
    return 0;
}

/* brightness 0 .. 2.5 of the common rhythm at time t */
static int level_frame(float L)
{
    static const float th[] = { 0.18f, 0.52f, 0.86f, 1.25f, 1.9f };
    int f = 0;
    for (int i = 0; i < 5; i++) if (L >= th[i]) f = i + 1;
    return f;
}

static void update_fireflies(world_t *w, event_t *e, float dt)
{
    e->t += dt;
    float t = e->t;
    float u = clampf((t - FF_SYNC) / (FF_FLASH_AT - FF_SYNC), 0.0f, 1.0f);       /* 0 .. 1 over the build-up */
    float period = t < FF_SYNC ? 2.0f - 0.3f * smooth01(t / FF_SYNC) : 1.7f - 1.25f * powf(u, 0.8f);
    float peak = 1.0f + 1.6f * powf(u, 1.2f);
    float base = 1.5f * powf(u, 2.5f);
    float conv = 1.0f - smooth01(t / (FF_SYNC * 0.8f));                           /* 1 = out of step, 0 = in step */
    e->phase += dt / period;

    float sum = 0.0f;
    for (int k = 0; k < e->n; k++) {
        actor_t *a = member(w, e, k);
        if (!a) continue;
        if (t >= FF_GONE_AT) {                       /* vanished at the peak of the flash: no trace */
            a->alive = 0;
            w->population--;
            continue;
        }
        float ph = e->phase + e->off[k] * conv;
        ph -= floorf(ph);
        float p = 0.5f - 0.5f * cosf(6.2831853f * ph);
        float L = base + (peak - base) * powf(p, 1.2f);
        if (t >= FF_FLASH_AT) L = 2.5f;
        a->ov_frame = 1 + level_frame(L);
        sum += L;
    }

    if (t >= FF_FLASH_AT) {
        float q = (t - FF_FLASH_AT) / (FF_END - FF_FLASH_AT);
        e->flash = t < FF_FLASH_AT + 0.12f ? (t - FF_FLASH_AT) / 0.12f : expf(-(q - 0.05f) * 5.0f);
    } else {
        /* before the flash only a faint breath of light around the swarm, in time with the blinks */
        int live = 0;
        for (int k = 0; k < e->n; k++) if (member(w, e, k)) live++;
        e->flash = live ? 0.10f * u * (sum / (float)live) / 2.5f : 0.0f;
    }
    if (t >= FF_GONE_AT && !e->gone) {
        e->gone = 1;
        world_log(w, "a flash — and the fireflies are gone");
    }
    if (t >= FF_END) {
        e->kind = EV_NONE;
        e->flash = 0;
        e->n = 0;
        e->next = rng_range(&e->rng, 180.0f, 420.0f);
    }
}

static int count_fireflies(const world_t *w)
{
    int n = 0;
    for (int i = 0; i < w->nact; i++)
        if (w->act[i].alive && !strcmp(w->act[i].spr->name, "firefly")) n++;
    return n;
}

/* One more firefly flies in from the edge of the screen (like the director's own arrivals). */
static void add_firefly(world_t *w, event_t *e)
{
    int left = rng_chance(&e->rng, 0.5f);
    float x = left ? -18.0f : (float)w->w + 18.0f;
    float y = rng_range(&e->rng, (float)w->h * 0.1f, (float)w->h * 0.6f);
    int z = w->ground_z + rng_between(&e->rng, 12, 26);
    actor_spawn(w, "firefly", x, y, NAV_AIR, z);
}

/* The event is picked up front: during the lead time the scene is made ready for it. */
static void prepare(world_t *w, event_t *e, float dt)
{
    if (weather_at(w, WX_RAIN, (float)w->w * 0.5f) > 0.4f) {      /* it started to rain: not tonight */
        e->pending = EV_NONE;
        e->next = rng_range(&e->rng, 60.0f, 120.0f);
        return;
    }
    e->prep -= dt;
    if (e->pending == EV_FIREFLIES && e->prep_left > 0) {
        e->prep_next -= dt;
        if (e->prep_next <= 0) {
            add_firefly(w, e);
            e->prep_left--;
            e->prep_next = rng_range(&e->rng, 4.0f, 8.0f);
        }
    }
    if (e->prep > 0) return;
    int kind = e->pending;
    e->pending = EV_NONE;
    if (kind == EV_FIREFLIES && !start_fireflies(w, 1))      /* (force: a few more appear if the swarm is still small) */
        e->next = 60.0f;
}

void events_update(world_t *w, float dt)
{
    event_t *e = &w->ev;
    if (e->kind == EV_FIREFLIES) {
        update_fireflies(w, e, dt);
        return;
    }
    if (e->pending != EV_NONE) {
        prepare(w, e, dt);
        return;
    }
    e->next -= dt;
    if (e->next > 0) return;
    /* the director: picks an event that fits the moment; if none does, it asks again soon */
    e->next = 20.0f;
    if (weather_at(w, WX_RAIN, (float)w->w * 0.5f) > 0.25f) return;
    e->pending = EV_FIREFLIES;
    e->prep = PREP_TIME;
    /* fireflies: if there are only two or three, a few more arrive before it starts (at least two) */
    int have = count_fireflies(w);
    e->prep_left = have >= FF_WANT ? 0 : clampi(FF_WANT - have, 2, EV_MAX_FF - have);
    e->prep_next = rng_range(&e->rng, 2.0f, 6.0f);
    world_log(w, "something is about to happen among the fireflies");
}

void events_render(world_t *w, raster_t *r)
{
    const event_t *e = &w->ev;
    if (e->kind == EV_NONE || e->flash <= 0.004f) return;
    const rgb_t wash = mkrgb(255, 243, 190);
    float R = (float)r->w * 0.5f;
    for (int y = 0; y < r->h; y++) {
        float dy = ((float)y - e->cy) * 1.4f;
        for (int x = 0; x < r->w; x++) {
            float dx = (float)x - e->cx;
            float k = 1.0f / (1.0f + (dx * dx + dy * dy) / (R * R));
            float a = e->flash * (0.30f + 0.60f * k);
            if (a > 0.98f) a = 0.98f;
            rgb_t *c = &r->c[y * r->w + x];
            *c = rgb_mix(*c, wash, a);
        }
    }
}
