/*
 * hxfs_wad — Helix IAssetPlugin for WAD1/WAD2/WAD3 archives.
 * Copyright (C) 2026 Helix-Public contributors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include "iasset_plugin.h"
#include "wad_file.h"

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
    hxAssetPlugin api;
    WadFile** files;
    int count;
    int cap;
} HxfsWad;

static void hxfs_close_all(HxfsWad* p) {
    int i;
    if (!p) return;
    for (i = 0; i < p->count; i++) {
        if (p->files[i]) {
            wad_close(p->files[i]);
        }
    }
    free(p->files);
    p->files = NULL;
    p->count = 0;
    p->cap = 0;
}

static int hxfs_add_file(HxfsWad* p, const char* path) {
    WadFile* next = wad_open_file(path);
    if (!next) return 0;
    if (p->count == p->cap) {
        int ncap = p->cap ? p->cap * 2 : 4;
        WadFile** n = (WadFile**)realloc(p->files, ncap * sizeof(WadFile*));
        if (!n) {
            wad_close(next);
            return 0;
        }
        p->files = n;
        p->cap = ncap;
    }
    p->files[p->count++] = next;
    return 1;
}

static int hxfs_open(hxAssetPlugin* self, const char* path) {
    HxfsWad* p = (HxfsWad*)self;
    if (!p) return 0;
    hxfs_close_all(p);
    if (!path || !path[0]) return 0;
    return hxfs_add_file(p, path);
}

static int hxfs_open_many(hxAssetPlugin* self, const char** paths, int count) {
    HxfsWad* p = (HxfsWad*)self;
    int i, opened = 0;
    if (!p) return 0;
    hxfs_close_all(p);
    if (!paths) return 0;
    for (i = 0; i < count; i++) {
        if (paths[i] && paths[i][0]) {
            if (hxfs_add_file(p, paths[i])) opened++;
        }
    }
    return opened;
}

static int cmp_str(const void* a, const void* b) {
    return strcmp(*(const char**)a, *(const char**)b);
}

static int hxfs_open_dir(hxAssetPlugin* self, const char* dir_path) {
    HxfsWad* p = (HxfsWad*)self;
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
        char search[512];
        snprintf(search, sizeof(search), "%s/*.wad", dir_path);
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
                size_t extlen = strlen(".wad");
                if (len >= extlen && hxfs_strcasecmp(ent->d_name + len - extlen, ".wad") == 0) {
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
            if (hxfs_add_file(p, paths[i])) opened++;
            free(paths[i]);
        }
        free(paths);
    }
    
    return opened;
}

static int hxfs_contains(hxAssetPlugin* self, const char* name) {
    HxfsWad* p = (HxfsWad*)self;
    int i;
    if (!p) return 0;
    for (i = p->count - 1; i >= 0; i--) {
        if (wad_contains(p->files[i], name)) return 1;
    }
    return 0;
}

static int hxfs_read(hxAssetPlugin* self, const char* name, unsigned char** out_buf, unsigned int* out_size) {
    HxfsWad* p = (HxfsWad*)self;
    int i;
    if (!p) return 0;
    for (i = p->count - 1; i >= 0; i--) {
        if (wad_read(p->files[i], name, out_buf, out_size)) return 1;
    }
    return 0;
}

static void hxfs_free_buf(hxAssetPlugin* self, unsigned char* buf) {
    (void)self;
    free(buf);
}

static void hxfs_list(hxAssetPlugin* self, char*** names, unsigned int* count) {
    HxfsWad* p = (HxfsWad*)self;
    int i;
    unsigned int j;
    char** all_names = NULL;
    unsigned int all_count = 0;
    unsigned int all_cap = 0;

    if (!names || !count) return;
    *names = NULL;
    *count = 0;
    if (!p) return;

    for (i = p->count - 1; i >= 0; i--) {
        char** tnames = NULL;
        unsigned int tcount = 0;
        wad_list(p->files[i], &tnames, &tcount);
        for (j = 0; j < tcount; j++) {
            unsigned int k;
            int found = 0;
            for (k = 0; k < all_count; k++) {
                if (hxfs_strcasecmp(all_names[k], tnames[j]) == 0) {
                    found = 1;
                    break;
                }
            }
            if (!found) {
                if (all_count == all_cap) {
                    all_cap = all_cap ? all_cap * 2 : 64;
                    all_names = (char**)realloc(all_names, all_cap * sizeof(char*));
                }
                all_names[all_count++] = hxfs_strdup(tnames[j]);
            }
        }
        wad_free_list(tnames, tcount);
    }
    
    *names = all_names;
    *count = all_count;
}

static void hxfs_free_list(hxAssetPlugin* self, char** names, unsigned int count) {
    unsigned int i;
    (void)self;
    if (!names) return;
    for (i = 0; i < count; i++) free(names[i]);
    free(names);
}

static void hxfs_close(hxAssetPlugin* self) {
    hxfs_close_all((HxfsWad*)self);
}

static void hxfs_destroy(hxAssetPlugin* self) {
    HxfsWad* p = (HxfsWad*)self;
    if (!p) return;
    hxfs_close_all(p);
    free(p);
}

HXFS_EXPORT hxAssetPlugin* helix_asset_plugin_create(void) {
    HxfsWad* p = (HxfsWad*)calloc(1, sizeof(HxfsWad));
    if (!p) return NULL;
    p->api.api_version = HELIX_ASSET_PLUGIN_API_VERSION;
    p->api.format_name = "wad";
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
