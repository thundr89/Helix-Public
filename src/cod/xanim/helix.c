/*
 * xanim → Helix. Text exports are copied. Binary clips are not framed.
 * Copyright (C) 2026 Helix-Public contributors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include "xanim/helix.h"
#include "xanim/read.h"

#include <stdlib.h>
#include <string.h>

int xanim_to_helix_text(const void* data, size_t size,
                        unsigned char** out_buf, unsigned int* out_size) {
    unsigned char* copy;
    if (!out_buf || !out_size) return 0;
    if (xanim_identify(data, size) != XANIM_TEXT) return 0;
    copy = (unsigned char*)malloc(size ? size : 1);
    if (!copy) return 0;
    if (size) memcpy(copy, data, size);
    *out_buf = copy;
    *out_size = (unsigned int)size;
    return 1;
}

int xanim_to_helix(const CodArchive* ar, const char* name,
                   unsigned char** out_buf, unsigned int* out_size) {
    unsigned char* raw = NULL;
    unsigned int sz = 0;
    int ok;

    if (!ar || !name || !out_buf || !out_size) return 0;
    if (strncmp(name, "xanim/", 6) != 0 || !name[6]) return 0;
    if (strchr(name + 6, '/') || strchr(name + 6, '\\')) return 0;
    if (!cod_archive_read(ar, name, &raw, &sz)) return 0;

    if (xanim_identify(raw, sz) == XANIM_TEXT) {
        ok = xanim_to_helix_text(raw, sz, out_buf, out_size);
        free(raw);
        return ok;
    }
    *out_buf = raw;
    *out_size = sz;
    return 1;
}
