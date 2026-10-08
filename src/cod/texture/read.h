/*
 * CoD1 image bytes to RGBA. TGA (type 2 and 10) and DDS (uncompressed, DXT1, DXT3, DXT5).
 * An unknown compression returns 0 and does not invent pixels.
 * Copyright (C) 2026 Helix-Public contributors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#ifndef TEXTURE_READ_H
#define TEXTURE_READ_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* *out_rgba is malloc'd width*height*4, top-down, straight alpha. Caller frees. */
int texture_read_rgba(const void* data, size_t size,
                      unsigned char** out_rgba, int* out_w, int* out_h);

#ifdef __cplusplus
}
#endif

#endif /* TEXTURE_READ_H */
