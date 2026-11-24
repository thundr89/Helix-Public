/*
 * xmodel/<name> from the archive, written as Helix OBJ.
 * The binary layout is xmodel/read.h.
 * Copyright (C) 2026 Helix-Public contributors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef XMODEL_HELIX_H
#define XMODEL_HELIX_H

#include "archive/archive.h"

#ifdef __cplusplus
extern "C" {
#endif

int cod_weapon_catalog(const CodArchive*,unsigned char**,unsigned*);
int cod_weapon_view_exists(const CodArchive*,const char*);
int cod_weapon_view_checksum(const CodArchive*,const char*,unsigned*);
int cod_weapon_view_file(const CodArchive*,const char*,unsigned char**,unsigned*);
int xmodel_to_helix(const CodArchive* ar, const char* name,
                    unsigned char** out_buf, unsigned int* out_size);
int xmodel_lod_to_helix(const CodArchive*,const char*,int lod,unsigned char**,unsigned*);

/* Helix gameplay mesh path -> xmodel/<name>, or NULL when it is not one of those.
 * models/player.obj uses the body from cod_set_player_body when one is set. */
const char* cod_gameplay_xmodel(const char* name);

/* xmodel/playerbody_… for the local player, or NULL to use the default body. */
void cod_set_player_body(const char* xmodel);

/* 1 when both xmodels are in the archive and tag_weapon resolves. */
int cod_viewhand_file(const CodArchive* ar, unsigned char** out_buf, unsigned int* out_size);

#ifdef __cplusplus
}
#endif

#endif /* XMODEL_HELIX_H */

int cod_model_tags(const CodArchive* ar, const char* model, unsigned char** out, unsigned int* size);
