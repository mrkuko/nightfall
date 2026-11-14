/* main.c — NIGHTFALL: entry point
 *
 *   nightfall                     screensaver: same scene as --play, no HUD (any key = quit)
 *   nightfall --gallery           library browser: every shape with its animations (also --galery)
 *   nightfall --play              playground: creatures with intents, HUD, spawn keys
 *   nightfall --sheet out.ppm     export the library to an image
 *   nightfall --snap out.ppm      without a terminal: playground simulation (--time s, --spawn species) + snapshot
 *
 * Options:
 *   --seed N        seed for procedural shapes and behaviour
 *   --scene-time S  (screensaver) after how many seconds to swap the scene (0 = never, default 600)
 *   --spawn-rate N=F …  overrides the spawn chance of species N in any biome (the biome's chances
 *                       add up to 1; e.g. --spawn-rate dhole=0.2 rabbit=0; 0 = never)
 *   --biome B           auto (default: random by seed) | off (no recommendations) | NAME | list
 *   --weather W         auto (default: fog and rain come and go) | clear | mist | fog | drizzle | rain | rainfog
 *   --weather-time M    about how long one weather lasts in minutes (default 6, random 0.6–1.4 ×, 0 = no weather)
 *   --grass-density F   foreground plant density (1 = default, 2 = twice as dense, 0.5 = sparser)
 *   --grass-spread F    randomness of the gaps between plants (0 = even, 1 = default, 2 = clumpy)
 *   --grass-zspread F   depth of the foreground (0 = one layer, 1 = default strip, 2 = half way down the dirt, 3+ = down to the bottom edge)
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

#ifndef NF_VERSION
#define NF_VERSION "dev"
#endif

static void usage(void)
{
    printf("NIGHTFALL %s — a living night forest in your terminal\n"
           "\n"
           "usage:\n"
           "  nightfall [options]              screensaver (any key quits)\n"
           "  nightfall --play [options]       playground: HUD, spawn keys, debug view\n"
           "  nightfall --gallery              browse every shape and its animations\n"
           "\n"
           "scene options (screensaver and playground):\n"
           "  --seed N                         reproducible scene and behaviour\n"
           "  --scene-time S                   new scene every S seconds (0 = never, default 600)\n"
           "  --biome auto|off|NAME|list       biome (default: random by seed)\n"
           "  --weather auto|NAME              weather: auto comes and goes, or one that stays:\n"
           "                                   clear mist fog drizzle rain rainfog\n"
           "  --weather-time M                 a weather lasts about M minutes, randomly 0.6-1.4x\n"
           "                                   (default 6, 0 = no weather)\n"
           "  --spawn-rate NAME=F [NAME=F ...] override spawn chances (biome chances sum to 1)\n"
           "  --grass-density F                foreground plant density (default 1)\n"
           "  --grass-spread F                 randomness of the gaps between plants (default 1)\n"
           "  --grass-zspread F                depth of the foreground: 2 = half way down the dirt, 3+ = to the bottom (default 1)\n"
           "\n"
           "authoring:\n"
           "  --assets DIR                     load .spr assets from DIR instead of the built-in ones\n"
           "  --sheet OUT.ppm [--filter X] [--depth] [--scale N] [--width PX]   export a sprite sheet\n"
           "  --snap OUT.ppm [--time S] [--spawn NAME ...] [--shot-scale N] [--width COLS] [--rows N]\n"
           "                                   headless simulation + snapshot of a COLS x N-row terminal\n"
           "  --record DIR --record-time S [--record-fps F] [--follow NAME] [--crop W H]\n"
           "                                   with --snap: also write frames f0000.ppm … (for GIFs)\n"
           "\n"
           "  -h, --help                       this help\n"
           "  -v, --version                    version\n", NF_VERSION);
}

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
    int nspawn = 0, nrate = 0;
    const char *rate_names[16];
    int snap_rows = 48;
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
        else if (!strcmp(a, "--gallery") || !strcmp(a, "--galery")) gallery = 1;
        else if (!strcmp(a, "-v") || !strcmp(a, "--version")) { printf("nightfall %s\n", NF_VERSION); return 0; }
        else if (!strcmp(a, "--spawn-rate") && i + 1 < argc) {
            /* --spawn-rate dhole=1 rabbit=0.2 ...: takes every following NAME=F */
            while (i + 1 < argc && strncmp(argv[i + 1], "--", 2) != 0 && strchr(argv[i + 1], '=')) {
                i++;
                if (spawn_rate_set(argv[i]) != 0) { fprintf(stderr, "bad --spawn-rate '%s' (use NAME=F)\n", argv[i]); return 1; }
                if (nrate < 16) rate_names[nrate++] = argv[i];
            }
        }
        else if (!strcmp(a, "--record") && i + 1 < argc) g_rec_dir = argv[++i];
        else if (!strcmp(a, "--record-time") && i + 1 < argc) g_rec_secs = (float)atof(argv[++i]);
        else if (!strcmp(a, "--record-fps") && i + 1 < argc) g_rec_fps = (float)atof(argv[++i]);
        else if (!strcmp(a, "--follow") && i + 1 < argc) g_rec_follow = argv[++i];
        else if (!strcmp(a, "--crop") && i + 2 < argc) { g_rec_cw = atoi(argv[i + 1]); g_rec_ch = atoi(argv[i + 2]); i += 2; }
        else if (!strcmp(a, "--rows") && i + 1 < argc) snap_rows = clampi(atoi(argv[++i]), 10, 400);
        else if (!strcmp(a, "--shot-scale") && i + 1 < argc) g_shot_scale = clampi(atoi(argv[++i]), 1, 12);
        else if (!strcmp(a, "--biome") && i + 1 < argc) g_biome_arg = argv[++i];
        else if (!strcmp(a, "--weather") && i + 1 < argc) g_weather_arg = argv[++i];
        else if (!strcmp(a, "--weather-time") && i + 1 < argc) { g_weather_minutes = (float)atof(argv[++i]); if (g_weather_minutes < 0) g_weather_minutes = 0; }
        else if (!strcmp(a, "--grass-density") && i + 1 < argc) g_fg_density = (float)atof(argv[++i]);
        else if (!strcmp(a, "--grass-spread") && i + 1 < argc) g_fg_spread = (float)atof(argv[++i]);
        else if (!strcmp(a, "--grass-zspread") && i + 1 < argc) g_fg_zspread = (float)atof(argv[++i]);
        else if (!strcmp(a, "--scene-time") && i + 1 < argc) scene_time = (float)atof(argv[++i]);
        else if (!strcmp(a, "-h") || !strcmp(a, "--help")) {
            usage();
            return 0;
        } else {
            fprintf(stderr, "nightfall: unknown option '%s' (see --help)\n", a);
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

    for (int k = 0; k < nrate; k++) {
        char nm[NF_NAME];
        snprintf(nm, sizeof nm, "%.*s", (int)(strchr(rate_names[k], '=') - rate_names[k]), rate_names[k]);
        const sprite_t *sp = lib_find(&lib, nm);
        if (!sp || !sprite_has_tag(sp, "creature"))
            fprintf(stderr, "nightfall: --spawn-rate: '%s' is not a creature\n", nm);
    }

    int rc;
    if (sheet) rc = sheet_export(&lib, sheet, filter, depth, scale, maxw);
    else if (snap) rc = play_snapshot(&lib, seed, maxw > 0 && maxw != 360 ? maxw : 160, snap_rows, simtime, snap, spawn, nspawn);
    else if (play) rc = play_run(&lib, assets, seed);
    else if (gallery) rc = gallery_run(&lib, assets, seed_given ? seed : 1);
    else rc = saver_run(&lib, seed, scene_time);
    lib_free(&lib);
    return rc;
}
