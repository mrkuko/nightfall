/* world.h — world: environment objects, interaction places, stimuli, creatures with intents
 *
 * Intent system:
 *   1. A creature has a PROFILE (from the .spr `creature` block): where it may move (move),
 *      speeds, mapping of logical animations to clips, intent weights, homes.
 *   2. The BRAIN picks an INTENT from the weights, drives (fear, hunger, fatigue,
 *      curiosity) and context (is a reachable home available? a trunk to climb?).
 *   3. An intent is expanded into a PLAN of steps: moving, climbing, animation,
 *      entering a hideout, peeking out, leaving …
 *   4. Stimuli can interrupt the plan: fox -> the rabbit freezes and runs into a bush.
 *   5. Steps physically affect the environment: a bush rustles / opens,
 *      grass bends in the direction of movement.
 */
#ifndef NF_WORLD_H
#define NF_WORLD_H

#include "sprite.h"
#include "raster.h"
#include "rng.h"

#define MAX_OBJ    160
#define MAX_ACT    32
#define MAX_PLACE  128
#define MAX_STIM   48
#define MAX_PLAN   16
#define MAX_PATH   64
#define MAX_LINK   128
#define PATH_PTS   40

/* --- environment ----------------------------------------------------- */

/* Floor patch types (the ground strip in front of the scene, coloured by the biome). */
enum { GT_SOIL, GT_LITTER, GT_NEEDLES, GT_MOSS, GT_GRASS, GT_STONE, GT_GRAVEL, GT_PEBBLES, GT_MUD, GT_SAND, GT_COUNT };
extern const char *const GROUND_NAMES[GT_COUNT];

typedef struct {
    sprite_t *spr;
    float  x, y;          /* anchor position in raster pixels */
    int    z;             /* base depth */
    int    flip;
    int    clip;          /* current clip */
    float  t;             /* time in the clip */
    int    oneshot;       /* a one-shot clip is playing; afterwards back to base */
    int    base_clip;     /* idle */
    int    sway_clip;     /* param clip for wind, -1 if none */
    float  phase, stiff;  /* individual phase and stiffness in the wind */
    int    bx0, bx1;      /* horizontal extent (for creature contact) */
    int    owned;         /* the sprite is an own procedural instance -> free it */
    float  bend, bvel;    /* bend spring (wind + creature contact) */
    float  push;          /* push from creatures passing through the plant this tick */
    int    is_static;     /* single frame, no wind -> drawn once into the baked layer */
} object_t;

/* navigation media */
enum { NAV_GROUND = 1, NAV_CLIMB = 2, NAV_AIR = 4, NAV_PERCH = 8, NAV_HIDDEN = 16 };

/* types of interaction places */
enum {
    PL_EDGE,        /* screen edge (arrival / departure) */
    PL_PEEK,        /* opening in a bush to peek out of */
    PL_ENTRY,       /* bottom entrance of a bush */
    PL_HOLLOW,      /* hollow in a trunk */
    PL_PERCH,       /* branch / top to sit on */
    PL_UNDER,       /* hideout under a log */
    PL_TRUNK,       /* trunk base (where climbing starts) */
    PL_SKY,         /* a point in the air */
    PL_COUNT
};

typedef struct {
    int   type;
    float x, y;       /* position (for entrances: where the creature stands) */
    int   z;          /* depth inside the hideout */
    int   nav;        /* medium through which the place is reachable */
    int   obj;        /* owner (bush, trunk …) or -1 */
    int   sub;        /* index of the opening in a bush (clips open%d / rustle%d) */
    int   occupant;   /* creature index or -1 */
    float climb_x;    /* for PL_TRUNK/PL_HOLLOW: x of the climbing lane */
    int   path;       /* climbing path from which the place is reachable, or -1 */
    int   has_eye;    /* hollow: centre of the opening where the eyes of the creature inside sit */
    float ex, ey;
    int   ew, eh;     /* size of the opening */
    float path_t;     /* position on that path (length from the start) */
} place_t;

/* Paths for creatures carried by the environment models (properties path.climbN / path.walkN):
 * climbing lanes along a trunk (bottom to top) and walkways along the top of a branch, log, boulder.
 * Paths that touch are linked (link) -> trunk -> branch -> back. */
enum { PATH_CLIMB, PATH_WALK, PATH_HANG };

typedef struct {
    int   kind;
    int   obj;
    int   z;
    int   n;
    float x[PATH_PTS], y[PATH_PTS];
    float len;
} navpath_t;

typedef struct {
    int   a, b;        /* path indices */
    float ta, tb;      /* position of the link on both paths */
} navlink_t;

enum { ST_DANGER, ST_NOISE, ST_GLOW };

typedef struct {
    float x, y, r;
    int   kind;
    float strength, ttl;
    int   src;        /* creature index or -1 */
} stim_t;

/* --- creatures ------------------------------------------------------- */

/* logical animations (mapped to clips via the profile "anim") */
enum {
    A_IDLE, A_MOVE, A_RUN, A_LOOK, A_ALERT, A_EAT, A_GROOM, A_REST, A_FREEZE, A_SNIFF,
    A_CLIMB, A_CLIMB_DOWN, A_APPEAR, A_VANISH, A_SIT,
    A_HANG, A_LAND, A_TAKEOFF, A_HOLLOW, A_HOLLOW_PEEK, A_CLING, A_PLAY, A_COUNT
};

/* intents */
enum {
    IN_NONE, IN_WANDER, IN_FORAGE, IN_LOOK, IN_GROOM, IN_REST, IN_HIDE, IN_PEEK, IN_LEAVE,
    IN_CLIMB, IN_PERCH, IN_FLEE, IN_FOLLOW, IN_WATCH, IN_PASS, IN_HOVER, IN_SNIFF, IN_EMERGE,
    IN_VANISH, IN_ROOST, IN_CHASE, IN_FROLIC, IN_COUNT
};

/* plan steps */
enum {
    ST_MOVE,        /* along the ground to x */
    ST_RUNTO,       /* along the ground to x, running */
    ST_CLIMB,       /* along a trunk to y */
    ST_FLY,         /* through the air to (x, y) */
    ST_ANIM,        /* animation in place for dur seconds */
    ST_FACE,        /* turn towards x */
    ST_ENTER,       /* enter a hideout (place) -> hidden */
    ST_EXIT,        /* leave a hideout (place) */
    ST_PEEK,        /* peek out of an opening (place) for dur seconds */
    ST_DEPTH,       /* smoothly change depth to z */
    ST_PATH,        /* move along a path (place = path index) to position x = t */
    ST_HANG,        /* hang upside down on a PATH_HANG path (place = path, x = t) for dur seconds */
    ST_HOLLOW,      /* sit in a hollow (place = place): the eyes glow, occasionally the head pokes out */
    ST_CHASE,       /* run after creature `place` (curiosity, not hunting) for at most dur seconds */
    ST_DESPAWN
};

typedef struct {
    int   kind;
    float x, y;
    int   z;
    int   place;
    int   anim;
    float dur;
} step_t;

typedef struct {
    float walk, run, climb;
    int   navmask;
    int   anim[A_COUNT];        /* clip index for a logical animation (the first variant) */
    int16_t avar[A_COUNT][6];   /* variants "groom=scratch|shake|play": one is picked at random */
    uint8_t nvar[A_COUNT];
    float iw[IN_COUNT];         /* intent weights */
    int   homes;                /* bit mask of place types (1 << PL_*) */
    float fear, curiosity;
    float lane;                 /* preferred ground row: 0 = standard lane (can climb), 1 = front row near the camera */
    int   rare;
    int   burrow;               /* emerges from the ground in place and vanishes again (mole) */
    char  scares[128];
    int   attracted_glow;
    int   head_dx, head_dy;     /* head point relative to the anchor (peeking) */
    float rest_min, rest_max;   /* how long a rest lasts (s) */
    int   pack;                 /* lives in a pack: looks for the others */
    char  chases[128];          /* " rabbit mouse " — runs after them out of curiosity */
} profile_t;

typedef struct {
    int        alive;
    sprite_t  *spr;
    profile_t  prof;
    float      x, y, zf;        /* zf: depth as a float for smooth transitions */
    int        facing;          /* +1 right, -1 left */
    int        nav;             /* current medium */
    int        hidden;          /* in a hideout (not drawn) */
    int        place;           /* place it is in (hideout) or -1 */
    int        clip;
    int        clip_after;      /* 1 + clip to play after the current transition clip (_in/_out), 0 = none */
    float      ct;
    int        anim;
    int        intent;
    float      intent_age;
    step_t     plan[MAX_PLAN];
    int        nplan, step;
    float      step_t;
    int        step_init;
    float      fear, hunger, fatigue, boredom;
    float      speed_mul;
    int        target;          /* tracked creature (follow / watch) */
    int        lane_z;          /* depth of the lane it moves in on the ground */
    float      moved;           /* displacement during the last tick (for grass contact) */
    float      bob;             /* phase for flying */
    int        path;            /* current path or -1 */
    float      path_t;
    uint32_t   id;
} actor_t;

typedef struct {
    int        w, h;            /* raster dimensions */
    float      ground[1024];    /* ground height for x (y of the feet) */
    int        ground_z;        /* base depth of the creature lane on the ground */
    float      ground_mix[GT_COUNT];  /* weights of the floor patch types (biome); all 0 = plain soil */
    float      ground_scale;    /* patch size multiplier (1 = default) */
    uint32_t   ground_seed;
    int        fg_z;            /* foreground depth */
    object_t   obj[MAX_OBJ];
    int        nobj;
    place_t    place[MAX_PLACE];
    int        nplace;
    actor_t    act[MAX_ACT];
    int        nact;
    navpath_t  path[MAX_PATH];
    int        npath;
    navlink_t  link[MAX_LINK];
    int        nlink;
    stim_t     stim[MAX_STIM];
    int        nstim;
    float      t, wind, gust;
    /* wind: the prevailing direction changes slowly, gust waves roll in the wind direction */
    float      wind_dir, wind_target, wind_timer, wind_str, wave_pos;
    rng_t      rng;
    library_t *lib;
    uint32_t   next_id;
    /* event log for the HUD */
    char       log[6][96];
    int        nlog;
    unsigned   nlog_total;      /* lines logged so far (for printing all of them) */
    /* baked static layer (background, ground, static objects) */
    raster_t   bake;
    int        bake_ok;
    /* director */
    float      next_spawn;
    int        population;
} world_t;

/* --- world --- */
void  world_init(world_t *w, library_t *lib, int width, int height, uint64_t seed);
void  world_free(world_t *w);
int   world_add_object(world_t *w, const char *sprite, float x, float y, int z, int flip, uint64_t seed);
int   world_add_place(world_t *w, int type, float x, float y, int z, int nav, int obj, int sub);
/* Adds interaction places from the object's properties (point.peek0, point.entry, point.hollow …). */
void  world_places_from_object(world_t *w, int obj);
void  world_play_object_clip(world_t *w, int obj, const char *clip);
float world_ground(const world_t *w, float x);
/* Ground y for a creature at depth z: closer rows (higher z) sit lower on the screen. */
float world_ground_at(const world_t *w, float x, float z);
/* Links paths that touch and assigns hollows to climbing paths. Call after the scene is built. */
void  world_link_paths(world_t *w);
void  path_point(const navpath_t *p, float t, float *x, float *y);
float path_nearest(const navpath_t *p, float x, float y, float *dist);
void  world_stimulus(world_t *w, float x, float y, float r, int kind, float strength, float ttl, int src);
void  world_log(world_t *w, const char *fmt, ...);
void  world_update(world_t *w, float dt);
/* Wind at x: sign = direction (+ right), magnitude ~0 … 1.3 */
float world_wind_at(const world_t *w, float x);
void  world_render(world_t *w, raster_t *r, int debug);

/* --- creatures --- */
int   actor_spawn(world_t *w, const char *species, float x, float y, int nav, int lane_z);
/* Spawns a creature directly in a hideout (bush, hollow) — it comes out according to its intent. */
int   actor_spawn_hidden(world_t *w, const char *species, int place);
void  actor_update(world_t *w, actor_t *a, float dt);
void  actor_set_intent(world_t *w, actor_t *a, int intent);
const char *intent_name(int intent);
void  profile_parse(const sprite_t *s, profile_t *p);
/* Picks a ground lane depth around the profile's preferred row (sometimes anywhere). */
int   actor_pick_lane(world_t *w, const profile_t *p);

#endif
