#ifndef NF_GALLERY_H
#define NF_GALLERY_H

#include "sprite.h"

/* Foreground tuning, 1.0 = default (set from the command line). */
extern float g_fg_density, g_fg_spread, g_fg_zspread;
/* Biome: "auto" (random by seed), "off" (no recommendations) or a biome name. */
extern const char *g_biome_arg;
/* Weather: "auto" (it comes and goes) or a pattern that stays: clear mist fog drizzle rain rainfog. */
extern const char *g_weather_arg;
int biome_cycle(const library_t *lib);
/* "NAME=F": spawn frequency of a species regardless of the biome (F = share of the commonest species). */
int spawn_rate_set(const char *spec);
/* Headless capture for docs (--snap + --record): see main.c */
extern const char *g_rec_dir, *g_rec_follow;
extern float g_rec_secs, g_rec_fps;
extern int g_rec_cw, g_rec_ch, g_shot_scale;

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
