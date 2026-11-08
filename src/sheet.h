#ifndef NF_SHEET_H
#define NF_SHEET_H

#include "sprite.h"

/* filter: part of a name or a tag (NULL = everything); depth: depth view. */
int sheet_export(const library_t *lib, const char *path, const char *filter, int depth, int scale,
                 int maxw);

#endif
