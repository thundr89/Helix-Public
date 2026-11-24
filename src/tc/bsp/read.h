/*
 * True Combat / Enemy Territory IBSP v47 reader.
 * Copyright (C) 2026 Helix-Public contributors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef TC_BSP_READ_H
#define TC_BSP_READ_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define BSP_LUMP_COUNT 17

#define BSP_LUMP_ENTITIES      0
#define BSP_LUMP_SHADERS       1
#define BSP_LUMP_PLANES        2
#define BSP_LUMP_NODES         3
#define BSP_LUMP_LEAFS         4
#define BSP_LUMP_LEAFSURFACES  5
#define BSP_LUMP_LEAFBRUSHES   6
#define BSP_LUMP_MODELS        7
#define BSP_LUMP_BRUSHES       8
#define BSP_LUMP_BRUSHSIDES    9
#define BSP_LUMP_DRAWVERTS    10
#define BSP_LUMP_DRAWINDEXES  11
#define BSP_LUMP_FOGS         12
#define BSP_LUMP_SURFACES     13
#define BSP_LUMP_LIGHTMAPS    14
#define BSP_LUMP_LIGHTGRID    15
#define BSP_LUMP_VISIBILITY   16

typedef struct BspLump {
    unsigned int length;
    unsigned int offset;
} BspLump;

/* 1 when the buffer is IBSP version 47 with a full lump directory. */
int bsp_read_valid(const void* data, size_t size);

/* Fills all 17 lumps. Returns 0 when the header is not a valid IBSP v47. */
int bsp_read_lumps(const void* data, size_t size, BspLump out[BSP_LUMP_COUNT]);

#ifdef __cplusplus
}
#endif

#endif /* TC_BSP_READ_H */
