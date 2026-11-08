/* actor.c — creature brain: profile, intent selection, step plan, execution, reactions
 *
 * One creature's cycle per tick:
 *   perception (stimuli) → drives (fear, hunger, fatigue, boredom)
 *   → if needed, interrupting the plan (fleeing, freezing, turning towards a sound)
 *   → executing the current step → when the plan finishes, picking a new intent.
 */
#include "world.h"
#include "noise.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *ANIM_NAMES[A_COUNT] = {
    "idle", "move", "run", "look", "alert", "eat", "groom", "rest", "freeze", "sniff",
    "climb", "climb_down", "appear", "vanish", "sit",
    "hang", "land", "takeoff", "hollow", "hollow_peek"
};

static const char *INTENT_NAMES[IN_COUNT] = {
    "none", "wander", "forage", "look", "groom", "rest", "hide", "peek", "leave",
    "climb", "perch", "flee", "follow", "watch", "pass", "hover", "sniff", "emerge", "vanish", "roost"
};

const char *intent_name(int i) { return (i >= 0 && i < IN_COUNT) ? INTENT_NAMES[i] : "?"; }

/* ------------------------------------------------------------------ */
/* profile                                                             */
/* ------------------------------------------------------------------ */

static int clip_or(const sprite_t *s, const char *a, const char *b)
{
    int c = a ? sprite_clip_index(s, a) : -1;
    if (c < 0 && b) c = sprite_clip_index(s, b);
    return c;
}

void profile_parse(const sprite_t *s, profile_t *p)
{
    memset(p, 0, sizeof *p);
    const char *mv = sprite_prop(s, "move");
    if (mv) {
        if (strstr(mv, "ground")) p->navmask |= NAV_GROUND;
        if (strstr(mv, "climb")) p->navmask |= NAV_CLIMB;
        if (strstr(mv, "air")) p->navmask |= NAV_AIR;
        if (strstr(mv, "perch")) p->navmask |= NAV_PERCH;
        if (strstr(mv, "hidden")) p->navmask |= NAV_HIDDEN;
    } else {
        p->navmask = NAV_GROUND;
    }
    char buf[NF_NAME];
    p->walk = sprite_prop_sub(s, "speed", "walk", buf, sizeof buf) ? (float)atof(buf) : 10.0f;
    p->run = sprite_prop_sub(s, "speed", "run", buf, sizeof buf) ? (float)atof(buf) : p->walk * 2.5f;
    p->climb = sprite_prop_sub(s, "speed", "climb", buf, sizeof buf) ? (float)atof(buf) : p->walk;

    int idle = clip_or(s, "idle", NULL);
    if (idle < 0) idle = 0;
    for (int a = 0; a < A_COUNT; a++) {
        int c = -1;
        if (sprite_prop_sub(s, "anim", ANIM_NAMES[a], buf, sizeof buf)) c = sprite_clip_index(s, buf);
        if (c < 0) c = sprite_clip_index(s, ANIM_NAMES[a]);
        p->anim[a] = c;
    }
    /* sensible fallbacks */
    if (p->anim[A_IDLE] < 0) p->anim[A_IDLE] = idle;
    if (p->anim[A_MOVE] < 0) p->anim[A_MOVE] = clip_or(s, "walk", "fly");
    if (p->anim[A_MOVE] < 0) p->anim[A_MOVE] = p->anim[A_IDLE];
    if (p->anim[A_RUN] < 0) p->anim[A_RUN] = p->anim[A_MOVE];
    if (p->anim[A_CLIMB] < 0) p->anim[A_CLIMB] = p->anim[A_MOVE];
    if (p->anim[A_CLIMB_DOWN] < 0) p->anim[A_CLIMB_DOWN] = p->anim[A_CLIMB];
    if (p->anim[A_SIT] < 0) p->anim[A_SIT] = p->anim[A_IDLE];
    for (int a = 0; a < A_COUNT; a++)
        if (p->anim[a] < 0) p->anim[a] = p->anim[A_IDLE];

    const char *iw = sprite_prop(s, "intents");
    if (iw) {
        for (int i = 1; i < IN_COUNT; i++)
            if (sprite_prop_sub(s, "intents", INTENT_NAMES[i], buf, sizeof buf)) p->iw[i] = (float)atof(buf);
    } else {
        p->iw[IN_WANDER] = 1;
    }
    const char *h = sprite_prop(s, "homes");
    if (h) {
        if (strstr(h, "bush")) p->homes |= 1 << PL_ENTRY;
        if (strstr(h, "log") || strstr(h, "stone") || strstr(h, "brushwood") || strstr(h, "stump"))
            p->homes |= 1 << PL_UNDER;
        if (strstr(h, "hollow")) p->homes |= 1 << PL_HOLLOW;
        if (strstr(h, "branch")) p->homes |= 1 << PL_PERCH;
        if (strstr(h, "edge")) p->homes |= 1 << PL_EDGE;
    }
    const char *v;
    p->fear = (v = sprite_prop(s, "fear")) ? (float)atof(v) : 0.5f;
    p->curiosity = (v = sprite_prop(s, "curiosity")) ? (float)atof(v) : 0.5f;
    p->lane = (v = sprite_prop(s, "lane")) ? (float)atof(v) : 0.15f;
    p->rare = (v = sprite_prop(s, "rare")) ? atoi(v) : 0;
    p->burrow = (v = sprite_prop(s, "burrow")) ? atoi(v) : 0;
    if ((v = sprite_prop(s, "scares"))) snprintf(p->scares, sizeof p->scares, " %s ", v);
    p->attracted_glow = (v = sprite_prop(s, "attracted")) && strstr(v, "glow");
    if ((v = sprite_prop(s, "point.head"))) sscanf(v, "%d %d", &p->head_dx, &p->head_dy);
}

/* ------------------------------------------------------------------ */
/* helpers                                                             */
/* ------------------------------------------------------------------ */

static int actor_index(const world_t *w, const actor_t *a) { return (int)(a - w->act); }

static void set_anim(actor_t *a, int anim)
{
    int c = a->prof.anim[anim];
    if (c < 0 || c >= a->spr->nclips) c = 0;
    if (c != a->clip) { a->clip = c; a->ct = 0; }
    a->anim = anim;
}

static void plan_clear(actor_t *a) { a->nplan = 0; a->step = 0; a->step_t = 0; a->step_init = 0; }

static step_t *plan_push(actor_t *a, int kind)
{
    if (a->nplan >= MAX_PLAN) return NULL;
    step_t *s = &a->plan[a->nplan++];
    memset(s, 0, sizeof *s);
    s->kind = kind;
    s->place = -1;
    return s;
}

static void push_anim(actor_t *a, int anim, float dur)
{
    step_t *s = plan_push(a, ST_ANIM);
    if (s) { s->anim = anim; s->dur = dur; }
}

/* Inserts steps before the current step (interruption). */
static void plan_interrupt(actor_t *a, const step_t *steps, int n)
{
    if (a->nplan + n > MAX_PLAN) { a->nplan = a->step; }
    if (a->nplan + n > MAX_PLAN) return;
    memmove(&a->plan[a->step + n], &a->plan[a->step], sizeof(step_t) * (size_t)(a->nplan - a->step));
    memcpy(&a->plan[a->step], steps, sizeof(step_t) * (size_t)n);
    a->nplan += n;
    a->step_t = 0;
    a->step_init = 0;
}

static float sgnf(float v) { return v < 0 ? -1.0f : 1.0f; }

/* Ground movement split into segments with pauses (irregular, lively movement). */
static void plan_ground_move(world_t *w, actor_t *a, float tx, int run, int z)
{
    if (run) {
        step_t *s = plan_push(a, ST_RUNTO);
        if (s) { s->x = tx; s->z = z; }
        return;
    }
    float x = a->x;
    for (int guard = 0; guard < 6 && fabsf(tx - x) > 1.0f; guard++) {
        float seg = rng_range(&w->rng, 14, 40);
        float nx = fabsf(tx - x) <= seg ? tx : x + sgnf(tx - x) * seg;
        step_t *s = plan_push(a, ST_MOVE);
        if (!s) return;
        s->x = nx; s->z = z;
        x = nx;
        if (fabsf(tx - x) > 1.0f && rng_chance(&w->rng, 0.55f)) {
            static const int pause_anims[] = { A_IDLE, A_SNIFF, A_LOOK, A_IDLE };
            push_anim(a, pause_anims[rng_int(&w->rng, 4)], rng_range(&w->rng, 0.4f, 1.6f));
        }
    }
}

static int can_reach_place(const actor_t *a, const place_t *p)
{
    int m = a->prof.navmask;
    switch (p->type) {
    case PL_HOLLOW: return (m & (NAV_CLIMB | NAV_AIR)) != 0;
    case PL_PERCH:  return (m & (NAV_PERCH | NAV_AIR)) != 0;
    case PL_ENTRY: case PL_UNDER: case PL_TRUNK: case PL_EDGE: return (m & NAV_GROUND) != 0;
    case PL_PEEK:   return a->hidden;
    default:        return 0;
    }
}

static int find_place(world_t *w, actor_t *a, int typemask, int free_only, float maxdist)
{
    int best = -1;
    float bd = 1e9f;
    for (int i = 0; i < w->nplace; i++) {
        place_t *p = &w->place[i];
        if (!(typemask & (1 << p->type))) continue;
        if (free_only && p->occupant >= 0 && p->occupant != actor_index(w, a)) continue;
        if (!can_reach_place(a, p)) continue;
        float d = fabsf(p->x - a->x) + fabsf(p->y - a->y) * 0.5f + rng_range(&w->rng, 0, 25);
        if (d < bd && d < maxdist) { bd = d; best = i; }
    }
    return best;
}

static int trunk_of(world_t *w, int obj)
{
    for (int i = 0; i < w->nplace; i++)
        if (w->place[i].type == PL_TRUNK && w->place[i].obj == obj) return i;
    return -1;
}

static float screen_x(world_t *w, float margin)
{
    return rng_range(&w->rng, margin, (float)w->w - margin);
}

static float air_y(world_t *w)
{
    return rng_range(&w->rng, (float)w->h * 0.12f, (float)w->h * 0.65f);
}

/* ------------------------------------------------------------------ */
/* intent planning                                                      */
/* ------------------------------------------------------------------ */

/* --- paths ------------------------------------------------------------ */

static void push_path(actor_t *a, int path, float t, int z)
{
    step_t *s = plan_push(a, ST_PATH);
    if (s) { s->place = path; s->x = t; s->z = z; }
}

/* A climbing path whose start is on the ground (it can be climbed from the ground).
 * Main trunks (closer, with branches) have more weight than trees in the background. */
static int pick_ground_climb(world_t *w, actor_t *a, float maxdist)
{
    float wsum = 0, wt[MAX_PATH];
    for (int i = 0; i < w->npath; i++) {
        navpath_t *p = &w->path[i];
        wt[i] = 0;
        if (p->kind != PATH_CLIMB) continue;
        if (fabsf(p->y[0] - world_ground(w, p->x[0])) > 6.0f) continue;
        if (fabsf(p->x[0] - a->x) > maxdist) continue;
        int links = 0;
        for (int k = 0; k < w->nlink; k++)
            if ((w->link[k].a == i && w->path[w->link[k].b].kind == PATH_WALK) ||
                (w->link[k].b == i && w->path[w->link[k].a].kind == PATH_WALK)) links++;
        wt[i] = (p->z >= 85 ? 3.0f : 1.0f) * (1.0f + (float)links * 2.0f);
        wsum += wt[i];
    }
    if (wsum <= 0) return -1;
    float r = rng_range(&w->rng, 0, wsum);
    for (int i = 0; i < w->npath; i++) {
        r -= wt[i];
        if (wt[i] > 0 && r <= 0) return i;
    }
    return -1;
}

/* A link from path `path` to a path of kind want_kind (-1 = any). */
static int pick_link_kind(world_t *w, int path, int want_kind, int *other, float *t_here, float *t_other)
{
    int c[MAX_LINK], n = 0;
    for (int i = 0; i < w->nlink; i++) {
        int o = w->link[i].a == path ? w->link[i].b : w->link[i].b == path ? w->link[i].a : -1;
        if (o < 0 || (want_kind >= 0 && w->path[o].kind != want_kind)) continue;
        c[n++] = i;
    }
    if (!n) return -1;
    navlink_t *l = &w->link[c[rng_int(&w->rng, n)]];
    if (l->a == path) { *other = l->b; *t_here = l->ta; *t_other = l->tb; }
    else { *other = l->a; *t_here = l->tb; *t_other = l->ta; }
    return 0;
}

static int pick_link(world_t *w, int path, int *other, float *t_here, float *t_other)
{
    return pick_link_kind(w, path, PATH_WALK, other, t_here, t_other);
}

/* Climb from the ground onto a path (move to the base, change depth). */
static void plan_mount(world_t *w, actor_t *a, int path, int run)
{
    navpath_t *p = &w->path[path];
    plan_ground_move(w, a, p->x[0], run, p->z);       /* from a front row it first walks back to the trunk */
    step_t *s = plan_push(a, ST_DEPTH);
    if (s) { s->z = p->z; s->dur = 0.3f; }
}

static void plan_dismount(world_t *w, actor_t *a, int path)
{
    push_path(a, path, 0.0f, w->path[path].z);
    step_t *s = plan_push(a, ST_DEPTH);
    if (s) { s->z = a->lane_z; s->dur = 0.3f; }
}

/* --- roosting: hollow (owl) / hanging under a branch (bat) --- */

static int has_anim(const actor_t *a, int anim) { return a->prof.anim[anim] != a->prof.anim[A_IDLE]; }

static int free_hollow(world_t *w, actor_t *a)
{
    /* only hollows visible on screen; main trunks (closer) take priority */
    int c[MAX_PLACE], n = 0;
    for (int i = 0; i < w->nplace; i++) {
        place_t *p = &w->place[i];
        if (p->type != PL_HOLLOW || !p->has_eye) continue;
        if (p->occupant >= 0 && p->occupant != actor_index(w, a)) continue;
        if (p->ey < (float)w->h * 0.15f || p->ey > (float)w->h * 0.75f || p->ex < 4.0f || p->ex > (float)w->w - 4.0f) continue;
        int main_trunk = p->obj >= 0 && w->obj[p->obj].z >= 85;
        for (int k = 0; k < (main_trunk ? 3 : 1); k++) c[n++] = i;
    }
    return n ? c[rng_int(&w->rng, n)] : -1;
}

/* Is the spot on the hanging path free (no other creature within 6 px)? */
static int hang_spot_free(world_t *w, actor_t *a, int path, float t)
{
    for (int i = 0; i < w->nact; i++) {
        actor_t *o = &w->act[i];
        if (o == a || !o->alive || o->path != path) continue;
        if (fabsf(o->path_t - t) < 6.0f) return 0;
    }
    return 1;
}

static int random_hang_path(world_t *w)
{
    int c[MAX_PATH], n = 0;
    for (int i = 0; i < w->npath; i++)
        if (w->path[i].kind == PATH_HANG && w->path[i].y[0] > 4 && w->path[i].x[0] > 0 && w->path[i].x[0] < (float)w->w)
            c[n++] = i;
    return n ? c[rng_int(&w->rng, n)] : -1;
}

static int roost_available(world_t *w, actor_t *a)
{
    if (has_anim(a, A_HOLLOW) && free_hollow(w, a) >= 0) return 1;
    if (has_anim(a, A_HANG) && random_hang_path(w) >= 0) return 1;
    return 0;
}

/* safe flight depth: in front of trunks and bushes, behind the foreground */
static int cruise_z(const world_t *w) { return w->ground_z + 14; }

static void plan_hide(world_t *w, actor_t *a, int run)
{
    int mask = a->prof.homes & ((1 << PL_ENTRY) | (1 << PL_UNDER) | (1 << PL_HOLLOW));
    int pi = find_place(w, a, mask, 1, 400);
    if (pi < 0) {
        /* nowhere to hide -> off the screen */
        float ex = a->x < (float)w->w / 2 ? -30.0f : (float)w->w + 30.0f;
        plan_ground_move(w, a, ex, run, a->lane_z);
        plan_push(a, ST_DESPAWN);
        a->intent = IN_FLEE;
        return;
    }
    place_t *p = &w->place[pi];
    if (p->type == PL_HOLLOW) {
        if (a->nav == NAV_AIR || (a->prof.navmask & NAV_AIR)) {
            step_t *s = plan_push(a, ST_FLY);
            if (s) { s->x = p->x; s->y = p->y; s->z = p->z + 6; }
        } else if (p->path >= 0) {
            /* along the climbing path to the hollow */
            if (a->nav != NAV_CLIMB || a->path != p->path) plan_mount(w, a, p->path, run);
            push_path(a, p->path, p->path_t, w->path[p->path].z);
        } else {
            int tb = trunk_of(w, p->obj);
            if (tb < 0) return;
            if (a->nav != NAV_CLIMB) plan_ground_move(w, a, w->place[tb].climb_x, run, w->place[tb].z);
            step_t *s = plan_push(a, ST_CLIMB);
            if (s) { s->x = p->climb_x; s->y = p->y; s->z = w->place[tb].z; }
        }
    } else {
        plan_ground_move(w, a, p->x, run, a->lane_z);
    }
    step_t *s = plan_push(a, ST_ENTER);
    if (s) s->place = pi;
    p->occupant = actor_index(w, a);
}

static void plan_leave(world_t *w, actor_t *a, int run)
{
    if (a->hidden) { plan_push(a, ST_DESPAWN); return; }
    if (a->nav == NAV_AIR || a->nav == NAV_PERCH) {
        step_t *s = plan_push(a, ST_FLY);
        if (s) {
            s->x = rng_chance(&w->rng, 0.5f) ? -30.0f : (float)w->w + 30.0f;
            s->y = rng_range(&w->rng, -20.0f, (float)w->h * 0.4f);
            s->z = (int)a->zf;
        }
        plan_push(a, ST_DESPAWN);
        return;
    }
    if (a->nav == NAV_CLIMB && a->path >= 0) {
        navpath_t *np = &w->path[a->path];
        int up = np->y[np->n - 1] < 0 && rng_chance(&w->rng, 0.5f);   /* the end of the path is above the screen */
        if (up) {
            push_path(a, a->path, np->len, np->z);
        } else {
            plan_dismount(w, a, a->path);
            plan_ground_move(w, a, a->x < (float)w->w / 2 ? -30.0f : (float)w->w + 30.0f, run, a->lane_z);
        }
        plan_push(a, ST_DESPAWN);
        return;
    }
    if (a->nav == NAV_CLIMB) {
        step_t *s = plan_push(a, ST_CLIMB);
        if (s) { s->x = a->x; s->y = -30.0f; s->z = (int)a->zf; }
        plan_push(a, ST_DESPAWN);
        return;
    }
    float ex = (a->x < (float)w->w / 2) ^ rng_chance(&w->rng, 0.25f) ? -30.0f : (float)w->w + 30.0f;
    plan_ground_move(w, a, ex, run, a->lane_z);
    plan_push(a, ST_DESPAWN);
}

static int find_glow_target(world_t *w, actor_t *a)
{
    int best = -1;
    float bd = 1e9f;
    for (int i = 0; i < w->nact; i++) {
        actor_t *o = &w->act[i];
        if (o == a || !o->alive || o->hidden) continue;
        if (!sprite_has_tag(o->spr, "glow")) continue;
        float d = fabsf(o->x - a->x) + fabsf(o->y - a->y);
        if (d < bd) { bd = d; best = i; }
    }
    return best;
}

static int nearest_mover(world_t *w, actor_t *a, float maxd)
{
    int best = -1;
    float bd = maxd;
    for (int i = 0; i < w->nact; i++) {
        actor_t *o = &w->act[i];
        if (o == a || !o->alive || o->hidden) continue;
        float d = fabsf(o->x - a->x) + fabsf(o->y - a->y) * 0.7f;
        if (d < bd) { bd = d; best = i; }
    }
    return best;
}

/* Ground rows span from the standard lane (ground_z, where trunks can be climbed)
 * to just in front of the middle undergrowth band (fg_z + 6). */
int actor_pick_lane(world_t *w, const profile_t *p)
{
    float t;
    if (rng_chance(&w->rng, 0.12f)) t = p->lane + rng_range(&w->rng, -0.55f, 0.55f);   /* now and then far from its usual row */
    else t = p->lane + (rng_float(&w->rng) + rng_float(&w->rng) - 1.0f) * 0.3f;
    t = clampf(t, -0.15f, 1.0f);
    return w->ground_z + (int)lroundf(t * (float)(w->fg_z + 6 - w->ground_z));
}

static void plan_intent(world_t *w, actor_t *a, int in)
{
    plan_clear(a);
    a->intent = in;
    a->intent_age = 0;
    int ground = a->nav == NAV_GROUND;
    int air = a->nav == NAV_AIR || a->nav == NAV_PERCH;
    step_t *s;

    switch (in) {
    case IN_WANDER:
        if (air) {
            s = plan_push(a, ST_FLY);
            if (s) { s->x = screen_x(w, 10); s->y = air_y(w); s->z = (int)a->zf; }
            if (a->prof.anim[A_IDLE] != a->prof.anim[A_MOVE]) push_anim(a, A_IDLE, rng_range(&w->rng, 0.5f, 2.0f));
        } else if (a->nav == NAV_CLIMB) {
            s = plan_push(a, ST_CLIMB);
            if (s) { s->x = a->x; s->y = world_ground(w, a->x); s->z = (int)a->zf; }
        } else {
            float tx = clampf(a->x + rng_range(&w->rng, -70, 70), 12, (float)w->w - 12);
            if (rng_chance(&w->rng, 0.35f)) a->lane_z = actor_pick_lane(w, &a->prof);
            plan_ground_move(w, a, tx, 0, a->lane_z);
            push_anim(a, A_IDLE, rng_range(&w->rng, 1.0f, 3.0f));
        }
        break;
    case IN_HOVER:
        s = plan_push(a, ST_FLY);
        if (s) {
            s->x = clampf(a->x + rng_range(&w->rng, -25, 25), 4, (float)w->w - 4);
            s->y = clampf(a->y + rng_range(&w->rng, -12, 12), 6, world_ground(w, a->x) - 6);
            s->z = (int)a->zf;
        }
        break;
    case IN_FORAGE: {
        float tx = clampf(a->x + rng_range(&w->rng, -60, 60), 12, (float)w->w - 12);
        if (ground && rng_chance(&w->rng, 0.3f)) a->lane_z = actor_pick_lane(w, &a->prof);
        plan_ground_move(w, a, tx, 0, a->lane_z);
        push_anim(a, A_SNIFF, rng_range(&w->rng, 0.8f, 1.8f));
        push_anim(a, A_EAT, rng_range(&w->rng, 2.5f, 6.0f));
        if (rng_chance(&w->rng, 0.5f)) push_anim(a, A_LOOK, rng_range(&w->rng, 1.0f, 2.5f));
        a->hunger = fmaxf(0, a->hunger - 0.5f);
        break;
    }
    case IN_SNIFF:
        push_anim(a, A_SNIFF, rng_range(&w->rng, 1.5f, 3.5f));
        break;
    case IN_LOOK:
        s = plan_push(a, ST_FACE);
        if (s) s->x = a->x + rng_range(&w->rng, -10, 10);
        push_anim(a, A_LOOK, rng_range(&w->rng, 1.5f, 4.0f));
        if (rng_chance(&w->rng, 0.4f)) push_anim(a, A_ALERT, rng_range(&w->rng, 1.0f, 2.5f));
        break;
    case IN_GROOM:
        push_anim(a, A_GROOM, rng_range(&w->rng, 2.0f, 4.5f));
        break;
    case IN_REST:
        push_anim(a, A_REST, rng_range(&w->rng, 4.0f, 10.0f));
        a->fatigue = 0;
        break;
    case IN_HIDE:
        plan_hide(w, a, a->fear > 0.5f);
        break;
    case IN_PEEK: {
        /* peek out of its hideout: from a bush through an opening, from a hollow directly */
        place_t *home = a->place >= 0 ? &w->place[a->place] : NULL;
        int pi = -1;
        if (home && (home->type == PL_HOLLOW || home->type == PL_UNDER)) pi = a->place;
        else if (home) {
            int cands[8], n = 0;
            for (int i = 0; i < w->nplace && n < 8; i++)
                if (w->place[i].type == PL_PEEK && w->place[i].obj == home->obj) cands[n++] = i;
            if (n) pi = cands[rng_int(&w->rng, n)];
        }
        if (pi < 0) { push_anim(a, A_IDLE, 1.0f); break; }
        s = plan_push(a, ST_PEEK);
        if (s) { s->place = pi; s->dur = rng_range(&w->rng, 3.0f, 8.0f); }
        break;
    }
    case IN_WATCH: {
        place_t *home = a->place >= 0 ? &w->place[a->place] : NULL;
        int pi = home ? a->place : -1;
        if (home && home->type == PL_ENTRY) {
            int cands[8], n = 0;
            for (int i = 0; i < w->nplace && n < 8; i++)
                if (w->place[i].type == PL_PEEK && w->place[i].obj == home->obj) cands[n++] = i;
            if (n) pi = cands[rng_int(&w->rng, n)];
        }
        if (pi < 0) { plan_push(a, ST_DESPAWN); break; }
        s = plan_push(a, ST_PEEK);
        if (s) { s->place = pi; s->dur = rng_range(&w->rng, 5.0f, 14.0f); s->anim = A_LOOK; }
        break;
    }
    case IN_EMERGE: {
        place_t *p = &w->place[a->place];
        s = plan_push(a, ST_EXIT);
        if (s) s->place = a->place;
        if (p->type == PL_HOLLOW && p->path >= 0) {
            s = plan_push(a, ST_DEPTH);
            if (s) { s->z = w->path[p->path].z; s->dur = 0.6f; }
            push_anim(a, A_LOOK, rng_range(&w->rng, 1.0f, 2.5f));
            /* sometimes first higher up or onto a branch, then down */
            int other;
            float th, to;
            if (rng_chance(&w->rng, 0.4f) && pick_link(w, p->path, &other, &th, &to) == 0) {
                push_path(a, p->path, th, w->path[p->path].z);
                push_path(a, other, to, w->path[other].z);
                push_path(a, other, rng_range(&w->rng, 0, w->path[other].len), w->path[other].z);
                push_anim(a, A_LOOK, rng_range(&w->rng, 1.0f, 3.0f));
                push_path(a, other, to, w->path[other].z);
                push_path(a, p->path, th, w->path[p->path].z);
            }
            plan_dismount(w, a, p->path);
        } else if (p->type == PL_HOLLOW) {
            int tb = trunk_of(w, p->obj);
            int tz = tb >= 0 ? w->place[tb].z : (int)p->z + 8;
            s = plan_push(a, ST_DEPTH);
            if (s) { s->z = tz; s->dur = 0.6f; }
            push_anim(a, A_LOOK, rng_range(&w->rng, 1.0f, 2.5f));
            s = plan_push(a, ST_CLIMB);
            if (s) { s->x = p->climb_x; s->y = world_ground(w, p->climb_x); s->z = tz; }
        } else {
            float out = rng_range(&w->rng, 14, 34) * (rng_chance(&w->rng, 0.5f) ? -1.0f : 1.0f);
            s = plan_push(a, ST_MOVE);
            if (s) { s->x = clampf(p->x + out, 8, (float)w->w - 8); s->z = a->lane_z; }
            push_anim(a, A_LOOK, rng_range(&w->rng, 0.8f, 2.0f));
        }
        break;
    }
    case IN_LEAVE:
        plan_leave(w, a, 0);
        break;
    case IN_FLEE:
        plan_leave(w, a, 1);
        break;
    case IN_VANISH:
        if (!a->hidden && sprite_clip_index(a->spr, "vanish") >= 0) push_anim(a, A_VANISH, -1.0f);
        plan_push(a, ST_DESPAWN);
        break;
    case IN_CLIMB: {
        /* pick a climbing path from the ground and one of the options:
         *  up and away · onto a branch (link) and back · into a hollow · a bit up, look around, down */
        int pc = pick_ground_climb(w, a, 600);
        if (pc < 0) { push_anim(a, A_LOOK, 1.5f); break; }
        navpath_t *p = &w->path[pc];
        plan_mount(w, a, pc, 0);
        int other;
        float th, to;
        int hollow = -1;
        for (int i = 0; i < w->nplace; i++)
            if (w->place[i].type == PL_HOLLOW && w->place[i].path == pc && w->place[i].occupant < 0) hollow = i;
        int r = rng_int(&w->rng, 10);
        if (r < 6 && pick_link(w, pc, &other, &th, &to) == 0) {
            navpath_t *o = &w->path[other];
            float far_t = to < o->len * 0.5f ? rng_range(&w->rng, o->len * 0.5f, o->len) : rng_range(&w->rng, 0, o->len * 0.5f);
            push_path(a, pc, th, p->z);
            push_path(a, other, to, o->z);
            push_path(a, other, far_t, o->z);
            push_anim(a, rng_chance(&w->rng, 0.5f) ? A_EAT : A_LOOK, rng_range(&w->rng, 1.5f, 4.0f));
            if (rng_chance(&w->rng, 0.3f)) push_anim(a, A_GROOM, rng_range(&w->rng, 1.0f, 2.5f));
            push_path(a, other, to, o->z);
            push_path(a, pc, th, p->z);
            plan_dismount(w, a, pc);
        } else if (r < 8 && hollow >= 0) {
            push_path(a, pc, w->place[hollow].path_t, p->z);
            step_t *e = plan_push(a, ST_ENTER);
            if (e) e->place = hollow;
            w->place[hollow].occupant = actor_index(w, a);
        } else if (r < 9) {
            push_path(a, pc, rng_range(&w->rng, p->len * 0.2f, p->len * 0.7f), p->z);
            push_anim(a, A_LOOK, rng_range(&w->rng, 1.0f, 3.0f));
            plan_dismount(w, a, pc);
        } else {
            push_path(a, pc, p->len, p->z);
            plan_push(a, ST_DESPAWN);
        }
        break;
    }
    case IN_PERCH: {
        int pi = find_place(w, a, 1 << PL_PERCH, 1, 2000);
        if (pi < 0) { plan_intent(w, a, IN_WANDER); return; }
        place_t *p = &w->place[pi];
        s = plan_push(a, ST_FLY);
        if (s) { s->x = p->x; s->y = p->y; s->z = p->z; s->place = pi; }
        push_anim(a, A_IDLE, rng_range(&w->rng, 6.0f, 18.0f));
        break;
    }
    case IN_FOLLOW: {
        int t = a->prof.attracted_glow ? find_glow_target(w, a) : nearest_mover(w, a, 200);
        if (t < 0) { plan_intent(w, a, air ? IN_HOVER : IN_WANDER); return; }
        a->target = t;
        s = plan_push(a, ST_FLY);
        if (s) {
            s->x = w->act[t].x + rng_range(&w->rng, -8, 8);
            s->y = w->act[t].y + rng_range(&w->rng, -6, 6);
            s->z = (int)a->zf;
        }
        break;
    }
    case IN_ROOST: {
        int h = has_anim(a, A_HOLLOW) ? free_hollow(w, a) : -1;
        if (h >= 0) {
            /* owl: flies to the opening (at a safe depth), slips in, the eyes glow, occasionally pokes its head out */
            place_t *p = &w->place[h];
            p->occupant = actor_index(w, a);
            s = plan_push(a, ST_FLY);
            if (s) { s->x = p->ex; s->y = p->ey; s->z = cruise_z(w); }
            s = plan_push(a, ST_HOLLOW);
            if (s) { s->place = h; s->dur = rng_range(&w->rng, 15.0f, 50.0f); }
            s = plan_push(a, ST_FLY);
            if (s) { s->x = screen_x(w, 10); s->y = air_y(w); s->z = cruise_z(w); }
            break;
        }
        int hp = has_anim(a, A_HANG) ? random_hang_path(w) : -1;
        if (hp >= 0) {
            /* bat: flies under a branch, flips over, hangs head down */
            navpath_t *p = &w->path[hp];
            float t = 0, x, y;
            int ok = 0;
            for (int tries = 0; tries < 8 && !ok; tries++) {
                t = rng_range(&w->rng, 2.0f, fmaxf(2.5f, p->len - 2.0f));
                ok = hang_spot_free(w, a, hp, t);
            }
            if (!ok) { plan_intent(w, a, IN_WANDER); return; }
            a->path = hp;               /* reserve the spot on the branch */
            a->path_t = t;
            path_point(p, t, &x, &y);
            s = plan_push(a, ST_FLY);
            if (s) { s->x = x; s->y = y + 4.0f; s->z = p->z; }
            s = plan_push(a, ST_HANG);
            if (s) { s->place = hp; s->x = t; s->dur = rng_range(&w->rng, 10.0f, 35.0f); }
            s = plan_push(a, ST_FLY);
            if (s) { s->x = screen_x(w, 10); s->y = air_y(w); s->z = cruise_z(w); }
            break;
        }
        plan_intent(w, a, IN_WANDER);
        return;
    }
    case IN_PASS: {
        float ex = a->x < (float)w->w / 2 ? (float)w->w + 60.0f : -60.0f;
        s = plan_push(a, ST_MOVE);
        if (s) { s->x = ex; s->z = a->lane_z; }
        plan_push(a, ST_DESPAWN);
        break;
    }
    default:
        push_anim(a, A_IDLE, 1.0f);
        break;
    }
    (void)ground;
    world_log(w, "%s#%u → %s", a->spr->name, a->id, intent_name(a->intent));
}

static void choose_intent(world_t *w, actor_t *a)
{
    float score[IN_COUNT];
    memset(score, 0, sizeof score);
    const profile_t *p = &a->prof;

    if (a->hidden) {
        /* in a hideout: peek out, come out, stay, vanish */
        int is_eyes = (p->navmask & NAV_HIDDEN) && !(p->navmask & NAV_GROUND);
        if (is_eyes) {
            score[IN_WATCH] = (p->iw[IN_WATCH] + 0.1f) * (a->intent_age < 1 ? 2.0f : 1.0f);
            score[IN_VANISH] = (p->iw[IN_VANISH] + 0.5f) * (0.3f + a->boredom * 3.0f);
        } else {
            score[IN_PEEK] = (1.0f + p->curiosity * 3.0f) * (a->fear > 0.4f ? 0.3f : 1.0f);
            score[IN_EMERGE] = 2.5f * (1.0f - a->fear) * (a->fear > 0.35f ? 0.0f : 1.0f);
            score[IN_VANISH] = 0.4f + a->boredom * 2.5f;
            score[IN_REST] = 0.8f + a->fear * 3.0f;     /* stays hidden quietly */
        }
    } else {
        for (int i = 1; i < IN_COUNT; i++) score[i] = p->iw[i];
        int g = a->nav == NAV_GROUND;
        score[IN_FORAGE] *= g ? 0.5f + a->hunger : 0.0f;
        score[IN_REST] *= 0.3f + a->fatigue * 2.0f;
        score[IN_LEAVE] *= 0.3f + a->boredom * 2.5f;
        score[IN_HIDE] *= 0.4f + a->fear * 3.0f + a->boredom;
        if (!p->homes) score[IN_HIDE] = 0;
        score[IN_PEEK] = 0;
        score[IN_EMERGE] = 0;
        score[IN_WATCH] = 0;
        if (!p->burrow) score[IN_VANISH] = 0;
        else score[IN_VANISH] *= 0.3f + a->boredom * 3.0f + a->fear * 4.0f;
        if (!(p->navmask & NAV_CLIMB) || pick_ground_climb(w, a, 2000) < 0) score[IN_CLIMB] = 0;
        if (!(p->navmask & (NAV_PERCH | NAV_AIR)) || find_place(w, a, 1 << PL_PERCH, 1, 4000) < 0) score[IN_PERCH] = 0;
        if (!roost_available(w, a)) score[IN_ROOST] = 0;
        if (!g) { score[IN_GROOM] *= 0.2f; score[IN_SNIFF] = 0; }
        if (a->nav == NAV_AIR) score[IN_LOOK] *= 0.2f;
        if (a->nav == NAV_PERCH) { score[IN_LOOK] += 2.0f; score[IN_WANDER] *= 0.3f; }
    }
    int best = IN_WANDER;
    float bs = -1;
    for (int i = 1; i < IN_COUNT; i++) {
        if (score[i] <= 0) continue;
        float s = score[i] * rng_range(&w->rng, 0.5f, 1.5f);
        if (s > bs) { bs = s; best = i; }
    }
    if (a->hidden && best == IN_REST) {
        plan_clear(a);
        a->intent = IN_REST;
        push_anim(a, A_IDLE, rng_range(&w->rng, 2.0f, 6.0f));
        return;
    }
    plan_intent(w, a, best);
}

/* ------------------------------------------------------------------ */
/* perception                                                          */
/* ------------------------------------------------------------------ */

static void perceive(world_t *w, actor_t *a, float dt)
{
    int me = actor_index(w, a);
    char key[NF_NAME + 2];
    snprintf(key, sizeof key, " %s ", a->spr->name);
    float danger = 0;
    float dx_danger = 0;
    for (int i = 0; i < w->nstim; i++) {
        stim_t *s = &w->stim[i];
        if (s->src == me) continue;
        float d = fabsf(s->x - a->x) + fabsf(s->y - a->y) * 0.6f;
        if (d > s->r) continue;
        if (s->kind == ST_DANGER && s->src >= 0 && strstr(w->act[s->src].prof.scares, key)) {
            float k = s->strength * (1.0f - d / s->r);
            if (k > danger) { danger = k; dx_danger = s->x - a->x; }
        } else if (s->kind == ST_NOISE && !a->hidden && a->intent != IN_FLEE && a->intent != IN_HIDE) {
            /* curious creatures turn towards a sound */
            if (a->nav == NAV_GROUND && rng_float(&w->rng) < a->prof.curiosity * dt * 2.0f &&
                a->step < a->nplan && a->plan[a->step].kind != ST_ANIM) {
                step_t st[2];
                memset(st, 0, sizeof st);
                st[0].kind = ST_FACE; st[0].x = s->x; st[0].place = -1;
                st[1].kind = ST_ANIM; st[1].anim = A_ALERT; st[1].dur = rng_range(&w->rng, 0.8f, 1.8f); st[1].place = -1;
                plan_interrupt(a, st, 2);
            }
        }
    }
    if (danger > 0) a->fear = fmaxf(a->fear, danger * (0.5f + a->prof.fear));
    else a->fear = fmaxf(0, a->fear - dt * 0.06f);

    /* fear -> freeze and run to a hideout */
    if (a->fear > 0.55f && a->prof.burrow && a->intent != IN_VANISH) {
        plan_intent(w, a, IN_VANISH);
        return;
    }
    if (a->fear > 0.55f && !a->hidden && a->intent != IN_HIDE && a->intent != IN_FLEE && a->nav == NAV_GROUND) {
        plan_clear(a);
        a->intent = IN_HIDE;
        if (a->prof.anim[A_FREEZE] != a->prof.anim[A_IDLE] && rng_chance(&w->rng, 0.7f)) {
            step_t *s = plan_push(a, ST_FACE);
            if (s) s->x = a->x + dx_danger;
            push_anim(a, A_FREEZE, rng_range(&w->rng, 0.6f, 1.8f));
        }
        plan_hide(w, a, 1);
        world_log(w, "%s#%u: danger! → %s", a->spr->name, a->id, intent_name(a->intent));
    }
}

/* ------------------------------------------------------------------ */
/* step execution                                                      */
/* ------------------------------------------------------------------ */

static float approach(float v, float target, float maxstep)
{
    float d = target - v;
    if (fabsf(d) <= maxstep) return target;
    return v + sgnf(d) * maxstep;
}

static float clip_speed(const actor_t *a, int anim, float fallback)
{
    const clip_t *c = &a->spr->clips[a->prof.anim[anim]];
    return c->speed > 0 ? c->speed : fallback;
}

/* Returns 1 if the step is finished. */
static int exec_step(world_t *w, actor_t *a, step_t *s, float dt)
{
    int first = !a->step_init;
    a->step_init = 1;
    switch (s->kind) {
    case ST_MOVE:
    case ST_RUNTO: {
        int run = s->kind == ST_RUNTO;
        set_anim(a, run ? A_RUN : A_MOVE);
        float spd = clip_speed(a, run ? A_RUN : A_MOVE, run ? a->prof.run : a->prof.walk) * a->speed_mul;
        float dx = s->x - a->x;
        if (fabsf(dx) < 0.6f) return 1;
        a->facing = dx < 0 ? -1 : 1;
        float st = sgnf(dx) * spd * dt;
        if (fabsf(st) > fabsf(dx)) st = dx;
        a->x += st;
        a->moved = st;
        a->nav = NAV_GROUND;
        a->path = -1;
        a->zf = approach(a->zf, (float)s->z, dt * 12.0f);
        a->y = world_ground_at(w, a->x, a->zf);
        return 0;
    }
    case ST_CLIMB: {
        float dy = s->y - a->y;
        set_anim(a, dy < 0 ? A_CLIMB : A_CLIMB_DOWN);
        a->nav = NAV_CLIMB;
        a->x = approach(a->x, s->x, dt * 20.0f);
        a->zf = approach(a->zf, (float)s->z, dt * 12.0f);
        if (fabsf(dy) < 0.6f) {
            if (fabsf(s->y - world_ground(w, a->x)) < 1.0f) a->nav = NAV_GROUND;
            return 1;
        }
        float spd = clip_speed(a, dy < 0 ? A_CLIMB : A_CLIMB_DOWN, a->prof.climb) * a->speed_mul;
        /* short stops while climbing */
        if (fmodf(a->step_t, 2.6f) > 2.2f) { set_anim(a, A_IDLE); return 0; }
        a->y = approach(a->y, s->y, spd * dt);
        return 0;
    }
    case ST_PATH: {
        navpath_t *p = &w->path[s->place];
        if (first || a->path != s->place) {
            a->path = s->place;
            a->path_t = path_nearest(p, a->x, a->y, NULL);
        }
        int climb = p->kind == PATH_CLIMB;
        float target = clampf(s->x, 0, p->len);
        float d = target - a->path_t;
        if (fabsf(d) < 0.4f) {
            a->path_t = target;
            if (fabsf(a->y - world_ground(w, a->x)) < 2.0f) a->nav = NAV_GROUND;
            return 1;
        }
        /* movement direction from the path geometry -> animation */
        float nx, ny;
        path_point(p, a->path_t + (d > 0 ? 1.0f : -1.0f), &nx, &ny);
        int up = ny < a->y - 0.05f;
        int anim = climb ? (up ? A_CLIMB : A_CLIMB_DOWN) : A_MOVE;
        set_anim(a, anim);
        if (!climb && fabsf(nx - a->x) > 0.05f) a->facing = nx < a->x ? -1 : 1;
        float spd = clip_speed(a, anim, climb ? a->prof.climb : a->prof.walk) * a->speed_mul;
        if (climb && fmodf(a->step_t, 2.6f) > 2.2f) { set_anim(a, A_IDLE); return 0; }
        a->path_t = approach(a->path_t, target, spd * dt);
        path_point(p, a->path_t, &a->x, &a->y);
        a->nav = climb ? NAV_CLIMB : NAV_PERCH;
        a->zf = approach(a->zf, (float)s->z, dt * 12.0f);
        return 0;
    }
    case ST_HANG: {
        navpath_t *p = &w->path[s->place];
        if (first) {
            path_point(p, s->x, &a->x, &a->y);
            a->zf = (float)p->z;
            a->nav = NAV_PERCH;
            a->facing = rng_chance(&w->rng, 0.5f) ? 1 : -1;
            a->anim = -1;
            set_anim(a, A_LAND);
            world_log(w, "%s#%u hangs under a branch", a->spr->name, a->id);
        }
        const clip_t *c = &a->spr->clips[a->clip];
        if (a->anim == A_LAND && a->ct >= clip_duration(c)) set_anim(a, A_HANG);
        if (a->step_t >= s->dur && a->anim != A_TAKEOFF) set_anim(a, A_TAKEOFF);
        if (a->anim == A_TAKEOFF && a->ct >= clip_duration(&a->spr->clips[a->clip])) {
            a->nav = NAV_AIR;
            a->y += 4.0f;
            a->path = -1;
            return 1;
        }
        return 0;
    }
    case ST_HOLLOW: {
        place_t *p = &w->place[s->place];
        int objz = p->obj >= 0 ? w->obj[p->obj].z : p->z + 4;
        if (first) {
            a->x = p->ex;
            a->y = p->ey;
            a->zf = (float)p->z;
            a->nav = NAV_PERCH;
            a->place = s->place;
            p->occupant = actor_index(w, a);
            set_anim(a, A_HOLLOW);
            a->bob = rng_range(&w->rng, 4.0f, 10.0f);     /* countdown until the head pokes out */
            world_log(w, "%s#%u in a hollow", a->spr->name, a->id);
        }
        a->bob -= dt;
        if (a->anim == A_HOLLOW && a->bob <= 0 && a->step_t < s->dur - 4.0f) {
            set_anim(a, A_HOLLOW_PEEK);
            a->zf = (float)(objz + 2);                  /* head in front of the interior edge, behind the rim */
            a->bob = rng_range(&w->rng, 2.5f, 5.0f);
        } else if (a->anim == A_HOLLOW_PEEK && a->bob <= 0) {
            set_anim(a, A_HOLLOW);
            a->zf = (float)p->z;
            a->bob = rng_range(&w->rng, 6.0f, 14.0f);
        }
        if (a->step_t >= s->dur) {
            p->occupant = -1;
            a->place = -1;
            a->nav = NAV_AIR;
            a->zf = (float)cruise_z(w);
            return 1;
        }
        return 0;
    }
    case ST_FLY: {
        set_anim(a, A_MOVE);
        a->nav = NAV_AIR;
        float dx = s->x - a->x, dy = s->y - a->y;
        float d = sqrtf(dx * dx + dy * dy);
        if (d < 1.0f || a->step_t > 25.0f) {
            if (s->place >= 0 && w->place[s->place].type == PL_PERCH) a->nav = NAV_PERCH;
            return 1;
        }
        float spd = (a->intent == IN_FLEE ? a->prof.run : a->prof.walk) * a->speed_mul;
        float st = fminf(d, spd * dt);
        a->bob += dt * 3.0f;
        a->x += dx / d * st;
        a->y += dy / d * st + sinf(a->bob) * 0.08f * spd * dt;
        if (fabsf(dx) > 0.5f) a->facing = dx < 0 ? -1 : 1;
        /* while flying keep a safe depth in front of trunks; drop to the target depth only close to the target */
        float cz = fmaxf((float)s->z, (float)cruise_z(w));
        a->zf = approach(a->zf, d < 14.0f ? (float)s->z : cz, dt * 14.0f);
        return 0;
    }
    case ST_ANIM:
        set_anim(a, s->anim);
        if (s->dur > 0) return a->step_t >= s->dur;
        return a->step_t >= clip_duration(&a->spr->clips[a->clip]);
    case ST_FACE:
        if (fabsf(s->x - a->x) > 0.5f) a->facing = s->x < a->x ? -1 : 1;
        return 1;
    case ST_DEPTH:
        a->zf = approach(a->zf, (float)s->z, dt * 30.0f / fmaxf(s->dur, 0.05f) * 0.2f + dt * 8.0f);
        return a->step_t >= s->dur && fabsf(a->zf - (float)s->z) < 0.5f;
    case ST_ENTER: {
        place_t *p = &w->place[s->place];
        if (first) {
            if (p->obj >= 0 && p->type == PL_ENTRY) {
                char clip[16];
                snprintf(clip, sizeof clip, "rustle%d", p->sub);
                world_play_object_clip(w, p->obj, clip);
            }
            world_stimulus(w, p->x, p->y, 60, ST_NOISE, 0.4f, 0.8f, actor_index(w, a));
        }
        /* going inside: the depth drops behind the leaves */
        set_anim(a, a->nav == NAV_CLIMB ? A_CLIMB : A_MOVE);
        a->zf = approach(a->zf, (float)p->z, dt * 14.0f);
        if (a->nav == NAV_GROUND) {
            a->x = approach(a->x, p->x + (float)a->facing * 3.0f, dt * a->prof.walk);
            a->y = world_ground_at(w, a->x, a->zf);
        }
        if (a->step_t > 0.7f && fabsf(a->zf - (float)p->z) < 0.5f) {
            a->hidden = 1;
            a->nav = NAV_HIDDEN;
            a->place = s->place;
            p->occupant = actor_index(w, a);
            world_log(w, "%s#%u hid", a->spr->name, a->id);
            return 1;
        }
        return 0;
    }
    case ST_EXIT: {
        place_t *p = &w->place[s->place];
        if (first) {
            a->hidden = 0;
            a->x = p->type == PL_HOLLOW ? p->climb_x : p->x;
            a->y = p->type == PL_HOLLOW ? p->y : world_ground_at(w, p->x, (float)p->z);
            a->zf = (float)p->z;
            a->nav = p->type == PL_HOLLOW ? NAV_CLIMB : NAV_GROUND;
            p->occupant = -1;
            a->place = -1;
            if (p->obj >= 0 && p->type == PL_ENTRY) {
                char clip[16];
                snprintf(clip, sizeof clip, "rustle%d", p->sub);
                world_play_object_clip(w, p->obj, clip);
            }
            world_stimulus(w, p->x, p->y, 60, ST_NOISE, 0.4f, 0.8f, actor_index(w, a));
        }
        set_anim(a, A_IDLE);
        return a->step_t > 0.4f;
    }
    case ST_PEEK: {
        place_t *p = &w->place[s->place];
        place_t *home = a->place >= 0 ? &w->place[a->place] : p;
        if (first) {
            a->hidden = 0;
            a->facing = rng_chance(&w->rng, 0.5f) ? 1 : -1;
            a->x = p->x - (float)(a->prof.head_dx * a->facing);
            a->y = p->y - 1.0f - (float)a->prof.head_dy;
            a->zf = (float)p->z;
            if (p->obj >= 0 && p->type == PL_PEEK) {
                char clip[16];
                snprintf(clip, sizeof clip, "open%d", p->sub);
                world_play_object_clip(w, p->obj, clip);
            }
            if (a->spr->nclips && sprite_clip_index(a->spr, "appear") >= 0) set_anim(a, A_APPEAR);
            world_log(w, "%s#%u peeks out", a->spr->name, a->id);
        }
        (void)home;
        float t = a->step_t;
        if (t < 0.6f) {
            if (a->anim != A_APPEAR) set_anim(a, A_IDLE);
        } else if (t < s->dur - 0.7f) {
            /* follows the nearest movement (a firefly, another creature) */
            int tg = nearest_mover(w, a, 160);
            if (tg >= 0 && (int)(t * 2.0f) % 3 == 0) a->facing = w->act[tg].x < a->x ? -1 : 1;
            set_anim(a, s->anim ? s->anim : ((int)(t / 2.5f) % 2 ? A_LOOK : A_IDLE));
        } else if (t < s->dur) {
            if (a->anim != A_VANISH) {
                if (sprite_clip_index(a->spr, "vanish") >= 0) set_anim(a, A_VANISH);
                if (p->obj >= 0 && p->type == PL_PEEK) {
                    char clip[16];
                    snprintf(clip, sizeof clip, "close%d", p->sub);
                    world_play_object_clip(w, p->obj, clip);
                }
            }
        } else {
            a->hidden = 1;
            return 1;
        }
        return 0;
    }
    case ST_DESPAWN:
        if (a->place >= 0) w->place[a->place].occupant = -1;
        for (int i = 0; i < w->nplace; i++)
            if (w->place[i].occupant == actor_index(w, a)) w->place[i].occupant = -1;
        world_log(w, "%s#%u left", a->spr->name, a->id);
        a->alive = 0;
        w->population--;
        return 1;
    }
    return 1;
}

void actor_set_intent(world_t *w, actor_t *a, int intent) { plan_intent(w, a, intent); }

void actor_update(world_t *w, actor_t *a, float dt)
{
    a->ct += dt;
    a->intent_age += dt;
    a->moved = 0;
    a->hunger = fminf(1.0f, a->hunger + dt * 0.01f);
    a->fatigue = fminf(1.0f, a->fatigue + dt * 0.006f);
    a->boredom = fminf(1.0f, a->boredom + dt * 0.008f);

    perceive(w, a, dt);

    /* a predator spreads danger, a firefly spreads light */
    if (!a->hidden) {
        if (a->prof.scares[0]) world_stimulus(w, a->x, a->y, 90, ST_DANGER, 1.0f, 0.4f, actor_index(w, a));
        if (sprite_has_tag(a->spr, "glow")) world_stimulus(w, a->x, a->y, 70, ST_GLOW, 0.5f, 0.4f, actor_index(w, a));
    }

    if (a->step >= a->nplan) choose_intent(w, a);
    if (!a->alive) return;
    if (a->step < a->nplan) {
        a->step_t += dt;
        if (exec_step(w, a, &a->plan[a->step], dt)) {
            a->step++;
            a->step_t = 0;
            a->step_init = 0;
        }
    }
    /* on the ground the screen row follows the depth (depth changes walk forward / back) */
    if (a->nav == NAV_GROUND && !a->hidden && a->path < 0)
        a->y = approach(a->y, world_ground_at(w, a->x, a->zf), dt * 30.0f);
}

/* ------------------------------------------------------------------ */
/* spawn                                                                */
/* ------------------------------------------------------------------ */

static actor_t *alloc_actor(world_t *w)
{
    for (int i = 0; i < w->nact; i++)
        if (!w->act[i].alive) return &w->act[i];
    if (w->nact >= MAX_ACT) return NULL;
    return &w->act[w->nact++];
}

int actor_spawn(world_t *w, const char *species, float x, float y, int nav, int lane_z)
{
    sprite_t *s = lib_find(w->lib, species);
    if (!s) return -1;
    actor_t *a = alloc_actor(w);
    if (!a) return -1;
    memset(a, 0, sizeof *a);
    a->alive = 1;
    a->spr = s;
    profile_parse(s, &a->prof);
    a->x = x; a->y = y;
    a->zf = (float)lane_z;
    a->lane_z = lane_z;
    a->nav = nav;
    if (nav == NAV_GROUND) a->y = world_ground_at(w, x, (float)lane_z);
    a->place = -1;
    a->target = -1;
    a->path = -1;
    a->facing = x < (float)w->w / 2 ? 1 : -1;
    a->speed_mul = rng_range(&w->rng, 0.85f, 1.15f);
    a->hunger = rng_range(&w->rng, 0.2f, 0.7f);
    a->fatigue = rng_range(&w->rng, 0.0f, 0.3f);
    a->id = w->next_id++;
    set_anim(a, A_IDLE);
    if (a->prof.burrow) {
        /* mole: immediately emerges from the ground */
        a->intent = IN_EMERGE;
        push_anim(a, A_APPEAR, -1.0f);
    }
    w->population++;
    world_log(w, "%s#%u arrives", s->name, a->id);
    return actor_index(w, a);
}

int actor_spawn_hidden(world_t *w, const char *species, int place)
{
    if (place < 0 || place >= w->nplace) return -1;
    place_t *p = &w->place[place];
    int i = actor_spawn(w, species, p->x, p->y, NAV_HIDDEN, w->ground_z + 4);
    if (i < 0) return -1;
    actor_t *a = &w->act[i];
    a->hidden = 1;
    a->place = place;
    a->zf = (float)p->z;
    p->occupant = i;
    return i;
}
