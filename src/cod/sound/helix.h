/*
 * Sound aliases → Helix sound-shader text.
 * Copyright (C) 2026 Helix-Public contributors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef SOUND_HELIX_H
#define SOUND_HELIX_H

#include "archive/archive.h"
#include "sound/read.h"

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

int sound_to_helix(const SoundAliasFile* aliases,
                   unsigned char** out_buf, unsigned int* out_size);

/* Read a soundaliases CSV from bytes and translate it. */
int sound_csv_to_helix(const void* data, size_t size,
                       unsigned char** out_buf, unsigned int* out_size);

/* Append Helix gameplay shader names when the archive has the CoD wav. */
int sound_append_gameplay(const CodArchive* ar, unsigned char** buf, unsigned int* size);

#ifdef __cplusplus
}
#endif

#endif /* SOUND_HELIX_H */
