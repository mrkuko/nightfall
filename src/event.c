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
 *
 * Dhole pack chase: a deer bursts in from one edge and runs flat out across the screen, a pack of 3–4 dholes
 * on its heels in a loose row (they share one target: each keeps its own place behind the deer, ST_PURSUE).
 * Dholes that were already on the screen watch the deer go by and fall in. Everything small that the dholes
 * frighten hides as the pack goes past; the deer and the pack leave over the other edge.
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

#define DH_MAX_PACK 4
#define DH_TIMEOUT  60.0f      /* safety net: the event is over by then even if somebody got stuck */

/* Dholes on the screen, on the ground and free, that can join the chase. */
static int free_dholes(world_t *w, int *out, int cap)
{
    int n = 0;
    for (int i = 0; i < w->nact && n < cap; i++) {
        const actor_t *a = &w->act[i];
        if (!a->alive || a->hidden || a->scripted || a->nav != NAV_GROUND || a->path >= 0 || a->clip_after ||
            strcmp(a->spr->name, "dhole") != 0) continue;
        if (a->x < 0 || a->x > (float)w->w) continue;
        out[n++] = i;
    }
    return n;
}

static int start_dholes(world_t *w)
{
    event_t *e = &w->ev;
    if (!lib_find(w->lib, "deer") || !lib_find(w->lib, "dhole")) return 0;
    int have[DH_MAX_PACK];
    int nhave = free_dholes(w, have, DH_MAX_PACK);
    /* the deer comes from the side away from the dholes already here (so it passes them), else at random */
    float mean = 0;
    for (int k = 0; k < nhave; k++) mean += w->act[have[k]].x;
    int from_left = nhave ? mean / (float)nhave > (float)w->w * 0.5f : rng_chance(&e->rng, 0.5f);
    float dir = from_left ? 1.0f : -1.0f;
    float x0 = from_left ? -50.0f : (float)w->w + 50.0f;
    float x1 = from_left ? (float)w->w + 70.0f : -70.0f;

    profile_t dp;
    profile_parse(lib_find(w->lib, "deer"), &dp);
    int lane = actor_pick_lane(w, &dp);
    int di = actor_spawn(w, "deer", x0, 0, NAV_GROUND, lane);
    if (di < 0) return 0;
    actor_t *deer = &w->act[di];
    deer->speed_mul = 0.92f;                    /* flat out, but not a blur: the pack keeps its pace */
    deer->facing = (int)dir;
    step_t st[4];
    memset(st, 0, sizeof st);
    st[0].kind = ST_RUNTO; st[0].x = x1; st[0].z = lane; st[0].place = -1;
    st[1].kind = ST_DESPAWN; st[1].place = -1;
    actor_script(w, deer, IN_FLEE, st, 2);

    e->kind = EV_DHOLES;
    e->t = 0;
    e->flash = 0;
    e->gone = 0;
    e->n = 0;
    e->id[e->n++] = (di << 16) | (int)(deer->id & 0xffff);

    /* the pack: the ones already here first (front of the row), the rest run in behind the deer */
    int want = rng_between(&e->rng, 3, DH_MAX_PACK);
    if (want < nhave) want = nhave;
    float gap = 62.0f;                          /* the first one a good length behind the deer's tail */
    for (int k = 0; k < want; k++) {
        int i;
        if (k < nhave) {
            i = have[k];
        } else {
            float x = x0 - dir * (gap + rng_range(&e->rng, 0.0f, 12.0f));
            i = actor_spawn(w, "dhole", x, 0, NAV_GROUND, lane);
            if (i < 0) break;
        }
        actor_t *a = &w->act[i];
        a->speed_mul = rng_range(&e->rng, 1.0f, 1.15f);
        memset(st, 0, sizeof st);
        st[0].kind = ST_PURSUE;
        st[0].place = di;
        st[0].anim = (int)deer->id;
        st[0].x = gap;
        st[0].z = 2 + (k % 2) * 4 + rng_between(&e->rng, -1, 1);      /* staggered rows: not one on top of the other */
        st[1].kind = ST_RUNTO; st[1].x = x1 - dir * 10.0f; st[1].z = lane + st[0].z; st[1].place = -1;
        st[2].kind = ST_DESPAWN; st[2].place = -1;
        actor_script(w, a, IN_CHASE, st, 3);
        e->id[e->n++] = (i << 16) | (int)(a->id & 0xffff);
        gap += rng_range(&e->rng, 34.0f, 46.0f);
    }
    if (e->n < 4) {                             /* not even a pack of three: let them all go their way */
        for (int k = 0; k < e->n; k++) {
            actor_t *a = member(w, e, k);
            if (a) a->nplan = a->step;          /* plan over -> own brain again (the deer still has to get away) */
        }
        if (deer->alive) actor_set_intent(w, deer, IN_LEAVE);
        e->kind = EV_NONE;
        e->n = 0;
        return 0;
    }
    world_log(w, "a deer bursts out, %d dholes on its heels", e->n - 1);
    return 1;
}

static void update_dholes(world_t *w, event_t *e, float dt)
{
    e->t += dt;
    int live = 0;
    for (int k = 0; k < e->n; k++) {
        actor_t *a = member(w, e, k);
        if (!a) continue;
        live++;
        /* the pack in full cry frightens from further away than one running dhole */
        if (k > 0 && a->anim == A_RUN) world_stimulus(w, a->x, a->y, 170, ST_DANGER, 1.0f, 0.4f, e->id[k] >> 16);
    }
    if (!e->gone && !member(w, e, 0)) {
        e->gone = 1;
        world_log(w, "the chase is gone, the forest holds its breath");
    }
    if (!live || e->t > DH_TIMEOUT) {
        e->kind = EV_NONE;
        e->n = 0;
        e->next = rng_range(&e->rng, 180.0f, 420.0f);
    }
}

int event_start(world_t *w, const char *name)
{
    if (w->ev.kind != EV_NONE) return 0;
    w->ev.pending = EV_NONE;
    if (!strcmp(name, "fireflies")) return start_fireflies(w, 1);
    if (!strcmp(name, "dholes")) return start_dholes(w);
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
    if (e->kind == EV_DHOLES) {
        update_dholes(w, e, dt);
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
    /* dholes about (so the biome has them): now and then they pick up a deer's trail instead */
    int dh[1];
    if (free_dholes(w, dh, 1) && rng_chance(&e->rng, 0.5f) && start_dholes(w)) return;
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
