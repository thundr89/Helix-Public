/*
 * CoD1 IBSP reader. Bytes and lump directory only; no Helix text.
 * Copyright (C) 2026 Helix-Public contributors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include "bsp/read.h"

#define BSP_IDENT 0x50534249 /* "IBSP" */
#define BSP_VERSION 59

int bsp_read_valid(const void* data, size_t size) {
    const unsigned char* p = (const unsigned char*)data;
    unsigned int ident;
    int version;
    if (!data || size < 8 + BSP_LUMP_COUNT * 8) return 0;
    ident = (unsigned int)p[0] | ((unsigned int)p[1] << 8) |
            ((unsigned int)p[2] << 16) | ((unsigned int)p[3] << 24);
    version = (int)((unsigned int)p[4] | ((unsigned int)p[5] << 8) |
                    ((unsigned int)p[6] << 16) | ((unsigned int)p[7] << 24));
    return ident == BSP_IDENT && version == BSP_VERSION;
}

int bsp_read_lumps(const void* data, size_t size, BspLump out[BSP_LUMP_COUNT]) {
    const unsigned char* bytes = (const unsigned char*)data;
    int i;
    if (!out || !bsp_read_valid(data, size)) return 0;
    for (i = 0; i < BSP_LUMP_COUNT; i++) {
        const unsigned char* p = bytes + 8 + i * 8;
        out[i].length = (unsigned int)p[0] | ((unsigned int)p[1] << 8) |
                        ((unsigned int)p[2] << 16) | ((unsigned int)p[3] << 24);
        out[i].offset = (unsigned int)p[4] | ((unsigned int)p[5] << 8) |
                        ((unsigned int)p[6] << 16) | ((unsigned int)p[7] << 24);
    }
    return 1;
}
