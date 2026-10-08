/*
 * GSC facts → Helix .hxsc game-mode text.
 * Copyright (C) 2026 Helix-Public contributors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef GSC_HELIX_H
#define GSC_HELIX_H

#include "gsc/read.h"

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

int gsc_to_helix(const GscFile* script,
                 unsigned char** out_buf, unsigned int* out_size);

int gsc_source_to_helix(const void* data, size_t size,
                        unsigned char** out_buf, unsigned int* out_size);

/* xmodel/playerbody_<allies>_<type>[_<variation>] from game["…"] assigns. */
int gsc_allied_body(const GscFile* script, char* out, size_t cap);

#ifdef __cplusplus
}
#endif

#endif /* GSC_HELIX_H */
