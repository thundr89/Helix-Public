/*
 * Virtual .hxmap path mapping for the modular hxfs_cod plugin.
 * Copyright (C) 2026 Helix-Public contributors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef BSP_NAMES_H
#define BSP_NAMES_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Fills up to three candidate BSP virtual paths for an .hxmap request:
 *   the same path with .bsp, maps/mp/<name>.bsp, and maps/<basename>.bsp.
 * `map_name` receives the basename without the extension.
 * Returns the number of paths written, or 0 if `hxmap_name` is not an .hxmap.
 */
int bsp_candidate_paths(const char* hxmap_name,
                        char out[][256], int max_out,
                        char* map_name, size_t map_name_cap);

/*
 * If `bsp_vpath`'s basename is mp_*.bsp, writes maps/<basename>.hxmap.
 * Returns 1 when an alias was written.
 */
int bsp_hxmap_alias(const char* bsp_vpath, char* out, size_t cap);

#ifdef __cplusplus
}
#endif

#endif /* BSP_NAMES_H */
