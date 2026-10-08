/*
 * Shared helpers for the modular hxfs_cod plugin.
 * Copyright (C) 2026 Helix-Public contributors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef COD_UTIL_H
#define COD_UTIL_H

#include <string.h>

#if defined(_MSC_VER)
#define cod_strcasecmp _stricmp
#define cod_strdup _strdup
#else
#include <strings.h>
#define cod_strcasecmp strcasecmp
#define cod_strdup strdup
#endif

#endif /* COD_UTIL_H */
