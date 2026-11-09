/* gen.h — procedural shape generators (plants, environment, small lights)
 *
 * Each generator fills a sprite from a seed: frames with per-pixel depth z,
 * animation clips (wind = param clip "sway", rustle, opening …)
 * and properties (interaction points "point.*", climbing lanes …).
 */
#ifndef NF_GEN_H
#define NF_GEN_H

#include "sprite.h"

/* Registers all generators in the library (with their default seed). */
void gen_register_all(library_t *lib, uint64_t seed);

/* Height of the generated trunks, background trees and moonbeams (128 = normal, taller for tall windows). */
void gen_set_tree_height(int h);

#endif
