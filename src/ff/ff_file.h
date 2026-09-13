/*
 * hxfs_ff — versioned CoD FastFile (IWff) reader.
 * Copyright (C) 2026 Helix-Public contributors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * Layouts follow public notes from OpenAssetTools / IW4x / CoD engine
 * research (magic + version → engine/platform). No game code is copied.
 */

#ifndef FF_FILE_H
#define FF_FILE_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct FfFile FfFile;

FfFile* ff_open_mem(const void* bytes, size_t len);
FfFile* ff_open_file(const char* path);
void ff_close(FfFile* p);

int ff_contains(const FfFile* p, const char* name);
int ff_read(const FfFile* p, const char* name, unsigned char** out_buf, unsigned int* out_size);
void ff_list(const FfFile* p, char*** names, unsigned int* count);
void ff_free_list(char** names, unsigned int count);

/* Stable id: "iw3-pc", "iw3-xenon", "waw-pc", "iw4-pc", … */
const char* ff_profile(const FfFile* p);
unsigned int ff_version(const FfFile* p);
int ff_big_endian(const FfFile* p);

#ifdef __cplusplus
}
#endif

#endif /* FF_FILE_H */
