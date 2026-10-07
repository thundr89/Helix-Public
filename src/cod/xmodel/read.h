/*
 * Call of Duty 1 (2003) xmodel -> OBJ
 * Copyright (C) 2026 Helix-Public contributors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * Lives in the modular hxfs_cod plugin (src/cod). src/cod2003 is a separate plugin and is left unchanged.
 *
 * CoD1 keeps a static prop as three PK3 entries: xmodel/<name> (LOD table),
 * xmodelparts/<name> (bones, not required for the bind-pose mesh) and
 * xmodelsurfs/<name> (vertices and triangles). The plugin returns OBJ text
 * so the engine asset array can load it without a CoD-specific parser.
 */
#ifndef COD_XMODEL_H
#define COD_XMODEL_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 1 if `data` is an ASCII XMODEL_EXPORT (VERSION 5), not a PK3 binary. */
int cod_xmodel_is_text(const void* data, size_t size);

/*
 * First non-empty LOD file name from a binary xmodel header (version 14, 5, or 20).
 * `out` is NUL-terminated. Returns 1 on success.
 */
int cod_xmodel_lod_name(const void* data, size_t size, char* out, size_t out_cap);

/* Every named LOD surf, file order. `names` is cap rows of 64 chars. Returns the count. */
int cod_xmodel_lod_names(const void* data, size_t size, char names[][64], int cap);

typedef struct CodXmodelLod {
    float distance;
    char name[64];
} CodXmodelLod;

/* v14 writes 3 slots, including empty names. v5/v20 writes 4. Returns the slot count, or 0. */
int cod_xmodel_lod_slots(const void* data, size_t size, CodXmodelLod* out, int cap);

/* Viewmodels keep a hand skin. Body models drop it when another skin exists. */
void cod_xmodel_set_keep_hands(int keep);

/* Image names in the contiguous .dds/.tga/.jpg/.png run at the end of the xmodel.
 * `names` is cap rows of 160 chars. Returns the count. Does not sort and does not add skins/. */
int cod_xmodel_skin_names(const void* xmodel, size_t size, char names[][160], int cap);

/*
 * Convert an xmodel into OBJ text.
 * Text export: `surfs` and `parts` may be NULL.
 * Binary: `xmodel` is the xmodel header (version checked), `surfs` is the
 * matching xmodelsurfs blob, and `parts` is the optional xmodelparts blob
 * providing bind-pose bone translations for multi-part models (e.g. vehicle wheels).
 * On success *out_buf is malloc'd OBJ text and the function returns 1.
 * Caller frees *out_buf with free().
 */
int cod_xmodel_to_obj(const void* xmodel, size_t xmodel_size,
                      const void* surfs, size_t surfs_size,
                      const void* parts, size_t parts_size,
                      unsigned char** out_buf, unsigned int* out_size);

#ifdef __cplusplus
}
#endif

#endif /* COD_XMODEL_H */
