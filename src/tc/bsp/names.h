/*
 * Virtual .hxmap path mapping for the modular hxfs_tc plugin.
 * Copyright (C) 2026 Helix-Public contributors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef TC_BSP_NAMES_H
#define TC_BSP_NAMES_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

int bsp_candidate_paths(const char* hxmap_name,
                        char out[][256], int max_out,
                        char* map_name, size_t map_name_cap);

int bsp_hxmap_alias(const char* bsp_vpath, char* out, size_t cap);

#ifdef __cplusplus
}
#endif

#endif /* TC_BSP_NAMES_H */
