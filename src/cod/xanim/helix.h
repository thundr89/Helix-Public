/*
 * xanim → Helix. Text exports are copied. Binary clips are not framed.
 * Copyright (C) 2026 Helix-Public contributors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef XANIM_HELIX_H
#define XANIM_HELIX_H

#include "archive/archive.h"

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Copies a text export. Returns 0 for a binary clip. */
int xanim_to_helix_text(const void* data, size_t size,
                        unsigned char** out_buf, unsigned int* out_size);

/*
 * xanim/<name>. Text goes through xanim_to_helix_text.
 * A binary clip is the archive bytes; it is not a decoded animation.
 */
int xanim_to_helix(const CodArchive* ar, const char* name,
                   unsigned char** out_buf, unsigned int* out_size);

#ifdef __cplusplus
}
#endif

#endif /* XANIM_HELIX_H */
