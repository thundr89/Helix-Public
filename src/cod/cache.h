/*
 * Parsed-asset cache beside the plugin. The .cache file is the translation
 * itself (text or image bytes, no header). The source checksum lives in a
 * sibling .crc file. A match returns the body; a mismatch, a missing
 * sidecar, or a legacy HXCA prefix parses that element again.
 * Copyright (C) 2026 Helix-Public contributors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef COD_CACHE_H
#define COD_CACHE_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 1 when the file exists and its stored checksum equals `checksum`. */
int cod_cache_load(const char* dir, const char* key, unsigned int checksum,
                   unsigned char** out, unsigned int* size);

void cod_cache_store(const char* dir, const char* key, unsigned int checksum,
                     const void* data, unsigned int size);

void cod_cache_path(const char* dir, const char* key, char* out, size_t cap);

#ifdef __cplusplus
}
#endif

#endif /* COD_CACHE_H */
