/*
 * Call of Duty 1 (2003) xmodel -> OBJ
 * Copyright (C) 2026 Helix-Public contributors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
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

/*
 * Convert an xmodel into OBJ text.
 * Text export: `surfs` may be NULL.
 * Binary: `xmodel` is the xmodel header (used only to accept the version) and
 * `surfs` is the matching xmodelsurfs blob. Bind-pose positions are emitted;
 * skin weights are skipped.
 * On success *out_buf is malloc'd OBJ text and the function returns 1.
 * Caller frees *out_buf with free().
 */
int cod_xmodel_to_obj(const void* xmodel, size_t xmodel_size,
                      const void* surfs, size_t surfs_size,
                      unsigned char** out_buf, unsigned int* out_size);

#ifdef __cplusplus
}
#endif

#endif /* COD_XMODEL_H */
