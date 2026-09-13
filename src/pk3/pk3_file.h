/*
 * hxfs_pk3 — PK3 is ZIP; this header aliases the shared reader.
 * Copyright (C) 2026 Helix-Public contributors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef PK3_FILE_H
#define PK3_FILE_H

#include "zip_file.h"

typedef ZipFile Pk3File;

#define pk3_open_mem zip_open_mem
#define pk3_open_file zip_open_file
#define pk3_close zip_close
#define pk3_contains zip_contains
#define pk3_read zip_read
#define pk3_list zip_list
#define pk3_free_list zip_free_list

#endif /* PK3_FILE_H */
