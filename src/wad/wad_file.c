/*
 * hxfs_wad — clean-room WAD1/WAD2/WAD3 reader (published layout only).
 * Copyright (C) 2026 Helix-Public contributors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include "wad_file.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define WAD_VNAME_MAX 80
#define WAD_RAWNAME_MAX 16
#define WAD_MAX_LUMPS 1000000u

enum {
    WAD_KIND_WAD1 = 1,
    WAD_KIND_WAD2 = 2,
    WAD_KIND_WAD3 = 3
};

/* Quake / GoldSrc directory type bytes (published). */
enum {
    WAD_TYPE_PALETTE = 64,
    WAD_TYPE_QPIC = 66,
    WAD_TYPE_MIPTEX = 67,
    WAD_TYPE_MIPTEX_D = 68
};

typedef struct {
    char vname[WAD_VNAME_MAX];
    unsigned int offset;
    unsigned int size;
} WadLump;

struct WadFile {
    unsigned char* bytes;
    size_t length;
    WadLump* lumps;
    int count;
};

static unsigned int wad_rd_u32(const unsigned char* p) {
    return (unsigned int)p[0]
         | ((unsigned int)p[1] << 8)
         | ((unsigned int)p[2] << 16)
         | ((unsigned int)p[3] << 24);
}

static int wad_ascii_eq(const char* a, const char* b) {
    while (*a && *b) {
        unsigned char ca = (unsigned char)*a++;
        unsigned char cb = (unsigned char)*b++;
        if (tolower(ca) != tolower(cb)) return 0;
    }
    return *a == 0 && *b == 0;
}

static void wad_normalize(char* dst, size_t dstsz, const char* src) {
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

static void wad_copy_raw_name(char* dst, size_t dstsz, const unsigned char* src, int n) {
    int i = 0;
    int end = 0;
    if (dstsz == 0) return;
    while (i < n && src[i]) {
        dst[i] = (char)tolower((unsigned char)src[i]);
        i++;
    }
    end = i;
    while (end > 0 && dst[end - 1] == ' ') end--;
    if ((size_t)end >= dstsz) end = (int)dstsz - 1;
    dst[end] = 0;
}

static int wad_is_map_marker(const char* n) {
    size_t len;
    if (!n) return 0;
    len = strlen(n);
    if (len == 4 && n[0] == 'e' && n[2] == 'm' && isdigit((unsigned char)n[1]) && isdigit((unsigned char)n[3])) {
        return 1;
    }
    if (len == 5 && n[0] == 'm' && n[1] == 'a' && n[2] == 'p' && isdigit((unsigned char)n[3]) && isdigit((unsigned char)n[4])) {
        return 1;
    }
    return 0;
}

static int wad_is_map_data_lump(const char* n) {
    static const char* k[] = {
        "things", "linedefs", "sidedefs", "vertexes", "segs", "ssectors",
        "nodes", "sectors", "reject", "blockmap", "behavior", "scripts",
        "leafs", "textmap", "znodes", "endmap", "dialogue",
        "gl_vert", "gl_segs", "gl_ssect", "gl_nodes", "gl_pvs",
        NULL
    };
    int i;
    for (i = 0; k[i]; i++) {
        if (wad_ascii_eq(n, k[i])) return 1;
    }
    return 0;
}

static int wad_is_palette_name(const char* n) {
    return wad_ascii_eq(n, "palette") || wad_ascii_eq(n, "pal");
}

/* Returns namespace prefix, or NULL if not a start marker. */
static const char* wad_ns_start(const char* n) {
    if (wad_ascii_eq(n, "f_start") || wad_ascii_eq(n, "ff_start")) return "flats";
    if (wad_ascii_eq(n, "p_start") || wad_ascii_eq(n, "pp_start")) return "patches";
    if (wad_ascii_eq(n, "s_start") || wad_ascii_eq(n, "ss_start")) return "sprites";
    if (wad_ascii_eq(n, "tx_start")) return "textures";
    return NULL;
}

static int wad_is_ns_end(const char* n) {
    return wad_ascii_eq(n, "f_end") || wad_ascii_eq(n, "ff_end")
        || wad_ascii_eq(n, "p_end") || wad_ascii_eq(n, "pp_end")
        || wad_ascii_eq(n, "s_end") || wad_ascii_eq(n, "ss_end")
        || wad_ascii_eq(n, "tx_end");
}

static int wad_find_last(const WadFile* w, const char* name) {
    char key[WAD_VNAME_MAX];
    int i;
    if (!w || !name) return -1;
    wad_normalize(key, sizeof(key), name);
    if (!key[0]) return -1;
    for (i = w->count - 1; i >= 0; i--) {
        if (wad_ascii_eq(w->lumps[i].vname, key)) return i;
    }
    return -1;
}

static int wad_is_last_occurrence(const WadFile* w, int idx) {
    int j;
    for (j = idx + 1; j < w->count; j++) {
        if (wad_ascii_eq(w->lumps[j].vname, w->lumps[idx].vname)) return 0;
    }
    return 1;
}

static WadFile* wad_fail(WadFile* w) {
    wad_close(w);
    return NULL;
}

static WadFile* wad_parse_owned(unsigned char* bytes, size_t len) {
    WadFile* w;
    unsigned int num;
    unsigned int dirofs;
    unsigned int entsize;
    int kind;
    unsigned int i;
    const char* ns = NULL;
    char mapname[WAD_RAWNAME_MAX + 1];
    int out;

    w = (WadFile*)calloc(1, sizeof(WadFile));
    if (!w) {
        free(bytes);
        return NULL;
    }
    w->bytes = bytes;
    w->length = len;
    mapname[0] = 0;

    if (len < 12) return wad_fail(w);

    if (memcmp(bytes, "IWAD", 4) == 0 || memcmp(bytes, "PWAD", 4) == 0) {
        kind = WAD_KIND_WAD1;
        entsize = 16;
    } else if (memcmp(bytes, "WAD2", 4) == 0) {
        kind = WAD_KIND_WAD2;
        entsize = 32;
    } else if (memcmp(bytes, "WAD3", 4) == 0) {
        kind = WAD_KIND_WAD3;
        entsize = 32;
    } else {
        return wad_fail(w);
    }

    num = wad_rd_u32(bytes + 4);
    dirofs = wad_rd_u32(bytes + 8);
    if (num > WAD_MAX_LUMPS) return wad_fail(w);
    if ((size_t)dirofs > len) return wad_fail(w);
    if (num > 0 && (len - (size_t)dirofs) / (size_t)entsize < (size_t)num) return wad_fail(w);

    w->lumps = (WadLump*)calloc((size_t)num + 1u, sizeof(WadLump));
    if (num > 0 && !w->lumps) return wad_fail(w);
    out = 0;

    for (i = 0; i < num; i++) {
        const unsigned char* e = bytes + (size_t)dirofs + (size_t)i * (size_t)entsize;
        unsigned int filepos;
        unsigned int size;
        unsigned int disksize;
        unsigned char type = 0;
        unsigned char compression = 0;
        char raw[WAD_RAWNAME_MAX + 1];
        WadLump* L;
        const char* started;

        filepos = wad_rd_u32(e + 0);
        if (kind == WAD_KIND_WAD1) {
            size = wad_rd_u32(e + 4);
            disksize = size;
            wad_copy_raw_name(raw, sizeof(raw), e + 8, 8);
        } else {
            disksize = wad_rd_u32(e + 4);
            size = wad_rd_u32(e + 8);
            type = e[12];
            compression = e[13];
            wad_copy_raw_name(raw, sizeof(raw), e + 16, 16);
        }

        if ((size_t)filepos > len) return wad_fail(w);
        if ((size_t)disksize > len - (size_t)filepos) return wad_fail(w);
        if (compression != 0 && kind != WAD_KIND_WAD1) continue;

        if (!raw[0]) continue;

        if (kind == WAD_KIND_WAD1) {
            started = wad_ns_start(raw);
            if (started) {
                ns = started;
                mapname[0] = 0;
                continue;
            }
            if (wad_is_ns_end(raw)) {
                ns = NULL;
                continue;
            }
            if (wad_is_map_marker(raw)) {
                ns = NULL;
                memcpy(mapname, raw, sizeof(mapname));
                L = &w->lumps[out++];
                memset(L, 0, sizeof(*L));
                snprintf(L->vname, sizeof(L->vname), "maps/%s", raw);
                L->offset = filepos;
                L->size = size;
                continue;
            }
            if (mapname[0] && wad_is_map_data_lump(raw)) {
                L = &w->lumps[out++];
                memset(L, 0, sizeof(*L));
                snprintf(L->vname, sizeof(L->vname), "maps/%s/%s", mapname, raw);
                L->offset = filepos;
                L->size = size;
                continue;
            }
            mapname[0] = 0;
            L = &w->lumps[out++];
            memset(L, 0, sizeof(*L));
            if (ns) {
                snprintf(L->vname, sizeof(L->vname), "%s/%s", ns, raw);
            } else {
                snprintf(L->vname, sizeof(L->vname), "%s", raw);
            }
            L->offset = filepos;
            L->size = size;
            continue;
        }

        /* WAD2 / WAD3 */
        L = &w->lumps[out++];
        memset(L, 0, sizeof(*L));
        if (type == WAD_TYPE_QPIC) {
            snprintf(L->vname, sizeof(L->vname), "gfx/%s", raw);
        } else if (type == WAD_TYPE_MIPTEX || type == WAD_TYPE_MIPTEX_D) {
            snprintf(L->vname, sizeof(L->vname), "textures/%s", raw);
        } else if (type == WAD_TYPE_PALETTE && wad_is_palette_name(raw)) {
            snprintf(L->vname, sizeof(L->vname), "gfx/palette");
        } else {
            snprintf(L->vname, sizeof(L->vname), "%s", raw);
        }
        L->offset = filepos;
        L->size = size;
    }

    w->count = out;
    return w;
}

WadFile* wad_open_mem(const void* bytes, size_t len) {
    unsigned char* copy;
    if (!bytes && len != 0) return NULL;
    copy = (unsigned char*)malloc(len ? len : 1);
    if (!copy) return NULL;
    if (len && bytes) memcpy(copy, bytes, len);
    return wad_parse_owned(copy, len);
}

WadFile* wad_open_file(const char* path) {
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
    return wad_parse_owned(buf, (size_t)sz);
}

void wad_close(WadFile* w) {
    if (!w) return;
    free(w->bytes);
    free(w->lumps);
    free(w);
}

int wad_contains(const WadFile* w, const char* name) {
    return wad_find_last(w, name) >= 0;
}

int wad_read(const WadFile* w, const char* name, unsigned char** out_buf, unsigned int* out_size) {
    int idx;
    unsigned char* buf;
    const WadLump* L;
    if (!w || !out_buf || !out_size) return 0;
    idx = wad_find_last(w, name);
    if (idx < 0) return 0;
    L = &w->lumps[idx];
    buf = (unsigned char*)malloc(L->size ? L->size : 1);
    if (!buf) return 0;
    if (L->size) memcpy(buf, w->bytes + L->offset, L->size);
    *out_buf = buf;
    *out_size = L->size;
    return 1;
}

void wad_list(const WadFile* w, char*** names, unsigned int* count) {
    char** arr;
    unsigned int n = 0;
    unsigned int k = 0;
    int i;
    if (!names || !count) return;
    *names = NULL;
    *count = 0;
    if (!w) return;
    for (i = 0; i < w->count; i++) {
        if (wad_is_last_occurrence(w, i)) n++;
    }
    arr = (char**)malloc(n ? n * sizeof(char*) : sizeof(char*));
    if (!arr) return;
    for (i = 0; i < w->count; i++) {
        size_t len;
        if (!wad_is_last_occurrence(w, i)) continue;
        len = strlen(w->lumps[i].vname);
        arr[k] = (char*)malloc(len + 1);
        if (!arr[k]) {
            wad_free_list(arr, k);
            return;
        }
        memcpy(arr[k], w->lumps[i].vname, len + 1);
        k++;
    }
    *names = arr;
    *count = k;
}

void wad_free_list(char** names, unsigned int count) {
    unsigned int i;
    if (!names) return;
    for (i = 0; i < count; i++) free(names[i]);
    free(names);
}
