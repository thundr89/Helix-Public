/*
 * Call of Duty 1 (2003) IBSP v59 Map Loader & .hxmap Converter
 * Copyright (C) 2026 Helix-Public contributors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef COD_BSP_H
#define COD_BSP_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define COD_BSP_IDENT 0x50534249 /* "IBSP" in little endian */
#define COD_BSP_VERSION 59

/*
 * Returns 1 if `data` begins with "IBSP" and version 59, 0 otherwise.
 */
int cod_bsp_is_valid(const void* data, size_t size);

/*
 * Parses a Call of Duty 1 IBSP v59 map and converts its entities, materials,
 * and geometry into a valid Helix .hxmap text buffer.
 *
 * `map_name`: optional name for the map (e.g. "mp_harbor").
 * On success, *out_buf is allocated via malloc() and contains the null-terminated
 * .hxmap string, *out_size is its length, and returns 1.
 * On failure, returns 0.
 * Caller frees *out_buf with free().
 */
int cod_bsp_to_hxmap(const void* data, size_t size, const char* map_name,
                     unsigned char** out_buf, unsigned int* out_size);

#ifdef __cplusplus
}
#endif

#endif /* COD_BSP_H */
