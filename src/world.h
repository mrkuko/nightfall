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

/* widest scene in art pixels (terminal: columns; Windows screensaver: the whole desktop) */
#define NF_MAX_W   2048

/* Objects, places, paths and links grow with the scene (a 4K desktop has several times more than a
 * terminal), so they live on the heap and grow as needed (world_free releases them). Creatures and
 * stimuli stay fixed arrays (the code holds pointers to creatures all the time, so they must not move);
 * their caps are generous: a 4K scene has ~30 creatures. A full array silently refuses a new one. */
#define MAX_ACT    128
#define MAX_STIM   256
#define MAX_PLAN   16
#define PATH_PTS   40

/* --- environment ----------------------------------------------------- */

/* Floor patch types (the ground strip in front of the scene, coloured by the biome). */
enum { GT_SOIL, GT_LITTER, GT_NEEDLES, GT_MOSS, GT_GRASS, GT_STONE, GT_GRAVEL, GT_PEBBLES, GT_MUD, GT_SAND, GT_COUNT };
extern const char *const GROUND_NAMES[GT_COUNT];

/* One gust of wind: a wave of strength `amp` (< 0 = blowing to the left) that travels `left` more pixels. */
#define MAX_GUST 8
typedef struct { float pos, v, width, amp, left, total; } gust_t;

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
    PL_SHELTER,     /* on the ground under big leaves (butterbur): a dry spot in the rain */
    PL_SETT,        /* the mouth of a burrow in the ground (badger): dived into nose first; sub = +1/-1 the way the tunnel goes */
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
/* `src` of a stimulus with no creature behind it that every creature still senses (the ghost walk): fog does not
 * hide it, each one reacts by its own `fear` (fear 0 = not at all) */
#define SRC_UNSEEN (-2)

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
    A_HANG, A_LAND, A_TAKEOFF, A_HOLLOW, A_HOLLOW_PEEK, A_CLING, A_PLAY,
    A_SWOOP, A_STRIKE, A_CARRY, A_COUNT
};

/* intents */
enum {
    IN_NONE, IN_WANDER, IN_FORAGE, IN_LOOK, IN_GROOM, IN_REST, IN_HIDE, IN_PEEK, IN_LEAVE,
    IN_CLIMB, IN_PERCH, IN_FLEE, IN_FOLLOW, IN_WATCH, IN_PASS, IN_HOVER, IN_SNIFF, IN_EMERGE,
    IN_VANISH, IN_ROOST, IN_CHASE, IN_FROLIC, IN_HUNT, IN_SHELTER, IN_WALLOW, IN_DIG, IN_COUNT
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
    ST_TUNNEL,      /* (mole) underground to x, then surfaces with a new molehill */
    ST_HUNT,        /* (owl) stoop on creature `place` from above; z = 1: it will catch it, 0: it misses */
    ST_SHELTER,     /* wait under big leaves (place) until the rain stops */
    ST_BOUND,       /* long leaps along arcs to x (frames bound0..3 by the leap phase), depth to z */
    ST_ROOT,        /* (boar) plough along the ground to x with the snout, turned earth every y px, depth z */
    ST_PURSUE,      /* (event) run after creature `place` (id `anim`) x px behind it, z deeper/closer; when it is gone, runs on off the screen */
    ST_DIG,         /* (badger) digs a new sett with its mouth at x, tunnel going the way of `z` (+1/-1), for dur seconds */
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
    char  hunts[128];           /* " mouse " — a bird of prey stoops on them (and may catch them) */
    int   shelter;              /* "shelter rain": goes under big leaves when it rains */
    int   roots;                /* rooting leaves turned-earth patches (boar) */
    float aura;                 /* "aura R": its presence alone frightens the `scares` creatures within R px */
    float wary;                 /* grazing, it lifts its head now and then to listen (and may bolt); 0 = never */
    float bound_len, bound_h;   /* "bound LEN H": it bolts in leaps this long and high (px) */
    int   bound_f[4];           /* frames bound0..bound3 (gather, launch, flight, landing); -1 = no leaps */
    int   digs;                 /* "digs sett": digs its own burrows (intent `dig`) */
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
    int        clip_after;
    int        mask_place;      /* 1 + hollow place whose opening masks the creature, 0 = none */
    int        mask_floor;      /* pixels at y >= this are not masked (climbing in / out); INT16 max = all masked */      /* 1 + clip to play after the current transition clip (_in/_out), 0 = none */
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
    int        carry;           /* carries its catch in the talons (clip "carry") */
    int        hphase;          /* ST_HUNT: 0 approach, 1 stoop, 2 strike, 3 pull up */
    float      hx, hy;          /* ST_HUNT: the point it aims at */
    int        hold;            /* an event keeps it in place (it only bobs) */
    int        scripted;        /* an event drives it: no perception or own choices until its plan ends */
    int        ov_frame;        /* an event picks its frame: 1 + frame, 0 = the clip decides */
    uint32_t   id;
} actor_t;

/* --- weather ---------------------------------------------------------
 * Layers (fog, rain) that live side by side: each has a strength and a front that travels across the
 * screen with the wind. Arriving, the leading edge sweeps in from one side; leaving, the trailing edge
 * follows it out, so fog can roll out while rain sweeps in. A mix such as "12 % fog, 88 % rain" is
 * simply two layers at different strengths. */
enum { WX_FOG, WX_RAIN, WX_COUNT };
enum { WXS_NONE, WXS_ARRIVE, WXS_ON, WXS_LEAVE };

typedef struct {
    int   state;                /* WXS_* */
    float amount, target;       /* strength 0 … 1; it eases towards target (a change without a front) */
    float lead, tail;           /* front edges in travel coordinates: 0 = the side it comes from, 1 = the other */
    float dir;                  /* +1 travels to the right, -1 to the left */
    float speed;                /* screen widths per second */
    float soft;                 /* width of the soft edge (screen widths) */
} wx_layer_t;

#define MAX_DROP   700
#define MAX_SPLASH 96
typedef struct { float x, y, z, vx, vy, len; float floor_y; } wx_drop_t;
typedef struct { float x, y; int z; float age, life; int ring; } wx_splash_t;

typedef struct {
    wx_layer_t layer[WX_COUNT];
    int        pattern;         /* current pattern (weather_pattern_name) */
    int        fixed;           /* --weather NAME: it stays */
    float      next_change;     /* seconds to the next pattern */
    float      bias[WX_COUNT];  /* biome: how likely fog / rain is (1 = normal) */
    float      spawn_acc;       /* fractional raindrops to spawn */
    float      drift;           /* how far the fog wisps have crept with the wind (integrated, so wind changes never make them jump) */
    float      wet;             /* how wet the leaves are (they drip for a while after the rain) */
    /* storm: lightning (mostly distant sheet flashes, now and then a strike into the scene) */
    float      lt_next;         /* seconds to the next flash */
    float      lt_t;            /* seconds since the current flash started (< 0: none) */
    float      lt_int;          /* its strength (a calm screensaver: dim) */
    float      lt_cx, lt_reach; /* where it glows (0..1 across) and how high up the sky it reaches (0..1) */
    int        lt_np;           /* flicker pulses: start, length, strength */
    float      lt_pulse[4][3];
    int        lt_strike;       /* this flash is a strike: the bolt is drawn */
    float      lt_since;        /* seconds since the last strike */
    int        lt_nseg;         /* the bolt: line segments x0 y0 x1 y1, the core first, then the branches */
    float      lt_seg[64][4];
    int        lt_z;            /* its depth (behind the trunks of the clearing) */
    int        stag;            /* (slot << 16) | low id bits of the spirit stag, 0 = none */
    float      stag_left;       /* seconds until the strike that takes it away */
    int        stag_done;       /* it came in this storm already */
    float      storm_k;         /* how stormy the scene is dimmed, 0..1: eases in and out over ~10 s (no jump at a weather change) */
    float      blue_t;          /* seconds since the stag's strike: a slow blue flash across the screen (< 0: none) */
    float      blue_k;          /* its strength (1 = it comes, less = it goes) */
    wx_drop_t  drop[MAX_DROP];
    int        ndrop;
    wx_splash_t splash[MAX_SPLASH];
    int        nsplash;
    rng_t      rng;             /* its own stream: the weather does not change how a seed builds the scene */
} weather_t;

/* --- events (src/event.c): rare, mysterious happenings; one at a time, with their own rng stream --- */
enum { EV_NONE, EV_FIREFLIES, EV_DHOLES, EV_BATS, EV_GHOST };
#define EV_MAX_FF 16
#define EV_MAX_PRINT 128
/* a footprint of the ghost walk: pressed into the ground, it glows a moment, then fades (also after the event) */
typedef struct {
    float x, y, age;
    int   z;
    int   side;                 /* 0 = the far foot, 1 = the near one (which edge the arch is on) */
    int   dir;                  /* +1 = walking to the right */
} ev_print_t;
typedef struct {
    int      kind;              /* EV_* running now */
    float    t;                 /* seconds since it started */
    float    next;              /* seconds until the director picks the next one */
    int      pending;           /* EV_* picked, being prepared (the scene is made ready first) */
    float    prep;              /* seconds left until the pending event starts */
    float    prep_next;         /* seconds until the next preparation step (e.g. one more firefly) */
    int      prep_left;         /* preparation steps still to do */
    int      n;                 /* actors taking part (fireflies; the deer + the dholes) */
    int      id[EV_MAX_FF];     /* (slot << 16) | low bits of their actor ids (the slot may be reused: compare a->id) */
    float    off[EV_MAX_FF];    /* each one's starting phase (0..1), converges to 0 = in sync */
    float    phase;             /* common blink phase (cycles) */
    float    cx, cy;            /* centre of the swarm (where the flash is brightest) */
    float    flash;             /* 0..1 whole-scene flash */
    int      gone;              /* the fireflies have vanished */
    /* ghost walk: something unseen walks slowly through the fog; only its footprints and the fog it parts show */
    float    gx, gz, gdir;      /* where it is (x, depth) and which way it walks */
    float    gfy;               /* y of its feet */
    float    gmoved;            /* x moved during the last tick (the plants it brushes) */
    float    gnext;             /* x of the next footfall */
    float    gstep;             /* stride (px between two footfalls) */
    float    gspeed;            /* px/s */
    float    gend;              /* the trail stops here (beyond the edge: it walks off) */
    float    gpause_at;         /* it stops here once for a while (NAN: no pause) */
    float    gwait;             /* > 0: standing still for this many more seconds */
    float    gvis;              /* 0..1 how strongly it parts the fog */
    float    glift;             /* the end: px it has risen while dissolving */
    int      gfoot;             /* the foot that steps next (0 / 1) */
    int      gstate;            /* 0 the hush before it, 1 walking, 2 standing, 3 dissolving */
    ev_print_t print[EV_MAX_PRINT];
    int      nprint;            /* footprints in use (when full, the oldest is reused) */
    rng_t    rng;
} event_t;

typedef struct {
    int        w, h;            /* raster dimensions */
    float      ground[NF_MAX_W];    /* ground height for x (y of the feet) */
    int        ground_z;        /* base depth of the creature lane on the ground */
    float      ground_mix[GT_COUNT];  /* weights of the floor patch types (biome); all 0 = plain soil */
    float      ground_scale;    /* patch size multiplier (1 = default) */
    uint32_t   ground_seed;
    int        fg_z;            /* foreground depth */
    float      ext_frac;        /* --grass-zspread > 1: 0..1, the ground extends this far down towards the bottom edge (depth + 60 * ext_frac) */
    /* growable arrays (world.c): pointers into them are valid only until the next world_add_* call */
    object_t  *obj;
    int        nobj, cap_obj;
    place_t   *place;
    int        nplace, cap_place;
    actor_t    act[MAX_ACT];
    int        nact;
    navpath_t *path;
    int        npath, cap_path;
    navlink_t *link;
    int        nlink, cap_link;
    stim_t     stim[MAX_STIM];
    int        nstim;
    float      t, wind, gust;
    /* wind: the prevailing direction changes slowly; between the gusts the air is calm, a gust is a wave
     * that is born somewhere, rolls through the plants in the wind direction and dies out */
    float      wind_dir, wind_target, wind_timer, wind_str;
    gust_t     gusts[MAX_GUST];
    int        ngust;
    float      next_gust;
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
    weather_t  wx;
    event_t    ev;
    /* director */
    int        rooted_ring;     /* which turned-earth patch is recycled next once there are enough */
    float      next_spawn;
    int        population;
} world_t;

/* --- world --- */
/* world_init expects a world that is new or was released with world_free (it does not free anything). */
void  world_init(world_t *w, library_t *lib, int width, int height, uint64_t seed);
void  world_free(world_t *w);
int   world_add_object(world_t *w, const char *sprite, float x, float y, int z, int flip, uint64_t seed);
int   world_add_place(world_t *w, int type, float x, float y, int z, int nav, int obj, int sub);
/* Adds interaction places from the object's properties (point.peek0, point.entry, point.hollow …). */
void  world_places_from_object(world_t *w, int obj);
void  world_play_object_clip(world_t *w, int obj, const char *clip);
void  world_set_object_base(world_t *w, int obj, const char *clip);
/* Moves a placed object (e.g. recycling the oldest turned-earth patch); the baked layer is redrawn. */
void  world_move_object(world_t *w, int obj, float x, float y, int z);
float world_ground(const world_t *w, float x);
/* Opening of a hollow (place): centre and radii of the ellipse. */
void  world_hollow_opening(const world_t *w, int place, float *cx, float *cy, float *rx, float *ry);
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

/* --- weather (weather.c) --- */
/* About how long one weather lasts (minutes); the real length is random around it. 0 = no weather. */
extern float g_weather_minutes;
/* Fronts run this many times faster (recording timelapses only, default 1). */
extern float g_weather_timelapse;
void  weather_init(world_t *w, uint64_t seed);
/* Starts the weather of a new scene: "auto" = random changes (sometimes it is already there),
 * a pattern name = that weather, fully there and staying. Unknown names fall back to auto. */
void  weather_begin(world_t *w, const char *arg);
/* Changes to the next pattern now (playground 'W'): the fronts start moving. */
void  events_init(world_t *w, uint64_t seed);
void  events_update(world_t *w, float dt);
void  events_render(world_t *w, raster_t *r);        /* footprints and the flash over the finished frame */
int   event_start(world_t *w, const char *name);     /* by name ("fireflies", "dholes", "bats", "ghost"); 0 = could not start */
/* The ghost walk parts the fog: the box (x0 y0 x1 y1) where it does and the depth of the walker (only what is
 * behind it is affected); 0 = nothing to do. event_fog_part at (x, y) inside the box: hole = how much of the
 * fog is pushed away (0..1), rim = how much gathers at the outline (0..1). */
int   event_fog_box(const world_t *w, int box[4], int *z);
void  event_fog_part(const world_t *w, int x, int y, float *hole, float *rim);
/* The walker brushing the plants: its x, depth and the x moved in the last tick; 0 = no walker. */
int   event_walker(const world_t *w, float *x, float *z, float *moved);
void  weather_next(world_t *w);
void  weather_set(world_t *w, int pattern);   /* roll in pattern and keep it; -1 = auto again */
/* A lightning strike into the scene now (storm or not); stag = 1: the spirit stag stands where it hits. */
int   weather_strike(world_t *w, int stag);
/* A ring spreading on a puddle at (x, y) (a raindrop, a footfall). */
void  weather_ring(world_t *w, float x, float y, int z);
int   weather_count(void);
void  weather_update(world_t *w, float dt);
/* Draws rain, splashes and fog over the finished frame (uses the z-buffer). */
void  weather_render(world_t *w, raster_t *r);
/* How much of a layer (WX_FOG, WX_RAIN) is at x right now, 0 … 1 (for the creatures' reactions). */
float weather_at(const world_t *w, int layer, float x);
const char *weather_pattern_name(int pattern);
/* "fog 12% rain 88%" style description for the HUD */
void  weather_describe(const world_t *w, char *out, int cap);

/* --- creatures --- */
int   actor_spawn(world_t *w, const char *species, float x, float y, int nav, int lane_z);
/* Spawns a creature directly in a hideout (bush, hollow) — it comes out according to its intent. */
int   actor_spawn_hidden(world_t *w, const char *species, int place);
void  actor_update(world_t *w, actor_t *a, float dt);
void  actor_set_intent(world_t *w, actor_t *a, int intent);
/* An event takes the creature over: it runs these steps (the last one should be ST_DESPAWN), its own brain
 * (perception, fear, curiosity, rain) stays off until the plan ends. */
void  actor_script(world_t *w, actor_t *a, int intent, const step_t *steps, int n);
const char *intent_name(int intent);
void  profile_parse(const sprite_t *s, profile_t *p);
/* Picks a ground lane depth around the profile's preferred row (sometimes anywhere). */
int   actor_pick_lane(world_t *w, const profile_t *p);

#endif
