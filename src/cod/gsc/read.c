/*
 * CoD1 GSC reader. Assignments and simple calls, not a VM.
 * Copyright (C) 2026 Helix-Public contributors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include "gsc/read.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int ident_char(char c) {
    return isalnum((unsigned char)c) || c == '_' || c == '\\' || c == ':';
}

static int push_assign(GscFile* f, const GscAssign* a) {
    GscAssign* grown = (GscAssign*)realloc(f->assigns, (size_t)(f->nassigns + 1) * sizeof(GscAssign));
    if (!grown) return 0;
    f->assigns = grown;
    f->assigns[f->nassigns++] = *a;
    return 1;
}

static int push_call(GscFile* f, const GscCall* c) {
    GscCall* grown = (GscCall*)realloc(f->calls, (size_t)(f->ncalls + 1) * sizeof(GscCall));
    if (!grown) return 0;
    f->calls = grown;
    f->calls[f->ncalls++] = *c;
    return 1;
}

static void read_string(const char* text, size_t n, size_t* i, char* out, size_t cap) {
    size_t k = 0;
    if (*i < n && text[*i] == '"') (*i)++;
    while (*i < n && text[*i] != '"') {
        char c = text[(*i)++];
        if (c == '\\' && *i < n) c = text[(*i)++];
        if (k + 1 < cap) out[k++] = c;
    }
    if (*i < n && text[*i] == '"') (*i)++;
    out[k] = '\0';
}

int gsc_read(const void* data, size_t size, GscFile* out) {
    const char* text;
    size_t i = 0;
    if (!out) return 0;
    memset(out, 0, sizeof(*out));
    if (!data) return 0;
    text = (const char*)data;
    while (i < size) {
        if (text[i] == '/' && i + 1 < size && text[i + 1] == '/') {
            while (i < size && text[i] != '\n') i++;
            continue;
        }
        if (text[i] == '/' && i + 1 < size && text[i + 1] == '*') {
            i += 2;
            while (i + 1 < size && !(text[i] == '*' && text[i + 1] == '/')) i++;
            if (i + 1 < size) i += 2;
            continue;
        }
        if (i + 5 <= size && memcmp(text + i, "game[", 5) == 0 &&
            (i == 0 || !ident_char(text[i - 1]))) {
            GscAssign a;
            memset(&a, 0, sizeof(a));
            i += 5;
            while (i < size && isspace((unsigned char)text[i])) i++;
            if (i < size && text[i] == '"') read_string(text, size, &i, a.key, sizeof(a.key));
            while (i < size && (text[i] == ']' || isspace((unsigned char)text[i]))) i++;
            if (i < size && text[i] == '=') {
                i++;
                while (i < size && isspace((unsigned char)text[i])) i++;
                if (i < size && text[i] == '"') {
                    a.is_string = 1;
                    a.literal = 1;
                    read_string(text, size, &i, a.value, sizeof(a.value));
                }
            }
            if (a.key[0] && a.value[0] && !push_assign(out, &a)) {
                gsc_read_free(out);
                return 0;
            }
            continue;
        }
        if (text[i] == '"') {
            i++;
            while (i < size && text[i] != '"') {
                if (text[i] == '\\') i++;
                i++;
            }
            if (i < size) i++;
            continue;
        }
        if (i + 6 <= size && memcmp(text + i, "level.", 6) == 0 &&
            (i == 0 || !ident_char(text[i - 1]))) {
            GscAssign a;
            size_t k = 0;
            memset(&a, 0, sizeof(a));
            i += 6;
            while (i < size && (isalnum((unsigned char)text[i]) || text[i] == '_')) {
                if (k + 1 < sizeof(a.key)) a.key[k++] = text[i];
                i++;
            }
            a.key[k] = '\0';
            while (i < size && isspace((unsigned char)text[i])) i++;
            if (i < size && text[i] == '=') {
                i++;
                while (i < size && isspace((unsigned char)text[i])) i++;
                if (i < size && text[i] == '"') {
                    a.is_string = 1;
                    a.literal = 1;
                    read_string(text, size, &i, a.value, sizeof(a.value));
                } else if (i < size && (isdigit((unsigned char)text[i]) || text[i] == '-' || text[i] == '.')) {
                    k = 0;
                    a.literal = 1;
                    while (i < size && (isdigit((unsigned char)text[i]) || text[i] == '-' || text[i] == '.')) {
                        if (k + 1 < sizeof(a.value)) a.value[k++] = text[i];
                        i++;
                    }
                    a.value[k] = '\0';
                }
            }
            if (a.key[0] && !push_assign(out, &a)) {
                gsc_read_free(out);
                return 0;
            }
            continue;
        }
        if (isalpha((unsigned char)text[i]) || text[i] == '_' || text[i] == '\\') {
            GscCall call;
            size_t k = 0;
            size_t start = i;
            memset(&call, 0, sizeof(call));
            while (i < size && ident_char(text[i])) {
                if (k + 1 < sizeof(call.name)) call.name[k++] = text[i];
                i++;
            }
            call.name[k] = '\0';
            while (i < size && isspace((unsigned char)text[i])) i++;
            if (i < size && text[i] == '(' && call.name[0]) {
                i++;
                while (i < size && isspace((unsigned char)text[i])) i++;
                if (i < size && text[i] == '"') read_string(text, size, &i, call.arg, sizeof(call.arg));
                if (!push_call(out, &call)) {
                    gsc_read_free(out);
                    return 0;
                }
            } else {
                i = start + 1;
            }
            continue;
        }
        i++;
    }
    return 1;
}

void gsc_read_free(GscFile* file) {
    if (!file) return;
    free(file->assigns);
    free(file->calls);
    file->assigns = NULL;
    file->calls = NULL;
    file->nassigns = 0;
    file->ncalls = 0;
}
