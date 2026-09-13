/*
 * Shared ZIP reader for PK3 / PK4 (published APPNOTE).
 * Copyright (C) 2026 Helix-Public contributors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef ZIP_FILE_H
#define ZIP_FILE_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ZipFile ZipFile;

/* Takes a copy of `bytes`. Returns NULL on parse failure. */
ZipFile* zip_open_mem(const void* bytes, size_t len);

/* Reads the whole file into memory. Returns NULL on I/O or parse failure. */
ZipFile* zip_open_file(const char* path);

void zip_close(ZipFile* p);

/* Case-insensitive virtual path. Returns 1 if present and readable. */
int zip_contains(const ZipFile* p, const char* name);

/*
 * Heap-allocates a copy of the file (inflated if needed). On success writes
 * *out_buf / *out_size and returns 1. On failure returns 0 and leaves the
 * outputs untouched. Caller frees *out_buf with free().
 */
int zip_read(const ZipFile* p, const char* name, unsigned char** out_buf, unsigned int* out_size);

/*
 * Unique virtual paths (last-wins). Heap array of heap strings.
 * Caller must zip_free_list().
 */
void zip_list(const ZipFile* p, char*** names, unsigned int* count);
void zip_free_list(char** names, unsigned int count);

#ifdef __cplusplus
}
#endif

#endif /* ZIP_FILE_H */
