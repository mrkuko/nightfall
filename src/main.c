/* main.c — NIGHTFALL: entry point
 *
 *   nightfall                     screensaver: same scene as --play, no HUD (any key = quit)
 *   nightfall --gallery           library browser
 *   nightfall --play              playground: creatures with intents, HUD, spawn keys
 *   nightfall --sheet out.ppm     export the library to an image
 *   nightfall --snap out.ppm      without a terminal: playground simulation (--time s, --spawn species) + snapshot
 *
 * Options:
 *   --seed N        seed for procedural shapes and behaviour
 *   --scene-time S  (screensaver) after how many seconds to swap the scene (0 = never, default 600)
 *   --biome B           auto (default: random by seed) | off (no recommendations) | NAME | list
 *   --grass-density F   foreground plant density (1 = default, 2 = twice as dense, 0.5 = sparser)
 *   --grass-spread F    randomness of the gaps between plants (0 = even, 1 = default, 2 = clumpy)
 *   --grass-zspread F   spread of foreground depth (0 = one layer, 1 = default, 2 = deeper)
 *   --assets DIR    load assets from a directory (instead of the embedded ones; 'r' = reload)
 *   --filter X      (sheet) only shapes whose name/tag matches X
 *   --depth         (sheet) depth view
 *   --scale N       (sheet) pixel magnification
 */
#include "sprite.h"
#include "gen.h"
#include "sheet.h"
#include "gallery.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

int nf_load_library(library_t *lib, const char *assets_dir, uint64_t seed)
{
    lib_init(lib);
    gen_register_all(lib, seed);
    if (assets_dir) return lib_load_dir(lib, assets_dir);
    return lib_load_embedded(lib);
}

int main(int argc, char **argv)
{
    const char *sheet = NULL, *filter = NULL, *assets = NULL, *snap = NULL;
    float simtime = 30.0f;
    const char *spawn[16];
    int nspawn = 0;
    int depth = 0, scale = 3, play = 0, gallery = 0, maxw = 360, seed_given = 0;
    float scene_time = 600.0f;
    uint64_t seed = 0;
    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        if (!strcmp(a, "--sheet") && i + 1 < argc) sheet = argv[++i];
        else if (!strcmp(a, "--filter") && i + 1 < argc) filter = argv[++i];
        else if (!strcmp(a, "--assets") && i + 1 < argc) assets = argv[++i];
        else if (!strcmp(a, "--seed") && i + 1 < argc) { seed = strtoull(argv[++i], NULL, 10); seed_given = 1; }
        else if (!strcmp(a, "--scale") && i + 1 < argc) scale = atoi(argv[++i]);
        else if (!strcmp(a, "--width") && i + 1 < argc) maxw = atoi(argv[++i]);
        else if (!strcmp(a, "--depth")) depth = 1;
        else if (!strcmp(a, "--snap") && i + 1 < argc) snap = argv[++i];
        else if (!strcmp(a, "--time") && i + 1 < argc) simtime = (float)atof(argv[++i]);
        else if (!strcmp(a, "--spawn") && i + 1 < argc) { if (nspawn < 16) spawn[nspawn++] = argv[i + 1]; i++; }
        else if (!strcmp(a, "--play")) play = 1;
        else if (!strcmp(a, "--gallery")) gallery = 1;
        else if (!strcmp(a, "--biome") && i + 1 < argc) g_biome_arg = argv[++i];
        else if (!strcmp(a, "--grass-density") && i + 1 < argc) g_fg_density = (float)atof(argv[++i]);
        else if (!strcmp(a, "--grass-spread") && i + 1 < argc) g_fg_spread = (float)atof(argv[++i]);
        else if (!strcmp(a, "--grass-zspread") && i + 1 < argc) g_fg_zspread = (float)atof(argv[++i]);
        else if (!strcmp(a, "--scene-time") && i + 1 < argc) scene_time = (float)atof(argv[++i]);
        else if (!strcmp(a, "-h") || !strcmp(a, "--help")) {
            printf("nightfall [--seed N] [--scene-time S]        screensaver\n"
                   "nightfall --gallery [--assets DIR]            shape library\n"
                   "nightfall --play [--seed N]                   playground with intents\n"
                   "  biome (all modes): [--biome auto|off|NAME|list]\n"
                   "  foreground (all modes): [--grass-density F] [--grass-spread F] [--grass-zspread F]\n"
                   "nightfall --sheet out.ppm [--filter X] [--depth] [--scale N] [--width PX]\n");
            return 0;
        } else {
            fprintf(stderr, "unknown option: %s\n", a);
            return 1;
        }
    }
    if (!seed_given) seed = (uint64_t)time(NULL);

    library_t lib;
    nf_load_library(&lib, assets, seed_given ? seed : 1);

    if (!strcmp(g_biome_arg, "list")) {
        for (int i = 0; i < lib.nbiome; i++) {
            const char *l = sprite_prop(lib.biomes[i], "label");
            printf("%-14s %s\n", lib.biomes[i]->name, l ? l : "");
        }
        lib_free(&lib);
        return 0;
    }
    if (strcmp(g_biome_arg, "auto") && strcmp(g_biome_arg, "off") && !lib_find_biome(&lib, g_biome_arg))
        fprintf(stderr, "nightfall: unknown biome '%s' (see --biome list), using auto\n", g_biome_arg);

    int rc;
    if (sheet) rc = sheet_export(&lib, sheet, filter, depth, scale, maxw);
    else if (snap) rc = play_snapshot(&lib, seed, maxw > 0 && maxw != 360 ? maxw : 160, 48, simtime, snap, spawn, nspawn);
    else if (play) rc = play_run(&lib, assets, seed);
    else if (gallery) rc = gallery_run(&lib, assets, seed_given ? seed : 1);
    else rc = saver_run(&lib, seed, scene_time);
    lib_free(&lib);
    return rc;
}
