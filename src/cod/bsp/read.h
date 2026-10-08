/*
 * CoD1 IBSP reader. Bytes and lump directory only; no Helix text.
 * Copyright (C) 2026 Helix-Public contributors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef BSP_READ_H
#define BSP_READ_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define BSP_LUMP_COUNT 33
#define BSP_LUMP_ENTITIES 29

typedef struct BspLump {
    unsigned int length;
    unsigned int offset;
} BspLump;

/* 1 when the buffer is IBSP version 59 with a full lump directory. */
int bsp_read_valid(const void* data, size_t size);

/* Fills all 33 lumps. Returns 0 when the header is not a valid IBSP. */
int bsp_read_lumps(const void* data, size_t size, BspLump out[BSP_LUMP_COUNT]);

#ifdef __cplusplus
}
#endif

#endif /* BSP_READ_H */
