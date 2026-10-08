/*
 * Small text buffer for Helix translations.
 * Copyright (C) 2026 Helix-Public contributors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef COD_EMIT_H
#define COD_EMIT_H

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct CodEmit {
    char* data;
    size_t len;
    size_t cap;
} CodEmit;

static int cod_emit_init(CodEmit* e) {
    e->cap = 256;
    e->len = 0;
    e->data = (char*)malloc(e->cap);
    if (!e->data) return 0;
    e->data[0] = '\0';
    return 1;
}

static int cod_emit_add(CodEmit* e, const char* s) {
    size_t n;
    if (!e || !e->data || !s) return 0;
    n = strlen(s);
    if (e->len + n + 1 > e->cap) {
        size_t ncap = e->cap ? e->cap * 2 : 256;
        char* grown;
        while (e->len + n + 1 > ncap) ncap *= 2;
        grown = (char*)realloc(e->data, ncap);
        if (!grown) return 0;
        e->data = grown;
        e->cap = ncap;
    }
    memcpy(e->data + e->len, s, n);
    e->len += n;
    e->data[e->len] = '\0';
    return 1;
}

static int cod_emit_fmt(CodEmit* e, const char* fmt, ...) {
    char buf[4096];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    if (n < 0) return 0;
    if ((size_t)n < sizeof(buf)) {
        return cod_emit_add(e, buf);
    }
    char* big = (char*)malloc((size_t)n + 1);
    if (!big) return 0;
    va_start(ap, fmt);
    vsnprintf(big, (size_t)n + 1, fmt, ap);
    va_end(ap);
    int ok = cod_emit_add(e, big);
    free(big);
    return ok;
}

static int cod_emit_take(CodEmit* e, unsigned char** out, unsigned int* size) {
    if (!e || !e->data || !out || !size) return 0;
    *out = (unsigned char*)e->data;
    *size = (unsigned int)e->len;
    e->data = NULL;
    return 1;
}

static void cod_emit_free(CodEmit* e) {
    if (!e) return;
    free(e->data);
    e->data = NULL;
}

#endif /* COD_EMIT_H */
