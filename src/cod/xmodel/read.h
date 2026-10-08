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

typedef struct CodXmodelBoneRule {
    int one_based;       /* Unused. Placement uses the engine's 0-based part index. */
    int parent0_is_root; /* Unused. Roots are the leading rootCount parts. */
} CodXmodelBoneRule;

void cod_xmodel_set_bone_rule(CodXmodelBoneRule rule);
CodXmodelBoneRule cod_xmodel_bone_rule(void);

/* World translation of each bone, xyz packed. Returns the bone count, or 0. */
int cod_xmodel_world_translations(const void* parts, size_t size, float* out_xyz, int cap);

/* cod_axes is column-major: [0..2] CoD X, [3..5] CoD Y, [6..8] CoD Z.
   out is Xx Xy Xz Yx Yy Yz Zx Zy Zz Tx Ty Tz. Returns 1, or 0 if a pointer is NULL. */
int cod_view_basis_rigid(const float cod_axes[9], const float cod_trans[3], float out[12]);

/* Baked pose of `name`, in the Helix view basis. Returns 1 when the name is found.
   A pose set with cod_xmodel_set_anim_pose replaces matching bone locals. */
int cod_xmodel_view_tag(const void* parts, size_t size, const char* name, float out[12]);

/* Frame 0 of a version-14 xanim. NULL clears. Returns 1 when a clip was stored. */
int cod_xmodel_set_anim_pose(const void* xanim, size_t size);

/* Merges frame 0 of another clip onto the pose. Bones already stored are replaced.
   A failed clip leaves the pose as it was. Returns 1 when the clip was merged. */
int cod_xmodel_overlay_anim_pose(const void* xanim, size_t size);

/* Three lines. *out_buf is malloc'd. Returns 1. */
int cod_viewhand_text(const float tag[12], unsigned char** out_buf, unsigned int* out_size);

/* Surface names of the first named LOD, after the collision block.
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
