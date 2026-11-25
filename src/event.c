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
 *
 * Ghost walk (vanishing footprints; any weather but rain, most often in fog): something nobody can see walks
 * slowly through the forest. The forest falls silent first (the curious turn to where it comes from, the timid
 * slip away), then shoe prints appear on the ground one by one — a pale, half-transparent frosted
 * sheen that seeps in heel to toe, a thread of cold mist curling up from each fresh one; slow steps, close to the camera; a step into a puddle leaves only a ring. The plants it passes
 * bend aside. The walker itself shows only in the fog: the mist parts around a tall hooded figure (thinner where
 * it is, a little denser at its outline, a wake closing behind it); in clear air there are only the prints.
 * The creatures sense it rather than see it (SRC_UNSEEN: the fog does not hide it) and hide by their own fear.
 * It walks off the edge — or in the middle of the clearing it turns into the forest (the last two prints half
 * turned, then with the toes into the forest, a step deeper in) and the figure dissolves upwards. After the last step the prints go out one after another, the oldest first.
 *
 * Bat swarm: out of one spot (a hollow in a trunk, else a branch high up) bats pour out one after another,
 * rise in a loose column, then scatter across the sky in zigzags; most leave over the edges, one or two stay
 * and go on as ordinary bats.
 */
#include "world.h"
#include "noise.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#define PREP_TIME   30.0f      /* the event is picked this long before it starts; the scene is made ready meanwhile */
#define FF_WANT     6          /* fireflies wanted for the event */
#define FF_SYNC     6.0f
#define FF_FLASH_AT 17.0f
#define FF_GONE_AT  (FF_FLASH_AT + 0.18f)
#define FF_END      (FF_FLASH_AT + 2.6f)

static float ease01(float x) { x = clampf(x, 0.0f, 1.0f); return x * x * (3.0f - 2.0f * x); }

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

#define BAT_MIN     10
#define BAT_MAX     16          /* (<= EV_MAX_FF) */
#define BAT_TIMEOUT 45.0f

/* Where the bats come out: a hollow on the screen (a free one if there is one), else a branch high up,
 * else just somewhere in the crowns. */
static void bat_spot(world_t *w, event_t *e, float *x, float *y)
{
    int free_h[32], any_h[32], perch[32], nf = 0, na = 0, np = 0;
    for (int i = 0; i < w->nplace; i++) {
        const place_t *p = &w->place[i];
        int hollow = p->type == PL_HOLLOW && p->has_eye;
        if (!hollow && p->type != PL_PERCH) continue;
        float px = hollow ? p->ex : p->x, py = hollow ? p->ey : p->y;
        if (px < 12.0f || px > (float)w->w - 12.0f || py < 8.0f || py > (float)w->h * 0.7f) continue;
        if (hollow && p->occupant < 0 && nf < 32) free_h[nf++] = i;
        if (hollow && na < 32) any_h[na++] = i;
        if (!hollow && np < 32) perch[np++] = i;
    }
    int pick = nf ? free_h[rng_int(&e->rng, nf)] : na ? any_h[rng_int(&e->rng, na)] : np ? perch[rng_int(&e->rng, np)] : -1;
    if (pick < 0) {
        *x = rng_range(&e->rng, (float)w->w * 0.2f, (float)w->w * 0.8f);
        *y = (float)w->h * rng_range(&e->rng, 0.12f, 0.3f);
        return;
    }
    const place_t *p = &w->place[pick];
    *x = p->type == PL_HOLLOW ? p->ex : p->x;
    *y = p->type == PL_HOLLOW ? p->ey : p->y;
}

/* One more bat out of the spot: up in the column, then off in its own direction. */
static void emerge_bat(world_t *w, event_t *e)
{
    int z = w->ground_z + 14;                   /* in front of the trunks */
    int i = actor_spawn(w, "bat", e->cx + rng_range(&e->rng, -1.5f, 1.5f), e->cy, NAV_AIR, z);
    if (i < 0) { e->prep_left = 0; return; }
    actor_t *a = &w->act[i];
    a->speed_mul = rng_range(&e->rng, 0.9f, 1.25f);
    int stay = e->n >= 2 && e->gone < 2 && rng_chance(&e->rng, 0.18f);       /* gone = how many stay */
    step_t st[6];
    memset(st, 0, sizeof st);
    int n = 0;
    float W = (float)w->w, H = (float)w->h;
    /* out and up: a loose column above the spot, leaning the way it will go */
    float side = rng_chance(&e->rng, 0.5f) ? -1.0f : 1.0f;
    float x = e->cx + side * rng_range(&e->rng, 2.0f, 10.0f), y = fmaxf(4.0f, e->cy - rng_range(&e->rng, 12.0f, 26.0f));
    st[n].kind = ST_FLY; st[n].x = x; st[n].y = y; st[n].z = z; st[n].place = -1; n++;
    /* scattering: two zigzag legs across the sky */
    for (int k = 0; k < 2; k++) {
        x = clampf(x + side * rng_range(&e->rng, 25.0f, 70.0f), 6.0f, W - 6.0f);
        y = clampf(y + rng_range(&e->rng, -18.0f, 16.0f), 4.0f, H * 0.5f);
        st[n].kind = ST_FLY; st[n].x = x; st[n].y = y; st[n].z = z; st[n].place = -1; n++;
    }
    if (stay) {
        e->gone++;
    } else {
        /* away over the nearest edge (or up into the canopy) */
        float ex = side < 0 ? -20.0f : W + 20.0f, ey = rng_range(&e->rng, -15.0f, H * 0.4f);
        if (rng_chance(&e->rng, 0.3f)) { ex = clampf(x + side * rng_range(&e->rng, 20.0f, 60.0f), -20.0f, W + 20.0f); ey = -20.0f; }
        st[n].kind = ST_FLY; st[n].x = ex; st[n].y = ey; st[n].z = z; st[n].place = -1; n++;
        st[n].kind = ST_DESPAWN; st[n].place = -1; n++;
    }
    actor_script(w, a, IN_FLEE, st, n);         /* (IN_FLEE: they fly at full speed) */
    if (e->n < EV_MAX_FF) e->id[e->n++] = (i << 16) | (int)(a->id & 0xffff);
}

static int start_bats(world_t *w)
{
    event_t *e = &w->ev;
    if (!lib_find(w->lib, "bat")) return 0;
    e->kind = EV_BATS;
    e->t = 0;
    e->flash = 0;
    e->gone = 0;
    e->n = 0;
    bat_spot(w, e, &e->cx, &e->cy);
    e->prep_left = rng_between(&e->rng, BAT_MIN, BAT_MAX);
    e->prep_next = 0;
    world_log(w, "bats pour out at x=%d", (int)e->cx);
    return 1;
}

static void update_bats(world_t *w, event_t *e, float dt)
{
    e->t += dt;
    if (e->prep_left > 0) {
        e->prep_next -= dt;
        if (e->prep_next <= 0) {
            emerge_bat(w, e);
            e->prep_left--;
            /* a stream with gaps: sometimes two right behind each other, sometimes a pause */
            e->prep_next = rng_chance(&e->rng, 0.35f) ? rng_range(&e->rng, 0.04f, 0.12f) : rng_range(&e->rng, 0.15f, 0.4f);
        }
        return;
    }
    int busy = 0;
    for (int k = 0; k < e->n; k++) {
        actor_t *a = member(w, e, k);
        if (a && a->scripted) busy++;
    }
    if (!busy || e->t > BAT_TIMEOUT) {
        world_log(w, "the bats have scattered");
        e->kind = EV_NONE;
        e->n = 0;
        e->next = rng_range(&e->rng, 180.0f, 420.0f);
    }
}

/* --- ghost walk ------------------------------------------------------------------------------------------- */

#define GH_HUSH      4.5f       /* the silence before the first step */
#define GH_HEIGHT    58.0f      /* the figure in the fog (a deer with antlers is ~74 px; it walks close to the camera) */
#define GH_DREAD_R   130.0f     /* how far the creatures sense it */
#define GH_RISE      3.5f       /* the end in the middle of the clearing: it dissolves upwards this long */
#define GH_TIMEOUT   150.0f
#define PR_LIGHT     0.9f       /* a print seeps into the ground heel to toe this slowly … */
#define PR_WISP      2.6f       /* … and a thread of cold mist rises from it this long */
#define PR_HOLD      1.6f       /* after the last step the trail glows on this long, then … */
#define PR_TURN      0.38f      /* … the prints go out one after another, the oldest first, this far apart, … */
#define PR_FADE      2.4f       /* … each dimming out over this long */
#define PR_ON        (-1e9f)    /* `fade` of a print still glowing on (the trail has not ended yet) */
#define PR_MAXAGE    240.0f     /* a print nobody ended (cannot happen, but): it goes out on its own */

/* How much of the ground row at depth z between x0 and x1 is hidden by what stands in front of it (0..1): the
 * walker takes a row where its footprints can be seen. */
static float row_hidden(const world_t *w, int z, float x0, float x1)
{
    int n = 0, hid = 0;
    if (x0 > x1) { float t = x0; x0 = x1; x1 = t; }
    x0 = fmaxf(x0, 2.0f); x1 = fminf(x1, (float)w->w - 3.0f);
    for (float x = x0; x <= x1; x += 3.0f) {
        int px = (int)x, py = (int)floorf(world_ground_at(w, x, (float)z) + 0.5f);
        n++;
        for (int i = 0; i < w->nobj; i++) {
            const object_t *o = &w->obj[i];
            if (o->z <= z - 6 || px < o->bx0 || px > o->bx1) continue;
            const frame_t *f = &o->spr->frames[0];
            int fx = px - (int)floorf(o->x + 0.5f), fy = py - (int)floorf(o->y + 0.5f) + f->ay;
            fx = o->flip ? f->ax - fx : f->ax + fx;
            if (fx < 0 || fy < 0 || fx >= f->w || fy >= f->h) continue;
            const px_t *q = &f->px[fy * f->w + fx];
            if (q->a && o->z + q->z > z) { hid++; break; }
        }
    }
    return n ? (float)hid / (float)n : 1.0f;
}

/* the walker is there (walking, standing or dissolving) */
static int walking(const event_t *e) { return e->kind == EV_GHOST && e->gstate > 0; }

static int start_ghost(world_t *w)
{
    event_t *e = &w->ev;
    float W = (float)w->w;
    e->kind = EV_GHOST;
    e->t = 0;
    e->flash = 0;
    e->n = 0;
    e->gstate = 0;
    e->gvis = 0;
    e->glift = 0;
    e->gwait = 0;
    e->gmoved = 0;
    e->gfoot = rng_int(&e->rng, 2);
    e->gdir = rng_chance(&e->rng, 0.5f) ? 1.0f : -1.0f;
    e->gstep = rng_range(&e->rng, 15.0f, 18.0f);                  /* slow, unhurried steps … */
    e->gspeed = e->gstep / rng_range(&e->rng, 2.0f, 2.5f);        /* … a step every two seconds or so */
    /* mostly in from the edge; now and then the first print appears out of nowhere, in the open */
    float x0 = rng_chance(&e->rng, 0.6f) ? (e->gdir > 0 ? -12.0f : W + 12.0f)
                                          : (e->gdir > 0 ? rng_range(&e->rng, 0.12f, 0.45f) : rng_range(&e->rng, 0.55f, 0.88f)) * W;
    /* how far: mostly it turns into the forest somewhere in the middle, now and then it walks across and off the
     * screen (at most ~25 strides: on a very wide screen it turns into the forest before the edge) */
    float len = rng_chance(&e->rng, 0.3f) ? W + 60.0f : rng_range(&e->rng, 0.35f, 0.75f) * W;
    len = fminf(len, e->gstep * 25.0f);
    e->gx = x0;
    e->gend = x0 + e->gdir * len;
    /* the row: of a few candidates the one least hidden behind plants (a little randomness for variety) */
    float best = 1e9f;
    for (int k = 0; k < 6; k++) {
        int z = w->fg_z + rng_between(&e->rng, -6, 12);             /* in front, on the open ground near the camera */
        float h = row_hidden(w, z, x0, e->gend) + rng_range(&e->rng, 0.0f, 0.08f);
        if (h < best) { best = h; e->gz = (float)z; }
    }
    if (e->gend > 24.0f && e->gend < W - 24.0f && fabsf(e->gend - x0) < 3.0f * e->gstep) e->gend = x0 + e->gdir * 3.0f * e->gstep;
    e->gnext = x0 + e->gdir * e->gstep * 0.5f;
    e->gpause_at = rng_chance(&e->rng, 0.5f) ? x0 + e->gdir * len * rng_range(&e->rng, 0.3f, 0.6f) : NAN;
    e->gfy = world_ground_at(w, clampf(e->gx, 0.0f, W - 1.0f), e->gz);
    world_log(w, "the forest falls silent …");
    return 1;
}

/* A footfall at x: a print on the ground (or a ring in a puddle), heard by the curious. shape 0 along the trail
 * (the two feet a hand apart in depth), 1 half turned into the forest, 2 with the toes into the forest (a step
 * deeper in). */
static void footfall_shape(world_t *w, event_t *e, float x, int shape)
{
    if (x < -4.0f || x > (float)w->w + 4.0f) return;
    int side = e->gfoot;
    e->gfoot ^= 1;
    int z = (int)lroundf(e->gz) + (shape == 0 ? (side ? 4 : -4) : shape == 1 ? (side ? 1 : -2) - 2 : -6);
    float y = floorf(world_ground_at(w, x, (float)z) + 0.5f);
    world_stimulus(w, x, y, 70, ST_NOISE, 0.3f, 0.5f, -1);
    for (int i = 0; i < w->nobj; i++) {
        const object_t *o = &w->obj[i];
        if (strcmp(o->spr->name, "puddle") != 0) continue;
        if (x >= (float)o->bx0 + 1.0f && x <= (float)o->bx1 - 1.0f && fabsf((float)z - (float)o->z) < 6.0f) {
            weather_ring(w, x, o->y - 1.0f, o->z + 1);
            return;
        }
    }
    int k = e->nprint < EV_MAX_PRINT ? e->nprint++ : 0;
    if (e->nprint == EV_MAX_PRINT)                                  /* full: the oldest one goes */
        for (int i = 1; i < EV_MAX_PRINT; i++) if (e->print[i].age > e->print[k].age) k = i;
    ev_print_t *p = &e->print[k];
    p->x = x; p->y = y; p->z = z; p->age = 0; p->fade = PR_ON; p->side = side; p->dir = e->gdir > 0 ? 1 : -1;
    p->shape = shape;
}

static void footfall(world_t *w, event_t *e, float x) { footfall_shape(w, e, x, 0); }

/* The last step has been made: the trail glows on a moment, then the prints go out in the order they were made
 * (the oldest first). Only those still glowing on; calling it twice does nothing more. */
static void trail_end(event_t *e)
{
    for (;;) {
        int k = -1, rank = 0;
        for (int i = 0; i < e->nprint; i++) {
            if (e->print[i].fade != PR_ON) { if (e->print[i].fade < 0) rank++; continue; }
            if (k < 0 || e->print[i].age > e->print[k].age) k = i;
        }
        if (k < 0) return;
        e->print[k].fade = -(PR_HOLD + (float)rank * PR_TURN);
    }
}

static void end_ghost(event_t *e)
{
    trail_end(e);
    e->kind = EV_NONE;
    e->gstate = 0;
    e->gvis = 0;
    e->gmoved = 0;
    e->next = rng_range(&e->rng, 180.0f, 420.0f);
}

static void update_ghost(world_t *w, event_t *e, float dt)
{
    e->t += dt;
    float W = (float)w->w;
    e->gmoved = 0;
    if (e->gstate == 0) {
        /* the hush: from where it will come the curious hear something, the timid slip away */
        if (e->t < 0.2f) world_stimulus(w, clampf(e->gx, 0.0f, W), e->gfy, 220, ST_NOISE, 0.5f, 1.5f, -1);
        if (e->t >= GH_HUSH) { e->gstate = 1; world_log(w, "footprints appear — nobody makes them"); }
    } else if (e->gstate == 1) {
        float st = e->gdir * e->gspeed * dt;
        e->gx += st;
        e->gmoved = st;
        e->gz += sinf(e->t * 0.4f) * dt * 0.8f;                     /* the line wanders a little in depth */
        if ((e->gx - e->gnext) * e->gdir >= 0) {
            footfall(w, e, e->gnext);
            e->gnext += e->gdir * e->gstep * rng_range(&e->rng, 0.93f, 1.07f);
        }
        if (!isnan(e->gpause_at) && (e->gx - e->gpause_at) * e->gdir >= 0) {
            /* it stops: the back foot comes up beside the front one, and it stands there … */
            footfall(w, e, e->gnext - e->gdir * e->gstep * 0.95f);
            e->gnext += e->gdir * e->gstep * 0.05f;
            e->gpause_at = NAN;
            e->gwait = rng_range(&e->rng, 3.0f, 6.5f);
            e->gstate = 2;
            world_log(w, "the footprints stop … and wait");
        } else if ((e->gx - e->gend) * e->gdir >= 0) {
            if (e->gx < -20.0f || e->gx > W + 20.0f) { world_log(w, "the footprints lead away"); end_ghost(e); return; }
            /* it stops in the middle of the clearing and turns into the forest: two more steps, half turned, then
             * with the toes into the forest, a step deeper in — and nothing more */
            e->gstate = 4;
            e->gturn = 0;
            e->gwait = clampf(fabsf(e->gnext - e->gx) / e->gspeed, 0.4f, 2.5f);
            world_log(w, "the footprints turn into the forest");
        }
    } else if (e->gstate == 2) {
        e->gwait -= dt;
        if (e->gwait <= 0) e->gstate = 1;
    } else if (e->gstate == 4) {
        float st = e->gdir * e->gspeed * 0.4f * dt;
        e->gx += st;
        e->gmoved = st;
        e->gz -= 4.0f * dt;                                         /* into the depth */
        e->gwait -= dt;
        if (e->gwait <= 0) {
            if (e->gturn++ == 0) {
                footfall_shape(w, e, e->gnext, 1);
                e->gnext += e->gdir * e->gstep * 0.3f;
                e->gwait = e->gstep / e->gspeed * 0.9f;
            } else {
                footfall_shape(w, e, e->gnext, 2);
                trail_end(e);
                e->gstate = 3;
                e->t = 0;
                world_log(w, "the trail ends at the edge of the forest");
            }
        }
    } else {
        e->glift += dt * 9.0f;                                      /* dissolving upwards … */
        e->gz -= 3.0f * dt;                                         /* … and away into the forest */
        if (e->t >= GH_RISE) { end_ghost(e); return; }
    }
    if (e->t > GH_TIMEOUT && e->gstate != 3) { end_ghost(e); return; }
    e->gfy = world_ground_at(w, clampf(e->gx, 0.0f, W - 1.0f), e->gz);
    /* how strongly it parts the fog: comes with the first steps, goes as it dissolves */
    float want = e->gstate == 0 ? 0.0f : e->gstate == 3 ? fmaxf(0.0f, 1.0f - e->t / GH_RISE) : 1.0f;
    e->gvis += (want - e->gvis) * fminf(1.0f, dt * (e->gstate == 3 ? 6.0f : 0.8f));
    /* the creatures sense it (a little ahead of it: they slip away before it comes) */
    if (e->gstate < 3)
        world_stimulus(w, e->gx + e->gdir * 30.0f, e->gfy, GH_DREAD_R, ST_DANGER, e->gstate ? 1.5f : 1.0f, 0.4f, SRC_UNSEEN);
}

int event_walker(const world_t *w, float *x, float *z, float *moved)
{
    const event_t *e = &w->ev;
    if (!walking(e) || e->gstate == 3) return 0;
    *x = e->gx; *z = e->gz; *moved = e->gmoved;
    return 1;
}

int event_fog_box(const world_t *w, int box[4], int *z)
{
    const event_t *e = &w->ev;
    if (!walking(e) || e->gvis < 0.01f) return 0;
    float wake = 30.0f;
    box[0] = (int)floorf(e->gx - 10.0f - (e->gdir > 0 ? wake : 0.0f));
    box[2] = (int)ceilf(e->gx + 10.0f + (e->gdir < 0 ? wake : 0.0f));
    box[1] = (int)floorf(e->gfy - e->glift - GH_HEIGHT * 1.25f);
    box[3] = (int)ceilf(e->gfy - e->glift + 4.0f);
    *z = (int)lroundf(e->gz) + 2;
    return box[2] >= 0 && box[0] < w->w;
}

/* half the width of the figure at height v (0 = the ground, 1 = the top of the hood): a long robe, shoulders,
 * a hood */
static float ghost_halfw(float v)
{
    static const float V[] = { 0.0f, 0.3f, 0.55f, 0.7f, 0.79f, 0.87f, 0.95f, 1.0f };
    static const float H[] = { 4.8f, 4.2f, 4.6f, 5.6f, 4.6f, 3.8f, 3.0f, 1.2f };
    if (v <= V[0]) return H[0];
    for (int i = 1; i < 8; i++)
        if (v <= V[i]) return H[i - 1] + (H[i] - H[i - 1]) * (v - V[i - 1]) / (V[i] - V[i - 1]);
    return H[7];
}

void event_fog_part(const world_t *w, int px, int py, float *hole, float *rim)
{
    const event_t *e = &w->ev;
    *hole = *rim = 0.0f;
    float fy = e->gfy - e->glift;
    float v = (fy - (float)py) / GH_HEIGHT;
    if (v < -0.08f || v > 1.25f) return;
    /* the outline wavers like smoke */
    float wob = sinf((float)py * 0.21f + w->t * 1.1f) * 0.8f + (vnoise2((float)py * 0.11f, w->t * 0.5f, 77u) - 0.5f) * 2.6f;
    float dx = (float)px - e->gx - wob;
    float hw = ghost_halfw(v) * (1.0f + e->glift * 0.02f);        /* dissolving: it spreads */
    float top = fmaxf(0.0f, (v - 0.9f) / 0.1f);                     /* the rounded top of the hood */
    float vert = ease01((v + 0.08f) / 0.1f);                        /* the robe fades into the ground mist */
    float d = sqrtf(dx * dx / (hw * hw) + top * top);
    float body = (1.0f - ease01((d - 0.55f) / 0.7f)) * vert;
    /* the wake: where it was a moment ago the mist closes again, a little slower */
    float wake = 0.0f;
    if (dx * e->gdir < 0.0f)
        for (int k = 1; k <= 10; k++) {
            float back = (float)k * 2.5f;
            float dk = dx + e->gdir * back;
            float q = sqrtf(dk * dk / (hw * hw) + top * top);
            wake = fmaxf(wake, (1.0f - ease01((q - 0.55f) / 0.7f)) * vert * expf(-back / 9.0f));
        }
    float behind = -dx * e->gdir;
    *hole = fmaxf(body, wake) * e->gvis;
    /* the mist it pushes aside gathers at its outline (ragged, thinner on the side it leaves behind) */
    float rag = 0.55f + 0.45f * vnoise2((float)px * 0.4f, (float)py * 0.25f + w->t * 0.8f, 79u);
    *rim = expf(-(d - 1.2f) * (d - 1.2f) / 0.09f) * vert * rag * (behind > 0 ? 0.45f : 1.0f) * (1.0f - body) * e->gvis;
}

/* A ghost's shoe print seen low across the ground ('#' full, '+' faint), toe to the right / up:
 *   0  along the trail, 10 x 3: the heel, the narrow waist of the sole (the arch side open), the sole, the rounded
 *      toe; this is the near foot (the arch away from the camera: row 0), the far foot is the same upside down
 *   1  half turned into the forest, 7 x 3: toe up and ahead
 *   2  toe into the forest, 4 x 4: foreshortened, toe at the top
 * Walking left: mirrored. ax, ay: which way heel -> toe runs (it seeps in that way). */
typedef struct { int w, h, ax, ay; const char *row[4]; } pmask_t;
static const pmask_t PMASK[3] = {
    { 10, 3, 1, 0, { ".##...###.", "###++#####", ".##+++###." , NULL } },
    { 7, 3, 1, 1, { "....###", "..++##.", "###...." } },
    { 4, 4, 0, 2, { ".##.", "####", ".+#.", ".##." } },
};

/* how bright (col, row) of print p is (0..1), not yet lit up heel to toe */
static float print_px(const ev_print_t *p, int col, int row)
{
    const pmask_t *M = &PMASK[p->shape];
    if (col < 0 || row < 0 || col >= M->w || row >= M->h) return 0.0f;
    int mirror = p->shape == 2 ? p->side : p->dir < 0;     /* shape 2: the left shoe is the right one mirrored */
    int c = mirror ? M->w - 1 - col : col;
    int r = p->shape == 0 && !p->side ? M->h - 1 - row : row;
    char ch = M->row[r][c];
    if (ch == '.') return 0.0f;
    float prog = (float)(c * M->ax + (M->h - 1 - r) * M->ay);
    float total = (float)((M->w - 1) * M->ax + (M->h - 1) * M->ay);
    float lit = clampf(p->age / PR_LIGHT * (total + 2.0f) - prog, 0.0f, 1.0f);
    return (ch == '#' ? 1.0f : 0.45f) * lit;
}

/* how strongly it shows at its age: it seeps in, then breathes slowly (as if something were still standing
 * there), going out at the end */
static float print_level(const ev_print_t *p, float t)
{
    float k = 0.78f + 0.22f * vnoise2(t * 0.45f, p->x * 0.37f, 31u);
    if (p->fade != PR_ON && p->fade > 0.0f) k *= 1.0f - ease01(p->fade / PR_FADE);
    return k;
}

static void update_prints(event_t *e, float dt)
{
    int n = 0;
    for (int i = 0; i < e->nprint; i++) {
        ev_print_t *p = &e->print[i];
        p->age += dt;
        if (p->fade == PR_ON) { if (p->age > PR_MAXAGE) p->fade = 0.0f; }
        else p->fade += dt;
        if (p->fade == PR_ON || p->fade < PR_FADE) e->print[n++] = *p;   /* the ones gone out are forgotten */
    }
    e->nprint = n;
}

/* The footprints: a pale, half-transparent sheen on the ground in the shape of the shoe, as if the cold had
 * frosted it (the ground shows through, grained like hoarfrost, a soft edge around it); from a fresh print a thin
 * thread of cold mist curls up and thins out. Drawn over the fog and the dark of a storm; what stands in front
 * hides it. */
static void render_prints(const world_t *w, raster_t *r)
{
    const event_t *e = &w->ev;
    const rgb_t pale = mkrgb(196, 208, 214), mist = mkrgb(150, 162, 170);
    for (int i = 0; i < e->nprint; i++) {
        const ev_print_t *p = &e->print[i];
        const pmask_t *M = &PMASK[p->shape];
        float L = print_level(p, w->t);
        if (L <= 0.01f) continue;
        float fog = clampf(weather_at(w, WX_FOG, p->x), 0.0f, 1.0f);
        float soft = 0.08f + 0.06f * fog;                           /* the edge: wider in fog */
        int x0 = (int)floorf(p->x + 0.5f) - M->w / 2, y0 = (int)p->y - M->h / 2;
        for (int row = -1; row < M->h + 1; row++)
            for (int col = -2; col < M->w + 2; col++) {
                int x = x0 + col, y = y0 + row;
                if (x < 0 || y < 0 || x >= r->w || y >= r->h) continue;
                int idx = y * r->w + x;
                if (r->z[idx] > p->z) continue;                     /* a plant in front of it */
                float m = print_px(p, col, row), h = 0.0f;
                for (int rr = row - 1; rr <= row + 1; rr++)
                    for (int cc = col - 1; cc <= col + 1; cc++)
                        if (rr != row || cc != col) h = fmaxf(h, print_px(p, cc, rr) * (rr != row && cc != col ? 0.5f : 1.0f));
                float grain = 0.72f + 0.28f * hash2f(x, y, 913u);   /* hoarfrost, not paint */
                float a = (m * 0.34f * grain + h * soft) * L;
                if (a <= 0.004f) continue;
                r->c[idx] = rgb_mix(r->c[idx], pale, clampf(a, 0.0f, 0.5f));
            }
        /* the thread of mist rising from a fresh print */
        if (p->age < PR_WISP && p->fade == PR_ON) {
            float u = p->age / PR_WISP;
            for (int k = 0; k < 7; k++) {
                float h = p->age * 5.0f - (float)k * 1.4f;          /* the head rises, the tail follows */
                if (h < 0.5f) continue;
                int x = (int)floorf(p->x + sinf(p->age * 2.3f + (float)k * 0.9f) * (0.6f + 0.25f * h) + 0.5f);
                int y = (int)floorf(p->y - 1.0f - h + 0.5f);
                if (x < 0 || y < 0 || x >= r->w || y >= r->h) continue;
                int idx = y * r->w + x;
                if (r->z[idx] > p->z) continue;
                float a = 0.22f * sinf(3.14159f * u) * (1.0f - (float)k / 7.0f);
                r->c[idx] = rgb_mix(r->c[idx], mist, a);
            }
        }
    }
}

int event_start(world_t *w, const char *name)
{
    if (w->ev.kind != EV_NONE) return 0;
    w->ev.pending = EV_NONE;
    if (!strcmp(name, "fireflies")) return start_fireflies(w, 1);
    if (!strcmp(name, "dholes")) return start_dholes(w);
    if (!strcmp(name, "bats")) return start_bats(w);
    if (!strcmp(name, "ghost")) return start_ghost(w);
    if (!strcmp(name, "lightning")) return weather_strike(w, 0);
    if (!strcmp(name, "stag")) return weather_strike(w, 1);
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
    float period = t < FF_SYNC ? 2.0f - 0.3f * ease01(t / FF_SYNC) : 1.7f - 1.25f * powf(u, 0.8f);
    float peak = 1.0f + 1.6f * powf(u, 1.2f);
    float base = 1.5f * powf(u, 2.5f);
    float conv = 1.0f - ease01(t / (FF_SYNC * 0.8f));                           /* 1 = out of step, 0 = in step */
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
    update_prints(e, dt);
    if (e->kind == EV_GHOST) {
        update_ghost(w, e, dt);
        return;
    }
    if (e->kind == EV_FIREFLIES) {
        update_fireflies(w, e, dt);
        return;
    }
    if (e->kind == EV_DHOLES) {
        update_dholes(w, e, dt);
        return;
    }
    if (e->kind == EV_BATS) {
        update_bats(w, e, dt);
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
    /* something unseen walks through, leaving glowing footprints: any time now and then, in fog or mist more often
     * (only there the walker itself shows, as the mist parts around it) */
    if (rng_chance(&e->rng, weather_at(w, WX_FOG, (float)w->w * 0.5f) > 0.4f ? 0.55f : 0.15f) && start_ghost(w)) return;
    /* dholes about (so the biome has them): now and then they pick up a deer's trail instead */
    int dh[1];
    if (free_dholes(w, dh, 1) && rng_chance(&e->rng, 0.5f) && start_dholes(w)) return;
    /* bats: mostly early in the night (the scene is young), now and then later too */
    if (rng_chance(&e->rng, w->t < 600.0f ? 0.45f : 0.2f) && start_bats(w)) return;
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
    render_prints(w, r);
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
