/*
 * Shared helpers for the modular hxfs_tc plugin.
 * Copyright (C) 2026 Helix-Public contributors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef TC_UTIL_H
#define TC_UTIL_H

#include <string.h>

#if defined(_MSC_VER)
#define tc_strcasecmp _stricmp
#define tc_strdup _strdup
#else
#include <strings.h>
#define tc_strcasecmp strcasecmp
#define tc_strdup strdup
#endif

#endif /* TC_UTIL_H */
