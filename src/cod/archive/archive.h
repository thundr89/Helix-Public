/*
 * Multi-zip plus loose-file storage for the modular hxfs_cod plugin.
 * Copyright (C) 2026 Helix-Public contributors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * Later zips override earlier ones. A loose blob with the same virtual
 * path overrides every zip (standalone .bsp behaviour).
 */

#ifndef COD_ARCHIVE_H
#define COD_ARCHIVE_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct CodArchive CodArchive;

CodArchive* cod_archive_create(void);
void cod_archive_destroy(CodArchive* a);
void cod_archive_clear(CodArchive* a);

/* Opens a PK3/ZIP. Returns 1 on success. */
int cod_archive_add_zip(CodArchive* a, const char* path);

/* Copies `data` under a virtual path. Replaces an earlier blob of the same name. */
int cod_archive_add_blob(CodArchive* a, const char* vpath, const void* data, unsigned int size);

int cod_archive_contains(const CodArchive* a, const char* name);

/*
 * Heap-allocates a copy. On success writes *out_buf / *out_size and returns 1.
 * Caller frees *out_buf with free().
 */
int cod_archive_read(const CodArchive* a, const char* name,
                     unsigned char** out_buf, unsigned int* out_size);

/* ZIP central-directory CRC, or CRC-32 of a loose blob. Does not inflate. */
int cod_archive_checksum(const CodArchive* a, const char* name, unsigned int* crc);

/* Unique virtual paths. Caller frees each string and the array. */
void cod_archive_list(const CodArchive* a, char*** names, unsigned int* count);

#ifdef __cplusplus
}
#endif

#endif /* COD_ARCHIVE_H */
