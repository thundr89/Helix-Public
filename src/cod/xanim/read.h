/*
 * CoD1 xanim reader. Identifies an export or a binary clip; no frames.
 * Copyright (C) 2026 Helix-Public contributors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef XANIM_READ_H
#define XANIM_READ_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum XanimKind {
    XANIM_NONE = 0,
    XANIM_TEXT = 1,
    XANIM_BINARY = 2
} XanimKind;

XanimKind xanim_identify(const void* data, size_t size);

/* Little-endian u16 version. 0 when the buffer is not a binary clip. */
int xanim_binary_version(const void* data, size_t size);

#ifdef __cplusplus
}
#endif

#endif /* XANIM_READ_H */
