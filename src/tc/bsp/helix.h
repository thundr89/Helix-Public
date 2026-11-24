/*
 * True Combat / Enemy Territory IBSP v47 Map Loader & .hxmap Converter
 * Copyright (C) 2026 Helix-Public contributors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef TC_BSP_HELIX_H
#define TC_BSP_HELIX_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

int bsp_to_helix_map(const unsigned char* bytes, size_t size, const char* map_name,
                     unsigned char** out_buf, unsigned int* out_size);

#ifdef __cplusplus
}
#endif

#endif /* TC_BSP_HELIX_H */
