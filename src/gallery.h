#ifndef NF_GALLERY_H
#define NF_GALLERY_H

#include "sprite.h"

int nf_load_library(library_t *lib, const char *assets_dir, uint64_t seed);

/* Interactive library browser. */
int gallery_run(library_t *lib, const char *assets_dir, uint64_t seed);
/* Playground: a small scene where creatures live by their intents. */
int play_run(library_t *lib, const char *assets_dir, uint64_t seed);
/* Screensaver (default mode): same scene as the playground, the scene changes after scene_time s. */
int saver_run(library_t *lib, uint64_t seed, float scene_time);
/* Without a terminal: simulation + saving a snapshot (PPM) and printing events. */
int play_snapshot(library_t *lib, uint64_t seed, int cols, int rows, float seconds, const char *ppm,
                  const char *const *spawn, int nspawn);

#endif
