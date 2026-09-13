/*
 * hxfs_pak — clean-room Quake 1/2 PAK reader (published layout only).
 * Copyright (C) 2026 Helix-Public contributors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * Header (12 bytes): "PACK" + dirofs + dirlen.
 * Directory entry (64 bytes): name[56] + filepos + filelen.
 */

#include "pak_file.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PAK_NAME_MAX 56
#define PAK_VNAME_MAX 64
#define PAK_ENTRY_SIZE 64u
#define PAK_MAX_FILES 1000000u

typedef struct {
    char vname[PAK_VNAME_MAX];
    unsigned int offset;
    unsigned int size;
} PakEntry;

struct PakFile {
    unsigned char* bytes;
    size_t length;
    PakEntry* files;
    int count;
};

static unsigned int pak_rd_u32(const unsigned char* p) {
    return (unsigned int)p[0]
         | ((unsigned int)p[1] << 8)
         | ((unsigned int)p[2] << 16)
         | ((unsigned int)p[3] << 24);
}

static int pak_ascii_eq(const char* a, const char* b) {
    while (*a && *b) {
        unsigned char ca = (unsigned char)*a++;
        unsigned char cb = (unsigned char)*b++;
        if (tolower(ca) != tolower(cb)) return 0;
    }
    return *a == 0 && *b == 0;
}

static void pak_normalize(char* dst, size_t dstsz, const char* src) {
    size_t o = 0;
    if (!src) {
        if (dstsz) dst[0] = 0;
        return;
    }
    while (*src == '/' || *src == '\\') src++;
    while (src[0] == '.' && (src[1] == '/' || src[1] == '\\')) src += 2;
    while (*src && o + 1 < dstsz) {
        char c = *src++;
        if (c == '\\') c = '/';
        dst[o++] = (char)tolower((unsigned char)c);
    }
    dst[o] = 0;
}

static int pak_find_last(const PakFile* p, const char* name) {
    char key[PAK_VNAME_MAX];
    int i;
    if (!p || !name) return -1;
    pak_normalize(key, sizeof(key), name);
    if (!key[0]) return -1;
    for (i = p->count - 1; i >= 0; i--) {
        if (pak_ascii_eq(p->files[i].vname, key)) return i;
    }
    return -1;
}

static int pak_is_last_occurrence(const PakFile* p, int idx) {
    int j;
    for (j = idx + 1; j < p->count; j++) {
        if (pak_ascii_eq(p->files[j].vname, p->files[idx].vname)) return 0;
    }
    return 1;
}

static PakFile* pak_fail(PakFile* p) {
    pak_close(p);
    return NULL;
}

static PakFile* pak_parse_owned(unsigned char* bytes, size_t len) {
    PakFile* p;
    unsigned int dirofs;
    unsigned int dirlen;
    unsigned int num;
    unsigned int i;
    int out;

    p = (PakFile*)calloc(1, sizeof(PakFile));
    if (!p) {
        free(bytes);
        return NULL;
    }
    p->bytes = bytes;
    p->length = len;

    if (len < 12) return pak_fail(p);
    if (memcmp(bytes, "PACK", 4) != 0) return pak_fail(p);

    dirofs = pak_rd_u32(bytes + 4);
    dirlen = pak_rd_u32(bytes + 8);
    if (dirlen % PAK_ENTRY_SIZE != 0) return pak_fail(p);
    num = dirlen / PAK_ENTRY_SIZE;
    if (num > PAK_MAX_FILES) return pak_fail(p);
    if ((size_t)dirofs > len) return pak_fail(p);
    if ((size_t)dirlen > len - (size_t)dirofs) return pak_fail(p);

    p->files = (PakEntry*)calloc((size_t)num + 1u, sizeof(PakEntry));
    if (num > 0 && !p->files) return pak_fail(p);
    out = 0;

    for (i = 0; i < num; i++) {
        const unsigned char* e = bytes + (size_t)dirofs + (size_t)i * (size_t)PAK_ENTRY_SIZE;
        unsigned int filepos = pak_rd_u32(e + PAK_NAME_MAX);
        unsigned int filelen = pak_rd_u32(e + PAK_NAME_MAX + 4);
        PakEntry* E;
        char raw[PAK_VNAME_MAX];
        int n = 0;

        if ((size_t)filepos > len) return pak_fail(p);
        if ((size_t)filelen > len - (size_t)filepos) return pak_fail(p);

        while (n < PAK_NAME_MAX && e[n]) n++;
        if (n == 0) continue;
        memcpy(raw, e, (size_t)n);
        raw[n] = 0;
        pak_normalize(raw, sizeof(raw), raw);
        if (!raw[0]) continue;

        E = &p->files[out++];
        memset(E, 0, sizeof(*E));
        memcpy(E->vname, raw, sizeof(E->vname));
        E->offset = filepos;
        E->size = filelen;
    }

    p->count = out;
    return p;
}

PakFile* pak_open_mem(const void* bytes, size_t len) {
    unsigned char* copy;
    if (!bytes && len != 0) return NULL;
    copy = (unsigned char*)malloc(len ? len : 1);
    if (!copy) return NULL;
    if (len && bytes) memcpy(copy, bytes, len);
    return pak_parse_owned(copy, len);
}

PakFile* pak_open_file(const char* path) {
    FILE* f;
    long sz;
    unsigned char* buf;
    size_t n;

    if (!path || !path[0]) return NULL;
    f = fopen(path, "rb");
    if (!f) return NULL;
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return NULL;
    }
    sz = ftell(f);
    if (sz < 0) {
        fclose(f);
        return NULL;
    }
    if (fseek(f, 0, SEEK_SET) != 0) {
        fclose(f);
        return NULL;
    }
    buf = (unsigned char*)malloc(sz > 0 ? (size_t)sz : 1);
    if (!buf) {
        fclose(f);
        return NULL;
    }
    n = fread(buf, 1, (size_t)sz, f);
    fclose(f);
    if (n != (size_t)sz) {
        free(buf);
        return NULL;
    }
    return pak_parse_owned(buf, (size_t)sz);
}

void pak_close(PakFile* p) {
    if (!p) return;
    free(p->bytes);
    free(p->files);
    free(p);
}

int pak_contains(const PakFile* p, const char* name) {
    return pak_find_last(p, name) >= 0;
}

int pak_read(const PakFile* p, const char* name, unsigned char** out_buf, unsigned int* out_size) {
    int idx;
    unsigned char* buf;
    const PakEntry* E;
    if (!p || !out_buf || !out_size) return 0;
    idx = pak_find_last(p, name);
    if (idx < 0) return 0;
    E = &p->files[idx];
    buf = (unsigned char*)malloc(E->size ? E->size : 1);
    if (!buf) return 0;
    if (E->size) memcpy(buf, p->bytes + E->offset, E->size);
    *out_buf = buf;
    *out_size = E->size;
    return 1;
}

void pak_list(const PakFile* p, char*** names, unsigned int* count) {
    char** arr;
    unsigned int n = 0;
    unsigned int k = 0;
    int i;
    if (!names || !count) return;
    *names = NULL;
    *count = 0;
    if (!p) return;
    for (i = 0; i < p->count; i++) {
        if (pak_is_last_occurrence(p, i)) n++;
    }
    arr = (char**)malloc(n ? n * sizeof(char*) : sizeof(char*));
    if (!arr) return;
    for (i = 0; i < p->count; i++) {
        size_t len;
        if (!pak_is_last_occurrence(p, i)) continue;
        len = strlen(p->files[i].vname);
        arr[k] = (char*)malloc(len + 1);
        if (!arr[k]) {
            pak_free_list(arr, k);
            return;
        }
        memcpy(arr[k], p->files[i].vname, len + 1);
        k++;
    }
    *names = arr;
    *count = k;
}

void pak_free_list(char** names, unsigned int count) {
    unsigned int i;
    if (!names) return;
    for (i = 0; i < count; i++) free(names[i]);
    free(names);
}
