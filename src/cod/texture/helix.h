/*
 * CoD image bytes to a PNG the Helix material table can load with stb.
 * Copyright (C) 2026 Helix-Public contributors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#ifndef TEXTURE_HELIX_H
#define TEXTURE_HELIX_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Returns 1 and a malloc'd PNG. Already-PNG input is copied through. */
int texture_to_png(const void* data, size_t size,
                   unsigned char** out_buf, unsigned int* out_size);

#ifdef __cplusplus
}
#endif

#endif /* TEXTURE_HELIX_H */
