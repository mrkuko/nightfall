/* sprite.h — shape library: frames (with depth), animation clips, properties
 *
 * A sprite = one shape (rabbit, fern, bush …). It has:
 *   - frames (frame_t): pixels with colour + depth offset z, anchor (pivot),
 *   - clips (clip_t): named animations = sequences of frames,
 *   - properties (prop_t): key -> value (creature profile, interaction points …),
 *   - optionally a generator: procedural shapes are regenerated from another seed.
 *
 * All shapes are drawn facing RIGHT; mirroring is done by the blit.
 */
#ifndef NF_SPRITE_H
#define NF_SPRITE_H

#include "common.h"

#define NF_NAME 40

typedef struct {
    char  name[NF_NAME];
    int   w, h;
    int   ax, ay;       /* anchor in frame pixels (typically the middle of the feet) */
    px_t *px;           /* w*h */
} frame_t;

enum {
    CLIP_LOOP,          /* loops forever */
    CLIP_ONCE,          /* plays once, stays on the last frame */
    CLIP_PINGPONG,      /* back and forth */
    CLIP_PARAM          /* the frame is picked by a parameter in [-1, 1] (wind, bend) */
};

typedef struct {
    char     name[NF_NAME];
    int      mode;
    float    fps;
    float    speed;     /* root motion in px/s during the clip (walking, hopping) */
    int      n;
    int16_t *seq;       /* frame indices, holds are expanded */
} clip_t;

typedef struct {
    char key[NF_NAME];
    char val[384];
} prop_t;

typedef struct sprite sprite_t;
typedef void (*gen_fn)(sprite_t *s, uint64_t seed);

struct sprite {
    char     name[NF_NAME];
    char     tags[128];     /* space-separated words: creature plant ground air … */
    char     src[64];       /* where it comes from (file / generator) */
    int      nframes, capf;
    frame_t *frames;
    int      nclips, capc;
    clip_t  *clips;
    int      nprops, capp;
    prop_t  *props;
    gen_fn   gen;           /* != NULL -> procedural shape */
    uint64_t seed;
};

typedef struct {
    int        n, cap;
    sprite_t **items;
    /* named palettes (shared between files) */
    int        npal, cappal;
    struct palette *pals;
    /* last parser errors */
    char       errors[8][160];
    int        nerrors;
} library_t;

/* --- sprite --- */
sprite_t *sprite_new(const char *name);
void      sprite_free(sprite_t *s);
void      sprite_reset(sprite_t *s);              /* clears frames/clips/props, keeps the name and gen */
frame_t  *sprite_add_frame(sprite_t *s, const char *name, int w, int h, int ax, int ay);
clip_t   *sprite_add_clip(sprite_t *s, const char *name, int mode, float fps, float speed,
                          const int16_t *seq, int n);
void      sprite_set_prop(sprite_t *s, const char *key, const char *val);
const char *sprite_prop(const sprite_t *s, const char *key);
/* "anim" = "idle=sit move=hop" -> sprite_prop_sub(s, "anim", "move") == "hop" */
int       sprite_prop_sub(const sprite_t *s, const char *key, const char *sub, char *out, int outsz);
int       sprite_has_tag(const sprite_t *s, const char *tag);
int       sprite_frame_index(const sprite_t *s, const char *name);
int       sprite_clip_index(const sprite_t *s, const char *name);
void      sprite_regen(sprite_t *s, uint64_t seed);

/* Frame index of a clip at time t (seconds since the clip started). */
int       clip_frame_at(const clip_t *c, float t);
/* Frame index of a clip for a parameter in [-1, 1]. */
int       clip_frame_param(const clip_t *c, float v);
/* Clip duration in seconds (one pass). */
float     clip_duration(const clip_t *c);

/* --- library --- */
void      lib_init(library_t *lib);
void      lib_free(library_t *lib);
void      lib_add(library_t *lib, sprite_t *s);
sprite_t *lib_find(const library_t *lib, const char *name);
/* Loads text in the .spr format (may contain several shapes). Returns the error count. */
int       lib_parse(library_t *lib, const char *text, const char *srcname);
/* Loads all *.spr in a directory (recursing one level). Returns the file count. */
int       lib_load_dir(library_t *lib, const char *dir);
/* Embedded assets (generated from assets/ at build time). */
int       lib_load_embedded(library_t *lib);

#endif
