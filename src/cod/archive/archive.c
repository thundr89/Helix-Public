/*
 * Multi-zip plus loose-file storage for the modular hxfs_cod plugin.
 * Copyright (C) 2026 Helix-Public contributors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include "archive/archive.h"
#include "zip_file.h"
#include "cod_util.h"

#include <stdlib.h>
#include <string.h>
#include <zlib.h>

typedef struct CodBlob {
    char* name;
    unsigned char* data;
    unsigned int size;
} CodBlob;

struct CodArchive {
    ZipFile** zips;
    int zcount;
    int zcap;
    CodBlob* blobs;
    int bcount;
    int bcap;
};

CodArchive* cod_archive_create(void) {
    return (CodArchive*)calloc(1, sizeof(CodArchive));
}

void cod_archive_clear(CodArchive* a) {
    int i;
    if (!a) return;
    for (i = 0; i < a->zcount; i++) {
        if (a->zips[i]) zip_close(a->zips[i]);
    }
    free(a->zips);
    a->zips = NULL;
    a->zcount = 0;
    a->zcap = 0;

    for (i = 0; i < a->bcount; i++) {
        free(a->blobs[i].name);
        free(a->blobs[i].data);
    }
    free(a->blobs);
    a->blobs = NULL;
    a->bcount = 0;
    a->bcap = 0;
}

void cod_archive_destroy(CodArchive* a) {
    if (!a) return;
    cod_archive_clear(a);
    free(a);
}

int cod_archive_add_zip(CodArchive* a, const char* path) {
    ZipFile* next;
    if (!a || !path || !path[0]) return 0;
    next = zip_open_file(path);
    if (!next) return 0;
    if (a->zcount == a->zcap) {
        int ncap = a->zcap ? a->zcap * 2 : 4;
        ZipFile** n = (ZipFile**)realloc(a->zips, (size_t)ncap * sizeof(ZipFile*));
        if (!n) {
            zip_close(next);
            return 0;
        }
        a->zips = n;
        a->zcap = ncap;
    }
    a->zips[a->zcount++] = next;
    return 1;
}

int cod_archive_add_blob(CodArchive* a, const char* vpath, const void* data, unsigned int size) {
    unsigned char* copy;
    int i;
    if (!a || !vpath || !vpath[0] || (!data && size)) return 0;
    copy = (unsigned char*)malloc(size ? size : 1);
    if (!copy) return 0;
    if (size) memcpy(copy, data, size);

    for (i = 0; i < a->bcount; i++) {
        if (cod_strcasecmp(a->blobs[i].name, vpath) == 0) {
            free(a->blobs[i].data);
            a->blobs[i].data = copy;
            a->blobs[i].size = size;
            return 1;
        }
    }

    if (a->bcount == a->bcap) {
        int ncap = a->bcap ? a->bcap * 2 : 4;
        CodBlob* n = (CodBlob*)realloc(a->blobs, (size_t)ncap * sizeof(CodBlob));
        if (!n) {
            free(copy);
            return 0;
        }
        a->blobs = n;
        a->bcap = ncap;
    }
    a->blobs[a->bcount].name = cod_strdup(vpath);
    if (!a->blobs[a->bcount].name) {
        free(copy);
        return 0;
    }
    a->blobs[a->bcount].data = copy;
    a->blobs[a->bcount].size = size;
    a->bcount++;
    return 1;
}

static int blob_index(const CodArchive* a, const char* name) {
    int i;
    for (i = a->bcount - 1; i >= 0; i--) {
        if (cod_strcasecmp(a->blobs[i].name, name) == 0) return i;
    }
    return -1;
}

int cod_archive_contains(const CodArchive* a, const char* name) {
    int i;
    if (!a || !name) return 0;
    if (blob_index(a, name) >= 0) return 1;
    for (i = a->zcount - 1; i >= 0; i--) {
        if (zip_contains(a->zips[i], name)) return 1;
    }
    return 0;
}

int cod_archive_checksum(const CodArchive* a, const char* name, unsigned int* crc) {
    int bi, i;
    if (!a || !name || !crc) return 0;
    bi = blob_index(a, name);
    if (bi >= 0) {
        const unsigned char* data = a->blobs[bi].size ? a->blobs[bi].data : (const unsigned char*)"";
        *crc = (unsigned int)crc32(0L, data, a->blobs[bi].size);
        return 1;
    }
    for (i = a->zcount - 1; i >= 0; i--) {
        if (zip_checksum(a->zips[i], name, crc)) return 1;
    }
    return 0;
}

int cod_archive_read(const CodArchive* a, const char* name,
                     unsigned char** out_buf, unsigned int* out_size) {
    int bi, i;
    if (!a || !name || !out_buf || !out_size) return 0;
    bi = blob_index(a, name);
    if (bi >= 0) {
        unsigned char* copy = (unsigned char*)malloc(a->blobs[bi].size ? a->blobs[bi].size : 1);
        if (!copy) return 0;
        if (a->blobs[bi].size) memcpy(copy, a->blobs[bi].data, a->blobs[bi].size);
        *out_buf = copy;
        *out_size = a->blobs[bi].size;
        return 1;
    }
    for (i = a->zcount - 1; i >= 0; i--) {
        if (zip_read(a->zips[i], name, out_buf, out_size)) return 1;
    }
    return 0;
}

static int cmp_strptr(const void* a, const void* b) {
    return strcmp(*(const char* const*)a, *(const char* const*)b);
}

void cod_archive_list(const CodArchive* a, char*** names, unsigned int* count) {
    char** items = NULL;
    unsigned int total = 0, cap = 0, unique = 0, j;
    int i;

    if (!names || !count) return;
    *names = NULL;
    *count = 0;
    if (!a) return;

    for (i = 0; i < a->bcount; i++) {
        char* s;
        if (total == cap) {
            unsigned int ncap = cap ? cap * 2 : 16;
            char** n = (char**)realloc(items, ncap * sizeof(char*));
            if (!n) goto fail;
            items = n;
            cap = ncap;
        }
        s = cod_strdup(a->blobs[i].name);
        if (!s) goto fail;
        items[total++] = s;
    }

    for (i = a->zcount - 1; i >= 0; i--) {
        char** tnames = NULL;
        unsigned int tcount = 0;
        zip_list(a->zips[i], &tnames, &tcount);
        for (j = 0; j < tcount; j++) {
            if (total == cap) {
                unsigned int ncap = cap ? cap * 2 : 16;
                char** n = (char**)realloc(items, ncap * sizeof(char*));
                if (!n) {
                    zip_free_list(tnames, tcount);
                    goto fail;
                }
                items = n;
                cap = ncap;
            }
            items[total++] = tnames[j];
            tnames[j] = NULL;
        }
        zip_free_list(tnames, tcount);
    }

    if (total == 0) return;
    qsort(items, total, sizeof(char*), cmp_strptr);
    for (j = 0; j < total; j++) {
        if (j > 0 && strcmp(items[j], items[unique - 1]) == 0) {
            free(items[j]);
        } else {
            items[unique++] = items[j];
        }
    }
    *names = items;
    *count = unique;
    return;

fail:
    for (j = 0; j < total; j++) free(items[j]);
    free(items);
}
