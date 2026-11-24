/*
 * Parsed-asset cache beside the plugin for hxfs_tc.
 * Copyright (C) 2026 Helix-Public contributors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef TC_CACHE_H
#define TC_CACHE_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

int tc_cache_load(const char* dir, const char* key, unsigned int checksum,
                  unsigned char** out, unsigned int* size);

void tc_cache_store(const char* dir, const char* key, unsigned int checksum,
                    const void* data, unsigned int size);

void tc_cache_path(const char* dir, const char* key, char* out, size_t cap);

#ifdef __cplusplus
}
#endif

#endif /* TC_CACHE_H */
