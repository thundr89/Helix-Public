/*
 * CoD1 GSC reader. Collects level.* assignments and simple calls.
 * This is not a script VM.
 * Copyright (C) 2026 Helix-Public contributors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef GSC_READ_H
#define GSC_READ_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct GscAssign {
    char key[64];
    char value[160];
    int is_string; /* 1 when the source value was quoted */
    int literal;   /* 0 when the value is a call, not a number or string */
} GscAssign;

typedef struct GscCall {
    char name[96];
    char arg[160]; /* first string argument, if any */
} GscCall;

typedef struct GscFile {
    GscAssign* assigns;
    int nassigns;
    GscCall* calls;
    int ncalls;
} GscFile;

int gsc_read(const void* data, size_t size, GscFile* out);
void gsc_read_free(GscFile* file);

#ifdef __cplusplus
}
#endif

#endif /* GSC_READ_H */
