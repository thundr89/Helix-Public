/*
 * Virtual .hxmap path mapping for the modular hxfs_tc plugin.
 * Copyright (C) 2026 Helix-Public contributors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include "bsp/names.h"
#include "tc_util.h"

#include <stdio.h>
#include <string.h>

int bsp_candidate_paths(const char* hxmap_name,
                        char out[][256], int max_out,
                        char* map_name, size_t map_name_cap) {
    size_t len;
    const char* base;

    if (!hxmap_name || !out || max_out < 3) return 0;
    len = strlen(hxmap_name);
    if (len < 6 || tc_strcasecmp(hxmap_name + len - 6, ".hxmap") != 0) return 0;

    base = strrchr(hxmap_name, '/');
    if (!base) base = strrchr(hxmap_name, '\\');
    base = base ? base + 1 : hxmap_name;

    if (map_name && map_name_cap > 0) {
        size_t blen = strlen(base);
        size_t cplen = blen >= 6 ? blen - 6 : blen;
        if (cplen >= map_name_cap) cplen = map_name_cap - 1;
        memcpy(map_name, base, cplen);
        map_name[cplen] = '\0';
    }

    snprintf(out[0], 256, "%.*s.bsp", (int)(len - 6), hxmap_name);
    if (strncmp(hxmap_name, "maps/", 5) == 0) {
        snprintf(out[1], 256, "%s", out[0]);
        snprintf(out[2], 256, "%s", out[0]);
    } else {
        snprintf(out[1], 256, "maps/%.*s.bsp", (int)(strlen(base) - 6), base);
        snprintf(out[2], 256, "maps/%.*s.bsp", (int)(strlen(base) - 6), base);
    }
    return 3;
}

int bsp_hxmap_alias(const char* bsp_vpath, char* out, size_t cap) {
    size_t len;

    if (!bsp_vpath || !out || cap < 16) return 0;
    if (strstr(bsp_vpath, "prefabs/") || strstr(bsp_vpath, "prefabs\\")) return 0;
    len = strlen(bsp_vpath);
    if (len < 5 || tc_strcasecmp(bsp_vpath + len - 4, ".bsp") != 0) return 0;
    if (len - 4 + 7 >= cap) return 0;
    memcpy(out, bsp_vpath, len - 4);
    memcpy(out + (len - 4), ".hxmap", 7);
    return 1;
}
