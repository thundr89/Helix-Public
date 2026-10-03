/*
 * hxfs_cod2003 — Helix IAssetPlugin for Call of Duty 1 (2003) PK3 & IBSP v59 assets.
 * Copyright (C) 2026 Helix-Public contributors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include "iasset_plugin.h"
#include "zip_file.h"
#include "cod_bsp.h"
#include "cod_xmodel.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#define HXFS_EXPORT __declspec(dllexport)
#else
#define HXFS_EXPORT __attribute__((visibility("default")))
#endif

#if defined(_MSC_VER)
#define hxfs_strcasecmp _stricmp
#define hxfs_strdup _strdup
#else
#include <strings.h>
#define hxfs_strcasecmp strcasecmp
#define hxfs_strdup strdup
#endif

#ifdef _WIN32
#include <windows.h>
#else
#include <dirent.h>
#endif

typedef struct {
    char* name;
    unsigned char* data;
    unsigned int size;
} StandaloneBsp;

typedef struct {
    hxAssetPlugin api;
    ZipFile** files;
    int count;
    int cap;

    StandaloneBsp* bsps;
    int bsp_count;
    int bsp_cap;

    char** cached_list;
    unsigned int cached_count;
} HxfsCod2003;

static void hxfs_close_all(HxfsCod2003* p) {
    int i;
    if (!p) return;
    for (i = 0; i < p->count; i++) {
        if (p->files[i]) {
            zip_close(p->files[i]);
        }
    }
    free(p->files);
    p->files = NULL;
    p->count = 0;
    p->cap = 0;

    for (i = 0; i < p->bsp_count; i++) {
        free(p->bsps[i].name);
        free(p->bsps[i].data);
    }
    free(p->bsps);
    p->bsps = NULL;
    p->bsp_count = 0;
    p->bsp_cap = 0;

    if (p->cached_list) {
        for (i = 0; i < (int)p->cached_count; i++) {
            free(p->cached_list[i]);
        }
        free(p->cached_list);
        p->cached_list = NULL;
        p->cached_count = 0;
    }
}

static int hxfs_add_zip(HxfsCod2003* p, const char* path) {
    ZipFile* next = zip_open_file(path);
    if (!next) return 0;
    if (p->count == p->cap) {
        int ncap = p->cap ? p->cap * 2 : 4;
        ZipFile** n = (ZipFile**)realloc(p->files, ncap * sizeof(ZipFile*));
        if (!n) {
            zip_close(next);
            return 0;
        }
        p->files = n;
        p->cap = ncap;
    }
    p->files[p->count++] = next;
    return 1;
}

static int hxfs_add_standalone_bsp(HxfsCod2003* p, const char* path) {
    FILE* f = fopen(path, "rb");
    long sz;
    unsigned char* data;
    const char* base;
    char vpath[256];

    if (!f) return 0;
    fseek(f, 0, SEEK_END);
    sz = ftell(f);
    if (sz < 8) {
        fclose(f);
        return 0;
    }
    fseek(f, 0, SEEK_SET);
    data = (unsigned char*)malloc((size_t)sz);
    if (!data) {
        fclose(f);
        return 0;
    }
    if (fread(data, 1, (size_t)sz, f) != (size_t)sz || !cod_bsp_is_valid(data, (size_t)sz)) {
        free(data);
        fclose(f);
        return 0;
    }
    fclose(f);

    base = strrchr(path, '/');
    if (!base) base = strrchr(path, '\\');
    base = base ? base + 1 : path;
    snprintf(vpath, sizeof(vpath), "maps/%s", base);

    if (p->bsp_count == p->bsp_cap) {
        int ncap = p->bsp_cap ? p->bsp_cap * 2 : 4;
        StandaloneBsp* n = (StandaloneBsp*)realloc(p->bsps, ncap * sizeof(StandaloneBsp));
        if (!n) {
            free(data);
            return 0;
        }
        p->bsps = n;
        p->bsp_cap = ncap;
    }

    p->bsps[p->bsp_count].name = hxfs_strdup(vpath);
    p->bsps[p->bsp_count].data = data;
    p->bsps[p->bsp_count].size = (unsigned int)sz;
    p->bsp_count++;
    return 1;
}

static int hxfs_open(hxAssetPlugin* self, const char* path) {
    HxfsCod2003* p = (HxfsCod2003*)self;
    size_t len;
    if (!p) return 0;
    hxfs_close_all(p);
    if (!path || !path[0]) return 0;

    len = strlen(path);
    if (len >= 4 && hxfs_strcasecmp(path + len - 4, ".bsp") == 0) {
        return hxfs_add_standalone_bsp(p, path);
    }
    return hxfs_add_zip(p, path);
}

static int hxfs_open_many(hxAssetPlugin* self, const char** paths, int count) {
    HxfsCod2003* p = (HxfsCod2003*)self;
    int i, opened = 0;
    if (!p) return 0;
    hxfs_close_all(p);
    if (!paths) return 0;
    for (i = 0; i < count; i++) {
        if (paths[i] && paths[i][0]) {
            size_t len = strlen(paths[i]);
            if (len >= 4 && hxfs_strcasecmp(paths[i] + len - 4, ".bsp") == 0) {
                if (hxfs_add_standalone_bsp(p, paths[i])) opened++;
            } else {
                if (hxfs_add_zip(p, paths[i])) opened++;
            }
        }
    }
    return opened;
}

static int cmp_str(const void* a, const void* b) {
    return strcmp(*(const char**)a, *(const char**)b);
}

static int hxfs_open_dir(hxAssetPlugin* self, const char* dir_path) {
    HxfsCod2003* p = (HxfsCod2003*)self;
    int opened = 0;
    char** paths = NULL;
    int pcount = 0, pcap = 0;
    int i;

    if (!p) return 0;
    hxfs_close_all(p);
    if (!dir_path || !dir_path[0]) return 0;

#ifdef _WIN32
    {
        WIN32_FIND_DATAA fd;
        HANDLE hFind;
        char search[1024];
        snprintf(search, sizeof(search), "%s/*.pk3", dir_path);
        hFind = FindFirstFileA(search, &fd);
        if (hFind != INVALID_HANDLE_VALUE) {
            do {
                if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
                    if (pcount == pcap) {
                        pcap = pcap ? pcap * 2 : 16;
                        paths = (char**)realloc(paths, pcap * sizeof(char*));
                    }
                    paths[pcount] = (char*)malloc(strlen(dir_path) + strlen(fd.cFileName) + 2);
                    sprintf(paths[pcount], "%s/%s", dir_path, fd.cFileName);
                    pcount++;
                }
            } while (FindNextFileA(hFind, &fd));
            FindClose(hFind);
        }
    }
#else
    {
        DIR* dir = opendir(dir_path);
        if (dir) {
            struct dirent* ent;
            while ((ent = readdir(dir)) != NULL) {
                size_t len = strlen(ent->d_name);
                size_t extlen = strlen(".pk3");
                if (len >= extlen && hxfs_strcasecmp(ent->d_name + len - extlen, ".pk3") == 0) {
                    if (pcount == pcap) {
                        pcap = pcap ? pcap * 2 : 16;
                        paths = (char**)realloc(paths, pcap * sizeof(char*));
                    }
                    paths[pcount] = (char*)malloc(strlen(dir_path) + strlen(ent->d_name) + 2);
                    sprintf(paths[pcount], "%s/%s", dir_path, ent->d_name);
                    pcount++;
                }
            }
            closedir(dir);
        }
    }
#endif

    if (pcount > 0) {
        qsort(paths, pcount, sizeof(char*), cmp_str);
        for (i = 0; i < pcount; i++) {
            if (hxfs_add_zip(p, paths[i])) opened++;
            free(paths[i]);
        }
        free(paths);
    }

    return opened;
}

/* Helper to locate matching BSP for a requested virtual .hxmap name */
static int find_bsp_source(HxfsCod2003* p, const char* hxmap_name,
                           unsigned char** out_bsp_buf, unsigned int* out_bsp_sz,
                           char* out_map_name, size_t out_name_cap) {
    char bsp_try1[256];
    char bsp_try2[256];
    char bsp_try3[256];
    size_t len;
    int i;
    const char* base;

    if (!hxmap_name) return 0;
    len = strlen(hxmap_name);
    if (len < 6 || hxfs_strcasecmp(hxmap_name + len - 6, ".hxmap") != 0) return 0;

    /* Base name without .hxmap */
    base = strrchr(hxmap_name, '/');
    if (!base) base = strrchr(hxmap_name, '\\');
    base = base ? base + 1 : hxmap_name;

    if (out_map_name && out_name_cap > 0) {
        size_t blen = strlen(base);
        size_t cplen = blen >= 6 ? blen - 6 : blen;
        if (cplen >= out_name_cap) cplen = out_name_cap - 1;
        memcpy(out_map_name, base, cplen);
        out_map_name[cplen] = '\0';
    }

    /* Try direct .bsp instead of .hxmap */
    snprintf(bsp_try1, sizeof(bsp_try1), "%.*s.bsp", (int)(len - 6), hxmap_name);
    /* Try with mp/ prefix (CoD mp maps are often in maps/mp/) */
    if (strncmp(hxmap_name, "maps/", 5) == 0) {
        snprintf(bsp_try2, sizeof(bsp_try2), "maps/mp/%.*s.bsp", (int)(len - 11), hxmap_name + 5);
    } else {
        snprintf(bsp_try2, sizeof(bsp_try2), "maps/mp/%.*s.bsp", (int)(len - 6), hxmap_name);
    }
    snprintf(bsp_try3, sizeof(bsp_try3), "maps/%.*s.bsp", (int)(len - 6), base);

    /* 1. Check standalone BSPs */
    for (i = 0; i < p->bsp_count; i++) {
        if (hxfs_strcasecmp(p->bsps[i].name, bsp_try1) == 0 ||
            hxfs_strcasecmp(p->bsps[i].name, bsp_try2) == 0 ||
            hxfs_strcasecmp(p->bsps[i].name, bsp_try3) == 0) {
            *out_bsp_buf = (unsigned char*)malloc(p->bsps[i].size);
            if (!*out_bsp_buf) return 0;
            memcpy(*out_bsp_buf, p->bsps[i].data, p->bsps[i].size);
            *out_bsp_sz = p->bsps[i].size;
            return 1;
        }
    }

    /* 2. Check loaded ZIPs (PK3s) */
    for (i = p->count - 1; i >= 0; i--) {
        if (zip_contains(p->files[i], bsp_try1)) {
            return zip_read(p->files[i], bsp_try1, out_bsp_buf, out_bsp_sz);
        }
        if (zip_contains(p->files[i], bsp_try2)) {
            return zip_read(p->files[i], bsp_try2, out_bsp_buf, out_bsp_sz);
        }
        if (zip_contains(p->files[i], bsp_try3)) {
            return zip_read(p->files[i], bsp_try3, out_bsp_buf, out_bsp_sz);
        }
    }

    return 0;
}

static int hxfs_contains(hxAssetPlugin* self, const char* name) {
    HxfsCod2003* p = (HxfsCod2003*)self;
    int i;
    size_t len;
    if (!p || !name) return 0;
    len = strlen(name);

    /* Check if asking for virtual .hxmap from a contained .bsp */
    if (len >= 6 && hxfs_strcasecmp(name + len - 6, ".hxmap") == 0) {
        char bsp1[256], bsp2[256], bsp3[256];
        const char* base = strrchr(name, '/');
        if (!base) base = strrchr(name, '\\');
        base = base ? base + 1 : name;

        snprintf(bsp1, sizeof(bsp1), "%.*s.bsp", (int)(len - 6), name);
        if (strncmp(name, "maps/", 5) == 0) {
            snprintf(bsp2, sizeof(bsp2), "maps/mp/%.*s.bsp", (int)(len - 11), name + 5);
        } else {
            snprintf(bsp2, sizeof(bsp2), "maps/mp/%.*s.bsp", (int)(len - 6), name);
        }
        snprintf(bsp3, sizeof(bsp3), "maps/%.*s.bsp", (int)(len - 6), base);

        for (i = 0; i < p->bsp_count; i++) {
            if (hxfs_strcasecmp(p->bsps[i].name, bsp1) == 0 ||
                hxfs_strcasecmp(p->bsps[i].name, bsp2) == 0 ||
                hxfs_strcasecmp(p->bsps[i].name, bsp3) == 0) {
                return 1;
            }
        }
        for (i = p->count - 1; i >= 0; i--) {
            if (zip_contains(p->files[i], bsp1) ||
                zip_contains(p->files[i], bsp2) ||
                zip_contains(p->files[i], bsp3)) {
                return 1;
            }
        }
    }

    /* Standalone BSPs */
    for (i = 0; i < p->bsp_count; i++) {
        if (hxfs_strcasecmp(p->bsps[i].name, name) == 0) return 1;
    }

    /* PK3 archives */
    for (i = p->count - 1; i >= 0; i--) {
        if (zip_contains(p->files[i], name)) return 1;
    }
    return 0;
}

static int hxfs_read_zip(HxfsCod2003* p, const char* name, unsigned char** out_buf, unsigned int* out_size) {
    int i;
    if (!p || !name) return 0;
    for (i = p->count - 1; i >= 0; i--) {
        if (zip_read(p->files[i], name, out_buf, out_size)) return 1;
    }
    return 0;
}

/* xmodel/<name> is decoded to OBJ. Raw PK3 bytes are not returned. */
static int hxfs_read_xmodel(HxfsCod2003* p, const char* name, unsigned char** out_buf, unsigned int* out_size) {
    unsigned char* xm = NULL;
    unsigned char* sf = NULL;
    unsigned char* pt = NULL;
    unsigned int xsz = 0, ssz = 0, psz = 0;
    char lod[128];
    char surf[300];
    char parts[300];
    int ok;

    if (!hxfs_read_zip(p, name, &xm, &xsz)) return 0;
    if (cod_xmodel_is_text(xm, xsz)) {
        ok = cod_xmodel_to_obj(xm, xsz, NULL, 0, NULL, 0, out_buf, out_size);
        free(xm);
        return ok;
    }
    lod[0] = '\0';
    if (!cod_xmodel_lod_name(xm, xsz, lod, sizeof(lod))) {
        free(xm);
        return 0;
    }
    snprintf(surf, sizeof(surf), "xmodelsurfs/%s", lod);
    if (!hxfs_read_zip(p, surf, &sf, &ssz)) {
        free(xm);
        return 0;
    }
    snprintf(parts, sizeof(parts), "xmodelparts/%s", lod);
    hxfs_read_zip(p, parts, &pt, &psz); /* optional bind-pose bones */

    ok = cod_xmodel_to_obj(xm, xsz, sf, ssz, pt, psz, out_buf, out_size);
    if (pt) free(pt);
    free(sf);
    free(xm);
    return ok;
}

static int hxfs_read(hxAssetPlugin* self, const char* name, unsigned char** out_buf, unsigned int* out_size) {
    HxfsCod2003* p = (HxfsCod2003*)self;
    size_t len;
    int i;
    if (!p || !name || !out_buf || !out_size) return 0;
    len = strlen(name);

    /* Static props: xmodel/<name> -> OBJ, assembled from xmodelsurfs/<lod>. */
    if (strncmp(name, "xmodel/", 7) == 0 && name[7] &&
        !strchr(name + 7, '/') && !strchr(name + 7, '\\')) {
        return hxfs_read_xmodel(p, name, out_buf, out_size);
    }

    /* 1. Virtual .hxmap translation via internal CoD1 BSP loader */
    if (len >= 6 && hxfs_strcasecmp(name + len - 6, ".hxmap") == 0) {
        unsigned char* bsp_data = NULL;
        unsigned int bsp_size = 0;
        char mname[128] = {0};
        if (find_bsp_source(p, name, &bsp_data, &bsp_size, mname, sizeof(mname))) {
            int ok = cod_bsp_to_hxmap(bsp_data, bsp_size, mname, out_buf, out_size);
            free(bsp_data);
            if (ok) return 1;
        }
    }

    /* 2. Standalone BSP files */
    for (i = 0; i < p->bsp_count; i++) {
        if (hxfs_strcasecmp(p->bsps[i].name, name) == 0) {
            *out_buf = (unsigned char*)malloc(p->bsps[i].size);
            if (!*out_buf) return 0;
            memcpy(*out_buf, p->bsps[i].data, p->bsps[i].size);
            *out_size = p->bsps[i].size;
            return 1;
        }
    }

    /* 3. Normal PK3 files */
    for (i = p->count - 1; i >= 0; i--) {
        if (zip_read(p->files[i], name, out_buf, out_size)) return 1;
    }
    return 0;
}

static void hxfs_free_buf(hxAssetPlugin* self, unsigned char* buf) {
    (void)self;
    free(buf);
}

static void hxfs_build_cache(HxfsCod2003* p) {
    char** items = NULL;
    unsigned int total = 0, cap = 0;
    int i;
    unsigned int j;

    if (!p || p->cached_list) return;

    #define COLLECT_NAME(s) do { \
        if (total == cap) { \
            cap = cap ? cap * 2 : 1024; \
            items = (char**)realloc(items, cap * sizeof(char*)); \
        } \
        items[total++] = hxfs_strdup(s); \
    } while(0)

    /* Standalone BSPs */
    for (i = 0; i < p->bsp_count; i++) {
        const char* bname;
        COLLECT_NAME(p->bsps[i].name);
        bname = strrchr(p->bsps[i].name, '/');
        if (!bname) bname = strrchr(p->bsps[i].name, '\\');
        bname = bname ? bname + 1 : p->bsps[i].name;

        if (strncmp(bname, "mp_", 3) == 0) {
            size_t blen = strlen(bname);
            if (blen >= 4 && hxfs_strcasecmp(bname + blen - 4, ".bsp") == 0) {
                char hxname[256];
                snprintf(hxname, sizeof(hxname), "maps/%.*s.hxmap", (int)(blen - 4), bname);
                COLLECT_NAME(hxname);
            }
        }
    }

    /* PK3 archives */
    for (i = p->count - 1; i >= 0; i--) {
        char** tnames = NULL;
        unsigned int tcount = 0;
        zip_list(p->files[i], &tnames, &tcount);
        for (j = 0; j < tcount; j++) {
            const char* base;
            size_t nlen;
            COLLECT_NAME(tnames[j]);

            /* Only expose MULTIPLAYER maps (mp_*) as virtual .hxmap */
            base = strrchr(tnames[j], '/');
            if (!base) base = strrchr(tnames[j], '\\');
            base = base ? base + 1 : tnames[j];

            nlen = strlen(base);
            if (nlen >= 7 && strncmp(base, "mp_", 3) == 0 &&
                hxfs_strcasecmp(base + nlen - 4, ".bsp") == 0) {
                char hx[256];
                snprintf(hx, sizeof(hx), "maps/%.*s.hxmap", (int)(nlen - 4), base);
                COLLECT_NAME(hx);
            }
        }
        zip_free_list(tnames, tcount);
    }
    #undef COLLECT_NAME

    if (total > 0) {
        unsigned int unique = 0;
        qsort(items, total, sizeof(char*), cmp_str);
        for (j = 0; j < total; j++) {
            if (j > 0 && strcmp(items[j], items[unique - 1]) == 0) {
                free(items[j]);
            } else {
                items[unique++] = items[j];
            }
        }
        p->cached_list = items;
        p->cached_count = unique;
    }
}

static void hxfs_list(hxAssetPlugin* self, char*** names, unsigned int* count) {
    HxfsCod2003* p = (HxfsCod2003*)self;
    unsigned int j;
    char** out;

    if (!names || !count) return;
    *names = NULL;
    *count = 0;
    if (!p) return;

    hxfs_build_cache(p);

    if (p->cached_count > 0) {
        out = (char**)malloc(p->cached_count * sizeof(char*));
        if (out) {
            for (j = 0; j < p->cached_count; j++) {
                out[j] = hxfs_strdup(p->cached_list[j]);
            }
            *names = out;
            *count = p->cached_count;
        }
    }
}

static void hxfs_free_list(hxAssetPlugin* self, char** names, unsigned int count) {
    unsigned int i;
    (void)self;
    if (!names) return;
    for (i = 0; i < count; i++) free(names[i]);
    free(names);
}

static void hxfs_close(hxAssetPlugin* self) {
    hxfs_close_all((HxfsCod2003*)self);
}

static void hxfs_destroy(hxAssetPlugin* self) {
    HxfsCod2003* p = (HxfsCod2003*)self;
    if (!p) return;
    hxfs_close_all(p);
    free(p);
}

HXFS_EXPORT hxAssetPlugin* helix_asset_plugin_create(void) {
    HxfsCod2003* p = (HxfsCod2003*)calloc(1, sizeof(HxfsCod2003));
    if (!p) return NULL;
    p->api.api_version = HELIX_ASSET_PLUGIN_API_VERSION;
    p->api.format_name = "cod2003";
    p->api.open = hxfs_open;
    p->api.contains = hxfs_contains;
    p->api.read = hxfs_read;
    p->api.free_buf = hxfs_free_buf;
    p->api.list = hxfs_list;
    p->api.free_list = hxfs_free_list;
    p->api.close = hxfs_close;
    p->api.open_many = hxfs_open_many;
    p->api.open_dir = hxfs_open_dir;
    p->api.destroy = hxfs_destroy;
    return &p->api;
}
