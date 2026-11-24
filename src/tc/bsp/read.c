/*
 * True Combat / Enemy Territory IBSP v47 reader.
 * Copyright (C) 2026 Helix-Public contributors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include "bsp/read.h"

#include <string.h>

#define BSP_HEADER_SIZE (8 + BSP_LUMP_COUNT * 8)

static unsigned int read_u32(const unsigned char* p) {
    return (unsigned int)p[0] |
           ((unsigned int)p[1] << 8) |
           ((unsigned int)p[2] << 16) |
           ((unsigned int)p[3] << 24);
}

int bsp_read_valid(const void* data, size_t size) {
    const unsigned char* p;
    unsigned int version;
    int i;

    if (!data || size < BSP_HEADER_SIZE) return 0;
    p = (const unsigned char*)data;
    if (memcmp(p, "IBSP", 4) != 0) return 0;
    version = read_u32(p + 4);
    if (version != 47) return 0;

    for (i = 0; i < BSP_LUMP_COUNT; i++) {
        unsigned int off = read_u32(p + 8 + i * 8);
        unsigned int len = read_u32(p + 12 + i * 8);
        if (len == 0) continue;
        if (off >= size || (size_t)off + len > size) return 0;
    }
    return 1;
}

int bsp_read_lumps(const void* data, size_t size, BspLump out[BSP_LUMP_COUNT]) {
    const unsigned char* p;
    int i;

    if (!bsp_read_valid(data, size) || !out) return 0;
    p = (const unsigned char*)data;
    for (i = 0; i < BSP_LUMP_COUNT; i++) {
        out[i].offset = read_u32(p + 8 + i * 8);
        out[i].length = read_u32(p + 12 + i * 8);
    }
    return 1;
}
