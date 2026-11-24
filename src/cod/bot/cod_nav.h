/*
 * CoD1 BSP Automatic Navigation Area & Reachability Generator (ZBot-style)
 * plus virtual bot profile & chatter catalog for hxfs_cod.
 * Copyright (C) 2026 Helix-Public contributors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef HXFS_COD_BOT_NAV_H
#define HXFS_COD_BOT_NAV_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct CodNavBox {
    float min[3];
    float max[3];
    int flags; /* 0 = solid/walkable, 1 = ladder, 2 = crouch, 4 = objective/spawn anchor */
} CodNavBox;

typedef struct CodNavBuilder {
    CodNavBox* boxes;
    int count;
    int cap;
} CodNavBuilder;

void cod_nav_init(CodNavBuilder* nb);
void cod_nav_free(CodNavBuilder* nb);

/* Record a walkable brush top, ladder volume, or entity anchor during BSP conversion. */
void cod_nav_add_walkable_box(CodNavBuilder* nb, const float min[3], const float max[3]);
void cod_nav_add_ladder_box(CodNavBuilder* nb, const float min[3], const float max[3]);
void cod_nav_add_anchor_point(CodNavBuilder* nb, const float origin[3], int is_crouch);

/* Emit `area { ... }` and `reach { ... }` blocks into the .hxmap text buffer.
 * Returns a newly malloc'd string (caller frees) or NULL on allocation failure. */
char* cod_nav_emit_hxmap(const CodNavBuilder* nb, size_t* out_len);

/* Virtual files exposed by hxfs_cod for ZBot personalities, weapon prefs, and chatter. */
int cod_bot_virtual_exists(const char* name);
int cod_bot_virtual_read(const char* name, unsigned char** out_buf, unsigned int* out_size);

#ifdef __cplusplus
}
#endif

#endif /* HXFS_COD_BOT_NAV_H */
