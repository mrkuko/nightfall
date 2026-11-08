/* gallery.c — interactive shape library browser + playground with intents */
#include "gallery.h"
#include "gen.h"
#include "raster.h"
#include "screen.h"
#include "term.h"
#include "world.h"
#include "noise.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const rgb_t UI_FG = {170, 186, 180};
static const rgb_t UI_DIM = {90, 104, 100};
static const rgb_t UI_HI = {240, 210, 120};
static const rgb_t UI_BG = {10, 14, 16};

/* ================================================================== */
/* gallery                                                             */
/* ================================================================== */

typedef struct {
    int idx, clip, scale, flip, depth, paused, occl, props, moving, bg;
    float t, mx;
    int step;
    uint64_t seed;
} gstate_t;

static void draw_occluders(raster_t *r, int cx, int gy, int z, float t)
{
    /* grass blades in front of and behind the shape: shows how per-pixel depth works */
    for (int i = -30; i <= 30; i += 3) {
        int x0 = cx + i;
        int h = 6 + (int)(hash2f(i, 3, 9u) * 14.0f);
        int zz = z + ((i / 3) % 3 == 0 ? 4 : (i / 3) % 3 == 1 ? -4 : 1);
        rgb_t c = zz > z ? mkrgb(70, 120, 76) : zz < z ? mkrgb(30, 60, 44) : mkrgb(50, 90, 60);
        float sway = sinf(t * 1.3f + (float)i * 0.2f) * 2.0f;
        for (int k = 0; k < h; k++) {
            float u = (float)k / (float)h;
            raster_plot(r, x0 + (int)(sway * u * u), gy - k, c, zz);
        }
    }
}

static void gallery_draw(screen_t *sc, raster_t *r, library_t *lib, gstate_t *g)
{
    sprite_t *s = lib->items[g->idx];
    if (g->clip >= s->nclips) g->clip = 0;
    const clip_t *c = &s->clips[g->clip];
    int fi;
    if (g->paused) {
        fi = c->seq[((g->step % c->n) + c->n) % c->n];
    } else if (c->mode == CLIP_PARAM) {
        float v = sinf(g->t * 0.9f) * 0.8f + sinf(g->t * 2.3f) * 0.2f;
        fi = clip_frame_param(c, v);
    } else {
        fi = clip_frame_at(c, g->t);
    }
    const frame_t *f = &s->frames[fi];

    /* background */
    for (int y = 0; y < r->h; y++)
        for (int x = 0; x < r->w; x++) {
            rgb_t col = UI_BG;
            if (g->bg == 1) col = ((x / 4 + y / 4) & 1) ? mkrgb(22, 26, 28) : mkrgb(14, 18, 20);
            else if (g->bg == 2) col = mkrgb(70, 74, 78);
            r->c[y * r->w + x] = col;
            r->z[y * r->w + x] = Z_EMPTY;
        }
    int sc_ = g->scale;
    int cx = r->w / 2;
    /* vertically: anchor at the bottom -> stand on the "ground" at 82 %; anchor at the top (hanging) -> 10 % */
    int gy;
    if (f->ay < f->h / 3) gy = r->h / 10 + f->ay * sc_;
    else gy = NF_MIN(r->h - 3, (r->h - f->h * sc_) / 2 + f->ay * sc_);
    if (gy < 2) gy = 2;
    int x = cx;
    if (g->moving && c->speed > 0) {
        x = (int)fmodf(g->mx, (float)(r->w + 80)) - 40;
        if (g->flip) x = r->w - x;
    }
    /* ground line */
    for (int xx = 0; xx < r->w; xx++) raster_plot(r, xx, gy + 1, mkrgb(30, 36, 34), -1000);
    raster_blit(r, f, x, gy, 100, (g->flip ? BLIT_FLIPX : 0) | BLIT_NOFOG, sc_, NULL);
    if (g->occl) draw_occluders(r, x, gy, 100, g->t);
    if (g->depth) raster_depth_view(r, 92, 108);

    screen_from_raster(sc, r, 2);

    /* text */
    char line[512];
    screen_fill(sc, 0, 0, sc->w, 2, UI_BG);
    screen_fill(sc, 0, sc->h - 1, sc->w, 1, UI_BG);
    snprintf(line, sizeof line, " [%d/%d] %s", g->idx + 1, lib->n, s->name);
    int w = screen_text(sc, 0, 0, line, UI_HI, &UI_BG);
    snprintf(line, sizeof line, "  %s  tags: %s", s->src, s->tags);
    screen_text(sc, w, 0, line, UI_DIM, &UI_BG);
    static const char *modes[] = { "loop", "once", "pingpong", "param" };
    char spd[24] = "";
    if (c->speed > 0) snprintf(spd, sizeof spd, " %.0fpx/s", (double)c->speed);
    snprintf(line, sizeof line, " clip %d/%d: %s (%s %.0ffps%s)  frame %s [%d/%d] %dx%d  zoom %d%s%s%s",
             g->clip + 1, s->nclips, c->name, modes[c->mode], (double)c->fps, spd, f->name, fi + 1,
             s->nframes, f->w, f->h, g->scale, g->depth ? "  [DEPTH]" : "", g->occl ? "  [OCCLUSION]" : "",
             g->paused ? "  [PAUSED]" : "");
    if (s->gen) {
        char sd[40];
        snprintf(sd, sizeof sd, "  seed %llu", (unsigned long long)s->seed);
        strncat(line, sd, sizeof line - strlen(line) - 1);
    }
    screen_text(sc, 0, 1, line, UI_FG, &UI_BG);
    screen_text(sc, 0, sc->h - 1,
                " ←→ shape  ↑↓ clip  space pause  ,. frame  z zoom  f mirror  d depth  o occlusion"
                "  m motion  b background  s seed  p profile  r reload  q quit",
                UI_DIM, &UI_BG);
    if (g->props) {
        int y = 3;
        for (int i = 0; i < s->nprops && y < sc->h - 2; i++, y++) {
            snprintf(line, sizeof line, " %-12s %s ", s->props[i].key, s->props[i].val);
            screen_text(sc, 1, y, line, UI_FG, &UI_BG);
        }
        snprintf(line, sizeof line, " clips:");
        screen_text(sc, 1, y++, line, UI_DIM, &UI_BG);
        for (int i = 0; i < s->nclips && y < sc->h - 2; i++, y++) {
            snprintf(line, sizeof line, "  %s%s (%d) ", i == g->clip ? "▶" : " ", s->clips[i].name, s->clips[i].n);
            screen_text(sc, 1, y, line, i == g->clip ? UI_HI : UI_FG, &UI_BG);
        }
    }
    if (lib->nerrors) {
        snprintf(line, sizeof line, " parser errors: %d — last: %s", lib->nerrors, lib->errors[lib->nerrors - 1]);
        screen_text(sc, 0, sc->h - 2, line, mkrgb(230, 110, 90), &UI_BG);
    }
}

int gallery_run(library_t *lib, const char *assets_dir, uint64_t seed)
{
    if (term_init() != 0) {
        fprintf(stderr, "nightfall: an interactive terminal is required\n");
        return 1;
    }
    int cols, rows;
    term_size(&cols, &rows);
    screen_t sc;
    raster_t r;
    screen_init(&sc, cols, rows);
    raster_init(&r, cols, (rows - 3) * 2);

    gstate_t g;
    memset(&g, 0, sizeof g);
    g.scale = 2;
    g.seed = seed;
    /* start on the first creature, if there is one */
    for (int i = 0; i < lib->n; i++)
        if (sprite_has_tag(lib->items[i], "creature") && !lib->items[i]->gen) { g.idx = i; break; }

    double last = term_now();
    while (!term_quit_requested()) {
        if (term_resized()) {
            term_size(&cols, &rows);
            screen_resize(&sc, cols, rows);
            raster_resize(&r, cols, (rows - 3) * 2);
        }
        int k;
        while ((k = term_key()) != KEY_NONE) {
            sprite_t *s = lib->items[g.idx];
            switch (k) {
            case 'q': case KEY_ESC: goto done;
            case KEY_RIGHT: g.idx = (g.idx + 1) % lib->n; g.clip = 0; g.t = 0; g.step = 0; break;
            case KEY_LEFT: g.idx = (g.idx + lib->n - 1) % lib->n; g.clip = 0; g.t = 0; g.step = 0; break;
            case KEY_DOWN: g.clip = (g.clip + 1) % s->nclips; g.t = 0; g.step = 0; break;
            case KEY_UP: g.clip = (g.clip + s->nclips - 1) % s->nclips; g.t = 0; g.step = 0; break;
            case ' ': g.paused = !g.paused; g.step = 0; break;
            case '.': g.paused = 1; g.step++; break;
            case ',': g.paused = 1; g.step--; break;
            case 'z': g.scale = g.scale % 4 + 1; break;
            case 'f': g.flip = !g.flip; break;
            case 'd': g.depth = !g.depth; break;
            case 'o': g.occl = !g.occl; break;
            case 'm': g.moving = !g.moving; g.mx = 0; break;
            case 'b': g.bg = (g.bg + 1) % 3; break;
            case 'p': g.props = !g.props; break;
            case 's':
                if (s->gen) sprite_regen(s, s->seed * 6364136223846793005ull + 1442695040888963407ull);
                break;
            case 'r': {
                char name[NF_NAME];
                snprintf(name, sizeof name, "%s", s->name);
                lib_free(lib);
                nf_load_library(lib, assets_dir, g.seed);
                g.idx = 0;
                for (int i = 0; i < lib->n; i++)
                    if (strcmp(lib->items[i]->name, name) == 0) g.idx = i;
                g.clip = 0;
                break;
            }
            default: break;
            }
        }
        double now = term_now();
        float dt = (float)(now - last);
        last = now;
        if (!g.paused) {
            g.t += dt;
            const sprite_t *s = lib->items[g.idx];
            if (g.clip < s->nclips) g.mx += dt * s->clips[g.clip].speed * (float)g.scale;
        }
        gallery_draw(&sc, &r, lib, &g);
        screen_flush(&sc);
        term_sleep(1.0 / 20.0 - (term_now() - now));
    }
done:
    screen_free(&sc);
    raster_free(&r);
    term_restore();
    return 0;
}

/* ================================================================== */
/* playground                                                          */
/* ================================================================== */

typedef struct { const char *name; float weight; int max; } species_t;

static const species_t SPECIES[] = {
    { "rabbit", 10, 3 }, { "squirrel", 6, 2 }, { "hedgehog", 4, 1 }, { "mouse", 6, 2 },
    { "frog", 3, 2 }, { "snail", 2, 2 }, { "beetle", 3, 3 }, { "firefly", 12, 6 },
    { "moth", 4, 2 }, { "owl", 2, 1 }, { "bat", 1.5f, 1 }, { "fox", 1.5f, 1 },
    { "eyes", 4, 2 }, { "spirit", 0.8f, 1 }, { "deer", 0.7f, 1 },
    { "badger", 2, 1 }, { "boar", 0.7f, 1 }, { "wildcat", 0.8f, 1 }, { "weasel", 2, 1 },
    { "dormouse", 3, 2 }, { "mole", 2, 1 }, { "salamander", 2, 1 }, { "slug", 2, 2 },
    { "stag_beetle", 2, 1 }, { "dhole", 0.8f, 3 },
};

static int count_species(const world_t *w, const char *name)
{
    int n = 0;
    for (int i = 0; i < w->nact; i++)
        if (w->act[i].alive && strcmp(w->act[i].spr->name, name) == 0) n++;
    return n;
}

static int random_place(world_t *w, int typemask, int free_only)
{
    int c[MAX_PLACE], n = 0;
    for (int i = 0; i < w->nplace; i++)
        if ((typemask & (1 << w->place[i].type)) && (!free_only || w->place[i].occupant < 0)) c[n++] = i;
    return n ? c[rng_int(&w->rng, n)] : -1;
}

/* Director: decides who arrives, when and from where. */
static int spawn_species(world_t *w, const char *name)
{
    sprite_t *s = lib_find(w->lib, name);
    if (!s) return -1;
    profile_t p;
    profile_parse(s, &p);
    int left = rng_chance(&w->rng, 0.5f);
    float ex = left ? -18.0f : (float)w->w + 18.0f;
    int lane = w->ground_z + rng_between(&w->rng, 0, 6);
    int i = -1;

    if ((p.navmask & NAV_HIDDEN) && !(p.navmask & NAV_GROUND)) {
        /* eyes: appear in a bush or in a hollow */
        int pl = random_place(w, (1 << PL_ENTRY) | (1 << PL_HOLLOW), 1);
        return pl >= 0 ? actor_spawn_hidden(w, name, pl) : -1;
    }
    if (p.burrow) {
        float x = rng_range(&w->rng, 20, (float)w->w - 20);
        return actor_spawn(w, name, x, world_ground(w, x), NAV_GROUND, lane);
    }
    if (sprite_has_tag(s, "large")) lane = w->ground_z - 4;   /* large animals a bit further away */
    if (p.navmask & NAV_AIR) {
        float y = rng_range(&w->rng, (float)w->h * 0.1f, (float)w->h * 0.6f);
        int z = w->ground_z + rng_between(&w->rng, 12, 26);   /* they fly in front of the trunks, not through them */
        /* the owl sometimes appears directly in a hollow (only the eyes glow) */
        if (sprite_clip_index(s, "in_hollow") >= 0 && rng_chance(&w->rng, 0.5f)) {
            for (int k = 0; k < w->nplace; k++) {
                place_t *pl = &w->place[k];
                if (pl->type != PL_HOLLOW || !pl->has_eye || pl->occupant >= 0) continue;
                i = actor_spawn(w, name, pl->ex, pl->ey, NAV_PERCH, z);
                if (i >= 0) actor_set_intent(w, &w->act[i], IN_ROOST);
                return i;
            }
        }
        i = actor_spawn(w, name, ex, y, NAV_AIR, z);
        return i;
    }
    /* ground creatures: from the edge, or they come out of a hideout */
    int homemask = p.homes & ((1 << PL_ENTRY) | (1 << PL_UNDER) | (p.navmask & NAV_CLIMB ? 1 << PL_HOLLOW : 0));
    if (homemask && rng_chance(&w->rng, 0.45f)) {
        int pl = random_place(w, homemask, 1);
        if (pl >= 0) {
            i = actor_spawn_hidden(w, name, pl);
            if (i >= 0) w->act[i].lane_z = lane;
            return i;
        }
    }
    i = actor_spawn(w, name, ex, world_ground(w, ex), NAV_GROUND, lane);
    /* pack (property "pack N"): the other members arrive right behind the leader */
    const char *pk = sprite_prop(s, "pack");
    if (i >= 0 && pk) {
        int extra = rng_between(&w->rng, 1, atoi(pk) - 1);
        for (int k = 0; k < extra; k++) {
            float off = (left ? -1.0f : 1.0f) * rng_range(&w->rng, 10, 26) * (float)(k + 1);
            int j = actor_spawn(w, name, ex + off, world_ground(w, ex + off), NAV_GROUND, lane + rng_between(&w->rng, -3, 3));
            if (j < 0) break;
        }
    }
    return i;
}

static void director(world_t *w, float dt)
{
    w->next_spawn -= dt;
    if (w->next_spawn > 0) return;
    w->next_spawn = rng_range(&w->rng, 2.5f, 8.0f);
    int cap = 5 + w->w / 40;
    if (w->population >= cap) return;
    float total = 0;
    for (size_t i = 0; i < NF_COUNT(SPECIES); i++)
        if (count_species(w, SPECIES[i].name) < SPECIES[i].max) total += SPECIES[i].weight;
    float r = rng_range(&w->rng, 0, total);
    for (size_t i = 0; i < NF_COUNT(SPECIES); i++) {
        if (count_species(w, SPECIES[i].name) >= SPECIES[i].max) continue;
        r -= SPECIES[i].weight;
        if (r <= 0) { spawn_species(w, SPECIES[i].name); break; }
    }
}

/* ------------------------------------------------------------------ */
/* scene composer: archetype + forest type + mirroring, all from the seed */
/* ------------------------------------------------------------------ */

enum { AR_LEFT, AR_RIGHT, AR_FRAME, AR_GROVE, AR_CLEARING, AR_THICKET, AR_COUNT };
enum { FT_DECID, FT_PINE, FT_BIRCH, FT_MIXED, FT_COUNT };

static const char *AR_NAMES[AR_COUNT] = { "giant left", "giant right", "trunk frame", "grove", "clearing", "thicket" };
static const char *FT_NAMES[FT_COUNT] = { "deciduous", "coniferous", "birch", "mixed" };
static char g_scene_desc[96];

static const char *pick_trunk(rng_t *r, int ft)
{
    float u = rng_float(r);
    switch (ft) {
    case FT_PINE:  return u < 0.45f ? "trunk_pine" : u < 0.85f ? "trunk_spruce" : "trunk_dead";
    case FT_BIRCH: return u < 0.6f ? "trunk_birch" : u < 0.8f ? "trunk_beech" : u < 0.9f ? "trunk" : "trunk_dead";
    case FT_DECID: return u < 0.3f ? "trunk" : u < 0.5f ? "trunk_beech" : u < 0.62f ? "trunk_old" : u < 0.72f ? "trunk_ivy"
                        : u < 0.87f ? "trunk_birch" : "trunk_dead";
    default: {
        static const char *all[] = { "trunk", "trunk_beech", "trunk_old", "trunk_ivy", "trunk_birch", "trunk_pine",
                                     "trunk_spruce", "trunk_dead" };
        return all[rng_int(r, 8)];
    }
    }
}

static const char *pick_stump(rng_t *r, int ft)
{
    float u = rng_float(r);
    switch (ft) {
    case FT_PINE:  return u < 0.4f ? "stump" : u < 0.65f ? "snag" : u < 0.85f ? "stump_hollow" : "rootplate";
    case FT_BIRCH: return u < 0.5f ? "stump_birch" : u < 0.75f ? "stump" : u < 0.9f ? "stump_hollow" : "snag";
    default:       return u < 0.25f ? "stump" : u < 0.45f ? "stump_mossy" : u < 0.62f ? "stump_hollow"
                        : u < 0.78f ? "snag" : u < 0.9f ? "rootplate" : "stump_birch";
    }
}

static const char *pick_log(rng_t *r, int ft)
{
    static const char *logs[] = { "log", "log_mossy", "log_hollow", "log_rotten" };
    if (ft == FT_BIRCH && rng_chance(r, 0.6f)) return "log_birch";
    return logs[rng_int(r, 4)];
}

/* Branches on a tree: branch type from the tree's mapping (property "branch"), pointing towards the
 * middle of the screen, spread over the height; extras by species (vine on deciduous, lichen on conifers). */
static void add_branches(world_t *w, rng_t *r, int t, float x, int z, int nmin, int nmax, int decorate)
{
    const sprite_t *ts = w->obj[t].spr;
    const char *bname = sprite_prop(ts, "branch");
    if (!bname || !lib_find(w->lib, bname)) bname = "branch";
    const char *wv = sprite_prop(ts, "width");
    float half = (wv ? (float)atoi(wv) : 20.0f) * 0.5f;
    int conifer = sprite_has_tag(ts, "pine") || sprite_has_tag(ts, "spruce");
    int dead = sprite_has_tag(ts, "dead");
    int toward = x < (float)w->w * 0.5f;              /* 1 = branches to the right */
    int nb = rng_between(r, nmin, nmax);
    float ys[8];
    int ny = 0;
    for (int i = 0; i < nb * 3 && ny < nb && ny < 8; i++) {
        float by = (float)w->h * rng_range(r, 0.06f, 0.55f);
        int ok = by < world_ground(w, x) - 22.0f;
        for (int k = 0; k < ny; k++) if (fabsf(ys[k] - by) < 9.0f) ok = 0;
        if (!ok) continue;
        ys[ny++] = by;
        /* near the screen edge only towards the middle; otherwise alternating to both sides */
        int side = (x < (float)w->w * 0.2f || x > (float)w->w * 0.8f) ? toward : (ny % 2 ? toward : !toward);
        float bx = x + (side ? half - 3 : -half + 3);
        int br = world_add_object(w, bname, bx, by, z - 2, !side, rng_next(r) | 1);
        if (br >= 0) world_places_from_object(w, br);
        if (!decorate) continue;
        float dir = side ? 1.0f : -1.0f;
        float u = rng_float(r);
        if (conifer || dead) {
            if (u < 0.45f) world_add_object(w, "hanging_moss", bx + dir * rng_range(r, 10, 26), by + 2, z - 4, 0, rng_next(r) | 1);
        } else {
            if (u < 0.3f) world_add_object(w, "vine", bx + dir * rng_range(r, 12, 30), by + 2, z - 4, 0, rng_next(r) | 1);
            else if (u < 0.45f) world_add_object(w, "hanging_moss", bx + dir * rng_range(r, 10, 28), by + 2, z - 4, 0, rng_next(r) | 1);
        }
        if (rng_chance(r, 0.3f)) world_add_object(w, "spider", bx + dir * rng_range(r, 6, 22), by + 3, z - 3, 0, 0);
        if (rng_chance(r, 0.25f)) world_add_object(w, "web", bx + (side ? 0 : -18), by + 5, z - 4, 0, rng_next(r) | 1);
    }
}

static void place_trunk_named(world_t *w, rng_t *r, float x, const char *name, int z)
{
    int t = world_add_object(w, name, x, world_ground(w, x) + 3, z, 0, rng_next(r) | 1);
    if (t < 0) return;
    world_places_from_object(w, t);
    const char *wv = sprite_prop(w->obj[t].spr, "width");
    float half = (wv ? (float)atoi(wv) : 20.0f) * 0.5f;
    int right = x < (float)w->w * 0.5f;
    world_add_object(w, "roots", x + (right ? half - 6 : -half + 6), world_ground(w, x) - 3, z + 3, !right, rng_next(r) | 1);
    /* a tree in the picture: 2–4 branches; a tree beyond the edge: 1–2 (they reach into the picture) */
    int inside = x > 0 && x < (float)w->w;
    add_branches(w, r, t, x, z, inside ? 2 : 1, inside ? 4 : 2, 1);
    if (rng_chance(r, 0.6f))
        world_add_object(w, rng_chance(r, 0.4f) ? "mushroom_glow" : "mushrooms", x + (right ? half + 2 : -half - 2),
                         world_ground(w, x) + 1, z + 14, 0, rng_next(r) | 1);
}

/* a tree in the middle ground (in fog): it also has its branches, fewer extras */
static void place_mid_tree(world_t *w, rng_t *r, float x, const char *name, int z)
{
    int t = world_add_object(w, name, x, world_ground(w, x) - rng_range(r, 1, 4), z, rng_chance(r, 0.5f), rng_next(r) | 1);
    if (t < 0) return;
    world_places_from_object(w, t);
    add_branches(w, r, t, x, z, 0, 2, 0);
}

static void place_main_trunk(world_t *w, rng_t *r, float x, int ft, int z)
{
    place_trunk_named(w, r, x, pick_trunk(r, ft), z);
}

static int too_close(const float *xs, int n, float x, float d)
{
    for (int i = 0; i < n; i++) if (fabsf(xs[i] - x) < d) return 1;
    return 0;
}

static void build_scene(world_t *w, uint64_t seed)
{
    rng_t r = rng_seed(seed ^ 0x5eedu);
    int W = w->w;
    int ar = rng_int(&r, AR_COUNT), ft = rng_int(&r, FT_COUNT);
    snprintf(g_scene_desc, sizeof g_scene_desc, "%s, %s forest", AR_NAMES[ar], FT_NAMES[ft]);
#define G(x) world_ground(w, (x))
#define SD (rng_next(&r) | 1)
#define FLIP rng_chance(&r, 0.5f)

    /* 1. deep background: a dense row of distant trunks in fog + rear bushes */
    for (float x = rng_range(&r, -8, 4); x < (float)W + 10; x += rng_range(&r, 7, 20))
        world_add_object(w, rng_chance(&r, 0.85f) ? "tree_far" : pick_trunk(&r, ft), x,
                         G(x) - rng_range(&r, 6, 14), rng_between(&r, 22, 48), FLIP, SD);
    for (float x = rng_range(&r, -10, 10); x < (float)W + 20; x += rng_range(&r, 26, 50))
        world_add_object(w, rng_chance(&r, 0.5f) ? "bush_wide" : "bush", x, G(x) - rng_range(&r, 3, 7),
                         rng_between(&r, 50, 58), FLIP, SD);

    /* 2. main trunks by archetype */
    float trunks[8];
    int nt = 0;
    switch (ar) {
    case AR_LEFT:  trunks[nt++] = (float)W * rng_range(&r, 0.04f, 0.14f); break;
    case AR_RIGHT: trunks[nt++] = (float)W * rng_range(&r, 0.86f, 0.96f); break;
    case AR_FRAME: trunks[nt++] = (float)W * rng_range(&r, 0.02f, 0.1f);
                   trunks[nt++] = (float)W * rng_range(&r, 0.9f, 0.98f); break;
    case AR_GROVE: {
        int n = 3 + (W > 160);
        for (int i = 0; i < n; i++)
            trunks[nt++] = (float)W * ((float)i + rng_range(&r, 0.25f, 0.75f)) / (float)n;
        break;
    }
    case AR_CLEARING: if (rng_chance(&r, 0.6f)) trunks[nt++] = rng_chance(&r, 0.5f) ? -4.0f : (float)W + 4.0f; break;
    case AR_THICKET: trunks[nt++] = (float)W * rng_range(&r, 0.3f, 0.7f); break;
    }

    /* 3. middle ground: more trunks in half fog, away from the main ones */
    float mids[16];
    int nm = 0;
    int want = ar == AR_CLEARING ? 1 + W / 70 : 2 + W / 45;
    for (int tries = 0; tries < 40 && nm < want && nm < 16; tries++) {
        float x = rng_range(&r, 0, (float)W);
        if (too_close(trunks, nt, x, 26) || too_close(mids, nm, x, 16)) continue;
        mids[nm++] = x;
        place_mid_tree(w, &r, x, pick_trunk(&r, ft), rng_between(&r, 62, 76));
    }
    for (int i = 0; i < nt; i++) place_main_trunk(w, &r, trunks[i], ft, rng_between(&r, 88, 92));

    /* 4. overhead: canopies (no sky), moonbeams */
    float cstep = ar == AR_CLEARING ? rng_range(&r, 70, 110) : rng_range(&r, 35, 60);
    for (float x = rng_range(&r, -20, 20); x < (float)W + 20; x += cstep * rng_range(&r, 0.8f, 1.3f))
        world_add_object(w, "canopy", x, -rng_range(&r, 1, 6), rng_between(&r, 66, 112), FLIP, SD);
    int nbeam = ar == AR_CLEARING ? rng_between(&r, 1, 2) : rng_chance(&r, 0.55f);
    for (int i = 0; i < nbeam; i++)
        world_add_object(w, "moonbeam", rng_range(&r, (float)W * 0.2f, (float)W * 0.95f), 0, 140, 0, SD);

    /* 5. bushes with hideouts */
    int nbush = ar == AR_THICKET ? 4 + W / 80 : ar == AR_CLEARING ? 1 + W / 120 : 2 + W / 110;
    float bx[16];
    int nbx = 0;
    static const char *bushes[] = { "bush", "bush_wide", "bush", "bush_small" };
    for (int tries = 0; tries < 60 && nbx < nbush && nbx < 16; tries++) {
        float x = rng_range(&r, 10, (float)W - 10);
        if (too_close(trunks, nt, x, 18) || too_close(bx, nbx, x, ar == AR_THICKET ? 22 : 34)) continue;
        bx[nbx++] = x;
        int b = world_add_object(w, bushes[rng_int(&r, 4)], x, G(x) + 1, rng_between(&r, 88, 91), FLIP, SD);
        if (b >= 0) world_places_from_object(w, b);
    }

    /* 6. middle layer: saplings, stumps, boulders, fallen wood, brushwood */
    int nsap = ar == AR_THICKET ? 3 + W / 60 : rng_between(&r, 0, 1 + W / 100);
    for (int i = 0; i < nsap; i++) {
        float x = rng_range(&r, 0, (float)W);
        world_add_object(w, "sapling", x, G(x) + 1, rng_between(&r, 86, 94), FLIP, SD);
    }
    int nstump = ar == AR_CLEARING ? rng_between(&r, 1, 3) : rng_between(&r, 0, 1);
    for (int i = 0; i < nstump; i++) {
        float x = rng_range(&r, 12, (float)W - 12);
        int o = world_add_object(w, pick_stump(&r, ft), x, G(x) + 2, rng_between(&r, 94, 98), FLIP, SD);
        if (o >= 0) world_places_from_object(w, o);
    }
    int nboulder = ar == AR_CLEARING ? rng_between(&r, 1, 2) : rng_between(&r, 0, 1);
    for (int i = 0; i < nboulder; i++) {
        float x = rng_range(&r, 12, (float)W - 12);
        int o = world_add_object(w, "boulder", x, G(x) + 3, rng_between(&r, 94, 99), FLIP, SD);
        if (o >= 0) world_places_from_object(w, o);
    }
    int nlog = ar == AR_CLEARING ? rng_between(&r, 1, 2) : rng_between(&r, 0, 1 + (W > 140));
    for (int i = 0; i < nlog; i++) {
        float x = rng_range(&r, (float)W * 0.15f, (float)W * 0.9f);
        int o = world_add_object(w, pick_log(&r, ft), x, G(x) + 1, rng_between(&r, 95, 99), FLIP, SD);
        if (o >= 0) world_places_from_object(w, o);
    }
    if (rng_chance(&r, ar == AR_THICKET ? 0.9f : 0.5f)) {
        float x = rng_range(&r, 20, (float)W - 20);
        int o = world_add_object(w, "brushwood", x, G(x) + 1, 95, FLIP, SD);
        if (o >= 0) world_places_from_object(w, o);
    }

    /* 7. ground */
    for (int i = 0; i < W / 26; i++) {
        float x = rng_range(&r, 0, (float)W);
        const char *n = ft == FT_PINE ? (rng_chance(&r, 0.6f) ? "cones" : "litter") : (rng_chance(&r, 0.8f) ? "litter" : "cones");
        world_add_object(w, n, x, G(x) + 3, 98, 0, SD);
    }
    for (int i = 0; i < 1 + W / 60; i++) {
        float x = rng_range(&r, 0, (float)W);
        world_add_object(w, "moss_patch", x, G(x) + 3, 99, 0, SD);
    }
    if (rng_chance(&r, ar == AR_CLEARING ? 0.8f : 0.35f)) {
        float x = rng_range(&r, (float)W * 0.2f, (float)W * 0.9f);
        world_add_object(w, "puddle", x, G(x) + 5, 99, 0, SD);
    }
    for (int i = 0; i < rng_between(&r, 1, 4); i++) {
        float x = rng_range(&r, 0, (float)W);
        float u = rng_float(&r);
        world_add_object(w, u < 0.2f ? "mushroom_red" : u < 0.35f ? "mushroom_glow" : "mushrooms", x, G(x) + 2, 106, 0, SD);
    }
    for (int i = 0; i < 1 + W / 70; i++) {
        float x = rng_range(&r, 0, (float)W);
        world_add_object(w, "stone", x, G(x) + 4, 108, 0, SD);
    }

    /* 8. foreground in three depth bands (not everything in one layer), with wider spacing:
     *    far and low (just in front of the creatures) -> middle -> near, big and sparse (closest to the camera) */
    static const char *fg_decid[] = { "grass", "grass_tall", "fern", "broadleaf", "grass_low", "sprouts", "nettle", "flowers", "fern" };
    static const char *fg_pine[] = { "grass_low", "fern", "fern", "horsetail", "grass", "sprouts", "grass_tall" };
    static const char *fg_near[] = { "fern", "broadleaf", "grass_tall", "nettle", "fern", "horsetail" };
    static const char *fg_far[]  = { "grass_low", "sprouts", "flowers", "grass_low", "grass" };
    const char *const *fg = ft == FT_PINE ? fg_pine : fg_decid;
    int nfg = ft == FT_PINE ? (int)NF_COUNT(fg_pine) : (int)NF_COUNT(fg_decid);
    float step0 = (ar == AR_THICKET ? 5.0f : ar == AR_CLEARING ? 11.0f : 7.0f) * 1.45f;
    /* far band: low grass and small flowers, slightly in front of the creatures */
    for (float x = rng_range(&r, -4, 8); x < (float)W + 10; x += rng_range(&r, step0 * 1.2f, step0 * 2.6f))
        world_add_object(w, fg_far[rng_int(&r, (int)NF_COUNT(fg_far))], x, G(x) + rng_range(&r, 2, 6),
                         rng_between(&r, 108, 116), FLIP, SD);
    /* middle band: the main undergrowth */
    for (float x = rng_range(&r, -6, 6); x < (float)W + 10; x += rng_range(&r, step0, step0 * 2.2f))
        world_add_object(w, fg[rng_int(&r, nfg)], x, G(x) + rng_range(&r, 4, 11), w->fg_z + rng_between(&r, -8, 4), FLIP, SD);
    /* near band: big plants, sparse, closest to the camera and lowest on the screen */
    for (float x = rng_range(&r, -10, 20); x < (float)W + 12; x += rng_range(&r, step0 * 2.6f, step0 * 4.8f))
        world_add_object(w, fg_near[rng_int(&r, (int)NF_COUNT(fg_near))], x, G(x) + rng_range(&r, 9, 15),
                         w->fg_z + rng_between(&r, 12, 24), FLIP, SD);
    for (float x = rng_range(&r, 0, 10); x < (float)W; x += rng_range(&r, 12, 30))
        world_add_object(w, rng_chance(&r, 0.5f) ? "grass" : "grass_low", x, G(x) + 1, 98, FLIP, SD);

    /* 9. ground mist */
    for (float x = rng_range(&r, 0, 30); x < (float)W + 40; x += rng_range(&r, 45, 90))
        world_add_object(w, "mist", x, G(x) + 2, 115, 0, SD);

    world_link_paths(w);
#undef G
#undef SD
#undef FLIP
}

/* Screensaver: the same scene as the playground (build_scene), without HUD and controls,
 * any key quits. After scene_time seconds the scene is replaced by a new one (different seed). */
int saver_run(library_t *lib, uint64_t seed, float scene_time)
{
    if (term_init() != 0) {
        fprintf(stderr, "nightfall: an interactive terminal is required\n");
        return 1;
    }
    int cols, rows;
    term_size(&cols, &rows);
    screen_t sc;
    raster_t r;
    screen_init(&sc, cols, rows);
    raster_init(&r, cols, rows * 2);
    r.fog = mkrgb(6, 12, 14);
    r.fog_near = 100;
    r.fog_far = 40;
    r.fog_max = 0.8f;
    world_t *w = malloc(sizeof *w);
    if (!w) return 1;
    world_init(w, lib, cols, rows * 2, seed);
    build_scene(w, seed);
    for (int i = 0; i < 3; i++) director(w, 100.0f);
    float scene_age = 0;
    double last = term_now();
    while (!term_quit_requested()) {
        int rebuild = 0;
        if (term_resized()) {
            term_size(&cols, &rows);
            screen_resize(&sc, cols, rows);
            raster_resize(&r, cols, rows * 2);
            rebuild = 1;
        }
        if (scene_time > 0 && scene_age > scene_time) {
            seed = seed * 6364136223846793005ull + 1442695040888963407ull;
            rebuild = 1;
            sc.force = 1;
        }
        if (rebuild) {
            world_free(w);
            world_init(w, lib, cols, rows * 2, seed);
            build_scene(w, seed);
            for (int i = 0; i < 3; i++) director(w, 100.0f);
            scene_age = 0;
        }
        if (term_key() != KEY_NONE) break;
        double now = term_now();
        float dt = fminf(0.1f, (float)(now - last));
        last = now;
        scene_age += dt;
        director(w, dt);
        world_update(w, dt);
        world_render(w, &r, 0);
        screen_from_raster(&sc, &r, 0);
        screen_flush(&sc);
        term_sleep(1.0 / 12.0 - (term_now() - now));
    }
    world_free(w);
    free(w);
    screen_free(&sc);
    raster_free(&r);
    term_restore();
    return 0;
}

/* Without a terminal: simulates T seconds and saves a world snapshot (testing, debugging). */
int play_snapshot(library_t *lib, uint64_t seed, int cols, int rows, float seconds, const char *ppm,
                  const char *const *spawn, int nspawn)
{
    world_t *w = malloc(sizeof *w);
    if (!w) return 1;
    raster_t r;
    raster_init(&r, cols, rows * 2);
    r.fog = mkrgb(6, 12, 14);
    r.fog_near = 100;
    r.fog_far = 40;
    r.fog_max = 0.8f;
    world_init(w, lib, cols, rows * 2, seed);
    build_scene(w, seed);
    for (int i = 0; i < 3; i++) director(w, 100.0f);
    for (int i = 0; i < nspawn; i++) spawn_species(w, spawn[i]);
    int logged = 0;
    for (float t = 0; t < seconds; t += 1.0f / 15.0f) {
        director(w, 1.0f / 15.0f);
        world_update(w, 1.0f / 15.0f);
        if (getenv("NF_WIND") && fmodf(w->t, 1.0f) < 1.0f / 15.0f) {
            /* wind debugging: row = 1 s, column = 4 px; character = strength along the wind direction */
            char row[300];
            int n = 0;
            for (int x = 0; x < w->w && n < 290; x += 4) {
                float v = world_wind_at(w, (float)x);
                const char *ramp = " .:-=+*#%@";
                row[n++] = ramp[clampi((int)(fabsf(v) * 8.0f), 0, 9)];
            }
            row[n] = 0;
            printf("%5.0fs smer %+.2f sila %.2f |%s|\n", (double)w->t, (double)w->wind_dir, (double)w->wind_str, row);
            continue;
        }
        if (w->nlog && strcmp(w->log[w->nlog - 1], "") != 0 && logged != (int)(w->t * 15)) {
            /* print new log lines */
            static char lastline[96];
            if (strcmp(lastline, w->log[w->nlog - 1]) != 0) {
                printf("%s\n", w->log[w->nlog - 1]);
                snprintf(lastline, sizeof lastline, "%s", w->log[w->nlog - 1]);
            }
            logged = (int)(w->t * 15);
        }
    }
    if (getenv("NF_BENCH")) {
        /* performance: render + conversion to cells + diff, without writing to the terminal */
        screen_t sc;
        screen_init(&sc, cols, rows);
        int frames = 300;
        double t0 = term_now();
        long cells = 0;
        for (int i = 0; i < frames; i++) {
            director(w, 1.0f / 15.0f);
            world_update(w, 1.0f / 15.0f);
            world_render(w, &r, 0);
            screen_from_raster(&sc, &r, 0);
            for (int k = 0; k < sc.w * sc.h; k++)
                if (memcmp(&sc.cur[k], &sc.prev[k], sizeof sc.cur[k])) { cells++; sc.prev[k] = sc.cur[k]; }
        }
        double ms = (term_now() - t0) * 1000.0 / frames;
        printf("bench: %dx%d cells, %.3f ms/frame (%.1f %% of one core at 15 fps), changed cells ~%ld/frame\n",
               cols, rows, ms, ms * 15.0 / 10.0, cells / frames);
        screen_free(&sc);
    }
    world_render(w, &r, getenv("NF_DEBUG") != NULL);
    int rc = ppm ? raster_save_ppm(&r, ppm, 4) : 0;
    printf("scene: %s | objects %d, places %d, paths %d, links %d, creatures %d\n", g_scene_desc,
           w->nobj, w->nplace, w->npath, w->nlink, w->population);
    for (int i = 0; i < w->nact; i++)
        if (w->act[i].alive)
            printf("  %-10s#%u %-7s x=%.0f y=%.0f z=%.0f %s%s\n", w->act[i].spr->name, w->act[i].id,
                   intent_name(w->act[i].intent), (double)w->act[i].x, (double)w->act[i].y,
                   (double)w->act[i].zf, w->act[i].hidden ? "hidden" : "",
                   w->act[i].path >= 0 ? (w->path[w->act[i].path].kind == PATH_WALK ? " [on a branch/trunk]" :
                                          w->path[w->act[i].path].kind == PATH_HANG ? " [hanging]" : " [climbing]") : "");
    world_free(w);
    free(w);
    raster_free(&r);
    return rc;
}

int play_run(library_t *lib, const char *assets_dir, uint64_t seed)
{
    (void)assets_dir;
    if (term_init() != 0) {
        fprintf(stderr, "nightfall: an interactive terminal is required\n");
        return 1;
    }
    int cols, rows;
    term_size(&cols, &rows);
    screen_t sc;
    raster_t r;
    screen_init(&sc, cols, rows);
    raster_init(&r, cols, rows * 2);
    r.fog = mkrgb(6, 12, 14);
    r.fog_near = 100;
    r.fog_far = 40;
    r.fog_max = 0.8f;

    world_t *w = malloc(sizeof *w);
    if (!w) return 1;
    world_init(w, lib, cols, rows * 2, seed);
    build_scene(w, seed);
    for (int i = 0; i < 3; i++) director(w, 100.0f);

    int hud = 1, debug = 0, paused = 0;
    float speed = 1.0f;
    double last = term_now();
    while (!term_quit_requested()) {
        if (term_resized()) {
            term_size(&cols, &rows);
            screen_resize(&sc, cols, rows);
            raster_resize(&r, cols, rows * 2);
            world_free(w);
            world_init(w, lib, cols, rows * 2, seed);
            build_scene(w, seed);
        }
        int k;
        while ((k = term_key()) != KEY_NONE) {
            switch (k) {
            case 'q': case KEY_ESC: goto done;
            case 'h': hud = !hud; break;
            case 'd': debug = !debug; break;
            case ' ': paused = !paused; break;
            case '+': case '=': speed = fminf(8.0f, speed * 2.0f); break;
            case '-': speed = fmaxf(0.25f, speed * 0.5f); break;
            case 'n': w->next_spawn = 0; director(w, 0); break;
            case 'l':
                seed = seed * 6364136223846793005ull + 1442695040888963407ull;
                world_free(w);
                world_init(w, lib, cols, rows * 2, seed);
                build_scene(w, seed);
                for (int i = 0; i < 3; i++) director(w, 100.0f);
                break;
            case 'x': spawn_species(w, "fox"); break;
            case 'e': spawn_species(w, "eyes"); break;
            case 'r': spawn_species(w, "rabbit"); break;
            case 's': spawn_species(w, "squirrel"); break;
            case 'o': spawn_species(w, "owl"); break;
            case 'v': spawn_species(w, "bat"); break;
            case 'p': spawn_species(w, "dhole"); break;
            case 'g': spawn_species(w, "spirit"); break;
            case 'j': spawn_species(w, "deer"); break;
            case 'b': spawn_species(w, "boar"); break;
            case 'c': spawn_species(w, "wildcat"); break;
            case 'm': spawn_species(w, "mole"); break;
            case 'w': spawn_species(w, "weasel"); break;
            default: break;
            }
        }
        double now = term_now();
        float dt = fminf(0.1f, (float)(now - last));
        last = now;
        if (!paused) {
            float sdt = dt * speed;
            director(w, sdt);
            world_update(w, sdt);
        }
        world_render(w, &r, debug);
        screen_from_raster(&sc, &r, 0);
        if (hud) {
            char line[512];
            snprintf(line, sizeof line, " NIGHTFALL playground  seed %llu (%s)  creatures %d  %.2gx%s   "
                     "q quit  l new scene  h hud  d places/paths  space pause  +/- time  n spawn  r s x o v e g j b c m w p = rabbit squirrel fox owl bat eyes spirit deer boar cat mole weasel dhole ",
                     (unsigned long long)seed, g_scene_desc, w->population, (double)speed, paused ? " PAUSED" : "");
            screen_text(&sc, 0, 0, line, UI_DIM, &UI_BG);
            int y = 1;
            for (int i = 0; i < w->nact && y < sc.h - 8; i++) {
                actor_t *a = &w->act[i];
                if (!a->alive) continue;
                snprintf(line, sizeof line, " %-9s#%-3u %-7s %s fear %.1f %s", a->spr->name, a->id,
                         intent_name(a->intent), a->hidden ? "(hidden)" : "        ", (double)a->fear,
                         a->step < a->nplan ? "" : "");
                screen_text(&sc, 0, y++, line, a->fear > 0.5f ? UI_HI : UI_FG, NULL);
            }
            for (int i = 0; i < w->nlog; i++) {
                snprintf(line, sizeof line, " %s ", w->log[i]);
                screen_text(&sc, 0, sc.h - w->nlog + i, line, UI_DIM, NULL);
            }
        }
        screen_flush(&sc);
        term_sleep(1.0 / 15.0 - (term_now() - now));
    }
done:
    world_free(w);
    free(w);
    screen_free(&sc);
    raster_free(&r);
    term_restore();
    return 0;
}
