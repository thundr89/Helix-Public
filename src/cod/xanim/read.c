/*
 * CoD1 xanim reader. Identifies an export or a binary clip; no frames.
 * Copyright (C) 2026 Helix-Public contributors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include "xanim/read.h"

#include <string.h>

static size_t skip_ws(const unsigned char* p, size_t size) {
    size_t i = 0;
    while (i < size && (p[i] == ' ' || p[i] == '\t' || p[i] == '\r' || p[i] == '\n')) i++;
    return i;
}

XanimKind xanim_identify(const void* data, size_t size) {
    const unsigned char* p = (const unsigned char*)data;
    size_t i;
    if (!data || size == 0) return XANIM_NONE;
    i = skip_ws(p, size);
    if (size - i >= 9 && memcmp(p + i, "ANIMATION", 9) == 0) return XANIM_TEXT;
    if (size - i >= 5 && memcmp(p + i, "XANIM", 5) == 0) return XANIM_TEXT;
    if (size >= 2) return XANIM_BINARY;
    return XANIM_NONE;
}

int xanim_binary_version(const void* data, size_t size) {
    const unsigned char* p;
    if (xanim_identify(data, size) != XANIM_BINARY) return 0;
    p = (const unsigned char*)data;
    return (int)p[0] | ((int)p[1] << 8);
}
