/*
 * hxfs_pak — clean-room Quake 1/2 PAK reader.
 * Copyright (C) 2026 Helix-Public contributors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef PAK_FILE_H
#define PAK_FILE_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct PakFile PakFile;

/* Takes a copy of `bytes`. Returns NULL on parse failure. */
PakFile* pak_open_mem(const void* bytes, size_t len);

/* Reads the whole file into memory. Returns NULL on I/O or parse failure. */
PakFile* pak_open_file(const char* path);

void pak_close(PakFile* p);

/* Case-insensitive virtual path. Returns 1 if present and readable. */
int pak_contains(const PakFile* p, const char* name);

/*
 * Heap-allocates a copy of the file. On success writes *out_buf / *out_size
 * and returns 1. On failure returns 0 and leaves the outputs untouched.
 * Caller frees *out_buf with free().
 */
int pak_read(const PakFile* p, const char* name, unsigned char** out_buf, unsigned int* out_size);

/*
 * Unique virtual paths (last-wins). Heap array of heap strings.
 * Caller must pak_free_list().
 */
void pak_list(const PakFile* p, char*** names, unsigned int* count);
void pak_free_list(char** names, unsigned int count);

#ifdef __cplusplus
}
#endif

#endif /* PAK_FILE_H */
