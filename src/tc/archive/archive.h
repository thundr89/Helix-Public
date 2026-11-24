/*
 * Multi-zip plus loose-file storage for the modular hxfs_tc plugin.
 * Copyright (C) 2026 Helix-Public contributors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef TC_ARCHIVE_H
#define TC_ARCHIVE_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct TcArchive TcArchive;

TcArchive* tc_archive_create(void);
void tc_archive_destroy(TcArchive* a);
void tc_archive_clear(TcArchive* a);

int tc_archive_add_zip(TcArchive* a, const char* path);
int tc_archive_add_blob(TcArchive* a, const char* vpath, const void* data, unsigned int size);

int tc_archive_contains(const TcArchive* a, const char* name);
int tc_archive_read(const TcArchive* a, const char* name,
                    unsigned char** out_buf, unsigned int* out_size);
int tc_archive_checksum(const TcArchive* a, const char* name, unsigned int* crc);
void tc_archive_list(const TcArchive* a, char*** names, unsigned int* count);

#ifdef __cplusplus
}
#endif

#endif /* TC_ARCHIVE_H */
