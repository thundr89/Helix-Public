/*
 * hxfs_wad — clean-room WAD1/WAD2/WAD3 reader.
 * Copyright (C) 2026 Helix-Public contributors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef WAD_FILE_H
#define WAD_FILE_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct WadFile WadFile;

/* Takes a copy of `bytes`. Returns NULL on parse failure. */
WadFile* wad_open_mem(const void* bytes, size_t len);

/* Reads the whole file into memory. Returns NULL on I/O or parse failure. */
WadFile* wad_open_file(const char* path);

void wad_close(WadFile* w);

/* Case-insensitive virtual path. Returns 1 if present and readable. */
int wad_contains(const WadFile* w, const char* name);

/*
 * Heap-allocates a copy of the lump. On success writes *out_buf / *out_size
 * and returns 1. On failure returns 0 and leaves the outputs untouched.
 * Caller frees *out_buf with free().
 */
int wad_read(const WadFile* w, const char* name, unsigned char** out_buf, unsigned int* out_size);

/*
 * Unique virtual paths (last-wins). Heap array of heap strings.
 * Caller must wad_free_list().
 */
void wad_list(const WadFile* w, char*** names, unsigned int* count);
void wad_free_list(char** names, unsigned int count);

#ifdef __cplusplus
}
#endif

#endif /* WAD_FILE_H */
